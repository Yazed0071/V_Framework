#include "pch.h"

#include <Windows.h>
#include <array>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "SoldierFaction.h"
#include "log.h"

namespace
{
    using HasOtherSoldierOnVec_t = bool(__fastcall*)(void* self, std::uint32_t soldierIndex, void* work,
                                                     const float* start, const float* aim, const float* direction,
                                                     float length, void* context);

    constexpr std::uint32_t kSoldierSlotCount = 0x200u;

    HasOtherSoldierOnVec_t g_OrigHasOtherSoldierOnVec = nullptr;
    std::array<std::uint16_t, kSoldierSlotCount> g_loggedFiring{};
    std::array<std::uint16_t, kSoldierSlotCount> g_loggedBlocked{};

    void LogOncePerRival(std::array<std::uint16_t, kSoldierSlotCount>& logged, std::uint32_t soldierIndex,
                         std::uint16_t rivalId, const char* what)
    {
        if (soldierIndex >= kSoldierSlotCount || logged[soldierIndex] == rivalId)
            return;
        logged[soldierIndex] = rivalId;
        LogInfo("SoldierFaction: soldier %u %s rival %u from walker gear", soldierIndex, what, rivalId & 0x1FFu);
    }

    bool __fastcall hk_HasOtherSoldierOnVec(void* self, std::uint32_t soldierIndex, void* work,
                                            const float* start, const float* aim, const float* direction,
                                            float length, void* context)
    {
        MISSION_GUARD_ORIGINAL_RET(g_OrigHasOtherSoldierOnVec, self, soldierIndex, work, start, aim, direction, length, context);

        const std::uint16_t rivalId = SoldierFaction_GetRivalTarget(soldierIndex);
        if (rivalId == 0)
            return g_OrigHasOtherSoldierOnVec(self, soldierIndex, work, start, aim, direction, length, context);

        SoldierFaction_BeginShotScope(soldierIndex, rivalId);
        const bool blocked = g_OrigHasOtherSoldierOnVec(self, soldierIndex, work, start, aim, direction, length, context);
        SoldierFaction_EndShotScope();

        if (blocked)
        {
            LogOncePerRival(g_loggedBlocked, soldierIndex, rivalId, "has a comrade in the line of fire at");
            return true;
        }
        SoldierFaction_MarkFiring(soldierIndex);
        LogOncePerRival(g_loggedFiring, soldierIndex, rivalId, "is firing at");
        return false;
    }
}

bool Install_WalkerGearActionImpl_HasOtherSoldierOnVec_Hook()
{
    void* target = ResolveGameAddress(gAddr.WalkerGearActionImpl_HasOtherSoldierOnVec);
    if (!target)
        return false;
    return CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_HasOtherSoldierOnVec),
                               reinterpret_cast<void**>(&g_OrigHasOtherSoldierOnVec));
}

bool Uninstall_WalkerGearActionImpl_HasOtherSoldierOnVec_Hook()
{
    void* target = ResolveGameAddress(gAddr.WalkerGearActionImpl_HasOtherSoldierOnVec);
    if (target)
        DisableAndRemoveHook(target);
    g_OrigHasOtherSoldierOnVec = nullptr;
    return true;
}
