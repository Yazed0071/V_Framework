#include "pch.h"

extern "C" {
    #include "lua.h"
    #include "lauxlib.h"
}

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "V_TppSahelanLib.h"
#include "SahelanFunctions.h"
#include "LuaApi.h"


namespace
{
    static luaL_Reg g_VTppSahelanLib[] =
    {
        { "SetSahelanFova",                           l_SetSahelanFova },
        { "ClearSahelanFova",                         l_ClearSahelanFova },
        { "SetEyeLampColor",                          l_SetEyeLampColor },
        { "ClearEyeLampColor",                        l_ClearEyeLampColor },
        { "SetEyeLampDisco",                          l_SetEyeLampDisco },
        { "SetHeartLightColor",                       l_SetHeartLightColor },
        { "ClearHeartLightColor",                     l_ClearHeartLightColor },
        { "SetEyeLampColorLogging",                   l_SetEyeLampColorLogging },

        { nullptr,          nullptr }
    };
}

bool Register_V_TppSahelanLibrary(lua_State* L)
{
    return RegisterLuaLibrary(L, "V_Sahelan", g_VTppSahelanLib);
}
