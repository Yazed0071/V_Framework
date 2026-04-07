#include "pch.h"
extern "C"
{
    #include "lua.h"
    #include "lauxlib.h"
    #include "lualib.h"
}
#include "EquipIdTable_AddToEquipIdTable.h"

#include <Windows.h>
#include <algorithm>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"


namespace
{
    using ReloadEquipIdTable_t = int(__fastcall*)(lua_State* L);

    struct EquipIdRow
    {
        std::int32_t equipId = 0;
        std::int32_t equipType = 0;
        std::int32_t value3 = 0;
        std::int32_t equipBlock = 0;
        std::string partsPath;
        std::string packPath;
    };

    EquipIdTableAdd::Deps g_Deps{};
    ReloadEquipIdTable_t g_OrigReloadEquipIdTable = nullptr;
    bool g_ReloadEquipIdTableHookInstalled = false;

    std::vector<EquipIdRow> g_QueuedEquipIdRows;
    std::mutex g_QueuedEquipIdRowsMutex;
}

namespace
{
    static bool ValidateDeps()
    {
        return
            g_Deps.ResolveLuaApi &&
            g_Deps.GetLuaTop &&
            g_Deps.LuaType &&
            g_Deps.LuaIsNumber &&
            g_Deps.LuaIsString &&
            g_Deps.LuaObjLen &&
            g_Deps.LuaPop &&
            g_Deps.GetLuaString &&
            g_Deps.GetLuaInt &&
            g_Deps.PushLuaNumber &&
            g_Deps.LuaPushString &&
            g_Deps.LuaCreateTable &&
            g_Deps.LuaRawSet &&
            g_Deps.LuaSetTable &&
            g_Deps.LuaRawGetI &&
            g_Deps.LuaPushValue;
    }

    static bool EnsureLuaReady()
    {
        return ValidateDeps() && g_Deps.ResolveLuaApi && g_Deps.ResolveLuaApi();
    }

    static bool IsLuaTable(lua_State* L, int idx)
    {
        return g_Deps.LuaType(L, idx) == LUA_TTABLE;
    }

    static void WriteNumberArrayField(lua_State* L, int tableIndex, int fieldIndex1Based, std::int32_t value)
    {
        g_Deps.PushLuaNumber(L, static_cast<float>(fieldIndex1Based));
        g_Deps.PushLuaNumber(L, static_cast<float>(value));
        g_Deps.LuaRawSet(L, tableIndex);
    }

    static void WriteStringArrayField(lua_State* L, int tableIndex, int fieldIndex1Based, const std::string& value)
    {
        g_Deps.PushLuaNumber(L, static_cast<float>(fieldIndex1Based));
        g_Deps.LuaPushString(L, value.c_str());
        g_Deps.LuaRawSet(L, tableIndex);
    }

    static bool ReadNumberArrayField(lua_State* L, int tableIndex, int fieldIndex1Based, std::int32_t& outValue)
    {
        outValue = 0;

        g_Deps.LuaRawGetI(L, tableIndex, fieldIndex1Based);

        const bool ok = g_Deps.LuaIsNumber(L, -1);
        if (ok)
            outValue = g_Deps.GetLuaInt(L, -1);

        g_Deps.LuaPop(L, 1);
        return ok;
    }

    static bool ReadStringArrayField(lua_State* L, int tableIndex, int fieldIndex1Based, std::string& outValue)
    {
        outValue.clear();

        g_Deps.LuaRawGetI(L, tableIndex, fieldIndex1Based);

        const bool ok = g_Deps.LuaIsString(L, -1);
        if (ok)
        {
            const char* value = g_Deps.GetLuaString(L, -1);
            outValue = value ? value : "";
        }

        g_Deps.LuaPop(L, 1);
        return ok;
    }

    static bool ReadEquipIdRow(lua_State* L, int rowIndex, EquipIdRow& outRow)
    {
        outRow = {};

        if (!IsLuaTable(L, rowIndex))
            return false;

        if (!ReadNumberArrayField(L, rowIndex, 1, outRow.equipId))
            return false;
        if (!ReadNumberArrayField(L, rowIndex, 2, outRow.equipType))
            return false;
        if (!ReadNumberArrayField(L, rowIndex, 3, outRow.value3))
            return false;
        if (!ReadNumberArrayField(L, rowIndex, 4, outRow.equipBlock))
            return false;
        if (!ReadStringArrayField(L, rowIndex, 5, outRow.partsPath))
            return false;
        if (!ReadStringArrayField(L, rowIndex, 6, outRow.packPath))
            return false;

        return outRow.equipId > 0;
    }

    static void QueueEquipIdRow(const EquipIdRow& row)
    {
        if (row.equipId <= 0)
            return;

        std::lock_guard<std::mutex> lock(g_QueuedEquipIdRowsMutex);

        auto it = std::find_if(
            g_QueuedEquipIdRows.begin(),
            g_QueuedEquipIdRows.end(),
            [&](const EquipIdRow& existing)
            {
                return existing.equipId == row.equipId;
            });

        if (it != g_QueuedEquipIdRows.end())
        {
            *it = row;
            Log("[EquipIdTable] Updated queued entry equipId=%d\n", row.equipId);
            return;
        }

        g_QueuedEquipIdRows.push_back(row);
        Log("[EquipIdTable] Queued equipId=%d\n", row.equipId);
    }

