#include "pch.h"
#include "Soldier2Impl_ProcessSignal.h"

#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "AddressSet.h"
#include "ComradeTurn.h"
#include "FakeSurrender.h"
#include "GameClock.h"
#include "HookUtils.h"
#include "log.h"
#include "MissionCodeGuard.h"
#include "NoticeControllerImpl_CheckForceNotice.h"
#include "PlayerAlertPhase.h"
#include "RadioControllerImpl_StepRadioWaitResponse.h"
#include "SoldierCommandPost.h"
#include "SoldierVoiceType.h"
#include "VIPHoldupHook.h"

namespace
{
    using ProcessSignal_t          = void*(__fastcall*)(std::uint8_t* soldier, void* error,
                                                        std::uint32_t soldierIndex, std::uint8_t* signal);
    using AiStateOf_t              = int(__fastcall*)(void* noticeController, std::uint32_t soldierIndex);
    using IsInTower_t              = bool(__fastcall*)(void* utility, const float* position);
    using CallVoiceWithSync_t      = void(__fastcall*)(void* soundController, std::uint32_t realizedSlot,
                                                       std::uint32_t eventId, std::uint32_t priority,
                                                       std::uint32_t lineId, float delaySeconds);
    using IsPlayingVoice_t         = bool(__fastcall*)(void* soundController, std::uint32_t soldierIndex);
    using IsTranslatable_t         = bool(__fastcall*)(std::uint8_t* soldier, std::uint32_t soldierIndex);
    using GetQuarkSystemTable_t    = std::uint8_t*(__fastcall*)();
    using AddNoise_t               = void(__fastcall*)(void* noiseSystem, const void* noise);

    constexpr std::size_t   kSignalRequestType          = 0x0C;
    constexpr std::size_t   kSoldierCount               = 0x08;
    constexpr std::size_t   kSoldierTransforms          = 0x18;
    constexpr std::size_t   kTransformActiveBits        = 0x08;
    constexpr std::size_t   kTransformPositions         = 0x20;      // one Vector4 per soldier
    constexpr std::size_t   kRealizedSoldierIndices     = 0x28;      // u16 soldier index for each realized slot
    constexpr std::size_t   kRealizedSoldierCount       = 0x44;
    constexpr std::size_t   kSoldierLife                = 0x38;      // 0x40 per soldier at +8
    constexpr std::size_t   kLifeRecords                = 0x08;
    constexpr std::size_t   kLifeRecordSize             = 0x40;
    constexpr std::size_t   kLifeFlags                  = 0x12;
    constexpr std::uint16_t kLifeFlagsSkipped           = 0x3;
    constexpr std::size_t   kLifeState                  = 0x3E;
    constexpr std::uint8_t  kLifeStateSkipped           = 2;
    constexpr std::size_t   kSoldierNoticeController    = 0x68;
    constexpr std::size_t   kNoticeControllerSoldiers   = 0x58;      // and back: the notice controller's Soldier2Impl
    constexpr std::size_t   kAiStateOfVtblIndex         = 0x50 / 8;
    constexpr int           kAiStateOnRoute             = 0x3E;      // the only state vanilla's comrade search takes
    constexpr std::size_t   kSoldierParams              = 0x128;
    constexpr std::size_t   kParamsHearing              = 0x18;      // float[30]: normal, sandstorm, rain; ten noise classes each
    constexpr std::size_t   kHearingSandstorm           = 10;
    constexpr std::size_t   kHearingRain                = 20;
    constexpr std::size_t   kHearingCallClass           = 6;         // "hll", what vanilla's comrade search reads (normal table only)
    constexpr std::size_t   kSoldierFlags               = 0x2A4;
    constexpr std::uint32_t kFlagsRain                  = 1u << 0xD; // the weather the hearing tables follow
    constexpr std::uint32_t kFlagsSandstorm             = 1u << 0xE;
    constexpr std::size_t   kQuarkGameServices          = 0xA8;
    constexpr std::size_t   kGameServicesNoise          = 0x08;
    constexpr std::uint8_t  kHandsUpCryNoise            = 0x0B;      // the power of vanilla's hands-up cry
    constexpr std::size_t   kSoldierCommonUtility       = 0x138;
    constexpr std::size_t   kIsInTowerVtblIndex         = 0x30 / 8;   // a tower trap volume: guards up there stay at their post
    constexpr std::size_t   kSoldierSoundController     = 0xA8;
    constexpr std::size_t   kCallVoiceWithSyncVtblIndex = 0x18 / 8;
    constexpr std::size_t   kIsPlayingVoiceVtblIndex    = 0x58 / 8;  // this one takes the soldier index, not the realized slot

