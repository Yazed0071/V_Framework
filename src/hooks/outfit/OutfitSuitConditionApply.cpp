#include "pch.h"

#include "OutfitSuitConditionApply.h"
#include "OutfitRegistry.h"

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    // Verified signature (mgsvtpp.exe_Addresses.txt:7981094..7981196):
    //   RCX = this (Player2UtilityImpl*)
    //   RDX = SupplyCboxLoadoutInfo*
    using SetSuitAndHandConditionWithLoadoutInfo_t =
        void (__fastcall*)(void* self, void* loadoutInfo);

    static SetSuitAndHandConditionWithLoadoutInfo_t g_Orig = nullptr;
    static bool g_Installed = false;

    // Player2UtilityImpl::RequestToChangePlayerPartsInMissionPreparationMode
    // (3-arg variant — takes SupplyCboxLoadoutInfo*, not a raw blob).
    // Verified retail 0x1462B6590. Different from the 4-arg blob variant
    // OutfitCommit hooks (which is 0x14973DA60). The 3-arg wrapper is
    // the public API supply-drop pickup uses to request a suit change
    // via the loadout struct, so it's the natural intercept point for
    // catching the box-open equip event.
    using RequestToChangeLoadout_t =
        void (__fastcall*)(void* self, void* loadoutInfo, std::uint8_t apply);

    static RequestToChangeLoadout_t g_OrigReqLoadout    = nullptr;
    static bool                     g_InstalledReqLoadout = false;

    // Wrapper around SetSuit that ALSO applies the per-slot loadout buffer
    // to the player via Player2UtilityImpl::vtable[0x218]. Verified at
    // named-build line 5963287 (FUN_1462c93f0):
    //   void FUN_1462c93f0(Player2UtilityImpl*, SupplyCboxLoadoutInfo*) {
    //       SetSuitAndHandConditionWithLoadoutInfo(p1, p2);  // body change
    //       memset(&local_128, 0, 0xA0);
    //       /* build 3-slot buffer based on info[0xBC] flag bits 2/3/4 */
    //       /* zero slot if flag bit not set, else copy info[0x18+] */
    //       /* ... face apply, 0x54-0xA1 fields apply ... */
    //       (vtable[0x218])(p1, &local_128);  // APPLY to player
    //       p1[0x190] |= 8;  // post-flag
    //       Player2System+0x204 |= 0x80000;  // post-flag
    //   }
    //
    // The slot-clear-when-no-flag is what makes weapon slots show NONE
    // while wearing custom suits — broken-custom suit equip uses
    // flags=0x81 (bits 0+7 only, no slot bits) so the apply zeroes
    // slots. Vanilla weapon clicks use flags with slot bits set so
    // their click data populates the new weapon.
    using LoadoutApplyAfterSetSuit_t =
        void (__fastcall*)(void* self, void* loadoutInfo);

    static LoadoutApplyAfterSetSuit_t g_OrigLoadoutApply    = nullptr;
    static bool                       g_InstalledLoadoutApply = false;

    // tpp::gm::player::impl::Player2UtilityImpl::SetInitialConditionWithLoadoutInfo
    // (retail 0x1462C7670). Verified at named-build line 5962858:
    //   void(this, info, char preserve) {
    //       SetSuitAndHandConditionWithLoadoutInfo(this, info);  // body change
    //       // 3-iteration weapon-slot apply loop:
    //       for each of 3 slots:
    //           if ((slot_keep_bit & info[0xBC]) == 0) {
    //               if (preserve == 0) {
    //                   *(u16*)(quark_live + 0x520 + slot*2) = 0;  // CLEAR
    //                   *(u16*)(quark_live + 0x548 + slot*2) = 0;
    //                   *(u8 *)(quark_live + 0x57E + slot)   = 1;
    //               }
    //               // else: preserve mode — keep existing slot values
    //           } else {
    //               // copy slot data from info to player state
    //           }
    //       // additional camo/face/etc. blocks
    //       if (preserve == 0) Quark[0x130]->vtable[0x68](info[0xB9]);
    //   }
    //
    // For supply-drop custom outfit equip the orig caller passes
    // preserve=0 with info[0xBC]=0x01 (suit only, no slot keep bits) →
    // 3 slots cleared → weapon icons disappear in SORTIE PREP.
    //
    // Strategy: spoof preserve=1 when we detect a custom partsType in
    // info[0]. Skipping the slot-clear branch preserves the player's
    // existing weapon slots intact. Vanilla equips with valid slot data
    // pass through with preserve unchanged.
    using SetInitialConditionWithLoadoutInfo_t =
        void (__fastcall*)(void* self, void* loadoutInfo, std::uint8_t preserve);

    static SetInitialConditionWithLoadoutInfo_t g_OrigSetInitial    = nullptr;
    static bool                                 g_InstalledSetInitial = false;

    // SupplyCboxLoadoutInfo field offsets (verified from
    // SetSuitAndHandConditionWithLoadoutInfo disasm, retail addresses).
    // These differ from the SupplyDropSuitSetup state buffer layout —
    // SupplyCboxLoadoutInfo is the post-translation struct passed to
    // the actual equip pipeline.
    constexpr std::size_t kInfoOff_PartsType  = 0x00;  // u8
    constexpr std::size_t kInfoOff_CamoType   = 0x01;  // u8
    constexpr std::size_t kInfoOff_FaceId     = 0x03;  // u16
    constexpr std::size_t kInfoOff_Flags      = 0xBC;  // u32
    constexpr std::size_t kInfoOff_PlayerType = 0xC0;  // u8

    // Flag bits in info[0xBC] (decoded from disasm jump-table at
    // SetSuitAndHandConditionWithLoadoutInfo+0x35..+0x153):
    //   bit 0 (0x001) = "apply suit bytes"  (info[0..2])
    //   bit 1 (0x002) = "apply 13-byte loadout block" (info[8..])
    //   bit 7 (0x080) = "apply face id"     (info[3])
    //   bit 8 (0x100) = "playerType valid"  (info[0xC0])

    // Inspect a SupplyCboxLoadoutInfo, log it, and (if it carries a
    // suit and we can identify a registered custom outfit) rewrite
    // info[0]/info[1]/info[0xC0]/info[0xBC] in-place. Returns true if
    // a rewrite occurred. Used by both hkSetSuit and hkReqLoadout.
    static bool InspectAndRewriteLoadout(void* info, const char* tag)
    {
        if (!info) return false;

        auto* base = reinterpret_cast<std::uint8_t*>(info);

        std::uint8_t  partsType  = 0;
        std::uint8_t  camoType   = 0;
        std::uint8_t  playerType = 0;
        std::uint32_t flags      = 0;

        __try
        {
            partsType  = base[kInfoOff_PartsType];
            camoType   = base[kInfoOff_CamoType];
            playerType = base[kInfoOff_PlayerType];
            flags      = *reinterpret_cast<std::uint32_t*>(base + kInfoOff_Flags);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("[OutfitSuitConditionApply:%s] SEH reading info\n", tag);
            return false;
        }

        Log("[OutfitSuitConditionApply:%s] fire: partsType=0x%02X "
            "camo=0x%02X playerType=%u flags=0x%X (info=%p)\n",
            tag,
            static_cast<unsigned>(partsType),
            static_cast<unsigned>(camoType),
            static_cast<unsigned>(playerType),
            flags, info);

        const bool applySuit = (flags & 0x1u) != 0;
        if (!applySuit) return false;

        const outfit::OutfitEntry* chosen = nullptr;
        const char*                via    = nullptr;

        // 1. partsType already in custom range — direct lookup.
        if (partsType >= outfit::kCustomPartsTypeStart
         && partsType <= outfit::kCustomPartsTypeEnd)
        {
            outfit::TryGetOutfitByPartsType(partsType, &chosen);
            if (chosen) via = "by-partsType";
        }

        // 2. camo in custom selector range.
        if (!chosen
         && camoType >= outfit::kCustomSelectorStart
         && camoType <= outfit::kCustomSelectorEnd)
        {
            outfit::TryGetOutfitBySelectorCode(camoType, &chosen);
            if (chosen) via = "by-selectorCode";
        }

        // 3. Broken-custom signal (partsType=0, camo=0xFF) — resolve
        //    ONLY via pendingDevId from a recent ItemSelector click.
        //
        // The previous "live-PT-unique" fallback (resolve to the only
        // registered outfit matching the live player's playerType) was
        // removed 2026-04-26 because it hijacked legitimate vanilla
        // selections: when the user picks a vanilla suit while a
        // custom outfit is already equipped, the orig pipeline emits
        // a camo=0xFF transient and the fallback would silently re-
        // equip the custom outfit, dragging the playerType along with
        // it. Result: vanilla click → custom outfit stays equipped,
        // but with playerType-mismatch downstream forcing the slot
        // back to vanilla bytes — looked like "selecting Jill changed
        // my player type" because the player swapped DDFemale →
        // DDMale-in-vanilla.
        //
        // The supply-drop pickup path no longer needs this fallback —
        // OutfitSupplyDropPickup writes Quark + calls LoadPartsNew
        // directly with the right bytes, never emitting a broken-
        // custom signal. So pendingDevId is the only legitimate
        // source of "the user just picked a custom outfit."
        if (!chosen && partsType == 0x00 && camoType == 0xFF)
        {
            const std::uint16_t pendingDevId = outfit::GetPendingOutfitDevelopId();
            if (pendingDevId != 0)
            {
                const outfit::OutfitEntry* byPending = nullptr;
                if (outfit::TryGetOutfitByDevelopId(pendingDevId, &byPending)
                    && byPending)
                {
                    chosen = byPending;
                    via = "broken-custom-pendingDevId";
                    outfit::ClearPendingOutfitDevelopId();
                }
            }
        }

        // 4. Dev-menu / iDroid supply-drop request path. Orig builds
        //    the loadout info via the EDC's per-flowIndex table
        //    (vtable[0xB0]/[0x108] in `DecideActMotherBaseDeviceSupport-
        //    DropMode`). Our custom flowIndices (922..924) have no
        //    entry in that table → orig returns vanilla NORMAL bytes
        //    (partsType=0, camo=0, all other fields zero too) into
        //    the loadout info. SetSuit then fires with those zeros
        //    and applies vanilla NORMAL — visible symptom: "I requested
        //    FROGS as DD-Female but the character is wearing camo=0x00."
        //
        //    Recovery signal: `pendingDevId` was set by `OutfitItemSelector::
        //    hkDecideActSupplyDrop` (or hkDecideActMissionPrep) at the
        //    "user confirmed" click. It's set ONLY when the click
        //    matched a registered custom outfit (vanilla clicks clear
        //    it), and it's one-shot (cleared on consumption), so it
        //    can't get stale across multiple equip events.
        //
        //    Trigger conditions (all must hold):
        //      - paths 1-3 didn't match  (no custom byte signal)
        //      - flags has bit 0  (this IS a suit-equip operation)
        //      - partsType == 0 AND camo == 0  (vanilla NORMAL bytes,
        //        i.e. the orig's lookup couldn't find our flowIndex)
        //      - pendingDevId != 0  (user just clicked a custom outfit)
        //
        //    The bit-0/zero-bytes guard prevents this path from
        //    hijacking legitimate vanilla equips; for those, either
        //    pendingDevId is 0 (vanilla click) OR the bytes aren't
        //    pure zeros (vanilla suit has its own non-zero camo).
        if (!chosen
         && (flags & 0x01u) != 0
         && partsType == 0x00
         && camoType  == 0x00)
        {
            const std::uint16_t pendingDevId = outfit::GetPendingOutfitDevelopId();
            if (pendingDevId != 0)
            {
                const outfit::OutfitEntry* byPending = nullptr;
                if (outfit::TryGetOutfitByDevelopId(pendingDevId, &byPending)
                    && byPending)
                {
                    chosen = byPending;
                    via = "supply-drop-request-pendingDevId";
                    outfit::ClearPendingOutfitDevelopId();
                }
            }
        }

        if (!chosen)
        {
            // No custom matched. If the blob carries a broken-custom
            // signal (partsType=0, camo=0xFF) without any pending
            // selection, that's a stale orig-pipeline transient — zero
            // it out so orig sees clean vanilla NORMAL instead of an
            // invalid camo=0xFF that could OOB downstream.
            if (partsType == 0x00 && camoType == 0xFF)
            {
                __try
                {
                    base[kInfoOff_CamoType] = 0x00;
                    Log("[OutfitSuitConditionApply:%s] cleared stale "
                        "broken-custom signal (no pendingDevId) -> vanilla "
                        "NORMAL\n",
                        tag);
                }
                __except (EXCEPTION_EXECUTE_HANDLER) { }
            }
            return false;
        }

        __try
        {
            // Determine the EFFECTIVE playerType after orig SetSuit
            // returns:
            //   - If flags has 0x100, info[0xC0] is the TARGET PT.
            //     Orig will commit a body change (character switch,
            //     loadout slot restore, etc.). We must compare against
            //     this target — comparing against livePT misses
            //     because livePT is still the OLD body until orig
            //     finishes applying.
            //   - If flags lacks 0x100, info[0xC0] is junk and the
            //     orig keeps the body at livePT (broken-custom
            //     transient signal during active equip — flags=0x81).
            //     livePT IS the effective PT in that case.
            const bool playerTypeValid = (flags & 0x100u) != 0;
            const std::uint8_t livePT = outfit::ReadLivePlayerType();
            const std::uint8_t effectivePT =
                playerTypeValid ? playerType : livePT;
            const bool canCheckPT = playerTypeValid || (livePT != 0xFF);
            const bool clearMismatch = canCheckPT
                                    && (effectivePT != chosen->playerType);

            // We do NOT mask the 0x100 bit. Earlier (2026-04-27) we
            // masked it as a defense against saved-state corruption
            // sync events writing stale playerType to the player
            // slot, but that mask blocked legitimate character
            // switches: when the user picks a different character
            // in the menu, orig fires this hook with 0x100 set and
            // info[0xC0] = the new character's playerType, expecting
            // to commit the body swap. Masking 0x100 made the swap
            // a no-op — symptom user reported 2026-04-27: "when I
            // have Jill equipped, I can't change to any other
            // character, even if it's a female."
            //
            // Trade-off: the saved-state-corruption case (if it ever
            // re-emerges) will let the body change to whatever the
            // saved playerType says. That's vanilla orig behavior
            // and acceptable — the user's clear preference is that
            // character switches must work.

            if (clearMismatch)
            {
                // Effective body after this commit can't wear the
                // matched outfit (either user is on the wrong body,
                // or they're switching to a body that can't wear
                // it). Write vanilla NORMAL bytes — orig commits
                // the body change AND lands on a clean vanilla
                // outfit. No stray-custom transient, no infinite
                // loading from custom partsType on a body that
                // doesn't have the assets.
                base[kInfoOff_PartsType] = 0x00;
                base[kInfoOff_CamoType]  = 0x00;

                Log("[OutfitSuitConditionApply:%s] playerType mismatch "
                    "(effective=%u via=%s outfit-playerType=%u "
                    "developId=%u; livePT=%u info[0xC0]=%u "
                    "flags=0x%X 0x100=%s) — applied vanilla NORMAL "
                    "upfront\n",
                    tag,
                    static_cast<unsigned>(effectivePT),
                    via,
                    static_cast<unsigned>(chosen->playerType),
                    static_cast<unsigned>(chosen->developId),
                    static_cast<unsigned>(livePT),
                    static_cast<unsigned>(playerType),
                    flags,
                    playerTypeValid ? "set" : "unset");
                return true;
            }

            // Match path (effectivePT == chosen->playerType, OR PT
            // unavailable so we apply-and-pray): write the outfit's
            // bytes. If effectivePT truly matches, LoadPartsNew's
            // ResolveCustomEntry hits and the outfit applies cleanly.
            // If PT was unavailable and the body turns out to
            // mismatch, LoadPartsNew's stray-custom path catches it
            // as a safety net (force-vanilla).
            base[kInfoOff_PartsType] = chosen->partsType;
            base[kInfoOff_CamoType]  = chosen->selectorCode;

            Log("[OutfitSuitConditionApply:%s] rewrote loadout (via %s) "
                "-> developId=%u partsType=0x%02X selector=0x%02X "
                "(effective=%u outfit-playerType=%u; livePT=%u "
                "info[0xC0]=%u flags=0x%X 0x100=%s)\n",
                tag, via,
                static_cast<unsigned>(chosen->developId),
                static_cast<unsigned>(chosen->partsType),
                static_cast<unsigned>(chosen->selectorCode),
                static_cast<unsigned>(effectivePT),
                static_cast<unsigned>(chosen->playerType),
                static_cast<unsigned>(livePT),
                static_cast<unsigned>(playerType),
                flags,
                playerTypeValid ? "set" : "unset");
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("[OutfitSuitConditionApply:%s] SEH writing rewrite\n", tag);
            return false;
        }
    }

    static void __fastcall hkSetSuit(void* self, void* info)
    {
        InspectAndRewriteLoadout(info, "SetSuit");

        // No partsType spoofing here. Earlier attempts to spoof partsType
        // and/or camo to vanilla NORMAL inside this hook regressed body
        // rendering (orig SetSuit wrote spoofed values to a player-slot
        // state struct that LoadPartsNew later reads) or hung the load
        // (orig SetSuit set up state for partsType=0x00 while LoadPartsNew
        // tried to load custom assets for the rewritten partsType=0x40).
        //
        // The actual loadout-clearing happens NOT in SetSuit itself but
        // in its caller wrapper FUN_1462c93f0 (retail 0x1462C93F0). That
        // wrapper calls SetSuit and then runs a 3-slot apply pass that
        // zeroes player weapon-slot data when info[0xBC] flag bits 2/3/4
        // aren't set. Broken-custom suit equip arrives there with
        // flags=0x81 (no slot bits) → slots zeroed.
        //
        // The fix is `hkLoadoutApplyAfterSetSuit` below: it detects the
        // suppress-pattern (custom partsType + suit-equip-only flags) at
        // the FUN_1462c93f0 layer, calls SetSuit directly via this
        // hook's trampoline (so body change still happens), and skips
        // the slot-apply pass — preserving the player's loadout.
        g_Orig(self, info);
    }

    static void __fastcall hkReqLoadout(void* self, void* info, std::uint8_t apply)
    {
        InspectAndRewriteLoadout(info, "ReqLoadout");
        g_OrigReqLoadout(self, info, apply);
    }

    // Hook for FUN_1462c93f0 — the function that runs SetSuit then applies
    // a 3-slot loadout buffer to the player based on info[0xBC] flag bits.
    // For broken-custom suit-equip (flags=0x81 = bit 0 + bit 7, no slot
    // bits 2/3/4), the apply step zeroes the player's weapon slots →
    // SORTIE PREP slots show NONE while wearing a custom suit.
    //
    // Strategy: detect this exact pattern (custom partsType in info[0],
    // suit-equip flags lacking slot bits) and bypass orig — instead
    // call SetSuit directly via its trampoline so the body change still
    // happens, but skip the slot-apply. The player's existing weapon-
    // slot loadout is NOT touched, so SORTIE PREP keeps showing the
    // weapons.
    //
    // Other call patterns (vanilla weapon clicks with slot bits set,
    // full vanilla suit equip with flags=0x1FF) fall through to orig
    // — those need the apply to populate new weapon data.
    static void __fastcall hkLoadoutApplyAfterSetSuit(void* self, void* info)
    {
        if (!info || !g_OrigLoadoutApply)
        {
            if (g_OrigLoadoutApply) g_OrigLoadoutApply(self, info);
            return;
        }

        bool shouldSuppressSlotApply = false;
        std::uint8_t  partsType = 0;
        std::uint32_t flags     = 0;

        __try
        {
            auto* base = reinterpret_cast<std::uint8_t*>(info);
            partsType = base[kInfoOff_PartsType];
            flags     = *reinterpret_cast<std::uint32_t*>(base + kInfoOff_Flags);

            const bool isCustomPartsType =
                partsType >= outfit::kCustomPartsTypeStart
             && partsType <= outfit::kCustomPartsTypeEnd;

            // The clearing pattern: custom partsType + flags without
            // slot bits 2/3/4 (mask 0x1C). info[0x18+] in this pattern
            // is uninitialized garbage — orig would zero slots in
            // local_128 and apply that, clearing the player's loadout.
            //
            // Vanilla suit equip uses flags=0x1FF (slot bits set + good
            // info[0x18+]) → orig applies populated slot data correctly,
            // we let it through.
            // Vanilla weapon clicks use varying flags but at least one
            // slot bit set → orig applies the click's weapon, we let
            // it through.
            const bool noSlotBits = (flags & 0x1Cu) == 0;
            const bool hasSuitBit = (flags & 0x01u) != 0;

            if (isCustomPartsType && noSlotBits && hasSuitBit)
                shouldSuppressSlotApply = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("[OutfitLoadoutPreserve] SEH reading info — falling through to orig\n");
            g_OrigLoadoutApply(self, info);
            return;
        }

        if (shouldSuppressSlotApply)
        {
            Log("[OutfitLoadoutPreserve] SUPPRESSING slot-apply: custom partsType=0x%02X "
                "flags=0x%X (suit-equip without slot data — orig would zero "
                "player's weapon slots). Calling SetSuit directly so body "
                "change happens; player loadout untouched.\n",
                static_cast<unsigned>(partsType),
                flags);

            // Call SetSuit directly via the trampoline we already have.
            // This handles the body change exactly the same way orig
            // FUN_1462c93f0 would (it calls SetSuit as its first action).
            // We then skip the slot-apply (vtable[0x218] call) — the
            // player's loadout stays untouched.
            if (g_Orig)
                g_Orig(self, info);

            // Replicate orig's post-flag-set on Player2UtilityImpl so
            // downstream "loadout changed" listeners (notably the
            // event handler that fires LoadPartsNew ~400ms later for
            // body re-render) still see the signal. Without this bit,
            // LoadPartsNew may not fire and the body wouldn't update
            // visually after our suppressed-orig path.
            //
            // Verified at named-build line 5963470 inside FUN_1462c93f0:
            //   *(uint *)(param_1 + 0x190) = *(uint *)(param_1 + 0x190) | 8;
            // (decimal 400 = hex 0x190)
            //
            // We do NOT replicate the secondary set on
            // Player2System+0x204 |= 0x80000 yet — that requires
            // GetPlayer2System() which we don't currently expose. If
            // LoadPartsNew doesn't fire visibly, we'll add it.
            __try
            {
                auto* p1 = reinterpret_cast<std::uint8_t*>(self);
                if (p1)
                {
                    *reinterpret_cast<std::uint32_t*>(p1 + 0x190) |= 8u;
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                Log("[OutfitLoadoutPreserve] SEH writing post-bit at p1+0x190\n");
            }

            return;
        }

        g_OrigLoadoutApply(self, info);
    }

    // SetInitialConditionWithLoadoutInfo hook — see AddressSet comment
    // for full background. The body change happens via the orig calling
    // SetSuit internally; we only manipulate the `preserve` flag to
    // suppress the post-SetSuit slot-clearing loop when a custom outfit
    // is being equipped.
    static void __fastcall hkSetInitialConditionWithLoadoutInfo(
        void* self, void* info, std::uint8_t preserve)
    {
        if (!info || !g_OrigSetInitial)
        {
            if (g_OrigSetInitial) g_OrigSetInitial(self, info, preserve);
            return;
        }

        std::uint8_t  partsType    = 0;
        std::uint8_t  camoType     = 0;
        std::uint32_t flags        = 0;
        bool          isCustomOutfitEquip = false;

        __try
        {
            auto* base = reinterpret_cast<std::uint8_t*>(info);
            partsType  = base[kInfoOff_PartsType];
            camoType   = base[kInfoOff_CamoType];
            flags      = *reinterpret_cast<std::uint32_t*>(base + kInfoOff_Flags);

            // Detect custom outfit equip: either the partsType is in the
            // custom range, or (broken-custom transient) the camo is in
            // the custom selector range. Either way, the orig's slot-
            // clearing branch would zero out the player's loadout — we
            // want to preserve it.
            const bool customPT =
                partsType >= outfit::kCustomPartsTypeStart
             && partsType <= outfit::kCustomPartsTypeEnd;
            const bool customSel =
                camoType >= outfit::kCustomSelectorStart
             && camoType <= outfit::kCustomSelectorEnd;

            // Also catch the broken-custom signal (partsType=0, camo=0xFF).
            // InspectAndRewriteLoadout typically rewrites this to a real
            // partsType earlier in the pipeline, but if we reach here
            // before that rewrite happens we want to be safe.
            const bool brokenCustom = (partsType == 0x00 && camoType == 0xFF);

            isCustomOutfitEquip = customPT || customSel || brokenCustom;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("[OutfitSuitConditionApply:SetInitial] SEH reading info — "
                "passing through to orig untouched\n");
            g_OrigSetInitial(self, info, preserve);
            return;
        }

        if (isCustomOutfitEquip && preserve == 0)
        {
            Log("[OutfitSuitConditionApply:SetInitial] custom-outfit equip "
                "detected (partsType=0x%02X camo=0x%02X flags=0x%X) — "
                "spoofing preserve=1 to suppress slot-clear and keep "
                "the player's weapon-slot loadout intact\n",
                static_cast<unsigned>(partsType),
                static_cast<unsigned>(camoType),
                flags);

            g_OrigSetInitial(self, info, 1);
            return;
        }

        // Vanilla equip OR caller already requested preserve mode — pass
        // through with no modification.
        g_OrigSetInitial(self, info, preserve);
    }
}

