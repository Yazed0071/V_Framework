#include "pch.h"
#include "EquipMotionData.h"

extern "C" {
    #include "lua.h"
    #include "lauxlib.h"
    #include "lualib.h"
}

#include <Windows.h>
#include <MinHook.h>

#include <cstdint>
#include <vector>
#include <string>
#include <mutex>
#include <algorithm>
#include <cstring>

#include "HookUtils.h"
#include "log.h"

namespace
{
    struct MotionRow
    {
        std::uint32_t equipId = 0;
        std::string path;
    };

    struct LuaApi
    {
        bool (*ResolveLuaApi)() = nullptr;

        int (*GetLuaTop)(lua_State* L) = nullptr;
        int (*LuaType)(lua_State* L, int idx) = nullptr;
        int (*GetLuaInt)(lua_State* L, int idx) = nullptr;
        const char* (*GetLuaString)(lua_State* L, int idx) = nullptr;
        std::size_t(*LuaObjLen)(lua_State* L, int idx) = nullptr;

        void (*LuaSetTop)(lua_State* L, int idx) = nullptr;
        void (*PushLuaNumber)(lua_State* L, float value) = nullptr;
        void (*LuaPushString)(lua_State* L, const char* value) = nullptr;
        void (*LuaCreateTable)(lua_State* L, int narr, int nrec) = nullptr;
        void (*LuaGetField)(lua_State* L, int idx, const char* fieldName) = nullptr;
        void (*LuaRawGetI)(lua_State* L, int idx, int n) = nullptr;
        void (*LuaGetTable)(lua_State* L, int idx) = nullptr;
        void (*LuaSetTable)(lua_State* L, int idx) = nullptr;
        void (*LuaPushValue)(lua_State* L, int idx) = nullptr;
    };

    static LuaApi g_Lua{};

    using ReloadEquipMotionData_t = std::uint64_t(__cdecl*)(std::uint64_t luaState);
    using ReadMotionDataTable_t = void(*)(void* thisPtr, lua_State* L, int idx, char* fieldName);

    static ReloadEquipMotionData_t g_OrigReloadEquipMotionData = nullptr;
    static ReadMotionDataTable_t g_OrigReadMotionDataTable = nullptr;

    static bool g_ReloadHookInstalled = false;
    static bool g_ReadHookInstalled = false;

    static std::mutex g_MotionMutex;
    static std::vector<MotionRow> g_CustomMotionRows;

    static constexpr uintptr_t kAddr_ReloadEquipMotionData = 0x1463B2BF0ull;
    static constexpr uintptr_t kAddr_ReadMotionDataTable = 0x1463B0B60ull;

    static bool ResolveDeps()
    {
        return g_Lua.ResolveLuaApi && g_Lua.ResolveLuaApi();
    }

    static bool IsLuaTable(lua_State* L, int idx)
    {
        return g_Lua.LuaType && g_Lua.LuaType(L, idx) == LUA_TTABLE;
    }

    static bool IsLuaNumber(lua_State* L, int idx)
    {
        return g_Lua.LuaType && g_Lua.LuaType(L, idx) == LUA_TNUMBER;
    }

    static bool IsLuaString(lua_State* L, int idx)
    {
        return g_Lua.LuaType && g_Lua.LuaType(L, idx) == LUA_TSTRING;
    }

    static void LuaPop(lua_State* L, int count)
    {
        if (!g_Lua.LuaSetTop)
            return;

        g_Lua.LuaSetTop(L, -count - 1);
    }

    static bool ReadMotionRowFromTable(lua_State* L, int idx, MotionRow& outRow)
    {
        if (!ResolveDeps())
            return false;

        if (!IsLuaTable(L, idx))
            return false;

        const int top = g_Lua.GetLuaTop ? g_Lua.GetLuaTop(L) : 0;

        outRow = {};

        g_Lua.LuaGetField(L, idx, "equipId");
        if (!IsLuaNumber(L, -1))
        {
            g_Lua.LuaSetTop(L, top);
            return false;
        }
        outRow.equipId = static_cast<std::uint32_t>(g_Lua.GetLuaInt(L, -1));
        LuaPop(L, 1);

        g_Lua.LuaGetField(L, idx, "path");
        if (!IsLuaString(L, -1))
        {
            g_Lua.LuaSetTop(L, top);
            return false;
        }

        {
            const char* path = g_Lua.GetLuaString(L, -1);
            if (!path || !path[0])
            {
                g_Lua.LuaSetTop(L, top);
                return false;
            }
            outRow.path = path;
        }
        LuaPop(L, 1);

        g_Lua.LuaSetTop(L, top);
        return true;
    }

    static void PushFieldNumber(lua_State* L, const char* key, std::uint32_t value)
    {
        g_Lua.LuaPushString(L, key);
        g_Lua.PushLuaNumber(L, static_cast<float>(value));
        g_Lua.LuaSetTable(L, -3);
    }

    static void PushFieldString(lua_State* L, const char* key, const char* value)
    {
        g_Lua.LuaPushString(L, key);
        g_Lua.LuaPushString(L, value);
        g_Lua.LuaSetTable(L, -3);
    }

