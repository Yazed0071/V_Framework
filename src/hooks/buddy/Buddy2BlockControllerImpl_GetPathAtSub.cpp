#include <pch.h>

#include "Buddy2BlockControllerImpl_GetPathAtSub.h"

#include <cstdint>

#include "AddressSet.h"
#include "BattleGearArmory.h"
#include "BattleGearBuddy.h"
#include "BattleGearWeapons.h"
#include "FoxHashes.h"
#include "FoxPathInternal.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using GetPathAtSub_t = std::uint64_t*(__fastcall*)(void* self, std::uint64_t* outPath, std::int32_t slot,
                                                       std::uint64_t index, std::int32_t buddyType);
    using FoxPath_Path_t = void*(__fastcall*)(void* path, std::uint64_t code64ext);

    constexpr std::int32_t kWeaponSlot   = 2;
    constexpr const char*  kRailGunPack  = "/Assets/tpp/pack/buddy/battlegear/buddy_btg_wp_02.fpk";   // stun gun hidden, rail-gun blast
    constexpr const char*  kHMissilePack = "/Assets/tpp/pack/buddy/battlegear/buddy_btg_wp_01.fpk";   // missile pod hidden
    constexpr const char*  kStunGunPack  = "/Assets/tpp/pack/buddy/walkergear/bw_wp_02.fpk";
    constexpr const char*  kMissilePack  = "/Assets/tpp/pack/buddy/walkergear/bw_wp_01.fpk";

    FoxPath_Path_t g_FoxPath_Path = nullptr;

    std::uint64_t InstalledPack(std::uint64_t code)
    {
        return code && fox::detail::PathExistsByCode(code) ? code : 0;
    }

    struct WeaponPackChoice
    {
        std::uint64_t code = 0;
        bool          own = false;
    };

    WeaponPackChoice WeaponPack()
    {
        static const std::uint64_t railGun = FoxHashes::PathCode64Ext(kRailGunPack);
        static const std::uint64_t hMissile = FoxHashes::PathCode64Ext(kHMissilePack);
        static const std::uint64_t stunGun = FoxHashes::PathCode64Ext(kStunGunPack);
        static const std::uint64_t missile = FoxHashes::PathCode64Ext(kMissilePack);
        if (const std::uint64_t registered = InstalledPack(battleGearArmory::Latched().fpkPath))
            return { registered, true };
        const bool hMissileFitted = battleGearWeapons::MainWeaponType() == battleGearWeapons::kHMissile;
        if (const std::uint64_t own = InstalledPack(hMissileFitted ? hMissile : railGun))
            return { own, true };
        return { InstalledPack(hMissileFitted ? missile : stunGun), false };
    }

    const std::uint8_t kGetPathAtSubEntry[] = {
        0x48, 0x89, 0x54, 0x24, 0x10, 0x55, 0x57, 0x41, 0x56, 0x48, 0x8B, 0xEC, 0x48, 0x83, 0xEC, 0x50,
        0x48, 0xC7, 0x45, 0xF8, 0xFE, 0xFF, 0xFF, 0xFF
    };

    GetPathAtSub_t g_Orig = nullptr;
    void*          g_Target = nullptr;

    std::uint64_t* __fastcall hkGetPathAtSub(void* self, std::uint64_t* outPath, std::int32_t slot,
                                             std::uint64_t index, std::int32_t buddyType)
    {
        const bool hosted = battleGear::IsHostedType(buddyType);
        if (slot == kWeaponSlot)
            battleGearWeapons::SetHideMainGunAtSetup(hosted);
        if (!hosted)
        {
            battleGear::NoteBuddyBlock(self);
            return g_Orig(self, outPath, slot, index, buddyType);
        }
        const battleGear::BlockSetUp setUp(self);
        if (slot == kWeaponSlot && outPath && g_FoxPath_Path && battleGearWeapons::LoadoutApplies())
        {
            const WeaponPackChoice pack = WeaponPack();
            if (pack.code)
            {
                battleGearWeapons::SetHideMainGunAtSetup(!pack.own);
                g_FoxPath_Path(outPath, pack.code);
                return outPath;
            }
        }
        return g_Orig(self, outPath, slot, index, battleGear::kHostBuddyType);   // the D-Walker's sub packs, common pack included
    }
}

bool Install_Buddy2BlockControllerImpl_GetPathAtSub_Hook()
{
    if (g_Target)
        return true;
    g_FoxPath_Path = reinterpret_cast<FoxPath_Path_t>(ResolveGameAddress(gAddr.FoxPath_Path));
    void* target = ResolveGameAddress(gAddr.Buddy2BlockControllerImpl_GetPathAtSub);
    if (!target)
    {
        LogError("BattleGear: buddy sub pack lookup address missing on this build, Battle Gear is off");
        return false;
    }
    const HookEntry state = CheckHookEntry(target, kGetPathAtSubEntry, sizeof(kGetPathAtSubEntry));
    if (state == HookEntry::Unknown)
    {
        char desc[160] = {};
        DescribeFunctionEntry(target, desc, sizeof(desc));
        LogError("BattleGear: buddy sub pack lookup code differs on this build (%s), Battle Gear is off", desc);
        return false;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("BattleGear: GetPathAtSub", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkGetPathAtSub), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("BattleGear: buddy sub pack lookup hook failed, Battle Gear is off");
        return false;
    }
    g_Target = target;
    return true;
}

void Uninstall_Buddy2BlockControllerImpl_GetPathAtSub_Hook()
{
    if (!g_Target)
        return;
    DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig = nullptr;
}
