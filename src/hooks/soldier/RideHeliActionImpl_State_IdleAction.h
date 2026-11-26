#pragma once

#include <cstdint>

bool Install_RideHeliActionImpl_State_IdleAction_Hook();
bool Uninstall_RideHeliActionImpl_State_IdleAction_Hook();
bool RideHeliDoorSeat_HeliRealized(const std::uint8_t* heliSeat);   // heliSeat = the heli seat record a ride work entry points at; false when unsure
bool RideHeliDoorSeat_Up(std::uint32_t soldierIndex);   // a seated rider up in his crouch, his heli realized and alive
bool RideHeliDoorSeat_FireWanted(std::uint32_t soldierIndex);   // his attack action keeps vanilla's burst loop running while true, stops it when false
void RideHeliDoorSeat_Clear();
