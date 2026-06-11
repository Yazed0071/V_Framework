#pragma once

#include <cstdint>

void GameOverSplash_SetMainTexture(uint64_t textureHash);
void GameOverSplash_ClearMainTexture();
void GameOverSplash_SetBlurTexture(uint64_t textureHash);
void GameOverSplash_ClearBlurTexture();
void GameOverSplash_ClearTextures();

bool Install_GameOverSplash_Hook();
bool Uninstall_GameOverSplash_Hook();
