#include "pch.h"

#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "CustomSuitRegistry.h"
#include "SuitVariantHook.h"
#include "MissionPrepPlayerPartsRequestHook.h"

namespace
{
    using GetQuarkSystemTable_t = void* (__fastcall*)();
    using AddListSuit_t = void(__fastcall*)(
        void* self, std::uint32_t* rowCounter, std::uint16_t suitId, void* param4);
    using IsEnableCurrentSuit_t = bool(__fastcall*)(void* self);
    using IsEnableCurrentHeadOption_t = bool(__fastcall*)(void* self);
    using FetchCurrentHeadOptionKey_t = void(__fastcall*)(void* self);
    using FindHeadOptionRow_t = std::uint16_t(__fastcall*)(
        void* self, std::uint8_t group, std::uint8_t subkey1, std::uint8_t subkey2);
    using SetupEquipPanelParam_t = void(__fastcall*)(
        void* self, void* panelData, std::uint32_t slotIndex);

    // ---- Parked typedefs (HEAD OPTION cycling) ----
    using GetSuitVariation_t = std::uint8_t(__fastcall*)(void* self, std::uint16_t suitId);
    using HeadOptionTableLookup_t = std::uint64_t(__fastcall*)(
        void* self, std::int16_t* outIndex, std::uint64_t equipKey);
    using SetupCharacterSlotSelect_t = void(__fastcall*)(void* self);
    using GetSelectionNum_t = char(__fastcall*)(void* self);

    // HasHeadOptions (FUN_1460b9fa0, vtable+0x460 on sub-controller via
    // thunk LAB_1409575d0). Signature from decomp:
    //   undefined1 FUN_1460b9fa0(longlong *sub_ctrl, undefined2 flowIndex)
    // Returns 0/1 — used by GetSelectionNum's `+0x460` dispatch to decide
    // whether to return 2 (no HEAD OPTION) or 3 (HEAD OPTION row visible).
    using HasHeadOptions_t = std::uint8_t(__fastcall*)(
        void* subController, std::uint16_t flowIndex);

    // HeadOptionIndexGetter (FUN_1460b4300 at 0x1460b4300).
    // Called from GetCurrentItems (decomp line 2965913) as
    // `(this+0xa0).vtable+0x1d8(index)` — 8 times with index 0..7 — to
    // fill the 8 HEAD OPTION entries in the sortie CharaSlotSelect
    // dialog's item buffer.
    //
    // The vanilla function IGNORES its arguments and returns state[0x138]
    // (a u32 truncated to u16 when the caller stores it). So all 8 HEAD
    // OPTION slots get the same value. For custom suits state[0x138] is
    // typically 0xD000 which isn't a valid head-option equipId — the UI
    // shows empty slots.
    using HeadOptionIndexGetter_t = std::uint32_t(__fastcall*)(
        void* self, std::uint8_t index);

    // ---- SetItemDetail typedef ----
    // Decompiled signature (from mgsvtpp.exe.c line 2967711):
    //   void MissionPreparationCallbackImpl::SetItemDetail(this, uint16 flowIndex)
    // Internally makes 3 SendTrigger calls via vtable+0x1e0 on this+0x38:
    //   1. (this+0x38, this+0x130, 0x8fda3dfc95ed, 0)     — mode=0
    //   2. (this+0x38, this+0x130, 0x30a0d543e155, flowIndex) — data
    //   3. (this+0x38, this+0x128, SET_KIND_HASH, 7)       — trigger
    using SetItemDetail_t = void(__fastcall*)(void* self, std::uint16_t flowIndex);

    // ---- Active state ----
    static GetQuarkSystemTable_t g_GetQuarkSystemTable = nullptr;
    static AddListSuit_t g_OrigAddListSuit = nullptr;
    static IsEnableCurrentSuit_t g_OrigIsEnableCurrentSuit = nullptr;
    static IsEnableCurrentHeadOption_t g_OrigIsEnableCurrentHeadOption = nullptr;
    static FetchCurrentHeadOptionKey_t g_OrigFetchCurrentHeadOptionKey = nullptr;
    static FindHeadOptionRow_t g_OrigFindHeadOptionRow = nullptr;
    static SetupEquipPanelParam_t g_OrigSetupEquipPanelParam = nullptr;
    static SetItemDetail_t g_OrigSetItemDetail = nullptr;
    static void* g_HookedAddListSuitAddr = nullptr;
    static void* g_HookedIsEnableCurrentSuitAddr = nullptr;
    static void* g_HookedIsEnableCurrentHeadOptionAddr = nullptr;
    static void* g_HookedFetchCurrentHeadOptionKeyAddr = nullptr;
    static void* g_HookedFindHeadOptionRowAddr = nullptr;
    static void* g_HookedSetupEquipPanelParamAddr = nullptr;
    static void* g_HookedSetItemDetailAddr = nullptr;
    static bool g_Installed = false;

    // ---- Named-variant display hooks (lazy-installed via vtable resolve) ----
    //
    // These hook two vtable-dispatched game functions that drive the sortie
    // UNIFORMS variant name rendering. Neither function has a static
    // absolute address in our AddressSet — they live on singleton vtables
    // that are only populated after the game's develop/equip table init
    // runs. We resolve them lazily on the first successful AddListSuit call
    // (which fires when the sortie UI opens and QST is guaranteed live).
    //
    // Vtable locations (verified from mgsvtpp.exe.c decomp):
    //   EquipDevelopController = *(qst + 0x110) + 0xac8          (vtable+0x488 = IsNamedVariant)
    //   EquipParameterTables   = *(qst + 0x40)  + 0x48           (vtable+0xf0  = GetVariantDisplayId)
    //
    // All pointer derefs inside TryInstallVariantVtableHooks run under
    // __try/__except so a bad chain just logs and skips install — no crash.

    using IsNamedVariant_t       = std::uint8_t(__fastcall*)(void* self);
    using GetVariantDisplayId_t  = std::uint16_t(__fastcall*)(void* self, std::uint16_t flowIndex, std::uint32_t subIndex);

    static IsNamedVariant_t       g_OrigIsNamedVariant        = nullptr;
    static GetVariantDisplayId_t  g_OrigGetVariantDisplayId   = nullptr;
    static void*                  g_IsNamedVariantTarget      = nullptr;
    static void*                  g_GetVariantDisplayIdTarget = nullptr;
    static bool                   g_VariantVtableAttempted    = false;
    static bool                   g_VariantVtableInstalled    = false;

    // Vanilla suit to redirect to for variant-name display. Sneaking Suit
    // (The Boss) flowIndex = 697 has a full (Standard=7, Naked=0) variant
    // catalog the UI knows how to render. Our sub-slots then display
    // vanilla labels ("Standard" / "Naked") on our custom body.
    static constexpr std::uint16_t kVanillaSneakingSuitFlow = 697;

    // Last vanilla flowIndex per playerType (0=Snake, 1=DD?, 2=Female, 3=?).
    //
    // Seeded with 698 — empirically verified from 2026-04-20 log evidence
    // to trigger the game's internal `HasHeadOptions` check returning true
    // on pt=2 (Female). Log showed:
    //   fresh load with seed 521 → HasHeadOptions(521) false → orig=2 (row hidden
    //     without our forced override; entries empty even WITH override)
    //   after user cycled through fox (flow 698) → HasHeadOptions(698) true →
    //     orig=3 → entries populate
    // Previous notes claimed 521 was "empirically confirmed" to work; that
    // was wrong — likely based on a test with already-populated state that
    // didn't actually exercise the HasHeadOptions check.
    //
    // With `hkHasHeadOptions` installed we no longer depend on this cache
    // hitting a specific magic value — the hook forces true for custom
    // enableHead=true suits regardless of flow. The seed still matters for
    // a belt-and-suspenders remap in hkGetCurrentSuitFlowIndex (other call
    // sites that read the current flowIndex, e.g., for EQP-badge matching).
    //
    // Updated at runtime by hkGetCurrentSuitFlowIndex whenever a non-custom
    // result is seen — so if the user equips a different vanilla suit, the
    // cache tracks that and remapping uses the new value.
    static std::uint16_t s_vanillaFlowIndexByPt[4] = { 698, 698, 698, 698 };

    // Cached self and panel pointers.
    static void* s_cachedSelf = nullptr;
    static void* s_suitPanels[3] = {};

    // Thread-local flag set while GetSelectionNum is executing. Used by
    // hkGetCurrentSuitFlowIndex to return the vanilla remap target (521)
    // instead of the custom linkedFlowIndex (922) so GetSelectionNum's
    // HasHeadOptions check queries a vanilla flowIndex that carries a
    // head-option catalog. Outside GetSelectionNum (e.g. EQP badge paths)
    // the hook still returns linkedFlowIndex so the badge matches the
    // row's stored flowIndex.
    static thread_local bool g_InGetSelectionNum = false;

    // Set true when SetupEquipPanelParam fires (sortie context only).
    // Used by hkAddListSuit to prevent adding custom suits in the supply
    // drop selector, where FUN_1416a7610's table[flowIndex*0x68] access
    // goes out of bounds for custom flowIndices → infinite loading.
    static bool s_sortieContextActive = false;

    // ---- isDeveloped check hook (vtable+0x188 on loadout controller) ----
    //
    // PARKED: hook is still installed for logging but no longer forces true.
    // See the "NEXT STEPS" block further below for the concrete path forward.
    //
    // Previously hkIsDeveloped returned true for suit slots (0-2) when called
    // from inside hkSetupEquipPanelParam, so custom suits would display their
    // icon/name in the UNIFORMS list. But forcing true pushed the game's
    // original SetupEquipPanelParam down the "fully populate detail" code path.
    //
    // Verified call chain (mgsvtpp.exe.c):
    //   SetupEquipPanelParam (line 2968954)
    //     → vtable+0x188 on (self+0xa0)   [isDeveloped check]
    //     → detail-populate branch taken when isDeveloped=true
    //     → EquipSystemImpl::GetGunInfoOutOfMission (line 6594334) — thin wrapper:
    //           memset(outInfo, 0, 0x90);
    //           SetUpGunInfoFromGunPartsDesc(this-0x20, partsDesc, equipId, outInfo, ...);
    //     → EquipSystemImpl::SetUpGunInfoFromGunPartsDesc (line 1779829)
    //         → null deref at RVA 0x140dc36b5 when:
    //             - path A: partsDesc (local_38 in the wrapper FUN_14742e040) is null
    //                       because the game has no GunPartsDesc row for the custom equipId
    //             - path B: partsDesc is non-null but RDI+0x10 (its internal sub-table)
    //                       points to invalid memory for a custom equipId
    //
    // Runtime-bypass attempts (all failed — do NOT re-try without data-layer fix):
    //   1) Unconditional short-circuit of SetUpGunInfoFromGunPartsDesc for custom
    //      equipIds. Broke WeaponSys::MaybeInitWeapon (another caller that REQUIRES
    //      the real function to populate its out-pointers).
    //   2) Return-address-scoped bypass of SetUpGunInfoFromGunPartsDesc (only skip
    //      when called from sortie prep frames). Path B still crashed because the
    //      *partsDesc != 0 pointer is dereferenced before any return-address check
    //      could help.
    //   3) Forcing panel grade element (panel+0x100) visibility via vtable+0x2a8.
    //      Triggered a similar detail-refresh cascade that re-entered the crash.
    //
    // NEXT STEPS (data-layer approach, requires game testing):
    //   A) Add a new helper in EquipParameters that, for each custom suit equipId,
    //      inserts a minimal GunPartsDesc row before the first sortie UI open.
    //      Concretely: during EquipParameterTablesImpl::ReloadEquipParameterTablesImpl2
    //      (already hooked for gun-basic reload), extend to also write a stub row
    //      into the GunPartsDesc table with:
    //        - field 0 (first byte) != 0  — passes the "has desc" guard
    //        - RDI+0x10 sub-table pointer = pointer to a small zero-filled block
    //          that gun-info reads safely (weaponId=0, receiverId=-1, everything
    //          default). Write these once at reload; they persist for the session.
    //   B) Once (A) is in place, re-enable hkIsDeveloped to force true for suit
    //      slots when a custom suit is active, scoped via s_inSetupEquipPanel.
    //      Verify: sortie prep pass 3 completes without touching 0x140dc36b5.
    //   C) Separately, for the grade element at panel+0x100: the game's default
    //      path HIDES it for undeveloped suits. Force-showing requires the detail
    //      populate from (A) to also set a non-zero grade byte in the panel's
    //      extra-info region (+0x17d..+0x18a) so the switch-case in
    //      SetupEquipPanelParam (line ~2969068) picks a stringId instead of
    //      defaulting to LAB_1416c0af9 (the hide-branch).
    //
    // Until (A) lands, custom suits show as blank "undeveloped" panels during
    // sortie prep. This is the current compromise and matches the status note
    // "Sortie UNIFORMS display — on hold, needs GunPartsDesc data-layer fix".
    using IsDeveloped_t = bool(__fastcall*)(void* self, std::uint8_t slotIndex);
    static IsDeveloped_t g_OrigIsDeveloped = nullptr;
    static void* g_HookedIsDevelopedAddr = nullptr;

    // ---- HEAD OPTION cycling state ----
    static GetSuitVariation_t g_OrigGetSuitVariation = nullptr;
    static HeadOptionTableLookup_t g_OrigHeadOptionTableLookup = nullptr;
    static SetupCharacterSlotSelect_t g_OrigSetupCharSlotSelect = nullptr;
    static GetSelectionNum_t g_OrigGetSelectionNum = nullptr;
    static HasHeadOptions_t  g_OrigHasHeadOptions = nullptr;
    static HeadOptionIndexGetter_t g_OrigHeadOptionIndexGetter = nullptr;

    static bool ResolveApis()
    {
        if (!g_GetQuarkSystemTable)
        {
            g_GetQuarkSystemTable = reinterpret_cast<GetQuarkSystemTable_t>(
                ResolveGameAddress(gAddr.GetQuarkSystemTable));
        }
        return g_GetQuarkSystemTable != nullptr;
    }

    // Hook for GetCurrentSuitFlowIndex (FUN_140955c70, vtable+0x1F8 on sysObj+0x48).
    // This function returns the equipped suit's flowIndex. For custom suits it
    // returns 0x400 (blank sentinel), causing blank UNIFORMS panel and no EQP badge.
    // The fix: after the original returns, if the result is 0x400 and a custom suit
    // is active in Quark state, return the custom suit's linkedFlowIndex instead.
    using GetCurrentSuitFlowIndex_t = std::uint16_t(__fastcall*)(void* self);
    static GetCurrentSuitFlowIndex_t g_OrigGetCurrentSuitFlowIndex = nullptr;
    static void* g_HookedGetCurrentSuitFlowIndexAddr = nullptr;

