#pragma once

struct lua_State;

int __cdecl l_SetSahelanFova(lua_State* L);
int __cdecl l_ClearSahelanFova(lua_State* L);
int __cdecl l_SetEyeLampColor(lua_State* L);
int __cdecl l_ClearEyeLampColor(lua_State* L);
int __cdecl l_SetEyeLampDisco(lua_State* L);
int __cdecl l_SetHeartLightColor(lua_State* L);
int __cdecl l_ClearHeartLightColor(lua_State* L);
int __cdecl l_SetEyeLampColorLogging(lua_State* L);
