#pragma once

#include <cstdint>

bool Install_PlayerVoiceType_Hook();
bool Uninstall_PlayerVoiceType_Hook();


void Set_PlayerVoiceTypeForType(std::uint32_t playerType, std::uint32_t voiceType,
                                std::uint32_t dialogueEvent = 0, std::uint32_t dialogueEvent2 = 0);

void Clear_PlayerVoiceTypeForType(std::uint32_t playerType);

void Clear_AllPlayerVoiceTypeOverrides();

void Reset_PlayerVoiceLiveSlot();


std::uint32_t Get_EffectivePlayerVoiceType(std::uint32_t playerType);
