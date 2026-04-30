#pragma once

#include <cstdint>


bool Install_UpdateOptCamo_Hook();


bool Uninstall_UpdateOptCamo_Hook();


void Set_UpdateOptCamoEnableMappedIndex(std::uint32_t mappedIndex, bool enabled);


void Clear_UpdateOptCamoMappedIndexOverrides();