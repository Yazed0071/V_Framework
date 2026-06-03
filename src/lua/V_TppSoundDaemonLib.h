#pragma once

struct lua_State;

// Registers the "V_TppSoundDaemon" C library (RTPC + voice-pitch functions) into the
// given Lua state. Called from the single SetLuaFunctions hook (RegisterAllUiLuaLibraries),
// alongside "V_FrameWork" / "V_TppUiCommand". The implementations are shared with the
// V_FrameWork library (defined in SetLuaFunctions.cpp; declared in SoundDaemonFunctions.h).
bool Register_V_TppSoundDaemonLibrary(lua_State* L);
