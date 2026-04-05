#include "pch.h"

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <vector>

#include "EquipParameters_GunBasic.h"

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

extern "C"
{
    #include "lua.h"
    #include "lauxlib.h"
    #include "lualib.h"
}

namespace
{
    struct GunBasicEntry
    {
        std::int32_t weaponId = 0;
        std::int32_t receiverId = -1;
        std::int32_t barrelId = -1;
        std::int32_t ammoId = -1;
        std::int32_t stockId = -1;
        std::int32_t muzzleId = -1;
        std::int32_t muzzleOptionId = -1;
        std::int32_t scope1Id = -1;
        std::int32_t scope2Id = -1;
        std::int32_t underBarrelId = -1;
        std::int32_t laserFlash1Id = -1;
        std::int32_t laserFlash2Id = -1;
        std::int32_t weaponGrade = -1;
    };

    std::vector<GunBasicEntry> g_CustomGunBasicEntries;
    std::mutex g_CustomGunBasicMutex;

    using ReloadEquipParameterTablesImpl2_t = int(__fastcall*)(void* _this, lua_State* L);
    ReloadEquipParameterTablesImpl2_t g_OrigReloadEquipParameterTablesImpl2 = nullptr;

    bool g_ReloadEquipParameterTablesImpl2HookInstalled = false;
}

// Returns true if one row has useful data.
// Params: entry
static bool IsValidGunBasicEntry(const GunBasicEntry& entry)
{
    return entry.weaponId > 0;
}

// Reads one optional integer field from a Lua table currently at stack top.
// Params: L, fieldName, defaultValue, outValue
static void ReadOptionalIntField(lua_State* L, const char* fieldName, std::int32_t defaultValue, std::int32_t& outValue)
{
    outValue = defaultValue;

    lua_getfield(L, -1, fieldName);
    if (lua_isnumber(L, -1))
    {
        outValue = static_cast<std::int32_t>(lua_tointeger(L, -1));
    }
    lua_settop(L, -2);
}

// Reads one required integer field from a Lua table currently at stack top.
// Params: L, fieldName, outValue
static bool ReadRequiredIntField(lua_State* L, const char* fieldName, std::int32_t& outValue)
{
    outValue = 0;

    lua_getfield(L, -1, fieldName);
    const bool ok = lua_isnumber(L, -1) != 0;
    if (ok)
    {
        outValue = static_cast<std::int32_t>(lua_tointeger(L, -1));
    }
    lua_settop(L, -2);
    return ok;
}

// Reads one array int field from a row table.
// Params: L, rowIndex, fieldIndex1Based, defaultValue
static std::int32_t ReadArrayIntField(lua_State* L, int rowIndex, int fieldIndex1Based, std::int32_t defaultValue)
{
    lua_pushnumber(L, static_cast<lua_Number>(fieldIndex1Based));
    lua_gettable(L, rowIndex);

    std::int32_t value = defaultValue;
    if (lua_isnumber(L, -1))
    {
        value = static_cast<std::int32_t>(lua_tointeger(L, -1));
    }

    lua_settop(L, -2);
    return value;
}

// Writes one array int field into a row table.
// Params: L, rowIndex, fieldIndex1Based, value
static void WriteArrayIntField(lua_State* L, int rowIndex, int fieldIndex1Based, std::int32_t value)
{
    lua_pushnumber(L, static_cast<lua_Number>(fieldIndex1Based));
    lua_pushnumber(L, static_cast<lua_Number>(value));
    lua_settable(L, rowIndex);
}

// Returns Lua array size.
// Params: L, tableIndex
static int GetLuaArraySize(lua_State* L, int tableIndex)
{
    int count = 0;

    lua_pushnil(L);
    while (lua_next(L, tableIndex) != 0)
    {
        ++count;
        lua_settop(L, -2);
    }

    return count;
}

// Queues or replaces one custom gunBasic row.
// Params: entry
static void QueueGunBasicEntry(const GunBasicEntry& entry)
{
    if (!IsValidGunBasicEntry(entry))
        return;

    std::lock_guard<std::mutex> lock(g_CustomGunBasicMutex);

    auto it = std::find_if(
        g_CustomGunBasicEntries.begin(),
        g_CustomGunBasicEntries.end(),
        [&](const GunBasicEntry& existing)
        {
            return existing.weaponId == entry.weaponId;
        });

    if (it != g_CustomGunBasicEntries.end())
    {
        *it = entry;
        Log("[GunBasic] Updated queued entry weaponId=0x%X\n", entry.weaponId);
        return;
    }

    g_CustomGunBasicEntries.push_back(entry);
    Log("[GunBasic] Queued new entry weaponId=0x%X\n", entry.weaponId);
}

