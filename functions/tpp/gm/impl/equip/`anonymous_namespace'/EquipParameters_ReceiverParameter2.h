#pragma once

struct lua_State;

int __cdecl l_SetReceiverParameter2(lua_State* L);

bool Install_ReadReceiverParameter2_Hook();
bool Uninstall_ReadReceiverParameter2_Hook();