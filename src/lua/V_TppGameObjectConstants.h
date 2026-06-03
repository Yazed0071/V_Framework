#pragma once

struct lua_State;

// Registers the global "V_TppGameObject" constants table into the given Lua state.
//
// Called from the single SetLuaFunctions hook (RegisterAllUiLuaLibraries), next to
// the "V_FrameWork" / "V_TppUiCommand" libraries. Add constants by editing kEntries
// in V_TppGameObjectConstants.cpp.
void Register_V_TppGameObjectConstants(lua_State* L);
