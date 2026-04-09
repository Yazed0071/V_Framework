#pragma once

struct lua_State;

namespace DeclareRCs
{
    struct Deps
    {
        bool (*ResolveLuaApi)() = nullptr;

        int (*GetLuaTop)(lua_State* L) = nullptr;
        void (*LuaGetField)(lua_State* L, int idx, const char* fieldName) = nullptr;
        int (*LuaType)(lua_State* L, int idx) = nullptr;
        void (*LuaPop)(lua_State* L, int count) = nullptr;

        const char* (*GetLuaString)(lua_State* L, int idx) = nullptr;
        int (*GetLuaInt)(lua_State* L, int idx) = nullptr;
        void (*PushLuaNumber)(lua_State* L, float value) = nullptr;

        void (*LuaPushString)(lua_State* L, const char* value) = nullptr;
        void (*LuaCreateTable)(lua_State* L, int narr, int nrec) = nullptr;
        void (*LuaRawSet)(lua_State* L, int idx) = nullptr;
        void (*LuaSetTable)(lua_State* L, int idx) = nullptr;

        void (*LuaPushNil)(lua_State* L) = nullptr;
        int (*LuaNext)(lua_State* L, int idx) = nullptr;
    };

    void Bind(const Deps& deps);
    int __cdecl Lua_DeclareRCs(lua_State* L);
}