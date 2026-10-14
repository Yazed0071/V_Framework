#pragma once

#include <cstdint>

void SoldierIncomingVoice_OnShot(const void* fireRequest, const void* gunInfo, int engineEquipId, const float shotOrigin[3]);
void SoldierIncomingVoice_OnSightCheck(void* noticeController, std::uint32_t soldierIndex);
