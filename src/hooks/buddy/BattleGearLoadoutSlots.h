#pragma once

#include <cstdint>

// The game keeps three D-Walker loadout slots in the save and shows them wherever walker equipment is picked: Sortie Prep's
// equipment row, the iDroid's equipment list (the item selector's walker list) and the slot screen in front of Customize.
// Sortie Prep already sends its Battle Gear equipment row to that same walker list, and nothing behind the list knows a fifth
// buddy. While the Battle Gear is the buddy, or its customize screens are open, the hooks here answer for its own three slots
// instead: which slot is current, what a slot holds, what it costs, and the field change that drops it in again.
namespace battleGearLoadoutSlots
{
    bool Install();
    void Uninstall();

    // after MissionPreparationSystemImpl::GetParamBuddyPartsFromVars filled a model viewer param for a buddy and a walker slot;
    // caller is that call's return address
    void AfterGetParam(std::uint8_t buddyType, std::uint8_t* param, std::uint8_t slot, const void* caller);

    // the menus' model viewer put this Battle Gear param on the loaded Battle Gear: in the ACC its walker wears the param's head
    void ShowOnModel(const std::uint8_t* param);
    std::uint8_t ShownHeadType();    // 0: none shown, the slot's own
    std::uint8_t ShownHeadGrade();
    void ClearShown();
}
