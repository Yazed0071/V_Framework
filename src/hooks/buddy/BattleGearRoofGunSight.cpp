#include <pch.h>

#include "BattleGearRoofGunSight.h"

#include <Windows.h>
#include <cmath>
#include <cstdint>

#include "AddressSet.h"
#include "BattleGearWeapons.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using UpdateRiderUi_t = void(__fastcall*)(std::uint8_t* action, std::uint32_t index);
    using FireLine_t      = void(__fastcall*)(void* ui, const float* start, const float* end, float tolerance, std::uint32_t flags);

    constexpr std::size_t   kActionOwner  = 0x08;
    constexpr std::size_t   kOwnerSystem  = 0x138;   // the player system:
    constexpr std::size_t   kSystemUi     = 0xD8;    // its UI controller,
    constexpr std::size_t   kSystemShown  = 0x218;   // and the player whose HUD shows
    constexpr std::size_t   kFireLineSlot = 0x1C8 / sizeof(void*);
    constexpr std::int32_t  kRoofGunArm   = 1;
    constexpr float         kPastMuzzle   = 0.5f;    // the tanks' line starts half a metre out of the muzzle
    constexpr float         kTolerance    = 1.0f;    // squared metres between where it is blocked and the aim point

    const std::uint8_t kUpdateRiderUiEntry[] = { 0x48, 0x8B, 0xC4, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0x80,
                                                 0x00, 0x00, 0x00 };

    UpdateRiderUi_t g_Orig = nullptr;
    void*           g_Target = nullptr;

    void* ShownRiderUiSEH(const std::uint8_t* action, std::uint32_t index)
    {
        __try
        {
            const auto* owner = *reinterpret_cast<const std::uint8_t* const*>(action + kActionOwner);
            const auto* system = owner ? *reinterpret_cast<const std::uint8_t* const*>(owner + kOwnerSystem) : nullptr;
            if (!system || *reinterpret_cast<const std::uint32_t*>(system + kSystemShown) != index)
                return nullptr;
            return *reinterpret_cast<void* const*>(system + kSystemUi);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    void CastFireLineSEH(void* ui, const float* start, const float* end)
    {
        __try
        {
            reinterpret_cast<FireLine_t>((*static_cast<void* const* const*>(ui))[kFireLineSlot])(ui, start, end, kTolerance, 0);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void __fastcall hkUpdateRiderUi(std::uint8_t* action, std::uint32_t index)
    {
        g_Orig(action, index);
        if (!battleGearWeapons::Ridden() || !battleGearWeapons::MgSelected())
            return;
        alignas(16) float start[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        alignas(16) float end[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        float barrel[3];
        if (!battleGearWeapons::MuzzleFor(kRoofGunArm, 0, start) || !battleGearWeapons::BarrelFor(kRoofGunArm, barrel)
            || !battleGearWeapons::AimPoint(end))
            return;
        const float length = std::sqrt(barrel[0] * barrel[0] + barrel[1] * barrel[1] + barrel[2] * barrel[2]);
        if (length < 1e-4f)
            return;
        for (int i = 0; i < 3; ++i)
            start[i] += barrel[i] / length * kPastMuzzle;
        if (void* ui = action ? ShownRiderUiSEH(action, index) : nullptr)
            CastFireLineSEH(ui, start, end);
    }
}

namespace battleGearRoofGunSight
{
    bool Install()
    {
        if (g_Target)
            return true;
        constexpr const char* kWithout = "the roof gun's crosshair keeps the D-Walker gun's blocked-shot box";
        void* target = gAddr.RideWalkerGearActionImpl_UpdateRiderUi ? ResolveGameAddress(gAddr.RideWalkerGearActionImpl_UpdateRiderUi) : nullptr;
        if (!target)
        {
            LogError("BattleGear: rider HUD update address missing on this build, %s", kWithout);
            return false;
        }
        const HookEntry state = CheckHookEntry(target, kUpdateRiderUiEntry, sizeof(kUpdateRiderUiEntry));
        if (state == HookEntry::Unknown)
        {
            char desc[160] = {};
            DescribeFunctionEntry(target, desc, sizeof(desc));
            LogError("BattleGear: rider HUD update differs on this build (%s), %s", desc, kWithout);
            return false;
        }
        if (state == HookEntry::ForeignHook)
            LogHookedOnTop("BattleGear: RideWalkerGearActionImpl UpdateRiderUi", target);
        if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkUpdateRiderUi), reinterpret_cast<void**>(&g_Orig)))
        {
            LogError("BattleGear: rider HUD update hook failed, %s", kWithout);
            return false;
        }
        g_Target = target;
        return true;
    }

    void Uninstall()
    {
        if (!g_Target)
            return;
        DisableAndRemoveHook(g_Target);
        g_Target = nullptr;
        g_Orig = nullptr;
    }
}
