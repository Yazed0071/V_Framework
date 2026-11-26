#include <pch.h>
#include "VIPImportant.h"

#include <Windows.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "log.h"

namespace
{
    using GetEventHash_t  = std::uint32_t(__fastcall*)(void* evt);
    using PlayVoiceLine_t = void(__fastcall*)(void* soundController, std::uint32_t actorId, std::uint32_t dialogueEvent,
                                              int priority, std::uint32_t lineHash, float delaySeconds);

    constexpr std::size_t   kSoldierSlots     = 0x200;
    constexpr std::uint16_t kSoldierIdBase    = 0x400;
    constexpr std::uint16_t kSoldierTypeMask  = 0xFE00;
    constexpr std::uint16_t kSoldierIndexMask = 0x1FF;
    constexpr std::uint16_t kNoSoldier        = 0xFFFF;

    constexpr std::uint64_t kSlotUsed    = 1ull << 63;
    constexpr std::uint64_t kSlotOfficer = 1ull << 62;   // the low 32 bits hold the dead-body label

    constexpr std::size_t   kSoldierStatus    = 0x110;   // one 0x28 record per soldier at +8, the count at +0x10
    constexpr std::size_t   kStatusRecords    = 0x08;
    constexpr std::size_t   kStatusCount      = 0x10;
    constexpr std::size_t   kStatusRecordSize = 0x28;
    constexpr std::uint64_t kVanillaVipBits   = 0x20000003800ull;   // what SetVip and SetVipSpecial both set
    constexpr std::size_t   kStatusSpecial    = 0x10;
    constexpr std::uint64_t kVipSpecialBit    = 1ull << 63;          // SetVipSpecial's extra bit, in the record's third qword

    constexpr std::size_t   kNoticeEntryTable  = 0x90;   // one 0x68 entry per soldier, from the first index at +0x98
    constexpr std::size_t   kNoticeFirstActor  = 0x98;
    constexpr std::size_t   kNoticeEntrySize   = 0x68;
    constexpr std::size_t   kNoticeComrade     = 0x52;   // GameObjectId of the comrade he is reacting to
    constexpr std::size_t   kNoticeKind        = 0x57;
    constexpr std::size_t   kNoticeSoldier     = 0x78;
    constexpr std::size_t   kSoldierSound      = 0xA8;
    constexpr std::size_t   kPlayVoiceLineSlot = 0x20 / sizeof(void*);
    constexpr std::uint32_t kEnemyVoiceEvent   = 0x95EA16B0u;   // DD_vox_ene
    constexpr int           kLinePriority      = 1;
    constexpr float         kLineDelaySeconds  = 1.0f;

    std::array<std::atomic<std::uint64_t>, kSoldierSlots> g_Slots{};
    std::array<std::atomic<std::uint32_t>, kSoldierSlots> g_DeadBodyLabels{};

    std::uint64_t PackSlot(bool isOfficer, std::uint32_t deadBodyLabel)
    {
        return kSlotUsed | (isOfficer ? kSlotOfficer : 0) | deadBodyLabel;
    }
}

std::uint16_t SoldierIndexOfGameObjectId(std::uint32_t gameObjectId)
{
    const auto id = static_cast<std::uint16_t>(gameObjectId);
    if (id == kNoSoldier || (id & kSoldierTypeMask) != kSoldierIdBase)
        return kNoSoldier;
    return static_cast<std::uint16_t>(id & kSoldierIndexMask);
}

bool VIPImportant_Set(std::uint32_t gameObjectId, bool isOfficer, std::uint32_t deadBodyLabel)
{
    const std::uint16_t index = SoldierIndexOfGameObjectId(gameObjectId);
    if (index == kNoSoldier)
        return false;
    g_Slots[index].store(PackSlot(isOfficer, deadBodyLabel), std::memory_order_release);
    return true;
}

bool VIPImportant_Remove(std::uint32_t gameObjectId)
{
    const std::uint16_t index = SoldierIndexOfGameObjectId(gameObjectId);
    if (index == kNoSoldier)
        return false;
    g_Slots[index].store(0, std::memory_order_release);
    return true;
}

void VIPImportant_Clear()
{
    for (auto& slot : g_Slots)
        slot.store(0, std::memory_order_release);
}

bool DeadBodyLabel_Set(std::uint32_t gameObjectId, std::uint32_t label)
{
    const std::uint16_t index = SoldierIndexOfGameObjectId(gameObjectId);
    if (index == kNoSoldier)
        return false;
    g_DeadBodyLabels[index].store(label, std::memory_order_release);
    return true;
}

std::uint32_t DeadBodyLabel_Get(std::uint32_t gameObjectId)
{
    const std::uint16_t index = SoldierIndexOfGameObjectId(gameObjectId);
    if (index == kNoSoldier)
        return 0;
    return g_DeadBodyLabels[index].load(std::memory_order_acquire);
}

void DeadBodyLabel_Clear()
{
    for (auto& label : g_DeadBodyLabels)
        label.store(0, std::memory_order_release);
}

