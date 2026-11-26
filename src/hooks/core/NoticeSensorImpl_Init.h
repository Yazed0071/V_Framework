#pragma once

#include <cstdint>

bool Install_NoticeSensorImpl_Init_Hook();
void Uninstall_NoticeSensorImpl_Init_Hook();

// size of the notice table the game built last: 32 entries and 8 controllers when vanilla
std::uint32_t GetNoticeSensorEntrySlots();
std::uint32_t GetNoticeSensorControllerSlots();
std::uint32_t GetNoticeSensorBuildCount();   // goes up by one each time the game builds a notice table
