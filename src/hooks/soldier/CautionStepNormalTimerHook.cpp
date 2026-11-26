#include <pch.h>

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>

#include "HookUtils.h"
#include "log.h"
#include "MissionCodeGuard.h"
#include "AddressSet.h"
#include "PlayerAlertPhase.h"
#include "CautionStepNormalTimerHook.h"

namespace
{
    using DecrementPhaseCounter_t = void(__fastcall*)(void* self, std::uint32_t cpIndex, void* knowledge);

    static DecrementPhaseCounter_t g_OrigCautionDecrement = nullptr;
    static DecrementPhaseCounter_t g_OrigEvasionDecrement = nullptr;
    static DecrementPhaseCounter_t g_OrigAlertDecrement   = nullptr;

    static constexpr unsigned kMaxCommandPosts = 256;
    static constexpr unsigned kPhaseTimerCount = 3;

    struct PhaseDurationOverride
    {
        bool  enabled = false;
        float seconds = 0.0f;
        bool  cpEnabled[kMaxCommandPosts] = {};
        float cpSeconds[kMaxCommandPosts] = {};
    };

    static PhaseDurationOverride g_Overrides[kPhaseTimerCount];

    static constexpr std::size_t  kPhaseAiOperator     = 0x70;
    static constexpr std::size_t  kLuaCommandsOperator = 0x10;   // the CP Lua command interface sits 0x10 past the Cp2Operator
    static constexpr std::size_t  kOperatorCpParams    = 0x28;   // 0x14 per CP
    static constexpr std::size_t  kCpParamSize         = 0x14;
    static constexpr std::size_t  kOperatorCpStates    = 0x30;   // 0x60 per CP
    static constexpr std::size_t  kCpStateSize         = 0x60;
    static constexpr std::size_t  kCpStatePhase        = 0x48;
    static constexpr std::size_t  kCpStateTimer        = 0x50;
    static constexpr std::size_t  kCpStateFlags        = 0x54;
    static constexpr std::size_t  kOperatorFrameTime   = 0x158;
    static constexpr std::size_t  kOperatorCpCount     = 0x164;
    static constexpr float        kNoValue             = -1.0f;
    static constexpr std::uint32_t kNoCpIndex          = 0xFFFFFFFFu;

    static std::atomic<std::uint8_t*> g_Operator{ nullptr };   // Cp2Operator, re-read every tick and every CP command

    static thread_local bool          t_CpCaptureArmed  = false;
    static thread_local std::uint32_t t_CapturedCpIndex = kNoCpIndex;

    template <typename T>
    static T& At(std::uint8_t* base, std::size_t offset)
    {
        return *reinterpret_cast<T*>(base + offset);
    }

    static PhaseDurationOverride& OverrideOf(CpPhaseTimer phase)
    {
        return g_Overrides[static_cast<unsigned>(phase)];
    }

    static std::size_t CpParamRateOffset(CpPhaseTimer phase)   // each phase's timer drained per second, the timer starting at 1.0
    {
        switch (phase)
        {
        case CpPhaseTimer::Evasion: return 0x08;
        case CpPhaseTimer::Alert:   return 0x0C;
        default:                    return 0x04;
        }
    }

    static std::int32_t PhaseIdOf(CpPhaseTimer phase)
    {
        switch (phase)
        {
        case CpPhaseTimer::Evasion: return PlayerAlertPhase::kEvasion;
        case CpPhaseTimer::Alert:   return PlayerAlertPhase::kAlert;
        default:                    return PlayerAlertPhase::kCaution;
        }
    }

    static float SanitizedSeconds(float seconds)
    {
        return seconds > 0.0f ? seconds : 1.0f;
    }

    static float OverrideRate(CpPhaseTimer phase, std::uint32_t cpIndex)
    {
        const PhaseDurationOverride& over = OverrideOf(phase);
        if (cpIndex < kMaxCommandPosts && over.cpEnabled[cpIndex])
            return 1.0f / over.cpSeconds[cpIndex];
        if (over.enabled)
            return 1.0f / over.seconds;
        return 0.0f;
    }

