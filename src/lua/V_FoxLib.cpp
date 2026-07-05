#include "pch.h"

extern "C" {
    #include "lua.h"
    #include "lauxlib.h"
}

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "V_FoxLib.h"
#include "FoxFunctions.h"
#include <TppPickableRuntime.h>
#include "LuaApi.h"
#include "FoxPath.h"


namespace
{
    static int __cdecl l_SetPickableCountRawByIndex(lua_State* L)
    {
        const int locatorIndex = GetLuaInt(L, 1);
        const int countRaw = GetLuaInt(L, 2);

        const bool ok = Set_TppPickableCountRawByIndex(
            static_cast<std::uint32_t>(locatorIndex),
            static_cast<std::uint32_t>(countRaw));

        PushLuaBool(L, ok);
        return 1;
    }


    static int __cdecl l_GetPickableCountRawByIndex(lua_State* L)
    {
        const int locatorIndex = GetLuaInt(L, 1);

        std::uint16_t countRaw = 0;
        const bool ok = Get_TppPickableCountRawByIndex(
            static_cast<std::uint32_t>(locatorIndex),
            countRaw);

        if (!ok)
        {
            PushLuaBool(L, false);
            return 1;
        }

        PushLuaNumber(L, static_cast<float>(countRaw));
        return 1;
    }
    static int __cdecl l_PathExists(lua_State* L)
    {
        const char* path = GetLuaString(L, 1);
        PushLuaBool(L, fox::PathExists(path));
        return 1;
    }

    static luaL_Reg g_VFoxLib[] =
    {
        { "FNVHash32",                  l_FNVHash32 },
        { "SetPickableCountRawByIndex", l_SetPickableCountRawByIndex },
        { "GetPickableCountRawByIndex", l_GetPickableCountRawByIndex },
        { "PathExists",                 l_PathExists },

        { nullptr,          nullptr }
    };
}

bool Register_V_FoxLibrary(lua_State* L)
{
    return RegisterLuaLibrary(L, "V_Fox", g_VFoxLib);
}
