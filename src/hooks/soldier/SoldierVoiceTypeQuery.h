#pragma once

#include <cstdint>


// Returns the most recently observed Soldier2SoundControllerImpl* (impl base)
// associated with the soldier's slot. Returns nullptr if not seen yet.
// Lets other modules walk the audio chain (e.g. per-soldier RTPC).
void* GetSoldierSoundControllerImpl(std::uint32_t gameObjectId);


// Returns the slot index (== soldierIndex) for the gameObjectId, or 0xFFFF
// if the soldier-class precondition fails.
std::uint32_t GetSoldierSlotFromGameObjectId(std::uint32_t gameObjectId);


// Translates a sound-controller slot back to the soldier index (game-side,
// lower 9 bits of gameObjectId). Returns 0xFFFF if not captured yet.
//
// Populated by Soldier2SoundControllerImpl::Activate, which is called with
// (soldierIndex, soundSlot) once per soldier at mission load.
std::uint32_t GetSoldierIndexFromSoundSlot(std::uint32_t soundSlot);


bool Install_SoldierVoiceTypeQuery_Hook();
bool Uninstall_SoldierVoiceTypeQuery_Hook();
