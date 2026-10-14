#pragma once

#include <cstdint>

bool Install_AttackActionImpl_SetEquipMotion_Hook();
bool Uninstall_AttackActionImpl_SetEquipMotion_Hook();

int EquipSystem_GunSlotOfHandle(void* equipSystem, std::uint32_t equipHandle);
bool ChamberRound_TakeForGunSlot(int gunSlot);
int PlayerIndexHoldingGunSlot(int gunSlot);
void PlayerAttackAction_Forget();
