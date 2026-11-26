#include <pch.h>
#include "SoldierFactionAntiAir.h"

#include <Windows.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"
#include "SoldierFaction.h"
#include "../heli/HeliLineOfSight.h"

namespace
{
    using QuarkTable_t    = std::uint8_t*(__fastcall*)();
    using SetMemberRole_t = void(__fastcall*)(void* context, std::uint32_t member, std::uint8_t role, const void* param, std::uint64_t size);
    using CpRadius_t      = float(__fastcall*)(void* context, std::uint32_t cp);
    using SendSignal_t    = std::int32_t*(__fastcall*)(void* gameObjects, std::int32_t* result, std::uint32_t gameObjectId, void* signal);

    constexpr std::uint32_t kSoldierCount    = 0x200u;
    constexpr std::uint16_t kNoGun           = 0xFFFFu;
    constexpr std::uint16_t kNoHeli          = 0u;
    constexpr std::uint8_t  kNoticeAntiAir   = 7u;    // what CheckSpecialNotice hands a gunner of the support heli
    constexpr std::uint8_t  kRoleFree        = 0u;
    constexpr std::uint8_t  kRolePosted      = 3u;
    constexpr std::uint8_t  kRoleAntiAir     = 6u;
    constexpr std::uint16_t kNoLocator       = 0x100u;
    constexpr std::uint32_t kMaxGunnersPerCp = 4u;    // UpdateAntiAir's cap
    constexpr std::uint32_t kMaxCps          = 256u;
    constexpr std::uint32_t kMaxHeldGuns     = 64u;
    constexpr std::uint64_t kAssignMs        = 2000u;
    constexpr std::uint64_t kGaveUpMs        = 10000u;
    constexpr float         kGunReachSq      = 400.0f * 400.0f;   // vanilla only takes guns this close to the heli (horizontal)
    constexpr float         kKeepSq          = 500.0f * 500.0f;   // and lets the gunner go past this
    constexpr float         kHandleHeight    = 1.3f;              // StepAntiAir's own line starts this high
    constexpr float         kSkipGun         = 2.0f;              // the ray starts clear of the gun itself
    constexpr float         kHeliStop        = 4.0f;

    constexpr std::size_t kQuarkGameObjects        = 0x60;
    constexpr std::size_t kQuarkGameSystems        = 0x98;
    constexpr std::size_t kGameSystemsCommandPost  = 0x1C8;
    constexpr std::size_t kSendSignalSlot          = 0x38 / sizeof(void*);
    constexpr std::size_t kCpInterfaceOffset       = 0x20;   // game systems hold CommandPost2Impl + 0x20
    constexpr std::size_t kCpContext               = 0x38;
    constexpr std::size_t kCpRoleController        = 0x50;   // ClearAntiAir is slot 0x130 of what sits here
    constexpr std::size_t kRoleControllerInterface = 0x20;
    constexpr std::size_t kRoleControllerContext   = 0x30;
    constexpr std::size_t kRoleControllerLocators  = 0x50;
    constexpr std::size_t kLocatorStride           = 0x30;
    constexpr std::size_t kLocatorUsers            = 0x29;

    constexpr std::size_t   kCtxMembers      = 0x50;
    constexpr std::size_t   kCtxMemberFlags  = 0x58;
    constexpr std::size_t   kCtxCpMembers    = 0x118;
    constexpr std::size_t   kCtxCpFirst      = 0x120;
    constexpr std::size_t   kCtxCpMemberCount = 0x128;
    constexpr std::size_t   kCtxCps          = 0x160;
    constexpr std::size_t   kCtxCpRecords    = 0x1E0;
    constexpr std::size_t   kCtxAntiAirKill  = 0x6BA;   // CommandPost2Impl+0x6F2 bit 0: no anti-air anywhere
    constexpr std::size_t   kCpRecordStride  = 0x80;
    constexpr std::size_t   kCpRecordFlags   = 0x77;    // bit 3: this post never mans anti-air guns
    constexpr std::uint8_t  kCpNoAntiAir     = 0x08u;
    constexpr std::size_t   kMemberStride    = 0x20;
    constexpr std::size_t   kMemberRole      = 0x1D;
    constexpr std::size_t   kFlagsStride     = 0x14;
    constexpr std::size_t   kFlagsAttributes = 0x12;
    constexpr std::uint32_t kMemberAlive     = 0x80001u;
    constexpr std::uint8_t  kMemberCanGun    = 0x40u;
    constexpr std::uint32_t kMemberBusy      = (1u << 2) | (1u << 4) | (1u << 9) | (1u << 12) | (1u << 13) | (1u << 14)
                                             | (1u << 16) | (1u << 17) | (1u << 25) | (1u << 28);   // UpdateAntiAir skips any of these

