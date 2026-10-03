#include "pch.h"
#include "DeployMissionRegistry.h"

#include "AddressSet.h"
#include "FoxHashes.h"
#include "HookUtils.h"
#include "V_FrameWorkState.h"
#include "log.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <unordered_map>
#include <vector>

namespace deployMission
{
    namespace
    {
        constexpr std::uint8_t  kFirstRewardRow   = 0x86;
        constexpr std::uint8_t  kLastRewardRow    = 0xFD;
        constexpr std::uint16_t kLastSeqId        = 0x14;
        constexpr std::int32_t  kFactMissLimit    = 3;
        constexpr std::size_t   kFactsKeptPerSlot = 8;
        constexpr std::size_t   kMissionInfoSize  = 0xB0;
        constexpr std::size_t   kInfoSeed         = 0x54;
        constexpr std::size_t   kInfoMissionId    = 0xA4;
        constexpr std::size_t   kRandomListRows   = 10;
        constexpr std::uint8_t  kFirstRevengeSlot = 0x29;
        constexpr std::size_t   kStoryListedOffset   = 0x1E94;
        constexpr std::size_t   kRevengeListedOffset = 0x1E96;

        constexpr std::size_t kRecordBase        = 0x3C;
        constexpr std::size_t kPoolCountOffset   = 0x1D8C;
        constexpr std::size_t kSlotWordsOffset   = 0x1D90;
        constexpr std::size_t kLogIndexOffset    = 0x1E50;
        constexpr std::size_t kNewestNOffset     = 0x1E60;
        constexpr std::size_t kNewestROffset     = 0x1E68;
        constexpr std::uint8_t kFirstLogSlot     = 0x3D;
        constexpr std::uint8_t kLastLogSlot      = 0x46;

        constexpr std::size_t kRewardRowBase       = 8;
        constexpr std::size_t kRewardPoolCount     = 0x3008;
        constexpr std::size_t kPendingRewardWords  = 0x3010;
        constexpr std::size_t kPendingRewardSources = 0x3018;
        constexpr std::size_t kPendingRewardCount  = 0x3030;
        constexpr std::uint8_t kPendingRewardLimit = 200;
        constexpr std::size_t kMbmDeployOffset     = 0xB00;
        constexpr std::size_t kMbmRewardOffset     = 0xAF0;

        constexpr std::size_t kVtClear      = 0x08;
        constexpr std::size_t kVtFunnel     = 0xA8;
        constexpr std::size_t kVtGenerate   = 0xB8;
        constexpr std::size_t kVtCreateSeed = 0xC0;
        constexpr std::size_t kVtCancel     = 0x118;
        constexpr std::size_t kVtSetRevengeCleared = 0xE8;

        constexpr std::size_t kRewardSection        = 0x01;
        constexpr std::size_t kRewardMainType       = 0x02;
        constexpr std::size_t kRewardGmp            = 0x04;
        constexpr std::size_t kRewardStaffWeights   = 0x0A;
        constexpr std::size_t kRewardStaffBuckets   = 12;
        constexpr std::size_t kRewardDrawCount      = 0x18;
        constexpr std::size_t kRewardResourceWeights = 0x1A;
        constexpr std::size_t kRewardResourceBuckets = 13;
        constexpr std::size_t kRewardKeyItem        = 0x28;
        constexpr std::size_t kRewardFlags          = 0x2C;
        constexpr std::uint16_t kNoKeyItem          = 0xFFFF;
        constexpr std::uint8_t  kHighestRarity      = 3;

        constexpr std::size_t kRewardByteLimits[] = { 0x08, 0x09, 0x16, 0x2A, 0x2B };

        struct Mission
        {
            std::string   key;
            std::uint16_t id = 0;
            std::uint8_t  record[kRecordSize] = {};
            bool          hasReward = false;
            std::uint8_t  reward[kRewardSize] = {};
            std::string   nameLangId;
            std::string   infoLangId;
            std::uint8_t  rewardRow = 0;
            bool          registeredThisLaunch = false;
            bool          important = false;
            std::uint16_t revengeMissionId = 0;
        };

        struct RewardLimits
        {
            bool          ready = false;
            std::int32_t  gmp = 0;
            std::uint8_t  byteMax[kRewardSize] = {};
            std::uint16_t drawCount = 0;
            double        staffShare[kRewardStaffBuckets] = {};
            double        resourceShare[kRewardResourceBuckets] = {};
            std::set<std::uint16_t> keyItems;
            std::set<std::uint8_t>  mainRewardTypes;
            std::uint8_t  commonMainRewardType = 0;
        };

        std::recursive_mutex g_Lock;
        bool g_Loaded = false;
        bool g_Armed = false;
        bool g_RewardRowsWritten = false;

        std::map<std::string, Mission>          g_Missions;
        std::map<std::string, Mission>          g_RewardOnlyMissions;
        std::map<std::uint16_t, std::string>    g_KeyById;
        std::map<std::string, V_FrameWorkState::DispatchMission> g_Rows;
        std::vector<V_FrameWorkState::DispatchSlotFact> g_Facts;
        V_FrameWorkState::DispatchCursor g_Cursors[2];
        std::vector<std::string> g_QueuedScriptMissions;
        RewardLimits g_Limits;

        bool IsRewardRow(std::int32_t row)
        {
            return row >= kFirstRewardRow && row <= kLastRewardRow;
        }

        V_FrameWorkState::DispatchMission RowOf_NoLock(const std::string& key)
        {
            const auto found = g_Rows.find(key);
            if (found != g_Rows.end())
                return found->second;
            V_FrameWorkState::DispatchMission row;
            row.key = key;
            return row;
        }

        void SaveRow_NoLock(const V_FrameWorkState::DispatchMission& row)
        {
            g_Rows[row.key] = row;
            V_FrameWorkState::SetDispatchMission(row);
        }

        bool IsInstalled_NoLock(const std::string& key)
        {
            if (g_Missions.count(key) != 0)
                return true;
            const auto row = g_Rows.find(key);
            return row != g_Rows.end() && IsCustomId(static_cast<std::uint32_t>(row->second.deployMissionId))
                && row->second.misses < V_FrameWorkState::kDispatchOrphanGraceLaunches;
        }

        std::uint16_t AllocateId_NoLock()
        {
            std::set<std::int32_t> taken;
            for (const auto& entry : g_Rows)
                taken.insert(entry.second.deployMissionId);
            for (std::int32_t id = kFirstCustomId; id <= kLastCustomId; ++id)
                if (taken.count(id) == 0)
                    return static_cast<std::uint16_t>(id);
            return 0;
        }

        Mission RewardOnlyMission(const V_FrameWorkState::DispatchMission& row)
        {
            Mission mission;
            mission.key = row.key;
            mission.id = static_cast<std::uint16_t>(row.deployMissionId);
            mission.hasReward = true;
            std::memcpy(mission.reward, row.reward, kRewardSize);
            mission.rewardRow = static_cast<std::uint8_t>(row.rewardRow);
            return mission;
        }

