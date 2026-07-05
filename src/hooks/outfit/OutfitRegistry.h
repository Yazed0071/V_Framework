#pragma once

#include <cstddef>
#include <cstdint>

namespace outfit
{
    constexpr std::uint8_t kCustomPartsTypeStart = 0x40;
    constexpr std::uint8_t kCustomPartsTypeEnd   = 0x7F;
    constexpr std::uint8_t kCustomSelectorStart  = 0x80;
    constexpr std::uint8_t kCustomSelectorEnd    = 0xFE;

    constexpr std::uint8_t kVanillaEventCamoStart = 0x83;
    constexpr std::uint8_t kVanillaEventCamoEnd   = 0x88;


    constexpr std::uint64_t kSubAssetDisabled   = 0;
    constexpr std::uint64_t kSubAssetUseVanilla = 1;


    constexpr std::uint8_t kPlayerType_Snake     = 0;
    constexpr std::uint8_t kPlayerType_DDMale    = 1;
    constexpr std::uint8_t kPlayerType_DDFemale  = 2;
    constexpr std::uint8_t kPlayerType_Avatar    = 3;
    constexpr std::uint8_t kPlayerTypeMax        = 4;


    constexpr std::size_t  kCamoMaterialCount       = 82;
    constexpr std::uint8_t kVanillaCamoTypeMax      = 116;
    constexpr std::uint8_t kCamoVirtualIdStart      = 200;
    constexpr std::uint8_t kCamoVirtualIdEnd        = 254;
    constexpr std::uint8_t kCamoBonusTypeUnset      = 0xFF;


    constexpr std::size_t  kMaxOutfits = 128;


    constexpr std::size_t  kMaxHeadOptionsPerOutfit = 8;


    constexpr std::size_t  kMaxVariantsPerOutfit    = 15;
    constexpr std::uint16_t kHeadOption_None        = 0x400;
    constexpr std::uint16_t kHeadOption_Balaclava   = 0x210;


    struct OutfitVariant
    {
        bool           used               = false;
        std::uint64_t  partsPathCode64    = 0;
        std::uint64_t  fpkPathCode64      = 0;
        std::uint64_t  camoFpk            = kSubAssetUseVanilla;
        std::uint64_t  camoFv2            = kSubAssetUseVanilla;
        std::uint64_t  diamondFpk         = kSubAssetDisabled;
        std::uint64_t  diamondFv2         = kSubAssetUseVanilla;
        std::uint64_t  voiceFpk           = kSubAssetUseVanilla;
        std::uint64_t  displayNameHash    = 0;
        std::uint16_t  headOptionEquipIds[kMaxHeadOptionsPerOutfit] = {};
        std::uint8_t   headOptionCount     = 0;
        bool           headOptionsDeclared = false;
        std::uint64_t  pendingHeadNameHashes[kMaxHeadOptionsPerOutfit] = {};
        std::uint8_t   pendingHeadCount    = 0;
    };
    struct OutfitPlayerTypeData
    {
        bool           used                = false;
        std::uint64_t  partsPathCode64     = 0;
        std::uint64_t  fpkPathCode64       = 0;
        std::uint64_t  camoFpk             = kSubAssetDisabled;
        std::uint64_t  faceFpk             = kSubAssetUseVanilla;
        std::uint64_t  skinFv2             = kSubAssetUseVanilla;
        std::uint64_t  diamondFpk          = kSubAssetDisabled;
        std::uint64_t  voiceFpk            = kSubAssetUseVanilla;
        std::uint64_t  camoFv2             = kSubAssetUseVanilla;
        std::uint64_t  diamondFv2          = kSubAssetUseVanilla;
        std::uint64_t  baseDisplayNameHash = 0;
        OutfitVariant  variants[kMaxVariantsPerOutfit] = {};
        std::uint8_t   variantCount                    = 0;
        bool           enableArm              = true;
        bool           enableHead             = false;
        std::uint16_t  headOptionEquipIds[kMaxHeadOptionsPerOutfit] = {};
        std::uint8_t   headOptionCount                              = 0;
        bool           supportsHeadOptions                          = false;
        std::uint64_t  pendingHeadNameHashes[kMaxHeadOptionsPerOutfit] = {};
        std::uint8_t   pendingHeadCount                             = 0;
        std::uint8_t   camoBonusType                              = kCamoBonusTypeUnset;
        std::int32_t   camoBonusValues[kCamoMaterialCount]        = {};
        bool           hasCamoBonusValues                         = false;
    };
    struct OutfitDefinition
    {
        const char*    key             = nullptr;

