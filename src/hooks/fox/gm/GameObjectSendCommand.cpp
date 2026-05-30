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
#include "../../soldier/LostHostageHook.h"
#include "../../soldier/NoticeControllerImpl_GetOccasionalChat.h"
#include "../../../core/FoxHashes.h"

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
    using lua_toboolean_t  = int         (__fastcall*)(lua_State*, int);
    using lua_tointeger_t  = long long   (__fastcall*)(lua_State*, int);
    using lua_objlen_t     = size_t      (__fastcall*)(lua_State*, int);
    using lua_rawgeti_t    = void        (__fastcall*)(lua_State*, int, int);

    static lua_gettop_t     g_lua_gettop     = nullptr;
    static lua_settop_t     g_lua_settop     = nullptr;
    static lua_type_t       g_lua_type       = nullptr;
    static lua_pushstring_t g_lua_pushstring = nullptr;
    static lua_pushnumber_t g_lua_pushnumber = nullptr;
    static lua_gettable_t   g_lua_gettable   = nullptr;
    static lua_tolstring_t  g_lua_tolstring  = nullptr;
    static lua_tonumber_t   g_lua_tonumber   = nullptr;
    static lua_toboolean_t  g_lua_toboolean  = nullptr;
    static lua_tointeger_t  g_lua_tointeger  = nullptr;
    static lua_objlen_t     g_lua_objlen     = nullptr;
    static lua_rawgeti_t    g_lua_rawgeti    = nullptr;

    static bool ResolveLuaApi()
    {
        if (g_lua_gettop && g_lua_settop && g_lua_type && g_lua_pushstring &&
            g_lua_pushnumber && g_lua_gettable && g_lua_tolstring && g_lua_tonumber &&
            g_lua_toboolean && g_lua_tointeger)
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
        g_lua_toboolean  = reinterpret_cast<lua_toboolean_t>(ResolveGameAddress(gAddr.lua_toboolean));
        g_lua_tointeger  = reinterpret_cast<lua_tointeger_t>(ResolveGameAddress(gAddr.lua_tointeger));
        g_lua_objlen     = reinterpret_cast<lua_objlen_t>(ResolveGameAddress(gAddr.lua_objlen));
        g_lua_rawgeti    = reinterpret_cast<lua_rawgeti_t>(ResolveGameAddress(gAddr.lua_rawgeti));

        return g_lua_gettop && g_lua_settop && g_lua_type && g_lua_pushstring &&
               g_lua_pushnumber && g_lua_gettable && g_lua_tolstring && g_lua_tonumber &&
               g_lua_toboolean && g_lua_tointeger;
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

    static bool ReadCommandBool(lua_State* L, int cmdStackIdx, const char* key)
    {
        g_lua_pushstring(L, const_cast<char*>(key));
        g_lua_gettable(L, cmdStackIdx);
        bool v = false;
        const int t = g_lua_type(L, -1);
        if (t == LUA_TBOOLEAN)
            v = g_lua_toboolean(L, -1) != 0;
        else if (t == LUA_TNUMBER)
            v = static_cast<int>(g_lua_tonumber(L, -1)) != 0;
        return v;
    }

    static std::size_t ReadLabelArray(lua_State* L, int cmdStackIdx, std::uint32_t* out, const char* key)
    {
        std::size_t n = 0;

        if (g_lua_objlen && g_lua_rawgeti)
        {
            g_lua_pushstring(L, const_cast<char*>(key));
            g_lua_gettable(L, cmdStackIdx);
            if (g_lua_type(L, -1) == LUA_TTABLE)
            {
                const int tbl = g_lua_gettop(L);
                const std::size_t len = g_lua_objlen(L, tbl);
                for (std::size_t i = 1; i <= len && n < 255; ++i)
                {
                    g_lua_rawgeti(L, tbl, static_cast<int>(i));
                    const int et = g_lua_type(L, -1);
                    if (et == LUA_TNUMBER)
                        out[n++] = static_cast<std::uint32_t>(static_cast<long long>(g_lua_tonumber(L, -1)));
                    else if (et == LUA_TSTRING)
                    {
                        const char* s = g_lua_tolstring(L, -1, nullptr);
                        if (s) out[n++] = FoxHashes::StrCode32(s);
                    }
                    g_lua_settop(L, tbl);
                }
            }
        }

        return n;
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

        if (idStr == "SetSahelanPhase")
        {
            const std::int32_t phase =
                static_cast<std::int32_t>(ReadCommandNumber(L, 2, "phase"));
            g_lua_settop(L, top);
            ::Set_SahelanForcePhase(phase);
            Log("[SendCommand] SetSahelanPhase phase=%d\n", phase);
            return 0;
        }
        if (idStr == "GetSahelanPhase")
        {
            const double phase = static_cast<double>(::Get_SahelanCurrentPhase());
            g_lua_pushnumber(L, phase);
            return 1;
        }
        if (idStr == "SetEscapeState")
        {
            std::uint32_t gameObjectId = 0;
            if (g_lua_type(L, 1) == LUA_TNUMBER)
            {
                gameObjectId = static_cast<std::uint32_t>(
                    g_lua_tointeger(L, 1) & 0xFFFFFFFFLL);
            }
            const bool enable = ReadCommandBool(L, 2, "enable");
            g_lua_settop(L, top);
            ::PlayerTookHostage(gameObjectId, enable);
            Log("[SendCommand] SetEscapeState gameObjectId=0x%08X enable=%d\n",
                gameObjectId, enable ? 1 : 0);
            return 0;
        }
        if (idStr == "SetOccasionalChatList")
        {
            std::uint32_t labels[256];
            const std::size_t n = ReadLabelArray(L, 2, labels, "labels");
            g_lua_settop(L, top);
            ::SetOccasionalChatList(labels, n);
            Log("[SendCommand] SetOccasionalChatList count=%u\n", static_cast<unsigned>(n));
            return 0;
        }
        if (idStr == "InsertToOccasionalChatList")
        {
            std::uint32_t labels[256];
            const std::size_t n = ReadLabelArray(L, 2, labels, "labels");
            g_lua_settop(L, top);
            ::InsertToOccasionalChatList(labels, n);
            Log("[SendCommand] InsertToOccasionalChatList count=%u\n", static_cast<unsigned>(n));
            return 0;
        }
        if (idStr == "RemoveFromOccasionalChatList")
        {
            std::uint32_t labels[256];
            const std::size_t n = ReadLabelArray(L, 2, labels, "labels");
            g_lua_settop(L, top);
            ::RemoveFromOccasionalChatList(labels, n);
            Log("[SendCommand] RemoveFromOccasionalChatList count=%u\n", static_cast<unsigned>(n));
            return 0;
        }
        if (idStr == "ResetOccasionalChatList")
        {
            g_lua_settop(L, top);
            ::ClearOccasionalChatListOverride();
            Log("[SendCommand] ResetOccasionalChatList\n");
            return 0;
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
