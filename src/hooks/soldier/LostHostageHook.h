#pragma once

#include <cstdint>


void Add_LostHostageTrap(std::uint32_t gameObjectId, int hostageType);


void Remove_LostHostageTrap(std::uint32_t gameObjectId);


void Clear_LostHostagesTrap();


void PlayerTookHostage(std::uint32_t gameObjectId, bool playerTookHostage);


bool Install_LostHostage_Hooks();


bool Uninstall_LostHostage_Hooks();