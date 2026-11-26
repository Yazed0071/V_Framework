#pragma once

#include <cstdint>

bool Install_EquipBlockController2_StartLoadingToEquipMissionBlock_Hook();
void RequestWalkerSidearmPack(std::uint16_t equipId, const char* key);
bool Uninstall_EquipBlockController2_StartLoadingToEquipMissionBlock_Hook();
