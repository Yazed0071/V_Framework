#pragma once

// the D-Walker main weapon setters: a walker weapon change writes its new main gun into the D-Walker's saved loadout;
// while the Battle Gear carries its own loadout those writes are dropped, so the D-Walker keeps its own gun
bool Install_BuddyServiceImpl_SetBuddyGearMainWP_Hooks();
void Uninstall_BuddyServiceImpl_SetBuddyGearMainWP_Hooks();
