#include "pch.h"

#include "OutfitCommit.h"
#include "OutfitRegistry.h"

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{


    using RequestCommit_t = void (__fastcall*)(
        void*           self,
        std::uint32_t   param2,
        std::uint8_t*   blob,
        std::uint8_t    apply);

    static RequestCommit_t g_OrigRequestCommit = nullptr;
    static bool            g_Installed         = false;


    constexpr std::size_t kBlobOff_PartsType    = 0x00;
    constexpr std::size_t kBlobOff_Selector     = 0x01;
    constexpr std::size_t kBlobOff_Variant      = 0x02;
    constexpr std::size_t kBlobOff_HeadOption   = 0x03;
    constexpr std::size_t kBlobOff_ApplyFlag    = 0xBC;  // u32, observed = 0x81
    constexpr std::size_t kBlobOff_PlayerType   = 0xC0;
    constexpr std::size_t kBlobLogSpan          = 0xC4;


    static bool IsBrokenCustomPattern(const std::uint8_t* blob, std::uint8_t apply)
    {
        return apply == 1
            && blob[0x00] == 0x00
            && blob[0x01] == 0xFF
            && blob[0x02] == 0x00;
    }


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


        if (IsAlreadyResolvedCustom(blob, apply))
        {
            const outfit::OutfitEntry* entry = nullptr;
            if (outfit::TryGetOutfitBySelectorCode(blob[kBlobOff_Selector], &entry) && entry)
            {


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


        if (IsBrokenCustomPattern(blob, apply))
        {
            const std::uint16_t pendingDevId = outfit::GetPendingOutfitDevelopId();
            const outfit::OutfitEntry* entry = nullptr;


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


                const std::uint8_t variantIdx =
                    outfit::GetActiveVariant(livePtUnique->partsType);

                blob[kBlobOff_PartsType]  = livePtUnique->partsType;
                blob[kBlobOff_Selector]   = livePtUnique->selectorCode;
                blob[kBlobOff_Variant]    = variantIdx;

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
