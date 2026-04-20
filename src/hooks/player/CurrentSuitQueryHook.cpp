#include "pch.h"

#include <cstdint>
#include <windows.h>   // EXCEPTION_EXECUTE_HANDLER for SEH on vtable walk

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "CustomSuitRegistry.h"
#include "CurrentSuitQueryHook.h"

// ----------------------------------------------------------------------------
// CurrentSuitQueryHook — the vanilla-correct "this suit is equipped" bridge.
//
// The game has two authoritative data sources for "what outfit is the player
// wearing":
//
//   (1) Quark live state[0xF8] u8 partsType — the GAMEPLAY source of truth.
//       Written by Player2UtilityImpl::SetSuitAndHandConditionWithLoadoutInfo
//       during every commit. Drives rendering / body type.
//
//   (2) The UI's LoadoutInfo query interface at MissionPreparationCallbackImpl
//       +0xa0. Its vtable+0x180 returns "the equipId currently in slot N"
//       for slots 0, 1, 2 (CHARACTER / UNIFORMS / HEAD OPTION). This is what
//       every UNIFORMS-menu UI check actually reads: the EQP badge comparison,
//       icon/name lookup, IsDeveloped gating. Populated by the equip-system
//       side (independent of our mission-prep commit).
//
// For vanilla suits, the commit path eventually synchronises (1) → (2) via
// the equip system. For custom suits (partsType 0x40+), no such path exists
// — the vanilla equip system has no entry mapping our custom partsType/
// selectorCode to our custom flowIndex, so the UI query returns 0 (or 0x400)
// and every downstream "is this equipped?" check fails.
//
// Our legacy hook landscape compensated for this by forcing FOUR or FIVE
// separate downstream queries:
//   - hkGetCurrentSuitFlowIndex (FUN_140955c70) — which actually reads RAX
//     garbage because FUN_140955c70 is a VOID setter. It only "works" today
//     because `EDC->vtable+0x600(flowIndex, u8)` happens to leave the
//     flowIndex in RAX on return. Fragile undefined behaviour.
//   - hkIsEnableCurrentSuit — force-true for custom suits.
//   - hkIsDeveloped (vtable+0x188 on loadout controller) — force-true when
//     custom suit is live inside SetupEquipPanelParam. DISABLED currently
//     because forcing true took the UNIFORMS panel down a crash-prone path.
//   - hkSetItemDetail — remap custom flowIndex to vanilla one for the
//     HEAD-OPTION catalog prime.
//
// This file installs these hooks at the earliest/highest layer the game
// consults — plus two dynamic vtable hooks for the UI-side display chain:
//
//   A. GetEquipIdFromLoadoutInfo (static, 0x1416bb9c0) — the UI dispatcher
//      that sits directly above the vtable+0x180 LoadoutInfo read. For
//      slot 1 with a custom partsType live, returns entry->linkedFlowIndex.
//
//   B. IsVariantSuit (dynamic, +0x48 vtable+0x480) — returns 1 for custom
//      flowIndex so SetupEquipPanelParam takes the variant-path.
//
//   C. UiGetVariantDisplayId (dynamic, +0x68 vtable+0xf0) — redirects
//      custom flowIndex → vanilla Sneaking Suit (The Boss) flow 697 so
//      the UI resolves a valid displayId for icon/name lookup.
//
// IsEquipDeveloped is NOT hooked — custom suits are developed via the
// normal R&D system, which sets the bit naturally. No forcing needed.
// ----------------------------------------------------------------------------

namespace
{
    using GetQuarkSystemTable_t = void* (__fastcall*)();

    // GetEquipIdFromLoadoutInfo(callback, slotIndex) — returns u16 equipId
    // for the given UI slot (0 = CHARACTER, 1 = UNIFORMS, 2 = HEAD OPTION,
    // 3 = ???, 4-0xb = weapon slots, 0xc-0x13 = item slots).
    //
    // Ghidra-decomp return is undefined8 (function sets RAX only via the
    // vtable dispatch it tail-calls into), but semantically a u16 equipId.
    using GetEquipIdFromLoadoutInfo_t =
        std::uint64_t(__fastcall*)(void* self, std::uint32_t slotIndex);

