#pragma once

struct lua_State;

int __cdecl l_AddToEquipIdTable(lua_State* L);

bool Install_TppEquip_ReloadEquipIdTable_Hook();
bool Uninstall_TppEquip_ReloadEquipIdTable_Hook();
