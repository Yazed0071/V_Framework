#include "pch.h"

#include "OutfitCommit.h"
#include "OutfitRegistry.h"

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    // Verified arg layout (mgsvtpp.exe_Addresses.txt:128150140-128150152):
    //   RCX = self (Player2UtilityImpl*)
    //   RDX = int param2 (saved into R14D)
    //   R8  = blob*  (saved into R9 then used as memcpy source)
    //   R9B = u8 apply (saved into R15D)
    using RequestCommit_t = void (__fastcall*)(
        void*           self,
        std::uint32_t   param2,
        std::uint8_t*   blob,
        std::uint8_t    apply);

    static RequestCommit_t g_OrigRequestCommit = nullptr;
    static bool            g_Installed         = false;

    // Decoded blob field offsets currently treated as PROVISIONAL.
    // The decomp doesn't expose layout; we log raw bytes on every
    // commit to validate at runtime. Confirmed at runtime in Phase 3.
    constexpr std::size_t kBlobOff_PartsType    = 0x00;
    constexpr std::size_t kBlobOff_Selector     = 0x01;
    constexpr std::size_t kBlobOff_Variant      = 0x02;
    constexpr std::size_t kBlobOff_HeadOption   = 0x03;
    constexpr std::size_t kBlobOff_ApplyFlag    = 0xBC;  // u32, observed = 0x81
    constexpr std::size_t kBlobOff_PlayerType   = 0xC0;
    constexpr std::size_t kBlobLogSpan          = 0xC4;

    // Identify a commit that the ItemSelector failed to fully resolve
    // (custom suit selected via the developId-bridge UI path). The
    // game writes blob[0]=0x00, blob[1]=0xFF, blob[2]=0x00 to mean
    // "reserved for the resolver to fill". On retail with no resolver
    // installed, this pattern is what reaches commit when a custom
    // outfit is picked through R&D-style selectors.
    static bool IsBrokenCustomPattern(const std::uint8_t* blob, std::uint8_t apply)
    {
        return apply == 1
            && blob[0x00] == 0x00
            && blob[0x01] == 0xFF
            && blob[0x02] == 0x00;
    }

    // Already-resolved custom pattern: blob carries our partsType and
    // selector directly. We re-confirm variant/head fields and route
    // ActiveCustomSuit state.
    static bool IsAlreadyResolvedCustom(const std::uint8_t* blob, std::uint8_t apply)
    {
        return apply == 1
            && blob[0x00] >= outfit::kCustomPartsTypeStart
            && blob[0x00] <= outfit::kCustomPartsTypeEnd
            && blob[0x01] >= outfit::kCustomSelectorStart
            && blob[0x01] <= outfit::kCustomSelectorEnd;
    }

    static void LogBlobSnapshot(const char* label, std::uint8_t* blob)
    {
        if (!blob)
        {
            Log("[OutfitCommit] %s blob=null\n", label);
            return;
        }

        // Read defensively — the blob CAN be in a stack frame about
        // to be popped on the caller side; SEH-guard to be safe.
        std::uint8_t b[kBlobLogSpan] = {};
        __try
        {
            for (std::size_t i = 0; i < kBlobLogSpan; ++i) b[i] = blob[i];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("[OutfitCommit] %s blob read SEH'd\n", label);
            return;
        }

        Log("[OutfitCommit] %s blob[0..3]=%02X %02X %02X %02X "
            "blob[0xBC..0xBF]=%02X %02X %02X %02X blob[0xC0]=%02X\n",
            label,
            b[0x00], b[0x01], b[0x02], b[0x03],
            b[0xBC], b[0xBD], b[0xBE], b[0xBF],
            b[0xC0]);
    }

    static void __fastcall hkRequestCommit(
        void* self, std::uint32_t param2, std::uint8_t* blob, std::uint8_t apply)
    {
        if (!blob)
        {
            if (g_OrigRequestCommit) g_OrigRequestCommit(self, param2, blob, apply);
            return;
        }

        LogBlobSnapshot("pre", blob);

        // Path 1: blob arrives as a fully-resolved custom — re-confirm
        // ActiveCustomSuit registry and pass through. Variant cycling
        // (blob[0x02]) and head option (blob[0x03]) are honored verbatim
        // in Phase 2; Phase 3 will validate against the entry's variant
        // and head-option arrays.
        if (IsAlreadyResolvedCustom(blob, apply))
        {
            const outfit::OutfitEntry* entry = nullptr;
            if (outfit::TryGetOutfitBySelectorCode(blob[kBlobOff_Selector], &entry) && entry)
            {
                // Phase 3: publish active variant index so the
                // runtime-parts hooks pick the right variant paths.
                const std::uint8_t variantIdx = blob[kBlobOff_Variant];
                outfit::SetActiveVariant(entry->partsType, variantIdx);

                Log("[OutfitCommit] resolved-custom commit: developId=%u "
                    "partsType=0x%02X selector=0x%02X variant=%u head=%u\n",
                    static_cast<unsigned>(entry->developId),
                    static_cast<unsigned>(entry->partsType),
                    static_cast<unsigned>(entry->selectorCode),
                    static_cast<unsigned>(variantIdx),
                    static_cast<unsigned>(blob[kBlobOff_HeadOption]));
            }

            Log("[OutfitCommit] calling orig (resolved path)...\n");
            g_OrigRequestCommit(self, param2, blob, apply);
            Log("[OutfitCommit] orig returned\n");
            return;
        }

        // Path 2: broken-custom pattern. Resolves via two routes:
        //   (a) Pending developId published by OutfitItemSelector on a
        //       UI click. (Highest precedence — the user's actual click.)
        //   (b) Live-player-type fallback — when no pending devId is
        //       set (selector hook didn't fire / didn't match), AND the
        //       live player type has exactly ONE registered outfit, use
        //       that outfit. Single-outfit testing flow benefits hugely:
        //       the user can be on Snake / DDFemale / etc. and clicking
        //       a "looks-like-the-mod" UNIFORMS row will route to the
        //       only registered outfit for their character.
        // If both routes miss, NEUTRALIZE the commit by rewriting the
        // blob to safe vanilla NORMAL — passing the broken blob through
        // makes the game write selector=0xFF into player state, which
        // breaks the mesh load and causes infinite-loading or invisible
        // character symptoms.
        if (IsBrokenCustomPattern(blob, apply))
        {
            const std::uint16_t pendingDevId = outfit::GetPendingOutfitDevelopId();
            const outfit::OutfitEntry* entry = nullptr;

            // (b) live-player-type fallback search — only used when
            // (a) misses. We do the scan up front to log a helpful
            // diagnostic regardless of which path wins.
            const std::uint8_t livePT = outfit::ReadLivePlayerType();
            const outfit::OutfitEntry* livePtUnique = nullptr;
            std::size_t livePtCount = 0;
            if (livePT != 0xFF)
            {
                const outfit::OutfitEntry* all[outfit::kMaxOutfits] = {};
                const std::size_t n = outfit::GetAllOutfits(all, outfit::kMaxOutfits);
                for (std::size_t i = 0; i < n; ++i)
                {
                    if (!all[i]) continue;
                    if (all[i]->playerType != livePT) continue;
                    ++livePtCount;
                    if (livePtCount == 1) livePtUnique = all[i];
                }
            }

            if (pendingDevId != 0
                && outfit::TryGetOutfitByDevelopId(pendingDevId, &entry)
                && entry)
            {
                const std::uint8_t variantIdx =
                    outfit::GetActiveVariant(entry->partsType);

                blob[kBlobOff_PartsType]  = entry->partsType;
                blob[kBlobOff_Selector]   = entry->selectorCode;
                blob[kBlobOff_Variant]    = variantIdx;
                // Leave blob[0x03] (head option) alone.
                *reinterpret_cast<std::uint32_t*>(blob + kBlobOff_ApplyFlag) = 0x81;
                blob[kBlobOff_PlayerType] = entry->playerType;

                Log("[OutfitCommit] rewrote BROKEN-custom blob: "
                    "developId=%u partsType=0x%02X selector=0x%02X variant=%u playerType=%u\n",
                    static_cast<unsigned>(entry->developId),
                    static_cast<unsigned>(entry->partsType),
                    static_cast<unsigned>(entry->selectorCode),
                    static_cast<unsigned>(variantIdx),
                    static_cast<unsigned>(entry->playerType));

                outfit::ClearPendingOutfitDevelopId();
            }
            else if (livePtCount == 1 && livePtUnique)
            {
                // (b) Live-player-type fallback. Exactly one registered
                // outfit matches the live character — assume that's
                // what the user wanted. Most common in single-mod
                // testing where the user has e.g. one Jill outfit
                // for DDFemale and clicks any UNIFORMS row.
                const std::uint8_t variantIdx =
                    outfit::GetActiveVariant(livePtUnique->partsType);

                blob[kBlobOff_PartsType]  = livePtUnique->partsType;
                blob[kBlobOff_Selector]   = livePtUnique->selectorCode;
                blob[kBlobOff_Variant]    = variantIdx;
                // Leave blob[0x03] (head option) alone.
                *reinterpret_cast<std::uint32_t*>(blob + kBlobOff_ApplyFlag) = 0x81;
                blob[kBlobOff_PlayerType] = livePtUnique->playerType;

                Log("[OutfitCommit] rewrote BROKEN-custom blob via "
                    "live-PT fallback: livePT=%u developId=%u "
                    "partsType=0x%02X selector=0x%02X variant=%u\n",
                    static_cast<unsigned>(livePT),
                    static_cast<unsigned>(livePtUnique->developId),
                    static_cast<unsigned>(livePtUnique->partsType),
                    static_cast<unsigned>(livePtUnique->selectorCode),
                    static_cast<unsigned>(variantIdx));
            }
            else
            {
                // Setting apply=0 alone is insufficient — the game's
                // commit pipeline copies blob bytes into player state
                // regardless of apply (apply only gates visual
                // transitions). If we leave selector=0xFF in the
                // blob, the game writes playerCamoType=0xFF, then
                // LoadPartsNew tries to load camo type 0xFF →
                // invalid → infinite loading.
                //
                // Rewrite the blob to safe vanilla NORMAL values so
                // the propagated state is valid:
                //   partsType=0 (vanilla NORMAL)
                //   selector=0  (no specific selector)
                //   variant=0
                //   head=0
                // ApplyFlag and playerType left as-is.
                Log("[OutfitCommit] BROKEN-custom unresolvable "
                    "(pendingDevId=%u livePT=%u livePtCount=%zu — "
                    "0=no Quark, !=1 means ambiguous). Rewriting blob "
                    "to safe vanilla NORMAL.\n",
                    static_cast<unsigned>(pendingDevId),
                    static_cast<unsigned>(livePT),
                    livePtCount);
                blob[kBlobOff_PartsType] = 0;
                blob[kBlobOff_Selector]  = 0;
                blob[kBlobOff_Variant]   = 0;
                blob[kBlobOff_HeadOption] = 0;
                apply = 0;
            }
        }

        // Path 3: vanilla — passthrough (or apply=0 from path 2).
        Log("[OutfitCommit] calling orig (apply=%u)...\n",
            static_cast<unsigned>(apply));
        g_OrigRequestCommit(self, param2, blob, apply);
        Log("[OutfitCommit] orig returned\n");
    }
}

namespace outfit
{
    bool Install_OutfitCommit_Hook()
    {
        if (g_Installed) return true;

        void* target = ResolveGameAddress(
            gAddr.MissionPrep_RequestToChangePlayerPartsInMissionPreparationMode);
        if (!target)
        {
            Log("[OutfitCommit] target unresolved; module disabled\n");
            return false;
        }

        g_Installed = CreateAndEnableHook(
            target,
            reinterpret_cast<void*>(&hkRequestCommit),
            reinterpret_cast<void**>(&g_OrigRequestCommit));

        Log("[OutfitCommit] installed: %s (target=%p)\n",
            g_Installed ? "OK" : "FAIL", target);
        return g_Installed;
    }

    void Uninstall_OutfitCommit_Hook()
    {
        if (!g_Installed) return;

        if (void* t = ResolveGameAddress(
                gAddr.MissionPrep_RequestToChangePlayerPartsInMissionPreparationMode))
            DisableAndRemoveHook(t);

        g_OrigRequestCommit = nullptr;
        g_Installed         = false;
        Log("[OutfitCommit] removed\n");
    }
}
