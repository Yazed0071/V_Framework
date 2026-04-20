#pragma once

#include <cstdint>
#include <string>

namespace V_FrameWorkState
{
    // Loads the unified state file (mod/V_FrameWork/V_FrameWork_State.lua).
    // Safe to call multiple times — only loads once.
    void Load();

    // Saves the unified state file. Call after any ID allocation.
    void Save();

    // === Equip ID persistence ===

    // Resolves or creates a stable equipId for a given key.
    // Returns true on success, writes to outEquipId.
    bool ResolveOrCreateEquipId(
        const char* key,
        std::int32_t minimumId,
        std::int32_t& outEquipId);

    // Resolves or creates a stable developId for a given key.
    // Returns true on success, writes to outDevelopId.
    bool ResolveOrCreateDevelopId(
        const char* key,
        std::int32_t minimumId,
        std::int32_t& outDevelopId);

    // === Tape persistence ===

    // Resolves or creates a stable saveIndex for a given key.
    // Returns true on success, writes to outSaveIndex.
    bool ResolveOrCreateTapeSaveIndex(
        const char* key,
        std::int16_t minimumIndex,
        std::int16_t& outSaveIndex);

    // Sets the owned/new state for a tape key.
    void SetTapeOwned(const char* key, bool owned);
    void SetTapeNew(const char* key, bool isNew);

    // Sets the owned/new state by saveIndex (finds the matching tape entry).
    void SetTapeOwnedBySaveIndex(std::int16_t saveIndex, bool owned);
    void SetTapeNewBySaveIndex(std::int16_t saveIndex, bool isNew);

    // Gets the owned/new state for a tape key.
    bool GetTapeOwned(const char* key);
    bool GetTapeNew(const char* key);

    // Clears everything (for testing or reset).
    void Reset();
}
