#include <pch.h>

#include "EquipBlockController2_StartLoadingToEquipMissionBlock.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

#include "AddressSet.h"
#include "BuddyOutfitRegistry.h"
#include "EquipDevelopControllerImpl_GetDWalkerParts.h"
#include "HookUtils.h"
#include "LuaApi.h"
#include "MissionCodeGuard.h"
#include "TppEquip_ReloadEquipIdTable.h"
#include "UixUtilityControl.h"
#include "log.h"

namespace
{
    using LuaFn_t = int(__cdecl*)(lua_State* L);

    constexpr std::size_t   kVarBuddyType      = 0x2D68;
    constexpr std::size_t   kVarWalkerSidearm  = 0x2E5D;
    constexpr std::uint32_t kSidearmPackBytes  = 4u << 20;
    constexpr std::size_t   kKeptSidearmPacks  = 3;

    constexpr std::uint8_t kStartLoadingEntry[] =
    {
        0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0x0D, 0x35, 0xA4, 0x21, 0x02, 0xE8, 0x30, 0xF4, 0xFF, 0xFF,
    };
    constexpr std::size_t kInstanceRel = 7;   // rel32 of mov rcx, [0x142C1E3B0]: differs between EN and JP

    LuaFn_t g_Orig   = nullptr;
    void*   g_Target = nullptr;

    std::mutex                 g_PackMutex;
    std::vector<std::uint64_t> g_SidearmPacks;

    std::uint64_t AskResidentPack(std::uint16_t equipId, const char* key)
    {
        std::uint64_t pack = 0;
        if (!TppEquip_GetCustomEquipPack(equipId, &pack)
            || !UixUtilityControl::LoadResidentPackById(pack, kSidearmPackBytes, key))
            return 0;
        std::vector<std::uint64_t> release;
        {
            std::lock_guard<std::mutex> lock(g_PackMutex);
            g_SidearmPacks.erase(std::remove(g_SidearmPacks.begin(), g_SidearmPacks.end(), pack), g_SidearmPacks.end());
            g_SidearmPacks.push_back(pack);
            while (g_SidearmPacks.size() > kKeptSidearmPacks)
            {
                release.push_back(g_SidearmPacks.front());
                g_SidearmPacks.erase(g_SidearmPacks.begin());
            }
        }
        for (const std::uint64_t old : release)
            UixUtilityControl::ReleaseResidentPackById(old);
        return pack;
    }

    void LoadSelectedSidearmPackForMission()
    {
        const std::uint8_t* vars = buddyoutfit::ReadScriptVars();
        if (!vars)
            return;
        buddyoutfit::Entry e;
        std::uint64_t keep = 0;
        const std::uint8_t type = vars[kVarWalkerSidearm];
        if (vars[kVarBuddyType] == buddyoutfit::kBuddyTypeWalker
            && type >= buddyoutfit::FirstCustomValue(buddyoutfit::Kind::WalkerSidearm)
            && buddyoutfit::TryGetByValue(buddyoutfit::Kind::WalkerSidearm, type, &e)
            && IsUsableWalkerSidearm(e))
            TppEquip_GetCustomEquipPack(e.equipId, &keep);

        std::vector<std::uint64_t> release;
        {
            std::lock_guard<std::mutex> lock(g_PackMutex);
            for (const std::uint64_t pack : g_SidearmPacks)
                if (pack != keep)
                    release.push_back(pack);
            g_SidearmPacks.erase(std::remove_if(g_SidearmPacks.begin(), g_SidearmPacks.end(),
                [keep](std::uint64_t pack) { return pack != keep; }), g_SidearmPacks.end());
        }
        for (const std::uint64_t pack : release)
            UixUtilityControl::ReleaseResidentPackById(pack);
        if (keep)
            AskResidentPack(e.equipId, e.key.c_str());
    }

    int __cdecl hkStartLoadingToEquipMissionBlock(lua_State* L)
    {
        if (buddyoutfit::HasEntries() && !MissionCodeGuard::ShouldBypassHooks())
            LoadSelectedSidearmPackForMission();
        return g_Orig(L);
    }
}

void RequestWalkerSidearmPack(std::uint16_t equipId, const char* key)
{
    AskResidentPack(equipId, key);
}

bool Install_EquipBlockController2_StartLoadingToEquipMissionBlock_Hook()
{
    if (g_Target)
        return true;
    void* target = ResolveGameAddress(gAddr.EquipBlockController2_StartLoadingToEquipMissionBlock);
    if (!target)
    {
        LogError("BuddyOutfit: equip mission block address missing on this build, custom D-Walker sidearms load no model");
        return false;
    }
    std::uint8_t mask[sizeof(kStartLoadingEntry)];
    std::memset(mask, 1, sizeof(mask));
    std::memset(mask + kInstanceRel, 0, 4);
    const HookEntry state = CheckHookEntryMasked(target, kStartLoadingEntry, mask, sizeof(kStartLoadingEntry));
    if (state == HookEntry::Unknown)
    {
        char desc[160] = {};
        DescribeFunctionEntry(target, desc, sizeof(desc));
        LogError("BuddyOutfit: StartLoadingToEquipMissionBlock code differs on this build (%s), custom D-Walker sidearms load no model", desc);
        return false;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("BuddyOutfit: StartLoadingToEquipMissionBlock", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkStartLoadingToEquipMissionBlock),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("BuddyOutfit: StartLoadingToEquipMissionBlock hook failed, custom D-Walker sidearms load no model");
        return false;
    }
    g_Target = target;
    return true;
}

bool Uninstall_EquipBlockController2_StartLoadingToEquipMissionBlock_Hook()
{
    if (!g_Target)
        return true;
    DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig = nullptr;
    return true;
}
