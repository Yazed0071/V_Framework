#pragma once

#include <cstdint>

bool Install_EquipCrossEvCall_SetPrimaryOptionPanel_Hook();
void Uninstall_EquipCrossEvCall_SetPrimaryOptionPanel_Hook();

std::uint32_t EquipCrossEvCall_StripPrimaryWeapon();
