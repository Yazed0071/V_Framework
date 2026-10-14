#pragma once

struct lua_State;


int __cdecl l_RegisterOutfit(lua_State* L);
int __cdecl l_RegisterHeadOption(lua_State* L);
int __cdecl l_ExtendVanillaOutfit(lua_State* L);
int __cdecl l_GetOutfitInfo(lua_State* L);
int __cdecl l_GetOutfits(lua_State* L);
int __cdecl l_GetHeadOptionSlot(lua_State* L);
int __cdecl l_GetHeadOptions(lua_State* L);
int __cdecl l_PlayHeadMotion(lua_State* L);
int __cdecl l_PlayHeadEffect(lua_State* L);
int __cdecl l_StopHeadEffect(lua_State* L);
int __cdecl l_PlayHeadSound(lua_State* L);
int __cdecl l_GetOutfitColors(lua_State* L);
int __cdecl l_GetOutfitColor(lua_State* L);
int __cdecl l_SetOutfitColor(lua_State* L);
int __cdecl l_GetSortieOutfitCursor(lua_State* L);
int __cdecl l_EquipSortieOutfitCursor(lua_State* L);
int __cdecl l_AddToEquipDevelopTable(lua_State* L);

void OutfitLua_EnsureEquipDevelopBound();
void OutfitLua_ApplyPendingColors(lua_State* L);
