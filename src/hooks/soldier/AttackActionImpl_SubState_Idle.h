#pragma once

#include <cstdint>

bool Install_AttackActionImpl_SubState_Idle_Hook();
bool Uninstall_AttackActionImpl_SubState_Idle_Hook();
bool SquatIdleRoute_DropArmPose(void* motion, std::uint32_t slot, const std::uint8_t* soldierStates, std::uint32_t soldier); // soldierStates: the 0x28-byte-per-soldier state array
