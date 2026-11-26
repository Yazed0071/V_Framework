#pragma once

// the Battle Gear's rail-gun waits 1, 2 or 3 seconds after a level 1, 2 or full shot instead of the stun gun's 1 second, and
// reports while it holds a charge (the effects module shows the charge glow at the rail-gun muzzle)
bool Install_WalkerGearAttackActionImpl_State_FireStunGun_Hook();
void Uninstall_WalkerGearAttackActionImpl_State_FireStunGun_Hook();
