#pragma once

#include <cstdint>

void CpRadioVariants_OnRadioOnAir(std::uint32_t cp, std::uint8_t radioType, std::uint16_t speaker);
void CpRadioVariants_OnRadioOffAir(std::uint32_t cp, std::uint8_t radioType, std::uint16_t speaker);   // StepRadio left the call
void CpRadioVariants_OnCpIdle(std::uint32_t cp);   // a mortar call the queue pushed out is asked again
bool CpRadioVariants_SpeakSoldierHalf(std::uint32_t priority, std::uint32_t event, std::uint32_t voiceType, std::uint32_t label);   // true when a real soldier said it
std::uint64_t CpRadioVariants_RadioHeardMs(std::uint32_t soldierIndex);   // last tick a CP call with him in it was on air, 0 if never
void CpRadioVariants_OnCpChannelVoice(void* soundController, std::uint32_t owner, std::uint32_t voiceType, std::uint32_t label);   // every SoundControllerImpl::CallVoice
bool CpRadioVariants_HoldChaffJam(std::uint32_t cp, std::uint32_t soldierIndex);   // true while he still has to finish his request
void CpRadioVariants_OnSoldierDamages(std::uint32_t soldierIndex, const void* damages, std::uint32_t count);   // the support heli hitting a soldier

bool Install_CpRadioVariants_Hooks();
bool Uninstall_CpRadioVariants_Hooks();
