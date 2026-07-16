#include "pch.h"

#include "OutfitRegistry.h"
#include "ShadowState.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>

#include "AddressSet.h"
#include "log.h"
#include <HookUtils.h>
#include "../../core/V_FrameWorkState.h"

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


    static std::uint8_t g_ActiveVariant[256] = {};


    static std::uint16_t g_PendingDevelopId            = 0;
    static std::uint16_t g_PendingHeadOptionEquipId    = 0;
    static std::uint8_t  g_WornCustomHeadSlot          = 0;
    static bool          g_SupplyDropClickLatch        = false;
    static std::uint16_t g_PendingSupplyDropDevelopId  = 0;
    static std::uint8_t  g_PendingSupplyDropVariantIdx = 0;
    static std::uint16_t g_CrateDeliveredDevelopId     = 0;
    static std::uint8_t  g_CrateDeliveredVariantIdx    = 0;

    static constexpr std::size_t kMaxVanillaExtVariants =
        outfit::kMaxVariantsPerOutfit - 1;

    struct VanillaSuitExtension
    {
        bool          used             = false;
        std::uint8_t  vanillaPartsType = 0xFF;
        outfit::VanillaSuitHeadExt perPlayerType[kPlayerTypeMax] = {};

        std::uint8_t  variantCount[kPlayerTypeMax] = {};
        outfit::VanillaSuitVariantAsset
            variants[kPlayerTypeMax][kMaxVanillaExtVariants] = {};

        std::uint8_t  variantSelectorCodes[kMaxVanillaExtVariants] = {};
        std::uint8_t  variantSourceCamo[kMaxVanillaExtVariants] = {};
        std::uint8_t  variantSelectorCount = 0;

        std::uint64_t suitVoiceFpk[kPlayerTypeMax] = {};
        std::uint8_t  suitVoiceCamo[kPlayerTypeMax] = {};
    };
    static std::array<VanillaSuitExtension, outfit::kMaxVanillaSuitExtensions>
        g_VanillaExts{};

    static VanillaSuitExtension* FindVanillaExt_NoLock(std::uint8_t partsType)
    {
        for (auto& x : g_VanillaExts)
            if (x.used && x.vanillaPartsType == partsType) return &x;
        return nullptr;
    }

    struct PendingSummaryDisplay
    {
        std::uint64_t nameHash = 0;
        std::uint64_t iconHash = 0;
    };
    static std::unordered_map<std::uint16_t, PendingSummaryDisplay>
        g_PendingSummaryDisplay;

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

    static bool IsAllocatableSelector(std::uint8_t v)
    {
        return IsCustomSelector(v)
            && !(v >= outfit::kVanillaEventCamoStart
              && v <= outfit::kVanillaEventCamoEnd);
    }

    static bool          g_ReservationsBuilt = false;
    static std::uint64_t g_SelectorReservedBy[256]  = {};
    static std::uint64_t g_PartsTypeReservedBy[256] = {};

    static std::uint64_t HashOutfitKey(const char* key)
    {
        std::uint64_t h = 1469598103934665603ull;
        if (key)
            for (const unsigned char* p =
                     reinterpret_cast<const unsigned char*>(key); *p; ++p)
            { h ^= *p; h *= 1099511628211ull; }
        return h ? h : 1ull;
    }

    static void BuildReservations_NoLock()
    {
        if (g_ReservationsBuilt) return;
        g_ReservationsBuilt = true;

        std::size_t reservedSel = 0, reservedPt = 0, conflicts = 0;
        V_FrameWorkState::ForEachPersistedOutfit(
            [&](const std::string& key, std::uint8_t pt, std::uint8_t sel,
                const std::uint8_t* variants)
            {
                const std::uint64_t h = HashOutfitKey(key.c_str());
                if (IsCustomPartsType(pt))
                {
                    if (g_PartsTypeReservedBy[pt] == 0)
                    { g_PartsTypeReservedBy[pt] = h; ++reservedPt; }
                    else if (g_PartsTypeReservedBy[pt] != h) ++conflicts;
                }
                auto reserveSel = [&](std::uint8_t s)
                {
                    if (!IsAllocatableSelector(s)) return;
                    if (g_SelectorReservedBy[s] == 0)
                    { g_SelectorReservedBy[s] = h; ++reservedSel; }
                    else if (g_SelectorReservedBy[s] != h) ++conflicts;
                };
                reserveSel(sel);
                for (std::size_t i = 0; i < 14; ++i) reserveSel(variants[i]);
            });

        if (reservedSel || reservedPt || conflicts)
        {
#ifdef _DEBUG
            Log("[OutfitRegistry] reservation pass: %zu selector(s) + %zu "
                "partsType(s) reserved for persisted outfit keys%s\n",
                reservedSel, reservedPt,
                conflicts ? " (persisted-value CONFLICTS detected - "
                            "first-loaded key wins, later keys re-allocate)"
                          : "");
#endif
        }
    }

    static std::uint8_t AllocatePartsType_NoLock(std::uint8_t hint,
                                                 std::uint64_t keyHash)
    {
        auto takenBy = [&](std::uint8_t candidate)
        {
            for (const auto& e : g_Entries)
                if (e.used && e.partsType == candidate) return true;
            return false;
        };

        if (IsCustomPartsType(hint) && !takenBy(hint)
            && (g_PartsTypeReservedBy[hint] == 0
             || g_PartsTypeReservedBy[hint] == keyHash))
        {
            return hint;
        }

        for (std::uint16_t v = kCustomPartsTypeStart; v <= kCustomPartsTypeEnd; ++v)
        {
            const auto candidate = static_cast<std::uint8_t>(v);
            if (takenBy(candidate)) continue;
            if (g_PartsTypeReservedBy[candidate] != 0
             && g_PartsTypeReservedBy[candidate] != keyHash) continue;
            return candidate;
        }
        for (std::uint16_t v = kCustomPartsTypeStart; v <= kCustomPartsTypeEnd; ++v)
        {
            const auto candidate = static_cast<std::uint8_t>(v);
            if (takenBy(candidate)) continue;
            Log("[OutfitRegistry] partsType pool exhausted - consuming "
                "reserved value 0x%02X from an absent key\n", candidate);
            return candidate;
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
        for (const auto& x : g_VanillaExts)
        {
            if (!x.used) continue;
            for (std::uint8_t k = 0; k < x.variantSelectorCount; ++k)
                if (x.variantSelectorCodes[k] == code) return true;
        }
        return false;
    }

    static bool IsSelectorReservedForOther_NoLock(std::uint8_t code,
                                                  std::uint64_t keyHash)
    {
        return g_SelectorReservedBy[code] != 0
            && g_SelectorReservedBy[code] != keyHash;
    }

    static std::uint8_t AllocateSelector_NoLock(std::uint8_t hint,
                                                std::uint64_t keyHash)
    {
        if (IsCustomSelector(hint) && !IsAllocatableSelector(hint))
            Log("[OutfitRegistry] selector hint 0x%02X is inside the vanilla "
                "event-camo band 0x83-0x88 (legacy allocation) - migrating to "
                "a clean selector\n", hint);

        if (IsAllocatableSelector(hint) && !IsSelectorTaken_NoLock(hint)
            && !IsSelectorReservedForOther_NoLock(hint, keyHash))
            return hint;

        for (std::uint16_t v = kCustomSelectorStart; v <= kCustomSelectorEnd; ++v)
        {
            const auto candidate = static_cast<std::uint8_t>(v);
            if (!IsAllocatableSelector(candidate)) continue;
            if (IsSelectorTaken_NoLock(candidate)) continue;
            if (IsSelectorReservedForOther_NoLock(candidate, keyHash)) continue;
            return candidate;
        }
        for (std::uint16_t v = kCustomSelectorStart; v <= kCustomSelectorEnd; ++v)
        {
            const auto candidate = static_cast<std::uint8_t>(v);
            if (!IsAllocatableSelector(candidate)) continue;
            if (IsSelectorTaken_NoLock(candidate)) continue;
            Log("[OutfitRegistry] selector pool exhausted - consuming reserved "
                "value 0x%02X from an absent key\n", candidate);
            return candidate;
        }
        return 0xFF;
    }

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
        if (!d) return false;
        const std::uint8_t vi = GetActiveVariant(partsType);
        if (vi >= 1 && vi < kMaxVariantsPerOutfit
            && d->variants[vi].used && d->variants[vi].hasEnableArm)
            return d->variants[vi].enableArm;
        return d->enableArm;
    }

    bool OutfitEntry::IsHeadEnabled(std::uint8_t playerType) const
    {
        const auto* d = GetPTData(playerType);
        if (!d) return false;
        const std::uint8_t vi = GetActiveVariant(partsType);
        if (vi >= 1 && vi < kMaxVariantsPerOutfit
            && d->variants[vi].used && d->variants[vi].hasEnableHead)
            return d->variants[vi].enableHead;
        return d->enableHead;
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

    bool OutfitEntry::HasHeadOption(std::uint16_t equipId, std::uint8_t playerType) const
    {
        const std::uint16_t* ids = nullptr;
        std::uint8_t count = 0;
        if (!GetHeadOptionsFor(playerType, &ids, &count) || !ids) return false;
        for (std::uint8_t i = 0; i < count; ++i)
            if (ids[i] == equipId) return true;
        return false;
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

    bool OutfitEntry::GetHeadOptionsForVariant(std::uint8_t playerType,
                                               std::uint8_t variant,
                                               const std::uint16_t** outEquipIds,
                                               std::uint8_t* outCount) const
    {
        const auto* d = GetPTData(playerType);
        if (d && variant >= 1 && variant < kMaxVariantsPerOutfit
            && d->variants[variant].used)
        {
            if (d->variants[variant].headOptionsDeclared
                && d->variants[variant].headOptionCount > 0)
            {
                if (outEquipIds) *outEquipIds = d->variants[variant].headOptionEquipIds;
                if (outCount)    *outCount    = d->variants[variant].headOptionCount;
                return true;
            }
            if (outEquipIds) *outEquipIds = nullptr;
            if (outCount)    *outCount    = 0;
            return false;
        }
        return GetHeadOptionsFor(playerType, outEquipIds, outCount);
    }

    bool OutfitEntry::HasHeadOptionsForVariant(std::uint8_t playerType,
                                               std::uint8_t variant) const
    {
        const std::uint16_t* ids = nullptr;
        std::uint8_t count = 0;
        return GetHeadOptionsForVariant(playerType, variant, &ids, &count)
            && ids && count > 0;
    }

    bool OutfitEntry::HasHeadOptionForVariant(std::uint16_t equipId,
                                              std::uint8_t playerType,
                                              std::uint8_t variant) const
    {
        const std::uint16_t* ids = nullptr;
        std::uint8_t count = 0;
        if (!GetHeadOptionsForVariant(playerType, variant, &ids, &count) || !ids)
            return false;
        for (std::uint8_t i = 0; i < count; ++i)
            if (ids[i] == equipId) return true;
        return false;
    }

    bool OutfitEntry::HasHeadOptionAnyVariant(std::uint16_t equipId,
                                              std::uint8_t playerType) const
    {
        if (HasHeadOption(equipId, playerType)) return true;
        const auto* d = GetPTData(playerType);
        if (!d) return false;
        for (std::uint8_t vi = 1; vi < kMaxVariantsPerOutfit; ++vi)
        {
            const auto& v = d->variants[vi];
            if (!v.used || !v.headOptionsDeclared) continue;
            for (std::uint8_t i = 0; i < v.headOptionCount; ++i)
                if (v.headOptionEquipIds[i] == equipId) return true;
        }
        return false;
    }

    int ResolvePendingHeadName(std::uint64_t nameHash, std::uint16_t equipId)
    {
        if (nameHash == 0 || equipId == 0) return 0;

        auto fill = [&](std::uint16_t* ids, std::uint8_t& count,
                        std::uint64_t* pending, std::uint8_t& pendingCount,
                        bool* supports) -> int
        {
            int filled = 0;
            std::uint8_t keep = 0;
            for (std::uint8_t r = 0; r < pendingCount; ++r)
            {
                if (pending[r] != nameHash)
                {
                    pending[keep++] = pending[r];
                    continue;
                }
                bool dup = false;
                for (std::uint8_t j = 0; j < count; ++j)
                    if (ids[j] == equipId) { dup = true; break; }
                if (!dup && count < kMaxHeadOptionsPerOutfit)
                {
                    ids[count++] = equipId;
                    if (supports) *supports = true;
                    ++filled;
                }
            }
            pendingCount = keep;
            return filled;
        };

        std::lock_guard<std::mutex> lock(g_Mutex);
        int total = 0;
        for (auto& e : g_Entries)
        {
            if (!e.used) continue;
            for (std::uint8_t pt = 0; pt < kPlayerTypeMax; ++pt)
            {
                auto& b = e.perPlayerType[pt];
                if (!b.used) continue;
                total += fill(b.headOptionEquipIds, b.headOptionCount,
                              b.pendingHeadNameHashes, b.pendingHeadCount,
                              &b.supportsHeadOptions);
                for (std::uint8_t vi = 1; vi < kMaxVariantsPerOutfit; ++vi)
                {
                    auto& v = b.variants[vi];
                    if (!v.used) continue;
                    total += fill(v.headOptionEquipIds, v.headOptionCount,
                                  v.pendingHeadNameHashes, v.pendingHeadCount,
                                  nullptr);
                }
            }
        }
        for (auto& x : g_VanillaExts)
        {
            if (!x.used) continue;
            for (std::uint8_t pt = 0; pt < kPlayerTypeMax; ++pt)
            {
                auto& b = x.perPlayerType[pt];
                if (!b.declared) continue;
                total += fill(b.headOptionEquipIds, b.headOptionCount,
                              b.pendingHeadNameHashes, b.pendingHeadCount,
                              nullptr);
            }
        }
        return total;
    }


    std::uint8_t OutfitEntry::GetVariantCountFor(std::uint8_t playerType) const
    {
        const auto* d = GetPTData(playerType);
        return d ? d->variantCount : std::uint8_t{0};
    }

    std::uint8_t OutfitEntry::GetVariantSelectorCode(std::uint8_t variantIdx) const
    {
        if (variantIdx < kMaxVariantsPerOutfit)
        {
            const std::uint8_t sc = variantSelectorCodes[variantIdx];
            if (sc != 0 && sc != 0xFF)
                return sc;
        }
        return selectorCode;
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
        return d->variants[variantIdx].camoFpk;
    }

    std::uint64_t OutfitEntry::GetVariantCamoFv2(
        std::uint8_t playerType, std::uint8_t variantIdx) const
    {
        const auto* d = GetPTData(playerType);
        if (!d) return kSubAssetUseVanilla;
        if (variantIdx == 0 || variantIdx >= d->variantCount
            || !d->variants[variantIdx].used)
            return d->camoFv2;
        return d->variants[variantIdx].camoFv2;
    }

    std::uint64_t OutfitEntry::GetVariantDiamondFpk(
        std::uint8_t playerType, std::uint8_t variantIdx) const
    {
        const auto* d = GetPTData(playerType);
        if (!d) return kSubAssetDisabled;
        if (variantIdx == 0 || variantIdx >= d->variantCount
            || !d->variants[variantIdx].used)
            return d->diamondFpk;
        return d->variants[variantIdx].diamondFpk;
    }

    std::uint64_t OutfitEntry::GetVariantDiamondFv2(
        std::uint8_t playerType, std::uint8_t variantIdx) const
    {
        const auto* d = GetPTData(playerType);
        if (!d) return kSubAssetUseVanilla;
        if (variantIdx == 0 || variantIdx >= d->variantCount
            || !d->variants[variantIdx].used)
            return d->diamondFv2;
        return d->variants[variantIdx].diamondFv2;
    }

    std::uint64_t OutfitEntry::GetVariantVoiceFpk(
        std::uint8_t playerType, std::uint8_t variantIdx) const
    {
        const auto* d = GetPTData(playerType);
        if (!d) return kSubAssetUseVanilla;
        if (variantIdx == 0 || variantIdx >= d->variantCount
            || !d->variants[variantIdx].used)
            return d->voiceFpk;
        return d->variants[variantIdx].voiceFpk;
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

        if (def.developId == 0)
        {
            Log("[OutfitRegistry] reject: developId must be non-zero "
                "(key=%s)\n", def.key ? def.key : "(unkeyed)");
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

        BuildReservations_NoLock();
        const std::uint64_t keyHash = HashOutfitKey(def.key);


        for (auto& e : g_Entries)
        {
            if (!e.used) continue;

            if (e.developId == def.developId)
            {
                if (e.flowIndex != def.flowIndex)
                    e.flowIndex = def.flowIndex;
#ifdef _DEBUG
                Log("[OutfitRegistry] re-registration of same outfit "
                    "developId=%u flowIndex=%u partsType=0x%02X - "
                    "returning existing entry (idempotent)\n",
                    static_cast<unsigned>(e.developId),
                    static_cast<unsigned>(e.flowIndex),
                    static_cast<unsigned>(e.partsType));
#endif
                if (outAllocatedPartsType) *outAllocatedPartsType = e.partsType;
                return true;
            }

            if (e.flowIndex != 0 && e.flowIndex == def.flowIndex)
            {
                Log("[OutfitRegistry] flowIndex %u transferred to developId=%u "
                    "(previous claim by developId=%u was released for "
                    "paging)\n",
                    static_cast<unsigned>(def.flowIndex),
                    static_cast<unsigned>(def.developId),
                    static_cast<unsigned>(e.developId));
                e.flowIndex = 0;
            }
        }

        const std::uint8_t partsType =
            AllocatePartsType_NoLock(def.partsTypeHint, keyHash);
        if (partsType == 0xFF)
        {
            Log("[OutfitRegistry] reject: no free custom partsType slot\n");
            return false;
        }

        const std::uint8_t selector =
            AllocateSelector_NoLock(def.selectorCodeHint, keyHash);
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
            auto usedEarlier = [&](std::uint8_t c)
            {
                for (std::uint8_t k = 0; k < vi; ++k)
                    if (variantSelectors[k] == c) return true;
                return false;
            };

            std::uint8_t alloc = 0xFF;

            const std::uint8_t hint = def.variantSelectorHints[vi];
            if (IsAllocatableSelector(hint) && !IsSelectorTaken_NoLock(hint)
                && !usedEarlier(hint)
                && !IsSelectorReservedForOther_NoLock(hint, keyHash))
            {
                alloc = hint;
            }

            if (alloc == 0xFF)
            {
                for (std::uint16_t cand = kCustomSelectorStart;
                     cand <= kCustomSelectorEnd; ++cand)
                {
                    const auto c = static_cast<std::uint8_t>(cand);
                    if (!IsAllocatableSelector(c)) continue;
                    if (IsSelectorTaken_NoLock(c)) continue;
                    if (usedEarlier(c)) continue;
                    if (IsSelectorReservedForOther_NoLock(c, keyHash)) continue;
                    alloc = c; break;
                }
            }
            if (alloc == 0xFF)
            {
                for (std::uint16_t cand = kCustomSelectorStart;
                     cand <= kCustomSelectorEnd; ++cand)
                {
                    const auto c = static_cast<std::uint8_t>(cand);
                    if (!IsAllocatableSelector(c)) continue;
                    if (IsSelectorTaken_NoLock(c)) continue;
                    if (usedEarlier(c)) continue;
                    Log("[OutfitRegistry] selector pool exhausted - variant %u "
                        "consumes reserved value 0x%02X from an absent key\n",
                        static_cast<unsigned>(vi), c);
                    alloc = c; break;
                }
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

        std::uint8_t defVar = 0;
        for (std::uint8_t pt = 0; pt < kPlayerTypeMax; ++pt)
        {
            const auto& b = slot->perPlayerType[pt];
            if (b.used && b.defaultVariant > defVar)
                defVar = b.defaultVariant;
        }
        if (variantSlots == 0 || defVar >= variantSlots)
            defVar = 0;
        slot->defaultVariant       = defVar;
        g_ActiveVariant[partsType] = defVar;

        for (std::uint8_t pt = 0; pt < kPlayerTypeMax; ++pt)
        {
            auto& b = slot->perPlayerType[pt];
            if (!b.used || !b.hasCamoBonusValues) continue;

            const std::uint8_t vid = AllocateVirtualId_NoLock();
            if (vid == 0xFF)
            {
                Log("[OutfitRegistry] camo virtual-id pool exhausted while "
                    "registering PT=%u of '%s' - branch will run without a "
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

        if (const auto it = g_PendingSummaryDisplay.find(slot->developId);
            it != g_PendingSummaryDisplay.end())
        {
            if (it->second.nameHash != 0)
                slot->displaySummaryNameHash = it->second.nameHash;
            if (it->second.iconHash != 0)
                slot->displaySummaryIconHash = it->second.iconHash;
            g_PendingSummaryDisplay.erase(it);
#ifdef _DEBUG
            Log("[OutfitRegistry] drained pending summary display for "
                "developId=%u (nameHash=0x%016llX iconHash=0x%016llX)\n",
                static_cast<unsigned>(slot->developId),
                static_cast<unsigned long long>(slot->displaySummaryNameHash),
                static_cast<unsigned long long>(slot->displaySummaryIconHash));
#endif
        }


        return true;
    }

    void ClearAllOutfits()
    {
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            for (auto& e : g_Entries)        e = OutfitEntry{};
            for (auto& v : g_ActiveVariant)  v = 0;
            for (auto& x : g_VanillaExts)    x = VanillaSuitExtension{};
            g_PendingSummaryDisplay.clear();
            g_PendingDevelopId            = 0;
            g_PendingHeadOptionEquipId    = 0;
            g_WornCustomHeadSlot          = 0;
            g_SupplyDropClickLatch        = false;
            g_PendingSupplyDropDevelopId  = 0;
            g_PendingSupplyDropVariantIdx = 0;
            g_CrateDeliveredDevelopId     = 0;
            g_CrateDeliveredVariantIdx    = 0;
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
        if (flowIndex == 0)
            return false;
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

    bool SetOutfitFlowIndexByDevelopId(std::uint16_t developId,
                                       std::uint16_t flowIndex)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (auto& e : g_Entries)
        {
            if (e.used && e.developId == developId)
            {
                e.flowIndex = flowIndex;
                return true;
            }
        }
        return false;
    }

    bool SetOutfitSummaryDisplay(std::uint16_t developId,
                                 std::uint64_t nameHash,
                                 std::uint64_t iconHash)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (auto& e : g_Entries)
        {
            if (!e.used || e.developId != developId) continue;
            if (nameHash != 0) e.displaySummaryNameHash = nameHash;
            if (iconHash != 0) e.displaySummaryIconHash = iconHash;
#ifdef _DEBUG
            Log("[OutfitRegistry] summary display set developId=%u "
                "nameHash=0x%016llX iconHash=0x%016llX\n",
                static_cast<unsigned>(developId),
                static_cast<unsigned long long>(e.displaySummaryNameHash),
                static_cast<unsigned long long>(e.displaySummaryIconHash));
#endif
            return true;
        }
        PendingSummaryDisplay& p = g_PendingSummaryDisplay[developId];
        if (nameHash != 0) p.nameHash = nameHash;
        if (iconHash != 0) p.iconHash = iconHash;
#ifdef _DEBUG
        Log("[OutfitRegistry] summary display stashed (pending) developId=%u "
            "nameHash=0x%016llX iconHash=0x%016llX\n",
            static_cast<unsigned>(developId),
            static_cast<unsigned long long>(p.nameHash),
            static_cast<unsigned long long>(p.iconHash));
#endif
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
        if (flowIndex == 0)
            return false;
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


    namespace
    {
        struct RememberedOutfit
        {
            std::uint8_t partsType = 0;
            std::uint8_t selector  = 0;
            bool         valid     = false;
        };
        RememberedOutfit g_LastOutfitPerPT[kPlayerTypeMax] = {};
    }

    void RememberPlayerTypeOutfit(std::uint8_t playerType,
                                  std::uint8_t partsType, std::uint8_t selector)
    {
        if (playerType >= kPlayerTypeMax) return;
        g_LastOutfitPerPT[playerType] = { partsType, selector, true };
    }

    bool GetRememberedPlayerTypeOutfit(std::uint8_t playerType,
                                       std::uint8_t* outPartsType,
                                       std::uint8_t* outSelector)
    {
        if (playerType >= kPlayerTypeMax || !g_LastOutfitPerPT[playerType].valid)
            return false;
        if (outPartsType) *outPartsType = g_LastOutfitPerPT[playerType].partsType;
        if (outSelector)  *outSelector  = g_LastOutfitPerPT[playerType].selector;
        return true;
    }

    bool WriteLiveHeadSlot(std::uint8_t headSlot)
    {
        auto* state = GetQuarkLiveState();
        if (!state) return false;
        __try
        {
            state[0xFA] = headSlot;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool WriteLiveWornHeadCategory(std::uint16_t category)
    {
        auto* state = GetQuarkLiveState();
        if (!state) return false;
        __try
        {
            *reinterpret_cast<std::uint16_t*>(state + 0xFE) = category;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint16_t ReadLiveWornHeadCategory()
    {
        auto* state = GetQuarkLiveState();
        if (!state) return 0xFFFF;
        __try
        {
            return *reinterpret_cast<std::uint16_t*>(state + 0xFE);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0xFFFF;
        }
    }

    std::uint8_t ReadLiveHeadSlot()
    {
        auto* state = GetQuarkLiveState();
        if (!state) return 0xFF;
        __try
        {
            return state[0xFA];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0xFF;
        }
    }


    void SetActiveVariant(std::uint8_t partsType, std::uint8_t variantIndex)
    {
        std::uint8_t clamped = variantIndex;

        std::lock_guard<std::mutex> lock(g_Mutex);
        if (IsCustomPartsType(partsType))
        {
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
        }
        else
        {
            const VanillaSuitExtension* x = FindVanillaExt_NoLock(partsType);
            std::uint8_t maxCount = 0;
            if (x)
                for (std::uint8_t pt = 0; pt < kPlayerTypeMax; ++pt)
                    if (x->variantCount[pt] > maxCount)
                        maxCount = x->variantCount[pt];
            if (maxCount == 0) clamped = 0;
            else if (clamped >= maxCount)
                clamped = static_cast<std::uint8_t>(maxCount - 1);
        }
        g_ActiveVariant[partsType] = clamped;
    }

    std::uint8_t GetActiveVariant(std::uint8_t partsType)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        return g_ActiveVariant[partsType];
    }

    void ClearActiveVariant(std::uint8_t partsType)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_ActiveVariant[partsType] = 0;
    }

    bool ExtendVanillaSuitVariants(std::uint8_t vanillaPartsType,
                                   std::uint8_t playerType,
                                   std::uint8_t sourceCamo,
                                   const VanillaSuitVariantAsset* variants,
                                   std::uint8_t count)
    {
        if (IsCustomPartsType(vanillaPartsType) || vanillaPartsType == 0xFF)
            return false;
        if (playerType >= kPlayerTypeMax || !variants || count == 0)
            return false;

        std::lock_guard<std::mutex> lock(g_Mutex);
        BuildReservations_NoLock();
        VanillaSuitExtension* x = FindVanillaExt_NoLock(vanillaPartsType);
        if (!x)
        {
            for (auto& c : g_VanillaExts)
                if (!c.used) { x = &c; break; }
            if (!x) return false;
            x->used             = true;
            x->vanillaPartsType = vanillaPartsType;
            for (std::uint8_t i = 0; i < kMaxVanillaExtVariants; ++i)
                x->variantSourceCamo[i] = 0xFF;
        }

        VanillaSuitVariantAsset incoming[kMaxVanillaExtVariants] = {};
        std::uint8_t incomingCount = 0;
        for (std::uint8_t i = 0;
             i < count && incomingCount < kMaxVanillaExtVariants; ++i)
        {
            if (variants[i].partsPathCode64 == 0
                && variants[i].fpkPathCode64 == 0)
                continue;
            incoming[incomingCount] = variants[i];
            ++incomingCount;
        }
        if (incomingCount == 0) return false;

        int base = -1;
        std::uint8_t blockSize = 0;
        for (std::uint8_t i = 0; i < x->variantSelectorCount; ++i)
        {
            if (x->variantSourceCamo[i] == sourceCamo)
            {
                if (base < 0) base = static_cast<int>(i);
                ++blockSize;
            }
        }

        const bool newBlock = (base < 0);
        if (newBlock)
        {
            std::uint8_t room =
                static_cast<std::uint8_t>(kMaxVanillaExtVariants
                                          - x->variantSelectorCount);
            if (room == 0) return false;
            std::uint8_t take =
                (incomingCount < room) ? incomingCount : room;

            char keyBuf[24];
            std::snprintf(keyBuf, sizeof(keyBuf), "__vext:0x%02X",
                          vanillaPartsType);
            const std::uint64_t keyHash = HashOutfitKey(keyBuf);

            std::uint8_t persisted[14] = {};
            V_FrameWorkState::GetPersistedOutfitVariantSelectors(keyBuf,
                                                                 persisted, 14);
            base = static_cast<int>(x->variantSelectorCount);
            for (std::uint8_t j = 0; j < take; ++j)
            {
                const std::uint8_t slot =
                    static_cast<std::uint8_t>(base + j);
                std::uint8_t sel = (slot < 14) ? persisted[slot]
                                               : std::uint8_t{0};
                if (!IsAllocatableSelector(sel) || IsSelectorTaken_NoLock(sel)
                    || IsSelectorReservedForOther_NoLock(sel, keyHash))
                    sel = AllocateSelector_NoLock(sel, keyHash);
                if (sel == 0xFF || sel == 0) continue;
                x->variantSelectorCodes[slot] = sel;
                x->variantSourceCamo[slot]    = sourceCamo;
                ++blockSize;
                x->variantSelectorCount =
                    static_cast<std::uint8_t>(slot + 1);
            }
            x->variantSelectorCount =
                static_cast<std::uint8_t>(base + blockSize);
            if (blockSize == 0) return false;

            V_FrameWorkState::SetPersistedOutfitVariantSelectors(
                keyBuf, x->variantSelectorCodes, x->variantSelectorCount);
        }
        else if (incomingCount > blockSize)
        {
            const bool isTailBlock =
                static_cast<std::uint8_t>(base + blockSize)
                    == x->variantSelectorCount;
            if (isTailBlock)
            {
                std::uint8_t room =
                    static_cast<std::uint8_t>(kMaxVanillaExtVariants
                                              - x->variantSelectorCount);
                std::uint8_t need =
                    static_cast<std::uint8_t>(incomingCount - blockSize);
                if (need > room) need = room;
                if (need > 0)
                {
                    char keyBuf[24];
                    std::snprintf(keyBuf, sizeof(keyBuf), "__vext:0x%02X",
                                  vanillaPartsType);
                    const std::uint64_t keyHash = HashOutfitKey(keyBuf);
                    std::uint8_t persisted[14] = {};
                    V_FrameWorkState::GetPersistedOutfitVariantSelectors(
                        keyBuf, persisted, 14);
                    for (std::uint8_t g = 0; g < need; ++g)
                    {
                        const std::uint8_t slot = x->variantSelectorCount;
                        std::uint8_t sel = (slot < 14) ? persisted[slot]
                                                       : std::uint8_t{0};
                        if (!IsAllocatableSelector(sel)
                            || IsSelectorTaken_NoLock(sel)
                            || IsSelectorReservedForOther_NoLock(sel, keyHash))
                            sel = AllocateSelector_NoLock(sel, keyHash);
                        if (sel == 0xFF || sel == 0) break;
                        x->variantSelectorCodes[slot] = sel;
                        x->variantSourceCamo[slot]    = sourceCamo;
                        x->variantSelectorCount =
                            static_cast<std::uint8_t>(slot + 1);
                        ++blockSize;
                    }
                    V_FrameWorkState::SetPersistedOutfitVariantSelectors(
                        keyBuf, x->variantSelectorCodes,
                        x->variantSelectorCount);
                }
            }
            else
            {
                Log("[OutfitRegistry] vext partsType=0x%02X sourceCamo=0x%02X "
                    "band (base=%d size=%u) not tail - cannot grow for pt=%u "
                    "(%u variants); %u extra dropped\n",
                    static_cast<unsigned>(vanillaPartsType),
                    static_cast<unsigned>(sourceCamo),
                    base, static_cast<unsigned>(blockSize),
                    static_cast<unsigned>(playerType),
                    static_cast<unsigned>(incomingCount),
                    static_cast<unsigned>(incomingCount - blockSize));
            }
        }

        std::uint8_t filled = 0;
        for (std::uint8_t j = 0; j < blockSize && j < incomingCount; ++j)
        {
            const std::uint8_t slot = static_cast<std::uint8_t>(base + j);
            x->variants[playerType][slot] = incoming[j];
            x->variants[playerType][slot].used = true;
            ++filled;
        }
        if (filled == 0) return false;

        x->variantCount[playerType] =
            static_cast<std::uint8_t>(x->variantSelectorCount + 1);
        return true;
    }

    std::uint8_t VanillaExtVariantCount(std::uint8_t vanillaPartsType,
                                        std::uint8_t playerType)
    {
        if (playerType >= kPlayerTypeMax) return 0;
        std::lock_guard<std::mutex> lock(g_Mutex);
        const VanillaSuitExtension* x = FindVanillaExt_NoLock(vanillaPartsType);
        return x ? x->variantCount[playerType] : std::uint8_t{0};
    }

    std::uint8_t VanillaExtVariantSlotCount(std::uint8_t vanillaPartsType)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        const VanillaSuitExtension* x = FindVanillaExt_NoLock(vanillaPartsType);
        return x ? x->variantSelectorCount : std::uint8_t{0};
    }

    const VanillaSuitVariantAsset* VanillaExtGetVariant(
        std::uint8_t vanillaPartsType, std::uint8_t playerType,
        std::uint8_t variantIdx)
    {
        if (playerType >= kPlayerTypeMax || variantIdx == 0) return nullptr;
        std::lock_guard<std::mutex> lock(g_Mutex);
        const VanillaSuitExtension* x = FindVanillaExt_NoLock(vanillaPartsType);
        if (!x) return nullptr;
        const std::uint8_t slot = static_cast<std::uint8_t>(variantIdx - 1);
        if (slot >= kMaxVanillaExtVariants) return nullptr;
        const auto& v = x->variants[playerType][slot];
        return v.used ? &v : nullptr;
    }

    static std::uint8_t ResolveVextDonor_NoLock(const VanillaSuitExtension& x,
                                                std::uint8_t playerType)
    {
        const std::uint8_t partner =
            (playerType == kPlayerType_Avatar) ? kPlayerType_Snake
          : (playerType == kPlayerType_Snake)  ? kPlayerType_Avatar
                                               : std::uint8_t{0xFF};
        if (partner != 0xFF && x.variantCount[partner] > 0) return partner;
        for (std::uint8_t sp = 0; sp < kPlayerTypeMax; ++sp)
            if (sp != playerType && x.variantCount[sp] > 0) return sp;
        return 0xFF;
    }

    const VanillaSuitVariantAsset* VanillaExtGetVariantBridged(
        std::uint8_t vanillaPartsType, std::uint8_t playerType,
        std::uint8_t variantIdx)
    {
        if (variantIdx == 0 || playerType >= kPlayerTypeMax) return nullptr;
        std::lock_guard<std::mutex> lock(g_Mutex);
        const VanillaSuitExtension* x = FindVanillaExt_NoLock(vanillaPartsType);
        if (!x) return nullptr;
        const std::uint8_t slot = static_cast<std::uint8_t>(variantIdx - 1);
        if (slot >= kMaxVanillaExtVariants) return nullptr;
        auto pick = [&](std::uint8_t pt, std::uint8_t s)
            -> const VanillaSuitVariantAsset*
        {
            if (pt >= kPlayerTypeMax || s >= kMaxVanillaExtVariants)
                return nullptr;
            const auto& v = x->variants[pt][s];
            return v.used ? &v : nullptr;
        };
        auto nearest = [&](std::uint8_t pt)
            -> const VanillaSuitVariantAsset*
        {
            if (const auto* v = pick(pt, slot)) return v;
            for (std::uint8_t s = slot; s > 0; --s)
                if (const auto* v = pick(pt, static_cast<std::uint8_t>(s - 1)))
                    return v;
            for (std::uint8_t s = static_cast<std::uint8_t>(slot + 1);
                 s < kMaxVanillaExtVariants; ++s)
                if (const auto* v = pick(pt, s)) return v;
            return nullptr;
        };
        if (x->variantCount[playerType] > 0)
            if (const auto* v = nearest(playerType))
                return v;
        const std::uint8_t donor = ResolveVextDonor_NoLock(*x, playerType);
        if (donor == 0xFF) return nullptr;
        return nearest(donor);
    }

    std::uint8_t VanillaExtCollectSelectorSeeds(std::uint8_t* outSelectors,
                                                std::uint8_t* outSourceCamos,
                                                std::uint8_t maxCount)
    {
        if (!outSelectors || !outSourceCamos || maxCount == 0) return 0;
        std::lock_guard<std::mutex> lock(g_Mutex);
        std::uint8_t n = 0;
        for (const auto& x : g_VanillaExts)
        {
            if (!x.used) continue;
            for (std::uint8_t i = 0;
                 i < x.variantSelectorCount && n < maxCount; ++i)
            {
                const std::uint8_t sel = x.variantSelectorCodes[i];
                const std::uint8_t src = x.variantSourceCamo[i];
                if (sel < kCustomSelectorStart || sel > kCustomSelectorEnd)
                    continue;
                if (src == 0xFF) continue;
                outSelectors[n]   = sel;
                outSourceCamos[n] = src;
                ++n;
            }
        }
        return n;
    }

    std::uint8_t VanillaExtResolveVariantDonor(std::uint8_t vanillaPartsType,
                                               std::uint8_t playerType)
    {
        if (playerType >= kPlayerTypeMax) return 0xFF;
        std::lock_guard<std::mutex> lock(g_Mutex);
        const VanillaSuitExtension* x = FindVanillaExt_NoLock(vanillaPartsType);
        if (!x) return 0xFF;
        if (x->variantCount[playerType] > 0) return playerType;
        return ResolveVextDonor_NoLock(*x, playerType);
    }

    std::uint8_t VanillaExtGetVariantSelector(std::uint8_t vanillaPartsType,
                                              std::uint8_t variantIdx)
    {
        if (variantIdx == 0) return 0;
        std::lock_guard<std::mutex> lock(g_Mutex);
        const VanillaSuitExtension* x = FindVanillaExt_NoLock(vanillaPartsType);
        if (!x) return 0;
        const std::uint8_t i = static_cast<std::uint8_t>(variantIdx - 1);
        if (i >= x->variantSelectorCount) return 0;
        return x->variantSelectorCodes[i];
    }

    std::uint8_t VanillaExtGetVariantSourceCamo(std::uint8_t vanillaPartsType,
                                                std::uint8_t variantIdx)
    {
        if (variantIdx == 0) return 0xFF;
        std::lock_guard<std::mutex> lock(g_Mutex);
        const VanillaSuitExtension* x = FindVanillaExt_NoLock(vanillaPartsType);
        if (!x) return 0xFF;
        const std::uint8_t i = static_cast<std::uint8_t>(variantIdx - 1);
        if (i >= x->variantSelectorCount) return 0xFF;
        return x->variantSourceCamo[i];
    }

    void ResetAllVanillaExtVariants(std::uint8_t exceptPartsType)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (auto& x : g_VanillaExts)
        {
            if (!x.used) continue;
            if (x.vanillaPartsType == exceptPartsType) continue;
            g_ActiveVariant[x.vanillaPartsType] = 0;
        }
    }

    bool TryGetVanillaExtByVariantSelector(std::uint8_t selector,
                                           std::uint8_t* outPartsType,
                                           std::uint8_t* outVariantIdx)
    {
        if (!IsCustomSelector(selector)) return false;
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const auto& x : g_VanillaExts)
        {
            if (!x.used) continue;
            for (std::uint8_t i = 0; i < x.variantSelectorCount; ++i)
            {
                if (x.variantSelectorCodes[i] == selector)
                {
                    if (outPartsType)  *outPartsType  = x.vanillaPartsType;
                    if (outVariantIdx) *outVariantIdx =
                        static_cast<std::uint8_t>(i + 1);
                    return true;
                }
            }
        }
        return false;
    }

    bool ExtendVanillaSuitHeadOptions(std::uint8_t vanillaPartsType,
                                      std::uint8_t playerType,
                                      const std::uint16_t* equipIds,
                                      std::uint8_t idCount,
                                      const std::uint64_t* pendingHashes,
                                      std::uint8_t pendingCount)
    {
        if (IsCustomPartsType(vanillaPartsType) || vanillaPartsType == 0xFF)
            return false;
        if (playerType >= kPlayerTypeMax)
            return false;
        if (idCount == 0 && pendingCount == 0)
            return false;

        std::lock_guard<std::mutex> lock(g_Mutex);
        VanillaSuitExtension* x = FindVanillaExt_NoLock(vanillaPartsType);
        if (!x)
        {
            for (auto& c : g_VanillaExts)
                if (!c.used) { x = &c; break; }
            if (!x) return false;
            x->used             = true;
            x->vanillaPartsType = vanillaPartsType;
        }

        auto& b = x->perPlayerType[playerType];
        b.declared = true;
        for (std::uint8_t i = 0; equipIds && i < idCount; ++i)
        {
            const std::uint16_t id = equipIds[i];
            if (id == 0) continue;
            bool dup = false;
            for (std::uint8_t j = 0; j < b.headOptionCount; ++j)
                if (b.headOptionEquipIds[j] == id) { dup = true; break; }
            if (!dup && b.headOptionCount < kMaxHeadOptionsPerOutfit)
                b.headOptionEquipIds[b.headOptionCount++] = id;
        }
        for (std::uint8_t i = 0; pendingHashes && i < pendingCount; ++i)
        {
            const std::uint64_t h = pendingHashes[i];
            if (h == 0) continue;
            bool dup = false;
            for (std::uint8_t j = 0; j < b.pendingHeadCount; ++j)
                if (b.pendingHeadNameHashes[j] == h) { dup = true; break; }
            if (!dup && b.pendingHeadCount < kMaxHeadOptionsPerOutfit)
                b.pendingHeadNameHashes[b.pendingHeadCount++] = h;
        }
        return true;
    }

    bool ExtendVanillaSuitVoice(std::uint8_t vanillaPartsType,
                                std::uint8_t playerType,
                                std::uint8_t sourceCamo,
                                std::uint64_t voiceFpk)
    {
        if (IsCustomPartsType(vanillaPartsType) || vanillaPartsType == 0xFF)
            return false;
        if (playerType >= kPlayerTypeMax)
            return false;
        if (voiceFpk <= kSubAssetUseVanilla)
            return false;

        std::lock_guard<std::mutex> lock(g_Mutex);
        VanillaSuitExtension* x = FindVanillaExt_NoLock(vanillaPartsType);
        if (!x)
        {
            for (auto& c : g_VanillaExts)
                if (!c.used) { x = &c; break; }
            if (!x) return false;
            x->used             = true;
            x->vanillaPartsType = vanillaPartsType;
        }
        x->suitVoiceFpk[playerType]  = voiceFpk;
        x->suitVoiceCamo[playerType] = sourceCamo;
        return true;
    }

    std::uint64_t VanillaExtGetSuitVoiceFpk(std::uint8_t vanillaPartsType,
                                            std::uint8_t playerType,
                                            std::uint8_t wornCamo)
    {
        if (playerType >= kPlayerTypeMax) return 0;
        std::lock_guard<std::mutex> lock(g_Mutex);
        const VanillaSuitExtension* x = FindVanillaExt_NoLock(vanillaPartsType);
        if (!x) return 0;

        std::uint8_t src = playerType;
        if (x->suitVoiceFpk[src] <= kSubAssetUseVanilla)
        {
            const std::uint8_t partner =
                (playerType == kPlayerType_Avatar) ? kPlayerType_Snake
              : (playerType == kPlayerType_Snake)  ? kPlayerType_Avatar
                                                   : std::uint8_t{0xFF};
            src = 0xFF;
            if (partner != 0xFF
                && x->suitVoiceFpk[partner] > kSubAssetUseVanilla)
            {
                src = partner;
            }
            else
            {
                for (std::uint8_t sp = 0; sp < kPlayerTypeMax; ++sp)
                    if (sp != playerType
                        && x->suitVoiceFpk[sp] > kSubAssetUseVanilla)
                    { src = sp; break; }
            }
            if (src == 0xFF) return 0;
        }

        const std::uint8_t scope = x->suitVoiceCamo[src];
        if (scope != 0xFF && wornCamo != scope)
        {
            bool match = false;
            for (std::uint8_t s = 0; s < x->variantSelectorCount
                 && s < kMaxVanillaExtVariants; ++s)
            {
                if (x->variantSelectorCodes[s] == wornCamo)
                {
                    match = (x->variantSourceCamo[s] == scope);
                    break;
                }
            }
            if (!match) return 0;
        }
        return x->suitVoiceFpk[src];
    }

    bool VanillaExtHasAnyHeadOptions(std::uint8_t vanillaPartsType,
                                     std::uint8_t playerType)
    {
        if (playerType >= kPlayerTypeMax) return false;
        std::lock_guard<std::mutex> lock(g_Mutex);
        const VanillaSuitExtension* x = FindVanillaExt_NoLock(vanillaPartsType);
        return x && x->perPlayerType[playerType].headOptionCount > 0;
    }

    bool VanillaExtHasHeadOption(std::uint8_t vanillaPartsType,
                                 std::uint16_t equipId,
                                 std::uint8_t playerType)
    {
        if (playerType >= kPlayerTypeMax || equipId == 0) return false;
        std::lock_guard<std::mutex> lock(g_Mutex);
        const VanillaSuitExtension* x = FindVanillaExt_NoLock(vanillaPartsType);
        if (!x) return false;
        const auto& b = x->perPlayerType[playerType];
        for (std::uint8_t j = 0; j < b.headOptionCount; ++j)
            if (b.headOptionEquipIds[j] == equipId) return true;
        return false;
    }

    bool VanillaExtGetHeadOptions(std::uint8_t vanillaPartsType,
                                  std::uint8_t playerType,
                                  const std::uint16_t** outEquipIds,
                                  std::uint8_t* outCount)
    {
        if (outEquipIds) *outEquipIds = nullptr;
        if (outCount)    *outCount    = 0;
        if (playerType >= kPlayerTypeMax) return false;
        std::lock_guard<std::mutex> lock(g_Mutex);
        const VanillaSuitExtension* x = FindVanillaExt_NoLock(vanillaPartsType);
        if (!x) return false;
        const auto& b = x->perPlayerType[playerType];
        if (b.headOptionCount == 0) return false;
        if (outEquipIds) *outEquipIds = b.headOptionEquipIds;
        if (outCount)    *outCount    = b.headOptionCount;
        return true;
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

    void SetWornCustomHeadSlot(std::uint8_t slot)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_WornCustomHeadSlot = slot;
    }

    std::uint8_t GetWornCustomHeadSlot()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        return g_WornCustomHeadSlot;
    }

    void ClearWornCustomHeadSlot()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_WornCustomHeadSlot = 0;
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

    std::uint8_t PeekPendingSupplyDropVariantIdx()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        return g_PendingSupplyDropVariantIdx;
    }

    void SetCrateDeliveredVariant(std::uint16_t developId,
                                  std::uint8_t variantIndex)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_CrateDeliveredDevelopId  = developId;
        g_CrateDeliveredVariantIdx = variantIndex;
    }

    std::uint16_t PeekCrateDeliveredDevelopId()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        return g_CrateDeliveredDevelopId;
    }

    std::uint8_t PeekCrateDeliveredVariantIdx()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        return g_CrateDeliveredVariantIdx;
    }

    void ClearCrateDeliveredVariant()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_CrateDeliveredDevelopId  = 0;
        g_CrateDeliveredVariantIdx = 0;
    }
}
