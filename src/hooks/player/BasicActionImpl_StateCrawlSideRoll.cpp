#include "pch.h"
#include "BasicActionImpl_StateCrawlSideRoll.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <mutex>

#include "AddressSet.h"
#include "HookUtils.h"
#include "LuaBroadcaster.h"
#include "MissionCodeGuard.h"
#include "log.h"
#include "FoxHashes.h"

namespace
{
    using StateCrawlSideRoll_t = void(__fastcall*)(
        void* this_,
        std::uint32_t playerIndex,
        std::uint32_t phase,
        void* param3);

    static StateCrawlSideRoll_t g_OrigStateCrawlSideRoll = nullptr;

    // Game phases we care about.
    static constexpr std::uint32_t kRollStartPhase = 1u;
    static constexpr std::uint32_t kRollRollingPhase = 4u;
    static constexpr std::uint32_t kRollEndPhase = 2u;

    // If no state calls happen for this long, the chain/session is considered broken.
    static constexpr ULONGLONG kConsecutiveWindowMs = 1500ull;

    // Phase 4 fires every frame while rolling, so debounce it.
    // Tune this if Rolling emits too fast or too slow.
    static constexpr ULONGLONG kRollingRepeatMs = 500ull;

    // Set true only while debugging. This will spam your log every frame.
    static constexpr bool kVerboseEveryCallLog = false;

    static std::atomic<ULONGLONG> g_LastCallTickMs{ 0 };

    static std::mutex     g_RollMutex;
    static bool           g_IsRolling = false;
    static std::uint64_t  g_RollCount = 0;
    static ULONGLONG      g_LastRollingEmitMs = 0;

    static std::atomic<std::uint64_t> g_PhasesSeenMask{ 0 };
    static std::atomic<std::uint64_t> g_TotalCalls{ 0 };

    static constexpr std::size_t kActionInfoField = 0x38;
    static constexpr std::size_t kSlotOriginField = 0x24;
    static constexpr std::size_t kSlotBaseField = 0x1A0;
    static constexpr std::size_t kSlotStride = 0x390;
    static constexpr std::size_t kDirectionField = 0x2A9;

