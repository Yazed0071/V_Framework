#include <pch.h>

#include "EquipCrossEvCall_SecondaryOptionSelect.h"

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
    using SecondaryOptionSelect_t = void(__fastcall*)(std::uint8_t* cross);

    constexpr std::size_t kCrossSecondaryOpen = 0x2D1;

    const std::uint8_t kPrologue[] = { 0x40, 0x53, 0x57, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x60 };

    SecondaryOptionSelect_t g_Orig = nullptr;
    void*                   g_Target = nullptr;

    bool StripOpenSEH(const std::uint8_t* cross)
    {
        __try
        {
            return cross[kCrossSecondaryOpen] != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void __fastcall hkSecondaryOptionSelect(std::uint8_t* cross)
    {
        const bool wasOpen = cross && StripOpenSEH(cross);
        g_Orig(cross);
        if (!cross || MissionCodeGuard::ShouldBypassHooks())
            return;
        if (!wasOpen && StripOpenSEH(cross))
            EquipCrossLaserBox::Show(cross, EquipCrossLaserBox::FirstSecondaryWeapon(cross));
        else if (wasOpen)
            EquipCrossLaserBox::PollSecondaryKey(cross);
    }
}

bool Install_EquipCrossEvCall_SecondaryOptionSelect_Hook()
{
    void* fn = LaserSightSwitch::CheckedEntry(gAddr.EquipCrossEvCall_SecondaryOptionSelect,
                                              kPrologue, sizeof(kPrologue), "EquipCrossEvCall::SecondaryOptionSelect");
    if (!fn)
        return false;
    if (!CreateAndEnableHook(fn, reinterpret_cast<void*>(&hkSecondaryOptionSelect), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("LaserSight: SecondaryOptionSelect hook failed, no LASER box for pistols");
        return false;
    }
    g_Target = fn;
    return true;
}

void Uninstall_EquipCrossEvCall_SecondaryOptionSelect_Hook()
{
    if (g_Target)
    {
        DisableAndRemoveHook(g_Target);
        g_Target = nullptr;
    }
    g_Orig = nullptr;
}
