#include "pch.h"
#include "EquipMotionAssignments.h"

#include <Windows.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using ReadMotionDataTable2_t = void(__fastcall*)(void* thisPtr, lua_State* L, int idx, const char* fieldName);

    struct MotionAssignmentEntry
    {
        std::int32_t receiverId = 0;

        std::uint8_t a1 = 0;
        std::uint8_t a2 = 0;
        std::uint8_t a3 = 0;
        std::uint8_t a4 = 0;

        std::uint8_t b1 = 0;
        std::uint8_t b2 = 0;
        std::uint8_t b3 = 0;
        std::uint8_t b4 = 0;

        std::uint8_t mtarIndex = 0;
        bool flag1 = false;
        bool flag2 = false;
    };

    EquipMotionAssignments::Deps g_Deps{};
    ReadMotionDataTable2_t g_OrigReadMotionDataTable2 = nullptr;
    bool g_ReadMotionDataTable2HookInstalled = false;

    std::vector<MotionAssignmentEntry> g_CustomAssignments;
    std::mutex g_CustomAssignmentsMutex;

    constexpr int LUA_TNUMBER_CONST = 3;
    constexpr int LUA_TTABLE_CONST = 5;
}

namespace
{
    static bool ValidateDeps()
    {
        return
            g_Deps.ResolveLuaApi &&
            g_Deps.GetLuaTop &&
            g_Deps.LuaType &&
            g_Deps.GetLuaInt &&
            g_Deps.LuaObjLen &&
            g_Deps.LuaSetTop &&
            g_Deps.PushLuaNumber &&
            g_Deps.LuaPushString &&
            g_Deps.LuaCreateTable &&
            g_Deps.LuaGetField &&
            g_Deps.LuaRawGetI &&
            g_Deps.LuaGetTable &&
            g_Deps.LuaSetTable &&
            g_Deps.LuaPushValue;
    }

    static bool EnsureLuaReady()
    {
        return ValidateDeps() && g_Deps.ResolveLuaApi && g_Deps.ResolveLuaApi();
    }

    static int AbsIndex(lua_State* L, int idx)
    {
        if (idx > 0)
            return idx;

        return g_Deps.GetLuaTop(L) + idx + 1;
    }

    static bool LuaIsTable(lua_State* L, int idx)
    {
        return g_Deps.LuaType(L, idx) == LUA_TTABLE_CONST;
    }

    static bool LuaIsNumber(lua_State* L, int idx)
    {
        return g_Deps.LuaType(L, idx) == LUA_TNUMBER_CONST;
    }

    static void PopOne(lua_State* L)
    {
        g_Deps.LuaSetTop(L, -2);
    }

    static bool ReadArrayIntField(lua_State* L, int rowIndex, int fieldIndex1Based, std::int32_t& outValue)
    {
        outValue = 0;

        const int rowAbsIndex = AbsIndex(L, rowIndex);

        g_Deps.PushLuaNumber(L, static_cast<float>(fieldIndex1Based));
        g_Deps.LuaGetTable(L, rowAbsIndex);

        const bool ok = LuaIsNumber(L, -1);
        if (ok)
            outValue = static_cast<std::int32_t>(g_Deps.GetLuaInt(L, -1));

        PopOne(L);
        return ok;
    }

    static void WriteArrayIntField(lua_State* L, int rowIndex, int fieldIndex1Based, std::int32_t value)
    {
        const int rowAbsIndex = AbsIndex(L, rowIndex);

        g_Deps.PushLuaNumber(L, static_cast<float>(fieldIndex1Based));
        g_Deps.PushLuaNumber(L, static_cast<float>(value));
        g_Deps.LuaSetTable(L, rowAbsIndex);
    }

    static bool IsValidMotionAssignmentEntry(const MotionAssignmentEntry& entry)
    {
        return entry.receiverId > 0;
    }