    static bool HoldsCp(std::uint8_t* op, std::uint32_t cpIndex)
    {
        return op && cpIndex < At<std::uint32_t>(op, kOperatorCpCount);
    }

    static std::uint8_t* OperatorWithCp(std::uint32_t cpIndex)
    {
        std::uint8_t* op = g_Operator.load(std::memory_order_acquire);
        return HoldsCp(op, cpIndex) ? op : nullptr;
    }

    static std::uint8_t* OperatorOfPhaseAi(void* self)
    {
        std::uint8_t* op = self ? At<std::uint8_t*>(static_cast<std::uint8_t*>(self), kPhaseAiOperator) : nullptr;
        if (op)
            g_Operator.store(op, std::memory_order_release);
        return op;
    }

    static float* CpRate(std::uint8_t* op, std::uint32_t cpIndex, CpPhaseTimer phase)
    {
        std::uint8_t* params = At<std::uint8_t*>(op, kOperatorCpParams);
        return params ? &At<float>(params, cpIndex * kCpParamSize + CpParamRateOffset(phase)) : nullptr;
    }

    static float VanillaRate(std::uint8_t* op, std::uint32_t cpIndex, CpPhaseTimer phase)
    {
        const float* rate = CpRate(op, cpIndex, phase);
        return rate ? *rate : 0.0f;
    }

    static std::uint8_t* CpState(std::uint8_t* op, std::uint32_t cpIndex)
    {
        std::uint8_t* states = At<std::uint8_t*>(op, kOperatorCpStates);
        return states ? states + cpIndex * kCpStateSize : nullptr;
    }


    static std::uint8_t GetKnowledgeFlags(void* knowledge)
    {
        if (!knowledge)
            return 0xFFu;

        return *reinterpret_cast<std::uint8_t*>(
            reinterpret_cast<std::uintptr_t>(knowledge) + 7ull
            );
    }


    static void ReplaceVanillaCautionDrainIfNeeded(void* self, std::uint32_t cpIndex, void* knowledge)
    {
        std::uint8_t* op = OperatorOfPhaseAi(self);
        std::uint8_t* state = op ? CpState(op, cpIndex) : nullptr;
        if (!state || !knowledge)
        {
            g_OrigCautionDecrement(self, cpIndex, knowledge);
            return;
        }

        float& timer = At<float>(state, kCpStateTimer);
        std::uint32_t& flags = At<std::uint32_t>(state, kCpStateFlags);
        const float beforeTimer = timer;
        const std::uint32_t beforeFlags = flags;
        const std::uint8_t knowledgeFlagsBefore = GetKnowledgeFlags(knowledge);
        const float deltaScale = At<float>(op, kOperatorFrameTime);


        g_OrigCautionDecrement(self, cpIndex, knowledge);

        const float vanillaDrain = beforeTimer - timer;


        const float overrideDrain = OverrideRate(CpPhaseTimer::Caution, cpIndex);
        if (overrideDrain <= 0.0f)
            return;


        if ((knowledgeFlagsBefore & 0x80u) == 0u)
            return;


        if (beforeTimer <= 0.0f || vanillaDrain <= 0.000001f || deltaScale <= 0.0f)
            return;

        float customAfterTimer = beforeTimer - deltaScale * overrideDrain;
        if (customAfterTimer < 0.0f)
            customAfterTimer = 0.0f;


        timer = customAfterTimer;


        constexpr std::uint32_t kBit22 = (1u << 22);   // vanilla clears it once the timer is under 0.95

        if (customAfterTimer < 0.95f)
        {
            flags &= ~kBit22;
        }
        else
        {
            if ((beforeFlags & kBit22) != 0u)
                flags |= kBit22;
            else
                flags &= ~kBit22;
        }
    }


