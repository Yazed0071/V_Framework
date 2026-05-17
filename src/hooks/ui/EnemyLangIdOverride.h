#pragma once

#include <cstdint>


bool Install_EnemyLangIdOverride_Hooks();


bool Uninstall_EnemyLangIdOverride_Hooks();


void EnemyLangId_SetMapOverride(std::uint64_t langIdHash);


void EnemyLangId_ClearMapOverride();


void EnemyLangId_SetBinoOverride(std::uint64_t langIdHash);


void EnemyLangId_ClearBinoOverride();
