#include "pch.h"

#include "OutfitRegistry.h"
#include "ShadowState.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "AddressSet.h"
#include "log.h"
#include <HookUtils.h>

namespace
{
    using outfit::kCustomPartsTypeStart;
    using outfit::kCustomPartsTypeEnd;
    using outfit::kCustomSelectorStart;
    using outfit::kCustomSelectorEnd;
    using outfit::kMaxOutfits;
    using outfit::kPlayerTypeMax;
    using outfit::OutfitEntry;
    using outfit::OutfitDefinition;
    using outfit::OutfitPlayerTypeData;

    using GetQuarkSystemTable_t = void* (__fastcall*)();

    static std::array<OutfitEntry, kMaxOutfits> g_Entries{};
    static std::mutex                            g_Mutex;
    static GetQuarkSystemTable_t                 g_GetQuarkSystemTable = nullptr;


    static constexpr std::size_t kActiveVariantSize =
        static_cast<std::size_t>(kCustomPartsTypeEnd) -
        static_cast<std::size_t>(kCustomPartsTypeStart) + 1;
    static std::uint8_t g_ActiveVariant[kActiveVariantSize] = {};


    static std::uint16_t g_PendingDevelopId            = 0;
    static std::uint16_t g_PendingHeadOptionEquipId    = 0;
    static bool          g_SupplyDropClickLatch        = false;
    static std::uint16_t g_PendingSupplyDropDevelopId  = 0;
    static std::uint8_t  g_PendingSupplyDropVariantIdx = 0;

    static bool ResolveQuarkApi()
    {
        if (!g_GetQuarkSystemTable)
        {
            g_GetQuarkSystemTable = reinterpret_cast<GetQuarkSystemTable_t>(
                ResolveGameAddress(gAddr.GetQuarkSystemTable));
        }
        return g_GetQuarkSystemTable != nullptr;
    }

    static std::uint8_t* GetQuarkLiveState()
    {
        if (!ResolveQuarkApi()) return nullptr;
        auto* qt = reinterpret_cast<std::uint8_t*>(g_GetQuarkSystemTable());
        if (!qt) return nullptr;
        auto* q98 = *reinterpret_cast<std::uint8_t**>(qt + 0x98);
        if (!q98) return nullptr;
        return *reinterpret_cast<std::uint8_t**>(q98 + 0x10);
    }

    static bool IsCustomPartsType(std::uint8_t v)
    {
        return v >= kCustomPartsTypeStart && v <= kCustomPartsTypeEnd;
    }

    static bool IsCustomSelector(std::uint8_t v)
    {
        return v >= kCustomSelectorStart && v <= kCustomSelectorEnd;
    }


    static std::uint8_t AllocatePartsType_NoLock(std::uint8_t hint)
    {
        if (IsCustomPartsType(hint))
        {
            for (const auto& e : g_Entries)
            {
                if (e.used && e.partsType == hint)
                    return 0xFF;
            }
            return hint;
        }

        for (std::uint16_t v = kCustomPartsTypeStart; v <= kCustomPartsTypeEnd; ++v)
        {
            const auto candidate = static_cast<std::uint8_t>(v);
            bool taken = false;
            for (const auto& e : g_Entries)
            {
                if (e.used && e.partsType == candidate) { taken = true; break; }
            }
            if (!taken) return candidate;
        }
        return 0xFF;
    }


    static bool IsSelectorTaken_NoLock(std::uint8_t code)
    {
        for (const auto& e : g_Entries)
        {
            if (!e.used) continue;
            if (e.selectorCode == code) return true;


            for (std::uint8_t k = 1; k < e.variantCount; ++k)
            {
                if (e.variantSelectorCodes[k] == code) return true;
            }
        }
        return false;
    }

    static std::uint8_t AllocateSelector_NoLock(std::uint8_t hint)
    {
        if (IsCustomSelector(hint))
        {
            if (IsSelectorTaken_NoLock(hint)) return 0xFF;
            return hint;
        }

        for (std::uint16_t v = kCustomSelectorStart; v <= kCustomSelectorEnd; ++v)
        {
            const auto candidate = static_cast<std::uint8_t>(v);
            if (!IsSelectorTaken_NoLock(candidate)) return candidate;
        }
        return 0xFF;
    }


