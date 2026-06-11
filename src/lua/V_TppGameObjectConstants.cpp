#include "pch.h"

extern "C" {
    #include "lua.h"
}

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "V_TppGameObjectConstants.h"
#include "LuaApi.h"


namespace
{

    struct ConstEntry { const char* name; lua_Number value; };

    static const ConstEntry kEntries[] = {
        {"GAME_OVER_GENERAL", 0},
        {"GAME_OVER_PARADOX", 1},
        {"GAME_OVER_STEALTH", 2},
        {"GAME_OVER_CYPRUS",  3},
        { nullptr, 0.0 }
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
