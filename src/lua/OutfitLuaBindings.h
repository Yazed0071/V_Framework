#pragma once

struct lua_State;


int __cdecl OutfitLua_RegisterOutfit(lua_State* L);
int __cdecl OutfitLua_RegisterHeadOption(lua_State* L);
int __cdecl OutfitLua_GetOutfitInfo(lua_State* L);
int __cdecl OutfitLua_AddToEquipDevelopTable(lua_State* L);

void OutfitLua_EnsureEquipDevelopBound();