    // True if `virtualId` is currently assigned to ANY branch of ANY existing
    // entry. Used when allocating fresh virtual ids during registration.
    static bool IsVirtualIdTaken_NoLock(std::uint8_t virtualId)
    {
        for (const auto& e : g_Entries)
        {
            if (!e.used) continue;
            for (const auto& b : e.perPlayerType)
            {
                if (b.used && b.hasCamoBonusValues
                    && b.camoBonusType == virtualId)
                    return true;
            }
        }
        return false;
    }


    static std::uint8_t AllocateVirtualId_NoLock()
    {
        for (std::uint16_t v = outfit::kCamoVirtualIdStart;
             v <= outfit::kCamoVirtualIdEnd; ++v)
        {
            const auto cand = static_cast<std::uint8_t>(v);
            if (!IsVirtualIdTaken_NoLock(cand)) return cand;
        }
        return 0xFF;
    }


    static void SummarizeBranches(const OutfitDefinition& def,
                                  std::uint8_t& outBranchCount,
                                  std::uint8_t& outMaxVariants)
    {
        outBranchCount = 0;
        outMaxVariants = 0;
        for (const auto& b : def.perPlayerType)
        {
            if (!b.used) continue;
            ++outBranchCount;
            if (b.variantCount > outMaxVariants) outMaxVariants = b.variantCount;
        }
    }
}

namespace outfit
{

    bool IsSnakeAvatarBridge(std::uint8_t a, std::uint8_t b)
    {
        return (a == kPlayerType_Snake && b == kPlayerType_Avatar)
            || (a == kPlayerType_Avatar && b == kPlayerType_Snake);
    }


    const OutfitPlayerTypeData* OutfitEntry::GetPTData(std::uint8_t playerType) const
    {
        if (playerType >= kPlayerTypeMax) return nullptr;
        if (perPlayerType[playerType].used)
            return &perPlayerType[playerType];

        if (playerType == kPlayerType_Snake
            && perPlayerType[kPlayerType_Avatar].used)
            return &perPlayerType[kPlayerType_Avatar];
        if (playerType == kPlayerType_Avatar
            && perPlayerType[kPlayerType_Snake].used)
            return &perPlayerType[kPlayerType_Snake];

        return nullptr;
    }

    bool OutfitEntry::IsPlayerTypeSupported(std::uint8_t playerType) const
    {
        return GetPTData(playerType) != nullptr;
    }


    bool OutfitEntry::IsArmEnabled(std::uint8_t playerType) const
    {
        const auto* d = GetPTData(playerType);
        return d ? d->enableArm : false;
    }

    bool OutfitEntry::IsHeadEnabled(std::uint8_t playerType) const
    {
        const auto* d = GetPTData(playerType);
        return d ? d->enableHead : false;
    }

    bool OutfitEntry::IsCamoCustom(std::uint8_t playerType) const
    {
        const auto* d = GetPTData(playerType);
        return d && d->camoFpk > kSubAssetUseVanilla;
    }

    bool OutfitEntry::IsCamoFv2Custom(std::uint8_t playerType) const
    {
        const auto* d = GetPTData(playerType);
        return d && d->camoFv2 > kSubAssetUseVanilla;
    }

    bool OutfitEntry::IsFaceEnabled(std::uint8_t playerType) const
    {
        const auto* d = GetPTData(playerType);
        return d && d->faceFpk != kSubAssetDisabled;
    }

    bool OutfitEntry::IsDiamondEnabled(std::uint8_t playerType) const
    {
        const auto* d = GetPTData(playerType);
        return d && d->diamondFpk != kSubAssetDisabled;
    }

    bool OutfitEntry::IsDiamondCustom(std::uint8_t playerType) const
    {
        const auto* d = GetPTData(playerType);
        return d && d->diamondFpk > kSubAssetUseVanilla;
    }

    bool OutfitEntry::IsDiamondFv2Custom(std::uint8_t playerType) const
    {
        const auto* d = GetPTData(playerType);
        return d && d->diamondFv2 > kSubAssetUseVanilla;
    }

    bool OutfitEntry::IsVoiceCustom(std::uint8_t playerType) const
    {
        const auto* d = GetPTData(playerType);
        return d && d->voiceFpk > kSubAssetUseVanilla;
    }


