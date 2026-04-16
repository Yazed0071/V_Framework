#pragma once

struct lua_State;

int __cdecl l_SetGunBasic(lua_State* L);

bool Install_EquipParameterTablesImpl_ReloadEquipParameterTablesImpl2_Hook();
bool Uninstall_EquipParameterTablesImpl_ReloadEquipParameterTablesImpl2_Hook();