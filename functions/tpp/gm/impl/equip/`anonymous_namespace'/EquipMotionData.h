#pragma once
#include <cstddef>

struct lua_State;

namespace EquipMotionData
{
    struct Deps
    {
        bool (*ResolveLuaApi)() = nullptr;

        int (*GetLuaTop)(lua_State* L) = nullptr;
        int (*LuaType)(lua_State* L, int idx) = nullptr;
        int (*GetLuaInt)(lua_State* L, int idx) = nullptr;
        const char* (*GetLuaString)(lua_State* L, int idx) = nullptr;
        std::size_t(*LuaObjLen)(lua_State* L, int idx) = nullptr;

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

    int __cdecl Lua_AddEquipMotionDataEntry(lua_State* L);
    int __cdecl Lua_RemoveEquipMotionDataEntry(lua_State* L);
    int __cdecl Lua_ClearEquipMotionDataEntries(lua_State* L);
    int __cdecl Lua_AddEquipMotionDataTable(lua_State* L);

    bool Install_EquipMotionDataTableImpl_ReloadEquipMotionData_Hook();
    bool Uninstall_EquipMotionDataTableImpl_ReloadEquipMotionData_Hook();
}