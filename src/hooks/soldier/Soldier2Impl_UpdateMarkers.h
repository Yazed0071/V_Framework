#pragma once

#include <cstdint>

bool Install_Soldier2Impl_UpdateMarkers_Hook();
bool Uninstall_Soldier2Impl_UpdateMarkers_Hook();

void Set_HeadMarkStaffName(std::uint32_t gameObjectId, bool enable, bool showOnNeutralized,
                           const char* langId);
void Clear_AllHeadMarkStaffNames();
void Apply_HeadMarkStaffNameLangText(void* headMarkEvCall, void* marker, std::uint16_t markerId);
