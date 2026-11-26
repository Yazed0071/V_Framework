#pragma once

#include <cstdint>

bool Install_RideHeliActionImpl_State_GetInAction_Hook();
bool Uninstall_RideHeliActionImpl_State_GetInAction_Hook();

enum class HeliSeatSide : std::uint8_t
{
    Nearest,
    Left,
    Right,
};

constexpr std::uint16_t kRideHeliNoTarget = 0xFFFF;

bool SoldierRideHeli_GetOn(std::uint32_t soldierId, std::uint32_t heliId, HeliSeatSide side, std::uint16_t aimTarget, const char** reason);
bool SoldierRideHeli_GetOff(std::uint32_t soldierId, const char** reason);
void SoldierRideHeli_Clear();
bool SoldierRideHeli_AnyRider();
bool SoldierRideHeli_IsDoorRider(std::uint32_t soldierIndex);   // boarded by RequestToGetOnHelicopterAndAimTarget, until he is off again
std::uint16_t SoldierRideHeli_Heli(std::uint32_t soldierIndex);   // the heli he sits in, 0 while he is not seated
void SoldierRideHeli_NoteStep(std::uint32_t soldierIndex, std::uint8_t step);
bool SoldierRideHeli_OnSeat(std::uint32_t soldierIndex);   // a door rider, realized on his seat
HeliSeatSide SoldierRideHeli_Side(std::uint32_t soldierIndex);
bool SoldierRideHeli_OnSetCommand(std::uint8_t* actionController, std::uint32_t soldierIndex, std::uint8_t* command);   // true = keep this command from him; may rewrite a knock-out into his fall out of the door
bool SoldierRideHeli_AiAim(std::uint32_t soldierIndex, std::uint8_t* block);   // the AI's last aim order for him (0x28 bytes), while it is fresh
std::uint32_t SoldierRideHeli_AiShots(std::uint32_t soldierIndex);   // counts the AI's shoot orders parked behind his ride
bool SoldierRideHeli_AimTarget(std::uint32_t soldierIndex, std::uint16_t& gameObjectId);   // the target he boarded with
void SoldierRideHeli_SetDoorOut(std::uint32_t soldierIndex, float x, float z);   // out of his door, where he falls when he lets go
void SoldierRideHeli_KeepGun(std::uint32_t soldierIndex, std::uint8_t* lifeFlags);
void SoldierRideHeli_SetFlinchDisabled(std::uint32_t soldierIndex, bool disabled);   // GZ SetFlagDisableBulletDamageReactionInHelicopter
bool SoldierRideHeli_FlinchDisabled(std::uint32_t soldierIndex);
void SoldierRideHeli_RequestFall(std::uint32_t soldierIndex, const char* why);   // from his seat: the pump lets him go and he falls out of the door
void SoldierRideHeli_HeliDestroyed(std::uint32_t heliId);
bool SoldierRideHeli_HeliDead(std::uint32_t soldierIndex);   // the heli he rides has no life left
bool SoldierRideHeli_RequestFire(std::uint32_t soldierIndex, std::uint32_t targetId, float angle, float radius, const char** reason);   // GZ RequestToFireOnHelicopter, any soldier
bool SoldierRideHeli_ShotsQueued();
bool SoldierRideHeli_HasShot(std::uint32_t soldierIndex);   // a RequestToFireOnHelicopter shot waits for him
bool SoldierRideHeli_TakeShot(std::uint32_t soldierIndex, bool canFire, float* point);   // point: x, y, z, 1
bool SoldierRideHeli_AnyFalling();
bool SoldierRideHeli_Falling(std::uint32_t soldierIndex);   // let go of the heli and not down yet
void SoldierRideHeli_Landed(std::uint32_t soldierIndex);
void SoldierRideHeli_Pump();