bool VIPImportant_Find(std::uint32_t gameObjectId, VIPImportantInfo* out)
{
    const std::uint16_t index = SoldierIndexOfGameObjectId(gameObjectId);
    if (index == kNoSoldier)
        return false;
    const std::uint64_t slot = g_Slots[index].load(std::memory_order_acquire);
    if ((slot & kSlotUsed) == 0)
        return false;
    if (out)
    {
        out->isOfficer     = (slot & kSlotOfficer) != 0;
        out->deadBodyLabel = static_cast<std::uint32_t>(slot);
    }
    return true;
}

bool IsVIPImportantSoldier(std::uint16_t soldierIndex)
{
    if (soldierIndex >= kSoldierSlots)
        return false;
    return (g_Slots[soldierIndex].load(std::memory_order_acquire) & kSlotUsed) != 0;
}

static bool ReadStatusRecordSEH(const void* soldiers, std::uint16_t soldierIndex, std::uint64_t* flags, std::uint64_t* specialFlags)
{
    if (!soldiers)
        return false;
    __try
    {
        const auto* status = *reinterpret_cast<const std::uint8_t* const*>(static_cast<const std::uint8_t*>(soldiers) + kSoldierStatus);
        const auto* records = *reinterpret_cast<const std::uint8_t* const*>(status + kStatusRecords);
        if (!records || soldierIndex >= *reinterpret_cast<const std::uint32_t*>(status + kStatusCount))
            return false;
        const std::uint8_t* record = records + soldierIndex * kStatusRecordSize;
        *flags = *reinterpret_cast<const std::uint64_t*>(record);
        *specialFlags = *reinterpret_cast<const std::uint64_t*>(record + kStatusSpecial);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool IsVanillaVIPSoldier(const void* soldiers, std::uint16_t soldierIndex)
{
    std::uint64_t flags = 0;
    std::uint64_t specialFlags = 0;
    return ReadStatusRecordSEH(soldiers, soldierIndex, &flags, &specialFlags) && (flags & kVanillaVipBits) == kVanillaVipBits;
}

bool IsVanillaVIPSpecialSoldier(const void* soldiers, std::uint16_t soldierIndex)
{
    std::uint64_t flags = 0;
    std::uint64_t specialFlags = 0;
    return ReadStatusRecordSEH(soldiers, soldierIndex, &flags, &specialFlags)
        && (flags & kVanillaVipBits) == kVanillaVipBits && (specialFlags & kVipSpecialBit) != 0;
}

bool IsVIPSoldier(const void* soldiers, std::uint16_t soldierIndex)
{
    return IsVIPImportantSoldier(soldierIndex) || IsVanillaVIPSoldier(soldiers, soldierIndex);
}

const void* VIPImportant_NoticeSoldiers(void* noticeAction)
{
    if (!noticeAction)
        return nullptr;
    __try
    {
        return *reinterpret_cast<void* const*>(static_cast<std::uint8_t*>(noticeAction) + kNoticeSoldier);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
}

bool VIPImportant_ReadNoticeComrade(void* noticeAction, std::uint32_t actorId, std::uint16_t* comradeId, std::uint8_t* noticeKind)
{
    if (!noticeAction)
        return false;
    __try
    {
        const auto* base = static_cast<const std::uint8_t*>(noticeAction);
        const auto* table = *reinterpret_cast<const std::uint8_t* const*>(base + kNoticeEntryTable);
        const std::uint32_t first = *reinterpret_cast<const std::uint32_t*>(base + kNoticeFirstActor);
        if (!table)
            return false;
        const std::uint8_t* entry = table + static_cast<std::size_t>(actorId - first) * kNoticeEntrySize;
        if (comradeId)
            *comradeId = *reinterpret_cast<const std::uint16_t*>(entry + kNoticeComrade);
        if (noticeKind)
            *noticeKind = entry[kNoticeKind];
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

std::uint32_t VIPImportant_EventHash(void* evt)
{
    if (!evt)
        return 0;
    __try
    {
        const auto getHash = (*static_cast<GetEventHash_t* const*>(evt))[0];
        return getHash ? getHash(evt) : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
}

void VIPImportant_PlayVoiceLine(void* noticeAction, std::uint32_t actorId, std::uint32_t lineHash)
{
    if (!noticeAction)
        return;
    __try
    {
        auto* soldier = *reinterpret_cast<std::uint8_t* const*>(static_cast<std::uint8_t*>(noticeAction) + kNoticeSoldier);
        void* sound = soldier ? *reinterpret_cast<void* const*>(soldier + kSoldierSound) : nullptr;
        if (!sound)
            return;
        const auto play = (*static_cast<PlayVoiceLine_t* const*>(sound))[kPlayVoiceLineSlot];
        if (play)
            play(sound, actorId, kEnemyVoiceEvent, kLinePriority, lineHash, kLineDelaySeconds);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        LogWarn("VIP: actor %u voice line 0x%08X faulted, skipped", actorId, lineHash);
    }
}
