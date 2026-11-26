#pragma once

#include <cstdint>

bool Install_RouteAiImpl_StepEvent_Hook();
bool Uninstall_RouteAiImpl_StepEvent_Hook();
void AntiAirGunRoute_Clear();
float AntiAirGunRoute_ShotInterval(std::uint32_t soldierIndex); // seconds between shots from the node's param0, 0 = the game's own
float AntiAirGunRoute_TurnSpeed(std::uint32_t soldierIndex);    // radians per second from the node's param3, 0 = not a route gunner
void AntiAirGunRoute_NoteGunOnTarget(std::uint32_t soldierIndex, bool onTarget);
