#pragma once

#include <cstdint>

void SoldierFactionHeli_Update(void* heli, std::uint32_t heliIndex, std::uint64_t now);
bool SoldierFactionHeli_IsFighting(std::uint16_t heliId);
bool SoldierFactionHeli_AimPoint(std::uint16_t heliId, float* point);   // 16-byte aligned x, y, z, 0
bool SoldierFactionHeli_LineClear(std::uint16_t heliId);
std::uint64_t SoldierFactionHeli_BlockedForMs(std::uint16_t heliId, std::uint64_t now);
bool SoldierFactionHeli_OutsidePlayerGate(std::uint16_t heliId);   // its target is past the attack check's 20 m player gate
void SoldierFactionHeli_OnDecision(std::uint16_t heliId);
void SoldierFactionHeli_OnHit(std::uint16_t heliId, std::uint16_t attackerId, std::uint16_t attackId);
void SoldierFactionHeli_Clear();
