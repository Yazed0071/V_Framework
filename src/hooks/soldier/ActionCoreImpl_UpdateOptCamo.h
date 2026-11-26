#pragma once

#include <cstdint>


bool Install_UpdateOptCamo_Hook();


bool Uninstall_UpdateOptCamo_Hook();


void Set_UpdateOptCamoEnableMappedIndex(std::uint32_t mappedIndex, bool enabled, double seconds);   // seconds > 0: forced off after that much game time


void Set_UpdateOptCamoEnableEverySoldier(bool enabled, double seconds);   // also the soldiers that load later; clears the single-soldier settings


void Clear_UpdateOptCamoMappedIndexOverrides();


bool GetSoldierStatusArray(std::uint64_t& outBase, std::uint32_t& outCount);


bool GetSoldierActorMap(std::uint64_t& outMapTable);
