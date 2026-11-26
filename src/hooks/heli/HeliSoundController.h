#pragma once

#include <cstdint>

struct lua_State;

bool Install_HeliSoundController_Hook();
bool Uninstall_HeliSoundController_Hook();

bool Play_PilotCallVoice(std::uint32_t voiceId, std::uint32_t slot, std::uint32_t voiceType, std::uint32_t param4);
void PilotVoice_OnHeliDamaged(void* handler, std::uint32_t heliIndex, std::uint16_t attackId, const float hit[3], float lifeBefore, float lifeAfter);
void PilotVoice_OnDoorMoving(std::uint32_t heliType, std::uint32_t heliIndex, bool opening);
void PilotVoice_EnsureBoardingRadioHookInstalled(lua_State* L);   // wraps TppRadio.PlayCommonRadio for the boarding lines

void Set_HeliTargetSpeed(std::uint32_t heliKey, float targetSpeedKmh, float changeTime);
void Set_HeliPilotAiVoiceEnabled(std::uint32_t heliKey, bool enabled);
void Clear_HeliSoundControllerOverrides();
void HeliSoundController_ClearForReload();   // queued pilot lines and their timing, keeps SetEnablePilotAiVoice
