#include <pch.h>

#include <Windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

#include "AddressSet.h"
#include "CpRadioVariants.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "PlayerAlertPhase.h"
#include "RadioControllerImpl_StepRadioWaitResponse.h"
#include "SoldierCombatVoice.h"
#include "SoldierRealizedSlot.h"
#include "SoldierVoiceType.h"
#include "log.h"

namespace
{
    using Speak_t             = bool(__fastcall*)(void* soundController, std::uint32_t slot, std::uint32_t dialogueEvent,
                                                  int priority, std::uint32_t line, float delay);
    using GetPlayerPosition_t = void(__fastcall*)(void* playerInfo, float* outPosition);
    using GetCpPhase_t        = int(__fastcall*)(void* controller, std::uint32_t groupId, std::uint32_t soldierIndex);
    using IsPlayingVoice_t    = bool(__fastcall*)(void* soundController, std::uint32_t soldierIndex);
    using GetQuarkSystemTable_t = std::uint8_t*(__fastcall*)();
    using GetCommunication_t  = const std::int32_t*(__fastcall*)(void* vehicles, std::int64_t* result, const std::uint8_t** info,
                                                                 std::uint32_t vehicleIndex);
    using GetVehicleParameter_t = const std::uint8_t*(__fastcall*)(void* vehicles, std::uint32_t parameterIndex);

    constexpr std::uint32_t kEnemyVoiceEvent  = 0x95EA16B0u;  // DD_vox_ene
    constexpr std::uint32_t kAskOkLine        = 0xA79B6AC6u;  // EVC340 "You OK?"
    constexpr std::uint32_t kImOkLine         = 0xAA9B6F9Fu;  // EVC330 "I'm all right!"
    constexpr std::uint32_t kReloadLine       = 0x28986442u;  // EVC050 "Reload!"
    constexpr std::uint32_t kReadyLine        = 0x2C986A8Eu;  // EVC090 "Ready!"
    constexpr std::uint32_t kWatchOutLine     = 0xB19DB97Bu;  // EVC200 "Watch out!"
    constexpr std::uint32_t kGetDownLine      = 0x289625EBu;  // EVC190 "Get down!"
    constexpr std::uint32_t kAheadLine        = 0x21961AE6u;  // EVC120 "Dead ahead!"
    constexpr std::uint32_t kRearLine         = 0x22961C59u;  // EVC130 "To the rear!"
    constexpr std::uint32_t kRightLine        = 0x23961DCCu;  // EVC140 "Right!"
    constexpr std::uint32_t kLeftLine         = 0x24961FBFu;  // EVC150 "Left!"
    constexpr std::uint32_t kUpLine           = 0x25962112u;  // EVC160 "Eyes up!"
    constexpr std::uint32_t kDownLine         = 0x26962285u;  // EVC170 "Eyes down!"
    constexpr std::uint32_t kCheckingLine     = 0x84C7C665u;  // EVE020 "Checking it!"
    constexpr std::uint32_t kGotItLine        = 0x83C7C4F2u;  // EVE030 "Got it!"
    constexpr std::uint32_t kFallInLine       = 0x80C7C039u;  // EVE060 "Fall in!"
    constexpr std::uint32_t kTankLine         = 0x0CCD8752u;  // EVR084 "Tank!"
    constexpr std::uint32_t kArmoredLine      = 0x0CCD8753u;  // EVR085 "Armored vehicle!"
    constexpr std::uint32_t kContactLine      = 0x937AC414u;  // EVN150 "Contact!" on spotting Snake
    constexpr std::uint32_t kCombatContactLine = 0x20961973u; // EVC110 "Contact!" in combat
    constexpr std::uint32_t kChildVoiceEvent  = 0x68B311E1u;  // the child soldiers' event in vox_child
    constexpr std::uint32_t kRelaxLine        = 0xF8531A05u;  // EVS040 "Phew"
    constexpr std::uint32_t kChildIdleLines[4] = { 0xF3531266u, 0xF25310F3u, 0xF1530F00u, 0xFC5320B1u };  // EVS010 breathing, EVS020 yawn,
                                                                                                             // EVS030 sneeze, EVS080 cough
    constexpr std::uint32_t kClockLines[12]   = { 0xBDA2496Cu, 0xBDA2496Fu, 0xBDA2496Eu, 0xBDA24969u,    // EVC401..EVC412, one to
                                                  0xBDA24968u, 0xBDA2496Bu, 0xBDA2496Au, 0xBDA24965u,    // twelve o'clock, as
                                                  0xBDA24964u, 0xBCA247FAu, 0xBCA247FBu, 0xBCA247F8u };  // ConvertDirectionVoice picks them

