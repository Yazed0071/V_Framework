#include <pch.h>

#include "RideWalkerGearActionImpl_ChangeWeapon.h"

#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>

#include "AddressSet.h"
#include "BattleGearWeapons.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using ChangeWeapon_t = void(__fastcall*)(std::uint8_t* action, std::uint32_t index, const std::uint8_t* comm);
    using HudWeapons_t   = std::uint8_t*(__fastcall*)(void* hud);

    constexpr std::size_t  kActionOwner    = 0x08;
    constexpr std::size_t  kActionIndices  = 0x38;
    constexpr std::size_t  kIndicesFirst   = 0x24;
    constexpr std::size_t  kActionWorks    = 0x50;
    constexpr std::size_t  kWorkSize       = 0xB0;
    constexpr std::size_t  kWorkSlot       = 0x88;    // the weapon in use: 1 main gun, 2 sidearm, 3 arm
    constexpr std::uint8_t kSidearmSlot    = 2;
    constexpr std::size_t  kOwnerCore      = 0x138;
    constexpr std::size_t  kCoreHud        = 0xD8;
    constexpr std::size_t  kCoreShownRider = 0x218;   // the rider whose weapons the HUD shows
    constexpr std::size_t  kHudWeaponsSlot = 0x190 / sizeof(void*);
    constexpr std::size_t  kShownRounds    = 0x4A8;   // the weapon in use: rounds, spare rounds, its bar (filled, of)
    constexpr std::size_t  kShownSpare     = 0x4AC;
    constexpr std::size_t  kShownBar       = 0x4B0;
    constexpr std::size_t  kShownBarFull   = 0x4B4;
    constexpr std::size_t  kWheelRounds    = 0x4F8;   // the weapon wheel's sidearm entry, the same four
    constexpr std::size_t  kWheelSpare     = 0x4FC;
    constexpr std::size_t  kWheelBar       = 0x500;
    constexpr std::size_t  kWheelBarFull   = 0x504;
    constexpr std::int32_t kBarFull        = 100;

    const std::uint8_t kChangeWeaponEntry[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18,
                                                0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x41, 0x38 };

    ChangeWeapon_t g_Orig = nullptr;
    void*          g_Target = nullptr;

    void ShowRoofGunSEH(const std::uint8_t* action, std::uint32_t index, std::int32_t rounds, std::int32_t heat)
    {
        __try
        {
            const auto* owner = *reinterpret_cast<const std::uint8_t* const*>(action + kActionOwner);
            const auto* core = owner ? *reinterpret_cast<const std::uint8_t* const*>(owner + kOwnerCore) : nullptr;
            void* hud = core ? *reinterpret_cast<void* const*>(core + kCoreHud) : nullptr;
            if (!hud || index != *reinterpret_cast<const std::uint32_t*>(core + kCoreShownRider))
                return;
            std::uint8_t* weapons = reinterpret_cast<HudWeapons_t>((*static_cast<void* const* const*>(hud))[kHudWeaponsSlot])(hud);
            const auto* works = *reinterpret_cast<const std::uint8_t* const*>(action + kActionWorks);
            const auto* indices = *reinterpret_cast<const std::uint8_t* const*>(action + kActionIndices);
            if (!weapons || !works || !indices)
                return;
            const std::uint32_t first = *reinterpret_cast<const std::uint32_t*>(indices + kIndicesFirst);
            const bool inUse = works[static_cast<std::uint64_t>(index - first) * kWorkSize + kWorkSlot] == kSidearmSlot;
            // a belt has no magazine: both counts are the rounds left, as the game fills the walker's gatling, and no bar
            *reinterpret_cast<std::int32_t*>(weapons + kWheelRounds) = rounds;
            *reinterpret_cast<std::int32_t*>(weapons + kWheelSpare) = rounds;
            *reinterpret_cast<std::int32_t*>(weapons + kWheelBar) = 0;
            *reinterpret_cast<std::int32_t*>(weapons + kWheelBarFull) = 0;
            if (inUse)
            {
                *reinterpret_cast<std::int32_t*>(weapons + kShownRounds) = rounds;
                *reinterpret_cast<std::int32_t*>(weapons + kShownSpare) = rounds;
            }
            *reinterpret_cast<std::int32_t*>(weapons + kShownBar) = 0;
            *reinterpret_cast<std::int32_t*>(weapons + kShownBarFull) = 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void __fastcall hkChangeWeapon(std::uint8_t* action, std::uint32_t index, const std::uint8_t* comm)
    {
        g_Orig(action, index, comm);
        if (!comm || !battleGearWeapons::Ridden() || !battleGearWeapons::IsBattleGearComm(comm))
            return;
        const std::int32_t heat = battleGearWeapons::MgOverheated()
                                ? kBarFull
                                : static_cast<std::int32_t>(std::lround(std::clamp(battleGearWeapons::MgHeat(), 0.0f, 1.0f) * kBarFull));
        ShowRoofGunSEH(action, index, static_cast<std::int32_t>(battleGearWeapons::MgAmmo()), heat);
    }
}

bool Install_RideWalkerGearActionImpl_ChangeWeapon_Hook()
{
    if (g_Target)
        return true;
    void* target = gAddr.RideWalkerGearActionImpl_ChangeWeapon ? ResolveGameAddress(gAddr.RideWalkerGearActionImpl_ChangeWeapon) : nullptr;
    if (!target)
    {
        LogError("BattleGear: rider weapon change address missing on this build, the roof gun's rounds and heat do not show");
        return false;
    }
    const HookEntry state = CheckHookEntry(target, kChangeWeaponEntry, sizeof(kChangeWeaponEntry));
    if (state == HookEntry::Unknown)
    {
        char desc[160] = {};
        DescribeFunctionEntry(target, desc, sizeof(desc));
        LogError("BattleGear: rider weapon change code differs on this build (%s), the roof gun's rounds and heat do not show", desc);
        return false;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("BattleGear: rider ChangeWeapon", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkChangeWeapon), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("BattleGear: rider weapon change hook failed, the roof gun's rounds and heat do not show");
        return false;
    }
    g_Target = target;
    return true;
}

void Uninstall_RideWalkerGearActionImpl_ChangeWeapon_Hook()
{
    if (!g_Target)
        return;
    DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig = nullptr;
}
