#include <pch.h>
#include "WindowPages.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "LuaApi.h"
#include "LuaBroadcaster.h"
#include "log.h"
#include "../hooks/ui/MbDvcManageEv_CustomPage.h"
#include "../hooks/ui/UixUtilityControl.h"

namespace
{
    constexpr int           LUA_REGISTRYINDEX_51 = -10000;
    constexpr const char*   kRegistryKey = "V_FrameWork.WindowPages";
    constexpr int           kSlotsNeeded = 6;
    constexpr std::size_t   kMaxQueued = 1024;   // Lua stalled (a long load): drop instead of growing forever
    constexpr std::uint64_t kNameMask = 0xFFFFFFFFFFFFull;

    const char* const kHandlerNames[] = { "OnSetup", "OnInit", "OnStart", "OnUpdate", "OnStop", "OnFinalize", "OnBack" };

    struct Pending
    {
        std::uint64_t name = 0;
        void* window = nullptr;
        WindowPages::Event event = WindowPages::Event::Setup;
    };

    std::mutex                        g_Lock;
    std::unordered_set<std::uint64_t> g_Registered;
    std::vector<Pending>              g_Queue;
    std::unordered_set<void*>         g_UpdateQueued;
    std::unordered_set<void*>         g_Finalized;
    std::unordered_map<std::string, std::string> g_LastError;
    lua_State*                        g_State = nullptr;
    std::atomic<bool>                 g_HasWork{ false };
    bool                              g_Running = false;

    void ForgetState()
    {
        g_Registered.clear();
        g_Queue.clear();
        g_UpdateQueued.clear();
        g_Finalized.clear();
        g_LastError.clear();
        g_HasWork.store(false, std::memory_order_release);
    }

    bool PushHandlerSEH(lua_State* L, std::uint64_t name, const char* handler, int top)
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
            g_lua_pushnumber(L, static_cast<lua_Number>(name));
            g_lua_gettable(L, -2);
            if (g_lua_type(L, -1) != LUA_TTABLE)
            {
                g_lua_settop(L, top);
                return false;
            }
            g_lua_getfield(L, -1, const_cast<char*>(handler));
            if (g_lua_type(L, -1) != LUA_TFUNCTION)
            {
                g_lua_settop(L, top);
                return false;
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool CallHandler(lua_State* L, const Pending& p)   // true when the handler returned true
    {
        const char* handler = kHandlerNames[static_cast<int>(p.event)];
        const int top = g_lua_gettop(L);
        if (!PushHandlerSEH(L, p.name, handler, top))
            return false;
        if (p.event == WindowPages::Event::Finalize)
            g_lua_pushnil(L);   // the window is being destroyed: no handle to keep
        else
            g_lua_pushnumber(L, static_cast<lua_Number>(UixUtilityControl::HandleFor(UixUtilityControl::Kind::Window, p.window, p.window)));
        bool result = false;
        if (g_lua_pcall(L, 1, 1, 0) == 0)
            result = g_lua_toboolean(L, -1) != 0;
        else
        {
            const char* text = g_lua_tolstring ? g_lua_tolstring(L, -1, nullptr) : nullptr;
            char key[64];
            sprintf_s(key, "%012llX.%s", static_cast<unsigned long long>(p.name), handler);
            const std::string message = text ? text : "(no message)";
            std::lock_guard<std::mutex> lock(g_Lock);
            std::string& last = g_LastError[key];
            if (last != message)
            {
                last = message;
                LogWarn("V_UixUtility.SetWindowPage: %s of window 0x%012llX failed: %s", handler, static_cast<unsigned long long>(p.name), message.c_str());
            }
        }
        g_lua_settop(L, top);
        return result;
    }
}

bool WindowPages::HasPage(std::uint64_t windowName)
{
    std::lock_guard<std::mutex> lock(g_Lock);
    return g_Registered.count(windowName & kNameMask) != 0;
}

bool WindowPages::Set(lua_State* L, std::uint64_t windowName, int tableIdx)
{
    if (!L || !windowName || !ResolveLuaApi())
        return false;
    windowName &= kNameMask;
    const int top = g_lua_gettop(L);
    if (tableIdx < 0)
        tableIdx = top + tableIdx + 1;
    const int type = g_lua_type(L, tableIdx);
    if (type != LUA_TTABLE && type != LUA_TNIL && type != LUA_TNONE)
        return false;
    const bool add = type == LUA_TTABLE;

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
    g_lua_pushnumber(L, static_cast<lua_Number>(windowName));
    if (add)
        g_lua_pushvalue(L, tableIdx);
    else
        g_lua_pushnil(L);
    g_lua_settable(L, -3);
    g_lua_settop(L, top);

    lua_State* main = V_FrameWork_AnyLuaState();
    std::lock_guard<std::mutex> lock(g_Lock);
    if (main != g_State)
    {
        ForgetState();
        g_State = main;
    }
    if (add)
        g_Registered.insert(windowName);
    else
        g_Registered.erase(windowName);
    return true;
}

void WindowPages::Note(std::uint64_t windowName, void* window, Event e)
{
    windowName &= kNameMask;
    std::lock_guard<std::mutex> lock(g_Lock);
    if (!window || !g_Registered.count(windowName))
        return;
    if (e == Event::Setup)
        g_Finalized.erase(window);   // the engine reused the memory of a destroyed window
    if (e == Event::Update && !g_UpdateQueued.insert(window).second)
        return;   // one Update per window until Lua has seen it
    if (e == Event::Finalize)
        g_Finalized.insert(window);
    if (g_Queue.size() >= kMaxQueued)
        return;
    g_Queue.push_back({ windowName, window, e });
    g_HasWork.store(true, std::memory_order_release);
}

void WindowPages::Run()
{
    if (g_Running || !g_HasWork.load(std::memory_order_acquire) || !V_FrameWork::InsideGameLua())
        return;
    lua_State* L = V_FrameWork_AnyLuaState();
    if (!L || !ResolveLuaApi() || !V_FrameWork::LuaStackHasRoom(L, kSlotsNeeded))
        return;

    std::vector<Pending> work;
    std::unordered_set<void*> finalized;
    {
        std::lock_guard<std::mutex> lock(g_Lock);
        if (L != g_State)
        {
            ForgetState();
            g_State = nullptr;
            return;
        }
        work.swap(g_Queue);
        g_UpdateQueued.clear();
        finalized = g_Finalized;
        g_HasWork.store(false, std::memory_order_release);
    }

    g_Running = true;
    for (const Pending& p : work)
    {
        if (p.event != Event::Finalize && finalized.count(p.window))
        {
            if (p.event == Event::Back)
                mbDvcCustomPage::ClosePage(p.name);   // the page is gone: still give the menu and the pad back
            continue;   // destroyed before Lua got to it: its handle would point at freed memory
        }
        if (!CallHandler(L, p) && p.event == Event::Back)
            mbDvcCustomPage::ClosePage(p.name);   // OnBack missing or not true: Back closes the page
    }
    {
        std::lock_guard<std::mutex> lock(g_Lock);
        for (const Pending& p : work)
            if (p.event == Event::Finalize)
                g_Finalized.erase(p.window);
    }
    g_Running = false;
}