    constexpr int           kCombatPriority   = 3;
    constexpr int           kPhasePriority    = 2;
    constexpr float         kNoDelay          = 0.0f;
    constexpr std::uint64_t kImOkAfterMs      = 1800;
    constexpr std::uint64_t kReadyAfterMs     = 2600;   // a rifle reload, about
    constexpr std::uint64_t kAfterRadioMs     = 2500;   // the CP's own radio call goes first
    constexpr std::uint64_t kGotItAfterMs     = 1800;
    constexpr std::uint64_t kCheckingAfterSentMs  = 800;    // the CP sends its search team as the CPR0051 call ends
    constexpr std::uint64_t kFollowUpLateMs   = 3000;   // dropped when the soldier hasn't ticked by then
    constexpr std::uint64_t kQuietLateMs      = 25000;  // the same for a line that waits until he stops talking, a whole radio call
    constexpr std::uint64_t kHurtRecentlyMs   = 30000;
    constexpr std::uint64_t kPhaseLineGapMs   = 20000;  // per CP
    constexpr std::uint64_t kPhaseCheckMs     = 300;    // per soldier
    constexpr float         kAskOkReach       = 20.0f;
    constexpr float         kComradeNearSearch = 45.0f;     // vanilla's rule for a searcher's opening line
    constexpr float         kSnakeNearSearch   = 90.0f;
    constexpr float         kHearingReach     = 60.0f;
    constexpr float         kPrisonerOrderReach = 10.0f;
    constexpr float         kSnakeAtVehicle   = 4.0f;   // he is the one putting someone in
    constexpr float         kAboveOrBelow     = 5.0f;
    constexpr int           kPhaseSneak       = 0;
    constexpr int           kPhaseCaution     = 1;
    constexpr int           kPhaseEvasion     = 2;
    constexpr int           kPhaseAlert       = 3;
    constexpr int           kPhaseUnknown     = -1;
    constexpr std::uint32_t kNoSoldier        = 0xFFFFFFFFu;
    constexpr std::uint32_t kSoldierSlotCount = 0x200;
    constexpr std::size_t   kCommandPostCount = 0x100;
    constexpr std::size_t   kMaxFollowUps     = 16;

    constexpr std::size_t   kControllerSoldiers      = 0x58;
    constexpr std::size_t   kSoldiersCount           = 0x08;
    constexpr std::size_t   kSoldiersTransforms      = 0x18;
    constexpr std::size_t   kTransformsActiveBits    = 0x08;
    constexpr std::size_t   kTransformsPositions     = 0x20;  // xyz per soldier, 0x10 apart
    constexpr std::size_t   kPositionStride          = 0x10;
    constexpr std::size_t   kSoldiersLife            = 0x38;  // 0x40 per soldier at +8
    constexpr std::size_t   kLifeRecords             = 0x08;
    constexpr std::size_t   kLifeRecordSize          = 0x40;
    constexpr std::size_t   kLifeFlags               = 0x12;
    constexpr std::uint16_t kLifeFlagsStatus         = 0x3;   // 0 normal, 1 sleep, 2 faint, 3 dying
    constexpr std::size_t   kLifeState               = 0x3E;
    constexpr std::uint8_t  kLifeStateDead           = 2;
    constexpr std::size_t   kSoldiersSoundController = 0xA8;
    constexpr std::size_t   kSoldiersCommandPost     = 0xD0;
    constexpr std::size_t   kCommandPostController   = 0x08;
    constexpr std::size_t   kCommandPostGroupId      = 0x30;
    constexpr std::size_t   kSoldiersPlayerInfo      = 0x188;
    constexpr std::size_t   kSpeakSlot               = 0x20 / sizeof(void*);
    constexpr std::size_t   kIsPlayingVoiceSlot      = 0x58 / sizeof(void*);   // takes the soldier index
    constexpr std::size_t   kGetPlayerPositionSlot   = 0x18 / sizeof(void*);
    constexpr std::size_t   kGetCpPhaseSlot          = 0x188 / sizeof(void*);

    constexpr std::size_t   kQuarkApplication          = 0x98;
    constexpr std::size_t   kApplicationScriptVars     = 0x10;
    constexpr std::size_t   kApplicationVehicles       = 0x2A8;
    constexpr std::size_t   kScriptVarsPlayerVehicle   = 0x110;  // vars.playerVehicleGameObjectId
    constexpr std::size_t   kGetCommunicationSlot      = 0x18 / sizeof(void*);
    constexpr std::size_t   kGetVehicleParameterSlot   = 0x168 / sizeof(void*);
    constexpr std::size_t   kCommunicationVehicleType  = 0x6C;   // high nibble, as VehicleCommunicationInfo::GetVehicleType reads it
    constexpr std::size_t   kParameterVehicleType      = 0x1B6;
    constexpr std::uint16_t kVehicleObjectType         = 0x32;   // TppVehicle2
    constexpr std::uint16_t kTypeShift                 = 9;
    constexpr std::uint16_t kIndexMask                 = 0x1FF;
    constexpr std::uint8_t  kWesternArmoredVehicle     = 5;      // Vehicle.type order
    constexpr std::uint8_t  kEasternArmoredVehicle     = 6;
    constexpr std::uint8_t  kWesternTank               = 7;
    constexpr std::uint8_t  kEasternTank               = 8;

