#include "pch.h"

#include <Windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "NoticeControllerImpl_CheckSightNoticePlayer.h"
#include "SoldierIncomingVoice.h"
#include "SoldierRealizedSlot.h"
#include "log.h"

namespace
{
    using Speak_t      = bool(__fastcall*)(void* soundController, std::uint32_t soldierIndex, std::uint32_t dialogueEvent,
                                           int priority, std::uint32_t line, float delay);
    using GetCpPhase_t = int(__fastcall*)(void* controller, std::uint32_t groupId, std::uint32_t soldierIndex);

    constexpr std::uint32_t kEquipTypeMissile        = 8;
    constexpr std::uint32_t kEquipTypeMask           = 0x1F;
    constexpr std::size_t   kGunInfoEquipType        = 0x78;
    constexpr std::uint16_t kOwnerTypePlayer         = 0;
    constexpr std::uint32_t kEnemyVoiceEvent         = 0x95EA16B0u;  // DD_vox_ene
    constexpr std::uint32_t kIncomingLine            = 0x0CCD8755u;  // EVR083
    constexpr int           kShoutPriority           = 4;
    constexpr float         kShoutDelay              = 0.0f;
    constexpr int           kPhaseAlert              = 3;
    constexpr std::uint64_t kSoldierCooldownMs       = 10000;
    constexpr std::uint32_t kSoldierSlotCount        = 0x200;
    constexpr std::uint32_t kMaxWitnesses            = 8;
    constexpr std::uint64_t kPendingShoutMs          = 1500;
    constexpr std::size_t   kShotOwner               = 0x2A;  // shooter GameObjectId, type in the top 7 bits
    constexpr std::size_t   kControllerSoldiers      = 0x58;
    constexpr std::size_t   kSoldiersPositions       = 0x18;  // +0x20 -> xyz per soldier, 0x10 apart
    constexpr std::size_t   kPositionsBase           = 0x20;
    constexpr std::size_t   kPositionStride          = 0x10;
    constexpr std::size_t   kSoldiersSoundController = 0xA8;
    constexpr std::size_t   kSoldiersCommandPost     = 0xD0;
    constexpr std::size_t   kSoldiersSightList       = 0xB8;  // +0x08 count per soldier, +0x10 records per soldier
    constexpr std::size_t   kSightCounts             = 0x08;
    constexpr std::size_t   kSightRecords            = 0x10;
    constexpr std::size_t   kSightRecordStride       = 6;
    constexpr std::size_t   kSightRecordActive       = 4;
    constexpr std::size_t   kSoldiersStateTable      = 0x110;  // +0x10 soldier count
    constexpr std::size_t   kStateTableCount         = 0x10;
    constexpr std::size_t   kCommandPostController   = 0x08;
    constexpr std::size_t   kCommandPostGroupId      = 0x30;
    constexpr std::size_t   kGetCpPhaseSlot          = 0x188 / sizeof(void*);
    constexpr std::size_t   kSpeakSlot               = 0x20 / sizeof(void*);

    std::mutex                                      g_Mutex;
    std::array<std::uint64_t, kSoldierSlotCount>    g_LastShoutMs{};
    std::uint32_t                                   g_Pending[kMaxWitnesses] = {};
    std::uint32_t                                   g_PendingNext = 0;
    std::uint64_t                                   g_PendingSinceMs = 0;
    std::atomic<std::uint32_t>                      g_PendingCount{ 0 };

    template <typename T>
    T At(const std::uint8_t* base, std::size_t offset)
    {
        return *reinterpret_cast<const T*>(base + offset);
    }

