#pragma once

#include <cstdint>

namespace outfit
{
    // Hook on tpp::gm::player::impl::Player2UtilityImpl::
    //   SetSuitAndHandConditionWithLoadoutInfo
    // (gAddr.Player2UtilityImpl_SetSuitAndHandConditionWithLoadoutInfo
    //  = 0x1409DEFE0).
    //
    // This is the function called by the supply-drop pickup pipeline
    // (and by the mission-prep "set initial conditions" path) to apply
    // a SupplyCboxLoadoutInfo struct to the live Quark player state.
    // Verified write fields:
    //   info[0]    = playerPartsType  → state[0xF8]
    //   info[1]    = playerCamoType   → state[0xF9]
    //   info[2]    = (skin / pant?)   → state[0xBA0]
    //   info[3]    = playerFaceId u16 → state[0xFE]
    //   info[8..]  = 13-byte loadout slot block → state[0xBA1..]
    //   info[0xBC] = flag bitmask (bit 0 = "apply suit", bit 1 = "apply
    //                slots", bit 8 = "playerType valid", bit 7 = "apply
    //                face")
    //   info[0xC0] = playerType (Snake/DDMale/DDFemale/Avatar)
    //
    // Strategy: pre-hook. If the playerPartsType is in our custom range,
    // OR if the (partsType,camo) bytes look like the broken-custom
    // signal (partsType=0,camo=0xFF or camo in 0x80..0xFE), rewrite
    // the bytes to the registered outfit's values before the orig runs.
    // This fires at supply-drop crate pickup and at mission respawn —
    // both moments when the equip should "take" without going through
    // MissionPrep.
    bool Install_OutfitSuitConditionApply_Hook();
    void Uninstall_OutfitSuitConditionApply_Hook();

    // Drive a live mission-prep-style suit reload via the 3-arg
    // RequestToChangePlayerPartsInMissionPreparationMode trampoline
    // (retail 0x1462B6590). The orig wrapper uses GetPlayer2System()
    // internally (no `this` dependency) and dispatches into vtable
    // [0x140] which propagates through SetSuit → engine post-flag →
    // natural LoadPartsNew (~400ms later) → per-asset hook dispatch.
    //
    // Used by Lua-driven variant cycling and any other path that
    // needs an in-mission body re-render without going through the
    // UNIFORMS panel or supply-drop pickup machinery. The trampoline
    // bypasses our hkReqLoadout/hkSetSuit hooks (no rewrite, no
    // recursion); the synthesized loadout info is built with apply
    // flags 0x101 (suit + playerType valid) so existing weapon slots
    // and face are preserved.
    //
    // variantIndex is written into info[0x02] so OutfitCommit's
    // SetActiveVariant call uses it. Returns true if the trampoline
    // was reachable, false if the hook hasn't been installed yet.
    bool ForceLiveSuitReload(std::uint8_t playerType,
                             std::uint8_t partsType,
                             std::uint8_t selectorCode,
                             std::uint8_t variantIndex);
}
