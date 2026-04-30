#pragma once


#include <cstdint>

namespace SoldierRtpc
{


    int SetSoldierRtpc(std::uint32_t goId, const char* rtpcName, float value, long timeMs);


    int SetSoldierRtpcById(std::uint32_t goId, std::uint32_t rtpcId, float value, long timeMs);


    int SetGlobalRtpc(const char* rtpcName, float value, long timeMs);


    int SetGlobalRtpcById(std::uint32_t rtpcId, float value, long timeMs);


    int ResetSoldierRtpc(std::uint32_t goId, const char* rtpcName, long timeMs);
}
