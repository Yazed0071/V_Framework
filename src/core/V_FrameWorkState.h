#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_set>

namespace V_FrameWorkState
{


    void Load();


    void Save();


    bool ResolveOrCreateEquipId(
        const char* key,
        std::int32_t minimumId,
        std::int32_t& outEquipId);


    bool ResolveOrCreateDevelopId(
        const char* key,
        std::int32_t minimumId,
        std::int32_t& outDevelopId);


    bool ResolveOrCreateFlowIndex(
        const char* key,
        std::int32_t minimumIndex,
        std::int32_t& outFlowIndex);


    bool ResolveOrCreateTapeSaveIndex(
        const char* key,
        std::int16_t minimumIndex,
        std::int16_t& outSaveIndex);


    void SetTapeOwned(const char* key, bool owned);
    void SetTapeNew(const char* key, bool isNew);


    void SetTapeOwnedBySaveIndex(std::int16_t saveIndex, bool owned);
    void SetTapeNewBySaveIndex(std::int16_t saveIndex, bool isNew);


    bool GetTapeOwned(const char* key);
    bool GetTapeNew(const char* key);


    void ForEachTape(
        const std::function<void(const std::string& key,
                                 std::int16_t saveIndex,
                                 bool owned,
                                 bool isNew)>& callback);


    void Reset();
}
