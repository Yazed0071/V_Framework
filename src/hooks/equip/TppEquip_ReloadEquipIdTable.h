#pragma once

struct lua_State;

int __cdecl l_AddToEquipIdTable(lua_State* L);

int TppEquip_GetSubIdForEquipId(int equipId);
int TppEquip_ReleaseEquipRow(int equipId);

bool Install_TppEquip_ReloadEquipIdTable_Hook();
bool Uninstall_TppEquip_ReloadEquipIdTable_Hook();
