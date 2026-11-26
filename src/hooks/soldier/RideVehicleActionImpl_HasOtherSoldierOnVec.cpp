#include <pch.h>

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "RideVehicleActionImpl_State_IdleAction.h"
#include "SoldierFaction.h"
#include "log.h"

namespace
{
    using HasOtherSoldierOnVec_t = bool(__fastcall*)(void* self, void* commonWork, std::uint32_t soldierIndex,
                                                     const float* start, const float* aim, const float* direction,
                                                     float length, void* context);

    HasOtherSoldierOnVec_t g_OrigHasOtherSoldierOnVec = nullptr;

    bool __fastcall hk_HasOtherSoldierOnVec(void* self, void* commonWork, std::uint32_t soldierIndex,
                                            const float* start, const float* aim, const float* direction,
                                            float length, void* context)
    {
        MISSION_GUARD_ORIGINAL_RET(g_OrigHasOtherSoldierOnVec, self, commonWork, soldierIndex, start, aim, direction, length, context);

        const bool crewScope = JeepGunner_BeginCrewScope(soldierIndex, commonWork);
        const std::uint16_t rivalId = SoldierFaction_GetRivalTarget(soldierIndex);
        bool blocked = false;
        if (rivalId == 0)
        {
            blocked = g_OrigHasOtherSoldierOnVec(self, commonWork, soldierIndex, start, aim, direction, length, context);
        }
        else
        {
            SoldierFaction_BeginShotScope(soldierIndex, rivalId);
            blocked = g_OrigHasOtherSoldierOnVec(self, commonWork, soldierIndex, start, aim, direction, length, context);
            SoldierFaction_EndShotScope();
        }
        if (crewScope)
        {
            JeepGunner_EndCrewScope();
#ifdef _DEBUG
            JeepGunner_ProbeLineCheck(soldierIndex, blocked);
#endif
        }
        return blocked;
    }
}

bool Install_RideVehicleActionImpl_HasOtherSoldierOnVec_Hook()
{
    void* target = ResolveGameAddress(gAddr.RideVehicleActionImpl_HasOtherSoldierOnVec);
    if (!target)
        return false;
    return CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_HasOtherSoldierOnVec),
                               reinterpret_cast<void**>(&g_OrigHasOtherSoldierOnVec));
}

bool Uninstall_RideVehicleActionImpl_HasOtherSoldierOnVec_Hook()
{
    void* target = ResolveGameAddress(gAddr.RideVehicleActionImpl_HasOtherSoldierOnVec);
    if (target)
        DisableAndRemoveHook(target);
    g_OrigHasOtherSoldierOnVec = nullptr;
    return true;
}
