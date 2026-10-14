#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace EquipIdCompression
{
    constexpr std::int32_t kCompressedSlotBound = 0x289;

    constexpr std::int32_t ComputeCompressed(std::int32_t equipId)
    {
        if (equipId < 0)         return -1;
        if (equipId < 0x400)     return equipId;
        if (equipId < 0x600)     return equipId - 0x1D0;
        return equipId - 0x380;
    }

    inline bool IsCompressedInBounds(std::int32_t compressed)
    {
        return compressed >= 0 && compressed < kCompressedSlotBound;
    }

    inline bool IsEquipIdSafeForNativeTable(std::int32_t equipId)
    {
        return IsCompressedInBounds(ComputeCompressed(equipId));
    }

    constexpr std::int32_t kItemBandFirst        = 1;
    constexpr std::int32_t kItemBandLast         = 0x22F;
    constexpr std::int32_t kSupportWeaponHoleFirst = 0x15A;   // narrow custom support weapons took native holes from here
    constexpr std::int32_t kWeaponBandFirst      = 0x230;
    constexpr std::int32_t kWeaponBandLastUsable = 0x288;
    static_assert(kWeaponBandLastUsable == kCompressedSlotBound - 1,
                  "weapon band must end at the native table bound");
    static_assert(kItemBandLast + 1 == kWeaponBandFirst,
                  "band split must be contiguous");

    constexpr std::int32_t kItemHandWindowFirst = 0x1C2;
    constexpr std::int32_t kItemHandWindowLast  = 0x209;

    inline bool IsLegalNativeCustomId(std::int32_t equipId)
    {
        return equipId >= kItemBandFirst && equipId <= kItemBandLast
            && (equipId < kItemHandWindowFirst || equipId > kItemHandWindowLast);
    }

    void  MarkCompressedSlotUsed(std::int32_t compressed);
    void  ClearCompressedSlotUsed(std::int32_t compressed);
    bool  IsCompressedSlotUsed(std::int32_t compressed);

    std::size_t SyncFromNativeTable();

    constexpr std::int32_t kChimeraEquipIdFirst = 0x367;
    constexpr std::int32_t kChimeraEquipIdLast  = 0x36C;


    constexpr std::int32_t kNoEquipId            = 0x7FF;
    constexpr std::int32_t kExtendedEquipIdFirst = kCompressedSlotBound;
    constexpr std::int32_t kExtendedEquipIdLast  = kNoEquipId - 1;
    constexpr std::int32_t kExtendedAllocFirst   = 0x609;
    constexpr std::int32_t kExtendedAllocLast    = kExtendedEquipIdLast;
    constexpr std::int32_t kExtendedPreferredFirst = 0x700; // handed out first, so packs that fit here mint the ids they always did
    constexpr std::int32_t kExtendedChimeraHoleFirst = kChimeraEquipIdFirst + 0x380;
    constexpr std::int32_t kExtendedChimeraHoleLast  = kChimeraEquipIdLast + 0x380;

    static_assert(kExtendedAllocLast < kNoEquipId,
                  "0x7FF is the engine's no-equip value: equip handles, pickable "
                  "info and player EquipState all carry the equipId in 11 bits, so "
                  "an allocated 0x7FF reads as an empty slot and anything above it "
                  "truncates onto a vanilla equipId");
    static_assert(kExtendedAllocLast <= kExtendedEquipIdLast,
                  "allocation must stay inside the extended storage range");
    constexpr std::int32_t kExtendedFoldedFirst  =
        ComputeCompressed(kExtendedAllocFirst);
    constexpr std::int32_t kExtendedFoldedLast   =
        ComputeCompressed(kExtendedEquipIdLast);

    static_assert(kExtendedFoldedFirst >= kCompressedSlotBound &&
                  ComputeCompressed(kExtendedAllocFirst - 1) < kCompressedSlotBound,
                  "extended ids must fold clear of every vanilla row: vanilla folds "
                  "all land below the native table bound, and 0x609 is the first id "
                  "of the 0x600 family that lands past it");
    static_assert(kExtendedAllocFirst >= 0x600 &&
                  kExtendedFoldedLast - kExtendedFoldedFirst ==
                      kExtendedAllocLast - kExtendedAllocFirst,
                  "the whole band must take the single -0x380 fold, so no two "
                  "allocated ids share an InfoList mirror row");
    static_assert(kExtendedAllocFirst > kChimeraEquipIdLast &&
                  ComputeCompressed(kExtendedChimeraHoleFirst) == kChimeraEquipIdFirst &&
                  ComputeCompressed(kExtendedChimeraHoleLast) == kChimeraEquipIdLast,
                  "extended ids must clear the chimera band raw AND folded - an id "
                  "inside it is rewritten to a gunsmith work-slot id at loadout spawn, "
                  "so the ids folding onto it are never allocated");
    static_assert(kExtendedPreferredFirst > kExtendedChimeraHoleLast &&
                  kExtendedPreferredFirst <= kExtendedAllocLast,
                  "the preferred band must sit above the chimera hole");

    inline bool IsExtendedEquipId(std::int32_t equipId)
    {
        return equipId >= kExtendedEquipIdFirst && equipId <= kExtendedEquipIdLast;
    }

    constexpr std::int32_t kWideEquipIdFirst = kNoEquipId + 1;
    constexpr std::int32_t kWideEquipIdLast  = 0xFFFF;
    constexpr std::int32_t kWideFoldedLast   = ComputeCompressed(kWideEquipIdLast);
    constexpr std::int32_t kAliasPoolFirst   = kExtendedPreferredFirst;
    constexpr std::int32_t kAliasPoolLast    = 0x7FD;
    constexpr std::int32_t kNarrowCustomLast = kAliasPoolFirst - 1;
    constexpr std::int32_t kWideAllocLast    = kWideEquipIdLast;

    static_assert(ComputeCompressed(kWideEquipIdFirst) > kExtendedFoldedLast,
                  "wide ids must fold past every extended row, so a wide row never "
                  "lands on an id the 11-bit band can hold");
    static_assert(kAliasPoolFirst > kExtendedChimeraHoleLast && kAliasPoolLast < kExtendedEquipIdLast,
                  "the alias band must clear the chimera hole and leave 0x7FE free");

    bool WideWeaponsLive();

    inline bool IsAllocatableExtendedId(std::int32_t equipId)
    {
        const std::int32_t last = WideWeaponsLive() ? kNarrowCustomLast : kExtendedAllocLast;
        return equipId >= kExtendedAllocFirst && equipId <= last
            && (equipId < kExtendedChimeraHoleFirst || equipId > kExtendedChimeraHoleLast);
    }

    inline bool IsWideEquipId(std::int32_t equipId)
    {
        return equipId >= kWideEquipIdFirst && equipId <= kWideEquipIdLast;
    }

    inline bool IsAllocatableWideId(std::int32_t equipId)
    {
        return IsWideEquipId(equipId) && equipId <= kWideAllocLast
            && (equipId & kNoEquipId) < kAliasPoolFirst;
    }

    inline bool IsAliasPoolId(std::int32_t equipId)
    {
        return equipId >= kAliasPoolFirst && equipId <= kAliasPoolLast;
    }

    using ExtendedReservedFn = std::function<bool(std::int32_t)>;

    bool  MarkExtendedEquipIdUsed(std::int32_t equipId);
    bool  IsExtendedEquipIdUsed(std::int32_t equipId);
    std::int32_t FindLowestFreeExtendedEquipId();
    std::int32_t FindLowestFreeExtendedEquipId(
        const ExtendedReservedFn& isReservedElsewhere);

    template <typename SessionUsedFn>
    inline std::int32_t FindLowestFreeEquipId(SessionUsedFn isSessionUsed,
                                              std::int32_t minimumEquipId = 0)
    {
        const std::int32_t start = (minimumEquipId > 0) ? minimumEquipId : 0;
        for (std::int32_t equipId = start;
             equipId < kCompressedSlotBound;
             ++equipId)
        {
            if (equipId >= kChimeraEquipIdFirst && equipId <= kChimeraEquipIdLast)
                continue;
            if (equipId == 0x400) continue;
            if (IsCompressedSlotUsed(equipId)) continue;
            if (isSessionUsed(equipId))        continue;
            return equipId;
        }
        return -1;
    }

    template <typename SessionUsedFn>
    inline std::int32_t FindLowestFreeEquipIdInRange(SessionUsedFn isSessionUsed,
                                                     std::int32_t first,
                                                     std::int32_t last)
    {
        if (first < 0) first = 0;
        if (last >= kCompressedSlotBound) last = kCompressedSlotBound - 1;
        for (std::int32_t equipId = first; equipId <= last; ++equipId)
        {
            if (equipId >= kChimeraEquipIdFirst && equipId <= kChimeraEquipIdLast)
                continue;
            if (equipId == 0x400) continue;
            if (IsCompressedSlotUsed(equipId)) continue;
            if (isSessionUsed(equipId))        continue;
            return equipId;
        }
        return -1;
    }

}
