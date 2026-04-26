#pragma once

namespace outfit
{
    // Hook on tpp::ui::menu::impl::ItemSelectorCallbackImpl::
    // DecideActMissionPreparationSetEquipMode
    // (gAddr.ItemSelectorCallbackImpl_DecideActMissionPreparationSetEquipMode
    //  = 0x1416A3670).
    //
    // Fires when the user clicks a row in the sortie UNIFORMS panel.
    // The callback walks its own internal selection state to read the
    // selectedId for the clicked row, then dispatches commit logic.
    //
    // Our hook reads that same selection state (via the documented
    // self+0x4434 / +0x4440 / +0xC040 / +0xCC40 / +0x10C+0x110 layout
    // — see the cpp comment on the +0x10C scroll-page base) BEFORE
    // calling orig:
    //   - if equipKind == 0x80 (UNIFORMS suit) AND selectedId matches
    //     a registered custom outfit's flowIndex (or developId), we
    //     publish the matching developId via SetPendingOutfitDevelopId.
    //   - OutfitCommit's broken-custom rewrite path consumes that
    //     pending developId to fill in real partsType/selector/variant
    //     bytes before the game commits.
    //
    // For vanilla suits the hook does nothing — selection state is
    // read but no pending value is published, so OutfitCommit falls
    // through to its passthrough path.
    bool Install_OutfitItemSelector_Hook();
    void Uninstall_OutfitItemSelector_Hook();
}