    // Helper: read current playerType from Quark state[0xFB].
    static std::uint8_t ReadCurrentPlayerType()
    {
        if (!ResolveApis()) return 0xFF;
        auto* qt = reinterpret_cast<std::uint8_t*>(g_GetQuarkSystemTable());
        if (!qt) return 0xFF;
        auto* q98 = *reinterpret_cast<std::uint8_t**>(qt + 0x98);
        if (!q98) return 0xFF;
        auto* state = *reinterpret_cast<std::uint8_t**>(q98 + 0x10);
        if (!state) return 0xFF;
        return state[0xFB];
    }

    // Hook for vtable+0x188 (isDeveloped check on loadout controller).
    // Returns true for suit slots (0-2) when a custom suit is active,
    // so SetupEquipPanelParam runs ALL its display setup (icon, text, visibility).
    // Guard flag: only override isDeveloped when called from SetupEquipPanelParam.
    // vtable+0x188 is called from many game contexts — forcing true everywhere crashes.
    static bool s_inSetupEquipPanel = false;

    // PARKED force — see the IsDeveloped_t comment block above for the full
    // rationale. Currently a passthrough so custom suits show as undeveloped
    // during sortie prep (blank panels, no crash). Leave the hook installed
    // in case we want to re-enable later with data-layer fixes.
    static bool __fastcall hkIsDeveloped(void* self, std::uint8_t slotIndex)
    {
        return g_OrigIsDeveloped(self, slotIndex);
    }

    // hkGetCurrentSuitFlowIndex — canonical vtable+0x1F8 interceptor.
    //
    // Confirmed 2026-04-20 by runtime vtable walk (see CurrentSuitQueryHook.cpp
    // comment): FUN_140955c70 at 0x140955C70 IS the function at vtable[0x1F8]
    // on the MissionPreparationSystemImpl that sits at
    // MissionPreparationCallbackImpl+0x48. The Ghidra decomp shows it as void
    // only because the decompiler can't trace where RAX gets written — the
    // function's final call dispatches through EDC->vtable+0x600 which writes
    // the flowIndex into RAX, and the compiler leaves that as the return.
    //
    // This is the game's single authoritative "get the flowIndex of the
    // currently-equipped suit" getter. Every downstream consumer reads it:
    //   - UNIFORMS-list EQP badge per-row comparison (row.flowIndex == this())
    //   - `IsEnableCurrentSuit` (mgsvtpp.exe.c:2966968) first call
    //   - Any detail-populate flow that asks "what's worn?"
    //
    // The body below:
    //   (a) snapshots vanilla flowIndex per playerType into s_vanillaFlowIndexByPt
    //       for hkSetItemDetail's HEAD-OPTION-catalog remap;
    //   (b) for custom partsType (state[0xF8] in 0x40..0x7F), substitutes
    //       `entry->linkedFlowIndex` when the vanilla result is 0x400 or 0.
    //
    // Forcing the custom linkedFlowIndex here isn't a band-aid — it IS the
    // vanilla-correct write-site for "the game's current suit flowIndex".
    // Everything downstream (EQP badge, IsEnableCurrentSuit, detail populate)
    // reads this one value naturally.
    static std::uint16_t __fastcall hkGetCurrentSuitFlowIndex(void* self)
    {
        const std::uint16_t result = g_OrigGetCurrentSuitFlowIndex(self);

        // (a) Snapshot vanilla flowIndex for HEAD-OPTION SetItemDetail remap.
        //     Custom flowIndexes are filtered out so the cache stays clean.
        if (result != 0x400 && result != 0)
        {
            const CustomSuitEntry* customByFlow = nullptr;
            const bool isCustomFlow =
                TryGetCustomSuitByFlowIndex(result, &customByFlow) && customByFlow;

            if (!isCustomFlow)
            {
                const std::uint8_t pt = ReadCurrentPlayerType();
                if (pt < 4 && s_vanillaFlowIndexByPt[pt] != result)
                {
                    Log("[GetCurrentSuitFlowIndex] cached vanilla flow=%u for pt=%u\n",
                        static_cast<unsigned>(result),
                        static_cast<unsigned>(pt));
                    s_vanillaFlowIndexByPt[pt] = result;
                }
            }
        }

        // (b) For custom partsType, substitute our linkedFlowIndex so the
        //     game's own "what's equipped?" answer is correct.
        if ((result == 0x400 || result == 0) && ResolveApis())
        {
            auto* qt = reinterpret_cast<std::uint8_t*>(g_GetQuarkSystemTable());
            if (qt)
            {
                auto* q98 = *reinterpret_cast<std::uint8_t**>(qt + 0x98);
                if (q98)
                {
                    auto* state = *reinterpret_cast<std::uint8_t**>(q98 + 0x10);
                    if (state)
                    {
                        const std::uint8_t livePartsType = state[0xF8];
                        const CustomSuitEntry* entry = nullptr;
                        if (TryGetCustomSuitByPartsType(livePartsType, &entry) &&
                            entry && entry->linkedFlowIndex != 0 &&
                            entry->linkedFlowIndex != 0xFFFF)
                        {
                            static std::uint8_t s_lastLoggedPt = 0xFF;
                            if (s_lastLoggedPt != livePartsType)
                            {
                                s_lastLoggedPt = livePartsType;
                                Log("[GetCurrentSuitFlowIndex] partsType=0x%02X "
                                    "orig=0x%04X -> custom flow=%u\n",
                                    static_cast<unsigned>(livePartsType),
                                    static_cast<unsigned>(result),
                                    static_cast<unsigned>(entry->linkedFlowIndex));
                            }
                            return entry->linkedFlowIndex;
                        }
                    }
                }
            }
        }

        return result;
    }

    // Hook for IsEnableCurrentSuit — returns true when a custom suit is
    // equipped so the EQP badge and suit panel info display correctly.
    // The original checks vtable+0x478(flowIndex) which fails for custom suits.
    static bool __fastcall hkIsEnableCurrentSuit(void* self)
    {
        // Check if live Quark state has a custom suit for the current playerType
        if (ResolveApis())
        {
            auto* qt = reinterpret_cast<std::uint8_t*>(g_GetQuarkSystemTable());
            if (qt)
            {
                auto* q98 = *reinterpret_cast<std::uint8_t**>(qt + 0x98);
                if (q98)
                {
                    auto* state = *reinterpret_cast<std::uint8_t**>(q98 + 0x10);
                    if (state)
                    {
                        const std::uint8_t livePartsType = state[0xF8];
                        const CustomSuitEntry* entry = nullptr;
                        if (TryGetCustomSuitByPartsType(livePartsType, &entry) && entry)
                            return true;
                    }
                }
            }
        }

        return g_OrigIsEnableCurrentSuit(self);
    }

    // Hook for IsEnableCurrentHeadOption (FUN_14a56ba20).
    // The UI queries this to decide whether to render the HEAD OPTION row /
    // badge for the currently-equipped suit. For vanilla suits the original
    // check passes when the suit has head options in its catalog (balaclava,
    // bandana, etc.). For custom suits the original usually fails (no catalog
    // entry) — which hides HEAD OPTION even when we want it visible — OR
    // inherits head options from whatever vanilla flow we remapped to (via
    // SetItemDetail), which shows HEAD OPTION even when we don't want it.
    //
    // User requirement: HEAD OPTION visibility follows the Lua suit's
    // `enableHead` flag strictly:
    //   enableHead = true  → HEAD OPTION visible
    //   enableHead = false → HEAD OPTION hidden
    //
    // Since the registry's `IsFaceEnabled()` returns true iff `faceFpk` is
    // not disabled (which is exactly what `enableHead` controls in the Lua
    // API), we force this hook's return to match `IsFaceEnabled()` for
    // custom suits. Non-custom suits pass through to the vanilla check.
    static bool __fastcall hkIsEnableCurrentHeadOption(void* self)
    {
        if (ResolveApis())
        {
            auto* qt = reinterpret_cast<std::uint8_t*>(g_GetQuarkSystemTable());
            if (qt)
            {
                auto* q98 = *reinterpret_cast<std::uint8_t**>(qt + 0x98);
                if (q98)
                {
                    auto* state = *reinterpret_cast<std::uint8_t**>(q98 + 0x10);
                    if (state)
                    {
                        const std::uint8_t livePartsType = state[0xF8];
                        const CustomSuitEntry* entry = nullptr;
                        if (TryGetCustomSuitByPartsType(livePartsType, &entry) && entry)
                        {
                            const bool want = entry->IsFaceEnabled();

                            // Throttled log: fires every frame in UpdateLoadMark
                            // context, same pattern as other throttled hooks.
                            static std::uint32_t s_lastKey = 0xFFFFFFFFu;
                            const std::uint32_t key =
                                (static_cast<std::uint32_t>(livePartsType) << 8) |
                                static_cast<std::uint32_t>(want ? 1 : 0);
                            if (key != s_lastKey)
                            {
                                s_lastKey = key;
                                Log("[IsEnableCurrentHeadOption] custom partsType=0x%02X enableHead=%d -> %s\n",
                                    static_cast<unsigned>(livePartsType),
                                    want ? 1 : 0,
                                    want ? "true" : "false");
                            }

                            return want;
                        }
                    }
                }
            }
        }

        return g_OrigIsEnableCurrentHeadOption
            ? g_OrigIsEnableCurrentHeadOption(self)
            : false;
    }

