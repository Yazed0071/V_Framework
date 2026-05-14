#pragma once

#include <cstdint>


void* GetSoldierSoundControllerImpl(std::uint32_t gameObjectId);


std::uint32_t GetSoldierSlotFromGameObjectId(std::uint32_t gameObjectId);


std::uint32_t GetSoldierIndexFromSoundSlot(std::uint32_t soundSlot);


bool Install_SoldierVoiceTypeQuery_Hook();
bool Uninstall_SoldierVoiceTypeQuery_Hook();