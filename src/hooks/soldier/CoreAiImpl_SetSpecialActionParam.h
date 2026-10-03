#pragma once

#include <cstdint>

bool Install_CoreAiImpl_SetSpecialActionParam_Hook();
bool Uninstall_CoreAiImpl_SetSpecialActionParam_Hook();

bool Arm_HostageStruggle(std::uint32_t gameObjectId, bool struggle, bool ever);
void Disarm_HostageStruggle();