    bool OutfitEntry::HasHeadOptions(std::uint8_t playerType) const
    {
        const auto* d = GetPTData(playerType);
        return d && d->supportsHeadOptions && d->headOptionCount > 0;
    }

    bool OutfitEntry::HasAnyHeadOptions() const
    {
        for (const auto& b : perPlayerType)
        {
            if (b.used && b.supportsHeadOptions && b.headOptionCount > 0)
                return true;
        }
        return false;
    }

    bool OutfitEntry::GetHeadOptionsFor(std::uint8_t playerType,
                                        const std::uint16_t** outEquipIds,
                                        std::uint8_t* outCount) const
    {
        if (const auto* d = GetPTData(playerType);
            d && d->supportsHeadOptions && d->headOptionCount > 0)
        {
            if (outEquipIds) *outEquipIds = d->headOptionEquipIds;
            if (outCount)    *outCount    = d->headOptionCount;
            return true;
        }
        if (outEquipIds) *outEquipIds = nullptr;
        if (outCount)    *outCount    = 0;
        return false;
    }


    std::uint8_t OutfitEntry::GetVariantCountFor(std::uint8_t playerType) const
    {
        const auto* d = GetPTData(playerType);
        return d ? d->variantCount : std::uint8_t{0};
    }


    std::uint64_t OutfitEntry::GetLangEquipNameHashFor(std::uint8_t playerType) const
    {
        const auto* d = GetPTData(playerType);
        return d ? d->langEquipNameHash : std::uint64_t{0};
    }

    std::uint16_t OutfitEntry::GetDefaultSoldierFaceIdFor(std::uint8_t playerType) const
    {
        const auto* d = GetPTData(playerType);
        return d ? d->defaultSoldierFaceId : std::uint16_t{0};
    }


    std::uint8_t OutfitEntry::GetCamoBonusType(std::uint8_t playerType) const
    {
        const auto* d = GetPTData(playerType);
        return d ? d->camoBonusType : kCamoBonusTypeUnset;
    }

    bool OutfitEntry::HasCamoBonusValuesFor(std::uint8_t playerType) const
    {
        const auto* d = GetPTData(playerType);
        return d && d->hasCamoBonusValues;
    }

    const std::int32_t* OutfitEntry::GetCamoBonusValuesFor(std::uint8_t playerType) const
    {
        const auto* d = GetPTData(playerType);
        return (d && d->hasCamoBonusValues) ? d->camoBonusValues : nullptr;
    }


    std::uint64_t OutfitEntry::GetVariantPartsPath(
        std::uint8_t playerType, std::uint8_t variantIdx) const
    {
        const auto* d = GetPTData(playerType);
        if (!d) return 0;
        if (variantIdx == 0 || variantIdx >= d->variantCount
            || !d->variants[variantIdx].used)
            return d->partsPathCode64;
        return d->variants[variantIdx].partsPathCode64 != 0
             ? d->variants[variantIdx].partsPathCode64
             : d->partsPathCode64;
    }

    std::uint64_t OutfitEntry::GetVariantFpkPath(
        std::uint8_t playerType, std::uint8_t variantIdx) const
    {
        const auto* d = GetPTData(playerType);
        if (!d) return 0;
        if (variantIdx == 0 || variantIdx >= d->variantCount
            || !d->variants[variantIdx].used)
            return d->fpkPathCode64;
        return d->variants[variantIdx].fpkPathCode64 != 0
             ? d->variants[variantIdx].fpkPathCode64
             : d->fpkPathCode64;
    }

    std::uint64_t OutfitEntry::GetVariantCamoFpk(
        std::uint8_t playerType, std::uint8_t variantIdx) const
    {
        const auto* d = GetPTData(playerType);
        if (!d) return kSubAssetUseVanilla;
        if (variantIdx == 0 || variantIdx >= d->variantCount
            || !d->variants[variantIdx].used)
            return d->camoFpk;
        const auto v = d->variants[variantIdx].camoFpk;
        return v == kSubAssetUseVanilla ? d->camoFpk : v;
    }

    std::uint64_t OutfitEntry::GetVariantCamoFv2(
        std::uint8_t playerType, std::uint8_t variantIdx) const
    {
        const auto* d = GetPTData(playerType);
        if (!d) return kSubAssetUseVanilla;
        if (variantIdx == 0 || variantIdx >= d->variantCount
            || !d->variants[variantIdx].used)
            return d->camoFv2;
        const auto v = d->variants[variantIdx].camoFv2;
        return v == kSubAssetUseVanilla ? d->camoFv2 : v;
    }

