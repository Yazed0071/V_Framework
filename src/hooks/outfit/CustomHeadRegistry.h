#pragma once

#include <cstddef>
#include <cstdint>

#include "OutfitRegistry.h"

namespace outfit
{


    constexpr std::uint8_t  kCustomHeadSlotBase = 0x06;
    constexpr std::size_t   kMaxCustomHeads     = 250;
    static_assert(kCustomHeadSlotBase + kMaxCustomHeads <= 0x100,
                  "slot byte allocations must fit in a uint8");


    constexpr std::uint16_t kDefaultTppEnemyFaceId = 0x22D;

    struct CustomHeadEntry
    {
        bool           used         = false;
        std::uint16_t  equipId      = 0;
        std::uint16_t  developId    = 0;
        std::uint16_t  flowIndex    = 0;
        std::uint8_t   slotByte     = 0;

        std::uint16_t  TppEnemyFaceId[kPlayerTypeMax] = {};


        std::uint64_t  langNameHash = 0;
        std::uint64_t  iconFtexCode = 0;
        char           name[64]     = { 0 };
    };


    std::uint16_t RegisterHeadOption(
        const char* name,
        const std::uint16_t* TppEnemyFaceIdsPerPt,
        std::uint64_t langNameHash = 0,
        std::uint64_t iconFtexCode = 0,
        std::uint16_t explicitDevelopId = 0,
        bool showInDevelopMenu = false);


    const CustomHeadEntry* TryGetCustomHeadByName(const char* name);


    const CustomHeadEntry* TryGetCustomHeadByEquipId(std::uint16_t equipId);


    const CustomHeadEntry* TryGetCustomHeadBySlot(std::uint8_t slotByte);


    void SetCustomHeadSummaryDisplay(std::uint16_t developId,
                                     std::uint64_t nameHash,
                                     std::uint64_t iconHash);

    std::uint16_t GetCurrentWornHeadEquipId();

    bool GetCurrentEquippedSuitBytes(std::uint8_t* outPartsType,
                                     std::uint8_t* outSelector);


    inline bool IsCustomHeadEquipId(std::uint16_t equipId)
    {
        return TryGetCustomHeadByEquipId(equipId) != nullptr;
    }
    inline bool IsCustomHeadSlot(std::uint8_t slotByte)
    {
        return TryGetCustomHeadBySlot(slotByte) != nullptr;
    }
    inline bool IsCustomHeadFaceFv2(std::uint16_t faceFv2)
    {
        return IsCustomHeadEquipId(faceFv2);
    }
}
