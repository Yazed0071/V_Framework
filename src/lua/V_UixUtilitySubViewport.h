#pragma once

struct lua_State;

namespace UixUtilitySubViewport
{
    int __cdecl l_CreateSubViewport(lua_State* L);
    int __cdecl l_DeleteSubViewport(lua_State* L);
    int __cdecl l_IsSubViewportReady(lua_State* L);
    int __cdecl l_SetSubViewportCamera(lua_State* L);
    int __cdecl l_SetSubViewportDraw(lua_State* L);
    int __cdecl l_SetSubViewportExposure(lua_State* L);
    int __cdecl l_GetMainViewportExposure(lua_State* L);
    int __cdecl l_SetSubViewportTexture(lua_State* L);

    void ClearForNewMission();
}
