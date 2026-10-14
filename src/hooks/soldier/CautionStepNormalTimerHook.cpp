#include "pch.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>

#include "HookUtils.h"
#include "log.h"
#include "MissionCodeGuard.h"
#include "AddressSet.h"
#include "FoxHashes.h"
#include "CautionStepNormalTimerHook.h"

namespace
{
    using DecrementPhaseCounter_t = void(__fastcall*)(void* self, std::uint32_t cpIndex, void* knowledge);


    static DecrementPhaseCounter_t g_OrigDecrementPhaseCounter = nullptr;


    static float g_DurationSeconds = 60.0f;


    static float g_NormalizedDrainRate = 0.0f;

    static bool g_EnableOverride = false;

    static constexpr unsigned kMaxCommandPosts = 256;
    static bool  g_CpEnable[kMaxCommandPosts]          = {};
    static float g_CpDurationSeconds[kMaxCommandPosts] = {};
    static float g_CpNormalizedDrain[kMaxCommandPosts] = {};

    static constexpr std::size_t  kCautionAiOperator   = 0x70;
    static constexpr std::size_t  kLuaCommandsOperator = 0x10;   // the CP Lua command interface sits 0x10 past the Cp2Operator
    static constexpr std::size_t  kOperatorCpParams    = 0x28;   // 0x14 per CP
    static constexpr std::size_t  kCpParamSize         = 0x14;
    static constexpr std::size_t  kCpParamCautionRate  = 0x04;   // timer drained per second, the timer starting at 1.0
    static constexpr std::size_t  kOperatorCpStates    = 0x30;   // 0x60 per CP
    static constexpr std::size_t  kCpStateSize         = 0x60;
    static constexpr std::size_t  kCpStatePhase        = 0x48;
    static constexpr std::size_t  kCpStateTimer        = 0x50;
    static constexpr std::size_t  kCpStateFlags        = 0x54;
    static constexpr std::size_t  kOperatorFrameTime   = 0x158;
    static constexpr std::size_t  kOperatorCpCount     = 0x160;
    static constexpr std::int32_t kPhaseCaution        = 1;
    static constexpr float        kNoValue             = -1.0f;

    static std::atomic<std::uint8_t*> g_Operator{ nullptr };   // Cp2Operator, re-read every tick and every CP command

    template <typename T>
    static T& At(std::uint8_t* base, std::size_t offset)
    {
        return *reinterpret_cast<T*>(base + offset);
    }

    static float EffectiveNormalizedDrain(std::uint32_t cpIndex)
    {
        if (cpIndex < kMaxCommandPosts && g_CpEnable[cpIndex] && g_CpNormalizedDrain[cpIndex] > 0.0f)
            return g_CpNormalizedDrain[cpIndex];
        if (g_EnableOverride && g_NormalizedDrainRate > 0.0f)
            return g_NormalizedDrainRate;
        return 0.0f;
    }

    static float DurationSecondsToNormalizedDrain(float seconds)
    {
        if (seconds <= 0.0f)
            return 1.0f;

        return 1.0f / seconds;
    }

    static void SyncCautionStepNormalDrainFromDuration()
    {
        if (g_DurationSeconds <= 0.0f)
            g_DurationSeconds = 1.0f;

        g_NormalizedDrainRate = DurationSecondsToNormalizedDrain(g_DurationSeconds);
    }

    static std::uint8_t* OperatorWithCp(std::uint32_t cpIndex)
    {
        std::uint8_t* op = g_Operator.load(std::memory_order_acquire);
        if (!op || cpIndex >= At<std::uint32_t>(op, kOperatorCpCount))
            return nullptr;
        return op;
    }

