#include <pch.h>

#include "CustomizeUtilityImpl_GetBuddyUsageCost.h"

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "../buddy/BattleGearArmory.h"
#include "../buddy/BattleGearBuddy.h"

namespace
{
    using GetBuddyUsageCost_t = std::uint32_t(__fastcall*)(void* utility, std::uint8_t buddyType);

    const std::uint8_t kGetBuddyUsageCostEntry[] = { 0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x55, 0x48,
                                                     0x8D, 0x6C, 0x24, 0xA9, 0x48, 0x81, 0xEC, 0xA0, 0x00, 0x00, 0x00 };

    GetBuddyUsageCost_t g_Orig = nullptr;
    void*               g_Target = nullptr;

    std::uint32_t __fastcall hkGetBuddyUsageCost(void* utility, std::uint8_t buddyType)
    {
        if (!battleGear::IsHostedType(buddyType))
            return g_Orig(utility, buddyType);
        return battleGearArmory::FittedDeployCost();
    }
}

bool Install_CustomizeUtilityImpl_GetBuddyUsageCost_Hook()
{
    if (g_Target)
        return true;
    void* target = gAddr.CustomizeUtilityImpl_GetBuddyUsageCost ? ResolveGameAddress(gAddr.CustomizeUtilityImpl_GetBuddyUsageCost) : nullptr;
    if (!target)
    {
        LogWarn("BattleGear: buddy deploy cost address missing on this build, the Battle Gear deploys for free");
        return false;
    }
    const HookEntry state = CheckHookEntry(target, kGetBuddyUsageCostEntry, sizeof(kGetBuddyUsageCostEntry));
    if (state == HookEntry::Unknown)
    {
        char desc[160] = {};
        DescribeFunctionEntry(target, desc, sizeof(desc));
        LogWarn("BattleGear: buddy deploy cost code differs on this build (%s), the Battle Gear deploys for free", desc);
        return false;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("BattleGear: buddy deploy cost", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkGetBuddyUsageCost), reinterpret_cast<void**>(&g_Orig)))
    {
        LogWarn("BattleGear: buddy deploy cost hook failed, the Battle Gear deploys for free");
        return false;
    }
    g_Target = target;
    return true;
}

void Uninstall_CustomizeUtilityImpl_GetBuddyUsageCost_Hook()
{
    if (!g_Target)
        return;
    DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig = nullptr;
}
