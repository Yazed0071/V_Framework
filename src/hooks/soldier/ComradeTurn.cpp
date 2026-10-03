#include "pch.h"
#include "ComradeTurn.h"

#include <Windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace
{
    using IsPlayingVoice_t = bool(__fastcall*)(void* soundController, std::uint32_t soldierIndex);

    constexpr std::size_t   kSoldierSoundController  = 0xA8;
    constexpr std::size_t   kIsPlayingVoiceVtblIndex = 0x58 / 8;
    constexpr std::size_t   kSoldierStatus           = 0x110;
    constexpr std::size_t   kStatusArray             = 0x08;
    constexpr std::size_t   kStatusCount             = 0x10;
    constexpr std::size_t   kStatusSize              = 0x28;
    constexpr std::uint64_t kStatusUpperBodyFree     = 1ull << 37;   // set by standing/walking states that allow idle gestures

    constexpr std::size_t   kCommandNoticeParam      = 0x20;
    constexpr std::size_t   kCommandNoticeKind       = 0x28;
    constexpr std::size_t   kCommandKeyActionType    = 0xB4;
    constexpr std::uint8_t  kKeyActionNotice         = 8;
    constexpr std::uint32_t kNoticeTurnToTarget      = 0xE;     // NoticeAction's TurnAction, in place of the "huh?" head tilt
    constexpr float         kTurnParam               = 2.0f;    // what StepAware passes along with a turn

    constexpr std::uint64_t kWaitToStandStillMs      = 1000;
    constexpr std::uint64_t kReplyStartGraceMs       = 1000;
    constexpr std::uint64_t kLongestReplyMs          = 3000;

    enum class Stage : int
    {
        Idle,
        WaitingForNotice,
        Turning,
        Holding,
        Replying,
    };

    std::atomic<int>  g_Stage{ static_cast<int>(Stage::Idle) };
    std::atomic<bool> g_ReplyCue{ false };
    std::uint32_t     g_ComradeIndex = 0;
    std::uint64_t     g_StageStarted = 0;
    bool              g_ReplyHeard   = false;

    template <typename T>
    T& At(std::uint8_t* base, std::size_t offset)
    {
        return *reinterpret_cast<T*>(base + offset);
    }

    Stage CurrentStage()
    {
        return static_cast<Stage>(g_Stage.load(std::memory_order_acquire));
    }

    void SetStage(Stage stage)
    {
        g_StageStarted = GetTickCount64();
        g_Stage.store(static_cast<int>(stage), std::memory_order_release);
    }

    bool IsUpperBodyFree(std::uint8_t* soldier, std::uint32_t soldierIndex)
    {
        std::uint8_t* status = At<std::uint8_t*>(soldier, kSoldierStatus);
        if (soldierIndex >= At<std::uint32_t>(status, kStatusCount))
            return false;

        std::uint8_t* entry = At<std::uint8_t*>(status, kStatusArray) + soldierIndex * kStatusSize;
        return (At<std::uint64_t>(entry, 0) & kStatusUpperBodyFree) != 0;
    }

    bool IsSaying(std::uint8_t* soldier, std::uint32_t soldierIndex)
    {
        void* soundController = At<void*>(soldier, kSoldierSoundController);
        auto isPlaying = (*reinterpret_cast<IsPlayingVoice_t**>(soundController))[kIsPlayingVoiceVtblIndex];
        return isPlaying(soundController, soldierIndex);
    }

    void Answer()
    {
        g_ReplyHeard = false;
        g_ReplyCue.store(true, std::memory_order_release);
        SetStage(Stage::Replying);
    }
}

void CallComrade::TurnToCaller(std::uint32_t comradeIndex)
{
    g_ComradeIndex = comradeIndex;
    g_ReplyCue.store(false, std::memory_order_release);
    SetStage(Stage::WaitingForNotice);
}

bool CallComrade::TakeReplyCue()
{
    return g_ReplyCue.exchange(false, std::memory_order_acq_rel);
}

void CallComrade::StopTurn()
{
    g_ReplyCue.store(false, std::memory_order_release);
    SetStage(Stage::Idle);
}

void CallComrade::OnSetCommand(std::uint32_t soldierIndex, std::uint8_t* command)
{
    const Stage stage = CurrentStage();
    if ((stage != Stage::WaitingForNotice && stage != Stage::Turning) || soldierIndex != g_ComradeIndex || !command)
        return;
    if (At<std::uint8_t>(command, kCommandKeyActionType) != kKeyActionNotice)
        return;

    At<std::uint32_t>(command, kCommandNoticeKind) = kNoticeTurnToTarget;
    At<float>(command, kCommandNoticeParam)        = kTurnParam;
    if (stage != Stage::Turning)
        SetStage(Stage::Turning);
}

bool CallComrade::ShouldHoldMovePause(std::uint8_t* soldier, std::uint32_t soldierIndex)
{
    const Stage stage = CurrentStage();
    if (stage == Stage::Idle || soldierIndex != g_ComradeIndex || !soldier)
        return false;

    const std::uint64_t elapsed = GetTickCount64() - g_StageStarted;
    switch (stage)
    {
    case Stage::WaitingForNotice:
    case Stage::Turning:
        SetStage(Stage::Holding);
        return true;

    case Stage::Holding:
        if (IsUpperBodyFree(soldier, soldierIndex) || elapsed >= kWaitToStandStillMs)
            Answer();
        return true;

    case Stage::Replying:
        if (IsSaying(soldier, soldierIndex))
            g_ReplyHeard = true;
        else if (g_ReplyHeard || elapsed >= kReplyStartGraceMs)
        {
            SetStage(Stage::Idle);
            return false;
        }
        if (elapsed >= kLongestReplyMs)
        {
            SetStage(Stage::Idle);
            return false;
        }
        return true;

    default:
        return false;
    }
}
