#pragma once

#include <cstdint>


bool Install_UiTextureOverrides_Hook();


bool Uninstall_UiTextureOverrides_Hook();


void EquipBg_SetDefaultTexture(uint64_t textureHash);


void EquipBg_ClearDefaultTexture();


void EquipBg_SetEnemyWeaponTexture(uint64_t textureHash);


void EquipBg_ClearEnemyWeaponTexture();


void EquipBg_SetEnemyEquipTexture(int equipId, uint64_t textureHash);


void EquipBg_ClearEnemyEquipTexture(int equipId);


void EquipBg_SetEquipTexture(int equipId, uint64_t textureHash);


void EquipBg_ClearEquipTexture(int equipId);


void EquipBg_ClearAllEquipTextures();


void LoadingSplash_SetMainTexture(uint64_t textureHash);


void LoadingSplash_ClearMainTexture();


void LoadingSplash_SetBlurTexture(uint64_t textureHash);


void LoadingSplash_ClearBlurTexture();


void LoadingSplash_ClearTextures();


void GameOverSplash_SetMainTexture(uint64_t textureHash);


void GameOverSplash_ClearMainTexture();


void GameOverSplash_SetBlurTexture(uint64_t textureHash);


void GameOverSplash_ClearBlurTexture();


void GameOverSplash_ClearTextures();