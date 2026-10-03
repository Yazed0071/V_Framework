#pragma once

struct lua_State;

int __cdecl l_AddCallMenuItem(lua_State* L);
int __cdecl l_GetCallMenuItem(lua_State* L);
int __cdecl l_EnableCallMenuItem(lua_State* L);
int __cdecl l_DisableCallMenuItem(lua_State* L);
int __cdecl l_ShowCallMenuItem(lua_State* L);
int __cdecl l_HideCallMenuItem(lua_State* L);
int __cdecl l_ResetCallMenuItem(lua_State* L);
int __cdecl l_ResetAllCallMenuItems(lua_State* L);
int __cdecl l_SetCallMenuItemInPlaceOf(lua_State* L);
