#pragma once

namespace outfit
{
    // Hook on tpp::ui::menu::impl::MissionPreparationCallbackImpl::IsEnableCurrentHeadOption
    // (gAddr.MissionPrep_IsEnableCurrentHeadOption = 0x14A56BA20).
    //
    // The vanilla function gates the HEAD OPTION submenu — if it
    // returns false, the user cannot open the submenu for the current
    // outfit. For custom outfits, we override the gate based on the
    // outfit's `supportsHeadOptions` flag.
    //
    // PARTIAL DELIVERY NOTE:
    //   Phase 3 ships the GATE only. The submenu itself (the function
    //   that builds the head-option list when the user enters it) is
    //   a vtable-slot call at *(self+0x48) → vtable[+0x470] per Phase 1.
    //   The owning class of self+0x48 was not isolated by the Phase-1
    //   decomp pass, so we cannot safely hook it without runtime
    //   probing. Until that target is identified, custom outfits
    //   that support head options will see the vanilla submenu when
    //   the user enters it — which renders vanilla SP/balaclava
    //   entries, NOT the custom outfit's headOptionEquipIds[]. The
    //   selected head equipId still drives runtime appearance via
    //   our existing parts-load hooks.
    //
    //   Phase 5 (or a follow-up runtime probe) will identify the
    //   submenu vtable target and complete this integration.
    bool Install_OutfitHeadOption_Hook();
    void Uninstall_OutfitHeadOption_Hook();
}