    static GetEquipIdFromLoadoutInfo_t g_OrigGetEquipIdFromLoadoutInfo = nullptr;

    static GetQuarkSystemTable_t g_GetQuarkSystemTable = nullptr;

    static bool g_InstalledGetEquipId = false;

    // NOTE: hkIsEquipDeveloped hook removed 2026-04-21 per user request.
    // Custom suits are developed via the normal R&D system (EquipDevelop
    // reservation + in-game R&D menu / Lua grant), which naturally sets
    // the bit at `*(edcThis + 0x1E008) + flowIndex`. The force-true hook
    // was redundant once the user actually develops the suit — and caused
    // log spam on every per-row badge render. Pipeline is now:
    //   - Register via V_TppPlayer.AddOutfit → reserves R&D slot.
    //   - User develops via normal game mechanics.
    //   - Vanilla IsEquipDeveloped returns 1 naturally.
    // No forcing needed.

    // UI slot constants (confirmed from mgsvtpp.exe.c:2965982 switch cases).
    static constexpr std::uint32_t kSlotCharacter   = 0;
    static constexpr std::uint32_t kSlotUniforms    = 1;
    static constexpr std::uint32_t kSlotHeadOption  = 2;

    // NOTE: the dynamic vtable+0x1F8 hook we added here before resolved at
    // runtime to `0x140955C70` — the SAME function that AddressSet exposes
    // as `GetCurrentSuitFlowIndex`, already hooked by SuitVariantHook. So
    // `FUN_140955c70` IS the game's canonical "get currently-equipped suit
    // flowIndex" method (Ghidra decomp shows it as void because it can't
    // trace the return-value flow through internal vtable+0x600 dispatches).
    // `hkGetCurrentSuitFlowIndex` in SuitVariantHook.cpp owns that target.

    // ---- Dynamic vtable+0x480 hook on MissionPreparationSystemImpl ----
    //
    // `SetupEquipPanelParam` (mgsvtpp.exe.c:2969000) asks the sub-controller
    // at `(+0x48)->vtable[0x480](flowIndex)`: "is this flowIndex a variant-
    // aware suit?". Vanilla Sneaking Suit (The Boss) returns 1 → the game
    // takes the variant-path (vtable+0x488 → vtable+0xf0) which WE already
    // hook for variant name/icon rendering. For custom flow 922/923/924 the
    // vanilla table has no entry → returns 0 → falls to `vtable[0xe8]` which
    // returns 0x400 (unknown) → `panel+0x17a = 0x400` → no icon, no name.
    //
    // This hook forces return 1 for any registered custom flowIndex so the
    // game takes the variant-path. Our existing `hkIsNamedVariant` then
    // returns 1, and `hkGetVariantDisplayId` redirects to vanilla Sneaking
    // Suit's displayId — the UNIFORMS panel + list rows then resolve icon
    // and name from vanilla Sneaking Suit strings (first-pass: shows vanilla
    // Sneaking Suit name/icon on custom suit rows; custom-string tables are
    // a separate future improvement).
    //
    // Lazy-installed on first hkGetEquipIdFromLoadoutInfo call (we get a
    // live MissionPreparationCallbackImpl* there), SEH-guarded.
    using IsVariantSuit_t =
        std::uint8_t(__fastcall*)(void* self, std::uint16_t flowIndex);
    static IsVariantSuit_t g_OrigIsVariantSuit      = nullptr;
    static void*           g_IsVariantSuitTarget    = nullptr;
    static bool            g_IsVariantSuitAttempted = false;
    static bool            g_IsVariantSuitInstalled = false;

