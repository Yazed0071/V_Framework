#include <pch.h>
#include "FrameCallbacks.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "LuaApi.h"
#include "LuaBroadcaster.h"
#include "log.h"

namespace
{
    constexpr int         LUA_REGISTRYINDEX_51 = -10000;
    constexpr const char* kRegistryKey = "V_FrameWork.FrameCallbacks";
    constexpr int         kSlotsNeeded = 6;
    constexpr ULONGLONG   kFallbackFrameMs = 15;   // no UI frame count (UI hooks off): about once per 60 Hz frame

    std::mutex               g_Lock;
    std::vector<std::string> g_Names;   // call order = order of first Set
    lua_State*               g_State = nullptr;   // the main state the functions were stored in
    std::unordered_map<std::string, std::string> g_LastError;

    std::atomic<std::uint32_t> g_Frame{ 0 };
    std::uint32_t g_LastRunFrame = 0;
    ULONGLONG     g_LastRunTick = 0;
    bool          g_Running = false;

    double SecondsSinceStart()
    {
        static LARGE_INTEGER s_freq{};
        static LARGE_INTEGER s_start{};
        if (!s_freq.QuadPart)
        {
            QueryPerformanceFrequency(&s_freq);
            QueryPerformanceCounter(&s_start);
        }
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        return static_cast<double>(now.QuadPart - s_start.QuadPart) / static_cast<double>(s_freq.QuadPart);
    }

    bool PushCallbackSEH(lua_State* L, const char* name, double time, int top)
    {
        __try
        {
            g_lua_pushstring(L, const_cast<char*>(kRegistryKey));
            g_lua_gettable(L, LUA_REGISTRYINDEX_51);
            if (g_lua_type(L, -1) != LUA_TTABLE)
            {
                g_lua_settop(L, top);
                return false;
            }
            g_lua_getfield(L, -1, const_cast<char*>(name));
            if (g_lua_type(L, -1) != LUA_TFUNCTION)
            {
                g_lua_settop(L, top);
                return false;
            }
            g_lua_pushnumber(L, static_cast<lua_Number>(time));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool CallOne(lua_State* L, const char* name, double time, char* error, std::size_t errorSize)
    {
        const int top = g_lua_gettop(L);
        if (!PushCallbackSEH(L, name, time, top))
            return true;
        bool ok = true;
        if (g_lua_pcall(L, 1, 0, 0) != 0)
        {
            const char* text = g_lua_tolstring ? g_lua_tolstring(L, -1, nullptr) : nullptr;
            strncpy_s(error, errorSize, text ? text : "(no message)", _TRUNCATE);
            ok = false;
        }
        g_lua_settop(L, top);
        return ok;
    }
}

bool FrameCallbacks::Set(lua_State* L, const char* name, int fnIdx)
{
    if (!L || !name || !*name || !ResolveLuaApi())
        return false;
    const int top = g_lua_gettop(L);
    if (fnIdx < 0)
        fnIdx = top + fnIdx + 1;
    const int fnType = g_lua_type(L, fnIdx);
    if (fnType != LUA_TFUNCTION && fnType != LUA_TNIL && fnType != LUA_TNONE)
        return false;
    const bool add = fnType == LUA_TFUNCTION;

    g_lua_pushstring(L, const_cast<char*>(kRegistryKey));
    g_lua_gettable(L, LUA_REGISTRYINDEX_51);
    if (g_lua_type(L, -1) != LUA_TTABLE)
    {
        g_lua_settop(L, top);
        g_lua_createtable(L, 0, 4);
        g_lua_pushstring(L, const_cast<char*>(kRegistryKey));
        g_lua_pushvalue(L, -2);
        g_lua_settable(L, LUA_REGISTRYINDEX_51);
    }
    g_lua_pushstring(L, const_cast<char*>(name));
    if (add)
        g_lua_pushvalue(L, fnIdx);
    else
        g_lua_pushnil(L);
    g_lua_settable(L, -3);
    g_lua_settop(L, top);

    lua_State* main = V_FrameWork_AnyLuaState();
    std::lock_guard<std::mutex> lock(g_Lock);
    if (main != g_State)
    {
        g_Names.clear();
        g_LastError.clear();
        g_State = main;
    }
    const auto it = std::find(g_Names.begin(), g_Names.end(), name);
    if (add && it == g_Names.end())
        g_Names.emplace_back(name);
    else if (!add && it != g_Names.end())
        g_Names.erase(it);
    g_LastError.erase(name);
    return true;
}

void FrameCallbacks::NoteFrame()
{
    g_Frame.fetch_add(1, std::memory_order_release);
}

void FrameCallbacks::Run()
{
    if (g_Running || !V_FrameWork::InsideGameLua())
        return;
    const std::uint32_t frame = g_Frame.load(std::memory_order_acquire);
    const ULONGLONG now = GetTickCount64();
    if (frame != 0 ? frame == g_LastRunFrame : now - g_LastRunTick < kFallbackFrameMs)
        return;

    std::vector<std::string> names;
    lua_State* state = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_Lock);
        if (g_Names.empty())
            return;
        names = g_Names;
        state = g_State;
    }
    lua_State* L = V_FrameWork_AnyLuaState();
    if (!L || !ResolveLuaApi())
        return;
    if (L != state)
    {
        std::lock_guard<std::mutex> lock(g_Lock);
        g_Names.clear();
        g_LastError.clear();
        g_State = nullptr;
        return;
    }
    if (!V_FrameWork::LuaStackHasRoom(L, kSlotsNeeded))
        return;

    g_LastRunFrame = frame;
    g_LastRunTick = now;
    g_Running = true;
    const double time = SecondsSinceStart();
    char error[512];
    for (const std::string& name : names)
    {
        error[0] = '\0';
        if (CallOne(L, name.c_str(), time, error, sizeof(error)))
            continue;
        const std::string text(error);
        std::lock_guard<std::mutex> lock(g_Lock);
        std::string& last = g_LastError[name];
        if (last != text)
        {
            last = text;
            LogWarn("V_FrameWork.SetFrameCallback: '%s' failed: %s", name.c_str(), text.c_str());
        }
    }
    g_Running = false;
}