        void* QuarkSystemTable()
        {
            using GetQuarkSystemTable_t = void* (__fastcall*)();
            const auto getTable = reinterpret_cast<GetQuarkSystemTable_t>(
                ResolveGameAddress(gAddr.GetQuarkSystemTable));
            return getTable ? getTable() : nullptr;
        }

        std::uint8_t* MbmMember_Guarded(void* table, std::size_t offset)
        {
            __try
            {
                if (!table)
                    return nullptr;
                const auto holder = *reinterpret_cast<std::uint8_t**>(static_cast<std::uint8_t*>(table) + 0x98);
                if (!holder)
                    return nullptr;
                const auto management = *reinterpret_cast<std::uint8_t**>(holder + 0x110);
                return management ? *reinterpret_cast<std::uint8_t**>(management + offset) : nullptr;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return nullptr;
            }
        }

        bool CopyGuarded(void* destination, const void* source, std::size_t count)
        {
            __try
            {
                std::memcpy(destination, source, count);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        template <typename T>
        T* PointerAt(std::uint8_t* base, std::size_t offset)
        {
            T* pointer = nullptr;
            if (!base || !CopyGuarded(&pointer, base + offset, sizeof(pointer)))
                return nullptr;
            return pointer;
        }

        void** VtableOf(std::uint8_t* object)
        {
            return PointerAt<void*>(object, 0);
        }

        std::uint64_t LangHash(const std::string& langId)
        {
            return langId.empty() ? 0 : FoxHashes::StrCode64(langId);
        }

        bool RecordOffline(const std::uint8_t* record)
        {
            return record && record[0] == 0;
        }

        void PersistFacts_NoLock()
        {
            V_FrameWorkState::SetDispatchSlotFacts(g_Facts);
        }

        void EnsureLoaded_NoLock()
        {
            if (g_Loaded)
                return;
            g_Loaded = true;

            for (const V_FrameWorkState::DispatchMission& row : V_FrameWorkState::GetDispatchMissions())
            {
                g_Rows[row.key] = row;
                if (row.rewardPending && row.hasReward && IsRewardRow(row.rewardRow))
                    g_RewardOnlyMissions[row.key] = RewardOnlyMission(row);
            }

            g_Facts = V_FrameWorkState::GetDispatchSlotFacts();
            g_Cursors[0] = V_FrameWorkState::GetDispatchCursor(false);
            g_Cursors[1] = V_FrameWorkState::GetDispatchCursor(true);
        }

        MissionView ViewOf(const Mission& mission)
        {
            MissionView view;
            view.key = mission.key;
            view.id = mission.id;
            std::memcpy(view.record, mission.record, kRecordSize);
            view.rewardRow = mission.hasReward ? mission.rewardRow : 0;
            view.nameHash = LangHash(mission.nameLangId);
            view.infoHash = LangHash(mission.infoLangId);
            view.registeredThisLaunch = mission.registeredThisLaunch;
            view.important = mission.important;
            view.revengeMissionId = mission.revengeMissionId;
            return view;
        }

        const Mission* MissionById_NoLock(std::uint16_t id)
        {
            const auto key = g_KeyById.find(id);
            if (key == g_KeyById.end())
                return nullptr;
            const auto mission = g_Missions.find(key->second);
            return (mission != g_Missions.end()) ? &mission->second : nullptr;
        }

        bool ReadWord(std::uint32_t* words, std::uint8_t slot, std::uint32_t& word)
        {
            return words && slot < kSlotCount && CopyGuarded(&word, words + slot, sizeof(word));
        }

        bool FactIsLive(std::uint32_t* words, const V_FrameWorkState::DispatchSlotFact& fact)
        {
            if (fact.slot < 0 || fact.slot >= kSlotCount)
                return false;
            std::uint32_t word = 0;
            if (!ReadWord(words, static_cast<std::uint8_t>(fact.slot), word))
                return false;
            return DecodeSlotId(word) == fact.placeholderId && SlotSeed(word) == fact.seed;
        }

        const V_FrameWorkState::DispatchSlotFact* LiveFactInSlot_NoLock(std::uint32_t* words, std::uint8_t slot)
        {
            for (auto it = g_Facts.rbegin(); it != g_Facts.rend(); ++it)
                if (it->slot == slot && FactIsLive(words, *it))
                    return &*it;
            return nullptr;
        }

        std::uint8_t PoolCount(std::uint8_t* iface)
        {
            std::uint8_t count = 0;
            if (!iface || !CopyGuarded(&count, iface + kPoolCountOffset, sizeof(count)))
                return 0;
            return static_cast<std::uint8_t>(std::min<std::size_t>(count, kPoolCapacity));
        }

        bool ReadVanillaRecord(std::uint8_t* iface, std::uint16_t id, std::uint8_t* out)
        {
            return iface && id < kVanillaIdBound
                && CopyGuarded(out, iface + kRecordBase + id * kRecordSize, kRecordSize);
        }

        std::uint16_t* CursorOf(std::uint8_t* iface, bool rareBand)
        {
            return PointerAt<std::uint16_t>(iface, rareBand ? kNewestROffset : kNewestNOffset);
        }

        bool ReadCursor(std::uint8_t* iface, bool rareBand, std::uint16_t& value)
        {
            std::uint16_t* cursor = CursorOf(iface, rareBand);
            return cursor && CopyGuarded(&value, cursor, sizeof(value));
        }

        bool RarityMatches(std::uint8_t rarity, bool rareBand)
        {
            return rareBand ? static_cast<std::uint8_t>(rarity - 2) < 2 : rarity == kRarityN;
        }

        std::vector<std::uint16_t> Ring_NoLock(std::uint8_t* iface, bool rareBand)
        {
            std::vector<std::uint16_t> ring;
            std::uint8_t record[kRecordSize] = {};
            for (std::uint16_t id = kFirstPoolId; id <= kLastPoolId; ++id)
                if (ReadVanillaRecord(iface, id, record) && RarityMatches(record[2], rareBand))
                    ring.push_back(id);

            for (const auto& entry : g_KeyById)
            {
                const Mission* mission = MissionById_NoLock(entry.first);
                if (mission && mission->registeredThisLaunch && RarityMatches(mission->record[2], rareBand))
                    ring.push_back(entry.first);
            }
            return ring;
        }

        void ComputeRewardLimits_NoLock(std::uint8_t* rewardIface)
        {
            if (g_Limits.ready || !rewardIface)
                return;

            std::uint8_t poolCount = 0;
            CopyGuarded(&poolCount, rewardIface + kRewardPoolCount, sizeof(poolCount));
            poolCount = static_cast<std::uint8_t>(std::min<std::size_t>(poolCount, kPoolCapacity));

            std::vector<std::uint16_t> rows;
            for (std::uint16_t key = 1; key <= kLastSeqId; ++key)
                rows.push_back(key);
            for (std::uint16_t key = kFirstRevengeId; key <= kLastRevengeId; ++key)
                rows.push_back(key);
            for (std::uint16_t row = 0; row < poolCount; ++row)
                rows.push_back(static_cast<std::uint16_t>(kFirstPoolId + row));

            RewardLimits limits;
            std::map<std::uint8_t, int> mainTypeVotes;
            std::size_t used = 0;
            for (const std::uint16_t key : rows)
            {
                std::uint8_t reward[kRewardSize] = {};
                if (!CopyGuarded(reward, rewardIface + kRewardRowBase + key * kRewardSize, kRewardSize))
                    continue;
                if (reward[0] == 0xFF || reward[kRewardSection] == 0xFF)
                    continue;
                ++used;

                std::int32_t gmp = 0;
                std::memcpy(&gmp, reward + kRewardGmp, sizeof(gmp));
                limits.gmp = (std::max)(limits.gmp, gmp);
                for (const std::size_t offset : kRewardByteLimits)
                    limits.byteMax[offset] = (std::max)(limits.byteMax[offset], reward[offset]);
                std::uint16_t drawCount = 0;
                std::memcpy(&drawCount, reward + kRewardDrawCount, sizeof(drawCount));
                limits.drawCount = (std::max)(limits.drawCount, drawCount);

                auto shareMax = [&](std::size_t offset, std::size_t buckets, double* share)
                {
                    int total = 0;
                    for (std::size_t i = 0; i < buckets; ++i)
                        total += reward[offset + i];
                    if (total == 0)
                        return;
                    for (std::size_t i = 0; i < buckets; ++i)
                        share[i] = (std::max)(share[i], static_cast<double>(reward[offset + i]) / total);
                };
                shareMax(kRewardStaffWeights, kRewardStaffBuckets, limits.staffShare);
                shareMax(kRewardResourceWeights, kRewardResourceBuckets, limits.resourceShare);

                std::uint16_t keyItem = kNoKeyItem;
                std::memcpy(&keyItem, reward + kRewardKeyItem, sizeof(keyItem));
                if (keyItem != kNoKeyItem)
                    limits.keyItems.insert(keyItem);
                limits.mainRewardTypes.insert(reward[kRewardMainType]);
                ++mainTypeVotes[reward[kRewardMainType]];
            }

            if (used == 0)
                return;

            int bestVotes = -1;
            for (const auto& vote : mainTypeVotes)
                if (vote.second > bestVotes)
                {
                    bestVotes = vote.second;
                    limits.commonMainRewardType = vote.first;
                }
            limits.ready = true;
            g_Limits = limits;
        }

        void CapBuckets(std::uint8_t* weights, std::size_t buckets, const double* maxShare)
        {
            int total = 0;
            for (std::size_t i = 0; i < buckets; ++i)
                total += weights[i];
            if (total == 0)
                return;

            int capped[kRewardResourceBuckets] = {};
            int spent = 0;
            for (std::size_t i = 1; i < buckets; ++i)
            {
                const int limit = static_cast<int>(maxShare[i] * total);
                capped[i] = (std::min)(static_cast<int>(weights[i]), limit);
                spent += capped[i];
            }
            capped[0] = total - spent;

            if (capped[0] > 0xFF)
            {
                const double scale = 255.0 / capped[0];
                for (std::size_t i = 0; i < buckets; ++i)
                    capped[i] = static_cast<int>(capped[i] * scale);
            }
            for (std::size_t i = 0; i < buckets; ++i)
                weights[i] = static_cast<std::uint8_t>(std::clamp(capped[i], 0, 0xFF));
        }

        void ClampReward_NoLock(const std::uint8_t* raw, std::uint8_t row, std::uint8_t* out)
        {
            std::memcpy(out, raw, kRewardSize);
            out[0] = row;
            out[3] = 0;
            out[0x17] = 0;
            out[0x27] = 0;
            out[kRewardFlags] &= 1;
            out[0x2D] = out[0x2E] = out[0x2F] = 0;

            if (g_Limits.mainRewardTypes.count(out[kRewardMainType]) == 0)
                out[kRewardMainType] = g_Limits.commonMainRewardType;

            std::int32_t gmp = 0;
            std::memcpy(&gmp, out + kRewardGmp, sizeof(gmp));
            gmp = std::clamp(gmp, 0, g_Limits.gmp);
            std::memcpy(out + kRewardGmp, &gmp, sizeof(gmp));

            for (const std::size_t offset : kRewardByteLimits)
                out[offset] = (std::min)(out[offset], g_Limits.byteMax[offset]);

            std::uint16_t drawCount = 0;
            std::memcpy(&drawCount, out + kRewardDrawCount, sizeof(drawCount));
            drawCount = (std::min)(drawCount, g_Limits.drawCount);
            std::memcpy(out + kRewardDrawCount, &drawCount, sizeof(drawCount));

            CapBuckets(out + kRewardStaffWeights, kRewardStaffBuckets, g_Limits.staffShare);
            CapBuckets(out + kRewardResourceWeights, kRewardResourceBuckets, g_Limits.resourceShare);

            std::uint16_t keyItem = kNoKeyItem;
            std::memcpy(&keyItem, out + kRewardKeyItem, sizeof(keyItem));
            if (keyItem != kNoKeyItem && g_Limits.keyItems.count(keyItem) == 0)
            {
                keyItem = kNoKeyItem;
                std::memcpy(out + kRewardKeyItem, &keyItem, sizeof(keyItem));
            }
        }

        void WriteRewardRow_NoLock(std::uint8_t* rewardIface, const Mission& mission)
        {
            if (!mission.hasReward || mission.rewardRow == 0)
                return;
            std::uint8_t clamped[kRewardSize] = {};
            ClampReward_NoLock(mission.reward, mission.rewardRow, clamped);
            CopyGuarded(rewardIface + kRewardRowBase + mission.rewardRow * kRewardSize, clamped, kRewardSize);
        }

        void WriteRewardRows_NoLock()
        {
            std::uint8_t* rewardIface = RewardIface();
            if (!rewardIface)
                return;
            ComputeRewardLimits_NoLock(rewardIface);
            if (!g_Limits.ready)
            {
                static bool s_said = false;
                if (!s_said)
                {
                    s_said = true;
                    LogWarn("Dispatch: vanilla dispatch rewards not registered yet, custom rewards stay empty until they are");
                }
                return;
            }
            for (const auto& entry : g_Missions)
                WriteRewardRow_NoLock(rewardIface, entry.second);
            for (const auto& entry : g_RewardOnlyMissions)
                WriteRewardRow_NoLock(rewardIface, entry.second);
            g_RewardRowsWritten = true;
        }

        std::uint8_t AllocateRewardRow_NoLock(const std::string& key)
        {
            V_FrameWorkState::DispatchMission row = RowOf_NoLock(key);
            if (IsRewardRow(row.rewardRow))
                return static_cast<std::uint8_t>(row.rewardRow);

            std::set<std::int32_t> taken;
            for (const auto& entry : g_Rows)
                taken.insert(entry.second.rewardRow);
            for (std::int32_t candidate = kFirstRewardRow; candidate <= kLastRewardRow; ++candidate)
            {
                if (taken.count(candidate) != 0)
                    continue;
                row.rewardRow = candidate;
                SaveRow_NoLock(row);
                return static_cast<std::uint8_t>(candidate);
            }
            return 0;
        }

        using Clear_t      = void(__fastcall*)(void*, std::uint8_t);
        using CreateSeed_t = std::uint16_t(__fastcall*)(void*);
        using Generate_t   = void(__fastcall*)(void*, std::uint8_t, std::uint16_t, std::uint16_t, std::uint8_t);
        using Funnel_t     = void(__fastcall*)(void*, void*, std::uint8_t);
        using Cancel_t     = void(__fastcall*)(void*, void*);
        using SetRevengeCleared_t = void(__fastcall*)(void*, std::uint16_t, bool);

        template <typename Fn>
        Fn VirtualAt(std::uint8_t* iface, std::size_t offset)
        {
            void** vtable = VtableOf(iface);
            void* function = nullptr;
            if (!vtable || !CopyGuarded(&function, reinterpret_cast<std::uint8_t*>(vtable) + offset, sizeof(function)))
                return nullptr;
            return reinterpret_cast<Fn>(function);
        }

        struct SweepTarget
        {
            std::uint8_t slot = 0;
            std::string  key;
        };

        bool CancelAcceptedSlot(std::uint8_t* iface, std::uint8_t slot)
        {
            const auto funnel = VirtualAt<Funnel_t>(iface, kVtFunnel);
            const auto cancel = VirtualAt<Cancel_t>(iface, kVtCancel);
            if (!funnel || !cancel)
                return false;
            alignas(16) std::uint8_t missionInfo[kMissionInfoSize] = {};
            funnel(iface, missionInfo, slot);
            cancel(iface, missionInfo);
            return true;
        }

        void SweepUninstalled(std::uint8_t* iface, const std::vector<SweepTarget>& targets)
        {
            std::uint32_t* words = SlotWords(iface);
            for (const SweepTarget& target : targets)
            {
                std::uint32_t word = 0;
                if (!ReadWord(words, target.slot, word))
                    continue;
                const bool scriptSlot = target.slot >= kFirstScriptSlot && target.slot <= kLastScriptSlot;
                bool slotFree = true;
                if (IsSlotAccepted(word))
                {
                    slotFree = CancelAcceptedSlot(iface, target.slot);
                    if (slotFree)
                        LogWarn("Dispatch: '%s' uninstalled, deployment in slot %u recalled with no reward",
                            target.key.c_str(), static_cast<unsigned>(target.slot));
                    else
                        LogWarn("Dispatch: '%s' uninstalled, slot %u could not be recalled - the team finishes it as its vanilla placeholder",
                            target.key.c_str(), static_cast<unsigned>(target.slot));
                }
                if (scriptSlot && slotFree)
                    CallClear(iface, target.slot);
            }
        }

        void UpdatePendingRewards_NoLock()
        {
            std::uint8_t* rewardIface = RewardIface();
            std::uint32_t* pendingWords = PointerAt<std::uint32_t>(rewardIface, kPendingRewardWords);
            std::uint8_t pendingCount = 0;
            if (!pendingWords || !CopyGuarded(&pendingCount, rewardIface + kPendingRewardCount, sizeof(pendingCount)))
                return;

            std::uint32_t* pendingSources = PointerAt<std::uint32_t>(rewardIface, kPendingRewardSources);
            std::set<std::int32_t> referencedRows;
            std::set<std::int32_t> referencedIds;
            for (std::uint8_t i = 0; i < pendingCount && i < kPendingRewardLimit; ++i)
            {
                std::uint32_t word = 0;
                if (!CopyGuarded(&word, pendingWords + i, sizeof(word)) || ((word >> 9) & 0x1F) == 0)
                    continue;
                const std::int32_t row = static_cast<std::int32_t>(word & 0x1FF);
                if (IsRewardRow(row))
                    referencedRows.insert(row);
                std::uint32_t source = 0;
                if (pendingSources && CopyGuarded(&source, pendingSources + i, sizeof(source)) && IsCustomId(source))
                    referencedIds.insert(static_cast<std::int32_t>(source));
            }

            for (const auto& entry : g_Rows)
            {
                V_FrameWorkState::DispatchMission row = entry.second;
                const bool pending = referencedIds.count(row.deployMissionId) != 0
                                  || referencedRows.count(row.rewardRow) != 0;
                const auto live = g_Missions.find(row.key);
                const bool registered = live != g_Missions.end();

                row.rewardPending = pending;
                row.hasReward = false;
                std::memset(row.reward, 0, sizeof(row.reward));
                if (pending && registered && live->second.hasReward)
                {
                    row.hasReward = true;
                    std::memcpy(row.reward, live->second.reward, kRewardSize);
                }
                else if (pending && entry.second.hasReward)
                {
                    row.hasReward = true;
                    std::memcpy(row.reward, entry.second.reward, kRewardSize);
                }

                if (!pending && registered && !live->second.hasReward && row.rewardRow != 0)
                {
                    row.rewardRow = 0;
                    live->second.rewardRow = 0;
                }
                if (!pending)
                    g_RewardOnlyMissions.erase(row.key);
                SaveRow_NoLock(row);
            }
        }
    }

    bool IsSafeLangId(const std::string& langId)
    {
        return langId.find_first_of("\"\\\r\n") == std::string::npos;
    }

    bool IsCustomId(std::uint32_t id)
    {
        return id >= kFirstCustomId && id <= kLastCustomId;
    }

    bool IsValidKey(const char* key)
    {
        if (!key || !key[0])
            return false;
        const char head = key[0];
        if (!((head >= 'A' && head <= 'Z') || (head >= 'a' && head <= 'z') || head == '_'))
            return false;
        std::size_t length = 0;
        for (const char* p = key; *p; ++p, ++length)
        {
            const char c = *p;
            const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
                         || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
            if (!ok)
                return false;
        }
        return length <= 96;
    }

    bool CopyEngineMemory(void* destination, const void* source, std::size_t count)
    {
        return destination && source && CopyGuarded(destination, source, count);
    }

    bool WriteVtableSlot(void** slot, void* function)
    {
        DWORD previous = 0;
        if (!slot || !VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &previous))
            return false;
        const bool written = CopyGuarded(slot, &function, sizeof(function));
        VirtualProtect(slot, sizeof(void*), previous, &previous);
        return written;
    }