    static void PushMotionRowTable(lua_State* L, const MotionRow& row)
    {
        g_Lua.LuaCreateTable(L, 0, 2);
        PushFieldNumber(L, "equipId", row.equipId);
        PushFieldString(L, "path", row.path.c_str());
    }

    static std::vector<MotionRow> CopyCustomRows()
    {
        std::lock_guard<std::mutex> lock(g_MotionMutex);
        return g_CustomMotionRows;
    }

    static bool UpsertCustomMotionRow(const MotionRow& row)
    {
        if (row.equipId == 0 || row.path.empty())
            return false;

        std::lock_guard<std::mutex> lock(g_MotionMutex);

        auto it = std::find_if(
            g_CustomMotionRows.begin(),
            g_CustomMotionRows.end(),
            [&](const MotionRow& existing) { return existing.equipId == row.equipId; });

        if (it != g_CustomMotionRows.end())
        {
            it->path = row.path;
        }
        else
        {
            g_CustomMotionRows.push_back(row);
        }

        Log("[EquipMotionData] Added/updated motion row equipId=%u path=%s\n",
            row.equipId, row.path.c_str());
        return true;
    }

    static bool RemoveCustomMotionRow(std::uint32_t equipId)
    {
        std::lock_guard<std::mutex> lock(g_MotionMutex);

        const auto oldSize = g_CustomMotionRows.size();
        g_CustomMotionRows.erase(
            std::remove_if(
                g_CustomMotionRows.begin(),
                g_CustomMotionRows.end(),
                [&](const MotionRow& row) { return row.equipId == equipId; }),
            g_CustomMotionRows.end());

        const bool removed = g_CustomMotionRows.size() != oldSize;
        if (removed)
        {
            Log("[EquipMotionData] Removed motion row equipId=%u\n", equipId);
        }

        return removed;
    }

    static void ClearCustomMotionRows()
    {
        std::lock_guard<std::mutex> lock(g_MotionMutex);
        g_CustomMotionRows.clear();
        Log("[EquipMotionData] Cleared custom motion rows\n");
    }

    // Appends:
    // table[arrayIndex] = { equipId = ..., path = ... }
    static void AppendMotionRow(lua_State* L, int tableIndex, int arrayIndex, const MotionRow& row)
    {
        // Push value table
        PushMotionRowTable(L, row);

        // Push key
        g_Lua.PushLuaNumber(L, static_cast<float>(arrayIndex));

        // Reorder stack so settable sees: ... table key value
        // Current top: value, below that key
        g_Lua.LuaPushValue(L, -2); // duplicate key
        LuaPop(L, 1); // no-op safety symmetry; keeps helper style consistent

        // Instead of fancy swaps, rebuild in correct order:
        // easiest path is:
        // 1) value table is on top
        // 2) push numeric key
        // 3) duplicate value
        // 4) set into table by tableIndex logic
    }

    static void SetArrayTableValue(lua_State* L, int tableIndex, int arrayIndex, const MotionRow& row)
    {
        // push key
        g_Lua.PushLuaNumber(L, static_cast<float>(arrayIndex));
        // push value
        PushMotionRowTable(L, row);

        // set table[key] = value
        g_Lua.LuaSetTable(L, tableIndex);
    }

    static void hkReadMotionDataTable(void* thisPtr, lua_State* L, int idx, char* fieldName)
    {
        if (!ResolveDeps() || !g_OrigReadMotionDataTable)
        {
            if (g_OrigReadMotionDataTable)
                g_OrigReadMotionDataTable(thisPtr, L, idx, fieldName);
            return;
        }

        if (!fieldName || std::strcmp(fieldName, "MotionDataTable") != 0)
        {
            g_OrigReadMotionDataTable(thisPtr, L, idx, fieldName);
            return;
        }

        const int top = g_Lua.GetLuaTop(L);

        // Open MotionDataTable from Lua root table
        g_Lua.LuaGetField(L, idx, fieldName);
        if (!IsLuaTable(L, -1))
        {
            g_Lua.LuaSetTop(L, top);
            g_OrigReadMotionDataTable(thisPtr, L, idx, fieldName);
            return;
        }

        const int motionTableIndex = g_Lua.GetLuaTop(L);
        std::size_t len = g_Lua.LuaObjLen(L, motionTableIndex);

        const auto customRows = CopyCustomRows();
        for (const auto& row : customRows)
        {
            ++len;
            SetArrayTableValue(L, motionTableIndex, static_cast<int>(len), row);

            Log("[EquipMotionData] Injected motion row into MotionDataTable equipId=%u path=%s\n",
                row.equipId, row.path.c_str());
        }

        g_Lua.LuaSetTop(L, top);

        g_OrigReadMotionDataTable(thisPtr, L, idx, fieldName);
    }

    static std::uint64_t __cdecl hkReloadEquipMotionData(std::uint64_t luaStateValue)
    {
        Log("[EquipMotionData] ReloadEquipMotionData hook fired\n");

        if (!g_OrigReloadEquipMotionData)
            return 0;

        return g_OrigReloadEquipMotionData(luaStateValue);
    }
}

