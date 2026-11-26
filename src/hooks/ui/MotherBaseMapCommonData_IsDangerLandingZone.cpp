#include <pch.h>
#include "MotherBaseMapCommonData_IsDangerLandingZone.h"

#include <Windows.h>
#include <intrin.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "FeatureSwitch.h"
#include "V_FrameWorkState.h"
#include "log.h"
#include "../fox/gm/GameObjectInstance.h"
#include "../soldier/SoldierCommandPost.h"

namespace
{
    using GetCommonDataManager_t  = const std::uint8_t*(__fastcall*)();
    using GetQuarkSystemTable_t   = std::uint8_t*(__fastcall*)();
    using IsDangerLandingZone_t   = bool(__fastcall*)(void* mapData, std::uint32_t lzName);
    using IsDangerLandPoint_t     = bool(__fastcall*)(void* mapData, std::uint32_t landPoint);
    using InsertNearestLzMarker_t = void(__fastcall*)(void* nearest, float distanceSquared, std::uint32_t lzIndex, std::uint32_t flags,
                                                      std::uint32_t iconKind, std::uint8_t iconFlag);
    using RegisterMarker_t        = std::uint8_t(__fastcall*)(void* markers, std::uint32_t flags);

    constexpr std::size_t  kManagerMapData        = 0x78;    // UiCommonDataManager -> MotherBaseMapCommonData
    constexpr std::size_t  kSlotIsDangerLandPoint = 0x278 / sizeof(void*);
    constexpr std::size_t  kSlotAddDangerLz       = 0x280 / sizeof(void*);
    constexpr std::size_t  kSlotIsDangerLz        = 0x290 / sizeof(void*);
    constexpr std::size_t  kDangerLandPointCount  = 0x1194;  // byte
    constexpr std::size_t  kDangerLandPointNames  = 0x1198;  // one name hash per drop point RegisterDangerLandPointTable got
    constexpr std::size_t  kDangerLzCount         = 0x1298;  // byte
    constexpr std::size_t  kDangerLzNames         = 0x129C;  // one name hash per LZ RegisterDangerLandingZoneTable got
    constexpr int          kMaxDangerLzs          = 0x40;
    constexpr std::size_t  kSelectedLzSafety      = 0xA4;    // SetSelectedLandingZone: 0 Danger Low, 1 Danger Moderate, 2 Danger High
    constexpr std::uint8_t kSafetyModerate        = 1;
    constexpr std::uint8_t kReturnFalseBytes[]    = { 0x32, 0xC0, 0xC3 };   // xor al, al / ret
    constexpr std::uint8_t kAddDangerLzBytes[]    = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x01, 0x8B, 0xFA, 0x48,
        0x8B, 0xD9, 0xFF, 0x90, 0x90, 0x02, 0x00, 0x00, 0x84, 0xC0, 0x75, 0x1B, 0x0F, 0xB6, 0x83, 0x98,
        0x12, 0x00, 0x00, 0x3C, 0x40
    };
    constexpr std::uint8_t kIsDangerLandPointBytes[] = {
        0x44, 0x0F, 0xB6, 0x89, 0x94, 0x11, 0x00, 0x00, 0x45, 0x33, 0xC0, 0x45, 0x85, 0xC9, 0x74, 0x17,
        0x48, 0x8D, 0x81, 0x98, 0x11, 0x00, 0x00, 0x39, 0x10, 0x74, 0x0F, 0x41, 0xFF, 0xC0, 0x48, 0x83,
        0xC0, 0x04, 0x45, 0x3B, 0xC1, 0x72, 0xF0, 0x32, 0xC0, 0xC3, 0xB0, 0x01, 0xC3
    };

    constexpr std::uint8_t kInsertNearestLzMarkerEntry[] = {
        0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x41, 0x8B, 0xF9, 0x41, 0x8B, 0xF0, 0x4C, 0x8B, 0xD1, 0x33,
        0xD2, 0x48, 0x8B, 0xC1, 0x0F, 0x2F, 0x08, 0x72
    };
    constexpr std::uint8_t kRegisterMarkerEntry[] = {
        0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x80,
        0x79, 0x0C, 0x80, 0x8B, 0xEA, 0x48, 0x8B, 0xF9, 0x72, 0x04, 0xC6, 0x41, 0x0C, 0x00, 0x0F, 0xB6
    };
    constexpr std::size_t  kLandPointDangerCall = 0x17E;   // RegisterMapIconSelectMode_LandPoint asks IsDangerLandPoint here, then registers that drop point's marker
    constexpr std::uint8_t kLandPointDangerCallBytes[] = {
        0xFF, 0x93, 0x78, 0x02, 0x00, 0x00,   // call [rbx + 0x278]
        0x48, 0x8B, 0x5C, 0x24, 0x70,         // mov rbx, [rsp + 0x70]
        0xBA, 0x08, 0x00, 0x02, 0x00,         // mov edx, 0x20008
        0x84, 0xC0,                           // test al, al
        0xB8, 0x08, 0x00, 0x04, 0x00          // mov eax, 0x40008
    };
    constexpr std::size_t   kLandPointDangerCallSize = 6;
    constexpr std::uint32_t kMarkerDangerLow  = 1u << 17;
    constexpr std::uint32_t kMarkerDangerHigh = 1u << 18;   // neither bit = Danger Moderate

    constexpr std::size_t   kQuarkApplication       = 0x98;
    constexpr std::size_t   kApplicationLzTable     = 0xC8;   // the heli LZ table RegisterLZMarkerInUpdate walks
    constexpr std::size_t   kApplicationCommandPost = 0x1C8;  // CommandPost2Impl + 0x20
    constexpr std::size_t   kLzEntries              = 0x10;   // 0x30 per LZ, position first
    constexpr std::size_t   kLzEntrySize            = 0x30;
    constexpr std::size_t   kLzName                 = 0x18;
    constexpr std::size_t   kLzCount                = 0xC2C;
    constexpr int           kMaxLzs                 = 0x40;

    constexpr std::size_t   kCommandPostInterface   = 0x20;
    constexpr std::size_t   kCommandPostExecution   = 0x28;   // the part whose first virtual is ExecuteSerially
    constexpr std::size_t   kCommandPostCount       = 0x198;
    constexpr std::size_t   kOutpostRecords         = 0x68;
    constexpr std::size_t   kOutpostRecordSize      = 0x60;
    constexpr std::size_t   kOutpostRecordFlags     = 0x54;
    constexpr std::uint32_t kOutpostHasSoldiers     = 0x20;   // UpdateCpAtHost clears it when every member is down, sets it again when one is back
    constexpr std::uint32_t kOutpostIndexMask       = 0x1FF;
    constexpr std::uint16_t kCommandPostIds         = 0x200;
    constexpr std::uint16_t kNoGameObject           = 0xFFFF;
    constexpr std::size_t   kExecuteSeriallyBody    = 0x14;
    constexpr std::uint8_t  kExecuteSeriallyBodyBytes[] = { 0xF6, 0x86, 0x86, 0x01, 0x00, 0x00, 0x01, 0x48, 0x8B, 0x68, 0x40 };   // test byte [rsi + 0x186], 1 / mov rbp, [rax + 0x40]
    constexpr std::size_t   kUpdateCpCall           = 0x1F0;  // ExecuteSerially hands UpdateCpAtHost its object minus 0x28 here
    constexpr std::uint8_t  kUpdateCpCallBytes[]    = { 0x48, 0x8D, 0x4E, 0xD8 };   // lea rcx, [rsi - 0x28]
    constexpr int           kOutpostCheckTries      = 5;

    constexpr std::uint32_t kFirstSoldierId     = 0x400;
    constexpr std::size_t   kSoldier2Part       = 0x20;   // FindSoldier's Soldier2Impl; the CP lookup and transforms hang off this part
    constexpr std::size_t   kPartSoldierCount   = 0x08;
    constexpr std::size_t   kPartTransforms     = 0x18;
    constexpr std::size_t   kTransformPositions = 0x20;   // one Vector4 per soldier
    constexpr std::uint32_t kMaxSoldiers        = 0x200;
    constexpr float         kOutpostReach       = 150.0f; // the outpost with the most members this close to an in-base LZ owns it

    constexpr std::size_t   kHandleGameObject   = 0x20;   // GatlingGunGameObject: 64 blocks of anti-air guns
    constexpr std::size_t   kBlockStride        = 0x20;
    constexpr std::size_t   kBlockUnits         = 0x20;
    constexpr std::size_t   kBlockUnitCount     = 0x28;
    constexpr std::size_t   kBlockState         = 0x38;
    constexpr std::size_t   kBlockStateLoading  = 0x94;   // the game skips a block while this is above 0
    constexpr std::size_t   kLoadedBlocks       = 0x820;
    constexpr int           kGunBlocks          = 0x40;
    constexpr std::size_t   kGunUnitSize        = 0x2F0;
    constexpr std::size_t   kGunUnitId          = 0x60;   // dataSet path hash << 32 | locator hash, what Gimmick.IsBrokenGimmick looks up
    constexpr std::size_t   kGunUnitState       = 0x68;
    constexpr std::uint32_t kGunUnitBroken      = 1u << 29;   // what the unit's own IsBroken reads
    constexpr std::size_t   kGunUnitModel       = 0x78;
    constexpr std::size_t   kModelBoneMatrices  = 0xE0;
    constexpr std::size_t   kBoneTranslation    = 0x30;
    constexpr std::uint32_t kMaxGunsPerBlock    = 0x100;
    constexpr float         kAntiAirReach       = 250.0f; // a gun belongs to the nearest in-base LZ this close

    constexpr int           kSavedBlocks        = 8;      // the save's gimmick states: 8 blocks of 32
    constexpr std::size_t   kSavedBlockSize     = 0x148;
    constexpr std::size_t   kSavedStates        = 0x100;  // one u16 per id
    constexpr std::size_t   kSavedUsed          = 0x144;
    constexpr std::uint32_t kSavedBlockRows     = 32;
    constexpr std::uint16_t kSavedKindShift     = 11;
    constexpr std::uint16_t kSavedKindGatling   = 4;
    constexpr std::uint16_t kSavedBrokenBits    = 0xC;    // 4 or 8 = broken, as IsBrokenInSaveData reads it
    constexpr std::uint16_t kSavedBroken        = 4;
    constexpr std::uint16_t kSavedBrokenForGood = 8;

    constexpr ULONGLONG     kUpdateMs           = 1000;
    constexpr ULONGLONG     kOutpostRetryMs     = 3000;
    constexpr int           kMaxGunsPerBase     = 16;
    constexpr int           kMaxLoadedGuns      = 256;
    constexpr int           kMaxGunChanges      = 64;
    constexpr char          kKnownGunSpace[]    = "LandingZoneGuns";

    enum : int { kOff, kWatching, kBusy, kSwapped };
    enum : int { kOutpostUnchecked, kOutpostKnown, kOutpostUnknown };
    enum : std::uint8_t { kAntiAirUnseen, kAntiAirStanding, kAntiAirAllBroken };
    enum : std::uint8_t { kNotLogged, kLoggedOccupied, kLoggedCleared };

    struct InBaseLandingZone
    {
        std::uint32_t name;                      // RegisterDangerLandingZoneTable's row
        std::uint32_t landPoint;                 // the same row of RegisterDangerLandPointTable
        std::uint64_t gunIds[kMaxGunsPerBase];   // every anti-air gun seen at this base, kept in the state file
        int           gunCount;
        ULONGLONG     nextOutpostTry;
        std::uint16_t outpostId;
        bool          listed;
        bool          cleared;
        std::uint8_t  antiAir;
        std::uint8_t  loggedAntiAir;
        std::uint8_t  loggedCleared;
    };

    struct ZoneTable
    {
        InBaseLandingZone zones[kMaxDangerLzs];
        int               count;
    };

    struct ZoneDanger
    {
        bool cleared;
        bool antiAirAllBroken;
    };

    struct LoadedGun
    {
        std::uint64_t id;
        float         position[3];
        bool          placed;
        bool          stateKnown;
        bool          broken;
    };

    struct GunChange
    {
        std::uint32_t lzName;
        std::uint64_t gunId;
        bool          known;
    };

    std::atomic<int>        g_State{ kOff };
    std::atomic<int>        g_OutpostObject{ kOutpostUnchecked };
    int                     g_OutpostChecks          = 0;
    void**                  g_Vtable                 = nullptr;
    bool                    g_LandingZoneSlotSwapped = false;
    bool                    g_LandPointSlotSwapped   = false;
    IsDangerLandingZone_t   g_OrigIsDanger           = nullptr;
    IsDangerLandPoint_t     g_OrigIsDangerLandPoint  = nullptr;
    InsertNearestLzMarker_t g_OrigInsertMarker       = nullptr;
    RegisterMarker_t        g_OrigRegisterMarker     = nullptr;
    void*                   g_InsertTarget           = nullptr;
    void*                   g_RegisterMarkerTarget   = nullptr;
    const void*             g_LandPointCaller        = nullptr;
    SRWLOCK                 g_Lock                   = SRWLOCK_INIT;
    ZoneTable               g_Zones                  = {};
    ULONGLONG               g_NextUpdate             = 0;
    std::uint16_t           g_ZonesMission           = 0;
    bool                    g_KnownGunsLoaded        = false;
    GunChange               g_SavedGuns[kMaxDangerLzs * kMaxGunsPerBase] = {};
    int                     g_SavedGunCount          = 0;
    std::atomic<std::uint32_t> g_LoggedLandPoint{ 0 };
    thread_local bool          t_ModerateLandPoint     = false;
    thread_local std::uint32_t t_ModerateLandPointName = 0;

    bool Matches(const void* at, const std::uint8_t* bytes, const std::uint8_t* mask, std::size_t size)
    {
        std::uint8_t read[64] = {};
        if (!at || size > sizeof(read) || !ReadEntryBytesSEH(at, read, size))
            return false;
        for (std::size_t i = 0; i < size; ++i)
        {
            if ((!mask || mask[i]) && read[i] != bytes[i])
                return false;
        }
        return true;
    }

    const std::uint8_t* MapDataSEH()
    {
        const auto getManager = reinterpret_cast<GetCommonDataManager_t>(ResolveGameAddress(gAddr.Ui_UiCommonDataManagerGetInstance));
        if (!getManager)
            return nullptr;
        __try
        {
            const std::uint8_t* manager = getManager();
            return manager ? *reinterpret_cast<const std::uint8_t* const*>(manager + kManagerMapData) : nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    bool InDangerListSEH(const void* mapData, std::uint32_t lzName)
    {
        __try
        {
            const std::uint8_t* data = static_cast<const std::uint8_t*>(mapData);
            int count = data[kDangerLzCount];
            if (count > kMaxDangerLzs)
                count = kMaxDangerLzs;
            const std::uint32_t* names = reinterpret_cast<const std::uint32_t*>(data + kDangerLzNames);
            for (int i = 0; i < count; ++i)
            {
                if (names[i] == lzName)
                    return true;
            }
            return false;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    int NameListSEH(const std::uint8_t* mapData, std::size_t countAt, std::size_t namesAt, std::uint32_t* names)
    {
        __try
        {
            int count = mapData[countAt];
            if (count > kMaxDangerLzs)
                count = kMaxDangerLzs;
            std::memcpy(names, mapData + namesAt, static_cast<std::size_t>(count) * sizeof(std::uint32_t));
            return count;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    const std::uint8_t* ApplicationPartSEH(std::size_t part)
    {
        const auto quarkTable = reinterpret_cast<GetQuarkSystemTable_t>(ResolveGameAddress(gAddr.Fox_GetQuarkSystemTable));
        if (!quarkTable)
            return nullptr;
        __try
        {
            const std::uint8_t* table = quarkTable();
            const std::uint8_t* application = table ? *reinterpret_cast<const std::uint8_t* const*>(table + kQuarkApplication) : nullptr;
            return application ? *reinterpret_cast<const std::uint8_t* const*>(application + part) : nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    bool LzNameAtSEH(const std::uint8_t* lzTable, std::uint32_t lzIndex, std::uint32_t& name)
    {
        __try
        {
            if (lzIndex >= lzTable[kLzCount] || lzIndex >= static_cast<std::uint32_t>(kMaxLzs))
                return false;
            name = *reinterpret_cast<const std::uint32_t*>(lzTable + kLzEntries + lzIndex * kLzEntrySize + kLzName);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool LzPositionSEH(const std::uint8_t* lzTable, std::uint32_t lzName, float position[3])
    {
        __try
        {
            int count = lzTable[kLzCount];
            if (count > kMaxLzs)
                count = kMaxLzs;
            for (int i = 0; i < count; ++i)
            {
                const std::uint8_t* lz = lzTable + kLzEntries + static_cast<std::size_t>(i) * kLzEntrySize;
                if (*reinterpret_cast<const std::uint32_t*>(lz + kLzName) != lzName)
                    continue;
                std::memcpy(position, lz, 3 * sizeof(float));
                return true;
            }
            return false;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    float FlatDistanceSquared(const float* a, const float* b)
    {
        const float dx = a[0] - b[0];
        const float dz = a[2] - b[2];
        return dx * dx + dz * dz;
    }

    void CountOutpostMembersSEH(const std::uint8_t* part, const float lz[3], std::uint16_t votes[256])
    {
        __try
        {
            std::uint32_t count = *reinterpret_cast<const std::uint32_t*>(part + kPartSoldierCount);
            if (count > kMaxSoldiers)
                count = kMaxSoldiers;
            const std::uint8_t* transforms = *reinterpret_cast<const std::uint8_t* const*>(part + kPartTransforms);
            const float* positions = transforms ? *reinterpret_cast<const float* const*>(transforms + kTransformPositions) : nullptr;
            if (!positions)
                return;
            for (std::uint32_t i = 0; i < count; ++i)
            {
                if (FlatDistanceSquared(positions + i * 4, lz) > kOutpostReach * kOutpostReach)
                    continue;
                const std::uint8_t cp = SoldierCommandPost::IndexOf(part, i);
                if (cp != SoldierCommandPost::kNone)
                    ++votes[cp];
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    std::uint16_t OutpostNear(const float lz[3])
    {
        const std::uint8_t* soldiers = GameObjectInstance::FindSoldier(kFirstSoldierId).object;
        if (!soldiers)
            return kNoGameObject;
        std::uint16_t votes[256] = {};
        CountOutpostMembersSEH(soldiers + kSoldier2Part, lz, votes);
        int best = -1;
        for (int cp = 0; cp < 256; ++cp)
        {
            if (votes[cp] && (best < 0 || votes[cp] > votes[best]))
                best = cp;
        }
        return best < 0 ? kNoGameObject : static_cast<std::uint16_t>(kCommandPostIds | best);
    }

    const std::uint8_t* CommandPosts()
    {
        const std::uint8_t* commandPostInterface = ApplicationPartSEH(kApplicationCommandPost);
        return commandPostInterface ? commandPostInterface - kCommandPostInterface : nullptr;
    }

    bool CommandPostsKnown(const std::uint8_t* commandPosts)
    {
        const int checked = g_OutpostObject.load(std::memory_order_acquire);
        if (checked != kOutpostUnchecked)
            return checked == kOutpostKnown;
        void** execution = nullptr;
        std::uint8_t* executeSerially = nullptr;
        if (!ReadEntryBytesSEH(commandPosts + kCommandPostExecution, &execution, sizeof(execution)) || !execution
            || !ReadEntryBytesSEH(execution, &executeSerially, sizeof(executeSerially)))
            executeSerially = nullptr;
        const bool known = executeSerially
                        && Matches(executeSerially + kExecuteSeriallyBody, kExecuteSeriallyBodyBytes, nullptr, sizeof(kExecuteSeriallyBodyBytes))
                        && Matches(executeSerially + kUpdateCpCall, kUpdateCpCallBytes, nullptr, sizeof(kUpdateCpCallBytes));
        if (known)
        {
            g_OutpostObject.store(kOutpostKnown, std::memory_order_release);
            return true;
        }
        if (++g_OutpostChecks < kOutpostCheckTries)
            return false;
        LogWarn("LandingZoneDanger: the outpost update differs on %s, in-base LZs stay Danger High after you clear the outpost",
                GetGameBuildName(gGameBuild));
        g_OutpostObject.store(kOutpostUnknown, std::memory_order_release);
        return false;
    }

    bool OutpostFlagsSEH(const std::uint8_t* commandPosts, std::uint32_t index, std::uint32_t& flags)
    {
        __try
        {
            if (index >= *reinterpret_cast<const std::uint32_t*>(commandPosts + kCommandPostCount))
                return false;
            const std::uint8_t* records = *reinterpret_cast<const std::uint8_t* const*>(commandPosts + kOutpostRecords);
            if (!records)
                return false;
            flags = *reinterpret_cast<const std::uint32_t*>(records + static_cast<std::size_t>(index) * kOutpostRecordSize + kOutpostRecordFlags);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool GunPositionSEH(const std::uint8_t* unit, float position[3])
    {
        __try
        {
            const std::uint8_t* model = *reinterpret_cast<const std::uint8_t* const*>(unit + kGunUnitModel);
            const std::uint8_t* bones = model ? *reinterpret_cast<const std::uint8_t* const*>(model + kModelBoneMatrices) : nullptr;
            if (!bones)
                return false;
            std::memcpy(position, bones + kBoneTranslation, 3 * sizeof(float));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool GunBrokenSEH(const std::uint8_t* unit, bool& broken)
    {
        __try
        {
            broken = (*reinterpret_cast<const std::uint32_t*>(unit + kGunUnitState) & kGunUnitBroken) != 0;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool GunIdSEH(const std::uint8_t* unit, std::uint64_t& id)
    {
        __try
        {
            id = *reinterpret_cast<const std::uint64_t*>(unit + kGunUnitId);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    int LoadedGunBlocksSEH(const std::uint8_t* guns, const std::uint8_t** units, std::uint32_t* counts)
    {
        int found = 0;
        __try
        {
            const std::uint64_t loaded = *reinterpret_cast<const std::uint64_t*>(guns + kLoadedBlocks);
            for (int b = 0; b < kGunBlocks; ++b)
            {
                if (!((loaded >> b) & 1))
                    continue;
                const std::uint8_t* block = guns + static_cast<std::size_t>(b) * kBlockStride;
                const std::uint8_t* state = *reinterpret_cast<const std::uint8_t* const*>(block + kBlockState);
                if (!state || *reinterpret_cast<const std::int16_t*>(state + kBlockStateLoading) > 0)
                    continue;
                units[found]  = *reinterpret_cast<const std::uint8_t* const*>(block + kBlockUnits);
                counts[found] = *reinterpret_cast<const std::uint32_t*>(block + kBlockUnitCount);
                if (units[found] && counts[found])
                    ++found;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
        return found;
    }

    const std::uint8_t* GunsSEH()
    {
        if (!gAddr.GatlingGunGameObject_Handle)
            return nullptr;
        __try
        {
            const std::uint8_t* handle = *reinterpret_cast<const std::uint8_t* const*>(ResolveGameAddress(gAddr.GatlingGunGameObject_Handle));
            return handle ? *reinterpret_cast<const std::uint8_t* const*>(handle + kHandleGameObject) : nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    int LoadedGuns(LoadedGun* guns, bool& overflowed)
    {
        const std::uint8_t* gunObject = GunsSEH();
        if (!gunObject)
            return 0;
        const std::uint8_t* units[kGunBlocks] = {};
        std::uint32_t counts[kGunBlocks] = {};
        const int blocks = LoadedGunBlocksSEH(gunObject, units, counts);
        int found = 0;
        for (int b = 0; b < blocks; ++b)
        {
            const std::uint32_t count = counts[b] < kMaxGunsPerBlock ? counts[b] : kMaxGunsPerBlock;
            for (std::uint32_t u = 0; u < count; ++u)
            {
                const std::uint8_t* unit = units[b] + static_cast<std::size_t>(u) * kGunUnitSize;
                LoadedGun gun = {};
                if (!GunIdSEH(unit, gun.id) || gun.id == 0)
                    continue;
                if (found >= kMaxLoadedGuns)
                {
                    overflowed = true;
                    return found;
                }
                gun.placed     = GunPositionSEH(unit, gun.position);
                gun.stateKnown = GunBrokenSEH(unit, gun.broken);
                guns[found++]  = gun;
            }
        }
        return found;
    }

    bool GunBrokenInSaveSEH(std::uint64_t gunId)
    {
        if (!gAddr.PermanentGimmickSaveStorageController_Instance)
            return false;
        __try
        {
            const std::uint8_t* storage = *reinterpret_cast<const std::uint8_t* const*>(ResolveGameAddress(gAddr.PermanentGimmickSaveStorageController_Instance));
            const std::uint8_t* blocks = storage ? *reinterpret_cast<const std::uint8_t* const*>(storage) : nullptr;
            if (!blocks)
                return false;
            for (int b = 0; b < kSavedBlocks; ++b)
            {
                const std::uint8_t* block = blocks + static_cast<std::size_t>(b) * kSavedBlockSize;
                std::uint32_t used = *reinterpret_cast<const std::uint32_t*>(block + kSavedUsed);
                if (used > kSavedBlockRows)
                    used = kSavedBlockRows;
                const std::uint64_t* ids = reinterpret_cast<const std::uint64_t*>(block);
                const std::uint16_t* states = reinterpret_cast<const std::uint16_t*>(block + kSavedStates);
                for (std::uint32_t i = 0; i < used; ++i)
                {
                    if (ids[i] != gunId || (states[i] >> kSavedKindShift) != kSavedKindGatling)
                        continue;
                    const std::uint16_t broken = states[i] & kSavedBrokenBits;
                    return broken == kSavedBroken || broken == kSavedBrokenForGood;
                }
            }
            return false;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    InBaseLandingZone* ZoneOf(ZoneTable& table, std::uint32_t lzName, bool add)
    {
        for (int i = 0; i < table.count; ++i)
        {
            if (table.zones[i].name == lzName)
                return &table.zones[i];
        }
        if (!add || table.count >= kMaxDangerLzs)
            return nullptr;
        InBaseLandingZone& zone = table.zones[table.count++];
        zone = {};
        zone.name      = lzName;
        zone.outpostId = kNoGameObject;
        return &zone;
    }

    int GunIndex(const InBaseLandingZone& zone, std::uint64_t gunId)
    {
        for (int i = 0; i < zone.gunCount; ++i)
        {
            if (zone.gunIds[i] == gunId)
                return i;
        }
        return -1;
    }

    void RemoveGunAt(InBaseLandingZone& zone, int index)
    {
        zone.gunIds[index] = zone.gunIds[--zone.gunCount];
        zone.gunIds[zone.gunCount] = 0;
    }

    void GunKey(char (&key)[32], std::uint32_t lzName, std::uint64_t gunId)
    {
        std::snprintf(key, sizeof(key), "%08X.%016llX", lzName, static_cast<unsigned long long>(gunId));
    }

    void CollectSavedGun(const char* name, std::int32_t value)
    {
        unsigned int lzName = 0;
        unsigned long long gunId = 0;
        if (value == 0 || g_SavedGunCount >= static_cast<int>(sizeof(g_SavedGuns) / sizeof(g_SavedGuns[0]))
            || sscanf_s(name, "%8X.%16llX", &lzName, &gunId) != 2)
            return;
        g_SavedGuns[g_SavedGunCount++] = { lzName, gunId, true };
    }

    void LoadKnownGuns(ZoneTable& table)
    {
        g_SavedGunCount = 0;
        V_FrameWorkState::ForEachPersistedConstant(kKnownGunSpace, &CollectSavedGun);
        for (int i = 0; i < g_SavedGunCount; ++i)
        {
            char key[32] = {};
            GunKey(key, g_SavedGuns[i].lzName, g_SavedGuns[i].gunId);
            V_FrameWorkState::GetPersistedConstant(kKnownGunSpace, key);   // reading it keeps the state file from dropping it
            InBaseLandingZone* zone = ZoneOf(table, g_SavedGuns[i].lzName, true);
            if (zone && GunIndex(*zone, g_SavedGuns[i].gunId) < 0 && zone->gunCount < kMaxGunsPerBase)
                zone->gunIds[zone->gunCount++] = g_SavedGuns[i].gunId;
        }
    }

    void SaveGunChanges(const GunChange* changes, int count)
    {
        if (count == 0)
            return;
        const V_FrameWorkState::SaveBatch batch;
        for (int i = 0; i < count; ++i)
        {
            char key[32] = {};
            GunKey(key, changes[i].lzName, changes[i].gunId);
            V_FrameWorkState::SetPersistedConstant(kKnownGunSpace, key, changes[i].known ? 1 : 0);
        }
    }

    void NoteGunChange(GunChange* changes, int& count, std::uint32_t lzName, std::uint64_t gunId, bool known)
    {
        if (count < kMaxGunChanges)
            changes[count++] = { lzName, gunId, known };
        LogDebug("LandingZoneDanger probe: LZ 0x%08X anti-air gun 0x%016llX %s", lzName, static_cast<unsigned long long>(gunId),
                 known ? "remembered" : "forgotten");
    }

    void StartMission(ZoneTable& table)
    {
        for (int i = 0; i < table.count; ++i)
        {
            InBaseLandingZone& zone = table.zones[i];
            zone.outpostId      = kNoGameObject;
            zone.nextOutpostTry = 0;
            zone.cleared        = false;
            zone.loggedAntiAir  = kAntiAirUnseen;
            zone.loggedCleared  = kNotLogged;
        }
    }

    void ReadDangerTables(const std::uint8_t* mapData, ZoneTable& table)
    {
        std::uint32_t landingZones[kMaxDangerLzs] = {};
        std::uint32_t landPoints[kMaxDangerLzs] = {};
        const int listed = NameListSEH(mapData, kDangerLzCount, kDangerLzNames, landingZones);
        const int drops  = NameListSEH(mapData, kDangerLandPointCount, kDangerLandPointNames, landPoints);
        for (int i = 0; i < table.count; ++i)
        {
            table.zones[i].listed    = false;
            table.zones[i].landPoint = 0;
        }
        for (int i = 0; i < listed; ++i)
        {
            InBaseLandingZone* zone = ZoneOf(table, landingZones[i], true);
            if (!zone)
                continue;
            zone->listed    = true;
            zone->landPoint = drops == listed ? landPoints[i] : 0;   // TppLandingZone.OnInitialize fills both tables in one loop, row for row
        }
    }

    int NearestPlacedZone(const float gun[3], const float (*positions)[3], const bool* placed, int count)
    {
        int nearest = -1;
        float nearestSquared = kAntiAirReach * kAntiAirReach;
        for (int i = 0; i < count; ++i)
        {
            if (!placed[i])
                continue;
            const float squared = FlatDistanceSquared(gun, positions[i]);
            if (squared <= nearestSquared)
            {
                nearestSquared = squared;
                nearest = i;
            }
        }
        return nearest;
    }

    int LoadedGunIndex(const LoadedGun* guns, int count, std::uint64_t gunId)
    {
        for (int i = 0; i < count; ++i)
        {
            if (guns[i].id == gunId)
                return i;
        }
        return -1;
    }

    bool DataSetLoaded(const LoadedGun* guns, int count, std::uint64_t gunId)
    {
        for (int i = 0; i < count; ++i)
        {
            if ((guns[i].id >> 32) == (gunId >> 32))
                return true;
        }
        return false;
    }

    void LearnGuns(ZoneTable& table, const LoadedGun* guns, int gunCount, const float (*positions)[3], const bool* placed,
                   GunChange* changes, int& changeCount)
    {
        for (int g = 0; g < gunCount; ++g)
        {
            if (!guns[g].placed)
                continue;
            const int owner = NearestPlacedZone(guns[g].position, positions, placed, table.count);
            if (owner < 0 || GunIndex(table.zones[owner], guns[g].id) >= 0)
                continue;
            for (int z = 0; z < table.count; ++z)
            {
                const int held = z == owner ? -1 : GunIndex(table.zones[z], guns[g].id);
                if (held < 0)
                    continue;
                RemoveGunAt(table.zones[z], held);
                NoteGunChange(changes, changeCount, table.zones[z].name, guns[g].id, false);
            }
            if (table.zones[owner].gunCount >= kMaxGunsPerBase)
                continue;
            table.zones[owner].gunIds[table.zones[owner].gunCount++] = guns[g].id;
            NoteGunChange(changes, changeCount, table.zones[owner].name, guns[g].id, true);
        }
    }

    void ForgetMissingGuns(ZoneTable& table, const LoadedGun* guns, int gunCount, GunChange* changes, int& changeCount)
    {
        for (int z = 0; z < table.count; ++z)
        {
            InBaseLandingZone& zone = table.zones[z];
            for (int i = zone.gunCount - 1; i >= 0; --i)
            {
                const std::uint64_t gunId = zone.gunIds[i];
                if (LoadedGunIndex(guns, gunCount, gunId) >= 0 || !DataSetLoaded(guns, gunCount, gunId))
                    continue;
                RemoveGunAt(zone, i);
                NoteGunChange(changes, changeCount, zone.name, gunId, false);
            }
        }
    }

    void TallyAntiAir(InBaseLandingZone& zone, const LoadedGun* guns, int gunCount)
    {
        int standing = 0;
        int broken = 0;
        for (int i = 0; i < zone.gunCount; ++i)
        {
            const int loaded = LoadedGunIndex(guns, gunCount, zone.gunIds[i]);
            const bool isBroken = loaded >= 0 ? guns[loaded].stateKnown && guns[loaded].broken : GunBrokenInSaveSEH(zone.gunIds[i]);
            if (isBroken)
                ++broken;
            else
                ++standing;
        }
        zone.antiAir = zone.gunCount == 0 ? kAntiAirUnseen : (standing == 0 ? kAntiAirAllBroken : kAntiAirStanding);
        if (zone.loggedAntiAir == zone.antiAir)
            return;
        zone.loggedAntiAir = zone.antiAir;
        LogDebug("LandingZoneDanger probe: LZ 0x%08X anti-air guns standing=%d broken=%d", zone.name, standing, broken);
    }

    void FindOutpost(InBaseLandingZone& zone, const float position[3], ULONGLONG now)
    {
        if (zone.outpostId != kNoGameObject || now < zone.nextOutpostTry)
            return;
        zone.nextOutpostTry = now + kOutpostRetryMs;
        zone.outpostId = OutpostNear(position);
        if (zone.outpostId != kNoGameObject)
            LogDebug("LandingZoneDanger probe: LZ 0x%08X belongs to outpost 0x%04X", zone.name, zone.outpostId);
    }

    void ReadCleared(InBaseLandingZone& zone, const std::uint8_t* commandPosts)
    {
        std::uint32_t flags = 0;
        const bool known = zone.outpostId != kNoGameObject && commandPosts
                        && OutpostFlagsSEH(commandPosts, zone.outpostId & kOutpostIndexMask, flags);
        zone.cleared = known && (flags & kOutpostHasSoldiers) == 0;
        const std::uint8_t logged = !known ? kNotLogged : (zone.cleared ? kLoggedCleared : kLoggedOccupied);
        if (logged == kNotLogged || zone.loggedCleared == logged)
            return;
        zone.loggedCleared = logged;
        LogDebug("LandingZoneDanger probe: LZ 0x%08X cleared=%u (outpost 0x%04X flags 0x%08X)", zone.name, zone.cleared ? 1u : 0u,
                 zone.outpostId, flags);
    }

    void UpdateInBaseLandingZones()   // only the Lua owner thread comes here
    {
        const ULONGLONG now = GetTickCount64();
        if (now < g_NextUpdate)
            return;
        g_NextUpdate = now + kUpdateMs;
        if (MissionCodeGuard::ShouldBypassHooks() || !FeatureSwitch::IsOn(FeatureSwitch::Feature::DangerLandingZones))
            return;
        const std::uint8_t* mapData = MapDataSEH();
        if (!mapData)
            return;

        static ZoneTable table;
        static LoadedGun guns[kMaxLoadedGuns];
        AcquireSRWLockShared(&g_Lock);
        table = g_Zones;
        ReleaseSRWLockShared(&g_Lock);

        if (!g_KnownGunsLoaded)
        {
            g_KnownGunsLoaded = true;
            LoadKnownGuns(table);
        }
        const std::uint16_t mission = MissionCodeGuard::GetCurrentMissionCode();
        if (mission != g_ZonesMission)
        {
            g_ZonesMission = mission;
            StartMission(table);
        }
        ReadDangerTables(mapData, table);

        float positions[kMaxDangerLzs][3] = {};
        bool placed[kMaxDangerLzs] = {};
        const std::uint8_t* lzTable = ApplicationPartSEH(kApplicationLzTable);
        for (int i = 0; i < table.count; ++i)
            placed[i] = table.zones[i].listed && lzTable && LzPositionSEH(lzTable, table.zones[i].name, positions[i]);

        GunChange changes[kMaxGunChanges] = {};
        int changeCount = 0;
        bool overflowed = false;
        const int gunCount = LoadedGuns(guns, overflowed);
        LearnGuns(table, guns, gunCount, positions, placed, changes, changeCount);
        if (!overflowed)
            ForgetMissingGuns(table, guns, gunCount, changes, changeCount);

        const std::uint8_t* commandPosts = CommandPosts();
        if (commandPosts && !CommandPostsKnown(commandPosts))
            commandPosts = nullptr;
        for (int i = 0; i < table.count; ++i)
        {
            InBaseLandingZone& zone = table.zones[i];
            TallyAntiAir(zone, guns, gunCount);
            if (placed[i])
                FindOutpost(zone, positions[i], now);
            ReadCleared(zone, commandPosts);
        }

        AcquireSRWLockExclusive(&g_Lock);
        g_Zones = table;
        ReleaseSRWLockExclusive(&g_Lock);
        SaveGunChanges(changes, changeCount);
    }

    ZoneDanger DangerOf(std::uint32_t name, bool isLandPoint)
    {
        ZoneDanger danger = {};
        if (name == 0)
            return danger;
        AcquireSRWLockShared(&g_Lock);
        for (int i = 0; i < g_Zones.count; ++i)
        {
            const InBaseLandingZone& zone = g_Zones.zones[i];
            if (!zone.listed || (isLandPoint ? zone.landPoint : zone.name) != name)
                continue;
            danger.cleared          = zone.cleared;
            danger.antiAirAllBroken = zone.antiAir == kAntiAirAllBroken;
            break;
        }
        ReleaseSRWLockShared(&g_Lock);
        return danger;
    }

    bool __fastcall hkIsDangerLandingZone(void* mapData, std::uint32_t lzName)
    {
        if (MissionCodeGuard::ShouldBypassHooks() || !FeatureSwitch::IsOn(FeatureSwitch::Feature::DangerLandingZones))
            return g_OrigIsDanger(mapData, lzName);
        return InDangerListSEH(mapData, lzName) && !DangerOf(lzName, false).cleared;
    }

    void __fastcall hkInsertNearestLzMarker(void* nearest, float distanceSquared, std::uint32_t lzIndex, std::uint32_t flags,
                                            std::uint32_t iconKind, std::uint8_t iconFlag)
    {
        if ((flags & kMarkerDangerHigh) && !MissionCodeGuard::ShouldBypassHooks()
            && FeatureSwitch::IsOn(FeatureSwitch::Feature::DangerLandingZones)
            && FeatureSwitch::IsOn(FeatureSwitch::Feature::ModerateLandingZones))
        {
            const std::uint8_t* lzTable = ApplicationPartSEH(kApplicationLzTable);
            std::uint32_t lzName = 0;
            if (lzTable && LzNameAtSEH(lzTable, lzIndex, lzName) && DangerOf(lzName, false).antiAirAllBroken)
                flags &= ~(kMarkerDangerHigh | kMarkerDangerLow);
        }
        g_OrigInsertMarker(nearest, distanceSquared, lzIndex, flags, iconKind, iconFlag);
    }

    bool __fastcall hkIsDangerLandPoint(void* mapData, std::uint32_t landPoint)
    {
        const bool listed = g_OrigIsDangerLandPoint(mapData, landPoint);
        if (_ReturnAddress() != g_LandPointCaller)
            return listed;
        t_ModerateLandPoint = false;
        if (!listed || MissionCodeGuard::ShouldBypassHooks() || !FeatureSwitch::IsOn(FeatureSwitch::Feature::DangerLandingZones))
            return listed;
        const ZoneDanger danger = DangerOf(landPoint, true);
        if (danger.cleared)
            return false;
        t_ModerateLandPoint     = danger.antiAirAllBroken && FeatureSwitch::IsOn(FeatureSwitch::Feature::ModerateLandingZones);
        t_ModerateLandPointName = landPoint;
        return true;
    }

    std::uint8_t __fastcall hkRegisterMarker(void* markers, std::uint32_t flags)
    {
        if (t_ModerateLandPoint)   // the LZ picker registers the drop point's marker right after it asked
        {
            t_ModerateLandPoint = false;
            if (flags & kMarkerDangerHigh)
            {
                if (g_LoggedLandPoint.exchange(t_ModerateLandPointName, std::memory_order_relaxed) != t_ModerateLandPointName)
                    LogDebug("LandingZoneDanger probe: LZ picker drop point 0x%08X marker 0x%X -> 0x%X", t_ModerateLandPointName, flags,
                             flags & ~(kMarkerDangerHigh | kMarkerDangerLow));
                flags &= ~(kMarkerDangerHigh | kMarkerDangerLow);
            }
        }
        return g_OrigRegisterMarker(markers, flags);
    }

    void** MapDataVtableSEH()
    {
        const std::uint8_t* mapData = MapDataSEH();
        __try
        {
            return mapData ? *reinterpret_cast<void** const*>(mapData) : nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    void InstallMarkerHook()
    {
        if (g_InsertTarget || !gAddr.UiMarkerCommonDataImpl_InsertNearestLzMarker)
            return;
        void* target = ResolveGameAddress(gAddr.UiMarkerCommonDataImpl_InsertNearestLzMarker);
        const HookEntry entry = CheckHookEntry(target, kInsertNearestLzMarkerEntry, sizeof(kInsertNearestLzMarkerEntry));
        if (entry == HookEntry::Unknown)
        {
            LogError("LandingZoneDanger: the LZ marker list code differs on %s, no LZ shows Danger Moderate", GetGameBuildName(gGameBuild));
            return;
        }
        if (entry == HookEntry::ForeignHook)
            LogHookedOnTop("LandingZoneDanger: InsertNearestLzMarker", target);
        if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkInsertNearestLzMarker), reinterpret_cast<void**>(&g_OrigInsertMarker)))
        {
            LogError("LandingZoneDanger: LZ marker hook failed, no LZ shows Danger Moderate");
            return;
        }
        g_InsertTarget = target;
    }

    void InstallLandPointHook()
    {
        if (g_RegisterMarkerTarget)
            return;
        if (!gAddr.UiMarkerCommonDataImpl_RegisterMarker || !gAddr.MbDvcDependJobFunctionsImpl_RegisterMapIconSelectMode_LandPoint)
        {
            LogWarn("LandingZoneDanger: LZ picker addresses unknown for %s, its drop points never show Danger Moderate", GetGameBuildName(gGameBuild));
            return;
        }
        const auto* landPointMode = static_cast<const std::uint8_t*>(ResolveGameAddress(gAddr.MbDvcDependJobFunctionsImpl_RegisterMapIconSelectMode_LandPoint));
        if (!Matches(landPointMode + kLandPointDangerCall, kLandPointDangerCallBytes, nullptr, sizeof(kLandPointDangerCallBytes)))
        {
            LogError("LandingZoneDanger: the LZ picker code differs on %s, its drop points never show Danger Moderate", GetGameBuildName(gGameBuild));
            return;
        }
        void* target = ResolveGameAddress(gAddr.UiMarkerCommonDataImpl_RegisterMarker);
        const HookEntry entry = CheckHookEntry(target, kRegisterMarkerEntry, sizeof(kRegisterMarkerEntry));
        if (entry == HookEntry::Unknown)
        {
            LogError("LandingZoneDanger: the marker register code differs on %s, the LZ picker's drop points never show Danger Moderate",
                     GetGameBuildName(gGameBuild));
            return;
        }
        if (entry == HookEntry::ForeignHook)
            LogHookedOnTop("LandingZoneDanger: RegisterMarker", target);
        if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkRegisterMarker), reinterpret_cast<void**>(&g_OrigRegisterMarker)))
        {
            LogError("LandingZoneDanger: marker register hook failed, the LZ picker's drop points never show Danger Moderate");
            return;
        }
        g_RegisterMarkerTarget = target;
        g_LandPointCaller      = landPointMode + kLandPointDangerCall + kLandPointDangerCallSize;
    }

    void SwapLandingZoneSlot(void** vtable)
    {
        if (!Matches(vtable[kSlotAddDangerLz], kAddDangerLzBytes, nullptr, sizeof(kAddDangerLzBytes))
            || !Matches(vtable[kSlotIsDangerLz], kReturnFalseBytes, nullptr, sizeof(kReturnFalseBytes)))
        {
            LogWarn("LandingZoneDanger: the iDroid's LZ danger check is not the game's empty one on %s (another mod may answer it), left alone",
                    GetGameBuildName(gGameBuild));
            return;
        }
        if (!SwapVtableSlot(&vtable[kSlotIsDangerLz], reinterpret_cast<void*>(&hkIsDangerLandingZone), reinterpret_cast<void**>(&g_OrigIsDanger)))
        {
            LogError("LandingZoneDanger: could not patch the LZ danger check, every LZ keeps showing Danger Low");
            return;
        }
        g_LandingZoneSlotSwapped = true;
    }

    void SwapLandPointSlot(void** vtable)
    {
        if (!g_LandPointCaller)
            return;
        if (!Matches(vtable[kSlotIsDangerLandPoint], kIsDangerLandPointBytes, nullptr, sizeof(kIsDangerLandPointBytes)))
        {
            LogWarn("LandingZoneDanger: the LZ picker's danger check is not the game's own on %s (another mod may answer it), left alone",
                    GetGameBuildName(gGameBuild));
            return;
        }
        if (!SwapVtableSlot(&vtable[kSlotIsDangerLandPoint], reinterpret_cast<void*>(&hkIsDangerLandPoint), reinterpret_cast<void**>(&g_OrigIsDangerLandPoint)))
        {
            LogError("LandingZoneDanger: could not patch the LZ picker's danger check, its drop points never show Danger Moderate");
            return;
        }
        g_LandPointSlotSwapped = true;
    }

    void SwapMapDataSlots()
    {
        void** vtable = MapDataVtableSEH();
        if (!vtable)
            return;
        int expected = kWatching;
        if (!g_State.compare_exchange_strong(expected, kBusy, std::memory_order_acq_rel))
            return;
        g_Vtable = vtable;
        SwapLandingZoneSlot(vtable);
        SwapLandPointSlot(vtable);
        g_State.store(kSwapped, std::memory_order_release);
    }
}

bool LandingZoneDanger_SelectedIsModerate()
{
    const std::uint8_t* mapData = MapDataSEH();
    if (!mapData)
        return false;
    std::uint8_t safety = 0;
    return ReadEntryBytesSEH(mapData + kSelectedLzSafety, &safety, sizeof(safety)) && safety == kSafetyModerate;
}

bool Install_MotherBaseMapCommonData_IsDangerLandingZone()
{
    if (!gAddr.Ui_UiCommonDataManagerGetInstance)
    {
        LogWarn("LandingZoneDanger: UI data manager unknown for %s, every LZ keeps showing Danger Low", GetGameBuildName(gGameBuild));
        return true;
    }
    InstallMarkerHook();
    InstallLandPointHook();
    int expected = kOff;
    g_State.compare_exchange_strong(expected, kWatching, std::memory_order_acq_rel);
    return true;
}

void MotherBaseMapCommonData_IsDangerLandingZone_Watch()
{
    const int state = g_State.load(std::memory_order_acquire);
    if (state == kWatching)
        SwapMapDataSlots();
    else if (state == kSwapped && (g_LandingZoneSlotSwapped || g_LandPointSlotSwapped || g_InsertTarget))
        UpdateInBaseLandingZones();
}

void Uninstall_MotherBaseMapCommonData_IsDangerLandingZone()
{
    if (g_Vtable && g_LandingZoneSlotSwapped)
        RestoreVtableSlot(&g_Vtable[kSlotIsDangerLz], reinterpret_cast<void*>(g_OrigIsDanger), reinterpret_cast<void*>(&hkIsDangerLandingZone));
    if (g_Vtable && g_LandPointSlotSwapped)
        RestoreVtableSlot(&g_Vtable[kSlotIsDangerLandPoint], reinterpret_cast<void*>(g_OrigIsDangerLandPoint), reinterpret_cast<void*>(&hkIsDangerLandPoint));
    if (g_InsertTarget)
    {
        DisableAndRemoveHook(g_InsertTarget);
        g_InsertTarget     = nullptr;
        g_OrigInsertMarker = nullptr;
    }
    if (g_RegisterMarkerTarget)
    {
        DisableAndRemoveHook(g_RegisterMarkerTarget);
        g_RegisterMarkerTarget = nullptr;
        g_OrigRegisterMarker   = nullptr;
    }
    g_LandPointCaller        = nullptr;
    g_LandingZoneSlotSwapped = false;
    g_LandPointSlotSwapped   = false;
    g_Vtable                 = nullptr;
    g_State.store(kOff, std::memory_order_release);
    g_OutpostObject.store(kOutpostUnchecked, std::memory_order_release);
    g_OutpostChecks   = 0;
    g_KnownGunsLoaded = false;
    g_NextUpdate      = 0;
    g_ZonesMission    = 0;
    AcquireSRWLockExclusive(&g_Lock);
    g_Zones = {};
    ReleaseSRWLockExclusive(&g_Lock);
}