    // ---- Dynamic vtable+0xf0 hook on the UI-side displayId getter ----
    //
    // The existing `hkGetVariantDisplayId` (SuitVariantHook.cpp) is installed
    // on the EquipParameterTables facade at `(q98+0x40)+0x48 -> vtable[0xf0]`
    // (address 0x1408E5D10). That hook fires for AddListSuit variant-row
    // rendering, but NOT for `SetupEquipPanelParam`'s variant-path, because
    // SetupEquipPanelParam calls `(self+0x68)->vtable[0xf0]` where `self+0x68`
    // is a different object with a different vtable+0xf0 function address.
    //
    // Log evidence 2026-04-20:
    //   [IsVariantSuit] flow=922 -> force 1   (variant-path taken)
    //   [SetupEquipPanel] slot=1 equipId=922 displayId=0x0400   (0x400 = unknown)
    //   NO `[GetVariantDisplayId]` entry     (our facade hook never fired)
    //
    // Fix: dynamically resolve `(self+0x68)->vtable[0xf0]` from the
    // MissionPreparationCallbackImpl* that hkGetEquipIdFromLoadoutInfo
    // receives, and install a hook there too. The hook body redirects
    // custom flowIndex → vanilla Sneaking Suit (The Boss) flow 697 so the
    // game's downstream string/icon lookups resolve naturally.
    using UiGetVariantDisplayId_t =
        std::uint16_t(__fastcall*)(
            void* self, std::uint16_t flowIndex, std::uint32_t subIndex);
    static UiGetVariantDisplayId_t g_OrigUiGetVariantDisplayId      = nullptr;
    static void*                   g_UiGetVariantDisplayIdTarget    = nullptr;
    static bool                    g_UiGetVariantDisplayIdAttempted = false;
    static bool                    g_UiGetVariantDisplayIdInstalled = false;

    static constexpr std::uint16_t kVanillaSneakingSuitFlow = 697;

    static bool ResolveApis()
    {
        if (!g_GetQuarkSystemTable)
        {
            g_GetQuarkSystemTable = reinterpret_cast<GetQuarkSystemTable_t>(
                ResolveGameAddress(gAddr.GetQuarkSystemTable));
        }
        return g_GetQuarkSystemTable != nullptr;
    }

    // Read the live Quark state[0xF8] partsType. Returns 0xFF on failure.
    static std::uint8_t ReadLivePartsType()
    {
        if (!ResolveApis()) return 0xFF;

        auto* qt = reinterpret_cast<std::uint8_t*>(g_GetQuarkSystemTable());
        if (!qt) return 0xFF;
        auto* q98 = *reinterpret_cast<std::uint8_t**>(qt + 0x98);
        if (!q98) return 0xFF;
        auto* state = *reinterpret_cast<std::uint8_t**>(q98 + 0x10);
        if (!state) return 0xFF;

        return state[0xF8];
    }
}

// Hook body for vtable+0xf0 on MissionPreparationCallbackImpl+0x68 — UI-side
// displayId resolver for variant suits. Redirects custom flowIndex to vanilla
// Sneaking Suit (The Boss) **Standard** (sub=7) unconditionally.
//
// Why always sub=7 (Standard) rather than remapping the game's sub arg:
// For SetupEquipPanelParam's CHARACTER-summary rendering, the game derives
// its sub arg from `(+0xa0)->vtable+0x1b8` or `vtable+0x1e0(slot-0xc)` —
// both return semi-arbitrary values for the UNIFORMS slot (slot-0xc is -0xb,
// out of range, yields garbage). Passing that garbage-sub to our redirect
// lands on a variant like NAKED (sub=0) whose **icon** resolves fine but
// whose **NAME** is empty because vanilla NAKED inherits its name from the
// Standard base via a separate variant-label system we don't wire into this
// path. Always using Standard (sub=7) avoids that: Standard has its own full
// stringId, so vtable+0x770 returns a proper name and text renders.
//
// For AddListSuit list-row rendering the different subIndexes (one per
// variant) are still handled by the facade's vtable+0xf0 via `hkGetVariant-
// DisplayId` in SuitVariantHook.cpp; that hook keeps the per-variant
// remapping {0→7, 1→0, 2+→1+} because the list iterates variant subs.
static constexpr std::uint32_t kVanillaSneakingSuitStandardSub = 7;

