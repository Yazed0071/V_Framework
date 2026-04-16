#pragma once

#include <cstdint>

struct lua_State;

namespace SupportWeaponType
{
    struct Deps
    {
        bool (*ResolveLuaApi)() = nullptr;
        int (*LuaType)(lua_State* L, int idx) = nullptr;
        int (*GetLuaInt)(lua_State* L, int idx) = nullptr;
    };

    void Bind(const Deps& deps);

    int __cdecl Lua_SetSupportWeaponType(lua_State* L);
    int __cdecl Lua_RemoveSupportWeaponType(lua_State* L);
    int __cdecl Lua_ClearSupportWeaponTypes(lua_State* L);

    bool Install_EquipIdTableImpl_GetSupportWeaponTypeId_Hook();
    bool Uninstall_EquipIdTableImpl_GetSupportWeaponTypeId_Hook();
}