    static void DrainAtOverrideRate(DecrementPhaseCounter_t original, CpPhaseTimer phase,
                                    void* self, std::uint32_t cpIndex, void* knowledge)
    {
        std::uint8_t* op = OperatorOfPhaseAi(self);
        const float overrideRate = OverrideRate(phase, cpIndex);
        float* rate = (overrideRate > 0.0f && HoldsCp(op, cpIndex)) ? CpRate(op, cpIndex, phase) : nullptr;
        if (!rate)
        {
            original(self, cpIndex, knowledge);
            return;
        }

        const float vanillaRate = *rate;
        *rate = overrideRate;
        original(self, cpIndex, knowledge);
        *rate = vanillaRate;
    }


    using TppCp2LuaCommands_t = int(__fastcall*)(unsigned long long, unsigned long long, unsigned int, unsigned int);
    static TppCp2LuaCommands_t g_OrigTppCp2LuaCommands = nullptr;

    static int __fastcall hkTppCp2LuaCommands(unsigned long long luaCommands, unsigned long long args,
                                              unsigned int cpIndex, unsigned int commandHash)
    {
        if (luaCommands)
            g_Operator.store(reinterpret_cast<std::uint8_t*>(luaCommands) + kLuaCommandsOperator, std::memory_order_release);

        const int results = g_OrigTppCp2LuaCommands
            ? g_OrigTppCp2LuaCommands(luaCommands, args, cpIndex, commandHash)
            : 0;

        if (t_CpCaptureArmed)
        {
            t_CpCaptureArmed = false;
            t_CapturedCpIndex = cpIndex;
        }
        return results;   // GameObject.SendCommand gives this to Lua as its number of results
    }

    static void __fastcall hkCautionDecrementPhaseCounter(void* self, std::uint32_t cpIndex, void* knowledge)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigCautionDecrement, self, cpIndex, knowledge);
        ReplaceVanillaCautionDrainIfNeeded(self, cpIndex, knowledge);
    }

    static void __fastcall hkEvasionDecrementPhaseCounter(void* self, std::uint32_t cpIndex, void* knowledge)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigEvasionDecrement, self, cpIndex, knowledge);
        DrainAtOverrideRate(g_OrigEvasionDecrement, CpPhaseTimer::Evasion, self, cpIndex, knowledge);
    }

    static void __fastcall hkAlertDecrementPhaseCounter(void* self, std::uint32_t cpIndex, void* knowledge)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigAlertDecrement, self, cpIndex, knowledge);
        DrainAtOverrideRate(g_OrigAlertDecrement, CpPhaseTimer::Alert, self, cpIndex, knowledge);
    }


    static const std::uint8_t kCautionDecrementPrologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18,
        0x48, 0x89, 0x7C, 0x24, 0x20, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x30 };
    static const std::uint8_t kEvasionDecrementPrologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x10, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
        0x48, 0x83, 0xEC, 0x40, 0x48, 0x8B, 0x79, 0x70, 0x8B, 0xEA };
    static const std::uint8_t kAlertDecrementPrologue[] = {
        0x4C, 0x8B, 0xDC, 0x55, 0x56, 0x57, 0x41, 0x55, 0x48, 0x81, 0xEC, 0xC8, 0x00, 0x00, 0x00,
        0x41, 0x0F, 0x29, 0x73, 0xB8, 0x41, 0x0F, 0x29, 0x7B, 0xA8 };

    static bool HookPhaseDrain(std::uintptr_t address, const std::uint8_t* prologue, std::size_t prologueSize,
                               const char* drainName, const char* lostFeature, void* detour, void** original)
    {
        if (address == 0)
        {
            LogError("CpPhaseTimer: %s address missing, %s off", drainName, lostFeature);
            return false;
        }

        void* target = ResolveGameAddress(address);
        const HookEntry entry = CheckHookEntry(target, prologue, prologueSize);
        if (entry == HookEntry::Unknown)
        {
            LogError("CpPhaseTimer: %s code at %p does not match %s, %s off",
                drainName, target, GetGameBuildName(gGameBuild), lostFeature);
            return false;
        }
        if (entry == HookEntry::ForeignHook)
        {
            char what[96] = {};
            std::snprintf(what, sizeof(what), "CpPhaseTimer: %s", drainName);
            LogHookedOnTop(what, target);
        }

        if (!CreateAndEnableHook(target, detour, original))
        {
            LogError("CpPhaseTimer: %s hook failed, %s off", drainName, lostFeature);
            return false;
        }
        return true;
    }
}


