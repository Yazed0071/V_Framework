#include <pch.h>
#include "V_PlacedSystemLib.h"

#include <cstdint>

#include "LuaApi.h"
#include "MissionCodeGuard.h"
#include "log.h"
#include "../hooks/equip/PlacedImpl_ExecuteAfter.h"

namespace
{
    bool InOnlineMission(const char* function)
    {
        if (!MissionCodeGuard::ShouldBypassHooks())
            return false;
        LogWarn("%s: not available in online missions, ignored", function);
        return true;
    }

    bool HandleArg(lua_State* L, std::uint32_t& handle)
    {
        if (!LuaIsNumber(L, 1))
            return false;
        const int value = GetLuaInt(L, 1);
        if (value < 0)
            return false;
        handle = static_cast<std::uint32_t>(value);
        return true;
    }

    int l_GetPosition(lua_State* L)
    {
        std::uint32_t handle = 0;
        float position[3] = {};
        if (!HandleArg(L, handle) || !CustomPlaced::GetPosition(handle, position))
            return 0;
        g_lua_pushnumber(L, position[0]);
        g_lua_pushnumber(L, position[1]);
        g_lua_pushnumber(L, position[2]);
        return 3;
    }

    int QueueRequest(lua_State* L, const char* function, CustomPlaced::Request kind)
    {
        std::uint32_t handle = 0;
        if (InOnlineMission(function) || !HandleArg(L, handle) || !CustomPlaced::IsCustom(handle))
        {
            PushLuaBool(L, false);
            return 1;
        }
        CustomPlaced::Queue(kind, handle);
        PushLuaBool(L, true);
        return 1;
    }

    int l_Activate(lua_State* L) { return QueueRequest(L, "V_PlacedSystem.Activate", CustomPlaced::Request::Activate); }
    int l_Remove(lua_State* L)   { return QueueRequest(L, "V_PlacedSystem.Remove", CustomPlaced::Request::Remove); }

    luaL_Reg g_PlacedSystemLib[] =
    {
        { "GetPosition", l_GetPosition },
        { "Activate",    l_Activate },
        { "Remove",      l_Remove },
        { nullptr, nullptr }
    };
}

bool Register_V_PlacedSystemLibrary(lua_State* L)
{
    return RegisterLuaLibrary(L, "V_PlacedSystem", g_PlacedSystemLib);
}
