#pragma once

#include <cstdint>

struct lua_State;

namespace PlayerPartsFpkHook
{
    struct LuaBindings
    {
        bool (*ResolveLuaApi)() = nullptr;
        int (*GetLuaTop)(lua_State* L) = nullptr;
        int (*LuaType)(lua_State* L, int idx) = nullptr;
        int (*GetLuaInt)(lua_State* L, int idx) = nullptr;
        const char* (*GetLuaString)(lua_State* L, int idx) = nullptr;
        void (*PushLuaNumber)(lua_State* L, float value) = nullptr;
    };

    void BindLua(const LuaBindings& bindings);

    int __cdecl Lua_RegisterCustomPlayerPartsFpk(lua_State* L);
    int __cdecl Lua_RemoveCustomPlayerPartsFpk(lua_State* L);
    int __cdecl Lua_ClearCustomPlayerPartsFpk(lua_State* L);

    bool Install();
    bool Uninstall();
}