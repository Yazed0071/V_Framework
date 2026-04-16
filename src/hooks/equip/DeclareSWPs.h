#pragma once

struct lua_State;

namespace DeclareSWPs
{
    struct Deps
    {
        bool(__cdecl* ResolveLuaApi)();
        int(__cdecl* GetLuaTop)(lua_State* L);
        void(__cdecl* LuaGetField)(lua_State* L, int idx, const char* k);
        int(__cdecl* LuaType)(lua_State* L, int idx);
        void(__cdecl* LuaPop)(lua_State* L, int count);

        const char* (__cdecl* GetLuaString)(lua_State* L, int idx);
        void(__cdecl* PushLuaNumber)(lua_State* L, float n);

        void(__cdecl* LuaPushString)(lua_State* L, const char* s);
        void(__cdecl* LuaCreateTable)(lua_State* L, int narr, int nrec);
        void(__cdecl* LuaRawSet)(lua_State* L, int idx);
        void(__cdecl* LuaSetTable)(lua_State* L, int idx);
    };

    void Bind(const Deps& deps);
    int __cdecl Lua_DeclareSWPs(lua_State* L);
}