#include "pch.h"

extern "C" {
    #include "lua.h"
    #include "lauxlib.h"
}

#include <cstdint>
#include <string>

#include "V_TppMotionLib.h"
#include "../hooks/fox/anim/AnimFile_GetEventData.h"
#include "../core/FoxHashes.h"
#include "LuaApi.h"
#include "log.h"

namespace
{
    std::string StringField(lua_State* L, const char* key)
    {
        const int top = GetLuaTop(L);
        LuaGetField(L, 1, key);
        const char* text = LuaType(L, -1) == LUA_TSTRING ? GetLuaString(L, -1) : nullptr;
        std::string value = text ? text : "";
        g_lua_settop(L, top);
        return value;
    }

    int IntField(lua_State* L, const char* key, int fallback)
    {
        const int top = GetLuaTop(L);
        LuaGetField(L, 1, key);
        const int value = LuaType(L, -1) == LUA_TNUMBER ? GetLuaInt(L, -1) : fallback;
        g_lua_settop(L, top);
        return value;
    }

    void EventNameField(lua_State* L, ClipEventLabel& label)
    {
        const int top = GetLuaTop(L);
        LuaGetField(L, 1, "event");
        if (LuaType(L, -1) == LUA_TNUMBER)
        {
            label.eventHash = static_cast<std::uint32_t>(GetLuaInt64(L, -1));
        }
        else if (LuaType(L, -1) == LUA_TSTRING)
        {
            const std::string text = GetLuaString(L, -1);
            label.event = text.rfind("MTEV_", 0) == 0 ? text.substr(5) : text;
            if (!label.event.empty())
                label.eventHash = FoxHashes::StrCode32("MTEV_" + label.event);
        }
        g_lua_settop(L, top);
    }

    int __cdecl l_AddClipEvent(lua_State* L)
    {
        if (LuaType(L, 1) != LUA_TTABLE)
        {
            LogWarn("AddClipEvent: needs a table { clip, frame, event }, nothing added");
            PushLuaBool(L, false);
            return 1;
        }

        ClipEventLabel label;
        label.clip = StringField(L, "clip");
        const std::uint64_t clip = label.clip.empty() ? 0 : FoxHashes::ClipPathId(label.clip);
        const int frame = IntField(L, "frame", -1);
        EventNameField(L, label);
        if (!clip || frame < 0 || !label.eventHash)
        {
            LogWarn("AddClipEvent: clip '%s': needs clip, frame (0 or more) and event, nothing added", label.clip.c_str());
            PushLuaBool(L, false);
            return 1;
        }

        const bool added = AddClipEvent(clip, frame, label);
        if (!added)
            LogWarn("AddClipEvent: clip '%s': frame %d too high or 64 events already on it, nothing added", label.clip.c_str(), frame);
        PushLuaBool(L, added);
        return 1;
    }

    luaL_Reg g_VTppMotionLib[] =
    {
        { "AddClipEvent", l_AddClipEvent },

        { nullptr, nullptr }
    };
}

bool Register_V_TppMotionLibrary(lua_State* L)
{
    return RegisterLuaLibrary(L, "V_TppMotion", g_VTppMotionLib);
}
