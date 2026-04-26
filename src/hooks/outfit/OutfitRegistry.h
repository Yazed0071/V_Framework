#pragma once

#include <cstddef>
#include <cstdint>

namespace outfit
{
    // ---------------------------------------------------------------
    // Custom-range allocation rules
    // ---------------------------------------------------------------
    //
    // Vanilla `playerPartsType` values occupy 0x00..0x3F. We reserve
    // 0x40..0x7F for custom outfits — every runtime path-loader hook
    // detects values in this range and returns the registered FoxPath
    // instead of falling through to the game's vanilla lookup.
    //
    // The selector code is the byte the game writes to Quark
    // state[+0xF9] when a suit is equipped. Vanilla selectors land
    // in 0x00..0x7F. We reserve 0x80..0xFE for custom suits.

    constexpr std::uint8_t kCustomPartsTypeStart = 0x40;
    constexpr std::uint8_t kCustomPartsTypeEnd   = 0x7F;
    constexpr std::uint8_t kCustomSelectorStart  = 0x80;
    constexpr std::uint8_t kCustomSelectorEnd    = 0xFE;

    // Sub-asset slot encoding for camo/face/arm/skin/diamond paths.
    constexpr std::uint64_t kSubAssetDisabled   = 0;
    constexpr std::uint64_t kSubAssetUseVanilla = 1;

    // PlayerType enum values from Quark state[+0xFB].
    constexpr std::uint8_t kPlayerType_Snake     = 0;
    constexpr std::uint8_t kPlayerType_DDMale    = 1;
    constexpr std::uint8_t kPlayerType_DDFemale  = 2;
    constexpr std::uint8_t kPlayerType_Avatar    = 3;

    // Registry capacity. Phase 2 supports up to 128 outfits across
    // all playerTypes — well above any realistic mod load.
    constexpr std::size_t  kMaxOutfits = 128;

    // Phase 3 — head options, variants.
    constexpr std::size_t  kMaxHeadOptionsPerOutfit = 8;   // typical max in vanilla
    constexpr std::size_t  kMaxVariantsPerOutfit    = 8;
    constexpr std::uint16_t kHeadOption_None        = 0x400;  // game-native NONE sentinel
    constexpr std::uint16_t kHeadOption_VanillaSP   = 0x17CA; // BALACLAVA equipId (vanilla SP base)

    // ---------------------------------------------------------------
    // OutfitVariant — one variant of a registered outfit.
    // Variants share a single UNIFORMS row but cycle through distinct
    // appearance bytes when the user presses the variant-cycle button.
    // ---------------------------------------------------------------

    struct OutfitVariant
    {
        bool           used               = false;
        std::uint64_t  partsPathCode64    = 0;     // 0 = inherit from parent
        std::uint64_t  fpkPathCode64      = 0;     // 0 = inherit from parent
        std::uint64_t  camoFpk            = kSubAssetUseVanilla;
        std::uint64_t  camoFv2            = kSubAssetUseVanilla;
        std::uint64_t  diamondFpk         = kSubAssetDisabled;
        std::uint16_t  displayNameId      = 0;     // localized string id (0 = inherit)
    };

    // ---------------------------------------------------------------
    // OutfitDefinition — registration input.
    // ---------------------------------------------------------------

    struct OutfitDefinition
    {
        const char*    key             = nullptr;
        std::uint16_t  developId       = 0;
        std::uint16_t  flowIndex       = 0;
        std::uint8_t   playerType      = 0;

        // 0xFF = auto-allocate from the custom pool.
        std::uint8_t   partsTypeHint    = 0xFF;
        std::uint8_t   selectorCodeHint = 0xFF;

        // FoxPath code64ext hashes (Lua bridge in Phase 4 computes
        // these from path strings).
        std::uint64_t  partsPathCode64  = 0;
        std::uint64_t  fpkPathCode64    = 0;

        // Sub-asset paths (Phase 2 fields).
        std::uint64_t  camoFpk          = kSubAssetDisabled;
        std::uint64_t  faceFpk          = kSubAssetUseVanilla;
        std::uint64_t  armFpk           = kSubAssetUseVanilla;
        std::uint64_t  skinFv2          = kSubAssetUseVanilla;
        std::uint64_t  diamondFpk       = kSubAssetDisabled;

