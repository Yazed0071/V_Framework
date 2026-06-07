#include "pch.h"

extern "C" {
    #include "lua.h"
    #include "lauxlib.h"
}

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "V_TppSecurityCameraLib.h"
#include "SecurityCameraFunctions.h"
#include "LuaApi.h"


namespace
{
    static luaL_Reg g_VTppSecurityCameraLib[] =
    {
        { "ClearSecurityCameraFova",                  l_ClearSecurityCameraFova },
        { "ClearAllSecurityCameraFovas",              l_ClearAllSecurityCameraFovas },

        { nullptr,          nullptr }
    };
}

bool Register_V_TppSecurityCameraLibrary(lua_State* L)
{
    return RegisterLuaLibrary(L, "V_SecurityCamera", g_VTppSecurityCameraLib);
}
