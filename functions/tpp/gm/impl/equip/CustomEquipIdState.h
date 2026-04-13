#pragma once

#include <cstdint>

namespace CustomEquipIdState
{
    bool ResolveOrCreatePersistentEquipId(
        const char* gunKey,
        std::int32_t minimumEquipId,
        std::int32_t& outEquipId);

    bool TryGetPersistentEquipId(const char* gunKey, std::int32_t& outEquipId);
    bool SaveNow();
    void ClearAllPersistentEquipIds();
}