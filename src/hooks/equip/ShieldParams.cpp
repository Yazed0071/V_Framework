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
#include "log.h"

namespace
{
    using GetShieldLifeMax_t = std::uint32_t(__fastcall*)(void* equipParameterTables, std::uint32_t equipId);
    using GetShieldLevel_t   = std::uint32_t(__fastcall*)(std::uint32_t equipId);

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

    std::mutex                      g_Lock;
    std::map<std::uint32_t, Shield> g_Shields;
    std::atomic<bool>               g_Any{ false };

    GetShieldLifeMax_t g_OrigLifeMax   = nullptr;
    GetShieldLevel_t   g_OrigLevel     = nullptr;
    void*              g_LifeMaxTarget = nullptr;
    void*              g_LevelTarget   = nullptr;

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

    void* HookEntryOrNull(std::uintptr_t address, const std::uint8_t* entry, const std::uint8_t* mask, std::size_t size,
                          void* detour, void** original, const char* name)
    {
        void* target = address ? ResolveGameAddress(address) : nullptr;
        if (!target)
        {
            LogError("ShieldParams: %s address missing for %s, V_TppEquip.SetShield is off", name, GetGameBuildName(gGameBuild));
            return nullptr;
        }
        const HookEntry state = CheckHookEntryMasked(target, entry, mask, size);
        if (state == HookEntry::Unknown)
        {
            LogError("ShieldParams: %s does not match this build, V_TppEquip.SetShield is off", name);
            return nullptr;
        }
        if (state == HookEntry::ForeignHook)
            LogHookedOnTop(name, target);
        if (!CreateAndEnableHook(target, detour, original))
        {
            LogError("ShieldParams: %s hook failed, V_TppEquip.SetShield is off", name);
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
    g_LifeMaxTarget = HookEntryOrNull(gAddr.EquipParameterTablesImpl_GetShieldLifeMax, kLifeMaxEntry, kLifeMaxEntryMask, sizeof(kLifeMaxEntry),
                                      reinterpret_cast<void*>(&hkGetShieldLifeMax), reinterpret_cast<void**>(&g_OrigLifeMax),
                                      "EquipParameterTablesImpl::GetShieldLifeMax");
    g_LevelTarget = HookEntryOrNull(gAddr.PlayerEquipControllerImpl_GetShieldLevel, kLevelEntry, kLevelEntryMask, sizeof(kLevelEntry),
                                    reinterpret_cast<void*>(&hkGetShieldLevel), reinterpret_cast<void**>(&g_OrigLevel),
                                    "EquipControllerImpl::GetShieldLevel");
    return g_LifeMaxTarget != nullptr && g_LevelTarget != nullptr;
}

void Uninstall_ShieldParams_Hooks()
{
    if (g_LifeMaxTarget)
        DisableAndRemoveHook(g_LifeMaxTarget);
    if (g_LevelTarget)
        DisableAndRemoveHook(g_LevelTarget);
    g_LifeMaxTarget = nullptr;
    g_LevelTarget   = nullptr;
    g_OrigLifeMax   = nullptr;
    g_OrigLevel     = nullptr;
}
