#include <pch.h>
#include "CustomizeControllerImpl_SetSubWeaponAmmo.h"

#include <Windows.h>
#include <cstdint>

#include "MissionCodeGuard.h"
#include "SupportHeliHookInstall.h"
#include "SupportHeliWeapon.h"

namespace
{
    using SetSubWeaponAmmo_t = std::uint64_t(__fastcall*)(void* customize, std::uint32_t heliIndex);
    using SetSubWeaponPods_t = std::uint64_t(__fastcall*)(void* plugin, void* model, std::uint32_t instance,
                                                          std::uint32_t subWeaponLevel, std::uint32_t applyNow);
    using SetPartVisible_t   = void(__fastcall*)(void* model, std::uint32_t instance, std::uint32_t part, std::uint8_t visible);

    constexpr std::size_t   kCustomizeRecords  = 0x8;
    constexpr std::size_t   kRecordStride      = 0x24;
    constexpr std::size_t   kRecordAmmo        = 0x14;
    constexpr std::size_t   kModelSlotSetPart  = 8;
    constexpr std::uint32_t kApplyNow          = 1;   // only the menu preview passes 1

    constexpr std::uint8_t kSetAmmoEntry[] =
    {
        0x48, 0x83, 0xEC, 0x38, 0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00, 0x48, 0x33, 0xC4, 0x48, 0x89,
    };
    constexpr std::uint8_t kSetAmmoMask[] =
    {
        1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1,
    };
    constexpr std::uint8_t kSetPodsEntry[] =
    {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
    };

    SetSubWeaponAmmo_t g_OrigSetAmmo = nullptr;
    SetSubWeaponPods_t g_OrigSetPods = nullptr;
    void*              g_AmmoTarget  = nullptr;
    void*              g_PodsTarget  = nullptr;

    void WriteAmmoSEH(void* customize, std::uint32_t heliIndex, std::int32_t ammo)
    {
        __try
        {
            std::uint8_t* records = *reinterpret_cast<std::uint8_t**>(static_cast<std::uint8_t*>(customize) + kCustomizeRecords);
            if (!records)
                return;
            auto* slot = reinterpret_cast<volatile LONG*>(records + heliIndex * kRecordStride + kRecordAmmo);
            InterlockedExchange(slot, ammo);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void** ModelVtableSEH(void* model)
    {
        __try
        {
            return model ? *static_cast<void***>(model) : nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    std::uint64_t __fastcall hkSetSubWeaponAmmo(void* customize, std::uint32_t heliIndex)
    {
        const std::uint64_t result = g_OrigSetAmmo(customize, heliIndex);
        SupportHeliWeapon::Weapon weapon{};
        if (SupportHeliWeapon::GetSortieWeapon(weapon))
            WriteAmmoSEH(customize, heliIndex, weapon.ammo);
        return result;
    }

    std::uint64_t __fastcall hkSetSubWeaponPods(void* plugin, void* model, std::uint32_t instance,
                                                std::uint32_t subWeaponLevel, std::uint32_t applyNow)
    {
        const std::uint64_t result = g_OrigSetPods(plugin, model, instance, subWeaponLevel, applyNow);
        if (applyNow == kApplyNow)
            return result;
        SupportHeliWeapon::Weapon weapon{};
        if (!SupportHeliWeapon::GetSortieWeapon(weapon) || !weapon.hasPodMask)
            return result;
        void** vtable = ModelVtableSEH(model);
        if (!vtable)
            return result;
        auto setPartVisible = reinterpret_cast<SetPartVisible_t>(vtable[kModelSlotSetPart]);
        for (std::uint8_t pod = 0; pod < SupportHeliWeapon::kPodPartCount; ++pod)
        {
            const std::uint8_t visible = (weapon.podMask >> pod) & 1u;
            setPartVisible(model, instance, SupportHeliWeapon::kFirstPodPart + pod, visible);
        }
        return result;
    }
}

bool Install_CustomizeControllerImpl_SetSubWeaponAmmo_Hooks()
{
    if (!g_AmmoTarget)
    {
        g_AmmoTarget = InstallSupportHeliHook(
            "Pequod ammo setup", gAddr.CustomizeControllerImpl_SetSubWeaponAmmo,
            kSetAmmoEntry, kSetAmmoMask, sizeof(kSetAmmoEntry),
            reinterpret_cast<void*>(&hkSetSubWeaponAmmo), reinterpret_cast<void**>(&g_OrigSetAmmo),
            "custom Pequod weapons start with the vanilla ammo");
    }
    if (!g_PodsTarget)
    {
        g_PodsTarget = InstallSupportHeliHook(
            "Pequod pod setup", gAddr.CustomizeControllerImpl_SetSubWeaponPods,
            kSetPodsEntry, nullptr, sizeof(kSetPodsEntry),
            reinterpret_cast<void*>(&hkSetSubWeaponPods), reinterpret_cast<void**>(&g_OrigSetPods),
            "custom Pequod weapons show the vanilla pods");
    }
    return g_AmmoTarget != nullptr;
}

void Uninstall_CustomizeControllerImpl_SetSubWeaponAmmo_Hooks()
{
    UninstallSupportHeliHook(g_PodsTarget);
    UninstallSupportHeliHook(g_AmmoTarget);
}
