#include "pch.h"

#include <cstdint>
#include <cstring>
#include <mutex>

#include "FoxHashes.h"
#include "log.h"
#include "tpp/player/appearance/CustomSuitRegistry.h"

namespace
{
    static constexpr std::size_t kMaxCustomSuits = 128;
    static constexpr std::uint8_t kCustomPartsTypeStart = 0x40;
    static constexpr std::uint8_t kCustomPartsTypeEnd = 0x7F;
    static constexpr std::uint8_t kCustomSelectorStart = 0x80;
    static constexpr std::uint8_t kCustomSelectorEnd = 0xFE;
    static constexpr std::size_t kMaxPlayerTypes = 8;

    static CustomSuitEntry g_CustomSuits[kMaxCustomSuits]{};
    static std::mutex g_CustomSuitMutex;

    static ActiveCustomSuitState g_ActiveCustomSuit{};
    static std::uint16_t g_PendingCustomSuitDevelopId = 0;

    static PreservedAppearanceState g_PreservedAppearance[kMaxPlayerTypes]{};

    static CustomSuitEntry* FindFreeEntry_NoLock()
    {
        for (auto& entry : g_CustomSuits)
        {
            if (!entry.used)
                return &entry;
        }

        return nullptr;
    }

    static CustomSuitEntry* FindByPartsType_NoLock(std::uint8_t partsType)
    {
        for (auto& entry : g_CustomSuits)
        {
            if (entry.used && entry.customPartsType == partsType)
                return &entry;
        }

        return nullptr;
    }

    static CustomSuitEntry* FindBySelector_NoLock(std::uint8_t selectorCode)
    {
        for (auto& entry : g_CustomSuits)
        {
            if (entry.used && entry.customSelectorCode == selectorCode)
                return &entry;
        }

        return nullptr;
    }

    static CustomSuitEntry* FindByDevelopId_NoLock(std::uint16_t developId)
    {
        for (auto& entry : g_CustomSuits)
        {
            if (entry.used && entry.linkedDevelopId == developId)
                return &entry;
        }

        return nullptr;
    }

    static CustomSuitEntry* FindByFlowIndex_NoLock(std::uint16_t flowIndex)
    {
        for (auto& entry : g_CustomSuits)
        {
            if (entry.used && entry.linkedFlowIndex == flowIndex)
                return &entry;
        }

        return nullptr;
    }

    static CustomSuitEntry* FindByDevelopIdAndPlayerType_NoLock(
        std::uint16_t developId,
        std::uint8_t playerType)
    {
        for (auto& entry : g_CustomSuits)
        {
            if (entry.used &&
                entry.linkedDevelopId == developId &&
                entry.playerType == playerType)
            {
                return &entry;
            }
        }

        return nullptr;
    }

    static CustomSuitEntry* FindByFlowIndexAndPlayerType_NoLock(
        std::uint16_t flowIndex,
        std::uint8_t playerType)
    {
        for (auto& entry : g_CustomSuits)
        {
            if (entry.used &&
                entry.linkedFlowIndex == flowIndex &&
                entry.playerType == playerType)
            {
                return &entry;
            }
        }

        return nullptr;
    }

    static CustomSuitEntry* FindByPathsAndPlayerType_NoLock(
        std::uint64_t partsHash,
        std::uint64_t fpkHash,
        std::uint8_t playerType)
    {
        for (auto& entry : g_CustomSuits)
        {
            if (entry.used &&
                entry.partsPathCode64Ext == partsHash &&
                entry.fpkPathCode64Ext == fpkHash &&
                entry.playerType == playerType)
            {
                return &entry;
            }
        }

        return nullptr;
    }

    static std::uint8_t AllocateCustomPartsType_NoLock()
    {
        for (std::uint32_t pt = kCustomPartsTypeStart; pt <= kCustomPartsTypeEnd; ++pt)
        {
            if (!FindByPartsType_NoLock(static_cast<std::uint8_t>(pt)))
                return static_cast<std::uint8_t>(pt);
        }

        return 0xFF;
    }

    static std::uint8_t AllocateCustomSelector_NoLock()
    {
        for (std::uint32_t code = kCustomSelectorStart; code <= kCustomSelectorEnd; ++code)
        {
            if (!FindBySelector_NoLock(static_cast<std::uint8_t>(code)))
                return static_cast<std::uint8_t>(code);
        }

        return 0xFF;
    }
}

