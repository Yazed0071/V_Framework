#include "pch.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

#include "HookUtils.h"
#include "log.h"
#include "AddressSet.h"
#include "`anonymous_namespace'_RegisterConstantEquipId.h"

extern "C"
{
    #include "lua.h"
    #include "lauxlib.h"
    #include "lualib.h"
}

namespace
{
    // Stock function for registering built-in EquipId constants.
    // Params: lua_State* L
    using RegisterConstantEquipId_t = void(__fastcall*)(lua_State* L);

    // Stock FOX hash helper.
    // Params: const char* str
    using FoxStrHash32_t = std::uint32_t(__fastcall*)(const char* str);

    // Lua helpers used by this hook.
    using lua_getfield_t = void(__fastcall*)(lua_State* L, int idx, char* k);
    using lua_type_t = int(__fastcall*)(lua_State* L, int idx);
    using lua_pushstring_t = void(__fastcall*)(lua_State* L, const char* s);
    using lua_createtable_t = void(__fastcall*)(lua_State* L, int narr, int nrec);
    using lua_rawset_t = void(__fastcall*)(lua_State* L, int idx);
    using lua_pushvalue_t = void(__fastcall*)(lua_State* L, int idx);
    using lua_pushlstring_t = void(__fastcall*)(lua_State* L, const char* s, size_t len);
    using lua_pushnumber_t = void(__fastcall*)(lua_State* L, lua_Number n);
    using lua_settable_t = void(__fastcall*)(lua_State* L, int idx);
    using lua_settop_t = void(__fastcall*)(lua_State* L, int idx);

    // First safe dynamic custom EquipId after the stock range you dumped.
    static constexpr std::uint32_t kFirstDynamicEquipId = 0x609u;

    static RegisterConstantEquipId_t g_OrigRegisterConstantEquipId = nullptr;
    static FoxStrHash32_t g_FoxStrHash32 = nullptr;

    static lua_getfield_t g_lua_getfield = nullptr;
    static lua_type_t g_lua_type = nullptr;
    static lua_pushstring_t g_lua_pushstring = nullptr;
    static lua_createtable_t g_lua_createtable = nullptr;
    static lua_rawset_t g_lua_rawset = nullptr;
    static lua_pushvalue_t g_lua_pushvalue = nullptr;
    static lua_pushlstring_t g_lua_pushlstring = nullptr;
    static lua_pushnumber_t g_lua_pushnumber = nullptr;
    static lua_settable_t g_lua_settable = nullptr;
    static lua_settop_t g_lua_settop = nullptr;

    static bool g_RegisterConstantEquipIdHookInstalled = false;

    // Protects the custom registry.
    static std::mutex g_CustomEquipMutex;

    // Name -> EquipId
    static std::unordered_map<std::string, std::uint32_t> g_CustomEquipNameToId;

    // EquipId -> Name
    static std::unordered_map<std::uint32_t, std::string> g_CustomEquipIdToName;

    // Next dynamic candidate.
    static std::uint32_t g_NextDynamicEquipId = kFirstDynamicEquipId;
}

// Resolves the Lua helpers and hash function used by this hook.
// Params: none
static bool ResolveRegisterConstantEquipIdApi()
{
    if (!g_FoxStrHash32)
    {
        g_FoxStrHash32 = reinterpret_cast<FoxStrHash32_t>(
            ResolveGameAddress(gAddr.FoxStrHash32));
    }

    if (!g_lua_getfield)
    {
        g_lua_getfield = reinterpret_cast<lua_getfield_t>(
            ResolveGameAddress(gAddr.lua_getfield));
    }

    if (!g_lua_type)
    {
        g_lua_type = reinterpret_cast<lua_type_t>(
            ResolveGameAddress(gAddr.lua_type));
    }

    if (!g_lua_pushstring)
    {
        g_lua_pushstring = reinterpret_cast<lua_pushstring_t>(
            ResolveGameAddress(gAddr.lua_pushstring));
    }

    if (!g_lua_createtable)
    {
        g_lua_createtable = reinterpret_cast<lua_createtable_t>(
            ResolveGameAddress(gAddr.lua_createtable));
    }

    if (!g_lua_rawset)
    {
        g_lua_rawset = reinterpret_cast<lua_rawset_t>(
            ResolveGameAddress(gAddr.lua_rawset));
    }

    if (!g_lua_pushvalue)
    {
        g_lua_pushvalue = reinterpret_cast<lua_pushvalue_t>(
            ResolveGameAddress(gAddr.lua_pushvalue));
    }

    if (!g_lua_pushlstring)
    {
        g_lua_pushlstring = reinterpret_cast<lua_pushlstring_t>(
            ResolveGameAddress(gAddr.lua_pushlstring));
    }

    if (!g_lua_pushnumber)
    {
        g_lua_pushnumber = reinterpret_cast<lua_pushnumber_t>(
            ResolveGameAddress(gAddr.lua_pushnumber));
    }

    if (!g_lua_settable)
    {
        g_lua_settable = reinterpret_cast<lua_settable_t>(
            ResolveGameAddress(gAddr.lua_settable));
    }

    if (!g_lua_settop)
    {
        g_lua_settop = reinterpret_cast<lua_settop_t>(
            ResolveGameAddress(gAddr.lua_settop));
    }

    return g_FoxStrHash32 &&
        g_lua_getfield &&
        g_lua_type &&
        g_lua_pushstring &&
        g_lua_createtable &&
        g_lua_rawset &&
        g_lua_pushvalue &&
        g_lua_pushlstring &&
        g_lua_pushnumber &&
        g_lua_settable &&
        g_lua_settop;
}

