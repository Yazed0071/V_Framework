#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace deployMission
{
    constexpr std::uint16_t kFirstCustomId    = 0x100;
    constexpr std::uint16_t kLastCustomId     = 0xFFF0;
    constexpr std::uint16_t kVanillaIdBound   = 0x86;
    constexpr std::uint16_t kFirstPoolId      = 0x15;
    constexpr std::uint16_t kLastPoolId       = 0x77;
    constexpr std::uint8_t  kPoolCapacity     = 0x63;
    constexpr std::size_t   kRecordSize       = 0x38;
    constexpr std::size_t   kRewardSize       = 0x30;
    constexpr std::uint8_t  kSlotCount        = 0x47;
    constexpr std::uint8_t  kTutorialSlot     = 0x15;
    constexpr std::uint8_t  kFirstScriptSlot  = 0x37;
    constexpr std::uint8_t  kLastScriptSlot   = 0x3C;
    constexpr std::uint8_t  kRarityNone       = 0;
    constexpr std::uint8_t  kRarityN          = 1;
    constexpr std::uint16_t kFirstRevengeId   = 0x78;
    constexpr std::uint16_t kLastRevengeId    = 0x85;

    struct MissionView
    {
        std::string   key;
        std::uint16_t id = 0;
        std::uint8_t  record[kRecordSize] = {};
        std::uint8_t  rewardRow = 0;
        std::uint64_t nameHash = 0;
        std::uint64_t infoHash = 0;
        bool          registeredThisLaunch = false;
        bool          important = false;
        std::uint16_t revengeMissionId = 0;

        std::uint8_t Category() const { return record[1]; }
        std::uint8_t Rarity() const { return record[2]; }
        std::uint8_t Rank() const { return record[3]; }
    };

    enum class ScriptAddResult { Added, Queued, AlreadyListed, Cleared, Refused };

    struct NoticeCounts
    {
        std::uint8_t storyListed = 0;
        std::uint8_t revengeListed = 0;
    };

    bool IsCustomId(std::uint32_t id);
    bool IsValidKey(const char* key);
    bool IsSafeLangId(const std::string& langId);
    bool CopyEngineMemory(void* destination, const void* source, std::size_t count);
    bool WriteVtableSlot(void** slot, void* function);

    template <typename T>
    bool ReadEngine(const void* source, T& value)
    {
        return source && CopyEngineMemory(&value, source, sizeof(T));
    }

    template <typename T>
    bool WriteEngine(void* destination, const T& value)
    {
        return destination && CopyEngineMemory(destination, &value, sizeof(T));
    }

    template <typename T>
    T* EnginePointerAt(std::uint8_t* base, std::size_t offset)
    {
        T* pointer = nullptr;
        if (!base || !CopyEngineMemory(&pointer, base + offset, sizeof(pointer)))
            return nullptr;
        return pointer;
    }

    std::uint8_t* DeployIface();
    std::uint8_t* RewardIface();

    std::uint32_t* SlotWords(std::uint8_t* iface);
    std::uint16_t  DecodeSlotId(std::uint32_t word);
    bool           IsSlotAccepted(std::uint32_t word);
    std::uint16_t  SlotSeed(std::uint32_t word);

    const std::uint8_t* VanillaRecord(std::uint8_t* iface, std::uint16_t id);
    std::uint8_t        VanillaPoolCount(std::uint8_t* iface);

    std::uint16_t RegisterMission(const char* key, const std::uint8_t* record,
                                  const std::string& nameLangId, const std::string& infoLangId,
                                  bool important, std::uint16_t revengeMissionId);
    bool          RegisterReward(const char* key, const std::uint8_t* reward);
    std::uint16_t FindId(const char* key);

    bool FindMission(std::uint16_t id, MissionView& out);
    bool FindMissionByKey(const char* key, MissionView& out);

    void Initialize();
    bool IsArmed();

    bool FindLiveMissionInSlot(std::uint8_t* iface, std::uint8_t slot, MissionView& out);
    bool FindLiveMissionByPair(std::uint8_t* iface, std::uint16_t placeholderId,
                               std::uint16_t seed, MissionView& out);

    std::uint16_t ChoosePlaceholder(std::uint8_t* iface, std::uint8_t slot, const MissionView& mission);
    bool          SeedCollides(std::uint8_t* iface, std::uint8_t slot, std::uint16_t id, std::uint16_t seed);
    void          BindSlot(std::uint8_t slot, std::uint16_t placeholderId, std::uint16_t seed,
                           const std::string& key);
    void          ForgetForeignFacts(std::uint8_t slot, std::uint16_t id, std::uint16_t seed,
                                     const std::string& keepKey);
    void          CopyFactToLogSlot(std::uint8_t* iface, std::uint8_t sourceSlot, std::uint8_t logSlot);

    std::uint8_t  NextLogSlot(std::uint8_t* iface);
    std::uint16_t VanillaPoolIdWithCategory(std::uint8_t* iface, std::uint8_t category);
    bool          HasRotationMissions();
    bool          AnyLiveFact(std::uint8_t* iface);

    std::uint16_t EffectiveRingPosition(std::uint8_t* iface, bool rareBand);
    std::uint16_t NextInRing(std::uint8_t* iface, bool rareBand, std::uint16_t from);
    void          CommitRingPick(std::uint8_t* iface, bool rareBand, std::uint16_t pick);
    void          AdoptCustomCursor(std::uint8_t* iface, bool rareBand, std::uint16_t customId,
                                    std::uint16_t anchor);
    std::uint8_t  RankOf(std::uint8_t* iface, std::uint16_t id);

    bool RewardRowFor(std::uint16_t id, std::uint8_t& row);
    void EnsureRewardRowsWritten();

    ScriptAddResult AddScriptMission(const char* key);
    void            MarkCompleted(std::uint16_t id);
    bool            IsCleared(const char* key);
    bool            UnsetCleared(const char* key);

    void ArrangeListRows(std::uint8_t* rows, std::size_t count);
    bool CountImportantAsStory(std::uint8_t* iface, NoticeCounts& saved);
    void RestoreNoticeCounts(std::uint8_t* iface, const NoticeCounts& saved);

    void OnRestorePoint();

    void          CallClear(std::uint8_t* iface, std::uint8_t slot);
    std::uint16_t CallCreateSeed(std::uint8_t* iface);
    void          CallGenerate(std::uint8_t* iface, std::uint8_t slot, std::uint16_t id,
                               std::uint16_t seed, std::uint8_t rank);
    void          CallSetRevengeCleared(std::uint8_t* iface, std::uint16_t revengeId);
}