    static bool ReadMotionAssignmentRow(lua_State* L, int rowIndex, MotionAssignmentEntry& outEntry)
    {
        outEntry = {};

        if (!LuaIsTable(L, rowIndex))
            return false;

        std::int32_t temp = 0;

        if (!ReadArrayIntField(L, rowIndex, 1, outEntry.receiverId))
            return false;

        if (ReadArrayIntField(L, rowIndex, 2, temp))  outEntry.a1 = static_cast<std::uint8_t>(temp);
        if (ReadArrayIntField(L, rowIndex, 3, temp))  outEntry.a2 = static_cast<std::uint8_t>(temp);
        if (ReadArrayIntField(L, rowIndex, 4, temp))  outEntry.a3 = static_cast<std::uint8_t>(temp);
        if (ReadArrayIntField(L, rowIndex, 5, temp))  outEntry.a4 = static_cast<std::uint8_t>(temp);

        if (ReadArrayIntField(L, rowIndex, 6, temp))  outEntry.b1 = static_cast<std::uint8_t>(temp);
        if (ReadArrayIntField(L, rowIndex, 7, temp))  outEntry.b2 = static_cast<std::uint8_t>(temp);
        if (ReadArrayIntField(L, rowIndex, 8, temp))  outEntry.b3 = static_cast<std::uint8_t>(temp);
        if (ReadArrayIntField(L, rowIndex, 9, temp))  outEntry.b4 = static_cast<std::uint8_t>(temp);

        if (ReadArrayIntField(L, rowIndex, 10, temp)) outEntry.mtarIndex = static_cast<std::uint8_t>(temp);
        if (ReadArrayIntField(L, rowIndex, 11, temp)) outEntry.flag1 = (temp != 0);
        if (ReadArrayIntField(L, rowIndex, 12, temp)) outEntry.flag2 = (temp != 0);

        return IsValidMotionAssignmentEntry(outEntry);
    }

    static void QueueMotionAssignmentEntry(const MotionAssignmentEntry& entry)
    {
        if (!IsValidMotionAssignmentEntry(entry))
            return;

        std::lock_guard<std::mutex> lock(g_CustomAssignmentsMutex);

        auto it = std::find_if(
            g_CustomAssignments.begin(),
            g_CustomAssignments.end(),
            [&](const MotionAssignmentEntry& existing)
            {
                return existing.receiverId == entry.receiverId;
            });

        if (it != g_CustomAssignments.end())
        {
            *it = entry;

            Log("[EquipMotionAssignments] Updated receiverId=0x%X {%u,%u,%u,%u,%u,%u,%u,%u,%u,%d,%d}\n",
                entry.receiverId,
                entry.a1, entry.a2, entry.a3, entry.a4,
                entry.b1, entry.b2, entry.b3, entry.b4,
                entry.mtarIndex,
                entry.flag1 ? 1 : 0,
                entry.flag2 ? 1 : 0);
            return;
        }

        g_CustomAssignments.push_back(entry);

        Log("[EquipMotionAssignments] Queued receiverId=0x%X {%u,%u,%u,%u,%u,%u,%u,%u,%u,%d,%d}\n",
            entry.receiverId,
            entry.a1, entry.a2, entry.a3, entry.a4,
            entry.b1, entry.b2, entry.b3, entry.b4,
            entry.mtarIndex,
            entry.flag1 ? 1 : 0,
            entry.flag2 ? 1 : 0);
    }

    static void ClearMotionAssignments()
    {
        std::lock_guard<std::mutex> lock(g_CustomAssignmentsMutex);
        g_CustomAssignments.clear();
        Log("[EquipMotionAssignments] Cleared all assignments\n");
    }