void SetActiveCustomSuit(
    std::uint16_t developId,
    std::uint8_t playerType,
    std::uint8_t partsType,
    std::uint8_t selectorCode,
    std::uint16_t faceId,
    std::uint16_t headOption)
{
    std::lock_guard<std::mutex> lock(g_CustomSuitMutex);

    g_ActiveCustomSuit.valid = true;
    g_ActiveCustomSuit.developId = developId;
    g_ActiveCustomSuit.playerType = playerType;
    g_ActiveCustomSuit.partsType = partsType;
    g_ActiveCustomSuit.selectorCode = selectorCode;
    g_ActiveCustomSuit.faceId = faceId;
    g_ActiveCustomSuit.headOption = headOption;

    Log(
        "[CustomSuit] Active developId=%u parts=0x%02X selector=0x%02X type=0x%02X face=0x%04X head=0x%02X\n",
        static_cast<unsigned>(developId),
        static_cast<unsigned>(partsType),
        static_cast<unsigned>(selectorCode),
        static_cast<unsigned>(playerType),
        static_cast<unsigned>(faceId),
        static_cast<unsigned>(headOption & 0xFF)
    );
}

bool TryGetActiveCustomSuit(ActiveCustomSuitState& outState)
{
    std::lock_guard<std::mutex> lock(g_CustomSuitMutex);

    if (!g_ActiveCustomSuit.valid)
        return false;

    outState = g_ActiveCustomSuit;
    return true;
}

void ClearActiveCustomSuit()
{
    std::lock_guard<std::mutex> lock(g_CustomSuitMutex);
    g_ActiveCustomSuit = {};
}

void RememberPreservedLoadPartsAppearance(
    std::uint8_t playerType,
    std::uint8_t armType,
    std::uint8_t faceEquipId,
    std::uint8_t faceEquipUnk)
{
    if (playerType >= kMaxPlayerTypes)
        return;

    // Ignore empty snapshots completely.
    if (armType == 0 && faceEquipId == 0 && faceEquipUnk == 0)
        return;

    std::lock_guard<std::mutex> lock(g_CustomSuitMutex);

    auto& slot = g_PreservedAppearance[playerType];
    slot.valid = true;
    slot.playerType = playerType;
    slot.armType = armType;
    slot.faceEquipId = faceEquipId;
    slot.faceEquipUnk = faceEquipUnk;

    Log(
        "[CustomSuit] Preserve loadparts type=0x%02X arm=%u faceEquip=%u unk=0x%02X\n",
        static_cast<unsigned>(playerType),
        static_cast<unsigned>(slot.armType),
        static_cast<unsigned>(slot.faceEquipId),
        static_cast<unsigned>(slot.faceEquipUnk)
    );
}

void RememberPreservedHeadOption(
    std::uint8_t playerType,
    std::uint16_t headOption)
{
    if (playerType >= kMaxPlayerTypes)
        return;

    if (headOption == 0 || headOption == 0xFFFF)
        return;

    std::lock_guard<std::mutex> lock(g_CustomSuitMutex);

    auto& slot = g_PreservedAppearance[playerType];
    slot.valid = true;
    slot.playerType = playerType;
    slot.headOption = headOption;

    Log(
        "[CustomSuit] Preserve headOption type=0x%02X head=0x%04X\n",
        static_cast<unsigned>(playerType),
        static_cast<unsigned>(headOption)
    );
}

void RememberPreservedFullAppearance(
    std::uint8_t playerType,
    std::uint8_t armType,
    std::uint8_t faceEquipId,
    std::uint8_t faceEquipUnk,
    std::uint16_t headOption)
{
    if (playerType >= kMaxPlayerTypes)
        return;

    if (armType == 0 &&
        faceEquipId == 0 &&
        faceEquipUnk == 0 &&
        (headOption == 0 || headOption == 0xFFFF))
    {
        return;
    }

    std::lock_guard<std::mutex> lock(g_CustomSuitMutex);

    auto& slot = g_PreservedAppearance[playerType];
    slot.valid = true;
    slot.playerType = playerType;

    if (armType != 0)
        slot.armType = armType;

    if (faceEquipId != 0 || faceEquipUnk != 0)
    {
        slot.faceEquipId = faceEquipId;
        slot.faceEquipUnk = faceEquipUnk;
    }

    if (headOption != 0 && headOption != 0xFFFF)
        slot.headOption = headOption;

    Log(
        "[CustomSuit] Preserve full type=0x%02X arm=%u faceEquip=%u unk=0x%02X head=0x%04X\n",
        static_cast<unsigned>(playerType),
        static_cast<unsigned>(slot.armType),
        static_cast<unsigned>(slot.faceEquipId),
        static_cast<unsigned>(slot.faceEquipUnk),
        static_cast<unsigned>(slot.headOption)
    );
}

