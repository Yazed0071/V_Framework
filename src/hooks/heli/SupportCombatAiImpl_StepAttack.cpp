#include <pch.h>
#include "SupportCombatAiImpl_StepAttack.h"

#include <Windows.h>
#include <cstdint>

#include "HeliKey.h"
#include "SupportHeliHookInstall.h"
#include "SupportHeliWeapon.h"

namespace
{
    using Step_t = std::uint64_t(__fastcall*)(void* ai, std::uint32_t heliIndex, std::uint32_t phase);

    constexpr std::uint32_t kPhaseEnter     = 0;
    constexpr std::size_t   kAiHeli         = 0x70;
    constexpr std::size_t   kAiHeliStates   = 0x80;
    constexpr std::size_t   kHeliStateSize  = 0x70;
    constexpr std::size_t   kStateCooldown  = 0x54;   // seconds until the next rocket or missile attack
    constexpr std::size_t   kHeliType       = 0x130;

    constexpr std::uint8_t kStepRocketAimEntry[] =
    {
        0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x18, 0x55, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D,
    };
    constexpr std::uint8_t kStepMissileEntry[] =
    {
        0x40, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0x30, 0x01, 0x00, 0x00, 0x48, 0x8B, 0x05,
    };

    Step_t g_OrigStepRocketAim = nullptr;
    Step_t g_OrigStepMissile   = nullptr;
    void*  g_RocketAimTarget   = nullptr;
    void*  g_MissileTarget     = nullptr;

    void SetCooldownSEH(void* ai, std::uint32_t heliIndex, float cooldown)
    {
        __try
        {
            auto* self = static_cast<std::uint8_t*>(ai);
            const std::uint8_t* heli = *reinterpret_cast<std::uint8_t**>(self + kAiHeli);
            if (!heli || *reinterpret_cast<const std::uint16_t*>(heli + kHeliType) != HeliKey::kFirstType)
                return;
            std::uint8_t* states = *reinterpret_cast<std::uint8_t**>(self + kAiHeliStates);
            if (states)
                *reinterpret_cast<float*>(states + heliIndex * kHeliStateSize + kStateCooldown) = cooldown;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void ApplyCooldown(void* ai, std::uint32_t heliIndex, std::uint32_t phase)
    {
        if (phase != kPhaseEnter)
            return;
        SupportHeliWeapon::Weapon weapon{};
        if (SupportHeliWeapon::GetSortieWeapon(weapon) && weapon.cooldown >= 0.0f)
            SetCooldownSEH(ai, heliIndex, weapon.cooldown);
    }

    std::uint64_t __fastcall hkStepRocketAim(void* ai, std::uint32_t heliIndex, std::uint32_t phase)
    {
        const std::uint64_t result = g_OrigStepRocketAim(ai, heliIndex, phase);
        ApplyCooldown(ai, heliIndex, phase);
        return result;
    }

    std::uint64_t __fastcall hkStepMissile(void* ai, std::uint32_t heliIndex, std::uint32_t phase)
    {
        const std::uint64_t result = g_OrigStepMissile(ai, heliIndex, phase);
        ApplyCooldown(ai, heliIndex, phase);
        return result;
    }
}

bool Install_SupportCombatAiImpl_StepAttack_Hooks()
{
    if (!g_RocketAimTarget)
    {
        g_RocketAimTarget = InstallSupportHeliHook(
            "Pequod rocket attack step", gAddr.SupportCombatAiImpl_StepRocketAim,
            kStepRocketAimEntry, nullptr, sizeof(kStepRocketAimEntry),
            reinterpret_cast<void*>(&hkStepRocketAim), reinterpret_cast<void**>(&g_OrigStepRocketAim),
            "Rocket-type custom Pequod weapons keep the vanilla 30 s cooldown");
    }
    if (!g_MissileTarget)
    {
        g_MissileTarget = InstallSupportHeliHook(
            "Pequod missile attack step", gAddr.SupportCombatAiImpl_StepMissile,
            kStepMissileEntry, nullptr, sizeof(kStepMissileEntry),
            reinterpret_cast<void*>(&hkStepMissile), reinterpret_cast<void**>(&g_OrigStepMissile),
            "Missile-type custom Pequod weapons keep the vanilla 30 s cooldown");
    }
    return g_RocketAimTarget != nullptr || g_MissileTarget != nullptr;
}

void Uninstall_SupportCombatAiImpl_StepAttack_Hooks()
{
    UninstallSupportHeliHook(g_MissileTarget);
    UninstallSupportHeliHook(g_RocketAimTarget);
}
