#pragma once

#include <cstdint>


// customLostLabel: optional StrCode32 speech-label hash. If non-zero, the
// "prisoner gone" radio for this hostage uses this label instead of the
// built-in MALE/FEMALE/CHILD × TAKEN/NOT_TAKEN matrix. 0 keeps the default.
void Add_LostHostageTrap(std::uint32_t gameObjectId, int hostageType, std::uint32_t customLostLabel = 0);


void Remove_LostHostageTrap(std::uint32_t gameObjectId);


void Clear_LostHostagesTrap();


void PlayerTookHostage(std::uint32_t gameObjectId, bool playerTookHostage);


bool Install_LostHostage_Hooks();


bool Uninstall_LostHostage_Hooks();