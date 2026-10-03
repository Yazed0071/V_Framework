#include "pch.h"

#include <Windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "SoldierFaction.h"
#include "log.h"

namespace
{
    constexpr std::uint32_t kSoldierIndexMask = 0x1FFu;
    constexpr std::uint32_t kSoldierSlotCount = 0x200u;
    constexpr std::uint16_t kNoFaction        = 0u;
    constexpr std::uint16_t kNoRival          = 0u;
    constexpr std::uint16_t kNoMount          = 0u;
    constexpr std::uint16_t kNoMountInNode    = 0xFFFFu;
    constexpr std::uint32_t kGameObjectIdCount = 0x10000u;
    constexpr std::uint16_t kSoldierCategory  = 2u;

    constexpr std::uint8_t  kSightKindSoldier     = 2u;
    constexpr std::uint8_t  kNoticeTypeWalkerGear = 0x0Cu;
    constexpr std::uint8_t  kNoticeLevelSighted   = 1u;
    constexpr std::uint32_t kMaxSightEntries      = 64u;

    constexpr std::uint64_t kRivalLostMs     = 30000u;
    constexpr std::uint64_t kNodeWakeGraceMs = 2000u;
    constexpr std::uint64_t kNodeStalledMs   = 15000u;
    constexpr std::uint64_t kRefusalLogMs    = 3000u;

    constexpr std::uint64_t kGunfireHeardMs = 3000u;
    constexpr std::uint64_t kScanIntervalMs = 200u;
    constexpr std::uint64_t kNoticeRepushMs = 1000u;
    constexpr std::uint64_t kPerfWindowMs   = 10000u;

    constexpr float kFightRangeSq        = 80.0f * 80.0f;
    constexpr float kNoticeRadiusSq      = 20.0f * 20.0f;
    constexpr float kHearingRadiusSq     = 60.0f * 60.0f;
    constexpr float kAimHeight           = 1.2f;
    constexpr float kShotAtRivalRadiusSq = 1.5f * 1.5f;
    constexpr float kShotAtRivalHeight   = 2.5f;

    constexpr std::size_t kNoticeControllerSoldierCore = 0x58;
    constexpr std::size_t kNoticeControllerInterface   = 0x20;

    constexpr std::size_t kSoldierCoreNotice      = 0x68;
    constexpr std::size_t kGetNoticeInfoSlot      = 2;
    constexpr std::size_t kSoldierCoreCommandPost = 0xD0;
    constexpr std::size_t kCommandPostMembers     = 0x10;
    constexpr std::size_t kMemberStride           = 0x20;
    constexpr std::size_t kMemberState            = 0x1D;

    constexpr std::size_t kSoldierCorePositions = 0x18;
    constexpr std::size_t kPositionArray        = 0x20;
    constexpr std::size_t kPositionStride       = 0x10;

    constexpr std::size_t  kSoldierCoreLifeStates = 0x38;
    constexpr std::size_t  kLifeStateArray        = 0x08;
    constexpr std::size_t  kLifeStateStride       = 0x40;
    constexpr std::size_t  kLifeStateDeadByte     = 0x3E;
    constexpr std::size_t  kLifeStateDownByte     = 0x12;
    constexpr std::uint8_t kLifeStateDownMask     = 3u;

    constexpr std::size_t   kSoldierCoreRecords   = 0x110;
    constexpr std::size_t   kRecordArray          = 0x08;
    constexpr std::size_t   kRecordCount          = 0x10;
    constexpr std::size_t   kRecordStride         = 0x28;
    constexpr std::uint64_t kRecordInVehicle      = 1ull << 61;
    constexpr std::size_t   kSoldierCoreVehicles  = 0x48;
    constexpr std::size_t   kIsArmoredCrewSlot    = 0xE0 / sizeof(void*);
    constexpr std::uint8_t  kMemberStateNoCombat  = 6u;

    constexpr std::size_t kSoldierCoreSightLists = 0xB8;
    constexpr std::size_t kSightListCounts       = 0x08;
    constexpr std::size_t kSightListEntries      = 0x10;
    constexpr std::size_t kSightEntryStride      = 6;

    constexpr std::size_t   kBulletRequests      = 0x38;
    constexpr std::size_t   kBulletRequestStride = 0x40;
    constexpr std::size_t   kBulletRequestMask   = 0x28;
    constexpr std::size_t   kBulletRequestOwner  = 0x2A;
    constexpr std::uint16_t kGroupEnemy          = 0x0002u;

    constexpr std::size_t kSearchIds         = 0x00;
    constexpr std::size_t kSearchEntries     = 0x08;
    constexpr std::size_t kSearchCount       = 0x14;
    constexpr std::size_t kSearchEntryStride = 0x1C;

    constexpr std::size_t  kDamageStride           = 0x30;
    constexpr std::size_t  kDamageAttacker         = 0x22;
    constexpr std::size_t  kDamageFlags            = 0x26;
    constexpr std::size_t  kDamageAttackId         = 0x28;
    constexpr std::uint8_t kDamageFlagInstantKill  = 0x04u;

    constexpr std::size_t   kDamageHandlerCore    = 0x48;
    constexpr std::size_t   kLifeControllerCore   = 0x78;
    constexpr std::uint64_t kRecordHeldUp         = 1ull << 16;
    constexpr std::uint64_t kRecordOnWalkerGear   = 1ull << 48;
    constexpr std::uint8_t  kLifeStatusAlive      = 0u;
    constexpr std::uint8_t  kLifeStatusDead       = 2u;
    constexpr std::uint8_t  kLifeStatusUnreadable = 0xFFu;
    constexpr std::uint16_t kPlayerId             = 0u;
    constexpr std::uint16_t kUnknownAttacker      = 0xFFFFu;
    constexpr std::uint16_t kCauseStale           = 0xFFFFu;
    constexpr std::uint16_t kTaintHoldUp          = 0xFFFEu;
    constexpr std::uint64_t kTaintSet             = 1ull << 32;
    constexpr std::uint64_t kCauseFreshMs         = 5000u;

    constexpr std::uint16_t kCorpseCategory        = 0x26u;
    constexpr std::size_t   kSoldierCoreAttributes = 0x120;
    constexpr std::size_t   kAttributeArray        = 0x08;
    constexpr std::size_t   kAttributeStride       = 0x20;
    constexpr std::size_t   kAttributeFlags        = 0x1E;
    constexpr std::uint16_t kAttributeNeutralized  = 0x0414u;

    constexpr std::size_t   kBodyTableIds      = 0x5C;
    constexpr std::size_t   kBodyTableCount    = 0x62;
    constexpr std::uint32_t kBodySlotCount     = 3u;
    constexpr std::uint16_t kNoBody            = 0xFFFFu;
    constexpr std::uint8_t  kSightKindHidden   = 0xFFu;
    constexpr std::uint32_t kMaxHiddenSight    = 64u;

    constexpr std::size_t   kNoticeControllerNoise = 0x60;
    constexpr std::size_t   kNoiseSlotStride       = 0x80;
    constexpr std::size_t   kNoiseSlotCurrent      = 0x18;
    constexpr std::size_t   kNoiseKind             = 0x10;
    constexpr std::size_t   kNoiseSource           = 0x12;
    constexpr std::uint8_t  kNoiseKindCollapse     = 0x1Eu;
    constexpr std::uint64_t kRecordHearsCollapse   = 1ull << 39;

    constexpr std::size_t  kNoticeInterfaceSoldierCore = 0x38;
    constexpr std::size_t  kNoticeTarget               = 0x02;
    constexpr std::uint8_t kNoticeBodyFound            = 0x1Eu;
    constexpr std::uint8_t kNoticeBodyKnown            = 0x2Eu;

    constexpr std::size_t  kBodyNodeSoldierCore     = 0x50;
    constexpr std::size_t  kDownComradeBody         = 0x26;
    constexpr std::size_t  kDownComradeInTable      = 0x2E;
    constexpr std::uint8_t kDownComradeInTableBit   = 0x01u;
    constexpr std::size_t  kVehicleDownComradeBody  = 0x14;

    constexpr std::uint64_t kCorpseOwnerMessage    = 0xC201DDCB7E2Dull;
    constexpr std::size_t   kQuarkGameObjects      = 0x60;
    constexpr std::size_t   kSendMessageSlot       = 0x38 / sizeof(void*);
    constexpr std::size_t   kMessageSize           = 0x40;
    constexpr std::size_t   kMessageResult         = 0x08;
    constexpr std::uint64_t kCorpseOwnerRefreshMs  = 5000u;
    constexpr std::uint64_t kIgnoredLogMs          = 10000u;

    struct SightEntry
    {
        std::uint16_t gameObjectId;
        std::uint8_t  tier;
        std::uint8_t  kind;
    };

    struct SoldierPoint
    {
        float x;
        float y;
        float z;
    };

    struct ShotScope
    {
        bool          active;
        std::uint32_t shooterIndex;
        std::uint32_t rivalIndex;
    };

    thread_local ShotScope t_shotScope{};

    struct IdleSnapshot
    {
        std::uint8_t notice;
        std::uint8_t lifeFlags;
        std::uint8_t lifeDead;
        std::uint8_t duty;
    };