    constexpr std::uint32_t kEnemyVoiceEvent            = 0x95EA16B0u; // DD_vox_ene
    constexpr std::uint32_t kCallComradeLine            = 0x82762C03u; // EVN320
    constexpr std::uint32_t kCallOneComradeLine         = 0x81762A90u; // EVN330, recorded for the D voices only
    constexpr std::uint32_t kComradeReplyLine           = 0x81762A91u; // EVN331
    constexpr std::uint32_t kNotUnderstoodLine          = 0x16CD9716u; // EVR062; vanilla picks it or the next id, EVR063, at random
    constexpr std::uint32_t kNotUnderstoodOtherLine     = 0x5E2C8E38u; // what vanilla has the soldiers flagged below say instead
    constexpr std::uint32_t kRefusalLine                = 0x17CD9886u; // EVR071 "Go on - do it"; the next id is EVR070, the outbursts
    constexpr std::size_t   kSoldierStatus              = 0x110;       // 0x28 per soldier at +8, the count at +0x10
    constexpr std::size_t   kStatusRecords              = 0x08;
    constexpr std::size_t   kStatusCount                = 0x10;
    constexpr std::size_t   kStatusRecordSize           = 0x28;
    constexpr std::uint64_t kSaysOtherLine              = 1ull << 0x21; // in the record's second qword
    constexpr std::uint32_t kSkippedByComradeSearch     = 1u << 0xD;    // in the record's first dword
    constexpr std::size_t   kStatusSyncFlags            = 0x24;
    constexpr std::uint32_t kSyncHeldUp                 = 1u << 28;     // HoldupAi sets it when it wakes, clears it when it sleeps
    constexpr std::uint64_t kHeld                       = 1ull << 16;   // in the record's first qword
    constexpr std::uint32_t kInterrogationVoicePriority = 2;
    constexpr float         kRepeatGuardSeconds         = 1.0f;      // the same line can't play again on that soldier for this long
    constexpr std::uint64_t kShoutStartTimeoutMs        = 1500;
    constexpr std::uint64_t kReplyTimeoutMs             = 6000;      // reply anyway if he never turns
    constexpr std::uint64_t kAbandonCallMs              = 10000;
    constexpr std::uint64_t kDeafToShoutMs              = 2000;      // the summoned comrade doesn't treat the shout as a noise to check
    constexpr float         kMaxCallDistance            = 30.0f;     // as far as GZ soldiers hear the shout
    constexpr float         kSearchRadiusPadding        = 20.0f;     // vanilla's search reaches this far past the soldier param
    constexpr std::uint16_t kSoldierIdBase              = 0x400;
    constexpr std::uint16_t kSoldierIndexMask           = 0x1FF;
    constexpr std::uint16_t kNoComrade                  = 0xFFFF;
    constexpr std::uint32_t kNotRealized                = 0xFFFF;

    enum class CallStage
    {
        None,
        WaitingForShout,
        Shouting,
        WaitingForReply,
    };

    struct PendingCall
    {
        CallStage     stage        = CallStage::None;
        std::uint8_t* soldier      = nullptr;
        std::uint32_t callerIndex  = 0;
        std::uint32_t comradeIndex = 0;
        std::uint64_t shoutQueued  = 0;
        std::uint64_t replyDue     = 0;
        std::uint64_t abandonAt    = 0;
    };