    std::uint64_t OutfitEntry::GetVariantDiamondFpk(
        std::uint8_t playerType, std::uint8_t variantIdx) const
    {
        const auto* d = GetPTData(playerType);
        if (!d) return kSubAssetDisabled;
        if (variantIdx == 0 || variantIdx >= d->variantCount
            || !d->variants[variantIdx].used)
            return d->diamondFpk;
        return d->variants[variantIdx].diamondFpk != kSubAssetDisabled
             ? d->variants[variantIdx].diamondFpk
             : d->diamondFpk;
    }

    std::uint64_t OutfitEntry::GetVariantDiamondFv2(
        std::uint8_t playerType, std::uint8_t variantIdx) const
    {
        const auto* d = GetPTData(playerType);
        if (!d) return kSubAssetUseVanilla;
        if (variantIdx == 0 || variantIdx >= d->variantCount
            || !d->variants[variantIdx].used)
            return d->diamondFv2;
        const auto v = d->variants[variantIdx].diamondFv2;
        return v == kSubAssetUseVanilla ? d->diamondFv2 : v;
    }

    std::uint64_t OutfitEntry::GetVariantVoiceFpk(
        std::uint8_t playerType, std::uint8_t variantIdx) const
    {
        const auto* d = GetPTData(playerType);
        if (!d) return kSubAssetUseVanilla;
        if (variantIdx == 0 || variantIdx >= d->variantCount
            || !d->variants[variantIdx].used)
            return d->voiceFpk;
        const auto v = d->variants[variantIdx].voiceFpk;
        return v == kSubAssetUseVanilla ? d->voiceFpk : v;
    }

    std::uint64_t OutfitEntry::GetVariantDisplayNameHash(
        std::uint8_t playerType, std::uint8_t variantIdx) const
    {
        const auto* d = GetPTData(playerType);
        if (!d) return 0;
        if (variantIdx == 0) return d->baseDisplayNameHash;
        if (variantIdx >= d->variantCount) return 0;
        return d->variants[variantIdx].displayNameHash;
    }


