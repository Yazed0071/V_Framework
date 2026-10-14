#include "pch.h"

#include <Windows.h>
#include <array>
#include <atomic>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "NoticeControllerImpl_CheckForceNotice.h"
#include "NoticeControllerImpl_CheckSightNoticePlayer.h"
#include "RadioControllerImpl_StepRadioWaitResponse.h"
#include "SoldierRealizedSlot.h"
#include "SoldierStinkVoice.h"
#include "log.h"

namespace
{
    using GetQuarkSystemTable_t = std::uint8_t*(__fastcall*)();
    using Speak_t               = bool(__fastcall*)(void* soundController, std::uint32_t soldierIndex, std::uint32_t dialogueEvent,
                                                    int priority, std::uint32_t line, float delay);
    using GetPlayerPosition_t   = void(__fastcall*)(void* playerInfo, float* outPosition);
    using GetCpPhase_t          = int(__fastcall*)(void* controller, std::uint32_t groupId, std::uint32_t soldierIndex);

    constexpr std::uint32_t kEnemyVoiceEvent        = 0x95EA16B0u;  // DD_vox_ene
    constexpr std::uint32_t kStinkLine              = 0x9BD0A72Au;  // EVR130
    constexpr int           kStinkPriority          = 4;
    constexpr float         kHeldStinkDelay         = 1.0f;
    constexpr float         kCloseStinkDelay        = 0.0f;
    constexpr std::int32_t  kSecondsPerDay          = 86400;
    constexpr std::int32_t  kDaysBeforeFlies        = 5;
    constexpr float         kCloseDistance          = 8.0f;
    constexpr std::uint64_t kStinkCheckMs           = 1000;
    constexpr std::uint64_t kCloseCheckMs           = 500;
    constexpr std::uint64_t kSoldierCooldownMs      = 60000;
    constexpr std::uint64_t kAnySoldierCooldownMs   = 8000;
    constexpr std::uint64_t kNoticeAfterEwMs        = 1500;
    constexpr std::uint32_t kSoldierSlotCount       = 0x200;
    constexpr std::size_t   kQuarkApplication       = 0x98;
    constexpr std::size_t   kApplicationPlayerVars  = 0x10;
    constexpr std::size_t   kSecondsSinceShower     = 0x64;
    constexpr std::size_t   kControllerSoldiers     = 0x58;
    constexpr std::size_t   kSoldiersPositions      = 0x18;  // +0x20 -> xyz per soldier, 0x10 apart
    constexpr std::size_t   kPositionsBase          = 0x20;
    constexpr std::size_t   kPositionStride         = 0x10;
    constexpr std::size_t   kSoldiersSoundController = 0xA8;
    constexpr std::size_t   kSoldiersLife           = 0x38;  // 0x40 per soldier at +8
    constexpr std::size_t   kLifeRecords            = 0x08;
    constexpr std::size_t   kLifeRecordSize         = 0x40;
    constexpr std::size_t   kLifeFlags              = 0x12;
    constexpr std::uint16_t kLifeFlagsStatus        = 0x3;   // 0 normal, 1 sleep, 2 faint, 3 dying
    constexpr std::size_t   kLifeState              = 0x3E;
    constexpr std::uint8_t  kLifeStateDead          = 2;
    constexpr std::size_t   kSoldiersPlayerInfo     = 0x188;
    constexpr std::size_t   kSoldiersCommandPost    = 0xD0;
    constexpr std::size_t   kCommandPostController  = 0x08;
    constexpr std::size_t   kCommandPostGroupId     = 0x30;
    constexpr int           kPhaseAlert             = 3;
    constexpr int           kPhaseUnknown           = -1;
    constexpr std::size_t   kSpeakSlot              = 0x20 / sizeof(void*);
    constexpr std::size_t   kGetPlayerPositionSlot  = 0x18 / sizeof(void*);
    constexpr std::size_t   kGetCpPhaseSlot         = 0x188 / sizeof(void*);

