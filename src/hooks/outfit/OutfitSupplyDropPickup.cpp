#include "pch.h"

#include "OutfitSupplyDropPickup.h"
#include "OutfitRegistry.h"
#include "OutfitRuntimeParts.h"

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    // ---------------------------------------------------------------
    // Vanilla-feel supply-drop pickup architecture (2026-04-28 final):
    //
    //   1. EQUIP TRIGGER — SupplyCboxActionPluginImpl phase-2 handler
    //      (FUN_1412a2f80 @ 0x1412A2F80). Phase 2 is entered ONLY when
    //      the player physically initiates pickup interaction (walks
    //      up + presses E). Fires for both iDroid Supply-Drop UI and
    //      dev-menu R&D Request paths once SettledHandler (1.6) keeps
    //      the box alive on landing.
    //
    //      Inside FUN_1412a2f80 the switch is on (param_3 - 1), so the
    //      decomp's case 9 corresponds to param_3 == 10 — that's the
    //      "pickup motion progress check" containing the Lua-hash
    //      0x6c72b84d send at 50% animation progress (line 2402782).
    //      Firing on the FIRST param_3==10 frame catches the start of
    //      pickup motion; the ~500ms FoxPath load completes by ~50%
    //      animation time, so the visible suit transition is masked by
    //      the player's bend-over-box pose — matches vanilla feel.
    //
    //   1.5. RESET BACKSTOP — SupplyCboxSystemImpl::Reset @ 0x1415C5270.
    //      Defense-in-depth: if SettledHandler's override doesn't take
    //      effect (e.g., target unresolved, SEH inside override) and
    //      the box still bursts on landing, Reset fires with the drop-
    //      flag bits set → ConsumeStashAndEquip drives ForcePartsReload
    //      so the outfit equips even without vanilla pickup motion.
    //      Also catches legitimate Reset firings post-StateHandler1
    //      pickup, but at that point the stash is already consumed and
    //      ConsumeStashAndEquip silently no-ops.
    //
    //   1.6. AUTO-BURST PREVENTION (PRIMARY) — SupplyCboxSystemImpl
    //      mode-7 settled handler @ 0x14A3A7B30 (FUN_14a3a7b30). After
    //      the chopper drop arc completes, the state machine reaches
    //      mode 7 ("settled after chopper drop"). For dev-menu R&D
    //      Request crates flags124 bit 0x20 is clear, so orig runs a
    //      raycast and calls Reset on raycast failure → "self-destruct
    //      on landing". Pre-orig override skips orig entirely for our
    //      requests and manually sets flags124 |= 0x68 (0x40+0x20+0x08)
    //      / mode = 8 — the same end state orig produces on its
    //      success path. State machine continues normally; phase 2
    //      engages on player E-press; vanilla pickup motion plays.
    //
    //   1.7. DROP-TIMER TICK (DISPROVEN) — FUN_14a3a83f0 @ 0x14A3A83F0.
    //      Initial hypothesis was that this counter-completion handler
    //      was the auto-burst trigger. Disproven 2026-04-28: never
    //      fires for dev-menu drops (only XREF is a collision callback
    //      that's never invoked during normal drop arc). Hook kept as
    //      no-op telemetry / future-proofing.
    //
    //   2. AUTO-BURST→INTERACTIVE SWITCH (SECONDARY) — SupplyCboxSystem
    //      Impl::RequestToDropImpl @ 0x14A3A9030 (mgsvtpp.exe.c:
    //      2835510). Pre-orig hook clears bit 0x20 at this+0x124 for
    //      our custom-outfit requests so RequestToDropImpl takes the
    //      interactive-pickup branch (this[0x124] |= 0x08, mode = 0)
    //      instead of the auto-burst branch (this[0x124] |= 0x10,
    //      mode = 0xc). Bit 0x20 is naturally clear for dev-menu
    //      drops, so this hook is mostly a no-op there; primary value
    //      is for future request paths that might set bit 0x20 with a
    //      custom-outfit stash matching.
    //
    //   3. CLEANUP — SupplyCboxGameObjectImpl::RestoreRequestFromSVars
    //      @ 0x140ACA230. Fires only on save/load (Lua command
    //      0xc1324e75 = "restore from SVars"), NOT on actual pickup.
    //      Kept purely to clear stale stashes that linger across
    //      save/load boundaries.
    // ---------------------------------------------------------------

    // ---- (1) SupplyCboxActionPluginImpl phase-2 handler ----
    //
    // Verified signature (mgsvtpp.exe.c:2402357 / addresses 0x1412A2F80):
    //   void __fastcall(work, row, sub_state, arg4)
    //   sub_state range: 1..0xa (DEC R8D + CMP 0x9 range check at +0x22)
    //   sub_state == 10 → decomp case 9 (switch on param_3-1) → the
    //     pickup-motion-progress check that fires Lua 0x6c72b84d at
    //     50% (mgsvtpp.exe.c:2402782).
    using StateHandler1_t = void (__fastcall*)(
        void* work, std::uint32_t row, std::uint32_t subState, void* arg4);

    static StateHandler1_t g_OrigStateHandler1      = nullptr;
    static bool            g_InstalledStateHandler1 = false;

    // ---- (1.5) SupplyCboxSystemImpl::Reset (backstop) ----
    //
    // Empirical finding 2026-04-28: dev-menu R&D Request crates auto-
    // burst on landing through a mechanism that is NOT bit 0x20 at
    // self+0x124 (verified clear when RequestToDropImpl ran). Bit 0x10
    // (the auto-burst flag itself) appears to be set by a SECOND code
    // path: thunk_FUN_14a3a83f0 at mgsvtpp.exe.c:2835446 sets it when
    // counter at self+0x754 reaches 0 AND mode at self+0xf0 == 5 or
    // 10. That's the chopper-drop-completion path firing AFTER
    // RequestToDropImpl returned.
    //
    // Until we identify the real gate that distinguishes "auto-burst"
    // from "interactive pickup" (TBD — phase-machine entry condition
    // likely lives in SupplyCboxActionPluginImpl::ExecStateChangeImpl),
    // the practical fallback is to drive the equip from the Reset
    // hook when the auto-burst happens. Outfit changes shortly after
    // landing instead of during a player-pickup animation. Not vanilla
    // perfect, but the feature works.
    using Reset_t = void (__fastcall*)(void* self);

    static Reset_t g_OrigReset      = nullptr;
    static bool    g_InstalledReset = false;

    // ---- (1.6) SupplyCboxSystemImpl::SettledHandler ----
    //
    // Retail 0x14A3A7B30 (FUN_14a3a7b30; mode-7 case in the Update
    // state machine FUN_1415c2210, mgsvtpp.exe.c:2834024-2834026).
    // After the chopper drop arc completes, the state machine
    // advances mode through 0 → 1 → 2 → 3 → ... → 7. Mode 7 is the
    // "settled after chopper drop" handler; this is where iDroid and
    // dev-menu paths diverge.
    //
    // Vanilla orig behavior (mgsvtpp.exe.c:2835324-2835433):
    //   1. flags124 &= ~0x80; flags124 |= 0x40
    //   2. if flags128 & 1 (early-direct path):
    //        dropFlag++; ActivateCollision; flags124 |= 0x28; mode=8;
    //        return
    //   3. if flags124 & 0x10 (auto-burst): Quark vtable callback
    //   4. cVar6 = thunk_FUN_14a3a3570(self) (target-queue check)
    //   5. if cVar6 == 0 AND flags124 & 0x20:
    //        flags124 = (~0x20 | 0x800000); dropFlags &= 0xf8; return
    //   6. if cVar6 != 0 AND flags124 & 0x20 == 0:
    //        raycast for ground; if iVar7 == 0 → CALL RESET → box
    //        bursts on landing → "self-destruct" symptom
    //   7. else: flags124 |= 0x08; mode = 8 (success path)
    //
    // iDroid Supply-Drop UI drops reach mode 7 with flags124 bit 0x20
    // set, so step 6 is skipped (raycast/Reset bypassed) and the
    // function ends with mode=8 + bit 0x08 (interactive-pickup ready).
    // Phase machine then engages phase 2; player walks up + E-press
    // triggers StateHandler1 subState=10 → vanilla pickup motion.
    //
    // Dev-menu R&D Request drops reach mode 7 with bit 0x20 CLEAR.
    // Step 6 runs the raycast; if iVar7 == 0 (target-out-of-range
    // because the dev-menu doesn't aim, just drops at the player),
    // Reset is called and the box bursts.
    //
    // Strategy: pre-orig override. When pendingSupplyDropDevelopId
    // matches a registered custom outfit, COMPLETELY REPLACE orig
    // with manual state advancement that mimics orig's success-path
    // end state without going through the raycast/Reset block:
    //   flags124 &= ~0x80                    // clear bit 0x80
    //   flags124 |= 0x40 | 0x28              // set bits 0x40 / 0x20 / 0x08
    //   mode = 8                             // advance state machine
    // ActivateCollision was already called in case 2 during chopper
    // drop, so we don't need to call it again. thunk_FUN_14a3b0500
    // and thunk_FUN_14a3adc70 (position cleanup) are skipped — the
    // box stays at its drop position which is exactly what iDroid
    // does anyway. State machine continues to mode 8 → 9 → ... and
    // phase 2 engages naturally. Vanilla iDroid drops aren't touched
    // (their pendingSupplyDropDevelopId stash either is 0 or matches
    // a registered outfit — but for vanilla outfits there is no
    // matching registered entry, so isOurRequest stays false).
    using SettledHandler_t = void (__fastcall*)(void* self);

    static SettledHandler_t g_OrigSettledHandler      = nullptr;
    static bool             g_InstalledSettledHandler = false;

    // ---- (1.7) SupplyCboxSystemImpl::OnDropTimerTick (DISPROVEN) ----
    //
    // Retail 0x14A3A83F0 (FUN_14a3a83f0; mgsvtpp.exe.c:2835446).
    // Decrements counter at self+0x754; when counter reaches 0 AND
    // mode at self+0xf0 is 5 or 10, sets bit 0x10 at self+0x124
    // (auto-burst flag) and transitions mode to 6 or 0xb. Initially
    // hypothesized as the auto-burst trigger for dev-menu R&D
    // Request crates, but disproven by runtime testing 2026-04-28:
    // the hook fires ZERO times for the dev-menu path. FUN_14a3a83f0
    // has only ONE call site (FUN_14a3b1ca0, a TargetCallbackExec-
    // style collision/hit handler), and that callback isn't invoked
    // during the dev-menu drop arc. The actual auto-burst trigger
    // is SettledHandler above. Hook kept as a low-cost defense-in-
    // depth no-op + future telemetry hook point.
    using OnDropTimerTick_t = void (__fastcall*)(
        void* self, std::uint16_t decrementBy, void* posVec);

    static OnDropTimerTick_t g_OrigOnDropTimerTick      = nullptr;
    static bool              g_InstalledOnDropTimerTick = false;

    // ---- (2) SupplyCboxSystemImpl::RequestToDropImpl ----
    //
    // Retail 0x14A3A9030 (mgsvtpp.exe.c:2835510). Decides whether the
    // delivered crate auto-bursts on landing or stays put for player
    // interaction. The decision (lines 2835540-2835562):
    //
    //   cVar3 = vtable[0x18]( this+0x20 )
    //   bVar2 = (cVar3 != 0) && (this[0x124] & 0x20)
    //   if (bVar2 && this[0xf0] == 10) {
    //       this[0x124] |= 0x10;     // ← AUTO-BURST mode
    //       this[0xf0]  = 0xc;
    //   } else {
    //       this[0x124] |= 0x08;     // ← INTERACTIVE PICKUP
    //       this[0xf0]  = 0;
    //   }
    //
    // Dev-menu R&D Request supply drops set bit 0x20 at this+0x124,
    // which makes them auto-burst on landing. iDroid Supply-Drop UI
    // drops don't have that bit set, so they take the interactive
    // pickup branch — vanilla "walk to crate, press E" feel.
    //
    // We hook this function and clear bit 0x20 at this+0x124 BEFORE
    // calling orig, but ONLY for our custom-outfit requests (gated on
    // pendingSupplyDropDevelopId matching a registered outfit). With
    // bit 0x20 cleared, bVar2 stays false, and orig takes the
    // interactive-pickup branch. The crate stays put after landing,
    // the player walks to it and presses E, StateHandler1 (PickupAnim
    // subState=10) fires, ConsumeStashAndEquip runs — exactly like
    // iDroid drops. Vanilla supply drops are not touched (their bit
    // 0x20 reflects intentional auto-burst behavior for stock items).
    using RequestToDropImpl_t = void (__fastcall*)(void* self, void* request);

    static RequestToDropImpl_t g_OrigRequestToDropImpl      = nullptr;
    static bool                g_InstalledRequestToDropImpl = false;

    // ---- (3) SupplyCboxGameObjectImpl::RestoreRequestFromSVars ----
    using RestoreRequestFromSVars_t = void (__fastcall*)(void* self);

    static RestoreRequestFromSVars_t g_OrigRestore       = nullptr;
    static bool                      g_InstalledRestore  = false;

    // Consume the supply-drop stash, look up the registered outfit,
    // and drive ForcePartsReload to equip the body. One-shot —
    // subsequent calls with empty stash silently no-op.
    //
    // Architecture (verified working both iDroid and dev-menu paths
    // 2026-04-28):
    //
    //   1. ForcePartsReload's trampoline call drives an internal
    //      LoadPartsNew that primes asset-load setup state.
    //   2. ForcePartsReload writes Quark live state with the real
    //      outfit bytes (partsType=0x40, camo=0x80).
    //   3. Orig pickup pipeline's redundant LoadPartsNew arrives ~100-
    //      200ms later. Because Quark was updated, orig's call has
    //      FULL bytes (partsType=0x40, camo=0x80) — NOT a broken-
    //      custom transient.
    //   4. hkLoadPartsNew main-flow spoof activates (partsType
    //      0x40 -> 0x00 spoof), orig runs, returns.
    //   5. LoadPlayerPartsParts dispatches → body loads ✓.
    //
    // The dev-menu R&D path additionally relies on
    // OutfitItemSelector::hkSetSupplyCBoxInfo's post-orig overwrite
    // of the SupplyCboxLoadoutInfo at self+0x23A0, which guarantees
    // the supply-drop request itself carries the correct outfit
    // bytes (orig populates that struct from an OOB suit-info-table
    // read for our custom flowIndices, so we overwrite post-orig
    // before the trigger event handler reads the bytes lazily).
    static bool ConsumeStashAndEquip(const char* tag)
    {
        const std::uint16_t pendingDevId =
            outfit::ConsumePendingSupplyDropDevelopId();
        if (pendingDevId == 0) return false;

        const outfit::OutfitEntry* entry = nullptr;
        if (!outfit::TryGetOutfitByDevelopId(pendingDevId, &entry) || !entry)
        {
            Log("[OutfitSupplyDropPickup:%s] consumed stash developId=%u "
                "but no matching registered entry; skipping\n",
                tag, static_cast<unsigned>(pendingDevId));
            return true;
        }

        Log("[OutfitSupplyDropPickup:%s] forcing equip of stashed "
            "developId=%u partsType=0x%02X selector=0x%02X playerType=%u\n",
            tag,
            static_cast<unsigned>(entry->developId),
            static_cast<unsigned>(entry->partsType),
            static_cast<unsigned>(entry->selectorCode),
            static_cast<unsigned>(entry->playerType));

        __try
        {
            outfit::ForcePartsReload(entry->playerType,
                                      entry->partsType,
                                      entry->selectorCode);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("[OutfitSupplyDropPickup:%s] SEH while driving "
                "ForcePartsReload\n", tag);
        }
        return true;
    }

    // (1) Player-side pickup state. Fires per-frame while sub-state in
    // 1..11 — keep the fast path small, only act on subState==10.
    static void __fastcall hkStateHandler1(
        void* work, std::uint32_t row, std::uint32_t subState, void* arg4)
    {
        if (g_OrigStateHandler1)
            g_OrigStateHandler1(work, row, subState, arg4);

        // Sub-state 10 = pickup motion in progress. Consume the stash
        // on first entry with a pending dev — subsequent state-10
        // frames find empty stash and skip. ForcePartsReload kicks
        // off the asset load so the suit becomes visible mid-animation.
        if (subState == 10u)
            ConsumeStashAndEquip("PickupAnim");
    }

    // (1.5) Box-side Reset — fires when the crate completes its arc
    // (auto-burst on landing for dev-menu R&D Request, OR pickup
    // completion for iDroid drops where StateHandler1 already won).
    //
    // For dev-menu drops: this is the equip trigger because the crate
    // auto-bursts before the player can physically interact. Not
    // vanilla pickup-motion feel, but makes the feature work.
    //
    // For iDroid drops: stash is empty by the time this fires (already
    // consumed by StateHandler1 PickupAnim) → ConsumeStashAndEquip
    // no-ops. Vanilla pickup-motion is preserved.
    //
    // Gated on real-pickup flag bits to avoid firing on post-confirm
    // cleanup or fresh-init Resets (which would consume the stash
    // before the actual pickup):
    //   self+0x10c bit 0..2 → drop-state active
    //   self+0x124 bit 0x20000 → active-in-world
    static void __fastcall hkReset(void* self)
    {
        std::uint8_t  flags10c = 0;
        std::uint32_t flags124 = 0;
        if (self)
        {
            __try
            {
                const auto* base = reinterpret_cast<const std::uint8_t*>(self);
                flags10c = base[0x10c];
                flags124 = *reinterpret_cast<const std::uint32_t*>(base + 0x124);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                flags10c = 0;
                flags124 = 0;
            }
        }

        const bool dropFlags  = (flags10c & 0x07u) != 0;
        const bool activeColl = (flags124 & 0x20000u) != 0;
        const bool realPickup = dropFlags || activeColl;

        if (g_OrigReset) g_OrigReset(self);

        if (!realPickup) return;

        ConsumeStashAndEquip("ResetBackstop");
    }

    // (2) RequestToDropImpl — the auto-burst-vs-interactive switch.
    //
    // Pre-orig hook: peek pendingSupplyDropDevelopId (non-consuming,
    // we want StateHandler1 to consume it during pickup interaction).
    // If the stash matches a registered custom outfit, clear bit 0x20
    // at self+0x124. Orig's branch at line 2835554 (mgsvtpp.exe.c)
    // then sees `bVar2 == false` because the bit-0x20 check fails,
    // and takes the else-branch: `this[0x124] |= 0x08; this[0xf0] = 0`
    // — the INTERACTIVE PICKUP setup. The crate lands and stays put
    // until the player walks up and presses E, just like iDroid
    // Supply-Drop UI drops.
    //
    // For non-custom-outfit requests (vanilla supply drops, including
    // weapon/item drops where bit 0x20 indicates intentional auto-
    // deploy behavior), we leave the flag alone and orig runs as
    // designed.
    static void __fastcall hkRequestToDropImpl(void* self, void* request);

    // (1.6) Mode-7 "settled" handler. Pre-orig override for our
    // custom-outfit requests: skip orig (which would call Reset on
    // the raycast-fail path) and manually advance the state machine
    // to mode=8 with flags124 bits 0x40 / 0x20 / 0x08 set. This is
    // the same end state orig produces on its success path; we just
    // skip the raycast/Reset block.
    //
    // Vanilla orig fall-through preserved for non-custom requests.
    static void __fastcall hkSettledHandler(void* self)
    {
        if (!self)
        {
            if (g_OrigSettledHandler) g_OrigSettledHandler(self);
            return;
        }

        // Peek (non-consuming) — the stash is consumed later by
        // StateHandler1 phase-2 when the player physically interacts
        // with the box. We just need to know if THIS box is ours.
        const std::uint16_t pendingDevId =
            outfit::PeekPendingSupplyDropDevelopId();
        const outfit::OutfitEntry* entry = nullptr;
        const bool isOurRequest =
            pendingDevId != 0
         && outfit::TryGetOutfitByDevelopId(pendingDevId, &entry)
         && entry;

        if (!isOurRequest)
        {
            // Vanilla supply drop — let orig run its normal logic
            // (including the raycast/Reset path for vanilla items
            // that genuinely need ground-level alignment).
            if (g_OrigSettledHandler) g_OrigSettledHandler(self);
            return;
        }

        // Our custom-outfit request. Snapshot pre-state for telemetry,
        // then skip orig entirely and manually drive the state machine
        // to the success-path end state.
        std::uint32_t flags124Pre = 0;
        std::uint32_t modePre     = 0;
        __try
        {
            const auto* base = reinterpret_cast<const std::uint8_t*>(self);
            flags124Pre = *reinterpret_cast<const std::uint32_t*>(base + 0x124);
            modePre     = *reinterpret_cast<const std::uint32_t*>(base + 0xF0);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            // Snapshot failed — fall back to orig so we don't leave
            // the state machine stalled. Orig may still call Reset,
            // but the Reset hook's ConsumeStashAndEquip will catch it.
            if (g_OrigSettledHandler) g_OrigSettledHandler(self);
            return;
        }

        __try
        {
            auto* base = reinterpret_cast<std::uint8_t*>(self);
            auto* flags124 =
                reinterpret_cast<std::uint32_t*>(base + 0x124);
            auto* mode =
                reinterpret_cast<std::uint32_t*>(base + 0xF0);

            // Mimic orig's pre-branch state changes (lines 2835352-
            // 2835353): clear bit 0x80, set bit 0x40.
            //
            // PLUS: clear bits that drive the box down the auto-burst
            // pipeline downstream:
            //   bit 0x10  — "auto-burst armed" flag. Set somewhere in
            //               the chopper-drop path for dev-menu drops
            //               (NOT cleared by our RequestToDropImpl
            //               hook because that hook ran 9s earlier).
            //               Empirically present in flags124 at
            //               SettledHandler entry (e.g. 0x520A30).
            //               Case 0xe / 0xf / 0x10 handlers all branch
            //               on this bit to drive bursting visual + a
            //               timer-based Reset call. CLEARING IT here
            //               prevents that progression.
            //   bit 0x200 — case-8 mode-dispatch steer. With bit 0x200
            //               set, FUN_1415cd8e0 advances mode 8 → 0xe
            //               (case 0xe burst path). Clear, the path
            //               goes 8 → 10 (decimal, case-10 handler)
            //               which has cleaner bit-0x100-gated branches
            //               we can keep at "no progress" state.
            // bit 0x400 is left as-is (it's also a steer toggle — if
            // already clear, fine; if set, we still take a sane path).
            *flags124 = (*flags124 & ~(0x80u | 0x10u | 0x200u)) | 0x40u;

            // End-state same as orig's success path (line 2835426
            // sets bit 0x20; line 2835428 sets bit 0x08; line 2835429
            // sets mode=8). Combined into one write.
            *flags124 |= 0x28u;     // bits 0x20 + 0x08
            *mode      = 8u;        // advance state machine

            Log("[OutfitSupplyDropPickup:SettledHandler] override for "
                "custom outfit developId=%u: skipped orig and "
                "manually advanced state. mode 0x%X→0x8, flags124 "
                "0x%08X→0x%08X (set 0x40/0x20/0x08; cleared "
                "0x80/0x10/0x200 to suppress downstream auto-burst "
                "pipeline in cases 0xe/0xf/0x10) — phase 2 should "
                "engage for player E-press pickup\n",
                static_cast<unsigned>(entry->developId),
                static_cast<unsigned>(modePre),
                static_cast<unsigned>(flags124Pre),
                static_cast<unsigned>(*flags124));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("[OutfitSupplyDropPickup:SettledHandler] SEH while "
                "writing override state for developId=%u — falling "
                "through to orig (auto-burst may follow)\n",
                static_cast<unsigned>(entry->developId));
            if (g_OrigSettledHandler) g_OrigSettledHandler(self);
        }
    }

    // (1.7) Drop-timer tick handler. Post-orig: if our stash matches
    // the request AND orig just transitioned mode 10 → 0xb (and set
    // bit 0x10 at self+0x124), revert both. The box stays in mode 10
    // (chopper-drop-active), no auto-burst, phase 2 engages → player
    // can walk up and press E like vanilla iDroid drops.
    //
    // 2026-04-28: this hook was disproven as the auto-burst trigger
    // for dev-menu requests (its only call site is FUN_14a3b1ca0, a
    // collision callback that doesn't fire during normal drops). Hook
    // kept for low-cost telemetry / defense-in-depth.
    static void __fastcall hkOnDropTimerTick(
        void* self, std::uint16_t decrementBy, void* posVec)
    {
        // Snapshot mode + flags PRE-orig so we know what orig changed.
        std::uint32_t modePre = 0;
        std::uint32_t flagsPre = 0;
        bool snapshotOk = false;
        if (self)
        {
            __try
            {
                const auto* base = reinterpret_cast<const std::uint8_t*>(self);
                modePre  = *reinterpret_cast<const std::uint32_t*>(base + 0xF0);
                flagsPre = *reinterpret_cast<const std::uint32_t*>(base + 0x124);
                snapshotOk = true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }

        if (g_OrigOnDropTimerTick) g_OrigOnDropTimerTick(self, decrementBy, posVec);

        if (!snapshotOk || !self) return;

        // Check if our stash matches a registered custom outfit. Peek
        // (non-consuming) — StateHandler1 PickupAnim consumes when
        // player completes pickup interaction.
        const std::uint16_t pendingDevId =
            outfit::PeekPendingSupplyDropDevelopId();
        const outfit::OutfitEntry* entry = nullptr;
        const bool isOurRequest =
            pendingDevId != 0
         && outfit::TryGetOutfitByDevelopId(pendingDevId, &entry)
         && entry;
        if (!isOurRequest) return;

        // Read what orig set.
        std::uint32_t modePost  = 0;
        std::uint32_t flagsPost = 0;
        __try
        {
            const auto* base = reinterpret_cast<const std::uint8_t*>(self);
            modePost  = *reinterpret_cast<const std::uint32_t*>(base + 0xF0);
            flagsPost = *reinterpret_cast<const std::uint32_t*>(base + 0x124);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return; }

        // Detect the auto-burst transition we want to revert:
        //   mode pre==10, mode post==0xb, bit 0x10 newly set in flags.
        const bool wasMode10  = (modePre == 10u);
        const bool nowMode0xB = (modePost == 0x0Bu);
        const bool bit0x10NewlySet =
            ((flagsPre & 0x10u) == 0u) && ((flagsPost & 0x10u) != 0u);

        if (wasMode10 && nowMode0xB && bit0x10NewlySet)
        {
            __try
            {
                auto* base = reinterpret_cast<std::uint8_t*>(self);
                // Revert: keep mode at 10 (chopper-drop-active) so the
                // phase machine can engage interactive pickup.
                *reinterpret_cast<std::uint32_t*>(base + 0xF0) = 10u;
                // Clear bit 0x10 (auto-burst flag) but keep all other
                // flag bits orig set.
                *reinterpret_cast<std::uint32_t*>(base + 0x124) =
                    flagsPost & ~0x10u;

                Log("[OutfitSupplyDropPickup:OnDropTimerTick] reverted "
                    "auto-burst transition for custom outfit developId=%u "
                    "(mode 10→0xB → 10, flags 0x%08X → 0x%08X with bit "
                    "0x10 cleared) — box stays in chopper-drop-active "
                    "state for phase-2 interactive pickup\n",
                    static_cast<unsigned>(entry->developId),
                    static_cast<unsigned>(flagsPost),
                    static_cast<unsigned>(flagsPost & ~0x10u));
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                Log("[OutfitSupplyDropPickup:OnDropTimerTick] SEH "
                    "reverting mode/flags for developId=%u\n",
                    static_cast<unsigned>(entry->developId));
            }
        }
    }

    static void __fastcall hkRequestToDropImpl(void* self, void* request)
    {
        // 2026-04-28 — DOWNGRADED to observer-only (no state mutation).
        //
        // Original intent: clear bit 0x20 at self+0x124 for our custom-
        // outfit requests so RequestToDropImpl's orig branch at line
        // 2835554 takes the interactive-pickup path instead of the
        // auto-burst path. That was based on the (incorrect) assumption
        // that auto-burst-on-landing came from RequestToDropImpl's
        // bit-0x10-set branch.
        //
        // Empirically the bit-0x20-clear breaks iDroid Supply-Drop UI
        // drops:
        //   * iDroid drops have bit 0x20 SET naturally at request time
        //     (vanilla pre-orig flags=0x...B228 with bit 0x20 = 0x20).
        //   * Vanilla orig case-7 handler (FUN_14a3a7b30) checks bit
        //     0x20:
        //       - bit 0x20 SET → skip raycast block → mode = 8 cleanly
        //       - bit 0x20 CLEAR → raycast block runs (position adjust,
        //         thunk_FUN_14a3b0500 + thunk_FUN_14a3adc70 calls,
        //         re-set bit 0x20 at end)
        //   * The thunk_FUN_14a3b0500 / thunk_FUN_14a3adc70 calls in
        //     the raycast block evidently auto-progress the supply-cbox
        //     state machine to phase 2 (pickup-active) at chopper
        //     landing without requiring player E-press. StateHandler1
        //     subState=10 fires immediately, our PickupAnim hook
        //     consumes the stash, ForcePartsReload equips the outfit
        //     PREMATURELY — user perceives this as "uniform changes
        //     before box arrives" (verified in user log 13:31:01-
        //     13:31:11 — both FROG enableHead=1 and Jill enableHead=0
        //     iDroid requests equipped at chopper-landing time without
        //     player walking to the box).
        //
        //   * Dev-menu R&D Request drops have bit 0x20 CLEAR naturally,
        //     so our hook's clear was always a no-op for that path.
        //
        // Net: the bit-0x20-clear was harmful to iDroid and useless to
        // dev-menu. Hook is kept as an observer (logs the request flag
        // state for diagnostic purposes) but no longer mutates state.
        if (self)
        {
            const std::uint16_t pendingDevId =
                outfit::PeekPendingSupplyDropDevelopId();
            const outfit::OutfitEntry* entry = nullptr;
            const bool isOurRequest =
                pendingDevId != 0
             && outfit::TryGetOutfitByDevelopId(pendingDevId, &entry)
             && entry;

            if (isOurRequest)
            {
                __try
                {
                    const std::uint32_t flags124 =
                        *reinterpret_cast<const std::uint32_t*>(
                            reinterpret_cast<const std::uint8_t*>(self) + 0x124);
                    Log("[OutfitSupplyDropPickup:RequestToDropImpl] "
                        "observed flags124=0x%08X (bit 0x20 %s) for "
                        "custom outfit developId=%u partsType=0x%02X — "
                        "leaving state untouched (orig decides interactive "
                        "vs auto-burst per cVar3/bit-0x20 gate)\n",
                        static_cast<unsigned>(flags124),
                        (flags124 & 0x20u) ? "set (iDroid pattern)"
                                           : "clear (dev-menu pattern)",
                        static_cast<unsigned>(entry->developId),
                        static_cast<unsigned>(entry->partsType));
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    Log("[OutfitSupplyDropPickup:RequestToDropImpl] SEH "
                        "reading self+0x124 (self=%p)\n", self);
                }
            }
        }

        if (g_OrigRequestToDropImpl) g_OrigRequestToDropImpl(self, request);
    }

    // (3) Save/load cleanup — never represents a real pickup.
    static void __fastcall hkRestore(void* self)
    {
        // Defensive: clear any stash that lingered across a save/load
        // boundary so it can't fire stale on the next box.
        const std::uint16_t pendingDevId =
            outfit::ConsumePendingSupplyDropDevelopId();
        if (pendingDevId != 0)
        {
            Log("[OutfitSupplyDropPickup:Restore] cleared stale stash "
                "developId=%u (save/load boundary)\n",
                static_cast<unsigned>(pendingDevId));
        }
        if (g_OrigRestore) g_OrigRestore(self);
    }
}

