#include <pch.h>
#include "LauncherActionImpl_Execute.h"

#include <Windows.h>
#include <cstdint>

#include "SupportHeliHookInstall.h"
#include "SupportHeliWeapon.h"
#include "HeliKey.h"

namespace
{
    using Execute_t = std::uint64_t(__fastcall*)(void* launcher, std::uint32_t phase, std::uint32_t action);
    using Wake_t    = std::uint64_t(__fastcall*)(void* launcher, std::uint32_t action);

    constexpr std::uint32_t kExecutePhase       = 4;
    constexpr std::size_t   kLauncherHeli       = 0x40;
    constexpr std::size_t   kLauncherEntries    = 0x50;
    constexpr std::size_t   kLauncherFirstAction = 0x64;
    constexpr std::size_t   kEntryStride        = 0x34;
    constexpr std::size_t   kEntrySalvo         = 0x0C;
    constexpr std::size_t   kEntryMode          = 0x10;
    constexpr std::size_t   kEntryShotCount     = 0x1C;
    constexpr std::size_t   kEntryWindup        = 0x24;
    constexpr std::size_t   kEntryShotTimer     = 0x28;
    constexpr std::size_t   kHeliType           = 0x130;
    constexpr std::int32_t  kModeMissile        = 1;
    constexpr std::int32_t  kRocketVolleyDone   = 8;   // the rocket volley stops once the counter passes 7

    constexpr std::uint8_t kExecuteEntry[] =
    {
        0x83, 0xFA, 0x04, 0x0F, 0x85, 0x00, 0x00, 0x00, 0x00, 0x55, 0x53, 0x56, 0x41, 0x56, 0x48, 0x8D,
    };
    constexpr std::uint8_t kExecuteMask[] =
    {
        1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1,
    };
    constexpr std::uint8_t kWakeEntry[] =
    {
        0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83,
    };

    Execute_t g_OrigExecute   = nullptr;
    Wake_t    g_OrigWake      = nullptr;
    void*     g_ExecuteTarget = nullptr;
    void*     g_WakeTarget    = nullptr;

    std::uint8_t* SupportHeliEntrySEH(void* launcher, std::uint32_t action)
    {
        __try
        {
            auto* self = static_cast<std::uint8_t*>(launcher);
            const std::uint8_t* heli = *reinterpret_cast<std::uint8_t**>(self + kLauncherHeli);
            if (!heli || *reinterpret_cast<const std::uint16_t*>(heli + kHeliType) != HeliKey::kFirstType)
                return nullptr;
            std::uint8_t* entries = *reinterpret_cast<std::uint8_t**>(self + kLauncherEntries);
            const std::int32_t first = *reinterpret_cast<const std::int32_t*>(self + kLauncherFirstAction);
            return entries ? entries + (action - static_cast<std::uint32_t>(first)) * kEntryStride : nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    std::uint64_t __fastcall hkExecute(void* launcher, std::uint32_t phase, std::uint32_t action)
    {
        SupportHeliWeapon::Weapon weapon{};
        if (phase != kExecutePhase || !SupportHeliWeapon::GetSortieWeapon(weapon))
            return g_OrigExecute(launcher, phase, action);
        std::uint8_t* entry = SupportHeliEntrySEH(launcher, action);
        if (!entry)
            return g_OrigExecute(launcher, phase, action);

        auto& shotCount = *reinterpret_cast<std::int32_t*>(entry + kEntryShotCount);
        const std::int32_t mode = *reinterpret_cast<const std::int32_t*>(entry + kEntryMode);
        if (mode == kModeMissile && weapon.shotCount > 0)
            *reinterpret_cast<std::int32_t*>(entry + kEntrySalvo) = weapon.shotCount;
        const std::int32_t shotsBefore = shotCount;

        const std::uint64_t result = g_OrigExecute(launcher, phase, action);

        if (shotCount == shotsBefore || shotCount <= 0)
            return result;
        if (weapon.shotInterval >= 0.0f)
            *reinterpret_cast<float*>(entry + kEntryShotTimer) = weapon.shotInterval;
        if (mode != kModeMissile && weapon.shotCount > 0 && shotCount >= weapon.shotCount)
            shotCount = kRocketVolleyDone;
        return result;
    }

    std::uint64_t __fastcall hkWake(void* launcher, std::uint32_t action)
    {
        const std::uint64_t result = g_OrigWake(launcher, action);
        SupportHeliWeapon::Weapon weapon{};
        if (!SupportHeliWeapon::GetSortieWeapon(weapon) || weapon.windup < 0.0f)
            return result;
        if (std::uint8_t* entry = SupportHeliEntrySEH(launcher, action))
            *reinterpret_cast<float*>(entry + kEntryWindup) = weapon.windup;
        return result;
    }
}

bool Install_LauncherActionImpl_Execute_Hooks()
{
    if (!g_ExecuteTarget)
    {
        g_ExecuteTarget = InstallSupportHeliHook(
            "Pequod launcher", gAddr.LauncherActionImpl_Execute,
            kExecuteEntry, kExecuteMask, sizeof(kExecuteEntry),
            reinterpret_cast<void*>(&hkExecute), reinterpret_cast<void**>(&g_OrigExecute),
            "custom Pequod weapons fire with the vanilla shot count and interval");
    }
    if (!g_WakeTarget)
    {
        g_WakeTarget = InstallSupportHeliHook(
            "Pequod launcher wake", gAddr.LauncherActionImpl_Wake,
            kWakeEntry, nullptr, sizeof(kWakeEntry),
            reinterpret_cast<void*>(&hkWake), reinterpret_cast<void**>(&g_OrigWake),
            "custom Pequod weapons keep the vanilla windup");
    }
    return g_ExecuteTarget != nullptr;
}

void Uninstall_LauncherActionImpl_Execute_Hooks()
{
    UninstallSupportHeliHook(g_WakeTarget);
    UninstallSupportHeliHook(g_ExecuteTarget);
}
