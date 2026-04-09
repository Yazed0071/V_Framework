#pragma once

#include <cstdint>

// Installs the combined custom tape state hooks.
// Params: none
bool Install_CustomTapeOwnership_Hooks();

// Removes the combined custom tape state hooks.
// Params: none
bool Uninstall_CustomTapeOwnership_Hooks();

// Returns true when one saveIndex belongs to the custom tape range.
// Params: saveIndex
bool IsCustomTapeSaveIndex(std::int16_t saveIndex);

// Returns true when one custom saveIndex is owned in the external custom state.
// Params: saveIndex
bool IsCustomTapeOwnedSaveIndex(std::int16_t saveIndex);

// Returns true when one custom saveIndex has the external new flag set.
// Params: saveIndex
bool IsCustomTapeNewFlagSaveIndex(std::int16_t saveIndex);

// Registers one custom tape metadata record so the save file can write saveIndex + albumId + fileName.
// Params: saveIndex, albumId, fileName
void Register_CustomTapeStateTrackMetadata(std::int16_t saveIndex, const char* albumId, const char* fileName);

// Resolves one stable custom save index for this albumId + fileName.
// Reuses an existing entry when found.
// If requestedSaveIndex conflicts, falls back to the next available custom index.
// Params: requestedSaveIndex, albumId, fileName, outResolvedSaveIndex, outWasCreated
bool ResolveOrCreateCustomTapeSaveIndex(
    std::int16_t requestedSaveIndex,
    const char* albumId,
    const char* fileName,
    std::int16_t& outResolvedSaveIndex,
    bool& outWasCreated);

// Initializes owned/new only when the persist entry is first created.
// Params: saveIndex, owned, isNew
void InitializeCustomTapeStateIfMissing(
    std::int16_t saveIndex,
    bool owned,
    bool isNew);

// Syncs all loaded custom tape owned/new state into the live cassette flag table.
// Params: none
void Sync_CustomTapeStateToLiveTable();