static std::uint16_t __fastcall hkUiGetVariantDisplayId(
    void* self, std::uint16_t flowIndex, std::uint32_t subIndex)
{
    if (!g_OrigUiGetVariantDisplayId)
        return 0;

    const CustomSuitEntry* entry = nullptr;
    if (TryGetCustomSuitByFlowIndex(flowIndex, &entry) && entry)
    {
        const std::uint16_t redirected =
            g_OrigUiGetVariantDisplayId(
                self,
                kVanillaSneakingSuitFlow,
                kVanillaSneakingSuitStandardSub);

        static std::uint32_t s_lastKey = 0xFFFFFFFFu;
        const std::uint32_t key =
            (static_cast<std::uint32_t>(flowIndex) << 16) | (subIndex & 0xFFFF);
        if (key != s_lastKey)
        {
            s_lastKey = key;
            Log("[UiGetVariantDisplayId] redirect custom flow=%u sub=%u -> "
                "vanilla flow=%u sub=%u (Standard) -> displayId=0x%04X\n",
                static_cast<unsigned>(flowIndex),
                static_cast<unsigned>(subIndex),
                static_cast<unsigned>(kVanillaSneakingSuitFlow),
                static_cast<unsigned>(kVanillaSneakingSuitStandardSub),
                static_cast<unsigned>(redirected));
        }
        return redirected;
    }

    return g_OrigUiGetVariantDisplayId(self, flowIndex, subIndex);
}

// Lazy vtable-hook install for (self+0x68)->vtable[0xf0].
static void TryInstallUiGetVariantDisplayId(void* missionPrepCallbackImpl)
{
    if (g_UiGetVariantDisplayIdAttempted) return;
    if (!missionPrepCallbackImpl) return;

    g_UiGetVariantDisplayIdAttempted = true;

    __try
    {
        auto* subObj = *reinterpret_cast<std::uint8_t**>(
            reinterpret_cast<std::uint8_t*>(missionPrepCallbackImpl) + 0x68);
        if (!subObj)
        {
            Log("[UiGetVariantDisplayId] self+0x68 null — skip install\n");
            return;
        }

        auto** vtable = *reinterpret_cast<void***>(subObj);
        if (!vtable)
        {
            Log("[UiGetVariantDisplayId] subObj vtable null — skip install\n");
            return;
        }

        g_UiGetVariantDisplayIdTarget = vtable[0xf0 / sizeof(void*)];
        if (!g_UiGetVariantDisplayIdTarget)
        {
            Log("[UiGetVariantDisplayId] vtable[0xf0] null — skip install\n");
            return;
        }

        Log("[UiGetVariantDisplayId] chain callbackImpl=%p subObj=%p vtable=%p target=%p\n",
            missionPrepCallbackImpl, subObj, static_cast<void*>(vtable),
            g_UiGetVariantDisplayIdTarget);

        const bool ok = CreateAndEnableHook(
            g_UiGetVariantDisplayIdTarget,
            reinterpret_cast<void*>(&hkUiGetVariantDisplayId),
            reinterpret_cast<void**>(&g_OrigUiGetVariantDisplayId));
        Log("[UiGetVariantDisplayId] install @ %p: %s\n",
            g_UiGetVariantDisplayIdTarget, ok ? "OK" : "FAIL");

        g_UiGetVariantDisplayIdInstalled = ok;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Log("[UiGetVariantDisplayId] exception walking vtable — skip install\n");
    }
}

// Hook body for vtable+0x480 on MissionPreparationSystemImpl — "IsVariantSuit".
// Force 1 for registered custom flowIndex so the game takes the variant-path
// and our existing IsNamedVariant + GetVariantDisplayId hooks populate
// displayId from vanilla Sneaking Suit.
static std::uint8_t __fastcall hkIsVariantSuit(void* self, std::uint16_t flowIndex)
{
    const CustomSuitEntry* entry = nullptr;
    if (TryGetCustomSuitByFlowIndex(flowIndex, &entry) && entry)
    {
        static std::uint16_t s_lastLogged = 0xFFFF;
        if (s_lastLogged != flowIndex)
        {
            s_lastLogged = flowIndex;
            Log("[IsVariantSuit] flow=%u (custom partsType=0x%02X) -> force 1\n",
                static_cast<unsigned>(flowIndex),
                static_cast<unsigned>(entry->customPartsType));
        }
        return 1;
    }

    return g_OrigIsVariantSuit(self, flowIndex);
}

