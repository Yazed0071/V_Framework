#pragma once

#include <cstdint>

bool AccSeatIdleMotion_Install();
void AccSeatIdleMotion_Uninstall();
bool AccSeatIdleMotion_Set(std::uint64_t clipId);
std::uint64_t AccSeatIdleMotion_Replacement();
bool AccSeatIdleMotion_SetDropSeatPose(std::uint64_t clipId, std::uint32_t missionCode, bool anyMission);
bool SwapGani(std::uint64_t from, std::uint64_t to);
bool RestoreGani(std::uint64_t from, std::uint64_t to);
void RestoreAllGani();
