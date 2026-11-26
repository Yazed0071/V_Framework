#pragma once

// the walker's step-on test while it moves: the D-Walker steps onto ledges 0.3 - 0.75 m high; the Battle Gear also onto higher ones,
// up to the height its hull climbs. The game's step-on state lifts the walker by the height found.
bool Install_WalkerGearBasicActionImpl_CheckLowStepOnMove_Hook();
void Uninstall_WalkerGearBasicActionImpl_CheckLowStepOnMove_Hook();

// metres over the ground the highest thing stands that the Battle Gear's walker passes: a ledge its hull climbs, or whatever
// still passes under its belly
float BattleGearHighestStep();