    constexpr std::size_t kSlotSetMemberRole = 0x130 / sizeof(void*);
    constexpr std::size_t kSlotSearchRadius  = 0x18 / sizeof(void*);
    constexpr std::size_t kSlotAntiAirRadius = 0x28 / sizeof(void*);

    constexpr std::uint8_t kSetMemberRoleBytes[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57,
        0x48, 0x83, 0xEC, 0x20, 0xF6, 0x81, 0x76, 0x01, 0x00, 0x00, 0x01
    };
    constexpr std::uint8_t kAntiAirRadiusBytes[] = {
        0x48, 0x83, 0xEC, 0x38, 0x0F, 0xB7, 0xC2, 0x0F, 0x29, 0x74, 0x24, 0x20, 0xF3, 0x0F, 0x10, 0x35
    };
    constexpr std::uint8_t kSearchRadiusBytes[] = {
        0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0x01, 0xFF, 0x50, 0x08, 0xF3, 0x0F, 0x58, 0x05
    };

    constexpr std::uint64_t kGunQuery          = 0xBC02A3A62EEFull;   // the free-gatling query UpdateAntiAir sends
    constexpr std::uint32_t kGunCategory       = 0xA000u;
    constexpr std::uint32_t kGunResults        = 16u;
    constexpr std::size_t   kGunResultStride   = 0x50;
    constexpr std::size_t   kGunResultPosition = 0x30;
    constexpr std::size_t   kGunResultId       = 0x40;
    constexpr std::size_t   kGunResultFlags    = 0x44;
    constexpr std::uint64_t kUnitSignal        = 0x9C17DEAF1DFCull;   // hands out a gimmick unit (real name unknown)
    constexpr std::size_t   kUnitOwner         = 0x0A;
    constexpr std::size_t   kUnitState         = 0x68;
    constexpr std::uint32_t kUnitBroken        = 1u << 29;

    constexpr std::size_t kCoreCommandPost = 0xD0;
    constexpr std::size_t kPostMembers     = 0x10;

    constexpr std::size_t   kCommandTarget     = 0x18;
    constexpr std::size_t   kCommandTargetKind = 0x24;
    constexpr std::size_t   kCommandFire       = 0x2B;
    constexpr std::size_t   kCommandHandle     = 0x80;
    constexpr std::size_t   kCommandType       = 0xB4;
    constexpr std::uint8_t  kTurretCommand     = 0x16;
    constexpr std::uint8_t  kTargetKindMask    = 7;
    constexpr std::uint8_t  kTargetGameObject  = 2;
    constexpr std::uint16_t kSupportHeli       = 0x1C00;   // StepAntiAir only ever aims at it
    constexpr std::uint8_t  kFire              = 2;
    constexpr std::uint8_t  kHold              = 0;

    struct alignas(16) GunQuery
    {
        std::uint64_t id;
        std::uint32_t capacity;   // the hit count comes back here
        std::uint32_t pad0;
        std::uint8_t* results;
        std::uint64_t pad1;
        float         sphere[4];  // centre, then radius
        std::uint32_t flags;
        std::uint8_t  pad2[12];
    };
    static_assert(offsetof(GunQuery, sphere) == 0x20 && offsetof(GunQuery, flags) == 0x30, "UpdateAntiAir's query layout");

