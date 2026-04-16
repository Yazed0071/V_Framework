#pragma once

#include <cstdint>

struct lua_State;

namespace DeclareAMs
{
    struct Deps
    {
        bool (*ResolveLuaApi)();

        int (*GetLuaTop)(lua_State*);
        int (*LuaType)(lua_State*, int);
        const char* (*GetLuaString)(lua_State*, int);
        int (*GetLuaInt)(lua_State*, int);
        void (*LuaSetTop)(lua_State*, int);

        void (*PushLuaNumber)(lua_State*, float);
        void (*LuaPushString)(lua_State*, const char*);
        void (*LuaCreateTable)(lua_State*, int, int);
        void (*LuaGetField)(lua_State*, int, const char*);
        void (*LuaSetTable)(lua_State*, int);
        void (*LuaPushNil)(lua_State*);
        int (*LuaNext)(lua_State*, int);
        void (*LuaRawSet)(lua_State*, int);
    };

    void Bind(const Deps& deps);

    int __cdecl Lua_DeclareAMs(lua_State* L);

    bool Install_DeclareAMs_Hook();
    bool Uninstall_DeclareAMs_Hook();
}