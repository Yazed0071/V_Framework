#pragma once

struct lua_State;

bool Install_TppVarInit_InitializeForNewMission_Hook();
bool Uninstall_TppVarInit_InitializeForNewMission_Hook();

namespace outfit::headabilities
{
    void EnsureLuaWrapsInstalled(lua_State* L);
}