    std::atomic<bool>                                         g_SnakeStinks{ false };
    std::atomic<std::uint64_t>                                g_StinkCheckedMs{ 0 };
    std::atomic<std::uint64_t>                                g_AnySoldierSpokeMs{ 0 };
    std::array<std::atomic<std::uint64_t>, kSoldierSlotCount> g_SoldierSpokeMs{};
    std::array<std::atomic<std::uint64_t>, kSoldierSlotCount> g_SoldierCheckedMs{};
    std::array<std::atomic<std::uint64_t>, kSoldierSlotCount> g_NoticeDueMs{};
    std::array<std::array<float, 3>, kSoldierSlotCount>       g_NoticeAt{};

    bool ReadSnakeStinks()
    {
        const auto getQuarkSystemTable = reinterpret_cast<GetQuarkSystemTable_t>(ResolveGameAddress(gAddr.GetQuarkSystemTable));
        if (!getQuarkSystemTable)
            return false;
        __try
        {
            const std::uint8_t* application = *reinterpret_cast<std::uint8_t* const*>(getQuarkSystemTable() + kQuarkApplication);
            const std::uint8_t* playerVars = *reinterpret_cast<std::uint8_t* const*>(application + kApplicationPlayerVars);
            const std::int32_t seconds = *reinterpret_cast<const std::int32_t*>(playerVars + kSecondsSinceShower);
            return seconds / kSecondsPerDay >= kDaysBeforeFlies;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool SnakeStinks(std::uint64_t now)
    {
        if (now - g_StinkCheckedMs.load(std::memory_order_relaxed) >= kStinkCheckMs)
        {
            g_SnakeStinks.store(ReadSnakeStinks(), std::memory_order_relaxed);
            g_StinkCheckedMs.store(now, std::memory_order_relaxed);
        }
        return g_SnakeStinks.load(std::memory_order_relaxed);
    }

    bool CooledDown(std::uint32_t soldierIndex, std::uint64_t now)
    {
        return now - g_SoldierSpokeMs[soldierIndex].load(std::memory_order_relaxed) >= kSoldierCooldownMs
            && now - g_AnySoldierSpokeMs.load(std::memory_order_relaxed) >= kAnySoldierCooldownMs;
    }

    const std::uint8_t* SoldiersOf(void* noticeController)
    {
        __try
        {
            return *reinterpret_cast<const std::uint8_t* const*>(static_cast<const std::uint8_t*>(noticeController) + kControllerSoldiers);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    bool IsAwake(const std::uint8_t* soldiers, std::uint32_t soldierIndex)
    {
        __try
        {
            const auto* life = *reinterpret_cast<const std::uint8_t* const*>(
                *reinterpret_cast<const std::uint8_t* const*>(soldiers + kSoldiersLife) + kLifeRecords) + soldierIndex * kLifeRecordSize;
            return life[kLifeState] != kLifeStateDead
                && (*reinterpret_cast<const std::uint16_t*>(life + kLifeFlags) & kLifeFlagsStatus) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    int CommandPostPhase(const std::uint8_t* soldiers, std::uint32_t soldierIndex)
    {
        __try
        {
            const std::uint8_t* commandPost = *reinterpret_cast<const std::uint8_t* const*>(soldiers + kSoldiersCommandPost);
            void* controller = *reinterpret_cast<void* const*>(commandPost + kCommandPostController);
            const std::uint32_t groupId = *reinterpret_cast<const std::uint32_t*>(commandPost + kCommandPostGroupId);
            const auto getCpPhase = (*reinterpret_cast<GetCpPhase_t* const*>(controller))[kGetCpPhaseSlot];
            return getCpPhase(controller, groupId, soldierIndex);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return kPhaseUnknown;
        }
    }

    bool IsCloseToSnake(const std::uint8_t* soldiers, std::uint32_t soldierIndex, float* snake)
    {
        __try
        {
            void* playerInfo = *reinterpret_cast<void* const*>(soldiers + kSoldiersPlayerInfo);
            const auto getPlayerPosition = (*reinterpret_cast<GetPlayerPosition_t* const*>(playerInfo))[kGetPlayerPositionSlot];
            getPlayerPosition(playerInfo, snake);

            const float* soldier = reinterpret_cast<const float*>(
                *reinterpret_cast<const std::uint8_t* const*>(*reinterpret_cast<const std::uint8_t* const*>(soldiers + kSoldiersPositions) + kPositionsBase)
                + soldierIndex * kPositionStride);
            const float dx = soldier[0] - snake[0];
            const float dy = soldier[1] - snake[1];
            const float dz = soldier[2] - snake[2];
            return dx * dx + dy * dy + dz * dz <= kCloseDistance * kCloseDistance;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool SayEw(const std::uint8_t* soldiers, std::uint32_t soldierIndex, float delay, std::uint64_t now)
    {
        g_SoldierSpokeMs[soldierIndex].store(now, std::memory_order_relaxed);
        g_AnySoldierSpokeMs.store(now, std::memory_order_relaxed);
        const std::uint32_t slot = SoldierRealizedSlot::Of(soldiers, soldierIndex);
        if (slot == SoldierRealizedSlot::kNotRealized)
            return false;
        __try
        {
            void* soundController = *reinterpret_cast<void* const*>(soldiers + kSoldiersSoundController);
            const auto speak = (*reinterpret_cast<Speak_t* const*>(soundController))[kSpeakSlot];
            return speak(soundController, slot, kEnemyVoiceEvent, kStinkPriority, kStinkLine, delay);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void PostDueSmellNotice(std::uint32_t soldierIndex, std::uint64_t now)
    {
        const std::uint64_t due = g_NoticeDueMs[soldierIndex].load(std::memory_order_acquire);
        if (due == 0 || now < due)
            return;
        g_NoticeDueMs[soldierIndex].store(0, std::memory_order_relaxed);
        const bool noticed = SoldierNotice::Add(soldierIndex, SoldierNotice::kCalledForHelp, SoldierNotice::kNoTarget,
                                                g_NoticeAt[soldierIndex].data());
        LogInfo("VoiceDebug: soldier %u smell notice %d", soldierIndex, noticed ? 1 : 0);
    }
}

void SoldierStinkVoice_OnSightCheck(void* noticeController, std::uint32_t soldierIndex)
{
    if (soldierIndex >= kSoldierSlotCount || MissionCodeGuard::ShouldBypassHooks())
        return;
    const std::uint64_t now = GetTickCount64();
    PostDueSmellNotice(soldierIndex, now);
    if (!SnakeStinks(now))
        return;
    if (now - g_SoldierCheckedMs[soldierIndex].load(std::memory_order_relaxed) < kCloseCheckMs)
        return;
    g_SoldierCheckedMs[soldierIndex].store(now, std::memory_order_relaxed);
    if (!CooledDown(soldierIndex, now))
        return;

    const std::uint8_t* soldiers = SoldiersOf(noticeController);
    alignas(16) float snake[4] = {};
    if (!soldiers || !IsAwake(soldiers, soldierIndex) || !IsCloseToSnake(soldiers, soldierIndex, snake))
        return;
    bool heldKnown = false;
    if (CpRadioResponse::IsHeld(static_cast<std::uint16_t>(soldierIndex), heldKnown))
        return;
    const int phase = CommandPostPhase(soldiers, soldierIndex);
    if (phase == kPhaseAlert || phase == kPhaseUnknown)
        return;

    const bool spoke = SayEw(soldiers, soldierIndex, kCloseStinkDelay, now);
    g_NoticeAt[soldierIndex] = { snake[0], snake[1], snake[2] };
    g_NoticeDueMs[soldierIndex].store(now + kNoticeAfterEwMs, std::memory_order_release);
    LogInfo("VoiceDebug: soldier %u smells Snake (phase %d), EVR130 speak %d, notice in %llu ms",
        soldierIndex, phase, spoke ? 1 : 0, kNoticeAfterEwMs);
}

void SoldierStinkVoice_OnHeld(std::uint16_t soldierIndex)
{
    if (soldierIndex >= kSoldierSlotCount || MissionCodeGuard::ShouldBypassHooks())
        return;
    const std::uint64_t now = GetTickCount64();
    if (!SnakeStinks(now) || !CooledDown(soldierIndex, now))
        return;

    void* noticeController = SoldierNotice_LiveController();
    const std::uint8_t* soldiers = noticeController ? SoldiersOf(noticeController) : nullptr;
    if (soldiers && IsAwake(soldiers, soldierIndex))
    {
        const bool spoke = SayEw(soldiers, soldierIndex, kHeldStinkDelay, now);
        LogInfo("VoiceDebug: held soldier %u smells Snake, EVR130 speak %d", soldierIndex, spoke ? 1 : 0);
    }
}
