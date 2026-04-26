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
            base[kInfoOff_PartsType] = chosen->partsType;
            base[kInfoOff_CamoType]  = chosen->selectorCode;
            // DO NOT write playerType or set the 0x100 "playerType valid"
            // flag bit. (Removed 2026-04-27 after confirming via
            // LoadPartsNew traces that orig SetSuit, when given
            // info[0xC0]=2 + flags 0x100, mutates the player slot's
            // body to playerType=1 — i.e. selecting a DDFemale-tagged
            // custom outfit while playing as DDFemale ends up swapping
            // the avatar to DDMale. Symptom user reported: "Player type
            // changes upon selecting the custom suit."
            //
            // Leaving the player's current body type alone is both
            // safer and more vanilla:
            //   - User at the matching body type (playerType matches
            //     outfit) → outfit's parts/camo apply, body stays put.
            //   - User on a mismatched body type → LoadPartsNew sees
            //     (theirPT, ourPartsType) miss in ResolveCustomEntry,
            //     forces vanilla 0x00. Outfit silently doesn't apply,
            //     but the avatar isn't swapped out from under the user.
            //
            // The outfit's `playerType` field functions as a MATCH
            // criterion for ResolveCustomEntry (LoadPartsNew time), not
            // as a SETTER for the player slot's body type. The legacy
            // framework's writing-it-here behavior conflated the two.
            Log("[OutfitSuitConditionApply:%s] rewrote loadout (via %s) "
                "-> developId=%u partsType=0x%02X selector=0x%02X "
                "(playerType left at %u — outfit registers playerType=%u "
                "but we don't force the body swap)\n",
                tag, via,
                static_cast<unsigned>(chosen->developId),
                static_cast<unsigned>(chosen->partsType),
                static_cast<unsigned>(chosen->selectorCode),
                static_cast<unsigned>(playerType),
                static_cast<unsigned>(chosen->playerType));
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
        g_Orig(self, info);
    }

    static void __fastcall hkReqLoadout(void* self, void* info, std::uint8_t apply)
    {
        InspectAndRewriteLoadout(info, "ReqLoadout");
        g_OrigReqLoadout(self, info, apply);
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

        return g_Installed || g_InstalledReqLoadout;
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
        Log("[OutfitSuitConditionApply] removed\n");
    }
}
