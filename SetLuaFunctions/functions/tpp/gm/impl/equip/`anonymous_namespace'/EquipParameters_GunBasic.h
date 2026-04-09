#pragma once

struct lua_State;

// Lua bridge entry exposed through V_FrameWork.
int __cdecl l_SetGunBasic(lua_State* L);

// Hook install/remove for EquipParameterTablesImpl::ReloadEquipParameterTablesImpl2.
bool Install_EquipParameterTablesImpl_ReloadEquipParameterTablesImpl2_Hook();
bool Uninstall_EquipParameterTablesImpl_ReloadEquipParameterTablesImpl2_Hook();