    std::uint8_t NextLogSlot(std::uint8_t* iface)
    {
        std::uint8_t* logIndex = PointerAt<std::uint8_t>(iface, kLogIndexOffset);
        std::uint8_t index = 0;
        if (!logIndex || !CopyGuarded(&index, logIndex, sizeof(index)))
            return 0;
        const std::uint8_t slot = static_cast<std::uint8_t>(index + kFirstLogSlot);
        return std::clamp<std::uint8_t>(slot, kFirstLogSlot, kLastLogSlot);
    }

    std::uint16_t VanillaPoolIdWithCategory(std::uint8_t* iface, std::uint8_t category)
    {
        const std::uint8_t poolCount = PoolCount(iface);
        std::uint8_t record[kRecordSize] = {};
        for (std::uint16_t offset = 0; offset < poolCount; ++offset)
        {
            const std::uint16_t id = static_cast<std::uint16_t>(kFirstPoolId + offset);
            if (ReadVanillaRecord(iface, id, record) && record[1] == category)
                return id;
        }
        return 0;
    }

    bool HasRotationMissions()
    {
        std::lock_guard<std::recursive_mutex> lock(g_Lock);
        EnsureLoaded_NoLock();
        for (const auto& entry : g_Missions)
            if (entry.second.registeredThisLaunch && entry.second.record[2] != kRarityNone)
                return true;
        return false;
    }