bool TryGetPreservedAppearance(
    std::uint8_t playerType,
    PreservedAppearanceState& outState)
{
    if (playerType >= kMaxPlayerTypes)
        return false;

    std::lock_guard<std::mutex> lock(g_CustomSuitMutex);

    const auto& slot = g_PreservedAppearance[playerType];
    if (!slot.valid)
        return false;

    outState = slot;
    return true;
}

bool RegisterCustomSuit(
    std::uint8_t playerType,
    bool enableHead,
    bool enableHand,
    bool enableCamo,
    const char* partsPath,
    const char* fpkPath,
    std::uint8_t& outPartsType)
{
    outPartsType = 0xFF;

    if (!partsPath || !*partsPath || !fpkPath || !*fpkPath)
        return false;

    const std::uint64_t partsHash = FoxHashes::PathCode64Ext(partsPath);
    const std::uint64_t fpkHash = FoxHashes::PathCode64Ext(fpkPath);

    std::lock_guard<std::mutex> lock(g_CustomSuitMutex);

    CustomSuitEntry* entry = FindByPathsAndPlayerType_NoLock(partsHash, fpkHash, playerType);

    if (!entry)
    {
        entry = FindFreeEntry_NoLock();
        if (!entry)
        {
            Log("[CustomSuit] No free suit entry slots left\n");
            return false;
        }

        const std::uint8_t partsType = AllocateCustomPartsType_NoLock();
        if (partsType == 0xFF)
        {
            Log("[CustomSuit] No free custom partsType left\n");
            return false;
        }

        const std::uint8_t selectorCode = AllocateCustomSelector_NoLock();
        if (selectorCode == 0xFF)
        {
            Log("[CustomSuit] No free custom selector code left\n");
            return false;
        }

        entry->used = true;
        entry->customPartsType = partsType;
        entry->customSelectorCode = selectorCode;
        entry->linkedDevelopId = 0xFFFF;
        entry->linkedFlowIndex = 0xFFFF;
    }

    entry->playerType = playerType;
    entry->enableHead = enableHead;
    entry->enableHand = enableHand;
    entry->enableCamo = enableCamo;
    entry->partsPathCode64Ext = partsHash;
    entry->fpkPathCode64Ext = fpkHash;

    outPartsType = entry->customPartsType;

    Log(
        "[CustomSuit] Registered partsType=%u selector=0x%02X playerType=%u head=%u hand=%u camo=%u\n",
        static_cast<unsigned>(entry->customPartsType),
        static_cast<unsigned>(entry->customSelectorCode),
        static_cast<unsigned>(entry->playerType),
        entry->enableHead ? 1u : 0u,
        entry->enableHand ? 1u : 0u,
        entry->enableCamo ? 1u : 0u
    );

    return true;
}

bool LinkDevelopIdToPlayerSuit(
    std::uint16_t developId,
    std::uint8_t customPartsType)
{
    return LinkDevelopIdToPlayerSuitEx(developId, 0xFFFF, customPartsType);
}

bool LinkDevelopIdToPlayerSuitEx(
    std::uint16_t developId,
    std::uint16_t flowIndex,
    std::uint8_t customPartsType)
{
    if (developId == 0 || developId == 0xFFFF)
    {
        Log("[CustomSuit] Refusing invalid developId=%u\n", static_cast<unsigned>(developId));
        return false;
    }

    std::lock_guard<std::mutex> lock(g_CustomSuitMutex);

    CustomSuitEntry* entry = FindByPartsType_NoLock(customPartsType);
    if (!entry)
    {
        Log(
            "[CustomSuit] Link failed: no suit for partsType=%u\n",
            static_cast<unsigned>(customPartsType)
        );
        return false;
    }

    entry->linkedDevelopId = developId;
    entry->linkedFlowIndex = flowIndex;

    Log(
        "[CustomSuit] Linked developId=%u flowIndex=%u -> partsType=%u selector=0x%02X\n",
        static_cast<unsigned>(developId),
        static_cast<unsigned>(flowIndex),
        static_cast<unsigned>(entry->customPartsType),
        static_cast<unsigned>(entry->customSelectorCode)
    );

    return true;
}