    static void UpsertMotionAssignmentInTable(lua_State* L, int tableIndex, const MotionAssignmentEntry& entry)
    {
        const int tableAbsIndex = AbsIndex(L, tableIndex);
        int rowCount = static_cast<int>(g_Deps.LuaObjLen(L, tableAbsIndex));

        for (int i = 1; i <= rowCount; ++i)
        {
            g_Deps.LuaRawGetI(L, tableAbsIndex, i);

            if (LuaIsTable(L, -1))
            {
                const int rowIndex = g_Deps.GetLuaTop(L);
                std::int32_t existingReceiverId = 0;

                if (ReadArrayIntField(L, rowIndex, 1, existingReceiverId) &&
                    existingReceiverId == entry.receiverId)
                {
                    WriteArrayIntField(L, rowIndex, 1, entry.receiverId);
                    WriteArrayIntField(L, rowIndex, 2, entry.a1);
                    WriteArrayIntField(L, rowIndex, 3, entry.a2);
                    WriteArrayIntField(L, rowIndex, 4, entry.a3);
                    WriteArrayIntField(L, rowIndex, 5, entry.a4);
                    WriteArrayIntField(L, rowIndex, 6, entry.b1);
                    WriteArrayIntField(L, rowIndex, 7, entry.b2);
                    WriteArrayIntField(L, rowIndex, 8, entry.b3);
                    WriteArrayIntField(L, rowIndex, 9, entry.b4);
                    WriteArrayIntField(L, rowIndex, 10, entry.mtarIndex);
                    WriteArrayIntField(L, rowIndex, 11, entry.flag1 ? 1 : 0);
                    WriteArrayIntField(L, rowIndex, 12, entry.flag2 ? 1 : 0);

                    PopOne(L);
                    return;
                }
            }

            PopOne(L);
        }

        g_Deps.LuaCreateTable(L, 12, 0);
        const int rowIndex = g_Deps.GetLuaTop(L);

        WriteArrayIntField(L, rowIndex, 1, entry.receiverId);
        WriteArrayIntField(L, rowIndex, 2, entry.a1);
        WriteArrayIntField(L, rowIndex, 3, entry.a2);
        WriteArrayIntField(L, rowIndex, 4, entry.a3);
        WriteArrayIntField(L, rowIndex, 5, entry.a4);
        WriteArrayIntField(L, rowIndex, 6, entry.b1);
        WriteArrayIntField(L, rowIndex, 7, entry.b2);
        WriteArrayIntField(L, rowIndex, 8, entry.b3);
        WriteArrayIntField(L, rowIndex, 9, entry.b4);
        WriteArrayIntField(L, rowIndex, 10, entry.mtarIndex);
        WriteArrayIntField(L, rowIndex, 11, entry.flag1 ? 1 : 0);
        WriteArrayIntField(L, rowIndex, 12, entry.flag2 ? 1 : 0);

        ++rowCount;
        g_Deps.PushLuaNumber(L, static_cast<float>(rowCount));
        g_Deps.LuaPushValue(L, rowIndex);
        g_Deps.LuaSetTable(L, tableAbsIndex);

        PopOne(L);
    }

    static void BuildSanitizedAssignmentsTable(lua_State* L, int parentTableIndex)
    {
        const int entryTop = g_Deps.GetLuaTop(L);
        const int parentAbsIndex = AbsIndex(L, parentTableIndex);

        g_Deps.LuaGetField(L, parentAbsIndex, "assignments");
        const int originalIndex = g_Deps.GetLuaTop(L);

        g_Deps.LuaCreateTable(L, 0, 0);
        const int tempIndex = g_Deps.GetLuaTop(L);

        if (LuaIsTable(L, originalIndex))
        {
            const int rowCount = static_cast<int>(g_Deps.LuaObjLen(L, originalIndex));

            for (int i = 1; i <= rowCount; ++i)
            {
                g_Deps.LuaRawGetI(L, originalIndex, i);

                if (LuaIsTable(L, -1))
                {
                    MotionAssignmentEntry entry{};
                    if (ReadMotionAssignmentRow(L, -1, entry))
                    {
                        UpsertMotionAssignmentInTable(L, tempIndex, entry);
                    }
                }

                PopOne(L);
            }
        }

        std::vector<MotionAssignmentEntry> queuedSnapshot;
        {
            std::lock_guard<std::mutex> lock(g_CustomAssignmentsMutex);
            queuedSnapshot = g_CustomAssignments;
        }

        for (const auto& entry : queuedSnapshot)
        {
            if (!IsValidMotionAssignmentEntry(entry))
                continue;

            UpsertMotionAssignmentInTable(L, tempIndex, entry);

            Log("[EquipMotionAssignments] Added custom assignment receiverId=0x%X\n",
                entry.receiverId);
        }

        g_Deps.LuaPushString(L, "assignments");
        g_Deps.LuaPushValue(L, tempIndex);
        g_Deps.LuaSetTable(L, parentAbsIndex);

        g_Deps.LuaSetTop(L, entryTop);
    }

