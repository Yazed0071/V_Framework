#pragma once
#include <cstdint>

namespace FakeSurrender
{
    void OnSetCommand(std::uint32_t soldierIndex, const std::uint8_t* command);
    bool IsFaking(std::uint8_t* soldier, std::uint32_t soldierIndex);
    void Clear();
}
