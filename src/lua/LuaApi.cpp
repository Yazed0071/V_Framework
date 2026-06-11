#include "pch.h"
#include "LuaApi.h"

#include "AddressSet.h"
#include "FoxHashes.h"
#include "HookUtils.h"
#include "log.h"

FoxLuaRegisterLibrary_t g_FoxLuaRegisterLibrary = nullptr;
lua_tolstring_t    g_lua_tolstring    = nullptr;
lua_tointeger_t    g_lua_tointeger    = nullptr;
lua_tonumber_t     g_lua_tonumber     = nullptr;
lua_pushnumber_t   g_lua_pushnumber   = nullptr;
lua_toboolean_t    g_lua_toboolean    = nullptr;
lua_gettop_t       g_lua_gettop       = nullptr;
lua_settop_t       g_lua_settop       = nullptr;
lua_getfield_t     g_lua_getfield     = nullptr;
lua_rawgeti_t      g_lua_rawgeti      = nullptr;
lua_type_t         g_lua_type         = nullptr;
lua_isstring_t     g_lua_isstring     = nullptr;
lua_isnumber_t     g_lua_isnumber     = nullptr;
lua_objlen_t       g_lua_objlen       = nullptr;
lua_pushboolean_t  g_lua_pushboolean  = nullptr;
lua_pushstring_t   g_lua_pushstring   = nullptr;
lua_createtable_t  g_lua_createtable  = nullptr;
lua_rawset_t       g_lua_rawset       = nullptr;
lua_settable_t     g_lua_settable     = nullptr;
lua_pushnil_t      g_lua_pushnil      = nullptr;
lua_next_t         g_lua_next         = nullptr;
lua_gettable_t     g_lua_gettable     = nullptr;
lua_pushvalue_t    g_lua_pushvalue    = nullptr;
lua_pushcclosure_t g_lua_pushcclosure = nullptr;
lua_pcall_t        g_lua_pcall        = nullptr;

namespace
{
    constexpr uintptr_t BOOTSTRAP_EN_FoxLuaRegisterLibrary = 0x14006B6D0ull;
    constexpr uintptr_t BOOTSTRAP_EN_lua_tolstring    = 0x141A123C0ull;
    constexpr uintptr_t BOOTSTRAP_EN_lua_tointeger    = 0x141A12390ull;
    constexpr uintptr_t BOOTSTRAP_EN_lua_tonumber     = 0x141A12460ull;
    constexpr uintptr_t BOOTSTRAP_EN_lua_pushnumber   = 0x141A11BC0ull;
    constexpr uintptr_t BOOTSTRAP_EN_lua_toboolean    = 0x141A12330ull;
    constexpr uintptr_t BOOTSTRAP_EN_lua_gettop       = 0x14C1D7D40ull;
    constexpr uintptr_t BOOTSTRAP_EN_lua_settop       = 0x14C1EBBE0ull;
    constexpr uintptr_t BOOTSTRAP_EN_lua_getfield     = 0x14C1D7320ull;
    constexpr uintptr_t BOOTSTRAP_EN_lua_rawgeti      = 0x14C1E9320ull;
    constexpr uintptr_t BOOTSTRAP_EN_lua_type         = 0x14C1ED760ull;
    constexpr uintptr_t BOOTSTRAP_EN_lua_isstring     = 0x14C1D9250ull;
    constexpr uintptr_t BOOTSTRAP_EN_lua_isnumber     = 0x14C1D8C90ull;
    constexpr uintptr_t BOOTSTRAP_EN_lua_objlen       = 0x14C1DA960ull;
    constexpr uintptr_t BOOTSTRAP_EN_lua_pushboolean  = 0x14C1DB230ull;
    constexpr uintptr_t BOOTSTRAP_EN_lua_pushstring   = 0x14C1E7EE0ull;
    constexpr uintptr_t BOOTSTRAP_EN_lua_createtable  = 0x14C1D6320ull;
    constexpr uintptr_t BOOTSTRAP_EN_lua_rawset       = 0x14C1E9CF0ull;
    constexpr uintptr_t BOOTSTRAP_EN_lua_settable     = 0x14C1EB2B0ull;
    constexpr uintptr_t BOOTSTRAP_EN_lua_pushnil      = 0x14C1E7CC0ull;
    constexpr uintptr_t BOOTSTRAP_EN_lua_next         = 0x14C1DA770ull;
    constexpr uintptr_t BOOTSTRAP_EN_lua_gettable     = 0x14C1D7C10ull;
    constexpr uintptr_t BOOTSTRAP_EN_lua_pushvalue    = 0x14C1E87E0ull;
    constexpr uintptr_t BOOTSTRAP_EN_lua_pushcclosure = 0x14C1E67B0ull;
    constexpr uintptr_t BOOTSTRAP_EN_lua_pcall        = 0x141A11930ull;

