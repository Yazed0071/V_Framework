#include "pch.h"

extern "C" {
    #include "lua.h"
    #include "lauxlib.h"
}

#include <cstdint>
#include <string>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "V_FoxLib.h"
#include "FoxFunctions.h"
#include "LuaApi.h"
#include "FoxPath.h"
#include "../hooks/outfit/HeadMotion.h"
#include "../hooks/player/AccSeatIdleMotion.h"

namespace
{
    static int __cdecl hk_FoxLog(lua_State*)
    {
        return 0;
    }

    static bool RebindFoxLog(lua_State* L)
    {
        if (!L || !ResolveLuaApi())
            return false;
        const int top = g_lua_gettop(L);
        g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("Fox"));
        const int foxType = g_lua_type(L, -1);
        bool ok = false;
        if (foxType == LUA_TTABLE)
        {
            g_lua_pushstring(L, const_cast<char*>("Log"));
            g_lua_pushcclosure(L, &hk_FoxLog, 0);
            g_lua_rawset(L, -3);
            ok = true;
        }
        g_lua_settop(L, top);
        if (!ok)
            LogWarn("ShowConsole: 'Fox' global is type %d, not a table; Fox.Log not "
                    "re-bound", foxType);
        return ok;
    }

    static int __cdecl l_ShowConsole(lua_State* L)
    {
        const bool enable = GetLuaBool(L, 1) != 0;
        if (enable)
        {
            EnsureConsole();
            if (HWND hwnd = GetConsoleWindow())
                ShowWindow(hwnd, SW_SHOW);
            RebindFoxLog(L);
        }
        else if (HWND hwnd = GetConsoleWindow())
        {
            ShowWindow(hwnd, SW_HIDE);
        }
        return 0;
    }

    std::uint64_t GaniArg(lua_State* L, int index, const char* function)
    {
        const char* text = (GetLuaTop(L) >= index && LuaIsString(L, index)) ? GetLuaString(L, index) : nullptr;
        if (!text || !*text)
            return 0;
        std::string path = text;
        const std::size_t slash = path.find_last_of('/');
        if (slash != std::string::npos && path.find('.', slash) == std::string::npos)
            path += ".gani";
        const std::uint64_t clip = outfit::headmotion::ParseClipId(path.c_str());
        if (!clip)
            LogWarn("%s: '%s' is not a clip path or id, ignored", function, text);
        return clip;
    }

    static int __cdecl l_SwapGani(lua_State* L)
    {
        const std::uint64_t from = GaniArg(L, 1, "SwapGani");
        const std::uint64_t to = GaniArg(L, 2, "SwapGani");
        PushLuaBool(L, from && to && SwapGani(from, to));
        return 1;
    }

    static int __cdecl l_RestoreGani(lua_State* L)
    {
        const std::uint64_t from = GaniArg(L, 1, "RestoreGani");
        PushLuaBool(L, from && RestoreGani(from, GaniArg(L, 2, "RestoreGani")));
        return 1;
    }

    static luaL_Reg g_VFoxLib[] =
    {
        { "FNVHash32",                  l_FNVHash32 },
        { "ShowConsole",                l_ShowConsole },
        { "SwapGani",                   l_SwapGani },
        { "RestoreGani",                l_RestoreGani },

        { nullptr,          nullptr }
    };
}

bool Register_V_FoxLibrary(lua_State* L)
{
    return RegisterLuaLibrary(L, "V_Fox", g_VFoxLib);
}
