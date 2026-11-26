#pragma once

struct lua_State;

namespace UixUtilityPrefabList
{
    int __cdecl l_SetupPrefabList(lua_State* L);
    int __cdecl l_SetPrefabListRows(lua_State* L);
    int __cdecl l_GetPrefabListCursor(lua_State* L);
    int __cdecl l_ClosePrefabList(lua_State* L);
}
