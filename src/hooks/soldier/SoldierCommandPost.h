#pragma once
#include <cstdint>

namespace SoldierCommandPost
{
    constexpr std::uint8_t kNone = 0xFF;

    std::uint8_t IndexOf(const std::uint8_t* soldiers, std::uint32_t soldierIndex);
}