    uintptr_t GetLuaBridgeAddress(uintptr_t resolvedAddr, uintptr_t bootstrapAddr)
    {
        return resolvedAddr ? resolvedAddr : bootstrapAddr;
    }
}

bool ResolveLuaApi()
{
    if (!g_FoxLuaRegisterLibrary)
        g_FoxLuaRegisterLibrary = reinterpret_cast<FoxLuaRegisterLibrary_t>(ResolveGameAddress(GetLuaBridgeAddress(gAddr.FoxLuaRegisterLibrary, BOOTSTRAP_EN_FoxLuaRegisterLibrary)));
    if (!g_lua_tolstring)
        g_lua_tolstring = reinterpret_cast<lua_tolstring_t>(ResolveGameAddress(GetLuaBridgeAddress(gAddr.lua_tolstring, BOOTSTRAP_EN_lua_tolstring)));
    if (!g_lua_tointeger)
        g_lua_tointeger = reinterpret_cast<lua_tointeger_t>(ResolveGameAddress(GetLuaBridgeAddress(gAddr.lua_tointeger, BOOTSTRAP_EN_lua_tointeger)));
    if (!g_lua_tonumber)
        g_lua_tonumber = reinterpret_cast<lua_tonumber_t>(ResolveGameAddress(GetLuaBridgeAddress(gAddr.lua_tonumber, BOOTSTRAP_EN_lua_tonumber)));
    if (!g_lua_toboolean)
        g_lua_toboolean = reinterpret_cast<lua_toboolean_t>(ResolveGameAddress(GetLuaBridgeAddress(gAddr.lua_toboolean, BOOTSTRAP_EN_lua_toboolean)));
    if (!g_lua_pushnumber)
        g_lua_pushnumber = reinterpret_cast<lua_pushnumber_t>(ResolveGameAddress(GetLuaBridgeAddress(gAddr.lua_pushnumber, BOOTSTRAP_EN_lua_pushnumber)));
    if (!g_lua_gettop)
        g_lua_gettop = reinterpret_cast<lua_gettop_t>(ResolveGameAddress(GetLuaBridgeAddress(gAddr.lua_gettop, BOOTSTRAP_EN_lua_gettop)));
    if (!g_lua_settop)
        g_lua_settop = reinterpret_cast<lua_settop_t>(ResolveGameAddress(GetLuaBridgeAddress(gAddr.lua_settop, BOOTSTRAP_EN_lua_settop)));
    if (!g_lua_getfield)
        g_lua_getfield = reinterpret_cast<lua_getfield_t>(ResolveGameAddress(GetLuaBridgeAddress(gAddr.lua_getfield, BOOTSTRAP_EN_lua_getfield)));
    if (!g_lua_rawgeti)
        g_lua_rawgeti = reinterpret_cast<lua_rawgeti_t>(ResolveGameAddress(GetLuaBridgeAddress(gAddr.lua_rawgeti, BOOTSTRAP_EN_lua_rawgeti)));
    if (!g_lua_type)
        g_lua_type = reinterpret_cast<lua_type_t>(ResolveGameAddress(GetLuaBridgeAddress(gAddr.lua_type, BOOTSTRAP_EN_lua_type)));
    if (!g_lua_isstring)
        g_lua_isstring = reinterpret_cast<lua_isstring_t>(ResolveGameAddress(GetLuaBridgeAddress(gAddr.lua_isstring, BOOTSTRAP_EN_lua_isstring)));
    if (!g_lua_isnumber)
        g_lua_isnumber = reinterpret_cast<lua_isnumber_t>(ResolveGameAddress(GetLuaBridgeAddress(gAddr.lua_isnumber, BOOTSTRAP_EN_lua_isnumber)));
    if (!g_lua_objlen)
        g_lua_objlen = reinterpret_cast<lua_objlen_t>(ResolveGameAddress(GetLuaBridgeAddress(gAddr.lua_objlen, BOOTSTRAP_EN_lua_objlen)));
    if (!g_lua_pushboolean)
        g_lua_pushboolean = reinterpret_cast<lua_pushboolean_t>(ResolveGameAddress(GetLuaBridgeAddress(gAddr.lua_pushboolean, BOOTSTRAP_EN_lua_pushboolean)));
    if (!g_lua_pushstring)
        g_lua_pushstring = reinterpret_cast<lua_pushstring_t>(ResolveGameAddress(GetLuaBridgeAddress(gAddr.lua_pushstring, BOOTSTRAP_EN_lua_pushstring)));
    if (!g_lua_createtable)
        g_lua_createtable = reinterpret_cast<lua_createtable_t>(ResolveGameAddress(GetLuaBridgeAddress(gAddr.lua_createtable, BOOTSTRAP_EN_lua_createtable)));
    if (!g_lua_rawset)
        g_lua_rawset = reinterpret_cast<lua_rawset_t>(ResolveGameAddress(GetLuaBridgeAddress(gAddr.lua_rawset, BOOTSTRAP_EN_lua_rawset)));
    if (!g_lua_settable)
        g_lua_settable = reinterpret_cast<lua_settable_t>(ResolveGameAddress(GetLuaBridgeAddress(gAddr.lua_settable, BOOTSTRAP_EN_lua_settable)));
    if (!g_lua_pushnil)
        g_lua_pushnil = reinterpret_cast<lua_pushnil_t>(ResolveGameAddress(GetLuaBridgeAddress(gAddr.lua_pushnil, BOOTSTRAP_EN_lua_pushnil)));
    if (!g_lua_next)
        g_lua_next = reinterpret_cast<lua_next_t>(ResolveGameAddress(GetLuaBridgeAddress(gAddr.lua_next, BOOTSTRAP_EN_lua_next)));
    if (!g_lua_gettable)
        g_lua_gettable = reinterpret_cast<lua_gettable_t>(ResolveGameAddress(GetLuaBridgeAddress(gAddr.lua_gettable, BOOTSTRAP_EN_lua_gettable)));
    if (!g_lua_pushvalue)
        g_lua_pushvalue = reinterpret_cast<lua_pushvalue_t>(ResolveGameAddress(GetLuaBridgeAddress(gAddr.lua_pushvalue, BOOTSTRAP_EN_lua_pushvalue)));
    if (!g_lua_pushcclosure)
        g_lua_pushcclosure = reinterpret_cast<lua_pushcclosure_t>(ResolveGameAddress(GetLuaBridgeAddress(gAddr.lua_pushcclosure, BOOTSTRAP_EN_lua_pushcclosure)));
    if (!g_lua_pcall)
        g_lua_pcall = reinterpret_cast<lua_pcall_t>(ResolveGameAddress(GetLuaBridgeAddress(gAddr.lua_pcall, BOOTSTRAP_EN_lua_pcall)));

    return g_FoxLuaRegisterLibrary &&
        g_lua_tolstring && g_lua_tointeger && g_lua_tonumber && g_lua_toboolean && g_lua_pushnumber &&
        g_lua_gettop && g_lua_settop && g_lua_getfield && g_lua_gettable && g_lua_rawgeti && g_lua_type &&
        g_lua_isstring && g_lua_isnumber && g_lua_objlen && g_lua_pushboolean && g_lua_pushvalue &&
        g_lua_pushstring && g_lua_createtable && g_lua_rawset && g_lua_settable && g_lua_pushnil && g_lua_next;
}

