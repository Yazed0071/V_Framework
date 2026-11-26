#pragma once

#include <cstdint>

bool Install_BulletSystem3Impl_CreateBullet_Hook();
void Uninstall_BulletSystem3Impl_CreateBullet_Hook();

// One real round through the bullet system, as the vehicles' and Sahelanthropus' guns fire them: start and unit direction in world
// space, the shooter's and the walker's game object ids ((type << 9) | index).
bool FireBattleGearRailRound(const float* start, const float* direction, std::uint16_t attackId, std::uint16_t shooterId,
                             std::uint16_t walkerId);

// The tanks' cupola gun round from the roof M2: BL_Turret with the tank gun's attack, every sixth one a tracer.
bool FireBattleGearMgRound(const float* start, const float* direction, std::uint16_t shooterId, std::uint16_t walkerId);

// A registered Battle Gear gun's round: its own attack and TppEquip.BL_* row.
bool FireBattleGearRound(const float* start, const float* direction, std::uint16_t attackId, std::uint8_t bulletRow, bool tracer,
                         std::uint16_t shooterId, std::uint16_t walkerId);
