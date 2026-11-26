#pragma once

#include <cstdint>

bool Install_RideHeliActionPluginImpl_ExecActionButtonFunction_Hook();
void Uninstall_RideHeliActionPluginImpl_ExecActionButtonFunction_Hook();

void Set_HeliPlayerMachineGunEnabled(std::uint32_t heliIndex, bool enabled);
void Clear_HeliPlayerMachineGunEnabled();
