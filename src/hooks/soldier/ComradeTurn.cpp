#include <pch.h>
#include "ComradeTurn.h"

#include <Windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "AiControllerImpl_WakeNode.h"
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
    constexpr std::uint64_t kLongestPauseMs          = 2500;
    constexpr std::uint64_t kReplyStartGraceMs       = 1000;
    constexpr std::uint64_t kLongestReplyMs          = 3000;
    constexpr std::uint64_t kLongestHurryMs          = 20000;
    constexpr std::uint64_t kNodeStartMs             = 15000;   // his NoticeNoiseAi node has this long to start
    constexpr std::uint64_t kLongestWalkOverMs       = 60000;   // only when nothing tells us his node ended
    constexpr std::uint64_t kStayOnArrivalMs         = 1000;
    constexpr std::uint64_t kNoDeadline              = 0;
    constexpr std::uint32_t kMostSoldiers            = 0x200;

    enum class Stage : int
    {
        Idle,
        WaitingForNotice,
        Turning,
        Holding,
        Replying,
    };

    struct CalledComrade
    {
        std::atomic<int>           stage{ static_cast<int>(Stage::Idle) };
        std::atomic<bool>          replyCue{ false };
        std::atomic<bool>          walking{ false };      // from the call until his NoticeNoiseAi node ends
        std::atomic<bool>          nodeAwake{ false };
        std::atomic<bool>          calm{ false };
        std::atomic<bool>          hurrying{ false };
        std::atomic<bool>          hurryArmed{ false };
        std::atomic<std::uint64_t> walkUntil{ kNoDeadline };
        std::uint64_t              stageStarted = 0;
        std::uint64_t              arrivedAt    = 0;
        std::uint64_t              hurryUntil   = 0;
        bool                       replyHeard   = false;
        bool                       hurryMoving  = false;
    };

    CalledComrade g_Comrades[kMostSoldiers];

    template <typename T>
    T& At(std::uint8_t* base, std::size_t offset)
    {
        return *reinterpret_cast<T*>(base + offset);
    }

    Stage StageOf(const CalledComrade& comrade)
    {
        return static_cast<Stage>(comrade.stage.load(std::memory_order_acquire));
    }

    void SetStage(CalledComrade& comrade, Stage stage)
    {
        comrade.stageStarted = GameClock::NowMs();
        comrade.stage.store(static_cast<int>(stage), std::memory_order_release);
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

    void Answer(CalledComrade& comrade)
    {
        comrade.replyHeard = false;
        comrade.replyCue.store(true, std::memory_order_release);
        SetStage(comrade, Stage::Replying);
    }

    void Hurry(CalledComrade& comrade, std::uint8_t* command)
    {
        if (!comrade.hurrying.load(std::memory_order_acquire))
            return;
        if (GetTickCount64() >= comrade.hurryUntil)
        {
            comrade.hurrying.store(false, std::memory_order_release);
            return;
        }
        if (!comrade.hurryArmed.load(std::memory_order_acquire))
            return;

        if (At<std::uint8_t>(command, kCommandKeyActionType) != kKeyActionMove)
        {
            if (comrade.hurryMoving)
                comrade.hurrying.store(false, std::memory_order_release);
            return;
        }

        std::uint32_t& flags = At<std::uint32_t>(command, kCommandMoveFlags);
        if ((flags & kMoveSpeedMask) == kMoveWalk)
            flags = (flags & ~kMoveSpeedMask) | kMoveRun;
        comrade.hurryMoving = true;
    }

    bool SetOff(CalledComrade& comrade)
    {
        if (comrade.hurrying.load(std::memory_order_acquire))
            comrade.hurryArmed.store(true, std::memory_order_release);
        return false;
    }
}

void CallComrade::TurnToCaller(std::uint32_t comradeIndex, bool calm)
{
    if (comradeIndex >= kMostSoldiers)
        return;

    CalledComrade& comrade = g_Comrades[comradeIndex];
    const std::uint64_t nodeDue = AiControllerImpl_WakeNode_IsWatching() ? kNodeStartMs : kLongestWalkOverMs;
    comrade.hurrying.store(false, std::memory_order_release);
    comrade.replyCue.store(false, std::memory_order_release);
    comrade.nodeAwake.store(false, std::memory_order_release);
    comrade.calm.store(calm, std::memory_order_release);
    comrade.arrivedAt = 0;
    comrade.walkUntil.store(GetTickCount64() + nodeDue, std::memory_order_release);
    comrade.walking.store(true, std::memory_order_release);
    SetStage(comrade, Stage::WaitingForNotice);
}

void CallComrade::HurryToCaller(std::uint32_t comradeIndex)
{
    if (comradeIndex >= kMostSoldiers)
        return;

    CalledComrade& comrade = g_Comrades[comradeIndex];
    comrade.hurryUntil  = GetTickCount64() + kLongestHurryMs;
    comrade.hurryMoving = false;
    comrade.hurryArmed.store(false, std::memory_order_release);
    comrade.hurrying.store(true, std::memory_order_release);
}

