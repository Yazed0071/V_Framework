#pragma once

#include <cstdint>

bool Install_RideHorseActionPluginImpl_State_Idle_Hook();
void Uninstall_RideHorseActionPluginImpl_State_Idle_Hook();
bool TakeHorsePatReentry(const std::uint8_t* horseWork);   // D-Horse's BasicAction2Impl work; true once after Snake held Reload on it, its +0x431 is set