    static bool TryReadRollDirection(
        void* this_,
        std::uint32_t playerIndex,
        std::uint8_t& outDirection)
    {
        outDirection = 0;

        if (!this_)
            return false;

        __try
        {
            const std::uintptr_t base =
                reinterpret_cast<std::uintptr_t>(this_);

            void* actionInfo =
                *reinterpret_cast<void**>(base + kActionInfoField);

            if (!actionInfo)
                return false;

            const std::uint32_t slotOrigin =
                *reinterpret_cast<std::uint32_t*>(
                    reinterpret_cast<std::uintptr_t>(actionInfo) + kSlotOriginField);

            // Prevent unsigned underflow.
            if (playerIndex < slotOrigin)
                return false;

            const std::uintptr_t slotBase =
                *reinterpret_cast<std::uintptr_t*>(base + kSlotBaseField);

            if (!slotBase)
                return false;

            const std::uintptr_t slot =
                slotBase +
                static_cast<std::uintptr_t>(playerIndex - slotOrigin) * kSlotStride;

            outDirection =
                *reinterpret_cast<std::uint8_t*>(slot + kDirectionField);

            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
}

static const uint32_t RollPhaseToStringStrCode32(std::uint32_t phase)
{
    switch (phase)
    {
    case 1:
        return FoxHashes::StrCode32("Start");
    case 4:
        return FoxHashes::StrCode32("Rolling");
    case 2:
        return FoxHashes::StrCode32("End");
    default:
        return FoxHashes::StrCode32("Unknown");
    }
}

static void __fastcall hkStateCrawlSideRoll(
    void* this_,
    std::uint32_t playerIndex,
    std::uint32_t phase,
    void* param3)
{
    if (kVerboseEveryCallLog)
    {
        Log("[CrawlSideRoll] LOGGING: playerIndex=%u phase=%u param3=%p\n",
            playerIndex,
            phase,
            param3);
    }

    if (g_OrigStateCrawlSideRoll)
        g_OrigStateCrawlSideRoll(this_, playerIndex, phase, param3);

    const ULONGLONG nowMs =
        GetTickCount64();

    const ULONGLONG prevMs =
        g_LastCallTickMs.exchange(nowMs, std::memory_order_relaxed);

    const std::uint64_t totalCalls =
        g_TotalCalls.fetch_add(1, std::memory_order_relaxed) + 1ull;

    if (totalCalls == 1)
    {
        Log("[CrawlSideRoll] hook firing first call: playerIndex=%u phase=%u param3=%p\n",
            playerIndex,
            phase,
            param3);
    }

    if (phase < 64u)
    {
        const std::uint64_t bit = 1ull << phase;

        const std::uint64_t prev =
            g_PhasesSeenMask.fetch_or(bit, std::memory_order_relaxed);

        if ((prev & bit) == 0)
        {
            Log("[CrawlSideRoll] new phase observed: playerIndex=%u phase=%u param3=%p\n",
                playerIndex,
                phase,
                param3);
        }
    }

    if (MissionCodeGuard::ShouldBypassHooks())
        return;

    const bool isStart =
        phase == kRollStartPhase;

    const bool isRolling =
        phase == kRollRollingPhase;

    const bool isEnd =
        phase == kRollEndPhase;

    if (!isStart && !isRolling && !isEnd)
        return;

    std::uint32_t emitPhase = 0;
    std::uint64_t emitCount = 0;
    bool shouldEmit = false;

    {
        std::lock_guard<std::mutex> lock(g_RollMutex);

        const bool chainBroken =
            (prevMs == 0) || ((nowMs - prevMs) > kConsecutiveWindowMs);

        if (chainBroken)
        {
            g_IsRolling = false;
            g_RollCount = 0;
            g_LastRollingEmitMs = 0;
        }

        if (isStart)
        {
            g_IsRolling = true;
            g_RollCount = 1;
            g_LastRollingEmitMs = nowMs;

            emitPhase = kRollStartPhase;   // 1 = Start
            emitCount = g_RollCount;
            shouldEmit = true;
        }
        else if (isRolling && g_IsRolling)
        {
            // Phase 4 fires every frame, so only emit Rolling every kRollingRepeatMs.
            if (g_LastRollingEmitMs == 0 ||
                (nowMs - g_LastRollingEmitMs) >= kRollingRepeatMs)
            {
                ++g_RollCount;
                g_LastRollingEmitMs = nowMs;

                emitPhase = kRollRollingPhase; // 4 = Rolling
                emitCount = g_RollCount;
                shouldEmit = true;
            }
        }
        else if (isEnd)
        {
            // Emit final End event with the final count.
            if (g_IsRolling || g_RollCount > 0)
            {
                emitPhase = kRollEndPhase; // 2 = End
                emitCount = g_RollCount;
                shouldEmit = true;
            }

            g_IsRolling = false;
            g_LastRollingEmitMs = 0;
        }
    }

    if (!shouldEmit)
        return;

    std::uint8_t direction = 0;
    TryReadRollDirection(this_, playerIndex, direction);

    Log("[CrawlSideRoll] emit playerIndex=%u rollPhase=%u count=%llu direction=%u\n",
        playerIndex,
        emitPhase,
        static_cast<unsigned long long>(emitCount),
        static_cast<unsigned>(direction));



    // Args:
    //   playerIndex
    //   emitPhase: 1 = Start, 4 = Rolling, 2 = End StrCode32
    //   emitCount
    //   direction
    V_FrameWork::EmitMessage(
        "Player",
        "CrawlSideRoll",
        playerIndex,
        RollPhaseToStringStrCode32(emitPhase),
        emitCount,
        direction);
}

bool Install_BasicActionImpl_StateCrawlSideRoll_Hook()
{
    void* target = ResolveGameAddress(
        gAddr.BasicActionImpl_StateCrawlSideRoll);

    if (!target)
    {
        Log("[Hook] BasicActionImpl::StateCrawlSideRoll: address resolve failed\n");
        return false;
    }

    const bool ok = CreateAndEnableHook(
        target,
        reinterpret_cast<void*>(&hkStateCrawlSideRoll),
        reinterpret_cast<void**>(&g_OrigStateCrawlSideRoll));

    Log("[Hook] BasicActionImpl::StateCrawlSideRoll: %s\n",
        ok ? "OK" : "FAIL");

    return ok;
}


bool Uninstall_BasicActionImpl_StateCrawlSideRoll_Hook()
{
    DisableAndRemoveHook(ResolveGameAddress(
        gAddr.BasicActionImpl_StateCrawlSideRoll));

    g_OrigStateCrawlSideRoll = nullptr;

    {
        std::lock_guard<std::mutex> lock(g_RollMutex);

        g_IsRolling = false;
        g_RollCount = 0;
        g_LastRollingEmitMs = 0;
    }

    g_LastCallTickMs.store(0, std::memory_order_relaxed);
    g_PhasesSeenMask.store(0, std::memory_order_relaxed);
    g_TotalCalls.store(0, std::memory_order_relaxed);

    return true;
}