// Applies all queued gunBasic rows into param_1["gunBasic"].
// Params: L
static void ApplyAllQueuedGunBasic(lua_State* L)
{
    if (!L)
        return;

    std::lock_guard<std::mutex> lock(g_CustomGunBasicMutex);

    if (g_CustomGunBasicEntries.empty())
        return;

    lua_getfield(L, -1, "gunBasic");
    if (!lua_istable(L, -1))
    {
        lua_settop(L, -2);
        Log("[GunBasic] gunBasic table not found during reload\n");
        return;
    }

    const int gunBasicIndex = lua_gettop(L);
    int rowCount = GetLuaArraySize(L, gunBasicIndex);

    for (const auto& entry : g_CustomGunBasicEntries)
    {
        bool found = false;

        for (int i = 1; i <= rowCount; ++i)
        {
            lua_pushnumber(L, static_cast<lua_Number>(i));
            lua_gettable(L, gunBasicIndex);

            if (lua_istable(L, -1))
            {
                const int rowIndex = lua_gettop(L);
                const std::int32_t rowWeaponId = ReadArrayIntField(L, rowIndex, 1, 0);

                if (rowWeaponId == entry.weaponId)
                {
                    if (entry.receiverId >= 0)     WriteArrayIntField(L, rowIndex, 2, entry.receiverId);
                    if (entry.barrelId >= 0)       WriteArrayIntField(L, rowIndex, 3, entry.barrelId);
                    if (entry.ammoId >= 0)         WriteArrayIntField(L, rowIndex, 4, entry.ammoId);
                    if (entry.stockId >= 0)        WriteArrayIntField(L, rowIndex, 5, entry.stockId);
                    if (entry.muzzleId >= 0)       WriteArrayIntField(L, rowIndex, 6, entry.muzzleId);
                    if (entry.muzzleOptionId >= 0) WriteArrayIntField(L, rowIndex, 7, entry.muzzleOptionId);
                    if (entry.scope1Id >= 0)       WriteArrayIntField(L, rowIndex, 8, entry.scope1Id);
                    if (entry.scope2Id >= 0)       WriteArrayIntField(L, rowIndex, 9, entry.scope2Id);
                    if (entry.underBarrelId >= 0)  WriteArrayIntField(L, rowIndex, 10, entry.underBarrelId);
                    if (entry.laserFlash1Id >= 0)  WriteArrayIntField(L, rowIndex, 11, entry.laserFlash1Id);
                    if (entry.laserFlash2Id >= 0)  WriteArrayIntField(L, rowIndex, 12, entry.laserFlash2Id);
                    if (entry.weaponGrade >= 0)    WriteArrayIntField(L, rowIndex, 13, entry.weaponGrade);

                    Log("[GunBasic] Applied patch weaponId=0x%X\n", entry.weaponId);
                    found = true;
                }
            }

            lua_settop(L, -2);

            if (found)
                break;
        }

        if (!found)
        {
            lua_createtable(L, 13, 0);
            const int rowIndex = lua_gettop(L);

            WriteArrayIntField(L, rowIndex, 1, entry.weaponId);
            WriteArrayIntField(L, rowIndex, 2, entry.receiverId >= 0 ? entry.receiverId : 0);
            WriteArrayIntField(L, rowIndex, 3, entry.barrelId >= 0 ? entry.barrelId : 0);
            WriteArrayIntField(L, rowIndex, 4, entry.ammoId >= 0 ? entry.ammoId : 0);
            WriteArrayIntField(L, rowIndex, 5, entry.stockId >= 0 ? entry.stockId : 0);
            WriteArrayIntField(L, rowIndex, 6, entry.muzzleId >= 0 ? entry.muzzleId : 0);
            WriteArrayIntField(L, rowIndex, 7, entry.muzzleOptionId >= 0 ? entry.muzzleOptionId : 0);
            WriteArrayIntField(L, rowIndex, 8, entry.scope1Id >= 0 ? entry.scope1Id : 0);
            WriteArrayIntField(L, rowIndex, 9, entry.scope2Id >= 0 ? entry.scope2Id : 0);
            WriteArrayIntField(L, rowIndex, 10, entry.underBarrelId >= 0 ? entry.underBarrelId : 0);
            WriteArrayIntField(L, rowIndex, 11, entry.laserFlash1Id >= 0 ? entry.laserFlash1Id : 0);
            WriteArrayIntField(L, rowIndex, 12, entry.laserFlash2Id >= 0 ? entry.laserFlash2Id : 0);
            WriteArrayIntField(L, rowIndex, 13, entry.weaponGrade >= 0 ? entry.weaponGrade : 0);

            ++rowCount;
            lua_pushnumber(L, static_cast<lua_Number>(rowCount));
            lua_pushvalue(L, rowIndex);
            lua_settable(L, gunBasicIndex);

            lua_settop(L, -2);

            Log("[GunBasic] Appended entry weaponId=0x%X\n", entry.weaponId);
        }
    }

    lua_settop(L, -2);
}

