#pragma once

#include <cstdint>


bool Install_EnemyLangIdOverride_Hooks();


bool Uninstall_EnemyLangIdOverride_Hooks();


void EnemyLangId_SetMapOverride(std::uint64_t langIdHash);


void EnemyLangId_ClearMapOverride();


void EnemyLangId_SetBinoOverride(std::uint64_t langIdHash);


void EnemyLangId_ClearBinoOverride();


void EnemyLangId_SetMapOverrideForSoldier(std::uint32_t soldierGameObjectId,
    std::uint64_t langIdHash);


void EnemyLangId_ClearMapOverrideForSoldier(std::uint32_t soldierGameObjectId);


void EnemyLangId_ClearAllMapOverridesForSoldier();


void EnemyLangId_SetBinoOverrideForSoldier(std::uint32_t soldierGameObjectId,
    std::uint64_t langIdHash);


void EnemyLangId_ClearBinoOverrideForSoldier(std::uint32_t soldierGameObjectId);


void EnemyLangId_ClearAllBinoOverridesForSoldier();
