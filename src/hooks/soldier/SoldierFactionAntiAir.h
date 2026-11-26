#pragma once

#include <cstdint>

bool SoldierFactionAntiAir_OnSightCheck(void* noticeController, const void* soldierCore, std::uint32_t soldierIndex, std::uint64_t now);
void SoldierFactionAntiAir_OnSetCommand(std::uint32_t soldierIndex, std::uint8_t* command);
void SoldierFactionAntiAir_Forget(std::uint32_t soldierIndex, const char* reason);   // takes him off his gun if he is on one
bool SoldierFactionAntiAir_IsGunner(std::uint32_t soldierIndex);
void SoldierFactionAntiAir_OnGaveUp(std::uint32_t soldierIndex);
void SoldierFactionAntiAir_Clear();
