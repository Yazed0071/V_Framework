#include "pch.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
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

    constexpr std::uint32_t kStockGunBasicMaxWeaponId = 0x202;
    constexpr std::size_t kGunBasicEntrySize = 0x0C;
    constexpr std::ptrdiff_t kEquipParameterTablesImpl_GunBasicPtr_Offset = 0x08;

    std::vector<GunBasicEntry> g_CustomGunBasicEntries;
    std::mutex g_CustomGunBasicMutex;

    std::vector<std::uint8_t> g_GunBasicShadowBuffer;
    std::uint32_t g_GunBasicShadowCapacity = 0;
    void* g_StockGunBasicPtr = nullptr;
    void* g_LastEquipParameterTablesImpl = nullptr;

    using ReloadEquipParameterTablesImpl2_t = int(__fastcall*)(void* _this, lua_State* L);
    static ReloadEquipParameterTablesImpl2_t g_OrigReloadEquipParameterTablesImpl2 = nullptr;

    static bool g_ReloadEquipParameterTablesImpl2HookInstalled = false;

    using lua_getfield_t = void(__fastcall*)(lua_State* L, int idx, char* k);
    using lua_isnumber_t = int(__fastcall*)(lua_State* L, int idx);
    using lua_tointeger_t = long long(__fastcall*)(lua_State* L, int idx);
    using lua_settop_t = void(__fastcall*)(lua_State* L, int idx);
    using lua_pushnumber_t = void(__fastcall*)(lua_State* L, lua_Number n);
    using lua_gettable_t = void(__fastcall*)(lua_State* L, int idx);
    using lua_settable_t = void(__fastcall*)(lua_State* L, int idx);
    using lua_pushnil_t = void(__fastcall*)(lua_State* L);
    using lua_next_t = int(__fastcall*)(lua_State* L, int idx);
    using lua_gettop_t = int(__fastcall*)(lua_State* L);
    using lua_type_t = int(__fastcall*)(lua_State* L, int idx);
    using lua_createtable_t = void(__fastcall*)(lua_State* L, int narr, int nrec);
    using lua_pushvalue_t = void(__fastcall*)(lua_State* L, int idx);

    static lua_getfield_t    g_lua_getfield = nullptr;
    static lua_isnumber_t    g_lua_isnumber = nullptr;
    static lua_tointeger_t   g_lua_tointeger = nullptr;
    static lua_settop_t      g_lua_settop = nullptr;
    static lua_pushnumber_t  g_lua_pushnumber = nullptr;
    static lua_gettable_t    g_lua_gettable = nullptr;
    static lua_settable_t    g_lua_settable = nullptr;
    static lua_pushnil_t     g_lua_pushnil = nullptr;
    static lua_next_t        g_lua_next = nullptr;
    static lua_gettop_t      g_lua_gettop = nullptr;
    static lua_type_t        g_lua_type = nullptr;
    static lua_createtable_t g_lua_createtable = nullptr;
    static lua_pushvalue_t   g_lua_pushvalue = nullptr;
}

