#pragma once

#include <cstdint>

namespace HeliLineOfSight
{
    constexpr std::uint32_t kRayMask = 0x04010700u;   // what the heli's own attack check passes to this same query

    // false when the query could not run; *blocked tells whether world geometry lies between the two points
    bool Blocked(const float* from, const float* to, std::uint32_t mask, bool* blocked);
}
