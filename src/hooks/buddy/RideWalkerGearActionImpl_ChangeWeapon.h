#pragma once

// the rider's weapon change and HUD weapon update, each frame of the drive state: the game fills the sidearm's rounds and bar from
// the walker's sidearm. On the Battle Gear that slot is the roof gun: its own rounds left, and its heat on the bar
bool Install_RideWalkerGearActionImpl_ChangeWeapon_Hook();
void Uninstall_RideWalkerGearActionImpl_ChangeWeapon_Hook();
