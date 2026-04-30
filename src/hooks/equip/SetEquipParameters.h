#pragma once

#include <cstddef>
#include <cstdint>

struct lua_State;

namespace EquipParams
{
    struct Deps
    {
        bool (*ResolveLuaApi)();

        int (*GetLuaTop)(lua_State*);
        int (*LuaType)(lua_State*, int);

        int (*GetLuaInt)(lua_State*, int);
        double (*GetLuaNumber)(lua_State*, int);
        const char* (*GetLuaString)(lua_State*, int);

        size_t(*LuaObjLen)(lua_State*, int);
        void (*LuaSetTop)(lua_State*, int);

        void (*PushLuaNumber)(lua_State*, float);
        void (*LuaPushString)(lua_State*, const char*);
        void (*LuaCreateTable)(lua_State*, int, int);
        void (*LuaGetField)(lua_State*, int, const char*);
        void (*LuaRawGetI)(lua_State*, int, int);
        void (*LuaGetTable)(lua_State*, int);
        void (*LuaSetTable)(lua_State*, int);
        void (*LuaPushValue)(lua_State*, int);
    };

    void Bind(const Deps& deps);

    int __cdecl Lua_SetEquipParameters(lua_State* L);

    void ApplyQueuedEquipParameters_LuaTables(lua_State* L);

    bool Install_EquipParameterTablesImpl_ReloadEquipParameterTablesImpl2_Hook();
    bool Uninstall_EquipParameterTablesImpl_ReloadEquipParameterTablesImpl2_Hook();
}