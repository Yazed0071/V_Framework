#include <pch.h>
#include "ShellSystemImpl_Fire.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>

#include "HeliKey.h"
#include "SupportHeliHookInstall.h"
#include "SupportHeliWeapon.h"

namespace
{
    using Fire_t = std::uint64_t(__fastcall*)(void* shells, const std::uint8_t* shellDesc);

    constexpr std::size_t   kShellDescSize      = 0x40;
    constexpr std::size_t   kDescOwner          = 0x2C;
    constexpr std::size_t   kDescShellId        = 0x2E;
    constexpr std::size_t   kDescShooter        = 0x32;
    constexpr std::size_t   kDescAttack         = 0x3C;
    constexpr std::uint16_t kPequodOwner        = 0x3000;
    constexpr std::uint16_t kAttackIdBits       = 0x3FF;   // the rest of the word is the guidance mode

    constexpr std::uint8_t kFireEntry[] =
    {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xFA, 0x48, 0x8B, 0xD9,
    };

    Fire_t g_OrigFire = nullptr;
    void*  g_Target   = nullptr;

    bool IsPequodLauncherShellSEH(const std::uint8_t* desc)
    {
        __try
        {
            const std::uint16_t shooter = *reinterpret_cast<const std::uint16_t*>(desc + kDescShooter);
            const std::uint16_t owner   = *reinterpret_cast<const std::uint16_t*>(desc + kDescOwner);
            const std::uint16_t shellId = *reinterpret_cast<const std::uint16_t*>(desc + kDescShellId);
            return (shooter >> 9) == HeliKey::kFirstType && owner == kPequodOwner
                && (shellId == SupportHeliWeapon::kRocketShell || shellId == SupportHeliWeapon::kMissileShell);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint64_t __fastcall hkFire(void* shells, const std::uint8_t* shellDesc)
    {
        if (!shellDesc || !IsPequodLauncherShellSEH(shellDesc))
            return g_OrigFire(shells, shellDesc);
        SupportHeliWeapon::Weapon weapon{};
        if (!SupportHeliWeapon::GetSortieWeapon(weapon) || (weapon.attackId == 0 && weapon.shellId == 0))
            return g_OrigFire(shells, shellDesc);

        alignas(16) std::uint8_t desc[kShellDescSize];
        std::memcpy(desc, shellDesc, sizeof(desc));
        if (weapon.attackId != 0)
        {
            auto& attack = *reinterpret_cast<std::uint16_t*>(desc + kDescAttack);
            attack = static_cast<std::uint16_t>((attack & ~kAttackIdBits) | weapon.attackId);
        }
        if (weapon.shellId != 0)
            *reinterpret_cast<std::uint16_t*>(desc + kDescShellId) = weapon.shellId;
        return g_OrigFire(shells, desc);
    }
}

bool Install_ShellSystemImpl_Fire_Hook()
{
    if (g_Target)
        return true;
    g_Target = InstallSupportHeliHook(
        "Pequod shells", gAddr.ShellSystemImpl_Fire,
        kFireEntry, nullptr, sizeof(kFireEntry),
        reinterpret_cast<void*>(&hkFire), reinterpret_cast<void**>(&g_OrigFire),
        "custom Pequod weapons fire the vanilla shell and damage");
    return g_Target != nullptr;
}

void Uninstall_ShellSystemImpl_Fire_Hook()
{
    UninstallSupportHeliHook(g_Target);
}
