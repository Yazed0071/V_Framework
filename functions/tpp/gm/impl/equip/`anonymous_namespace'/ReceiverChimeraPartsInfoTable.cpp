#include "pch.h"
#include "ReceiverChimeraPartsInfoTable.h"

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
    using ReadPartsPackageInfoIndexTable_t =
        void(__fastcall*)(lua_State* L, int idx, const char* fieldName, void* outIndexTable);

    struct ReceiverPackageEntry
    {
        std::int32_t receiverId = 0;
        std::int32_t packageId = 0;
    };

    ReceiverChimeraPartsInfoTable::Deps g_Deps{};
    ReadPartsPackageInfoIndexTable_t g_OrigReadPartsPackageInfoIndexTable = nullptr;
    bool g_ReadPartsPackageInfoIndexTableHookInstalled = false;

    std::vector<ReceiverPackageEntry> g_CustomReceiverPackageEntries;
    std::mutex g_CustomReceiverPackageMutex;

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

    static bool IsValidReceiverPackageEntry(const ReceiverPackageEntry& entry)
    {
        return entry.receiverId > 0 && entry.packageId >= 0;
    }

    static bool ReadReceiverPackageRow(lua_State* L, int rowIndex, ReceiverPackageEntry& outEntry)
    {
        outEntry = {};

        if (!LuaIsTable(L, rowIndex))
            return false;

        if (!ReadArrayIntField(L, rowIndex, 1, outEntry.receiverId))
            return false;

        if (!ReadArrayIntField(L, rowIndex, 2, outEntry.packageId))
            return false;

        return IsValidReceiverPackageEntry(outEntry);
    }

    static void QueueReceiverPackageEntry(const ReceiverPackageEntry& entry)
    {
        if (!IsValidReceiverPackageEntry(entry))
            return;

        std::lock_guard<std::mutex> lock(g_CustomReceiverPackageMutex);

        auto it = std::find_if(
            g_CustomReceiverPackageEntries.begin(),
            g_CustomReceiverPackageEntries.end(),
            [&](const ReceiverPackageEntry& existing)
            {
                return existing.receiverId == entry.receiverId;
            });

        if (it != g_CustomReceiverPackageEntries.end())
        {
            *it = entry;
            Log("[ReceiverChimeraPartsInfoTable] Updated receiverId=0x%X packageId=%d\n",
                entry.receiverId, entry.packageId);
            return;
        }

        g_CustomReceiverPackageEntries.push_back(entry);
        Log("[ReceiverChimeraPartsInfoTable] Queued receiverId=0x%X packageId=%d\n",
            entry.receiverId, entry.packageId);
    }

    static void ClearReceiverPackageEntries()
    {
        std::lock_guard<std::mutex> lock(g_CustomReceiverPackageMutex);
        g_CustomReceiverPackageEntries.clear();
        Log("[ReceiverChimeraPartsInfoTable] Cleared all entries\n");
    }

    static void UpsertReceiverPackageInTable(lua_State* L, int tableIndex, const ReceiverPackageEntry& entry)
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
                    WriteArrayIntField(L, rowIndex, 2, entry.packageId);
                    PopOne(L);
                    return;
                }
            }

            PopOne(L);
        }

        g_Deps.LuaCreateTable(L, 2, 0);
        const int rowIndex = g_Deps.GetLuaTop(L);

        WriteArrayIntField(L, rowIndex, 1, entry.receiverId);
        WriteArrayIntField(L, rowIndex, 2, entry.packageId);

        ++rowCount;
        g_Deps.PushLuaNumber(L, static_cast<float>(rowCount));
        g_Deps.LuaPushValue(L, rowIndex);
        g_Deps.LuaSetTable(L, tableAbsIndex);

        PopOne(L);
    }

    static void BuildSanitizedReceiverTable(lua_State* L, int parentTableIndex)
    {
        const int entryTop = g_Deps.GetLuaTop(L);
        const int parentAbsIndex = AbsIndex(L, parentTableIndex);

        g_Deps.LuaGetField(L, parentAbsIndex, "receiver");
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
                    ReceiverPackageEntry entry{};
                    if (ReadReceiverPackageRow(L, -1, entry))
                    {
                        UpsertReceiverPackageInTable(L, tempIndex, entry);
                    }
                }

                PopOne(L);
            }
        }

        std::vector<ReceiverPackageEntry> queuedSnapshot;
        {
            std::lock_guard<std::mutex> lock(g_CustomReceiverPackageMutex);
            queuedSnapshot = g_CustomReceiverPackageEntries;
        }

        for (const auto& entry : queuedSnapshot)
        {
            if (!IsValidReceiverPackageEntry(entry))
                continue;

            UpsertReceiverPackageInTable(L, tempIndex, entry);

            Log("[ReceiverChimeraPartsInfoTable] Added custom receiver row receiverId=0x%X packageId=%d\n",
                entry.receiverId, entry.packageId);
        }

        g_Deps.LuaPushString(L, "receiver");
        g_Deps.LuaPushValue(L, tempIndex);
        g_Deps.LuaSetTable(L, parentAbsIndex);

        g_Deps.LuaSetTop(L, entryTop);
    }

    static void __fastcall hkReadPartsPackageInfoIndexTable(
        lua_State* L,
        int parentIndex,
        const char* fieldName,
        void* outIndexTable)
    {
        if (!g_OrigReadPartsPackageInfoIndexTable)
            return;

        if (!L || !fieldName || std::strcmp(fieldName, "receiver") != 0)
        {
            g_OrigReadPartsPackageInfoIndexTable(L, parentIndex, fieldName, outIndexTable);
            return;
        }

        if (!EnsureLuaReady() || !LuaIsTable(L, parentIndex))
        {
            g_OrigReadPartsPackageInfoIndexTable(L, parentIndex, fieldName, outIndexTable);
            return;
        }

        const int entryTop = g_Deps.GetLuaTop(L);
        const int parentAbsIndex = AbsIndex(L, parentIndex);

        BuildSanitizedReceiverTable(L, parentAbsIndex);
        g_Deps.LuaSetTop(L, entryTop);

        g_OrigReadPartsPackageInfoIndexTable(L, parentAbsIndex, fieldName, outIndexTable);
    }
}

namespace ReceiverChimeraPartsInfoTable
{
    void Bind(const Deps& deps)
    {
        g_Deps = deps;
    }

    int __cdecl Lua_SetReceiverchimeraPartsInfoTable(lua_State* L)
    {
        if (!L || !EnsureLuaReady() || !LuaIsTable(L, 1))
            return 0;

        const int rowCount = static_cast<int>(g_Deps.LuaObjLen(L, 1));

        for (int i = 1; i <= rowCount; ++i)
        {
            g_Deps.LuaRawGetI(L, 1, i);

            if (LuaIsTable(L, -1))
            {
                ReceiverPackageEntry entry{};
                if (ReadReceiverPackageRow(L, -1, entry))
                {
                    QueueReceiverPackageEntry(entry);
                }
                else
                {
                    Log("[ReceiverChimeraPartsInfoTable] Ignored invalid row at index=%d\n", i);
                }
            }

            PopOne(L);
        }

        return 0;
    }

    int __cdecl Lua_ClearReceiverchimeraPartsInfoTable(lua_State* L)
    {
        UNREFERENCED_PARAMETER(L);
        ClearReceiverPackageEntries();
        return 0;
    }

    bool Install_ReadPartsPackageInfoIndexTable_Hook()
    {
        if (g_ReadPartsPackageInfoIndexTableHookInstalled)
        {
            Log("[ReceiverChimeraPartsInfoTable] ReadPartsPackageInfoIndexTable hook already installed\n");
            return true;
        }

        void* target = ResolveGameAddress(gAddr.EquipIdTableImpl_ReadPartsPackageInfoIndexTable);
        if (!target)
        {
            Log("[ReceiverChimeraPartsInfoTable] Failed to resolve ReadPartsPackageInfoIndexTable target\n");
            return false;
        }

        const bool ok = CreateAndEnableHook(
            target,
            reinterpret_cast<void*>(&hkReadPartsPackageInfoIndexTable),
            reinterpret_cast<void**>(&g_OrigReadPartsPackageInfoIndexTable));

        if (!ok)
        {
            Log("[ReceiverChimeraPartsInfoTable] Failed to install ReadPartsPackageInfoIndexTable hook\n");
            return false;
        }

        g_ReadPartsPackageInfoIndexTableHookInstalled = true;
        Log("[ReceiverChimeraPartsInfoTable] ReadPartsPackageInfoIndexTable hook installed\n");
        return true;
    }

    bool Uninstall_ReadPartsPackageInfoIndexTable_Hook()
    {
        if (!g_ReadPartsPackageInfoIndexTableHookInstalled)
            return true;

        if (void* target = ResolveGameAddress(gAddr.EquipIdTableImpl_ReadPartsPackageInfoIndexTable))
        {
            DisableAndRemoveHook(target);
        }

        g_OrigReadPartsPackageInfoIndexTable = nullptr;
        g_ReadPartsPackageInfoIndexTableHookInstalled = false;
        return true;
    }
}