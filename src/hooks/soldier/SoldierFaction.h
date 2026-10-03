#pragma once

#include <cstdint>

struct SoldierFactionHiddenSight
{
    std::uint8_t* entries[64];
    std::uint16_t ids[64];
    std::uint32_t count;
    void**        noiseSlot;
    void*         noise;
};

enum class SoldierFactionBodyNode : std::uint8_t
{
    DownComrade,
    VehicleDownComrade,
};

void Set_SoldierFaction(std::uint32_t gameObjectId, const char* factionName);
void Clear_AllSoldierFactions();

bool SoldierFaction_AnyFaction();
bool SoldierFaction_IsFactionSoldier(std::uint16_t gameObjectId);
bool SoldierFaction_AreHostileSoldiers(std::uint16_t gameObjectId, std::uint16_t otherGameObjectId);

void SoldierFaction_OnSightCheck(void* noticeController, std::uint32_t soldierIndex);
void SoldierFaction_WidenBulletRequest(void* bullet3, std::uint32_t requestIndex);
void SoldierFaction_OnSoldierDamages(void* damageHandler, std::uint32_t victimIndex, void* damages, std::uint32_t count);
void SoldierFaction_OnLifeStatusChanged(void* lifeController, std::uint32_t soldierIndex, const void* life,
                                        std::uint32_t oldStatus);

std::uint16_t SoldierFaction_GetRivalTarget(std::uint32_t soldierIndex);
void SoldierFaction_MarkRivalNodeAlive(std::uint32_t soldierIndex);
const char* SoldierFaction_WhyRivalLost(const void* soldierCore, std::uint32_t soldierIndex);
void SoldierFaction_MarkFiring(std::uint32_t shooterIndex);
bool SoldierFaction_ReadRivalAimPoint(const void* soldierCore, std::uint16_t rivalId, float* aimPoint);
bool SoldierFaction_IsShotAtRival(const void* soldierCore, std::uint32_t shooterIndex, const float* shotTarget);
void SoldierFaction_EndRivalFight(std::uint32_t soldierIndex, const char* reason);
void SoldierFaction_SetMount(std::uint32_t soldierIndex, std::uint16_t mountId);
void SoldierFaction_BeginShotScope(std::uint32_t shooterIndex, std::uint16_t rivalId);
void SoldierFaction_EndShotScope();
std::uint32_t SoldierFaction_FilterSoldierSearch(std::uint8_t* result, std::uint32_t count);
std::uint16_t SoldierFaction_RiderOrSelf(std::uint16_t gameObjectId);

void SoldierFaction_HideDownedRivals(void* noticeController, std::uint32_t soldierIndex, void* bodyTable,
                                     SoldierFactionHiddenSight* hidden);
void SoldierFaction_RestoreDownedRivals(SoldierFactionHiddenSight* hidden);
bool SoldierFaction_ShouldDropNotice(void* noticeInterface, std::uint32_t soldierIndex, std::uint8_t noticeType,
                                     const void* notice);
std::uint32_t SoldierFaction_IgnoredNoticeCount();
bool SoldierFaction_ShouldIgnoreNoise(void* noticeController, std::uint32_t soldierIndex, const void* noise);
bool SoldierFaction_ShouldLeaveBody(void* node, std::uint32_t soldierIndex, void* const* context,
                                    SoldierFactionBodyNode kind);
