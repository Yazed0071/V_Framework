#pragma once

#include <cstddef>
#include <cstdint>

namespace outfit
{


    // Custom partsType byte range. Upper bound is fixed at 0x7F because the
    // engine treats bytes with the high bit set (0x80+) as selector codes.
    // Lower bound is 0x40, which is the safe boundary above all vanilla
    // partsType references in the player/parts/outfit code paths.
    // Vanilla partsType collisions:
    //   0x00-0x19  vanilla outfit whitelist (Player2GameObjectImpl::Update-
    //              PartsStatus arm-tier gate at mgsvtpp:1325085)
    //   0x0E       Player2Impl::SetUpParts:2727478 — thermal model gate
    //              (excluded from `1 < param_4 - 0xE && param_4 != 0x14`)
    //   0x14       same gate, explicit exclusion
    //   0x15-0x16  ResourceTable::DoesNeedFaceFovaForAvatar switch entries
    //   0x17-0x19  same
    //   0x1A-0x1B  UpdatePartsStatus writes for playerType==5/6 (DDFemale/
    //              Avatar) — would clobber custom-outfit identity
    //   0x80+      reserved for selector codes
    // Safe contiguous range: [0x40..0x7F] = 64 slots.
    //
    // History: extended down to 0x1C (giving 100 slots) on 2026-05-04 to
    // support more custom outfits, but the dev-menu integration for
    // outfits past index 63 never converged to a working state. Reverted
    // to the original 0x40 lower bound on 2026-05-04 round 18.
    constexpr std::uint8_t kCustomPartsTypeStart = 0x40;
    constexpr std::uint8_t kCustomPartsTypeEnd   = 0x7F;
    constexpr std::uint8_t kCustomSelectorStart  = 0x80;
    constexpr std::uint8_t kCustomSelectorEnd    = 0xFE;


    constexpr std::uint64_t kSubAssetDisabled   = 0;
    constexpr std::uint64_t kSubAssetUseVanilla = 1;


    constexpr std::uint8_t kPlayerType_Snake     = 0;
    constexpr std::uint8_t kPlayerType_DDMale    = 1;
    constexpr std::uint8_t kPlayerType_DDFemale  = 2;
    constexpr std::uint8_t kPlayerType_Avatar    = 3;


    constexpr std::size_t  kCamoMaterialCount       = 82;
    constexpr std::uint8_t kVanillaCamoTypeMax      = 116;
    constexpr std::uint8_t kCamoVirtualIdStart      = 200;
    constexpr std::uint8_t kCamoVirtualIdEnd        = 254;
    constexpr std::uint8_t kCamoBonusTypeUnset      = 0xFF;


    // Hard cap on registered outfit entries. The partsType range
    // [0x40..0x7F] = 64 slots is the actual limiter; 128 here leaves
    // slack for future range tweaks without resizing the entry table.
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
        std::uint64_t  voiceFpk           = kSubAssetUseVanilla;


