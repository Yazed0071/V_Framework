#pragma once

#include <cstdint>

struct ActiveCustomSuitState
{
    bool valid = false;

    std::uint16_t developId = 0xFFFF;
    std::uint8_t playerType = 0xFF;
    std::uint8_t partsType = 0xFF;
    std::uint8_t selectorCode = 0xFF;
    std::uint16_t faceId = 0xFFFF;
    std::uint16_t headOption = 0x0000;
};

void SetActiveCustomSuit(
    std::uint16_t developId,
    std::uint8_t playerType,
    std::uint8_t partsType,
    std::uint8_t selectorCode,
    std::uint16_t faceId,
    std::uint16_t headOption);

bool TryGetActiveCustomSuit(ActiveCustomSuitState& outState);
void ClearActiveCustomSuit();

struct PreservedAppearanceState
{
    bool valid = false;

    std::uint8_t playerType = 0xFF;
    std::uint8_t armType = 0x00;
    std::uint8_t faceEquipId = 0x00;
    std::uint8_t faceEquipUnk = 0x00;
    std::uint16_t headOption = 0x0000;
};

void RememberPreservedLoadPartsAppearance(
    std::uint8_t playerType,
    std::uint8_t armType,
    std::uint8_t faceEquipId,
    std::uint8_t faceEquipUnk);

void RememberPreservedHeadOption(
    std::uint8_t playerType,
    std::uint16_t headOption);

void RememberPreservedFullAppearance(
    std::uint8_t playerType,
    std::uint8_t armType,
    std::uint8_t faceEquipId,
    std::uint8_t faceEquipUnk,
    std::uint16_t headOption);

bool TryGetPreservedAppearance(
    std::uint8_t playerType,
    PreservedAppearanceState& outState);

// Sub-asset slot state:
//   0                = disabled (don't load this slot)
//   kUseVanilla (1)  = let the game's original loader handle it
//   any other value  = custom path hash to load
static constexpr std::uint64_t kSubAssetDisabled   = 0;
static constexpr std::uint64_t kSubAssetUseVanilla = 1;

struct CustomSuitEntry
{
    bool used = false;

    std::uint8_t playerType = 0;

    std::uint8_t customPartsType = 0xFF;
    std::uint8_t customSelectorCode = 0xFF;

    std::uint16_t linkedDevelopId = 0xFFFF;
    std::uint16_t linkedFlowIndex = 0xFFFF;

    // Core model (required)
    std::uint64_t partsPathCode64Ext = 0;
    std::uint64_t fpkPathCode64Ext = 0;

    // Sub-asset slots: 0 = disabled, 1 = vanilla, hash = custom path
    std::uint64_t camoFpk     = kSubAssetDisabled;    // camo pattern FPK
    std::uint64_t faceFpk     = kSubAssetUseVanilla;   // face/balaclava FOVA
    std::uint64_t armFpk      = kSubAssetUseVanilla;   // bionic arm FOVA
    std::uint64_t skinToneFv2 = kSubAssetUseVanilla;   // DD skin tone FV2
    std::uint64_t diamondFpk  = kSubAssetDisabled;    // diamond mark FPK

    // Body variant group: links multiple suits as variants of the same outfit.
    // All entries with the same non-zero variantGroupId cycle in the head option slot.
    std::uint16_t variantGroupId = 0;
    std::uint8_t  variantIndex = 0;        // 0=base, 1=first variant, 2=second, etc.

    // Derived convenience accessors
    bool HasVariantGroup()   const { return variantGroupId != 0; }
    bool IsFaceEnabled()     const { return faceFpk     != kSubAssetDisabled; }
    bool IsArmEnabled()      const { return armFpk      != kSubAssetDisabled; }
    bool IsCamoEnabled()     const { return camoFpk     != kSubAssetDisabled; }
    bool IsSkinToneEnabled() const { return skinToneFv2  != kSubAssetDisabled; }
    bool IsDiamondEnabled()  const { return diamondFpk  != kSubAssetDisabled; }