        std::uint16_t  developId       = 0;
        std::uint16_t  flowIndex       = 0;


        std::uint8_t   partsTypeHint    = 0xFF;
        std::uint8_t   selectorCodeHint = 0xFF;

        std::uint8_t   variantSelectorHints[kMaxVariantsPerOutfit] = {};


        OutfitPlayerTypeData perPlayerType[kPlayerTypeMax] = {};
    };


    struct OutfitEntry
    {
        bool           used              = false;

        std::uint16_t  developId         = 0;
        std::uint16_t  flowIndex         = 0;

        std::uint8_t   partsType         = 0;
        std::uint8_t   selectorCode      = 0;


        OutfitPlayerTypeData perPlayerType[kPlayerTypeMax] = {};

        std::uint8_t   variantSelectorCodes[kMaxVariantsPerOutfit] = {};
        std::uint8_t   variantCount                                 = 0;
        std::uint64_t  displaySummaryNameHash                       = 0;
        std::uint64_t  displaySummaryIconHash                       = 0;
        const OutfitPlayerTypeData* GetPTData(std::uint8_t playerType) const;
        bool IsPlayerTypeSupported(std::uint8_t playerType) const;


        bool HasVariants()       const { return variantCount > 0; }
        bool IsArmEnabled(std::uint8_t playerType)        const;
        bool IsHeadEnabled(std::uint8_t playerType)       const;
        bool IsCamoCustom(std::uint8_t playerType)        const;
        bool IsCamoFv2Custom(std::uint8_t playerType)     const;
        bool IsFaceEnabled(std::uint8_t playerType)       const;
        bool IsDiamondEnabled(std::uint8_t playerType)    const;
        bool IsDiamondCustom(std::uint8_t playerType)     const;
        bool IsDiamondFv2Custom(std::uint8_t playerType)  const;
        bool IsVoiceCustom(std::uint8_t playerType)       const;
        bool HasHeadOptions(std::uint8_t playerType)      const;
        bool HasHeadOption(std::uint16_t equipId, std::uint8_t playerType) const;
        bool HasAnyHeadOptions() const;

        bool GetHeadOptionsFor(std::uint8_t playerType,
                               const std::uint16_t** outEquipIds,
                               std::uint8_t* outCount) const;

        bool GetHeadOptionsForVariant(std::uint8_t playerType, std::uint8_t variant,
                                      const std::uint16_t** outEquipIds,
                                      std::uint8_t* outCount) const;
        bool HasHeadOptionsForVariant(std::uint8_t playerType,
                                      std::uint8_t variant) const;
        bool HasHeadOptionForVariant(std::uint16_t equipId, std::uint8_t playerType,
                                     std::uint8_t variant) const;

        bool HasHeadOptionAnyVariant(std::uint16_t equipId,
                                     std::uint8_t playerType) const;


        std::uint64_t GetVariantPartsPath(std::uint8_t playerType, std::uint8_t variantIdx) const;
        std::uint64_t GetVariantFpkPath(std::uint8_t playerType, std::uint8_t variantIdx) const;
        std::uint64_t GetVariantCamoFpk(std::uint8_t playerType, std::uint8_t variantIdx) const;
        std::uint64_t GetVariantCamoFv2(std::uint8_t playerType, std::uint8_t variantIdx) const;
        std::uint64_t GetVariantDiamondFpk(std::uint8_t playerType, std::uint8_t variantIdx) const;
        std::uint64_t GetVariantDiamondFv2(std::uint8_t playerType, std::uint8_t variantIdx) const;
        std::uint64_t GetVariantVoiceFpk(std::uint8_t playerType, std::uint8_t variantIdx) const;
        std::uint64_t GetVariantDisplayNameHash(std::uint8_t playerType, std::uint8_t variantIdx) const;


        std::uint8_t  GetVariantCountFor(std::uint8_t playerType) const;


