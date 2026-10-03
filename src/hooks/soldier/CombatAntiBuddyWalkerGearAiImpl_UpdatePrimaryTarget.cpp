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
    using UpdatePrimaryTarget_t = bool(__fastcall*)(void* self, std::uint32_t soldierIndex, std::uint8_t* knowledge);

    constexpr std::size_t   kNodeSoldierCore      = 0x1A0;
    constexpr std::size_t   kKnowledgeTargetPoint = 0x10;
    constexpr std::size_t   kKnowledgeTargetId    = 0x50;
    constexpr std::size_t   kKnowledgeStep        = 0x54;
    constexpr std::size_t   kKnowledgePrevTarget  = 0x60;
    constexpr std::size_t   kKnowledgeMount       = 0x98;
    constexpr std::size_t   kTargetPointSize      = 0x10;
    constexpr std::uint32_t kSoldierSlotCount     = 0x200u;
    constexpr std::uint8_t  kNoStepLogged         = 0xFFu;

    UpdatePrimaryTarget_t g_OrigUpdatePrimaryTarget = nullptr;
    std::array<std::uint8_t, kSoldierSlotCount> g_lastLoggedStep = [] {
        std::array<std::uint8_t, kSoldierSlotCount> steps{};
        steps.fill(kNoStepLogged);
        return steps;
    }();

    void LogStepChange(std::uint32_t soldierIndex, std::uint8_t step)
    {
        if (soldierIndex >= kSoldierSlotCount || g_lastLoggedStep[soldierIndex] == step)
            return;
        LogInfo("SoldierFaction: soldier %u combat step %u -> %u",
            soldierIndex, g_lastLoggedStep[soldierIndex], step);
        g_lastLoggedStep[soldierIndex] = step;
    }

    void RetargetToRival(void* self, std::uint8_t* knowledge, std::uint16_t rivalId)
    {
        const void* soldierCore = nullptr;
        std::memcpy(&soldierCore, static_cast<const std::uint8_t*>(self) + kNodeSoldierCore, sizeof(soldierCore));

        std::memcpy(knowledge + kKnowledgePrevTarget, knowledge + kKnowledgeTargetPoint, kTargetPointSize);
        std::memcpy(knowledge + kKnowledgeTargetId, &rivalId, sizeof(rivalId));

        float aimPoint[3];
        if (SoldierFaction_ReadRivalAimPoint(soldierCore, rivalId, aimPoint))
            std::memcpy(knowledge + kKnowledgeTargetPoint, aimPoint, sizeof(aimPoint));
    }

    bool __fastcall hk_UpdatePrimaryTarget(void* self, std::uint32_t soldierIndex, std::uint8_t* knowledge)
    {
        MISSION_GUARD_ORIGINAL_RET(g_OrigUpdatePrimaryTarget, self, soldierIndex, knowledge);

        const std::uint16_t rivalId = SoldierFaction_GetRivalTarget(soldierIndex);
        if (rivalId == 0 || !knowledge)
            return g_OrigUpdatePrimaryTarget(self, soldierIndex, knowledge);

        SoldierFaction_MarkRivalNodeAlive(soldierIndex);
        std::uint16_t mountId = 0;
        std::memcpy(&mountId, knowledge + kKnowledgeMount, sizeof(mountId));
        SoldierFaction_SetMount(soldierIndex, mountId);
        LogStepChange(soldierIndex, knowledge[kKnowledgeStep]);
        RetargetToRival(self, knowledge, rivalId);
        return true;
    }
}

bool Install_CombatAntiBuddyWalkerGearAiImpl_UpdatePrimaryTarget_Hook()
{
    void* target = ResolveGameAddress(gAddr.CombatAntiBuddyWalkerGearAiImpl_UpdatePrimaryTarget);
    if (!target)
        return false;
    return CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_UpdatePrimaryTarget),
                               reinterpret_cast<void**>(&g_OrigUpdatePrimaryTarget));
}

bool Uninstall_CombatAntiBuddyWalkerGearAiImpl_UpdatePrimaryTarget_Hook()
{
    void* target = ResolveGameAddress(gAddr.CombatAntiBuddyWalkerGearAiImpl_UpdatePrimaryTarget);
    if (target)
        DisableAndRemoveHook(target);
    g_OrigUpdatePrimaryTarget = nullptr;
    return true;
}