    bool AnyLiveFact(std::uint8_t* iface)
    {
        std::uint32_t* words = SlotWords(iface);
        if (!words)
            return false;
        std::lock_guard<std::recursive_mutex> lock(g_Lock);
        EnsureLoaded_NoLock();
        for (const auto& fact : g_Facts)
            if (FactIsLive(words, fact))
                return true;
        return false;
    }

    std::uint8_t* DeployIface()
    {
        return MbmMember_Guarded(QuarkSystemTable(), kMbmDeployOffset);
    }

    std::uint8_t* RewardIface()
    {
        return MbmMember_Guarded(QuarkSystemTable(), kMbmRewardOffset);
    }

    std::uint32_t* SlotWords(std::uint8_t* iface)
    {
        return PointerAt<std::uint32_t>(iface, kSlotWordsOffset);
    }

    std::uint16_t DecodeSlotId(std::uint32_t word)
    {
        const std::uint16_t id = static_cast<std::uint16_t>((word >> 5) & 0xFF);
        return id < kVanillaIdBound ? id : 0;
    }

    bool IsSlotAccepted(std::uint32_t word)
    {
        return ((word >> 4) & 1) != 0;
    }

    std::uint16_t SlotSeed(std::uint32_t word)
    {
        return static_cast<std::uint16_t>((word >> 13) & 0xFFFF);
    }

