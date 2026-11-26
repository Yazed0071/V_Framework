#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "OutfitRegistry.h"

namespace outfit
{


    constexpr std::uint8_t  kCustomHeadSlotBase = 0x06;


    constexpr std::uint16_t kDefaultTppEnemyFaceId     = 0x22D;
    constexpr std::uint16_t kDefaultTppEnemyFaceIdMale = 0x22A;

    constexpr std::uint8_t kHeadLayerKeepFace     = 0x01;
    constexpr std::uint8_t kHeadLayerKeepFaceDeco = 0x02;
    constexpr std::uint8_t kHeadLayerKeepHair     = 0x04;
    constexpr std::uint8_t kHeadLayerKeepHairDeco = 0x08;
    constexpr std::uint8_t kHeadLayerDeclared     = 0x80;

    inline bool IsDdPlayerType(std::uint8_t playerType)
    {
        return playerType == kPlayerType_DDMale || playerType == kPlayerType_DDFemale;
    }

    struct HeadLayerPaths
    {
        std::uint64_t faceFova     = 0;
        std::uint64_t faceDecoFova = 0;
        std::uint64_t hairFova     = 0;
        std::uint64_t hairDecoFova = 0;
        std::uint64_t faceFovaFpk     = 0;
        std::uint64_t faceDecoFovaFpk = 0;
        std::uint64_t hairFovaFpk     = 0;
        std::uint64_t hairDecoFovaFpk = 0;
    };

    struct CustomHeadEntry
    {
        bool           used            = false;
        bool           provisional     = false;
        bool           hiddenInDevelop = false;
        bool           rehomeBlockLogged = false;
        std::uint16_t  equipId      = 0;
        std::uint16_t  developId    = 0;
        std::uint16_t  flowIndex    = 0;
        std::uint8_t   slotByte     = 0;   // 0 while it borrows none
        std::uint8_t   savedSlot    = 0;   // the slot the state file names for it
        std::uint64_t  lastUse      = 0;   // GetTickCount64 of the last use of its slot

        std::uint16_t  TppEnemyFaceId[kPlayerTypeMax] = {};

        std::uint64_t  faceFv2Code[kPlayerTypeMax] = {};
        std::uint64_t  faceFpkCode[kPlayerTypeMax] = {};

        std::uint8_t   faceLayerFlags[kPlayerTypeMax] = {};
        HeadLayerPaths layerPaths[kPlayerTypeMax] = {};

        char           name[64]     = { 0 };
    };


    std::uint16_t RegisterHeadOption(
        const char* name,
        const std::uint16_t* TppEnemyFaceIdsPerPt,
        const std::uint64_t* faceFv2CodesPerPt = nullptr,
        const std::uint64_t* faceFpkCodesPerPt = nullptr,
        bool showInDevelopMenu = false,
        const std::uint8_t* faceLayerFlagsPerPt = nullptr,
        const HeadLayerPaths* layerPathsPerPt = nullptr);

    struct DdHeadLayers
    {
        std::uint8_t  flags       = 0;
        std::uint16_t enemyFaceId = 0;
        std::uint64_t branchFv2   = 0;
        std::uint64_t branchFpk   = 0;
        const char*   name        = nullptr;
        HeadLayerPaths paths{};
    };

    bool TryGetDdHeadLayers(std::uint8_t slotByte, std::uint8_t playerType,
                            DdHeadLayers* out);

    bool TryGetDdHeadBranch(std::uint8_t slotByte, std::uint8_t playerType,
                            DdHeadLayers* out);

    bool IsFv2PathCode(std::uint64_t pathCode64Ext);
    bool IsFpkPathCode(std::uint64_t pathCode64Ext);

    inline bool HasBranchModelPair(const DdHeadLayers& layers)
    {
        return IsFv2PathCode(layers.branchFv2) && IsFpkPathCode(layers.branchFpk);
    }

    inline bool ReplacesFaceWithBranchModel(const DdHeadLayers& layers)
    {
        return (layers.flags & kHeadLayerKeepFace) == 0 && HasBranchModelPair(layers);
    }

    constexpr std::uint8_t kSnakeFaceStageCount = 3;

    void SetCustomHeadSnakeFaceStages(const char* name,
                                      const std::uint64_t* fv2ByStage,
                                      const std::uint64_t* fpkByStage);
    std::uint64_t GetCustomHeadSnakeStageFv2(const char* name,
                                             std::uint32_t stage);
    std::uint64_t GetCustomHeadSnakeStageFpk(const char* name,
                                             std::uint32_t stage);

    void SetCustomHeadInfiniteAmmo(const char* name, bool enable);
    bool GetCustomHeadInfiniteAmmo(const char* name);

    void SetCustomHeadMissionAbilities(const char* name, bool stealthAssist, bool chickCap);
    bool GetCustomHeadStealthAssist(const char* name);
    bool GetCustomHeadChickCap(const char* name);

    void SetCustomHeadSaluteVoice(const char* name, const char* line);
    std::string GetCustomHeadSaluteVoice(const char* name);

    struct CustomHeadHat
    {
        std::uint64_t fpk      = 0;
        std::uint64_t fv2      = 0;
        bool          hideHair = false;
    };

    void SetCustomHeadHat(const char* name, std::uint8_t playerType, const CustomHeadHat& hat);
    bool GetCustomHeadHat(const char* name, std::uint8_t playerType, CustomHeadHat* out);

    void SetCustomHeadBranchMask(const char* name, std::uint8_t mask);
    std::uint8_t GetCustomHeadBranchMask(const char* name);

    struct HeadColorSet
    {
        OutfitColorList colors[kColorChannelCount];
    };

    void SetCustomHeadColors(const char* name, const HeadColorSet (&perPlayerType)[kPlayerTypeMax]);
    std::shared_ptr<const HeadColorSet> GetCustomHeadColors(const char* name, std::uint8_t playerType);


    int DrainPendingHeads();

    bool HasPendingCustomHeads();

    void NoteDevelopRowsMoved();

    bool IsCustomHeadSlotReserved(std::uint8_t slotByte);


    const CustomHeadEntry* TryGetCustomHeadByName(const char* name);


    const CustomHeadEntry* TryGetCustomHeadByEquipId(std::uint16_t equipId);


    const CustomHeadEntry* TryGetCustomHeadBySlot(std::uint8_t slotByte);

    // the head's slot, borrowed from the least used head when it has none; 0 when every slot is in use
    std::uint8_t BorrowCustomHeadSlot(std::uint16_t equipId, bool wearing);

    std::vector<const CustomHeadEntry*> ListCustomHeads();


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