    struct alignas(16) UnitSignal
    {
        std::uint64_t id;
        std::uint8_t* unit;
        std::uint8_t  pad[0x30];
    };

    struct CpRefs
    {
        std::uint8_t* context;
        void* const*  vtbl;
        std::uint8_t* locators;
        void*         gameObjects;
    };

    struct Post
    {
        std::uint32_t cp;
        bool          antiAirOff;
        float         position[3];
        std::uint32_t gunners;
        std::uint32_t held;
        std::uint16_t heldGuns[kMaxHeldGuns];
    };

    struct Gunner
    {
        std::atomic<std::uint16_t> heli{ kNoHeli };
        std::atomic<std::uint16_t> gun{ kNoGun };
        std::atomic<bool>          gaveUp{ false };
        std::atomic<bool>          fired{ false };
        float                      gunPosition[3]{};
        std::uint64_t              nextTryMs = 0;
    };

    std::array<Gunner, kSoldierCount> g_Gunners;
    std::atomic<void* const*>         g_TrustedVtbl{ nullptr };
    std::atomic<bool>                 g_Refused{ false };

    float HorizontalSq(const float* a, const float* b)
    {
        const float dx = a[0] - b[0];
        const float dz = a[2] - b[2];
        return dx * dx + dz * dz;
    }

    float DistanceSq(const float* a, const float* b)
    {
        const float dy = a[1] - b[1];
        return HorizontalSq(a, b) + dy * dy;
    }