    std::uint8_t VanillaPoolCount(std::uint8_t* iface)
    {
        return PoolCount(iface);
    }

    const std::uint8_t* VanillaRecord(std::uint8_t* iface, std::uint16_t id)
    {
        return (iface && id < kVanillaIdBound) ? iface + kRecordBase + id * kRecordSize : nullptr;
    }

    std::uint16_t RegisterMission(const char* key, const std::uint8_t* record,
                                  const std::string& nameLangId, const std::string& infoLangId,
                                  bool important, std::uint16_t revengeMissionId)
    {
        if (!IsValidKey(key) || !record || !IsSafeLangId(nameLangId) || !IsSafeLangId(infoLangId))
            return 0;
        if (record[2] > kHighestRarity)
        {
            LogWarn("RegisterDeployMissionParam '%s': rarity %u is not NONE, N, R or SR, not registered",
                key, static_cast<unsigned>(record[2]));
            return 0;
        }

        std::lock_guard<std::recursive_mutex> lock(g_Lock);
        EnsureLoaded_NoLock();

        V_FrameWorkState::DispatchMission row = RowOf_NoLock(key);
        if (!IsCustomId(static_cast<std::uint32_t>(row.deployMissionId)))
        {
            row.deployMissionId = AllocateId_NoLock();
            if (row.deployMissionId == 0)
            {
                LogWarn("RegisterDeployMissionParam '%s': all %u custom deployMissionIds are taken, not registered",
                    key, static_cast<unsigned>(kLastCustomId - kFirstCustomId + 1));
                return 0;
            }
        }
        row.misses = 0;
        SaveRow_NoLock(row);
        V_FrameWorkState::MarkDispatchMissionRegistered(key);

        const auto rewardOnly = g_RewardOnlyMissions.find(key);
        if (g_Missions.count(key) == 0 && rewardOnly != g_RewardOnlyMissions.end())
        {
            g_Missions[key] = rewardOnly->second;
            g_RewardOnlyMissions.erase(rewardOnly);
        }

        Mission& mission = g_Missions[key];
        mission.key = key;
        mission.id = static_cast<std::uint16_t>(row.deployMissionId);
        std::memcpy(mission.record, record, kRecordSize);
        mission.record[0] = 0;
        mission.nameLangId = nameLangId;
        mission.infoLangId = infoLangId;
        mission.important = important;
        mission.revengeMissionId = revengeMissionId;
        if (mission.rewardRow == 0 && IsRewardRow(row.rewardRow))
            mission.rewardRow = static_cast<std::uint8_t>(row.rewardRow);
        mission.registeredThisLaunch = true;
        g_KeyById[mission.id] = mission.key;
        return mission.id;
    }

    bool RegisterReward(const char* key, const std::uint8_t* reward)
    {
        if (!IsValidKey(key) || !reward)
            return false;

        std::lock_guard<std::recursive_mutex> lock(g_Lock);
        EnsureLoaded_NoLock();

        const auto found = g_Missions.find(key);
        if (found == g_Missions.end() || !found->second.registeredThisLaunch)
        {
            LogWarn("RegisterPoolRewardParam '%s': no dispatch mission registered under that key, reward not registered", key);
            return false;
        }

        Mission& mission = found->second;
        const std::uint8_t row = AllocateRewardRow_NoLock(mission.key);
        if (row == 0)
        {
            LogWarn("RegisterPoolRewardParam '%s': all %d custom reward rows are taken, reward not registered",
                key, kLastRewardRow - kFirstRewardRow + 1);
            return false;
        }

        mission.rewardRow = row;
        mission.hasReward = true;
        std::memcpy(mission.reward, reward, kRewardSize);

        if (std::uint8_t* rewardIface = RewardIface())
        {
            ComputeRewardLimits_NoLock(rewardIface);
            if (g_Limits.ready)
                WriteRewardRow_NoLock(rewardIface, mission);
        }
        return true;
    }

    std::uint16_t FindId(const char* key)
    {
        if (!IsValidKey(key))
            return 0;
        std::lock_guard<std::recursive_mutex> lock(g_Lock);
        EnsureLoaded_NoLock();
        const auto found = g_Missions.find(key);
        if (found != g_Missions.end())
            return found->second.id;
        return IsInstalled_NoLock(key) ? static_cast<std::uint16_t>(g_Rows.find(key)->second.deployMissionId) : 0;
    }

    bool FindMission(std::uint16_t id, MissionView& out)
    {
        if (!IsCustomId(id))
            return false;
        std::lock_guard<std::recursive_mutex> lock(g_Lock);
        EnsureLoaded_NoLock();
        const Mission* mission = MissionById_NoLock(id);
        if (!mission)
            return false;
        out = ViewOf(*mission);
        return true;
    }

    bool FindMissionByKey(const char* key, MissionView& out)
    {
        if (!IsValidKey(key))
            return false;
        std::lock_guard<std::recursive_mutex> lock(g_Lock);
        EnsureLoaded_NoLock();
        const auto found = g_Missions.find(key);
        if (found == g_Missions.end())
            return false;
        out = ViewOf(found->second);
        return true;
    }

    void Initialize()
    {
        std::lock_guard<std::recursive_mutex> lock(g_Lock);
        EnsureLoaded_NoLock();
    }

    bool IsArmed()
    {
        std::lock_guard<std::recursive_mutex> lock(g_Lock);
        return g_Armed;
    }

