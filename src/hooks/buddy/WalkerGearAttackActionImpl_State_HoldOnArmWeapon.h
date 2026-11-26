#pragma once

// the Battle Gear's heavy MG (its sidearm slot): 900 rounds a minute, belt-fed (never reloads), overheats after 5 seconds of
// fire and then holds fire for 3 seconds; heat drains twice as fast as it builds
bool Install_WalkerGearAttackActionImpl_State_HoldOnArmWeapon_Hook();
void Uninstall_WalkerGearAttackActionImpl_State_HoldOnArmWeapon_Hook();
