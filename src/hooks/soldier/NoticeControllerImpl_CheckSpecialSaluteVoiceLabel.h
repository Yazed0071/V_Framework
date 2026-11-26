#pragma once

struct lua_State;

bool Install_MotherBaseSalutes_Hook();
bool Uninstall_MotherBaseSalutes_Hook();

namespace MotherBaseSalutes
{
    void AddRestoredSalutes(lua_State* L, int commandIndex);
    void EnsureMissionRankHookInstalled(lua_State* L);
    void ClearForNewMission();
}
