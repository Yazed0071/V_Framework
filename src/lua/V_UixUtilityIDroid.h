#pragma once

struct lua_State;

namespace VUixUtilityIDroid
{
    int __cdecl l_AddIDroidMenuItem(lua_State* L);
    int __cdecl l_RemoveIDroidMenuItem(lua_State* L);
    int __cdecl l_OpenIDroidPage(lua_State* L);
    int __cdecl l_CloseIDroidPage(lua_State* L);
    int __cdecl l_IsIDroidPageOpen(lua_State* L);
}
