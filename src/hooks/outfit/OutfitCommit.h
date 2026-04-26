#pragma once

#include <cstdint>

namespace outfit
{
    // Hook on Player2UtilityImpl::RequestToChangePlayerPartsInMissionPreparationMode
    // (gAddr.MissionPrep_RequestToChangePlayerPartsInMissionPreparationMode = 0x14973DA60).
    //
    // Real signature (verified mgsvtpp.exe_Addresses.txt:128150140):
    //   void(self, int param2, undefined8* blob, u8 apply)
    //
    // The function copies blob[0..0xC0] to a stack-local buffer and
    // forwards to AttackActionImpl::RequestToChangePlayerPartsInMissionPreparationMode
    // via vtable[0x208] of an inner sub-object. The copy happens AFTER
    // our hook returns to the body, so any blob rewrite we apply
    // before calling orig is preserved through the forward.
    //
    // The blob layout is opaque in the decomp. Phase 2 logs raw blob
    // bytes on every commit (low volume — user-driven event) so we
    // observe what the game pushes; Phase 3 / Phase 4 will tighten
    // the rewrite once layout is confirmed by live data.
    bool Install_OutfitCommit_Hook();
    void Uninstall_OutfitCommit_Hook();
}
