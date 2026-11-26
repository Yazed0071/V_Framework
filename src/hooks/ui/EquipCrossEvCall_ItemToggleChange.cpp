#include <pch.h>

#include "EquipCrossEvCall_ItemToggleChange.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"
#include "../equip/LaserSightSwitch.h"

namespace
{
    using ItemToggleChange_t = void(__fastcall*)(std::uint8_t* cross, char commit);

    const std::uint8_t kPrologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20,
        0x0F, 0xB6, 0xB1, 0x52, 0x02, 0x00, 0x00 };

    ItemToggleChange_t g_Orig = nullptr;
    void*              g_Target = nullptr;

    void __fastcall hkItemToggleChange(std::uint8_t* cross, char commit)
    {
        if (LaserSightSwitch::AimKeyToggledJustNow() && !MissionCodeGuard::ShouldBypassHooks())
            return;   // the D-pad Left tap went to the laser, keep the item the player has out
        g_Orig(cross, commit);
    }
}

bool Install_EquipCrossEvCall_ItemToggleChange_Hook()
{
    void* fn = LaserSightSwitch::CheckedEntry(gAddr.EquipCrossEvCall_ItemToggleChange,
                                              kPrologue, sizeof(kPrologue), "EquipCrossEvCall::ItemToggleChange");
    if (!fn)
        return false;
    if (!CreateAndEnableHook(fn, reinterpret_cast<void*>(&hkItemToggleChange), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("LaserSight: ItemToggleChange hook failed, D-pad Left may also switch the item while aiming");
        return false;
    }
    g_Target = fn;
    return true;
}

void Uninstall_EquipCrossEvCall_ItemToggleChange_Hook()
{
    if (g_Target)
    {
        DisableAndRemoveHook(g_Target);
        g_Target = nullptr;
    }
    g_Orig = nullptr;
}