void CallComrade::ForgetComrade(std::uint32_t comradeIndex)
{
    if (comradeIndex >= kMostSoldiers)
        return;

    CalledComrade& comrade = g_Comrades[comradeIndex];
    comrade.hurrying.store(false, std::memory_order_release);
    comrade.walking.store(false, std::memory_order_release);
    comrade.nodeAwake.store(false, std::memory_order_release);
    comrade.calm.store(false, std::memory_order_release);
    StopTurn(comradeIndex);
}

void CallComrade::ForgetComrades()
{
    for (std::uint32_t index = 0; index < kMostSoldiers; ++index)
        ForgetComrade(index);
}

bool CallComrade::TakeReplyCue(std::uint32_t comradeIndex)
{
    return comradeIndex < kMostSoldiers && g_Comrades[comradeIndex].replyCue.exchange(false, std::memory_order_acq_rel);
}

bool CallComrade::HasTurned(std::uint32_t comradeIndex)
{
    if (comradeIndex >= kMostSoldiers)
        return false;

    const Stage stage = StageOf(g_Comrades[comradeIndex]);
    return stage == Stage::Turning || stage == Stage::Holding || stage == Stage::Replying;
}

void CallComrade::StopTurn(std::uint32_t comradeIndex)
{
    if (comradeIndex >= kMostSoldiers)
        return;

    CalledComrade& comrade = g_Comrades[comradeIndex];
    comrade.replyCue.store(false, std::memory_order_release);
    SetStage(comrade, Stage::Idle);
}

void CallComrade::OnSetCommand(std::uint32_t soldierIndex, std::uint8_t* command)
{
    if (!command || soldierIndex >= kMostSoldiers)
        return;

    CalledComrade& comrade = g_Comrades[soldierIndex];
    Hurry(comrade, command);

    const Stage stage = StageOf(comrade);
    if (stage != Stage::WaitingForNotice && stage != Stage::Turning)
        return;
    if (At<std::uint8_t>(command, kCommandKeyActionType) != kKeyActionNotice)
        return;

    At<std::uint32_t>(command, kCommandNoticeKind) = kNoticeTurnToTarget;
    At<float>(command, kCommandNoticeParam)        = kTurnParam;
    if (stage != Stage::Turning)
        SetStage(comrade, Stage::Turning);
}

bool CallComrade::ShouldHoldMovePause(std::uint8_t* soldier, std::uint32_t soldierIndex)
{
    if (soldierIndex >= kMostSoldiers)
        return false;

    CalledComrade& comrade = g_Comrades[soldierIndex];
    const Stage stage = StageOf(comrade);
    if (stage == Stage::Idle || !soldier)
        return SetOff(comrade);

    const std::uint64_t elapsed = GameClock::NowMs() - comrade.stageStarted;
    switch (stage)
    {
    case Stage::WaitingForNotice:
    case Stage::Turning:
        SetStage(comrade, Stage::Holding);
        return true;

    case Stage::Holding:
        if (elapsed < kShortestPauseMs || (!IsUpperBodyFree(soldier, soldierIndex) && elapsed < kLongestPauseMs))
            return true;
        Answer(comrade);
        return true;

    case Stage::Replying:
        if (IsSaying(soldier, soldierIndex))
            comrade.replyHeard = true;
        else if (comrade.replyHeard || elapsed >= kReplyStartGraceMs)
        {
            SetStage(comrade, Stage::Idle);
            return SetOff(comrade);
        }
        if (elapsed >= kLongestReplyMs)
        {
            SetStage(comrade, Stage::Idle);
            return SetOff(comrade);
        }
        return true;

    default:
        return SetOff(comrade);
    }
}

bool CallComrade::IsWalkingOver(std::uint32_t soldierIndex)
{
    if (soldierIndex >= kMostSoldiers)
        return false;

    CalledComrade& comrade = g_Comrades[soldierIndex];
    if (!comrade.walking.load(std::memory_order_acquire))
        return false;

    const std::uint64_t until = comrade.walkUntil.load(std::memory_order_acquire);
    if (until == kNoDeadline || GetTickCount64() < until)
        return true;
    ForgetComrade(soldierIndex);
    return false;
}

bool CallComrade::ShouldLeaveOnArrival(std::uint32_t soldierIndex)
{
    if (!IsWalkingOver(soldierIndex))
        return false;

    CalledComrade& comrade = g_Comrades[soldierIndex];
    const std::uint64_t now = GameClock::NowMs();
    if (!comrade.arrivedAt)
        comrade.arrivedAt = now;
    if (now - comrade.arrivedAt < kStayOnArrivalMs)
        return false;

    ForgetComrade(soldierIndex);
    return true;
}

bool CallComrade::ComesCalm(std::uint32_t soldierIndex)
{
    return IsWalkingOver(soldierIndex) && g_Comrades[soldierIndex].calm.load(std::memory_order_acquire);
}

void CallComrade::OnNoiseNode(std::uint32_t soldierIndex, bool awake)
{
    if (!IsWalkingOver(soldierIndex))
        return;

    CalledComrade& comrade = g_Comrades[soldierIndex];
    if (awake)
    {
        comrade.nodeAwake.store(true, std::memory_order_release);
        comrade.walkUntil.store(kNoDeadline, std::memory_order_release);
    }
    else if (comrade.nodeAwake.load(std::memory_order_acquire))
    {
        ForgetComrade(soldierIndex);
    }
}
