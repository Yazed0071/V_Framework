#pragma once

#include <cstdint>

bool Install_Soldier2CombatUtil_GrenadeUpdate_Hook();
bool Uninstall_Soldier2CombatUtil_GrenadeUpdate_Hook();

bool SoldierGrenadeIds_SetForSoldier(std::uint32_t soldierIndex, std::int32_t grenadeId, std::int32_t stunId);   // both 0: back to the shared RegistGrenadeId ids
void SoldierGrenadeIds_ClearForNewMission();
std::int32_t SoldierGrenadeIds_Pick(std::uint32_t soldierIndex, const std::uint8_t* soldier2Part);   // his own ids, else the shared ones, stunId first
bool SoldierGrenadeIds_Active();
