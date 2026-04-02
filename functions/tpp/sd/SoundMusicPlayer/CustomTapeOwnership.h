#pragma once

#include <cstdint>

// Installs the custom tape ownership hooks.
// Params: none
bool Install_CustomTapeOwnership_Hooks();

// Removes the custom tape ownership hooks.
// Params: none
bool Uninstall_CustomTapeOwnership_Hooks();

// Returns true when this save index belongs to the custom ownership range.
// Params: saveIndex
bool IsCustomTapeSaveIndex(std::int16_t saveIndex);

// Returns true when this custom save index is currently owned.
// Params: saveIndex
bool IsCustomTapeOwnedSaveIndex(std::int16_t saveIndex);