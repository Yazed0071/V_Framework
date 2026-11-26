#include <pch.h>

#include <Windows.h>
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

    CheckGrenade_t g_OrigCheckGrenade = nullptr;

    bool __fastcall hk_CheckGrenade(void* self, std::uint32_t soldierIndex, std::uint8_t* knowledge)
    {
        MISSION_GUARD_ORIGINAL_RET(g_OrigCheckGrenade, self, soldierIndex, knowledge);

        const std::uint16_t rivalId = SoldierFaction_GetRivalTarget(soldierIndex);
        if (rivalId == 0 || !knowledge)
            return g_OrigCheckGrenade(self, soldierIndex, knowledge);
        if (SoldierFaction_IsEnemyHeliId(rivalId))   // a thrown grenade can't reach a heli
            return false;

        std::uint16_t targetId = 0;
        std::memcpy(&targetId, knowledge + kKnowledgeTargetId, sizeof(targetId));
        if (targetId != rivalId)
            return g_OrigCheckGrenade(self, soldierIndex, knowledge);

        SoldierFaction_BeginShotScope(soldierIndex, rivalId);
        const bool allowed = g_OrigCheckGrenade(self, soldierIndex, knowledge);
        SoldierFaction_EndShotScope();
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
