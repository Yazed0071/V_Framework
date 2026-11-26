#pragma once

#include <cstdint>

bool Install_RouteAiImpl_SetSubActionParamWithNodeEvent_Hook();
bool Uninstall_RouteAiImpl_SetSubActionParamWithNodeEvent_Hook();
std::uint64_t SquatIdleRoute_GzClip(std::uint64_t clip, void* animControl); // GZ squat for a SquatIdle/SquatIdleAct SquatActRifle node and GZ crouched radio, each only with a long-barrel gun in hand; TPP's squat loop comes back as the copy without the gun-butt sound; 0 = keep the clip
void SquatIdleRoute_NoteLeafClip(void* animControl, std::uint64_t clip, std::uint64_t picked); // which clip a soldier's motion tree got for the relaxed squat
bool SquatIdleRoute_PlaysGzSquat(const void* animControl);
std::uint32_t SquatIdleRoute_SoldierOfAnimControl(const void* animControl); // 0xFFFFFFFF until a route node event has run this mission
void SquatIdleRoute_Clear();
