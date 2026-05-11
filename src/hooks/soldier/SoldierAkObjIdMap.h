#pragma once


#include <cstdint>
#include <vector>


namespace SoldierAkObjIdMap
{
    bool Install();
    bool Uninstall();
    std::vector<std::uint32_t> GetAkObjIdsForObject(void* object);
    std::vector<std::uint32_t> GetAkObjIdsForControl(void* control);
    std::vector<std::uint32_t> GetAllSoldierVoiceAkObjIds();
    void SetActiveSoldierVoiceCents(float cents);
    void ClearActiveSoldierVoiceCents();
    void SetPitchForControl(void* control, float cents);
    void ClearPitchForControl(void* control);
    void SetDesiredPitchForGoId(std::uint32_t goId, float cents);
    void ClearDesiredPitchForGoId(std::uint32_t goId);
    std::vector<std::uint32_t> GetAkObjIdsForGoId(std::uint32_t goId);
}