    // Hook for FUN_1416041e0 — the data-layer cacher that fetches the
    // "current head option" equipId-key for the sortie-prep UI.
    //
    // Original body (from decomp):
    //   Walks a Quark chain to a sub-object, calls its vtable[+0x510],
    //   and stores the u16 result at `self+0x28` via MOV [RBX+0x28], RAX
    //   at 0x141604228. 0x400 is the no-match sentinel (returned when
    //   the suit has no entry in the vanilla head-option table).
    //
    // (A first implementation mistakenly hooked FUN_141604150, a sibling
    // function with only 6 clean prologue bytes — MinHook's 14-byte
    // relocation window splices mid-instruction and the game crashed
    // at 0x141604169. FUN_1416041e0 has a clean 14-byte prologue.)
    //
    // For custom suits, the vanilla table has no matching entry, so
    // self+0x28 ends up 0x400 → HEAD OPTION row's equipId list is empty →
    // user sees a HEAD OPTION menu with no items to pick.
    //
    // Fix: on exit, if the live suit is a custom enableHead=true suit and
    // the cached key is 0x400 (or 0), overwrite with 0x17CA (BALACLAVA).
    // The UI's downstream sibling-enumerator then finds the vanilla HEAD
    // OPTION catalog (0x17CA=BALACLAVA, 0x17CB=BANDANA, ... 0x17CE=HEADGEAR)
    // anchored at this equipId and populates the menu with those entries.
    //
    // Only touch the cache for enableHead=true — for enableHead=false the
    // row is already hidden via hkGetSelectionNum, and writing here would
    // be a no-op anyway since the row isn't shown.
    static void __fastcall hkFetchCurrentHeadOptionKey(void* self)
    {
        if (g_OrigFetchCurrentHeadOptionKey)
            g_OrigFetchCurrentHeadOptionKey(self);

        // Unconditional entry probe — fires ONCE per session regardless of
        // state so we can confirm the function is reached at all. If this
        // never logs, FUN_1416041e0 is not in the sortie-prep HEAD OPTION
        // code path and a different hook target is needed.
        static bool s_probeFired = false;
        if (!s_probeFired)
        {
            s_probeFired = true;
            std::uint16_t initialCached = 0xFFFF;
            __try
            {
                if (self)
                    initialCached = *reinterpret_cast<std::uint16_t*>(
                        reinterpret_cast<std::uint8_t*>(self) + 0x28);
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { initialCached = 0xDEAD; }

            Log("[FetchHeadOption] PROBE fired self=%p self+0x28=0x%04X\n",
                self, static_cast<unsigned>(initialCached));
        }

        if (!self) return;
        if (!ResolveApis()) return;

        auto* qt = reinterpret_cast<std::uint8_t*>(g_GetQuarkSystemTable());
        if (!qt) return;
        auto* q98 = *reinterpret_cast<std::uint8_t**>(qt + 0x98);
        if (!q98) return;
        auto* state = *reinterpret_cast<std::uint8_t**>(q98 + 0x10);
        if (!state) return;

        const std::uint8_t livePartsType = state[0xF8];

        // Loosened filter: log every time the function fires with ANY state,
        // once per (partsType, cached) key. Helps diagnose why the previous
        // filter never fired.
        auto* p = reinterpret_cast<std::uint8_t*>(self);
        std::uint16_t currentCached = 0xFFFF;
        __try {
            currentCached = *reinterpret_cast<std::uint16_t*>(p + 0x28);
        } __except (EXCEPTION_EXECUTE_HANDLER) { return; }

        {
            static std::uint32_t s_lastProbeKey = 0xFFFFFFFFu;
            const std::uint32_t probeKey =
                (static_cast<std::uint32_t>(livePartsType) << 16) |
                static_cast<std::uint32_t>(currentCached);
            if (probeKey != s_lastProbeKey)
            {
                s_lastProbeKey = probeKey;
                Log("[FetchHeadOption] trace livePartsType=0x%02X cached=0x%04X\n",
                    static_cast<unsigned>(livePartsType),
                    static_cast<unsigned>(currentCached));
            }
        }

        const CustomSuitEntry* entry = nullptr;
        if (!TryGetCustomSuitByPartsType(livePartsType, &entry) || !entry)
            return;
        if (!entry->IsFaceEnabled())
            return;

        // Only override the no-match sentinel. If the cache already has a
        // valid equipId (either vanilla or from a previous override in this
        // same session), leave it alone so the user's current selection is
        // preserved across frames.
        constexpr std::uint16_t kNoMatch = 0x400;
        constexpr std::uint16_t kBalaclava = 0x17CA;

        auto* cached = reinterpret_cast<std::uint16_t*>(p + 0x28);
        if (*cached == kNoMatch || *cached == 0x0000)
        {
            const std::uint16_t oldValue = *cached;
            *cached = kBalaclava;

            static std::uint32_t s_lastKey = 0xFFFFFFFFu;
            const std::uint32_t key =
                (static_cast<std::uint32_t>(livePartsType) << 16) |
                static_cast<std::uint32_t>(oldValue);
            if (key != s_lastKey)
            {
                s_lastKey = key;
                Log("[FetchHeadOption] custom partsType=0x%02X cached=0x%04X -> 0x%04X (BALACLAVA)\n",
                    static_cast<unsigned>(livePartsType),
                    static_cast<unsigned>(oldValue),
                    static_cast<unsigned>(kBalaclava));
            }
        }
    }

    // Hook for FUN_140f665a0 = SuitCatalog::FindHeadOptionRow(self, group,
    // subkey1, subkey2). Scans the 25-row vanilla head-option table at
    // DAT_14239a5f0. Returns the matching equipId or 0x400 on miss.
    //
    // For custom enableHead=true suits the vanilla table has no matching
    // row so every call misses → HEAD OPTION menu stays empty. We hook
    // the miss-return and substitute a vanilla head-option equipId
    // derived from `subkey1`:
    //   subkey1=0 → 0x17CA BALACLAVA
    //   subkey1=1 → 0x17CB BANDANA
    //   subkey1=2 → 0x17CC
    //   subkey1=3 → 0x17CD
    //   subkey1=4 → 0x17CE HEADGEAR
    //   subkey1>=5 → leave as 0x400 (no vanilla entry for that slot)
    //
    // The original returns equipId via vtable+0xe0 on hit vs plain-return
    // 0x400 on miss. Since we're overriding the miss return, a plain
    // return from our hook is fine — the caller treats both paths the
    // same as "got an equipId".
    static std::uint16_t __fastcall hkFindHeadOptionRow(
        void* self,
        std::uint8_t group,
        std::uint8_t subkey1,
        std::uint8_t subkey2)
    {
        const std::uint16_t origResult = g_OrigFindHeadOptionRow
            ? g_OrigFindHeadOptionRow(self, group, subkey1, subkey2)
            : 0x400;

        // Entry probe — once per session, no matter what state.
        static bool s_probeFired = false;
        if (!s_probeFired)
        {
            s_probeFired = true;
            Log("[FindHeadOptionRow] PROBE fired self=%p group=0x%02X subkey1=0x%02X subkey2=0x%02X -> 0x%04X\n",
                self,
                static_cast<unsigned>(group),
                static_cast<unsigned>(subkey1),
                static_cast<unsigned>(subkey2),
                static_cast<unsigned>(origResult));
        }

        // Only override on miss. If vanilla returned a valid equipId,
        // pass it through — the suit genuinely has that head option.
        if (origResult != 0x400)
            return origResult;

        if (!ResolveApis()) return origResult;

        auto* qt = reinterpret_cast<std::uint8_t*>(g_GetQuarkSystemTable());
        if (!qt) return origResult;
        auto* q98 = *reinterpret_cast<std::uint8_t**>(qt + 0x98);
        if (!q98) return origResult;
        auto* state = *reinterpret_cast<std::uint8_t**>(q98 + 0x10);
        if (!state) return origResult;

        const std::uint8_t livePartsType = state[0xF8];
        const CustomSuitEntry* entry = nullptr;
        if (!TryGetCustomSuitByPartsType(livePartsType, &entry) || !entry)
            return origResult;
        if (!entry->IsFaceEnabled())
            return origResult;

        // Map subkey1 → vanilla BALACLAVA-family equipId. Only 5 valid
        // entries (0x17CA..0x17CE); beyond that, leave as miss.
        if (subkey1 > 4)
            return origResult;

        const std::uint16_t injected =
            static_cast<std::uint16_t>(0x17CA + subkey1);

        // Throttled log per (partsType, subkey1) to keep noise down
        // while still showing every distinct injection.
        static std::uint32_t s_lastKey = 0xFFFFFFFFu;
        const std::uint32_t key =
            (static_cast<std::uint32_t>(livePartsType) << 16) |
            (static_cast<std::uint32_t>(group) << 8) |
            static_cast<std::uint32_t>(subkey1);
        if (key != s_lastKey)
        {
            s_lastKey = key;
            Log("[FindHeadOptionRow] inject custom partsType=0x%02X group=0x%02X sub1=0x%02X sub2=0x%02X -> 0x%04X\n",
                static_cast<unsigned>(livePartsType),
                static_cast<unsigned>(group),
                static_cast<unsigned>(subkey1),
                static_cast<unsigned>(subkey2),
                static_cast<unsigned>(injected));
        }

        return injected;
    }

    // Hook for SetupEquipPanelParam — diagnostic/logging only.
    //
    // PARKED: this hook previously (a) lazily installed a vtable+0x188
    // IsDeveloped hook that forced true for custom-suit slots, and (b) called
    // vtable+0x2a8(uixUtil, panel+0x100, 1) to force-show the grade element.
    // Both caused crashes during sortie prep pass 3 for custom-suit equipIds:
    //   - IsDeveloped=true pushed the game down the "fully populate detail"
    //     path → GetGunInfoOutOfMission → SetUpGunInfoFromGunPartsDesc
    //     → null-deref at 0x140dc36b5 (custom suits have no GunPartsDesc).
    //   - force-show triggered a similar detail-refresh cascade.
    //
    // Both are disabled. The hook stays installed for logging, and also to
    // keep s_sortieContextActive and s_cachedSelf / s_suitPanels tracking
    // which other sortie-adjacent code relies on.
    //
    // Panel layout (EquipPanelInfo at panelData):
    //   +0x178 = equipId (uint16) — the suit's flowIndex
    //   +0x17a = displayId (uint16) — resolved icon/name key
    //   +0x17c = isDeveloped (byte) — controls whether details show
    // Data-layer stub row injection for custom suits — writes minimum-viable
    // rows into EquipDevelopController's per-equipId instance table at
    // `edc_this + 0x28 + equipId*0x68`. Each row is 0x68 bytes.
    //
    // The game's HEAD OPTION subscriber registration (inside the UNIFORMS
    // sub-menu open cascade) iterates this table and calls
    // `CountRegisteredVariants(equipId)` which reads the row's
    // `registeredVariants[]` array at row-offset +0x18. For custom equipIds
    // 922/923/924, these rows are all-zero → scanners return 0 → subscribers
    // skip our suit → HEAD OPTION menu stays empty.
    //
    // By writing minimum-viable fields into these rows BEFORE the UI reads
    // them, the game's native systems treat our custom suits like any other
    // registered equipId — HEAD OPTION entries populate automatically, EQP
    // badge works correctly, and the whole UI cascade processes them
    // natively.
    //
    // Row layout (from deep decomp analysis):
    //   row = edc_this + 0x28 + equipId*0x68   (0x68 bytes)
    //   row[+0x00] u16 sharedGroupId    — non-zero sentinel; 0 = unused
    //   row[+0x02] u16 variantGroupId   — groups variant sibling equipIds
    //   row[+0x08] u16 normalizedEquipId — self-reference (what vtable+0xe0 returns)
    //   row[+0x0c] u16 typeIdAndFlags   — bit 0 = developed; upper = type
    //   row[+0x15] u8  gradeAndFlags    — top 3 bits = grade (non-zero for enabled)
    //   row[+0x18..0x2f] u16[12] registeredVariants — HEAD OPTION equipIds
    //   row[+0x36] u8  categoryCode     — UNIFORMS sub-menu category filter
    //   row[+0x37] u8  subtype          — '[' = character slot marker
    //   row[+0x39] s8  sectionIdRequired — -1 = no requirement
    //   row[+0x3a] u16 levelRequired    — 0 = no gate
    //   row[+0x3c] u16 altLevelRequired — 0 = no gate
    //   row[+0x3f] u8  developKindFallback — non-zero for "enabled head option"
    //
    // Called once per session from hkSetupEquipPanelParam (first fire).
    static bool s_stubRowsInjectedThisSession = false;

    static bool InjectCustomSuitStubRows()
    {
        if (s_stubRowsInjectedThisSession)
            return true;

        if (!ResolveApis())
            return false;

        // Walk Quark chain to get EquipDevelopController this-pointer.
        auto* qt = reinterpret_cast<std::uint8_t*>(g_GetQuarkSystemTable());
        if (!qt) return false;
        auto* q98 = *reinterpret_cast<std::uint8_t**>(qt + 0x98);
        if (!q98) return false;
        auto* q110 = *reinterpret_cast<std::uint8_t**>(q98 + 0x110);
        if (!q110) return false;
        auto* edcThis = *reinterpret_cast<std::uint8_t**>(q110 + 0xac8);
        if (!edcThis) return false;

        Log("[StubRowInject] edcThis=%p — injecting custom suit rows\n",
            edcThis);

        // Iterate registered custom suits, write their stub rows.
        std::size_t injectedCount = 0;
        __try
        {
            for (std::uint8_t partsType = 0x40; partsType <= 0x7F; partsType++)
            {
                const CustomSuitEntry* entry = nullptr;
                if (!TryGetCustomSuitByPartsType(partsType, &entry) || !entry)
                    continue;

                const std::uint16_t equipId = entry->linkedFlowIndex;
                if (equipId == 0 || equipId == 0xFFFF || equipId >= 0x400)
                {
                    Log("[StubRowInject] skip partsType=0x%02X equipId=%u "
                        "(invalid or out-of-range)\n",
                        static_cast<unsigned>(partsType),
                        static_cast<unsigned>(equipId));
                    continue;
                }

                auto* row = edcThis + 0x28 + static_cast<std::size_t>(equipId) * 0x68;

                // Snapshot pre-write state (for log diff).
                const std::uint16_t preSharedGroup =
                    *reinterpret_cast<std::uint16_t*>(row + 0x00);
                const std::uint16_t preVariant0 =
                    *reinterpret_cast<std::uint16_t*>(row + 0x18);

                // Derive a unique non-zero sharedGroupId / variantGroupId.
                // Use 0xFFF0 + partsType so it's unique per suit-family but
                // high enough not to collide with vanilla group IDs.
                const std::uint16_t sharedGroupId = 0xFFF0 | (partsType & 0x0F);
                const std::uint16_t variantGroupId = sharedGroupId;

                // Write minimum viable fields.
                *reinterpret_cast<std::uint16_t*>(row + 0x00) = sharedGroupId;
                *reinterpret_cast<std::uint16_t*>(row + 0x02) = variantGroupId;
                *reinterpret_cast<std::uint16_t*>(row + 0x08) = equipId;  // normalizedEquipId self-ref
                // typeIdAndFlags: bit 0 = developed=1, upper bits = partsType hint
                *reinterpret_cast<std::uint16_t*>(row + 0x0c) =
                    static_cast<std::uint16_t>(
                        0x0001 | (static_cast<std::uint16_t>(partsType) << 4));
                *(row + 0x15) = 0x20;  // grade=1 (top 3 bits)

                // registeredVariants[] — ONLY populate for enableHead=true.
                // BALACLAVA, BANDANA, HEADGEAR A/B/C as vanilla head options.
                if (entry->IsFaceEnabled())
                {
                    *reinterpret_cast<std::uint16_t*>(row + 0x18) = 0x17CA;
                    *reinterpret_cast<std::uint16_t*>(row + 0x1a) = 0x17CB;
                    *reinterpret_cast<std::uint16_t*>(row + 0x1c) = 0x17CC;
                    *reinterpret_cast<std::uint16_t*>(row + 0x1e) = 0x17CD;
                    *reinterpret_cast<std::uint16_t*>(row + 0x20) = 0x17CE;
                    // slots 5-11 stay zero (end of list)
                }

                *(row + 0x36) = 0x42;   // categoryCode: UNIFORMS category (tentative)
                *(row + 0x37) = '[';    // subtype: character slot marker
                *(row + 0x39) = 0xFF;   // sectionIdRequired = -1 (no req)
                *reinterpret_cast<std::uint16_t*>(row + 0x3a) = 0;  // levelRequired
                *reinterpret_cast<std::uint16_t*>(row + 0x3c) = 0;  // altLevelRequired
                *(row + 0x3f) = 0x56;   // developKindFallback: "enabled head option"

                Log("[StubRowInject] partsType=0x%02X equipId=%u row=%p: "
                    "pre{sharedGrp=0x%04X var0=0x%04X} -> "
                    "wrote{sharedGrp=0x%04X var0=0x%04X cat=0x%02X "
                    "enableHead=%d}\n",
                    static_cast<unsigned>(partsType),
                    static_cast<unsigned>(equipId),
                    row,
                    static_cast<unsigned>(preSharedGroup),
                    static_cast<unsigned>(preVariant0),
                    static_cast<unsigned>(sharedGroupId),
                    entry->IsFaceEnabled() ? 0x17CA : 0,
                    static_cast<unsigned>(0x42),
                    entry->IsFaceEnabled() ? 1 : 0);

                injectedCount++;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("[StubRowInject] EXCEPTION during row write — aborting. "
                "edcThis may be invalid or row access crashed.\n");
            return false;
        }

        Log("[StubRowInject] complete: injected %zu custom suit stub rows\n",
            injectedCount);

        s_stubRowsInjectedThisSession = (injectedCount > 0);
        return s_stubRowsInjectedThisSession;
    }

    // Forward declaration — defined ~line 1613, used by hkSetupEquipPanelParam
    // to install variant-vtable hooks before the original's vtable dispatch
    // runs (so vtable+0xf0 GetVariantDisplayId is live when SetupEquipPanelParam
    // calls it during the variant-path that our hkIsVariantSuit now triggers).
    static void TryInstallVariantVtableHooks();

    static void __fastcall hkSetupEquipPanelParam(
        void* self, void* panelData, std::uint32_t slotIndex)
    {
        // Inject custom suit stub rows into EquipDevelopController's
        // instance table. Runs once per session at the first
        // SetupEquipPanelParam fire (by which time EquipDevelopController
        // is fully populated with vanilla data).
        InjectCustomSuitStubRows();

        // Install the dynamic variant-vtable hooks BEFORE the original runs.
        //
        // The original SetupEquipPanelParam (mgsvtpp.exe.c:2969000-2969018)
        // dispatches to (+0x48)->vtable[0x488] (IsNamedVariant) and
        // (+0x68)->vtable[0xf0] (GetVariantDisplayId) when the variant-path
        // is taken (which our IsVariantSuit hook now triggers for custom
        // suits). If those vtable hooks aren't installed yet, the vanilla
        // functions run and return 0x400 for custom flow → displayId stays
        // blank. By priming the install here we ensure both hooks are live
        // before the original's dispatch. One-shot (TryInstall... has a
        // g_VariantVtableAttempted guard).
        TryInstallVariantVtableHooks();

        s_sortieContextActive = true;  // we're in sortie prep
        s_inSetupEquipPanel = true;
        g_OrigSetupEquipPanelParam(self, panelData, slotIndex);
        s_inSetupEquipPanel = false;

        if (!panelData)
            return;

        auto* panel = reinterpret_cast<std::uint8_t*>(panelData);
        const std::uint16_t equipId = *reinterpret_cast<std::uint16_t*>(panel + 0x178);
        const std::uint16_t displayId = *reinterpret_cast<std::uint16_t*>(panel + 0x17a);
        const std::uint8_t isDeveloped = panel[0x17c];

        Log("[SetupEquipPanel] slot=%u equipId=%u displayId=0x%04X isDev=%u\n",
            slotIndex, static_cast<unsigned>(equipId),
            static_cast<unsigned>(displayId), static_cast<unsigned>(isDeveloped));

        // Only track suit slots (0, 1, 2)
        if (slotIndex > 2)
            return;

        // Cache self and panel for re-triggering from hkSetItemDetail
        s_cachedSelf = self;
        s_suitPanels[slotIndex] = panelData;

        // IsDeveloped hook and grade-show force BOTH removed (see PARKED note
        // above). Re-enablement requires registering valid GunPartsDesc rows
        // for each custom-suit equipId before the detail path is allowed to
        // run. Until then, custom suits show as "undeveloped" (blank panels).
        if (isDeveloped != 0)
        {
            bool customActive = false;
            if (ResolveApis())
            {
                auto* qt = reinterpret_cast<std::uint8_t*>(g_GetQuarkSystemTable());
                if (qt)
                {
                    auto* q98 = *reinterpret_cast<std::uint8_t**>(qt + 0x98);
                    if (q98)
                    {
                        auto* state = *reinterpret_cast<std::uint8_t**>(q98 + 0x10);
                        if (state)
                        {
                            const CustomSuitEntry* e = nullptr;
                            if (TryGetCustomSuitByPartsType(state[0xF8], &e) && e)
                                customActive = true;
                        }
                    }
                }
            }

            if (customActive)
            {
                // Diagnostic only — all display-forcing is parked.
                Log("[SetupEquipPanel] custom suit visible at slot=%u "
                    "(display-forcing parked)\n", slotIndex);

                // Silent-commit-prime attempt (2026-04-20): fired
                // TriggerSilentSuitCommit with a vanilla blob during
                // panel setup → CRASH at 0x14973DBA7 inside the game's
                // commit function. That function expects state on the
                // `self` pointer that isn't valid during the
                // SetupEquipPanelParam phase. Reverted — don't re-try
                // calling RequestToChangePlayerPartsInMissionPreparationMode
                // from inside a panel-setup callback.
                //
                // Final state: HEAD OPTION entries for custom enableHead=true
                // suits populate naturally AFTER the user picks any other
                // suit and re-picks the custom one (one-time per sortie
                // session). Documented UX wart. All other HEAD OPTION
                // behavior (row visibility per enableHead, variant cycling,
                // EQP badge, etc.) works on initial equip.
            }
        }
    }

    // Hook for SetItemDetail — diagnostic passthrough.
    // The UNIFORMS display fix is now in hkSetupEquipPanelParam (visibility fix).
    // SetItemDetail still fires for dynamic updates when the user changes suits.
    // For custom suits, remap to the last vanilla flowIndex so mode 0 can resolve.
    static void __fastcall hkSetItemDetail(void* self, std::uint16_t flowIndex)
    {
        const CustomSuitEntry* entry = nullptr;
        if (TryGetCustomSuitByFlowIndex(flowIndex, &entry) && entry)
        {
            const std::uint8_t pt = ReadCurrentPlayerType();
            const std::uint16_t vanillaId = (pt < 4) ? s_vanillaFlowIndexByPt[pt] : 0;

            if (vanillaId != 0)
            {
                Log("[SetItemDetail] remap custom %u -> vanilla %u (pt=%u)\n",
                    static_cast<unsigned>(flowIndex),
                    static_cast<unsigned>(vanillaId),
                    static_cast<unsigned>(pt));
                g_OrigSetItemDetail(self, vanillaId);
                return;
            }
        }

        g_OrigSetItemDetail(self, flowIndex);
    }

    // Hook for GetSelectionNum (0x1416bc2c0).
    // Returns the number of rows in the CHARACTER-SELECTION dialog (the menu
    // with CHARACTER / UNIFORMS / HEAD OPTION rows):
    //   1 = CHARACTER only
    //   2 = CHARACTER + UNIFORMS
    //   3 = CHARACTER + UNIFORMS + HEAD OPTION
    //
    // User requirement: HEAD OPTION visibility follows the Lua suit's
    // `enableHead` flag, NOT the vanilla "has a face FOVA?" gate:
    //   enableHead=true  → HEAD OPTION shown (row 3)
    //   enableHead=false → HEAD OPTION hidden (row 2)
    //
    // Strategy:
    //   - enableHead=true  custom suit → force return 3 to show HEAD OPTION.
    //     The HEAD OPTION row enumerates from the currently-displayed flow,
    //     which SetItemDetail remaps to a vanilla suit with a head-option
    //     catalog, so entries are present and navigation is safe.
    //   - enableHead=false custom suit → force return 2 (CHARACTER+UNIFORMS
    //     only). This hides HEAD OPTION without hiding UNIFORMS — returning
    //     1 is wrong because it would kill the UNIFORMS row too.
    //   - non-custom suits → passthrough.
    //
    // Risk: memo documents that 2→3 for enableHead=true suits previously
    // crashed on navigation due to empty EquipIdTable variation-list. That
    // was before SetItemDetail remap was in place. If the crash returns,
    // the fix is to hook the HEAD OPTION enumerator (vtable+0x220,
    // GetCurrentHeadOptionEquipId) to inject BALACLAVA (0x17CA) for
    // custom enableHead=true suits. Tracked as a followup.
    // First-time-per-session prime flag for HEAD OPTION entries on initial
    // equip with persisted custom suit. See hkGetSelectionNum below for the
    // use site.
    //
    // Rationale: entries are empty on fresh sortie-prep entry when the user
    // loaded with a custom enableHead=true suit already equipped. One round
    // of RequestToChangePlayerPartsInMissionPreparationMode (user manually
    // cycling through another suit and back) makes them populate. This
    // flag tracks whether we've already fired an automatic "silent commit"
    // for this sortie session — prevents re-firing every frame while the
    // dialog is open.
    //
    // Reset to false via hkSetupEquipPanelParam's sortie-context clearing
    // path (next session can re-prime).
    static bool s_silentCommitPrimedThisSortie = false;
    static std::uint8_t s_silentCommitPrimedPartsType = 0xFF;

    // Re-entrancy guard for the silent-commit prime. Protects against the
    // two-commit sequence in TriggerSilentSuitCommitFromLiveState — if the
    // first commit (vanilla) triggers downstream code that ends up calling
    // back into hkGetSelectionNum (e.g., a game system polling the
    // selection count mid-commit), we don't want that nested call to
    // observe the in-flight state and fire ANOTHER prime. Thread-local
    // because all commits dispatch on the game's main thread, and we want
    // the flag to cover the entire commit chain (outer TriggerSilentSuitCommitFromLiveState
    // call + both inner wrapper calls).
    static thread_local bool g_PrimeInFlight = false;

    static char __fastcall hkGetSelectionNum(void* self)
    {
        // Scope-gate: while GetSelectionNum runs (including its internal
        // vtable[+0x1f8]() call that chains to our hkGetCurrentSuitFlowIndex),
        // set the thread-local flag so the flow-index hook returns the
        // vanilla remap target instead of the custom linkedFlowIndex —
        // ensuring HasHeadOptions(+0x460) gets a flowIndex with a head-
        // option catalog.
        g_InGetSelectionNum = true;
        const char orig = g_OrigGetSelectionNum(self);
        g_InGetSelectionNum = false;

        if (!ResolveApis())
            return orig;

        auto* qt = reinterpret_cast<std::uint8_t*>(g_GetQuarkSystemTable());
        if (!qt) return orig;
        auto* q98 = *reinterpret_cast<std::uint8_t**>(qt + 0x98);
        if (!q98) return orig;
        auto* state = *reinterpret_cast<std::uint8_t**>(q98 + 0x10);
        if (!state) return orig;

        const std::uint8_t livePartsType = state[0xF8];
        const CustomSuitEntry* entry = nullptr;
        if (!TryGetCustomSuitByPartsType(livePartsType, &entry) || !entry)
            return orig;

        // enableHead=true  → 3 (show HEAD OPTION)
        // enableHead=false → 2 (hide HEAD OPTION, keep UNIFORMS)
        const char forced = entry->IsFaceEnabled() ? 3 : 2;

        // Throttled change-only log.
        static std::uint32_t s_lastKey = 0xFFFFFFFFu;
        const std::uint32_t key =
            (static_cast<std::uint32_t>(livePartsType) << 16) |
            (static_cast<std::uint32_t>(static_cast<std::uint8_t>(orig)) << 8) |
            static_cast<std::uint32_t>(static_cast<std::uint8_t>(forced));
        if (key != s_lastKey)
        {
            s_lastKey = key;
            Log("[GetSelectionNum] custom partsType=0x%02X enableHead=%d orig=%d -> forced=%d\n",
                static_cast<unsigned>(livePartsType),
                entry->IsFaceEnabled() ? 1 : 0,
                static_cast<int>(orig),
                static_cast<int>(forced));
        }

        // Silent-commit prime for HEAD OPTION entries on initial equip.
        //
        // CORRECTED THEORY 2026-04-20 (after two failed attempts):
        //
        // Earlier hypotheses (state[0x138] is the gate, state[F9]=0xFF
        // transition is the trigger) were BOTH disproven by log evidence:
        //
        //   Fresh load with persisted custom suit state:
        //     state[F9]=0xFF (already UI-sentinel from previous session save)
        //     state[0x134/0x138]=... (already non-zero from previous session)
        //     → HEAD OPTION entries STILL empty
        //
        //   After user re-equips via UI:
        //     state unchanged (same f9=0xFF, same 0x134/0x138)
        //     → HEAD OPTION entries populate
        //
        // So the gate is NOT in Quark state. It's in `AttackActionImpl`'s
        // runtime flags at `[this+0x5b8]`, `[this+0x5b9]`, `[this+0x5bb]`
        // and the dirty bit at `((this+0x8).+0x138)+0x264 |= 0x80`. These
        // are set by the commit function's update block at `LAB_14973dcf8`
        // and persist ONLY within a game session (reset to 0 on every
        // game load).
        //
        // Path to force the update block WITHOUT requiring SIL=1 (which
        // needs blob[0..2] to differ from state[F8/F9/BA0]):
        //
        //   `LAB_14973dd4e` at 0x14973dd4e fires when `blob[0x03] !=
        //   state[0xFE]`. It sets `[RBX+0x5b9] |= 0x4` and JMPs DIRECTLY
        //   to `LAB_14973dcf8` (the update block) — bypassing the SIL=0
        //   skip. So if we commit with blob[0x03] != state[0xFE], the
        //   flags get set regardless of whether the other state bytes
        //   match.
        //
        //   TriggerSilentSuitCommitFromLiveState already constructs a blob
        //   with blob[0x03]=1 when state[0xFE]=0 (enableHead=true default).
        //   This forces the LAB_14973dd4e path to run the update block.
        //
        // Gating:
        //   - only for enableHead=true suits (HEAD OPTION is hidden for
        //     enableHead=false; no point priming)
        //   - only when sortie context is active (SetupEquipPanelParam has
        //     fired at least once, indicating UI is up)
        //   - throttled: at most one prime per (partsType, session) so
        //     repeated GetSelectionNum calls don't spam commits
        //
        // NO F9 GATE: drop the earlier condition. Prime fires on fresh
        // load regardless of state[F9] value because the AttackActionImpl
        // flags are always reset to 0 on game load, and the commit is
        // needed to re-establish them.
        //
        // Side effect: state[0xFE] gets written to blob[0x03] (=1) if it
        // was 0. User ends up wearing BALACLAVA (head option index 1) by
        // default. They can change it via the now-populated HEAD OPTION
        // menu.
        if (entry->IsFaceEnabled() && s_sortieContextActive)
        {
            const std::uint8_t  liveF9    = state[0xF9];
            const std::uint16_t liveFE    =
                *reinterpret_cast<std::uint16_t*>(state + 0xFE);
            const std::uint32_t state134  =
                *reinterpret_cast<std::uint32_t*>(state + 0x134);
            const std::uint32_t state138  =
                *reinterpret_cast<std::uint32_t*>(state + 0x138);

            // Throttled diagnostic: log state snapshot on entry and on
            // any change. Useful for seeing what the commit actually does.
            static std::uint64_t s_lastPackedLog = 0xFFFFFFFFFFFFFFFFull;
            static std::uint32_t s_lastF9FELog   = 0xFFFFFFFFu;
            const std::uint64_t packed =
                (static_cast<std::uint64_t>(state138) << 32) |
                static_cast<std::uint64_t>(state134);
            const std::uint32_t f9feKey =
                (static_cast<std::uint32_t>(liveF9) << 16) |
                static_cast<std::uint32_t>(liveFE);
            if (packed != s_lastPackedLog || f9feKey != s_lastF9FELog)
            {
                s_lastPackedLog = packed;
                s_lastF9FELog   = f9feKey;
                Log("[GetSelectionNum] state[F9]=0x%02X state[FE]=0x%04X "
                    "state[0x134]=0x%08X state[0x138]=0x%08X\n",
                    static_cast<unsigned>(liveF9),
                    static_cast<unsigned>(liveFE),
                    static_cast<unsigned>(state134),
                    static_cast<unsigned>(state138));
            }

            // SetItemDetail prime was previously fired from HERE but that's
            // TOO EARLY — subscribers for the SendTrigger cascade aren't
            // registered when hkGetSelectionNum first runs. Log evidence
            // 2026-04-20: user navigated directly to HEAD OPTION row after
            // fresh load, prime from hkGetSelectionNum fired, entries still
            // empty.
            //
            // Moved to hkSetupCharacterSlotSelect (below) which fires
            // per-row AFTER the UI is fully set up — subscribers are
            // registered by then and the prime's SetItemDetail events
            // reach the HEAD OPTION menu builder.
            (void)g_PrimeInFlight;
        }

        return forced;
    }

    // Hook for HasHeadOptions (FUN_1460b9fa0, sub-controller vtable+0x460).
    //
    // GetSelectionNum calls this via `vtable[+0x460](flowIndex)` where
    // flowIndex comes from GetCurrentSuitFlowIndex (which our existing
    // hook already redirects to a cached vanilla flow `s_vanillaFlowIndexByPt[pt]`
    // inside GetSelectionNum scope). The function does a 7-way OR of
    // EquipDevelopController table lookups:
    //   uVar3 = ED.vtable+0x178(flow)   (flow → small category code)
    //   sVar2 = ED.vtable+0x128(flow)   (flow → equipId-like)
    //   returns true if any of:
    //     uVar3 ∈ {0x55, 0x56, 0x5d, 0x58, 0x61, 0x62, 0x5e}
    //     sVar2 ∈ {0x4a88, 0x4a8d, 0x4a8e}
    //     iVar3 (= uVar3 as int) ∈ [0x4f, 0x54]
    //     sVar2 ∈ [0x4a92..0x4a9d] ∪ [0x4ab0..0x4ace]
    //
    // The PROBLEM with relying on the redirect alone (what our existing
    // hkGetCurrentSuitFlowIndex does): the seeded vanilla `521` doesn't
    // pass any of these checks for some playerTypes, so HasHeadOptions
    // returns false → GetSelectionNum returns 2 → row hidden AND entries
    // never populate. Empirically `698` (Fox's default) works on pt=2,
    // but we can't universally guarantee a magic seed value.
    //
    // SOLUTION: force return 1 for custom enableHead=true suits when our
    // GetSelectionNum scope is active. This bypasses the brittle vanilla
    // flow matching entirely. Scoped via `g_InGetSelectionNum` so we
    // don't accidentally enable HEAD OPTION in other contexts (though the
    // decomp shows GetSelectionNum is the only caller, scoping is cheap
    // insurance).
    //
    // 2026-04-20 log evidence — fresh load with persisted custom suit:
    //   `vanilla=521 ... orig=2` → HasHeadOptions(521) = false
    // After user cycles through fox:
    //   `vanilla=698 ... orig=3` → HasHeadOptions(698) = true
    // Same custom suit, different vanilla remap, different outcome —
    // confirms the check depends on the argument, not on any cached
    // state. Forcing 1 here sidesteps the whole problem.
    static std::uint8_t __fastcall hkHasHeadOptions(
        void* subController, std::uint16_t flowIndex)
    {
        if (g_InGetSelectionNum && ResolveApis())
        {
            auto* qt = reinterpret_cast<std::uint8_t*>(g_GetQuarkSystemTable());
            if (qt)
            {
                auto* q98 = *reinterpret_cast<std::uint8_t**>(qt + 0x98);
                if (q98)
                {
                    auto* state = *reinterpret_cast<std::uint8_t**>(q98 + 0x10);
                    if (state)
                    {
                        const std::uint8_t livePartsType = state[0xF8];
                        const CustomSuitEntry* entry = nullptr;
                        if (TryGetCustomSuitByPartsType(livePartsType, &entry) &&
                            entry && entry->IsFaceEnabled())
                        {
                            // Throttled log: fires every GetSelectionNum tick
                            // during sortie prep, so key on (partsType, flow)
                            // to only print once per combination.
                            static std::uint32_t s_lastKey = 0xFFFFFFFFu;
                            const std::uint32_t key =
                                (static_cast<std::uint32_t>(livePartsType) << 16) |
                                static_cast<std::uint32_t>(flowIndex);
                            if (key != s_lastKey)
                            {
                                s_lastKey = key;
                                Log("[HasHeadOptions] forced 1 for custom "
                                    "partsType=0x%02X flow=%u (orig would "
                                    "depend on ED table lookups)\n",
                                    static_cast<unsigned>(livePartsType),
                                    static_cast<unsigned>(flowIndex));
                            }
                            return 1;
                        }
                    }
                }
            }
        }

        return g_OrigHasHeadOptions
            ? g_OrigHasHeadOptions(subController, flowIndex)
            : 0;
    }

    // Hook for FUN_1460b4300 (HeadOptionIndexGetter, vtable+0x1d8 on
    // sub-controller, called by GetCurrentItems to fill the 8 HEAD OPTION
    // entries in the sortie CharaSlotSelect item buffer).
    //
    // The vanilla function ignores the index argument and returns
    // state[0x138] for all 8 calls, giving the UI 8 copies of the same
    // value. For vanilla suits, state[0x138] is a valid packed StaffId that
    // the UI eventually resolves to the current head option. For custom
    // suits, state[0x138] is typically 0x6602D000 → truncated to u16
    // 0xD000 → not a valid equipId → UI renders empty slots.
    //
    // Fix: for custom enableHead=true suits, return distinct vanilla
    // head-option equipIds per index so the UI has something to render.
    // The head-option catalog at `DAT_14239a5c0` starts with:
    //   row 0 = 0x17CA (BALACLAVA)
    //   row 1 = 0x17CB (BANDANA)
    //   row 2 = 0x17CC (HEADGEAR A)
    //   row 3 = 0x17CD (HEADGEAR B)
    //   row 4 = 0x17CE (HEADGEAR C)
    // These are the default vanilla suits' head options — the 5 canonical
    // options any user with an enableHead=true custom suit should be able
    // to pick from.
    //
    // This replaces the previous failed approach of firing `SetItemDetail`
    // primes to register HEAD OPTION subscribers — that path only works
    // when the user opens the UNIFORMS sub-menu because the subscriber
    // registration happens inside that sub-menu's initialization cascade.
    // By directly returning the entries from the enumerator, we bypass
    // the subscriber-registration requirement entirely — the UI reads
    // our forced values and renders them without needing any subscriber
    // to have been notified.
    static std::uint32_t __fastcall hkHeadOptionIndexGetter(
        void* self, std::uint8_t index)
    {
        if (ResolveApis())
        {
            auto* qt = reinterpret_cast<std::uint8_t*>(g_GetQuarkSystemTable());
            if (qt)
            {
                auto* q98 = *reinterpret_cast<std::uint8_t**>(qt + 0x98);
                if (q98)
                {
                    auto* state = *reinterpret_cast<std::uint8_t**>(q98 + 0x10);
                    if (state)
                    {
                        const std::uint8_t livePartsType = state[0xF8];
                        const CustomSuitEntry* entry = nullptr;
                        if (TryGetCustomSuitByPartsType(livePartsType, &entry) &&
                            entry && entry->IsFaceEnabled())
                        {
                            // Vanilla head-option equipIds from DAT_14239a5c0
                            // (indices 0..4 → BALACLAVA..HEADGEAR variants).
                            // Indices 5-7 return 0 (no more options).
                            static constexpr std::uint16_t kHeadOptions[8] = {
                                0x17CA,  // BALACLAVA
                                0x17CB,  // BANDANA
                                0x17CC,  // HEADGEAR A
                                0x17CD,  // HEADGEAR B
                                0x17CE,  // HEADGEAR C
                                0x0000,  // (unused)
                                0x0000,  // (unused)
                                0x0000,  // (unused)
                            };
                            const std::uint32_t forced =
                                (index < 8) ? kHeadOptions[index] : 0;

                            // Throttled log: 8 calls per UI tick, key on
                            // (partsType, index) to log each index-slot
                            // once per partsType.
                            static std::uint16_t s_lastPtIndex = 0xFFFF;
                            const std::uint16_t key =
                                (static_cast<std::uint16_t>(livePartsType) << 8) |
                                static_cast<std::uint16_t>(index);
                            if (key != s_lastPtIndex)
                            {
                                s_lastPtIndex = key;
                                Log("[HeadOptionIndexGetter] custom "
                                    "partsType=0x%02X index=%u -> equipId=0x%04X "
                                    "(forced vanilla head option)\n",
                                    static_cast<unsigned>(livePartsType),
                                    static_cast<unsigned>(index),
                                    static_cast<unsigned>(forced));
                            }
                            return forced;
                        }
                    }
                }
            }
        }

        return g_OrigHeadOptionIndexGetter
            ? g_OrigHeadOptionIndexGetter(self, index)
            : 0;
    }

    // Hook for GetSuitVariation (vtable+0x460).
    // Returns: bit 0 = has 1st body variant, bit 1 = has 2nd body variant.
    // The UI reads these bits to decide whether to show ◀/▶ cycle arrows
    // for in-row variant browsing (e.g. Sneaking Suit [The Boss]
    // Standard/Naked). Without them, variants appear as separate rows
    // instead of cycling within the UNIFORMS row.
    //
    // ENABLED (body-variant branch only): returns bits based on
    // HasVariantGroup size so our sub-slot emission in hkAddListSuit is
    // matched by cycle-arrow rendering.
    //
    // DISABLED (IsFaceEnabled branch): previously we ALSO set bit 0 for any
    // custom suit with IsFaceEnabled=true (wrong — those bits are for
    // body variants, not head options). That made the UI try to render a
    // body-variant row for face-only suits, and when it tried to populate
    // the row from the EquipIdTable variation-list (empty for custom) it
    // crashed on navigation. Do NOT re-enable that path without the
    // variation-list data-layer fix.
    static std::uint8_t __fastcall hkGetSuitVariation(void* self, std::uint16_t suitId)
    {
        const std::uint8_t origRet = g_OrigGetSuitVariation
            ? g_OrigGetSuitVariation(self, suitId)
            : 0;

        const CustomSuitEntry* entry = nullptr;
        if (!TryGetCustomSuitByFlowIndex(suitId, &entry) || !entry)
        {
            if (!TryGetCustomSuitByDevelopId(suitId, &entry) || !entry)
                TryGetCustomSuitByPartsType(static_cast<std::uint8_t>(suitId & 0xFF), &entry);
        }
        if (!entry)
            TryGetCustomSuitBySelectorCode(static_cast<std::uint8_t>(suitId & 0xFF), &entry);

        // GetSuitVariation returning non-zero bits makes GetSelectionNum add
        // +1 (decomp: `(cVar2 != 0) + 2`), which upgrades the row layout from
        // 2 rows (UNIFORMS + HEAD OPTION) to 3 rows (UNIFORMS + variant-track
        // + HEAD OPTION). Note that the HEAD OPTION row ALWAYS exists in
        // vanilla CharaSlotSelectMode 0/2; returning 0 doesn't remove it —
        // it just removes the variant-track middle row.
        //
        // User requirement: HEAD OPTION appears ONLY when enableHead=true.
        // Since HEAD OPTION is always present by default, we achieve the
        // requirement by gating the variant-row ADDITION on IsFaceEnabled
        // AND suppressing the HEAD OPTION row separately in hkGetSelectionNum
        // below when !IsFaceEnabled.
        //
        // Here we only return variant bits when the suit opted into
        // enableHead, matching the user's "HEAD OPTION only when enableHead"
        // convention.
        if (entry && entry->HasVariantGroup() && entry->IsFaceEnabled())
        {
            const std::size_t groupSize = GetVariantGroupSize(entry->variantGroupId);
            std::uint8_t bits = 0;
            if (groupSize >= 2) bits |= 1;
            if (groupSize >= 3) bits |= 2;

            Log("[GetSuitVariation] custom suitId=%u group=%u size=%zu enableHead=1 origRet=0x%02X -> bits=0x%02X\n",
                static_cast<unsigned>(suitId),
                static_cast<unsigned>(entry->variantGroupId),
                groupSize,
                static_cast<unsigned>(origRet),
                static_cast<unsigned>(bits));

            return bits != 0 ? bits : origRet;
        }

        // Custom suit with variants but NOT enableHead → return 0 so the
        // game reports "no body variants" and GetSelectionNum stays at 2
        // (UNIFORMS + HEAD OPTION only; variant cycling continues to work
        // via the sub-slot count byte at +0xbc40 independent of this).
        if (entry && entry->HasVariantGroup() && !entry->IsFaceEnabled())
        {
            Log("[GetSuitVariation] custom suitId=%u group=%u enableHead=0 -> 0 (suppress variant-row)\n",
                static_cast<unsigned>(suitId),
                static_cast<unsigned>(entry->variantGroupId));
            return 0;
        }

        // Custom suit WITHOUT variant group but WITH enableHead=true
        // (e.g. plain SSD) → return 1 so GetSelectionNum's `+0x460`
        // HasHeadOptions check comes back non-zero and the dialog adds
        // the HEAD OPTION row naturally. This also signals to downstream
        // enumerators that head options exist, potentially causing the
        // vanilla head-option catalog (0x17CA..0x17CE) to populate.
        //
        // Log always (no throttle) so we confirm this branch actually
        // fires for SSD during the GetSelectionNum decision path. If the
        // log never shows (suitId=922 not matched here), then
        // GetSelectionNum's `+0x460` dispatches through a different
        // vtable than the 0x149519e60 we hook here, and we need a
        // different strategy.
        if (entry && !entry->HasVariantGroup() && entry->IsFaceEnabled())
        {
            static std::uint16_t s_lastSuit = 0xFFFF;
            if (suitId != s_lastSuit)
            {
                s_lastSuit = suitId;
                Log("[GetSuitVariation] custom suitId=%u enableHead=1 no-variants origRet=0x%02X -> 0x01 (force HasHeadOptions)\n",
                    static_cast<unsigned>(suitId),
                    static_cast<unsigned>(origRet));
            }
            return 1;
        }

        // Throttled passthrough log for diagnostics.
        static std::uint32_t s_lastKey = 0xFFFFFFFFu;
        const std::uint32_t key = (static_cast<std::uint32_t>(suitId) << 8) | origRet;
        if (key != s_lastKey)
        {
            s_lastKey = key;
            Log("[GetSuitVariation] passthrough suitId=%u origRet=0x%02X (not custom)\n",
                static_cast<unsigned>(suitId),
                static_cast<unsigned>(origRet));
        }
        return origRet;
    }

    // Hook for FUN_1460af810 (mis-labeled "HeadOptionTableLookup").
    //
    // DISABLED for custom suits (passthrough). Per decomp research this is
    // actually `tpp::mbm::StaffController::FindByUniqueTypeId` (search fields
    // at +0x9c78/+0x9c80/+0x9e6c on StaffController), NOT a head-option
    // catalog. Force-returning 1 told the game "this suit has a head-option
    // entry" without any backing data → HEAD OPTION row became visible but
    // crashed on navigation.
    //
    // Kept installed purely as a diagnostic tap in case we later need to
    // observe calls; the forcing behavior is removed.
    static std::uint64_t __fastcall hkHeadOptionTableLookup(
        void* self, std::int16_t* outIndex, std::uint64_t equipKey)
    {
        return g_OrigHeadOptionTableLookup(self, outIndex, equipKey);
    }

    // ---- Named-variant display hook bodies ----
    //
    // These are the replacement functions for the dynamically-resolved
    // vtable slots on EquipDevelopController and EquipParameterTables.
    // See the "Named-variant display hooks" block earlier in this file
    // for the vtable chain.

    // IsNamedVariant (EquipDevelopController vtable+0x488).
    // Zero args — the game reads "currently-selected suit" from the object's
    // internal state. Decides grade-track vs name-track in
    // SetupEquipPanelParam (mgsvtpp.exe.c:2969007).
    //
    // We force return 1 whenever the live player partsType (state[0xF8]) is
    // a custom variant-group base. That forces the name-track branch so the
    // UI calls GetVariantDisplayId (which we also hook) for each sub-slot
    // label.
    static std::uint8_t __fastcall hkIsNamedVariant(void* self)
    {
        if (ResolveApis())
        {
            auto* qt = reinterpret_cast<std::uint8_t*>(g_GetQuarkSystemTable());
            if (qt)
            {
                auto* q98 = *reinterpret_cast<std::uint8_t**>(qt + 0x98);
                if (q98)
                {
                    auto* state = *reinterpret_cast<std::uint8_t**>(q98 + 0x10);
                    if (state)
                    {
                        const std::uint8_t livePartsType = state[0xF8];
                        const CustomSuitEntry* entry = nullptr;
                        if (TryGetCustomSuitByPartsType(livePartsType, &entry) &&
                            entry && entry->HasVariantGroup())
                        {
                            // Throttled: UI renderer calls this many times per
                            // frame (one per panel). Only log on key change.
                            static std::uint32_t s_lastKey = 0xFFFFFFFFu;
                            const std::uint32_t key =
                                (static_cast<std::uint32_t>(livePartsType) << 16) |
                                static_cast<std::uint32_t>(entry->variantGroupId);
                            if (key != s_lastKey)
                            {
                                s_lastKey = key;
                                Log("[IsNamedVariant] forced 1 for custom partsType=0x%02X group=%u\n",
                                    static_cast<unsigned>(livePartsType),
                                    static_cast<unsigned>(entry->variantGroupId));
                            }
                            return 1;
                        }
                    }
                }
            }
        }

        return g_OrigIsNamedVariant ? g_OrigIsNamedVariant(self) : 0;
    }

    // GetVariantDisplayId (EquipParameterTables vtable+0xf0).
    // Args: (self, flowIndex u16, subIndex u32). Returns a u16 displayId
    // that the UI later resolves to a label string via vtable+0x770 and an
    // icon path via vtable+0x128.
    //
    // Our redirect: for a custom variant-group suit, call the original with
    // vanilla Sneaking Suit (The Boss) flowIndex=697, remapping our zero-
    // based variantIndex to the game's canonical variant enum
    // {base → 7 (BASE), 1 → 0 (NAKED), 2 → 1 (ALT1), ...}. The returned
    // displayId points to a valid vanilla variant name string. Our custom
    // body's icon is unaffected because panel+0x178 (flowIndex) remains
    // custom and the icon resolver (+0x668) uses flowIndex, not displayId.
    //
    // First-pass limitation: variant labels show as vanilla "Standard" /
    // "Naked" verbatim until we own a custom string table. User's suit
    // name (from develop entry) is still correct; only the sub-slot text
    // uses vanilla vocab.
    static std::uint16_t __fastcall hkGetVariantDisplayId(
        void* self, std::uint16_t flowIndex, std::uint32_t subIndex)
    {
        if (!g_OrigGetVariantDisplayId)
            return 0;

        // Throttled passthrough diagnostic: confirms whether the hook is
        // reachable at all for non-custom flows. If IsNamedVariant returns 1
        // but this never fires, the UI is dispatching to a different vtable
        // slot (wrong offset) or resolving displayId via a separate path.
        {
            static std::uint32_t s_lastKey = 0xFFFFFFFFu;
            const std::uint32_t key =
                (static_cast<std::uint32_t>(flowIndex) << 8) |
                (subIndex & 0xFF);
            if (key != s_lastKey)
            {
                s_lastKey = key;
                Log("[GetVariantDisplayId] called flow=%u sub=%u\n",
                    static_cast<unsigned>(flowIndex),
                    static_cast<unsigned>(subIndex));
            }
        }

        const CustomSuitEntry* entry = nullptr;
        if (TryGetCustomSuitByFlowIndex(flowIndex, &entry) &&
            entry && entry->HasVariantGroup())
        {
            // Remap our subIndex to vanilla's variant enum.
            //   our 0 (base)  → vanilla 7 (BASE / "Standard")
            //   our 1 (first) → vanilla 0 (NAKED)
            //   our 2+        → vanilla 1+ (ALT1, ALT2, ...)
            std::uint32_t vanillaSub = subIndex;
            if (subIndex == 0)       vanillaSub = 7;
            else if (subIndex >= 1)  vanillaSub = subIndex - 1;

            const std::uint16_t redirected =
                g_OrigGetVariantDisplayId(self, kVanillaSneakingSuitFlow, vanillaSub);

            Log("[GetVariantDisplayId] redirect custom flow=%u sub=%u -> vanilla flow=%u sub=%u -> displayId=0x%04X\n",
                static_cast<unsigned>(flowIndex),
                static_cast<unsigned>(subIndex),
                static_cast<unsigned>(kVanillaSneakingSuitFlow),
                static_cast<unsigned>(vanillaSub),
                static_cast<unsigned>(redirected));
            return redirected;
        }

        return g_OrigGetVariantDisplayId(self, flowIndex, subIndex);
    }

    // Lazy resolver: walks the QST object graph to find the two vtable
    // entries, then MinHooks them. Runs once per session. Guarded by SEH
    // around every pointer deref so a missing link in the chain just logs
    // and skips install — the mod continues to work without the name-
    // display redirect.
    static void TryInstallVariantVtableHooks()
    {
        if (g_VariantVtableAttempted)
            return;

        if (!ResolveApis())
            return;

        g_VariantVtableAttempted = true;

        __try
        {
            // Correct QST chain, verified directly against decomp line
            // 2966742-2966754 (MissionPreparationCallbackImpl::Init):
            //   lVar5 = *(pQVar2 + 0x98)                — q98 (impl root)
            //   lVar1 = *(lVar5 + 0x110)                 — q110 = MissionPreparationSystem
            //   devCtrl = *(lVar1 + 0xac8)               — EquipDevelopController
            //   q40 = *(lVar5 + 0x40)                    — EquipSystem-adjacent impl
            //   facade = *(q40 + 0x48)                   — EquipParameterTables / SuitInfo
            auto* qt = reinterpret_cast<std::uint8_t*>(g_GetQuarkSystemTable());
            if (!qt)
            {
                Log("[VariantVtable] QST null — skip install\n");
                return;
            }

            auto* q98 = *reinterpret_cast<std::uint8_t**>(qt + 0x98);
            if (!q98)
            {
                Log("[VariantVtable] qst+0x98 null — skip install\n");
                return;
            }

            // --- EquipDevelopController vtable+0x488 ---
            auto* q110 = *reinterpret_cast<std::uint8_t**>(q98 + 0x110);
            auto* devCtrl = q110 ? *reinterpret_cast<std::uint8_t**>(q110 + 0xac8) : nullptr;
            auto** devVtable = devCtrl ? *reinterpret_cast<void***>(devCtrl) : nullptr;
            if (devVtable)
            {
                g_IsNamedVariantTarget = devVtable[0x488 / sizeof(void*)];
            }

            // --- EquipParameterTables vtable+0xf0 ---
            auto* q40 = *reinterpret_cast<std::uint8_t**>(q98 + 0x40);
            auto* facade = q40 ? *reinterpret_cast<std::uint8_t**>(q40 + 0x48) : nullptr;
            auto** facadeVtable = facade ? *reinterpret_cast<void***>(facade) : nullptr;
            if (facadeVtable)
            {
                g_GetVariantDisplayIdTarget = facadeVtable[0xf0 / sizeof(void*)];
            }

            Log("[VariantVtable] chain qt=%p q98=%p q110=%p devCtrl=%p vtable=%p isNamedVariant=%p\n",
                qt, q98, q110, devCtrl, static_cast<void*>(devVtable), g_IsNamedVariantTarget);
            Log("[VariantVtable] chain q40=%p facade=%p vtable=%p getVariantDisplayId=%p\n",
                q40, facade, static_cast<void*>(facadeVtable), g_GetVariantDisplayIdTarget);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("[VariantVtable] exception walking QST chain — skip install\n");
            return;
        }

        // Install MinHooks on the resolved targets, each gated on a valid
        // pointer + a successful MinHook call. If either fails the other
        // stays uninstalled too — we either get the whole name-display path
        // or none of it (partial would leave the UI in an inconsistent state).
        bool ok1 = false;
        bool ok2 = false;

        if (g_IsNamedVariantTarget)
        {
            ok1 = CreateAndEnableHook(
                g_IsNamedVariantTarget,
                reinterpret_cast<void*>(&hkIsNamedVariant),
                reinterpret_cast<void**>(&g_OrigIsNamedVariant));
            Log("[VariantVtable] IsNamedVariant hook @%p: %s\n",
                g_IsNamedVariantTarget, ok1 ? "OK" : "FAIL");
        }

        if (g_GetVariantDisplayIdTarget && ok1)
        {
            ok2 = CreateAndEnableHook(
                g_GetVariantDisplayIdTarget,
                reinterpret_cast<void*>(&hkGetVariantDisplayId),
                reinterpret_cast<void**>(&g_OrigGetVariantDisplayId));
            Log("[VariantVtable] GetVariantDisplayId hook @%p: %s\n",
                g_GetVariantDisplayIdTarget, ok2 ? "OK" : "FAIL");
        }

        if (ok1 && ok2)
        {
            g_VariantVtableInstalled = true;
            Log("[VariantVtable] installed — variant name rendering active\n");
        }
        else if (ok1 && !ok2)
        {
            // Partial install would leave UI rendering inconsistent. Unhook.
            DisableAndRemoveHook(g_IsNamedVariantTarget);
            g_OrigIsNamedVariant = nullptr;
            Log("[VariantVtable] GetVariantDisplayId hook failed — rolled back IsNamedVariant\n");
        }
    }

    // Hook for AddListSuit.
    //
    // Two jobs:
    //   (a) Per-playerType filter — suppress custom suits whose playerType
    //       doesn't match the current character.
    //   (b) Variant group collapse + sub-slot emission — for custom suits
    //       in a variant group, only the group's base (lowest variantIndex)
    //       gets a row; the other variants are attached as sub-slots of
    //       that row so the UI cycles them in place of spawning new rows.
    //
    // Sub-slot layout (indexed by slot = row*0xF + subIndex):
    //   this + 0x4440 + slot * 2    = u16 flowIndex (what TrackSelectedCustomSuit reads)
    //   this + 0xcc40 + slot * 0xC  = u32 selectorCode
    //   this + 0xcc44 + slot * 0xC  = u32 variantIndex
    //   this + 0xcc48 + slot * 0xC  = u8  camoType
    //   this + 0xbc40 + row         = u8  total-sub-count for this row
    //   this + 0xc040 + row         = u8  currently-selected sub (UI nav state; we don't touch it)
    //
    // Limit: 0xF = 15 sub-slots per row.
    static void __fastcall hkAddListSuit(
        void* self, std::uint32_t* rowCounter, std::uint16_t suitId, void* param4)
    {
        // Lazy-install the name-display vtable hooks on first call.
        // AddListSuit only fires when the sortie UI opens, guaranteeing that
        // QST and the singleton chain are live. One-shot (TryInstall...
        // sets g_VariantVtableAttempted to prevent re-entry).
        TryInstallVariantVtableHooks();

        constexpr std::size_t kMaxSubSlots = 0x0F;

        // Bulletproof entry lookup: AddListSuit is documented to receive
        // flowIndex (p50), but different call sites in the game may pass
        // developId, partsType, or selectorCode depending on how the caller
        // enumerated the suit list. Try all four keys so variant suppression
        // fires regardless of which key matches. Diagnostic log shows exactly
        // which lookup succeeded.
        const CustomSuitEntry* entry = nullptr;
        const char* matchKind = "none";
        if (TryGetCustomSuitByFlowIndex(suitId, &entry) && entry)
        {
            matchKind = "flowIndex";
        }
        else if (TryGetCustomSuitByDevelopId(suitId, &entry) && entry)
        {
            matchKind = "developId";
        }
        else if (TryGetCustomSuitByPartsType(
                     static_cast<std::uint8_t>(suitId & 0xFF), &entry) && entry)
        {
            matchKind = "partsType";
        }
        else if (TryGetCustomSuitBySelectorCode(
                     static_cast<std::uint8_t>(suitId & 0xFF), &entry) && entry)
        {
            matchKind = "selectorCode";
        }

        if (entry)
        {
            Log("[AddListSuit] entry match suitId=%u via %s partsType=0x%02X group=%u index=%u\n",
                static_cast<unsigned>(suitId),
                matchKind,
                static_cast<unsigned>(entry->customPartsType),
                static_cast<unsigned>(entry->variantGroupId),
                static_cast<unsigned>(entry->variantIndex));

            std::uint8_t currentPlayerType = 0xFF;
            if (ResolveApis())
            {
                auto* qt = reinterpret_cast<std::uint8_t*>(g_GetQuarkSystemTable());
                if (qt)
                {
                    auto* q98 = *reinterpret_cast<std::uint8_t**>(qt + 0x98);
                    if (q98)
                    {
                        auto* state = *reinterpret_cast<std::uint8_t**>(q98 + 0x10);
                        if (state)
                            currentPlayerType = state[0xFB];
                    }
                }
            }

            if (currentPlayerType != 0xFF && entry->playerType != currentPlayerType)
            {
                Log("[AddListSuit] filtered suitId=%u playerType=%u (current=%u)\n",
                    static_cast<unsigned>(suitId),
                    static_cast<unsigned>(entry->playerType),
                    static_cast<unsigned>(currentPlayerType));
                return;
            }

            // Variant group collapse: if this entry is not the base of its
            // group, suppress the whole AddListSuit call — the base will
            // emit all variants as sub-slots later.
            if (entry->HasVariantGroup())
            {
                const CustomSuitEntry* variants[16] = {};
                const std::size_t vcount =
                    GetVariantGroupEntries(entry->variantGroupId, variants, 16);

                if (vcount >= 2 && variants[0] && variants[0] != entry)
                {
                    Log("[AddListSuit] suppress non-base variant suitId=%u group=%u index=%u\n",
                        static_cast<unsigned>(suitId),
                        static_cast<unsigned>(entry->variantGroupId),
                        static_cast<unsigned>(entry->variantIndex));
                    return;
                }
            }
        }

        const std::uint32_t rowBefore = rowCounter ? *rowCounter : 0;

        g_OrigAddListSuit(self, rowCounter, suitId, param4);

        const std::uint32_t rowAfter = rowCounter ? *rowCounter : rowBefore;

        // Emit variant sub-slots after the base has been added.
        if (entry && entry->HasVariantGroup() && rowAfter > rowBefore && self)
        {
            const CustomSuitEntry* variants[16] = {};
            const std::size_t vcount =
                GetVariantGroupEntries(entry->variantGroupId, variants, 16);

            if (vcount >= 2)
            {
                auto* p = reinterpret_cast<std::uint8_t*>(self);
                const std::uint32_t row = rowBefore;
                std::uint8_t sub = 1; // sub 0 is the base (just written by original)

                for (std::size_t i = 0; i < vcount && sub < kMaxSubSlots; ++i)
                {
                    const CustomSuitEntry* v = variants[i];
                    if (!v || v == entry)
                        continue; // skip the base, already at sub 0

                    const std::size_t slot =
                        static_cast<std::size_t>(row) * kMaxSubSlots + sub;

                    // Write the BASE's flowIndex into every sub-slot. Vanilla
                    // cycling works this way — Sneaking Suit (The Boss)
                    // Standard/Naked both hold the same flowIndex in +0x4440
                    // and are distinguished only by the selectorCode at
                    // +0xcc40 and variantIndex at +0xcc44. Writing the
                    // variant's own flowIndex here makes the UI treat the
                    // sub-slot as a DIFFERENT suit (separate row, no cycle
                    // arrows) instead of an in-row variant.
                    //
                    // The commit path reads the selectorCode from +0xcc40 to
                    // resolve back to the variant's CustomSuitEntry (see
                    // TrackSelectedCustomSuit in ItemSelectorSuitCommitHook.cpp).
                    *reinterpret_cast<std::uint16_t*>(p + 0x4440 + slot * 2) =
                        entry->linkedFlowIndex;   // base flowIndex shared across sub-slots
                    *reinterpret_cast<std::uint32_t*>(p + 0xcc40 + slot * 0x0C) =
                        v->customSelectorCode;    // variant-unique selector
                    *reinterpret_cast<std::uint32_t*>(p + 0xcc44 + slot * 0x0C) =
                        v->variantIndex;
                    *(p + 0xcc48 + slot * 0x0C) = 0;

                    // ---- RED X fix ----
                    // Per-sub-slot availability byte. Verified against decomp
                    // (mgsvtpp.exe.c:2950088):
                    //   ((ulonglong)*param_2 * 0xf + 0x548 + param_1) = byte
                    // where *param_2 is the ROW index. Since slot = row*0xF+sub,
                    // the stride is 1 BYTE per sub-slot. Offset is `base + slot`:
                    //   +0x548  = available (1 = can equip; 0 = red X)
                    *(p + 0x548   + slot) = 1;

                    // DO NOT write +0x425a4. Observation (2026-04-20): writing
                    // 1 here caused the UNIFORMS panel's EQP badge to stick
                    // permanently on the variant-group row regardless of which
                    // suit was actually equipped. That offset is NOT the
                    // "developed" flag for suits — it appears to be the
                    // per-row "is-loaded/is-equipped" mark that the EQP badge
                    // renderer keys off. Leaving it at its default (0, set by
                    // the game's zeroing of the row buffer) keeps EQP honest.
                    // If this ever causes the variant to re-acquire a RED X,
                    // the correct fix is probably to write 1 only to the
                    // sub-slot whose partsType/selector matches live state.

                    ++sub;
                }

                // Row-level bytes vanilla AddListWeaponInner writes (decomp
                // lines 2950086-2950087: both = 0). We only set these if the
                // original didn't — i.e. if our sub-slot emission added more
                // slots, confirm the row bytes match vanilla 0.
                *(p + 0xc440 + row) = 0;
                *(p + 0xc840 + row) = 0;

                *(p + 0xbc40 + row) = sub;

                Log("[AddListSuit] variant group row=%u subCount=%u base=flow=%u group=%u\n",
                    static_cast<unsigned>(row),
                    static_cast<unsigned>(sub),
                    static_cast<unsigned>(entry->linkedFlowIndex),
                    static_cast<unsigned>(entry->variantGroupId));
            }
        }

        // TODO: EQP badge for custom suits. The badge is determined at render
        // time by comparing vtable+0x1f8() (returns 0x400 for custom suits)
        // with each entry's flowIndex. Fixing this requires hooking
        // vtable+0x1f8 to return the custom suit's flowIndex.
    }

    // Hook for SetupCharacterSlotSelectPrefabListElement (0x1416bf490).
    //
    // Fires PER-ROW during the CHARACTER-SELECTION dialog setup (once for
    // each of CHARACTER / UNIFORMS / HEAD OPTION rows). Runs AFTER the
    // earlier UI lifecycle phases (SetupEquipPanelParam, GetSelectionNum,
    // SetupCharacterSlotSelectPrefabListParameter), so by the time it
    // fires, all the subscribers for SetItemDetail's SendTrigger cascade
    // are registered.
    //
    // SetItemDetail prime fires from here to populate HEAD OPTION entries
    // on fresh load. Earlier attempts to fire from hkGetSelectionNum were
    // too early — subscribers hadn't registered yet, so the SendTrigger
    // broadcast was lost. Log evidence 2026-04-20:
    //   fresh load → user directly navigates to HEAD OPTION row →
    //   entries empty despite our GetSelectionNum-time prime firing.
    //
    // Fires once per (partsType, session) gated by sortie context active
    // and custom enableHead=true suit equipped.
    //
    // Layout note (from original force attempt): self+0x2b48 = pointer to
    // first UI element (head option slot 0), element+0xb8 = bVar14 flag.
    // DISABLED force — previously set `*(self+0x2b48)+0xb8 = 1` to enable
    // HEAD OPTION cycling UI. That combined with GetSelectionNum=3 and the
    // HeadOptionTableLookup force-1 caused the UI to show a HEAD OPTION row
    // with no entries, which crashed on navigation.
    static void __fastcall hkSetupCharacterSlotSelect(void* self)
    {
        // Log entry (first 10 calls) so we can verify the hook fires
        // during the HEAD OPTION population window.
        {
            static std::uint32_t s_entryCount = 0;
            if (s_entryCount < 10)
            {
                s_entryCount++;
                Log("[SetupCharSlotSelect] entry #%u self=%p\n",
                    s_entryCount, self);
            }
        }

        g_OrigSetupCharSlotSelect(self);

        // SetItemDetail prime — fires AFTER the slot's setup completes,
        // when subscribers are registered and ready to process events.
        // Only fires in sortie prep context (gated on s_sortieContextActive
        // since SetupCharacterSlotSelectPrefabListElement can fire in
        // other dialogs).
        if (!s_sortieContextActive || !g_OrigSetItemDetail || !ResolveApis())
            return;

        auto* qt = reinterpret_cast<std::uint8_t*>(g_GetQuarkSystemTable());
        if (!qt) return;
        auto* q98 = *reinterpret_cast<std::uint8_t**>(qt + 0x98);
        if (!q98) return;
        auto* state = *reinterpret_cast<std::uint8_t**>(q98 + 0x10);
        if (!state) return;

        const std::uint8_t livePartsType = state[0xF8];
        const CustomSuitEntry* entry = nullptr;
        if (!TryGetCustomSuitByPartsType(livePartsType, &entry) || !entry)
            return;
        if (!entry->IsFaceEnabled()) return;

        // Fire on EVERY SetupCharSlotSelect call (no once-per-session
        // throttle).
        //
        // Log evidence 2026-04-20: the FIRST call (dialog init) fires too
        // early — SendTrigger subscribers haven't registered yet and the
        // event is lost. Subsequent calls (user navigates cursor between
        // rows) fire later, when subscribers ARE registered, and the prime
        // actually works.
        //
        // SetupCharSlotSelect fires at most ~3 times per dialog session
        // (once per row: CHARACTER / UNIFORMS / HEAD OPTION as cursor
        // moves), so unthrottled firing is a handful of extra SetItemDetail
        // SendTrigger broadcasts — idempotent on subscribers, lightweight,
        // no visible side effects. The last fire catches the right timing.
        //
        // User confirmed manually: navigating to UNIFORMS populates HEAD
        // OPTION entries via `[SetItemDetail] remap custom 922 -> vanilla
        // 698` in their real UI path. Our fire from here replicates exactly
        // that same call, just without requiring the user to navigate.
        const std::uint8_t pt = state[0xFB];
        const std::uint16_t vanillaFlow =
            (pt < 4) ? s_vanillaFlowIndexByPt[pt] : 698;
        if (vanillaFlow == 0) return;

        static int s_primeCallCount = 0;
        s_primeCallCount++;
        Log("[SetupCharSlotSelect] firing SetItemDetail prime #%d with "
            "vanilla flow=%u (pt=%u, custom partsType=0x%02X) — unthrottled "
            "so we catch the call where subscribers are registered\n",
            s_primeCallCount,
            static_cast<unsigned>(vanillaFlow),
            static_cast<unsigned>(pt),
            static_cast<unsigned>(livePartsType));

        __try
        {
            g_OrigSetItemDetail(self, vanillaFlow);
            Log("[SetupCharSlotSelect] SetItemDetail prime #%d completed\n",
                s_primeCallCount);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("[SetupCharSlotSelect] SetItemDetail prime #%d THREW — "
                "continuing, next call will retry\n",
                s_primeCallCount);
        }
    }

}

bool Install_SuitVariant_Hooks()
{
    if (g_Installed)
    {
        Log("[Hook] SuitVariant: already installed\n");
        return true;
    }

    // ---- HEAD OPTION cycling hooks ----
    //
    // All four HEAD OPTION UI hooks below are installed in PASSTHROUGH MODE
    // as of this build. They do not force the HEAD OPTION row visible for
    // custom suits. See project_head_option.md in memory for the re-enable
    // path (requires EquipIdTable variation-list data-layer injection).
    Log("[SuitVariant] HEAD OPTION UI forcing is DISABLED — menu will not appear for custom suits. enableHead=true still drives face/hair FOVA loading via DoesNeedFaceFova.\n");

    // Hook GetSelectionNum — PASSTHROUGH (was: force 2->3 for custom suits with face)
    if (gAddr.MissionPrep_GetSelectionNum != 0)
    {
        void* addr = ResolveGameAddress(gAddr.MissionPrep_GetSelectionNum);
        if (addr)
        {
            const bool ok = CreateAndEnableHook(
                addr,
                reinterpret_cast<void*>(&hkGetSelectionNum),
                reinterpret_cast<void**>(&g_OrigGetSelectionNum));
            if (ok)
                Log("[Hook] SuitVariant: Hooked GetSelectionNum at %p\n", addr);
        }
    }

    // Hook HasHeadOptions (FUN_1460b9fa0) — force return 1 for custom
    // enableHead=true suits in GetSelectionNum scope. This is the robust
    // fix for the initial-equip HEAD OPTION empty-menu issue: the vanilla
    // flow remap we do in hkGetCurrentSuitFlowIndex might not happen to
    // match a HEAD-OPTION-bearing flow on fresh load (the previous `521`
    // seed didn't; `698` does, but that's empirical), so forcing the
    // check to return true here sidesteps the brittle matching entirely.
    if (gAddr.HasHeadOptions != 0)
    {
        void* addr = ResolveGameAddress(gAddr.HasHeadOptions);
        if (addr)
        {
            const bool ok = CreateAndEnableHook(
                addr,
                reinterpret_cast<void*>(&hkHasHeadOptions),
                reinterpret_cast<void**>(&g_OrigHasHeadOptions));
            if (ok)
                Log("[Hook] SuitVariant: Hooked HasHeadOptions at %p\n", addr);
            else
                Log("[Hook] SuitVariant: HasHeadOptions install FAILED at %p\n", addr);
        }
    }

    // Hook HeadOptionIndexGetter (FUN_1460b4300) — return distinct
    // vanilla head-option equipIds (0x17CA..0x17CE) per index for
    // custom enableHead=true suits. This populates the 8 HEAD OPTION
    // entries in GetCurrentItems' buffer directly, bypassing the
    // subscriber-registration cascade that normally requires the user
    // to open the UNIFORMS sub-menu.
    if (gAddr.HeadOptionIndexGetter != 0)
    {
        void* addr = ResolveGameAddress(gAddr.HeadOptionIndexGetter);
        if (addr)
        {
            const bool ok = CreateAndEnableHook(
                addr,
                reinterpret_cast<void*>(&hkHeadOptionIndexGetter),
                reinterpret_cast<void**>(&g_OrigHeadOptionIndexGetter));
            if (ok)
                Log("[Hook] SuitVariant: Hooked HeadOptionIndexGetter at %p\n", addr);
            else
                Log("[Hook] SuitVariant: HeadOptionIndexGetter install FAILED at %p\n", addr);
        }
    }

    // Hook GetSuitVariation (vtable+0x460) — returns bits for head/body variants
    if (gAddr.GetSuitVariation != 0)
    {
        void* addr = ResolveGameAddress(gAddr.GetSuitVariation);
        if (addr)
        {
            const bool ok = CreateAndEnableHook(
                addr,
                reinterpret_cast<void*>(&hkGetSuitVariation),
                reinterpret_cast<void**>(&g_OrigGetSuitVariation));
            if (ok)
                Log("[Hook] SuitVariant: Hooked GetSuitVariation at %p\n", addr);
        }
    }

    // Hook HeadOptionTableLookup (FUN_1460af810) — force match for custom suits
    if (gAddr.HeadOptionTableLookup != 0)
    {
        void* addr = ResolveGameAddress(gAddr.HeadOptionTableLookup);
        if (addr)
        {
            const bool ok = CreateAndEnableHook(
                addr,
                reinterpret_cast<void*>(&hkHeadOptionTableLookup),
                reinterpret_cast<void**>(&g_OrigHeadOptionTableLookup));
            if (ok)
                Log("[Hook] SuitVariant: Hooked HeadOptionTableLookup at %p\n", addr);
        }
    }

    // Hook SetupCharacterSlotSelectPrefabListElement — force bVar14 for cycling
    if (gAddr.SetupCharacterSlotSelectPrefabListElement != 0)
    {
        void* addr = ResolveGameAddress(gAddr.SetupCharacterSlotSelectPrefabListElement);
        if (addr)
        {
            const bool ok = CreateAndEnableHook(
                addr,
                reinterpret_cast<void*>(&hkSetupCharacterSlotSelect),
                reinterpret_cast<void**>(&g_OrigSetupCharSlotSelect));
            if (ok)
                Log("[Hook] SuitVariant: Hooked SetupCharSlotSelect at %p\n", addr);
        }
    }

    // Hook AddListSuit — per-playerType filtering + head option entries
    if (gAddr.AddListSuit != 0)
    {
        void* addListAddr = ResolveGameAddress(gAddr.AddListSuit);
        if (addListAddr)
        {
            const bool ok = CreateAndEnableHook(
                addListAddr,
                reinterpret_cast<void*>(&hkAddListSuit),
                reinterpret_cast<void**>(&g_OrigAddListSuit)
            );

            if (ok)
            {
                g_HookedAddListSuitAddr = addListAddr;
                Log("[Hook] SuitVariant: Hooked AddListSuit at %p\n", addListAddr);
            }
            else
            {
                Log("[Hook] SuitVariant: MinHook failed for AddListSuit at %p\n", addListAddr);
            }
        }
    }

    // Hook GetCurrentSuitFlowIndex at 0x140955C70 — THE canonical vtable+0x1F8
    // entry point on MissionPreparationSystemImpl (verified via runtime walk
    // 2026-04-20). Body does (a) vanilla-flow cache for SetItemDetail and
    // (b) custom-flow substitution for the EQP badge + IsEnableCurrentSuit
    // chain. See the detailed comment on `hkGetCurrentSuitFlowIndex` above.
    if (gAddr.GetCurrentSuitFlowIndex != 0)
    {
        void* addr = ResolveGameAddress(gAddr.GetCurrentSuitFlowIndex);
        if (addr)
        {
            const bool ok = CreateAndEnableHook(
                addr,
                reinterpret_cast<void*>(&hkGetCurrentSuitFlowIndex),
                reinterpret_cast<void**>(&g_OrigGetCurrentSuitFlowIndex)
            );
            if (ok)
            {
                g_HookedGetCurrentSuitFlowIndexAddr = addr;
                Log("[Hook] SuitVariant: Hooked GetCurrentSuitFlowIndex at %p (vtable+0x1F8 on +0x48)\n", addr);
            }
        }
    }

    // Hook IsEnableCurrentSuit — KEPT as a safety net for the center-panel
    // suit-info gate.
    //
    // Decomp (mgsvtpp.exe.c:2966957):
    //   uVar3 = (**(self+0x48)->vtable+0x1f8)();   // some flowIndex getter
    //   if ((+0x4f0)())
    //     return EDC->vtable+0x478(EDC, uVar3);    // IsEquipDeveloped(flow)
    //   return 1;
    //
    // The (+0x48)->vtable+0x1f8 getter is a DIFFERENT function from our new
    // GetEquipIdFromLoadoutInfo hook (which is on (+0xa0)->vtable+0x180);
    // we don't intercept it, so for custom suits it likely returns 0x400 —
    // and IsEquipDeveloped(0x400) in CurrentSuitQueryHook doesn't match any
    // custom linkedFlowIndex, so the natural chain collapses to false.
    //
    // To avoid that collapse, force true at the IsEnableCurrentSuit entry
    // whenever state[0xF8] is in the custom range. This is a narrow forcing
    // scoped to one function (not a global "is developed?" lie), and it's
    // still needed until we hook the real (+0x48)->vtable+0x1f8 getter.
    if (gAddr.IsEnableCurrentSuit != 0)
    {
        void* addr = ResolveGameAddress(gAddr.IsEnableCurrentSuit);
        if (addr)
        {
            const bool ok = CreateAndEnableHook(
                addr,
                reinterpret_cast<void*>(&hkIsEnableCurrentSuit),
                reinterpret_cast<void**>(&g_OrigIsEnableCurrentSuit)
            );

            if (ok)
            {
                g_HookedIsEnableCurrentSuitAddr = addr;
                Log("[Hook] SuitVariant: Hooked IsEnableCurrentSuit at %p\n", addr);
            }
        }
    }

    // Hook IsEnableCurrentHeadOption — gate HEAD OPTION row/badge on the Lua
    // suit's enableHead flag for custom suits.
    if (gAddr.MissionPrep_IsEnableCurrentHeadOption != 0)
    {
        void* addr = ResolveGameAddress(gAddr.MissionPrep_IsEnableCurrentHeadOption);
        if (addr)
        {
            const bool ok = CreateAndEnableHook(
                addr,
                reinterpret_cast<void*>(&hkIsEnableCurrentHeadOption),
                reinterpret_cast<void**>(&g_OrigIsEnableCurrentHeadOption)
            );

            if (ok)
            {
                g_HookedIsEnableCurrentHeadOptionAddr = addr;
                Log("[Hook] SuitVariant: Hooked IsEnableCurrentHeadOption at %p\n", addr);
            }
            else
            {
                Log("[Hook] SuitVariant: MinHook failed for IsEnableCurrentHeadOption at %p\n", addr);
            }
        }
    }

    // Hook FetchCurrentHeadOptionKey — inject BALACLAVA (0x17CA) as the
    // cached head-option equipId for custom enableHead=true suits so the
    // HEAD OPTION menu populates with BALACLAVA..HEADGEAR entries.
    if (gAddr.FetchCurrentHeadOptionKey != 0)
    {
        void* addr = ResolveGameAddress(gAddr.FetchCurrentHeadOptionKey);
        if (addr)
        {
            const bool ok = CreateAndEnableHook(
                addr,
                reinterpret_cast<void*>(&hkFetchCurrentHeadOptionKey),
                reinterpret_cast<void**>(&g_OrigFetchCurrentHeadOptionKey)
            );

            if (ok)
            {
                g_HookedFetchCurrentHeadOptionKeyAddr = addr;
                Log("[Hook] SuitVariant: Hooked FetchCurrentHeadOptionKey at %p\n", addr);
            }
            else
            {
                Log("[Hook] SuitVariant: MinHook failed for FetchCurrentHeadOptionKey at %p\n", addr);
            }
        }
    }

    // Hook SuitCatalog_FindHeadOptionRow — inject vanilla head-option
    // equipIds (0x17CA..0x17CE) on miss when a custom enableHead=true
    // suit is active, so the HEAD OPTION menu populates with 5 entries
    // (BALACLAVA..HEADGEAR) instead of staying empty.
    if (gAddr.SuitCatalog_FindHeadOptionRow != 0)
    {
        void* addr = ResolveGameAddress(gAddr.SuitCatalog_FindHeadOptionRow);
        if (addr)
        {
            const bool ok = CreateAndEnableHook(
                addr,
                reinterpret_cast<void*>(&hkFindHeadOptionRow),
                reinterpret_cast<void**>(&g_OrigFindHeadOptionRow)
            );

            if (ok)
            {
                g_HookedFindHeadOptionRowAddr = addr;
                Log("[Hook] SuitVariant: Hooked FindHeadOptionRow at %p\n", addr);
            }
            else
            {
                Log("[Hook] SuitVariant: MinHook failed for FindHeadOptionRow at %p\n", addr);
            }
        }
    }

    // Hook SetItemDetail — remap custom flowIndex to vanilla for sortie display
    if (gAddr.SetItemDetail != 0)
    {
        void* addr = ResolveGameAddress(gAddr.SetItemDetail);
        if (addr)
        {
            const bool ok = CreateAndEnableHook(
                addr,
                reinterpret_cast<void*>(&hkSetItemDetail),
                reinterpret_cast<void**>(&g_OrigSetItemDetail)
            );
            if (ok)
            {
                g_HookedSetItemDetailAddr = addr;
                Log("[Hook] SuitVariant: Hooked SetItemDetail at %p\n", addr);
            }
            else
            {
                Log("[Hook] SuitVariant: MinHook failed for SetItemDetail at %p\n", addr);
            }
        }
    }

    // Hook SetupEquipPanelParam — fix blank UNIFORMS panel for custom suits
    if (gAddr.SetupEquipPanelParam != 0)
    {
        void* addr = ResolveGameAddress(gAddr.SetupEquipPanelParam);
        if (addr)
        {
            const bool ok = CreateAndEnableHook(
                addr,
                reinterpret_cast<void*>(&hkSetupEquipPanelParam),
                reinterpret_cast<void**>(&g_OrigSetupEquipPanelParam)
            );

            if (ok)
            {
                g_HookedSetupEquipPanelParamAddr = addr;
                Log("[Hook] SuitVariant: Hooked SetupEquipPanelParam at %p\n", addr);
            }
        }
    }

    g_Installed = (g_HookedAddListSuitAddr != nullptr) ||
                  (g_HookedIsEnableCurrentSuitAddr != nullptr) ||
                  (g_HookedSetupEquipPanelParamAddr != nullptr) ||
                  (g_HookedSetItemDetailAddr != nullptr);
    Log("[Hook] SuitVariant: %s\n", g_Installed ? "OK" : "no hooks installed");
    return true;
}

bool Uninstall_SuitVariant_Hooks()
{
    if (!g_Installed)
        return true;

    if (g_HookedAddListSuitAddr)
    {
        DisableAndRemoveHook(g_HookedAddListSuitAddr);
        g_HookedAddListSuitAddr = nullptr;
    }

    if (g_HookedIsEnableCurrentSuitAddr)
    {
        DisableAndRemoveHook(g_HookedIsEnableCurrentSuitAddr);
        g_HookedIsEnableCurrentSuitAddr = nullptr;
    }

    if (g_HookedIsEnableCurrentHeadOptionAddr)
    {
        DisableAndRemoveHook(g_HookedIsEnableCurrentHeadOptionAddr);
        g_HookedIsEnableCurrentHeadOptionAddr = nullptr;
    }

    if (g_HookedFetchCurrentHeadOptionKeyAddr)
    {
        DisableAndRemoveHook(g_HookedFetchCurrentHeadOptionKeyAddr);
        g_HookedFetchCurrentHeadOptionKeyAddr = nullptr;
    }

    if (g_HookedFindHeadOptionRowAddr)
    {
        DisableAndRemoveHook(g_HookedFindHeadOptionRowAddr);
        g_HookedFindHeadOptionRowAddr = nullptr;
    }

    if (g_HookedSetupEquipPanelParamAddr)
    {
        DisableAndRemoveHook(g_HookedSetupEquipPanelParamAddr);
        g_HookedSetupEquipPanelParamAddr = nullptr;
    }

    if (g_HookedSetItemDetailAddr)
    {
        DisableAndRemoveHook(g_HookedSetItemDetailAddr);
        g_HookedSetItemDetailAddr = nullptr;
    }

    if (g_HookedGetCurrentSuitFlowIndexAddr)
    {
        DisableAndRemoveHook(g_HookedGetCurrentSuitFlowIndexAddr);
        g_HookedGetCurrentSuitFlowIndexAddr = nullptr;
    }

    // Dynamic vtable hooks for named-variant display (lazy-installed).
    if (g_VariantVtableInstalled)
    {
        if (g_IsNamedVariantTarget)
        {
            DisableAndRemoveHook(g_IsNamedVariantTarget);
            g_IsNamedVariantTarget = nullptr;
        }
        if (g_GetVariantDisplayIdTarget)
        {
            DisableAndRemoveHook(g_GetVariantDisplayIdTarget);
            g_GetVariantDisplayIdTarget = nullptr;
        }
        g_OrigIsNamedVariant = nullptr;
        g_OrigGetVariantDisplayId = nullptr;
        g_VariantVtableInstalled = false;
        g_VariantVtableAttempted = false;
    }

    g_OrigAddListSuit = nullptr;
    g_OrigIsEnableCurrentSuit = nullptr;
    g_OrigSetupEquipPanelParam = nullptr;
    g_OrigSetItemDetail = nullptr;
    g_OrigGetCurrentSuitFlowIndex = nullptr;
    g_OrigIsEnableCurrentHeadOption = nullptr;
    g_OrigFetchCurrentHeadOptionKey = nullptr;
    g_OrigFindHeadOptionRow = nullptr;

    // HasHeadOptions hook — uninstall. Address resolved lazily so we need
    // to re-resolve to get the pointer back. Safe even if install failed.
    if (gAddr.HasHeadOptions != 0)
    {
        void* addr = ResolveGameAddress(gAddr.HasHeadOptions);
        if (addr)
            DisableAndRemoveHook(addr);
    }
    g_OrigHasHeadOptions = nullptr;

    // HeadOptionIndexGetter hook — uninstall.
    if (gAddr.HeadOptionIndexGetter != 0)
    {
        void* addr = ResolveGameAddress(gAddr.HeadOptionIndexGetter);
        if (addr)
            DisableAndRemoveHook(addr);
    }
    g_OrigHeadOptionIndexGetter = nullptr;

    // IsDeveloped hook is no longer lazily installed (force was parked), but
    // keep the uninstall path in case a stale install lingers after a reload.
    if (g_HookedIsDevelopedAddr)
    {
        DisableAndRemoveHook(g_HookedIsDevelopedAddr);
        g_HookedIsDevelopedAddr = nullptr;
    }
    g_OrigIsDeveloped = nullptr;
    // Reset back to the seed default. The seed value is now less critical
    // because hkHasHeadOptions forces return 1 for custom enableHead=true
    // suits regardless of flow, but we still seed with 698 (known-good
    // vanilla with HEAD OPTION catalog, empirically verified from log
    // evidence 2026-04-20) instead of 521 (which was empirically WRONG —
    // HasHeadOptions(521) returns false on pt=2). Defensive belt-and-
    // suspenders for any remaining code path that reads this cache.
    s_vanillaFlowIndexByPt[0] = s_vanillaFlowIndexByPt[1] = 698;
    s_vanillaFlowIndexByPt[2] = s_vanillaFlowIndexByPt[3] = 698;
    s_cachedSelf = nullptr;
    s_suitPanels[0] = s_suitPanels[1] = s_suitPanels[2] = nullptr;
    g_Installed = false;

    Log("[Hook] SuitVariant: removed\n");
    return true;
}
