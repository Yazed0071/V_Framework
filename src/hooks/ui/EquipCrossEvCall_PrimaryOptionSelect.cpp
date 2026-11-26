#include <pch.h>

#include "EquipCrossEvCall_PrimaryOptionSelect.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"
#include "EquipCrossLaserBox.h"
#include "EquipCrossEvCall_SetPrimaryOptionPanel.h"
#include "../equip/LaserSightSwitch.h"

namespace
{
    using PrimaryOptionSelect_t = void(__fastcall*)(std::uint8_t* cross);

    const std::uint8_t kPrologue[] = {
        0x40, 0x53, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x38, 0x48, 0x8B, 0xD9, 0x48, 0x8B, 0x89, 0x10, 0x01, 0x00, 0x00 };

    PrimaryOptionSelect_t g_Orig = nullptr;
    void*                 g_Target = nullptr;

    void __fastcall hkPrimaryOptionSelect(std::uint8_t* cross)
    {
        g_Orig(cross);
        if (!MissionCodeGuard::ShouldBypassHooks())
            EquipCrossLaserBox::PollPrimaryKey(cross, EquipCrossEvCall_StripPrimaryWeapon());
    }
}

bool Install_EquipCrossEvCall_PrimaryOptionSelect_Hook()
{
    void* fn = LaserSightSwitch::CheckedEntry(gAddr.EquipCrossEvCall_PrimaryOptionSelect,
                                              kPrologue, sizeof(kPrologue), "EquipCrossEvCall::PrimaryOptionSelect");
    if (!fn)
        return false;
    if (!CreateAndEnableHook(fn, reinterpret_cast<void*>(&hkPrimaryOptionSelect), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("LaserSight: PrimaryOptionSelect hook failed, the LASER box key does nothing for primary weapons");
        return false;
    }
    g_Target = fn;
    return true;
}

void Uninstall_EquipCrossEvCall_PrimaryOptionSelect_Hook()
{
    if (g_Target)
    {
        DisableAndRemoveHook(g_Target);
        g_Target = nullptr;
    }
    g_Orig = nullptr;
}
