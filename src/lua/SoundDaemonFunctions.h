#pragma once

// Raw Lua C functions for the sound daemon (RTPC + voice pitch). They are DEFINED in
// SetLuaFunctions.cpp (where the shared lua-arg helpers live) and registered into the
// "V_FrameWork" library there. This header gives them external linkage so the
// "V_TppSoundDaemon" library (V_TppSoundDaemonLib.cpp) can expose the same
// implementations — each is callable as both V_FrameWork.X(...) and V_TppSoundDaemon.X(...).

struct lua_State;

// RTPC (raw Wwise)
int __cdecl l_SetSoldierRtpc(lua_State* L);
int __cdecl l_SetGlobalRtpc(lua_State* L);
int __cdecl l_SetSoldierRtpcById(lua_State* L);
int __cdecl l_SetGlobalRtpcById(lua_State* L);
int __cdecl l_SetRtpcByAkObjId(lua_State* L);
int __cdecl l_SetRtpcByAkObjIdById(lua_State* L);
int __cdecl l_SetRtpcLoggingEnabled(lua_State* L);
int __cdecl l_IsRtpcLoggingEnabled(lua_State* L);

// RTPC (per-soldier via SoundController)
int __cdecl l_SetSoldierObjectRtpc(lua_State* L);
int __cdecl l_SetSoldierObjectRtpcByName(lua_State* L);

// Voice pitch
int __cdecl l_SetGlobalVoicePitch(lua_State* L);
int __cdecl l_GetGlobalVoicePitch(lua_State* L);
int __cdecl l_SetPitchByAkObjId(lua_State* L);
int __cdecl l_ClearPitchByAkObjId(lua_State* L);
int __cdecl l_ClearAllPerAkObjIdPitchBiases(lua_State* L);
int __cdecl l_GetSoldierAkObjId(lua_State* L);
int __cdecl l_SetSoldierVoicePitch(lua_State* L);
