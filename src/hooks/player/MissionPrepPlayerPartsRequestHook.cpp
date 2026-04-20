#include "pch.h"

#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "CustomSuitRegistry.h"
#include "MissionPrepPlayerPartsRequestHook.h"

namespace
{
    using RequestToChangePlayerPartsInMissionPreparationMode_t = void(__fastcall*)(
        void* self,
        int param_2,
        const void* param_3,
        std::uint8_t param_4
        );

    // Player2UtilityImpl::RequestToChangePlayerPartsInMissionPreparationMode
    // 3-arg wrapper at 0x1462b6590. `self` (RCX) is ignored — the function
    // uses GetPlayer2System() internally to fetch the proper inner-player
    // receiver. Safe to call with any self (even nullptr).
    using Player2UtilCommitWrapper_t = void(__fastcall*)(
        void* selfUnused,
        const void* blob,
        std::uint8_t apply
        );

    using GetQuarkSystemTable_t = void* (__fastcall*)();

    static RequestToChangePlayerPartsInMissionPreparationMode_t
        g_OrigRequestToChangePlayerPartsInMissionPreparationMode = nullptr;

    // Resolved lazily on first use — safer than resolving at hook install
    // because the address is used from re-entrant contexts and we want to
    // tolerate missing bindings.
    static Player2UtilCommitWrapper_t g_Player2UtilCommitWrapper = nullptr;

    static GetQuarkSystemTable_t g_GetQuarkSystemTable = nullptr;
    static bool g_Installed = false;

    // Re-entrancy guard: set true while our silent-commit prime is in flight
    // so our own hook body doesn't treat the synthetic commit as a user
    // action and spam duplicate logs / state-writes.
    static thread_local bool g_InSilentCommitPrime = false;

    static constexpr std::size_t kBlobSize = 0xE8;

    struct LivePlayerAppearance
    {
        std::uint8_t  f8_partsType = 0xFF;
        std::uint8_t  f9_camoType = 0xFF;
        std::uint8_t  fb_playerType = 0xFF;
        std::uint16_t fc_faceId = 0xFFFF;
        std::uint16_t fe_headOption = 0xFFFF;
        std::uint8_t  ba0_extra = 0xFF;
    };

    static bool IsCustomLiveAppearance(const LivePlayerAppearance& s)
    {
        return
            (s.f8_partsType >= 0x40 && s.f8_partsType <= 0x7F) ||
            (s.f9_camoType >= 0x80 && s.f9_camoType <= 0xFE);
    }

    static bool ResolveApis()
    {
        if (!g_GetQuarkSystemTable)
        {
            g_GetQuarkSystemTable = reinterpret_cast<GetQuarkSystemTable_t>(
                ResolveGameAddress(gAddr.GetQuarkSystemTable)
                );
        }

        return g_GetQuarkSystemTable != nullptr;
    }

    static bool TryReadLivePlayerAppearance(LivePlayerAppearance& out)
    {
        if (!ResolveApis())
            return false;

        auto* quarkSystemTable = reinterpret_cast<std::uint8_t*>(g_GetQuarkSystemTable());
        if (!quarkSystemTable)
            return false;

        auto* q98 = *reinterpret_cast<std::uint8_t**>(quarkSystemTable + 0x98);
        if (!q98)
            return false;

        auto* state = *reinterpret_cast<std::uint8_t**>(q98 + 0x10);
        if (!state)
            return false;

        out.f8_partsType = state[0xF8];
        out.f9_camoType = state[0xF9];
        out.fb_playerType = state[0xFB];
        out.fc_faceId = *reinterpret_cast<std::uint16_t*>(state + 0xFC);
        out.fe_headOption = *reinterpret_cast<std::uint16_t*>(state + 0xFE);
        out.ba0_extra = state[0xBA0];
        return true;
    }

    static std::uint8_t ReadU8(const std::uint8_t* blob, std::size_t offset)
    {
        return blob[offset];
    }

    static std::uint32_t ReadU32(const std::uint8_t* blob, std::size_t offset)
    {
        std::uint32_t value = 0;
        std::memcpy(&value, blob + offset, sizeof(value));
        return value;
    }

