#pragma once

namespace outfit
{
    // Installs hooks on:
    //   - tpp::mbm::impl::EquipDevelopControllerImpl::IsEquipDeveloped
    //     (gAddr.IsEquipDeveloped = 0x14951F860)
    //   - tpp::ui::menu::impl::MissionPreparationCallbackImpl::GetEquipIdFromLoadoutInfo
    //     (gAddr.GetEquipIdFromLoadoutInfo = 0x1416BB9C0)
    //
    // These two hooks together solve forward and reverse mapping for
    // custom outfits — IsEquipDeveloped reports our flowIndices as
    // "developed" (R&D gate), GetEquipIdFromLoadoutInfo returns the
    // currently-equipped flowIndex when the live partsType is custom,
    // which causes the game's own SetupEquipPanelParam to render the
    // Equipped badge on the correct row without any UI-side hack.
    bool Install_OutfitEquippedState_Hooks();
    void Uninstall_OutfitEquippedState_Hooks();

    // Live EquipDevelopControllerImpl* captured from IsEquipDeveloped's
    // self argument on first fire. Used by Phase 3 list-injection to
    // walk EDC's row table without a Quark chain walk. Returns null
    // until the game has fired at least one IsEquipDeveloped call.
    void* GetCachedEquipDevelopController();

    // Calls the orig IsEquipDeveloped via the cached EDC pointer.
    // Returns true if the bit is set for this flowIndex. Returns
    // false if either the EDC isn't cached yet or the orig hook
    // isn't installed. Used by panel-build injection to gate which
    // registered outfits appear: undeveloped (bit=0) outfits are
    // skipped so the player only sees what they've researched.
    bool IsFlowIndexDevelopedByOrig(unsigned short flowIndex);
}
