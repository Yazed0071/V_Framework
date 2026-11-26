#pragma once

#include <cstdint>

#include "BattleGearArmory.h"

// iDroid Customize > BATTLE GEAR opens the D-Walker's customize screens as the Battle Gear's: the slot screen lists its three
// loadout slots, the page edits the one picked there with only Main weapon, Sub weapon (the roof gun) and Head usable, the lists
// cut to the Battle Gear's own guns, and saves it as the Battle Gear's (V Framework's state). Titles, help and the details window
// name the Battle Gear; the D-Walker's slots and picks stay untouched.
bool Install_BattleGearCustomizePage_Patches();
void Uninstall_BattleGearCustomizePage_Patches();

namespace battleGearCustomizePage
{
    void AfterSetupItemList(std::uintptr_t callback);   // the customize item list hook calls it after the game built a list
    bool Active();                                       // the page open is the Battle Gear's

    // an R&D row that is a registered Battle Gear gun: the walker slot it fills (1 main gun, 2 sidearm), the D-Walker type there
    // and a level of its own (the page saves only what differs from the loaded slot in type or level)
    bool RegisteredPartRow(std::uint16_t developId, std::uint8_t* slot, std::uint8_t* type, std::uint8_t* level);
    std::uint16_t PickedRegisteredRow(std::uint8_t slot, std::uint8_t type);   // its develop id while it is the page's pick, else 0
    battleGearArmory::Loadout PickedGuns();   // the main gun and roof gun picked on the open page
}