// Resolves the Lua bridge functions used by this file.
// Params: none
static bool ResolveLuaApi()
{
    if (!g_lua_getfield)
        g_lua_getfield = reinterpret_cast<lua_getfield_t>(ResolveGameAddress(gAddr.lua_getfield));
    if (!g_lua_isnumber)
        g_lua_isnumber = reinterpret_cast<lua_isnumber_t>(ResolveGameAddress(gAddr.lua_isnumber));
    if (!g_lua_tointeger)
        g_lua_tointeger = reinterpret_cast<lua_tointeger_t>(ResolveGameAddress(gAddr.lua_tointeger));
    if (!g_lua_settop)
        g_lua_settop = reinterpret_cast<lua_settop_t>(ResolveGameAddress(gAddr.lua_settop));
    if (!g_lua_pushnumber)
        g_lua_pushnumber = reinterpret_cast<lua_pushnumber_t>(ResolveGameAddress(gAddr.lua_pushnumber));
    if (!g_lua_gettable)
        g_lua_gettable = reinterpret_cast<lua_gettable_t>(ResolveGameAddress(gAddr.lua_gettable));
    if (!g_lua_settable)
        g_lua_settable = reinterpret_cast<lua_settable_t>(ResolveGameAddress(gAddr.lua_settable));
    if (!g_lua_pushnil)
        g_lua_pushnil = reinterpret_cast<lua_pushnil_t>(ResolveGameAddress(gAddr.lua_pushnil));
    if (!g_lua_next)
        g_lua_next = reinterpret_cast<lua_next_t>(ResolveGameAddress(gAddr.lua_next));
    if (!g_lua_gettop)
        g_lua_gettop = reinterpret_cast<lua_gettop_t>(ResolveGameAddress(gAddr.lua_gettop));
    if (!g_lua_type)
        g_lua_type = reinterpret_cast<lua_type_t>(ResolveGameAddress(gAddr.lua_type));
    if (!g_lua_createtable)
        g_lua_createtable = reinterpret_cast<lua_createtable_t>(ResolveGameAddress(gAddr.lua_createtable));
    if (!g_lua_pushvalue)
        g_lua_pushvalue = reinterpret_cast<lua_pushvalue_t>(ResolveGameAddress(gAddr.lua_pushvalue));

    return g_lua_getfield &&
        g_lua_isnumber &&
        g_lua_tointeger &&
        g_lua_settop &&
        g_lua_pushnumber &&
        g_lua_gettable &&
        g_lua_settable &&
        g_lua_pushnil &&
        g_lua_next &&
        g_lua_gettop &&
        g_lua_type &&
        g_lua_createtable &&
        g_lua_pushvalue;
}

// Pushes one table field onto the stack.
// Params: L, idx, fieldName
static void LuaGetField(lua_State* L, int idx, const char* fieldName)
{
    if (!ResolveLuaApi() || !fieldName)
        return;

    g_lua_getfield(L, idx, const_cast<char*>(fieldName));
}

// Returns true if one Lua value is a number.
// Params: L, idx
static bool LuaIsNumber(lua_State* L, int idx)
{
    return ResolveLuaApi() && g_lua_isnumber(L, idx) != 0;
}

// Returns one Lua integer.
// Params: L, idx
static std::int32_t LuaToInt(lua_State* L, int idx)
{
    if (!ResolveLuaApi())
        return 0;

    return static_cast<std::int32_t>(g_lua_tointeger(L, idx));
}

// Sets the Lua stack top.
// Params: L, idx
static void LuaSetTop(lua_State* L, int idx)
{
    if (!ResolveLuaApi())
        return;

    g_lua_settop(L, idx);
}

// Pushes one number.
// Params: L, value
static void LuaPushNumber(lua_State* L, lua_Number value)
{
    if (!ResolveLuaApi())
        return;

    g_lua_pushnumber(L, value);
}

// Runs lua_gettable.
// Params: L, idx
static void LuaGetTable(lua_State* L, int idx)
{
    if (!ResolveLuaApi())
        return;

    g_lua_gettable(L, idx);
}

// Runs lua_settable.
// Params: L, idx
static void LuaSetTable(lua_State* L, int idx)
{
    if (!ResolveLuaApi())
        return;

    g_lua_settable(L, idx);
}

// Pushes nil.
// Params: L
static void LuaPushNil(lua_State* L)
{
    if (!ResolveLuaApi())
        return;

    g_lua_pushnil(L);
}

// Runs lua_next.
// Params: L, idx
static int LuaNext(lua_State* L, int idx)
{
    if (!ResolveLuaApi())
        return 0;

    return g_lua_next(L, idx);
}

// Returns the current stack top.
// Params: L
static int LuaGetTop(lua_State* L)
{
    if (!ResolveLuaApi())
        return 0;

    return g_lua_gettop(L);
}

// Returns the Lua type.
// Params: L, idx
static int LuaType(lua_State* L, int idx)
{
    if (!ResolveLuaApi())
        return LUA_TNONE;

    return g_lua_type(L, idx);
}

// Returns true if one Lua value is a table.
// Params: L, idx
static bool LuaIsTable(lua_State* L, int idx)
{
    return LuaType(L, idx) == LUA_TTABLE;
}

// Runs lua_createtable.
// Params: L, narr, nrec
static void LuaCreateTable(lua_State* L, int narr, int nrec)
{
    if (!ResolveLuaApi())
        return;

    g_lua_createtable(L, narr, nrec);
}

