#include <pch.h>
#include "ShieldParams.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>

#include "AddressSet.h"
#include "EquipAliasLease.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "TppEquip_ReloadEquipIdTable.h"
#include "log.h"

namespace
{
    using GetShieldLifeMax_t = std::uint32_t(__fastcall*)(void* equipParameterTables, std::uint32_t equipId);
    using GetShieldLevel_t   = std::uint32_t(__fastcall*)(std::uint32_t equipId);
    using GetFovaFile_t      = void*(__fastcall*)(void* equipSystem, std::uint32_t equipId, const void* block, bool* flag);

    struct Shield
    {
        std::uint32_t durability = 0;
        std::uint32_t level      = 0;
    };

    // both functions are a switch on the shield ids of the game and answer 0 for every other id
    const std::uint8_t kLifeMaxEntry[]     = { 0x83, 0xC2, 0x97, 0x83, 0xFA, 0x1B, 0x77, 0x36, 0x48, 0x63, 0xC2, 0x48, 0x8D, 0x15,
                                               0x00, 0x00, 0x00, 0x00, 0x0F, 0xB6, 0x84, 0x02 };
    const std::uint8_t kLifeMaxEntryMask[] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1 };
    const std::uint8_t kLevelEntry[]       = { 0x83, 0xC1, 0x96, 0x83, 0xF9, 0x19, 0x77, 0x2A, 0x48, 0x8D, 0x15,
                                               0x00, 0x00, 0x00, 0x00, 0x48, 0x63, 0xC1, 0x0F, 0xB6, 0x84, 0x02 };
    const std::uint8_t kLevelEntryMask[]   = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1 };
    // a switch on the game's equipIds handing out each shield's paint (fova); any other id gets none
    const std::uint8_t kFovaFileEntry[]     = { 0x4C, 0x8B, 0xD1, 0x81, 0xFA, 0x6B, 0x01, 0x00, 0x00 };
    const std::uint8_t kFovaFileEntryMask[] = { 1, 1, 1, 1, 1, 1, 1, 1, 1 };

    std::mutex                      g_Lock;
    std::map<std::uint32_t, Shield> g_Shields;
    std::atomic<bool>               g_Any{ false };

    std::map<std::uint32_t, std::uint32_t> g_PaintDonors;   // custom shield -> vanilla shield whose paint it wears, 0 = none
    std::uint32_t                          g_PaintEpoch = 0;

    GetShieldLifeMax_t g_OrigLifeMax    = nullptr;
    GetShieldLevel_t   g_OrigLevel      = nullptr;
    GetFovaFile_t      g_OrigFovaFile   = nullptr;
    void*              g_LifeMaxTarget  = nullptr;
    void*              g_LevelTarget    = nullptr;
    void*              g_FovaFileTarget = nullptr;

    bool Find(std::uint32_t engineEquipId, Shield& shield)
    {
        if (!g_Any.load(std::memory_order_acquire) || MissionCodeGuard::ShouldBypassHooks())
            return false;
        const auto equipId = static_cast<std::uint32_t>(equip::FromEngine(static_cast<std::int32_t>(engineEquipId)));
        std::lock_guard<std::mutex> lock(g_Lock);
        const auto it = g_Shields.find(equipId);
        if (it == g_Shields.end())
            return false;
        shield = it->second;
        return true;
    }

    std::uint32_t __fastcall hkGetShieldLifeMax(void* equipParameterTables, std::uint32_t equipId)
    {
        Shield shield;
        if (Find(equipId, shield))
            return shield.durability;
        return g_OrigLifeMax(equipParameterTables, equipId);
    }

    std::uint32_t __fastcall hkGetShieldLevel(std::uint32_t equipId)
    {
        Shield shield;
        if (Find(equipId, shield))
            return shield.level;
        return g_OrigLevel(equipId);
    }

    std::uint32_t PaintDonor(std::uint32_t engineEquipId)
    {
        const auto equipId = static_cast<std::uint32_t>(equip::FromEngine(static_cast<std::int32_t>(engineEquipId)));
        const std::uint32_t epoch = TppEquip_EquipTableEpoch();
        {
            std::lock_guard<std::mutex> lock(g_Lock);
            if (g_PaintEpoch != epoch)
            {
                g_PaintDonors.clear();
                g_PaintEpoch = epoch;
            }
            const auto it = g_PaintDonors.find(equipId);
            if (it != g_PaintDonors.end())
                return it->second;
        }
        const auto donor = static_cast<std::uint32_t>(TppEquip_FindShieldPaintDonor(static_cast<int>(equipId)));
        std::lock_guard<std::mutex> lock(g_Lock);
        g_PaintDonors[equipId] = donor;
        return donor;
    }

    void* __fastcall hkGetFovaFileByEquipIdInBlock(void* equipSystem, std::uint32_t equipId, const void* block, bool* flag)
    {
        void* fova = g_OrigFovaFile(equipSystem, equipId, block, flag);
        if (fova || !block || MissionCodeGuard::ShouldBypassHooks())
            return fova;
        const std::uint32_t donor = PaintDonor(equipId);
        return donor != 0 ? g_OrigFovaFile(equipSystem, donor, block, flag) : nullptr;
    }

    void* HookEntryOrNull(std::uintptr_t address, const std::uint8_t* entry, const std::uint8_t* mask, std::size_t size,
                          void* detour, void** original, const char* name, const char* consequence)
    {
        void* target = address ? ResolveGameAddress(address) : nullptr;
        if (!target)
        {
            LogError("ShieldParams: %s address missing for %s, %s", name, GetGameBuildName(gGameBuild), consequence);
            return nullptr;
        }
        const HookEntry state = CheckHookEntryMasked(target, entry, mask, size);
        if (state == HookEntry::Unknown)
        {
            LogError("ShieldParams: %s does not match this build, %s", name, consequence);
            return nullptr;
        }
        if (state == HookEntry::ForeignHook)
            LogHookedOnTop(name, target);
        if (!CreateAndEnableHook(target, detour, original))
        {
            LogError("ShieldParams: %s hook failed, %s", name, consequence);
            return nullptr;
        }
        return target;
    }
}

