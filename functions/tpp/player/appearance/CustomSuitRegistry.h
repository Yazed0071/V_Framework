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

struct CustomSuitEntry
{
    bool used = false;

    std::uint8_t playerType = 0;
    bool enableHead = true;
    bool enableHand = true;
    bool enableCamo = true;

    std::uint8_t customPartsType = 0xFF;
    std::uint8_t customSelectorCode = 0xFF;

    std::uint16_t linkedDevelopId = 0xFFFF;
    std::uint16_t linkedFlowIndex = 0xFFFF;

    std::uint64_t partsPathCode64Ext = 0;
    std::uint64_t fpkPathCode64Ext = 0;
};

bool RegisterCustomSuit(
    std::uint8_t playerType,
    bool enableHead,
    bool enableHand,
    bool enableCamo,
    const char* partsPath,
    const char* fpkPath,
    std::uint8_t& outPartsType);

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

void ClearAllCustomSuits();