    struct alignas(16) Noise   // what vanilla hands NoiseSystem::AddNoise
    {
        float         position[4];
        std::uint8_t  power;
        std::uint8_t  flags;
        std::uint16_t source;
        std::uint32_t unused[3];
    };
    static_assert(offsetof(Noise, source) == 0x12, "listeners read the source at +0x12");

    ProcessSignal_t         g_Orig                  = nullptr;
    void*                   g_HookTarget            = nullptr;
    IsTranslatable_t        g_IsTranslatable        = nullptr;
    GetQuarkSystemTable_t   g_GetQuarkSystemTable   = nullptr;
    PendingCall             g_PendingCall;
    CallComrade::HeldAnswer g_Answer                = CallComrade::HeldAnswer::Complies;
    std::uint32_t           g_LineRoll              = 0x2545F491u;
    std::atomic<int>        g_DeafComrade{ -1 };
    std::atomic<std::uint32_t> g_NoiseSource{ 0 };   // our last shout or refusal
    std::atomic<std::uint64_t> g_NoiseUntil{ 0 };

    template <typename T>
    T& At(std::uint8_t* base, std::size_t offset)
    {
        return *reinterpret_cast<T*>(base + offset);
    }

    std::uint32_t RequestOf(const std::uint8_t* signal)
    {
        return *reinterpret_cast<const std::uint64_t*>(signal) == CallComrade::kInterrogationSignal
            ? *reinterpret_cast<const std::uint32_t*>(signal + kSignalRequestType) : 0;
    }

    const float* SoldierPosition(std::uint8_t* soldier, std::uint32_t soldierIndex)
    {
        std::uint8_t* transforms = At<std::uint8_t*>(soldier, kSoldierTransforms);
        return At<float*>(transforms, kTransformPositions) + soldierIndex * 4;
    }

    float SquaredDistance(const float* from, const float* to)
    {
        const float dx = to[0] - from[0];
        const float dy = to[1] - from[1];
        const float dz = to[2] - from[2];
        return dx * dx + dy * dy + dz * dz;
    }

    std::uint8_t* StatusRecordOf(std::uint8_t* soldier, std::uint32_t soldierIndex)
    {
        std::uint8_t* status = At<std::uint8_t*>(soldier, kSoldierStatus);
        std::uint8_t* record = At<std::uint8_t*>(status, kStatusRecords);
        if (soldierIndex < At<std::uint32_t>(status, kStatusCount))
            record += soldierIndex * kStatusRecordSize;
        return record;
    }

    bool IsActive(std::uint8_t* soldier, std::uint32_t soldierIndex)
    {
        const std::uint8_t* active = At<std::uint8_t*>(At<std::uint8_t*>(soldier, kSoldierTransforms), kTransformActiveBits);
        return (active[soldierIndex >> 3] & (1u << (soldierIndex & 7))) != 0;
    }

    bool VanillaWouldSend(std::uint8_t* soldier, std::uint32_t soldierIndex)   // the checks vanilla's SearchSpecificComrade makes
    {
        std::uint8_t* life = At<std::uint8_t*>(At<std::uint8_t*>(soldier, kSoldierLife), kLifeRecords) + soldierIndex * kLifeRecordSize;
        if (At<std::uint8_t>(life, kLifeState) == kLifeStateSkipped || (At<std::uint16_t>(life, kLifeFlags) & kLifeFlagsSkipped) != 0)
            return false;

        void* noticeController = At<void*>(soldier, kSoldierNoticeController);
        auto aiStateOf = (*reinterpret_cast<AiStateOf_t**>(noticeController))[kAiStateOfVtblIndex];
        if (aiStateOf(noticeController, soldierIndex) != kAiStateOnRoute)
            return false;

        if (At<std::uint32_t>(StatusRecordOf(soldier, soldierIndex), 0) & kSkippedByComradeSearch)
            return false;

        alignas(16) float position[4] = {};
        const float* at = SoldierPosition(soldier, soldierIndex);
        position[0] = at[0];
        position[1] = at[1];
        position[2] = at[2];
        void* utility = At<void*>(soldier, kSoldierCommonUtility);
        auto isInTower = (*reinterpret_cast<IsInTower_t**>(utility))[kIsInTowerVtblIndex];
        return !isInTower(utility, position);
    }

