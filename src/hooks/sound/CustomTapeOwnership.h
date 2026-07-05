#pragma once

#include <cstdint>


bool Install_CustomTapeOwnership_Hooks();


bool Uninstall_CustomTapeOwnership_Hooks();


bool IsCustomTapeSaveIndex(std::int16_t saveIndex);
bool IsCustomTapeOwnedSaveIndex(std::int16_t saveIndex);
bool IsCustomTapeOwnedInLiveTable(std::int16_t saveIndex);
bool IsCustomTapeNewFlagSaveIndex(std::int16_t saveIndex);


void Register_CustomTapeStateTrackMetadata(std::int16_t saveIndex, const char* albumId, const char* fileName);


bool ResolveOrCreateCustomTapeSaveIndex(
    std::int16_t requestedSaveIndex,
    const char* albumId,
    const char* fileName,
    std::int16_t& outResolvedSaveIndex,
    bool& outWasCreated);


void InitializeCustomTapeStateIfMissing(
    std::int16_t saveIndex,
    bool owned,
    bool isNew);


void Sync_CustomTapeStateToLiveTable();


void OnCassetteTrackPlayedByTrackId(std::uint32_t playedTrackId);