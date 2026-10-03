#include "pch.h"

#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "SoldierFaction.h"
#include "log.h"

namespace
{
    using CheckGrenade_t = bool(__fastcall*)(void* self, std::uint32_t soldierIndex, std::uint8_t* knowledge);

    constexpr std::size_t   kKnowledgeTargetId = 0x50;
    constexpr std::uint32_t kSoldierSlotCount  = 0x200u;

    CheckGrenade_t g_OrigCheckGrenade = nullptr;
    std::array<std::uint16_t, kSoldierSlotCount> g_loggedGrenade{};

    bool __fastcall hk_CheckGrenade(void* self, std::uint32_t soldierIndex, std::uint8_t* knowledge)
    {
        MISSION_GUARD_ORIGINAL_RET(g_OrigCheckGrenade, self, soldierIndex, knowledge);

        const std::uint16_t rivalId = SoldierFaction_GetRivalTarget(soldierIndex);
        if (rivalId == 0 || !knowledge)
            return g_OrigCheckGrenade(self, soldierIndex, knowledge);

        std::uint16_t targetId = 0;
        std::memcpy(&targetId, knowledge + kKnowledgeTargetId, sizeof(targetId));
        if (targetId != rivalId)
            return g_OrigCheckGrenade(self, soldierIndex, knowledge);

        SoldierFaction_BeginShotScope(soldierIndex, rivalId);
        const bool allowed = g_OrigCheckGrenade(self, soldierIndex, knowledge);
        SoldierFaction_EndShotScope();

        if (allowed && soldierIndex < kSoldierSlotCount && g_loggedGrenade[soldierIndex] != rivalId)
        {
            g_loggedGrenade[soldierIndex] = rivalId;
            LogInfo("SoldierFaction: soldier %u may throw a grenade at rival %u", soldierIndex, rivalId & 0x1FFu);
        }
        return allowed;
    }
}

bool Install_CombatAntiBuddyWalkerGearAiImpl_CheckGrenade_Hook()
{
    void* target = ResolveGameAddress(gAddr.CombatAntiBuddyWalkerGearAiImpl_CheckGrenade);
    if (!target)
        return false;
    return CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_CheckGrenade),
                               reinterpret_cast<void**>(&g_OrigCheckGrenade));
}

bool Uninstall_CombatAntiBuddyWalkerGearAiImpl_CheckGrenade_Hook()
{
    void* target = ResolveGameAddress(gAddr.CombatAntiBuddyWalkerGearAiImpl_CheckGrenade);
    if (target)
        DisableAndRemoveHook(target);
    g_OrigCheckGrenade = nullptr;
    return true;
}
