#pragma once

namespace buddyoutfit { struct Entry; }

bool Install_EquipDevelopControllerImpl_GetDWalkerParts_Hooks();
bool Uninstall_EquipDevelopControllerImpl_GetDWalkerParts_Hooks();
bool IsUsableWalkerSidearm(const buddyoutfit::Entry& e);