    static bool LuaTableContainsEquipId(lua_State* L, int rootTableIndex, std::int32_t equipId)
    {
        const int rowCount = static_cast<int>(g_Deps.LuaObjLen(L, rootTableIndex));

        for (int i = 1; i <= rowCount; ++i)
        {
            g_Deps.LuaRawGetI(L, rootTableIndex, i);

            bool found = false;
            if (IsLuaTable(L, -1))
            {
                const int rowIndex = g_Deps.GetLuaTop(L);
                std::int32_t existingEquipId = 0;
                if (ReadNumberArrayField(L, rowIndex, 1, existingEquipId) && existingEquipId == equipId)
                    found = true;
            }

            g_Deps.LuaPop(L, 1);

            if (found)
                return true;
        }

        return false;
    }

    static void AppendEquipIdRowToLuaTable(lua_State* L, int rootTableIndex, const EquipIdRow& row)
    {
        const int newRowIndex1Based = static_cast<int>(g_Deps.LuaObjLen(L, rootTableIndex)) + 1;

        g_Deps.LuaCreateTable(L, 6, 0);
        const int rowIndex = g_Deps.GetLuaTop(L);

        WriteNumberArrayField(L, rowIndex, 1, row.equipId);
        WriteNumberArrayField(L, rowIndex, 2, row.equipType);
        WriteNumberArrayField(L, rowIndex, 3, row.value3);
        WriteNumberArrayField(L, rowIndex, 4, row.equipBlock);
        WriteStringArrayField(L, rowIndex, 5, row.partsPath);
        WriteStringArrayField(L, rowIndex, 6, row.packPath);

        g_Deps.PushLuaNumber(L, static_cast<float>(newRowIndex1Based));
        g_Deps.LuaPushValue(L, rowIndex);
        g_Deps.LuaSetTable(L, rootTableIndex);

        g_Deps.LuaPop(L, 1);

        Log("[EquipIdTable] Appended equipId=%d at row=%d\n", row.equipId, newRowIndex1Based);
    }

    // Applies queued rows into argument table #1 before stock ReloadEquipIdTable runs.
    // Params: L
    static void ApplyAllQueuedEquipIdRows(lua_State* L)
    {
        if (!L || !EnsureLuaReady())
            return;

        if (!IsLuaTable(L, 1))
        {
            Log("[EquipIdTable] Argument #1 is not a table\n");
            return;
        }

        std::lock_guard<std::mutex> lock(g_QueuedEquipIdRowsMutex);

        if (g_QueuedEquipIdRows.empty())
            return;

        for (const auto& row : g_QueuedEquipIdRows)
        {
            if (LuaTableContainsEquipId(L, 1, row.equipId))
            {
                Log("[EquipIdTable] Skip append: equipId=%d already present in Lua table\n", row.equipId);
                continue;
            }

            AppendEquipIdRowToLuaTable(L, 1, row);
        }
    }

    // Hooked stock ReloadEquipIdTable.
    // Params: L
    static int __fastcall hkReloadEquipIdTable(lua_State* L)
    {
        if (L)
            ApplyAllQueuedEquipIdRows(L);

        if (g_OrigReloadEquipIdTable)
            return g_OrigReloadEquipIdTable(L);

        return 0;
    }
}

namespace EquipIdTableAdd
{
    void Bind(const Deps& deps)
    {
        g_Deps = deps;
    }

    // Lua: V_FrameWork.AddToEquipIdTable({ { equipId, equipType, value3, equipBlock, partsPath, packPath }, ... })
    // Params: L
    int __cdecl Lua_AddToEquipIdTable(lua_State* L)
    {
        if (!L || !EnsureLuaReady() || !IsLuaTable(L, 1))
            return 0;

        const int rowCount = static_cast<int>(g_Deps.LuaObjLen(L, 1));

        for (int i = 1; i <= rowCount; ++i)
        {
            g_Deps.LuaRawGetI(L, 1, i);

            if (IsLuaTable(L, -1))
            {
                const int rowIndex = g_Deps.GetLuaTop(L);
                EquipIdRow row{};
                if (ReadEquipIdRow(L, rowIndex, row))
                    QueueEquipIdRow(row);
                else
                    Log("[EquipIdTable] Ignored invalid row at index=%d\n", i);
            }

            g_Deps.LuaPop(L, 1);
        }

        return 0;
    }

    // Installs hook on EquipIdTableImpl::ReloadEquipIdTable.
    // Params: none
    bool Install_EquipIdTableImpl_ReloadEquipIdTable_Hook()
    {
        if (g_ReloadEquipIdTableHookInstalled)
        {
            Log("[EquipIdTable] Reload hook already installed\n");
            return true;
        }

        void* target = ResolveGameAddress(gAddr.EquipIdTableImpl_ReloadEquipIdTable);
        if (!target)
        {
            Log("[EquipIdTable] ReloadEquipIdTable target resolve failed\n");
            return false;
        }

        const bool ok = CreateAndEnableHook(
            target,
            reinterpret_cast<void*>(&hkReloadEquipIdTable),
            reinterpret_cast<void**>(&g_OrigReloadEquipIdTable));

        if (!ok)
        {
            Log("[EquipIdTable] Reload hook install failed\n");
            return false;
        }

        g_ReloadEquipIdTableHookInstalled = true;
        Log("[EquipIdTable] Reload hook installed\n");
        return true;
    }

    // Removes hook on EquipIdTableImpl::ReloadEquipIdTable.
    // Params: none
    bool Uninstall_EquipIdTableImpl_ReloadEquipIdTable_Hook()
    {
        if (!g_ReloadEquipIdTableHookInstalled)
            return true;

        void* target = ResolveGameAddress(gAddr.EquipIdTableImpl_ReloadEquipIdTable);
        if (target)
            DisableAndRemoveHook(target);

        g_OrigReloadEquipIdTable = nullptr;
        g_ReloadEquipIdTableHookInstalled = false;
        return true;
    }
}