    bool IsHeldUp(std::uint8_t* soldier, std::uint32_t soldierIndex)   // his own hands are up
    {
        std::uint8_t* status = StatusRecordOf(soldier, soldierIndex);
        return (At<std::uint32_t>(status, kStatusSyncFlags) & kSyncHeldUp) || (At<std::uint64_t>(status, 0) & kHeld)
            || FakeSurrender::IsFaking(soldier, soldierIndex);
    }

    float CallReach(std::uint8_t* soldier)   // vanilla's search radius, read from the hearing table of the current weather
    {
        const float* hearing = At<float*>(At<std::uint8_t*>(soldier, kSoldierParams), kParamsHearing);
        const std::uint32_t flags = At<std::uint32_t>(soldier, kSoldierFlags);
        std::size_t table = 0;
        if (flags & kFlagsRain)
            table = kHearingRain;
        else if (flags & kFlagsSandstorm)
            table = kHearingSandstorm;
        return (std::min)(hearing[table + kHearingCallClass] + kSearchRadiusPadding, kMaxCallDistance);
    }

    std::uint16_t FindComradeToCall(std::uint8_t* soldier, std::uint32_t callerIndex)   // the nearest one vanilla would send, minus VIPs
    {
        const std::uint8_t cp = SoldierCommandPost::IndexOf(soldier, callerIndex);
        if (cp == SoldierCommandPost::kNone)
            return kNoComrade;   // no CP of his own: nobody answers him

        const float* caller = SoldierPosition(soldier, callerIndex);
        const float  reach  = CallReach(soldier);
        const std::uint32_t count = At<std::uint32_t>(soldier, kSoldierCount);

        std::uint16_t nearest = kNoComrade;
        float nearestDistance = reach * reach;
        for (std::uint32_t index = 0; index < count; ++index)
        {
            if (index == callerIndex || !IsActive(soldier, index))
                continue;
            const float distance = SquaredDistance(caller, SoldierPosition(soldier, index));
            if (distance >= nearestDistance || SoldierCommandPost::IndexOf(soldier, index) != cp || !VanillaWouldSend(soldier, index))
                continue;
            if (IsVIPHoldupImportantSoldierIndex(static_cast<std::uint16_t>(index), nullptr))
                continue;   // VIPs and officers don't come running for a grunt
            if (IsHeldUp(soldier, index))
                continue;

            nearest = static_cast<std::uint16_t>(kSoldierIdBase | index);
            nearestDistance = distance;
        }
        return nearest;
    }

    void MakeNoise(std::uint8_t* soldier, std::uint32_t soldierIndex)   // heard the way vanilla's hands-up cry is
    {
        if (!g_GetQuarkSystemTable)
            return;
        std::uint8_t* services = At<std::uint8_t*>(g_GetQuarkSystemTable(), kQuarkGameServices);
        void* noiseSystem = services ? At<void*>(services, kGameServicesNoise) : nullptr;
        if (!noiseSystem)
            return;

        Noise noise{};
        const float* at = SoldierPosition(soldier, soldierIndex);
        noise.position[0] = at[0];
        noise.position[1] = at[1];
        noise.position[2] = at[2];
        noise.power  = kHandsUpCryNoise;
        noise.source = static_cast<std::uint16_t>(kSoldierIdBase | (soldierIndex & kSoldierIndexMask));
        g_NoiseSource.store(noise.source, std::memory_order_relaxed);
        g_NoiseUntil.store(GetTickCount64() + kDeafToShoutMs, std::memory_order_release);
        (*reinterpret_cast<AddNoise_t**>(noiseSystem))[0](noiseSystem, &noise);
    }