    bool FindLiveMissionInSlot(std::uint8_t* iface, std::uint8_t slot, MissionView& out)
    {
        std::uint32_t* words = SlotWords(iface);
        if (!words)
            return false;
        std::lock_guard<std::recursive_mutex> lock(g_Lock);
        EnsureLoaded_NoLock();
        const V_FrameWorkState::DispatchSlotFact* fact = LiveFactInSlot_NoLock(words, slot);
        if (!fact)
            return false;
        const auto mission = g_Missions.find(fact->key);
        if (mission == g_Missions.end())
            return false;
        out = ViewOf(mission->second);
        return true;
    }

    bool FindLiveMissionByPair(std::uint8_t* iface, std::uint16_t placeholderId,
                               std::uint16_t seed, MissionView& out)
    {
        std::uint32_t* words = SlotWords(iface);
        if (!words)
            return false;
        std::lock_guard<std::recursive_mutex> lock(g_Lock);
        EnsureLoaded_NoLock();
        for (auto it = g_Facts.rbegin(); it != g_Facts.rend(); ++it)
        {
            if (it->placeholderId != placeholderId || it->seed != seed || !FactIsLive(words, *it))
                continue;
            const auto mission = g_Missions.find(it->key);
            if (mission == g_Missions.end())
                return false;
            out = ViewOf(mission->second);
            return true;
        }
        return false;
    }

    std::uint16_t ChoosePlaceholder(std::uint8_t* iface, std::uint8_t slot, const MissionView& mission)
    {
        std::uint32_t* words = SlotWords(iface);
        if (!words)
            return 0;

        std::set<std::uint16_t> listed;
        for (std::uint8_t other = 0; other < kSlotCount; ++other)
        {
            std::uint32_t word = 0;
            if (other != slot && ReadWord(words, other, word) && DecodeSlotId(word) != 0)
                listed.insert(DecodeSlotId(word));
        }

        const std::uint8_t poolCount = PoolCount(iface);
        std::uint16_t best = 0;
        int bestScore = -1;
        std::uint8_t record[kRecordSize] = {};
        for (std::uint16_t offset = 0; offset < poolCount; ++offset)
        {
            const std::uint16_t id = static_cast<std::uint16_t>(
                kFirstPoolId + (offset + mission.id) % poolCount);
            if (listed.count(id) != 0 || !ReadVanillaRecord(iface, id, record) || !RecordOffline(record))
                continue;
            const int score = (record[1] == mission.Category() ? 2 : 0)
                            + (record[2] == mission.Rarity() ? 1 : 0);
            if (score > bestScore)
            {
                best = id;
                bestScore = score;
            }
        }
        return best;
    }

    bool SeedCollides(std::uint8_t* iface, std::uint8_t slot, std::uint16_t id, std::uint16_t seed)
    {
        std::uint32_t* words = SlotWords(iface);
        std::lock_guard<std::recursive_mutex> lock(g_Lock);
        EnsureLoaded_NoLock();
        for (const auto& fact : g_Facts)
            if (fact.placeholderId == id && fact.seed == seed)
                return true;
        for (std::uint8_t other = 0; other < kSlotCount; ++other)
        {
            std::uint32_t word = 0;
            if (other != slot && ReadWord(words, other, word)
                && DecodeSlotId(word) == id && SlotSeed(word) == seed)
                return true;
        }
        return false;
    }

    void BindSlot(std::uint8_t slot, std::uint16_t placeholderId, std::uint16_t seed,
                  const std::string& key)
    {
        std::lock_guard<std::recursive_mutex> lock(g_Lock);
        EnsureLoaded_NoLock();
        V_FrameWorkState::DispatchSlotFact fact;
        fact.slot = slot;
        fact.placeholderId = placeholderId;
        fact.seed = seed;
        fact.key = key;
        g_Facts.push_back(fact);
        PersistFacts_NoLock();
    }

    void ForgetForeignFacts(std::uint8_t slot, std::uint16_t id, std::uint16_t seed,
                            const std::string& keepKey)
    {
        std::lock_guard<std::recursive_mutex> lock(g_Lock);
        EnsureLoaded_NoLock();
        const auto before = g_Facts.size();
        g_Facts.erase(std::remove_if(g_Facts.begin(), g_Facts.end(),
            [&](const V_FrameWorkState::DispatchSlotFact& fact)
            {
                return fact.slot == slot && fact.placeholderId == id && fact.seed == seed
                    && fact.key != keepKey;
            }), g_Facts.end());
        if (g_Facts.size() != before)
            PersistFacts_NoLock();
    }

    void CopyFactToLogSlot(std::uint8_t* iface, std::uint8_t sourceSlot, std::uint8_t logSlot)
    {
        std::uint32_t* words = SlotWords(iface);
        if (!words)
            return;
        std::lock_guard<std::recursive_mutex> lock(g_Lock);
        EnsureLoaded_NoLock();
        const V_FrameWorkState::DispatchSlotFact* fact = LiveFactInSlot_NoLock(words, sourceSlot);
        if (!fact || g_Missions.count(fact->key) == 0)
            return;
        V_FrameWorkState::DispatchSlotFact copy = *fact;
        copy.slot = logSlot;
        copy.misses = 0;
        g_Facts.push_back(copy);
        PersistFacts_NoLock();
    }

    std::uint16_t EffectiveRingPosition(std::uint8_t* iface, bool rareBand)
    {
        std::uint16_t cursor = 0;
        if (!ReadCursor(iface, rareBand, cursor))
            return 0;
        std::lock_guard<std::recursive_mutex> lock(g_Lock);
        EnsureLoaded_NoLock();
        const V_FrameWorkState::DispatchCursor& state = g_Cursors[rareBand ? 1 : 0];
        if (state.key.empty() || state.anchor != cursor)
            return cursor;
        const auto mission = g_Missions.find(state.key);
        return (mission != g_Missions.end()) ? mission->second.id : cursor;
    }

    std::uint16_t NextInRing(std::uint8_t* iface, bool rareBand, std::uint16_t from)
    {
        std::lock_guard<std::recursive_mutex> lock(g_Lock);
        EnsureLoaded_NoLock();
        std::vector<std::uint16_t> ring = Ring_NoLock(iface, rareBand);
        if (ring.empty())
            return kFirstPoolId;
        std::sort(ring.begin(), ring.end());
        const std::uint16_t start = (from >= kFirstPoolId && from <= kLastPoolId) || IsCustomId(from)
            ? from : static_cast<std::uint16_t>(kFirstPoolId - 1);
        const auto next = std::upper_bound(ring.begin(), ring.end(), start);
        return (next != ring.end()) ? *next : ring.front();
    }

    void CommitRingPick(std::uint8_t* iface, bool rareBand, std::uint16_t pick)
    {
        std::uint16_t* cursor = CursorOf(iface, rareBand);
        if (!cursor)
            return;
        std::lock_guard<std::recursive_mutex> lock(g_Lock);
        EnsureLoaded_NoLock();
        V_FrameWorkState::DispatchCursor& state = g_Cursors[rareBand ? 1 : 0];

        if (IsCustomId(pick))
        {
            const Mission* mission = MissionById_NoLock(pick);
            std::uint16_t anchor = 0;
            if (!mission || !CopyGuarded(&anchor, cursor, sizeof(anchor)))
                return;
            state.key = mission->key;
            state.anchor = anchor;
        }
        else
        {
            if (!CopyGuarded(cursor, &pick, sizeof(pick)))
                return;
            state = {};
        }
        V_FrameWorkState::SetDispatchCursor(rareBand, state);
    }

