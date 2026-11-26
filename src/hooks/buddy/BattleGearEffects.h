#pragma once

#include <cstdint>

// the Battle Gear's own effects: the hangar cutscene's downwash under it while it glides in fire posture, the stun gun's charge
// glow at the rail-gun muzzle while a charge is held, and the tanks' cupola gun's effects on the roof M2: its muzzle flash for each
// shot, its barrel heat while the barrel is warm, its barrel smoke while it is overheated, and a belt link thrown for each round
namespace battleGearEffects
{
    bool Install();
    void Uninstall();

    // the walker update while the Battle Gear is out
    void OnFieldFrame();

    // mission end, checkpoint reload, Battle Gear gone: every effect goes at once
    void Drop();

    // the roof M2 fired this many rounds: each throws a belt link from its feed, the way the tanks' cupola gun does
    void EjectBeltLinks(std::uint32_t rounds);
}
