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

    // Camo bonus table dimensions and reserved virtual-id range.
    //
    // The vanilla 117x82 camo table is indexed by PlayerCamoType (0..116)
    // and material type (0..81 — PlayerCamoType is the row,
    // MaterialType is the column). For per-outfit unique camo bonus
    // rows (`camoBonusValues` field), the framework allocates a virtual
    // PlayerCamoType id in the reserved range below; the GetCamoufValue
    // hook intercepts those virtual ids and returns values from the
    // outfit's inline `camoBonusValues[]` array instead of reading the
    // vanilla table (which only has 117 rows). Vanilla pin ids
    // (camoBonusType in 0..116) bypass the hook and read the orig table.
    constexpr std::size_t  kCamoMaterialCount       = 82;
    constexpr std::uint8_t kVanillaCamoTypeMax      = 116;     // QUIET = highest vanilla
    constexpr std::uint8_t kCamoVirtualIdStart      = 200;     // first virtual id we hand out
    constexpr std::uint8_t kCamoVirtualIdEnd        = 254;     // last (255 = sentinel guard)
    constexpr std::uint8_t kCamoBonusTypeUnset      = 0xFF;    // "no pin" sentinel

    // Registry capacity. Phase 2 supports up to 128 outfits across
    // all playerTypes — well above any realistic mod load.
    constexpr std::size_t  kMaxOutfits = 128;

    // Phase 3 — head options, variants.
    constexpr std::size_t  kMaxHeadOptionsPerOutfit = 8;   // typical max in vanilla
    // 15 = vanilla UNIFORMS panel cell capacity per row
    // (cell offset stride is (row*15 + var) * sizeof(field), so 0..14
    // is the addressable range). Slot 0 is reserved for the base
    // outfit; slots 1..14 hold up to 14 explicit Lua override entries.
    constexpr std::size_t  kMaxVariantsPerOutfit    = 15;
    constexpr std::uint16_t kHeadOption_None        = 0x400;  // game-native NONE sentinel
    constexpr std::uint16_t kHeadOption_Balaclava   = 0x210;  // BALACLAVA equipId (vanilla SP base) — verified 2026-04-29 via runtime buffer dump of vanilla suit's HEAD OPTION submenu

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

        // StrCode64 hash of the LangId string used for this variant's
        // cycle-button label in the UNIFORMS panel (the spot where
        // vanilla shows "STANDARD"/"NAKED"/"SCARF"). 0 = no override
        // (panel will show whatever orig falls back to for this cell's
        // type field — typically blank or one of the vanilla three).
        //
        // Lua API: pass `displayName = "your_lang_id"` (string) and
        // the bridge computes StrCode64 automatically. Or pass
        // `displayNameHash = 0x...` (number) if you have the precomputed
        // hash. The vanilla LangId XML must have an <Entry LangId=...>
        // matching whatever string you used.
        std::uint64_t  displayNameHash    = 0;
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
        std::uint64_t  skinFv2          = kSubAssetUseVanilla;
        std::uint64_t  diamondFpk       = kSubAssetDisabled;

        // Arm visibility — true = vanilla bionic prosthetic arm loads,
        // false = arm suppressed (info->playerArmType zeroed before orig
        // dispatches the arm loader). Most non-Snake characters
        // (Quiet, female DD soldiers, FROG ports etc.) need this off.
        bool           enableArm        = true;

        // Phase 3 — FV2 (face variant 2) sub-asset paths.
        std::uint64_t  camoFv2          = kSubAssetUseVanilla;
        std::uint64_t  diamondFv2       = kSubAssetUseVanilla;

        // Phase 3 — head options. headOptionEquipIds[] holds equipIds
        // (not flowIndices) that map to vanilla head models. 0 = NONE
        // (the game treats kHeadOption_None=0x400 as the explicit NONE
        // sentinel; a 0 in our array is treated equivalently). Up to
        // kMaxHeadOptionsPerOutfit entries — leave trailing slots as 0.
        // headOptionCount must match the number of populated slots.
        //
        // The Lua bridge auto-sets supportsHeadOptions=true when
        // headOptions is non-empty, so most users only ever need to
        // populate headOptions and leave supportsHeadOptions implicit.
        std::uint16_t  headOptionEquipIds[kMaxHeadOptionsPerOutfit] = {};
        std::uint8_t   headOptionCount                              = 0;
        bool           supportsHeadOptions                          = false;

        // Phase 4 (added 2026-04-27) — enableHead controls whether the
        // human head model (skin + hair, the part the head equipment
        // sits on top of) is loaded for this outfit.
        //
        // Mechanism (CORRECTED 2026-04-27): orig BlockControllerImpl::LoadPartsNew
        // gates face/head FPK loading on
        //   isHeadNeeded = ResourceTable::DoesNeedFaceFova(playerPartsType);
        // (named-build line 1310939). The function takes the PARTSTYPE
        // byte (NOT camo as initially assumed) and returns true ONLY for
        // vanilla "real" outfit partsTypes {0,1,2,7,8,9, 0xB..0x19}.
        // For our custom range (0x40..0x7F) it returns false, so by
        // default LoadPartsNew never queues a face/head FPK for our
        // outfits — the head must come from the body parts file. Many
        // ports (FROG soldier, etc.) don't ship integrated heads.
        //
        // The gate is INLINED by MSVC inside LoadPartsNew, so a hook on
        // the function address doesn't catch it. The framework's
        // hkLoadPartsNew instead spoofs info->playerPartsType to 0x00
        // (vanilla NORMAL) for the duration of the orig call, while
        // stashing the real custom partsType in a thread-local that
        // the per-asset hooks consult to keep routing to this outfit's
        // assets. Orig sees spoofed 0x00 → DoesNeedFaceFova returns
        // true → orig calls Soldier2FaceSystem::vtable[0x20] with
        // info->playerFaceId, which loads FaceUnit[playerFaceId] →
        // default head for the outfit's playerType (DDFemale / DDMale).
        //
        // This is independent of headgear/balaclava — that goes via
        // playerFaceEquipId (info+0x06) and headOption dropdown, not
        // through the head-load gate.
        //
        // Default false: existing outfits whose body parts ship with
        // an integrated head (Quiet-style, like Jill's BattleSuit
        // .parts) keep their integrated head visible. Setting this to
        // true on those outfits would overlay a vanilla DD face on top
        // of the integrated head and look wrong.
        bool           enableHead                      = false;

        // Optional override for the soldier face index (info+0x04,
        // 16-bit) read by Soldier2FaceSystem::GetFaceFovaPathIdArrayAtFaceId
        // when the head FPK loads. The orig reads FaceUnit[playerFaceId]
        // and outputs 4 PathIds for face/hair/hairDeco/faceDeco. Each
        // is gated on a flag bit in FaceUnit[idx]+0x20 (0x20000 / 0x40000
        // / 0x80000 / 0x100000). Until vanilla Lua boot scripts call
        // SetFaceFovaDefinitionTable for a given face index, that
        // FaceUnit's flags are 0 → the orig outputs all-null PathIds
        // and the head doesn't load even though our gate said yes.
        //
        // 0 (default) — leave info->playerFaceId untouched (use
        //               whatever the player slot has, typically 0).
        // 1..899      — when enableHead is true AND info->playerFaceId
        //               is currently 0 (no manual face chosen), force
        //               playerFaceId to this value before orig reads
        //               the face FPK. Pick a value that's known to be
        //               registered for the playerType you're targeting.
        //               Vanilla DDFemale soldier faces are scattered
        //               across the 0..899 range; the modder may need
        //               to experiment until the head appears.
        std::uint16_t  defaultSoldierFaceId            = 0;

        // 64-bit FoxStrHash of the lang-string id used to display the
        // suit name in the SORTIE PREP > SELECT CHARACTER > UNIFORMS row.
        // The Lua bridge auto-derives this from the user's
        // `develop.const.langEquipName` field if it's set, so most users
        // don't have to fill this directly. When non-zero, the framework's
        // CharacterSelectorCallbackImpl::ChangeDetailsWindowBuddySelect
        // hook (retail 0x14163E5F0) overrides the vanilla translator's
        // partsType→hash output for our custom partsType range and uses
        // this hash instead — making the UI show the user's chosen
        // string instead of blank text.
        //
        // 0 (default) means "no override; UI will show blank" (vanilla
        // behavior — orig translator returns 0 for unrecognized
        // partsType).
        std::uint64_t  langEquipNameHash               = 0;

        // StrCode64 hash of the LangId for the VARIANT 0 (base) cycle-
        // button label — what shows up in the UNIFORMS panel before the
        // user cycles. Counterpart to OutfitVariant::displayNameHash for
        // each explicit variant slot. 0 = no override.
        //
        // Lua: pass `displayName = "your_lang_id"` at the top level OR
        // `displayNameHash = 0x...`. (Per-variant displayName goes
        // inside the `variants` array entry.)
        std::uint64_t  baseDisplayNameHash             = 0;

        // Phase 3 — variants. variantCount==0 → outfit has no variants
        // (single appearance). When variantCount>0, the OutfitDefinition's
        // own partsPathCode64 / fpkPathCode64 / camoFpk fields define
        // variant 0; variants[1..variantCount-1] override.
        OutfitVariant  variants[kMaxVariantsPerOutfit] = {};
        std::uint8_t   variantCount                    = 0;

        // Camo-bonus pin (PlayerCamoType, 0..116). When set to a value
        // in 0..116 (inclusive — `0` = OLIVEDRAB IS valid), while this
        // outfit is equipped the framework overrides the byte at
        // (Info+0x50)[playerSlot] inside CamouflageControllerImpl::
        // ExecSuitCorrect so the engine's surface-bonus lookup uses
        // this PlayerCamoType row of the 117x82 camo table — same
        // mechanism vanilla uses to pin BATTLEDRESS / SOLIDSNAKE /
        // etc. to specific suits at engine-coded equipIds 0x1c8..0x1d8.
        // Custom outfits aren't in that orig table; this field plumbs
        // the pin via our hook.
        //
        // 0xFF (default) = "unset", no pin → engine uses whatever the
        // player last selected via the iDroid camo picker. We use 0xFF
        // as the unset sentinel because 0 is a valid PlayerCamoType
        // value (OLIVEDRAB).
        //
        // Lua: pass `camoBonusType = PlayerCamoType.BATTLEDRESS` (or
        // numeric value 0..116). Omit / nil means "no pin".
        std::uint8_t   camoBonusType                  = kCamoBonusTypeUnset;

        // Per-outfit unique camo bonus row (UNUSED when camoBonusType
        // alone is set to a vanilla PlayerCamoType). When the modder
        // wants their outfit to have its OWN bonus profile rather than
        // inheriting a vanilla row, they pass `camoBonusValues = {
        //   MTR_LEAF = 50, MTR_RLEF = 50, ... }` — the lua bridge
        // resolves named keys to material indices and writes here.
        // hasCamoBonusValues is set to true.
        //
        // At registration the framework allocates a virtual camo-type
        // id from kCamoVirtualIdStart..kCamoVirtualIdEnd (200..254)
        // and stores it back in `camoBonusType` so the runtime path
        // (ExecSuitCorrect → vtable[0x18] → GetCamoufValue) lands at
        // a value our GetCamoufValue hook recognizes. The hook then
        // returns this[materialType] instead of reading the vanilla
        // table (which only goes 0..116).
        //
        // If BOTH camoBonusType and camoBonusValues are passed,
        // values wins (more specific intent).
        std::int32_t   camoBonusValues[kCamoMaterialCount] = {};
        bool           hasCamoBonusValues                  = false;
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
        std::uint64_t  skinFv2           = kSubAssetUseVanilla;
        std::uint64_t  diamondFpk        = kSubAssetDisabled;

        // Arm visibility — see OutfitDefinition::enableArm.
        bool           enableArm         = true;

        // Phase 3 fields.
        std::uint64_t  camoFv2           = kSubAssetUseVanilla;
        std::uint64_t  diamondFv2        = kSubAssetUseVanilla;

        std::uint16_t  headOptionEquipIds[kMaxHeadOptionsPerOutfit] = {};
        std::uint8_t   headOptionCount                              = 0;
        bool           supportsHeadOptions                          = false;

        // See OutfitDefinition::enableHead — controls whether
        // DoesNeedFaceFova is overridden to load a vanilla DD head FPK
        // for this outfit's selectorCode.
        bool           enableHead                                   = false;

        // See OutfitDefinition::defaultSoldierFaceId — optional override
        // for info->playerFaceId when the user's slot has 0 and our
        // enableHead override is in play. Lets the modder pick a
        // FaceUnit-populated face index instead of relying on faceId 0.
        std::uint16_t  defaultSoldierFaceId                         = 0;

        // See OutfitDefinition::langEquipNameHash — used by the UI hook
        // on ChangeDetailsWindowBuddySelect to override the suit-name
        // translator output for custom partsTypes. 0 = no override.
        std::uint64_t  langEquipNameHash                            = 0;

        OutfitVariant  variants[kMaxVariantsPerOutfit] = {};
        std::uint8_t   variantCount                    = 0;

        // Phase 5 — per-variant selector codes (UNIFORMS panel cycle).
        //
        // When variantCount > 0, RegisterOutfit reserves a contiguous
        // block of selector codes — one per variant slot. Slot 0 is
        // always the base selector (== this->selectorCode); slots
        // 1..variantCount-1 are the cycle-sub-selectors used to
        // populate adjacent cells of the same UNIFORMS row so the
        // panel's variant cycle button moves between them like
        // vanilla camo variants.
        //
        // For outfits with no variants (variantCount == 0), only
        // variantSelectorCodes[0] is meaningful and equals
        // selectorCode. The rest are 0xFF (sentinel / unallocated).
        std::uint8_t   variantSelectorCodes[kMaxVariantsPerOutfit] = {};

        // Phase 5 — per-variant cycle-button label hashes. Slot 0 holds
        // the base (== OutfitDefinition::baseDisplayNameHash); slots 1..N
        // hold the per-variant hashes from OutfitVariant::displayNameHash.
        // 0 = no label override (orig falls back to its hardcoded
        // STANDARD/SCARF/NAKED hashes per the cell type field, or shows
        // nothing for unhandled type values). The UpdateRecords hook
        // post-orig calls vtable[0x750]+[0x708] with this hash to
        // overwrite whatever the orig wrote.
        std::uint64_t  variantDisplayNameHashes[kMaxVariantsPerOutfit] = {};

        // See OutfitDefinition::camoBonusType — PlayerCamoType (0..116)
        // pinned for surface-bonus lookup while this outfit is equipped.
        // 0xFF = unset / no pin (engine reads whatever the iDroid camo
        // picker last wrote to Info+0x50[playerSlot]). 0 = OLIVEDRAB IS
        // a valid pin — don't conflate with "unset".
        //
        // When hasCamoBonusValues is true, this field holds the framework-
        // allocated VIRTUAL id (kCamoVirtualIdStart..kCamoVirtualIdEnd)
        // that our GetCamoufValue hook intercepts to return values from
        // the inline `camoBonusValues[]` array below.
        std::uint8_t   camoBonusType                              = kCamoBonusTypeUnset;

        // See OutfitDefinition::camoBonusValues — per-outfit unique
        // 82-cell bonus row. Only meaningful when hasCamoBonusValues
        // is true (else the array is all zeros and ignored).
        std::int32_t   camoBonusValues[kCamoMaterialCount]        = {};
        bool           hasCamoBonusValues                          = false;

        bool IsCamoCustom()      const { return camoFpk     > kSubAssetUseVanilla; }
        bool IsCamoFv2Custom()   const { return camoFv2     > kSubAssetUseVanilla; }
        bool IsFaceEnabled()     const { return faceFpk     != kSubAssetDisabled; }
        bool IsArmEnabled()      const { return enableArm; }
        bool IsDiamondEnabled()  const { return diamondFpk  != kSubAssetDisabled; }
        bool IsDiamondCustom()   const { return diamondFpk  > kSubAssetUseVanilla; }
        bool IsDiamondFv2Custom()const { return diamondFv2  > kSubAssetUseVanilla; }
        bool HasVariants()       const { return variantCount > 0; }
        bool HasHeadOptions()    const { return supportsHeadOptions && headOptionCount > 0; }
        bool IsHeadEnabled()     const { return enableHead; }

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

    // Look up an outfit by ANY of its variant selector codes. On match,
    // outEntry receives the OutfitEntry pointer and outVariantIndex
    // receives the variant slot (0 = base, 1..N-1 = explicit overrides).
    // For outfits with no variants, selectorCode matches the base only
    // and outVariantIndex is set to 0.
    bool TryGetOutfitByVariantSelector(std::uint8_t selectorCode,
                                       const OutfitEntry** outEntry,
                                       std::uint8_t* outVariantIndex);

    // Convenience accessor for the cycle-button label hash. Returns the
    // per-variant override (variantDisplayNameHashes[variantIndex]) or
    // 0 if the index is out of range / no override defined. Caller can
    // then decide whether to skip the orig label or substitute.
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

    // Look up an outfit by its allocated virtual camo-type id (the
    // framework-allocated value in kCamoVirtualIdStart..kCamoVirtualIdEnd
    // that lives in OutfitEntry::camoBonusType when the outfit has
    // hasCamoBonusValues=true). Used by the GetCamoufValue hook to
    // route reads of virtual ids to the outfit's inline values array.
    bool TryGetOutfitByCamoVirtualId(std::uint8_t virtualId,
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
    // Pending head-option equip id.
    //
    // Set by OutfitItemSelector when the user clicks a row in the HEAD
    // OPTION submenu (equipKind=0x201). The selectedId is the 16-bit
    // head-option equipId (e.g. 0x400 for NONE, 0x17CA for BALACLAVA).
    //
    // Consumed by OutfitSuitConditionApply's ReqLoadout/SetSuit rewrite
    // when flags carry 0x80 (head-option apply bit) AND info[3..4]
    // (faceEquipId u16) is 0 AND live partsType is a registered custom
    // outfit. The orig's apply pipeline fails to write info[3..4] for
    // our custom suit (no suit-category match -> orig drops the click),
    // so we rewrite info[3..4] from this pending stash instead.
    // ---------------------------------------------------------------

    void          SetPendingHeadOptionEquipId(std::uint16_t equipId);
    std::uint16_t GetPendingHeadOptionEquipId();
    void          ClearPendingHeadOptionEquipId();

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

    // Companion stash for the variant index the user picked when
    // confirming a supply-drop order. Set by the supply-drop click
    // hook alongside the developId; consumed by the pickup hook
    // before ForcePartsReload so the variant-specific selectorCode
    // (and downstream LoadPlayerPartsParts variant lookup) lands
    // correctly. Without this, a click on a non-base cell decoded
    // its own selector but the pickup pipeline always fell back to
    // entry->selectorCode (the BASE), and the resulting body
    // depended on whatever GetActiveVariant happened to return —
    // typically the previously-equipped variant, NOT the one the
    // user just requested.
    //
    // Default value 0 = "base / no override" (also covers the case
    // where the click came from a path that never set the stash —
    // legacy supply-drop confirmations still equip the base, which
    // matches the prior behavior).
    void          SetPendingSupplyDropVariantIdx(std::uint8_t variantIndex);
    std::uint8_t  ConsumePendingSupplyDropVariantIdx();
}
