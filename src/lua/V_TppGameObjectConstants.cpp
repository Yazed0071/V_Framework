#include "pch.h"

extern "C" {
    #include "lua.h"
}

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "V_TppGameObjectConstants.h"

// V_TppGameObject — a global table of numeric constants exposed to Lua.
//
// Self-contained: it resolves the few Lua functions it needs on its own (the same
// EN-bootstrap-fallback pattern as the other lua/ files), so the only touch-point in
// SetLuaFunctions.cpp is one call inside the existing hook. Add constants to kEntries.

namespace
{
    using lua_pushstring_t  = void(__fastcall*)(lua_State* L, char* s);
    using lua_pushnumber_t  = void(__fastcall*)(lua_State* L, lua_Number n);
    using lua_createtable_t = void(__fastcall*)(lua_State* L, int narr, int nrec);
    using lua_settable_t    = void(__fastcall*)(lua_State* L, int idx);

    static constexpr int LUA_GLOBALSINDEX_51 = -10002;

    // EN (mgsvtpp.exe, base 0x140000000) fallbacks — used only until the real
    // AddressSet is resolved. Identical to the values in SetLuaFunctions.cpp.
    static constexpr std::uintptr_t BOOTSTRAP_EN_lua_pushstring  = 0x14C1E7EE0ull;
    static constexpr std::uintptr_t BOOTSTRAP_EN_lua_pushnumber  = 0x141A11BC0ull;
    static constexpr std::uintptr_t BOOTSTRAP_EN_lua_createtable = 0x14C1D6320ull;
    static constexpr std::uintptr_t BOOTSTRAP_EN_lua_settable    = 0x14C1EB2B0ull;

    static lua_pushstring_t  g_lua_pushstring  = nullptr;
    static lua_pushnumber_t  g_lua_pushnumber  = nullptr;
    static lua_createtable_t g_lua_createtable = nullptr;
    static lua_settable_t    g_lua_settable    = nullptr;

    static std::uintptr_t PickAddr(std::uintptr_t resolved, std::uintptr_t bootstrap)
    {
        return resolved ? resolved : bootstrap;
    }

    static bool ResolveLuaApi()
    {
        if (!g_lua_pushstring)
            g_lua_pushstring = reinterpret_cast<lua_pushstring_t>(
                ResolveGameAddress(PickAddr(gAddr.lua_pushstring, BOOTSTRAP_EN_lua_pushstring)));

        if (!g_lua_pushnumber)
            g_lua_pushnumber = reinterpret_cast<lua_pushnumber_t>(
                ResolveGameAddress(PickAddr(gAddr.lua_pushnumber, BOOTSTRAP_EN_lua_pushnumber)));

        if (!g_lua_createtable)
            g_lua_createtable = reinterpret_cast<lua_createtable_t>(
                ResolveGameAddress(PickAddr(gAddr.lua_createtable, BOOTSTRAP_EN_lua_createtable)));

        if (!g_lua_settable)
            g_lua_settable = reinterpret_cast<lua_settable_t>(
                ResolveGameAddress(PickAddr(gAddr.lua_settable, BOOTSTRAP_EN_lua_settable)));

        return g_lua_pushstring && g_lua_pushnumber && g_lua_createtable && g_lua_settable;
    }

    struct ConstEntry { const char* name; lua_Number value; };

    // Add V_TppGameObject.* constants here (keep the { nullptr, 0.0 } terminator last).
    static const ConstEntry kEntries[] = {
        { nullptr, 0.0 },
    };
}

void Register_V_TppGameObjectConstants(lua_State* L)
{
    if (!L || !ResolveLuaApi())
        return;

    g_lua_pushstring(L, const_cast<char*>("V_TppGameObject"));
    g_lua_createtable(L, 0, 0);

    int registered = 0;
    for (const auto& e : kEntries)
    {
        if (!e.name) break;
        g_lua_pushstring(L, const_cast<char*>(e.name));
        g_lua_pushnumber(L, e.value);
        g_lua_settable(L, -3);
        ++registered;
    }

    g_lua_settable(L, LUA_GLOBALSINDEX_51);
    Log("[V_FrameWork] Registered global V_TppGameObject (%d constants)\n", registered);
}
