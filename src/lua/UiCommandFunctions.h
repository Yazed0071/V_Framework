#pragma once

// Raw Lua C functions for UI commands. They are DEFINED in SetLuaFunctions.cpp
// (where the shared lua-arg helpers live) and registered into the "V_FrameWork"
// library there. This header gives them external linkage so the "V_TppUiCommand"
// library (V_TppUiCommandLib.cpp) can expose the same implementations — a function
// is therefore callable as both V_FrameWork.X(...) and V_TppUiCommand.X(...).

struct lua_State;

// Equip background textures / loading + game-over splash
int __cdecl l_SetDefaultEquipBgTexturePath(lua_State* L);
int __cdecl l_ClearDefaultEquipBgTexture(lua_State* L);
int __cdecl l_SetEquipBgTexturePath(lua_State* L);
int __cdecl l_ClearEquipBgTexture(lua_State* L);
int __cdecl l_SetEnemyWeaponBgTexturePath(lua_State* L);
int __cdecl l_ClearEnemyWeaponBgTexture(lua_State* L);
int __cdecl l_SetEnemyEquipBgTexturePath(lua_State* L);
int __cdecl l_ClearEnemyEquipBgTexture(lua_State* L);
int __cdecl l_ClearAllEquipBgTextures(lua_State* L);
int __cdecl l_SetLoadingSplashMainTexturePath(lua_State* L);
int __cdecl l_SetLoadingSplashBlurTexturePath(lua_State* L);
int __cdecl l_ClearLoadingSplashTextures(lua_State* L);
int __cdecl l_SetGameOverSplashMainTexturePath(lua_State* L);
int __cdecl l_SetGameOverSplashBlurTexturePath(lua_State* L);
int __cdecl l_ClearGameOverSplashTextures(lua_State* L);

// Equip icons
int __cdecl l_SetEquipIdIconFtexPath(lua_State* L);
int __cdecl l_ClearIconFtexPath(lua_State* L);
int __cdecl l_ClearAllIconFtexPaths(lua_State* L);

// Mission emergency / popup
int __cdecl l_SetMissionEmergency(lua_State* L);
int __cdecl l_IsMissionEmergency(lua_State* L);
int __cdecl l_ClearAllMissionEmergencies(lua_State* L);
int __cdecl l_SetEmergencyMissionPopup(lua_State* L);
int __cdecl l_SetEmergencyMissionPopupLangId(lua_State* L);
int __cdecl l_ClearEmergencyMissionPopupOverride(lua_State* L);

// Mission icon
int __cdecl l_ShowMissionIcon(lua_State* L);

int __cdecl l_ShowTimeCigaretteUi(lua_State* L);
int __cdecl l_HideTimeCigaretteUi(lua_State* L);


// MbDvc announce popups
int __cdecl l_ShowMbDvcAnnouncePopupReport(lua_State* L);
int __cdecl l_ShowMbDvcAnnouncePopupReportLangId(lua_State* L);
int __cdecl l_ShowMbDvcAnnouncePopupReward(lua_State* L);
int __cdecl l_ShowMbDvcAnnouncePopupRewardLangId(lua_State* L);

// Enemy information LangId / unit name (global)
int __cdecl l_SetEnemyInformationLangId(lua_State* L);
int __cdecl l_ClearEnemyInformationLangId(lua_State* L);
int __cdecl l_SetEnemyUnitName(lua_State* L);
int __cdecl l_ClearEnemyUnitName(lua_State* L);

// Enemy information LangId / unit name (per soldier)
int __cdecl l_SetEnemyInformationLangIdForSoldier(lua_State* L);
int __cdecl l_ClearEnemyInformationLangIdForSoldier(lua_State* L);
int __cdecl l_ClearAllEnemyInformationLangIdForSoldiers(lua_State* L);
int __cdecl l_SetEnemyUnitNameForSoldier(lua_State* L);
int __cdecl l_ClearEnemyUnitNameForSoldier(lua_State* L);
int __cdecl l_ClearAllEnemyUnitNameForSoldiers(lua_State* L);
