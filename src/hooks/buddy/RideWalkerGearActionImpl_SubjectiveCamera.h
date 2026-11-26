#pragma once

// riding the Battle Gear the tanks' way: their camera button toggles first person, and the eye sits in the turret's cupola as
// the posed turret carries it
bool Install_RideWalkerGearActionImpl_SubjectiveCamera_Hooks();
void Uninstall_RideWalkerGearActionImpl_SubjectiveCamera_Hooks();

bool BattleGearFirstPersonEye(float* eye);   // where the first-person camera sits this frame; false outside first person
