#pragma once

#include <cstdint>


bool Install_MissionEmergency_Hook();
bool Uninstall_MissionEmergency_Hook();

void MissionEmergency_SetEnabled(std::uint16_t missionCode, bool enabled);
bool MissionEmergency_IsEnabled(std::uint16_t missionCode);
void MissionEmergency_ClearAll();

void MissionEmergency_SetSortiePrepEnabled(std::uint16_t missionCode, bool enabled);
bool MissionEmergency_IsSortiePrepEnabled(std::uint16_t missionCode);