    void AdoptCustomCursor(std::uint8_t* iface, bool rareBand, std::uint16_t customId,
                           std::uint16_t anchor)
    {
        std::uint16_t* cursor = CursorOf(iface, rareBand);
        if (!cursor)
            return;
        std::lock_guard<std::recursive_mutex> lock(g_Lock);
        EnsureLoaded_NoLock();
        const Mission* mission = MissionById_NoLock(customId);
        CopyGuarded(cursor, &anchor, sizeof(anchor));
        V_FrameWorkState::DispatchCursor& state = g_Cursors[rareBand ? 1 : 0];
        state = {};
        if (mission)
        {
            state.key = mission->key;
            state.anchor = anchor;
        }
        V_FrameWorkState::SetDispatchCursor(rareBand, state);
    }

    std::uint8_t RankOf(std::uint8_t* iface, std::uint16_t id)
    {
        if (IsCustomId(id))
        {
            MissionView mission;
            return FindMission(id, mission) ? mission.Rank() : 0;
        }
        std::uint8_t record[kRecordSize] = {};
        return ReadVanillaRecord(iface, id, record) ? record[3] : 0;
    }

    bool RewardRowFor(std::uint16_t id, std::uint8_t& row)
    {
        std::lock_guard<std::recursive_mutex> lock(g_Lock);
        EnsureLoaded_NoLock();
        const Mission* mission = MissionById_NoLock(id);
        if (!mission || !mission->hasReward || mission->rewardRow == 0)
            return false;
        if (!g_RewardRowsWritten)
            WriteRewardRows_NoLock();
        row = mission->rewardRow;
        return g_RewardRowsWritten;
    }

    void EnsureRewardRowsWritten()
    {
        std::lock_guard<std::recursive_mutex> lock(g_Lock);
        EnsureLoaded_NoLock();
        if (!g_RewardRowsWritten)
            WriteRewardRows_NoLock();
    }

    ScriptAddResult AddScriptMission(const char* key)
    {
        std::uint8_t* iface = DeployIface();
        std::uint32_t* words = SlotWords(iface);

        std::uint16_t id = 0;
        std::uint8_t rank = 0;
        std::uint8_t freeSlot = 0;
        {
            std::lock_guard<std::recursive_mutex> lock(g_Lock);
            EnsureLoaded_NoLock();

            const auto found = g_Missions.find(key ? key : "");
            if (found == g_Missions.end() || !found->second.registeredThisLaunch)
            {
                LogWarn("AddDeployMission '%s': no mission registered under that key, not added",
                    key ? key : "?");
                return ScriptAddResult::Refused;
            }
            if (found->second.record[2] != kRarityNone)
            {
                LogWarn("AddDeployMission '%s': rarity %u missions rotate on their own, not added, use rarity NONE",
                    key, static_cast<unsigned>(found->second.record[2]));
                return ScriptAddResult::Refused;
            }

            const auto row = g_Rows.find(found->first);
            if (row != g_Rows.end() && row->second.cleared)
                return ScriptAddResult::Cleared;

            if (!g_Armed || !words)
            {
                if (std::find(g_QueuedScriptMissions.begin(), g_QueuedScriptMissions.end(), key)
                    == g_QueuedScriptMissions.end())
                    g_QueuedScriptMissions.push_back(key);
                return ScriptAddResult::Queued;
            }

            for (std::uint8_t slot = 0; slot < kSlotCount; ++slot)
            {
                const V_FrameWorkState::DispatchSlotFact* fact = LiveFactInSlot_NoLock(words, slot);
                if (fact && fact->key == key && (slot < kFirstLogSlot || slot > kLastLogSlot))
                    return ScriptAddResult::AlreadyListed;
            }

            for (std::uint8_t slot = kFirstScriptSlot; slot <= kLastScriptSlot && freeSlot == 0; ++slot)
            {
                std::uint32_t word = 0;
                if (ReadWord(words, slot, word) && DecodeSlotId(word) == 0)
                    freeSlot = slot;
            }
            if (freeSlot == 0)
            {
                LogWarn("AddDeployMission '%s': all %d script dispatch slots are in use, not added",
                    key, kLastScriptSlot - kFirstScriptSlot + 1);
                return ScriptAddResult::Refused;
            }
            id = found->second.id;
            rank = found->second.record[3];
        }

        CallGenerate(iface, freeSlot, id, CallCreateSeed(iface), rank);
        return ScriptAddResult::Added;
    }

    void OnRestorePoint()
    {
        std::uint8_t* iface = DeployIface();
        std::uint32_t* words = SlotWords(iface);
        if (!iface || !words)
            return;

        std::vector<SweepTarget> sweep;
        std::vector<std::string> queued;
        {
            std::lock_guard<std::recursive_mutex> lock(g_Lock);
            EnsureLoaded_NoLock();
            g_Armed = true;

            std::map<int, std::size_t> keptPerSlot;
            std::vector<V_FrameWorkState::DispatchSlotFact> kept;
            for (auto it = g_Facts.rbegin(); it != g_Facts.rend(); ++it)
            {
                V_FrameWorkState::DispatchSlotFact fact = *it;
                const bool live = FactIsLive(words, fact);
                fact.misses = live ? 0 : fact.misses + 1;

                if (!IsInstalled_NoLock(fact.key))
                {
                    if (live)
                        sweep.push_back({ static_cast<std::uint8_t>(fact.slot), fact.key });
                    continue;
                }
                if (fact.misses >= kFactMissLimit || (!live && ++keptPerSlot[fact.slot] > kFactsKeptPerSlot))
                    continue;
                kept.push_back(fact);
            }
            std::reverse(kept.begin(), kept.end());
            g_Facts = kept;
            PersistFacts_NoLock();

            for (int band = 0; band < 2; ++band)
            {
                V_FrameWorkState::DispatchCursor& state = g_Cursors[band];
                std::uint16_t cursor = 0;
                if (!state.key.empty()
                    && (!IsInstalled_NoLock(state.key) || !ReadCursor(iface, band == 1, cursor)
                        || cursor != state.anchor))
                {
                    state = {};
                    V_FrameWorkState::SetDispatchCursor(band == 1, state);
                }
            }

            UpdatePendingRewards_NoLock();
            g_RewardRowsWritten = false;
            WriteRewardRows_NoLock();

            queued.swap(g_QueuedScriptMissions);
        }

        SweepUninstalled(iface, sweep);
        for (const std::string& key : queued)
            AddScriptMission(key.c_str());
    }

    void MarkCompleted(std::uint16_t id)
    {
        std::lock_guard<std::recursive_mutex> lock(g_Lock);
        EnsureLoaded_NoLock();
        const Mission* mission = MissionById_NoLock(id);
        if (!mission || mission->record[2] != kRarityNone)
            return;
        const auto row = g_Rows.find(mission->key);
        if (row == g_Rows.end() || row->second.cleared)
            return;
        V_FrameWorkState::DispatchMission updated = row->second;
        updated.cleared = true;
        SaveRow_NoLock(updated);
    }