    struct FollowUp
    {
        std::uint32_t soldierIndex  = kNoSoldier;
        std::uint32_t line          = 0;
        int           priority      = 0;
        std::uint64_t dueMs         = 0;
        std::uint32_t dialogueEvent = kEnemyVoiceEvent;
        std::uint64_t quietMs       = 0;       // waits until he has been quiet this long, 0 for no wait
        std::uint32_t replier       = kNoSoldier;   // says EVE030 once this line has played
    };

    std::mutex                                                g_Mutex;
    std::array<FollowUp, kMaxFollowUps>                       g_FollowUps{};
    std::array<int, kCommandPostCount>                        g_CpPhase{};
    std::array<std::uint64_t, kCommandPostCount>              g_CpSpokeMs{};
    std::atomic<const std::uint8_t*>                          g_Soldiers{ nullptr };
    std::array<std::atomic<std::uint64_t>, kSoldierSlotCount> g_HurtMs{};
    std::array<std::atomic<std::uint64_t>, kSoldierSlotCount> g_PhaseCheckedMs{};
    std::array<std::atomic<std::uint64_t>, kSoldierSlotCount> g_TalkedMs{};
    std::atomic<std::uint32_t>                                g_LineRoll{ 0x2545F491u };
#ifdef _DEBUG
    constexpr std::uint64_t kProbeWindowMs = 6000;   // TEMP probe: what cuts EVE020/EVE030 off
    std::array<std::atomic<std::uint32_t>, kSoldierSlotCount> g_ProbeLine{};
    std::array<std::atomic<std::uint64_t>, kSoldierSlotCount> g_ProbeStartMs{};
    std::array<std::atomic<bool>, kSoldierSlotCount>          g_ProbeHeard{};
#endif

    bool CoinFlip()
    {
        std::uint32_t roll = g_LineRoll.load(std::memory_order_relaxed);
        roll ^= roll << 13;
        roll ^= roll >> 7;
        roll ^= roll << 5;
        g_LineRoll.store(roll, std::memory_order_relaxed);
        return (roll & 1) != 0;
    }

