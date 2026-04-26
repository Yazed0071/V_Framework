#pragma once

namespace outfit
{
    // Hook on tpp::gm::impl::SupplyCboxGameObjectImpl::RestoreRequestFromSVars
    // (gAddr.SupplyCboxGameObjectImpl_RestoreRequestFromSVars
    //  = 0x140ACA230).
    //
    // This is the function called when the player interacts with a
    // delivered supply-drop crate. ProcessLuaCommand routes Lua hash
    // 0xc1324e75 to it, and the function then reads the saved request
    // (SupplyCboxRequest stored under hash 0xee90448b1d7e on the box's
    // sub-object vtable) and tail-calls vtable[0x18] to apply.
    //
    // For a vanilla suit drop, the apply-tail-call internally chains
    // to LoadPartsNew with valid bytes and the suit equips. For our
    // custom flowIndex (e.g. Jill at 922), the apply silently no-ops
    // because the vanilla flowIndex→equipId tables don't have an entry.
    //
    // Strategy: post-orig hook. After RestoreRequestFromSVars returns,
    // check if the live player has exactly one registered custom outfit
    // matching their playerType. If so, call ForcePartsReload with that
    // outfit's bytes — guaranteed to equip the registered outfit at
    // the moment the box is opened. The vanilla apply-tail-call's
    // silent no-op leaves the player state untouched, and our forced
    // reload then writes the right bytes.
    bool Install_OutfitSupplyDropPickup_Hook();
    void Uninstall_OutfitSupplyDropPickup_Hook();
}
