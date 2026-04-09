#pragma once

#include <cstdint>

// Installs the Equip icon FTEX path hook.
// Params: none
bool Install_EquipIconFtexPath_Hook();

// Removes the Equip icon FTEX path hook.
// Params: none
bool Uninstall_EquipIconFtexPath_Hook();

// Sets a custom FTEX icon path for one equipId.
// Params: equipId (int), texturePathHash (uint64_t)
void EquipIcon_SetEquipIdIconFtexPath(int equipId, uint64_t texturePathHash);

// Clears a custom FTEX icon path for one equipId.
// Params: equipId (int)
void EquipIcon_ClearIconFtexPath(int equipId);

// Clears all custom FTEX icon path overrides.
// Params: none
void EquipIcon_ClearAllIconFtexPaths();