    static std::uint64_t ReadU64(const std::uint8_t* blob, std::size_t offset)
    {
        std::uint64_t value = 0;
        std::memcpy(&value, blob + offset, sizeof(value));
        return value;
    }

    static void LogBlobSummary(
        const char* phase,
        int param_2,
        std::uint8_t param_4,
        const std::uint8_t* blob,
        const LivePlayerAppearance* live)
    {
        // Only log when a custom suit is involved (avoid per-frame spam for vanilla)
        const std::uint8_t b00 = ReadU8(blob, 0x00);
        const bool isCustomBlob = (b00 >= 0x40 && b00 <= 0x7F);
        const bool isCustomLive = live && IsCustomLiveAppearance(*live);
        if (!isCustomBlob && !isCustomLive)
            return;

        const std::uint8_t b01 = ReadU8(blob, 0x01);
        const std::uint8_t b02 = ReadU8(blob, 0x02);
        const std::uint8_t b03 = ReadU8(blob, 0x03);
        const std::uint32_t flags = ReadU32(blob, 0xBC);

        if (live)
        {
            Log(
                "[MissionPrepPartsReq] %s idx=%d apply=%u "
                "blob{00=%02X 01=%02X 02=%02X 03=%02X flags=%08X} "
                "live{f8=%02X f9=%02X fb=%02X fc=%04X fe=%04X}\n",
                phase, param_2, static_cast<unsigned>(param_4),
                static_cast<unsigned>(b00), static_cast<unsigned>(b01),
                static_cast<unsigned>(b02), static_cast<unsigned>(b03),
                static_cast<unsigned>(flags),
                static_cast<unsigned>(live->f8_partsType),
                static_cast<unsigned>(live->f9_camoType),
                static_cast<unsigned>(live->fb_playerType),
                static_cast<unsigned>(live->fc_faceId),
                static_cast<unsigned>(live->fe_headOption)
            );
        }
        else
        {
            Log(
                "[MissionPrepPartsReq] %s idx=%d apply=%u "
                "blob{00=%02X 01=%02X 02=%02X 03=%02X flags=%08X}\n",
                phase, param_2, static_cast<unsigned>(param_4),
                static_cast<unsigned>(b00), static_cast<unsigned>(b01),
                static_cast<unsigned>(b02), static_cast<unsigned>(b03),
                static_cast<unsigned>(flags)
            );
        }
    }
}

