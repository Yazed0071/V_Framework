#include "pch.h"
#include "EquipMotionData.h"

#include <Windows.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using ReadMotionDataTable_t = void(__fastcall*)(void* thisPtr, lua_State* L, int idx, const char* fieldName);

    struct MotionEntry
    {
        std::int32_t equipId = 0;
        std::string mtarPath;
    };

    EquipMotionData::Deps g_Deps{};
    ReadMotionDataTable_t g_OrigReadMotionDataTable = nullptr;
    bool g_ReadMotionDataTableHookInstalled = false;

    std::vector<MotionEntry> g_CustomMotionEntries;
    std::unordered_map<std::int32_t, std::string> g_CustomMotionPathMap;
    std::mutex g_CustomMotionMutex;

    constexpr int LUA_TNUMBER_CONST = 3;
    constexpr int LUA_TSTRING_CONST = 4;
    constexpr int LUA_TTABLE_CONST = 5;

    // Stock ReloadEquipMotionData clears about 205 qword entries total.
    constexpr int STOCK_MOTION_ENTRY_COUNT = 205;
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
            g_Deps.GetLuaString &&
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

    static bool LuaIsString(lua_State* L, int idx)
    {
        return g_Deps.LuaType(L, idx) == LUA_TSTRING_CONST;
    }

    static void PopOne(lua_State* L)
    {
        g_Deps.LuaSetTop(L, -2);
    }

    static int NormalizeMotionEquipIndex(std::int32_t equipId)
    {
        int idx = equipId;
        if (idx > 0x3FF)
            idx -= 0x383; // 899

        return idx;
    }

    static bool IsSafeStockMotionEquipId(std::int32_t equipId)
    {
        const int idx = NormalizeMotionEquipIndex(equipId);
        return idx >= 0 && idx < STOCK_MOTION_ENTRY_COUNT;
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

    static bool ReadArrayStringField(lua_State* L, int rowIndex, int fieldIndex1Based, std::string& outValue)
    {
        outValue.clear();

        const int rowAbsIndex = AbsIndex(L, rowIndex);

        g_Deps.PushLuaNumber(L, static_cast<float>(fieldIndex1Based));
        g_Deps.LuaGetTable(L, rowAbsIndex);

        const bool ok = LuaIsString(L, -1);
        if (ok)
        {
            const char* value = g_Deps.GetLuaString(L, -1);
            outValue = value ? value : "";
        }

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

    static void WriteArrayStringField(lua_State* L, int rowIndex, int fieldIndex1Based, const std::string& value)
    {
        const int rowAbsIndex = AbsIndex(L, rowIndex);

        g_Deps.PushLuaNumber(L, static_cast<float>(fieldIndex1Based));
        g_Deps.LuaPushString(L, value.c_str());
        g_Deps.LuaSetTable(L, rowAbsIndex);
    }

    static bool IsValidMotionEntry(const MotionEntry& entry)
    {
        return entry.equipId > 0 && !entry.mtarPath.empty();
    }

    static void StoreCustomMotionPath(const MotionEntry& entry)
    {
        if (!IsValidMotionEntry(entry))
            return;

        std::lock_guard<std::mutex> lock(g_CustomMotionMutex);
        g_CustomMotionPathMap[entry.equipId] = entry.mtarPath;

        Log("[EquipMotionData] Stored custom motion entry equipId=0x%X path=%s\n",
            entry.equipId, entry.mtarPath.c_str());
    }

    static void QueueMotionEntry(const MotionEntry& entry)
    {
        if (!IsValidMotionEntry(entry))
            return;

        std::lock_guard<std::mutex> lock(g_CustomMotionMutex);

        auto it = std::find_if(
            g_CustomMotionEntries.begin(),
            g_CustomMotionEntries.end(),
            [&](const MotionEntry& existing)
            {
                return existing.equipId == entry.equipId;
            });

        if (it != g_CustomMotionEntries.end())
        {
            *it = entry;
            Log("[EquipMotionData] Updated queued entry equipId=0x%X path=%s\n",
                entry.equipId, entry.mtarPath.c_str());
            return;
        }

        g_CustomMotionEntries.push_back(entry);
        Log("[EquipMotionData] Queued entry equipId=0x%X path=%s\n",
            entry.equipId, entry.mtarPath.c_str());
    }

    static bool RemoveMotionEntry(std::int32_t equipId)
    {
        std::lock_guard<std::mutex> lock(g_CustomMotionMutex);

        const auto oldSize = g_CustomMotionEntries.size();

        g_CustomMotionEntries.erase(
            std::remove_if(
                g_CustomMotionEntries.begin(),
                g_CustomMotionEntries.end(),
                [&](const MotionEntry& entry)
                {
                    return entry.equipId == equipId;
                }),
            g_CustomMotionEntries.end());

        g_CustomMotionPathMap.erase(equipId);

        const bool removed = g_CustomMotionEntries.size() != oldSize;
        if (removed)
        {
            Log("[EquipMotionData] Removed queued entry equipId=0x%X\n", equipId);
        }

        return removed;
    }

    static void ClearMotionEntries()
    {
        std::lock_guard<std::mutex> lock(g_CustomMotionMutex);
        g_CustomMotionEntries.clear();
        g_CustomMotionPathMap.clear();
        Log("[EquipMotionData] Cleared all queued/custom entries\n");
    }

    static bool ReadMotionEntryRow(lua_State* L, int rowIndex, MotionEntry& outEntry)
    {
        outEntry = {};

        if (!LuaIsTable(L, rowIndex))
            return false;

        if (!ReadArrayIntField(L, rowIndex, 1, outEntry.equipId))
            return false;

        if (!ReadArrayStringField(L, rowIndex, 2, outEntry.mtarPath))
            return false;

        return IsValidMotionEntry(outEntry);
    }

    static void UpsertMotionEntryInTable(lua_State* L, int tableIndex, const MotionEntry& entry)
    {
        const int tableAbsIndex = AbsIndex(L, tableIndex);
        int rowCount = static_cast<int>(g_Deps.LuaObjLen(L, tableAbsIndex));

        for (int i = 1; i <= rowCount; ++i)
        {
            g_Deps.LuaRawGetI(L, tableAbsIndex, i);

            if (LuaIsTable(L, -1))
            {
                const int rowIndex = g_Deps.GetLuaTop(L);
                std::int32_t existingEquipId = 0;

                if (ReadArrayIntField(L, rowIndex, 1, existingEquipId) && existingEquipId == entry.equipId)
                {
                    WriteArrayIntField(L, rowIndex, 1, entry.equipId);
                    WriteArrayStringField(L, rowIndex, 2, entry.mtarPath);
                    PopOne(L);
                    return;
                }
            }

            PopOne(L);
        }

        g_Deps.LuaCreateTable(L, 2, 0);
        const int rowIndex = g_Deps.GetLuaTop(L);

        WriteArrayIntField(L, rowIndex, 1, entry.equipId);
        WriteArrayStringField(L, rowIndex, 2, entry.mtarPath);

        ++rowCount;
        g_Deps.PushLuaNumber(L, static_cast<float>(rowCount));
        g_Deps.LuaPushValue(L, rowIndex);
        g_Deps.LuaSetTable(L, tableAbsIndex);

        PopOne(L);
    }

    static void BuildSanitizedMotionDataTable(lua_State* L, int parentTableIndex)
    {
        const int entryTop = g_Deps.GetLuaTop(L);
        const int parentAbsIndex = AbsIndex(L, parentTableIndex);

        g_Deps.LuaGetField(L, parentAbsIndex, "MotionDataTable");
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
                    MotionEntry entry{};
                    if (ReadMotionEntryRow(L, -1, entry))
                    {
                        if (IsSafeStockMotionEquipId(entry.equipId))
                        {
                            UpsertMotionEntryInTable(L, tempIndex, entry);
                        }
                        else
                        {
                            StoreCustomMotionPath(entry);

                            Log("[EquipMotionData] Filtered unsafe stock/custom row equipId=0x%X path=%s\n",
                                entry.equipId, entry.mtarPath.c_str());
                        }
                    }
                }

                PopOne(L);
            }
        }

        std::vector<MotionEntry> queuedSnapshot;
        {
            std::lock_guard<std::mutex> lock(g_CustomMotionMutex);
            queuedSnapshot = g_CustomMotionEntries;
        }

        for (const MotionEntry& entry : queuedSnapshot)
        {
            if (!IsValidMotionEntry(entry))
                continue;

            if (IsSafeStockMotionEquipId(entry.equipId))
            {
                UpsertMotionEntryInTable(L, tempIndex, entry);

                Log("[EquipMotionData] Added safe queued motion row equipId=0x%X path=%s\n",
                    entry.equipId, entry.mtarPath.c_str());
            }
            else
            {
                StoreCustomMotionPath(entry);

                Log("[EquipMotionData] Filtered unsafe queued motion row equipId=0x%X path=%s\n",
                    entry.equipId, entry.mtarPath.c_str());
            }
        }

        g_Deps.LuaPushString(L, "MotionDataTable");
        g_Deps.LuaPushValue(L, tempIndex);
        g_Deps.LuaSetTable(L, parentAbsIndex);

        g_Deps.LuaSetTop(L, entryTop);
    }

    static void __fastcall hkReadMotionDataTable(void* thisPtr, lua_State* L, int parentIndex, const char* fieldName)
    {
        if (!g_OrigReadMotionDataTable)
            return;

        if (!L || !fieldName || std::strcmp(fieldName, "MotionDataTable") != 0)
        {
            g_OrigReadMotionDataTable(thisPtr, L, parentIndex, fieldName);
            return;
        }

        if (!EnsureLuaReady() || !LuaIsTable(L, parentIndex))
        {
            g_OrigReadMotionDataTable(thisPtr, L, parentIndex, fieldName);
            return;
        }

        const int entryTop = g_Deps.GetLuaTop(L);
        const int parentAbsIndex = AbsIndex(L, parentIndex);

        BuildSanitizedMotionDataTable(L, parentAbsIndex);
        g_Deps.LuaSetTop(L, entryTop);

        g_OrigReadMotionDataTable(thisPtr, L, parentAbsIndex, fieldName);
    }
}

