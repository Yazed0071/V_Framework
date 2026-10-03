#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

namespace V_FrameWorkState
{


    void Load();

    void NoteInstallOutcome(bool allInstalled);


    void Save();

    void FlushPendingSaves();
    bool PersistNow();
    void SaveOnProcessExit();
    void AbandonFlusherThread();


    void BeginBatch();
    void EndBatch();

    struct SaveBatch
    {
        SaveBatch() { BeginBatch(); }
        ~SaveBatch() { EndBatch(); }
        SaveBatch(const SaveBatch&) = delete;
        SaveBatch& operator=(const SaveBatch&) = delete;
    };


    bool ResolveOrCreateEquipId(
        const char* key,
        std::int32_t minimumId,
        std::int32_t& outEquipId,
        bool isWeapon = false);

    bool IsClaimedEquipId(std::int32_t equipId);

    bool IsDevelopedByFlowRowDevelopId(std::int32_t developId, bool& developed);

    void SetVanillaIdentityEquipIds(const std::int32_t* equipIds,
                                    std::size_t count);

    void NotePinnedEquipId(std::int32_t equipId);
    void NoteStickyPinnedEquipId(std::int32_t equipId);
    void UnpinEquipId(std::int32_t equipId);


    bool ResolveOrCreateDevelopId(
        const char* key,
        std::int32_t minimumId,
        std::int32_t& outDevelopId,
        bool* outCreated = nullptr);

    std::int32_t GetDevelopIdByKey(const char* key);

    constexpr std::uint8_t kRowKindUnknown    = 0;
    constexpr std::uint8_t kRowKindWeapon     = 1;
    constexpr std::uint8_t kRowKindOutfit     = 2;
    constexpr std::uint8_t kRowKindHeadOption = 3;

    void SetRowKind(const char* key, std::uint8_t kind);

    std::int32_t GetDevelopIdAtOldFlowIndex(std::int32_t oldFlowIndex);

    std::int32_t GetFlowIndexByDevelopId(std::int32_t developId);


    std::vector<std::int32_t> TakePendingDevelopedResets();

    bool ResolveDevelopedFlag(const char* key, bool defaultDeveloped);
    bool IsManagedDevelopId(std::int32_t developId);
    void SetDevelopedByDevelopId(std::int32_t developId, bool developed);
    bool GetDevelopedByDevelopId(std::int32_t developId);
    bool IsExplicitlyUndevelopedByDevelopId(std::int32_t developId);

    void ForEachManagedDevelop(
        const std::function<void(std::int32_t developId, bool developed, bool isNew)>& callback);

    void SetNewByDevelopId(std::int32_t developId, bool isNew);
    bool GetNewByDevelopId(std::int32_t developId);

    void ForEachManagedDevelopRow(
        const std::function<void(std::int32_t developId, std::int32_t flowIndex,
                                 bool reqAnnounced)>& callback);

    bool GetDevReqAnnouncedByDevelopId(std::int32_t developId);
    void SetDevReqAnnouncedByDevelopId(std::int32_t developId, bool announced);


    bool ResolveOrCreateFlowIndex(
        const char* key,
        std::int32_t minimumIndex,
        std::int32_t& outFlowIndex);

    void SetSessionFlowIndex(const char* key, std::int32_t flowIndex);
    std::int32_t GetPersistedFlowIndex(const char* key);

    void ReleaseSessionFlowIndex(const char* key);


    bool ResolveOrCreateTapeSaveIndex(
        const char* key,
        std::int16_t minimumIndex,
        std::int16_t& outSaveIndex);

    bool ResolveOrCreateConstantValue(
        const char* spaceTag,
        const char* name,
        std::int32_t minimumValue,
        std::int32_t& outValue);

    std::int32_t GetPersistedConstant(const char* spaceTag, const char* name);
    void         SetPersistedConstant(const char* spaceTag, const char* name,
                                      std::int32_t value);
    void         ForEachPersistedConstant(const char* spaceTag,
                                          void (*fn)(const char* name,
                                                     std::int32_t value));

    std::uint8_t GetPersistedOutfitPartsType(const char* key);
    std::uint8_t GetPersistedOutfitSelector(const char* key);
    void         SetPersistedOutfitIds(const char* key,
                                       std::uint8_t partsType,
                                       std::uint8_t selector);