namespace outfit
{
    bool Install_OutfitSupplyDropPickup_Hook()
    {
        // (1) Primary: player state handler.
        if (!g_InstalledStateHandler1)
        {
            void* target = ResolveGameAddress(
                gAddr.SupplyCboxActionPluginImpl_StateHandler1);
            if (target)
            {
                g_InstalledStateHandler1 = CreateAndEnableHook(
                    target,
                    reinterpret_cast<void*>(&hkStateHandler1),
                    reinterpret_cast<void**>(&g_OrigStateHandler1));
                Log("[OutfitSupplyDropPickup] StateHandler1 installed: %s "
                    "(target=%p)\n",
                    g_InstalledStateHandler1 ? "OK" : "FAIL", target);
            }
            else
            {
                Log("[OutfitSupplyDropPickup] StateHandler1 target unresolved\n");
            }
        }

        // (1.5) Backstop: fires on auto-burst-on-landing for dev-menu
        // drops (when StateHandler1 misses).
        if (!g_InstalledReset)
        {
            void* target = ResolveGameAddress(
                gAddr.SupplyCboxSystemImpl_Reset);
            if (target)
            {
                g_InstalledReset = CreateAndEnableHook(
                    target,
                    reinterpret_cast<void*>(&hkReset),
                    reinterpret_cast<void**>(&g_OrigReset));
                Log("[OutfitSupplyDropPickup] Reset installed: %s "
                    "(target=%p)\n",
                    g_InstalledReset ? "OK" : "FAIL", target);
            }
            else
            {
                Log("[OutfitSupplyDropPickup] Reset target unresolved\n");
            }
        }

        // (1.6) Mode-7 settled handler — DISABLED 2026-04-28.
        //
        // The override successfully suppresses auto-burst-on-landing
        // (verified in user log 13:09:37 — box stays put, no visual
        // burst, player can walk up and press E for vanilla pickup
        // motion). HOWEVER it ALSO breaks body load: with auto-burst
        // path bypassed, the orig supply-drop pipeline's mode-0xe
        // handler (FUN_1415c06f0) doesn't run, which in turn doesn't
        // prime asset-load state. When phase 2 (StateHandler1) fires
        // the equip via ForcePartsReload, the orig pickup pipeline's
        // own LoadPartsNew arrives ~130ms later with a broken-custom
        // transient (partsType=0, camo=0xFF) — the resolver in
        // OutfitRuntimeParts::hkLoadPartsNew rewrites bytes to the
        // registered outfit, spoofs partsType→0x00, calls orig — but
        // orig RETURNS WITHOUT DISPATCHING LoadPlayerPartsParts. Body
        // never loads, infinite loading screen.
        //
        // Empirically the auto-burst path's mode-0xe processing IS
        // required for the asset load dispatch to succeed (verified
        // in user log 06:12:50 — auto-burst → ResetBackstop fires
        // ForcePartsReload → orig pipeline's full-info LoadPartsNew
        // 129ms later → LoadPlayerPartsParts dispatches → body loads).
        //
        // Until we identify what specifically primes the load (likely
        // DeactivateCollision or a Quark vtable callback inside the
        // bit-0x10 block of FUN_1415c06f0), the SettledHandler hook
        // is disabled and we accept the auto-burst-on-landing visual
        // for dev-menu R&D Request drops. iDroid Supply-Drop UI drops
        // are unaffected (they have a remote drop position so phase 2
        // engages naturally without auto-burst progression).
        //
        // The hook function body is retained for future iteration —
        // re-enabling it requires also identifying and replicating
        // whatever the bit-0x10 mode-0xe block does to prime asset
        // loading.
        if (false && !g_InstalledSettledHandler)
        {
            void* target = ResolveGameAddress(
                gAddr.SupplyCboxSystemImpl_SettledHandler);
            if (target)
            {
                g_InstalledSettledHandler = CreateAndEnableHook(
                    target,
                    reinterpret_cast<void*>(&hkSettledHandler),
                    reinterpret_cast<void**>(&g_OrigSettledHandler));
                Log("[OutfitSupplyDropPickup] SettledHandler installed: "
                    "%s (target=%p)\n",
                    g_InstalledSettledHandler ? "OK" : "FAIL", target);
            }
        }
        Log("[OutfitSupplyDropPickup] SettledHandler hook DISABLED "
            "2026-04-28 — auto-burst path required for body asset-load "
            "dispatch; ResetBackstop drives equip on landing\n");

        // (1.7) Drop-timer tick: disproven hypothesis (kept as no-op
        // telemetry). Real auto-burst path is SettledHandler above.
        if (!g_InstalledOnDropTimerTick)
        {
            void* target = ResolveGameAddress(
                gAddr.SupplyCboxSystemImpl_OnDropTimerTick);
            if (target)
            {
                g_InstalledOnDropTimerTick = CreateAndEnableHook(
                    target,
                    reinterpret_cast<void*>(&hkOnDropTimerTick),
                    reinterpret_cast<void**>(&g_OrigOnDropTimerTick));
                Log("[OutfitSupplyDropPickup] OnDropTimerTick installed: %s "
                    "(target=%p)\n",
                    g_InstalledOnDropTimerTick ? "OK" : "FAIL", target);
            }
            else
            {
                Log("[OutfitSupplyDropPickup] OnDropTimerTick target unresolved "
                    "(JP build?) — auto-burst-on-landing reversion will not "
                    "run; Reset backstop still equips the outfit\n");
            }
        }

        // (2) Auto-burst→interactive switch: clear bit 0x20 at +0x124
        // for our custom-outfit requests so they take the vanilla
        // interactive-pickup branch.
        if (!g_InstalledRequestToDropImpl)
        {
            void* target = ResolveGameAddress(
                gAddr.SupplyCboxSystemImpl_RequestToDropImpl);
            if (target)
            {
                g_InstalledRequestToDropImpl = CreateAndEnableHook(
                    target,
                    reinterpret_cast<void*>(&hkRequestToDropImpl),
                    reinterpret_cast<void**>(&g_OrigRequestToDropImpl));
                Log("[OutfitSupplyDropPickup] RequestToDropImpl installed: %s "
                    "(target=%p)\n",
                    g_InstalledRequestToDropImpl ? "OK" : "FAIL", target);
            }
            else
            {
                Log("[OutfitSupplyDropPickup] RequestToDropImpl target unresolved "
                    "(JP build?) — dev-menu R&D Request will fall back to "
                    "auto-burst behavior\n");
            }
        }

        // (3) Cleanup: save/load.
        if (!g_InstalledRestore)
        {
            void* target = ResolveGameAddress(
                gAddr.SupplyCboxGameObjectImpl_RestoreRequestFromSVars);
            if (target)
            {
                g_InstalledRestore = CreateAndEnableHook(
                    target,
                    reinterpret_cast<void*>(&hkRestore),
                    reinterpret_cast<void**>(&g_OrigRestore));
                Log("[OutfitSupplyDropPickup] Restore installed: %s "
                    "(target=%p)\n",
                    g_InstalledRestore ? "OK" : "FAIL", target);
            }
            else
            {
                Log("[OutfitSupplyDropPickup] Restore target unresolved\n");
            }
        }

        return g_InstalledStateHandler1
            || g_InstalledReset
            || g_InstalledSettledHandler
            || g_InstalledOnDropTimerTick
            || g_InstalledRequestToDropImpl
            || g_InstalledRestore;
    }

