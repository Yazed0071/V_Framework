#pragma once

// the walker stun gun shot: for the Battle Gear it becomes the rail-gun (its own barrel, long and thin, stops at the
// first wall, ATK_BattleGear_RailGun)
bool Install_WalkerGearAttackActionImpl_RegisterStunGunOffenseTarget_Hook();
void Uninstall_WalkerGearAttackActionImpl_RegisterStunGunOffenseTarget_Hook();
