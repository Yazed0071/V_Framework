#include <pch.h>
#include "RideHeliActionPluginImpl_ExecActionButtonFunction.h"

#include <Windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"
#include "../heli/HeliKey.h"

namespace
{
    using ExecActionButtonFunction_t = bool(__fastcall*)(void* plugin, std::uint32_t slot, std::uint8_t keyType, std::uint8_t* result);
    using IsForcedOffGun_t           = bool(__fastcall*)(void* plugin, std::uint32_t slot);

    constexpr std::size_t   kPluginSlots   = 0x38;
    constexpr std::size_t   kFirstSlot     = 0x24;
    constexpr std::size_t   kPluginWorks   = 0x78;
    constexpr std::size_t   kWorkSize      = 0x1D0;
    constexpr std::size_t   kWorkState     = 0x10;
    constexpr std::size_t   kWorkHeliId    = 0x60;
    constexpr std::size_t   kResultIcon    = 0x2C;
    constexpr std::uint8_t  kStateOnGun    = 4;
    constexpr std::int32_t  kIconGun       = 0x62;   // HELI_MACHINEGUN
    constexpr std::int32_t  kIconGunNg     = 0x63;
    constexpr std::uint32_t kMaxHelis      = 0x200;

    constexpr std::uint8_t kExecActionButtonFunctionEntry[] =
    {
        0x45, 0x84, 0xC0, 0x75, 0x51, 0x48, 0x8B, 0x41, 0x38, 0x44, 0x8B, 0xD2, 0x44, 0x2B, 0x50, 0x24,
        0x48, 0x8B, 0x41, 0x78, 0x4D, 0x69, 0xD2, 0xD0, 0x01, 0x00, 0x00, 0x45, 0x0F, 0xB6, 0x84, 0x02,
        0x81, 0x00, 0x00, 0x00, 0x45, 0x85, 0xC0,
    };
    constexpr std::uint8_t kIsForcedOffGunEntry[] =
    {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x41, 0x38, 0x2B, 0x50, 0x24, 0x8B, 0xDA, 0x48,
        0x69, 0xDB, 0xD0, 0x01, 0x00, 0x00, 0x48, 0x03, 0x59, 0x78, 0x48, 0x83, 0x7B, 0x68, 0x00,
    };

    ExecActionButtonFunction_t g_OrigExecActionButtonFunction = nullptr;
    IsForcedOffGun_t           g_OrigIsForcedOffGun           = nullptr;
    void*                      g_ExecActionButtonFunction     = nullptr;
    void*                      g_IsForcedOffGun               = nullptr;

    std::atomic<bool> g_GunDisabled[kMaxHelis];
    std::atomic<bool> g_AnyGunDisabled{ false };

    thread_local bool t_AskingForDisabledGun = false;

    struct RideWork
    {
        std::uint8_t  state  = 0;
        std::uint16_t heliId = 0;
    };

