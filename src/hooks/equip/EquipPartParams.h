#pragma once

struct lua_State;

int __cdecl l_SetMagazine(lua_State* L);
int __cdecl l_SetStock(lua_State* L);
int __cdecl l_SetMuzzle(lua_State* L);
int __cdecl l_SetReceiver(lua_State* L);
int __cdecl l_SetSight(lua_State* L);

int EquipParam_AllocateMagazineSlotForName(const char* name);
int EquipParam_AllocateStockSlotForName(const char* name);
int EquipParam_AllocateMuzzleSlotForName(const char* name);
int EquipParam_AllocateReceiverSlotForName(const char* name);
int EquipParam_AllocateSightSlotForName(const char* name);

bool Install_MotionLoader_ReceiverTypeHook();
void Uninstall_MotionLoader_ReceiverTypeHook();
