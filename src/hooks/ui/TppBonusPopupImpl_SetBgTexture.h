#pragma once

#include <cstdint>

void BonusPopup_SetLogoTexture(uint64_t textureHash, uint32_t missionCode = 0);
void BonusPopup_SetPhotoTexture(uint64_t textureHash, uint32_t missionCode = 0);
void BonusPopup_ClearTextures(uint32_t missionCode = 0);

bool Install_TppBonusPopupImpl_SetBgTexture_Hook();
bool Uninstall_TppBonusPopupImpl_SetBgTexture_Hook();
