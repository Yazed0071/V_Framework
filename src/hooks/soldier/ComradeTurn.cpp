#include "pch.h"
#include "ComradeTurn.h"

#include <Windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "GameClock.h"

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

    constexpr std::size_t   kCommandMoveFlags        = 0xB0;
    constexpr std::uint8_t  kKeyActionMove           = 1;
    constexpr std::uint32_t kMoveSpeedMask           = 0x0E;    // 0x10 above it is weapon ready
    constexpr std::uint32_t kMoveWalk                = 0x2;
    constexpr std::uint32_t kMoveRun                 = 0x4;     // what NoticeNoiseAi's StepRun asks for

    constexpr std::uint64_t kShortestPauseMs         = 1500;    // GZ's comrade waits this long before he reacts
    constexpr std::uint64_t kLongestPauseMs          = 2500;    // even if he never gets to stand still
    constexpr std::uint64_t kReplyStartGraceMs       = 1000;
    constexpr std::uint64_t kLongestReplyMs          = 3000;
    constexpr std::uint64_t kLongestHurryMs          = 20000;
    constexpr std::uint64_t kLongestWalkOverMs       = 60000;
    constexpr std::uint64_t kStayOnArrivalMs         = 1000;    // then back to his route, as in GZ
    constexpr int           kNobody                  = -1;

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
    std::atomic<int>  g_HurryingComrade{ kNobody };
    std::atomic<bool> g_HurryArmed{ false };   // only once he has answered: the moves before that are him standing still
    std::uint64_t     g_HurryUntil   = 0;
    bool              g_HurryMoving  = false;
    std::atomic<int>  g_WalkingComrade{ kNobody };   // heading for where the shout came from
    std::uint64_t     g_WalkUntil    = 0;
    std::uint64_t     g_ArrivedAt    = 0;

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
        g_StageStarted = GameClock::NowMs();
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

    void Hurry(std::uint32_t soldierIndex, std::uint8_t* command)
    {
        if (g_HurryingComrade.load(std::memory_order_acquire) != static_cast<int>(soldierIndex))
            return;
        if (GetTickCount64() >= g_HurryUntil)
        {
            g_HurryingComrade.store(kNobody, std::memory_order_release);
            return;
        }
        if (!g_HurryArmed.load(std::memory_order_acquire))
            return;

        if (At<std::uint8_t>(command, kCommandKeyActionType) != kKeyActionMove)
        {
            if (g_HurryMoving)
                g_HurryingComrade.store(kNobody, std::memory_order_release);   // he got there
            return;
        }

        std::uint32_t& flags = At<std::uint32_t>(command, kCommandMoveFlags);
        if ((flags & kMoveSpeedMask) == kMoveWalk)
            flags = (flags & ~kMoveSpeedMask) | kMoveRun;
        g_HurryMoving = true;
    }

    bool SetOff(std::uint32_t soldierIndex)   // MovePause lets him go: from here his moves are the walk over
    {
        if (g_HurryingComrade.load(std::memory_order_acquire) == static_cast<int>(soldierIndex))
            g_HurryArmed.store(true, std::memory_order_release);
        return false;
    }
}

void CallComrade::TurnToCaller(std::uint32_t comradeIndex)
{
    g_HurryingComrade.store(kNobody, std::memory_order_release);
    g_ComradeIndex = comradeIndex;
    g_ReplyCue.store(false, std::memory_order_release);
    g_WalkUntil = GetTickCount64() + kLongestWalkOverMs;
    g_ArrivedAt = 0;
    g_WalkingComrade.store(static_cast<int>(comradeIndex), std::memory_order_release);
    SetStage(Stage::WaitingForNotice);
}

void CallComrade::HurryToCaller(std::uint32_t comradeIndex)
{
    g_HurryUntil  = GetTickCount64() + kLongestHurryMs;
    g_HurryMoving = false;
    g_HurryArmed.store(false, std::memory_order_release);
    g_HurryingComrade.store(static_cast<int>(comradeIndex), std::memory_order_release);
}

void CallComrade::ForgetComrade()
{
    g_HurryingComrade.store(kNobody, std::memory_order_release);
    g_WalkingComrade.store(kNobody, std::memory_order_release);
    StopTurn();
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
    if (!command)
        return;
    Hurry(soldierIndex, command);

    const Stage stage = CurrentStage();
    if ((stage != Stage::WaitingForNotice && stage != Stage::Turning) || soldierIndex != g_ComradeIndex)
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
        return SetOff(soldierIndex);

    const std::uint64_t elapsed = GameClock::NowMs() - g_StageStarted;
    switch (stage)
    {
    case Stage::WaitingForNotice:
    case Stage::Turning:
        SetStage(Stage::Holding);
        return true;

    case Stage::Holding:
        if (elapsed < kShortestPauseMs || (!IsUpperBodyFree(soldier, soldierIndex) && elapsed < kLongestPauseMs))
            return true;
        Answer();
        return true;

    case Stage::Replying:
        if (IsSaying(soldier, soldierIndex))
            g_ReplyHeard = true;
        else if (g_ReplyHeard || elapsed >= kReplyStartGraceMs)
        {
            SetStage(Stage::Idle);
            return SetOff(soldierIndex);
        }
        if (elapsed >= kLongestReplyMs)
        {
            SetStage(Stage::Idle);
            return SetOff(soldierIndex);
        }
        return true;

    default:
        return SetOff(soldierIndex);
    }
}

bool CallComrade::IsWalkingOver(std::uint32_t soldierIndex)
{
    if (g_WalkingComrade.load(std::memory_order_acquire) != static_cast<int>(soldierIndex))
        return false;
    if (GetTickCount64() < g_WalkUntil)
        return true;
    g_WalkingComrade.store(kNobody, std::memory_order_release);
    return false;
}

bool CallComrade::ShouldLeaveOnArrival(std::uint32_t soldierIndex)
{
    if (!IsWalkingOver(soldierIndex))
        return false;

    const std::uint64_t now = GameClock::NowMs();
    if (!g_ArrivedAt)
        g_ArrivedAt = now;
    if (now - g_ArrivedAt < kStayOnArrivalMs)
        return false;

    g_WalkingComrade.store(kNobody, std::memory_order_release);
    return true;
}
