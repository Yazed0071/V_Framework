#include <pch.h>
#include "GunActionImpl_FireBarrel.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>

#include "HeliKey.h"
#include "MissionCodeGuard.h"
#include "SupportHeliHookInstall.h"
#include "SupportHeliParts.h"

namespace
{
    using FireBarrel_t = void(__fastcall*)(void* aim, std::int32_t heliIndex, std::int32_t barrel, const std::uint8_t* round);

    constexpr std::size_t kAimHeli        = 0x38;
    constexpr std::size_t kHeliType       = 0x130;
    constexpr std::size_t kRoundSize      = 0x40;    // Bullet3Desc
    constexpr std::size_t kRoundShooter   = 0x2A;
    constexpr std::size_t kRoundAttack    = 0x2E;
    constexpr std::size_t kRoundRow       = 0x30;    // TppEquip.BL_*
    constexpr DWORD       kPickRefreshMs  = 500;
    constexpr std::uint16_t kHeliMiniGunAttack  = 0x10D;   // ATK_HeliMiniGun, the door gun in the player's hands
    constexpr std::uint16_t kHeliChainGunAttack = 0x112;   // ATK_HeliChainGun, the door guns the heli fires itself

    constexpr std::uint8_t kFireBarrelEntry[] =
    {
        0x4C, 0x8B, 0xDC, 0x55, 0x53, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x49, 0x8D, 0xAB,
    };

    struct PickedGun
    {
        DWORD                  readAt = 0;
        bool                   read   = false;
        bool                   custom = false;
        SupportHeliParts::Part part;
    };

    FireBarrel_t g_Orig   = nullptr;
    void*        g_Target = nullptr;

    thread_local PickedGun t_Picked[SupportHeliParts::kKindCount];

    bool ReadPequodRoundSEH(const std::uint8_t* aim, const std::uint8_t* round, std::uint8_t* copy, std::uint16_t& shooter)
    {
        __try
        {
            const std::uint8_t* heli = *reinterpret_cast<std::uint8_t* const*>(aim + kAimHeli);
            if (!heli || *reinterpret_cast<const std::uint16_t*>(heli + kHeliType) != HeliKey::kFirstType)
                return false;
            std::memcpy(copy, round, kRoundSize);
            shooter = *reinterpret_cast<const std::uint16_t*>(round + kRoundShooter);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

#ifdef _DEBUG
    void ProbeRound(std::uint16_t shooter, std::uint16_t attack, std::uint8_t row)   // TEMP probe: which rounds the Pequod door guns fire
    {
        static std::uint16_t s_Seen[4] = {};
        static int s_SeenCount = 0;
        for (int i = 0; i < s_SeenCount; ++i)
        {
            if (s_Seen[i] == attack)
                return;
        }
        if (s_SeenCount >= 4)
            return;
        s_Seen[s_SeenCount++] = attack;
        LogDebug("PequodGun: door gun round from shooter 0x%04X, attack 0x%X, bullet row 0x%X", shooter, attack, row);
    }
#endif

    const PickedGun& Picked(SupportHeliParts::Kind kind)
    {
        PickedGun& picked = t_Picked[static_cast<std::size_t>(kind)];
        const DWORD now = GetTickCount();
        if (!picked.read || now - picked.readAt >= kPickRefreshMs)
        {
            picked.read   = true;
            picked.readAt = now;
            picked.custom = SupportHeliParts::PickedPart(kind, picked.part);
        }
        return picked;
    }

    void __fastcall hkFireBarrel(void* aim, std::int32_t heliIndex, std::int32_t barrel, const std::uint8_t* round)
    {
        alignas(16) std::uint8_t custom[kRoundSize];
        std::uint16_t shooter = 0;
        if (!aim || !round || MissionCodeGuard::ShouldBypassHooks()
            || !ReadPequodRoundSEH(static_cast<const std::uint8_t*>(aim), round, custom, shooter))
        {
            g_Orig(aim, heliIndex, barrel, round);
            return;
        }

        const std::uint16_t attack = *reinterpret_cast<const std::uint16_t*>(custom + kRoundAttack);
        bool firedByPlayer = (shooter >> 9) != HeliKey::kFirstType;
        if (attack == kHeliMiniGunAttack)
            firedByPlayer = true;
        else if (attack == kHeliChainGunAttack)
            firedByPlayer = false;
#ifdef _DEBUG
        ProbeRound(shooter, attack, custom[kRoundRow]);
#endif
        const PickedGun& picked = Picked(firedByPlayer ? SupportHeliParts::Kind::SideGun : SupportHeliParts::Kind::Gatling);
        if (!picked.custom)
        {
            g_Orig(aim, heliIndex, barrel, round);
            return;
        }
        if (picked.part.attackId >= 0)
            *reinterpret_cast<std::uint16_t*>(custom + kRoundAttack) = static_cast<std::uint16_t>(picked.part.attackId);
        if (picked.part.bulletId >= 0)
            custom[kRoundRow] = static_cast<std::uint8_t>(picked.part.bulletId);
        g_Orig(aim, heliIndex, barrel, custom);
    }
}

bool Install_GunActionImpl_FireBarrel_Hook()
{
    if (g_Target)
        return true;
    g_Target = InstallSupportHeliHook(
        "Pequod door gun fire", gAddr.GunActionImpl_FireBarrel,
        kFireBarrelEntry, nullptr, sizeof(kFireBarrelEntry),
        reinterpret_cast<void*>(&hkFireBarrel), reinterpret_cast<void**>(&g_Orig),
        "custom Pequod gatlings and side guns fire the vanilla rounds");
    return g_Target != nullptr;
}

void Uninstall_GunActionImpl_FireBarrel_Hook()
{
    UninstallSupportHeliHook(g_Target);
}