// Runs lua_pushvalue.
// Params: L, idx
static void LuaPushValue(lua_State* L, int idx)
{
    if (!ResolveLuaApi())
        return;

    g_lua_pushvalue(L, idx);
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

    LuaGetField(L, -1, fieldName);
    if (LuaIsNumber(L, -1))
    {
        outValue = LuaToInt(L, -1);
    }
    LuaSetTop(L, -2);
}

// Reads one required integer field from a Lua table currently at stack top.
// Params: L, fieldName, outValue
static bool ReadRequiredIntField(lua_State* L, const char* fieldName, std::int32_t& outValue)
{
    outValue = 0;

    LuaGetField(L, -1, fieldName);
    const bool ok = LuaIsNumber(L, -1);
    if (ok)
    {
        outValue = LuaToInt(L, -1);
    }
    LuaSetTop(L, -2);
    return ok;
}

// Reads one array int field from a row table.
// Params: L, rowIndex, fieldIndex1Based, defaultValue
static std::int32_t ReadArrayIntField(lua_State* L, int rowIndex, int fieldIndex1Based, std::int32_t defaultValue)
{
    LuaPushNumber(L, static_cast<lua_Number>(fieldIndex1Based));
    LuaGetTable(L, rowIndex);

    std::int32_t value = defaultValue;
    if (LuaIsNumber(L, -1))
    {
        value = LuaToInt(L, -1);
    }

    LuaSetTop(L, -2);
    return value;
}

// Writes one array int field into a row table.
// Params: L, rowIndex, fieldIndex1Based, value
static void WriteArrayIntField(lua_State* L, int rowIndex, int fieldIndex1Based, std::int32_t value)
{
    LuaPushNumber(L, static_cast<lua_Number>(fieldIndex1Based));
    LuaPushNumber(L, static_cast<lua_Number>(value));
    LuaSetTable(L, rowIndex);
}

// Returns Lua array size.
// Params: L, tableIndex
static int GetLuaArraySize(lua_State* L, int tableIndex)
{
    int count = 0;

    LuaPushNil(L);
    while (LuaNext(L, tableIndex) != 0)
    {
        ++count;
        LuaSetTop(L, -2);
    }

    return count;
}

// Returns the maximum weaponId currently queued.
// Params: none
static std::uint32_t GetMaxQueuedWeaponId()
{
    std::uint32_t maxWeaponId = kStockGunBasicMaxWeaponId;

    for (const auto& entry : g_CustomGunBasicEntries)
    {
        if (entry.weaponId > 0)
            maxWeaponId = (std::max)(maxWeaponId, static_cast<std::uint32_t>(entry.weaponId));
    }

    return maxWeaponId;
}

// Ensures the shadow gunBasic buffer is large enough for the current highest weaponId.
// Params: none
static bool EnsureGunBasicShadowCapacity()
{
    const std::uint32_t requiredCapacity = GetMaxQueuedWeaponId();
    if (requiredCapacity == 0)
        return false;

    if (requiredCapacity <= g_GunBasicShadowCapacity && !g_GunBasicShadowBuffer.empty())
    {
        std::memset(g_GunBasicShadowBuffer.data(), 0, g_GunBasicShadowBuffer.size());
        return true;
    }

    const std::size_t byteSize = static_cast<std::size_t>(requiredCapacity) * kGunBasicEntrySize;

    try
    {
        g_GunBasicShadowBuffer.assign(byteSize, 0);
    }
    catch (...)
    {
        Log("[GunBasic] Failed to allocate shadow buffer (%zu bytes)\n", byteSize);
        return false;
    }

    g_GunBasicShadowCapacity = requiredCapacity;

    Log(
        "[GunBasic] Resized shadow gunBasic buffer to 0x%X entries (%zu bytes)\n",
        requiredCapacity,
        byteSize);

    return true;
}

// Returns the current gunBasic pointer stored in EquipParameterTablesImpl.
// Params: _this
static void* GetEquipParameterTablesGunBasicPtr(void* _this)
{
    if (!_this)
        return nullptr;

    return *reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(_this) + kEquipParameterTablesImpl_GunBasicPtr_Offset);
}

