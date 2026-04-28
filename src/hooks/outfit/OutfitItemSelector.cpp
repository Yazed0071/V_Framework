#include "pch.h"

#include "OutfitItemSelector.h"
#include "OutfitRegistry.h"

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    // 4-arg signature: (self, ErrorCode* out, void* p3, void* p4).
    // Verified from prior framework's signature audit.
    using DecideActMissionPrep_t = void* (__fastcall*)(
        void* self, void* out, void* p3, void* p4);

    static DecideActMissionPrep_t g_OrigDecideActMissionPrep = nullptr;
    static DecideActMissionPrep_t g_OrigDecideActSupplyDrop  = nullptr;
    static bool                    g_Installed                = false;
    static bool                    g_InstalledSupplyDrop      = false;

    // EquipDevelopCallbackImpl::SetSupplyCBoxInfo signature.
    // RCX = this (EquipDevelopCallbackImpl*), DX = ushort flowIndex.
    using SetSupplyCBoxInfo_t = void (__fastcall*)(
        void* self, std::uint16_t flowIndex);

    static SetSupplyCBoxInfo_t g_OrigSetSupplyCBoxInfo = nullptr;
    static bool                g_InstalledSetSupplyCBox = false;

    // ItemSelector internal layout (offsets verified 2026-04-26 by
    // tracing DecideActMissionPreparationSetEquipMode @ 0x1416A3670):
    //   self + 0x4434 = u32 equipKind. 0x80 = MissionPrep suit equip.
    //   self + 0x10C  = i32 scroll-page base row index. Cursor scrolling
    //                   migrates the row from +0x110 into +0x10C while
    //                   the on-screen cursor offset returns to 0. Missed
    //                   in legacy framework — caused confirm to read
    //                   cell at row 0 instead of the user's actual row,
    //                   silently equipping vanilla suit at slot 0.
    //   self + 0x110  = i32 on-screen cursor row offset (relative to
    //                   the scroll base). Effective row = 0x10C + 0x110.
    //   self + 0xC040 = u8[64] per-row variant byte.
    //   self + 0x4440 = u16[?] selectedId table, stride (row*15+var)*2.
    //   self + 0xCC40 = u32[?] selectorCode table, stride (row*15+var)*12;
    //                    low byte is the selector we want.

    constexpr std::uint32_t kEquipKindMissionPrepSuit = 0x80;
    // Supply-drop's equivalent equipKind for suit-row clicks (verified
    // mgsvtpp.exe_Addresses.txt:15774601 — `CMP EAX,0x100` branch in
    // DecideActMotherBaseDeviceSupportDropMode is the suit path).
    constexpr std::uint32_t kEquipKindSupplyDropSuit = 0x100;

    struct SelectionSample
    {
        bool          haveSample   = false;
        std::uint32_t equipKind    = 0;
        std::uint16_t selectedId   = 0xFFFF;
        std::uint8_t  selectorCode = 0xFF;
    };

    static bool TryReadSelection(void* self, SelectionSample& out)
    {
        out = {};
        if (!self) return false;

        auto* base = reinterpret_cast<std::uint8_t*>(self);

        __try
        {
            const std::uint32_t equipKind = *reinterpret_cast<std::uint32_t*>(base + 0x4434);
            // Effective row = scroll-page base (+0x10C) + on-screen
            // cursor offset (+0x110). The orig
            // DecideActMissionPreparationSetEquipMode (line 2951220 of
            // mgsvtpp.exe.c) uses the SUM. Reading just +0x110 misses
            // any row past the first scroll page — the scroll commit
            // moves the value from +0x110 into +0x10C while +0x110
            // returns to 0. Hover-time reads happened to land on page 0
            // (where 0x10C==0), masking this for years.
            const std::int32_t baseRow   = *reinterpret_cast<std::int32_t*>(base + 0x10C);
            const std::int32_t cursorRow = *reinterpret_cast<std::int32_t*>(base + 0x110);
            const std::int32_t row       = baseRow + cursorRow;

            if (row < 0 || row > 0x3F) return false;

            const std::uint8_t variant = *(base + 0xC040 + row);
            if (variant > 14) return false;

            const std::size_t cellIndex = static_cast<std::size_t>(row) * 15 + variant;

            const std::uint16_t selectedId =
                *reinterpret_cast<std::uint16_t*>(base + 0x4440 + cellIndex * 2);

            const std::uint32_t selectorCodeU32 =
                *reinterpret_cast<std::uint32_t*>(base + 0xCC40 + cellIndex * 12);

            out.haveSample    = true;
            out.equipKind     = equipKind;
            out.selectedId    = selectedId;
            out.selectorCode  = static_cast<std::uint8_t>(selectorCodeU32 & 0xFF);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // Locate a registered outfit from the selection state. Returns
    // the matching developId (>0) or 0 if no custom outfit matches.
    // ALSO publishes the selected variant index into the active-variant
    // tracker so the runtime parts loaders pick the right variant.
    //
    // Match order:
    //   1. selectorCode in custom range → variant-selector lookup
    //      (decodes the variant index too, for in-row UNIFORMS cycle)
    //   2. selectedId == outfit.flowIndex (variant 0 — the base cell
    //      that the cycle would show before any cycling).
    //   3. selectedId == outfit.developId (mod-author convenience).
    static std::uint16_t MatchSelectionToOutfit(const SelectionSample& s)
    {
        // Path 1: selector byte already in custom range. Use the
        // variant-aware lookup so a click on a non-base cell (cycled-
        // to variant N's selectorCode) sets ActiveVariant=N before
        // the commit pipeline runs.
        if (s.selectorCode >= outfit::kCustomSelectorStart
            && s.selectorCode <= outfit::kCustomSelectorEnd)
        {
            const outfit::OutfitEntry* entry = nullptr;
            std::uint8_t variantIdx = 0;
            if (outfit::TryGetOutfitByVariantSelector(
                    s.selectorCode, &entry, &variantIdx) && entry)
            {
                // Publish active variant for runtime parts loaders.
                outfit::SetActiveVariant(entry->partsType, variantIdx);
                return entry->developId;
            }
        }

        // Path 2: selectedId equals our registered flowIndex (typically
        // a hover/pre-cycle read where the selectorCode hasn't
        // resolved yet). Default to variant 0.
        const outfit::OutfitEntry* byFlow = nullptr;
        if (outfit::TryGetOutfitByFlowIndex(s.selectedId, &byFlow) && byFlow)
        {
            outfit::SetActiveVariant(byFlow->partsType, 0);
            return byFlow->developId;
        }

        // Path 3: selectedId equals our developId (mod-author convenience).
        const outfit::OutfitEntry* byDev = nullptr;
        if (outfit::TryGetOutfitByDevelopId(s.selectedId, &byDev) && byDev)
        {
            outfit::SetActiveVariant(byDev->partsType, 0);
            return byDev->developId;
        }

        return 0;
    }

    // Shared body — reads the selection state, publishes pending devId
    // if it matches a registered outfit. Used by both the MissionPrep
    // suit-click hook and the Supply-Drop suit-click hook.
    static void ProcessSelectionAndPublish(void* self, const char* tag)
    {
        SelectionSample s{};
        const bool haveSample = TryReadSelection(self, s) && s.haveSample;

        if (haveSample)
        {
            const bool isSuitClick = (s.equipKind == kEquipKindMissionPrepSuit
                                   || s.equipKind == kEquipKindSupplyDropSuit);
            const std::uint16_t devId =
                isSuitClick ? MatchSelectionToOutfit(s) : 0;

            Log("[OutfitItemSelector:%s] fire: equipKind=0x%X selectedId=%u "
                "selector=0x%02X -> matched developId=%u\n",
                tag,
                s.equipKind,
                static_cast<unsigned>(s.selectedId),
                static_cast<unsigned>(s.selectorCode),
                static_cast<unsigned>(devId));

            if (devId != 0)
                outfit::SetPendingOutfitDevelopId(devId);
            else if (isSuitClick)
                outfit::ClearPendingOutfitDevelopId();
        }
        else
        {
            Log("[OutfitItemSelector:%s] fire: selection-state read failed\n", tag);
        }
    }

    static void* __fastcall hkDecideActMissionPrep(
        void* self, void* out, void* p3, void* p4)
    {
        ProcessSelectionAndPublish(self, "prep");
        return g_OrigDecideActMissionPrep(self, out, p3, p4);
    }

    static void* __fastcall hkDecideActSupplyDrop(
        void* self, void* out, void* p3, void* p4)
    {
        ProcessSelectionAndPublish(self, "supply");

        // Latch a "user just clicked confirm in supply-drop UI" signal.
        // OutfitSupplyDropSetup consumes this to distinguish a real
        // confirm from a hover/preview SupplyDropSuitSetup fire — the
        // hover doesn't go through this DecideAct callback, so the
        // latch stays clear and the framework skips the immediate
        // ForcePartsReload.
        outfit::SetSupplyDropClickLatch();

        // ALSO stash pendingSupplyDropDevelopId directly here — the
        // pickup-phase hook (`OutfitSupplyDropPickup`) consumes it to
        // force-equip the outfit when the supply-drop crate is opened.
        // Normally `OutfitSupplyDropSetup::hkSupplyDropSuitSetup` does
        // this stash on the SuitSetup pass that follows the click,
        // but some paths (notably the MotherBase R&D dev-menu
        // "Request Supply Drop" action) DON'T go through that
        // SupplyDropSuitSetup hook even though they reach this
        // DecideAct callback. Without this stash, the crate arrives
        // ~20-30s later, OutfitSupplyDropPickup runs, finds the stash
        // empty, and the equip pipeline falls into the broken-custom
        // (partsType=0/camo=0xFF) path with `pendingDevId=0` → forces
        // vanilla NORMAL (the user-visible "I requested FROGS but
        // got camo=0x00" symptom).
        //
        // Setting it on the click is safe: the stash is consumed
        // exactly once by the pickup hook and re-set if the user
        // hovers/selects via the SupplyDropSuitSetup path. For paths
        // that never see SupplyDropSuitSetup, this is the only
        // place pendingSupplyDropDevelopId gets set.
        SelectionSample s{};
        if (TryReadSelection(self, s) && s.haveSample)
        {
            const bool isSuitClick = (s.equipKind == kEquipKindMissionPrepSuit
                                   || s.equipKind == kEquipKindSupplyDropSuit);
            if (isSuitClick)
            {
                const std::uint16_t devId = MatchSelectionToOutfit(s);
                if (devId != 0)
                {
                    outfit::SetPendingSupplyDropDevelopId(devId);
                    Log("[OutfitItemSelector:supply] also stashed "
                        "pendingSupplyDropDevelopId=%u for crate-pickup "
                        "force-equip (covers paths that bypass "
                        "SupplyDropSuitSetup, e.g. MotherBase R&D)\n",
                        static_cast<unsigned>(devId));
                }
            }
        }

        return g_OrigDecideActSupplyDrop(self, out, p3, p4);
    }

    // R&D MotherBase dev-menu "Request Supply Drop" handler. Fires
    // when the user clicks "Request" on a developed item from the
    // R&D screen — independent of the iDroid Supply-Drop UI flow.
    //
    // For our custom outfit's flowIndex, the orig builds a zero
    // SupplyCboxLoadoutInfo (its EDC/suit-info table doesn't have
    // valid bytes for flowIndex >= 0x289) and queues the supply
    // drop. ~30 seconds later the crate arrives, the pickup
    // pipeline emits the broken-custom transient (partsType=0,
    // camo=0xFF) at LoadPartsNew, and without a pendingDevId stash
    // the framework forces vanilla NORMAL ("camo=0x00" symptom).
    //
    // Pre-orig: look up our outfit by flowIndex; if matched, stash
    // the developId in BOTH framework stashes:
    //   - pendingOutfitDevelopId — used by the SetSuit / LoadPartsNew
    //     broken-custom resolvers (covers the immediate-apply path
    //     if the dev-menu request triggers SetSuit synchronously)
    //   - pendingSupplyDropDevelopId — used by OutfitSupplyDrop-
    //     Pickup hooks (covers the delayed crate-pickup path)
    //
    // Either resolver alone is sufficient to recover the equip;
    // setting both makes the fix robust to whichever pipeline the
    // arriving crate actually triggers.
    static void __fastcall hkSetSupplyCBoxInfo(
        void* self, std::uint16_t flowIndex)
    {
        const outfit::OutfitEntry* entry = nullptr;
        const bool isCustom =
            outfit::TryGetOutfitByFlowIndex(flowIndex, &entry) && entry;

        if (isCustom)
        {
            outfit::SetPendingOutfitDevelopId(entry->developId);
            outfit::SetPendingSupplyDropDevelopId(entry->developId);
            Log("[OutfitItemSelector:devmenu] R&D request for custom outfit "
                "flowIndex=%u developId=%u partsType=0x%02X selector=0x%02X "
                "playerType=%u — stashed both pendingOutfitDevelopId AND "
                "pendingSupplyDropDevelopId for crate-pickup recovery\n",
                static_cast<unsigned>(flowIndex),
                static_cast<unsigned>(entry->developId),
                static_cast<unsigned>(entry->partsType),
                static_cast<unsigned>(entry->selectorCode),
                static_cast<unsigned>(entry->playerType));
        }

        if (g_OrigSetSupplyCBoxInfo)
            g_OrigSetSupplyCBoxInfo(self, flowIndex);

        // POST-ORIG SUPPLYCBOXLOADOUTINFO REWRITE (corrected offset
        // 2026-04-28 via retail decomp of FUN_141675600 at line 2927029).
        //
        // The retail decomp of EquipDevelopCallbackImpl::SetSupplyCBoxInfo
        // shows the loadoutInfo struct is at `param_1 + 0x23A0` (NOT
        // 0x2190 — that's a SEPARATE SupplyCboxLoadoutInfo at +0x2190
        // used by Update state 0x24's helicopter branch). The two
        // structs are independent.
        //
        // Verified retail decomp:
        //   void FUN_141675600(longlong param_1, ushort param_2) {
        //       ptr = (undefined4*)(param_1 + 0x23A0);
        //       memset(ptr, 0, 0x130);
        //       ...
        //       // populates ptr based on suit-info-table category byte
        //       // at (flowIndex * 0x68 + 0x36 + table) — OOB for our
        //       // custom flowIndices (922..925), so dispatch goes to
        //       // wrong branch and ptr stays mostly zeroed
        //       ...
        //       (**(code**)(lVar13 + 0x1e0))(
        //           *(longlong*)(param_1 + 0x80), uVar14,
        //           0x5db695f97e34, ptr);  // trigger event with ptr
        //   }
        //
        // The function fires trigger event 0x5db695f97e34 at the end,
        // passing ptr (= param_1+0x23A0) as the loadoutInfo. Our hook
        // runs AFTER orig returns, so we're post-trigger. If the
        // trigger handler reads ptr lazily (stores the pointer and
        // dereferences later when the crate is being created), our
        // post-orig overwrite still takes effect. If it reads eagerly,
        // we'll need a deeper hook (next iteration).
        //
        // Field layout of SupplyCboxLoadoutInfo (verified at
        // OutfitSuitConditionApply.cpp:100-103 + retail decomp's
        // *(undefined4*)ptr = 3 + offsets at ptr+0x10..0x1c for SUIT
        // branch):
        //   [0x00] u8  playerPartsType  (we want = entry->partsType)
        //   [0x01] u8  playerCamoType   (we want = entry->selectorCode)
        //   ... rest populated by orig's branch logic
        //
        // For custom outfits, overwrite [0]=partsType + [1]=selectorCode.
        // Note: orig writes `*(undefined4*)ptr = 3` in some branches
        // (which sets first 4 bytes to 0x03000000), but our overwrite
        // happens AFTER that, so the final bytes are our values.
        if (isCustom && self)
        {
            __try
            {
                constexpr std::size_t kSupplyCboxLoadoutInfoOff = 0x23A0;

                auto* loadout =
                    reinterpret_cast<std::uint8_t*>(self) + kSupplyCboxLoadoutInfoOff;

                const std::uint8_t prevParts = loadout[0];
                const std::uint8_t prevCamo  = loadout[1];

                loadout[0] = entry->partsType;
                loadout[1] = entry->selectorCode;

                Log("[OutfitItemSelector:devmenu] post-orig SupplyCboxLoadoutInfo "
                    "rewrite: self+0x%X loadout[0]=0x%02X->0x%02X "
                    "loadout[1]=0x%02X->0x%02X — orig populated garbage from "
                    "OOB suit-info-table read for custom flowIndex=%u; "
                    "trigger event 0x5DB695F97E34 already fired with "
                    "loadoutInfo pointer — works if handler reads lazily\n",
                    static_cast<unsigned>(kSupplyCboxLoadoutInfoOff),
                    static_cast<unsigned>(prevParts),
                    static_cast<unsigned>(loadout[0]),
                    static_cast<unsigned>(prevCamo),
                    static_cast<unsigned>(loadout[1]),
                    static_cast<unsigned>(flowIndex));
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                Log("[OutfitItemSelector:devmenu] SEH writing SupplyCboxLoadoutInfo "
                    "at self+0x23A0 (self=%p) — offset assumption may be wrong\n",
                    self);
            }
        }
    }
}

namespace outfit
{
    bool Install_OutfitItemSelector_Hook()
    {
        if (g_Installed && g_InstalledSupplyDrop) return true;

        if (!g_Installed)
        {
            void* target = ResolveGameAddress(
                gAddr.ItemSelectorCallbackImpl_DecideActMissionPreparationSetEquipMode);
            if (!target)
            {
                Log("[OutfitItemSelector] prep target unresolved; module disabled\n");
                return false;
            }

            g_Installed = CreateAndEnableHook(
                target,
                reinterpret_cast<void*>(&hkDecideActMissionPrep),
                reinterpret_cast<void**>(&g_OrigDecideActMissionPrep));

            Log("[OutfitItemSelector] prep installed: %s (target=%p)\n",
                g_Installed ? "OK" : "FAIL", target);
        }

        if (!g_InstalledSupplyDrop)
        {
            void* target = ResolveGameAddress(
                gAddr.ItemSelectorCallbackImpl_DecideActMotherBaseDeviceSupportDropMode);
            if (!target)
            {
                Log("[OutfitItemSelector] supply-drop target unresolved; "
                    "supply-drop hook disabled\n");
            }
            else
            {
                g_InstalledSupplyDrop = CreateAndEnableHook(
                    target,
                    reinterpret_cast<void*>(&hkDecideActSupplyDrop),
                    reinterpret_cast<void**>(&g_OrigDecideActSupplyDrop));

                Log("[OutfitItemSelector] supply installed: %s (target=%p)\n",
                    g_InstalledSupplyDrop ? "OK" : "FAIL", target);
            }
        }

        if (!g_InstalledSetSupplyCBox)
        {
            void* target = ResolveGameAddress(
                gAddr.EquipDevelopCallbackImpl_SetSupplyCBoxInfo);
            if (!target)
            {
                Log("[OutfitItemSelector] R&D dev-menu SetSupplyCBoxInfo "
                    "target unresolved; R&D-menu request hook disabled\n");
            }
            else
            {
                g_InstalledSetSupplyCBox = CreateAndEnableHook(
                    target,
                    reinterpret_cast<void*>(&hkSetSupplyCBoxInfo),
                    reinterpret_cast<void**>(&g_OrigSetSupplyCBoxInfo));

                Log("[OutfitItemSelector] devmenu SetSupplyCBoxInfo installed: "
                    "%s (target=%p)\n",
                    g_InstalledSetSupplyCBox ? "OK" : "FAIL", target);
            }
        }

        return g_Installed;
    }

    void Uninstall_OutfitItemSelector_Hook()
    {
        if (g_Installed)
        {
            if (void* t = ResolveGameAddress(
                    gAddr.ItemSelectorCallbackImpl_DecideActMissionPreparationSetEquipMode))
                DisableAndRemoveHook(t);
            g_OrigDecideActMissionPrep = nullptr;
            g_Installed                = false;
        }
        if (g_InstalledSupplyDrop)
        {
            if (void* t = ResolveGameAddress(
                    gAddr.ItemSelectorCallbackImpl_DecideActMotherBaseDeviceSupportDropMode))
                DisableAndRemoveHook(t);
            g_OrigDecideActSupplyDrop = nullptr;
            g_InstalledSupplyDrop     = false;
        }
        if (g_InstalledSetSupplyCBox)
        {
            if (void* t = ResolveGameAddress(
                    gAddr.EquipDevelopCallbackImpl_SetSupplyCBoxInfo))
                DisableAndRemoveHook(t);
            g_OrigSetSupplyCBoxInfo  = nullptr;
            g_InstalledSetSupplyCBox = false;
        }
        Log("[OutfitItemSelector] removed\n");
    }
}
