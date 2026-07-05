#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

namespace V_FrameWorkState
{


    void Load();


    void Save();


    void BeginBatch();
    void EndBatch();


    bool ResolveOrCreateEquipId(
        const char* key,
        std::int32_t minimumId,
        std::int32_t& outEquipId);


    bool ResolveOrCreateDevelopId(
        const char* key,
        std::int32_t minimumId,
        std::int32_t& outDevelopId,
        bool* outCreated = nullptr);   // outCreated: true = new id minted (added), false = existing (loaded)


    std::vector<std::int32_t> TakePendingDevelopedResets();

    bool ResolveDevelopedFlag(const char* key, bool defaultDeveloped);
    bool IsManagedDevelopId(std::int32_t developId);
    void SetDevelopedByDevelopId(std::int32_t developId, bool developed);
    bool GetDevelopedByDevelopId(std::int32_t developId);

    void ForEachManagedDevelop(
        const std::function<void(std::int32_t developId, bool developed, bool isNew)>& callback);

    void SetNewByDevelopId(std::int32_t developId, bool isNew);
    bool GetNewByDevelopId(std::int32_t developId);


    bool ResolveOrCreateFlowIndex(
        const char* key,
        std::int32_t minimumIndex,
        std::int32_t& outFlowIndex);


    bool ResolveOrCreateTapeSaveIndex(
        const char* key,
        std::int16_t minimumIndex,
        std::int16_t& outSaveIndex);

    bool ResolveOrCreateConstantValue(
        const char* spaceTag,
        const char* name,
        std::int32_t minimumValue,
        std::int32_t& outValue);


    std::uint8_t GetPersistedOutfitPartsType(const char* key);
    std::uint8_t GetPersistedOutfitSelector(const char* key);
    void         SetPersistedOutfitIds(const char* key,
                                       std::uint8_t partsType,
                                       std::uint8_t selector);

    std::size_t GetPersistedOutfitVariantSelectors(const char* key,
                                                   std::uint8_t* out,
                                                   std::size_t cap);
    void        SetPersistedOutfitVariantSelectors(const char* key,
                                                   const std::uint8_t* selectors,
                                                   std::size_t count);

    void ForEachPersistedOutfit(
        const std::function<void(const std::string& key,
                                 std::uint8_t partsType,
                                 std::uint8_t selector,
                                 const std::uint8_t* variants)>& callback);


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