static void __fastcall hkRequestToChangePlayerPartsInMissionPreparationMode(
    void* self,
    int param_2,
    const void* param_3,
    std::uint8_t param_4)
{
    if (!param_3)
    {
        g_OrigRequestToChangePlayerPartsInMissionPreparationMode(self, param_2, param_3, param_4);
        return;
    }

    // Silent-commit prime: bypass our rewrite logic (blob is already
    // well-formed by TriggerSilentSuitCommitFromLiveState) AND skip
    // active-suit state updates so the prime doesn't race with the
    // user's actual selection. Just forward to original.
    if (g_InSilentCommitPrime)
    {
        Log("[MissionPrepPartsReq] silent-commit prime pass-through\n");
        g_OrigRequestToChangePlayerPartsInMissionPreparationMode(
            self, param_2, param_3, param_4);
        return;
    }

    std::uint8_t blobCopy[kBlobSize]{};
    std::memcpy(blobCopy, param_3, kBlobSize);
    auto* blob = blobCopy;

    LivePlayerAppearance before{};
    const bool haveBefore = TryReadLivePlayerAppearance(before);

    LogBlobSummary("in ", param_2, param_4, blob, haveBefore ? &before : nullptr);

    const bool looksLikeBrokenCustomSuit =
        param_4 == 1 &&
        blob[0x00] == 0x00 &&
        blob[0x01] == 0xFF &&
        blob[0x02] == 0x00;

    // Already-filled custom suit commit — happens when the item selector
    // submits a variant directly (blob pre-filled with custom partsType +
    // selectorCode). The "broken" rewrite path below doesn't fire for this
    // case, so blob[0x02] stays at 0 → state[0xBA0] gets wrong variantIndex
    // → game renders the wrong sub-slot as "currently equipped".
    //
    // Resolve the entry by selectorCode (the most reliable key — it's unique
    // per variant and directly present in blob[0x01]) and patch blob[0x02]
    // before the original is invoked.
    const bool isAlreadyFilledCustom =
        param_4 == 1 &&
        !looksLikeBrokenCustomSuit &&
        blob[0x00] >= 0x40 && blob[0x00] <= 0x7F &&
        blob[0x01] >= 0x80 && blob[0x01] <= 0xFE;

    if (isAlreadyFilledCustom)
    {
        const CustomSuitEntry* entry = nullptr;
        if (TryGetCustomSuitBySelectorCode(blob[0x01], &entry) && entry)
        {
            const std::uint8_t oldVariantIdx = blob[0x02];
            blob[0x02] = entry->variantIndex;

            if (oldVariantIdx != entry->variantIndex)
            {
                Log(
                    "[MissionPrepPartsReq] patch variantIndex selector=0x%02X "
                    "blob[0x02] %02X -> %02X (developId=%u)\n",
                    static_cast<unsigned>(blob[0x01]),
                    static_cast<unsigned>(oldVariantIdx),
                    static_cast<unsigned>(entry->variantIndex),
                    static_cast<unsigned>(entry->linkedDevelopId)
                );
            }

            // Default head-option workaround (2026-04-20):
            //
            // The sortie-prep HEAD OPTION menu does NOT populate entries
            // for custom enableHead=true suits — the game's head-option
            // enumerator dispatches through an unknown path that we can't
            // hook via decomp analysis alone. As a compromise, when a
            // custom enableHead=true suit is committed and the current
            // head-option byte is 0 (no head), force it to 1 (BALACLAVA
            // index in the vanilla suit's head-option enumeration) so the
            // user at least wears a head-option on-body instead of no
            // head. Users can't cycle through options in the menu, but
            // they get a functional default.
            std::uint16_t defaultHeadOption =
                haveBefore ? before.fe_headOption : 0x0000;
            if (entry->IsFaceEnabled() &&
                (defaultHeadOption == 0 || defaultHeadOption == 0xFFFF))
            {
                defaultHeadOption = 1;
                blob[0x03] = 1;
                Log(
                    "[MissionPrepPartsReq] default head option for custom enableHead=true "
                    "selector=0x%02X -> blob[0x03]=01\n",
                    static_cast<unsigned>(blob[0x01])
                );
            }

            SetActiveCustomSuit(
                entry->linkedDevelopId,
                entry->playerType,
                entry->customPartsType,
                entry->customSelectorCode,
                haveBefore ? before.fc_faceId : 0xFFFF,
                defaultHeadOption
            );

            ClearPendingCustomSuitDevelopId();
        }
    }

    if (looksLikeBrokenCustomSuit)
    {
        const std::uint16_t pendingDevelopId = GetPendingCustomSuitDevelopId();

        // Cache the last meaningful non-custom head option before the custom swap wipes it.
        if (haveBefore &&
            !IsCustomLiveAppearance(before) &&
            before.fe_headOption != 0 &&
            before.fe_headOption != 0xFFFF)
        {
            RememberPreservedHeadOption(before.fb_playerType, before.fe_headOption);
        }

        const CustomSuitEntry* entry = nullptr;
        if (pendingDevelopId != 0 &&
            TryGetCustomSuitByDevelopIdForPlayerType(
                pendingDevelopId,
                haveBefore ? before.fb_playerType : 0xFF,
                &entry) &&
            entry)
        {
            PreservedAppearanceState preserved{};
            const bool havePreserved =
                TryGetPreservedAppearance(entry->playerType, preserved);

            std::uint8_t resolvedHead = 0x00;
            if (entry->IsFaceEnabled())
            {
                if (havePreserved &&
                    preserved.headOption != 0 &&
                    preserved.headOption != 0xFFFF)
                {
                    resolvedHead =
                        static_cast<std::uint8_t>(preserved.headOption & 0xFF);
                }
                else if (haveBefore &&
                    before.fe_headOption != 0 &&
                    before.fe_headOption != 0xFFFF)
                {
                    resolvedHead =
                        static_cast<std::uint8_t>(before.fe_headOption & 0xFF);
                }
                else
                {
                    resolvedHead = blob[0x03];
                }
            }

            blob[0x00] = entry->customPartsType;
            blob[0x01] = entry->customSelectorCode;
            // blob[0x02] → state[0xBA0] = committed variantIndex. The game's
            // IsPlayerEquip and UI renderer read this to identify which
            // sub-slot of the group is live. We write the VARIANT'S index
            // (0 for base, 1..N for variants) rather than carrying over the
            // previous value — otherwise cycling doesn't actually commit a
            // new variant, just reapplies the same one.
            blob[0x02] = entry->variantIndex;
            blob[0x03] = resolvedHead;

            *reinterpret_cast<std::uint32_t*>(blob + 0xBC) = 0x81;
            blob[0xC0] = entry->playerType;

            Log(
                "[MissionPrepPartsReq] rewrite developId=%u -> parts=%02X selector=%02X aux=%02X head=%02X type=%02X\n",
                static_cast<unsigned>(pendingDevelopId),
                static_cast<unsigned>(blob[0x00]),
                static_cast<unsigned>(blob[0x01]),
                static_cast<unsigned>(blob[0x02]),
                static_cast<unsigned>(blob[0x03]),
                static_cast<unsigned>(blob[0xC0])
            );

            SetActiveCustomSuit(
                pendingDevelopId,
                entry->playerType,
                entry->customPartsType,
                entry->customSelectorCode,
                haveBefore ? before.fc_faceId : 0xFFFF,
                static_cast<std::uint16_t>(resolvedHead)
            );

            ClearPendingCustomSuitDevelopId();
        }
        else
        {
            Log("[MissionPrepPartsReq] broken custom blob but no pending custom developId\n");
        }
    }

    g_OrigRequestToChangePlayerPartsInMissionPreparationMode(self, param_2, blob, param_4);

    LivePlayerAppearance after{};
    const bool haveAfter = TryReadLivePlayerAppearance(after);
    LogBlobSummary("out", param_2, param_4, blob, haveAfter ? &after : nullptr);

    if (haveAfter && !IsCustomLiveAppearance(after))
        ClearActiveCustomSuit();
}

