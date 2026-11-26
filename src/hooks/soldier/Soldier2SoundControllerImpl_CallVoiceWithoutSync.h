#pragma once

#include <cstdint>

void SoldierVoiceSwap_OnHeld(std::uint16_t soldierIndex, bool held);
void SoldierVoiceSwap_ClearHeld();
void SoldierVoiceSwap_ClearForReload();
bool SoldierVoiceSwap_SayJammedTake(const std::uint8_t* soldiers, std::uint32_t slot);
void SoldierVoiceSwap_SayCallOut(std::uint32_t soldierIndex);   // "CP, answer! CP!" + a give-up line, any phase, once per 10 s
void SoldierVoiceSwap_OnSoldierDamages(void* damageHandler, std::uint32_t soldierIndex, const void* damages, std::uint32_t count);