// Lazy vtable-hook install. `missionPrepCallbackImpl` is the `self` pointer
// received by hkGetEquipIdFromLoadoutInfo (MissionPreparationCallbackImpl*).
// Walks self+0x48 → vtable → [0x480/8] and MinHooks the resolved function.
// One-shot: g_IsVariantSuitAttempted prevents re-entry regardless of outcome.
static void TryInstallIsVariantSuit(void* missionPrepCallbackImpl)
{
    if (g_IsVariantSuitAttempted) return;
    if (!missionPrepCallbackImpl) return;

    g_IsVariantSuitAttempted = true;

    __try
    {
        auto* subObj = *reinterpret_cast<std::uint8_t**>(
            reinterpret_cast<std::uint8_t*>(missionPrepCallbackImpl) + 0x48);
        if (!subObj)
        {
            Log("[IsVariantSuit] self+0x48 null — skip install\n");
            return;
        }

        auto** vtable = *reinterpret_cast<void***>(subObj);
        if (!vtable)
        {
            Log("[IsVariantSuit] subObj vtable null — skip install\n");
            return;
        }

        g_IsVariantSuitTarget = vtable[0x480 / sizeof(void*)];
        if (!g_IsVariantSuitTarget)
        {
            Log("[IsVariantSuit] vtable[0x480] null — skip install\n");
            return;
        }

        Log("[IsVariantSuit] chain callbackImpl=%p subObj=%p vtable=%p target=%p\n",
            missionPrepCallbackImpl, subObj, static_cast<void*>(vtable),
            g_IsVariantSuitTarget);

        const bool ok = CreateAndEnableHook(
            g_IsVariantSuitTarget,
            reinterpret_cast<void*>(&hkIsVariantSuit),
            reinterpret_cast<void**>(&g_OrigIsVariantSuit));
        Log("[IsVariantSuit] install @ %p: %s\n",
            g_IsVariantSuitTarget, ok ? "OK" : "FAIL");

        g_IsVariantSuitInstalled = ok;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Log("[IsVariantSuit] exception walking vtable — skip install\n");
    }
}

// ----------------------------------------------------------------------------
// Hook A — GetEquipIdFromLoadoutInfo
//
// The UI's canonical "what is in slot N?" dispatcher. For the UNIFORMS slot
// (1), when the live playerPartsType is a registered custom suit, return the
// custom linkedFlowIndex. Otherwise passthrough.
//
// Why `slot == 1` specifically: slots 0 (CHARACTER) and 2 (HEAD OPTION) read
// different vtable paths and have their own semantics; we only want to
// intercept the UNIFORMS slot. Other UI queries (CHARACTER slot dispatches
// to a different vtable, weapon slots 4-0xb are entirely separate) pass
// through unchanged.
//
// Return value: u16 equipId zero-extended to u64. The original function
// returns via vtable dispatch which only writes RAX low bits; we match that
// by returning the u16 cast to u64.
// ----------------------------------------------------------------------------
static std::uint64_t __fastcall hkGetEquipIdFromLoadoutInfo(
    void* self, std::uint32_t slotIndex)
{
    // Lazy-install the vtable+0x480 (IsVariantSuit) hook on first call.
    // `self` is MissionPreparationCallbackImpl*, and `self+0x48` is the
    // sub-controller whose vtable+0x480 is the "is this flowIndex a variant
    // suit?" predicate consulted by SetupEquipPanelParam. Force-true'ing it
    // for custom flow routes displayId resolution through our existing
    // variant-path hooks (IsNamedVariant + GetVariantDisplayId).
    TryInstallIsVariantSuit(self);

    // Lazy-install the UI-side vtable+0xf0 (displayId getter) hook on
    // self+0x68. This is the function SetupEquipPanelParam actually calls
    // during the variant-path — DIFFERENT from the facade's vtable+0xf0
    // that SuitVariantHook's GetVariantDisplayId hook sits on.
    TryInstallUiGetVariantDisplayId(self);

    const std::uint64_t orig = g_OrigGetEquipIdFromLoadoutInfo(self, slotIndex);

    if (slotIndex != kSlotUniforms)
        return orig;

    const std::uint8_t livePartsType = ReadLivePartsType();
    if (livePartsType < 0x40 || livePartsType > 0x7F)
        return orig;

    const CustomSuitEntry* entry = nullptr;
    if (!TryGetCustomSuitByPartsType(livePartsType, &entry) || !entry)
        return orig;
    if (entry->linkedFlowIndex == 0 || entry->linkedFlowIndex == 0xFFFF)
        return orig;

    // Log once per (partsType, orig) transition so the log stays clean.
    {
        static std::uint8_t  s_lastPartsType = 0xFF;
        static std::uint64_t s_lastOrig      = 0xFFFFFFFFFFFFFFFFull;
        if (s_lastPartsType != livePartsType || s_lastOrig != orig)
        {
            s_lastPartsType = livePartsType;
            s_lastOrig      = orig;
            Log("[GetEquipIdFromLoadoutInfo] slot=1 partsType=0x%02X "
                "vanilla-ret=0x%llX -> custom flowIndex=%u\n",
                static_cast<unsigned>(livePartsType),
                static_cast<unsigned long long>(orig),
                static_cast<unsigned>(entry->linkedFlowIndex));
        }
    }

    return static_cast<std::uint64_t>(entry->linkedFlowIndex);
}

