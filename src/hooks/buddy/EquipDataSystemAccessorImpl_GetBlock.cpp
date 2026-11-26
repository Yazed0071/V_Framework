#include <pch.h>

#include "EquipDataSystemAccessorImpl_GetBlock.h"

#include <cstdint>

#include "AddressSet.h"
#include "BuddyOutfitRegistry.h"
#include "EquipAliasLease.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "TppEquip_ReloadEquipIdTable.h"
#include "UixUtilityControl.h"
#include "log.h"

namespace
{
    using GetBlock_t = std::uintptr_t(__fastcall*)(std::uintptr_t accessor, std::uint32_t equipId);

    constexpr std::size_t kVarWalkerSidearm = 0x2E5D;

    constexpr std::size_t kAccessorOwner = 0x8;    // EquipDataSystemImpl::Initialize lays out 6 accessors 0x10 apart, owner 0..5
    constexpr int         kOwnerBuddy    = 3;      // looks an equipId up in the 16 (equipId, block) pairs the buddy registered

    constexpr std::uint8_t kGetBlockEntry[] =
    {
        0x40, 0x53, 0x57, 0x48, 0x83, 0xEC, 0x28, 0x83, 0x79, 0x08, 0x03, 0x8B, 0xDA, 0x48, 0x8B, 0xF9,
    };

    GetBlock_t g_Orig   = nullptr;
    void*      g_Target = nullptr;
    int        g_Users  = 0;

    bool g_SidearmsOn = false;
    bool g_OwnPacksOn = false;

    int OwnerOf(std::uintptr_t accessor)
    {
        return *reinterpret_cast<const int*>(accessor + kAccessorOwner);
    }

    bool IsSelectedWalkerSidearm(std::uint32_t equipId, buddyoutfit::Entry* e)
    {
        const std::uint8_t* vars = buddyoutfit::ReadScriptVars();
        if (!vars)
            return false;
        const std::uint8_t type = vars[kVarWalkerSidearm];
        return type >= buddyoutfit::FirstCustomValue(buddyoutfit::Kind::WalkerSidearm)
            && buddyoutfit::TryGetByValue(buddyoutfit::Kind::WalkerSidearm, type, e)
            && equip::FromEngine(static_cast<std::int32_t>(equipId)) == e->equipId;
    }

    std::uintptr_t __fastcall hkGetBlock(std::uintptr_t accessor, std::uint32_t equipId)
    {
        std::uint64_t pack = 0;
        if (g_OwnPacksOn && !MissionCodeGuard::ShouldBypassHooks()
            && TppEquip_GetOwnPack(static_cast<int>(equipId), &pack))
            return reinterpret_cast<std::uintptr_t>(UixUtilityControl::GetResidentPackBlock(pack));   // 0 while it loads: IsLoaded fails, nothing is thrown

        const std::uintptr_t block = g_Orig(accessor, equipId);
        buddyoutfit::Entry e;
        if (!g_SidearmsOn || OwnerOf(accessor) != kOwnerBuddy || !IsSelectedWalkerSidearm(equipId, &e))
            return block;
        if (!TppEquip_GetCustomEquipPack(e.equipId, &pack))
            return block;
        if (void* resident = UixUtilityControl::GetResidentPackBlock(pack))
            return reinterpret_cast<std::uintptr_t>(resident);
        if (UixUtilityControl::IsResidentPackLoading(pack))
            return 0;
        return block;
    }

    bool AddUser(const char* feature)
    {
        if (g_Target)
        {
            ++g_Users;
            return true;
        }
        void* target = ResolveGameAddress(gAddr.EquipDataSystemAccessorImpl_GetBlock);
        if (!target)
        {
            LogError("%s: equip block lookup address missing on this build", feature);
            return false;
        }
        const HookEntry state = CheckHookEntry(target, kGetBlockEntry, sizeof(kGetBlockEntry));
        if (state == HookEntry::Unknown)
        {
            char desc[160] = {};
            DescribeFunctionEntry(target, desc, sizeof(desc));
            LogError("%s: equip block lookup code differs on this build (%s)", feature, desc);
            return false;
        }
        if (state == HookEntry::ForeignHook)
            LogHookedOnTop("Equip block lookup", target);
        if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkGetBlock), reinterpret_cast<void**>(&g_Orig)))
        {
            LogError("%s: equip block lookup hook failed", feature);
            return false;
        }
        g_Target = target;
        g_Users = 1;
        return true;
    }

    void DropUser()
    {
        if (!g_Target || --g_Users > 0)
            return;
        DisableAndRemoveHook(g_Target);
        g_Target = nullptr;
        g_Orig = nullptr;
        g_Users = 0;
    }
}

bool Install_EquipDataSystemAccessorImpl_GetBlock_Hook()
{
    if (g_SidearmsOn)
        return true;
    if (!AddUser("BuddyOutfit"))
    {
        LogError("BuddyOutfit: custom D-Walker sidearms show no model");
        return false;
    }
    g_SidearmsOn = true;
    return true;
}

bool Uninstall_EquipDataSystemAccessorImpl_GetBlock_Hook()
{
    if (!g_SidearmsOn)
        return true;
    g_SidearmsOn = false;
    DropUser();
    return true;
}

bool Install_EquipOwnPack_Hook()
{
    if (g_OwnPacksOn)
        return true;
    if (!AddUser("EquipOwnPack"))
    {
        LogError("EquipOwnPack: custom throwables, placed items and items with their own pack show no model");
        return false;
    }
    g_OwnPacksOn = true;
    return true;
}

bool Uninstall_EquipOwnPack_Hook()
{
    if (!g_OwnPacksOn)
        return true;
    g_OwnPacksOn = false;
    DropUser();
    return true;
}