    using AddNoticeInfo_t = bool(__fastcall*)(void* noticeInterface, std::uint32_t soldierIndex, const void* notice);
    using GetNoticeInfo_t = const std::uint8_t*(__fastcall*)(void* noticeInterface, std::uint32_t soldierIndex);
    using IsArmoredCrew_t = bool(__fastcall*)(void* vehicleInterface, std::uint32_t soldierIndex);
    using GetQuarkSystemTable_t = std::uint8_t*(__fastcall*)();
    using SendGameObjectMessage_t = const std::uint32_t*(__fastcall*)(void* gameObjects, std::uint32_t* result,
                                                                      std::uint32_t gameObjectId, void* message);

    std::mutex               g_namesMtx;
    std::vector<std::string> g_factionNames;

    std::array<std::atomic<std::uint16_t>, kSoldierSlotCount> g_faction{};
    std::array<std::atomic<std::uint16_t>, kSoldierSlotCount> g_rival{};
    std::array<std::atomic<std::uint16_t>, kSoldierSlotCount> g_pendingRival{};
    std::array<std::atomic<std::uint64_t>, kSoldierSlotCount> g_rivalSeenMs{};
    std::array<std::atomic<std::uint64_t>, kSoldierSlotCount> g_rivalAliveMs{};
    std::array<std::atomic<bool>, kSoldierSlotCount>          g_rivalNodeWoke{};
    std::array<std::atomic<std::uint64_t>, kSoldierSlotCount> g_lastRefusalLogMs{};
    std::array<std::atomic<std::uint64_t>, kSoldierSlotCount> g_lastShotMs{};
    std::array<std::atomic<std::uint64_t>, kSoldierSlotCount> g_nextScanMs{};
    std::array<std::atomic<std::uint64_t>, kSoldierSlotCount> g_nextRepushMs{};
    std::array<std::atomic<std::uint16_t>, kSoldierSlotCount> g_mount{};
    std::array<std::atomic<std::uint16_t>, kSoldierSlotCount> g_loggedMountShot{};
    std::array<std::atomic<std::uint16_t>, kGameObjectIdCount> g_mountRider{};
    std::array<std::atomic<std::uint16_t>, kSoldierSlotCount> g_neutralizedBy{};
    std::array<std::atomic<std::uint64_t>, kSoldierSlotCount> g_rivalHitMs{};
    std::array<std::atomic<std::uint64_t>, kSoldierSlotCount> g_taint{};
    std::array<std::atomic<std::uint64_t>, kSoldierSlotCount> g_downMs{};
    std::array<std::atomic<std::uint16_t>, kSoldierSlotCount> g_corpseOwner{};
    std::array<std::atomic<std::uint64_t>, kSoldierSlotCount> g_corpseOwnerMs{};
    std::array<std::atomic<std::uint16_t>, kSoldierSlotCount> g_lastIgnored{};
    std::array<std::atomic<std::uint64_t>, kSoldierSlotCount> g_lastIgnoredLogMs{};
    thread_local std::uint32_t                                t_ignoredNotices = 0;
    std::atomic<bool>                                         g_anyFaction{ false };

    std::atomic<std::uint64_t> g_perfWindowStartMs{ 0 };
    std::atomic<std::uint32_t> g_perfChecks{ 0 };
    std::atomic<std::uint32_t> g_perfScans{ 0 };
    std::atomic<std::int64_t>  g_perfTicks{ 0 };
    std::atomic<std::int64_t>  g_perfLastCallTicks{ 0 };
    std::atomic<std::int64_t>  g_perfMaxGapTicks{ 0 };

    bool IsSoldierId(std::uint16_t gameObjectId)
    {
        return (gameObjectId >> 9) == kSoldierCategory;
    }

    std::uint16_t RiderOrSelf(std::uint16_t gameObjectId)
    {
        if (IsSoldierId(gameObjectId))
            return gameObjectId;
        const std::uint16_t rider = g_mountRider[gameObjectId].load(std::memory_order_relaxed);
        return rider != kNoMount ? rider : gameObjectId;
    }

    void ClearMount(std::uint32_t soldierIndex)
    {
        const std::uint16_t mount = g_mount[soldierIndex].exchange(kNoMount, std::memory_order_relaxed);
        if (mount == kNoMount)
            return;
        std::uint16_t rider = static_cast<std::uint16_t>(0x400u | soldierIndex);
        g_mountRider[mount].compare_exchange_strong(rider, kNoMount, std::memory_order_relaxed);
    }

    std::uint16_t FactionOf(std::uint32_t soldierIndex)
    {
        return g_faction[soldierIndex & kSoldierIndexMask].load(std::memory_order_relaxed);
    }

    std::uint16_t InternFactionName(const std::string& name)
    {
        std::lock_guard<std::mutex> lk(g_namesMtx);
        for (std::size_t i = 0; i < g_factionNames.size(); ++i)
        {
            if (g_factionNames[i] == name)
                return static_cast<std::uint16_t>(i + 1);
        }
        g_factionNames.push_back(name);
        return static_cast<std::uint16_t>(g_factionNames.size());
    }

    std::string FactionNameOf(std::uint16_t factionId)
    {
        std::lock_guard<std::mutex> lk(g_namesMtx);
        if (factionId == kNoFaction || factionId > g_factionNames.size())
            return "none";
        return g_factionNames[factionId - 1];
    }

    bool AreHostile(std::uint32_t soldierIndex, std::uint32_t otherIndex)
    {
        const std::uint16_t mine   = FactionOf(soldierIndex);
        const std::uint16_t theirs = FactionOf(otherIndex);
        return mine != kNoFaction && theirs != kNoFaction && mine != theirs;
    }