        std::uint64_t  displayNameHash    = 0;
    };


    struct OutfitDefinition
    {
        const char*    key             = nullptr;
        std::uint16_t  developId       = 0;
        std::uint16_t  flowIndex       = 0;
        std::uint8_t   playerType      = 0;


        std::uint8_t   partsTypeHint    = 0xFF;
        std::uint8_t   selectorCodeHint = 0xFF;


        std::uint64_t  partsPathCode64  = 0;
        std::uint64_t  fpkPathCode64    = 0;


        std::uint64_t  camoFpk          = kSubAssetDisabled;
        std::uint64_t  faceFpk          = kSubAssetUseVanilla;
        std::uint64_t  skinFv2          = kSubAssetUseVanilla;
        std::uint64_t  diamondFpk       = kSubAssetDisabled;
        std::uint64_t  voiceFpk         = kSubAssetUseVanilla;


        bool           enableArm        = true;


        std::uint64_t  camoFv2          = kSubAssetUseVanilla;
        std::uint64_t  diamondFv2       = kSubAssetUseVanilla;


        std::uint16_t  headOptionEquipIds[kMaxHeadOptionsPerOutfit] = {};
        std::uint8_t   headOptionCount                              = 0;
        bool           supportsHeadOptions                          = false;


        bool           enableHead                      = false;


        std::uint16_t  defaultSoldierFaceId            = 0;


        std::uint64_t  langEquipNameHash               = 0;


        std::uint64_t  baseDisplayNameHash             = 0;


        OutfitVariant  variants[kMaxVariantsPerOutfit] = {};
        std::uint8_t   variantCount                    = 0;


        std::uint8_t   camoBonusType                  = kCamoBonusTypeUnset;


        std::int32_t   camoBonusValues[kCamoMaterialCount] = {};
        bool           hasCamoBonusValues                  = false;
    };


    struct OutfitEntry
    {
        bool           used              = false;

        std::uint16_t  developId         = 0;
        std::uint16_t  flowIndex         = 0;
        std::uint8_t   playerType        = 0;
        std::uint8_t   partsType         = 0;
        std::uint8_t   selectorCode      = 0;

        std::uint64_t  partsPathCode64   = 0;
        std::uint64_t  fpkPathCode64     = 0;

        std::uint64_t  camoFpk           = kSubAssetDisabled;
        std::uint64_t  faceFpk           = kSubAssetUseVanilla;
        std::uint64_t  skinFv2           = kSubAssetUseVanilla;
        std::uint64_t  diamondFpk        = kSubAssetDisabled;
        std::uint64_t  voiceFpk          = kSubAssetUseVanilla;


        bool           enableArm         = true;


        std::uint64_t  camoFv2           = kSubAssetUseVanilla;
        std::uint64_t  diamondFv2        = kSubAssetUseVanilla;

        std::uint16_t  headOptionEquipIds[kMaxHeadOptionsPerOutfit] = {};
        std::uint8_t   headOptionCount                              = 0;
        bool           supportsHeadOptions                          = false;


        bool           enableHead                                   = false;


        std::uint16_t  defaultSoldierFaceId                         = 0;


        std::uint64_t  langEquipNameHash                            = 0;

        OutfitVariant  variants[kMaxVariantsPerOutfit] = {};
        std::uint8_t   variantCount                    = 0;


        std::uint8_t   variantSelectorCodes[kMaxVariantsPerOutfit] = {};


        std::uint64_t  variantDisplayNameHashes[kMaxVariantsPerOutfit] = {};


        std::uint8_t   camoBonusType                              = kCamoBonusTypeUnset;


        std::int32_t   camoBonusValues[kCamoMaterialCount]        = {};
        bool           hasCamoBonusValues                          = false;

        bool IsCamoCustom()      const { return camoFpk     > kSubAssetUseVanilla; }
        bool IsCamoFv2Custom()   const { return camoFv2     > kSubAssetUseVanilla; }
        bool IsFaceEnabled()     const { return faceFpk     != kSubAssetDisabled; }
        bool IsArmEnabled()      const { return enableArm; }
        bool IsDiamondEnabled()  const { return diamondFpk  != kSubAssetDisabled; }
        bool IsDiamondCustom()   const { return diamondFpk  > kSubAssetUseVanilla; }
        bool IsDiamondFv2Custom()const { return diamondFv2  > kSubAssetUseVanilla; }
        bool IsVoiceCustom()     const { return voiceFpk    > kSubAssetUseVanilla; }
        bool HasVariants()       const { return variantCount > 0; }
        bool HasHeadOptions()    const { return supportsHeadOptions && headOptionCount > 0; }
        bool IsHeadEnabled()     const { return enableHead; }


        std::uint64_t GetVariantPartsPath(std::uint8_t idx) const;
        std::uint64_t GetVariantFpkPath(std::uint8_t idx) const;
        std::uint64_t GetVariantCamoFpk(std::uint8_t idx) const;
        std::uint64_t GetVariantCamoFv2(std::uint8_t idx) const;
        std::uint64_t GetVariantDiamondFpk(std::uint8_t idx) const;
        std::uint64_t GetVariantVoiceFpk(std::uint8_t idx) const;
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
                                            std::uint8_t variantIndex);

    bool TryGetOutfitByFlowIndex(std::uint16_t flowIndex,
                                 const OutfitEntry** outEntry);

    bool TryGetOutfitByDevelopId(std::uint16_t developId,
                                 const OutfitEntry** outEntry);

    bool TryGetOutfitByFlowIndexForPlayerType(
            std::uint16_t flowIndex,
            std::uint8_t  playerType,
            const OutfitEntry** outEntry);

    bool TryGetOutfitByDevelopIdForPlayerType(
            std::uint16_t developId,
            std::uint8_t  playerType,
            const OutfitEntry** outEntry);


    bool TryGetOutfitByCamoVirtualId(std::uint8_t virtualId,
                                     const OutfitEntry** outEntry);


    // Returns true when an outfit registered for `entryPlayerType` should be
    // accepted as a match for the live player whose type is `queriedPlayerType`.
    // Exact matches always pass. Snake (kPlayerType_Snake=0) and Avatar
    // (kPlayerType_Avatar=3) share skeleton + proportions, so outfits authored
    // for one render correctly on the other and are bridged bidirectionally.
    // DDMale (1) and DDFemale (2) are NOT bridged (different skeletons).
    bool IsPlayerTypeCompatible(std::uint8_t entryPlayerType,
                                std::uint8_t queriedPlayerType);

    std::size_t GetAllOutfits(const OutfitEntry** outEntries,
                              std::size_t maxEntries);


    std::uint8_t ReadLivePartsType();
    std::uint8_t ReadLiveSelectorCode();
    std::uint8_t ReadLivePlayerType();


    bool WriteLivePlayerOutfit(std::uint8_t partsType,
                               std::uint8_t selectorCode,
                               std::uint8_t playerType);


    void          SetActiveVariant(std::uint8_t partsType, std::uint8_t variantIndex);
    std::uint8_t  GetActiveVariant(std::uint8_t partsType);
    void          ClearActiveVariant(std::uint8_t partsType);


    void          SetPendingOutfitDevelopId(std::uint16_t developId);
    std::uint16_t GetPendingOutfitDevelopId();
    void          ClearPendingOutfitDevelopId();


    void          SetPendingHeadOptionEquipId(std::uint16_t equipId);
    std::uint16_t GetPendingHeadOptionEquipId();
    void          ClearPendingHeadOptionEquipId();


    void  SetSupplyDropClickLatch();
    bool  ConsumeSupplyDropClickLatch();


    void          SetPendingSupplyDropDevelopId(std::uint16_t developId);
    std::uint16_t ConsumePendingSupplyDropDevelopId();


    std::uint16_t PeekPendingSupplyDropDevelopId();


    void          SetPendingSupplyDropVariantIdx(std::uint8_t variantIndex);
    std::uint8_t  ConsumePendingSupplyDropVariantIdx();
}
