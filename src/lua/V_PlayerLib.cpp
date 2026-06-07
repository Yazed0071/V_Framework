#include "pch.h"

extern "C" {
    #include "lua.h"
    #include "lauxlib.h"
}

#include <cstdint>

#include "LuaApi.h"
#include "V_PlayerLib.h"
#include "PlayerFunctions.h"

namespace
{
    static luaL_Reg g_VTppPlayerLib[] =
    {
        { "SetPlayerVoiceFpkPathForType",             l_SetPlayerVoiceFpkPathForType },
        { "ClearPlayerVoiceFpkPathForType",           l_ClearPlayerVoiceFpkPathForType },
        { "ClearAllPlayerVoiceFpkOverrides",          l_ClearAllPlayerVoiceFpkOverrides },

        { nullptr,          nullptr }
    };
}

bool Register_V_TppPlayerLibrary(lua_State* L)
{
    return RegisterLuaLibrary(L, "V_Player", g_VTppPlayerLib);
}