namespace EquipMotionData
{
    void Bind(const Deps& deps)
    {
        g_Deps = deps;
    }

    int __cdecl Lua_AddEquipMotionDataEntry(lua_State* L)
    {
        if (!L || !EnsureLuaReady())
            return 0;

        if (!LuaIsNumber(L, 1) || !LuaIsString(L, 2))
            return 0;

        MotionEntry entry{};
        entry.equipId = static_cast<std::int32_t>(g_Deps.GetLuaInt(L, 1));

        const char* rawPath = g_Deps.GetLuaString(L, 2);
        entry.mtarPath = rawPath ? rawPath : "";

        QueueMotionEntry(entry);
        return 0;
    }

    int __cdecl Lua_RemoveEquipMotionDataEntry(lua_State* L)
    {
        if (!L || !EnsureLuaReady())
            return 0;

        if (!LuaIsNumber(L, 1))
            return 0;

        const std::int32_t equipId = static_cast<std::int32_t>(g_Deps.GetLuaInt(L, 1));
        RemoveMotionEntry(equipId);
        return 0;
    }

    int __cdecl Lua_ClearEquipMotionDataEntries(lua_State* L)
    {
        UNREFERENCED_PARAMETER(L);
        ClearMotionEntries();
        return 0;
    }

    int __cdecl Lua_AddEquipMotionDataTable(lua_State* L)
    {
        if (!L || !EnsureLuaReady() || !LuaIsTable(L, 1))
            return 0;

        const int rowCount = static_cast<int>(g_Deps.LuaObjLen(L, 1));

        for (int i = 1; i <= rowCount; ++i)
        {
            g_Deps.LuaRawGetI(L, 1, i);

            if (LuaIsTable(L, -1))
            {
                const int rowIndex = g_Deps.GetLuaTop(L);

                MotionEntry entry{};
                if (ReadMotionEntryRow(L, rowIndex, entry))
                {
                    QueueMotionEntry(entry);
                }
                else
                {
                    Log("[EquipMotionData] Ignored invalid row at index=%d\n", i);
                }
            }

            PopOne(L);
        }

        return 0;
    }