bool Install_CurrentSuitQuery_Hook()
{
    if (!ResolveApis())
    {
        Log("[Hook] CurrentSuitQuery: failed to resolve Quark API\n");
        return false;
    }

    if (!g_InstalledGetEquipId && gAddr.GetEquipIdFromLoadoutInfo != 0)
    {
        void* target = ResolveGameAddress(gAddr.GetEquipIdFromLoadoutInfo);
        if (!target)
        {
            Log("[Hook] CurrentSuitQuery: failed to resolve "
                "GetEquipIdFromLoadoutInfo\n");
        }
        else
        {
            const bool ok = CreateAndEnableHook(
                target,
                reinterpret_cast<void*>(&hkGetEquipIdFromLoadoutInfo),
                reinterpret_cast<void**>(&g_OrigGetEquipIdFromLoadoutInfo));
            Log("[Hook] CurrentSuitQuery GetEquipIdFromLoadoutInfo: %s @ %p\n",
                ok ? "OK" : "FAIL", target);
            g_InstalledGetEquipId = ok;
        }
    }

    // IsEquipDeveloped hook intentionally NOT installed — custom suits are
    // developed via the normal R&D pipeline, and vanilla IsEquipDeveloped
    // returns 1 naturally once the bit is set. No forcing needed.

    return g_InstalledGetEquipId;
}

bool Uninstall_CurrentSuitQuery_Hook()
{
    if (g_InstalledGetEquipId)
    {
        if (void* target = ResolveGameAddress(gAddr.GetEquipIdFromLoadoutInfo))
            DisableAndRemoveHook(target);

        g_OrigGetEquipIdFromLoadoutInfo = nullptr;
        g_InstalledGetEquipId = false;
    }

    if (g_IsVariantSuitInstalled && g_IsVariantSuitTarget)
    {
        DisableAndRemoveHook(g_IsVariantSuitTarget);
        g_OrigIsVariantSuit      = nullptr;
        g_IsVariantSuitTarget    = nullptr;
        g_IsVariantSuitInstalled = false;
    }
    g_IsVariantSuitAttempted = false;

    if (g_UiGetVariantDisplayIdInstalled && g_UiGetVariantDisplayIdTarget)
    {
        DisableAndRemoveHook(g_UiGetVariantDisplayIdTarget);
        g_OrigUiGetVariantDisplayId      = nullptr;
        g_UiGetVariantDisplayIdTarget    = nullptr;
        g_UiGetVariantDisplayIdInstalled = false;
    }
    g_UiGetVariantDisplayIdAttempted = false;

    g_GetQuarkSystemTable = nullptr;

    Log("[Hook] CurrentSuitQuery: removed\n");
    return true;
}