namespace EquipMotionData
{
    void Bind(const Deps& deps)
    {
        g_Lua.ResolveLuaApi = deps.ResolveLuaApi;
        g_Lua.GetLuaTop = deps.GetLuaTop;
        g_Lua.LuaType = deps.LuaType;
        g_Lua.GetLuaInt = deps.GetLuaInt;
        g_Lua.GetLuaString = deps.GetLuaString;
        g_Lua.LuaObjLen = deps.LuaObjLen;
        g_Lua.LuaSetTop = deps.LuaSetTop;
        g_Lua.PushLuaNumber = deps.PushLuaNumber;
        g_Lua.LuaPushString = deps.LuaPushString;
        g_Lua.LuaCreateTable = deps.LuaCreateTable;
        g_Lua.LuaGetField = deps.LuaGetField;
        g_Lua.LuaRawGetI = deps.LuaRawGetI;
        g_Lua.LuaGetTable = deps.LuaGetTable;
        g_Lua.LuaSetTable = deps.LuaSetTable;
        g_Lua.LuaPushValue = deps.LuaPushValue;
    }

    int __cdecl Lua_AddEquipMotionDataEntry(lua_State* L)
    {
        if (!ResolveDeps())
            return 0;

        MotionRow row{};
        if (!ReadMotionRowFromTable(L, 1, row))
        {
            Log("[EquipMotionData] Lua_AddEquipMotionDataEntry failed to parse row\n");
            return 0;
        }

        const bool ok = UpsertCustomMotionRow(row);
        if (ok)
        {
            g_Lua.PushLuaNumber(L, 1.0f);
            return 1;
        }

        return 0;
    }

    int __cdecl Lua_RemoveEquipMotionDataEntry(lua_State* L)
    {
        if (!ResolveDeps() || !g_Lua.GetLuaInt)
            return 0;

        const std::uint32_t equipId = static_cast<std::uint32_t>(g_Lua.GetLuaInt(L, 1));
        RemoveCustomMotionRow(equipId);
        return 0;
    }

    int __cdecl Lua_ClearEquipMotionDataEntries(lua_State* L)
    {
        UNREFERENCED_PARAMETER(L);
        ClearCustomMotionRows();
        return 0;
    }

    int __cdecl Lua_AddEquipMotionDataTable(lua_State* L)
    {
        if (!ResolveDeps())
            return 0;

        if (!IsLuaTable(L, 1))
            return 0;

        const std::size_t len = g_Lua.LuaObjLen(L, 1);
        std::uint32_t added = 0;

        for (std::size_t i = 1; i <= len; ++i)
        {
            g_Lua.LuaRawGetI(L, 1, static_cast<int>(i));

            MotionRow row{};
            if (ReadMotionRowFromTable(L, -1, row))
            {
                if (UpsertCustomMotionRow(row))
                    ++added;
            }

            LuaPop(L, 1);
        }

        g_Lua.PushLuaNumber(L, static_cast<float>(added));
        return 1;
    }

    bool Install_EquipMotionDataTableImpl_ReloadEquipMotionData_Hook()
    {
        if (g_ReloadHookInstalled)
            return true;

        void* target = reinterpret_cast<void*>(kAddr_ReloadEquipMotionData);

        const bool ok = CreateAndEnableHook(
            target,
            reinterpret_cast<void*>(&hkReloadEquipMotionData),
            reinterpret_cast<void**>(&g_OrigReloadEquipMotionData));

        if (!ok)
        {
            Log("[EquipMotionData] Failed to hook ReloadEquipMotionData at %p\n", target);
            return false;
        }

        g_ReloadHookInstalled = true;
        Log("[EquipMotionData] Hooked ReloadEquipMotionData at %p\n", target);
        return true;
    }

    bool Uninstall_EquipMotionDataTableImpl_ReloadEquipMotionData_Hook()
    {
        if (!g_ReloadHookInstalled)
            return true;

        DisableAndRemoveHook(reinterpret_cast<void*>(kAddr_ReloadEquipMotionData));
        g_OrigReloadEquipMotionData = nullptr;
        g_ReloadHookInstalled = false;
        return true;
    }

    bool Install_ReadMotionDataTable_Hook()
    {
        if (g_ReadHookInstalled)
            return true;

        void* target = reinterpret_cast<void*>(kAddr_ReadMotionDataTable);

        const bool ok = CreateAndEnableHook(
            target,
            reinterpret_cast<void*>(&hkReadMotionDataTable),
            reinterpret_cast<void**>(&g_OrigReadMotionDataTable));

        if (!ok)
        {
            Log("[EquipMotionData] Failed to hook ReadMotionDataTable at %p\n", target);
            return false;
        }

        g_ReadHookInstalled = true;
        Log("[EquipMotionData] Hooked ReadMotionDataTable at %p\n", target);
        return true;
    }

    bool Uninstall_ReadMotionDataTable_Hook()
    {
        if (!g_ReadHookInstalled)
            return true;

        DisableAndRemoveHook(reinterpret_cast<void*>(kAddr_ReadMotionDataTable));
        g_OrigReadMotionDataTable = nullptr;
        g_ReadHookInstalled = false;
        return true;
    }
}