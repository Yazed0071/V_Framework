#pragma once

#include <cstdint>

void LoadingSplash_SetMainTexture(uint64_t textureHash);
void LoadingSplash_ClearMainTexture();
void LoadingSplash_SetBlurTexture(uint64_t textureHash);
void LoadingSplash_ClearBlurTexture();
void LoadingSplash_ClearTextures();

bool Install_LoadingSplash_Hook();
bool Uninstall_LoadingSplash_Hook();
