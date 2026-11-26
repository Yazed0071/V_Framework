#include <pch.h>

#include <Windows.h>
#include <array>
#include <atomic>
#include <cmath>
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
#include "SoldierFactionAntiAir.h"
#include "SoldierFactionHeli.h"
#include "log.h"
#include "../heli/HeliKey.h"

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
    constexpr std::uint8_t  kNoticeTypeAntiAir    = 7u;
    constexpr std::uint8_t  kNoticeLevelSighted  = 1u;
    constexpr std::uint32_t kMaxSightEntries      = 64u;

    constexpr std::uint64_t kRivalLostMs     = 30000u;
    constexpr std::uint64_t kNodeWakeGraceMs = 2000u;
    constexpr std::uint64_t kNodeStalledMs   = 15000u;

    constexpr std::uint64_t kGunfireHeardMs = 3000u;
    constexpr std::uint64_t kScanIntervalMs = 200u;
    constexpr std::uint64_t kNoticeRepushMs = 1000u;

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

    constexpr std::uint16_t kCommonWalkerGearCategory = 0x2Du;
    constexpr std::uint16_t kVehicleCategory          = 0x32u;
    constexpr std::size_t   kGetRideTargetSlot        = 0xA8 / sizeof(void*);   // RouteAiImpl::CheckVehicleAndWalkerGearExist reads all four
    constexpr std::size_t   kGetVehicleInfoSlot       = 0xB0 / sizeof(void*);
    constexpr std::size_t   kGetWalkerGearInfoSlot    = 0xB8 / sizeof(void*);
    constexpr std::size_t   kGetVehicleSeatSlot       = 0xC0 / sizeof(void*);
    constexpr std::size_t   kVehicleSeats             = 0x30;
    constexpr std::uint8_t  kMaxVehicleSeats          = 4u;
    constexpr std::size_t   kWalkerGearPilot          = 0x2C8;
    constexpr std::size_t   kWalkerGearRideState      = 0x55C;
    constexpr std::uint64_t kRideCheckMs              = 500u;

    constexpr std::uint16_t kEnemyHeliCategory    = 0x0Fu;   // TppEnemyHeli, then TppOtherHeli, TppOtherHeli2; 0x0E is the player's support heli
    constexpr std::uint32_t kMaxFactionHelis      = 16u;
    constexpr std::size_t   kHeliType             = 0x130;
    constexpr std::size_t   kFlightControllerHeli = 0x48;
    constexpr std::size_t   kHeliShared           = 0x28;   // position table +0x20 {x,y,z,yaw}, count +0x40, as the GetPosition command reads it
    constexpr std::size_t   kHeliPositions        = 0x20;
    constexpr std::size_t   kHeliCount            = 0x40;
    constexpr std::size_t   kHeliPositionStride   = 0x10;
    constexpr std::size_t   kHeliLives            = 0xC8;   // +8 array, 0x10 per heli, life first
    constexpr std::size_t   kHeliLifeStride       = 0x10;
    constexpr std::size_t   kHeliStates           = 0x60;   // +8 array, 0x18 per heli, the u64 state the Unrealize command clears 0x1000 in
    constexpr std::size_t   kHeliStateStride      = 0x18;
    constexpr std::uint64_t kHeliStateRealized    = 0x1000u;
    constexpr std::uint64_t kHeliFreshMs          = 1000u;
    constexpr float         kHeliNoticeRadiusSq   = 50.0f * 50.0f;    // horizontal, the range vanilla soldiers report the support heli at
    constexpr float         kHeliFightRangeSq     = 150.0f * 150.0f;
    constexpr float         kShotAtHeliRadiusSq   = 6.0f * 6.0f;
    constexpr float         kShotAtHeliHeight     = 6.0f;

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
    using GetRideTarget_t = const std::uint16_t*(__fastcall*)(void* vehicleInterface, std::uint16_t* mountId, std::uint32_t soldierIndex);
    using GetRideInfo_t   = const std::uint8_t*(__fastcall*)(void* vehicleInterface, std::uint32_t soldierIndex);
    using GetSeat_t       = std::uint8_t(__fastcall*)(void* vehicleInterface, std::uint32_t soldierIndex);
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
    std::array<std::atomic<std::uint64_t>, kSoldierSlotCount> g_lastShotMs{};
    std::array<std::atomic<std::uint64_t>, kSoldierSlotCount> g_nextScanMs{};
    std::array<std::atomic<std::uint64_t>, kSoldierSlotCount> g_nextRepushMs{};
    std::array<std::atomic<std::uint16_t>, kSoldierSlotCount> g_mount{};
    std::array<std::atomic<std::uint16_t>, kGameObjectIdCount> g_mountRider{};
    std::array<std::atomic<std::uint16_t>, kSoldierSlotCount> g_rideMount{};
    std::array<std::atomic<std::uint64_t>, kSoldierSlotCount> g_nextRideCheckMs{};

    struct HeliSeen
    {
        std::atomic<float>         x{ 0.0f };
        std::atomic<float>         y{ 0.0f };
        std::atomic<float>         z{ 0.0f };
        std::atomic<std::uint64_t> ms{ 0 };
    };

    std::array<std::atomic<std::uint16_t>, HeliKey::kCount>   g_heliFaction{};
    std::array<HeliSeen, HeliKey::kCount>                     g_heliSeen{};
    std::array<std::atomic<std::uint16_t>, kMaxFactionHelis>  g_factionHelis{};
    std::atomic<std::uint32_t>                                g_factionHeliCount{ 0 };
    std::array<std::atomic<std::uint16_t>, kSoldierSlotCount> g_neutralizedBy{};
    std::array<std::atomic<std::uint64_t>, kSoldierSlotCount> g_rivalHitMs{};
    std::array<std::atomic<std::uint64_t>, kSoldierSlotCount> g_taint{};
    std::array<std::atomic<std::uint64_t>, kSoldierSlotCount> g_downMs{};
    std::array<std::atomic<std::uint16_t>, kSoldierSlotCount> g_corpseOwner{};
    std::array<std::atomic<std::uint64_t>, kSoldierSlotCount> g_corpseOwnerMs{};
    thread_local std::uint32_t                                t_ignoredNotices = 0;
    std::atomic<bool>                                         g_anyFaction{ false };
    std::atomic<bool>                                         g_moduleActive{ false };
    std::atomic<const void*>                                  g_soldierCore{ nullptr };   // every soldier shares it, any sight check hands it out

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
        if (mount == kNoMount || g_rideMount[soldierIndex].load(std::memory_order_relaxed) == mount)   // still aboard: keep him on it
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

    bool AreHostile(std::uint32_t soldierIndex, std::uint32_t otherIndex)
    {
        const std::uint16_t mine   = FactionOf(soldierIndex);
        const std::uint16_t theirs = FactionOf(otherIndex);
        return mine != kNoFaction && theirs != kNoFaction && mine != theirs;
    }

    bool IsEnemyHeliId(std::uint16_t gameObjectId)
    {
        const std::uint16_t category = gameObjectId >> 9;
        return category >= kEnemyHeliCategory && category <= HeliKey::kLastType;
    }

    std::uint16_t FactionOfId(std::uint16_t gameObjectId)
    {
        if (IsSoldierId(gameObjectId))
            return FactionOf(gameObjectId);
        if (IsEnemyHeliId(gameObjectId))
            return g_heliFaction[HeliKey::OfGameObjectId(gameObjectId)].load(std::memory_order_relaxed);
        return kNoFaction;
    }

    bool AreHostileIds(std::uint16_t gameObjectId, std::uint16_t otherGameObjectId)
    {
        const std::uint16_t mine   = FactionOfId(gameObjectId);
        const std::uint16_t theirs = FactionOfId(otherGameObjectId);
        return mine != kNoFaction && theirs != kNoFaction && mine != theirs;
    }

    float HorizontalDistanceSq(const SoldierPoint& a, const SoldierPoint& b)
    {
        const float dx = a.x - b.x;
        const float dz = a.z - b.z;
        return dx * dx + dz * dz;
    }

    bool ReadHeliPoint(std::uint16_t heliId, std::uint64_t now, SoldierPoint* out)   // gone, destroyed or unrealized helis stop being published
    {
        const HeliSeen& seen = g_heliSeen[HeliKey::OfGameObjectId(heliId)];
        const std::uint64_t ms = seen.ms.load(std::memory_order_acquire);
        if (ms == 0 || now - ms > kHeliFreshMs)
            return false;
        out->x = seen.x.load(std::memory_order_relaxed);
        out->y = seen.y.load(std::memory_order_relaxed);
        out->z = seen.z.load(std::memory_order_relaxed);
        return true;
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

    bool PushNotice(void* noticeController, std::uint32_t soldierIndex, std::uint8_t type, std::uint16_t targetId)
    {
        alignas(16) std::uint8_t notice[16] = {};
        notice[0] = type;
        notice[1] = kNoticeLevelSighted;
        std::memcpy(notice + 2, &targetId, sizeof(targetId));

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

    bool PushWalkerGearNotice(void* noticeController, std::uint32_t soldierIndex, std::uint16_t rivalId)
    {
        return PushNotice(noticeController, soldierIndex, kNoticeTypeWalkerGear, rivalId);
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
            if (FactionOfId(shooter) == kNoFaction)   // a faction soldier, or a faction heli whose gun only searches outside ENEMY
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

        if (IsEnemyHeliId(pending))
        {
            SoldierPoint self{};
            SoldierPoint heli{};
            const bool reachable = AreHostileIds(static_cast<std::uint16_t>(0x400u | soldierIndex), pending)
                && ReadSoldierPoint(soldierCore, soldierIndex, &self) && ReadHeliPoint(pending, GetTickCount64(), &heli)
                && HorizontalDistanceSq(self, heli) <= kHeliFightRangeSq;
            return reachable ? pending : kNoRival;
        }

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

    void TaintNeutralization(std::uint32_t soldierIndex, std::uint16_t who, std::uint16_t attackId)
    {
        std::uint64_t expected = 0;
        const std::uint64_t taint = kTaintSet | (static_cast<std::uint64_t>(attackId) << 16) | who;
        g_taint[soldierIndex].compare_exchange_strong(expected, taint, std::memory_order_relaxed);
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
        return AreHostileIds(static_cast<std::uint16_t>(0x400u | soldierIndex), causer);
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
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void PublishRider(std::uint16_t mountId, std::uint16_t rider)
    {
        std::uint16_t none = kNoMount;
        g_mountRider[mountId].compare_exchange_strong(none, rider, std::memory_order_relaxed);
    }

    std::uint16_t ReadRideMountSEH(const void* soldierCore, std::uint32_t soldierIndex, std::uint64_t record)
    {
        __try
        {
            void* vehicles = *reinterpret_cast<void* const*>(static_cast<const std::uint8_t*>(soldierCore) + kSoldierCoreVehicles);
            if (!vehicles)
                return kNoMount;
            void* const* slots = *reinterpret_cast<void* const* const*>(vehicles);
            std::uint16_t mountId = kNoMountInNode;
            reinterpret_cast<GetRideTarget_t>(slots[kGetRideTargetSlot])(vehicles, &mountId, soldierIndex);
            const std::uint16_t rider    = static_cast<std::uint16_t>(0x400u | soldierIndex);
            const std::uint16_t category = mountId >> 9;
            if (category == kVehicleCategory && (record & kRecordInVehicle) != 0)
            {
                const std::uint8_t* info = reinterpret_cast<GetRideInfo_t>(slots[kGetVehicleInfoSlot])(vehicles, soldierIndex);
                const std::uint8_t  seat = reinterpret_cast<GetSeat_t>(slots[kGetVehicleSeatSlot])(vehicles, soldierIndex);
                if (!info || seat >= kMaxVehicleSeats)
                    return kNoMount;
                std::uint16_t seated = kNoMountInNode;
                std::memcpy(&seated, info + kVehicleSeats + seat * sizeof(std::uint16_t), sizeof(seated));
                return seated == rider ? mountId : kNoMount;
            }
            if (category == kCommonWalkerGearCategory && (record & kRecordOnWalkerGear) != 0)
            {
                const std::uint8_t* info = reinterpret_cast<GetRideInfo_t>(slots[kGetWalkerGearInfoSlot])(vehicles, soldierIndex);
                if (!info)
                    return kNoMount;
                std::uint16_t pilot = kNoMountInNode;
                std::memcpy(&pilot, info + kWalkerGearPilot, sizeof(pilot));
                const std::uint8_t state = info[kWalkerGearRideState];
                return pilot == rider && (state == 1u || state == 2u) ? mountId : kNoMount;   // GetWalkerGearType counts both as ridden
            }
            return kNoMount;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return kNoMount;
        }
    }

    void SetRideMount(std::uint32_t soldierIndex, std::uint16_t mountId)
    {
        const std::uint16_t rider = static_cast<std::uint16_t>(0x400u | soldierIndex);
        const std::uint16_t old   = g_rideMount[soldierIndex].exchange(mountId, std::memory_order_relaxed);
        if (old == mountId)
        {
            if (mountId != kNoMount)
                PublishRider(mountId, rider);
            return;
        }
        if (old != kNoMount)
        {
            std::uint16_t expected = rider;
            if (g_mount[soldierIndex].load(std::memory_order_relaxed) != old)
                g_mountRider[old].compare_exchange_strong(expected, kNoMount, std::memory_order_relaxed);
        }
        if (mountId == kNoMount)
            return;
        PublishRider(mountId, rider);
    }

    void UpdateRideMount(const void* soldierCore, std::uint32_t soldierIndex, std::uint64_t now)
    {
        if (now < g_nextRideCheckMs[soldierIndex].load(std::memory_order_relaxed))
            return;
        g_nextRideCheckMs[soldierIndex].store(now + kRideCheckMs, std::memory_order_relaxed);

        const std::uint64_t record = ReadRecordSEH(soldierCore, soldierIndex);
        const bool aboard = (record & (kRecordInVehicle | kRecordOnWalkerGear)) != 0 && IsSoldierStanding(soldierCore, soldierIndex);
        SetRideMount(soldierIndex, aboard ? ReadRideMountSEH(soldierCore, soldierIndex, record) : kNoMount);
    }


    bool HeliInNoticeRange(const void* soldierCore, std::uint32_t soldierIndex, std::uint16_t heliId, std::uint64_t now)
    {
        SoldierPoint self{};
        SoldierPoint heli{};
        return ReadSoldierPoint(soldierCore, soldierIndex, &self) && ReadHeliPoint(heliId, now, &heli)
            && HorizontalDistanceSq(self, heli) <= kHeliNoticeRadiusSq;
    }

    std::uint16_t PickSensedHeli(const void* soldierCore, std::uint32_t soldierIndex, std::uint64_t now)
    {
        SoldierPoint self{};
        if (!ReadSoldierPoint(soldierCore, soldierIndex, &self))
            return kNoRival;
        const std::uint16_t soldierId = static_cast<std::uint16_t>(0x400u | soldierIndex);
        std::uint16_t best       = kNoRival;
        float         bestDistSq = 0.0f;
        const std::uint32_t count = g_factionHeliCount.load(std::memory_order_acquire);
        for (std::uint32_t i = 0; i < count && i < kMaxFactionHelis; ++i)
        {
            const std::uint16_t heliId = g_factionHelis[i].load(std::memory_order_relaxed);
            SoldierPoint heli{};
            if (!AreHostileIds(soldierId, heliId) || !ReadHeliPoint(heliId, now, &heli))
                continue;
            const float distSq = HorizontalDistanceSq(self, heli);
            if (distSq > kHeliNoticeRadiusSq || (best != kNoRival && distSq >= bestDistSq))
                continue;
            best       = heliId;
            bestDistSq = distSq;
        }
        return best;
    }

    void RememberFactionHeli(std::uint16_t heliId)
    {
        std::lock_guard<std::mutex> lk(g_namesMtx);
        const std::uint32_t count = g_factionHeliCount.load(std::memory_order_relaxed);
        for (std::uint32_t i = 0; i < count; ++i)
        {
            if (g_factionHelis[i].load(std::memory_order_relaxed) == heliId)
                return;
        }
        if (count >= kMaxFactionHelis)
        {
            LogWarn("SetFaction: more than %u helis have a faction, heli 0x%04X is ignored by soldiers", kMaxFactionHelis, heliId);
            return;
        }
        g_factionHelis[count].store(heliId, std::memory_order_relaxed);
        g_factionHeliCount.store(count + 1, std::memory_order_release);
    }

    std::uint32_t RefreshHelisSEH(const std::uint8_t* flightController, std::uint64_t now, std::uint8_t** heliOut,
                                  std::uint16_t* published, std::uint32_t capacity)
    {
        std::uint32_t publishedCount = 0;
        __try
        {
            std::uint8_t* heli = *reinterpret_cast<std::uint8_t* const*>(flightController + kFlightControllerHeli);
            if (!heli)
                return 0;
            const std::uint16_t type = *reinterpret_cast<const std::uint16_t*>(heli + kHeliType);
            if (type < kEnemyHeliCategory || type > HeliKey::kLastType)
                return 0;
            const std::uint8_t* shared    = *reinterpret_cast<const std::uint8_t* const*>(heli + kHeliShared);
            const std::uint8_t* positions = *reinterpret_cast<const std::uint8_t* const*>(shared + kHeliPositions);
            const std::uint32_t count     = *reinterpret_cast<const std::uint32_t*>(shared + kHeliCount);
            const std::uint8_t* lives     = *reinterpret_cast<const std::uint8_t* const*>(*reinterpret_cast<const std::uint8_t* const*>(heli + kHeliLives) + 8);
            const std::uint8_t* states    = *reinterpret_cast<const std::uint8_t* const*>(*reinterpret_cast<const std::uint8_t* const*>(heli + kHeliStates) + 8);
            if (!positions || !lives || !states)
                return 0;
            *heliOut = heli;
            for (std::uint32_t i = 0; i < count && i <= kSoldierIndexMask; ++i)
            {
                const std::uint32_t key = HeliKey::Of(type, i);
                if (g_heliFaction[key].load(std::memory_order_relaxed) == kNoFaction)
                    continue;
                const std::int32_t  life  = *reinterpret_cast<const std::int32_t*>(lives + i * kHeliLifeStride);
                const std::uint64_t state = *reinterpret_cast<const std::uint64_t*>(states + i * kHeliStateStride);
                if (life <= 0 || (state & kHeliStateRealized) == 0)
                    continue;
                const float* point = reinterpret_cast<const float*>(positions + i * kHeliPositionStride);
                HeliSeen& seen = g_heliSeen[key];
                seen.x.store(point[0], std::memory_order_relaxed);
                seen.y.store(point[1], std::memory_order_relaxed);
                seen.z.store(point[2], std::memory_order_relaxed);
                seen.ms.store(now != 0 ? now : 1, std::memory_order_release);
                if (publishedCount < capacity)
                    published[publishedCount++] = static_cast<std::uint16_t>(i);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
        return publishedCount;
    }
}

void Set_SoldierFaction(std::uint32_t gameObjectId, const char* factionName)
{
    if (!g_moduleActive.load(std::memory_order_relaxed))
    {
        static std::atomic<bool> warned{ false };
        if (!warned.exchange(true))
            LogWarn("SetFaction: the SoldierFaction module is not installed (disabled or unsupported build), soldiers keep vanilla behaviour");
        return;
    }

    const std::uint32_t slot = gameObjectId & kSoldierIndexMask;
    const std::string name = factionName ? factionName : "";

    const std::uint16_t factionId = name.empty() ? kNoFaction : InternFactionName(name);
    if (factionId != kNoFaction)
        g_anyFaction.store(true, std::memory_order_relaxed);
    const std::uint16_t previous = g_faction[slot].exchange(factionId, std::memory_order_relaxed);
}

void Set_HeliFaction(std::uint32_t heliKey, const char* factionName)
{
    if (!g_moduleActive.load(std::memory_order_relaxed))
    {
        static std::atomic<bool> warned{ false };
        if (!warned.exchange(true))
            LogWarn("SetFaction: the SoldierFaction module is not installed (disabled or unsupported build), helis keep vanilla behaviour");
        return;
    }
    if (heliKey >= HeliKey::kCount)
        return;

    const std::uint16_t heliId = static_cast<std::uint16_t>(HeliKey::GameObjectIdOf(heliKey));
    const std::string name = factionName ? factionName : "";
    const std::uint16_t factionId = name.empty() ? kNoFaction : InternFactionName(name);
    if (factionId != kNoFaction)
    {
        g_anyFaction.store(true, std::memory_order_relaxed);
        RememberFactionHeli(heliId);
    }
    const std::uint16_t previous = g_heliFaction[heliKey].exchange(factionId, std::memory_order_relaxed);
}

void SoldierFaction_RefreshHelis(void* flightController)
{
    if (!flightController || !g_anyFaction.load(std::memory_order_relaxed) || g_factionHeliCount.load(std::memory_order_relaxed) == 0)
        return;
    const std::uint64_t now = GetTickCount64();
    std::uint8_t* heli = nullptr;
    std::uint16_t published[kMaxFactionHelis];
    const std::uint32_t count = RefreshHelisSEH(static_cast<const std::uint8_t*>(flightController), now, &heli,
                                                published, kMaxFactionHelis);
    for (std::uint32_t i = 0; i < count; ++i)
        SoldierFactionHeli_Update(heli, published[i], now);
}

bool SoldierFaction_IsEnemyHeliId(std::uint16_t gameObjectId)
{
    return IsEnemyHeliId(gameObjectId);
}

bool SoldierFaction_HasFaction(std::uint16_t gameObjectId)
{
    return FactionOfId(gameObjectId) != kNoFaction;
}

bool SoldierFaction_AreHostileIds(std::uint16_t gameObjectId, std::uint16_t otherGameObjectId)
{
    return AreHostileIds(gameObjectId, otherGameObjectId);
}

void SoldierFaction_OnHeliDamages(std::uint16_t heliId, void* damages, std::uint32_t count)
{
    if (!g_anyFaction.load(std::memory_order_relaxed) || !damages || FactionOfId(heliId) == kNoFaction)
        return;
    std::uint8_t* damage = static_cast<std::uint8_t*>(damages);
    for (std::uint32_t i = 0; i < count; ++i, damage += kDamageStride)
    {
        std::uint16_t attacker = 0;
        std::uint16_t attackId = 0;
        std::memcpy(&attacker, damage + kDamageAttacker, sizeof(attacker));
        std::memcpy(&attackId, damage + kDamageAttackId, sizeof(attackId));
        const std::uint16_t shooter = RiderOrSelf(attacker);
        if (AreHostileIds(shooter, heliId))   // fights last: the heli handler zeroes its life on this bit
            damage[kDamageFlags] = static_cast<std::uint8_t>(damage[kDamageFlags] & ~kDamageFlagInstantKill);
        SoldierFactionHeli_OnHit(heliId, shooter, attackId);
    }
}

void Clear_AllSoldierFactions()
{
    g_anyFaction.store(false, std::memory_order_relaxed);
    g_soldierCore.store(nullptr, std::memory_order_relaxed);
    SoldierFactionHeli_Clear();
    SoldierFactionAntiAir_Clear();
    for (std::uint32_t i = 0; i < kSoldierSlotCount; ++i)
    {
        g_faction[i].store(kNoFaction, std::memory_order_relaxed);
        g_rival[i].store(kNoRival, std::memory_order_relaxed);
        g_pendingRival[i].store(kNoRival, std::memory_order_relaxed);
        g_mount[i].store(kNoMount, std::memory_order_relaxed);
        g_neutralizedBy[i].store(kNoRival, std::memory_order_relaxed);
        g_rivalHitMs[i].store(0, std::memory_order_relaxed);
        g_taint[i].store(0, std::memory_order_relaxed);
        g_downMs[i].store(0, std::memory_order_relaxed);
        g_corpseOwner[i].store(kNoBody, std::memory_order_relaxed);
        g_corpseOwnerMs[i].store(0, std::memory_order_relaxed);
        g_rideMount[i].store(kNoMount, std::memory_order_relaxed);
        g_nextRideCheckMs[i].store(0, std::memory_order_relaxed);
    }
    for (std::uint32_t i = 0; i < kGameObjectIdCount; ++i)
        g_mountRider[i].store(kNoMount, std::memory_order_relaxed);
    for (std::uint32_t i = 0; i < HeliKey::kCount; ++i)
    {
        g_heliFaction[i].store(kNoFaction, std::memory_order_relaxed);
        g_heliSeen[i].ms.store(0, std::memory_order_relaxed);
    }
    std::lock_guard<std::mutex> lk(g_namesMtx);
    g_factionHeliCount.store(0, std::memory_order_relaxed);
    g_factionNames.clear();
}

void SoldierFaction_SetModuleActive(bool active)
{
    g_moduleActive.store(active, std::memory_order_relaxed);
    if (!active)
        Clear_AllSoldierFactions();
}

bool SoldierFaction_AnyFaction()
{
    return g_anyFaction.load(std::memory_order_relaxed);
}

bool SoldierFaction_IsModuleActive()
{
    return g_moduleActive.load(std::memory_order_relaxed);
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

bool SoldierFaction_ReadSoldierTarget(std::uint16_t soldierId, float* position, bool* standing, bool* riding)
{
    const void* soldierCore = g_soldierCore.load(std::memory_order_relaxed);
    if (!soldierCore || !IsSoldierId(soldierId))
        return false;
    const std::uint32_t soldierIndex = soldierId & kSoldierIndexMask;
    SoldierPoint point{};
    if (!ReadSoldierPoint(soldierCore, soldierIndex, &point))
        return false;
    position[0] = point.x;
    position[1] = point.y;
    position[2] = point.z;
    *standing = IsSoldierStanding(soldierCore, soldierIndex);
    *riding   = g_rideMount[soldierIndex].load(std::memory_order_relaxed) != kNoMount;
    return true;
}

bool SoldierFaction_ReadHeliPoint(std::uint16_t heliId, float* position)
{
    SoldierPoint point{};
    if (!IsEnemyHeliId(heliId) || !ReadHeliPoint(heliId, GetTickCount64(), &point))
        return false;
    position[0] = point.x;
    position[1] = point.y;
    position[2] = point.z;
    return true;
}

std::uint32_t SoldierFaction_FactionHeliCount()
{
    const std::uint32_t count = g_factionHeliCount.load(std::memory_order_acquire);
    return count < kMaxFactionHelis ? count : kMaxFactionHelis;
}

std::uint16_t SoldierFaction_FactionHeliAt(std::uint32_t index)
{
    return index < kMaxFactionHelis ? g_factionHelis[index].load(std::memory_order_relaxed) : kNoRival;
}

void SoldierFaction_SetGunOperator(std::uint16_t gunId, std::uint16_t soldierId)
{
    g_mountRider[gunId].store(IsSoldierId(soldierId) ? soldierId : kNoMount, std::memory_order_relaxed);   // its bullets count as his
}

bool SoldierFaction_PushNotice(void* noticeController, std::uint32_t soldierIndex, std::uint8_t type, std::uint16_t targetId)
{
    return noticeController && PushNotice(noticeController, soldierIndex, type, targetId);
}

static bool RunSightCheck(void* noticeController, std::uint32_t soldierIndex, std::uint64_t now)
{
    if (soldierIndex >= kSoldierSlotCount)
        return false;
    if (FactionOf(soldierIndex) == kNoFaction)
    {
        SoldierFactionAntiAir_Forget(soldierIndex, "soldier has no faction");
        if (g_rideMount[soldierIndex].load(std::memory_order_relaxed) != kNoMount)
            SetRideMount(soldierIndex, kNoMount);
        if (g_rival[soldierIndex].load(std::memory_order_relaxed) != kNoRival)
            SoldierFaction_EndRivalFight(soldierIndex, "soldier has no faction");
        return false;
    }

    const void* soldierCore = nullptr;
    std::memcpy(&soldierCore, static_cast<const std::uint8_t*>(noticeController) + kNoticeControllerSoldierCore,
                sizeof(soldierCore));
    if (!soldierCore)
        return false;
    g_soldierCore.store(soldierCore, std::memory_order_relaxed);

    UpdateRideMount(soldierCore, soldierIndex, now);

    const std::uint16_t current = g_rival[soldierIndex].load(std::memory_order_relaxed);
    if (SoldierFactionAntiAir_OnSightCheck(noticeController, soldierCore, soldierIndex, now))   // on an anti-air gun, not in a rival fight
    {
        g_pendingRival[soldierIndex].store(kNoRival, std::memory_order_relaxed);
        if (current != kNoRival)
            SoldierFaction_EndRivalFight(soldierIndex, "soldier mans an anti-air gun");
        return false;
    }
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
        const bool sensed = IsEnemyHeliId(current)
            ? HeliInNoticeRange(soldierCore, soldierIndex, current, now)
            : RivalInSight(current, entries, count) || RivalSensed(soldierCore, soldierIndex, current & kSoldierIndexMask, now);
        if (sensed)
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
    std::uint32_t sightCount = 0;
    const char* how = "fights back against";
    std::uint16_t rival = TakePendingRival(soldierCore, soldierIndex);
    if (rival == kNoRival)
    {
        if (now < g_nextScanMs[soldierIndex].load(std::memory_order_relaxed))
            return false;
        g_nextScanMs[soldierIndex].store(now + kScanIntervalMs, std::memory_order_relaxed);
        scanned = true;

        sightCount = ReadSightEntries(soldierCore, soldierIndex, entries);
        how = "spotted";
        rival = PickRival(soldierCore, soldierIndex, entries, sightCount);
        if (rival == kNoRival)
        {
            how = "sensed";
            rival = PickSensedRival(soldierCore, soldierIndex, now);
        }
        if (rival == kNoRival)
            rival = PickSensedHeli(soldierCore, soldierIndex, now);
    }
    if (rival == kNoRival || !PushWalkerGearNotice(noticeController, soldierIndex, rival))
        return scanned;

    g_rivalSeenMs[soldierIndex].store(now, std::memory_order_relaxed);
    g_rivalAliveMs[soldierIndex].store(now, std::memory_order_relaxed);
    g_rivalNodeWoke[soldierIndex].store(false, std::memory_order_relaxed);
    g_rival[soldierIndex].store(rival, std::memory_order_relaxed);
    g_nextRepushMs[soldierIndex].store(now + kNoticeRepushMs, std::memory_order_relaxed);

    if (IsSoldierId(rival))
        AskRivalToFightBack(rival & kSoldierIndexMask, static_cast<std::uint16_t>(0x400u | soldierIndex));
    return scanned;
}

void SoldierFaction_OnSightCheck(void* noticeController, std::uint32_t soldierIndex)
{
    if (!g_anyFaction.load(std::memory_order_relaxed) || !noticeController)
        return;

    RunSightCheck(noticeController, soldierIndex, GetTickCount64());
}

void SoldierFaction_WidenBulletRequest(void* bullet3, std::uint32_t requestIndex)
{
    if (!g_anyFaction.load(std::memory_order_relaxed) || !bullet3 || MissionCodeGuard::ShouldBypassHooks())
        return;

    std::uint16_t owner = 0;
    if (!WidenFactionBulletSEH(bullet3, requestIndex, &owner))
        return;

    const std::uint16_t shooter = RiderOrSelf(owner);
    if (!IsSoldierId(shooter))
        return;
    const std::uint32_t shooterIndex = shooter & kSoldierIndexMask;
    if (g_mount[shooterIndex].load(std::memory_order_relaxed) == kNoMount)
        return;
    if (g_rival[shooterIndex].load(std::memory_order_relaxed) != kNoRival)
        g_lastShotMs[shooterIndex].store(GetTickCount64(), std::memory_order_relaxed);
}

void SoldierFaction_SetMount(std::uint32_t soldierIndex, std::uint16_t mountId)
{
    if (soldierIndex >= kSoldierSlotCount)
        return;
    if (mountId == kNoMountInNode || IsSoldierId(mountId))
        mountId = kNoMount;
    if (g_mount[soldierIndex].load(std::memory_order_relaxed) == mountId)
    {
        if (mountId != kNoMount)   // a second rider of the same vehicle may have cleared it when his fight ended
            PublishRider(mountId, static_cast<std::uint16_t>(0x400u | soldierIndex));
        return;
    }

    ClearMount(soldierIndex);
    if (mountId == kNoMount)
        return;
    g_mount[soldierIndex].store(mountId, std::memory_order_relaxed);
    g_mountRider[mountId].store(static_cast<std::uint16_t>(0x400u | soldierIndex), std::memory_order_relaxed);
}

std::uint16_t SoldierFaction_RiderOrSelf(std::uint16_t gameObjectId)
{
    return RiderOrSelf(gameObjectId);
}

bool SoldierFaction_IsMountId(std::uint16_t gameObjectId)
{
    const std::uint16_t category = gameObjectId >> 9;
    return category == kCommonWalkerGearCategory || category == kVehicleCategory;
}

bool SoldierFaction_OnMountShot(std::uint16_t shooterId, std::uint16_t mountId, std::uint64_t targetGroups, bool vanillaHit)
{
    if (!g_anyFaction.load(std::memory_order_relaxed) || !IsSoldierId(shooterId) || !SoldierFaction_IsMountId(mountId))
        return false;

    const std::uint32_t shooterIndex = shooterId & kSoldierIndexMask;
    const std::uint16_t mapped       = g_mountRider[mountId].load(std::memory_order_relaxed);
    bool hostile = false;
    for (std::uint32_t i = 0; i < kSoldierSlotCount; ++i)
    {
        const bool aboard = g_rideMount[i].load(std::memory_order_relaxed) == mountId
            || (IsSoldierId(mapped) && (mapped & kSoldierIndexMask) == i);
        if (!aboard || i == shooterIndex)
            continue;
        if (!AreHostile(shooterIndex, i))
            continue;
        hostile = true;
        AskRivalToFightBack(i, shooterId);
    }
    return hostile;
}

void SoldierFaction_BeginShotScope(std::uint32_t shooterIndex, std::uint16_t rivalId)
{
    t_shotScope.shooterIndex = shooterIndex & kSoldierIndexMask;
    t_shotScope.rivalIndex   = IsSoldierId(rivalId) ? (rivalId & kSoldierIndexMask) : kSoldierSlotCount;   // a heli rival is no soldier slot
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
        if (!self && AreHostileIds(victimId, source))   // a hostile soldier, or a hostile faction heli
        {
            damage[kDamageFlags] = static_cast<std::uint8_t>(damage[kDamageFlags] & ~kDamageFlagInstantKill);
            AskRivalToFightBack(victimIndex, source);
            if (!down)
                rivalCause = source;
            continue;
        }
        if (!self && IsSoldierId(source) && (down || IsSoldierHarmlessAttack(attackId)))
            continue;
        TaintNeutralization(victimIndex, source, attackId);
    }

    if (soldierCore && IsHeldUp(soldierCore, victimIndex))
        TaintNeutralization(victimIndex, kTaintHoldUp, 0);
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
            TaintNeutralization(soldierIndex, kTaintHoldUp, 0);

        const std::uint64_t now = GetTickCount64();
        const std::uint16_t causer = g_neutralizedBy[soldierIndex].load(std::memory_order_relaxed);
        const bool rivalCause = IsSoldierId(causer) || IsEnemyHeliId(causer);
        if (rivalCause && now - g_rivalHitMs[soldierIndex].load(std::memory_order_relaxed) > kCauseFreshMs)
        {
            g_neutralizedBy[soldierIndex].store(kCauseStale, std::memory_order_relaxed);
        }
        g_downMs[soldierIndex].store(now != 0 ? now : 1, std::memory_order_release);
        return;
    }

    if (oldStatus != kLifeStatusAlive && status == kLifeStatusAlive)
        ResetNeutralization(soldierIndex);
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

    const std::uint64_t now = GetTickCount64();
    if (IsEnemyHeliId(rival))
    {
        SoldierPoint self{};
        SoldierPoint heli{};
        if (!AreHostileIds(static_cast<std::uint16_t>(0x400u | soldierIndex), rival))
            return "heli is no longer hostile";
        if (!ReadHeliPoint(rival, now, &heli))
            return "heli is gone, destroyed or unrealized";
        if (!ReadSoldierPoint(soldierCore, soldierIndex, &self) || HorizontalDistanceSq(self, heli) > kHeliFightRangeSq)
            return "heli is more than 150 m away";
        if (now - g_rivalSeenMs[soldierIndex].load(std::memory_order_relaxed) > kRivalLostMs)
            return "heli out of range for 30 s";
        return nullptr;
    }

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
    if (IsEnemyHeliId(rivalId))
    {
        if (!ReadHeliPoint(rivalId, GetTickCount64(), &rival))
            return false;
        aimPoint[0] = rival.x;
        aimPoint[1] = rival.y;
        aimPoint[2] = rival.z;
        return true;
    }
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
    const bool heli = IsEnemyHeliId(rivalId);
    if (heli ? !ReadHeliPoint(rivalId, GetTickCount64(), &rival) : !ReadSoldierPoint(soldierCore, rivalId & kSoldierIndexMask, &rival))
        return false;

    const float dx = shotTarget[0] - rival.x;
    const float dz = shotTarget[2] - rival.z;
    const float dy = shotTarget[1] - rival.y;
    const float radiusSq = heli ? kShotAtHeliRadiusSq : kShotAtRivalRadiusSq;
    const float height   = heli ? kShotAtHeliHeight : kShotAtRivalHeight;
    return dx * dx + dz * dz <= radiusSq && dy > -height && dy < height;
}

void SoldierFaction_EndRivalFight(std::uint32_t soldierIndex, const char* reason)
{
    if (soldierIndex >= kSoldierSlotCount)
        return;
    ClearMount(soldierIndex);
    g_rival[soldierIndex].store(kNoRival, std::memory_order_relaxed);
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
    return true;
}

bool SoldierFaction_IsRivalNotice(std::uint32_t soldierIndex, std::uint8_t noticeType, const void* notice)
{
    if ((noticeType != kNoticeTypeWalkerGear && noticeType != kNoticeTypeAntiAir) || !notice || !ObserverHasFaction(soldierIndex))
        return false;
    std::uint16_t target = kNoRival;
    std::memcpy(&target, static_cast<const std::uint8_t*>(notice) + kNoticeTarget, sizeof(target));
    return (IsSoldierId(target) || IsEnemyHeliId(target)) && AreHostileIds(static_cast<std::uint16_t>(0x400u | soldierIndex), target);
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