    bool ReadSoldierPoint(const void* soldierCore, std::uint32_t soldierIndex, SoldierPoint* out)
    {
        __try
        {
            const std::uint8_t* core      = static_cast<const std::uint8_t*>(soldierCore);
            const std::uint8_t* positions = *reinterpret_cast<const std::uint8_t* const*>(core + kSoldierCorePositions);
            if (!positions)
                return false;
            const std::uint8_t* pointArray = *reinterpret_cast<const std::uint8_t* const*>(positions + kPositionArray);
            if (!pointArray)
                return false;
            const float* point = reinterpret_cast<const float*>(pointArray + soldierIndex * kPositionStride);
            out->x = point[0];
            out->y = point[1];
            out->z = point[2];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool IsSoldierStanding(const void* soldierCore, std::uint32_t soldierIndex)
    {
        __try
        {
            const std::uint8_t* core       = static_cast<const std::uint8_t*>(soldierCore);
            const std::uint8_t* lifeStates = *reinterpret_cast<const std::uint8_t* const*>(core + kSoldierCoreLifeStates);
            if (!lifeStates)
                return false;
            const std::uint8_t* records = *reinterpret_cast<const std::uint8_t* const*>(lifeStates + kLifeStateArray);
            if (!records)
                return false;
            const std::uint8_t* record = records + soldierIndex * kLifeStateStride;
            if (record[kLifeStateDeadByte] != 0)
                return false;
            return (record[kLifeStateDownByte] & kLifeStateDownMask) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    const char* WhyCombatNodeRefusesSEH(const void* soldierCore, std::uint32_t soldierIndex)
    {
        __try
        {
            const std::uint8_t* core    = static_cast<const std::uint8_t*>(soldierCore);
            const std::uint8_t* records = *reinterpret_cast<const std::uint8_t* const*>(core + kSoldierCoreRecords);
            if (records)
            {
                const std::uint8_t* recordArray = *reinterpret_cast<const std::uint8_t* const*>(records + kRecordArray);
                const std::uint32_t recordCount = *reinterpret_cast<const std::uint32_t*>(records + kRecordCount);
                if (recordArray && soldierIndex < recordCount)
                {
                    const std::uint64_t flags = *reinterpret_cast<const std::uint64_t*>(recordArray + soldierIndex * kRecordStride);
                    void* vehicles = *reinterpret_cast<void* const*>(core + kSoldierCoreVehicles);
                    if ((flags & kRecordInVehicle) != 0 && vehicles)
                    {
                        IsArmoredCrew_t isArmoredCrew = (*reinterpret_cast<IsArmoredCrew_t* const*>(vehicles))[kIsArmoredCrewSlot];
                        if (isArmoredCrew(vehicles, soldierIndex))
                            return "soldier is crewing an armored vehicle";
                    }
                }
            }

            const std::uint8_t* commandPost = *reinterpret_cast<const std::uint8_t* const*>(core + kSoldierCoreCommandPost);
            const std::uint8_t* members = commandPost
                ? *reinterpret_cast<const std::uint8_t* const*>(commandPost + kCommandPostMembers) : nullptr;
            if (members && members[soldierIndex * kMemberStride + kMemberState] == kMemberStateNoCombat)
                return "combat node refuses post state 6";
            return nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    const char* WhyCannotFight(const void* soldierCore, std::uint32_t soldierIndex)
    {
        if (!IsSoldierStanding(soldierCore, soldierIndex))
            return "soldier is down";
        return WhyCombatNodeRefusesSEH(soldierCore, soldierIndex);
    }

    std::uint32_t ReadSightEntries(const void* soldierCore, std::uint32_t soldierIndex, SightEntry* out)
    {
        __try
        {
            const std::uint8_t* core  = static_cast<const std::uint8_t*>(soldierCore);
            const std::uint8_t* lists = *reinterpret_cast<const std::uint8_t* const*>(core + kSoldierCoreSightLists);
            if (!lists)
                return 0;
            const std::uint32_t* counts = *reinterpret_cast<const std::uint32_t* const*>(lists + kSightListCounts);
            const std::uint8_t* const* entryLists = *reinterpret_cast<const std::uint8_t* const* const*>(lists + kSightListEntries);
            if (!counts || !entryLists)
                return 0;
            const std::uint8_t* entries = entryLists[soldierIndex];
            std::uint32_t count = counts[soldierIndex];
            if (!entries)
                return 0;
            if (count > kMaxSightEntries)
                count = kMaxSightEntries;
            for (std::uint32_t i = 0; i < count; ++i)
            {
                const std::uint8_t* entry = entries + i * kSightEntryStride;
                out[i].gameObjectId = *reinterpret_cast<const std::uint16_t*>(entry);
                out[i].tier         = entry[2];
                out[i].kind         = entry[4];
            }
            return count;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    std::uint32_t FilterSoldierSearchSEH(std::uint8_t* result, std::uint32_t count,
                                         std::uint32_t shooterIndex, std::uint32_t rivalIndex)
    {
        __try
        {
            std::uint16_t* ids = *reinterpret_cast<std::uint16_t**>(result + kSearchIds);
            std::uint8_t* entries = *reinterpret_cast<std::uint8_t**>(result + kSearchEntries);
            if (!ids || !entries)
                return count;

            std::uint32_t kept = 0;
            for (std::uint32_t i = 0; i < count; ++i)
            {
                const std::uint32_t otherIndex = ids[i] & kSoldierIndexMask;
                const bool rivalSide = otherIndex != shooterIndex
                    && (otherIndex == rivalIndex || AreHostile(shooterIndex, otherIndex));
                if (rivalSide)
                    continue;
                if (kept != i)
                {
                    ids[kept] = ids[i];
                    std::memmove(entries + kept * kSearchEntryStride, entries + i * kSearchEntryStride, kSearchEntryStride);
                }
                ++kept;
            }
            *reinterpret_cast<std::uint32_t*>(result + kSearchCount) = kept;
            return kept;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return count;
        }
    }

    bool ReadIdleSnapshot(const void* soldierCore, std::uint32_t soldierIndex, IdleSnapshot* out)
    {
        __try
        {
            const std::uint8_t* core = static_cast<const std::uint8_t*>(soldierCore);

            void* noticeInterface = *reinterpret_cast<void* const*>(core + kSoldierCoreNotice);
            GetNoticeInfo_t getNoticeInfo = (*reinterpret_cast<GetNoticeInfo_t* const*>(noticeInterface))[kGetNoticeInfoSlot];
            const std::uint8_t* notice = getNoticeInfo(noticeInterface, soldierIndex);
            out->notice = notice ? notice[0] : 0xFFu;

            const std::uint8_t* lifeStates = *reinterpret_cast<const std::uint8_t* const*>(core + kSoldierCoreLifeStates);
            const std::uint8_t* records = *reinterpret_cast<const std::uint8_t* const*>(lifeStates + kLifeStateArray);
            const std::uint8_t* record = records + soldierIndex * kLifeStateStride;
            out->lifeFlags = record[kLifeStateDownByte];
            out->lifeDead  = record[kLifeStateDeadByte];

            const std::uint8_t* commandPost = *reinterpret_cast<const std::uint8_t* const*>(core + kSoldierCoreCommandPost);
            const std::uint8_t* members = *reinterpret_cast<const std::uint8_t* const*>(commandPost + kCommandPostMembers);
            out->duty = members[soldierIndex * kMemberStride + kMemberState];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool PushWalkerGearNotice(void* noticeController, std::uint32_t soldierIndex, std::uint16_t rivalId)
    {
        alignas(16) std::uint8_t notice[16] = {};
        notice[0] = kNoticeTypeWalkerGear;
        notice[1] = kNoticeLevelSighted;
        std::memcpy(notice + 2, &rivalId, sizeof(rivalId));

        __try
        {
            void* noticeInterface = static_cast<std::uint8_t*>(noticeController) + kNoticeControllerInterface;
            AddNoticeInfo_t addNoticeInfo = (*reinterpret_cast<AddNoticeInfo_t* const*>(noticeInterface))[0];
            return addNoticeInfo(noticeInterface, soldierIndex, notice);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool WidenFactionBulletSEH(void* bullet3, std::uint32_t requestIndex, std::uint16_t* ownerOut)
    {
        __try
        {
            std::uint8_t* requests = *reinterpret_cast<std::uint8_t**>(static_cast<std::uint8_t*>(bullet3) + kBulletRequests);
            if (!requests)
                return false;
            std::uint8_t* request = requests + requestIndex * kBulletRequestStride;
            const std::uint16_t owner = *reinterpret_cast<const std::uint16_t*>(request + kBulletRequestOwner);
            const std::uint16_t shooter = RiderOrSelf(owner);
            if (!IsSoldierId(shooter) || FactionOf(shooter) == kNoFaction)
                return false;
            std::uint16_t* shooterGroups = reinterpret_cast<std::uint16_t*>(request + kBulletRequestMask);
            *shooterGroups = static_cast<std::uint16_t>(*shooterGroups & ~kGroupEnemy);
            *ownerOut = owner;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void LogMountShotOnce(std::uint16_t owner, std::uint16_t shooter)
    {
        const std::uint32_t rider = shooter & kSoldierIndexMask;
        const std::uint16_t mount = owner == shooter ? g_mount[rider].load(std::memory_order_relaxed) : owner;
        if (mount == kNoMount || g_loggedMountShot[rider].exchange(mount, std::memory_order_relaxed) == mount)
            return;
        if (owner == shooter)
            LogInfo("SoldierFaction: soldier %u fired from walker gear or vehicle 0x%04X, shots count as the soldier's",
                rider, mount);
        else
            LogInfo("SoldierFaction: walker gear or vehicle 0x%04X fired for soldier %u, shots now hit rivals",
                mount, rider);
    }

    std::int64_t PerfNow()
    {
        LARGE_INTEGER ticks;
        QueryPerformanceCounter(&ticks);
        return ticks.QuadPart;
    }

    double PerfTicksPerMs()
    {
        static const double ticksPerMs = [] {
            LARGE_INTEGER frequency;
            QueryPerformanceFrequency(&frequency);
            return static_cast<double>(frequency.QuadPart) / 1000.0;
        }();
        return ticksPerMs;
    }

    void NotePerfGap(std::int64_t start)
    {
        const std::int64_t last = g_perfLastCallTicks.exchange(start, std::memory_order_relaxed);
        if (last == 0 || start <= last)
            return;
        const std::int64_t gap = start - last;
        std::int64_t longest = g_perfMaxGapTicks.load(std::memory_order_relaxed);
        while (gap > longest && !g_perfMaxGapTicks.compare_exchange_weak(longest, gap, std::memory_order_relaxed))
        {
        }
    }

    void FlushPerfWindow(std::uint64_t now)
    {
        std::uint64_t windowStart = g_perfWindowStartMs.load(std::memory_order_relaxed);
        if (windowStart == 0)
        {
            g_perfWindowStartMs.compare_exchange_strong(windowStart, now, std::memory_order_relaxed);
            return;
        }
        if (now - windowStart < kPerfWindowMs)
            return;
        if (!g_perfWindowStartMs.compare_exchange_strong(windowStart, now, std::memory_order_relaxed))
            return;

        const std::uint32_t checks = g_perfChecks.exchange(0, std::memory_order_relaxed);
        const std::uint32_t scans  = g_perfScans.exchange(0, std::memory_order_relaxed);
        const std::int64_t  ticks  = g_perfTicks.exchange(0, std::memory_order_relaxed);
        const std::int64_t  gap    = g_perfMaxGapTicks.exchange(0, std::memory_order_relaxed);
        LogInfo("SoldierFaction: last %llu s: %u notice checks, %u rival scans, %.2f ms in faction code, slowest frame %.0f ms",
            static_cast<unsigned long long>((now - windowStart) / 1000u), checks, scans,
            static_cast<double>(ticks) / PerfTicksPerMs(), static_cast<double>(gap) / PerfTicksPerMs());
    }

    float DistanceSq(const SoldierPoint& a, const SoldierPoint& b)
    {
        const float dx = a.x - b.x;
        const float dy = a.y - b.y;
        const float dz = a.z - b.z;
        return dx * dx + dy * dy + dz * dz;
    }

    bool RivalInFightRange(const void* soldierCore, std::uint32_t soldierIndex, std::uint32_t rivalIndex)
    {
        SoldierPoint self{};
        SoldierPoint rival{};
        if (!ReadSoldierPoint(soldierCore, soldierIndex, &self) || !ReadSoldierPoint(soldierCore, rivalIndex, &rival))
            return false;
        return DistanceSq(self, rival) <= kFightRangeSq;
    }

    std::uint16_t PickRival(const void* soldierCore, std::uint32_t soldierIndex,
                            const SightEntry* entries, std::uint32_t count)
    {
        SoldierPoint self{};
        if (!ReadSoldierPoint(soldierCore, soldierIndex, &self))
            return kNoRival;

        std::uint16_t best       = kNoRival;
        std::uint8_t  bestTier   = 0xFFu;
        float         bestDistSq = 0.0f;

        for (std::uint32_t i = 0; i < count; ++i)
        {
            const SightEntry& entry = entries[i];
            if (entry.kind != kSightKindSoldier || !IsSoldierId(entry.gameObjectId))
                continue;

            const std::uint32_t otherIndex = entry.gameObjectId & kSoldierIndexMask;
            if (otherIndex == soldierIndex || !AreHostile(soldierIndex, otherIndex))
                continue;
            if (!IsSoldierStanding(soldierCore, otherIndex))
                continue;

            SoldierPoint other{};
            if (!ReadSoldierPoint(soldierCore, otherIndex, &other))
                continue;

            const float distSq = DistanceSq(self, other);
            const bool closerTier = entry.tier < bestTier;
            const bool sameTierNearer = entry.tier == bestTier && distSq < bestDistSq;
            if (best == kNoRival || closerTier || sameTierNearer)
            {
                best       = entry.gameObjectId;
                bestTier   = entry.tier;
                bestDistSq = distSq;
            }
        }
        return best;
    }

    bool FiredRecently(std::uint32_t soldierIndex, std::uint64_t now)
    {
        const std::uint64_t shotMs = g_lastShotMs[soldierIndex].load(std::memory_order_relaxed);
        return shotMs != 0 && now - shotMs <= kGunfireHeardMs;
    }

    bool CanSenseRival(std::uint32_t soldierIndex, std::uint32_t rivalIndex, float distSq, std::uint64_t now)
    {
        if (distSq <= kNoticeRadiusSq)
            return true;
        return distSq <= kHearingRadiusSq && FiredRecently(rivalIndex, now);
    }

    std::uint16_t PickSensedRival(const void* soldierCore, std::uint32_t soldierIndex, std::uint64_t now)
    {
        SoldierPoint self{};
        if (!ReadSoldierPoint(soldierCore, soldierIndex, &self))
            return kNoRival;

        std::uint16_t best       = kNoRival;
        float         bestDistSq = 0.0f;

        for (std::uint32_t otherIndex = 0; otherIndex < kSoldierSlotCount; ++otherIndex)
        {
            if (otherIndex == soldierIndex || !AreHostile(soldierIndex, otherIndex))
                continue;

            SoldierPoint other{};
            if (!ReadSoldierPoint(soldierCore, otherIndex, &other))
                continue;

            const float distSq = DistanceSq(self, other);
            if (!CanSenseRival(soldierIndex, otherIndex, distSq, now))
                continue;
            if (best != kNoRival && distSq >= bestDistSq)
                continue;
            if (!IsSoldierStanding(soldierCore, otherIndex))
                continue;

            best       = static_cast<std::uint16_t>(0x400u | otherIndex);
            bestDistSq = distSq;
        }
        return best;
    }

    bool RivalSensed(const void* soldierCore, std::uint32_t soldierIndex, std::uint32_t rivalIndex, std::uint64_t now)
    {
        SoldierPoint self{};
        SoldierPoint rival{};
        if (!ReadSoldierPoint(soldierCore, soldierIndex, &self) || !ReadSoldierPoint(soldierCore, rivalIndex, &rival))
            return false;
        return CanSenseRival(soldierIndex, rivalIndex, DistanceSq(self, rival), now);
    }

    std::uint16_t TakePendingRival(const void* soldierCore, std::uint32_t soldierIndex)
    {
        const std::uint16_t pending = g_pendingRival[soldierIndex].exchange(kNoRival, std::memory_order_relaxed);
        if (pending == kNoRival)
            return kNoRival;

        const std::uint32_t pendingIndex = pending & kSoldierIndexMask;
        if (!AreHostile(soldierIndex, pendingIndex) || !IsSoldierStanding(soldierCore, pendingIndex))
            return kNoRival;
        if (!RivalInFightRange(soldierCore, soldierIndex, pendingIndex))
            return kNoRival;
        return pending;
    }

    void AskRivalToFightBack(std::uint32_t rivalIndex, std::uint16_t attackerId)
    {
        if (g_rival[rivalIndex].load(std::memory_order_relaxed) != kNoRival)
            return;
        std::uint16_t expected = kNoRival;
        g_pendingRival[rivalIndex].compare_exchange_strong(expected, attackerId, std::memory_order_relaxed);
    }

    bool RivalInSight(std::uint16_t rivalId, const SightEntry* entries, std::uint32_t count)
    {
        for (std::uint32_t i = 0; i < count; ++i)
        {
            if (entries[i].kind == kSightKindSoldier && entries[i].gameObjectId == rivalId)
                return true;
        }
        return false;
    }

    void LogRefusal(std::uint32_t soldierIndex, std::uint16_t rival, std::uint64_t now)
    {
        const std::uint64_t last = g_lastRefusalLogMs[soldierIndex].load(std::memory_order_relaxed);
        if (last != 0 && now - last < kRefusalLogMs)
            return;
        g_lastRefusalLogMs[soldierIndex].store(now, std::memory_order_relaxed);
        LogInfo("SoldierFaction: soldier %u sees rival %u but AddNoticeInfo refused the fight notice",
            soldierIndex, rival & kSoldierIndexMask);
    }

    bool IsNeutralizedSEH(const void* soldierCore, std::uint32_t soldierIndex)
    {
        __try
        {
            const std::uint8_t* core       = static_cast<const std::uint8_t*>(soldierCore);
            const std::uint8_t* lifeStates = *reinterpret_cast<const std::uint8_t* const*>(core + kSoldierCoreLifeStates);
            const std::uint8_t* lifeRecords = lifeStates
                ? *reinterpret_cast<const std::uint8_t* const*>(lifeStates + kLifeStateArray) : nullptr;
            if (lifeRecords)
            {
                const std::uint8_t* life = lifeRecords + soldierIndex * kLifeStateStride;
                if (life[kLifeStateDeadByte] != 0 || (life[kLifeStateDownByte] & kLifeStateDownMask) != 0)
                    return true;
            }
            const std::uint8_t* attributes = *reinterpret_cast<const std::uint8_t* const*>(core + kSoldierCoreAttributes);
            const std::uint8_t* attributeArray = attributes
                ? *reinterpret_cast<const std::uint8_t* const*>(attributes + kAttributeArray) : nullptr;
            if (!attributeArray)
                return false;
            std::uint16_t flags = 0;
            std::memcpy(&flags, attributeArray + soldierIndex * kAttributeStride + kAttributeFlags, sizeof(flags));
            return (flags & kAttributeNeutralized) != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint16_t QueryCorpseOwnerSEH(GetQuarkSystemTable_t getQuarkSystemTable, std::uint16_t corpseId)
    {
        __try
        {
            std::uint8_t* quarkSystemTable = getQuarkSystemTable();
            if (!quarkSystemTable)
                return kNoBody;
            void* gameObjects = *reinterpret_cast<void* const*>(quarkSystemTable + kQuarkGameObjects);
            if (!gameObjects)
                return kNoBody;
            alignas(16) std::uint8_t message[kMessageSize] = {};
            std::memcpy(message, &kCorpseOwnerMessage, sizeof(kCorpseOwnerMessage));
            std::memcpy(message + kMessageResult, &kNoBody, sizeof(kNoBody));
            std::uint32_t result = 0;
            SendGameObjectMessage_t send = (*reinterpret_cast<SendGameObjectMessage_t* const*>(gameObjects))[kSendMessageSlot];
            const std::uint32_t* status = send(gameObjects, &result, corpseId, message);
            if (!status || (*status >> 31) != 0)
                return kNoBody;
            std::uint16_t owner = kNoBody;
            std::memcpy(&owner, message + kMessageResult, sizeof(owner));
            return owner;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return kNoBody;
        }
    }

    std::uint16_t ResolveBodySoldier(std::uint16_t bodyId)
    {
        if (IsSoldierId(bodyId))
            return bodyId;
        if ((bodyId >> 9) != kCorpseCategory)
            return kNoBody;

        const std::uint32_t slot = bodyId & kSoldierIndexMask;
        const std::uint64_t now = GetTickCount64();
        const std::uint64_t cachedMs = g_corpseOwnerMs[slot].load(std::memory_order_relaxed);
        if (cachedMs != 0 && now - cachedMs < kCorpseOwnerRefreshMs)
            return g_corpseOwner[slot].load(std::memory_order_relaxed);

        static const GetQuarkSystemTable_t getQuarkSystemTable =
            reinterpret_cast<GetQuarkSystemTable_t>(ResolveGameAddress(gAddr.GetQuarkSystemTable));
        std::uint16_t owner = getQuarkSystemTable ? QueryCorpseOwnerSEH(getQuarkSystemTable, bodyId) : kNoBody;
        if (!IsSoldierId(owner))
            owner = kNoBody;
        g_corpseOwner[slot].store(owner, std::memory_order_relaxed);
        g_corpseOwnerMs[slot].store(now, std::memory_order_relaxed);
        return owner;
    }

    std::uint8_t ReadLifeStatusSEH(const void* soldierCore, std::uint32_t soldierIndex)
    {
        __try
        {
            const std::uint8_t* core       = static_cast<const std::uint8_t*>(soldierCore);
            const std::uint8_t* lifeStates = *reinterpret_cast<const std::uint8_t* const*>(core + kSoldierCoreLifeStates);
            const std::uint8_t* records    = *reinterpret_cast<const std::uint8_t* const*>(lifeStates + kLifeStateArray);
            return records[soldierIndex * kLifeStateStride + kLifeStateDeadByte];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return kLifeStatusUnreadable;
        }
    }

    std::uint8_t ReadLifeStatusByteSEH(const void* life)
    {
        __try
        {
            return static_cast<const std::uint8_t*>(life)[kLifeStateDeadByte];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return kLifeStatusUnreadable;
        }
    }

    std::uint64_t ReadRecordSEH(const void* soldierCore, std::uint32_t soldierIndex)
    {
        __try
        {
            const std::uint8_t* core    = static_cast<const std::uint8_t*>(soldierCore);
            const std::uint8_t* records = *reinterpret_cast<const std::uint8_t* const*>(core + kSoldierCoreRecords);
            if (!records || soldierIndex >= *reinterpret_cast<const std::uint32_t*>(records + kRecordCount))
                return 0;
            const std::uint8_t* recordArray = *reinterpret_cast<const std::uint8_t* const*>(records + kRecordArray);
            return *reinterpret_cast<const std::uint64_t*>(recordArray + soldierIndex * kRecordStride);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    const void* ReadCorePointerSEH(const void* owner, std::size_t offset)
    {
        __try
        {
            return *reinterpret_cast<const void* const*>(static_cast<const std::uint8_t*>(owner) + offset);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    bool IsHeldUp(const void* soldierCore, std::uint32_t soldierIndex)
    {
        return (ReadRecordSEH(soldierCore, soldierIndex) & kRecordHeldUp) != 0;
    }

    bool IsPlayerOnlyAttack(std::uint16_t attackId)
    {
        return attackId == 0xC8u || attackId == 0xD3u;
    }

    bool IsConsequenceAttack(std::uint16_t attackId)
    {
        switch (attackId)
        {
        case 0x3Au: case 0x3Bu: case 0x4Bu: case 0x4Cu: case 0x4Du: case 0x4Eu: case 0x4Fu: case 0x67u:
            return true;
        default:
            return false;
        }
    }

    bool IsSoldierHarmlessAttack(std::uint16_t attackId)
    {
        switch (attackId)
        {
        case 0x01u: case 0x30u: case 0x31u: case 0x33u: case 0x34u: case 0x65u: case 0x66u:
            return true;
        default:
            return false;
        }
    }

    bool IsVehiclePhysicsAttack(std::uint16_t attackId)
    {
        return (attackId >= 0x6Fu && attackId <= 0x77u) || attackId == 0x7Bu || attackId == 0x7Cu;
    }

    std::uint16_t AttributeAttacker(const void* soldierCore, std::uint16_t attacker, std::uint16_t attackId)
    {
        if (IsSoldierId(attacker))
            return attacker;
        const std::uint16_t rider = g_mountRider[attacker].load(std::memory_order_relaxed);
        if (rider == kNoMount || IsVehiclePhysicsAttack(attackId) || !soldierCore)
            return attacker;
        const std::uint32_t riderIndex = rider & kSoldierIndexMask;
        if (ReadLifeStatusSEH(soldierCore, riderIndex) != kLifeStatusAlive)
            return attacker;
        const std::uint64_t record = ReadRecordSEH(soldierCore, riderIndex);
        if ((record & (kRecordInVehicle | kRecordOnWalkerGear)) == 0 || (record & kRecordHeldUp) != 0)
            return attacker;
        return rider;
    }

    void ResetNeutralization(std::uint32_t soldierIndex)
    {
        g_downMs[soldierIndex].store(0, std::memory_order_release);
        g_neutralizedBy[soldierIndex].store(kNoRival, std::memory_order_relaxed);
        g_rivalHitMs[soldierIndex].store(0, std::memory_order_relaxed);
        g_taint[soldierIndex].store(0, std::memory_order_relaxed);
    }

    bool RecordSaysFactionCaused(std::uint32_t soldierIndex)
    {
        if (g_downMs[soldierIndex].load(std::memory_order_acquire) == 0 || g_taint[soldierIndex].load(std::memory_order_relaxed) != 0)
            return false;
        const std::uint16_t causer = g_neutralizedBy[soldierIndex].load(std::memory_order_relaxed);
        return IsSoldierId(causer) && AreHostile(soldierIndex, causer & kSoldierIndexMask);
    }

    void DescribeTaint(std::uint64_t taint, char* out, std::size_t size)
    {
        const std::uint16_t who      = static_cast<std::uint16_t>(taint & 0xFFFFu);
        const std::uint16_t attackId = static_cast<std::uint16_t>((taint >> 16) & 0xFFFFu);
        if (who == kPlayerId)
            std::snprintf(out, size, "the player (attack 0x%X)", attackId);
        else if (who == kTaintHoldUp)
            std::snprintf(out, size, "a hold-up");
        else if (who == kUnknownAttacker)
            std::snprintf(out, size, "an unknown source (attack 0x%X)", attackId);
        else if (IsSoldierId(who))
            std::snprintf(out, size, "soldier %u (attack 0x%X)", who & kSoldierIndexMask, attackId);
        else
            std::snprintf(out, size, "0x%04X (attack 0x%X)", who, attackId);
    }

    void TaintNeutralization(std::uint32_t soldierIndex, std::uint16_t who, std::uint16_t attackId, bool down)
    {
        std::uint64_t expected = 0;
        const std::uint64_t taint = kTaintSet | (static_cast<std::uint64_t>(attackId) << 16) | who;
        if (!g_taint[soldierIndex].compare_exchange_strong(expected, taint, std::memory_order_relaxed))
            return;
        if (!down)
            return;
        const std::uint16_t causer = g_neutralizedBy[soldierIndex].load(std::memory_order_relaxed);
        if (!IsSoldierId(causer) || !AreHostile(soldierIndex, causer & kSoldierIndexMask))
            return;
        char source[64];
        DescribeTaint(taint, source, sizeof(source));
        LogInfo("SoldierFaction: downed soldier %u hit by %s, rivals react to his body", soldierIndex, source);
    }

    bool FactionCausedNeutralization(const void* soldierCore, std::uint32_t soldierIndex)
    {
        if (!soldierCore || g_downMs[soldierIndex].load(std::memory_order_acquire) == 0)
            return false;
        const std::uint8_t status = ReadLifeStatusSEH(soldierCore, soldierIndex);
        if (status == kLifeStatusAlive || status == kLifeStatusUnreadable)
            return false;
        if (g_taint[soldierIndex].load(std::memory_order_relaxed) != 0)
            return false;
        const std::uint16_t causer = g_neutralizedBy[soldierIndex].load(std::memory_order_relaxed);
        return IsSoldierId(causer) && AreHostile(soldierIndex, causer & kSoldierIndexMask);
    }

    bool IgnoreBody(const void* soldierCore, std::uint32_t observerIndex, std::uint16_t bodyId, bool needNeutralized,
                    std::uint16_t* soldierOut)
    {
        if (bodyId == kNoBody)
            return false;
        const std::uint16_t soldier = ResolveBodySoldier(bodyId);
        if (!IsSoldierId(soldier))
            return false;
        const std::uint32_t bodyIndex = soldier & kSoldierIndexMask;
        if (bodyIndex == observerIndex || !AreHostile(observerIndex, bodyIndex))
            return false;
        if (!FactionCausedNeutralization(soldierCore, bodyIndex))
            return false;
        if (needNeutralized && IsSoldierId(bodyId) && !IsNeutralizedSEH(soldierCore, bodyIndex))
            return false;
        *soldierOut = soldier;
        return true;
    }

    bool ObserverHasFaction(std::uint32_t observerIndex)
    {
        return g_anyFaction.load(std::memory_order_relaxed) && observerIndex < kSoldierSlotCount
            && FactionOf(observerIndex) != kNoFaction;
    }

    void NoteIgnored(std::uint32_t observerIndex, std::uint16_t soldierId, const char* how)
    {
        const std::uint64_t now = GetTickCount64();
        const std::uint16_t last = g_lastIgnored[observerIndex].exchange(soldierId, std::memory_order_relaxed);
        if (last == soldierId && now - g_lastIgnoredLogMs[observerIndex].load(std::memory_order_relaxed) < kIgnoredLogMs)
            return;
        g_lastIgnoredLogMs[observerIndex].store(now, std::memory_order_relaxed);
        LogInfo("SoldierFaction: soldier %u ignores downed rival %u (%s)", observerIndex, soldierId & kSoldierIndexMask, how);
    }

    void PurgeBodyTableSEH(const void* soldierCore, std::uint32_t observerIndex, std::uint8_t* bodyTable)
    {
        __try
        {
            for (std::uint32_t slot = 0; slot < kBodySlotCount; ++slot)
            {
                std::uint8_t* slotId = bodyTable + kBodyTableIds + slot * sizeof(std::uint16_t);
                std::uint16_t bodyId = kNoBody;
                std::memcpy(&bodyId, slotId, sizeof(bodyId));
                std::uint16_t soldier = kNoBody;
                if (!IgnoreBody(soldierCore, observerIndex, bodyId, false, &soldier))
                    continue;
                std::memcpy(slotId, &kNoBody, sizeof(kNoBody));
                std::uint16_t bodyCount = 0;
                std::memcpy(&bodyCount, bodyTable + kBodyTableCount, sizeof(bodyCount));
                bodyCount = static_cast<std::uint16_t>(bodyCount + kNoBody);
                std::memcpy(bodyTable + kBodyTableCount, &bodyCount, sizeof(bodyCount));
                NoteIgnored(observerIndex, soldier, "forgotten");
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void HideSightEntriesSEH(const void* soldierCore, std::uint32_t observerIndex, SoldierFactionHiddenSight* hidden)
    {
        __try
        {
            const std::uint8_t* core  = static_cast<const std::uint8_t*>(soldierCore);
            const std::uint8_t* lists = *reinterpret_cast<const std::uint8_t* const*>(core + kSoldierCoreSightLists);
            if (!lists)
                return;
            const std::uint32_t* counts = *reinterpret_cast<const std::uint32_t* const*>(lists + kSightListCounts);
            std::uint8_t* const* entryLists = *reinterpret_cast<std::uint8_t* const* const*>(lists + kSightListEntries);
            if (!counts || !entryLists)
                return;
            std::uint8_t* entries = entryLists[observerIndex];
            std::uint32_t count = counts[observerIndex];
            if (!entries)
                return;
            if (count > kMaxSightEntries)
                count = kMaxSightEntries;
            for (std::uint32_t i = 0; i < count && hidden->count < kMaxHiddenSight; ++i)
            {
                std::uint8_t* entry = entries + i * kSightEntryStride;
                if (entry[4] != kSightKindSoldier)
                    continue;
                std::uint16_t bodyId = kNoBody;
                std::memcpy(&bodyId, entry, sizeof(bodyId));
                std::uint16_t soldier = kNoBody;
                if (!IgnoreBody(soldierCore, observerIndex, bodyId, true, &soldier))
                    continue;
                entry[4] = kSightKindHidden;
                hidden->entries[hidden->count] = entry;
                hidden->ids[hidden->count]     = bodyId;
                ++hidden->count;
                NoteIgnored(observerIndex, soldier, "seen");
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void HideCollapseNoiseSEH(void* noticeController, const void* soldierCore, std::uint32_t observerIndex,
                              SoldierFactionHiddenSight* hidden)
    {
        __try
        {
            const std::uint8_t* core    = static_cast<const std::uint8_t*>(soldierCore);
            const std::uint8_t* records = *reinterpret_cast<const std::uint8_t* const*>(core + kSoldierCoreRecords);
            if (!records)
                return;
            const std::uint8_t* recordArray = *reinterpret_cast<const std::uint8_t* const*>(records + kRecordArray);
            const std::uint32_t recordCount = *reinterpret_cast<const std::uint32_t*>(records + kRecordCount);
            if (!recordArray || observerIndex >= recordCount)
                return;
            const std::uint64_t flags = *reinterpret_cast<const std::uint64_t*>(recordArray + observerIndex * kRecordStride);
            if ((flags & kRecordHearsCollapse) == 0)
                return;
            std::uint8_t* noiseSlots = *reinterpret_cast<std::uint8_t* const*>(
                static_cast<std::uint8_t*>(noticeController) + kNoticeControllerNoise);
            if (!noiseSlots)
                return;
            void** slot = reinterpret_cast<void**>(noiseSlots + observerIndex * kNoiseSlotStride + kNoiseSlotCurrent);
            const std::uint8_t* noise = static_cast<const std::uint8_t*>(*slot);
            if (!noise || noise[kNoiseKind] != kNoiseKindCollapse)
                return;
            std::uint16_t source = kNoBody;
            std::memcpy(&source, noise + kNoiseSource, sizeof(source));
            std::uint16_t soldier = kNoBody;
            if (!IsSoldierId(source) || !IgnoreBody(soldierCore, observerIndex, source, true, &soldier))
                return;
            hidden->noiseSlot = slot;
            hidden->noise     = *slot;
            *slot = nullptr;
            NoteIgnored(observerIndex, soldier, "heard");
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void RestoreHiddenSightSEH(SoldierFactionHiddenSight* hidden)
    {
        __try
        {
            for (std::uint32_t i = 0; i < hidden->count; ++i)
            {
                std::uint8_t* entry = hidden->entries[i];
                std::uint16_t bodyId = kNoBody;
                std::memcpy(&bodyId, entry, sizeof(bodyId));
                if (bodyId == hidden->ids[i] && entry[4] == kSightKindHidden)
                    entry[4] = kSightKindSoldier;
            }
            if (hidden->noiseSlot && !*hidden->noiseSlot)
                *hidden->noiseSlot = hidden->noise;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    bool LeaveBodySEH(const void* soldierCore, std::uint32_t soldierIndex, std::uint8_t* knowledge,
                      SoldierFactionBodyNode kind)
    {
        __try
        {
            const std::size_t bodyField = kind == SoldierFactionBodyNode::DownComrade ? kDownComradeBody : kVehicleDownComradeBody;
            std::uint16_t bodyId = kNoBody;
            std::memcpy(&bodyId, knowledge + bodyField, sizeof(bodyId));
            std::uint16_t soldier = kNoBody;
            if (!IgnoreBody(soldierCore, soldierIndex, bodyId, false, &soldier))
                return false;
            if (kind == SoldierFactionBodyNode::DownComrade)
                knowledge[kDownComradeInTable] = static_cast<std::uint8_t>(knowledge[kDownComradeInTable] & ~kDownComradeInTableBit);
            std::memcpy(knowledge + bodyField, &kNoBody, sizeof(kNoBody));
            LogInfo("SoldierFaction: soldier %u stops checking downed rival %u", soldierIndex, soldier & kSoldierIndexMask);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
}

void Set_SoldierFaction(std::uint32_t gameObjectId, const char* factionName)
{
    if (!gAddr.CombatAntiBuddyWalkerGearAiImpl_UpdatePrimaryTarget)
    {
        static std::atomic<bool> warned{ false };
        if (!warned.exchange(true))
            LogWarn("SetFaction: not supported on this game build yet, soldiers keep vanilla behaviour");
        return;
    }

    const std::uint32_t slot = gameObjectId & kSoldierIndexMask;
    const std::string name = factionName ? factionName : "";

    const std::uint16_t factionId = name.empty() ? kNoFaction : InternFactionName(name);
    if (factionId != kNoFaction)
        g_anyFaction.store(true, std::memory_order_relaxed);
    g_faction[slot].store(factionId, std::memory_order_relaxed);
}

void Clear_AllSoldierFactions()
{
    g_anyFaction.store(false, std::memory_order_relaxed);
    for (std::uint32_t i = 0; i < kSoldierSlotCount; ++i)
    {
        g_faction[i].store(kNoFaction, std::memory_order_relaxed);
        g_rival[i].store(kNoRival, std::memory_order_relaxed);
        g_pendingRival[i].store(kNoRival, std::memory_order_relaxed);
        g_mount[i].store(kNoMount, std::memory_order_relaxed);
        g_loggedMountShot[i].store(kNoMount, std::memory_order_relaxed);
        g_neutralizedBy[i].store(kNoRival, std::memory_order_relaxed);
        g_rivalHitMs[i].store(0, std::memory_order_relaxed);
        g_taint[i].store(0, std::memory_order_relaxed);
        g_downMs[i].store(0, std::memory_order_relaxed);
        g_corpseOwner[i].store(kNoBody, std::memory_order_relaxed);
        g_corpseOwnerMs[i].store(0, std::memory_order_relaxed);
        g_lastIgnored[i].store(kNoBody, std::memory_order_relaxed);
        g_lastIgnoredLogMs[i].store(0, std::memory_order_relaxed);
    }
    for (std::uint32_t i = 0; i < kGameObjectIdCount; ++i)
        g_mountRider[i].store(kNoMount, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lk(g_namesMtx);
    g_factionNames.clear();
}

bool SoldierFaction_AnyFaction()
{
    return g_anyFaction.load(std::memory_order_relaxed);
}

bool SoldierFaction_IsFactionSoldier(std::uint16_t gameObjectId)
{
    return IsSoldierId(gameObjectId) && FactionOf(gameObjectId) != kNoFaction;
}

bool SoldierFaction_AreHostileSoldiers(std::uint16_t gameObjectId, std::uint16_t otherGameObjectId)
{
    if (!IsSoldierId(gameObjectId) || !IsSoldierId(otherGameObjectId))
        return false;
    return AreHostile(gameObjectId & kSoldierIndexMask, otherGameObjectId & kSoldierIndexMask);
}

static bool RunSightCheck(void* noticeController, std::uint32_t soldierIndex, std::uint64_t now)
{
    if (soldierIndex >= kSoldierSlotCount)
        return false;
    if (FactionOf(soldierIndex) == kNoFaction)
    {
        if (g_rival[soldierIndex].load(std::memory_order_relaxed) != kNoRival)
            SoldierFaction_EndRivalFight(soldierIndex, "soldier has no faction");
        return false;
    }

    const void* soldierCore = nullptr;
    std::memcpy(&soldierCore, static_cast<const std::uint8_t*>(noticeController) + kNoticeControllerSoldierCore,
                sizeof(soldierCore));
    if (!soldierCore)
        return false;

    const std::uint16_t current = g_rival[soldierIndex].load(std::memory_order_relaxed);
    if (const char* blocked = WhyCannotFight(soldierCore, soldierIndex))
    {
        g_pendingRival[soldierIndex].store(kNoRival, std::memory_order_relaxed);
        if (current != kNoRival)
            SoldierFaction_EndRivalFight(soldierIndex, blocked);
        return false;
    }

    SightEntry entries[kMaxSightEntries];

    if (current != kNoRival)
    {
        const std::uint32_t count = ReadSightEntries(soldierCore, soldierIndex, entries);
        if (RivalInSight(current, entries, count)
            || RivalSensed(soldierCore, soldierIndex, current & kSoldierIndexMask, now))
            g_rivalSeenMs[soldierIndex].store(now, std::memory_order_relaxed);

        if (now >= g_nextRepushMs[soldierIndex].load(std::memory_order_relaxed))
        {
            g_nextRepushMs[soldierIndex].store(now + kNoticeRepushMs, std::memory_order_relaxed);
            PushWalkerGearNotice(noticeController, soldierIndex, current);
        }

        const std::uint64_t aliveMs = g_rivalAliveMs[soldierIndex].load(std::memory_order_relaxed);
        const bool woke = g_rivalNodeWoke[soldierIndex].load(std::memory_order_relaxed);
        const char* idle = nullptr;
        if (!woke && now - aliveMs > kNodeWakeGraceMs)
            idle = "combat node never woke";
        else if (woke && now - aliveMs > kNodeStalledMs)
            idle = "combat node stopped running";
        if (!idle)
            return false;

        char reason[128];
        IdleSnapshot snapshot{};
        if (ReadIdleSnapshot(soldierCore, soldierIndex, &snapshot))
            std::snprintf(reason, sizeof(reason), "%s (notice 0x%02X, life 0x%02X/%u, duty %u)",
                idle, snapshot.notice, snapshot.lifeFlags, snapshot.lifeDead, snapshot.duty);
        else
            std::snprintf(reason, sizeof(reason), "%s", idle);
        SoldierFaction_EndRivalFight(soldierIndex, reason);
        return false;
    }

    bool scanned = false;
    const char* how = "fights back against";
    std::uint16_t rival = TakePendingRival(soldierCore, soldierIndex);
    if (rival == kNoRival)
    {
        if (now < g_nextScanMs[soldierIndex].load(std::memory_order_relaxed))
            return false;
        g_nextScanMs[soldierIndex].store(now + kScanIntervalMs, std::memory_order_relaxed);
        scanned = true;

        const std::uint32_t count = ReadSightEntries(soldierCore, soldierIndex, entries);
        how = "spotted";
        rival = PickRival(soldierCore, soldierIndex, entries, count);
        if (rival == kNoRival)
        {
            how = "sensed";
            rival = PickSensedRival(soldierCore, soldierIndex, now);
        }
    }
    if (rival == kNoRival)
        return scanned;

    if (!PushWalkerGearNotice(noticeController, soldierIndex, rival))
    {
        LogRefusal(soldierIndex, rival, now);
        return scanned;
    }

    g_rivalSeenMs[soldierIndex].store(now, std::memory_order_relaxed);
    g_rivalAliveMs[soldierIndex].store(now, std::memory_order_relaxed);
    g_rivalNodeWoke[soldierIndex].store(false, std::memory_order_relaxed);
    g_rival[soldierIndex].store(rival, std::memory_order_relaxed);
    g_nextRepushMs[soldierIndex].store(now + kNoticeRepushMs, std::memory_order_relaxed);

    const std::uint32_t rivalIndex = rival & kSoldierIndexMask;
    AskRivalToFightBack(rivalIndex, static_cast<std::uint16_t>(0x400u | soldierIndex));

    LogInfo("SoldierFaction: soldier %u (%s) %s rival %u (%s), engaging",
        soldierIndex, FactionNameOf(FactionOf(soldierIndex)).c_str(), how,
        rivalIndex, FactionNameOf(FactionOf(rivalIndex)).c_str());
    return scanned;
}

void SoldierFaction_OnSightCheck(void* noticeController, std::uint32_t soldierIndex)
{
    if (!g_anyFaction.load(std::memory_order_relaxed) || !noticeController)
        return;

    const std::int64_t start = PerfNow();
    NotePerfGap(start);
    const std::uint64_t now = GetTickCount64();

    if (RunSightCheck(noticeController, soldierIndex, now))
        g_perfScans.fetch_add(1, std::memory_order_relaxed);
    g_perfChecks.fetch_add(1, std::memory_order_relaxed);
    g_perfTicks.fetch_add(PerfNow() - start, std::memory_order_relaxed);
    FlushPerfWindow(now);
}

void SoldierFaction_WidenBulletRequest(void* bullet3, std::uint32_t requestIndex)
{
    if (!g_anyFaction.load(std::memory_order_relaxed) || !bullet3 || MissionCodeGuard::ShouldBypassHooks())
        return;

    std::uint16_t owner = 0;
    if (!WidenFactionBulletSEH(bullet3, requestIndex, &owner))
        return;

    const std::uint16_t shooter = RiderOrSelf(owner);
    const std::uint32_t shooterIndex = shooter & kSoldierIndexMask;
    if (g_mount[shooterIndex].load(std::memory_order_relaxed) == kNoMount)
        return;
    if (g_rival[shooterIndex].load(std::memory_order_relaxed) != kNoRival)
        g_lastShotMs[shooterIndex].store(GetTickCount64(), std::memory_order_relaxed);
    LogMountShotOnce(owner, shooter);
}

void SoldierFaction_SetMount(std::uint32_t soldierIndex, std::uint16_t mountId)
{
    if (soldierIndex >= kSoldierSlotCount)
        return;
    if (mountId == kNoMountInNode || IsSoldierId(mountId))
        mountId = kNoMount;
    if (g_mount[soldierIndex].load(std::memory_order_relaxed) == mountId)
        return;

    ClearMount(soldierIndex);
    if (mountId == kNoMount)
        return;
    g_mount[soldierIndex].store(mountId, std::memory_order_relaxed);
    g_mountRider[mountId].store(static_cast<std::uint16_t>(0x400u | soldierIndex), std::memory_order_relaxed);
    LogInfo("SoldierFaction: soldier %u fights from walker gear or vehicle 0x%04X", soldierIndex, mountId);
}

std::uint16_t SoldierFaction_RiderOrSelf(std::uint16_t gameObjectId)
{
    return RiderOrSelf(gameObjectId);
}

void SoldierFaction_BeginShotScope(std::uint32_t shooterIndex, std::uint16_t rivalId)
{
    t_shotScope.shooterIndex = shooterIndex & kSoldierIndexMask;
    t_shotScope.rivalIndex   = rivalId & kSoldierIndexMask;
    t_shotScope.active       = true;
}

void SoldierFaction_EndShotScope()
{
    t_shotScope.active = false;
}

std::uint32_t SoldierFaction_FilterSoldierSearch(std::uint8_t* result, std::uint32_t count)
{
    if (!t_shotScope.active || !result || count == 0)
        return count;
    return FilterSoldierSearchSEH(result, count, t_shotScope.shooterIndex, t_shotScope.rivalIndex);
}

void SoldierFaction_OnSoldierDamages(void* damageHandler, std::uint32_t victimIndex, void* damages, std::uint32_t count)
{
    if (!g_anyFaction.load(std::memory_order_relaxed) || !damages || victimIndex >= kSoldierSlotCount)
        return;
    if (FactionOf(victimIndex) == kNoFaction)
        return;

    const void* soldierCore = damageHandler ? ReadCorePointerSEH(damageHandler, kDamageHandlerCore) : nullptr;
    bool down = g_downMs[victimIndex].load(std::memory_order_acquire) != 0;
    if (down && soldierCore && ReadLifeStatusSEH(soldierCore, victimIndex) == kLifeStatusAlive)
    {
        ResetNeutralization(victimIndex);
        down = false;
    }

    const std::uint16_t victimId = static_cast<std::uint16_t>(0x400u | victimIndex);
    std::uint16_t rivalCause = kNoRival;
    std::uint8_t* damage = static_cast<std::uint8_t*>(damages);
    for (std::uint32_t i = 0; i < count; ++i, damage += kDamageStride)
    {
        std::uint16_t attacker = 0;
        std::uint16_t attackId = 0;
        std::memcpy(&attacker, damage + kDamageAttacker, sizeof(attacker));
        std::memcpy(&attackId, damage + kDamageAttackId, sizeof(attackId));
        if (IsPlayerOnlyAttack(attackId))
            attacker = kPlayerId;

        const bool self = attacker == victimId;
        if ((attacker == kUnknownAttacker || self) && IsConsequenceAttack(attackId))
            continue;

        const std::uint16_t source = AttributeAttacker(soldierCore, attacker, attackId);
        if (!self && IsSoldierId(source) && AreHostile(victimIndex, source & kSoldierIndexMask))
        {
            damage[kDamageFlags] = static_cast<std::uint8_t>(damage[kDamageFlags] & ~kDamageFlagInstantKill);
            AskRivalToFightBack(victimIndex, source);
            if (!down)
                rivalCause = source;
            continue;
        }
        if (!self && IsSoldierId(source) && (down || IsSoldierHarmlessAttack(attackId)))
            continue;
        TaintNeutralization(victimIndex, source, attackId, down);
    }

    if (soldierCore && IsHeldUp(soldierCore, victimIndex))
        TaintNeutralization(victimIndex, kTaintHoldUp, 0, down);
    if (rivalCause != kNoRival && !down)
    {
        g_neutralizedBy[victimIndex].store(rivalCause, std::memory_order_relaxed);
        g_rivalHitMs[victimIndex].store(GetTickCount64(), std::memory_order_relaxed);
    }
}

void SoldierFaction_OnLifeStatusChanged(void* lifeController, std::uint32_t soldierIndex, const void* life,
                                        std::uint32_t oldStatus)
{
    if (!g_anyFaction.load(std::memory_order_relaxed) || soldierIndex >= kSoldierSlotCount || !life)
        return;

    const std::uint8_t status = ReadLifeStatusByteSEH(life);
    if (status == kLifeStatusUnreadable)
        return;
    if (status == kLifeStatusDead)
    {
        for (std::uint32_t i = 0; i < kSoldierSlotCount; ++i)
            g_corpseOwnerMs[i].store(0, std::memory_order_relaxed);
    }
    if (FactionOf(soldierIndex) == kNoFaction)
        return;

    if (oldStatus == kLifeStatusAlive && status != kLifeStatusAlive)
    {
        if (g_downMs[soldierIndex].load(std::memory_order_acquire) != 0)
            ResetNeutralization(soldierIndex);
        const void* soldierCore = lifeController ? ReadCorePointerSEH(lifeController, kLifeControllerCore) : nullptr;
        if (soldierCore && IsHeldUp(soldierCore, soldierIndex))
            TaintNeutralization(soldierIndex, kTaintHoldUp, 0, false);

        const std::uint64_t now = GetTickCount64();
        const std::uint16_t causer = g_neutralizedBy[soldierIndex].load(std::memory_order_relaxed);
        if (IsSoldierId(causer) && now - g_rivalHitMs[soldierIndex].load(std::memory_order_relaxed) > kCauseFreshMs)
        {
            g_neutralizedBy[soldierIndex].store(kCauseStale, std::memory_order_relaxed);
            LogInfo("SoldierFaction: soldier %u downed too long after rival %u's hit, vanilla reactions",
                soldierIndex, causer & kSoldierIndexMask);
        }
        g_downMs[soldierIndex].store(now != 0 ? now : 1, std::memory_order_release);

        if (RecordSaysFactionCaused(soldierIndex))
        {
            LogInfo("SoldierFaction: soldier %u downed by rival %u, rivals ignore his body",
                soldierIndex, causer & kSoldierIndexMask);
        }
        else if (IsSoldierId(causer))
        {
            const std::uint64_t taint = g_taint[soldierIndex].load(std::memory_order_relaxed);
            if (taint != 0)
            {
                char source[64];
                DescribeTaint(taint, source, sizeof(source));
                LogInfo("SoldierFaction: soldier %u downed by rival %u but also hit by %s, vanilla reactions",
                    soldierIndex, causer & kSoldierIndexMask, source);
            }
        }
        return;
    }

    if (oldStatus != kLifeStatusAlive && status == kLifeStatusAlive)
    {
        if (RecordSaysFactionCaused(soldierIndex))
            LogInfo("SoldierFaction: soldier %u is up again, rival kill record cleared", soldierIndex);
        ResetNeutralization(soldierIndex);
    }
}

std::uint16_t SoldierFaction_GetRivalTarget(std::uint32_t soldierIndex)
{
    if (soldierIndex >= kSoldierSlotCount)
        return kNoRival;
    return g_rival[soldierIndex].load(std::memory_order_relaxed);
}

void SoldierFaction_MarkRivalNodeAlive(std::uint32_t soldierIndex)
{
    if (soldierIndex >= kSoldierSlotCount)
        return;
    g_rivalAliveMs[soldierIndex].store(GetTickCount64(), std::memory_order_relaxed);
    g_rivalNodeWoke[soldierIndex].store(true, std::memory_order_relaxed);
}

const char* SoldierFaction_WhyRivalLost(const void* soldierCore, std::uint32_t soldierIndex)
{
    const std::uint16_t rival = SoldierFaction_GetRivalTarget(soldierIndex);
    if (rival == kNoRival || !soldierCore)
        return "no rival";

    const std::uint32_t rivalIndex = rival & kSoldierIndexMask;
    if (!AreHostile(soldierIndex, rivalIndex))
        return "rival is no longer hostile";
    if (!IsSoldierStanding(soldierCore, rivalIndex))
        return "rival is down";
    if (!RivalInFightRange(soldierCore, soldierIndex, rivalIndex))
        return "rival is more than 80 m away";

    const std::uint64_t seenMs = g_rivalSeenMs[soldierIndex].load(std::memory_order_relaxed);
    if (GetTickCount64() - seenMs > kRivalLostMs)
        return "rival unseen and unheard for 30 s";
    return nullptr;
}

void SoldierFaction_MarkFiring(std::uint32_t shooterIndex)
{
    if (shooterIndex < kSoldierSlotCount)
        g_lastShotMs[shooterIndex].store(GetTickCount64(), std::memory_order_relaxed);
}

bool SoldierFaction_ReadRivalAimPoint(const void* soldierCore, std::uint16_t rivalId, float* aimPoint)
{
    SoldierPoint rival{};
    if (!soldierCore || !ReadSoldierPoint(soldierCore, rivalId & kSoldierIndexMask, &rival))
        return false;
    aimPoint[0] = rival.x;
    aimPoint[1] = rival.y + kAimHeight;
    aimPoint[2] = rival.z;
    return true;
}

bool SoldierFaction_IsShotAtRival(const void* soldierCore, std::uint32_t shooterIndex, const float* shotTarget)
{
    const std::uint16_t rivalId = SoldierFaction_GetRivalTarget(shooterIndex);
    if (rivalId == kNoRival || !soldierCore || !shotTarget)
        return false;

    SoldierPoint rival{};
    if (!ReadSoldierPoint(soldierCore, rivalId & kSoldierIndexMask, &rival))
        return false;

    const float dx = shotTarget[0] - rival.x;
    const float dz = shotTarget[2] - rival.z;
    const float dy = shotTarget[1] - rival.y;
    return dx * dx + dz * dz <= kShotAtRivalRadiusSq && dy > -kShotAtRivalHeight && dy < kShotAtRivalHeight;
}

void SoldierFaction_EndRivalFight(std::uint32_t soldierIndex, const char* reason)
{
    if (soldierIndex >= kSoldierSlotCount)
        return;
    ClearMount(soldierIndex);
    const std::uint16_t rival = g_rival[soldierIndex].exchange(kNoRival, std::memory_order_relaxed);
    if (rival == kNoRival)
        return;
    LogInfo("SoldierFaction: soldier %u stopped fighting rival %u, %s",
        soldierIndex, rival & kSoldierIndexMask, reason);
}

void SoldierFaction_HideDownedRivals(void* noticeController, std::uint32_t soldierIndex, void* bodyTable,
                                     SoldierFactionHiddenSight* hidden)
{
    hidden->count     = 0;
    hidden->noiseSlot = nullptr;
    hidden->noise     = nullptr;
    if (!noticeController || !ObserverHasFaction(soldierIndex))
        return;

    const void* soldierCore = nullptr;
    std::memcpy(&soldierCore, static_cast<const std::uint8_t*>(noticeController) + kNoticeControllerSoldierCore,
                sizeof(soldierCore));
    if (!soldierCore)
        return;

    if (bodyTable)
        PurgeBodyTableSEH(soldierCore, soldierIndex, static_cast<std::uint8_t*>(bodyTable));
    HideSightEntriesSEH(soldierCore, soldierIndex, hidden);
    HideCollapseNoiseSEH(noticeController, soldierCore, soldierIndex, hidden);
}

void SoldierFaction_RestoreDownedRivals(SoldierFactionHiddenSight* hidden)
{
    if (hidden->count != 0 || hidden->noiseSlot)
        RestoreHiddenSightSEH(hidden);
}

bool SoldierFaction_ShouldDropNotice(void* noticeInterface, std::uint32_t soldierIndex, std::uint8_t noticeType,
                                     const void* notice)
{
    if (noticeType != kNoticeBodyFound && noticeType != kNoticeBodyKnown)
        return false;
    if (!noticeInterface || !notice || !ObserverHasFaction(soldierIndex))
        return false;

    const void* soldierCore = nullptr;
    std::memcpy(&soldierCore, static_cast<const std::uint8_t*>(noticeInterface) + kNoticeInterfaceSoldierCore,
                sizeof(soldierCore));
    std::uint16_t bodyId = kNoBody;
    std::memcpy(&bodyId, static_cast<const std::uint8_t*>(notice) + kNoticeTarget, sizeof(bodyId));
    std::uint16_t soldier = kNoBody;
    if (!soldierCore || !IgnoreBody(soldierCore, soldierIndex, bodyId, false, &soldier))
        return false;

    ++t_ignoredNotices;
    NoteIgnored(soldierIndex, soldier, "reported");
    return true;
}

std::uint32_t SoldierFaction_IgnoredNoticeCount()
{
    return t_ignoredNotices;
}

bool SoldierFaction_ShouldIgnoreNoise(void* noticeController, std::uint32_t soldierIndex, const void* noise)
{
    if (!noticeController || !noise || !ObserverHasFaction(soldierIndex))
        return false;

    const void* soldierCore = nullptr;
    std::memcpy(&soldierCore, static_cast<const std::uint8_t*>(noticeController) + kNoticeControllerSoldierCore,
                sizeof(soldierCore));
    std::uint16_t source = kNoBody;
    std::memcpy(&source, static_cast<const std::uint8_t*>(noise) + kNoiseSource, sizeof(source));
    std::uint16_t soldier = kNoBody;
    if (!soldierCore || !IsSoldierId(source) || !IgnoreBody(soldierCore, soldierIndex, source, true, &soldier))
        return false;

    NoteIgnored(soldierIndex, soldier, "heard");
    return true;
}

bool SoldierFaction_ShouldLeaveBody(void* node, std::uint32_t soldierIndex, void* const* context,
                                    SoldierFactionBodyNode kind)
{
    if (!node || !context || !context[0] || !ObserverHasFaction(soldierIndex))
        return false;

    const void* soldierCore = nullptr;
    std::memcpy(&soldierCore, static_cast<const std::uint8_t*>(node) + kBodyNodeSoldierCore, sizeof(soldierCore));
    if (!soldierCore)
        return false;
    return LeaveBodySEH(soldierCore, soldierIndex, static_cast<std::uint8_t*>(context[0]), kind);
}
