#pragma once

#include <cstdint>

constexpr std::uint32_t kEjectChamberRoundEvent = 0x6253F496;   // MTEV_EjectChamberRound

bool Install_RealizedEquipObjectImpl_MotionEventCallbackImpl_Hook();
bool Uninstall_RealizedEquipObjectImpl_MotionEventCallbackImpl_Hook();

std::uint32_t MotionEventName(void* event);
void* MotionEventOwner(void* event);
void RealizedEquip_EjectCasing(void* realizedEquip, std::uint32_t slot);