namespace
{
    // Resolve the Player2UtilityImpl commit wrapper at 0x1462b6590 lazily.
    // Returns true iff g_Player2UtilCommitWrapper is set.
    static bool ResolvePlayer2UtilCommitWrapper()
    {
        if (g_Player2UtilCommitWrapper)
            return true;

        void* addr = ResolveGameAddress(gAddr.Player2UtilityImpl_CommitWrapper);
        if (!addr)
        {
            // Don't log on every call — just once per session via static flag.
            static bool s_warned = false;
            if (!s_warned)
            {
                s_warned = true;
                Log("[SilentCommit] Player2UtilityImpl_CommitWrapper address "
                    "not bound (build mismatch?) — silent commit disabled\n");
            }
            return false;
        }

        g_Player2UtilCommitWrapper =
            reinterpret_cast<Player2UtilCommitWrapper_t>(addr);
        Log("[SilentCommit] resolved Player2UtilityImpl_CommitWrapper @ %p\n",
            addr);
        return true;
    }
}

// Invoke Player2UtilityImpl::RequestToChangePlayerPartsInMissionPreparationMode
// (the 3-arg wrapper at 0x1462b6590) with the provided blob. The wrapper
// ignores its `self` parameter and calls GetPlayer2System() internally to
// resolve the proper receiver — so this is safe to call from any context
// where the 4-arg AttackActionImpl entry point would crash.
//
// Our existing hook on 0x14973DA60 (AttackActionImpl) will still fire on the
// inner dispatch, but g_InSilentCommitPrime suppresses the duplicate
// bookkeeping.
//
// Parameter notes:
//   `missionPrepCallbackSelf` / `idx` are kept in the signature for ABI
//   stability with existing callers but are NOT used — both can be nullptr/0.
//   Only `blob` and `apply` matter.
bool TriggerSilentSuitCommit(
    void* missionPrepCallbackSelf,
    int idx,
    const void* blob,
    std::uint8_t apply)
{
    (void)missionPrepCallbackSelf;
    (void)idx;

    if (!blob)
    {
        Log("[TriggerSilentSuitCommit] null blob — skip\n");
        return false;
    }
    if (!ResolvePlayer2UtilCommitWrapper())
        return false;

    Log("[TriggerSilentSuitCommit] firing wrapper(blob{00=%02X 01=%02X "
        "02=%02X 03=%02X}, apply=%u)\n",
        static_cast<unsigned>(static_cast<const std::uint8_t*>(blob)[0x00]),
        static_cast<unsigned>(static_cast<const std::uint8_t*>(blob)[0x01]),
        static_cast<unsigned>(static_cast<const std::uint8_t*>(blob)[0x02]),
        static_cast<unsigned>(static_cast<const std::uint8_t*>(blob)[0x03]),
        static_cast<unsigned>(apply));

    g_InSilentCommitPrime = true;
    __try
    {
        g_Player2UtilCommitWrapper(nullptr, blob, apply);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        g_InSilentCommitPrime = false;
        Log("[TriggerSilentSuitCommit] EXCEPTION during wrapper call — "
            "aborting prime, game may be in an unsuitable phase\n");
        return false;
    }
    g_InSilentCommitPrime = false;
    return true;
}

