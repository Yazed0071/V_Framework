#include <pch.h>

#include "Buddy2BlockControllerImpl_SubBlockSetting.h"

#include <cstdint>

#include "AddressSet.h"
#include "BattleGearArmory.h"
#include "BattleGearBuddy.h"
#include "BattleGearWeapons.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using SubBlockSetting_t = void(__fastcall*)(void* self, std::uint8_t buddyType);

    constexpr std::size_t kWeaponPackWanted = 0x191;   // sub block slot 2 (the D-Walker weapon pack): its main weapon type, 0 = none

    const std::uint8_t kSubBlockSettingEntry[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48,
        0x89, 0x7C, 0x24, 0x20, 0x41, 0x54, 0x41, 0x56
    };

    SubBlockSetting_t g_Orig = nullptr;
    void*             g_Target = nullptr;

    void __fastcall hkSubBlockSetting(void* self, std::uint8_t buddyType)
    {
        if (!battleGear::IsHostedType(buddyType))
        {
            battleGear::NoteBuddyBlock(self);
            g_Orig(self, buddyType);
            return;
        }
        const battleGear::BlockSetUp setUp(self);
        battleGearArmory::Latch();
        battleGearWeapons::LatchLoadout();
        g_Orig(self, battleGear::kHostBuddyType);
        const bool applies = battleGearWeapons::LoadoutApplies();
        if (self && applies)
            static_cast<std::uint8_t*>(self)[kWeaponPackWanted] = battleGearWeapons::MainWeaponType();
        LogDebug("BattleGear: buddy block set up, main gun %s (%s)", battleGearWeapons::ChosenMainWeaponName(),
                 applies ? "Battle Gear loadout" : "ACC or loadout off: the D-Walker loadout");
    }
}

bool Install_Buddy2BlockControllerImpl_SubBlockSetting_Hook()
{
    if (g_Target)
        return true;
    void* target = ResolveGameAddress(gAddr.Buddy2BlockControllerImpl_SubBlockSetting);
    if (!target)
    {
        LogError("BattleGear: buddy sub block setup address missing on this build, Battle Gear is off");
        return false;
    }
    const HookEntry state = CheckHookEntry(target, kSubBlockSettingEntry, sizeof(kSubBlockSettingEntry));
    if (state == HookEntry::Unknown)
    {
        char desc[160] = {};
        DescribeFunctionEntry(target, desc, sizeof(desc));
        LogError("BattleGear: buddy sub block setup code differs on this build (%s), Battle Gear is off", desc);
        return false;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("BattleGear: SubBlockSetting", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkSubBlockSetting), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("BattleGear: buddy sub block setup hook failed, Battle Gear is off");
        return false;
    }
    g_Target = target;
    return true;
}

void Uninstall_Buddy2BlockControllerImpl_SubBlockSetting_Hook()
{
    if (!g_Target)
        return;
    DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig = nullptr;
}
