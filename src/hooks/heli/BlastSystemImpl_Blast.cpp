#include <pch.h>
#include "BlastSystemImpl_Blast.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>

#include "HeliKey.h"
#include "MissionCodeGuard.h"
#include "SupportHeliHookInstall.h"
#include "SupportHeliWeapon.h"

namespace
{
    using Blast_t = void(__fastcall*)(void* blastSystem, const std::uint8_t* desc);

    constexpr std::size_t   kBlastDescSize = 0x50;
    constexpr std::size_t   kDescRadius    = 0x30;
    constexpr std::size_t   kDescOwner     = 0x38;
    constexpr std::size_t   kDescAttack    = 0x3C;
    constexpr std::size_t   kDescBlastFlag = 0x3E;
    constexpr std::uint16_t kAttackIdBits  = 0x3FF;

    constexpr std::uint8_t kBlastEntry[] =
    {
        0x48, 0x8B, 0xC4, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D,
    };

    Blast_t g_Orig   = nullptr;
    void*   g_Target = nullptr;

    bool ReadPequodBlastSEH(const std::uint8_t* desc, std::uint16_t& attack)
    {
        __try
        {
            const std::uint16_t owner = *reinterpret_cast<const std::uint16_t*>(desc + kDescOwner);
            attack = *reinterpret_cast<const std::uint16_t*>(desc + kDescAttack) & kAttackIdBits;
            return (owner >> 9) == HeliKey::kFirstType;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void __fastcall hkBlast(void* blastSystem, const std::uint8_t* desc)
    {
        std::uint16_t attack = 0;
        if (!desc || MissionCodeGuard::ShouldBypassHooks() || !ReadPequodBlastSEH(desc, attack))
        {
            g_Orig(blastSystem, desc);
            return;
        }
        SupportHeliWeapon::Weapon weapon{};
        if (!SupportHeliWeapon::GetSortieWeapon(weapon) || weapon.blastId < 0 || weapon.cloudShell != 0)
        {
            g_Orig(blastSystem, desc);
            return;
        }
        float radius = 0.0f;
        std::uint8_t blastFlag = 0;
        if (attack != SupportHeliWeapon::ShellAttackId(weapon) || !SupportHeliWeapon::ReadWeaponBlastRow(weapon, radius, blastFlag))
        {
            g_Orig(blastSystem, desc);
            return;
        }

        alignas(16) std::uint8_t custom[kBlastDescSize];
        std::memcpy(custom, desc, sizeof(custom));
        *reinterpret_cast<float*>(custom + kDescRadius) = radius;
        custom[kDescBlastFlag] = blastFlag;
        g_Orig(blastSystem, custom);
    }
}

bool Install_BlastSystemImpl_Blast_Hook()
{
    if (g_Target)
        return true;
    g_Target = InstallSupportHeliHook(
        "Pequod blasts", gAddr.BlastSystemImpl_Blast,
        kBlastEntry, nullptr, sizeof(kBlastEntry),
        reinterpret_cast<void*>(&hkBlast), reinterpret_cast<void**>(&g_Orig),
        "custom Pequod weapons explode with the vanilla blast size");
    return g_Target != nullptr;
}

void Uninstall_BlastSystemImpl_Blast_Hook()
{
    UninstallSupportHeliHook(g_Target);
}