// Convenience entry point for the sortie-prep initial-equip HEAD OPTION
// prime: read live Quark state, build a well-formed commit blob matching the
// currently-equipped custom suit, and fire the silent wrapper commit. Builds
// the blob in the exact shape our `hkRequestToChangePlayerPartsInMissionPreparationMode`
// "isAlreadyFilledCustom" branch expects, so the hook is a no-op on its pass
// through.
//
// Returns false if:
//   - the wrapper hasn't been resolved yet (build mismatch), or
//   - no custom suit is currently equipped (nothing to prime), or
//   - the live state read fails.
bool TriggerSilentSuitCommitFromLiveState()
{
    if (!ResolvePlayer2UtilCommitWrapper())
        return false;

    LivePlayerAppearance live{};
    if (!TryReadLivePlayerAppearance(live))
    {
        Log("[SilentCommitLive] failed to read live player appearance — "
            "skip prime\n");
        return false;
    }

    // Only prime for registered custom suits — priming a vanilla suit
    // would round-trip through the commit path for no gain and risk
    // subtle UI flicker.
    const CustomSuitEntry* entry = nullptr;
    if (!TryGetCustomSuitByPartsType(live.f8_partsType, &entry) || !entry)
    {
        // Silent — not an error, just "no custom suit to prime".
        return false;
    }

    // Build commit blob. Strategy (2026-04-20, third iteration):
    //
    // Replicate the user's manual workflow exactly: pick a DIFFERENT suit
    // first, then re-pick the custom suit. Two consecutive silent commits:
    //
    //   Commit 1: VANILLA Sneaking Suit blob `{00=<vanillaPartsType>,
    //             01=<vanillaSelector>, 02=0, 03=0, flags=0x81,
    //             C0=playerType}`. Forces the game's commit function to
    //             see a real state transition (current custom →
    //             sneaking suit) and run the full update path on the
    //             AttackActionImpl flags.
    //
    //   Commit 2: CUSTOM blob `{00=<partsType>, 01=0xFF, 02=<variant>,
    //             03=<head>, flags=0x81, C0=playerType}`. Re-equips the
    //             custom suit. State transitions back. Flags are still
    //             set from commit 1.
    //
    // This mirrors exactly what the user does manually when "pick any suit,
    // pick custom back" unblocks HEAD OPTION entries. No more guessing
    // about which single-commit pattern triggers what — if the user's
    // manual path works, the same path done silently must work too.
    //
    // Vanilla proxy:
    //   playerType=0,1 (Snake/DD): partsType=0x00 ("Sneaking Suit"),
    //     selector=0xFF.
    //   playerType=2 (Female/Quiet): partsType=0x13 ("Snake Quiet"/ SSD
    //     vanilla), selector=0xFF. Chosen from log evidence where the
    //     user picked partsType=0x13 manually and it worked.
    //   playerType=3+: fall back to 0x00.
    //
    // Our pass-through `g_InSilentCommitPrime` keeps our own hook from
    // rewriting the synthetic blobs.
    std::uint8_t vanillaParts = 0x00;
    std::uint8_t vanillaSelector = 0xFF;
    if (live.fb_playerType == 2)
    {
        vanillaParts = 0x13;   // Sneaking Suit (female variant per log)
        vanillaSelector = 0x20; // Observed selector for this vanilla on pt=2
    }

    // ---- Commit 1: vanilla suit ----
    std::uint8_t blob1[kBlobSize]{};
    blob1[0x00] = vanillaParts;
    blob1[0x01] = vanillaSelector;
    blob1[0x02] = 0x01;  // variantIndex 1 observed in user's manual log
    blob1[0x03] = 0x00;
    *reinterpret_cast<std::uint32_t*>(blob1 + 0xBC) = 0x81;
    blob1[0xC0] = live.fb_playerType;

    // ---- Commit 2: custom suit (our target) ----
    std::uint8_t blob2[kBlobSize]{};
    blob2[0x00] = entry->customPartsType;
    blob2[0x01] = 0xFF;  // UI sentinel (matches user's manual pick pattern)
    blob2[0x02] = entry->variantIndex;

    std::uint8_t headByte = 0;
    if (live.fe_headOption != 0 && live.fe_headOption != 0xFFFF)
    {
        headByte = static_cast<std::uint8_t>(live.fe_headOption & 0xFF);
    }
    else if (entry->IsFaceEnabled())
    {
        headByte = 1;
    }
    blob2[0x03] = headByte;

    *reinterpret_cast<std::uint32_t*>(blob2 + 0xBC) = 0x81;
    blob2[0xC0] = entry->playerType;

    Log("[SilentCommitLive] two-step prime for partsType=0x%02X "
        "(developId=%u enableHead=%d): commit1 = vanilla parts=0x%02X "
        "sel=0x%02X, commit2 = custom parts=0x%02X sel=0xFF head=0x%02X\n",
        static_cast<unsigned>(entry->customPartsType),
        static_cast<unsigned>(entry->linkedDevelopId),
        entry->IsFaceEnabled() ? 1 : 0,
        static_cast<unsigned>(vanillaParts),
        static_cast<unsigned>(vanillaSelector),
        static_cast<unsigned>(entry->customPartsType),
        static_cast<unsigned>(blob2[0x03]));

    const bool ok1 = TriggerSilentSuitCommit(nullptr, 0, blob1, 1);
    if (!ok1)
    {
        Log("[SilentCommitLive] commit 1 (vanilla) failed — aborting "
            "two-step prime, state may be in a limbo config\n");
        return false;
    }

    const bool ok2 = TriggerSilentSuitCommit(nullptr, 0, blob2, 1);
    if (!ok2)
    {
        Log("[SilentCommitLive] commit 2 (custom) failed — user-visible "
            "state may be WRONG (stuck on vanilla), but commit 1's flags "
            "were set\n");
        return false;
    }

    return true;
}

