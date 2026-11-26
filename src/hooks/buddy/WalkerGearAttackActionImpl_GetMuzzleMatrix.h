#pragma once

// walker gun muzzle matrix: for the Battle Gear the shots leave its own barrels (heavy MG at the turret nose,
// H-Missile from the roof hatches) instead of the D-Walker weapons hidden in its hull
bool Install_WalkerGearAttackActionImpl_GetMuzzleMatrix_Hook();
void Uninstall_WalkerGearAttackActionImpl_GetMuzzleMatrix_Hook();
