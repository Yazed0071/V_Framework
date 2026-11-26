#pragma once

#include <cstdint>

namespace EquipCrossLaserBox
{
    bool Resolve();
    void Show(std::uint8_t* cross, std::uint32_t weaponId);
    void PollPrimaryKey(std::uint8_t* cross, std::uint32_t weaponId);
    void PollSecondaryKey(std::uint8_t* cross);
    std::uint32_t FirstSecondaryWeapon(std::uint8_t* cross);
}