bool Install_MissionPrepPlayerPartsRequest_Hook()
{
    if (g_Installed)
    {
        Log("[Hook] MissionPrepPartsReq: already installed\n");
        return true;
    }

    if (!ResolveApis())
    {
        Log("[Hook] MissionPrepPartsReq: failed to resolve GetQuarkSystemTable\n");
        return false;
    }

    void* target = ResolveGameAddress(gAddr.MissionPrep_RequestToChangePlayerPartsInMissionPreparationMode);
    if (!target)
    {
        Log("[Hook] MissionPrepPartsReq: failed to resolve target\n");
        return false;
    }

    const bool ok = CreateAndEnableHook(
        target,
        reinterpret_cast<void*>(&hkRequestToChangePlayerPartsInMissionPreparationMode),
        reinterpret_cast<void**>(&g_OrigRequestToChangePlayerPartsInMissionPreparationMode)
    );

    Log("[Hook] MissionPrepPartsReq: %s\n", ok ? "OK" : "FAIL");
    g_Installed = ok;
    return ok;
}

bool Uninstall_MissionPrepPlayerPartsRequest_Hook()
{
    if (!g_Installed)
        return true;

    if (void* target = ResolveGameAddress(gAddr.MissionPrep_RequestToChangePlayerPartsInMissionPreparationMode))
        DisableAndRemoveHook(target);

    g_OrigRequestToChangePlayerPartsInMissionPreparationMode = nullptr;
    g_GetQuarkSystemTable = nullptr;
    g_Installed = false;

    Log("[Hook] MissionPrepPartsReq: removed\n");
    return true;
}