    void SendComradeToCaller(std::uint16_t comradeId, std::uint32_t callerIndex)
    {
        const auto callerId = static_cast<std::uint16_t>(kSoldierIdBase | (callerIndex & kSoldierIndexMask));
        const std::uint32_t comradeIndex = comradeId & kSoldierIndexMask;

        const int phase = PlayerAlertPhase::Read();
        if (phase == PlayerAlertPhase::kCaution)
            CallComrade::DeliverComradeCallOnGuard(comradeIndex, callerId);
        else
            CallComrade::DeliverComradeCallRelaxed(comradeIndex, callerId);

        CallComrade::TurnToCaller(comradeIndex);
        if (IsVIPHoldupImportantSoldierIndex(static_cast<std::uint16_t>(callerIndex & kSoldierIndexMask), nullptr))
            CallComrade::HurryToCaller(comradeIndex);
    }

    bool StillHeld(std::uint32_t callerIndex)
    {
        bool known = false;
        const bool held = CpRadioResponse::IsHeld(static_cast<std::uint16_t>(callerIndex), known);
        return held || !known;
    }

    std::uint32_t RealizedSlotOf(std::uint8_t* soldier, std::uint32_t soldierIndex)
    {
        std::uint8_t* transforms = At<std::uint8_t*>(soldier, kSoldierTransforms);
        const std::uint16_t* soldierInSlot = At<std::uint16_t*>(transforms, kRealizedSoldierIndices);
        const std::uint32_t slotCount = At<std::uint32_t>(transforms, kRealizedSoldierCount);

        for (std::uint32_t slot = 0; slot < slotCount; ++slot)
        {
            if (soldierInSlot[slot] == soldierIndex)
                return slot;
        }
        return kNotRealized;
    }

    void CallVoice(std::uint8_t* soldier, std::uint32_t soldierIndex, std::uint32_t line)
    {
        const std::uint32_t slot = RealizedSlotOf(soldier, soldierIndex);
        if (slot == kNotRealized)
            return;

        void* soundController = At<void*>(soldier, kSoldierSoundController);
        auto callVoice = (*reinterpret_cast<CallVoiceWithSync_t**>(soundController))[kCallVoiceWithSyncVtblIndex];
        callVoice(soundController, slot, kEnemyVoiceEvent, kInterrogationVoicePriority, line, kRepeatGuardSeconds);
    }

    bool Understands(std::uint8_t* soldier, std::uint32_t soldierIndex)   // the interpreter Snake needs for his language
    {
        return !g_IsTranslatable || g_IsTranslatable(soldier, soldierIndex);
    }

    std::uint32_t RollLineVariant()   // vanilla's xorshift pick between two neighbouring lines
    {
        g_LineRoll ^= g_LineRoll << 13;
        g_LineRoll ^= g_LineRoll >> 7;
        g_LineRoll ^= g_LineRoll << 5;
        return ~g_LineRoll & 1;
    }

    std::uint32_t CallLineFor(std::uint8_t* soldier, std::uint32_t soldierIndex)
    {
        const std::uint32_t voiceType = SoldierVoiceType::Of(soldier, soldierIndex);
        const bool hasOneGuyLine = voiceType == SoldierVoiceType::kEneD || voiceType == SoldierVoiceType::kEneDRu
            || voiceType == SoldierVoiceType::kEneDAf;
        return hasOneGuyLine && RollLineVariant() ? kCallOneComradeLine : kCallComradeLine;
    }

    void SayNotUnderstood(std::uint8_t* soldier, std::uint32_t soldierIndex)   // as he answers vanilla's interrogation then
    {
        if (At<std::uint64_t>(StatusRecordOf(soldier, soldierIndex), sizeof(std::uint64_t)) & kSaysOtherLine)
        {
            CallVoice(soldier, soldierIndex, kNotUnderstoodOtherLine);
            return;
        }
        CallVoice(soldier, soldierIndex, kNotUnderstoodLine | RollLineVariant());
    }

    void SayRefusal(std::uint8_t* soldier, std::uint32_t soldierIndex)
    {
        CallVoice(soldier, soldierIndex, kRefusalLine | RollLineVariant());
        g_DeafComrade.store(-1, std::memory_order_release);
        MakeNoise(soldier, soldierIndex);
    }

