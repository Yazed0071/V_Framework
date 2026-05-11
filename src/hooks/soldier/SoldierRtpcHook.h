#pragma once


#include <cstdint>

namespace SoldierRtpc
{


    int SetSoldierRtpc(std::uint32_t goId, const char* rtpcName, float value, long timeMs);


    int SetSoldierRtpcById(std::uint32_t goId, std::uint32_t rtpcId, float value, long timeMs);


    int SetGlobalRtpc(const char* rtpcName, float value, long timeMs);


    int SetGlobalRtpcById(std::uint32_t rtpcId, float value, long timeMs);


    int ResetSoldierRtpc(std::uint32_t goId, const char* rtpcName, long timeMs);


    // Direct passthrough to AK::SoundEngine::SetRTPCValue.
    // akObjId is the Wwise game-object ID (NOT the engine gameObjectId).
    // Use this once you have identified the soldier's AkObjectID via the
    // logging hook (see SetRtpcLoggingEnabled).
    int SetRtpcByAkObjId(std::uint64_t akObjId, const char* rtpcName, float value, long timeMs);


    // Direct passthrough by precomputed RTPC id (Wwise FNV1).
    int SetRtpcByAkObjIdById(std::uint64_t akObjId, std::uint32_t rtpcId, float value, long timeMs);


    // Toggle a logging hook on AK::SoundEngine::SetRTPCValue. When enabled,
    // every game-side RTPC change is logged with rtpcId/akObjId/value so you
    // can identify which AkObjectID belongs to which soldier in the field.
    bool SetRtpcLoggingEnabled(bool enabled);


    // Query current logging state.
    bool IsRtpcLoggingEnabled();
}
