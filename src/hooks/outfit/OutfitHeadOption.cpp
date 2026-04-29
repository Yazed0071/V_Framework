#include "pch.h"

#include "OutfitHeadOption.h"
#include "OutfitRegistry.h"

#include <atomic>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    // Real signature: bool __fastcall(MissionPreparationCallbackImpl*)
    // (verified mgsvtpp.exe_Addresses.txt:139983616).
    using IsEnableCurrentHeadOption_t = std::uint8_t (__fastcall*)(void* self);

    static IsEnableCurrentHeadOption_t g_OrigIsEnableHead = nullptr;
    static bool                        g_Installed        = false;

    // ---------------------------------------------------------------
    // MissionPreparationCallbackImpl::IsEnableCurrentSuit
    // (retail 0x14A56BFA0, mgsvtpp.exe.c:2966957).
    //
    // Companion gate to IsEnableCurrentHeadOption inside
    // DecideActTargetSelWindow (retail 0x1416BAB00 / decomp 2965355).
    // Short-circuited check — both must pass:
    //
    //   if (iVar6 == 0x1c) {
    //       cVar3 = vtable[0x4f0](self+0x48);     // submenu-active gate
    //       if ((cVar3 != '\0') &&
    //           ((cVar3 = IsEnableCurrentSuit(self), cVar3 == '\0' ||
    //            (bVar4 = IsEnableCurrentHeadOption(self), !bVar4)))) {
    //         return 0x16;                         // REJECT (negative SFX)
    //       }
    //       ... return 0x1c;                       // proceed to submenu
    //   }
    //
    // The OR is short-circuited: if IsEnableCurrentSuit returns false,
    // IsEnableCurrentHeadOption is NEVER called and the gate fires
    // immediately. That's exactly the case for our custom outfits —
    // their suit equipId isn't recognized by the equip-development
    // system (vtable[0x478] at QuarkSystemTable->ApplicationSystem
    // ->field_0x110+0xac8 returns false for our flowIndices).
    //
    // This explains why the existing IsEnableCurrentHeadOption hook
    // installed at 0x14A56BA20 never fires for custom outfits — the
    // suit-side gate rejects first and short-circuits.
    //
    // Fix: hook IsEnableCurrentSuit and force-return 1 when the live
    // partsType is one of our registered customs that declares
    // HasHeadOptions(). The gate's other condition (IsEnableCurrentHead-
    // Option) returns true when the current head equipId is 0x400
    // (NONE sentinel) — our default state — so passing the suit gate
    // is sufficient to surface the head-option submenu.
    using IsEnableCurrentSuit_t = std::uint8_t (__fastcall*)(void* self);
    static IsEnableCurrentSuit_t g_OrigIsEnableCurrentSuit       = nullptr;
    static bool                  g_IsEnableCurrentSuitInstalled  = false;
    static std::atomic<bool>     g_IsEnableCurrentSuitFirstCall  { false };
    static std::atomic<bool>     g_IsEnableCurrentSuitFirstOverride{ false };

    // One-shot diagnostic flag (Phase 5 head-option list-build recon,
    // 2026-04-28). When the gate first fires for a custom outfit with
    // HasHeadOptions(), dump the inner CustomizeSlotSelectorCallbackImpl
    // vtable so we can identify which slot builds the head-option list.
    // Per existing notes (OutfitHeadOption.h:14-22) the suspected slot is
    // *(self+0x48) → vtable[+0x470], but on retail the offset may have
    // shifted (sibling slots used elsewhere are 0x220 / 0x4f0 vs the
    // named-build's 0x210 / 0x4c8). Dumping a wide range lets us
    // cross-reference each function pointer against retail and pick out
    // the list-builder.
    //
    // Remove these dumps (and the std::atomic include) once the list-
    // build hook lands.
    static std::atomic<bool> g_DiagVtableDumped{ false };
    static std::atomic<bool> g_DiagAnyCallLogged{ false };

    // ---------------------------------------------------------------
    // Phase-5 RECON PROBE: hook DecideActTargetSelWindow itself.
    //
    // Per retail decomp at 0x1416BAB00 (mgsvtpp.exe.c:2965355):
    //
    //   int DecideActTargetSelWindow(this, param_2)
    //   {
    //       (vtable[0x208 on *(this+0x38)])(...);
    //       iVar6 = GetNextPhaseFromSelectionTarget(this, param_2);
    //       if (iVar6 == 0x1c) {
    //           cVar3 = vtable[0x4f0 on *(this+0x48)]();   // submenu-active
    //           if (submenu-active && (suit-disabled || head-disabled)) {
    //               return 0x16;                            // error
    //           }
    //           vtable[0x470 on *(this+0x48)](self+0x48, 0); // ← LIST-BUILDER
    //           return 0x1c;
    //       }
    //       if (iVar6 == 10) { ... same pattern ... }
    //       ...
    //   }
    //
    // The sole call to IsEnableCurrentHeadOption lives inside the
    // 0x1c / 0x0a branches. If the user's UI navigation never produces
    // those phases (return value), our gate hook never fires and we
    // can't capture the inner vtable.
    //
    // This probe logs every fire of DecideActTargetSelWindow with:
    //   - the input param_2 (selection target / cursor item)
    //   - the orig's return value (= the resolved phase)
    // The user navigates UI; we see which selections produce which
    // phases. From that, we identify the user action that yields
    // phase 0x1c — and once that fires, the existing gate diagnostic
    // captures the vtable dump.
    using DecideActTargetSelWindow_t =
        std::uint32_t (__fastcall*)(void* self, std::uint32_t targetSel);
    static DecideActTargetSelWindow_t g_OrigDecideAct = nullptr;
    static bool                       g_DecideActInstalled = false;

    // ---------------------------------------------------------------
    // SetupCharacterSlotSelectPrefabListElement probe (retail 0x1416BF490).
    //
    // Per-slot display setup for the CHARACTER SLOT SELECT sub-panel.
    // Reads `*(this+0x3CA0)` (slot index) and configures display for
    // that slot. Slot 2 = HEAD_OPTION mode per
    // GetCharaSlotSelectMode return (=2 for DDMale/DDFemale).
    //
    // Probe goal: capture which slot indices fire when user navigates
    // SORTIE PREP > SELECT CHARACTER with FROGS Male equipped, vs
    // when they navigate with the vanilla head-option-supporting suit
    // equipped. Difference may reveal where HEAD_OPTION visibility
    // is gated.
    using SetupCharaSlotPrefab_t = void (__fastcall*)(void* self);
    static SetupCharaSlotPrefab_t g_OrigSetupCharaSlotPrefab = nullptr;
    static bool                   g_SetupCharaSlotPrefabInstalled = false;

    // Per-distinct-slotIndex dedup so we capture each slot index that
    // ever runs across the session.
    static std::atomic<std::uint32_t> g_SetupSlotSeen[16]{};
    static std::atomic<std::uint8_t>  g_SetupSlotSeenCount{ 0 };

    static bool SetupSlotAlreadyLogged(std::uint32_t slotIdx)
    {
        const std::uint8_t count = g_SetupSlotSeenCount.load();
        for (std::uint8_t i = 0; i < count && i < 16; ++i)
        {
            if (g_SetupSlotSeen[i].load() == slotIdx)
                return true;
        }
        if (count >= 16) return true;
        const std::uint8_t slot = g_SetupSlotSeenCount.fetch_add(1);
        if (slot < 16)
            g_SetupSlotSeen[slot].store(slotIdx);
        return false;
    }

    // ---------------------------------------------------------------
    // SetupTargetSelectPrefabListElement "log everything" probe
    // (retail 0x1416C2790). Builds the TOP-LEVEL cursor item list at
    // `MissionPrep + 0x2980` (count at `+0x29A4`). For each cursor in
    // the final list, reads per-cursor data from
    // `MissionPrep + (cursorVal + 0x22) * 0x20`.
    //
    // Post-orig dump — captures full panel state so we can diff
    // vanilla vs custom-outfit and find the visibility delta.
    using SetupTargetSelectPrefab_t = void (__fastcall*)(void* self);
    static SetupTargetSelectPrefab_t g_OrigSetupTargetSelectPrefab = nullptr;
    static bool             g_SetupTargetSelectPrefabInstalled = false;

    // Fire counter so we can dedup but still see multiple panel
    // builds (e.g., once per equip change).
    static std::atomic<std::uint32_t> g_SetupTargetFireCount{ 0 };

    static void __fastcall hkSetupTargetSelectPrefab(void* self)
    {
        // Run orig first so cursor list is populated.
        if (g_OrigSetupTargetSelectPrefab)
            g_OrigSetupTargetSelectPrefab(self);

        const std::uint32_t fireIdx =
            g_SetupTargetFireCount.fetch_add(1);

        // Cap log volume — first 4 fires only.
        if (fireIdx >= 4) return;

        const std::uint8_t pt = outfit::ReadLivePartsType();
        const bool isCustomLive =
            (pt >= outfit::kCustomPartsTypeStart
             && pt <= outfit::kCustomPartsTypeEnd);

        __try
        {
            const auto base = reinterpret_cast<std::uintptr_t>(self);

            // Cursor count at +0x29A4 (1 byte).
            const std::uint8_t count =
                *reinterpret_cast<std::uint8_t*>(base + 0x29A4);

            Log("[OutfitHeadOption:LogAll] fire #%u: self=%p "
                "livePartsType=0x%02X (custom=%d) cursorCount=%u\n",
                fireIdx, self,
                static_cast<unsigned>(pt),
                isCustomLive ? 1 : 0,
                static_cast<unsigned>(count));

            // Cursor list at +0x2980 (4 bytes per cursor, count entries).
            std::uint32_t* cursors =
                reinterpret_cast<std::uint32_t*>(base + 0x2980);
            for (std::uint8_t i = 0; i < count && i < 16; ++i)
            {
                const std::uint32_t cv = cursors[i];

                // Per-cursor data at +(cv + 0x22) * 0x20.
                const std::uintptr_t dataAddr =
                    base + (static_cast<std::uintptr_t>(cv) + 0x22) * 0x20;
                const std::uint64_t* data =
                    reinterpret_cast<std::uint64_t*>(dataAddr);

                // Dump 4 qwords (32 bytes) of per-cursor data.
                Log("[OutfitHeadOption:LogAll]   cursor[%u]=%u (0x%X) "
                    "dataAddr=%p data=%016llX %016llX %016llX %016llX\n",
                    static_cast<unsigned>(i), cv, cv,
                    reinterpret_cast<void*>(dataAddr),
                    static_cast<unsigned long long>(data[0]),
                    static_cast<unsigned long long>(data[1]),
                    static_cast<unsigned long long>(data[2]),
                    static_cast<unsigned long long>(data[3]));
            }

            // Dump the per-cursor data table for ALL cursor values
            // 0..0x11 (regardless of whether they're in the active
            // list) — so we can see if cursor 8's data area differs
            // between vanilla and custom panel state.
            for (std::uint32_t cv = 0; cv <= 0x11; ++cv)
            {
                const std::uintptr_t dataAddr =
                    base + (static_cast<std::uintptr_t>(cv) + 0x22) * 0x20;
                const std::uint64_t* data =
                    reinterpret_cast<std::uint64_t*>(dataAddr);
                Log("[OutfitHeadOption:LogAll]   table[cv=0x%02X]: "
                    "%016llX %016llX %016llX %016llX\n",
                    cv,
                    static_cast<unsigned long long>(data[0]),
                    static_cast<unsigned long long>(data[1]),
                    static_cast<unsigned long long>(data[2]),
                    static_cast<unsigned long long>(data[3]));
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("[OutfitHeadOption:LogAll] dump faulted on fire #%u\n",
                fireIdx);
        }
    }

    static void __fastcall hkSetupCharaSlotPrefab(void* self)
    {
        // Read slot index at this+0x3CA0 BEFORE orig (in case orig
        // modifies it, which is unlikely but safe).
        std::uint32_t slotIdx = 0xFFFFFFFFu;
        __try
        {
            slotIdx = *reinterpret_cast<std::uint32_t*>(
                reinterpret_cast<std::uintptr_t>(self) + 0x3CA0);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}

        const std::uint8_t pt = outfit::ReadLivePartsType();
        const bool isCustomLive =
            (pt >= outfit::kCustomPartsTypeStart
             && pt <= outfit::kCustomPartsTypeEnd);

        if (!SetupSlotAlreadyLogged(slotIdx))
        {
            Log("[OutfitHeadOption:SetupSlot] fire: slotIdx=%u (0x%X) "
                "self=%p livePartsType=0x%02X (custom=%d)\n",
                slotIdx, slotIdx, self,
                static_cast<unsigned>(pt),
                isCustomLive ? 1 : 0);
        }

        if (g_OrigSetupCharaSlotPrefab)
            g_OrigSetupCharaSlotPrefab(self);
    }

    // ---------------------------------------------------------------
    // MissionPreparationSystemImpl::IsEnableHeadOptionSuit
    // (vtable slot 134, retail address 0x140957D30).
    //
    // Upstream visibility filter for the head-option submenu button in
    // SORTIE PREP. Retail body has an inlined switch over a translated
    // equipId, accepting only vanilla suits in the range 0x4A60..0x4A8B
    // (the dozen-or-so vanilla outfits that ship with head-option
    // support — Quiet, EVA, Hospital, etc.). For our custom outfits'
    // flowIndices, the translation falls outside that set and the
    // function returns 0 → the orig UI hides the head-option button.
    //
    // We hook this and return 1 (override-true) when:
    //   - The inbound `flowIndex` matches a registered custom outfit
    //   - That outfit declares `HasHeadOptions()` (Lua provided a
    //     non-empty headOptions array, or supportsHeadOptions=true)
    // Otherwise we fall through to the orig switch.
    using IsEnableHeadOptionSuit_t =
        std::uint8_t (__fastcall*)(void* self, std::uint16_t flowIndex);
    static IsEnableHeadOptionSuit_t g_OrigIsEnableHeadOptionSuit = nullptr;
    static bool                     g_IsEnableHeadOptionSuitInstalled = false;

    // ---------------------------------------------------------------
    // HEAD_OPTION top-level icon gate — retail FUN_1460BC6D0.
    //
    // Vtable[0x4A0] on MissionPreparationSystemImpl resolves (via
    // thunk 0x1409578F0) to this tiny function:
    //   CMP EDX, 0x1F9
    //   SETZ AL
    //   RET
    //
    // It's the visibility check for slot 7 in
    // `SetupTargetSelectPrefabListElement` — slot 7 maps to cursor
    // value 6, and clicking cursor 6 fires phase 0x11 which opens the
    // head-option item selector (where the user picks a head equip via
    // equipKind=0x201). With slot 7 excluded, the HEAD_OPTION icon
    // simply isn't drawn at the top-level character-customize row.
    //
    // The orig calls this gate with EDX = the currently-selected suit
    // flowIndex. Returns 1 only when that flowIndex == 0x1F9 (the
    // single specific vanilla suit that the gate hardcodes). For our
    // custom outfit flowIndices (922..925), the call always returns 0
    // → icon hidden.
    //
    // We override-return 1 when the LIVE partsType is in our custom
    // range AND the registered outfit declares `HasHeadOptions()`.
    // That makes slot 7 appear in the cursor list, surfacing the
    // top-level HEAD_OPTION icon for our outfits — matching the
    // vanilla appears/disappears behavior, just keyed off our outfit's
    // declared support instead of the hardcoded equipId.
    using HeadOptionGate_t =
        std::uint8_t (__fastcall*)(void* self, std::int32_t param2);
    static HeadOptionGate_t g_OrigHeadOptionGate          = nullptr;
    static bool             g_HeadOptionGateInstalled     = false;

    // Per-distinct-param2 dedup so we can see every unique param2 value
    // the function gets called with across the session. Up to 16 slots
    // — enough to see the spectrum without flooding.
    static std::atomic<std::uint32_t> g_HeadOptionGateSeenParam2[16]{};
    static std::atomic<std::uint8_t>  g_HeadOptionGateSeenCount{ 0 };

    static bool HeadOptionGateAlreadyLoggedParam2(std::uint32_t param2)
    {
        const std::uint8_t count = g_HeadOptionGateSeenCount.load();
        for (std::uint8_t i = 0; i < count && i < 16; ++i)
        {
            if (g_HeadOptionGateSeenParam2[i].load() == param2)
                return true;
        }
        if (count >= 16) return true;

        const std::uint8_t slot =
            g_HeadOptionGateSeenCount.fetch_add(1);
        if (slot < 16)
            g_HeadOptionGateSeenParam2[slot].store(param2);
        return false;
    }

    static std::uint8_t __fastcall hkHeadOptionGate(
        void* self, std::int32_t param2)
    {
        const std::uint8_t pt = outfit::ReadLivePartsType();
        const bool isCustomLive =
            (pt >= outfit::kCustomPartsTypeStart
             && pt <= outfit::kCustomPartsTypeEnd);

        // Log the FIRST call for each distinct param2 value. Tells us
        // which contexts this function actually gets called from. Also
        // captures the return address so we can identify the caller
        // (the iteration code that walks 0x1F2..0x202 must live somewhere
        // — find it and we can trace where the result is stored).
        if (!HeadOptionGateAlreadyLoggedParam2(
                static_cast<std::uint32_t>(param2)))
        {
            const std::uint8_t origResult = g_OrigHeadOptionGate
                ? g_OrigHeadOptionGate(self, param2)
                : 0;
            void* retAddr = _ReturnAddress();
            Log("[OutfitHeadOption:HOGate] called with param2=0x%X "
                "livePartsType=0x%02X (custom=%d) — origResult=%d "
                "retAddr=%p\n",
                param2,
                static_cast<unsigned>(pt),
                isCustomLive ? 1 : 0,
                static_cast<int>(origResult),
                retAddr);
        }

        // Skip the override for now — we're in pure-recon mode until
        // we find the iterator and the result cache. Returning the
        // orig result lets vanilla logic flow through unchanged.
        return g_OrigHeadOptionGate(self, param2);
    }

    // First-fire log dedup. Two slots: one for "any call" (confirms
    // hook is alive), one for "override-true" (confirms our outfit
    // was matched).
    static std::atomic<bool> g_IsEnableHeadOptionSuitFirstAnyCall{ false };
    static std::atomic<bool> g_IsEnableHeadOptionSuitFirstOverride{ false };

    static std::uint8_t __fastcall hkIsEnableHeadOptionSuit(
        void* self, std::uint16_t param2)
    {
        // IMPORTANT: param2 here is NOT the suit's flowIndex.
        // GetSelectionNum (retail 0x1416BC2C0) calls this gate as
        //   uVar3 = vtable[0x1f8](self+0x48);   // current-suit "id"
        //   cVar2 = vtable[0x460](self+0x48, uVar3);
        // For our custom outfits, vtable[0x1f8] returns 0x400 (the
        // generic EQP_SUIT base / NONE sentinel) because our Lua sets
        // equipID = TppEquip.EQP_SUIT — there's no specific vanilla
        // equipId for our suit, so the orig's per-flowIndex translation
        // collapses to 0x400.
        //
        // Per-flowIndex lookup therefore won't find our outfit. Key
        // off the LIVE partsType instead — which the OutfitRegistry
        // already tracks — exactly like hkIsEnableCurrentHeadOption.
        const std::uint8_t pt = outfit::ReadLivePartsType();

        if (!g_IsEnableHeadOptionSuitFirstAnyCall.exchange(true))
        {
            Log("[OutfitHeadOption:HOSuit] FIRST CALL: param2=0x%X "
                "livePartsType=0x%02X (custom-range=%s) — keying off "
                "live partsType (NOT param2) since GetSelectionNum "
                "passes the orig's current-suit id, which collapses to "
                "0x400 for our custom outfits\n",
                static_cast<unsigned>(param2),
                static_cast<unsigned>(pt),
                (pt >= outfit::kCustomPartsTypeStart
                 && pt <= outfit::kCustomPartsTypeEnd) ? "YES" : "no");
        }

        if (pt >= outfit::kCustomPartsTypeStart && pt <= outfit::kCustomPartsTypeEnd)
        {
            const outfit::OutfitEntry* entry = nullptr;
            if (outfit::TryGetOutfitByPartsType(pt, &entry) && entry
                && entry->HasHeadOptions())
            {
                if (!g_IsEnableHeadOptionSuitFirstOverride.exchange(true))
                {
                    Log("[OutfitHeadOption:HOSuit] override-true for "
                        "custom outfit partsType=0x%02X developId=%u "
                        "(HasHeadOptions=true). GetSelectionNum should "
                        "now return 3 rows -> HEAD OPTION row appears.\n",
                        static_cast<unsigned>(pt),
                        static_cast<unsigned>(entry->developId));
                }
                return 1;
            }
        }

        return g_OrigIsEnableHeadOptionSuit(self, param2);
    }

    // Per-(param_2, returnPhase) dedup so a UI panel that ticks the
    // function 60Hz with the same selection doesn't flood the log.
    // Tracks up to 32 distinct pairs — beyond that we just stop
    // logging (it's a probe, not a permanent feature).
    static std::atomic<std::uint32_t> g_DecideActSeenSlots[32]{};
    static std::atomic<std::uint8_t>  g_DecideActSeenCount{ 0 };

    // First-fire-only dump of the inner object at MissionPrep+0x48. Lets
    // us identify the actual class at that offset (which the named-build
    // recon guessed was CustomizeSlotSelectorCallbackImpl, but that
    // class's vtable only has 6 slots — slot 0x470 is past its end —
    // so the guess is probably wrong, OR the layout has an offset we
    // haven't accounted for). Dumps regardless of which phase fires.
    static std::atomic<bool> g_DecideActInnerDumped{ false };

    static bool DecideActAlreadyLogged(
        std::uint32_t param2, std::uint32_t returnPhase)
    {
        const std::uint32_t key =
            (param2 & 0xFFFFu) | ((returnPhase & 0xFFFFu) << 16);

        const std::uint8_t count = g_DecideActSeenCount.load();
        for (std::uint8_t i = 0; i < count && i < 32; ++i)
        {
            if (g_DecideActSeenSlots[i].load() == key)
                return true;
        }
        if (count >= 32) return true;  // table full

        const std::uint8_t slot =
            g_DecideActSeenCount.fetch_add(1);
        if (slot < 32)
            g_DecideActSeenSlots[slot].store(key);
        return false;
    }

    static std::uint32_t __fastcall hkDecideActTargetSelWindow(
        void* self, std::uint32_t targetSel)
    {
        const std::uint32_t result = g_OrigDecideAct(self, targetSel);

        if (!DecideActAlreadyLogged(targetSel, result))
        {
            Log("[OutfitHeadOption:DIAG-Decide] fire: targetSel=0x%X "
                "-> phase=0x%X (head-option branch needs phase=0x1C or "
                "0x0A; see your selection action that produced one of "
                "those)\n",
                targetSel, result);
        }

        // First-call inner-object dump. We do this on EVERY fire of
        // DecideActTargetSelWindow (regardless of phase) since the
        // inner object at +0x48 is stable for the life of the SORTIE
        // PREP screen — capturing its vtable lets us cross-reference
        // against the binary to identify the actual class and look up
        // what slot 0x470 resolves to.
        if (!g_DecideActInnerDumped.exchange(true))
        {
            __try
            {
                const auto base   = reinterpret_cast<std::uintptr_t>(self);
                void* const inner = *reinterpret_cast<void**>(base + 0x48);

                Log("[OutfitHeadOption:DIAG-Inner] MissionPrep self=%p "
                    "self+0x48=%p (inner object pointer)\n",
                    self, inner);

                if (inner)
                {
                    void** vtable = *reinterpret_cast<void***>(inner);
                    Log("[OutfitHeadOption:DIAG-Inner] inner vtable=%p "
                        "(grep this address in mgsvtpp.exe_Addresses.txt "
                        "to identify the class)\n",
                        vtable);

                    // Dump slots 0x000..0x500 in 0x40 strides. Wider
                    // than the previous probe so we capture vtable
                    // start (slot 0 is usually destructor / typeinfo
                    // thunk — comparing it against known class layouts
                    // helps identify the class).
                    for (std::size_t off = 0x000; off < 0x500; off += 0x40)
                    {
                        Log("[OutfitHeadOption:DIAG-Inner] vtable+0x%03zX: "
                            "%p %p %p %p %p %p %p %p\n",
                            off,
                            vtable[(off + 0x00) / 8],
                            vtable[(off + 0x08) / 8],
                            vtable[(off + 0x10) / 8],
                            vtable[(off + 0x18) / 8],
                            vtable[(off + 0x20) / 8],
                            vtable[(off + 0x28) / 8],
                            vtable[(off + 0x30) / 8],
                            vtable[(off + 0x38) / 8]);
                    }

                    // Specifically print slot 0x470 by itself for ease
                    // of grep — that's the list-builder we want to
                    // identify.
                    Log("[OutfitHeadOption:DIAG-Inner] vtable[0x470] = %p "
                        "(the suspected list-builder)\n",
                        vtable[0x470 / 8]);
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                Log("[OutfitHeadOption:DIAG-Inner] inner dump faulted\n");
            }
        }

        return result;
    }

    static std::uint8_t __fastcall hkIsEnableCurrentHeadOption(void* self)
    {
        // If the live partsType is custom and the matching outfit
        // declares head-option support, force-enable the submenu.
        // Otherwise pass through to the vanilla gate.
        const std::uint8_t pt = outfit::ReadLivePartsType();

        // DIAGNOSTIC #1 — confirm the function is reachable AT ALL in
        // the user's current UI flow. Fires on the very first call
        // regardless of partsType. If this never logs, the user isn't
        // reaching the UI flow that invokes the gate at all.
        if (!g_DiagAnyCallLogged.exchange(true))
        {
            Log("[OutfitHeadOption:DIAG] first call observed: self=%p "
                "livePartsType=0x%02X (custom-range=%s)\n",
                self, static_cast<unsigned>(pt),
                (pt >= outfit::kCustomPartsTypeStart
                 && pt <= outfit::kCustomPartsTypeEnd) ? "YES" : "no");
        }

        if (pt >= outfit::kCustomPartsTypeStart && pt <= outfit::kCustomPartsTypeEnd)
        {
            const outfit::OutfitEntry* entry = nullptr;
            if (outfit::TryGetOutfitByPartsType(pt, &entry) && entry)
            {
                const bool enabled = entry->HasHeadOptions();

                // DIAGNOSTIC — see comment on g_DiagVtableDumped above.
                // Dump fires for ANY custom outfit (not just those with
                // headOptions declared) so the modder doesn't have to
                // edit Lua before triggering the recon. One-shot.
                if (!g_DiagVtableDumped.exchange(true))
                {
                    __try
                    {
                        const auto base    = reinterpret_cast<std::uintptr_t>(self);
                        void* const inner  =
                            *reinterpret_cast<void**>(base + 0x48);

                        Log("[OutfitHeadOption:DIAG] gate fired for custom "
                            "outfit (partsType=0x%02X developId=%u). self=%p "
                            "self+0x48=%p (CustomizeSlotSelectorCallbackImpl)\n",
                            static_cast<unsigned>(pt),
                            static_cast<unsigned>(entry->developId),
                            self, inner);

                        if (inner)
                        {
                            void** vtable = *reinterpret_cast<void***>(inner);
                            Log("[OutfitHeadOption:DIAG] inner vtable=%p\n",
                                vtable);

                            // Dump 0x100..0x5F8 in 0x40 strides (8 fn ptrs
                            // per line). Wide enough to catch slot 0x470
                            // and any neighbours the list-build might
                            // actually live at on retail.
                            for (std::size_t off = 0x100; off < 0x600; off += 0x40)
                            {
                                Log("[OutfitHeadOption:DIAG] vtable+0x%03zX: "
                                    "%p %p %p %p %p %p %p %p\n",
                                    off,
                                    vtable[(off + 0x00) / 8],
                                    vtable[(off + 0x08) / 8],
                                    vtable[(off + 0x10) / 8],
                                    vtable[(off + 0x18) / 8],
                                    vtable[(off + 0x20) / 8],
                                    vtable[(off + 0x28) / 8],
                                    vtable[(off + 0x30) / 8],
                                    vtable[(off + 0x38) / 8]);
                            }
                        }
                    }
                    __except (EXCEPTION_EXECUTE_HANDLER)
                    {
                        Log("[OutfitHeadOption:DIAG] vtable dump faulted; "
                            "self+0x48 layout may differ from expected\n");
                    }
                }

                return enabled ? 1 : 0;
            }
        }

        return g_OrigIsEnableHead(self);
    }

    static std::uint8_t __fastcall hkIsEnableCurrentSuit(void* self)
    {
        const std::uint8_t pt = outfit::ReadLivePartsType();

        if (!g_IsEnableCurrentSuitFirstCall.exchange(true))
        {
            Log("[OutfitHeadOption:SuitGate] FIRST CALL: self=%p "
                "livePartsType=0x%02X (custom-range=%s) — this is the "
                "short-circuited check that runs BEFORE IsEnableCurrent"
                "HeadOption inside DecideActTargetSelWindow's phase "
                "0x1C / 0x0A branches\n",
                self, static_cast<unsigned>(pt),
                (pt >= outfit::kCustomPartsTypeStart
                 && pt <= outfit::kCustomPartsTypeEnd) ? "YES" : "no");
        }

        if (pt >= outfit::kCustomPartsTypeStart && pt <= outfit::kCustomPartsTypeEnd)
        {
            const outfit::OutfitEntry* entry = nullptr;
            if (outfit::TryGetOutfitByPartsType(pt, &entry) && entry
                && entry->HasHeadOptions())
            {
                if (!g_IsEnableCurrentSuitFirstOverride.exchange(true))
                {
                    Log("[OutfitHeadOption:SuitGate] override-true for "
                        "custom outfit partsType=0x%02X developId=%u "
                        "(HasHeadOptions=true). Gate now allows head-"
                        "option submenu open from cursor 0xf/0x10/0x11.\n",
                        static_cast<unsigned>(pt),
                        static_cast<unsigned>(entry->developId));
                }
                return 1;
            }
        }

        return g_OrigIsEnableCurrentSuit(self);
    }
}

namespace outfit
{
    bool Install_OutfitHeadOption_Hook()
    {
        if (g_Installed) return true;

        void* target = ResolveGameAddress(gAddr.MissionPrep_IsEnableCurrentHeadOption);
        if (!target)
        {
            Log("[OutfitHeadOption] target unresolved; module disabled\n");
            return false;
        }

        g_Installed = CreateAndEnableHook(
            target,
            reinterpret_cast<void*>(&hkIsEnableCurrentHeadOption),
            reinterpret_cast<void**>(&g_OrigIsEnableHead));

        Log("[OutfitHeadOption] installed: %s (target=%p, gate-only — submenu list "
            "build deferred to follow-up runtime probe)\n",
            g_Installed ? "OK" : "FAIL", target);

        // Companion gate: IsEnableCurrentSuit at 0x14A56BFA0. Runs BEFORE
        // IsEnableCurrentHeadOption inside the same DecideActTargetSel-
        // Window check, short-circuiting it on false. We force-return 1
        // for registered custom outfits with HasHeadOptions(), so the
        // gate doesn't reject and the head-option submenu opens.
        void* suitTarget = ResolveGameAddress(gAddr.IsEnableCurrentSuit);
        if (suitTarget)
        {
            g_IsEnableCurrentSuitInstalled = CreateAndEnableHook(
                suitTarget,
                reinterpret_cast<void*>(&hkIsEnableCurrentSuit),
                reinterpret_cast<void**>(&g_OrigIsEnableCurrentSuit));
            Log("[OutfitHeadOption:SuitGate] override hook %s "
                "(target=%p) — pairs with IsEnableCurrentHeadOption to "
                "let cursor 0xf/0x10/0x11 -> phase 0x1C reach the head-"
                "option submenu for custom outfits\n",
                g_IsEnableCurrentSuitInstalled ? "installed" : "FAILED",
                suitTarget);
        }
        else
        {
            Log("[OutfitHeadOption:SuitGate] target unresolved; "
                "skipped\n");
        }

        // Upstream visibility-filter override: hook
        // MissionPreparationSystemImpl::IsEnableHeadOptionSuit so the
        // orig SORTIE PREP UI surfaces a head-option submenu button
        // for our custom outfits. See the long block comment on
        // hkIsEnableHeadOptionSuit above.
        void* hosTarget = ResolveGameAddress(
            gAddr.MissionPrepSystem_IsEnableHeadOptionSuit);
        if (hosTarget)
        {
            g_IsEnableHeadOptionSuitInstalled = CreateAndEnableHook(
                hosTarget,
                reinterpret_cast<void*>(&hkIsEnableHeadOptionSuit),
                reinterpret_cast<void**>(&g_OrigIsEnableHeadOptionSuit));
            Log("[OutfitHeadOption:HOSuit] override hook %s "
                "(target=%p) — orig retail body has hardcoded "
                "switch over 0x4A60..0x4A8B; we override to true for "
                "registered custom outfits with HasHeadOptions()\n",
                g_IsEnableHeadOptionSuitInstalled ? "installed" : "FAILED",
                hosTarget);
        }
        else
        {
            Log("[OutfitHeadOption:HOSuit] target unresolved; "
                "skipped\n");
        }

        // SetupTargetSelectPrefabListElement "log everything" probe.
        void* sstpTarget = ResolveGameAddress(
            gAddr.MissionPrep_SetupTargetSelectPrefabListElement);
        if (sstpTarget)
        {
            g_SetupTargetSelectPrefabInstalled = CreateAndEnableHook(
                sstpTarget,
                reinterpret_cast<void*>(&hkSetupTargetSelectPrefab),
                reinterpret_cast<void**>(&g_OrigSetupTargetSelectPrefab));
            Log("[OutfitHeadOption:LogAll] probe %s "
                "(target=%p) — dumps cursor list + per-cursor table "
                "post-orig, first 4 fires\n",
                g_SetupTargetSelectPrefabInstalled ? "installed" : "FAILED",
                sstpTarget);
        }

        // SetupCharacterSlotSelectPrefabListElement probe — captures
        // which slot indices fire when user navigates the sub-panel,
        // for both vanilla and custom outfits. See block comment on
        // hkSetupCharaSlotPrefab.
        void* sscsTarget = ResolveGameAddress(
            gAddr.MissionPrep_SetupCharaSlotSelectPrefabListElement);
        if (sscsTarget)
        {
            g_SetupCharaSlotPrefabInstalled = CreateAndEnableHook(
                sscsTarget,
                reinterpret_cast<void*>(&hkSetupCharaSlotPrefab),
                reinterpret_cast<void**>(&g_OrigSetupCharaSlotPrefab));
            Log("[OutfitHeadOption:SetupSlot] probe %s "
                "(target=%p) — logs first ~16 distinct slot indices\n",
                g_SetupCharaSlotPrefabInstalled ? "installed" : "FAILED",
                sscsTarget);
        }

        // HEAD_OPTION icon visibility override — the actual gate that
        // controls whether the top-level HEAD_OPTION icon shows in the
        // SORTIE PREP > SELECT CHARACTER row. See block comment on
        // hkHeadOptionGate for full rationale.
        void* hogTarget = ResolveGameAddress(
            gAddr.MissionPrepSystem_HeadOptionGate);
        if (hogTarget)
        {
            g_HeadOptionGateInstalled = CreateAndEnableHook(
                hogTarget,
                reinterpret_cast<void*>(&hkHeadOptionGate),
                reinterpret_cast<void**>(&g_OrigHeadOptionGate));
            Log("[OutfitHeadOption:HOGate] override hook %s "
                "(target=%p) — orig retail body is `CMP EDX,0x1F9 / "
                "SETZ AL / RET`; we override to true when live "
                "partsType is custom and outfit has HasHeadOptions()\n",
                g_HeadOptionGateInstalled ? "installed" : "FAILED",
                hogTarget);
        }
        else
        {
            Log("[OutfitHeadOption:HOGate] target unresolved; "
                "skipped\n");
        }

        // Phase-5 recon probe: hook the parent DecideActTargetSelWindow
        // so we can see which UI selections produce which phases. See
        // the long block comment on g_OrigDecideAct above.
        void* probeTarget = ResolveGameAddress(
            gAddr.MissionPrep_DecideActTargetSelWindow);
        if (probeTarget)
        {
            g_DecideActInstalled = CreateAndEnableHook(
                probeTarget,
                reinterpret_cast<void*>(&hkDecideActTargetSelWindow),
                reinterpret_cast<void**>(&g_OrigDecideAct));
            Log("[OutfitHeadOption:DIAG-Decide] probe %s "
                "(target=%p) — logs first ~32 distinct (targetSel, "
                "phase) pairs\n",
                g_DecideActInstalled ? "installed" : "FAILED", probeTarget);
        }
        else
        {
            Log("[OutfitHeadOption:DIAG-Decide] probe target unresolved; "
                "skipped\n");
        }

        return g_Installed;
    }

    void Uninstall_OutfitHeadOption_Hook()
    {
        if (g_SetupTargetSelectPrefabInstalled)
        {
            if (void* t = ResolveGameAddress(
                    gAddr.MissionPrep_SetupTargetSelectPrefabListElement))
                DisableAndRemoveHook(t);
            g_OrigSetupTargetSelectPrefab      = nullptr;
            g_SetupTargetSelectPrefabInstalled = false;
        }

        if (g_SetupCharaSlotPrefabInstalled)
        {
            if (void* t = ResolveGameAddress(
                    gAddr.MissionPrep_SetupCharaSlotSelectPrefabListElement))
                DisableAndRemoveHook(t);
            g_OrigSetupCharaSlotPrefab      = nullptr;
            g_SetupCharaSlotPrefabInstalled = false;
        }

        if (g_HeadOptionGateInstalled)
        {
            if (void* t = ResolveGameAddress(
                    gAddr.MissionPrepSystem_HeadOptionGate))
                DisableAndRemoveHook(t);
            g_OrigHeadOptionGate      = nullptr;
            g_HeadOptionGateInstalled = false;
        }

        if (g_IsEnableHeadOptionSuitInstalled)
        {
            if (void* t = ResolveGameAddress(
                    gAddr.MissionPrepSystem_IsEnableHeadOptionSuit))
                DisableAndRemoveHook(t);
            g_OrigIsEnableHeadOptionSuit      = nullptr;
            g_IsEnableHeadOptionSuitInstalled = false;
        }

        if (g_DecideActInstalled)
        {
            if (void* t = ResolveGameAddress(
                    gAddr.MissionPrep_DecideActTargetSelWindow))
                DisableAndRemoveHook(t);
            g_OrigDecideAct      = nullptr;
            g_DecideActInstalled = false;
        }

        if (g_IsEnableCurrentSuitInstalled)
        {
            if (void* t = ResolveGameAddress(gAddr.IsEnableCurrentSuit))
                DisableAndRemoveHook(t);
            g_OrigIsEnableCurrentSuit       = nullptr;
            g_IsEnableCurrentSuitInstalled  = false;
        }

        if (!g_Installed) return;
        if (void* t = ResolveGameAddress(gAddr.MissionPrep_IsEnableCurrentHeadOption))
            DisableAndRemoveHook(t);
        g_OrigIsEnableHead = nullptr;
        g_Installed        = false;
        Log("[OutfitHeadOption] removed\n");
    }
}
