#pragma once

#include <cstdint>

// The roof M2 as the Battle Gear's own gun, the way the tanks carry their cupola gun: while it is the selected weapon the trigger
// fires it, a real round every 0.125 s with the cupola gun's sound, flash, belt link and heat, from its 500 rounds. The D-Walker's
// own weapon slot stays on the main gun, so its sidearm and arm never take the trigger.
namespace battleGearRoofGun
{
    // each frame of the Battle Gear's walker, ridden or not
    void OnWalkerFrame(void* walkerSystem, std::uint32_t index, const std::uint8_t* comm, std::uint16_t commId, float dt);
}