// Returns the EquipId hash table base used by RegisterConstantEquipId.
// Params: none
static std::uint32_t* GetEquipIdHashTable()
{
    void* ptr = ResolveGameAddress(gAddr.EquipIdHashTable);
    return reinterpret_cast<std::uint32_t*>(ptr);
}

// Converts an EquipId into the hash-table slot used by RegisterConstantEquipId.
// Params: equipId, outIndex
static bool TryMapEquipIdToHashIndex(std::uint32_t equipId, std::uint32_t& outIndex)
{
    if (equipId < 0x400u)
    {
        outIndex = equipId;
        return true;
    }

    if (equipId < 0x500u)
    {
        outIndex = equipId - 0x99u;
        return true;
    }

    if (equipId < 0x600u)
    {
        outIndex = equipId - 0x149u;
        return true;
    }

    outIndex = equipId - 0x223u;
    return true;
}

// Returns true if the constant name looks like an EquipId constant.
// Params: equipName
static bool IsValidEquipConstantName(const char* equipName)
{
    if (!equipName || !equipName[0])
        return false;

    return std::strncmp(equipName, "EQP_", 4) == 0;
}

// Returns true if this EquipId is already reserved by the custom registry.
// Params: equipId
static bool IsCustomEquipIdReserved(std::uint32_t equipId)
{
    return g_CustomEquipIdToName.find(equipId) != g_CustomEquipIdToName.end();
}

// Returns an existing custom EquipId for a name if it was already registered.
// Params: equipName, outEquipId
static bool TryFindExistingCustomEquipId(const char* equipName, std::uint32_t& outEquipId)
{
    const auto it = g_CustomEquipNameToId.find(equipName);
    if (it == g_CustomEquipNameToId.end())
        return false;

    outEquipId = it->second;
    return true;
}

// Allocates the next free dynamic custom EquipId.
// Params: outEquipId
static bool TryAllocateDynamicCustomEquipId(std::uint32_t& outEquipId)
{
    std::uint32_t candidate = g_NextDynamicEquipId;

    for (;;)
    {
        if (!IsCustomEquipIdReserved(candidate))
        {
            outEquipId = candidate;
            g_NextDynamicEquipId = candidate + 1u;
            return true;
        }

        ++candidate;
    }
}

// Returns an existing custom id for the name or allocates a new dynamic one.
// Params: equipName, outEquipId
static bool GetOrCreateCustomEquipId(const char* equipName, std::uint32_t& outEquipId)
{
    std::lock_guard<std::mutex> lock(g_CustomEquipMutex);

    if (TryFindExistingCustomEquipId(equipName, outEquipId))
        return true;

    if (!TryAllocateDynamicCustomEquipId(outEquipId))
        return false;

    g_CustomEquipNameToId[equipName] = outEquipId;
    g_CustomEquipIdToName[outEquipId] = equipName;

    Log("[RegisterConstantEquipId] Allocated custom equip '%s' => 0x%X (%u)\n",
        equipName,
        outEquipId,
        outEquipId);

    return true;
}

// Ensures TppEquip exists and leaves it on the top of the Lua stack.
// Params: L
static bool EnsureTppEquipTable(lua_State* L)
{
    if (!ResolveRegisterConstantEquipIdApi() || !L)
        return false;

    g_lua_getfield(L, LUA_GLOBALSINDEX, const_cast<char*>("TppEquip"));

    if (g_lua_type(L, -1) == LUA_TTABLE)
        return true;

    g_lua_settop(L, -2);
    g_lua_pushstring(L, "TppEquip");
    g_lua_createtable(L, 0, 0);
    g_lua_rawset(L, LUA_GLOBALSINDEX);
    g_lua_getfield(L, LUA_GLOBALSINDEX, const_cast<char*>("TppEquip"));

    return g_lua_type(L, -1) == LUA_TTABLE;
}

