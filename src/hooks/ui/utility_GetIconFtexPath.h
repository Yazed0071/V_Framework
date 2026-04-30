#pragma once

#include <cstdint>


bool Install_EquipIconFtexPath_Hook();


bool Uninstall_EquipIconFtexPath_Hook();


void EquipIcon_SetEquipIdIconFtexPath(int equipId, uint64_t texturePathHash);


void EquipIcon_ClearIconFtexPath(int equipId);


void EquipIcon_ClearAllIconFtexPaths();
