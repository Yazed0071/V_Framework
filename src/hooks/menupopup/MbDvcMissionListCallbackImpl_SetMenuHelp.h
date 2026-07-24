#pragma once

#include <cstdint>

bool Set_MissionMenuHelp(std::uint16_t missionCode, const char* langId, const char* colorName);
void Clear_MissionMenuHelp(std::uint16_t missionCode);

bool Install_MissionMenuHelp_Hook();
bool Uninstall_MissionMenuHelp_Hook();
