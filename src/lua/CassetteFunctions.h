#pragma once

struct lua_State;

int __cdecl l_PlayCassetteTapeByTrackId(lua_State* L);
int __cdecl l_GetTapeTrackId(lua_State* L);
int __cdecl l_GetCassettePlayingTime(lua_State* L);
int __cdecl l_GetCassettePlayingTrackId(lua_State* L);
int __cdecl l_PauseCassette(lua_State* L);
int __cdecl l_ResumeCassette(lua_State* L);
int __cdecl l_StopCassette(lua_State* L);
int __cdecl l_IsCassetteSpeakerEnabled(lua_State* L);
int __cdecl l_SetCassetteSpeakerEnabled(lua_State* L);
int __cdecl l_RegisterRadioCassette(lua_State* L);
int __cdecl l_RegisterCustomTapes(lua_State* L);