namespace outfit
{
    bool Install_OutfitSuitConditionApply_Hook()
    {
        if (!g_Installed)
        {
            void* target = ResolveGameAddress(
                gAddr.Player2UtilityImpl_SetSuitAndHandConditionWithLoadoutInfo);
            if (target)
            {
                g_Installed = CreateAndEnableHook(
                    target,
                    reinterpret_cast<void*>(&hkSetSuit),
                    reinterpret_cast<void**>(&g_Orig));
                Log("[OutfitSuitConditionApply] SetSuit installed: %s (target=%p)\n",
                    g_Installed ? "OK" : "FAIL", target);
            }
            else
            {
                Log("[OutfitSuitConditionApply] SetSuit target unresolved\n");
            }
        }

        if (!g_InstalledReqLoadout)
        {
            void* target = ResolveGameAddress(
                gAddr.Player2UtilityImpl_CommitWrapper);
            if (target)
            {
                g_InstalledReqLoadout = CreateAndEnableHook(
                    target,
                    reinterpret_cast<void*>(&hkReqLoadout),
                    reinterpret_cast<void**>(&g_OrigReqLoadout));
                Log("[OutfitSuitConditionApply] ReqLoadout installed: %s (target=%p)\n",
                    g_InstalledReqLoadout ? "OK" : "FAIL", target);
            }
            else
            {
                Log("[OutfitSuitConditionApply] ReqLoadout target unresolved\n");
            }
        }

        if (!g_InstalledLoadoutApply)
        {
            void* target = ResolveGameAddress(
                gAddr.Player2UtilityImpl_LoadoutApplyAfterSetSuit);
            if (target)
            {
                g_InstalledLoadoutApply = CreateAndEnableHook(
                    target,
                    reinterpret_cast<void*>(&hkLoadoutApplyAfterSetSuit),
                    reinterpret_cast<void**>(&g_OrigLoadoutApply));
                Log("[OutfitSuitConditionApply] LoadoutApplyAfterSetSuit "
                    "installed: %s (target=%p)\n",
                    g_InstalledLoadoutApply ? "OK" : "FAIL", target);
            }
            else
            {
                Log("[OutfitSuitConditionApply] LoadoutApplyAfterSetSuit "
                    "target unresolved\n");
            }
        }

        if (!g_InstalledSetInitial)
        {
            void* target = ResolveGameAddress(
                gAddr.Player2UtilityImpl_SetInitialConditionWithLoadoutInfo);
            if (target)
            {
                g_InstalledSetInitial = CreateAndEnableHook(
                    target,
                    reinterpret_cast<void*>(&hkSetInitialConditionWithLoadoutInfo),
                    reinterpret_cast<void**>(&g_OrigSetInitial));
                Log("[OutfitSuitConditionApply] SetInitialConditionWithLoadoutInfo "
                    "installed: %s (target=%p)\n",
                    g_InstalledSetInitial ? "OK" : "FAIL", target);
            }
            else
            {
                Log("[OutfitSuitConditionApply] SetInitialConditionWithLoadoutInfo "
                    "target unresolved\n");
            }
        }

        return g_Installed || g_InstalledReqLoadout
            || g_InstalledLoadoutApply || g_InstalledSetInitial;
    }

