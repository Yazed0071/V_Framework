#include <pch.h>

#include "MotherBaseDeviceTopMenuTableImpl_ApplyConditionVisibility.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "../buddy/BattleGearBuddy.h"

namespace
{
    using ApplyConditionVisibility_t = void(__fastcall*)(void* table);

    constexpr std::size_t  kCustomizeBattleGear = 0x1E8;   // MBM_CUSTOM_BUDDY_BATTLE item flags
    constexpr std::uint8_t kShown  = 0x10;
    constexpr std::uint8_t kHidden = 0x20;

    const std::uint8_t kApplyConditionVisibilityEntry[] = { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74,
                                                            0x24, 0x20, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83,
                                                            0xEC, 0x40 };

    ApplyConditionVisibility_t g_Orig = nullptr;
    void*                      g_Target = nullptr;

    void HideSEH(std::uint8_t* table)
    {
        __try
        {
            std::uint8_t& flags = table[kCustomizeBattleGear];
            flags = static_cast<std::uint8_t>((flags & ~kShown) | kHidden);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void __fastcall hkApplyConditionVisibility(void* table)
    {
        g_Orig(table);
        if (table && battleGear::SortieUnlocked())
            HideSEH(static_cast<std::uint8_t*>(table));
    }
}

bool Install_MotherBaseDeviceTopMenuTableImpl_ApplyConditionVisibility_Hook()
{
    if (g_Target)
        return true;
    void* target = ResolveGameAddress(gAddr.MotherBaseDeviceTopMenuTableImpl_ApplyConditionVisibility);
    if (!target)
    {
        LogError("BattleGear: iDroid menu visibility address missing on this build, the empty Battle Gear customize entry shows");
        return false;
    }
    const HookEntry state = CheckHookEntry(target, kApplyConditionVisibilityEntry, sizeof(kApplyConditionVisibilityEntry));
    if (state == HookEntry::Unknown)
    {
        char desc[160] = {};
        DescribeFunctionEntry(target, desc, sizeof(desc));
        LogError("BattleGear: iDroid menu visibility code differs on this build (%s), the empty Battle Gear customize entry shows", desc);
        return false;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("BattleGear: MotherBaseDeviceTopMenuTableImpl::ApplyConditionVisibility", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkApplyConditionVisibility), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("BattleGear: iDroid menu visibility hook failed, the empty Battle Gear customize entry shows");
        return false;
    }
    g_Target = target;
    return true;
}

void Uninstall_MotherBaseDeviceTopMenuTableImpl_ApplyConditionVisibility_Hook()
{
    if (!g_Target)
        return;
    DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig = nullptr;
}
