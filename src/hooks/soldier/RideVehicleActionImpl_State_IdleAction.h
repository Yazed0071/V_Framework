#pragma once

#include <cstdint>

inline constexpr std::uint8_t kJeepGunnerSeat = 5; // 0-3 are the jeep's four seats and 4 means "no seat": the gunner is the fifth rider

struct JeepGunnerRide
{
    std::uint8_t*       common;
    const std::uint8_t* commInfo;
    std::uint8_t*       realWork;
    std::uint8_t        seat;
    std::uint32_t       soldier;
};

bool Install_RideVehicleActionImpl_State_IdleAction_Hook();
bool Uninstall_RideVehicleActionImpl_State_IdleAction_Hook();

bool JeepGunner_ReadRide(const void* rideAction, std::uint32_t slot, JeepGunnerRide& ride);
bool JeepGunner_IsArmedLightVehicle(const void* commInfo);
bool JeepGunner_IsGunner(std::uint32_t soldierIndex);
float JeepGunner_GunYaw(const void* commInfo); // the yaw the armed jeep's gun shows now, on its way to the ordered one
bool JeepGunner_BeginCrewScope(std::uint32_t shooterIndex, const void* commonWork); // false when the shooter is no armed-jeep gunner
void JeepGunner_EndCrewScope();
std::uint32_t JeepGunner_FilterCrewSearch(std::uint8_t* result, std::uint32_t count); // drops the gunner's own jeep crew inside a crew scope
#ifdef _DEBUG
void JeepGunner_ProbeLineCheck(std::uint32_t shooterIndex, bool blocked);
#endif
std::uint64_t JeepGunner_Clip(std::uint64_t clip, void* animControl); // the gunner's front passenger clips swapped for the emplaced MG idle and the rear seat hits; 0 = keep the clip