    bool Install_EquipMotionDataTableImpl_ReadMotionDataTable_Hook()
    {
        if (g_ReadMotionDataTableHookInstalled)
        {
            Log("[EquipMotionData] ReadMotionDataTable hook already installed\n");
            return true;
        }

        void* target = ResolveGameAddress(gAddr.EquipMotionDataTableImpl_ReadMotionDataTable);
        if (!target)
        {
            Log("[EquipMotionData] Failed to resolve ReadMotionDataTable target\n");
            return false;
        }

        const bool ok = CreateAndEnableHook(
            target,
            reinterpret_cast<void*>(&hkReadMotionDataTable),
            reinterpret_cast<void**>(&g_OrigReadMotionDataTable));

        if (!ok)
        {
            Log("[EquipMotionData] Failed to install ReadMotionDataTable hook\n");
            return false;
        }

        g_ReadMotionDataTableHookInstalled = true;
        Log("[EquipMotionData] ReadMotionDataTable hook installed\n");
        return true;
    }

    bool Uninstall_EquipMotionDataTableImpl_ReadMotionDataTable_Hook()
    {
        if (!g_ReadMotionDataTableHookInstalled)
            return true;

        if (void* target = ResolveGameAddress(gAddr.EquipMotionDataTableImpl_ReadMotionDataTable))
        {
            DisableAndRemoveHook(target);
        }

        g_OrigReadMotionDataTable = nullptr;
        g_ReadMotionDataTableHookInstalled = false;
        return true;
    }

    // Optional compatibility wrappers if you do not want to rename existing init code yet.
    bool Install_EquipMotionDataTableImpl_ReloadEquipMotionData_Hook()
    {
        return Install_EquipMotionDataTableImpl_ReadMotionDataTable_Hook();
    }

    bool Uninstall_EquipMotionDataTableImpl_ReloadEquipMotionData_Hook()
    {
        return Uninstall_EquipMotionDataTableImpl_ReadMotionDataTable_Hook();
    }
}