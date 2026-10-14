#pragma once

#include <cstdint>

namespace AnimalInstance
{
    enum class Presence
    {
        Unknown,
        InWorld,
        Gone,   // disabled, fultoned, or a pool slot no locator uses
    };

    struct DamageRequest
    {
        std::uint16_t attackId   = 0;
        std::uint16_t attackerId = 0xFFFF;
        bool          headShot   = false;
        bool          oneHitKill = false;
    };

    bool IsAnimal(std::uint32_t gameObjectId);
    Presence Check(std::uint32_t gameObjectId, const float reportedPosition[3]);
    bool AddDamage(std::uint32_t gameObjectId, const DamageRequest& request, const char** reason);
}
