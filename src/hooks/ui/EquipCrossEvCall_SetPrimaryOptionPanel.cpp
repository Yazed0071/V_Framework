#include <pch.h>

#include "EquipCrossEvCall_SetPrimaryOptionPanel.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"
#include "EquipCrossLaserBox.h"
#include "../equip/LaserSightSwitch.h"

namespace
{
    using SetPrimaryOptionPanel_t = void(__fastcall*)(std::uint8_t* cross, std::uint32_t weaponId);

    const std::uint8_t kPrologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x6C, 0x24, 0x20, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56 };

    SetPrimaryOptionPanel_t    g_Orig = nullptr;
    void*                      g_Target = nullptr;
    std::atomic<std::uint32_t> g_PrimaryWeapon{ 0 };

    void __fastcall hkSetPrimaryOptionPanel(std::uint8_t* cross, std::uint32_t weaponId)
    {
        g_Orig(cross, weaponId);
        g_PrimaryWeapon.store(weaponId, std::memory_order_release);
        if (!MissionCodeGuard::ShouldBypassHooks())
            EquipCrossLaserBox::Show(cross, weaponId);
    }
}

std::uint32_t EquipCrossEvCall_StripPrimaryWeapon()
{
    return g_PrimaryWeapon.load(std::memory_order_acquire);
}

bool Install_EquipCrossEvCall_SetPrimaryOptionPanel_Hook()
{
    void* fn = LaserSightSwitch::CheckedEntry(gAddr.EquipCrossEvCall_SetPrimaryOptionPanel,
                                              kPrologue, sizeof(kPrologue), "EquipCrossEvCall::SetPrimaryOptionPanel");
    if (!fn)
        return false;
    if (!CreateAndEnableHook(fn, reinterpret_cast<void*>(&hkSetPrimaryOptionPanel), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("LaserSight: SetPrimaryOptionPanel hook failed, no LASER box for primary weapons");
        return false;
    }
    g_Target = fn;
    return true;
}

void Uninstall_EquipCrossEvCall_SetPrimaryOptionPanel_Hook()
{
    if (g_Target)
    {
        DisableAndRemoveHook(g_Target);
        g_Target = nullptr;
    }
    g_Orig = nullptr;
}
