#pragma once

// the D-Walker gear getters (main weapon type/grade, sidearm type/grade): every native reader goes through them,
// so while the Battle Gear is out they answer with its loadout and the D-Walker's saved loadout stays untouched
bool Install_BuddyServiceImpl_GetBuddyGear_Hooks();
void Uninstall_BuddyServiceImpl_GetBuddyGear_Hooks();
