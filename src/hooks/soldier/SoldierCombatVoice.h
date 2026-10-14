#pragma once

#include <cstdint>

std::uint32_t SoldierCombatVoice_SwapLine(const std::uint8_t* soldiers, std::uint32_t slot, std::uint32_t dialogueEvent, std::uint32_t line);
void SoldierCombatVoice_OnSaid(const std::uint8_t* soldiers, std::uint32_t slot, std::uint32_t dialogueEvent, std::uint32_t line);
void SoldierCombatVoice_OnSightCheck(void* noticeController, std::uint32_t soldierIndex);
void SoldierCombatVoice_OnSoldierDamaged(std::uint32_t soldierIndex);
void SoldierCombatVoice_OnHeadingToLastKnownPosition(std::uint32_t soldierIndex);   // his CPR0051 call just ended
bool SoldierCombatVoice_InAlert(const std::uint8_t* soldiers, std::uint32_t soldierIndex);   // his own CP's phase
void SoldierCombatVoice_SayLater(std::uint32_t soldierIndex, std::uint32_t dialogueEvent, std::uint32_t line, int priority,
                                 std::uint64_t afterMs, std::uint64_t quietMs = 0);   // quietMs: once he has been quiet that long
void SoldierCombatVoice_Reset();
