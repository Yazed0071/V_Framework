#include <pch.h>
#include "RideVehicleActionImpl_UpdateShootState.h"

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "RideVehicleActionImpl_State_IdleAction.h"
#include "log.h"

namespace
{
    using UpdateShootState_t = void(__fastcall*)(void* rideAction, std::uint32_t slot);

    constexpr std::uint8_t kDriverSeat = 0;

    const std::uint8_t kUpdateShootStatePrologue[] = {
        0x40, 0x55, 0x56, 0x57, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0xAC, 0x24, 0x28, 0xFF,
        0xFF, 0xFF, 0x48, 0x81, 0xEC, 0xD8, 0x01, 0x00, 0x00
    };

    UpdateShootState_t g_OrigUpdateShootState = nullptr;
    void*              g_HookTarget           = nullptr;

    void __fastcall hk_UpdateShootState(void* rideAction, std::uint32_t slot)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigUpdateShootState, rideAction, slot);
        JeepGunnerRide ride{};
        if (JeepGunner_ReadRide(rideAction, slot, ride) && ride.seat == kDriverSeat && JeepGunner_IsArmedLightVehicle(ride.commInfo))
            return;
        g_OrigUpdateShootState(rideAction, slot);
    }
}

bool Install_RideVehicleActionImpl_UpdateShootState_Hook()
{
    if (g_HookTarget)
        return true;
    auto* target = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.SoldierRideVehicleActionImpl_UpdateShootState));
    if (!target)
        return true;
    const HookEntry entry = CheckHookEntry(target, kUpdateShootStatePrologue, sizeof(kUpdateShootStatePrologue));
    if (entry == HookEntry::Unknown)
    {
        LogError("JeepGunner: soldier vehicle shoot code does not match %s, armed jeep drivers also fire the gun", GetGameBuildName(gGameBuild));
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("JeepGunner: RideVehicleActionImpl::UpdateShootState", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_UpdateShootState), reinterpret_cast<void**>(&g_OrigUpdateShootState)))
    {
        LogError("JeepGunner: RideVehicleActionImpl::UpdateShootState hook failed, armed jeep drivers also fire the gun");
        return false;
    }
    g_HookTarget = target;
    return true;
}

bool Uninstall_RideVehicleActionImpl_UpdateShootState_Hook()
{
    if (g_HookTarget)
    {
        DisableAndRemoveHook(g_HookTarget);
        g_HookTarget = nullptr;
        g_OrigUpdateShootState = nullptr;
    }
    return true;
}