bool TryGetCustomSuitByPartsType(
    std::uint8_t customPartsType,
    const CustomSuitEntry** outEntry)
{
    if (outEntry)
        *outEntry = nullptr;

    std::lock_guard<std::mutex> lock(g_CustomSuitMutex);

    CustomSuitEntry* entry = FindByPartsType_NoLock(customPartsType);
    if (!entry)
        return false;

    if (outEntry)
        *outEntry = entry;

    return true;
}

bool TryGetCustomSuitBySelectorCode(
    std::uint8_t selectorCode,
    const CustomSuitEntry** outEntry)
{
    if (outEntry)
        *outEntry = nullptr;

    std::lock_guard<std::mutex> lock(g_CustomSuitMutex);

    CustomSuitEntry* entry = FindBySelector_NoLock(selectorCode);
    if (!entry)
        return false;

    if (outEntry)
        *outEntry = entry;

    return true;
}

bool TryGetCustomSuitByDevelopId(
    std::uint16_t developId,
    const CustomSuitEntry** outEntry)
{
    if (outEntry)
        *outEntry = nullptr;

    std::lock_guard<std::mutex> lock(g_CustomSuitMutex);

    CustomSuitEntry* entry = FindByDevelopId_NoLock(developId);
    if (!entry)
        return false;

    if (outEntry)
        *outEntry = entry;

    return true;
}

bool TryGetCustomSuitByFlowIndex(
    std::uint16_t flowIndex,
    const CustomSuitEntry** outEntry)
{
    if (outEntry)
        *outEntry = nullptr;

    std::lock_guard<std::mutex> lock(g_CustomSuitMutex);

    CustomSuitEntry* entry = FindByFlowIndex_NoLock(flowIndex);
    if (!entry)
        return false;

    if (outEntry)
        *outEntry = entry;

    return true;
}

bool TryGetCustomSuitByDevelopIdForPlayerType(
    std::uint16_t developId,
    std::uint8_t playerType,
    const CustomSuitEntry** outEntry)
{
    if (outEntry)
        *outEntry = nullptr;

    std::lock_guard<std::mutex> lock(g_CustomSuitMutex);

    CustomSuitEntry* entry =
        FindByDevelopIdAndPlayerType_NoLock(developId, playerType);

    if (!entry)
        entry = FindByDevelopId_NoLock(developId);

    if (!entry)
        return false;

    if (outEntry)
        *outEntry = entry;

    return true;
}

bool TryGetCustomSuitByFlowIndexForPlayerType(
    std::uint16_t flowIndex,
    std::uint8_t playerType,
    const CustomSuitEntry** outEntry)
{
    if (outEntry)
        *outEntry = nullptr;

    std::lock_guard<std::mutex> lock(g_CustomSuitMutex);

    CustomSuitEntry* entry =
        FindByFlowIndexAndPlayerType_NoLock(flowIndex, playerType);

    if (!entry)
        entry = FindByFlowIndex_NoLock(flowIndex);

    if (!entry)
        return false;

    if (outEntry)
        *outEntry = entry;

    return true;
}

bool SetPendingCustomSuitDevelopId(std::uint16_t developId)
{
    std::lock_guard<std::mutex> lock(g_CustomSuitMutex);

    if (developId == 0 || developId == 0xFFFF)
        return false;

    g_PendingCustomSuitDevelopId = developId;
    Log("[CustomSuit] Pending developId=%u\n", static_cast<unsigned>(developId));
    return true;
}

std::uint16_t GetPendingCustomSuitDevelopId()
{
    std::lock_guard<std::mutex> lock(g_CustomSuitMutex);
    return g_PendingCustomSuitDevelopId;
}

void ClearPendingCustomSuitDevelopId()
{
    std::lock_guard<std::mutex> lock(g_CustomSuitMutex);
    g_PendingCustomSuitDevelopId = 0;
}

void ClearAllCustomSuits()
{
    std::lock_guard<std::mutex> lock(g_CustomSuitMutex);

    std::memset(g_CustomSuits, 0, sizeof(g_CustomSuits));
    std::memset(g_PreservedAppearance, 0, sizeof(g_PreservedAppearance));
    g_ActiveCustomSuit = {};
    g_PendingCustomSuitDevelopId = 0;

    Log("[CustomSuit] Cleared all custom suits\n");
}