#pragma once

#include <cstdint>

bool Install_DeployControllerImpl_AddLogMission_Hook();
bool Uninstall_DeployControllerImpl_AddLogMission_Hook();

bool TakeLoggedDeployResult(std::uint8_t slot, bool& win);