    bool IsFaceCustom()      const { return faceFpk     > kSubAssetUseVanilla; }
    bool IsArmCustom()       const { return armFpk      > kSubAssetUseVanilla; }
    bool IsCamoCustom()      const { return camoFpk     > kSubAssetUseVanilla; }
    bool IsSkinToneCustom()  const { return skinToneFv2  > kSubAssetUseVanilla; }
    bool IsDiamondCustom()   const { return diamondFpk  > kSubAssetUseVanilla; }
};

// Sub-asset mode for registration: path string, true (vanilla), or false (disabled).
// Represented as a uint64 hash after resolution.

struct CustomSuitRegistration
{
    std::uint8_t playerType = 0;
    const char* partsPath = nullptr;       // required
    const char* fpkPath = nullptr;         // required
    const char* camoFpk = nullptr;         // optional custom path (null = disabled)
    const char* faceFpk = nullptr;         // optional custom path (null = vanilla)
    const char* armFpk = nullptr;          // optional custom path (null = vanilla)
    const char* skinToneFv2 = nullptr;     // optional custom path (null = vanilla)
    const char* diamondFpk = nullptr;      // optional custom path (null = disabled)
    bool faceVanilla = true;               // true = vanilla, false = disabled (when faceFpk is null)
    bool armVanilla = true;                // true = vanilla, false = disabled (when armFpk is null)
    bool skinToneVanilla = true;           // true = vanilla, false = disabled (when skinToneFv2 is null)
};

bool RegisterCustomSuit(const CustomSuitRegistration& reg, std::uint8_t& outPartsType);

bool LinkDevelopIdToPlayerSuit(
    std::uint16_t developId,
    std::uint8_t customPartsType);

bool LinkDevelopIdToPlayerSuitEx(
    std::uint16_t developId,
    std::uint16_t flowIndex,
    std::uint8_t customPartsType);

bool TryGetCustomSuitByPartsType(
    std::uint8_t customPartsType,
    const CustomSuitEntry** outEntry);

bool TryGetCustomSuitBySelectorCode(
    std::uint8_t selectorCode,
    const CustomSuitEntry** outEntry);

bool TryGetCustomSuitByDevelopId(
    std::uint16_t developId,
    const CustomSuitEntry** outEntry);

bool TryGetCustomSuitByFlowIndex(
    std::uint16_t flowIndex,
    const CustomSuitEntry** outEntry);

bool TryGetCustomSuitByDevelopIdForPlayerType(
    std::uint16_t developId,
    std::uint8_t playerType,
    const CustomSuitEntry** outEntry);

bool TryGetCustomSuitByFlowIndexForPlayerType(
    std::uint16_t flowIndex,
    std::uint8_t playerType,
    const CustomSuitEntry** outEntry);

bool SetPendingCustomSuitDevelopId(std::uint16_t developId);
std::uint16_t GetPendingCustomSuitDevelopId();
void ClearPendingCustomSuitDevelopId();

// Variant group management
std::uint16_t AllocateVariantGroupId();

bool SetVariantGroup(
    std::uint8_t customPartsType,
    std::uint16_t groupId,
    std::uint8_t variantIndex);

// Returns true if any registered custom suit has IsFaceEnabled().
bool HasAnyCustomSuitWithFaceEnabled();

// Returns the number of variants in a group (including the base).
std::size_t GetVariantGroupSize(std::uint16_t groupId);

// Returns all entries in a variant group, sorted by variantIndex.
std::size_t GetVariantGroupEntries(
    std::uint16_t groupId,
    const CustomSuitEntry** outEntries,
    std::size_t maxEntries);

// Checks if a developId belongs to a suit with variants.
bool DoesDevelopIdHaveVariants(std::uint16_t developId);

void ClearAllCustomSuits();