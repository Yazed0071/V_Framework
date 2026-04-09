#pragma once

struct lua_State;

namespace EquipMotionAssignments
{
    struct Deps
    {
        bool (*ResolveLuaApi)() = nullptr;
        int (*GetLuaTop)(lua_State* L) = nullptr;
        int (*LuaType)(lua_State* L, int idx) = nullptr;
        int (*GetLuaInt)(lua_State* L, int idx) = nullptr;
        size_t(*LuaObjLen)(lua_State* L, int idx) = nullptr;
        void (*LuaSetTop)(lua_State* L, int idx) = nullptr;
        void (*PushLuaNumber)(lua_State* L, float value) = nullptr;
        void (*LuaPushString)(lua_State* L, const char* value) = nullptr;
        void (*LuaCreateTable)(lua_State* L, int narr, int nrec) = nullptr;
        void (*LuaGetField)(lua_State* L, int idx, const char* fieldName) = nullptr;
        void (*LuaRawGetI)(lua_State* L, int idx, int n) = nullptr;
        void (*LuaGetTable)(lua_State* L, int idx) = nullptr;
        void (*LuaSetTable)(lua_State* L, int idx) = nullptr;
        void (*LuaPushValue)(lua_State* L, int idx) = nullptr;
    };

    void Bind(const Deps& deps);

    int __cdecl Lua_AddEquipMotionAssignment(lua_State* L);
    int __cdecl Lua_ClearEquipMotionAssignments(lua_State* L);

    bool Install_EquipMotionDataTableImpl_ReadMotionDataTable2_Hook();
    bool Uninstall_EquipMotionDataTableImpl_ReadMotionDataTable2_Hook();
}