bool Install_CautionStepNormalTimerHook()
{
    const bool cautionOk = HookPhaseDrain(gAddr.DecrementPhaseCounter,
        kCautionDecrementPrologue, sizeof(kCautionDecrementPrologue),
        "CautionAiImpl::DecrementPhaseCounter", "SetCautionPhaseDuration",
        reinterpret_cast<void*>(&hkCautionDecrementPhaseCounter), reinterpret_cast<void**>(&g_OrigCautionDecrement));

    const bool evasionOk = HookPhaseDrain(gAddr.EvasionAiImpl_DecrementPhaseCounter,
        kEvasionDecrementPrologue, sizeof(kEvasionDecrementPrologue),
        "EvasionAiImpl::DecrementPhaseCounter", "SetEvasionPhaseDuration",
        reinterpret_cast<void*>(&hkEvasionDecrementPhaseCounter), reinterpret_cast<void**>(&g_OrigEvasionDecrement));

    const bool alertOk = HookPhaseDrain(gAddr.AlertAiImpl_DecrementPhaseCounter,
        kAlertDecrementPrologue, sizeof(kAlertDecrementPrologue),
        "AlertAiImpl::DecrementPhaseCounter", "SetAlertPhaseDuration",
        reinterpret_cast<void*>(&hkAlertDecrementPhaseCounter), reinterpret_cast<void**>(&g_OrigAlertDecrement));

    if (gAddr.TppCp2LuaCommands != 0)
    {
        static const std::uint8_t kCp2LuaCommandsPrologue[] = {
            0x48, 0x8B, 0xC4, 0x4C, 0x89, 0x48, 0x20, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57 };
        void* cp2 = ResolveGameAddress(gAddr.TppCp2LuaCommands);
        const HookEntry cp2Entry = CheckHookEntry(cp2, kCp2LuaCommandsPrologue, sizeof(kCp2LuaCommandsPrologue));
        if (cp2Entry == HookEntry::Unknown)
        {
            LogError("CpPhaseTimer: TppCp2LuaCommands code at %p does not match %s, per-CP phase durations off",
                cp2, GetGameBuildName(gGameBuild));
        }
        else
        {
            if (cp2Entry == HookEntry::ForeignHook)
                LogHookedOnTop("CpPhaseTimer: TppCommandPost2_LuaCommands", cp2);
            const bool ok2 = CreateAndEnableHook(
                cp2,
                reinterpret_cast<void*>(&hkTppCp2LuaCommands),
                reinterpret_cast<void**>(&g_OrigTppCp2LuaCommands));
            if (!ok2)
            {
                LogError("CpPhaseTimer: TppCp2LuaCommands hook failed at %p, per-CP phase durations off", cp2);
            }
        }
    }
    else
    {
        LogError("CpPhaseTimer: TppCp2LuaCommands address missing, per-CP phase durations off");
    }

    return cautionOk || evasionOk || alertOk;
}


bool Uninstall_CautionStepNormalTimerHook()
{
    if (g_OrigCautionDecrement)
    {
        DisableAndRemoveHook(ResolveGameAddress(gAddr.DecrementPhaseCounter));
        g_OrigCautionDecrement = nullptr;
    }
    if (g_OrigEvasionDecrement)
    {
        DisableAndRemoveHook(ResolveGameAddress(gAddr.EvasionAiImpl_DecrementPhaseCounter));
        g_OrigEvasionDecrement = nullptr;
    }
    if (g_OrigAlertDecrement)
    {
        DisableAndRemoveHook(ResolveGameAddress(gAddr.AlertAiImpl_DecrementPhaseCounter));
        g_OrigAlertDecrement = nullptr;
    }
    if (g_OrigTppCp2LuaCommands)
    {
        DisableAndRemoveHook(ResolveGameAddress(gAddr.TppCp2LuaCommands));
        g_OrigTppCp2LuaCommands = nullptr;
    }
    g_Operator.store(nullptr, std::memory_order_release);
    return true;
}


