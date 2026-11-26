#pragma once

#include <cstdint>

bool Install_RideVehicleActionImpl_CheckVehicleIconArea_Hook();
bool Uninstall_RideVehicleActionImpl_CheckVehicleIconArea_Hook();

void Set_VehicleCannotRide(std::uint32_t vehicleIndex, bool cannotRide);
void Set_EveryVehicleCannotRide(bool cannotRide);
void Clear_VehicleCannotRide();