    bool RegisterOutfit(const OutfitDefinition& def, std::uint8_t* outAllocatedPartsType)
    {

        std::uint8_t branchCount  = 0;
        std::uint8_t maxVariants  = 0;
        SummarizeBranches(def, branchCount, maxVariants);

        if (branchCount == 0)
        {
            Log("[OutfitRegistry] reject: no playerType branches populated. "
                "At least one of {snake, ddMale, ddFemale, avatar} must "
                "supply partsPath/fpkPath. (key=%s)\n",
                def.key ? def.key : "(unkeyed)");
            return false;
        }


        for (std::uint8_t pt = 0; pt < kPlayerTypeMax; ++pt)
        {
            const auto& b = def.perPlayerType[pt];
            if (!b.used) continue;
            if (b.partsPathCode64 == 0 || b.fpkPathCode64 == 0)
            {
                Log("[OutfitRegistry] reject: playerType=%u branch is missing "
                    "required partsPath or fpkPath (key=%s)\n",
                    static_cast<unsigned>(pt),
                    def.key ? def.key : "(unkeyed)");
                return false;
            }
        }

        if (def.developId == 0 || def.flowIndex == 0)
        {
            Log("[OutfitRegistry] reject: developId/flowIndex must be non-zero "
                "(developId=%u flowIndex=%u key=%s)\n",
                static_cast<unsigned>(def.developId),
                static_cast<unsigned>(def.flowIndex),
                def.key ? def.key : "(unkeyed)");
            return false;
        }


        constexpr std::uint16_t kEdcRowCapacity = 0x400;
        if (def.flowIndex >= kEdcRowCapacity)
        {
            Log("[OutfitRegistry] reject: flowIndex=%u out of EDC capacity "
                "(max 0x3FF). (key=%s)\n",
                static_cast<unsigned>(def.flowIndex),
                def.key ? def.key : "(unkeyed)");
            return false;
        }

        std::lock_guard<std::mutex> lock(g_Mutex);


        for (const auto& e : g_Entries)
        {
            if (!e.used) continue;

            const bool sameDevelopId = (e.developId == def.developId);
            const bool sameFlowIndex = (e.flowIndex == def.flowIndex);
            if (!sameDevelopId && !sameFlowIndex) continue;

            if (sameDevelopId && sameFlowIndex)
            {
                Log("[OutfitRegistry] re-registration of same outfit "
                    "developId=%u flowIndex=%u partsType=0x%02X — "
                    "returning existing entry (idempotent)\n",
                    static_cast<unsigned>(e.developId),
                    static_cast<unsigned>(e.flowIndex),
                    static_cast<unsigned>(e.partsType));
                if (outAllocatedPartsType) *outAllocatedPartsType = e.partsType;
                return true;
            }


            if (sameDevelopId)
            {
                Log("[OutfitRegistry] reject: developId %u already taken "
                    "(existing partsType=0x%02X)\n",
                    static_cast<unsigned>(def.developId),
                    static_cast<unsigned>(e.partsType));
            }
            else
            {
                Log("[OutfitRegistry] reject: flowIndex %u already taken "
                    "(existing developId=%u, attempted developId=%u)\n",
                    static_cast<unsigned>(def.flowIndex),
                    static_cast<unsigned>(e.developId),
                    static_cast<unsigned>(def.developId));
            }
            return false;
        }

        const std::uint8_t partsType = AllocatePartsType_NoLock(def.partsTypeHint);
        if (partsType == 0xFF)
        {
            Log("[OutfitRegistry] reject: no free custom partsType slot\n");
            return false;
        }

        const std::uint8_t selector = AllocateSelector_NoLock(def.selectorCodeHint);
        if (selector == 0xFF)
        {
            Log("[OutfitRegistry] reject: no free custom selector slot\n");
            return false;
        }


        const std::uint8_t variantSlots =
            (maxVariants > outfit::kMaxVariantsPerOutfit)
                ? static_cast<std::uint8_t>(outfit::kMaxVariantsPerOutfit)
                : maxVariants;

        std::uint8_t variantSelectors[outfit::kMaxVariantsPerOutfit] = {};
        for (auto& v : variantSelectors) v = 0xFF;
        variantSelectors[0] = selector;
        for (std::uint8_t vi = 1; vi < variantSlots; ++vi)
        {
            std::uint8_t alloc = 0xFF;
            for (std::uint16_t cand = kCustomSelectorStart;
                 cand <= kCustomSelectorEnd; ++cand)
            {
                const auto c = static_cast<std::uint8_t>(cand);
                if (IsSelectorTaken_NoLock(c)) continue;

                bool taken = false;
                for (std::uint8_t k = 0; k < vi; ++k)
                {
                    if (variantSelectors[k] == c) { taken = true; break; }
                }
                if (!taken) { alloc = c; break; }
            }
            if (alloc == 0xFF)
            {
                Log("[OutfitRegistry] reject: ran out of selectors while "
                    "reserving variant %u of %u\n",
                    static_cast<unsigned>(vi),
                    static_cast<unsigned>(variantSlots));
                return false;
            }
            variantSelectors[vi] = alloc;
        }


        OutfitEntry* slot = nullptr;
        for (auto& e : g_Entries)
        {
            if (!e.used) { slot = &e; break; }
        }
        if (!slot)
        {
            Log("[OutfitRegistry] reject: registry full (kMaxOutfits=%zu)\n",
                kMaxOutfits);
            return false;
        }

        *slot = OutfitEntry{};
        slot->used         = true;
        slot->developId    = def.developId;
        slot->flowIndex    = def.flowIndex;
        slot->partsType    = partsType;
        slot->selectorCode = selector;


        for (std::uint8_t pt = 0; pt < kPlayerTypeMax; ++pt)
            slot->perPlayerType[pt] = def.perPlayerType[pt];


        slot->variantCount = variantSlots;
        for (std::size_t i = 0; i < outfit::kMaxVariantsPerOutfit; ++i)
            slot->variantSelectorCodes[i] = variantSelectors[i];


        // For each branch declaring hasCamoBonusValues=true, allocate a
        // virtual id from the 200..254 pool. Each PT gets its own id; the
        // GetCamoufValue hook routes the row lookup based on the PT that
        // owns the matched virtual id.
        for (std::uint8_t pt = 0; pt < kPlayerTypeMax; ++pt)
        {
            auto& b = slot->perPlayerType[pt];
            if (!b.used || !b.hasCamoBonusValues) continue;

            const std::uint8_t vid = AllocateVirtualId_NoLock();
            if (vid == 0xFF)
            {
                Log("[OutfitRegistry] camo virtual-id pool exhausted while "
                    "registering PT=%u of '%s' — branch will run without a "
                    "unique bonus row\n",
                    static_cast<unsigned>(pt),
                    def.key ? def.key : "(unkeyed)");
                b.hasCamoBonusValues = false;
                if (b.camoBonusType == kCamoBonusTypeUnset)
                    b.camoBonusType = kCamoBonusTypeUnset;
            }
            else
            {
                b.camoBonusType = vid;
            }
        }

        if (outAllocatedPartsType) *outAllocatedPartsType = partsType;


        char branchBuf[160] = {};
        {
            std::size_t pos = 0;
            const char* names[kPlayerTypeMax] =
                { "Snake", "DDMale", "DDFemale", "Avatar" };
            for (std::uint8_t pt = 0; pt < kPlayerTypeMax; ++pt)
            {
                if (!slot->perPlayerType[pt].used) continue;
                const auto& b = slot->perPlayerType[pt];

                char camoBuf[24] = {};
                if (b.hasCamoBonusValues)
                    std::snprintf(camoBuf, sizeof(camoBuf),
                        "vid=%u", static_cast<unsigned>(b.camoBonusType));
                else if (b.camoBonusType != kCamoBonusTypeUnset)
                    std::snprintf(camoBuf, sizeof(camoBuf),
                        "pin=%u", static_cast<unsigned>(b.camoBonusType));
                else
                    std::snprintf(camoBuf, sizeof(camoBuf), "no-camo");

                pos += static_cast<std::size_t>(std::snprintf(
                    branchBuf + pos, sizeof(branchBuf) - pos,
                    (pos == 0) ? "%s(v=%u,h=%u,arm=%d,head=%d,%s)"
                               : ",%s(v=%u,h=%u,arm=%d,head=%d,%s)",
                    names[pt],
                    static_cast<unsigned>(b.variantCount),
                    static_cast<unsigned>(b.headOptionCount),
                    b.enableArm ? 1 : 0,
                    b.enableHead ? 1 : 0,
                    camoBuf));
            }
        }


        char variantBuf[16 * 5 + 1] = {};
        {
            std::size_t pos = 0;
            for (std::size_t i = 0;
                 i < outfit::kMaxVariantsPerOutfit && pos + 5 < sizeof(variantBuf);
                 ++i)
            {
                pos += static_cast<std::size_t>(std::snprintf(
                    variantBuf + pos, sizeof(variantBuf) - pos,
                    (i == 0) ? "0x%02X" : ",0x%02X",
                    static_cast<unsigned>(slot->variantSelectorCodes[i])));
            }
        }

        Log("[OutfitRegistry] registered key=%s developId=%u flowIndex=%u "
            "partsType=0x%02X selector=0x%02X branches=[%s] "
            "variantCount=%u variantSelectors=[%s]\n",
            def.key ? def.key : "(unkeyed)",
            static_cast<unsigned>(def.developId),
            static_cast<unsigned>(def.flowIndex),
            static_cast<unsigned>(partsType),
            static_cast<unsigned>(selector),
            branchBuf,
            static_cast<unsigned>(slot->variantCount),
            variantBuf);

        return true;
    }