    void Uninstall_OutfitSupplyDropPickup_Hook()
    {
        if (g_InstalledStateHandler1)
        {
            if (void* t = ResolveGameAddress(
                    gAddr.SupplyCboxActionPluginImpl_StateHandler1))
                DisableAndRemoveHook(t);
            g_OrigStateHandler1       = nullptr;
            g_InstalledStateHandler1  = false;
        }

        if (g_InstalledReset)
        {
            if (void* t = ResolveGameAddress(
                    gAddr.SupplyCboxSystemImpl_Reset))
                DisableAndRemoveHook(t);
            g_OrigReset       = nullptr;
            g_InstalledReset  = false;
        }

        if (g_InstalledSettledHandler)
        {
            if (void* t = ResolveGameAddress(
                    gAddr.SupplyCboxSystemImpl_SettledHandler))
                DisableAndRemoveHook(t);
            g_OrigSettledHandler       = nullptr;
            g_InstalledSettledHandler  = false;
        }

        if (g_InstalledOnDropTimerTick)
        {
            if (void* t = ResolveGameAddress(
                    gAddr.SupplyCboxSystemImpl_OnDropTimerTick))
                DisableAndRemoveHook(t);
            g_OrigOnDropTimerTick       = nullptr;
            g_InstalledOnDropTimerTick  = false;
        }

        if (g_InstalledRequestToDropImpl)
        {
            if (void* t = ResolveGameAddress(
                    gAddr.SupplyCboxSystemImpl_RequestToDropImpl))
                DisableAndRemoveHook(t);
            g_OrigRequestToDropImpl       = nullptr;
            g_InstalledRequestToDropImpl  = false;
        }

        if (g_InstalledRestore)
        {
            if (void* t = ResolveGameAddress(
                    gAddr.SupplyCboxGameObjectImpl_RestoreRequestFromSVars))
                DisableAndRemoveHook(t);
            g_OrigRestore       = nullptr;
            g_InstalledRestore  = false;
        }

        Log("[OutfitSupplyDropPickup] removed\n");
    }
}
