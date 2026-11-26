#include <pch.h>
#include "ShellImpl_RequestSmokeCloud.h"

#include <Windows.h>
#include <cstdint>

#include "HeliKey.h"
#include "MissionCodeGuard.h"
#include "SupportHeliHookInstall.h"
#include "SupportHeliWeapon.h"

namespace
{
    using RequestSmokeCloud_t = void(__fastcall*)(void* shells, std::uint32_t slot, float radius, std::uint32_t kind,
                                                 float duration, bool isSupplyShell);

    constexpr std::size_t kShellRecords      = 0x108;
    constexpr std::size_t kShellRecordStride = 0x2C;
    constexpr std::size_t kRecordShellId     = 0x14;
    constexpr std::size_t kRecordOwner       = 0x18;

    constexpr std::uint8_t kRequestSmokeCloudEntry[] =
    {
        0x4C, 0x8B, 0xDC, 0x53, 0x57, 0x48, 0x81, 0xEC, 0x88, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x05,
    };

    RequestSmokeCloud_t g_Orig   = nullptr;
    void*               g_Target = nullptr;

    bool IsPequodShellSEH(const std::uint8_t* shells, std::uint32_t slot)
    {
        __try
        {
            const std::uint8_t* records = *reinterpret_cast<std::uint8_t* const*>(shells + kShellRecords);
            if (!records)
                return false;
            const std::uint8_t* record = records + static_cast<std::size_t>(slot) * kShellRecordStride;
            const std::uint16_t shellId = *reinterpret_cast<const std::uint16_t*>(record + kRecordShellId);
            const std::uint16_t owner   = *reinterpret_cast<const std::uint16_t*>(record + kRecordOwner);
            return (owner >> 9) == HeliKey::kFirstType
                && (shellId == SupportHeliWeapon::kRocketShell || shellId == SupportHeliWeapon::kMissileShell);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void __fastcall hkRequestSmokeCloud(void* shells, std::uint32_t slot, float radius, std::uint32_t kind,
                                        float duration, bool isSupplyShell)
    {
        if (!shells || MissionCodeGuard::ShouldBypassHooks() || !IsPequodShellSEH(static_cast<const std::uint8_t*>(shells), slot))
        {
            g_Orig(shells, slot, radius, kind, duration, isSupplyShell);
            return;
        }
        SupportHeliWeapon::Weapon weapon{};
        float rowRadius = 0.0f;
        std::uint8_t blastFlag = 0;
        if (SupportHeliWeapon::GetSortieWeapon(weapon) && weapon.cloudShell != 0
            && SupportHeliWeapon::ReadWeaponBlastRow(weapon, rowRadius, blastFlag))
        {
            radius = rowRadius;
        }
        g_Orig(shells, slot, radius, kind, duration, isSupplyShell);
    }
}

bool Install_ShellImpl_RequestSmokeCloud_Hook()
{
    if (g_Target)
        return true;
    g_Target = InstallSupportHeliHook(
        "Pequod clouds", gAddr.ShellImpl_RequestSmokeCloud,
        kRequestSmokeCloudEntry, nullptr, sizeof(kRequestSmokeCloudEntry),
        reinterpret_cast<void*>(&hkRequestSmokeCloud), reinterpret_cast<void**>(&g_Orig),
        "gas and smoke from custom Pequod weapons keep the vanilla cloud size");
    return g_Target != nullptr;
}

void Uninstall_ShellImpl_RequestSmokeCloud_Hook()
{
    UninstallSupportHeliHook(g_Target);
}
