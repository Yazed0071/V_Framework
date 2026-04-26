#pragma once

namespace outfit
{
    // Hook on tpp::ui::menu::impl::ItemSelectorCallbackImpl::SupplyDropSuitSetup
    // (gAddr.SupplyDropSuitSetup = 0x1416A7610).
    //
    // Vanilla SupplyDropSuitSetup reads selectedFlowIndex from the
    // ItemSelector per-row table, looks up an entry in the per-equipId
    // info array (vtable[0x718] on this[0x58]), reads byte at +0x36
    // (equip-category), and if it equals 0x13 (suit), populates the
    // supply-drop state buffer at this+0x46250.
    //
    // For our custom outfits, the per-equipId array lookup is OOB —
    // 922 * 0x68 = 0xF290 bytes past the array base, returning garbage
    // that doesn't match 0x13. Result: state buffer stays uninitialized,
    // downstream supply-drop fulfillment can't resolve the suit, and
    // LoadPartsNew gets garbage bytes (partsType=0, camo=0xFF).
    //
    // Strategy: pre-hook. Compute selectedFlowIndex same way the orig
    // does. If it matches a registered custom outfit, manually populate
    // the state buffer with our outfit's bytes and return early —
    // skipping the OOB lookup entirely. Vanilla flowIndices fall through
    // to the orig.
    bool Install_OutfitSupplyDropSetup_Hook();
    void Uninstall_OutfitSupplyDropSetup_Hook();
}