int GetLuaTop(lua_State* L)
{
    if (!ResolveLuaApi() || !g_lua_gettop)
        return 0;
    return g_lua_gettop(L);
}

const char* GetLuaString(lua_State* L, int idx)
{
    if (!ResolveLuaApi() || !g_lua_tolstring)
        return nullptr;
    return g_lua_tolstring(L, idx, nullptr);
}

int GetLuaInt(lua_State* L, int idx)
{
    if (!ResolveLuaApi() || !g_lua_tointeger)
        return 0;
    return static_cast<int>(g_lua_tointeger(L, idx));
}

std::uint64_t GetLuaInt64(lua_State* L, int idx)
{
    if (!ResolveLuaApi() || !g_lua_tointeger)
        return 0;
    return static_cast<std::uint64_t>(g_lua_tointeger(L, idx));
}

bool GetLuaBool(lua_State* L, int idx)
{
    if (!ResolveLuaApi() || !g_lua_toboolean)
        return false;
    return g_lua_toboolean(L, idx) != 0;
}

float GetLuaNumber(lua_State* L, int idx)
{
    if (!ResolveLuaApi() || !g_lua_tonumber)
        return 0.0f;
    return static_cast<float>(g_lua_tonumber(L, idx));
}

int LuaType(lua_State* L, int idx)
{
    if (!ResolveLuaApi() || !g_lua_type)
        return -1;
    return g_lua_type(L, idx);
}