    void ClearAllOutfits()
    {
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            for (auto& e : g_Entries)        e = OutfitEntry{};
            for (auto& v : g_ActiveVariant)  v = 0;
            g_PendingDevelopId            = 0;
            g_PendingHeadOptionEquipId    = 0;
            g_SupplyDropClickLatch        = false;
            g_PendingSupplyDropDevelopId  = 0;
            g_PendingSupplyDropVariantIdx = 0;
        }
        outfit::shadow::ResetAll("ClearAllOutfits");
        outfit::shadow::ResetArmTierCache();
    }

    bool TryGetOutfitByPartsType(std::uint8_t partsType, const OutfitEntry** outEntry)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const auto& e : g_Entries)
        {
            if (e.used && e.partsType == partsType)
            {
                if (outEntry) *outEntry = &e;
                return true;
            }
        }
        return false;
    }

    bool TryGetOutfitBySelectorCode(std::uint8_t selectorCode, const OutfitEntry** outEntry)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const auto& e : g_Entries)
        {
            if (!e.used) continue;
            if (e.selectorCode == selectorCode)
            {
                if (outEntry) *outEntry = &e;
                return true;
            }
            for (std::uint8_t vi = 1; vi < e.variantCount; ++vi)
            {
                if (e.variantSelectorCodes[vi] == selectorCode)
                {
                    if (outEntry) *outEntry = &e;
                    return true;
                }
            }
        }
        return false;
    }

    bool TryGetOutfitByFlowIndex(std::uint16_t flowIndex, const OutfitEntry** outEntry)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const auto& e : g_Entries)
        {
            if (e.used && e.flowIndex == flowIndex)
            {
                if (outEntry) *outEntry = &e;
                return true;
            }
        }
        return false;
    }

    bool TryGetOutfitByVariantSelector(std::uint8_t selectorCode,
                                       const OutfitEntry** outEntry,
                                       std::uint8_t* outVariantIndex)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const auto& e : g_Entries)
        {
            if (!e.used) continue;

            if (e.selectorCode == selectorCode)
            {
                if (outEntry) *outEntry = &e;
                if (outVariantIndex) *outVariantIndex = 0;
                return true;
            }

            for (std::uint8_t vi = 1; vi < e.variantCount; ++vi)
            {
                if (e.variantSelectorCodes[vi] == selectorCode)
                {
                    if (outEntry) *outEntry = &e;
                    if (outVariantIndex) *outVariantIndex = vi;
                    return true;
                }
            }
        }
        return false;
    }

    std::uint64_t GetVariantDisplayNameHash(std::uint8_t partsType,
                                            std::uint8_t playerType,
                                            std::uint8_t variantIndex)
    {
        if (variantIndex >= outfit::kMaxVariantsPerOutfit) return 0;
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const auto& e : g_Entries)
        {
            if (e.used && e.partsType == partsType)
                return e.GetVariantDisplayNameHash(playerType, variantIndex);
        }
        return 0;
    }

    bool TryGetOutfitByDevelopId(std::uint16_t developId, const OutfitEntry** outEntry)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const auto& e : g_Entries)
        {
            if (e.used && e.developId == developId)
            {
                if (outEntry) *outEntry = &e;
                return true;
            }
        }
        return false;
    }

    bool TryGetOutfitByCamoVirtualId(std::uint8_t virtualId,
                                     const OutfitEntry** outEntry,
                                     std::uint8_t* outPlayerType)
    {
        if (virtualId < kCamoVirtualIdStart || virtualId > kCamoVirtualIdEnd)
            return false;

        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const auto& e : g_Entries)
        {
            if (!e.used) continue;
            for (std::uint8_t pt = 0; pt < kPlayerTypeMax; ++pt)
            {
                const auto& b = e.perPlayerType[pt];
                if (b.used && b.hasCamoBonusValues
                    && b.camoBonusType == virtualId)
                {
                    if (outEntry)      *outEntry      = &e;
                    if (outPlayerType) *outPlayerType = pt;
                    return true;
                }
            }
        }
        return false;
    }

    bool TryGetOutfitByFlowIndexForPlayerType(
            std::uint16_t flowIndex, std::uint8_t playerType,
            const OutfitEntry** outEntry)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const auto& e : g_Entries)
        {
            if (e.used && e.flowIndex == flowIndex
                && e.IsPlayerTypeSupported(playerType))
            {
                if (outEntry) *outEntry = &e;
                return true;
            }
        }
        return false;
    }

    bool TryGetOutfitByDevelopIdForPlayerType(
            std::uint16_t developId, std::uint8_t playerType,
            const OutfitEntry** outEntry)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const auto& e : g_Entries)
        {
            if (e.used && e.developId == developId
                && e.IsPlayerTypeSupported(playerType))
            {
                if (outEntry) *outEntry = &e;
                return true;
            }
        }
        return false;
    }

    std::size_t GetAllOutfits(const OutfitEntry** outEntries, std::size_t maxEntries)
    {
        if (!outEntries || maxEntries == 0) return 0;

        std::lock_guard<std::mutex> lock(g_Mutex);
        std::size_t count = 0;
        for (const auto& e : g_Entries)
        {
            if (count >= maxEntries) break;
            if (e.used) outEntries[count++] = &e;
        }
        return count;
    }

    std::uint8_t ReadLivePartsType()
    {
        auto* state = GetQuarkLiveState();
        return state ? state[0xF8] : static_cast<std::uint8_t>(0xFF);
    }

    std::uint8_t ReadLiveSelectorCode()
    {
        auto* state = GetQuarkLiveState();
        return state ? state[0xF9] : static_cast<std::uint8_t>(0xFF);
    }

    std::uint8_t ReadLivePlayerType()
    {
        auto* state = GetQuarkLiveState();
        return state ? state[0xFB] : static_cast<std::uint8_t>(0xFF);
    }

    bool WriteLivePlayerOutfit(std::uint8_t partsType,
                               std::uint8_t selectorCode,
                               std::uint8_t playerType)
    {
        auto* state = GetQuarkLiveState();
        if (!state) return false;

        __try
        {
            state[0xF8] = partsType;
            state[0xF9] = selectorCode;
            state[0xFB] = playerType;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }


    void SetActiveVariant(std::uint8_t partsType, std::uint8_t variantIndex)
    {
        if (partsType < kCustomPartsTypeStart || partsType > kCustomPartsTypeEnd)
            return;

        std::uint8_t clamped = variantIndex;

        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const auto& e : g_Entries)
        {
            if (e.used && e.partsType == partsType)
            {
                if (e.variantCount == 0) clamped = 0;
                else if (clamped >= e.variantCount)
                    clamped = static_cast<std::uint8_t>(e.variantCount - 1);
                break;
            }
        }
        g_ActiveVariant[partsType - kCustomPartsTypeStart] = clamped;
    }

    std::uint8_t GetActiveVariant(std::uint8_t partsType)
    {
        if (partsType < kCustomPartsTypeStart || partsType > kCustomPartsTypeEnd)
            return 0;
        std::lock_guard<std::mutex> lock(g_Mutex);
        return g_ActiveVariant[partsType - kCustomPartsTypeStart];
    }

    void ClearActiveVariant(std::uint8_t partsType)
    {
        if (partsType < kCustomPartsTypeStart || partsType > kCustomPartsTypeEnd)
            return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_ActiveVariant[partsType - kCustomPartsTypeStart] = 0;
    }


    void SetPendingOutfitDevelopId(std::uint16_t developId)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_PendingDevelopId = developId;
    }

    std::uint16_t GetPendingOutfitDevelopId()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        return g_PendingDevelopId;
    }

    void ClearPendingOutfitDevelopId()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_PendingDevelopId = 0;
    }

    void SetPendingHeadOptionEquipId(std::uint16_t equipId)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_PendingHeadOptionEquipId = equipId;
    }

    std::uint16_t GetPendingHeadOptionEquipId()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        return g_PendingHeadOptionEquipId;
    }

    void ClearPendingHeadOptionEquipId()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_PendingHeadOptionEquipId = 0;
    }

    void SetSupplyDropClickLatch()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_SupplyDropClickLatch = true;
    }

    bool ConsumeSupplyDropClickLatch()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        const bool was = g_SupplyDropClickLatch;
        g_SupplyDropClickLatch = false;
        return was;
    }

    void SetPendingSupplyDropDevelopId(std::uint16_t developId)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_PendingSupplyDropDevelopId = developId;
    }

    std::uint16_t ConsumePendingSupplyDropDevelopId()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        const std::uint16_t was = g_PendingSupplyDropDevelopId;
        g_PendingSupplyDropDevelopId = 0;
        return was;
    }

    std::uint16_t PeekPendingSupplyDropDevelopId()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        return g_PendingSupplyDropDevelopId;
    }

    void SetPendingSupplyDropVariantIdx(std::uint8_t variantIndex)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_PendingSupplyDropVariantIdx = variantIndex;
    }

    std::uint8_t ConsumePendingSupplyDropVariantIdx()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        const std::uint8_t was = g_PendingSupplyDropVariantIdx;
        g_PendingSupplyDropVariantIdx = 0;
        return was;
    }
}
