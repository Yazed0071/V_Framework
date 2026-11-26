#pragma once

#include <cstdint>

bool Install_CommandPost2Impl_Initialize_Hook();
void Uninstall_CommandPost2Impl_Initialize_Hook();

// member slots of the command post tables the game built: 360 when vanilla
std::uint32_t GetCommandPostMemberCapacity(const std::uint8_t* commandPost);
std::uint32_t GetCommandPostBuildCount();   // goes up by one each time the game builds the command post tables
