#pragma once

#include <cstdint>

bool SoldierThrow_ToPosition(std::uint32_t soldierId, const float position[3], std::int32_t equipId, const char** reason);   // equipId < 0: his RegistGrenadeId ids
bool SoldierThrow_InDoor(std::uint32_t soldierId, const char* gimmickId, const char* dataSetName, std::int32_t equipId, const char** reason);

void SoldierThrow_Pump();
void SoldierThrow_OnSetCommand(std::uint8_t* actionController, std::uint32_t soldierIndex, const std::uint8_t* command);
void SoldierThrow_ClearForNewMission();
