#include "pch.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>

#include "CureActionPluginImpl_StateCure.h"
#include "AddressSet.h"
#include "HookArena.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "NativeCallPatch.h"
#include "log.h"

namespace
{
    using GetStance_t = std::uint32_t(__fastcall*)(void* plugin, std::uint32_t index);

    constexpr std::uint32_t kStanceSquat = 2;
    constexpr std::uint32_t kStanceCrawl = 3; // face down
    constexpr std::uint32_t kStanceSupine = 4;
    constexpr std::uint32_t kSupineFlag = 0x100000; // the "lying on his back" bit in the action flags
    constexpr std::uint32_t kTrackedIndices = 4;
    constexpr std::size_t kCallSites = 4;
    constexpr std::size_t kThunkSize = 16;

    using SelectWakeupState_t = std::uint8_t(__fastcall*)(void* basicAction, std::uint32_t index);
    using BlendValuesOf_t = std::uintptr_t(__fastcall*)(void* inputs, std::uint32_t index);
    using FinishCureSprayEffect_t = void(__fastcall*)(void* plugin, std::uint32_t index);
    using StateCure_t = void(__fastcall*)(void* plugin, std::uint32_t index, std::uint32_t process, void* message);

    constexpr std::uint32_t kProcessUpdate = 0; // StateCure gets this every frame
    constexpr double kSprayStopSeconds = 208.0 / 60.0; // frame 208 of snapcue_q_bdy_cue_r, its stop-spraying event lost its frame so the game never fires it
    constexpr double kLongestFrameSeconds = 0.1; // a longer gap means the game was paused, count it as one frame
    constexpr std::uint8_t kRightFootForward = 0; // 0 gets him the _r idles, 1 the _l ones
    constexpr std::int32_t kInjuryTypeRightClips = 1; // the body cure states only pick their right-side clips at InjuryType 1, the game itself never writes 1

    constexpr std::uint8_t kStateCrawlIdle = 0x26;
    constexpr std::uint8_t kStateCrawlSupineIdle = 0x27;
    constexpr std::uint8_t kStateParallelCrawlIdle = 0x35; // the other face-down idle the game can wake into

    GetStance_t g_GetStance = nullptr;
    FinishCureSprayEffect_t g_FinishCureSprayEffect = nullptr;
    StateCure_t g_OrigStateCure = nullptr;
    void* g_StateCureTarget = nullptr;
    SelectWakeupState_t g_OrigSelectWakeupState = nullptr;
    void* g_SelectWakeupStateTarget = nullptr;
    std::atomic<bool> g_SupineCure[kTrackedIndices] = {};
    std::atomic<bool> g_WakeSupine[kTrackedIndices] = {};
    std::atomic<bool> g_RightSideCure[kTrackedIndices] = {};
    std::int32_t g_InjuryTypeBeforeCure[kTrackedIndices] = {};
    std::atomic<bool> g_SprayStopPending[kTrackedIndices] = {};
    double g_SprayClock[kTrackedIndices] = {};
    ULONGLONG g_SprayLastTick[kTrackedIndices] = {};
    NativeCallPatch g_Patches[kCallSites]{};
    bool g_Installed = false;