    bool ReadRideWorkSEH(void* plugin, std::uint32_t slot, RideWork& out)
    {
        __try
        {
            auto* self  = static_cast<std::uint8_t*>(plugin);
            auto* slots = *reinterpret_cast<std::uint8_t**>(self + kPluginSlots);
            auto* works = *reinterpret_cast<std::uint8_t**>(self + kPluginWorks);
            if (!slots || !works)
                return false;
            const std::uint32_t entry = slot - *reinterpret_cast<std::uint32_t*>(slots + kFirstSlot);
            const std::uint8_t* work  = works + static_cast<std::size_t>(entry) * kWorkSize;
            out.state  = work[kWorkState];
            out.heliId = *reinterpret_cast<const std::uint16_t*>(work + kWorkHeliId);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::int32_t ResultIconSEH(const std::uint8_t* result)
    {
        __try
        {
            return *reinterpret_cast<const std::int32_t*>(result + kResultIcon);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    bool GunDisabledOnRide(void* plugin, std::uint32_t slot, RideWork& work)
    {
        if (!g_AnyGunDisabled.load(std::memory_order_relaxed) || MissionCodeGuard::ShouldBypassHooks())
            return false;
        return ReadRideWorkSEH(plugin, slot, work)
            && (work.heliId >> 9) == HeliKey::kFirstType
            && g_GunDisabled[work.heliId & 0x1FFu].load(std::memory_order_relaxed);
    }

    bool __fastcall hkExecActionButtonFunction(void* plugin, std::uint32_t slot, std::uint8_t keyType, std::uint8_t* result)
    {
        RideWork work{};
        if (!GunDisabledOnRide(plugin, slot, work))
            return g_OrigExecActionButtonFunction(plugin, slot, keyType, result);

        t_AskingForDisabledGun = true;   // the door phase then drops the gun icon and keeps its own
        const bool shown = g_OrigExecActionButtonFunction(plugin, slot, keyType, result);
        t_AskingForDisabledGun = false;
        if (!shown)
            return false;
        const std::int32_t icon = ResultIconSEH(result);
        return icon != kIconGun && icon != kIconGunNg;
    }

    bool __fastcall hkIsForcedOffGun(void* plugin, std::uint32_t slot)
    {
        if (t_AskingForDisabledGun)
            return true;
        RideWork work{};
        if (GunDisabledOnRide(plugin, slot, work) && work.state == kStateOnGun)
            return true;
        return g_OrigIsForcedOffGun(plugin, slot);
    }

    void* InstallHook(const char* what, std::uintptr_t address, const std::uint8_t* entry, std::size_t size, void* detour, void** original)
    {
        void* target = address ? ResolveGameAddress(address) : nullptr;
        if (!target)
        {
            LogError("RideHeliActionPluginImpl: %s address missing for this build, SetPlayerMachineGunEnabled off", what);
            return nullptr;
        }
        const HookEntry state = CheckHookEntry(target, entry, size);
        if (state == HookEntry::Unknown)
        {
            char description[160] = {};
            DescribeFunctionEntry(target, description, sizeof(description));
            LogError("RideHeliActionPluginImpl: %s code differs on this build (%s), SetPlayerMachineGunEnabled off", what, description);
            return nullptr;
        }
        if (state == HookEntry::ForeignHook)
            LogHookedOnTop(what, target);
        if (!CreateAndEnableHook(target, detour, original))
        {
            LogError("RideHeliActionPluginImpl: %s hook failed, SetPlayerMachineGunEnabled off", what);
            return nullptr;
        }
        return target;
    }
}

void Set_HeliPlayerMachineGunEnabled(std::uint32_t heliIndex, bool enabled)
{
    if (heliIndex >= kMaxHelis)
        return;
    g_GunDisabled[heliIndex].store(!enabled, std::memory_order_relaxed);
    bool any = false;
    for (std::uint32_t i = 0; i < kMaxHelis && !any; ++i)
        any = g_GunDisabled[i].load(std::memory_order_relaxed);
    g_AnyGunDisabled.store(any, std::memory_order_relaxed);
}

void Clear_HeliPlayerMachineGunEnabled()
{
    for (std::uint32_t i = 0; i < kMaxHelis; ++i)
        g_GunDisabled[i].store(false, std::memory_order_relaxed);
    g_AnyGunDisabled.store(false, std::memory_order_relaxed);
}

bool Install_RideHeliActionPluginImpl_ExecActionButtonFunction_Hook()
{
    if (g_ExecActionButtonFunction)
        return true;

    Clear_HeliPlayerMachineGunEnabled();

    g_IsForcedOffGun = InstallHook("IsForcedOffGun", gAddr.RideHeliActionPluginImpl_IsForcedOffGun,
        kIsForcedOffGunEntry, sizeof(kIsForcedOffGunEntry),
        reinterpret_cast<void*>(&hkIsForcedOffGun), reinterpret_cast<void**>(&g_OrigIsForcedOffGun));
    if (!g_IsForcedOffGun)
        return false;

    g_ExecActionButtonFunction = InstallHook("ExecActionButtonFunction", gAddr.RideHeliActionPluginImpl_ExecActionButtonFunction,
        kExecActionButtonFunctionEntry, sizeof(kExecActionButtonFunctionEntry),
        reinterpret_cast<void*>(&hkExecActionButtonFunction), reinterpret_cast<void**>(&g_OrigExecActionButtonFunction));
    if (!g_ExecActionButtonFunction)
    {
        DisableAndRemoveHook(g_IsForcedOffGun);
        g_IsForcedOffGun = nullptr;
        return false;
    }
    return true;
}

void Uninstall_RideHeliActionPluginImpl_ExecActionButtonFunction_Hook()
{
    if (g_ExecActionButtonFunction)
        DisableAndRemoveHook(g_ExecActionButtonFunction);
    if (g_IsForcedOffGun)
        DisableAndRemoveHook(g_IsForcedOffGun);
    g_ExecActionButtonFunction = nullptr;
    g_IsForcedOffGun           = nullptr;
}