    CallComrade::HeldAnswer AnswerOf(std::uint8_t* soldier, std::uint32_t soldierIndex)
    {
        if (!Understands(soldier, soldierIndex))
            return CallComrade::HeldAnswer::NotUnderstood;
        if (FakeSurrender::IsFaking(soldier, soldierIndex))
            return CallComrade::HeldAnswer::Refuses;
        return CallComrade::HeldAnswer::Complies;
    }

    bool IsSaying(std::uint8_t* soldier, std::uint32_t soldierIndex)
    {
        void* soundController = At<void*>(soldier, kSoldierSoundController);
        auto isPlaying = (*reinterpret_cast<IsPlayingVoice_t**>(soundController))[kIsPlayingVoiceVtblIndex];
        return isPlaying(soundController, soldierIndex);
    }

    bool IsPlayerBeingHunted()
    {
        const int phase = PlayerAlertPhase::Read();
        return phase == PlayerAlertPhase::kAlert || phase == PlayerAlertPhase::kEvasion;
    }

    void SummonComrade(std::uint64_t now)
    {
        PendingCall& call = g_PendingCall;
        if (!StillHeld(call.callerIndex))
        {
            call = PendingCall{};   // let go or knocked out before his shout ended: nobody heard a call, as in GZ
            return;
        }

        const std::uint16_t comradeId = IsPlayerBeingHunted() ? kNoComrade : FindComradeToCall(call.soldier, call.callerIndex);
        g_DeafComrade.store(comradeId != kNoComrade ? comradeId & kSoldierIndexMask : -1, std::memory_order_release);
        MakeNoise(call.soldier, call.callerIndex);   // whoever else is near enough hears the shout too
        if (comradeId == kNoComrade)
        {
            call = PendingCall{};
            return;
        }

        SendComradeToCaller(comradeId, call.callerIndex);
        call.comradeIndex = comradeId & kSoldierIndexMask;
        call.replyDue     = now + kReplyTimeoutMs;
        call.stage        = CallStage::WaitingForReply;
    }

    void CallComradeOver(std::uint8_t* soldier, std::uint32_t callerIndex)
    {
        g_PendingCall = PendingCall{};
        CallComrade::StopTurn();

        if (callerIndex >= At<std::uint32_t>(soldier, kSoldierCount))
            return;

        CallVoice(soldier, callerIndex, CallLineFor(soldier, callerIndex));

        const std::uint64_t now = GameClock::NowMs();
        g_PendingCall.stage       = CallStage::WaitingForShout;
        g_PendingCall.soldier     = soldier;
        g_PendingCall.callerIndex = callerIndex;
        g_PendingCall.shoutQueued = now;
        g_PendingCall.abandonAt   = now + kAbandonCallMs;
    }

    void* __fastcall hk_ProcessSignal(std::uint8_t* soldier, void* error, std::uint32_t soldierIndex,
                                      std::uint8_t* signal)
    {
        void* result = g_Orig(soldier, error, soldierIndex, signal);

        if (MissionCodeGuard::ShouldBypassHooks() || !signal || soldierIndex >= At<std::uint32_t>(soldier, kSoldierCount))
            return result;

        switch (RequestOf(signal))
        {
        case CallComrade::kCallComradeRequest:
            CallComradeOver(soldier, soldierIndex);
            break;
        case CallComrade::kWillComplyRequest:
            g_Answer = AnswerOf(soldier, soldierIndex);
            break;
        case CallComrade::kSayNotUnderstoodRequest:
            SayNotUnderstood(soldier, soldierIndex);
            break;
        case CallComrade::kSayRefusalRequest:
            SayRefusal(soldier, soldierIndex);
            break;
        default:
            break;
        }
        return result;
    }
}

bool CallComrade::IsCallPending()
{
    return g_PendingCall.stage != CallStage::None;
}

CallComrade::HeldAnswer CallComrade::TakeAnswer()
{
    const HeldAnswer answer = g_Answer;
    g_Answer = HeldAnswer::Complies;
    return answer;
}

