#pragma once

#include <cstdint>

bool Install_EquipBgTexture_Hook();
bool Uninstall_EquipBgTexture_Hook();

void EquipBg_SetDefaultTexture(uint64_t textureHash, bool colored, float opacity);
void EquipBg_ClearDefaultTexture();
void EquipBg_SetEquipTexture(int equipId, uint64_t textureHash, bool colored, float opacity);
void EquipBg_ClearEquipTexture(int equipId);

void EquipBg_SetEnemyWeaponTexture(uint64_t textureHash, bool colored, float opacity);
void EquipBg_ClearEnemyWeaponTexture();
void EquipBg_SetEnemyEquipTexture(int equipId, uint64_t textureHash, bool colored, float opacity);
void EquipBg_ClearEnemyEquipTexture(int equipId);

void EquipBg_ClearAllEquipTextures();