    std::uint32_t GunType(const void* gunInfo)
    {
        if (!gunInfo)
            return 0;
        __try
        {
            return At<std::uint8_t>(static_cast<const std::uint8_t*>(gunInfo), kGunInfoEquipType) & kEquipTypeMask;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    bool SeesPlayer(const std::uint8_t* soldiers, std::uint32_t soldierIndex)
    {
        const std::uint8_t* sightList = At<const std::uint8_t*>(soldiers, kSoldiersSightList);
        const std::uint32_t count = At<const std::uint32_t*>(sightList, kSightCounts)[soldierIndex];
        const std::uint8_t* records = At<const std::uint8_t* const*>(sightList, kSightRecords)[soldierIndex];
        for (std::uint32_t i = 0; i < count; ++i)
        {
            if (records[i * kSightRecordStride + kSightRecordActive] == 1)
                return true;
        }
        return false;
    }

    int CommandPostPhase(const std::uint8_t* soldiers, std::uint32_t soldierIndex)
    {
        const std::uint8_t* commandPost = At<const std::uint8_t*>(soldiers, kSoldiersCommandPost);
        void* controller = At<void*>(commandPost, kCommandPostController);
        const std::uint32_t groupId = At<std::uint32_t>(commandPost, kCommandPostGroupId);
        GetCpPhase_t getCpPhase = (*reinterpret_cast<GetCpPhase_t* const*>(controller))[kGetCpPhaseSlot];
        return getCpPhase(controller, groupId, soldierIndex);
    }

    float DistanceSquared(const std::uint8_t* soldiers, std::uint32_t soldierIndex, const float origin[3])
    {
        const float* position = reinterpret_cast<const float*>(
            At<const std::uint8_t*>(At<const std::uint8_t*>(soldiers, kSoldiersPositions), kPositionsBase)
            + soldierIndex * kPositionStride);
        const float dx = position[0] - origin[0];
        const float dy = position[1] - origin[1];
        const float dz = position[2] - origin[2];
        return dx * dx + dy * dy + dz * dz;
    }

    struct Witness
    {
        std::uint32_t index;
        float         distance;
    };

    struct WitnessScan
    {
        Witness       witnesses[kMaxWitnesses] = {};
        std::uint32_t witnessCount = 0;
        std::uint32_t soldiers     = 0;
        std::uint32_t seeing       = 0;
        std::uint32_t seeingAlert  = 0;
        int           seeingPhase  = -1;
        bool          faulted      = false;
    };

    void KeepClosest(WitnessScan& scan, std::uint32_t index, float distance)
    {
        std::uint32_t slot = 0;
        if (scan.witnessCount < kMaxWitnesses)
            slot = scan.witnessCount++;
        else if (distance < scan.witnesses[kMaxWitnesses - 1].distance)
            slot = kMaxWitnesses - 1;
        else
            return;
        while (slot > 0 && scan.witnesses[slot - 1].distance > distance)
        {
            scan.witnesses[slot] = scan.witnesses[slot - 1];
            --slot;
        }
        scan.witnesses[slot] = { index, distance };
    }

    WitnessScan AlertedWitnesses(const std::uint8_t* soldiers, const float origin[3], std::uint64_t now)
    {
        WitnessScan scan;
        __try
        {
            scan.soldiers = At<std::uint32_t>(At<const std::uint8_t*>(soldiers, kSoldiersStateTable), kStateTableCount);
            for (std::uint32_t index = 0; index < scan.soldiers && index < kSoldierSlotCount; ++index)
            {
                if (!SeesPlayer(soldiers, index))
                    continue;
                ++scan.seeing;
                const int phase = CommandPostPhase(soldiers, index);
                scan.seeingPhase = phase;
                if (phase != kPhaseAlert)
                    continue;
                ++scan.seeingAlert;
                if (now - g_LastShoutMs[index] < kSoldierCooldownMs)
                    continue;
                KeepClosest(scan, index, DistanceSquared(soldiers, index, origin));
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            scan.faulted = true;
        }
        return scan;
    }

    std::uint16_t ShotOwner(const void* fireRequest)
    {
        __try
        {
            return At<std::uint16_t>(static_cast<const std::uint8_t*>(fireRequest), kShotOwner);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0xFFFF;
        }
    }

    const std::uint8_t* SoldiersOf(void* controller)
    {
        __try
        {
            return At<const std::uint8_t*>(static_cast<const std::uint8_t*>(controller), kControllerSoldiers);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    bool Shout(const std::uint8_t* soldiers, std::uint32_t soldierIndex)
    {
        const std::uint32_t slot = SoldierRealizedSlot::Of(soldiers, soldierIndex);
        if (slot == SoldierRealizedSlot::kNotRealized)
            return false;
        __try
        {
            void* soundController = At<void*>(soldiers, kSoldiersSoundController);
            Speak_t speak = (*reinterpret_cast<Speak_t* const*>(soundController))[kSpeakSlot];
            return speak(soundController, slot, kEnemyVoiceEvent, kShoutPriority, kIncomingLine, kShoutDelay);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint64_t g_LastShotLogMs = 0;
}

void SoldierIncomingVoice_OnShot(const void* fireRequest, const void* gunInfo, int engineEquipId, const float shotOrigin[3])
{
    if (MissionCodeGuard::ShouldBypassHooks() || !fireRequest)
        return;

    const std::uint64_t now = GetTickCount64();
    const std::uint32_t equipType = GunType(gunInfo);
    const std::uint16_t owner = ShotOwner(fireRequest);
    const bool byPlayer = (owner >> 9) == kOwnerTypePlayer;
    if (byPlayer && (equipType == kEquipTypeMissile || now - g_LastShotLogMs > 2000))
    {
        g_LastShotLogMs = now;
        LogInfo("VoiceDebug: player shot owner 0x%X equip 0x%X gun type %u", owner, engineEquipId, equipType);
    }
    if (!byPlayer || equipType != kEquipTypeMissile)
        return;

    void* controller = SoldierNotice_LiveController();
    const std::uint8_t* soldiers = controller ? SoldiersOf(controller) : nullptr;
    if (!soldiers)
    {
        LogInfo("VoiceDebug: incoming skipped, no live soldier system (controller %p)", controller);
        return;
    }

    std::lock_guard<std::mutex> lock(g_Mutex);
    const WitnessScan scan = AlertedWitnesses(soldiers, shotOrigin, now);
    LogInfo("VoiceDebug: incoming scan soldiers %u seeing %u seeingAlert %u lastPhase %d candidates %u faulted %d",
        scan.soldiers, scan.seeing, scan.seeingAlert, scan.seeingPhase, scan.witnessCount, scan.faulted ? 1 : 0);
    for (std::uint32_t i = 0; i < scan.witnessCount; ++i)
        g_Pending[i] = scan.witnesses[i].index;
    g_PendingNext = 0;
    g_PendingSinceMs = now;
    g_PendingCount.store(scan.witnessCount, std::memory_order_release);
}

void SoldierIncomingVoice_OnSightCheck(void* noticeController, std::uint32_t soldierIndex)
{
    if (g_PendingCount.load(std::memory_order_acquire) == 0)
        return;

    std::lock_guard<std::mutex> lock(g_Mutex);
    const std::uint32_t count = g_PendingCount.load(std::memory_order_relaxed);
    if (count == 0 || g_PendingNext >= count || g_Pending[g_PendingNext] != soldierIndex)
        return;

    const std::uint64_t now = GetTickCount64();
    const std::uint8_t* soldiers = SoldiersOf(noticeController);
    if (now - g_PendingSinceMs > kPendingShoutMs || !soldiers)
    {
        g_PendingCount.store(0, std::memory_order_release);
        return;
    }

    const bool spoke = Shout(soldiers, soldierIndex);
    LogInfo("VoiceDebug: incoming EVR083 soldier %u speak %d (%llu ms after the shot)", soldierIndex, spoke ? 1 : 0, now - g_PendingSinceMs);
    if (spoke)
    {
        g_LastShoutMs[soldierIndex] = now;
        g_PendingCount.store(0, std::memory_order_release);
        return;
    }
    if (++g_PendingNext >= count)
        g_PendingCount.store(0, std::memory_order_release);
}
