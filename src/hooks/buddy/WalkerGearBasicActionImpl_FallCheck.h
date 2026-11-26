#pragma once

// the walker's fall test while it moves: the game drops a walker with no ground 0.7 m under its feet into its ledge-drop clip and
// lands it with another. Ridden, the Battle Gear walks off what its legs reach down and falls only from higher
bool Install_WalkerGearBasicActionImpl_FallCheck_Hook();
void Uninstall_WalkerGearBasicActionImpl_FallCheck_Hook();

// false when the hook could not be installed: the game's test then looks 1.5 m under the walker's position as ever
bool BattleGearFallCheckHooked();

// the ground under a walker's own position, against what the game's fall test casts at, down to `below` metres under it
bool BattleGearGroundUnder(const float* position, float below, float* ground);
