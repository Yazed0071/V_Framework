#include <pch.h>

#include "BuddyServiceImpl_BuddyEquipSettings.h"

#include <atomic>
#include <cstdint>

#include "AddressSet.h"
#include "BattleGearBuddy.h"
#include "BattleGearLoadoutSlots.h"
#include "HookUtils.h"
#include "log.h"

#ifdef _DEBUG
namespace
{
    using BuddyEquipSettings_t = bool(__fastcall*)(void* service, std::uint8_t buddyType, const void* param);

    constexpr int kLoggedCalls = 20;

    const std::uint8_t kBuddyEquipSettingsEntry[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48,
        0x8B, 0x01, 0x49, 0x8B, 0xD8, 0x0F, 0xB6, 0xF2
    };

    BuddyEquipSettings_t g_Orig = nullptr;
    void*                g_Target = nullptr;
    std::atomic<int>     g_Logged{ 0 };

    // the model viewer changes the loaded buddy's equipment this way; the game has no branch for buddy type 5 and failed the change
    bool __fastcall hkBuddyEquipSettings(void* service, std::uint8_t buddyType, const void* param)
    {
        if (buddyType != battleGear::kBuddyType || !param)
            return g_Orig(service, buddyType, param);
        battleGearLoadoutSlots::ShowOnModel(static_cast<const std::uint8_t*>(param));
        if (g_Logged.fetch_add(1, std::memory_order_relaxed) < kLoggedCalls)
            LogDebug("BattleGear: BuddyEquipSettings for Battle Gear: head %u shown on the model", battleGearLoadoutSlots::ShownHeadType());   // TEMP probe
        return true;
    }
}
#endif

bool Install_BuddyServiceImpl_BuddyEquipSettings_Hook()
{
#ifndef _DEBUG
    return true;
#else
    if (g_Target)
        return true;
    void* target = ResolveGameAddress(gAddr.BuddyServiceImpl_BuddyEquipSettings);
    if (!target)
        return false;
    const HookEntry state = CheckHookEntry(target, kBuddyEquipSettingsEntry, sizeof(kBuddyEquipSettingsEntry));
    if (state == HookEntry::Unknown)
    {
        char desc[160] = {};
        DescribeFunctionEntry(target, desc, sizeof(desc));
        LogWarn("BattleGear: BuddyEquipSettings code differs on this build (%s), the menus' Battle Gear model keeps its own head", desc);
        return false;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("BattleGear: BuddyEquipSettings", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkBuddyEquipSettings), reinterpret_cast<void**>(&g_Orig)))
    {
        LogWarn("BattleGear: BuddyEquipSettings hook failed, the menus' Battle Gear model keeps its own head");
        return false;
    }
    g_Target = target;
    return true;
#endif
}

void Uninstall_BuddyServiceImpl_BuddyEquipSettings_Hook()
{
#ifdef _DEBUG
    if (!g_Target)
        return;
    DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig = nullptr;
#endif
}
