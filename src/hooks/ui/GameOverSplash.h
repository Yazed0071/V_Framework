#pragma once

#include <cstdint>

void GameOverSplash_SetMainTexture(uint64_t textureHash, uint32_t missionCode = 0);
void GameOverSplash_ClearMainTexture(uint32_t missionCode = 0);
void GameOverSplash_SetBlurTexture(uint64_t textureHash, uint32_t missionCode = 0);
void GameOverSplash_ClearBlurTexture(uint32_t missionCode = 0);
void GameOverSplash_ClearTextures(uint32_t missionCode = 0);

// the game's tpp::ui::GameOverEvCall, seen when a game over menu starts (null before the first one), and its vtable as
// it was then: an event whose first pointer no longer matches is gone (the game makes it anew each mission)
void* GameOverSplash_GameOverEvent(void** vtable);

bool Install_GameOverSplash_Hook();
bool Uninstall_GameOverSplash_Hook();