    void Uninstall_OutfitSuitConditionApply_Hook()
    {
        if (g_Installed)
        {
            if (void* t = ResolveGameAddress(
                    gAddr.Player2UtilityImpl_SetSuitAndHandConditionWithLoadoutInfo))
                DisableAndRemoveHook(t);
            g_Orig      = nullptr;
            g_Installed = false;
        }
        if (g_InstalledReqLoadout)
        {
            if (void* t = ResolveGameAddress(
                    gAddr.Player2UtilityImpl_CommitWrapper))
                DisableAndRemoveHook(t);
            g_OrigReqLoadout      = nullptr;
            g_InstalledReqLoadout = false;
        }
        if (g_InstalledLoadoutApply)
        {
            if (void* t = ResolveGameAddress(
                    gAddr.Player2UtilityImpl_LoadoutApplyAfterSetSuit))
                DisableAndRemoveHook(t);
            g_OrigLoadoutApply      = nullptr;
            g_InstalledLoadoutApply = false;
        }
        if (g_InstalledSetInitial)
        {
            if (void* t = ResolveGameAddress(
                    gAddr.Player2UtilityImpl_SetInitialConditionWithLoadoutInfo))
                DisableAndRemoveHook(t);
            g_OrigSetInitial      = nullptr;
            g_InstalledSetInitial = false;
        }
        Log("[OutfitSuitConditionApply] removed\n");
    }
}