    std::uint8_t* FootForwardOf(void* plugin, std::uint32_t index)
    {
        __try
        {
            const std::uintptr_t owner = *reinterpret_cast<std::uintptr_t*>(reinterpret_cast<std::uintptr_t>(plugin) + 8); // grab the player from the plugin
            const std::uintptr_t holder = *reinterpret_cast<std::uintptr_t*>(owner + 0x78); // per-avatar stuff lives in here
            const std::uint32_t base = *reinterpret_cast<std::uint32_t*>(holder + 8); // index of the first avatar, need it to find ours
            const std::uintptr_t feet = *reinterpret_cast<std::uintptr_t*>(holder + 0x2F0); // one byte per avatar: which foot is forward, footstep events keep it updated
            return feet ? reinterpret_cast<std::uint8_t*>(feet + (index - base)) : nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    bool RightFootForward(void* plugin, std::uint32_t index)
    {
        const std::uint8_t* foot = FootForwardOf(plugin, index);
        __try
        {
            return foot && *foot == kRightFootForward;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void PutRightFootForward(void* plugin, std::uint32_t index)
    {
        std::uint8_t* foot = FootForwardOf(plugin, index);
        __try
        {
            if (foot)
                *foot = kRightFootForward;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    bool SwapInjuryType(void* plugin, std::uint32_t index, std::int32_t value, std::int32_t* previous)
    {
        __try
        {
            const std::uintptr_t owner = *reinterpret_cast<std::uintptr_t*>(reinterpret_cast<std::uintptr_t>(plugin) + 8); // grab the player from the plugin
            const std::uintptr_t inputs = *reinterpret_cast<std::uintptr_t*>(*reinterpret_cast<std::uintptr_t*>(owner + 0x78) + 0x260); // the values the player feeds its motion graph
            const auto valuesOf = *reinterpret_cast<BlendValuesOf_t*>(*reinterpret_cast<std::uintptr_t*>(inputs)); // first vtable entry hands out one avatar's values
            const std::uintptr_t values = valuesOf(reinterpret_cast<void*>(inputs), index);
            if (!values)
                return false;
            auto* injuryType = reinterpret_cast<std::int32_t*>(values + 0x70); // the InjuryType input the body cure states switch on
            if (previous)
                *previous = *injuryType;
            *injuryType = value;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void KeepRightSideClips(void* plugin, std::uint32_t index)
    {
        if (index < kTrackedIndices && g_RightSideCure[index].load(std::memory_order_relaxed))
            SwapInjuryType(plugin, index, kInjuryTypeRightClips, nullptr);
    }

    std::uint32_t __fastcall StanceAtCureStart(void* plugin, std::uint32_t index)
    {
        const std::uint32_t stance = g_GetStance(plugin, index);
        if (index >= kTrackedIndices)
            return stance;
        g_SupineCure[index].store(stance == kStanceSupine, std::memory_order_relaxed);
        if (!g_RightSideCure[index].load(std::memory_order_relaxed) && !MissionCodeGuard::ShouldBypassHooks()
            && RightFootForward(plugin, index)
            && SwapInjuryType(plugin, index, kInjuryTypeRightClips, &g_InjuryTypeBeforeCure[index]))
            g_RightSideCure[index].store(true, std::memory_order_relaxed);
        return stance;
    }

    std::uint32_t StanceDuringCure(void* plugin, std::uint32_t index)
    {
        const std::uint32_t stance = g_GetStance(plugin, index);
        const bool supine = index < kTrackedIndices && g_SupineCure[index].load(std::memory_order_relaxed);
        const bool force = supine && stance == kStanceCrawl && !MissionCodeGuard::ShouldBypassHooks();
        return force ? kStanceSupine : stance;
    }

    std::uint32_t __fastcall StanceAtCureLoop(void* plugin, std::uint32_t index)
    {
        KeepRightSideClips(plugin, index);
        const std::uint32_t stance = StanceDuringCure(plugin, index);
        if (index < kTrackedIndices && g_RightSideCure[index].load(std::memory_order_relaxed) && stance <= kStanceSquat)
        {
            g_SprayClock[index] = 0.0;
            g_SprayLastTick[index] = GetTickCount64();
            g_SprayStopPending[index].store(true, std::memory_order_relaxed);
        }
        return stance;
    }

    bool KeepSupine(void* plugin, std::uint32_t index)
    {
        __try
        {
            const std::uintptr_t owner = *reinterpret_cast<std::uintptr_t*>(reinterpret_cast<std::uintptr_t>(plugin) + 8); // grab the player from the plugin
            const std::uintptr_t holder = *reinterpret_cast<std::uintptr_t*>(owner + 0x78); // per-avatar stuff lives in here
            const std::uint32_t base = *reinterpret_cast<std::uint32_t*>(holder + 8); // index of the first avatar, need it to find ours
            const std::uintptr_t flags = *reinterpret_cast<std::uintptr_t*>(holder + 0x280); // every avatar gets 0x20 bytes of action flags
            const std::uintptr_t life = *reinterpret_cast<std::uintptr_t*>(*reinterpret_cast<std::uintptr_t*>(owner + 0x138) + 0x60); // player status, then its life state
            const std::uintptr_t stances = *reinterpret_cast<std::uintptr_t*>(life + 0x30); // one stance byte per avatar, the same one GetStance reads
            if (!flags || !stances)
                return false;
            *reinterpret_cast<std::uint32_t*>(flags + static_cast<std::uintptr_t>(index - base) * 0x20 + 0xC) |= kSupineFlag; // put the bit back, the lying state wiped it when the cure started
            *reinterpret_cast<std::uint8_t*>(stances + index) = static_cast<std::uint8_t>(kStanceSupine);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint32_t __fastcall StanceAtCureEnd(void* plugin, std::uint32_t index)
    {
        if (index < kTrackedIndices && g_RightSideCure[index].load(std::memory_order_relaxed))
        {
            KeepRightSideClips(plugin, index);
            PutRightFootForward(plugin, index); // the game just marked the left foot forward, but the _r end clip leaves him on the right one
        }
        const std::uint32_t stance = StanceDuringCure(plugin, index);
        if (index < kTrackedIndices)
        {
            g_SupineCure[index].store(false, std::memory_order_relaxed);
            g_SprayStopPending[index].store(false, std::memory_order_relaxed);
        }
        if (stance == kStanceSupine)
        {
            if (!KeepSupine(plugin, index))
                LogWarn("CureMotion: index %u could not keep the supine stance after the cure", index);
            if (index < kTrackedIndices)
                g_WakeSupine[index].store(true, std::memory_order_relaxed);
        }
        return stance;
    }

    void __fastcall hkStateCure(void* plugin, std::uint32_t index, std::uint32_t process, void* message)
    {
        g_OrigStateCure(plugin, index, process, message);
        if (process != kProcessUpdate || index >= kTrackedIndices || !g_SprayStopPending[index].load(std::memory_order_relaxed))
            return;
        const ULONGLONG now = GetTickCount64();
        double step = static_cast<double>(now - g_SprayLastTick[index]) / 1000.0;
        if (step > kLongestFrameSeconds)
            step = 1.0 / 60.0;
        g_SprayLastTick[index] = now;
        g_SprayClock[index] += step;
        if (g_SprayClock[index] < kSprayStopSeconds)
            return;
        g_SprayStopPending[index].store(false, std::memory_order_relaxed);
        g_FinishCureSprayEffect(plugin, index);
    }

    std::uint8_t __fastcall hkSelectWakeupState(void* basicAction, std::uint32_t index)
    {
        const std::uint8_t state = g_OrigSelectWakeupState(basicAction, index);
        if (index >= kTrackedIndices)
            return state;
        if (g_RightSideCure[index].exchange(false, std::memory_order_relaxed))
            SwapInjuryType(basicAction, index, g_InjuryTypeBeforeCure[index], nullptr);
        if (!g_WakeSupine[index].exchange(false, std::memory_order_relaxed))
            return state;
        const bool face = state == kStateCrawlIdle || state == kStateParallelCrawlIdle;
        const bool force = face && !MissionCodeGuard::ShouldBypassHooks();
        return force ? kStateCrawlSupineIdle : state;
    }

    void* MakeThunk(void* site, void* function)
    {
        auto* thunk = static_cast<std::uint8_t*>(HookArena::AllocateNear(reinterpret_cast<std::uintptr_t>(site), kThunkSize));
        if (!thunk)
            return nullptr;
        const std::uint64_t target = reinterpret_cast<std::uint64_t>(function);
        std::memset(thunk, 0xCC, kThunkSize); // int3 filler so a stray jump traps right away
        thunk[0] = 0x48; // mov rax, <our function>
        thunk[1] = 0xB8;
        std::memcpy(thunk + 2, &target, sizeof(target));
        thunk[10] = 0xFF; // jmp rax
        thunk[11] = 0xE0;
        FlushInstructionCache(GetCurrentProcess(), thunk, kThunkSize);
        return thunk;
    }

    bool Calls(const std::uint8_t* site, const void* function)
    {
        if (site[0] != 0xE8) // call rel32
            return false;
        std::int32_t rel = 0;
        std::memcpy(&rel, site + 1, sizeof(rel));
        return reinterpret_cast<std::uintptr_t>(site) + 5 + rel == reinterpret_cast<std::uintptr_t>(function);
    }
}

bool Install_CureActionPluginImpl_StateCure_Hook()
{
    if (g_Installed)
        return true;
    g_GetStance = reinterpret_cast<GetStance_t>(ResolveGameAddress(gAddr.ActionPluginImpl_GetStance));
    g_FinishCureSprayEffect = reinterpret_cast<FinishCureSprayEffect_t>(ResolveGameAddress(gAddr.CureActionPluginImpl_FinishCureSprayEffect));
    void* sites[kCallSites] = {
        ResolveGameAddress(gAddr.CureActionPluginImpl_StateCureStart_StanceCallBody),
        ResolveGameAddress(gAddr.CureActionPluginImpl_StateCureStart_StanceCallArm),
        ResolveGameAddress(gAddr.CureActionPluginImpl_StateCure_StanceCall),
        ResolveGameAddress(gAddr.CureActionPluginImpl_StateCureEnd_StanceCall),
    };
    if (!g_GetStance || !g_FinishCureSprayEffect)
        return true;
    const void* callees[kCallSites] = { g_GetStance, g_GetStance, g_GetStance, g_GetStance };
    for (std::size_t i = 0; i < kCallSites; ++i)
    {
        void* site = sites[i];
        if (!site)
            return true;
        if (!Calls(static_cast<const std::uint8_t*>(site), callees[i]))
        {
            LogError("CureMotion: unexpected code at %p, cure motion changes off", site);
            return true;
        }
    }
    void* start = MakeThunk(sites[0], reinterpret_cast<void*>(&StanceAtCureStart));
    void* loop = MakeThunk(sites[0], reinterpret_cast<void*>(&StanceAtCureLoop));
    void* end = MakeThunk(sites[0], reinterpret_cast<void*>(&StanceAtCureEnd));
    if (!start || !loop || !end)
    {
        LogError("CureMotion: could not allocate the stance thunks, supine cure motion off");
        return true;
    }
    void* destinations[kCallSites] = { start, start, loop, end };
    for (std::size_t i = 0; i < kCallSites; ++i)
    {
        g_Patches[i] = {};
        g_Patches[i].target = sites[i];
        g_Patches[i].destination = destinations[i];
        g_Patches[i].size = 5;
        std::memcpy(g_Patches[i].expected, sites[i], 5);
    }
    for (int attempt = 0; attempt < 50; ++attempt)
    {
        if (InstallNativeCallPatches(g_Patches, kCallSites))
        {
            g_Installed = true;
            void* wake = ResolveGameAddress(gAddr.BasicActionImpl_SelectWakeupState);
            if (wake && CreateAndEnableHook(wake, reinterpret_cast<void*>(&hkSelectWakeupState),
                                            reinterpret_cast<void**>(&g_OrigSelectWakeupState)))
                g_SelectWakeupStateTarget = wake;
            else if (wake)
                LogError("CureMotion: hooking the wake state selection failed, supine stance after the cure off");
            void* cure = ResolveGameAddress(gAddr.CureActionPluginImpl_StateCure);
            if (cure && CreateAndEnableHook(cure, reinterpret_cast<void*>(&hkStateCure), reinterpret_cast<void**>(&g_OrigStateCure)))
                g_StateCureTarget = cure;
            else if (cure)
                LogError("CureMotion: hooking the cure loop state failed, the right-side spray will not stop");
            return true;
        }
        if (!NativePatchErrorIsTransient(GetLastError()))
            break;
        Sleep(1);
    }
    LogError("CureMotion: patching the cure stance calls failed (%s), supine cure motion off",
             NativePatchErrorName(GetLastError()));
    return true;
}

void Uninstall_CureActionPluginImpl_StateCure_Hook()
{
    if (g_StateCureTarget)
    {
        DisableAndRemoveHook(g_StateCureTarget);
        g_StateCureTarget = nullptr;
        g_OrigStateCure = nullptr;
    }
    for (auto& pending : g_SprayStopPending)
        pending.store(false, std::memory_order_relaxed);
    if (g_SelectWakeupStateTarget)
    {
        DisableAndRemoveHook(g_SelectWakeupStateTarget);
        g_SelectWakeupStateTarget = nullptr;
        g_OrigSelectWakeupState = nullptr;
    }
    for (auto& wake : g_WakeSupine)
        wake.store(false, std::memory_order_relaxed);
    for (auto& right : g_RightSideCure)
        right.store(false, std::memory_order_relaxed);
    if (g_Installed)
    {
        for (int attempt = 0; attempt < 50; ++attempt)
        {
            if (RemoveNativeCallPatches(g_Patches, kCallSites))
                break;
            if (!NativePatchErrorIsTransient(GetLastError()))
                break;
            Sleep(1);
        }
        g_Installed = false;
    }
    for (auto& supine : g_SupineCure)
        supine.store(false, std::memory_order_relaxed);
}
