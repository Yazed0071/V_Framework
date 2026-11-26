#pragma once

struct lua_State;

namespace MissionStateReset
{
    void EnsureFinalizerRegistered(lua_State* L);
    void PollMissionChange();
    void OnCheckpointReload();   // the mission block reloads in place: no finalizer runs, mod Lua setup stays
}