// Hooked stock ReloadEquipParameterTablesImpl2.
// Params: _this, L
static int __fastcall hkReloadEquipParameterTablesImpl2(void* _this, lua_State* L)
{
    if (L && lua_istable(L, -1))
    {
        ApplyAllQueuedGunBasic(L);
    }

    if (g_OrigReloadEquipParameterTablesImpl2)
        return g_OrigReloadEquipParameterTablesImpl2(_this, L);

    return 0;
}

// Lua bridge for V_FrameWork.SetGunBasic{...}.
// Params: L
int __cdecl l_SetGunBasic(lua_State* L)
{
    if (!L || !lua_istable(L, 1))
        return 0;

    lua_pushvalue(L, 1);

    GunBasicEntry entry{};

    if (!ReadRequiredIntField(L, "weaponId", entry.weaponId) || entry.weaponId <= 0)
    {
        lua_settop(L, -2);
        Log("[GunBasic] weaponId is required\n");
        return 0;
    }

    ReadOptionalIntField(L, "receiverId", -1, entry.receiverId);
    ReadOptionalIntField(L, "barrelId", -1, entry.barrelId);
    ReadOptionalIntField(L, "ammoId", -1, entry.ammoId);
    ReadOptionalIntField(L, "stockId", -1, entry.stockId);
    ReadOptionalIntField(L, "muzzleId", -1, entry.muzzleId);
    ReadOptionalIntField(L, "muzzleOptionId", -1, entry.muzzleOptionId);
    ReadOptionalIntField(L, "scope1Id", -1, entry.scope1Id);
    ReadOptionalIntField(L, "scope2Id", -1, entry.scope2Id);
    ReadOptionalIntField(L, "underBarrelId", -1, entry.underBarrelId);
    ReadOptionalIntField(L, "laserFlash1Id", -1, entry.laserFlash1Id);
    ReadOptionalIntField(L, "laserFlash2Id", -1, entry.laserFlash2Id);
    ReadOptionalIntField(L, "weaponGrade", -1, entry.weaponGrade);

    lua_settop(L, -2);

    QueueGunBasicEntry(entry);
    return 0;
}

// Installs the reload hook.
// Params: none
bool Install_EquipParameterTablesImpl_ReloadEquipParameterTablesImpl2_Hook()
{
    if (g_ReloadEquipParameterTablesImpl2HookInstalled)
    {
        Log("[GunBasic] ReloadEquipParameterTablesImpl2 already installed\n");
        return true;
    }

    void* target = ResolveGameAddress(gAddr.EquipParameterTablesImpl_ReloadEquipParameterTablesImpl2);
    if (!target)
    {
        Log("[GunBasic] ReloadEquipParameterTablesImpl2 target resolve failed\n");
        return false;
    }

    if (!CreateAndEnableHook(
        target,
        reinterpret_cast<void*>(&hkReloadEquipParameterTablesImpl2),
        reinterpret_cast<void**>(&g_OrigReloadEquipParameterTablesImpl2)))
    {
        Log("[GunBasic] ReloadEquipParameterTablesImpl2 hook install failed\n");
        return false;
    }

    g_ReloadEquipParameterTablesImpl2HookInstalled = true;
    Log("[GunBasic] ReloadEquipParameterTablesImpl2 hook installed\n");
    return true;
}

// Uninstalls the reload hook.
// Params: none
bool Uninstall_EquipParameterTablesImpl_ReloadEquipParameterTablesImpl2_Hook()
{
    if (!g_ReloadEquipParameterTablesImpl2HookInstalled)
        return true;

    void* target = ResolveGameAddress(gAddr.EquipParameterTablesImpl_ReloadEquipParameterTablesImpl2);
    if (target)
        DisableAndRemoveHook(target);

    g_OrigReloadEquipParameterTablesImpl2 = nullptr;
    g_ReloadEquipParameterTablesImpl2HookInstalled = false;

    Log("[GunBasic] ReloadEquipParameterTablesImpl2 hook removed\n");
    return true;
}