    static float VanillaCautionRate(std::uint8_t* op, std::uint32_t cpIndex)
    {
        std::uint8_t* params = At<std::uint8_t*>(op, kOperatorCpParams);
        return params ? At<float>(params, cpIndex * kCpParamSize + kCpParamCautionRate) : 0.0f;
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


    static void ReplaceVanillaDrainIfNeeded(void* self, std::uint32_t cpIndex, void* knowledge)
    {
        std::uint8_t* op = self ? At<std::uint8_t*>(static_cast<std::uint8_t*>(self), kCautionAiOperator) : nullptr;
        if (op)
            g_Operator.store(op, std::memory_order_release);

        std::uint8_t* state = op ? CpState(op, cpIndex) : nullptr;
        if (!state || !knowledge)
        {
            g_OrigDecrementPhaseCounter(self, cpIndex, knowledge);
            return;
        }

        float& timer = At<float>(state, kCpStateTimer);
        std::uint32_t& flags = At<std::uint32_t>(state, kCpStateFlags);
        const float beforeTimer = timer;
        const std::uint32_t beforeFlags = flags;
        const std::uint8_t knowledgeFlagsBefore = GetKnowledgeFlags(knowledge);
        const float deltaScale = At<float>(op, kOperatorFrameTime);


        g_OrigDecrementPhaseCounter(self, cpIndex, knowledge);

        const float vanillaDrain = beforeTimer - timer;


        const float overrideDrain = EffectiveNormalizedDrain(cpIndex);
        if (overrideDrain <= 0.0f)
            return;


        if ((knowledgeFlagsBefore & 0x80u) == 0u)   // vanilla only drains on this flag, else it ends caution outright
            return;


        if (beforeTimer <= 0.0f || vanillaDrain <= 0.000001f || deltaScale <= 0.0f)   // kept in caution, or paused
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


    static bool  g_PendingCpActive   = false;
    static float g_PendingCpDuration = 0.0f;

    static bool          g_PendingCpCapture = false;
    static std::uint32_t g_CapturedCpIndex  = 0xFFFFFFFFu;

    using TppCp2LuaCommands_t = void(__fastcall*)(unsigned long long, unsigned long long, unsigned int, unsigned int);
    static TppCp2LuaCommands_t   g_OrigTppCp2LuaCommands       = nullptr;
    static std::uint32_t         g_HashSetCautionPhaseDuration = 0;
    static std::uintptr_t Addr_TppCp2LuaCommands()
    {
        using GB = AddressSetRuntime::GameBuild;
        switch (gGameBuild)
        {
        case GB::En_1_0_15_4:
        case GB::En_1_0_15_4a: return 0x140D5DF30ull;
        case GB::En_1_0_15_3:  return 0x140D5E070ull;
        case GB::Jp_1_0_15_4:
        case GB::Jp_1_0_15_4a: return 0x140D5DEF0ull;
        case GB::Jp_1_0_15_3:  return 0x140D5DDE0ull;
        default:               return 0;
        }
    }

    static void __fastcall hkTppCp2LuaCommands(unsigned long long p1, unsigned long long p2,
                                               unsigned int p3, unsigned int cmdHash)
    {
        if (p1)
            g_Operator.store(reinterpret_cast<std::uint8_t*>(p1) + kLuaCommandsOperator, std::memory_order_release);

        if (g_OrigTppCp2LuaCommands)
            g_OrigTppCp2LuaCommands(p1, p2, p3, cmdHash);

        if (cmdHash == g_HashSetCautionPhaseDuration && g_PendingCpActive)
        {
            g_PendingCpActive = false;
            Set_CautionStepNormalDurationSecondsForCp(p3, g_PendingCpDuration);
        }

        if (g_PendingCpCapture)
        {
            g_PendingCpCapture = false;
            g_CapturedCpIndex = p3;
        }
    }

    static void __fastcall hkDecrementPhaseCounter(void* self, std::uint32_t cpIndex, void* knowledge)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigDecrementPhaseCounter, self, cpIndex, knowledge);
        ReplaceVanillaDrainIfNeeded(self, cpIndex, knowledge);
    }
}


bool Install_CautionStepNormalTimerHook()
{
    SyncCautionStepNormalDrainFromDuration();

    void* target = ResolveGameAddress(gAddr.DecrementPhaseCounter);
    if (!target)
    {
        LogError("CautionPhaseTimer: DecrementPhaseCounter address missing, caution duration override off");
        return false;
    }

    const bool ok = CreateAndEnableHook(
        target,
        reinterpret_cast<void*>(&hkDecrementPhaseCounter),
        reinterpret_cast<void**>(&g_OrigDecrementPhaseCounter)
    );

    if (!ok)
        LogError("CautionPhaseTimer: DecrementPhaseCounter hook failed, caution duration override off");

    g_HashSetCautionPhaseDuration = FoxHashes::StrCode32("SetCautionPhaseDuration");
    if (Addr_TppCp2LuaCommands() != 0)
    {
        void* cp2 = ResolveGameAddress(Addr_TppCp2LuaCommands());
        const bool ok2 = cp2 && CreateAndEnableHook(
            cp2,
            reinterpret_cast<void*>(&hkTppCp2LuaCommands),
            reinterpret_cast<void**>(&g_OrigTppCp2LuaCommands));
        if (!ok2)
        {
            LogError("CautionPhaseTimer: TppCp2LuaCommands hook failed at %p, per-CP caution duration and CP capture off", cp2);
        }
    }
    else
    {
        LogError("CautionPhaseTimer: TppCp2LuaCommands address missing, per-CP caution duration and CP capture off");
    }

    return ok;
}


