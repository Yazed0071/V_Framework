#include "pch.h"
#include "Soldier2Impl_ProcessSignal.h"

#include <Windows.h>
#include <cstddef>
#include <cstdint>

#include "AddressSet.h"
#include "ComradeTurn.h"
#include "HookUtils.h"
#include "log.h"
#include "MissionCodeGuard.h"
#include "NoticeControllerImpl_CheckForceNotice.h"
#include "PlayerAlertPhase.h"

namespace
{
    using ProcessSignal_t          = void*(__fastcall*)(std::uint8_t* soldier, void* error,
                                                        std::uint32_t soldierIndex, std::uint8_t* signal);
    using SearchSpecificComrade_t  = void(__fastcall*)(void* noticeNoiseAi, std::uint16_t* comradeId,
                                                       std::uint32_t callerIndex);
    using CallVoiceWithSync_t      = void(__fastcall*)(void* soundController, std::uint32_t realizedSlot,
                                                       std::uint32_t eventId, std::uint32_t priority,
                                                       std::uint32_t lineId, float delaySeconds);
    using IsPlayingVoice_t         = bool(__fastcall*)(void* soundController, std::uint32_t soldierIndex);

    constexpr std::size_t   kSignalRequestType          = 0x0C;
    constexpr std::size_t   kSoldierCount               = 0x08;
    constexpr std::size_t   kSoldierTransforms          = 0x18;
    constexpr std::size_t   kTransformPositions         = 0x20;      // one Vector4 per soldier
    constexpr std::size_t   kRealizedSoldierIndices     = 0x28;      // u16 soldier index for each realized slot
    constexpr std::size_t   kRealizedSoldierCount       = 0x44;
    constexpr std::size_t   kSoldierSoundController     = 0xA8;
    constexpr std::size_t   kNoticeAiSoldier            = 0x70;      // the only field SearchSpecificComrade reads
    constexpr std::size_t   kCallVoiceWithSyncVtblIndex = 0x18 / 8;
    constexpr std::size_t   kIsPlayingVoiceVtblIndex    = 0x58 / 8;  // this one takes the soldier index, not the realized slot

    constexpr std::uint32_t kEnemyVoiceEvent            = 0x95EA16B0u; // DD_vox_ene
    constexpr std::uint32_t kCallComradeLine            = 0x82762C03u; // EVN320
    constexpr std::uint32_t kComradeReplyLine           = 0x81762A91u; // EVN331
    constexpr std::uint32_t kInterrogationVoicePriority = 2;
    constexpr float         kRepeatGuardSeconds         = 1.0f;      // the same line can't play again on that soldier for this long
    constexpr std::uint64_t kShoutStartTimeoutMs        = 1500;
    constexpr std::uint64_t kReplyTimeoutMs             = 4000;      // reply anyway if he never turns
    constexpr std::uint64_t kAbandonCallMs              = 10000;
    constexpr float         kMaxCallDistance            = 60.0f;
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

    ProcessSignal_t         g_Orig                  = nullptr;
    void*                   g_HookTarget            = nullptr;
    SearchSpecificComrade_t g_SearchSpecificComrade = nullptr;
    PendingCall             g_PendingCall;

    struct NoticeNoiseAiStandIn
    {
        std::uint8_t  unused[kNoticeAiSoldier];
        std::uint8_t* soldier;
    };

    template <typename T>
    T& At(std::uint8_t* base, std::size_t offset)
    {
        return *reinterpret_cast<T*>(base + offset);
    }

    bool IsCallComradeSignal(const std::uint8_t* signal)
    {
        return *reinterpret_cast<const std::uint64_t*>(signal) == CallComrade::kInterrogationSignal
            && *reinterpret_cast<const std::uint32_t*>(signal + kSignalRequestType) == CallComrade::kCallComradeRequest;
    }

    std::uint16_t FindComradeToCall(std::uint8_t* soldier, std::uint32_t callerIndex)
    {
        NoticeNoiseAiStandIn noticeNoiseAi{};
        noticeNoiseAi.soldier = soldier;

        std::uint16_t comradeId = kNoComrade;
        g_SearchSpecificComrade(&noticeNoiseAi, &comradeId, callerIndex);
        return comradeId;
    }

    const float* SoldierPosition(std::uint8_t* soldier, std::uint32_t soldierIndex)
    {
        std::uint8_t* transforms = At<std::uint8_t*>(soldier, kSoldierTransforms);
        return At<float*>(transforms, kTransformPositions) + soldierIndex * 4;
    }

    bool IsWithinCallRange(std::uint8_t* soldier, std::uint32_t callerIndex, std::uint16_t comradeId)
    {
        const float* caller  = SoldierPosition(soldier, callerIndex);
        const float* comrade = SoldierPosition(soldier, comradeId & kSoldierIndexMask);

        const float dx = comrade[0] - caller[0];
        const float dy = comrade[1] - caller[1];
        const float dz = comrade[2] - caller[2];
        return dx * dx + dy * dy + dz * dz <= kMaxCallDistance * kMaxCallDistance;
    }

    void SendComradeToCaller(std::uint16_t comradeId, std::uint32_t callerIndex)
    {
        const auto callerId = static_cast<std::uint16_t>(kSoldierIdBase | (callerIndex & kSoldierIndexMask));
        const std::uint32_t comradeIndex = comradeId & kSoldierIndexMask;

        CallComrade::DeliverComradeCallRelaxed(comradeIndex, callerId);
        CallComrade::TurnToCaller(comradeIndex);
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
        if (IsPlayerBeingHunted())
        {
            call = PendingCall{};
            return;
        }

        const std::uint16_t comradeId = FindComradeToCall(call.soldier, call.callerIndex);
        if (comradeId == kNoComrade || !IsWithinCallRange(call.soldier, call.callerIndex, comradeId))
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

        CallVoice(soldier, callerIndex, kCallComradeLine);

        const std::uint64_t now = GetTickCount64();
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

        if (MissionCodeGuard::ShouldBypassHooks())
            return result;
        if (!signal || !IsCallComradeSignal(signal))
            return result;

        CallComradeOver(soldier, soldierIndex);
        return result;
    }
}

bool CallComrade::IsCallPending()
{
    return g_PendingCall.stage != CallStage::None;
}

void CallComrade::UpdatePendingCall()
{
    PendingCall& call = g_PendingCall;
    if (call.stage == CallStage::None)
        return;

    const std::uint64_t now = GetTickCount64();
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

    if (!gAddr.Soldier2Impl_ProcessSignal || !gAddr.NoticeNoiseAiImpl_SearchSpecificComrade)
    {
        LogError("CallComradeCommand: soldier addresses missing for %s, held soldiers will not call comrades",
                 GetGameBuildName(gGameBuild));
        return true;
    }

    g_SearchSpecificComrade = reinterpret_cast<SearchSpecificComrade_t>(
        ResolveGameAddress(gAddr.NoticeNoiseAiImpl_SearchSpecificComrade));

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
