#pragma once

#include <cstdint>

bool Install_PlayerVoiceType_Hook();
bool Uninstall_PlayerVoiceType_Hook();


void Set_PlayerVoiceTypeForType(std::uint32_t playerType, std::uint32_t voiceType);

void Clear_PlayerVoiceTypeForType(std::uint32_t playerType);

void Clear_AllPlayerVoiceTypeOverrides();


std::uint32_t Get_EffectivePlayerVoiceType(std::uint32_t playerType);
