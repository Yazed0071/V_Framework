#include <pch.h>
#include "RideVehicleActionImpl_CheckVehicleIconArea.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>

#include "AddressSet.h"
#include "FeatureSwitch.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"
#include "../outfit/OutfitRegistry.h"
#include "../player/FobPlayerCharacters.h"

namespace
{
    using CheckVehicleIconArea_t = bool(__fastcall*)(void* rideAction, std::uint32_t playerIndex, void* vehicle, std::uint16_t vehicleId, std::uint8_t* seat);

    constexpr std::uint16_t kTppVehicle2  = 0x32;
    constexpr std::uint32_t kMaxVehicles  = 0x200;
    constexpr std::uint8_t  kDriverSeat   = 0;
    constexpr std::uint8_t  kNoSeat       = 4;

    CheckVehicleIconArea_t g_OrigCheckVehicleIconArea = nullptr;
    bool                   g_Installed                = false;

    std::atomic<bool> g_CannotRide[kMaxVehicles];

    bool __fastcall hkCheckVehicleIconArea(void* rideAction, std::uint32_t playerIndex, void* vehicle, std::uint16_t vehicleId, std::uint8_t* seat)
    {
        if (MissionCodeGuard::ShouldBypassHooks() || (vehicleId >> 9) != kTppVehicle2)
            return g_OrigCheckVehicleIconArea(rideAction, playerIndex, vehicle, vehicleId, seat);

        if (g_CannotRide[vehicleId & 0x1FFu].load(std::memory_order_relaxed))
        {
            if (seat)
                *seat = kNoSeat;
            return false;
        }

        const bool canRide = g_OrigCheckVehicleIconArea(rideAction, playerIndex, vehicle, vehicleId, seat);
        if (seat && *seat == kDriverSeat && !FeatureSwitch::IsOn(FeatureSwitch::Feature::LiquidDriving)
            && outfit::ReadLivePlayerType() == fobchars::kPlayerType_Liquid)
        {
            *seat = kNoSeat;
            return false;
        }
        return canRide;
    }
}

void Set_VehicleCannotRide(std::uint32_t vehicleIndex, bool cannotRide)
{
    if (vehicleIndex < kMaxVehicles)
        g_CannotRide[vehicleIndex].store(cannotRide, std::memory_order_relaxed);
}

void Set_EveryVehicleCannotRide(bool cannotRide)
{
    for (std::uint32_t i = 0; i < kMaxVehicles; ++i)
        g_CannotRide[i].store(cannotRide, std::memory_order_relaxed);
}

void Clear_VehicleCannotRide()
{
    Set_EveryVehicleCannotRide(false);
}

bool Install_RideVehicleActionImpl_CheckVehicleIconArea_Hook()
{
    if (g_Installed)
        return true;

    Clear_VehicleCannotRide();

    void* target = gAddr.RideVehicleActionImpl_CheckVehicleIconArea ? ResolveGameAddress(gAddr.RideVehicleActionImpl_CheckVehicleIconArea) : nullptr;
    if (!target)
    {
        LogError("RideVehicleActionImpl_CheckVehicleIconArea: address missing for this build, SetCannotRide off");
        return false;
    }
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkCheckVehicleIconArea), reinterpret_cast<void**>(&g_OrigCheckVehicleIconArea)))
    {
        LogError("RideVehicleActionImpl_CheckVehicleIconArea: hook failed, SetCannotRide off");
        return false;
    }
    g_Installed = true;
    return true;
}

bool Uninstall_RideVehicleActionImpl_CheckVehicleIconArea_Hook()
{
    if (!g_Installed)
        return true;
    DisableAndRemoveHook(ResolveGameAddress(gAddr.RideVehicleActionImpl_CheckVehicleIconArea));
    g_OrigCheckVehicleIconArea = nullptr;
    g_Installed                = false;
    return true;
}