        // Phase 3 — FV2 (face variant 2) sub-asset paths.
        std::uint64_t  camoFv2          = kSubAssetUseVanilla;
        std::uint64_t  diamondFv2       = kSubAssetUseVanilla;

        // Phase 3 — head options. headOptionEquipIds[] holds equipIds
        // (not flowIndices) that map to vanilla head models. 0 = NONE
        // (the game treats kHeadOption_None=0x400 as the explicit NONE
        // sentinel; a 0 in our array is treated equivalently). Up to
        // kMaxHeadOptionsPerOutfit entries — leave trailing slots as 0.
        // headOptionCount must match the number of populated slots.
        std::uint16_t  headOptionEquipIds[kMaxHeadOptionsPerOutfit] = {};
        std::uint8_t   headOptionCount                              = 0;
        bool           supportsHeadOptions                          = false;

        // Phase 3 — variants. variantCount==0 → outfit has no variants
        // (single appearance). When variantCount>0, the OutfitDefinition's
        // own partsPathCode64 / fpkPathCode64 / camoFpk fields define
        // variant 0; variants[1..variantCount-1] override.
        OutfitVariant  variants[kMaxVariantsPerOutfit] = {};
        std::uint8_t   variantCount                    = 0;
    };

    // ---------------------------------------------------------------
    // OutfitEntry — internal record after allocation.
    // ---------------------------------------------------------------

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
        std::uint64_t  armFpk            = kSubAssetUseVanilla;
        std::uint64_t  skinFv2           = kSubAssetUseVanilla;
        std::uint64_t  diamondFpk        = kSubAssetDisabled;

        // Phase 3 fields.
        std::uint64_t  camoFv2           = kSubAssetUseVanilla;
        std::uint64_t  diamondFv2        = kSubAssetUseVanilla;

        std::uint16_t  headOptionEquipIds[kMaxHeadOptionsPerOutfit] = {};
        std::uint8_t   headOptionCount                              = 0;
        bool           supportsHeadOptions                          = false;

        OutfitVariant  variants[kMaxVariantsPerOutfit] = {};
        std::uint8_t   variantCount                    = 0;

        bool IsCamoCustom()      const { return camoFpk     > kSubAssetUseVanilla; }
        bool IsCamoFv2Custom()   const { return camoFv2     > kSubAssetUseVanilla; }
        bool IsFaceEnabled()     const { return faceFpk     != kSubAssetDisabled; }
        bool IsArmEnabled()      const { return armFpk      != kSubAssetDisabled; }
        bool IsDiamondEnabled()  const { return diamondFpk  != kSubAssetDisabled; }
        bool IsDiamondCustom()   const { return diamondFpk  > kSubAssetUseVanilla; }
        bool IsDiamondFv2Custom()const { return diamondFv2  > kSubAssetUseVanilla; }
        bool HasVariants()       const { return variantCount > 0; }
        bool HasHeadOptions()    const { return supportsHeadOptions && headOptionCount > 0; }

