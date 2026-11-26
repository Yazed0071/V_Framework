#pragma once

#include <cstdint>

bool Install_LightActionImpl_Execute_Hook();
bool Uninstall_LightActionImpl_Execute_Hook();

void HeliSearchLight_StartAutoAim(std::uint32_t heliKey, std::uint32_t targetId);
void HeliSearchLight_StopAutoAim(std::uint32_t heliKey);
void HeliSearchLight_SetAngle(std::uint32_t heliKey, float yawDegrees, float pitchDegrees);
void HeliSearchLight_ClearAngle(std::uint32_t heliKey);
void HeliSearchLight_Clear();
void HeliSearchLight_Pump();