    static void __fastcall hkReadMotionDataTable2(void* thisPtr, lua_State* L, int parentIndex, const char* fieldName)
    {
        if (!g_OrigReadMotionDataTable2)
            return;

        if (!L || !fieldName || std::strcmp(fieldName, "assignments") != 0)
        {
            g_OrigReadMotionDataTable2(thisPtr, L, parentIndex, fieldName);
            return;
        }

        if (!EnsureLuaReady() || !LuaIsTable(L, parentIndex))
        {
            g_OrigReadMotionDataTable2(thisPtr, L, parentIndex, fieldName);
            return;
        }

        const int entryTop = g_Deps.GetLuaTop(L);
        const int parentAbsIndex = AbsIndex(L, parentIndex);

        BuildSanitizedAssignmentsTable(L, parentAbsIndex);
        g_Deps.LuaSetTop(L, entryTop);

        g_OrigReadMotionDataTable2(thisPtr, L, parentAbsIndex, fieldName);
    }
}

namespace EquipMotionAssignments
{
    void Bind(const Deps& deps)
    {
        g_Deps = deps;
    }

    int __cdecl Lua_AddEquipMotionAssignment(lua_State* L)
    {
        if (!L || !EnsureLuaReady() || !LuaIsTable(L, 1))
            return 0;

        MotionAssignmentEntry entry{};

        auto readByteField = [&](const char* name, std::uint8_t& outValue)
            {
                g_Deps.LuaGetField(L, 1, name);
                outValue = LuaIsNumber(L, -1)
                    ? static_cast<std::uint8_t>(g_Deps.GetLuaInt(L, -1))
                    : 0;
                PopOne(L);
            };

        auto readBoolField = [&](const char* name, bool& outValue)
            {
                g_Deps.LuaGetField(L, 1, name);
                outValue = LuaIsNumber(L, -1)
                    ? (g_Deps.GetLuaInt(L, -1) != 0)
                    : false;
                PopOne(L);
            };

        g_Deps.LuaGetField(L, 1, "receiverId");
        if (!LuaIsNumber(L, -1))
        {
            PopOne(L);
            Log("[EquipMotionAssignments] receiverId is required\n");
            return 0;
        }
        entry.receiverId = static_cast<std::int32_t>(g_Deps.GetLuaInt(L, -1));
        PopOne(L);

        readByteField("a1", entry.a1);
        readByteField("a2", entry.a2);
        readByteField("a3", entry.a3);
        readByteField("a4", entry.a4);
        readByteField("b1", entry.b1);
        readByteField("b2", entry.b2);
        readByteField("b3", entry.b3);
        readByteField("b4", entry.b4);
        readByteField("mtarIndex", entry.mtarIndex);
        readBoolField("flag1", entry.flag1);
        readBoolField("flag2", entry.flag2);

        QueueMotionAssignmentEntry(entry);
        return 0;
    }

    int __cdecl Lua_ClearEquipMotionAssignments(lua_State* L)
    {
        UNREFERENCED_PARAMETER(L);
        ClearMotionAssignments();
        return 0;
    }

    bool Install_EquipMotionDataTableImpl_ReadMotionDataTable2_Hook()
    {
        if (g_ReadMotionDataTable2HookInstalled)
        {
            Log("[EquipMotionAssignments] ReadMotionDataTable2 hook already installed\n");
            return true;
        }

        void* target = ResolveGameAddress(gAddr.EquipMotionDataTableImpl_ReadMotionDataTable2);
        if (!target)
        {
            Log("[EquipMotionAssignments] Failed to resolve ReadMotionDataTable2 target\n");
            return false;
        }

        const bool ok = CreateAndEnableHook(
            target,
            reinterpret_cast<void*>(&hkReadMotionDataTable2),
            reinterpret_cast<void**>(&g_OrigReadMotionDataTable2));

        if (!ok)
        {
            Log("[EquipMotionAssignments] Failed to install ReadMotionDataTable2 hook\n");
            return false;
        }

        g_ReadMotionDataTable2HookInstalled = true;
        Log("[EquipMotionAssignments] ReadMotionDataTable2 hook installed\n");
        return true;
    }

    bool Uninstall_EquipMotionDataTableImpl_ReadMotionDataTable2_Hook()
    {
        if (!g_ReadMotionDataTable2HookInstalled)
            return true;

        if (void* target = ResolveGameAddress(gAddr.EquipMotionDataTableImpl_ReadMotionDataTable2))
        {
            DisableAndRemoveHook(target);
        }

        g_OrigReadMotionDataTable2 = nullptr;
        g_ReadMotionDataTable2HookInstalled = false;
        return true;
    }
}