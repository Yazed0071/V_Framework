#pragma once

#include <cstddef>
#include <cstdint>

namespace outfit
{


    // [0x40..0x7F]: above all vanilla partsType references; 0x80+ is reserved
    // for selector codes (engine treats high-bit-set bytes as selectors).
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
    };


    // All per-playerType state. `name` and `developId`/`flowIndex` (and the
    // session-allocated `partsType`/`selectorCode`/variant selectors) live on
    // the outer OutfitEntry. Everything else is here, so each playerType
    // configures its own paths, sub-assets, variants, head options, behavior
    // flags, lang strings, and camo bonus profile independently.
    struct OutfitPlayerTypeData
    {
        bool           used                = false;


        // Body asset paths (REQUIRED for any populated branch).
        std::uint64_t  partsPathCode64     = 0;
        std::uint64_t  fpkPathCode64       = 0;


        // Sub-asset overrides. Each accepts a hashed path (custom),
        // kSubAssetUseVanilla (load vanilla), or kSubAssetDisabled (load nothing).
        std::uint64_t  camoFpk             = kSubAssetDisabled;
        std::uint64_t  faceFpk             = kSubAssetUseVanilla;
        std::uint64_t  skinFv2             = kSubAssetUseVanilla;
        std::uint64_t  diamondFpk          = kSubAssetDisabled;
        std::uint64_t  voiceFpk            = kSubAssetUseVanilla;
        std::uint64_t  camoFv2             = kSubAssetUseVanilla;
        std::uint64_t  diamondFv2          = kSubAssetUseVanilla;


        // Variant 0's iDroid cycle-button label (StrCode64 of a LangId string).
        std::uint64_t  baseDisplayNameHash = 0;


        // Variants 1..variantCount-1 (variant 0 is the branch base above).
        OutfitVariant  variants[kMaxVariantsPerOutfit] = {};
        std::uint8_t   variantCount                    = 0;


        // Behavior flags — apply to the player while wearing this outfit AS
        // this playerType.
        bool           enableArm              = true;
        bool           enableHead             = false;
        std::uint16_t  defaultSoldierFaceId   = 0;


        // iDroid suit-name lookup hash for this PT.
        std::uint64_t  langEquipNameHash      = 0;


        // HEAD OPTION submenu entries available when wearing this outfit AS
        // this playerType.
        std::uint16_t  headOptionEquipIds[kMaxHeadOptionsPerOutfit] = {};
        std::uint8_t   headOptionCount                              = 0;
        bool           supportsHeadOptions                          = false;


        // Camo-bonus pin / unique row for this PT. `camoBonusType` holds either
        // a vanilla PlayerCamoType (0..116, INHERIT mode) or a framework-
        // allocated virtual id (200..254, UNIQUE-ROW mode), or kCamoBonusTypeUnset
        // (no override). The virtual id is allocated at registration time when
        // `hasCamoBonusValues == true`.
        std::uint8_t   camoBonusType                              = kCamoBonusTypeUnset;
        std::int32_t   camoBonusValues[kCamoMaterialCount]        = {};
        bool           hasCamoBonusValues                         = false;
    };


    // Outfit-level definition produced by the Lua bridge. Only `key`,
    // `developId`/`flowIndex` (auto-allocated under `key`), and `partsTypeHint`/
    // `selectorCodeHint` are at this level. Everything else lives in
    // perPlayerType[Snake/DDMale/DDFemale/Avatar].
    struct OutfitDefinition
    {
        const char*    key             = nullptr;


        // Populated by the Lua bridge from V_FrameWork_State.lua under `key`.
        std::uint16_t  developId       = 0;
        std::uint16_t  flowIndex       = 0;


        std::uint8_t   partsTypeHint    = 0xFF;
        std::uint8_t   selectorCodeHint = 0xFF;


        OutfitPlayerTypeData perPlayerType[kPlayerTypeMax] = {};
    };


    struct OutfitEntry
    {
        bool           used              = false;

        std::uint16_t  developId         = 0;
        std::uint16_t  flowIndex         = 0;


        // partsType and selectorCode are SHARED across all PT branches: there
        // is exactly one (partsType, selectorCode) pair per outfit, and it
        // identifies the outfit globally. PT-specific paths are looked up via
        // perPlayerType[livePT] at runtime.
        std::uint8_t   partsType         = 0;
        std::uint8_t   selectorCode      = 0;


        OutfitPlayerTypeData perPlayerType[kPlayerTypeMax] = {};


        // Variant selectors are global (shared across PTs). Index k always
        // refers to "variant k" of this outfit; the engine sees the same
        // selector code regardless of which playerType is wearing it. The
        // variant's PATHS, however, come from perPlayerType[livePT].variants[k].
        std::uint8_t   variantSelectorCodes[kMaxVariantsPerOutfit] = {};


        // Maximum variantCount across all populated PT branches. Drives
        // selector allocation. Per-PT variantCount may be smaller — branches
        // with fewer variants stop their cycle button early.
        std::uint8_t   variantCount                                 = 0;


        // Returns the per-PT branch for `playerType`, applying Snake↔Avatar
        // bridge: if the requested branch is empty but the bridged side is
        // populated, returns the bridged branch. Returns nullptr if neither
        // side supports this PT.
        const OutfitPlayerTypeData* GetPTData(std::uint8_t playerType) const;


        // True if this outfit can be worn by `playerType` (directly or via
        // Snake↔Avatar bridge).
        bool IsPlayerTypeSupported(std::uint8_t playerType) const;


        bool HasVariants()       const { return variantCount > 0; }


        // Per-PT behavior flag accessors. All return the live PT branch's
        // value (Snake↔Avatar bridge applied), or a sensible default if the
        // PT isn't supported.
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


        // True if ANY supported PT has head options. Useful for outfit-wide
        // gates that don't have a live PT to consult.
        bool HasAnyHeadOptions() const;


        // Resolves the head-option list for `playerType`. Returns true and
        // fills `*outEquipIds` / `*outCount` if the branch supplies any.
        bool GetHeadOptionsFor(std::uint8_t playerType,
                               const std::uint16_t** outEquipIds,
                               std::uint8_t* outCount) const;


        std::uint64_t GetVariantPartsPath(std::uint8_t playerType, std::uint8_t variantIdx) const;
        std::uint64_t GetVariantFpkPath(std::uint8_t playerType, std::uint8_t variantIdx) const;
        std::uint64_t GetVariantCamoFpk(std::uint8_t playerType, std::uint8_t variantIdx) const;
        std::uint64_t GetVariantCamoFv2(std::uint8_t playerType, std::uint8_t variantIdx) const;
        std::uint64_t GetVariantDiamondFpk(std::uint8_t playerType, std::uint8_t variantIdx) const;
        std::uint64_t GetVariantDiamondFv2(std::uint8_t playerType, std::uint8_t variantIdx) const;
        std::uint64_t GetVariantVoiceFpk(std::uint8_t playerType, std::uint8_t variantIdx) const;
        std::uint64_t GetVariantDisplayNameHash(std::uint8_t playerType, std::uint8_t variantIdx) const;


        // Number of variants for this PT branch (after Snake↔Avatar bridge).
        // 0 if the PT is not supported.
        std::uint8_t  GetVariantCountFor(std::uint8_t playerType) const;


        // Live-PT lang-equip-name hash and default-soldier-face id.
        std::uint64_t GetLangEquipNameHashFor(std::uint8_t playerType) const;
        std::uint16_t GetDefaultSoldierFaceIdFor(std::uint8_t playerType) const;


        // Camo-bonus accessors.
        // GetCamoBonusType returns the per-PT camoBonusType (vanilla pin or
        // virtual id), or kCamoBonusTypeUnset if the branch has no override.
        std::uint8_t  GetCamoBonusType(std::uint8_t playerType) const;
        // True if THIS PT's branch has a unique camoBonusValues row.
        bool          HasCamoBonusValuesFor(std::uint8_t playerType) const;
        // Returns a pointer to the PT's 82-entry row (only valid if
        // HasCamoBonusValuesFor returns true).
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

    bool TryGetOutfitByFlowIndexForPlayerType(
            std::uint16_t flowIndex,
            std::uint8_t  playerType,
            const OutfitEntry** outEntry);

    bool TryGetOutfitByDevelopIdForPlayerType(
            std::uint16_t developId,
            std::uint8_t  playerType,
            const OutfitEntry** outEntry);


    // Find the outfit (and the specific PT branch) that owns `virtualId`.
    // Returns true if any branch's camoBonusType == virtualId. The branch's
    // camoBonusValues row should be returned by the GetCamoufValue hook.
    bool TryGetOutfitByCamoVirtualId(std::uint8_t virtualId,
                                     const OutfitEntry** outEntry,
                                     std::uint8_t* outPlayerType);


    bool IsSnakeAvatarBridge(std::uint8_t a, std::uint8_t b);


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
