#pragma once

#include <cstdint>

void SoldierVoiceSwap_OnHeld(std::uint16_t soldierIndex, bool held);
bool SoldierVoiceSwap_SayJammedTake(const std::uint8_t* soldiers, std::uint32_t slot);   // alert only, adult voices
void SoldierVoiceSwap_OnSoldierDamages(void* damageHandler, std::uint32_t soldierIndex, const void* damages, std::uint32_t count);
