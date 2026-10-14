#pragma once

#include <cstdint>

void LoadingSplash_SetMainTexture(uint64_t textureHash, uint32_t missionCode = 0);
void LoadingSplash_ClearMainTexture(uint32_t missionCode = 0);
void LoadingSplash_SetBlurTexture(uint64_t textureHash, uint32_t missionCode = 0);
void LoadingSplash_ClearBlurTexture(uint32_t missionCode = 0);
void LoadingSplash_ClearTextures(uint32_t missionCode = 0);

// the game's tpp::ui::LoadingTipsEv, seen when a loading-tips session starts (null before the first one)
void* LoadingSplash_TipsEvent();

bool Install_LoadingSplash_Hook();
bool Uninstall_LoadingSplash_Hook();