bool LuaIsString(lua_State* L, int idx)
{
    if (!ResolveLuaApi() || !g_lua_isstring)
        return false;
    return g_lua_isstring(L, idx) != 0;
}

bool LuaIsNumber(lua_State* L, int idx)
{
    if (!ResolveLuaApi() || !g_lua_isnumber)
        return false;
    return g_lua_isnumber(L, idx) != 0;
}

size_t LuaObjLen(lua_State* L, int idx)
{
    if (!ResolveLuaApi() || !g_lua_objlen)
        return 0;
    return g_lua_objlen(L, idx);
}

std::uint32_t GetLuaStrCode32Arg(lua_State* L, int idx)
{
    if (GetLuaTop(L) < idx)
        return 0u;
    if (LuaIsNumber(L, idx))
        return static_cast<std::uint32_t>(GetLuaInt64(L, idx));
    if (LuaIsString(L, idx))
    {
        const char* s = GetLuaString(L, idx);
        if (!s || !s[0])
            return 0u;
        return FoxHashes::StrCode32(s);
    }
    return 0u;
}

std::uint32_t GetLuaFnvHash32Arg(lua_State* L, int idx)
{
    if (GetLuaTop(L) < idx)
        return 0u;
    if (LuaIsNumber(L, idx))
        return static_cast<std::uint32_t>(GetLuaInt64(L, idx));
    if (LuaIsString(L, idx))
    {
        const char* s = GetLuaString(L, idx);
        if (!s || !s[0])
            return 0u;
        return FoxHashes::FNVHash32(s);
    }
    return 0u;
}

void PushLuaBool(lua_State* L, bool value)
{
    if (!ResolveLuaApi() || !g_lua_pushboolean)
        return;
    g_lua_pushboolean(L, value ? 1 : 0);
}

void PushLuaNumber(lua_State* L, float value)
{
    if (!ResolveLuaApi() || !g_lua_pushnumber)
        return;
    g_lua_pushnumber(L, static_cast<lua_Number>(value));
}

void PushLuaString(lua_State* L, const char* s)
{
    if (!ResolveLuaApi() || !g_lua_pushstring)
        return;
    g_lua_pushstring(L, const_cast<char*>(s ? s : ""));
}

void PushLuaNil(lua_State* L)
{
    if (!ResolveLuaApi() || !g_lua_pushnil)
        return;
    g_lua_pushnil(L);
}

void LuaPop(lua_State* L, int count)
{
    if (!ResolveLuaApi() || !g_lua_settop)
        return;
    g_lua_settop(L, -count - 1);
}

void LuaGetField(lua_State* L, int idx, const char* fieldName)
{
    if (!ResolveLuaApi() || !g_lua_getfield || !fieldName)
        return;
    g_lua_getfield(L, idx, const_cast<char*>(fieldName));
}

void LuaRawGetI(lua_State* L, int idx, int n)
{
    if (!ResolveLuaApi() || !g_lua_rawgeti)
        return;
    g_lua_rawgeti(L, idx, n);
}

bool RegisterLuaLibrary(lua_State* L, const char* libName, luaL_Reg* funcs)
{
    if (!ResolveLuaApi() || !L || !libName || !funcs)
        return false;
    g_FoxLuaRegisterLibrary(L, libName, funcs);
    Log("[V_FrameWork] Registered library: %s (L=%p)\n", libName, L);
    return true;
}