        std::uint8_t  GetCamoBonusType(std::uint8_t playerType) const;
        bool          HasCamoBonusValuesFor(std::uint8_t playerType) const;
        const std::int32_t* GetCamoBonusValuesFor(std::uint8_t playerType) const;
    };


    bool RegisterOutfit(const OutfitDefinition& def,
                        std::uint8_t* outAllocatedPartsType);

    void ClearAllOutfits();


    bool TryGetOutfitByPartsType(std::uint8_t partsType,
                                 const OutfitEntry** outEntry);

    bool TryGetOutfitBySelectorCode(std::uint8_t selectorCode,
                                    const OutfitEntry** outEntry);


    bool TryGetOutfitByVariantSelector(std::uint8_t selectorCode,
                                       const OutfitEntry** outEntry,
                                       std::uint8_t* outVariantIndex);


    std::uint64_t GetVariantDisplayNameHash(std::uint8_t partsType,
                                            std::uint8_t playerType,
                                            std::uint8_t variantIndex);

    bool TryGetOutfitByFlowIndex(std::uint16_t flowIndex,
                                 const OutfitEntry** outEntry);

    bool TryGetOutfitByDevelopId(std::uint16_t developId,
                                 const OutfitEntry** outEntry);

    bool SetOutfitSummaryDisplay(std::uint16_t developId,
                                 std::uint64_t nameHash,
                                 std::uint64_t iconHash);

    bool TryGetOutfitByFlowIndexForPlayerType(
            std::uint16_t flowIndex,
            std::uint8_t  playerType,
            const OutfitEntry** outEntry);

    bool TryGetOutfitByDevelopIdForPlayerType(
            std::uint16_t developId,
            std::uint8_t  playerType,
            const OutfitEntry** outEntry);


    bool TryGetOutfitByCamoVirtualId(std::uint8_t virtualId,
                                     const OutfitEntry** outEntry,
                                     std::uint8_t* outPlayerType);


    bool IsSnakeAvatarBridge(std::uint8_t a, std::uint8_t b);


    std::size_t GetAllOutfits(const OutfitEntry** outEntries,
                              std::size_t maxEntries);

    int ResolvePendingHeadName(std::uint64_t nameHash, std::uint16_t equipId);


    std::uint8_t ReadLivePartsType();
    std::uint8_t ReadLiveSelectorCode();
    std::uint8_t ReadLivePlayerType();


    bool WriteLivePlayerOutfit(std::uint8_t partsType,
                               std::uint8_t selectorCode,
                               std::uint8_t playerType);

    void RememberPlayerTypeOutfit(std::uint8_t playerType,
                                  std::uint8_t partsType, std::uint8_t selector);
    bool GetRememberedPlayerTypeOutfit(std::uint8_t playerType,
                                       std::uint8_t* outPartsType,
                                       std::uint8_t* outSelector);


    bool WriteLiveHeadSlot(std::uint8_t headSlot);


    void          SetActiveVariant(std::uint8_t partsType, std::uint8_t variantIndex);
    std::uint8_t  GetActiveVariant(std::uint8_t partsType);
    void          ClearActiveVariant(std::uint8_t partsType);


    void          SetPendingOutfitDevelopId(std::uint16_t developId);
    std::uint16_t GetPendingOutfitDevelopId();
    void          ClearPendingOutfitDevelopId();


    void          SetPendingHeadOptionEquipId(std::uint16_t equipId);
    std::uint16_t GetPendingHeadOptionEquipId();
    void          ClearPendingHeadOptionEquipId();

    void          SetWornCustomHeadSlot(std::uint8_t slot);
    std::uint8_t  GetWornCustomHeadSlot();
    void          ClearWornCustomHeadSlot();


    void  SetSupplyDropClickLatch();
    bool  ConsumeSupplyDropClickLatch();


    void          SetPendingSupplyDropDevelopId(std::uint16_t developId);
    std::uint16_t ConsumePendingSupplyDropDevelopId();


    std::uint16_t PeekPendingSupplyDropDevelopId();


    void          SetPendingSupplyDropVariantIdx(std::uint8_t variantIndex);
    std::uint8_t  ConsumePendingSupplyDropVariantIdx();
}
