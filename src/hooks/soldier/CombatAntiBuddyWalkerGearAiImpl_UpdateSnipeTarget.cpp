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
    using UpdateSnipeTarget_t = void(__fastcall*)(void* self, std::uint32_t soldierIndex, std::uint8_t* knowledge);

    constexpr std::size_t   kKnowledgeFlags       = 0x48;
    constexpr std::size_t   kKnowledgeTargetId    = 0x50;
    constexpr std::uint32_t kFlagWalkerGearRider  = 0x00800000u;
    constexpr std::uint32_t kFlagClearShot        = 0x00200000u;
    constexpr std::uint32_t kSoldierSlotCount     = 0x200u;

    UpdateSnipeTarget_t g_OrigUpdateSnipeTarget = nullptr;
    std::array<std::uint16_t, kSoldierSlotCount> g_loggedClearShot{};

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

        std::uint32_t flags = 0;
        std::memcpy(&flags, knowledge + kKnowledgeFlags, sizeof(flags));
        if ((flags & kFlagClearShot) != 0 && soldierIndex < kSoldierSlotCount
            && g_loggedClearShot[soldierIndex] != rivalId)
        {
            g_loggedClearShot[soldierIndex] = rivalId;
            LogInfo("SoldierFaction: soldier %u has a clear %s shot at rival %u", soldierIndex,
                (flags & kFlagWalkerGearRider) != 0 ? "walker gear" : "aimed", rivalId & 0x1FFu);
        }
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