        // Variant accessor: returns variant N's effective path (falls
        // back to base outfit fields when variant slot is unused or
        // variant field is 0). variantIndex 0 always returns the base.
        std::uint64_t GetVariantPartsPath(std::uint8_t idx) const;
        std::uint64_t GetVariantFpkPath(std::uint8_t idx) const;
        std::uint64_t GetVariantCamoFpk(std::uint8_t idx) const;
        std::uint64_t GetVariantCamoFv2(std::uint8_t idx) const;
        std::uint64_t GetVariantDiamondFpk(std::uint8_t idx) const;
    };

    // ---------------------------------------------------------------
    // Registration API
    // ---------------------------------------------------------------

    bool RegisterOutfit(const OutfitDefinition& def,
                        std::uint8_t* outAllocatedPartsType);

    void ClearAllOutfits();

    // ---------------------------------------------------------------
    // Lookup API (thread-safe via internal mutex).
    // ---------------------------------------------------------------

    bool TryGetOutfitByPartsType(std::uint8_t partsType,
                                 const OutfitEntry** outEntry);

    bool TryGetOutfitBySelectorCode(std::uint8_t selectorCode,
                                    const OutfitEntry** outEntry);

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

    std::size_t GetAllOutfits(const OutfitEntry** outEntries,
                              std::size_t maxEntries);

    // ---------------------------------------------------------------
    // Live player state (read from Quark).
    // Returns 0xFF when Quark is not yet ready.
    // ---------------------------------------------------------------

    std::uint8_t ReadLivePartsType();
    std::uint8_t ReadLiveSelectorCode();
    std::uint8_t ReadLivePlayerType();

    // Write live Quark player-state bytes for partsType/camo/playerType.
    // Used by out-of-band equip paths (e.g. supply-drop force-reload)
    // to make the persistent player state match the suit being applied,
    // so subsequent natural LoadPartsNew fires don't override us with
    // stale vanilla bytes from the unmodified state. Returns false if
    // Quark live state isn't yet available.
    bool WriteLivePlayerOutfit(std::uint8_t partsType,
                               std::uint8_t selectorCode,
                               std::uint8_t playerType);

    // ---------------------------------------------------------------
    // Active-variant tracker (Phase 3).
    //
    // Variant index for the currently-equipped outfit. Updated by
    // OutfitCommit when a custom commit blob carries a variant index
    // in blob[0x02]. Read by OutfitRuntimeParts to choose which
    // variant's paths to load.
    //
    // Keyed by partsType (0x40..0x7F). Defaults to 0 (base) until a
    // commit explicitly sets it. SetActiveVariant clamps to the
    // outfit's variantCount.
    // ---------------------------------------------------------------

    void          SetActiveVariant(std::uint8_t partsType, std::uint8_t variantIndex);
    std::uint8_t  GetActiveVariant(std::uint8_t partsType);
    void          ClearActiveVariant(std::uint8_t partsType);

    // ---------------------------------------------------------------
    // Pending-developId bridge.
    //
    // Set by OutfitItemSelector when the user clicks a custom-outfit
    // row in UNIFORMS. Consumed by OutfitCommit's broken-custom
    // rewrite path: when blob[0..2] arrives as 00 FF 00 ("custom
    // suit selected, please resolve"), the commit hook reads the
    // pending developId and rewrites the blob with the matching
    // outfit's partsType / selector / variant / playerType bytes.
    // ---------------------------------------------------------------

    void          SetPendingOutfitDevelopId(std::uint16_t developId);
    std::uint16_t GetPendingOutfitDevelopId();
    void          ClearPendingOutfitDevelopId();

    // ---------------------------------------------------------------
    // Supply-drop click latch.
    //
    // OutfitItemSelector's supply-drop hook sets this on every fire
    // (every confirm-click in supply-drop UI). OutfitSupplyDropSetup
    // checks + clears it: if set when SupplyDropSuitSetup fires, the
    // current invocation is a confirm-click (not a hover/preview) and
    // the framework should drive an immediate equip via ForcePartsReload.
    // Hovering a suit fires SupplyDropSuitSetup too but WITHOUT a
    // preceding ItemSelector hit, so the latch stays clear and we
    // skip the equip.
    // ---------------------------------------------------------------

    void  SetSupplyDropClickLatch();
    bool  ConsumeSupplyDropClickLatch();

    // ---------------------------------------------------------------
    // Pending supply-drop custom outfit.
    //
    // OutfitSupplyDropSetup sets this (via developId) when the user
    // confirms a supply-drop order for a registered custom outfit.
    // OutfitSupplyDropPickup consumes it when the player interacts
    // with the delivered crate, then force-equips that specific
    // outfit. Vanilla supply drops leave this clear, so the pickup
    // hook does nothing for them and vanilla apply runs normally.
    // ---------------------------------------------------------------

    void          SetPendingSupplyDropDevelopId(std::uint16_t developId);
    std::uint16_t ConsumePendingSupplyDropDevelopId();
    // Read the pending stash without consuming. Used by the pickup
    // hook's gate-skip path to log "stash is still set, waiting for
    // real pickup."
    std::uint16_t PeekPendingSupplyDropDevelopId();
}
