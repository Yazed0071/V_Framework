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
    // Three-layer pickup detection:
    //
    //   1. PRIMARY — SupplyCboxActionPluginImpl phase-2 handler
    //      (FUN_1412a2f80 @ 0x1412A2F80). The action plugin has three
    //      phases (1=chopper monitoring, 2=pickup interaction active,
    //      3=post-pickup). Phase 2 is ONLY entered when the player
    //      physically initiates pickup interaction — fires for the
    //      iDroid Supply-Drop UI path.
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
    //   2. BACKSTOP — SupplyCboxSystemImpl::Reset @ 0x1415C5270.
    //      Fires when the crate completes its arc (auto-burst on
    //      landing for dev-menu R&D Requests, or pickup-completion
    //      for iDroid drops where StateHandler1 already won the race).
    //      Required for the dev-menu R&D Request path: orig's
    //      dev-menu request makes the crate AUTO-BURST on landing
    //      without giving the player a chance to physically interact,
    //      so StateHandler1 (1) never fires. Reset is the only
    //      pickup-completion signal in that path.
    //
    //      Gated on +0x10c bit 0 (drop-state active) OR +0x124 bit
    //      0x20000 (active-in-world) so it doesn't fire on post-confirm
    //      cleanup or fresh-init Resets. ConsumePendingSupplyDropDevelopId
    //      is one-shot, so if (1) already won the race the Reset call
    //      finds an empty stash and no-ops.
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

    // ---- (2) SupplyCboxSystemImpl::Reset (backstop) ----
    using Reset_t = void (__fastcall*)(void* self);

    static Reset_t g_OrigReset      = nullptr;
    static bool    g_InstalledReset = false;

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

    // (2) Box-side completion. Required for dev-menu R&D Request path
    // because the crate auto-bursts on landing without the player
    // physically interacting — StateHandler1 never reaches subState=10
    // for that flow. For iDroid drops where StateHandler1 already won,
    // the stash is empty by the time this fires and ConsumeStashAndEquip
    // no-ops.
    //
    // Gated on real-pickup flag bits to avoid firing on post-confirm
    // cleanup or fresh-init Resets (which would consume the stash
    // before the actual pickup).
    static void __fastcall hkReset(void* self)
    {
        // Read box-state bits before orig (orig clears some of them).
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

        // Backstop — typically a no-op for iDroid drops (StateHandler1
        // already consumed the stash). Required for dev-menu R&D Request
        // drops because their crate auto-bursts on landing.
        ConsumeStashAndEquip("ResetBackstop");
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

        // (2) Backstop: box-side reset.
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
