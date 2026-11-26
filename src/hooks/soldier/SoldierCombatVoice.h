#pragma once

#include <cstdint>

std::uint32_t SoldierCombatVoice_SwapLine(const std::uint8_t* soldiers, std::uint32_t slot, std::uint32_t dialogueEvent, std::uint32_t line);
void SoldierCombatVoice_OnSaid(const std::uint8_t* soldiers, std::uint32_t slot, std::uint32_t dialogueEvent, std::uint32_t line);
void SoldierCombatVoice_OnSightCheck(void* noticeController, std::uint32_t soldierIndex);
void SoldierCombatVoice_OnSoldierDamaged(std::uint32_t soldierIndex);
void SoldierCombatVoice_OnSearchTeamSent(std::uint32_t lead, std::uint32_t second);   // the CP's search after CPR0051, nearest first; 0xFFFFFFFF = none
bool SoldierCombatVoice_InAlert(const std::uint8_t* soldiers, std::uint32_t soldierIndex);   // his own CP's phase
void SoldierCombatVoice_SayLater(std::uint32_t soldierIndex, std::uint32_t dialogueEvent, std::uint32_t line, int priority,
                                 std::uint64_t afterMs, std::uint64_t quietMs = 0);   // quietMs: once he has been quiet that long
void SoldierCombatVoice_SayPrisonerOrder(const float* vehicle, std::uint32_t line);   // the nearest Afghan or Soviet soldier at the vehicle
void SoldierCombatVoice_Reset();
#ifdef _DEBUG
void SoldierCombatVoice_ProbeVoiceCall(std::uint32_t soldierIndex, std::uint32_t dialogueEvent, std::uint32_t priority, std::uint32_t line);   // TEMP probe: what cuts EVE020 off
#endif