// Replaces EquipParameterTablesImpl->gunBasic pointer with our shadow buffer.
// Params: _this
static bool RedirectGunBasicPointerToShadow(void* _this)
{
    if (!_this)
        return false;

    if (!EnsureGunBasicShadowCapacity() || g_GunBasicShadowBuffer.empty())
        return false;

    void** ppGunBasic = reinterpret_cast<void**>(
        reinterpret_cast<std::uint8_t*>(_this) + kEquipParameterTablesImpl_GunBasicPtr_Offset);

    if (!g_StockGunBasicPtr)
        g_StockGunBasicPtr = *ppGunBasic;

    *ppGunBasic = g_GunBasicShadowBuffer.data();
    g_LastEquipParameterTablesImpl = _this;

    Log(
        "[GunBasic] Redirected gunBasic ptr %p -> %p (capacity=0x%X)\n",
        g_StockGunBasicPtr,
        g_GunBasicShadowBuffer.data(),
        g_GunBasicShadowCapacity);

    return true;
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

    if (!ResolveLuaApi())
    {
        Log("[GunBasic] Lua API resolve failed\n");
        return;
    }

    std::lock_guard<std::mutex> lock(g_CustomGunBasicMutex);

    if (g_CustomGunBasicEntries.empty())
        return;

    LuaGetField(L, -1, "gunBasic");
    if (!LuaIsTable(L, -1))
    {
        LuaSetTop(L, -2);
        Log("[GunBasic] gunBasic table not found during reload\n");
        return;
    }

    const int gunBasicIndex = LuaGetTop(L);
    int rowCount = GetLuaArraySize(L, gunBasicIndex);

    for (const auto& entry : g_CustomGunBasicEntries)
    {
        bool found = false;

        for (int i = 1; i <= rowCount; ++i)
        {
            LuaPushNumber(L, static_cast<lua_Number>(i));
            LuaGetTable(L, gunBasicIndex);

            if (LuaIsTable(L, -1))
            {
                const int rowIndex = LuaGetTop(L);
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

            LuaSetTop(L, -2);

            if (found)
                break;
        }

        if (!found)
        {
            LuaCreateTable(L, 13, 0);
            const int rowIndex = LuaGetTop(L);

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
            LuaPushNumber(L, static_cast<lua_Number>(rowCount));
            LuaPushValue(L, rowIndex);
            LuaSetTable(L, gunBasicIndex);

            LuaSetTop(L, -2);

            Log("[GunBasic] Appended entry weaponId=0x%X\n", entry.weaponId);
        }
    }

    LuaSetTop(L, -2);
}

// Hooked stock ReloadEquipParameterTablesImpl2.
// Params: _this, L
static int __fastcall hkReloadEquipParameterTablesImpl2(void* _this, lua_State* L)
{
    if (_this)
    {
        std::lock_guard<std::mutex> lock(g_CustomGunBasicMutex);

        if (!RedirectGunBasicPointerToShadow(_this))
        {
            Log("[GunBasic] Failed to redirect gunBasic pointer to shadow buffer\n");
        }
    }

    if (L && LuaIsTable(L, -1))
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
    if (!L || !LuaIsTable(L, 1))
        return 0;

    LuaPushValue(L, 1);

    GunBasicEntry entry{};

    if (!ReadRequiredIntField(L, "weaponId", entry.weaponId) || entry.weaponId <= 0)
    {
        LuaSetTop(L, -2);
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

    if (entry.weaponGrade < 1)
        entry.weaponGrade = 1;
    else if (entry.weaponGrade > 15)
		entry.weaponGrade = 15;

    LuaSetTop(L, -2);

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

    if (!ResolveLuaApi())
    {
        Log("[GunBasic] Lua API resolve failed during install\n");
        return false;
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

    if (g_LastEquipParameterTablesImpl && g_StockGunBasicPtr)
    {
        void** ppGunBasic = reinterpret_cast<void**>(
            reinterpret_cast<std::uint8_t*>(g_LastEquipParameterTablesImpl) + kEquipParameterTablesImpl_GunBasicPtr_Offset);
        *ppGunBasic = g_StockGunBasicPtr;
    }

    g_OrigReloadEquipParameterTablesImpl2 = nullptr;
    g_ReloadEquipParameterTablesImpl2HookInstalled = false;
    g_LastEquipParameterTablesImpl = nullptr;

    Log("[GunBasic] ReloadEquipParameterTablesImpl2 hook removed\n");
    return true;
}