// Applies one custom constant to TppEquip and to the internal hash table.
// Params: L, equipName, equipId
static bool ApplyCustomEquipId(lua_State* L, const char* equipName, std::uint32_t equipId)
{
    if (!ResolveRegisterConstantEquipIdApi() || !L || !equipName || !equipName[0])
        return false;

    if (!EnsureTppEquipTable(L))
        return false;

    const size_t nameLen = std::strlen(equipName);

    // TppEquip[equipName] = equipId
    g_lua_pushvalue(L, -1);
    g_lua_pushlstring(L, equipName, nameLen);
    g_lua_pushnumber(L, static_cast<lua_Number>(equipId));
    g_lua_settable(L, -3);
    g_lua_settop(L, -2);

    std::uint32_t* hashTable = GetEquipIdHashTable();
    if (!hashTable || !g_FoxStrHash32)
    {
        g_lua_settop(L, -2);
        return false;
    }

    std::uint32_t hashIndex = 0;
    if (!TryMapEquipIdToHashIndex(equipId, hashIndex))
    {
        g_lua_settop(L, -2);
        return false;
    }

    const std::uint32_t hashValue = g_FoxStrHash32(equipName);
    hashTable[hashIndex] = hashValue;

    Log("[RegisterConstantEquipId] Applied custom equip '%s' => 0x%X (hashIndex=0x%X hash=0x%08X)\n",
        equipName,
        equipId,
        hashIndex,
        hashValue);

    g_lua_settop(L, -2);
    return true;
}

// Re-applies all registered custom constants after the stock function runs.
// Params: L
static void ApplyAllCustomEquipIds(lua_State* L)
{
    std::lock_guard<std::mutex> lock(g_CustomEquipMutex);

    for (const auto& pair : g_CustomEquipNameToId)
    {
        ApplyCustomEquipId(L, pair.first.c_str(), pair.second);
    }
}

// Hooked stock RegisterConstantEquipId.
// Params: L
static void __fastcall hkRegisterConstantEquipId(lua_State* L)
{
    if (g_OrigRegisterConstantEquipId)
        g_OrigRegisterConstantEquipId(L);

    ApplyAllCustomEquipIds(L);
}

// Registers one custom EquipId from Lua and applies it immediately.
// Params: L, equipName, outEquipId
bool Register_CustomEquipId_FromLua(lua_State* L, const char* equipName, std::uint32_t& outEquipId)
{
    outEquipId = 0;

    if (!IsValidEquipConstantName(equipName))
        return false;

    if (!ResolveRegisterConstantEquipIdApi())
        return false;

    if (!GetOrCreateCustomEquipId(equipName, outEquipId))
        return false;

    return ApplyCustomEquipId(L, equipName, outEquipId);
}

// Installs the RegisterConstantEquipId hook.
// Params: none
bool Install_RegisterConstantEquipId_Hook()
{
    if (g_RegisterConstantEquipIdHookInstalled)
    {
        Log("[RegisterConstantEquipId] already installed\n");
        return true;
    }

    void* target = ResolveGameAddress(gAddr.RegisterConstantEquipId);
    if (!target)
    {
        Log("[RegisterConstantEquipId] target resolve failed\n");
        return false;
    }

    if (!CreateAndEnableHook(
        target,
        reinterpret_cast<void*>(&hkRegisterConstantEquipId),
        reinterpret_cast<void**>(&g_OrigRegisterConstantEquipId)))
    {
        Log("[RegisterConstantEquipId] hook install failed\n");
        return false;
    }

    g_RegisterConstantEquipIdHookInstalled = true;
    Log("[RegisterConstantEquipId] hook installed\n");
    return true;
}

// Uninstalls the RegisterConstantEquipId hook.
// Params: none
bool Uninstall_RegisterConstantEquipId_Hook()
{
    if (!g_RegisterConstantEquipIdHookInstalled)
        return true;

    void* target = ResolveGameAddress(gAddr.RegisterConstantEquipId);
    if (target)
        DisableAndRemoveHook(target);

    g_OrigRegisterConstantEquipId = nullptr;
    g_RegisterConstantEquipIdHookInstalled = false;

    Log("[RegisterConstantEquipId] hook removed\n");
    return true;
}