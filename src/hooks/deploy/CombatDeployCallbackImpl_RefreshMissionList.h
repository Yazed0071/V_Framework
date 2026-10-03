#pragma once

#include <cstdint>

bool Install_CombatDeployCallbackImpl_RefreshMissionList_Hook();
bool Uninstall_CombatDeployCallbackImpl_RefreshMissionList_Hook();

void ArrangeMissionListRow(std::uint8_t* missionInfo);
