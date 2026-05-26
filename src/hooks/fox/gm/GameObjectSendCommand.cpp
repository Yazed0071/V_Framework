#include "pch.h"
#include "GameObjectSendCommand.h"

extern "C" {
    #include "lua.h"
}

#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <string>

#include "MinHook.h"
#include "log.h"
#include "../../../core/AddressSet.h"
#include "../../../core/HookUtils.h"
#include "../../sahelan/PhaseSneakAiImpl_PreUpdate.h"

namespace
{
    using lua_gettop_t     = int         (__fastcall*)(lua_State*);
    using lua_settop_t     = void        (__fastcall*)(lua_State*, int);
    using lua_type_t       = int         (__fastcall*)(lua_State*, int);
    using lua_pushstring_t = void        (__fastcall*)(lua_State*, char*);
    using lua_pushnumber_t = void        (__fastcall*)(lua_State*, lua_Number);
    using lua_gettable_t   = void        (__fastcall*)(lua_State*, int);
    using lua_tolstring_t  = const char* (__fastcall*)(lua_State*, int, size_t*);
    using lua_tonumber_t   = lua_Number  (__fastcall*)(lua_State*, int);

    static lua_gettop_t     g_lua_gettop     = nullptr;
    static lua_settop_t     g_lua_settop     = nullptr;
    static lua_type_t       g_lua_type       = nullptr;
    static lua_pushstring_t g_lua_pushstring = nullptr;
    static lua_pushnumber_t g_lua_pushnumber = nullptr;
    static lua_gettable_t   g_lua_gettable   = nullptr;
    static lua_tolstring_t  g_lua_tolstring  = nullptr;
    static lua_tonumber_t   g_lua_tonumber   = nullptr;

    static bool ResolveLuaApi()
    {
        if (g_lua_gettop && g_lua_settop && g_lua_type && g_lua_pushstring &&
            g_lua_pushnumber && g_lua_gettable && g_lua_tolstring && g_lua_tonumber)
        {
            return true;
        }

        g_lua_gettop     = reinterpret_cast<lua_gettop_t>(ResolveGameAddress(gAddr.lua_gettop));
        g_lua_settop     = reinterpret_cast<lua_settop_t>(ResolveGameAddress(gAddr.lua_settop));
        g_lua_type       = reinterpret_cast<lua_type_t>(ResolveGameAddress(gAddr.lua_type));
        g_lua_pushstring = reinterpret_cast<lua_pushstring_t>(ResolveGameAddress(gAddr.lua_pushstring));
        g_lua_pushnumber = reinterpret_cast<lua_pushnumber_t>(ResolveGameAddress(gAddr.lua_pushnumber));
        g_lua_gettable   = reinterpret_cast<lua_gettable_t>(ResolveGameAddress(gAddr.lua_gettable));
        g_lua_tolstring  = reinterpret_cast<lua_tolstring_t>(ResolveGameAddress(gAddr.lua_tolstring));
        g_lua_tonumber   = reinterpret_cast<lua_tonumber_t>(ResolveGameAddress(gAddr.lua_tonumber));

        return g_lua_gettop && g_lua_settop && g_lua_type && g_lua_pushstring &&
               g_lua_pushnumber && g_lua_gettable && g_lua_tolstring && g_lua_tonumber;
    }

    using GameObjectSendCommand_t = int (__fastcall*)(lua_State* L);

    static GameObjectSendCommand_t g_OrigSendCommand = nullptr;
    static bool                    g_Installed       = false;

    static const char* ReadCommandId(lua_State* L, int cmdStackIdx, std::string* out)
    {
        out->clear();
        g_lua_pushstring(L, const_cast<char*>("id"));
        g_lua_gettable(L, cmdStackIdx);
        if (g_lua_type(L, -1) != LUA_TSTRING) return nullptr;
        const char* s = g_lua_tolstring(L, -1, nullptr);
        if (!s) return nullptr;
        *out = s;
        return out->c_str();
    }

    static double ReadCommandNumber(lua_State* L, int cmdStackIdx, const char* key)
    {
        g_lua_pushstring(L, const_cast<char*>(key));
        g_lua_gettable(L, cmdStackIdx);
        double v = 0.0;
        if (g_lua_type(L, -1) == LUA_TNUMBER)
            v = static_cast<double>(g_lua_tonumber(L, -1));
        return v;
    }

    static int __fastcall hk_SendCommand(lua_State* L)
    {
        if (!g_OrigSendCommand) return 0;
        if (!ResolveLuaApi()) return g_OrigSendCommand(L);

        const int top = g_lua_gettop(L);
        if (top < 2 || g_lua_type(L, 2) != LUA_TTABLE)
            return g_OrigSendCommand(L);

        std::string idStr;
        const char* id = ReadCommandId(L, 2, &idStr);
        if (!id || !*id)
        {
            g_lua_settop(L, top);
            return g_OrigSendCommand(L);
        }
        g_lua_settop(L, top);

        if (idStr == "V_SetSahelanPhase")
        {
            const std::int32_t phase =
                static_cast<std::int32_t>(ReadCommandNumber(L, 2, "phase"));
            g_lua_settop(L, top);
            ::Set_SahelanForcePhase(phase);
            Log("[SendCommand] V_SetSahelanPhase phase=%d\n", phase);
            return 0;
        }
        if (idStr == "V_ClearSahelanPhase")
        {
            ::Clear_SahelanForcePhase();
            Log("[SendCommand] V_ClearSahelanPhase\n");
            return 0;
        }
        if (idStr == "V_GetSahelanPhase")
        {
            const double phase = static_cast<double>(::Get_SahelanCurrentPhase());
            g_lua_pushnumber(L, phase);
            return 1;
        }

        return g_OrigSendCommand(L);
    }
}

bool Install_GameObjectSendCommand_Hook()
{
    if (g_Installed) return true;

    if (!gAddr.GameObject_SendCommand)
    {
        Log("[GameObjectSendCommand] address is 0 (unsupported build)\n");
        return false;
    }

    void* target = ResolveGameAddress(gAddr.GameObject_SendCommand);
    if (!target)
    {
        Log("[GameObjectSendCommand] resolve failed\n");
        return false;
    }

    if (!ResolveLuaApi())
    {
        Log("[GameObjectSendCommand] lua API resolve failed; aborting install\n");
        return false;
    }

    const bool ok = CreateAndEnableHook(
        target,
        reinterpret_cast<void*>(&hk_SendCommand),
        reinterpret_cast<void**>(&g_OrigSendCommand));

    if (ok)
    {
        g_Installed = true;
        Log("[GameObjectSendCommand] hook installed @ %p (orig=%p)\n",
            target, reinterpret_cast<void*>(g_OrigSendCommand));
    }
    else
    {
        Log("[GameObjectSendCommand] hook install FAILED @ %p\n", target);
    }
    return ok;
}

bool Uninstall_GameObjectSendCommand_Hook()
{
    if (!g_Installed) return true;
    void* target = ResolveGameAddress(gAddr.GameObject_SendCommand);
    if (target) DisableAndRemoveHook(target);
    g_OrigSendCommand = nullptr;
    g_Installed       = false;
    return true;
}
