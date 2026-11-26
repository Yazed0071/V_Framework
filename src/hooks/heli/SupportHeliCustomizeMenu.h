#pragma once

#include <cstdint>

namespace SupportHeliCustomizeMenu
{
    void AfterSetupItemList(std::uintptr_t callback);                       // adds the outfit / gatling / side gun rows and fills their lists
    bool DecidePart(void* callback, std::uint32_t itemIndex);               // true: one of the three custom lists took the pick
    bool HoverPart(void* callback);                                         // true: one of the three custom lists is open
}

bool Install_SupportHeliCustomizeMenu_Hooks();
void Uninstall_SupportHeliCustomizeMenu_Hooks();
