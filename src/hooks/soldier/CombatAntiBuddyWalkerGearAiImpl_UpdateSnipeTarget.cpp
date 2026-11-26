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
    using UpdateSnipeTarget_t = void(__fastcall*)(void* self, std::uint32_t soldierIndex, std::uint8_t* knowledge);

    constexpr std::size_t   kKnowledgeTargetId    = 0x50;

    UpdateSnipeTarget_t g_OrigUpdateSnipeTarget = nullptr;

    void __fastcall hk_UpdateSnipeTarget(void* self, std::uint32_t soldierIndex, std::uint8_t* knowledge)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigUpdateSnipeTarget, self, soldierIndex, knowledge);

        const std::uint16_t rivalId = SoldierFaction_GetRivalTarget(soldierIndex);
        if (rivalId == 0 || !knowledge)
        {
            g_OrigUpdateSnipeTarget(self, soldierIndex, knowledge);
            return;
        }

        std::uint16_t targetId = 0;
        std::memcpy(&targetId, knowledge + kKnowledgeTargetId, sizeof(targetId));
        if (targetId != rivalId)
        {
            g_OrigUpdateSnipeTarget(self, soldierIndex, knowledge);
            return;
        }

        SoldierFaction_BeginShotScope(soldierIndex, rivalId);
        g_OrigUpdateSnipeTarget(self, soldierIndex, knowledge);
        SoldierFaction_EndShotScope();
    }
}

bool Install_CombatAntiBuddyWalkerGearAiImpl_UpdateSnipeTarget_Hook()
{
    void* target = ResolveGameAddress(gAddr.CombatAntiBuddyWalkerGearAiImpl_UpdateSnipeTarget);
    if (!target)
        return false;
    return CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_UpdateSnipeTarget),
                               reinterpret_cast<void**>(&g_OrigUpdateSnipeTarget));
}

bool Uninstall_CombatAntiBuddyWalkerGearAiImpl_UpdateSnipeTarget_Hook()
{
    void* target = ResolveGameAddress(gAddr.CombatAntiBuddyWalkerGearAiImpl_UpdateSnipeTarget);
    if (target)
        DisableAndRemoveHook(target);
    g_OrigUpdateSnipeTarget = nullptr;
    return true;
}