void ShieldParams::Set(std::uint32_t equipId, std::uint32_t durability, std::uint32_t level)
{
    std::lock_guard<std::mutex> lock(g_Lock);
    Shield& shield = g_Shields[equipId];
    shield.durability = durability;
    shield.level      = level;
    g_Any.store(true, std::memory_order_release);
}

bool Install_ShieldParams_Hooks()
{
    const char* setShieldOff = "V_TppEquip.SetShield is off";
    g_LifeMaxTarget = HookEntryOrNull(gAddr.EquipParameterTablesImpl_GetShieldLifeMax, kLifeMaxEntry, kLifeMaxEntryMask, sizeof(kLifeMaxEntry),
                                      reinterpret_cast<void*>(&hkGetShieldLifeMax), reinterpret_cast<void**>(&g_OrigLifeMax),
                                      "EquipParameterTablesImpl::GetShieldLifeMax", setShieldOff);
    g_LevelTarget = HookEntryOrNull(gAddr.PlayerEquipControllerImpl_GetShieldLevel, kLevelEntry, kLevelEntryMask, sizeof(kLevelEntry),
                                    reinterpret_cast<void*>(&hkGetShieldLevel), reinterpret_cast<void**>(&g_OrigLevel),
                                    "EquipControllerImpl::GetShieldLevel", setShieldOff);
    g_FovaFileTarget = HookEntryOrNull(gAddr.EquipSystemImpl_GetFovaFileByEquipIdInBlock, kFovaFileEntry, kFovaFileEntryMask,
                                       sizeof(kFovaFileEntry), reinterpret_cast<void*>(&hkGetFovaFileByEquipIdInBlock),
                                       reinterpret_cast<void**>(&g_OrigFovaFile), "EquipSystemImpl::GetFovaFileByEquipIdInBlock",
                                       "custom shields stay unpainted (white)");
    return g_LifeMaxTarget != nullptr && g_LevelTarget != nullptr;
}

void Uninstall_ShieldParams_Hooks()
{
    if (g_LifeMaxTarget)
        DisableAndRemoveHook(g_LifeMaxTarget);
    if (g_LevelTarget)
        DisableAndRemoveHook(g_LevelTarget);
    if (g_FovaFileTarget)
        DisableAndRemoveHook(g_FovaFileTarget);
    g_LifeMaxTarget  = nullptr;
    g_LevelTarget    = nullptr;
    g_FovaFileTarget = nullptr;
    g_OrigLifeMax    = nullptr;
    g_OrigLevel      = nullptr;
    g_OrigFovaFile   = nullptr;
}
