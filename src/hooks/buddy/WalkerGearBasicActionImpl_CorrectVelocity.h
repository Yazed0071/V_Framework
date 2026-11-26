#pragma once

#include <cstdint>

// riding the Battle Gear: its own movement replaces the D-Walker's. Run posture moves like a horse (walk, trot and canter by
// stick tilt, Dash gallops, pressing Dash again right after letting it go sprints) and turns toward the stick; fire posture
// glides toward the stick with inertia while the walker turns with the swinging hull
bool Install_WalkerGearBasicActionImpl_CorrectVelocity_Hook();
void Uninstall_WalkerGearBasicActionImpl_CorrectVelocity_Hook();

// metres above the ground under its feet the Battle Gear climbs: its hull lets lower things pass under it (its legs and pods rise over
// them) and its walker steps onto them
constexpr float kBattleGearStepHeight = 1.3f;

// metres under its feet the ridden Battle Gear runs down without the game's ledge-drop clip: its walker is set down at once
// and its hull falls after it in an arc of its own; from higher the game's fall takes it, with what that fall does to it
constexpr float kBattleGearDeepestDrop = 6.0f;

// metres the D-Walker's own position (the middle of its body ball) rides over the ground under it
constexpr float kWalkerOverGround = 0.8f;

// the Battle Gear's own walk frame for a walker whose velocity callback another of its actions holds (a hit's flinch clip):
// written over the step and turn that action set in `controller`. Does nothing for other walkers or with no rider.
void BattleGearWalkThrough(std::uint32_t index, std::uint8_t* controller);

#ifdef _DEBUG
// TEMP probe: the Battle Gear walker's basic action state and the state it is about to enter (-1 unknown), how many walk
// hook calls since the last asking could not be read as the Battle Gear's, and the vtable of the object that holds the
// walker's velocity callback now (fox::cc::VelocityControlAction +0xB8; 0 unknown)
void BattleGearWalkerStateProbe(int* state, int* next, unsigned* unreadCalls, unsigned long long* callbackVtable);
#endif
