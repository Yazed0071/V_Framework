#pragma once

#include <cstdint>

struct VIPImportantInfo
{
    bool          isOfficer     = false;
    std::uint32_t deadBodyLabel = 0;
};

bool VIPImportant_Set(std::uint32_t gameObjectId, bool isOfficer, std::uint32_t deadBodyLabel);
bool VIPImportant_Remove(std::uint32_t gameObjectId);
void VIPImportant_Clear();
bool VIPImportant_Find(std::uint32_t gameObjectId, VIPImportantInfo* out);
bool IsVIPImportantSoldier(std::uint16_t soldierIndex);
bool IsVanillaVIPSoldier(const void* soldiers, std::uint16_t soldierIndex);   // given SetVip or SetVipSpecial
bool IsVanillaVIPSpecialSoldier(const void* soldiers, std::uint16_t soldierIndex);
bool IsVIPSoldier(const void* soldiers, std::uint16_t soldierIndex);          // either of the two above

bool DeadBodyLabel_Set(std::uint32_t gameObjectId, std::uint32_t label);   // label 0 clears it
std::uint32_t DeadBodyLabel_Get(std::uint32_t gameObjectId);
void DeadBodyLabel_Clear();

std::uint16_t SoldierIndexOfGameObjectId(std::uint32_t gameObjectId);   // 0xFFFF when it is not a soldier

const void* VIPImportant_NoticeSoldiers(void* noticeAction);
bool VIPImportant_ReadNoticeComrade(void* noticeAction, std::uint32_t actorId, std::uint16_t* comradeId, std::uint8_t* noticeKind);
std::uint32_t VIPImportant_EventHash(void* evt);
void VIPImportant_PlayVoiceLine(void* noticeAction, std::uint32_t actorId, std::uint32_t lineHash);