void Set_CpPhaseDurationSeconds(CpPhaseTimer phase, float seconds)
{
    PhaseDurationOverride& over = OverrideOf(phase);
    over.seconds = SanitizedSeconds(seconds);
    over.enabled = true;
}


void Unset_CpPhaseDurationSeconds(CpPhaseTimer phase)
{
    OverrideOf(phase).enabled = false;
}


float Get_CpPhaseDurationSeconds(CpPhaseTimer phase)
{
    const PhaseDurationOverride& over = OverrideOf(phase);
    if (over.enabled)
        return over.seconds;

    std::uint8_t* op = g_Operator.load(std::memory_order_acquire);
    const std::uint32_t count = op ? At<std::uint32_t>(op, kOperatorCpCount) : 0;
    if (count == 0)
        return kNoValue;
    const float rate = VanillaRate(op, 0, phase);
    for (std::uint32_t cp = 1; cp < count; ++cp)
    {
        if (VanillaRate(op, cp, phase) != rate)
            return kNoValue;
    }
    return rate > 0.0f ? 1.0f / rate : kNoValue;
}


void Set_CpPhaseDurationSecondsForCp(CpPhaseTimer phase, std::uint32_t cpIndex, float seconds)
{
    if (cpIndex >= kMaxCommandPosts)
        return;
    PhaseDurationOverride& over = OverrideOf(phase);
    over.cpSeconds[cpIndex] = SanitizedSeconds(seconds);
    over.cpEnabled[cpIndex] = true;
}


void Unset_CpPhaseDurationSecondsForCp(CpPhaseTimer phase, std::uint32_t cpIndex)
{
    if (cpIndex < kMaxCommandPosts)
        OverrideOf(phase).cpEnabled[cpIndex] = false;
}


float Get_CpPhaseDurationSecondsForCp(CpPhaseTimer phase, std::uint32_t cpIndex)
{
    const PhaseDurationOverride& over = OverrideOf(phase);
    if (cpIndex < kMaxCommandPosts && over.cpEnabled[cpIndex])
        return over.cpSeconds[cpIndex];
    if (over.enabled)
        return over.seconds;

    std::uint8_t* op = OperatorWithCp(cpIndex);
    const float rate = op ? VanillaRate(op, cpIndex, phase) : 0.0f;
    return rate > 0.0f ? 1.0f / rate : kNoValue;
}


float Get_CpPhaseRemainingSecondsForCp(CpPhaseTimer phase, std::uint32_t cpIndex)
{
    std::uint8_t* op = OperatorWithCp(cpIndex);
    std::uint8_t* state = op ? CpState(op, cpIndex) : nullptr;
    if (!state || At<std::int32_t>(state, kCpStatePhase) != PhaseIdOf(phase))
        return kNoValue;

    float rate = OverrideRate(phase, cpIndex);
    if (rate <= 0.0f)
        rate = VanillaRate(op, cpIndex, phase);
    if (rate <= 0.0f)
        return kNoValue;

    const float timer = At<float>(state, kCpStateTimer);
    return timer > 0.0f ? timer / rate : 0.0f;
}


void Clear_AllCpPhaseDurationOverrides()
{
    for (PhaseDurationOverride& over : g_Overrides)
    {
        over.enabled = false;
        for (bool& cpEnabled : over.cpEnabled)
            cpEnabled = false;
    }
    g_Operator.store(nullptr, std::memory_order_release);
}


void Arm_CpIndexCapture()
{
    t_CapturedCpIndex = kNoCpIndex;
    t_CpCaptureArmed = true;
}


std::uint32_t Take_CapturedCpIndex()
{
    t_CpCaptureArmed = false;
    return t_CapturedCpIndex;
}
