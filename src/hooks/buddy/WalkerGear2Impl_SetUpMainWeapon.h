#pragma once

bool Install_WalkerGear2Impl_SetUpMainWeapon_Hook();
void Uninstall_WalkerGear2Impl_SetUpMainWeapon_Hook();

// Re-hides the Battle Gear's leftover D-Walker parts if the game showed them again; walkerInterface is the walker's object interface.
void KeepBattleGearWalkerPartsHidden(void* walkerInterface);

// Puts the buddy walker's main gun model at a world matrix (16 floats, 16-byte aligned) and re-poses it there.
void PlaceBattleGearMainGun(void* walkerInterface, const float* matrix);