    bool ReadCpRefsSEH(QuarkTable_t quarkTable, CpRefs* out)
    {
        __try
        {
            std::uint8_t* table = quarkTable();
            std::uint8_t* gameSystems = table ? *reinterpret_cast<std::uint8_t**>(table + kQuarkGameSystems) : nullptr;
            std::uint8_t* cpInterface = gameSystems ? *reinterpret_cast<std::uint8_t**>(gameSystems + kGameSystemsCommandPost) : nullptr;
            if (!cpInterface)
                return false;
            std::uint8_t* cp = cpInterface - kCpInterfaceOffset;
            out->context = cp + kCpContext;
            out->vtbl = *reinterpret_cast<void* const* const*>(out->context);
            std::uint8_t* roles = *reinterpret_cast<std::uint8_t**>(cp + kCpRoleController);
            std::uint8_t* controller = roles ? roles - kRoleControllerInterface : nullptr;
            out->locators = controller && *reinterpret_cast<std::uint8_t**>(controller + kRoleControllerContext) == out->context
                ? *reinterpret_cast<std::uint8_t**>(controller + kRoleControllerLocators) : nullptr;
            out->gameObjects = *reinterpret_cast<void**>(table + kQuarkGameObjects);
            return out->vtbl && out->gameObjects;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool Matches(const void* at, const std::uint8_t* bytes, std::size_t size)
    {
        std::uint8_t read[32] = {};
        return at && size <= sizeof(read) && ReadEntryBytesSEH(at, read, size) && std::memcmp(read, bytes, size) == 0;
    }

    bool Trusted(void* const* vtbl)
    {
        if (!vtbl || g_Refused.load(std::memory_order_relaxed))
            return false;
        if (g_TrustedVtbl.load(std::memory_order_acquire) == vtbl)
            return true;
        void* slots[kSlotSetMemberRole + 1] = {};
        if (!ReadEntryBytesSEH(vtbl, slots, sizeof(slots))
            || !Matches(slots[kSlotSetMemberRole], kSetMemberRoleBytes, sizeof(kSetMemberRoleBytes))
            || !Matches(slots[kSlotAntiAirRadius], kAntiAirRadiusBytes, sizeof(kAntiAirRadiusBytes))
            || !Matches(slots[kSlotSearchRadius], kSearchRadiusBytes, sizeof(kSearchRadiusBytes)))
        {
            if (!g_Refused.exchange(true, std::memory_order_relaxed))
                LogWarn("SoldierFaction: command post not recognised, faction soldiers do not man anti-air guns");
            return false;
        }
        g_TrustedVtbl.store(vtbl, std::memory_order_release);
        return true;
    }

    bool Refs(CpRefs* refs)
    {
        static const QuarkTable_t quarkTable = gAddr.GetQuarkSystemTable
            ? reinterpret_cast<QuarkTable_t>(ResolveGameAddress(gAddr.GetQuarkSystemTable)) : nullptr;
        return quarkTable && ReadCpRefsSEH(quarkTable, refs) && Trusted(refs->vtbl);
    }

    bool SameMembersSEH(const CpRefs& refs, const void* soldierCore)   // the soldier's own view must be the post's table
    {
        __try
        {
            const std::uint8_t* post = *reinterpret_cast<const std::uint8_t* const*>(static_cast<const std::uint8_t*>(soldierCore) + kCoreCommandPost);
            return post && *reinterpret_cast<const std::uint8_t* const*>(post + kPostMembers)
                == *reinterpret_cast<const std::uint8_t* const*>(refs.context + kCtxMembers);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint8_t RoleSEH(const CpRefs& refs, std::uint32_t member)
    {
        __try
        {
            return (*reinterpret_cast<const std::uint8_t* const*>(refs.context + kCtxMembers))[member * kMemberStride + kMemberRole];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0xFFu;
        }
    }

    bool CanGunSEH(const CpRefs& refs, std::uint32_t member)   // UpdateAntiAir's free-member test
    {
        __try
        {
            const std::uint8_t* flags = *reinterpret_cast<const std::uint8_t* const*>(refs.context + kCtxMemberFlags) + member * kFlagsStride;
            const std::uint32_t bits = *reinterpret_cast<const std::uint32_t*>(flags);
            const std::uint8_t role = (*reinterpret_cast<const std::uint8_t* const*>(refs.context + kCtxMembers))[member * kMemberStride + kMemberRole];
            return (bits & kMemberAlive) == kMemberAlive && (flags[kFlagsAttributes] & kMemberCanGun) != 0
                && (bits & kMemberBusy) == 0 && (role == kRoleFree || role == kRolePosted);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ReadPostSEH(const CpRefs& refs, std::uint32_t member, Post* post)
    {
        __try
        {
            const std::uint8_t* context = refs.context;
            std::uint32_t cps = *reinterpret_cast<const std::uint32_t*>(context + kCtxCps);
            if (cps > kMaxCps)
                cps = kMaxCps;
            const std::uint32_t* list   = *reinterpret_cast<const std::uint32_t* const*>(context + kCtxCpMembers);
            const std::uint16_t* first  = *reinterpret_cast<const std::uint16_t* const*>(context + kCtxCpFirst);
            const std::uint16_t* count  = *reinterpret_cast<const std::uint16_t* const*>(context + kCtxCpMemberCount);
            const std::uint8_t* members = *reinterpret_cast<const std::uint8_t* const*>(context + kCtxMembers);
            const std::uint8_t* flags   = *reinterpret_cast<const std::uint8_t* const*>(context + kCtxMemberFlags);
            const std::uint8_t* records = *reinterpret_cast<const std::uint8_t* const*>(context + kCtxCpRecords);
            if (!list || !first || !count || !members || !flags || !records)
                return false;
            for (std::uint32_t cp = 0; cp < cps; ++cp)
            {
                const std::uint32_t begin = first[cp];
                const std::uint32_t end = begin + count[cp];
                bool mine = false;
                for (std::uint32_t k = begin; k < end && !mine; ++k)
                    mine = list[k] == member;
                if (!mine)
                    continue;
                post->cp = cp;
                post->antiAirOff = (records[cp * kCpRecordStride + kCpRecordFlags] & kCpNoAntiAir) != 0
                    || (context[kCtxAntiAirKill] & 1u) != 0;   // the same two gates keep vanilla's anti-air flag off
                std::memcpy(post->position, records + cp * kCpRecordStride, sizeof(post->position));
                post->gunners = 0;
                post->held = 0;
                for (std::uint32_t k = begin; k < end; ++k)
                {
                    const std::uint32_t other = list[k];
                    const std::uint32_t bits = *reinterpret_cast<const std::uint32_t*>(flags + other * kFlagsStride);
                    if (members[other * kMemberStride + kMemberRole] != kRoleAntiAir)
                        continue;
                    if (post->held < kMaxHeldGuns)
                        post->heldGuns[post->held++] = *reinterpret_cast<const std::uint16_t*>(members + other * kMemberStride);
                    if ((bits & kMemberAlive) == kMemberAlive)
                        ++post->gunners;
                }
                return true;
            }
            return false;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool RadiiSEH(const CpRefs& refs, std::uint32_t cp, float* antiAir, float* search)
    {
        __try
        {
            *antiAir = reinterpret_cast<CpRadius_t>(refs.vtbl[kSlotAntiAirRadius])(refs.context, cp);
            *search  = reinterpret_cast<CpRadius_t>(refs.vtbl[kSlotSearchRadius])(refs.context, cp);
            return *antiAir > 0.0f && *search > 0.0f;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint32_t QueryGunsSEH(void* gameObjects, const float* centre, float radius, std::uint8_t* results)
    {
        __try
        {
            std::memset(results, 0, kGunResults * kGunResultStride);
            for (std::uint32_t i = 0; i < kGunResults; ++i)
                *reinterpret_cast<std::uint32_t*>(results + i * kGunResultStride + kGunResultId) = 0xFFFFFFFFu;
            GunQuery query{};
            query.id        = kGunQuery;
            query.capacity  = kGunResults;
            query.results   = results;
            query.sphere[0] = centre[0];
            query.sphere[1] = centre[1];
            query.sphere[2] = centre[2];
            query.sphere[3] = radius;
            query.flags     = 1u;
            std::int32_t status = -1;
            const auto sendSignal = reinterpret_cast<SendSignal_t>((*reinterpret_cast<void* const* const*>(gameObjects))[kSendSignalSlot]);
            const std::int32_t* result = sendSignal(gameObjects, &status, kGunCategory, &query);
            if (!result || *result < 0)
                return 0;
            return query.capacity < kGunResults ? query.capacity : kGunResults;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    bool GunFreeSEH(void* gameObjects, std::uint16_t gun, std::uint16_t soldierId)
    {
        __try
        {
            const auto sendSignal = reinterpret_cast<SendSignal_t>((*reinterpret_cast<void* const* const*>(gameObjects))[kSendSignalSlot]);
            UnitSignal signal{};
            signal.id = kUnitSignal;
            std::int32_t result = -1;
            sendSignal(gameObjects, &result, gun, &signal);
            if (!signal.unit)
                return false;
            const std::uint16_t owner = *reinterpret_cast<const std::uint16_t*>(signal.unit + kUnitOwner);
            const std::uint32_t state = *reinterpret_cast<const std::uint32_t*>(signal.unit + kUnitState);
            return (owner == kNoGun || owner == soldierId) && (state & kUnitBroken) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint16_t HeliOverPost(std::uint16_t soldierId, const float* centre, float radius, float* heliPoint)
    {
        std::uint16_t best = kNoHeli;
        float bestSq = radius * radius;
        const std::uint32_t helis = SoldierFaction_FactionHeliCount();
        for (std::uint32_t i = 0; i < helis; ++i)
        {
            const std::uint16_t heli = SoldierFaction_FactionHeliAt(i);
            float point[3];
            if (!SoldierFaction_AreHostileIds(soldierId, heli) || !SoldierFaction_ReadHeliPoint(heli, point))
                continue;
            const float distSq = HorizontalSq(centre, point);
            if (distSq >= bestSq)
                continue;
            best = heli;
            bestSq = distSq;
            std::memcpy(heliPoint, point, sizeof(point));
        }
        return best;
    }

    bool PickGun(const CpRefs& refs, const Post& post, float searchRadius, const float* heliPoint, std::uint16_t soldierId,
                 std::uint16_t* gun, float* gunPosition)
    {
        alignas(16) std::uint8_t results[kGunResults * kGunResultStride];
        const std::uint32_t found = QueryGunsSEH(refs.gameObjects, post.position, searchRadius, results);
        float bestSq = 0.0f;
        bool picked = false;
        for (std::uint32_t i = 0; i < found; ++i)
        {
            const std::uint8_t* entry = results + i * kGunResultStride;
            std::uint16_t id = kNoGun;
            std::memcpy(&id, entry + kGunResultId, sizeof(id));
            const float* position = reinterpret_cast<const float*>(entry + kGunResultPosition);
            if ((entry[kGunResultFlags] & 1u) != 0 || (id >> 9) != (kGunCategory >> 9) || HorizontalSq(position, heliPoint) > kGunReachSq
                || DistanceSq(position, heliPoint) >= searchRadius * searchRadius)   // UpdateAntiAir's own two range checks
                continue;
            bool held = false;
            for (std::uint32_t k = 0; k < post.held && !held; ++k)
                held = post.heldGuns[k] == id;
            const float distSq = DistanceSq(position, heliPoint);
            if (held || (picked && distSq >= bestSq) || !GunFreeSEH(refs.gameObjects, id, soldierId))
                continue;
            *gun = id;
            std::memcpy(gunPosition, position, sizeof(float) * 3);
            bestSq = distSq;
            picked = true;
        }
        return picked;
    }

    bool AssignSEH(const CpRefs& refs, std::uint32_t member, std::uint16_t gun)
    {
        __try
        {
            std::uint8_t* entry = *reinterpret_cast<std::uint8_t* const*>(refs.context + kCtxMembers) + member * kMemberStride;
            const std::uint8_t role = entry[kMemberRole];
            const std::uint16_t locator = *reinterpret_cast<const std::uint16_t*>(entry);
            if (refs.locators && (role == kRoleFree || role == kRolePosted) && locator != kNoLocator)   // DecrementLocatorMember: he leaves his route point
            {
                std::uint8_t* users = refs.locators + locator * kLocatorStride + kLocatorUsers;
                if (*users != 0)
                    --*users;
            }
            const std::uint16_t param[3] = { gun, 0, 0 };
            reinterpret_cast<SetMemberRole_t>(refs.vtbl[kSlotSetMemberRole])(refs.context, member, kRoleAntiAir, param, sizeof(param));
            return entry[kMemberRole] == kRoleAntiAir;   // online, a peer that is not the host changes nothing
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void ReleaseSEH(const CpRefs& refs, std::uint32_t member)
    {
        __try
        {
            const std::uint16_t none = kNoLocator;
            reinterpret_cast<SetMemberRole_t>(refs.vtbl[kSlotSetMemberRole])(refs.context, member, kRoleFree, &none, sizeof(none));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void Leave(Gunner& gunner, std::uint32_t soldierIndex, const CpRefs* refs, const char* reason, std::uint64_t now)
    {
        const std::uint16_t gun = gunner.gun.exchange(kNoGun, std::memory_order_relaxed);
        if (refs)
            ReleaseSEH(*refs, soldierIndex);
        if (gun != kNoGun)
            SoldierFaction_SetGunOperator(gun, 0);
        gunner.heli.store(kNoHeli, std::memory_order_relaxed);
        gunner.nextTryMs = now + (gunner.gaveUp.exchange(false, std::memory_order_relaxed) ? kGaveUpMs : kAssignMs);
    }

    const char* WhyLeave(Gunner& gunner, std::uint32_t soldierIndex, const CpRefs& refs, bool* stillOnGun)
    {
        const std::uint16_t soldierId = static_cast<std::uint16_t>(0x400u | soldierIndex);
        const std::uint16_t heli = gunner.heli.load(std::memory_order_relaxed);
        *stillOnGun = RoleSEH(refs, soldierIndex) == kRoleAntiAir;
        if (!*stillOnGun)
            return "his post took him off it";
        if (gunner.gaveUp.load(std::memory_order_relaxed))
            return "he could not get to it";
        float position[3];
        bool standing = false;
        bool riding = false;
        if (!SoldierFaction_ReadSoldierTarget(soldierId, position, &standing, &riding) || !standing)
            return "he is down";
        float heliPoint[3];
        if (!SoldierFaction_AreHostileIds(soldierId, heli))
            return "the heli is no longer hostile";
        if (!SoldierFaction_ReadHeliPoint(heli, heliPoint))
            return "the heli is gone, destroyed or unrealized";
        if (HorizontalSq(gunner.gunPosition, heliPoint) > kKeepSq)
            return "the heli flew off";
        return nullptr;
    }

    bool TryAssign(Gunner& gunner, const CpRefs& refs, void* noticeController, const void* soldierCore, std::uint32_t soldierIndex)
    {
        const std::uint16_t soldierId = static_cast<std::uint16_t>(0x400u | soldierIndex);
        const std::uint16_t rival = SoldierFaction_GetRivalTarget(soldierIndex);
        if (!SoldierFaction_HasFaction(soldierId) || (rival != 0 && !SoldierFaction_IsEnemyHeliId(rival)))   // a soldier fight comes first
            return false;
        float position[3];
        bool standing = false;
        bool riding = false;
        if (!SoldierFaction_ReadSoldierTarget(soldierId, position, &standing, &riding) || !standing || riding)
            return false;
        if (!SameMembersSEH(refs, soldierCore) || !CanGunSEH(refs, soldierIndex))
            return false;
        Post post{};
        float antiAirRadius = 0.0f;
        float searchRadius = 0.0f;
        if (!ReadPostSEH(refs, soldierIndex, &post) || post.antiAirOff || post.gunners >= kMaxGunnersPerCp
            || !RadiiSEH(refs, post.cp, &antiAirRadius, &searchRadius))
            return false;
        float heliPoint[3];
        const std::uint16_t heli = HeliOverPost(soldierId, post.position, antiAirRadius, heliPoint);
        std::uint16_t gun = kNoGun;
        float gunPosition[3];
        if (heli == kNoHeli || !PickGun(refs, post, searchRadius, heliPoint, soldierId, &gun, gunPosition))
            return false;
        if (!AssignSEH(refs, soldierIndex, gun))
            return false;
        std::memcpy(gunner.gunPosition, gunPosition, sizeof(gunPosition));
        gunner.gaveUp.store(false, std::memory_order_relaxed);
        gunner.fired.store(false, std::memory_order_relaxed);
        gunner.gun.store(gun, std::memory_order_relaxed);
        gunner.heli.store(heli, std::memory_order_release);
        SoldierFaction_SetGunOperator(gun, soldierId);
        SoldierFaction_PushNotice(noticeController, soldierIndex, kNoticeAntiAir, heli);
        return true;
    }

    bool ClearShot(const float* from, const float* heliPoint)
    {
        const float d[3] = { heliPoint[0] - from[0], heliPoint[1] - from[1], heliPoint[2] - from[2] };
        const float dist = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (dist <= kSkipGun + kHeliStop)
            return true;
        const float start[3] = { from[0] + d[0] / dist * kSkipGun, from[1] + d[1] / dist * kSkipGun, from[2] + d[2] / dist * kSkipGun };
        const float t = (dist - kHeliStop) / dist;
        const float end[3] = { from[0] + d[0] * t, from[1] + d[1] * t, from[2] + d[2] * t };
        bool blocked = true;
        return HeliLineOfSight::Blocked(start, end, HeliLineOfSight::kRayMask, &blocked) && !blocked;
    }
}

bool SoldierFactionAntiAir_OnSightCheck(void* noticeController, const void* soldierCore, std::uint32_t soldierIndex, std::uint64_t now)
{
    if (soldierIndex >= kSoldierCount || !noticeController || !soldierCore || MissionCodeGuard::ShouldBypassHooks())
        return false;
    Gunner& gunner = g_Gunners[soldierIndex];
    const std::uint16_t heli = gunner.heli.load(std::memory_order_acquire);
    if (heli == kNoHeli && (now < gunner.nextTryMs || SoldierFaction_FactionHeliCount() == 0))
        return false;

    CpRefs refs{};
    if (!Refs(&refs))
    {
        if (heli != kNoHeli)
            Leave(gunner, soldierIndex, nullptr, "his command post is unreadable", now);
        return false;
    }
    if (heli == kNoHeli)
    {
        gunner.nextTryMs = now + kAssignMs;
        return TryAssign(gunner, refs, noticeController, soldierCore, soldierIndex);
    }

    bool stillOnGun = false;
    if (const char* reason = WhyLeave(gunner, soldierIndex, refs, &stillOnGun))
    {
        Leave(gunner, soldierIndex, stillOnGun ? &refs : nullptr, reason, now);
        return false;
    }
    SoldierFaction_PushNotice(noticeController, soldierIndex, kNoticeAntiAir, heli);   // the gunner node wakes and stays on type 7
    return true;
}

void SoldierFactionAntiAir_OnSetCommand(std::uint32_t soldierIndex, std::uint8_t* command)
{
    if (soldierIndex >= kSoldierCount || !command || MissionCodeGuard::ShouldBypassHooks())
        return;
    Gunner& gunner = g_Gunners[soldierIndex];
    const std::uint16_t heli = gunner.heli.load(std::memory_order_acquire);
    if (heli == kNoHeli || command[kCommandType] != kTurretCommand
        || (command[kCommandTargetKind] & kTargetKindMask) != kTargetGameObject)
        return;
    std::uint16_t target = 0;
    std::memcpy(&target, command + kCommandTarget, sizeof(target));
    if (target != kSupportHeli || !SoldierFaction_AreHostileIds(static_cast<std::uint16_t>(0x400u | soldierIndex), heli))
        return;

    std::memcpy(command + kCommandTarget, &heli, sizeof(heli));
    bool fire = false;
    float heliPoint[3];
    if (!gunner.fired.load(std::memory_order_relaxed) && SoldierFaction_ReadHeliPoint(heli, heliPoint))   // bursts and pauses take turns, as StepAntiAir's own do
    {
        float handle[3];
        std::memcpy(handle, command + kCommandHandle, sizeof(handle));
        handle[1] += kHandleHeight;
        fire = HorizontalSq(handle, heliPoint) <= kGunReachSq && ClearShot(handle, heliPoint);
    }
    command[kCommandFire] = fire ? kFire : kHold;
    gunner.fired.store(fire, std::memory_order_relaxed);
}

void SoldierFactionAntiAir_Forget(std::uint32_t soldierIndex, const char* reason)
{
    if (soldierIndex >= kSoldierCount || g_Gunners[soldierIndex].heli.load(std::memory_order_relaxed) == kNoHeli)
        return;
    CpRefs refs{};
    const bool readable = Refs(&refs);
    const bool onGun = readable && RoleSEH(refs, soldierIndex) == kRoleAntiAir;
    Leave(g_Gunners[soldierIndex], soldierIndex, onGun ? &refs : nullptr, reason, GetTickCount64());
}

bool SoldierFactionAntiAir_IsGunner(std::uint32_t soldierIndex)
{
    return soldierIndex < kSoldierCount && g_Gunners[soldierIndex].heli.load(std::memory_order_relaxed) != kNoHeli;
}

void SoldierFactionAntiAir_OnGaveUp(std::uint32_t soldierIndex)
{
    if (soldierIndex < kSoldierCount)
        g_Gunners[soldierIndex].gaveUp.store(true, std::memory_order_relaxed);
}

void SoldierFactionAntiAir_Clear()
{
    for (Gunner& gunner : g_Gunners)
    {
        gunner.heli.store(kNoHeli, std::memory_order_relaxed);
        gunner.gun.store(kNoGun, std::memory_order_relaxed);
        gunner.gaveUp.store(false, std::memory_order_relaxed);
        gunner.fired.store(false, std::memory_order_relaxed);
        gunner.nextTryMs = 0;
    }
}