    constexpr std::size_t kPersistedVariantSelectorSlots = 254;

    std::size_t GetPersistedOutfitVariantSelectors(const char* key,
                                                   std::uint8_t* out,
                                                   std::size_t cap);
    void        SetPersistedOutfitVariantSelectors(const char* key,
                                                   const std::uint8_t* selectors,
                                                   std::size_t count);
    void        ClearPersistedOutfitIds(const char* key);

    void ForEachPersistedOutfit(
        const std::function<void(const std::string& key,
                                 std::uint8_t partsType,
                                 std::uint8_t selector,
                                 const std::uint8_t* variants)>& callback);


    bool ResolveOrCreateBluePrintId(const char* key, std::int32_t& outId);
    std::int32_t GetBluePrintId(const char* key);
    void ForEachBluePrint(void (*fn)(const char* key, std::int32_t id, bool owned));
    void SetBluePrintOwned(const char* key, bool owned);
    bool GetBluePrintOwned(const char* key);
    void SetBluePrintNew(const char* key, bool isNew);
    bool GetBluePrintNew(const char* key);

    void SetTapeOwned(const char* key, bool owned);
    void SetTapeNew(const char* key, bool isNew);


    constexpr std::size_t  kDispatchRewardRecordSize     = 0x30;
    constexpr std::int32_t kDispatchOrphanGraceLaunches  = 2;

    struct DispatchMission
    {
        std::string  key;
        std::int32_t deployMissionId = 0;
        std::int32_t rewardRow = 0;
        bool         cleared = false;
        bool         rewardPending = false;
        bool         hasReward = false;
        std::uint8_t reward[kDispatchRewardRecordSize] = {};
        std::int32_t misses = 0;
    };

    struct DispatchSlotFact
    {
        std::int32_t slot = 0;
        std::int32_t placeholderId = 0;
        std::int32_t seed = 0;
        std::string  key;
        std::int32_t misses = 0;
    };

    struct DispatchCursor
    {
        std::string  key;
        std::int32_t anchor = 0;
    };

    void SetDispatchMission(const DispatchMission& mission);
    void MarkDispatchMissionRegistered(const char* key);
    std::vector<DispatchMission> GetDispatchMissions();

    std::vector<DispatchSlotFact> GetDispatchSlotFacts();
    void SetDispatchSlotFacts(const std::vector<DispatchSlotFact>& facts);

    DispatchCursor GetDispatchCursor(bool rareBand);
    void SetDispatchCursor(bool rareBand, const DispatchCursor& cursor);


    void SetTapeOwnedBySaveIndex(std::int16_t saveIndex, bool owned);
    void SetTapeNewBySaveIndex(std::int16_t saveIndex, bool isNew);


    bool GetTapeOwned(const char* key);
    bool GetTapeNew(const char* key);


    void ForEachTape(
        const std::function<void(const std::string& key,
                                 std::int16_t saveIndex,
                                 bool owned,
                                 bool isNew)>& callback);


    constexpr std::int8_t kNoOverride = -1;

    struct CallMenuItem
    {
        std::string                key;
        std::uint64_t              label          = 0;
        std::uint64_t              description    = 0;
        std::int32_t               column         = 0;
        std::int32_t               when           = 0;
        std::uint32_t              message        = 0;
        std::uint32_t              exclusive      = 0;
        std::int32_t               inPlaceOf      = -1;
        std::int32_t               icon           = 0;
        std::uint64_t              iconTexture    = 0;
        std::int32_t               iconCell       = 0;
        bool                       hasIconUvShift = false;
        float                      iconUvShift[2]  = {};
        float                      iconUvRepeat[2] = { 0.5f, 0.5f };
        std::vector<std::uint32_t> voices;
        std::int8_t                enabled        = kNoOverride;
        std::int8_t                visible        = kNoOverride;
        bool                       hasInPlaceOfOverride = false;
        std::int32_t               inPlaceOfOverride    = -1;
        std::int32_t               page           = 0;    // where it was last placed, 0 = not placed yet
        std::int32_t               row            = -1;
        std::int32_t               misses         = 0;    // launches since a mod last added it
    };

    void SetCallMenuItem(const CallMenuItem& item);
    std::vector<CallMenuItem> GetCallMenuItems();


    void Reset();
}
