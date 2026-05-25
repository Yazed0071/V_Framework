#pragma once

#include <cstdint>

bool Install_ShowMissionIcon_Hook();
bool Uninstall_ShowMissionIcon_Hook();

bool ShowMissionIcon_SetTitleHash(std::uint64_t hash48);
bool ShowMissionIcon_SetTitleOverride(const char* text);
bool ShowMissionIcon_PatchTitleHash(std::uint64_t value);