void CallComrade::ClearForNewMission()
{
    g_PendingCall = PendingCall{};
    g_Answer      = HeldAnswer::Complies;
    g_DeafComrade.store(-1, std::memory_order_release);
    g_NoiseUntil.store(0, std::memory_order_release);
    CallComrade::ForgetComrade();
}

bool CallComrade::IsDeafToNoise(void* noticeController, std::uint32_t soldierIndex, const void* noise)
{
    if (!noticeController || !noise || GetTickCount64() >= g_NoiseUntil.load(std::memory_order_acquire))
        return false;
    if (reinterpret_cast<const Noise*>(noise)->source != g_NoiseSource.load(std::memory_order_relaxed))
        return false;
    if (g_DeafComrade.load(std::memory_order_acquire) == static_cast<int>(soldierIndex))
        return true;   // he answers the shout rather than checks it

    std::uint8_t* soldier = At<std::uint8_t*>(static_cast<std::uint8_t*>(noticeController), kNoticeControllerSoldiers);
    return soldier && soldierIndex < At<std::uint32_t>(soldier, kSoldierCount) && IsHeldUp(soldier, soldierIndex);   // hands up, they stay up
}

void CallComrade::UpdatePendingCall()
{
    PendingCall& call = g_PendingCall;
    if (call.stage == CallStage::None)
        return;

    const std::uint64_t now = GameClock::NowMs();
    if (now >= call.abandonAt)
    {
        call = PendingCall{};
        CallComrade::StopTurn();
        return;
    }

    switch (call.stage)
    {
    case CallStage::WaitingForShout:
        if (IsSaying(call.soldier, call.callerIndex))
            call.stage = CallStage::Shouting;
        else if (now - call.shoutQueued >= kShoutStartTimeoutMs)
            SummonComrade(now);
        break;

    case CallStage::Shouting:
        if (!IsSaying(call.soldier, call.callerIndex))
            SummonComrade(now);
        break;

    case CallStage::WaitingForReply:
        if (CallComrade::TakeReplyCue() || now >= call.replyDue)
        {
            CallVoice(call.soldier, call.comradeIndex, kComradeReplyLine);
            CallComrade::StopTurn();
            call = PendingCall{};
        }
        break;

    default:
        break;
    }
}

bool Install_Soldier2Impl_ProcessSignal_Hook()
{
    if (g_HookTarget)
        return true;

    if (!gAddr.Soldier2Impl_ProcessSignal)
    {
        LogError("CallComradeCommand: Soldier2Impl::ProcessSignal address missing for %s, held soldiers will not call comrades",
                 GetGameBuildName(gGameBuild));
        return true;
    }

    if (gAddr.GetQuarkSystemTable)
        g_GetQuarkSystemTable = reinterpret_cast<GetQuarkSystemTable_t>(ResolveGameAddress(gAddr.GetQuarkSystemTable));
    else
        LogWarn("CallComradeCommand: GetQuarkSystemTable missing for %s, shouts and refusals make no noise",
                GetGameBuildName(gGameBuild));
    if (gAddr.Soldier2InterrogateUtil_IsTranslatable)
        g_IsTranslatable = reinterpret_cast<IsTranslatable_t>(ResolveGameAddress(gAddr.Soldier2InterrogateUtil_IsTranslatable));
    else
        LogWarn("CallComradeCommand: IsTranslatable missing for %s, held soldiers understand Snake without an interpreter",
                GetGameBuildName(gGameBuild));

    void* target = ResolveGameAddress(gAddr.Soldier2Impl_ProcessSignal);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_ProcessSignal),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("CallComradeCommand: ProcessSignal hook failed at %p, held soldiers will not call comrades", target);
        return false;
    }

    g_HookTarget = target;
    return true;
}

void Uninstall_Soldier2Impl_ProcessSignal_Hook()
{
    if (g_HookTarget)
    {
        DisableAndRemoveHook(g_HookTarget);
        g_HookTarget = nullptr;
        g_Orig       = nullptr;
    }
    g_PendingCall = PendingCall{};
    CallComrade::StopTurn();
}
