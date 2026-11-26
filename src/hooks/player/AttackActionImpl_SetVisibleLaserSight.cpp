#include <pch.h>

#include "AttackActionImpl_SetVisibleLaserSight.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"
#include "../equip/LaserSightSwitch.h"
#include "../ui/UiControllerImpl_UpdateEquipHud.h"
#include "../ui/UiPadLock.h"

namespace
{
    using SetVisibleLaserSight_t = void(__fastcall*)(void* self, std::uint8_t* work, std::int32_t playerIndex, char request);

    constexpr std::size_t   kWorkGunFlagsHigh = 0x23B;    // bit 0: the gun has a laser sight
    constexpr std::size_t   kWorkEquipId      = 0x24C;
    constexpr std::size_t   kWorkPlaceFlags   = 0x280;
    constexpr std::uint32_t kPlacePointer     = 0x80000;  // the laser is aiming a placed weapon, not the gun
    constexpr std::size_t   kWorkPlayerFlags  = 0x3C0;    // bit 0: this work belongs to the local player
    constexpr std::uint32_t kAimingBits       = 0xC0000;  // bits 18 / 19: aim held or aim starting, the same test the game makes before the light key

    const std::uint8_t kPrologue[] = { 0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x57, 0x48, 0x8D, 0xAC, 0x24 };

    SetVisibleLaserSight_t g_Orig = nullptr;
    void*                  g_Target = nullptr;

    struct OwnLaserGun
    {
        std::uint32_t engineId = 0;
        bool          aiming = false;
    };

    OwnLaserGun OwnLaserGunSEH(const std::uint8_t* work)
    {
        OwnLaserGun gun;
        __try
        {
            const std::uint32_t playerFlags = *reinterpret_cast<const std::uint32_t*>(work + kWorkPlayerFlags);
            if ((playerFlags & 1) == 0)
                return gun;
            if ((work[kWorkGunFlagsHigh] & 1) == 0)
                return gun;
            if (*reinterpret_cast<const std::uint32_t*>(work + kWorkPlaceFlags) & kPlacePointer)
                return gun;
            gun.engineId = *reinterpret_cast<const std::uint32_t*>(work + kWorkEquipId);
            gun.aiming = (playerFlags & kAimingBits) != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            gun.engineId = 0;
        }
        return gun;
    }

    void SwitchByAimKey(std::uint32_t engineId)
    {
        bool pressed = false;
        if (!uipad::Press(LaserSightSwitch::kAimAction, pressed) || !pressed || LaserSightSwitch::AimKeyDebounced())
            return;
        bool aimPressed = false;
        if (uipad::Press(LaserSightSwitch::kHoldAction, aimPressed) && aimPressed)
            return;   // aim is rebound to the laser key, so this press raised the gun
        const std::int32_t equipId = LaserSightSwitch::EquipIdOfEngineId(engineId);
        const bool on = LaserSightSwitch::IsOff(equipId);
        LaserSightSwitch::SetOn(equipId, on);
        LaserSightSwitch::NoteAimKeyToggle();
        LaserSightSwitch::RefreshHudWords(EquipHud_CachedInfo(), equipId, on);
    }

    void __fastcall hkSetVisibleLaserSight(void* self, std::uint8_t* work, std::int32_t playerIndex, char request)
    {
        if (work && !MissionCodeGuard::ShouldBypassHooks())
        {
            const OwnLaserGun gun = OwnLaserGunSEH(work);
            if (gun.engineId != 0)
            {
                if (gun.aiming)
                    SwitchByAimKey(gun.engineId);
                if (request && LaserSightSwitch::AnyOff()
                    && LaserSightSwitch::IsOff(LaserSightSwitch::EquipIdOfEngineId(gun.engineId)))
                    request = 0;
            }
        }
        g_Orig(self, work, playerIndex, request);
    }
}

bool Install_AttackActionImpl_SetVisibleLaserSight_Hook()
{
    void* fn = LaserSightSwitch::CheckedEntry(gAddr.AttackActionImpl_SetVisibleLaserSight,
                                              kPrologue, sizeof(kPrologue), "AttackActionImpl::SetVisibleLaserSight");
    if (!fn)
        return false;
    if (!CreateAndEnableHook(fn, reinterpret_cast<void*>(&hkSetVisibleLaserSight), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("LaserSight: SetVisibleLaserSight hook failed, the laser stays on");
        return false;
    }
    g_Target = fn;
    return true;
}

void Uninstall_AttackActionImpl_SetVisibleLaserSight_Hook()
{
    if (g_Target)
    {
        DisableAndRemoveHook(g_Target);
        g_Target = nullptr;
    }
    g_Orig = nullptr;
}