bool Uninstall_CautionStepNormalTimerHook()
{
    DisableAndRemoveHook(ResolveGameAddress(gAddr.DecrementPhaseCounter));
    g_OrigDecrementPhaseCounter = nullptr;
    if (g_OrigTppCp2LuaCommands)
    {
        DisableAndRemoveHook(ResolveGameAddress(Addr_TppCp2LuaCommands()));
        g_OrigTppCp2LuaCommands = nullptr;
    }
    g_Operator.store(nullptr, std::memory_order_release);
    return true;
}


void Set_CautionStepNormalDurationSeconds(float seconds)
{
    g_DurationSeconds = seconds;
    SyncCautionStepNormalDrainFromDuration();
    g_EnableOverride = true;
}


float Get_CautionStepNormalDurationSeconds()
{
    if (g_EnableOverride)
        return g_DurationSeconds;

    std::uint8_t* op = g_Operator.load(std::memory_order_acquire);
    const std::uint32_t count = op ? At<std::uint32_t>(op, kOperatorCpCount) : 0;
    if (count == 0)
        return kNoValue;
    const float rate = VanillaCautionRate(op, 0);
    for (std::uint32_t cp = 1; cp < count; ++cp)
    {
        if (VanillaCautionRate(op, cp) != rate)
            return kNoValue;   // the CPs differ: only one CP has a length
    }
    return rate > 0.0f ? 1.0f / rate : kNoValue;
}


void Unset_CautionStepNormalDurationSeconds()
{
    g_EnableOverride = false;
}


void Set_CautionStepNormalDurationSecondsForCp(std::uint32_t cpIndex, float seconds)
{
    if (cpIndex >= kMaxCommandPosts)
        return;
    if (seconds <= 0.0f)
        seconds = 1.0f;
    g_CpDurationSeconds[cpIndex] = seconds;
    g_CpNormalizedDrain[cpIndex] = 1.0f / seconds;
    g_CpEnable[cpIndex] = true;
}


void Unset_CautionStepNormalDurationSecondsForCp(std::uint32_t cpIndex)
{
    if (cpIndex < kMaxCommandPosts)
        g_CpEnable[cpIndex] = false;
}


void Clear_AllCautionStepNormalDurationOverrides()
{
    g_EnableOverride = false;
    for (unsigned i = 0; i < kMaxCommandPosts; ++i)
        g_CpEnable[i] = false;
    g_PendingCpActive  = false;
    g_PendingCpCapture = false;
    g_Operator.store(nullptr, std::memory_order_release);   // the next mission builds its own
}


void Set_PendingCautionDurationForCp(float seconds)
{
    g_PendingCpDuration = seconds;
    g_PendingCpActive = true;
}


float Get_CautionStepNormalRemainingSeconds()
{
    return kNoValue;   // only a single CP has a remaining time
}


void Arm_CautionCpCapture()
{
    g_CapturedCpIndex = 0xFFFFFFFFu;
    g_PendingCpCapture = true;
}


std::uint32_t Take_CautionCpIndex()
{
    g_PendingCpCapture = false;
    return g_CapturedCpIndex;
}


float Get_CautionStepNormalDurationSecondsForCp(std::uint32_t cpIndex)
{
    if (cpIndex < kMaxCommandPosts && g_CpEnable[cpIndex])
        return g_CpDurationSeconds[cpIndex];
    if (g_EnableOverride)
        return g_DurationSeconds;

    std::uint8_t* op = OperatorWithCp(cpIndex);
    const float rate = op ? VanillaCautionRate(op, cpIndex) : 0.0f;
    return rate > 0.0f ? 1.0f / rate : kNoValue;
}


float Get_CautionStepNormalRemainingSecondsForCp(std::uint32_t cpIndex)
{
    std::uint8_t* op = OperatorWithCp(cpIndex);
    std::uint8_t* state = op ? CpState(op, cpIndex) : nullptr;
    if (!state || At<std::int32_t>(state, kCpStatePhase) != kPhaseCaution)
        return kNoValue;

    float rate = EffectiveNormalizedDrain(cpIndex);
    if (rate <= 0.0f)
        rate = VanillaCautionRate(op, cpIndex);
    if (rate <= 0.0f)
        return kNoValue;

    const float timer = At<float>(state, kCpStateTimer);
    return timer > 0.0f ? timer / rate : 0.0f;
}
