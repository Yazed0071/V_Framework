#pragma once

struct lua_State;

int __cdecl l_SetPlayerVoiceFpkPathForType(lua_State* L);
int __cdecl l_ClearPlayerVoiceFpkPathForType(lua_State* L);
int __cdecl l_ClearAllPlayerVoiceFpkOverrides(lua_State* L);

int __cdecl l_IsBarrierActive(lua_State* L);