    bool IsCleared(const char* key)
    {
        if (!IsValidKey(key))
            return false;
        std::lock_guard<std::recursive_mutex> lock(g_Lock);
        EnsureLoaded_NoLock();
        const auto row = g_Rows.find(key);
        return row != g_Rows.end() && row->second.cleared;
    }

    bool UnsetCleared(const char* key)
    {
        if (!IsValidKey(key))
            return false;
        std::lock_guard<std::recursive_mutex> lock(g_Lock);
        EnsureLoaded_NoLock();
        const auto row = g_Rows.find(key);
        if (row == g_Rows.end())
            return false;
        if (row->second.cleared)
        {
            V_FrameWorkState::DispatchMission updated = row->second;
            updated.cleared = false;
            SaveRow_NoLock(updated);
        }
        return true;
    }

    void ArrangeListRows(std::uint8_t* rows, std::size_t count)
    {
        if (!rows || count < 2)
            return;

        struct Row
        {
            std::size_t   index = 0;
            std::uint16_t id = 0;
            std::uint32_t mix = 0;
        };

        std::vector<Row> first;
        std::vector<Row> middle;
        std::vector<Row> scattered;
        std::vector<Row> last;
        {
            std::lock_guard<std::recursive_mutex> lock(g_Lock);
            EnsureLoaded_NoLock();
            for (std::size_t i = 0; i < count; ++i)
            {
                const std::uint8_t* info = rows + i * kMissionInfoSize;
                Row row;
                row.index = i;
                std::uint16_t seed = 0;
                if (!CopyGuarded(&row.id, info + kInfoMissionId, sizeof(row.id))
                    || !CopyGuarded(&seed, info + kInfoSeed, sizeof(seed)))
                    return;
                std::uint32_t mix = (static_cast<std::uint32_t>(row.id) << 16) ^ seed;
                mix ^= mix >> 16;
                mix *= 0x7FEB352Du;
                mix ^= mix >> 15;
                mix *= 0x846CA68Bu;
                mix ^= mix >> 16;
                row.mix = mix;

                const Mission* mission = IsCustomId(row.id) ? MissionById_NoLock(row.id) : nullptr;
                if (!mission)
                    middle.push_back(row);
                else if (mission->important)
                    first.push_back(row);
                else if (mission->revengeMissionId != 0)
                    last.push_back(row);
                else
                    scattered.push_back(row);
            }
        }
        if (first.empty() && scattered.empty() && last.empty())
            return;

        std::vector<Row> order = first;
        order.insert(order.end(), middle.begin(), middle.end());
        std::sort(scattered.begin(), scattered.end(), [](const Row& a, const Row& b)
            { return a.mix != b.mix ? a.mix < b.mix : a.id < b.id; });
        for (const Row& row : scattered)
        {
            const std::size_t lowest = first.size();
            const std::size_t highest = (std::max)(lowest, (std::min)(kRandomListRows - 1, order.size()));
            const std::size_t at = lowest + row.mix % (highest - lowest + 1);
            order.insert(order.begin() + static_cast<std::ptrdiff_t>(at), row);
        }
        order.insert(order.end(), last.begin(), last.end());

        bool moved = false;
        for (std::size_t i = 0; i < order.size() && !moved; ++i)
            moved = order[i].index != i;
        if (!moved)
            return;

        std::vector<std::uint8_t> copy(count * kMissionInfoSize);
        if (!CopyGuarded(copy.data(), rows, copy.size()))
            return;
        for (std::size_t i = 0; i < order.size(); ++i)
            CopyGuarded(rows + i * kMissionInfoSize, copy.data() + order[i].index * kMissionInfoSize,
                        kMissionInfoSize);
    }

    bool CountImportantAsStory(std::uint8_t* iface, NoticeCounts& saved)
    {
        std::uint32_t* words = SlotWords(iface);
        if (!words)
            return false;

        std::uint32_t important = 0;
        std::uint32_t inRevengeBand = 0;
        {
            std::lock_guard<std::recursive_mutex> lock(g_Lock);
            EnsureLoaded_NoLock();
            if (!g_Armed)
                return false;
            for (std::uint8_t slot = 0; slot < kSlotCount; ++slot)
            {
                if (slot >= kFirstLogSlot && slot <= kLastLogSlot)
                    continue;
                const V_FrameWorkState::DispatchSlotFact* fact = LiveFactInSlot_NoLock(words, slot);
                if (!fact)
                    continue;
                const auto mission = g_Missions.find(fact->key);
                std::uint32_t word = 0;
                if (mission == g_Missions.end() || !mission->second.important
                    || !ReadWord(words, slot, word) || IsSlotAccepted(word))
                    continue;
                ++important;
                if (slot >= kFirstRevengeSlot && slot <= kLastScriptSlot)
                    ++inRevengeBand;
            }
        }
        if (important == 0)
            return false;

        if (!CopyGuarded(&saved.storyListed, iface + kStoryListedOffset, sizeof(saved.storyListed))
            || !CopyGuarded(&saved.revengeListed, iface + kRevengeListedOffset, sizeof(saved.revengeListed)))
            return false;

        const std::uint8_t story = static_cast<std::uint8_t>((std::min)(0xFFu, saved.storyListed + important));
        const std::uint8_t revenge = static_cast<std::uint8_t>(
            saved.revengeListed > inRevengeBand ? saved.revengeListed - inRevengeBand : 0);
        CopyGuarded(iface + kStoryListedOffset, &story, sizeof(story));
        CopyGuarded(iface + kRevengeListedOffset, &revenge, sizeof(revenge));
        return true;
    }

    void RestoreNoticeCounts(std::uint8_t* iface, const NoticeCounts& saved)
    {
        CopyGuarded(iface + kStoryListedOffset, &saved.storyListed, sizeof(saved.storyListed));
        CopyGuarded(iface + kRevengeListedOffset, &saved.revengeListed, sizeof(saved.revengeListed));
    }

    void CallClear(std::uint8_t* iface, std::uint8_t slot)
    {
        if (const auto clear = VirtualAt<Clear_t>(iface, kVtClear))
            clear(iface, slot);
    }

    std::uint16_t CallCreateSeed(std::uint8_t* iface)
    {
        const auto createSeed = VirtualAt<CreateSeed_t>(iface, kVtCreateSeed);
        return createSeed ? createSeed(iface) : 0;
    }

    void CallGenerate(std::uint8_t* iface, std::uint8_t slot, std::uint16_t id,
                      std::uint16_t seed, std::uint8_t rank)
    {
        if (const auto generate = VirtualAt<Generate_t>(iface, kVtGenerate))
            generate(iface, slot, id, seed, rank);
    }

    void CallSetRevengeCleared(std::uint8_t* iface, std::uint16_t revengeId)
    {
        if (revengeId < kFirstRevengeId || revengeId > kLastRevengeId)
            return;
        if (const auto setCleared = VirtualAt<SetRevengeCleared_t>(iface, kVtSetRevengeCleared))
            setCleared(iface, revengeId, true);
    }
}
