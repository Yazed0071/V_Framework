#include "pch.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>

#include "AccIDroidPutAway.h"
#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using Request_t = std::uint64_t(__fastcall*)(void* motion, std::uint32_t index, std::uint32_t layer,
                                                 std::uint64_t tag, float blend, std::uint32_t flags);
    using CloseState_t = std::uint64_t(__fastcall*)(void* plugin, std::uint32_t index, std::uint32_t ev, void* msg);
    using Deactivate_t = std::uint64_t(__fastcall*)(void* iface, std::uint32_t index, std::uint64_t a3, std::uint64_t a4);
    using SetItemVisible_t = std::uint64_t(__fastcall*)(void* items, std::uint32_t handle, std::uint64_t visible, std::uint64_t a4);
    using PluginQuery_t = bool(__fastcall*)(void* plugin, std::uint32_t index);

    constexpr std::uint64_t kTagMask = 0xFFFFFFFFFFFFull;
    constexpr std::uint32_t kUpperBodyLayer = 2;
    constexpr std::uint64_t kIDroidAction = 0x8D647EA01654ull;
    constexpr std::uint64_t kIDroidHold = 0xCEB5EBCD6662ull;
    constexpr std::uint64_t kUpperBodyEmpty = 0xC1A2050D3802ull;
    constexpr std::uint64_t kIDroidPutAway = 0x7B19F8D7B13Eull;
    constexpr float kPutAwayBlend = 0.0f;
    constexpr float kReleaseBlend = 30.0f;
    constexpr float kIdleRestartBlend = 8.0f;
    constexpr std::uint32_t kBodyLayer = 0;
    constexpr ULONGLONG kPutAwayMs = 147 * 1000 / 60 - 250;
    constexpr ULONGLONG kEndGraceMs = 500;
    constexpr std::uint32_t kMaxEndSkips = 1500;
    constexpr std::uint32_t kTrackedIndices = 2;

    constexpr std::uint32_t kEvTick = 1;
    constexpr std::uint32_t kEvEnter = 2;
    constexpr std::uint32_t kEvEndCheck = 4;
    constexpr std::uint32_t kWorkStride = 0x60;
    constexpr std::uint32_t kSeatFlag = 0x100;
    constexpr std::uint32_t kIDroidItem = 0x1F3; // TppEquip.EQP_IT_IDroid
    constexpr std::uint32_t kItemType = 0x11;

    Request_t g_Orig160 = nullptr;
    Request_t g_Orig170 = nullptr;
    void** g_Slot160 = nullptr;
    void** g_Slot170 = nullptr;
    CloseState_t g_OrigCloseState = nullptr;
    Deactivate_t g_OrigDeactivate = nullptr;
    SetItemVisible_t g_OrigSetItemVisible = nullptr;
    PluginQuery_t g_IsPutAwayInterrupted = nullptr;
    PluginQuery_t g_IsOtherAvatar = nullptr;
    bool g_ItemHooks = false;
    bool g_Tried = false;
    std::atomic<bool> g_Enabled{false};

    bool g_Holding[kTrackedIndices] = {};
    void* g_Motion[kTrackedIndices] = {};
    void* g_SeatPlugin[kTrackedIndices] = {};
    ULONGLONG g_PlayingUntil[kTrackedIndices] = {};
    bool g_RestartPending[kTrackedIndices] = {};
    ULONGLONG g_RestartDeadline[kTrackedIndices] = {};
    std::uint32_t g_EndSkips[kTrackedIndices] = {};
    std::atomic<bool> g_IdleRestartRequested[kTrackedIndices] = {};

    thread_local std::int32_t t_ClosingIndex = -1;
    thread_local std::uint32_t t_ClosingHandle = 0;

    bool InHeliSpace()
    {
        return MissionCodeGuard::GetCurrentMissionCode() / 10000 == 4; // the game's own heli space test, so addon ACCs like 40011 count too
    }

    bool Tracked(std::uint32_t index, std::uint32_t layer)
    {
        return layer == kUpperBodyLayer && index < kTrackedIndices && InHeliSpace();
    }

    bool PuttingAway(std::uint32_t index)
    {
        return g_PlayingUntil[index] && GetTickCount64() < g_PlayingUntil[index];
    }

    void ResetIndex(std::uint32_t index)
    {
        g_Holding[index] = false;
        g_PlayingUntil[index] = 0;
        g_Motion[index] = nullptr;
        g_RestartPending[index] = false;
        g_RestartDeadline[index] = 0;
        g_EndSkips[index] = 0;
    }

    std::uintptr_t ReadPtr(std::uintptr_t p)
    {
        __try
        {
            return p ? *reinterpret_cast<std::uintptr_t*>(p) : 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    std::uint32_t ReadU32(std::uintptr_t p)
    {
        __try
        {
            return p ? *reinterpret_cast<std::uint32_t*>(p) : 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    std::uintptr_t WorkOf(std::uintptr_t list, std::uintptr_t info, std::uint32_t index)
    {
        if (!list || !info)
            return 0;
        const std::uint32_t base = ReadU32(info + 0x24);
        return list + static_cast<std::uintptr_t>(index - base) * kWorkStride;
    }

    std::uintptr_t PluginWork(std::uintptr_t plugin, std::uint32_t index)
    {
        return WorkOf(ReadPtr(plugin + 0x78), ReadPtr(plugin + 0x38), index);
    }

    bool HoldsIDroid(std::uintptr_t work)
    {
        const std::uint32_t handle = ReadU32(work + 0x18);
        return work && (handle & 0x1F) == kItemType && ((handle >> 5) & 0x7FF) == kIDroidItem;
    }

    bool ForcedEnd(std::uintptr_t plugin, std::uint32_t index)
    {
        const std::uintptr_t holder = ReadPtr(ReadPtr(plugin + 8) + 0x78);
        const std::uintptr_t flags = ReadPtr(holder + 0x280);
        if (!flags)
            return true;
        const std::uint32_t base = ReadU32(holder + 8);
        const std::uint32_t bits = ReadU32(flags + static_cast<std::uintptr_t>(index - base) * 0x20 + 0x18);
        return ((bits >> 4) & 1) != 0;
    }

    bool IsOtherAvatar(void* plugin, std::uint32_t index)
    {
        return g_IsOtherAvatar ? g_IsOtherAvatar(plugin, index) : index != 0;
    }

    int g_FaultStage = 0;
    DWORD g_FaultCode = 0;
    void* g_FaultAddr = nullptr;

    int CaptureFault(EXCEPTION_POINTERS* ep)
    {
        g_FaultCode = ep->ExceptionRecord->ExceptionCode;
        g_FaultAddr = ep->ExceptionRecord->ExceptionAddress;
        return EXCEPTION_EXECUTE_HANDLER;
    }

    std::uintptr_t MotionOf(std::uintptr_t seatPlugin)
    {
        return ReadPtr(ReadPtr(ReadPtr(seatPlugin + 8) + 0x78) + 0x238);
    }

    void RestartSeatIdle(std::uint32_t index, void* motion)
    {
        const std::uintptr_t self = reinterpret_cast<std::uintptr_t>(g_SeatPlugin[index]);
        if (!self || !motion || !g_Orig160 || !InHeliSpace())
            return;
        if (MotionOf(self) != reinterpret_cast<std::uintptr_t>(motion))
            return;
        std::uint32_t idxOff = 0;
        std::uint64_t seat = 0;
        __try
        {
            g_FaultStage = 1;
            const std::uintptr_t a38 = *reinterpret_cast<std::uintptr_t*>(self + 0x38);
            idxOff = static_cast<std::uint32_t>(*reinterpret_cast<int*>(a38 + 0x24)); // index of the first avatar
            g_FaultStage = 2;
            const std::uintptr_t plv = *reinterpret_cast<std::uintptr_t*>(self + 0x78) + static_cast<std::uintptr_t>(index - idxOff) * 0x1d0; // the seat plugin keeps 0x1D0 bytes per avatar
            seat = *reinterpret_cast<std::uint64_t*>(plv + 0x70); // the seat idle it last asked for
            if (!seat)
                return;
            g_FaultStage = 3;
            g_Orig160(motion, index, kBodyLayer, seat, kIdleRestartBlend, 0);
        }
        __except (CaptureFault(GetExceptionInformation()))
        {
            LogError("AccIDroidPutAway: restarting the seat idle faulted (index %u stage %d code 0x%08lX at %p idxOff %u seat 0x%012llX)",
                     index, g_FaultStage, g_FaultCode, g_FaultAddr, idxOff, seat & kTagMask);
        }
    }

    std::uint64_t __fastcall hk170(void* motion, std::uint32_t index, std::uint32_t layer,
                                   std::uint64_t tag, float blend, std::uint32_t flags)
    {
        if (Tracked(index, layer))
        {
            const std::uint64_t state = tag & kTagMask;
            const bool iDroid = state == kIDroidAction || state == kIDroidHold;
            if (iDroid && PuttingAway(index))
                return 0;
            g_PlayingUntil[index] = 0;
            g_RestartPending[index] = false;
            g_Holding[index] = iDroid;
        }
        return g_Orig170(motion, index, layer, tag, blend, flags);
    }

    std::uint64_t __fastcall hk160(void* motion, std::uint32_t index, std::uint32_t layer,
                                   std::uint64_t tag, float blend, std::uint32_t flags)
    {
        if (Tracked(index, layer))
        {
            const bool empty = (tag & kTagMask) == kUpperBodyEmpty;
            if (empty && g_PlayingUntil[index])
                return 0;
            const bool closing = empty && g_Holding[index] && g_Enabled.load(std::memory_order_relaxed);
            g_Holding[index] = false;
            g_PlayingUntil[index] = 0;
            if (closing)
            {
                g_Motion[index] = motion;
                g_RestartPending[index] = false;
                g_EndSkips[index] = 0;
                g_PlayingUntil[index] = GetTickCount64() + kPutAwayMs;
                return g_Orig170(motion, index, layer, kIDroidPutAway, kPutAwayBlend, flags);
            }
        }
        return g_Orig160(motion, index, layer, tag, blend, flags);
    }

    std::uint64_t __fastcall hkCloseState(void* plugin, std::uint32_t index, std::uint32_t ev, void* msg)
    {
        if (index < kTrackedIndices && InHeliSpace())
        {
            const std::uintptr_t p = reinterpret_cast<std::uintptr_t>(plugin);
            if (ev == kEvEnter)
            {
                const std::int32_t prevIndex = t_ClosingIndex;
                const std::uint32_t prevHandle = t_ClosingHandle;
                t_ClosingIndex = static_cast<std::int32_t>(index);
                t_ClosingHandle = ReadU32(PluginWork(p, index) + 0x18);
                const std::uint64_t result = g_OrigCloseState(plugin, index, ev, msg);
                t_ClosingIndex = prevIndex;
                t_ClosingHandle = prevHandle;
                return result;
            }
            if ((ev == kEvEndCheck || ev == kEvTick) && PuttingAway(index) && g_EndSkips[index] < kMaxEndSkips)
            {
                const std::uintptr_t work = PluginWork(p, index);
                if ((ReadU32(work + 0x50) & kSeatFlag) && HoldsIDroid(work) && !ForcedEnd(p, index)) // work + 0x50 holds the close flags
                {
                    if (ev == kEvEndCheck && g_IsOtherAvatar && g_IsPutAwayInterrupted
                        && !g_IsOtherAvatar(plugin, index) && g_IsPutAwayInterrupted(plugin, index))
                    {
                        for (std::uint32_t i = 0; i < kTrackedIndices; ++i)
                        {
                            if (i == index)
                                continue;
                            g_PlayingUntil[i] = 0;
                            g_RestartPending[i] = false;
                        }
                        return g_OrigCloseState(plugin, index, ev, msg);
                    }
                    ++g_EndSkips[index];
                    return 0;
                }
            }
        }
        return g_OrigCloseState(plugin, index, ev, msg);
    }

    std::uint64_t __fastcall hkSetItemVisible(void* items, std::uint32_t handle, std::uint64_t visible, std::uint64_t a4)
    {
        const std::int32_t i = t_ClosingIndex;
        if (i >= 0 && i < static_cast<std::int32_t>(kTrackedIndices) && !(visible & 0xFF) && g_PlayingUntil[i] && handle == t_ClosingHandle)
            visible = (visible & ~0xFFull) | 1;
        return g_OrigSetItemVisible(items, handle, visible, a4);
    }

    std::uint64_t __fastcall hkDeactivate(void* iface, std::uint32_t index, std::uint64_t a3, std::uint64_t a4)
    {
        bool mine = false;
        if (index < kTrackedIndices)
        {
            const std::uintptr_t f = reinterpret_cast<std::uintptr_t>(iface);
            mine = HoldsIDroid(WorkOf(ReadPtr(f + 0x48), ReadPtr(f + 8), index)); // called through the interface at +0x30, so every field sits 0x30 lower
            if (mine && !InHeliSpace())
            {
                ResetIndex(index);
                mine = false;
            }
            else if (mine && PuttingAway(index) && IsOtherAvatar(reinterpret_cast<void*>(f - 0x30), index)) // f - 0x30 is the plugin itself
            {
                mine = false;
            }
            else if (mine && g_PlayingUntil[index])
            {
                g_RestartPending[index] = !PuttingAway(index);
                g_PlayingUntil[index] = 0;
                g_Holding[index] = false;
            }
        }
        const std::uint64_t result = g_OrigDeactivate(iface, index, a3, a4);
        if (mine && g_RestartPending[index])
        {
            g_RestartPending[index] = false;
            RestartSeatIdle(index, g_Motion[index]);
        }
        return result;
    }

    void InstallItemHooks()
    {
        void* close = ResolveGameAddress(gAddr.IDroidActionPluginImpl_CloseState);
        void* deactivate = ResolveGameAddress(gAddr.IDroidActionPluginImpl_Deactivate);
        void* visible = ResolveGameAddress(gAddr.EquipSystemImpl_SetItemVisible);
        if (!close || !deactivate || !visible)
            return;
        g_IsPutAwayInterrupted = reinterpret_cast<PluginQuery_t>(ResolveGameAddress(gAddr.IDroidActionPluginImpl_IsPutAwayInterrupted));
        g_IsOtherAvatar = reinterpret_cast<PluginQuery_t>(ResolveGameAddress(gAddr.IDroidActionPluginImpl_IsOtherAvatar));
        if (!CreateAndEnableHook(visible, reinterpret_cast<void*>(&hkSetItemVisible), reinterpret_cast<void**>(&g_OrigSetItemVisible)))
        {
            LogError("AccIDroidPutAway: hooking SetItemVisible failed");
            return;
        }
        if (!CreateAndEnableHook(deactivate, reinterpret_cast<void*>(&hkDeactivate), reinterpret_cast<void**>(&g_OrigDeactivate)))
        {
            LogError("AccIDroidPutAway: hooking the iDroid plugin deactivate failed");
            DisableAndRemoveHook(visible);
            return;
        }
        if (!CreateAndEnableHook(close, reinterpret_cast<void*>(&hkCloseState), reinterpret_cast<void**>(&g_OrigCloseState)))
        {
            LogError("AccIDroidPutAway: hooking the iDroid close state failed");
            DisableAndRemoveHook(deactivate);
            DisableAndRemoveHook(visible);
            return;
        }
        g_ItemHooks = true;
    }

    void TryInstall(std::uintptr_t self)
    {
        g_Tried = true;
        __try
        {
            const std::uintptr_t owner = *reinterpret_cast<std::uintptr_t*>(self + 8);
            const std::uintptr_t holder = owner ? *reinterpret_cast<std::uintptr_t*>(owner + 0x78) : 0;
            const std::uintptr_t motion = holder ? *reinterpret_cast<std::uintptr_t*>(holder + 0x238) : 0; // the motion controller
            if (!motion)
                return;
            void** vt = *reinterpret_cast<void***>(motion);
            g_Slot160 = vt + 0x160 / 8; // request a motion right now
            g_Slot170 = vt + 0x170 / 8; // request a motion through the graph
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            LogError("AccIDroidPutAway: reading the motion controller faulted");
            return;
        }
        if (!SwapVtableSlot(g_Slot170, reinterpret_cast<void*>(&hk170), reinterpret_cast<void**>(&g_Orig170)))
        {
            LogError("AccIDroidPutAway: hooking the motion request failed");
            g_Slot170 = g_Slot160 = nullptr;
            return;
        }
        if (!SwapVtableSlot(g_Slot160, reinterpret_cast<void*>(&hk160), reinterpret_cast<void**>(&g_Orig160)))
        {
            LogError("AccIDroidPutAway: hooking the immediate motion request failed");
            g_Slot160 = nullptr;
            return;
        }
    }
}

void AccIDroidPutAway_Install()
{
    if (!g_ItemHooks)
        InstallItemHooks();
}

void AccIDroidPutAway_OnRideHeliPreMotion(void* self, unsigned int idx)
{
    if (!InHeliSpace())
        return;
    if (!g_Tried)
        TryInstall(reinterpret_cast<std::uintptr_t>(self));
    if (!g_Orig160 || idx >= kTrackedIndices)
        return;
    g_SeatPlugin[idx] = self;
    if (g_Motion[idx] && MotionOf(reinterpret_cast<std::uintptr_t>(self)) != reinterpret_cast<std::uintptr_t>(g_Motion[idx]))
        ResetIndex(idx);
    if (!g_Holding[idx] && g_IdleRestartRequested[idx].exchange(false, std::memory_order_acquire))
    {
        LogInfo("SetMotion: index %u seat idle restart (putting away %d, restart pending %d)",
                idx, g_PlayingUntil[idx] ? 1 : 0, g_RestartPending[idx] ? 1 : 0);
        if (!g_PlayingUntil[idx] && !g_RestartPending[idx])
            RestartSeatIdle(idx, reinterpret_cast<void*>(MotionOf(reinterpret_cast<std::uintptr_t>(self))));
    }
    const ULONGLONG now = GetTickCount64();
    if (g_PlayingUntil[idx] && g_Motion[idx] && now >= g_PlayingUntil[idx])
    {
        g_PlayingUntil[idx] = 0;
        if (g_ItemHooks)
        {
            g_RestartPending[idx] = true;
            g_RestartDeadline[idx] = now + kEndGraceMs;
            return;
        }
        g_Orig160(g_Motion[idx], idx, kUpperBodyLayer, kUpperBodyEmpty, kReleaseBlend, 0);
        RestartSeatIdle(idx, g_Motion[idx]);
        return;
    }
    if (g_RestartPending[idx] && now >= g_RestartDeadline[idx])
    {
        g_RestartPending[idx] = false;
        if (idx == 0)
            LogWarn("AccIDroidPutAway: index %u iDroid plugin did not end after the put-away; releasing the upper body directly", idx);
        g_Orig160(g_Motion[idx], idx, kUpperBodyLayer, kUpperBodyEmpty, kReleaseBlend, 0);
        RestartSeatIdle(idx, g_Motion[idx]);
    }
}

void AccIDroidPutAway_SetEnabled(bool enabled)
{
    g_Enabled.store(enabled, std::memory_order_relaxed);
}

void AccIDroidPutAway_RequestSeatIdleRestart()
{
    LogInfo("SetMotion: restart requested, mission %u", MissionCodeGuard::GetCurrentMissionCode());
    if (!InHeliSpace())
        return;
    for (auto& requested : g_IdleRestartRequested)
        requested.store(true, std::memory_order_release);
}

void AccIDroidPutAway_Uninstall()
{
    if (g_ItemHooks)
    {
        if (void* close = ResolveGameAddress(gAddr.IDroidActionPluginImpl_CloseState))
            DisableAndRemoveHook(close);
        if (void* deactivate = ResolveGameAddress(gAddr.IDroidActionPluginImpl_Deactivate))
            DisableAndRemoveHook(deactivate);
        if (void* visible = ResolveGameAddress(gAddr.EquipSystemImpl_SetItemVisible))
            DisableAndRemoveHook(visible);
    }
    RestoreVtableSlot(g_Slot160, reinterpret_cast<void*>(g_Orig160));
    RestoreVtableSlot(g_Slot170, reinterpret_cast<void*>(g_Orig170));
    g_ItemHooks = false;
    g_OrigCloseState = nullptr;
    g_OrigDeactivate = nullptr;
    g_OrigSetItemVisible = nullptr;
    g_Slot160 = nullptr;
    g_Slot170 = nullptr;
    g_Orig160 = nullptr;
    g_Orig170 = nullptr;
    g_Tried = false;
    for (std::uint32_t i = 0; i < kTrackedIndices; ++i)
    {
        ResetIndex(i);
        g_SeatPlugin[i] = nullptr;
        g_IdleRestartRequested[i].store(false, std::memory_order_relaxed);
    }
}
