#pragma once

struct lua_State;

namespace PlayerCamoFpkHook
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

    int __cdecl Lua_RegisterCustomPlayerCamoFpk(lua_State* L);
    int __cdecl Lua_RemoveCustomPlayerCamoFpk(lua_State* L);
    int __cdecl Lua_ClearCustomPlayerCamoFpk(lua_State* L);

    bool Install();
    bool Uninstall();
}