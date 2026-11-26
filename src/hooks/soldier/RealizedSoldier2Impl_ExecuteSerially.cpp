#include <pch.h>
#include "RealizedSoldier2Impl_ExecuteSerially.h"

#include <Windows.h>
#include <intrin.h>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "AddressSet.h"
#include "GameCamera.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using ExecuteSerially_t     = void(__fastcall*)(void* self);
    using StopFovaResources_t   = void(__fastcall*)(void* self, std::uint32_t soldierIndex, std::uint32_t instanceIndex);

    constexpr std::uint32_t kEverySoldier      = 0x1FF;
    constexpr std::uint8_t  kFree              = 0;
    constexpr std::uint8_t  kHeld              = 1;
    constexpr std::uint8_t  kArming            = 2;
    constexpr float         kFarDistanceSq     = 2500.0f;   // 50 m, the distance where soldiers may flip their NVG too
    constexpr std::size_t   kMostHiddenPerCall = 256;
    constexpr std::uint32_t kFovaChangeRequest = 1;

    const std::uint8_t kExecuteSeriallyEntry[] = { 0x40, 0x53, 0x56, 0x57, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x38 };
    const std::uint8_t kStopFovaEntry[]        = { 0x48, 0x8B, 0x89, 0x60, 0x02, 0x00, 0x00, 0x48, 0x85, 0xC9, 0x74, 0x08, 0x41, 0x8B, 0xD0, 0xE9 };

    ExecuteSerially_t     g_OrigExecuteSerially = nullptr;
    StopFovaResources_t   g_OrigStopFova        = nullptr;
    void*                 g_ExecuteSeriallyFn   = nullptr;
    void*                 g_StopFovaFn          = nullptr;
    std::atomic<bool>     g_Installed{ false };

    std::atomic<std::uint8_t> g_Hold[kEverySoldier];
    std::atomic<int>          g_HeldCount{ 0 };

    void Arm(std::uint32_t soldier)
    {
        if (g_Hold[soldier].exchange(kArming) == kFree)
            g_HeldCount.fetch_add(1);
    }

    void Confirm(std::uint32_t soldier)
    {
        std::uint8_t arming = kArming;
        g_Hold[soldier].compare_exchange_strong(arming, kHeld);
    }

    void Release(std::uint32_t soldier)
    {
        std::uint8_t held = kHeld;
        if (g_Hold[soldier].compare_exchange_strong(held, kFree))
            g_HeldCount.fetch_sub(1);
    }

    void Drop(std::uint32_t soldier)
    {
        if (g_Hold[soldier].exchange(kFree) != kFree)
            g_HeldCount.fetch_sub(1);
    }

    bool OutOfSight(const float* position)
    {
        if (!GameCamera::IsOnScreen(position, GameCamera::kSoldierRadius))
            return true;
        return GameCamera::DistanceSq(position) >= kFarDistanceSq;
    }

    std::size_t HideRequestsInSight(void* self, std::uint32_t** hidden)
    {
        std::size_t count = 0;
        __try
        {
            auto* realized = static_cast<std::uint8_t*>(self);
            std::uint8_t* soldiers = *reinterpret_cast<std::uint8_t**>(realized + 0xF8);
            const std::uint32_t first = *reinterpret_cast<std::uint32_t*>(realized + 0xAC);   // first instance this realizer owns
            const std::uint32_t total = *reinterpret_cast<std::uint32_t*>(realized + 0xB0);
            std::uint8_t* shared = *reinterpret_cast<std::uint8_t**>(soldiers + 0x18);
            const auto* instanceToSoldier = *reinterpret_cast<std::uint16_t**>(shared + 0x28);
            const auto* positions = *reinterpret_cast<float**>(shared + 0x20);                  // x, y, z, rotation per soldier
            std::uint8_t* fovaRecords = *reinterpret_cast<std::uint8_t**>(soldiers + 0x98);
            std::uint8_t* records = *reinterpret_cast<std::uint8_t**>(fovaRecords + 0x08);
            const std::uint32_t recordCount = *reinterpret_cast<std::uint32_t*>(fovaRecords + 0x10);

            for (std::uint32_t instance = first; instance < first + total && count < kMostHiddenPerCall; ++instance)
            {
                const std::uint32_t soldier = instanceToSoldier[instance];
                if (soldier >= kEverySoldier || soldier >= recordCount)
                    continue;
                const std::uint8_t state = g_Hold[soldier].load(std::memory_order_relaxed);
                if (state == kFree)
                    continue;
                auto* flags = reinterpret_cast<std::uint32_t*>(records + soldier * 0x70 + 0x64);   // 0x70 per soldier, bit 0 = fova change waiting
                if ((*flags & kFovaChangeRequest) == 0)
                {
                    Release(soldier);
                    continue;
                }
                if (OutOfSight(positions + soldier * 4))
                {
                    Release(soldier);
                    continue;
                }
                _InterlockedAnd(reinterpret_cast<volatile long*>(flags), ~static_cast<long>(kFovaChangeRequest));
                hidden[count++] = flags;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
        return count;
    }

    void __fastcall hkExecuteSerially(void* self)
    {
        if (g_HeldCount.load(std::memory_order_relaxed) <= 0 || MissionCodeGuard::ShouldBypassHooks())
        {
            g_OrigExecuteSerially(self);
            return;
        }

        std::uint32_t* hidden[kMostHiddenPerCall] = {};
        const std::size_t count = HideRequestsInSight(self, hidden);
        g_OrigExecuteSerially(self);
        for (std::size_t i = 0; i < count; ++i)
            _InterlockedOr(reinterpret_cast<volatile long*>(hidden[i]), static_cast<long>(kFovaChangeRequest));
    }

    void __fastcall hkStopAllFovaResourceManager(void* self, std::uint32_t soldierIndex, std::uint32_t instanceIndex)
    {
        if (soldierIndex < kEverySoldier && g_Hold[soldierIndex].load() == kArming)
            return;
        g_OrigStopFova(self, soldierIndex, instanceIndex);
    }

    bool HookAt(void* fn, const std::uint8_t* entry, std::size_t entrySize, void* detour, void** orig, const char* what)
    {
        const HookEntry state = CheckHookEntry(fn, entry, entrySize);
        if (state == HookEntry::Unknown)
        {
            char found[160] = {};
            DescribeFunctionEntry(fn, found, sizeof(found));
            LogError("%s is not the expected code (%s), applyOffScreen off", what, found);
            return false;
        }
        if (!CreateAndEnableHook(fn, detour, orig))
        {
            LogError("%s hook failed, applyOffScreen off", what);
            return false;
        }
        if (state == HookEntry::ForeignHook)
            LogHookedOnTop(what, fn);
        return true;
    }
}

void ChangeFovaOffScreen::BeginHold(bool everySoldier, std::uint32_t soldierIndex)
{
    if (!g_Installed.load())
        return;
    if (everySoldier)
    {
        for (std::uint32_t soldier = 0; soldier < kEverySoldier; ++soldier)
            Arm(soldier);
    }
    else if (soldierIndex < kEverySoldier)
    {
        Arm(soldierIndex);
    }
}

void ChangeFovaOffScreen::EndHold(bool everySoldier, std::uint32_t soldierIndex)
{
    if (everySoldier)
    {
        for (std::uint32_t soldier = 0; soldier < kEverySoldier; ++soldier)
            Confirm(soldier);
    }
    else if (soldierIndex < kEverySoldier)
    {
        Confirm(soldierIndex);
    }
}

void ChangeFovaOffScreen::Cancel(bool everySoldier, std::uint32_t soldierIndex)
{
    if (everySoldier)
    {
        for (std::uint32_t soldier = 0; soldier < kEverySoldier; ++soldier)
            Drop(soldier);
    }
    else if (soldierIndex < kEverySoldier)
    {
        Drop(soldierIndex);
    }
}

bool ChangeFovaOffScreen::AnyHeld()
{
    return g_HeldCount.load(std::memory_order_relaxed) > 0;
}

void ChangeFovaOffScreen::Clear()
{
    for (std::uint32_t soldier = 0; soldier < kEverySoldier; ++soldier)
        g_Hold[soldier].store(kFree);
    g_HeldCount.store(0);
}

bool Install_RealizedSoldier2Impl_ExecuteSerially_Hook()
{
    if (g_Installed.load())
        return true;
    if (!gAddr.RealizedSoldier2Impl_ExecuteSerially || !gAddr.RealizedSoldier2Impl_StopAllFovaResourceManager
        || !gAddr.GetQuarkSystemTable)
    {
        LogError("ChangeFovaOffScreen: address missing for this build, applyOffScreen off");
        return false;
    }

    void* executeSerially = ResolveGameAddress(gAddr.RealizedSoldier2Impl_ExecuteSerially);
    void* stopFova = ResolveGameAddress(gAddr.RealizedSoldier2Impl_StopAllFovaResourceManager);

    if (!HookAt(executeSerially, kExecuteSeriallyEntry, sizeof(kExecuteSeriallyEntry),
                reinterpret_cast<void*>(&hkExecuteSerially), reinterpret_cast<void**>(&g_OrigExecuteSerially),
                "ChangeFovaOffScreen: RealizedSoldier2Impl::ExecuteSerially"))
        return false;
    g_ExecuteSeriallyFn = executeSerially;

    if (!HookAt(stopFova, kStopFovaEntry, sizeof(kStopFovaEntry),
                reinterpret_cast<void*>(&hkStopAllFovaResourceManager), reinterpret_cast<void**>(&g_OrigStopFova),
                "ChangeFovaOffScreen: RealizedSoldier2Impl::StopAllFovaResourceManager"))
    {
        DisableAndRemoveHook(g_ExecuteSeriallyFn);
        g_ExecuteSeriallyFn = nullptr;
        g_OrigExecuteSerially = nullptr;
        return false;
    }
    g_StopFovaFn = stopFova;

    g_Installed.store(true);
    return true;
}

void Uninstall_RealizedSoldier2Impl_ExecuteSerially_Hook()
{
    g_Installed.store(false);
    ChangeFovaOffScreen::Clear();
    if (g_StopFovaFn)
    {
        DisableAndRemoveHook(g_StopFovaFn);
        g_StopFovaFn = nullptr;
    }
    if (g_ExecuteSeriallyFn)
    {
        DisableAndRemoveHook(g_ExecuteSeriallyFn);
        g_ExecuteSeriallyFn = nullptr;
    }
}
