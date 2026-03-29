#pragma once

#include <cstdint>

// Installs the ActionCoreImpl::UpdateOptCamo hook.
// Params: none
bool Install_UpdateOptCamo_Hook();

// Removes the ActionCoreImpl::UpdateOptCamo hook.
// Params: none
bool Uninstall_UpdateOptCamo_Hook();

// Enables or disables one soldier mappedIndex.
// Params: mappedIndex, enabled
void Set_UpdateOptCamoEnableMappedIndex(std::uint32_t mappedIndex, bool enabled);

// Clears all mappedIndex overrides.
// Params: none
void Clear_UpdateOptCamoMappedIndexOverrides();