#pragma once

#include <cstdint>

namespace outfit
{
    bool Install_OutfitAbilities_Hooks();
    void Uninstall_OutfitAbilities_Hooks();
    void Reset_PartsTypeChangeTracking();
    std::uint8_t SprintLive();
    std::uint64_t SprintSparkLive();
}