    bool PositionOf(const std::uint8_t* soldiers, std::uint32_t soldierIndex, float* out)
    {
        __try
        {
            const std::uint8_t* transforms = *reinterpret_cast<const std::uint8_t* const*>(soldiers + kSoldiersTransforms);
            const float* at = reinterpret_cast<const float*>(*reinterpret_cast<const std::uint8_t* const*>(transforms + kTransformsPositions)
                + soldierIndex * kPositionStride);
            out[0] = at[0];
            out[1] = at[1];
            out[2] = at[2];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool SnakePosition(const std::uint8_t* soldiers, float* out)
    {
        alignas(16) float snake[4] = {};
        __try
        {
            void* playerInfo = *reinterpret_cast<void* const*>(soldiers + kSoldiersPlayerInfo);
            const auto getPlayerPosition = (*reinterpret_cast<GetPlayerPosition_t* const*>(playerInfo))[kGetPlayerPositionSlot];
            getPlayerPosition(playerInfo, snake);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        out[0] = snake[0];
        out[1] = snake[1];
        out[2] = snake[2];
        return true;
    }

    float SquaredDistance(const float* a, const float* b)
    {
        const float dx = a[0] - b[0];
        const float dy = a[1] - b[1];
        const float dz = a[2] - b[2];
        return dx * dx + dy * dy + dz * dz;
    }

    std::uint32_t SoldierCount(const std::uint8_t* soldiers)
    {
        __try
        {
            return *reinterpret_cast<const std::uint32_t*>(soldiers + kSoldiersCount);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    bool IsActive(const std::uint8_t* soldiers, std::uint32_t soldierIndex)
    {
        __try
        {
            const std::uint8_t* transforms = *reinterpret_cast<const std::uint8_t* const*>(soldiers + kSoldiersTransforms);
            const std::uint8_t* active = *reinterpret_cast<const std::uint8_t* const*>(transforms + kTransformsActiveBits);
            return (active[soldierIndex >> 3] & (1u << (soldierIndex & 7))) != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
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

    bool CanSpeak(const std::uint8_t* soldiers, std::uint32_t soldierIndex)
    {
        bool heldKnown = false;
        return IsActive(soldiers, soldierIndex) && IsAwake(soldiers, soldierIndex)
            && !CpRadioResponse::IsHeld(static_cast<std::uint16_t>(soldierIndex), heldKnown)
            && SoldierRealizedSlot::Of(soldiers, soldierIndex) != SoldierRealizedSlot::kNotRealized
            && SoldierVoiceType::IsAdult(SoldierVoiceType::Of(soldiers, soldierIndex));
    }

    template <typename Filter>
    std::uint32_t Nearest(const std::uint8_t* soldiers, const float* from, float reach, Filter filter)
    {
        std::uint32_t nearest = kNoSoldier;
        float nearestDistance = reach * reach;
        const std::uint32_t count = SoldierCount(soldiers);
        for (std::uint32_t index = 0; index < count && index < kSoldierSlotCount; ++index)
        {
            float at[3] = {};
            if (!PositionOf(soldiers, index, at))
                continue;
            const float distance = SquaredDistance(from, at);
            if (distance >= nearestDistance || !filter(index) || !CanSpeak(soldiers, index))
                continue;
            nearest = index;
            nearestDistance = distance;
        }
        return nearest;
    }

    bool HasComradeWithin(const std::uint8_t* soldiers, std::uint32_t soldierIndex, const float* at, float reach)
    {
        const std::uint32_t count = SoldierCount(soldiers);
        for (std::uint32_t index = 0; index < count && index < kSoldierSlotCount; ++index)
        {
            float other[3] = {};
            if (index != soldierIndex && IsActive(soldiers, index) && IsAwake(soldiers, index) && PositionOf(soldiers, index, other)
                && SquaredDistance(at, other) < reach * reach)
                return true;
        }
        return false;
    }

    void Schedule(std::uint32_t soldierIndex, std::uint32_t line, int priority, std::uint64_t dueMs,
                  std::uint32_t dialogueEvent = kEnemyVoiceEvent, std::uint64_t quietMs = 0, std::uint32_t replier = kNoSoldier)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        const std::uint64_t now = GetTickCount64();
        FollowUp* free = nullptr;
        FollowUp* expired = nullptr;   // his soldier never ticked again (fultoned, unrealized), so nothing frees it
        for (FollowUp& followUp : g_FollowUps)
        {
            if (followUp.soldierIndex == soldierIndex && followUp.line == line && followUp.dialogueEvent == dialogueEvent)
            {
                free = &followUp;
                break;
            }
            if (!free && followUp.soldierIndex == kNoSoldier)
                free = &followUp;
            else if (!expired && followUp.soldierIndex != kNoSoldier && now > followUp.dueMs
                     && now - followUp.dueMs > (followUp.quietMs ? kQuietLateMs : kFollowUpLateMs))
                expired = &followUp;
        }
        if (!free)
            free = expired;
        if (free)
            *free = FollowUp{ soldierIndex, line, priority, dueMs, dialogueEvent, quietMs, replier };
    }

    bool Speak(const std::uint8_t* soldiers, std::uint32_t soldierIndex, std::uint32_t line, int priority,
               std::uint32_t dialogueEvent = kEnemyVoiceEvent)
    {
        const std::uint32_t slot = SoldierRealizedSlot::Of(soldiers, soldierIndex);
        if (slot == SoldierRealizedSlot::kNotRealized)
            return false;
        __try
        {
            void* soundController = *reinterpret_cast<void* const*>(soldiers + kSoldiersSoundController);
            const auto speak = (*reinterpret_cast<Speak_t* const*>(soundController))[kSpeakSlot];
            return speak(soundController, slot, dialogueEvent, priority, line, kNoDelay);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool IsTalking(const std::uint8_t* soldiers, std::uint32_t soldierIndex)
    {
        __try
        {
            void* soundController = *reinterpret_cast<void* const*>(soldiers + kSoldiersSoundController);
            const auto isPlaying = (*reinterpret_cast<IsPlayingVoice_t* const*>(soundController))[kIsPlayingVoiceSlot];
            return isPlaying(soundController, soldierIndex);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool WaitsForQuiet(std::uint32_t soldierIndex)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const FollowUp& followUp : g_FollowUps)
        {
            if (followUp.soldierIndex == soldierIndex && followUp.quietMs)
                return true;
        }
        return false;
    }

#ifdef _DEBUG
    const char* ProbeName(std::uint32_t line)
    {
        return line == kCheckingLine ? "EVE020" : "EVE030";
    }

    void ProbeWatch(std::uint32_t soldierIndex, std::uint32_t line, bool spoke, std::uint64_t now)
    {
        if (line != kCheckingLine && line != kGotItLine)
            return;
        LogDebug("VoiceProbe: soldier %u %s %s", soldierIndex, spoke ? "starts" : "was refused", ProbeName(line));
        if (!spoke)
            return;
        g_ProbeHeard[soldierIndex].store(false, std::memory_order_relaxed);
        g_ProbeStartMs[soldierIndex].store(now, std::memory_order_relaxed);
        g_ProbeLine[soldierIndex].store(line, std::memory_order_release);
    }

    void ProbeTick(const std::uint8_t* soldiers, std::uint32_t soldierIndex, std::uint64_t now)
    {
        const std::uint32_t line = g_ProbeLine[soldierIndex].load(std::memory_order_acquire);
        if (!line)
            return;
        const std::uint64_t elapsed = now - g_ProbeStartMs[soldierIndex].load(std::memory_order_relaxed);
        const bool talking = IsTalking(soldiers, soldierIndex);
        if (talking)
            g_ProbeHeard[soldierIndex].store(true, std::memory_order_relaxed);
        const bool heard = g_ProbeHeard[soldierIndex].load(std::memory_order_relaxed);
        if (elapsed <= kProbeWindowMs && (talking || !heard))
            return;
        LogDebug("VoiceProbe: soldier %u %s %s after %llu ms", soldierIndex, ProbeName(line), heard ? "ended" : "never played", elapsed);
        g_ProbeLine[soldierIndex].store(0, std::memory_order_relaxed);
    }
#endif

    void SpeakDueFollowUp(const std::uint8_t* soldiers, std::uint32_t soldierIndex, std::uint64_t now)
    {
        if (IsTalking(soldiers, soldierIndex))
            g_TalkedMs[soldierIndex].store(now, std::memory_order_relaxed);
        if (WaitsForQuiet(soldierIndex))
        {
            const std::uint64_t onRadio = CpRadioVariants_RadioHeardMs(soldierIndex);   // his radio call plays on the CP's channel, not his own
            if (onRadio > g_TalkedMs[soldierIndex].load(std::memory_order_relaxed))
                g_TalkedMs[soldierIndex].store(onRadio < now ? onRadio : now, std::memory_order_relaxed);
        }
        const std::uint64_t quietFor = now - g_TalkedMs[soldierIndex].load(std::memory_order_relaxed);

        FollowUp due;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            std::uint64_t heldDueMs = ~0ull;
            for (const FollowUp& followUp : g_FollowUps)
            {
                if (followUp.soldierIndex == soldierIndex && now >= followUp.dueMs && quietFor < followUp.quietMs
                    && now - followUp.dueMs <= kQuietLateMs && followUp.dueMs < heldDueMs)
                    heldDueMs = followUp.dueMs;
            }
            FollowUp* earliest = nullptr;
            for (FollowUp& followUp : g_FollowUps)
            {
                if (followUp.soldierIndex != soldierIndex || now < followUp.dueMs)
                    continue;
                if (quietFor < followUp.quietMs && now - followUp.dueMs <= kQuietLateMs)
                    continue;
                if (followUp.quietMs && followUp.dueMs > heldDueMs)
                    continue;
                if (!earliest || followUp.dueMs < earliest->dueMs)
                    earliest = &followUp;
            }
            if (earliest)
            {
                due = *earliest;
                *earliest = FollowUp{};
            }
        }
        if (due.soldierIndex == kNoSoldier)
            return;
        if (now - due.dueMs > (due.quietMs ? kQuietLateMs : kFollowUpLateMs) || !CanSpeak(soldiers, soldierIndex))
        {
#ifdef _DEBUG
            if (due.line == kCheckingLine || due.line == kGotItLine)
                LogDebug("VoiceProbe: soldier %u dropped %s: late or can't speak", soldierIndex, ProbeName(due.line));
#endif
            return;
        }
        if (due.line == kReadyLine && !SoldierCombatVoice_InAlert(soldiers, soldierIndex))
            return;
        const bool spoke = Speak(soldiers, soldierIndex, due.line, due.priority, due.dialogueEvent);
#ifdef _DEBUG
        ProbeWatch(soldierIndex, due.line, spoke, now);
#endif
        if (!spoke)
            return;
        g_TalkedMs[soldierIndex].store(now, std::memory_order_relaxed);
        if (due.replier != kNoSoldier)
            Schedule(due.replier, kGotItLine, kCombatPriority, now + kGotItAfterMs);
    }

    void SayPhaseLine(const std::uint8_t* soldiers, std::uint32_t line, std::uint64_t now)
    {
        float snake[3] = {};
        if (!SnakePosition(soldiers, snake))
            return;
        const std::uint32_t speaker = Nearest(soldiers, snake, kHearingReach, [](std::uint32_t) { return true; });
        if (speaker == kNoSoldier)
            return;
        Schedule(speaker, line, kPhasePriority, now + kAfterRadioMs);
    }

    std::uint32_t PhaseLine(int previous, int phase)
    {
        if (previous == kPhaseEvasion && phase == kPhaseCaution)
            return kFallInLine;     // the search teams form up
        return 0;   // user: no "Clear!" back to sneak
    }

    void TrackCommandPostPhase(const std::uint8_t* soldiers, std::uint32_t soldierIndex, std::uint64_t now)
    {
        if (now - g_PhaseCheckedMs[soldierIndex].load(std::memory_order_relaxed) < kPhaseCheckMs)
            return;
        g_PhaseCheckedMs[soldierIndex].store(now, std::memory_order_relaxed);

        const std::uint32_t cp = 0;   // one slot: the game's own player phase
        const int phase = PlayerAlertPhase::Read();   // vars.playerPhase; soldier and CP phase reads flicker to 0 in alert
        if (phase < kPhaseSneak || phase > kPhaseAlert)
            return;

        std::uint32_t line = 0;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            const int previous = g_CpPhase[cp];
            g_CpPhase[cp] = phase;
            if (previous == kPhaseUnknown || previous == phase)
                return;
            line = PhaseLine(previous, phase);
            if (line == 0 || now - g_CpSpokeMs[cp] < kPhaseLineGapMs)
                return;
            g_CpSpokeMs[cp] = now;
        }
        SayPhaseLine(soldiers, line, now);
    }

    std::uint32_t PlayerVehicleLine()   // "Tank!" or "Armored vehicle!" for what Snake rides, 0 for anything else
    {
        const auto quarkTable = reinterpret_cast<GetQuarkSystemTable_t>(ResolveGameAddress(gAddr.Fox_GetQuarkSystemTable));
        if (!quarkTable)
            return 0;
        __try
        {
            const std::uint8_t* application = *reinterpret_cast<const std::uint8_t* const*>(quarkTable() + kQuarkApplication);
            const std::uint8_t* scriptVars = *reinterpret_cast<const std::uint8_t* const*>(application + kApplicationScriptVars);
            const std::uint16_t vehicle = *reinterpret_cast<const std::uint16_t*>(scriptVars + kScriptVarsPlayerVehicle);
            void* vehicles = *reinterpret_cast<void* const*>(application + kApplicationVehicles);
            if ((vehicle >> kTypeShift) != kVehicleObjectType || !vehicles)
                return 0;

            std::int64_t result = -1;
            const std::uint8_t* info = nullptr;
            const auto getCommunication = (*reinterpret_cast<GetCommunication_t* const*>(vehicles))[kGetCommunicationSlot];
            const std::int32_t* found = getCommunication(vehicles, &result, &info, vehicle & kIndexMask);
            if (!found || *found < 0 || !info)
                return 0;

            const std::uint8_t parameterIndex = info[kCommunicationVehicleType] >> 4;
            const auto getParameter = (*reinterpret_cast<GetVehicleParameter_t* const*>(vehicles))[kGetVehicleParameterSlot];
            const std::uint8_t* parameter = getParameter(vehicles, parameterIndex);
            const std::uint8_t type = parameter ? parameter[kParameterVehicleType] : parameterIndex;
            if (type == kWesternTank || type == kEasternTank)
                return kTankLine;
            if (type == kWesternArmoredVehicle || type == kEasternArmoredVehicle)
                return kArmoredLine;
            return 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    void ForgetMission(const std::uint8_t* soldiers)
    {
        if (soldiers == g_Soldiers.load(std::memory_order_acquire))
            return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        if (soldiers == g_Soldiers.load(std::memory_order_relaxed))
            return;
        g_Soldiers.store(soldiers, std::memory_order_release);
        g_FollowUps.fill(FollowUp{});
        g_CpPhase.fill(kPhaseUnknown);
        g_CpSpokeMs.fill(0);
    }

    std::uint32_t DirectionLine(const std::uint8_t* soldiers, std::uint32_t soldierIndex, int hour, std::uint32_t line)
    {
        float soldier[3] = {};
        float snake[3] = {};
        if (PositionOf(soldiers, soldierIndex, soldier) && SnakePosition(soldiers, snake))
        {
            const float above = snake[1] - soldier[1];
            if (above >= kAboveOrBelow)
                return kUpLine;
            if (above <= -kAboveOrBelow)
                return kDownLine;
        }
        if (!CoinFlip())
            return line;
        switch (hour)
        {
        case 11: case 12: case 1: return kAheadLine;
        case 5: case 6: case 7:   return kRearLine;
        case 2: case 3: case 4:   return kRightLine;
        case 8: case 9: case 10:  return kLeftLine;
        default:                  return line;
        }
    }

    int HourOf(std::uint32_t line)
    {
        for (int i = 0; i < 12; ++i)
        {
            if (kClockLines[i] == line)
                return i + 1;
        }
        return 0;
    }

    bool HasEscortLines(std::uint32_t voiceType)
    {
        switch (voiceType)
        {
        case SoldierVoiceType::kEneARu: case SoldierVoiceType::kEneBRu: case SoldierVoiceType::kEneCRu: case SoldierVoiceType::kEneDRu:
        case SoldierVoiceType::kEneAAf: case SoldierVoiceType::kEneBAf: case SoldierVoiceType::kEneCAf: case SoldierVoiceType::kEneDAf:
            return true;
        default:
            return false;
        }
    }
}

namespace
{
    std::uint32_t SpeakerOf(const std::uint8_t* soldiers, std::uint32_t slot, std::uint32_t dialogueEvent)
    {
        if (dialogueEvent != kEnemyVoiceEvent || !soldiers || MissionCodeGuard::ShouldBypassHooks())
            return kNoSoldier;
        const std::uint32_t soldierIndex = SoldierRealizedSlot::SoldierIn(soldiers, slot);
        return soldierIndex < kSoldierSlotCount ? soldierIndex : kNoSoldier;
    }
}

std::uint32_t SoldierCombatVoice_SwapLine(const std::uint8_t* soldiers, std::uint32_t slot, std::uint32_t dialogueEvent, std::uint32_t line)
{
    if (line == kContactLine || line == kCombatContactLine)
    {
        const std::uint32_t soldierIndex = SpeakerOf(soldiers, slot, dialogueEvent);
        if (soldierIndex == kNoSoldier || !SoldierVoiceType::IsAdult(SoldierVoiceType::Of(soldiers, soldierIndex)))   // the child voices have no tank lines
            return line;
        const std::uint32_t vehicleLine = PlayerVehicleLine();
        if (vehicleLine == 0)
            return line;
        return vehicleLine;
    }
    if (line == kRelaxLine && (dialogueEvent == kChildVoiceEvent || dialogueEvent == kEnemyVoiceEvent) && soldiers
        && !MissionCodeGuard::ShouldBypassHooks())
    {
        const std::uint32_t soldierIndex = SoldierRealizedSlot::SoldierIn(soldiers, slot);
        const std::uint32_t voice = soldierIndex < kSoldierSlotCount ? SoldierVoiceType::Of(soldiers, soldierIndex) : SoldierVoiceType::kUnknown;
        const bool child = voice == SoldierVoiceType::kChildA || voice == SoldierVoiceType::kChildB
                        || voice == SoldierVoiceType::kChildC || voice == SoldierVoiceType::kChildD;
        if (!child || !CoinFlip())
            return line;
        const std::uint32_t swapped = kChildIdleLines[(CoinFlip() ? 2 : 0) + (CoinFlip() ? 1 : 0)];
        return swapped;
    }
    if (line != kWatchOutLine && HourOf(line) == 0)
        return line;
    const std::uint32_t soldierIndex = SpeakerOf(soldiers, slot, dialogueEvent);
    if (soldierIndex == kNoSoldier || !SoldierVoiceType::IsAdult(SoldierVoiceType::Of(soldiers, soldierIndex)))
        return line;

    if (line == kWatchOutLine)
    {
        const std::uint32_t swapped = CoinFlip() ? kGetDownLine : line;
        return swapped;
    }
    const int hour = HourOf(line);
    const std::uint32_t swapped = DirectionLine(soldiers, soldierIndex, hour, line);
    return swapped;
}

void SoldierCombatVoice_OnSaid(const std::uint8_t* soldiers, std::uint32_t slot, std::uint32_t dialogueEvent, std::uint32_t line)
{
    if (line != kAskOkLine && line != kReloadLine)
        return;
    const std::uint32_t soldierIndex = SpeakerOf(soldiers, slot, dialogueEvent);
    if (soldierIndex == kNoSoldier)
        return;
    const std::uint64_t now = GetTickCount64();

    if (line == kReloadLine)
    {
        if (SoldierVoiceType::IsAdult(SoldierVoiceType::Of(soldiers, soldierIndex)))
            Schedule(soldierIndex, kReadyLine, kCombatPriority, now + kReadyAfterMs);
        return;
    }

    float at[3] = {};
    if (!PositionOf(soldiers, soldierIndex, at))
        return;
    const std::uint32_t hurt = Nearest(soldiers, at, kAskOkReach, [&](std::uint32_t index) {
        return index != soldierIndex && now - g_HurtMs[index].load(std::memory_order_relaxed) < kHurtRecentlyMs; });
    if (hurt != kNoSoldier)
        Schedule(hurt, kImOkLine, kCombatPriority, now + kImOkAfterMs);
}

void SoldierCombatVoice_OnSightCheck(void* noticeController, std::uint32_t soldierIndex)
{
    if (!noticeController || soldierIndex >= kSoldierSlotCount || MissionCodeGuard::ShouldBypassHooks())
        return;
    const std::uint8_t* soldiers = nullptr;
    __try
    {
        soldiers = *reinterpret_cast<const std::uint8_t* const*>(static_cast<const std::uint8_t*>(noticeController) + kControllerSoldiers);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return;
    }
    if (!soldiers)
        return;

    ForgetMission(soldiers);
    const std::uint64_t now = GetTickCount64();
    TrackCommandPostPhase(soldiers, soldierIndex, now);
    SpeakDueFollowUp(soldiers, soldierIndex, now);
#ifdef _DEBUG
    ProbeTick(soldiers, soldierIndex, now);
#endif
}

bool SoldierCombatVoice_InAlert(const std::uint8_t* soldiers, std::uint32_t soldierIndex)
{
    if (!soldiers || soldierIndex >= kSoldierSlotCount)
        return false;
    const int phase = PlayerAlertPhase::Read();
    if (phase >= kPhaseSneak)
        return phase == kPhaseAlert;
    return CommandPostPhase(soldiers, soldierIndex) == kPhaseAlert;
}

void SoldierCombatVoice_SayLater(std::uint32_t soldierIndex, std::uint32_t dialogueEvent, std::uint32_t line, int priority,
                                 std::uint64_t afterMs, std::uint64_t quietMs)
{
    if (soldierIndex >= kSoldierSlotCount)
        return;
    const std::uint64_t now = GetTickCount64();
    if (quietMs)
    {
        const std::uint64_t onRadio = CpRadioVariants_RadioHeardMs(soldierIndex);   // the quiet counts from his last radio line, his "Over!!"
        if (onRadio > g_TalkedMs[soldierIndex].load(std::memory_order_relaxed))
            g_TalkedMs[soldierIndex].store(onRadio < now ? onRadio : now, std::memory_order_relaxed);
    }
    Schedule(soldierIndex, line, priority, now + afterMs, dialogueEvent, quietMs);
}

void SoldierCombatVoice_SayPrisonerOrder(const float* vehicle, std::uint32_t line)
{
    const std::uint8_t* soldiers = g_Soldiers.load(std::memory_order_acquire);
    if (!soldiers || !vehicle || MissionCodeGuard::ShouldBypassHooks())
        return;
    float snake[3] = {};
    if (SnakePosition(soldiers, snake) && SquaredDistance(snake, vehicle) < kSnakeAtVehicle * kSnakeAtVehicle)
        return;
    const std::uint32_t speaker = Nearest(soldiers, vehicle, kPrisonerOrderReach,
        [&](std::uint32_t index) { return HasEscortLines(SoldierVoiceType::Of(soldiers, index)); });
    if (speaker != kNoSoldier)
        Schedule(speaker, line, kCombatPriority, GetTickCount64());
}

void SoldierCombatVoice_OnSearchTeamSent(std::uint32_t lead, std::uint32_t second)
{
    const std::uint8_t* soldiers = g_Soldiers.load(std::memory_order_acquire);
    if (!soldiers || lead >= kSoldierSlotCount || MissionCodeGuard::ShouldBypassHooks())
        return;
    float at[3] = {};
    float snake[3] = {};
    const bool leadCanSpeak = CanSpeak(soldiers, lead) && PositionOf(soldiers, lead, at);
    const bool snakeNear = leadCanSpeak && SnakePosition(soldiers, snake) && SquaredDistance(at, snake) < kSnakeNearSearch * kSnakeNearSearch;
    const bool comradeNear = snakeNear && HasComradeWithin(soldiers, lead, at, kComradeNearSearch);
    LogDebug("VoiceProbe: search team lead %u, second %u: %s", lead, second,
             !leadCanSpeak ? "lead can't speak" : !snakeNear ? "Snake beyond 90 m" : !comradeNear ? "nobody within 45 m" : "EVE020 due");
    if (!comradeNear)
        return;
    const std::uint32_t replier = second < kSoldierSlotCount && CanSpeak(soldiers, second) ? second : kNoSoldier;
    Schedule(lead, kCheckingLine, kCombatPriority, GetTickCount64() + kCheckingAfterSentMs, kEnemyVoiceEvent, 0, replier);
}

#ifdef _DEBUG
void SoldierCombatVoice_ProbeVoiceCall(std::uint32_t soldierIndex, std::uint32_t dialogueEvent, std::uint32_t priority, std::uint32_t line)
{
    if (soldierIndex >= kSoldierSlotCount)
        return;
    const std::uint32_t watched = g_ProbeLine[soldierIndex].load(std::memory_order_acquire);
    if (!watched || line == watched)
        return;
    const std::uint64_t elapsed = GetTickCount64() - g_ProbeStartMs[soldierIndex].load(std::memory_order_relaxed);
    if (elapsed <= kProbeWindowMs)
        LogDebug("VoiceProbe: soldier %u gets line 0x%08X (event 0x%08X, priority %u) %llu ms into his %s",
                 soldierIndex, line, dialogueEvent, priority, elapsed, ProbeName(watched));
}
#endif

void SoldierCombatVoice_OnSoldierDamaged(std::uint32_t soldierIndex)
{
    if (soldierIndex < kSoldierSlotCount)
        g_HurtMs[soldierIndex].store(GetTickCount64(), std::memory_order_relaxed);
}

void SoldierCombatVoice_Reset()
{
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_Soldiers.store(nullptr, std::memory_order_release);
        g_FollowUps.fill(FollowUp{});
        g_CpPhase.fill(kPhaseUnknown);
        g_CpSpokeMs.fill(0);
    }
    for (auto& hurt : g_HurtMs)
        hurt.store(0, std::memory_order_relaxed);
    for (auto& checked : g_PhaseCheckedMs)
        checked.store(0, std::memory_order_relaxed);
    for (auto& talked : g_TalkedMs)
        talked.store(0, std::memory_order_relaxed);
#ifdef _DEBUG
    for (auto& line : g_ProbeLine)
        line.store(0, std::memory_order_relaxed);
#endif
}
