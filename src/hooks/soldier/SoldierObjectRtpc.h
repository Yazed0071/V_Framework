#pragma once

#include <cstdint>


bool Set_SoldierObjectRtpc(std::uint32_t gameObjectId,
                           std::uint32_t rtpcId,
                           float         value,
                           long          timeMs);


bool Set_SoldierObjectRtpcByName(std::uint32_t gameObjectId,
                                 const char*   rtpcName,
                                 float         value,
                                 long          timeMs);


std::uint32_t Get_SoldierAkObjId(std::uint32_t gameObjectId);


bool Set_SoldierVoicePitch(std::uint32_t gameObjectId, float cents);


void TryApplyAllPendingSoldierPitches();
