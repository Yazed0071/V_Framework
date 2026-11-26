#pragma once

#include <cstdint>

bool Install_EquipDevelopControllerImpl_GetHeliPartsId_Hooks();
void Uninstall_EquipDevelopControllerImpl_GetHeliPartsId_Hooks();

void          HeliParts_BeginDecideWatch();
std::uint32_t HeliParts_EndDecideWatch();

void HeliParts_SetHoveredSubRow(std::uint16_t row);
bool HeliParts_BeginHoverPanel();
void HeliParts_EndHoverPanel();
bool HeliParts_TakePanelNumber(std::int32_t level, std::int32_t& number);
