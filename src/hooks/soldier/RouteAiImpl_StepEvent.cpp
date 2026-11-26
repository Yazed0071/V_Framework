#include <pch.h>
#include "RouteAiImpl_StepEvent.h"

#include <Windows.h>
#include <atomic>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "TppPermanentGimmickGatlingGun_SetAimMatrix.h"
#include "log.h"
#include "../fox/gm/GameObjectPosition.h"

namespace
{
    using StepEvent_t           = void(__fastcall*)(std::uint8_t* routeAi, std::uint32_t soldierIndex, std::int32_t proc, std::uint8_t* knowledge, void* extra);
    using SetCommand_t          = void(__fastcall*)(void* actionController, std::uint32_t soldierIndex, std::uint8_t* command);
    using SetAimingActionParam_t = void(__fastcall*)(std::uint8_t* routeAi, std::uint32_t soldierIndex, std::uint8_t* aimingParam, const std::uint8_t* routeEvent, bool keepAiming, bool aimAtCombatTarget);
    using GetTargetPosition_t   = void(__fastcall*)(std::uint8_t* soldier, const std::uint8_t* target, std::uint32_t soldierIndex, float* position);
    using EventCountQuery_t     = std::uint16_t(__fastcall*)(void* routeSet, std::uint16_t route, std::uint16_t node);
    using EventIdQuery_t        = std::uint32_t(__fastcall*)(void* routeSet, std::uint16_t route, std::uint16_t node, std::uint16_t event);
    using EventParamQuery_t     = std::uint32_t(__fastcall*)(void* routeSet, std::uint16_t route, std::uint16_t node, std::uint16_t event, std::uint32_t param);
    using EventByteQuery_t      = std::uint8_t(__fastcall*)(void* routeSet, std::uint16_t route, std::uint16_t node, std::uint16_t event);
    using EventTimeQuery_t      = float(__fastcall*)(void* routeSet, std::uint16_t route, std::uint16_t node, std::uint16_t event);
    using GetQuarkSystemTable_t = std::uint8_t*(__fastcall*)();
    using SendSignal_t          = std::int32_t*(__fastcall*)(void* gameObjects, std::int32_t* result, std::uint32_t gameObjectId, void* signal);

    constexpr std::uint32_t kAttackFireAAG = 0x0A7C0D0A; // AttackFireAAG

    constexpr std::uint32_t kSquatIdle         = 0x8082EE2E; // SquatIdle
    constexpr std::uint32_t kSquatIdleAct      = 0x413F5645; // SquatIdleAct
    constexpr std::uint32_t kRelaxedIdleAct    = 0xEF94DD47; // RelaxedIdleAct
    constexpr std::uint32_t kCautionIdleAct    = 0xB135DCBD; // CautionIdleAct
    constexpr std::uint32_t kSquatActBinocular = 0x61078EC9; // SquatActBinocular
    constexpr std::uint32_t kStandActBinocular = 0x887FD85A; // StandActBinocular

    constexpr std::int32_t kStepEnter     = 0;
    constexpr std::int32_t kStepExit      = 1;
    constexpr std::int32_t kStepUpdate    = 2;
    constexpr std::int32_t kStepPreUpdate = 3;
    constexpr std::int32_t kStepResume    = 4;

    constexpr std::size_t kRouteAiSoldier   = 0x1B0;
    constexpr std::size_t kRouteAiRoutes    = 0x1C0; // one 0x70 record per soldier
    constexpr std::size_t kRouteRecordSize  = 0x70;
    constexpr std::size_t kRouteRecordSet   = 0x48;
    constexpr std::size_t kRouteRecordRoute = 0x50;
    constexpr std::size_t kKnowledgeNode    = 0x20;
    constexpr std::size_t kKnowledgeEvent   = 0x2A;
    constexpr std::size_t kKnowledgeFlags   = 0x28;
    constexpr std::uint8_t kWaitForAct      = 1;    // the event lasts until the action reports the task done, not the node's time
    constexpr std::size_t kEventCountQuery  = 0x18 / 8;
    constexpr std::size_t kEventIdQuery     = 0x28 / 8;
    constexpr std::size_t kEventLoopQuery   = 0x30 / 8;
    constexpr std::size_t kEventParamQuery  = 0x38 / 8;
    constexpr std::size_t kEventTimeQuery   = 0x48 / 8;
    constexpr std::size_t kEventAimQuery    = 0x60 / 8;

    constexpr std::uint32_t kActParam           = 0;
    constexpr std::size_t   kActKind            = 0x04;
    constexpr std::uint32_t kActBinocular       = 6;
    constexpr std::size_t   kActTime            = 0x08;
    constexpr std::size_t   kActRange           = 0x0C;
    constexpr float         kVanillaActRange    = 11.0f; // what both of the game's binocular users put here
    constexpr std::size_t   kActMode            = 0x22;
    constexpr std::uint8_t  kActing             = 2;
    constexpr std::size_t   kActStance          = 0x23;
    constexpr std::uint8_t  kActStand           = 1;
    constexpr std::uint8_t  kActSquat           = 2;
    constexpr std::size_t   kActHold            = 0x24;
    constexpr std::size_t   kCommandSubParam    = 0x40;
    constexpr std::size_t   kCommandSubParamSize = 0x28;
    constexpr std::size_t   kCommandSubKind     = 0xB5;
    constexpr std::uint8_t  kSubKindAim         = 1;
    constexpr std::uint8_t  kIdleActingCommand  = 0x11;
    constexpr std::uint32_t kSquatBinocularFlags = 0x110012; // as the anti-sniper AI sends it
    constexpr std::uint32_t kStandBinocularFlags = 0x112012; // as the mortar spotter sends it
    constexpr std::size_t   kAimLimitsAt        = 0x14;
    constexpr std::uint32_t kAimLimits          = 0x013A001E;
    constexpr std::size_t   kAimLimit2At        = 0x18;
    constexpr std::uint16_t kAimLimit2          = 0x013A;
    constexpr float         kActGrace           = 6.0f; // raising and lowering the binoculars; past this the node ends on time

    constexpr std::uint32_t kFireRateParam   = 0;
    constexpr std::uint32_t kFireTimeParam   = 1;
    constexpr std::uint32_t kCoolTimeParam   = 2;
    constexpr std::uint32_t kTurnSpeedParam  = 3;
    constexpr float         kParamToSeconds  = 0.01f;
    constexpr float         kDefaultTurnSpeed = 45.0f; // degrees per second
    constexpr float         kDegToRad        = 3.14159265f / 180.0f;
    constexpr float         kSwingCheck      = 0.5f;   // hold this long, then look again whether the gun is on target
    constexpr std::uint8_t  kVanillaAccuracy = 1; // hit class the game's anti-air AI uses: 30%

    constexpr std::size_t   kSoldierActionController = 0x50;
    constexpr std::size_t   kSoldierPositions        = 0x18;  // its +0x20 holds one Vector4 per soldier
    constexpr std::size_t   kActionSlots             = 0x10;
    constexpr std::size_t   kActionSlotSize          = 0x1E0;
    constexpr std::size_t   kActionSlotFlags         = 0x19E;
    constexpr std::uint16_t kTaskDone                = 0x10;

    constexpr std::uint32_t kMaxSoldiers  = 0x200;
    constexpr std::uint16_t kSoldierIds   = 0x400;
    constexpr std::uint16_t kGatlingIds   = 0xA000;
    constexpr std::uint32_t kMaxGatlings  = 0x200;
    constexpr std::uint16_t kNoGun        = 0xFFFF;
    constexpr std::uint16_t kSupportHeli  = 0x1C00;
    constexpr std::uint16_t kPlayer       = 0;
    constexpr float         kGunRange     = 10.0f;
    constexpr float         kFireRange    = 150.0f;
    constexpr float         kMuzzleHeight = 1.3f;
    constexpr float         kHandleOffset = -0.666832f; // the gunner stands this far along the gun's facing
    constexpr float         kAimAhead     = 30.0f;
    constexpr float         kOriginRadius = 1.0f;       // objects that aren't out (the uncalled heli) sit at the world origin

    constexpr std::size_t  kTargetSize        = 0x10;
    constexpr std::size_t  kTargetKind        = 0x0C;
    constexpr std::uint8_t kTargetKindMask    = 7;
    constexpr std::uint8_t kTargetPoint       = 1;
    constexpr std::uint8_t kTargetGameObject  = 2;
    constexpr std::size_t  kAimingParamSize   = 0x40;
    constexpr std::size_t  kRouteEventSize    = 0x18;
    constexpr std::size_t  kRouteEventSet     = 0x08;
    constexpr std::size_t  kRouteEventIds     = 0x10; // route, node, event

    constexpr std::uint64_t kUnitSignal   = 0x9C17DEAF1DFCull; // hash of the signal that hands out a gimmick unit (real name unknown)
    constexpr std::size_t   kUnitOwner    = 0x0A;
    constexpr std::size_t   kUnitState    = 0x68;
    constexpr std::uint32_t kUnitBroken   = 1u << 29;
    constexpr std::size_t   kQuarkTable_GameObjects = 0x60;
    constexpr std::size_t   kGameObjects_SendSignal = 0x38 / 8;

    constexpr std::size_t   kCommandSize        = 0xC0;
    constexpr std::size_t   kCommandGunPosition = 0x00;
    constexpr std::size_t   kCommandGunYaw      = 0x10;
    constexpr std::size_t   kCommandBurst       = 0x14;
    constexpr std::size_t   kCommandTarget      = 0x18;
    constexpr std::size_t   kCommandGun         = 0x28;
    constexpr std::size_t   kCommandAccuracy    = 0x2A; // hit class: 1 = 30% per shot
    constexpr std::size_t   kCommandFire        = 0x2B;
    constexpr std::size_t   kCommandHandle      = 0x80;
    constexpr std::size_t   kCommandFlags       = 0xB0;
    constexpr std::size_t   kCommandType        = 0xB4;
    constexpr std::size_t   kCommandNoObject    = 0xB6;
    constexpr std::uint32_t kTurretFlags        = 0x110216;
    constexpr std::uint8_t  kTurretCommand      = 0x16;
    constexpr std::uint8_t  kFire               = 2;
    constexpr std::uint8_t  kHold               = 0;

    struct Gunner
    {
        std::atomic<std::uint16_t> gun{ kNoGun };
        float                      gunPosition[3]{};
        float                      gunYaw = 0.0f;
        float                      handle[3]{};
        std::uint32_t              seed = 0;
        float                      fireTime = 0.0f;       // 0 = the game's own 2.5-3 s bursts
        float                      coolTime = 0.0f;
        bool                       coolNext = false;
        std::atomic<float>         shotInterval{ 0.0f };  // read by the fire control hook
        std::atomic<float>         turnSpeed{ 0.0f };     // radians per second, read by the gatling gun hook
        std::atomic<bool>          onTarget{ false };     // written by the gatling gun hook
        alignas(16) std::uint8_t   target[kTargetSize]{}; // what the node says to shoot at, as the aim code reads it
    };

    struct alignas(16) UnitSignal
    {
        std::uint64_t id;
        std::uint8_t* unit;
        std::uint8_t  pad[0x30];
    };

    StepEvent_t            g_OrigStepEvent        = nullptr;
    SetCommand_t           g_SetCommand           = nullptr;
    SetAimingActionParam_t g_SetAimingActionParam = nullptr;
    GetTargetPosition_t    g_GetTargetPosition    = nullptr;
    void*                  g_HookTarget           = nullptr;
    Gunner                 g_Gunners[kMaxSoldiers];
    std::atomic<float>     g_BinocularDeadline[kMaxSoldiers] = {}; // node time + grace while he does a binocular act, 0 = none

    struct RouteSites
    {
        std::uintptr_t stepEvent;
        std::uintptr_t setAimingActionParam;
        std::uintptr_t getTargetPosition;
    };

    RouteSites RouteSitesForBuild()
    {
        return { gAddr.RouteAiImpl_StepEvent, gAddr.RouteAiImpl_SetAimingActionParam, gAddr.RouteAiImpl_GetTargetPosition };
    }

    const std::uint8_t kStepEventPrologue[] = {
        0x48, 0x8B, 0xC4, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57
    };
    const std::uint8_t kSetAimingActionParamPrologue[] = {
        0x40, 0x53, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x60
    };
    const std::uint8_t kGetTargetPositionPrologue[] = {
        0x48, 0x8B, 0xC4, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x68, 0xB8
    };

    std::uint32_t CurrentEventId(std::uint8_t* routeAi, std::uint32_t soldierIndex, const std::uint8_t* knowledge, std::uint8_t routeEvent[kRouteEventSize])
    {
        __try
        {
            const std::uint8_t* record = *reinterpret_cast<std::uint8_t* const*>(routeAi + kRouteAiRoutes) + soldierIndex * kRouteRecordSize;
            void* routeSet = *reinterpret_cast<void* const*>(record + kRouteRecordSet);
            if (!routeSet)
                return 0;
            const std::uint16_t route = *reinterpret_cast<const std::uint16_t*>(record + kRouteRecordRoute);
            const std::uint16_t node  = *reinterpret_cast<const std::uint16_t*>(knowledge + kKnowledgeNode);
            const std::uint8_t  event = knowledge[kKnowledgeEvent];
            void* const* queries = *reinterpret_cast<void* const* const*>(routeSet);
            if (event >= reinterpret_cast<EventCountQuery_t>(queries[kEventCountQuery])(routeSet, route, node))
                return 0;
            const std::uint16_t ids[3] = { route, node, event };
            std::memcpy(routeEvent + kRouteEventSet, &routeSet, sizeof(routeSet));
            std::memcpy(routeEvent + kRouteEventIds, ids, sizeof(ids));
            return reinterpret_cast<EventIdQuery_t>(queries[kEventIdQuery])(routeSet, route, node, event);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    std::uint32_t ReadEventParam(const std::uint8_t* routeEvent, std::uint32_t param)
    {
        __try
        {
            void* routeSet = *reinterpret_cast<void* const*>(routeEvent + kRouteEventSet);
            const auto* ids = reinterpret_cast<const std::uint16_t*>(routeEvent + kRouteEventIds);
            const auto query = reinterpret_cast<EventParamQuery_t>((*reinterpret_cast<void* const* const*>(routeSet))[kEventParamQuery]);
            return query(routeSet, ids[0], ids[1], ids[2], param);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }


    bool ReadEventShape(const std::uint8_t* routeEvent, bool& loop, float& time, bool& hasTarget)
    {
        __try
        {
            void* routeSet = *reinterpret_cast<void* const*>(routeEvent + kRouteEventSet);
            const auto* ids = reinterpret_cast<const std::uint16_t*>(routeEvent + kRouteEventIds);
            void* const* queries = *reinterpret_cast<void* const* const*>(routeSet);
            loop = reinterpret_cast<EventByteQuery_t>(queries[kEventLoopQuery])(routeSet, ids[0], ids[1], ids[2]) != 0;
            time = reinterpret_cast<EventTimeQuery_t>(queries[kEventTimeQuery])(routeSet, ids[0], ids[1], ids[2]);
            hasTarget = reinterpret_cast<EventByteQuery_t>(queries[kEventAimQuery])(routeSet, ids[0], ids[1], ids[2]) != 0;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void SetGameObjectTarget(std::uint8_t target[kTargetSize], std::uint16_t gameObjectId)
    {
        std::memset(target, 0, kTargetSize);
        std::memcpy(target, &gameObjectId, sizeof(gameObjectId));
        target[kTargetKind] = kTargetGameObject;
    }

    void ReadNodeTarget(std::uint8_t* routeAi, std::uint32_t soldierIndex, const std::uint8_t* routeEvent, std::uint8_t target[kTargetSize])
    {
        alignas(16) std::uint8_t aimingParam[kAimingParamSize] = {};
        __try
        {
            g_SetAimingActionParam(routeAi, soldierIndex, aimingParam, routeEvent, false, false);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            std::memset(aimingParam, 0, sizeof(aimingParam));
        }
        std::memcpy(target, aimingParam, kTargetSize);
        if ((target[kTargetKind] & kTargetKindMask) == 0)
            SetGameObjectTarget(target, kSupportHeli); // no target on the node: the support heli, like GZ's nodes
    }

    bool IsHeliTarget(const std::uint8_t target[kTargetSize])
    {
        std::uint16_t gameObjectId;
        std::memcpy(&gameObjectId, target, sizeof(gameObjectId));
        return (target[kTargetKind] & kTargetKindMask) == kTargetGameObject && (gameObjectId >> 9) == (kSupportHeli >> 9);
    }

    bool SoldierPosition(std::uint8_t* soldier, std::uint32_t soldierIndex, float position[3])
    {
        __try
        {
            const std::uint8_t* positions = *reinterpret_cast<std::uint8_t* const*>(*reinterpret_cast<std::uint8_t* const*>(soldier + kSoldierPositions) + 0x20);
            std::memcpy(position, positions + soldierIndex * 0x10, sizeof(float) * 3);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool TaskDone(std::uint8_t* soldier, std::uint32_t soldierIndex)
    {
        __try
        {
            const std::uint8_t* controller = *reinterpret_cast<std::uint8_t* const*>(soldier + kSoldierActionController);
            const std::uint8_t* slots = *reinterpret_cast<std::uint8_t* const*>(controller + kActionSlots);
            const std::uint16_t flags = *reinterpret_cast<const std::uint16_t*>(slots + soldierIndex * kActionSlotSize + kActionSlotFlags);
            return (flags & kTaskDone) != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint8_t* AskGunUnitSeh(GetQuarkSystemTable_t getQuarkSystemTable, std::uint16_t gun)
    {
        __try
        {
            std::uint8_t* quarkTable = getQuarkSystemTable();
            void* gameObjects = quarkTable ? *reinterpret_cast<void**>(quarkTable + kQuarkTable_GameObjects) : nullptr;
            if (!gameObjects)
                return nullptr;
            const auto sendSignal = reinterpret_cast<SendSignal_t>((*reinterpret_cast<void***>(gameObjects))[kGameObjects_SendSignal]);
            UnitSignal signal{};
            signal.id = kUnitSignal;
            std::int32_t result = -1;
            sendSignal(gameObjects, &result, gun, &signal);
            return signal.unit;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    std::uint8_t* GunUnit(std::uint16_t gun)
    {
        static const GetQuarkSystemTable_t getQuarkSystemTable = gAddr.GetQuarkSystemTable
            ? reinterpret_cast<GetQuarkSystemTable_t>(ResolveGameAddress(gAddr.GetQuarkSystemTable))
            : nullptr;
        return getQuarkSystemTable ? AskGunUnitSeh(getQuarkSystemTable, gun) : nullptr;
    }

    bool GunFreeFor(std::uint16_t gun, std::uint16_t soldierId)
    {
        std::uint8_t* unit = GunUnit(gun);
        if (!unit)
            return false;
        __try
        {
            const std::uint16_t owner = *reinterpret_cast<const std::uint16_t*>(unit + kUnitOwner);
            const std::uint32_t state = *reinterpret_cast<const std::uint32_t*>(unit + kUnitState);
            return (owner == kNoGun || owner == soldierId) && (state & kUnitBroken) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool GunnedByAnother(std::uint16_t gun, std::uint32_t soldierIndex)
    {
        for (std::uint32_t i = 0; i < kMaxSoldiers; ++i)
        {
            if (i != soldierIndex && g_Gunners[i].gun.load(std::memory_order_relaxed) == gun)
                return true;
        }
        return false;
    }

    void Rotate(const float q[4], const float v[3], float out[3])
    {
        const float t[3] = {
            2.0f * (q[1] * v[2] - q[2] * v[1]),
            2.0f * (q[2] * v[0] - q[0] * v[2]),
            2.0f * (q[0] * v[1] - q[1] * v[0])
        };
        out[0] = v[0] + q[3] * t[0] + (q[1] * t[2] - q[2] * t[1]);
        out[1] = v[1] + q[3] * t[1] + (q[2] * t[0] - q[0] * t[2]);
        out[2] = v[2] + q[3] * t[2] + (q[0] * t[1] - q[1] * t[0]);
    }

    float DistanceSquared(const float a[3], const float b[3])
    {
        const float dx = a[0] - b[0];
        const float dy = a[1] - b[1];
        const float dz = a[2] - b[2];
        return dx * dx + dy * dy + dz * dz;
    }

    bool TakeNearestGun(std::uint8_t* soldier, std::uint32_t soldierIndex, Gunner& gunner)
    {
        float soldierPosition[3];
        if (!SoldierPosition(soldier, soldierIndex, soldierPosition))
            return false;
        const std::uint16_t soldierId = static_cast<std::uint16_t>(kSoldierIds | (soldierIndex & 0x1FF));

        std::uint16_t bestGun = kNoGun;
        float bestDistance = kGunRange * kGunRange;
        float bestPosition[3]{};
        float bestRotation[4]{};
        for (std::uint32_t i = 0; i < kMaxGatlings; ++i)
        {
            const std::uint16_t gun = static_cast<std::uint16_t>(kGatlingIds | i);
            float position[3];
            float rotation[4];
            if (!GameObjectPosition::ReadTransform(gun, position, rotation))
                continue;
            const float distance = DistanceSquared(position, soldierPosition);
            if (distance > bestDistance || !GunFreeFor(gun, soldierId) || GunnedByAnother(gun, soldierIndex))
                continue;
            bestGun = gun;
            bestDistance = distance;
            std::memcpy(bestPosition, position, sizeof(position));
            std::memcpy(bestRotation, rotation, sizeof(rotation));
        }
        if (bestGun == kNoGun)
            return false;

        const float forward[3] = { 0.0f, 0.0f, 1.0f };
        const float behind[3] = { 0.0f, 0.0f, kHandleOffset };
        float facing[3];
        float handle[3];
        Rotate(bestRotation, forward, facing);
        Rotate(bestRotation, behind, handle);
        std::memcpy(gunner.gunPosition, bestPosition, sizeof(bestPosition));
        gunner.gunYaw = std::atan2(facing[0], facing[2]);
        for (int i = 0; i < 3; ++i)
            gunner.handle[i] = bestPosition[i] + handle[i];
        gunner.seed = soldierIndex * 2654435761u + 1u;
        gunner.gun.store(bestGun, std::memory_order_relaxed);
        return true;
    }

    std::uint32_t NextRandom(Gunner& gunner)
    {
        std::uint32_t x = gunner.seed;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 5;
        gunner.seed = x;
        return x;
    }

    bool ResolveTarget(std::uint8_t* soldier, std::uint32_t soldierIndex, const std::uint8_t target[kTargetSize], float position[3])
    {
        alignas(16) float resolved[4] = {};
        __try
        {
            g_GetTargetPosition(soldier, target, soldierIndex, resolved);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        std::memcpy(position, resolved, sizeof(float) * 3);
        const float origin[3] = {};
        return DistanceSquared(position, origin) > kOriginRadius * kOriginRadius;
    }

    void AimAhead(const Gunner& gunner, std::uint8_t target[kTargetSize])
    {
        const float point[3] = {
            gunner.gunPosition[0] + std::sin(gunner.gunYaw) * kAimAhead,
            gunner.gunPosition[1] + kMuzzleHeight,
            gunner.gunPosition[2] + std::cos(gunner.gunYaw) * kAimAhead
        };
        std::memset(target, 0, kTargetSize);
        std::memcpy(target, point, sizeof(point));
        target[kTargetKind] = kTargetPoint;
    }

    template <typename T>
    void Put(std::uint8_t* command, std::size_t offset, T value)
    {
        std::memcpy(command + offset, &value, sizeof(value));
    }

    void SendBurst(std::uint8_t* soldier, std::uint32_t soldierIndex, Gunner& gunner)
    {
        const std::uint16_t gun = gunner.gun.load(std::memory_order_relaxed);
        if (gun == kNoGun)
            return;
        if (!GunFreeFor(gun, static_cast<std::uint16_t>(kSoldierIds | (soldierIndex & 0x1FF))))
        {
            gunner.gun.store(kNoGun, std::memory_order_relaxed);
            return;
        }

        alignas(16) std::uint8_t target[kTargetSize];
        std::memcpy(target, gunner.target, sizeof(target));
        float aim[3];
        bool present = ResolveTarget(soldier, soldierIndex, target, aim);
        if (!present && IsHeliTarget(target))
        {
            SetGameObjectTarget(target, kPlayer); // the heli isn't called in, so he turns the gun on the player
            present = ResolveTarget(soldier, soldierIndex, target, aim);
        }
        if (!present)
            AimAhead(gunner, target);
        const float muzzle[3] = { gunner.gunPosition[0], gunner.gunPosition[1] + kMuzzleHeight, gunner.gunPosition[2] };
        bool fire = present && DistanceSquared(aim, muzzle) <= kFireRange * kFireRange;
        const std::uint32_t roll = NextRandom(gunner);
        float burst = fire ? 2.5f + static_cast<float>(roll % 6) * 0.1f
                           : 1.0f + static_cast<float>(roll & 0xF) * 0.1f;
        if (fire && gunner.coolNext && gunner.coolTime > 0.0f)
        {
            fire = false; // a hold command still tracks the target
            burst = gunner.coolTime;
        }
        else if (fire && GatlingGunTurn_IsActive() && !gunner.onTarget.load(std::memory_order_relaxed))
        {
            fire = false; // the gun is still swinging round
            burst = kSwingCheck;
        }
        else if (fire && gunner.fireTime > 0.0f)
        {
            burst = gunner.fireTime;
        }
        gunner.coolNext = fire;

        alignas(16) std::uint8_t command[kCommandSize] = {};
        std::memcpy(command + kCommandGunPosition, gunner.gunPosition, sizeof(gunner.gunPosition));
        Put<float>(command, kCommandGunYaw, gunner.gunYaw);
        Put<float>(command, kCommandBurst, burst);
        std::memcpy(command + kCommandTarget, target, sizeof(target));
        Put<std::uint16_t>(command, kCommandGun, gun);
        Put<std::uint8_t>(command, kCommandAccuracy, kVanillaAccuracy);
        Put<std::uint8_t>(command, kCommandFire, fire ? kFire : kHold);
        std::memcpy(command + kCommandHandle, gunner.handle, sizeof(gunner.handle));
        Put<std::uint32_t>(command, kCommandFlags, kTurretFlags);
        Put<std::uint8_t>(command, kCommandType, kTurretCommand);
        Put<std::uint16_t>(command, kCommandNoObject, 0xFFFF);

        __try
        {
            g_SetCommand(*reinterpret_cast<void**>(soldier + kSoldierActionController), soldierIndex, command);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            gunner.gun.store(kNoGun, std::memory_order_relaxed);
        }
    }

    std::uint8_t BinocularStance(std::uint32_t eventId, const std::uint8_t* routeEvent)
    {
        if (eventId == kSquatIdleAct || eventId == kSquatIdle)
            return ReadEventParam(routeEvent, kActParam) == kSquatActBinocular ? kActSquat : 0;
        if (eventId == kRelaxedIdleAct || eventId == kCautionIdleAct)
            return ReadEventParam(routeEvent, kActParam) == kStandActBinocular ? kActStand : 0;
        return 0;
    }

    bool AimAtNodeTarget(std::uint8_t* routeAi, std::uint32_t soldierIndex, const std::uint8_t* routeEvent, std::uint8_t* command)
    {
        alignas(16) std::uint8_t aiming[kAimingParamSize] = {};
        Put<std::uint32_t>(aiming, kAimLimitsAt, kAimLimits);
        Put<std::uint16_t>(aiming, kAimLimit2At, kAimLimit2);
        __try
        {
            g_SetAimingActionParam(routeAi, soldierIndex, aiming, routeEvent, false, false);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        std::memcpy(command + kCommandSubParam, aiming, kCommandSubParamSize);
        command[kCommandSubKind] = kSubKindAim;
        return true;
    }

    bool SendCommandSEH(std::uint8_t* soldier, std::uint32_t soldierIndex, std::uint8_t* command)
    {
        __try
        {
            g_SetCommand(*reinterpret_cast<void**>(soldier + kSoldierActionController), soldierIndex, command);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool SendBinocularAct(std::uint8_t* routeAi, std::uint8_t* soldier, std::uint32_t soldierIndex, const std::uint8_t* routeEvent, std::uint8_t stance, float& time)
    {
        bool loop = false;
        bool hasTarget = false;
        if (!ReadEventShape(routeEvent, loop, time, hasTarget))
            return false;
        alignas(16) std::uint8_t command[kCommandSize] = {};
        Put<std::uint32_t>(command, kActKind, kActBinocular);
        Put<float>(command, kActTime, time);
        Put<float>(command, kActRange, kVanillaActRange);
        Put<std::uint8_t>(command, kActMode, kActing);
        Put<std::uint8_t>(command, kActStance, stance);
        Put<std::uint8_t>(command, kActHold, loop ? 1 : 0);
        if (hasTarget)
            AimAtNodeTarget(routeAi, soldierIndex, routeEvent, command);
        Put<std::uint32_t>(command, kCommandFlags, stance == kActSquat ? kSquatBinocularFlags : kStandBinocularFlags);
        Put<std::uint8_t>(command, kCommandType, kIdleActingCommand);
        return SendCommandSEH(soldier, soldierIndex, command);
    }

    void __fastcall hk_StepEvent(std::uint8_t* routeAi, std::uint32_t soldierIndex, std::int32_t proc, std::uint8_t* knowledge, void* extra)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigStepEvent, routeAi, soldierIndex, proc, knowledge, extra);
        if (soldierIndex >= kMaxSoldiers)
        {
            g_OrigStepEvent(routeAi, soldierIndex, proc, knowledge, extra);
            return;
        }

        const float binocularDeadline = g_BinocularDeadline[soldierIndex].load(std::memory_order_relaxed);
        if (proc == kStepPreUpdate && binocularDeadline > 0.0f && *reinterpret_cast<const float*>(knowledge) > binocularDeadline)
            knowledge[kKnowledgeFlags] &= static_cast<std::uint8_t>(~kWaitForAct); // the act never finished (the game turned it down), so the node ends on its time

        Gunner& gunner = g_Gunners[soldierIndex];
        if (proc == kStepPreUpdate && gunner.gun.load(std::memory_order_relaxed) != kNoGun)
        {
            float& waited = *reinterpret_cast<float*>(knowledge); // the event ends once this passes its time, so the gunner never runs out
            const float realWait = waited;
            waited = -FLT_MAX;
            g_OrigStepEvent(routeAi, soldierIndex, proc, knowledge, extra);
            waited = realWait;
            return;
        }
        g_OrigStepEvent(routeAi, soldierIndex, proc, knowledge, extra);
        std::uint8_t* soldier = *reinterpret_cast<std::uint8_t**>(routeAi + kRouteAiSoldier);
        if (proc == kStepExit)
        {
            gunner.gun.store(kNoGun, std::memory_order_relaxed);
            g_BinocularDeadline[soldierIndex].store(0.0f, std::memory_order_relaxed);
            return;
        }
        if (proc == kStepEnter || proc == kStepResume)
        {
            gunner.gun.store(kNoGun, std::memory_order_relaxed);
            g_BinocularDeadline[soldierIndex].store(0.0f, std::memory_order_relaxed);
            alignas(8) std::uint8_t routeEvent[kRouteEventSize] = {};
            const std::uint32_t eventId = CurrentEventId(routeAi, soldierIndex, knowledge, routeEvent);
            const std::uint8_t binocularStance = BinocularStance(eventId, routeEvent);
            float binocularTime = 0.0f;
            if (binocularStance && SendBinocularAct(routeAi, soldier, soldierIndex, routeEvent, binocularStance, binocularTime))
            {
                knowledge[kKnowledgeFlags] |= kWaitForAct; // like the game's own acts, so the lowering clip is never cut
                g_BinocularDeadline[soldierIndex].store(binocularTime + kActGrace, std::memory_order_relaxed);
                return;
            }
            if (eventId != kAttackFireAAG)
                return;
            ReadNodeTarget(routeAi, soldierIndex, routeEvent, gunner.target);
            const std::uint32_t roundsPerMinute = ReadEventParam(routeEvent, kFireRateParam);
            gunner.shotInterval.store(roundsPerMinute ? 60.0f / static_cast<float>(roundsPerMinute) : 0.0f, std::memory_order_relaxed);
            gunner.fireTime = static_cast<float>(ReadEventParam(routeEvent, kFireTimeParam)) * kParamToSeconds;
            gunner.coolTime = static_cast<float>(ReadEventParam(routeEvent, kCoolTimeParam)) * kParamToSeconds;
            gunner.coolNext = false;
            const std::uint32_t degreesPerSecond = ReadEventParam(routeEvent, kTurnSpeedParam);
            gunner.turnSpeed.store((degreesPerSecond ? static_cast<float>(degreesPerSecond) : kDefaultTurnSpeed) * kDegToRad, std::memory_order_relaxed);
            gunner.onTarget.store(false, std::memory_order_relaxed);
            if (TakeNearestGun(soldier, soldierIndex, gunner))
                SendBurst(soldier, soldierIndex, gunner);
            return;
        }
        if (proc == kStepUpdate && gunner.gun.load(std::memory_order_relaxed) != kNoGun && TaskDone(soldier, soldierIndex))
            SendBurst(soldier, soldierIndex, gunner);
    }
}

void AntiAirGunRoute_Clear()
{
    for (Gunner& gunner : g_Gunners)
        gunner.gun.store(kNoGun, std::memory_order_relaxed);
    for (auto& deadline : g_BinocularDeadline)
        deadline.store(0.0f, std::memory_order_relaxed);
}

float AntiAirGunRoute_ShotInterval(std::uint32_t soldierIndex)
{
    if (soldierIndex >= kMaxSoldiers || g_Gunners[soldierIndex].gun.load(std::memory_order_relaxed) == kNoGun)
        return 0.0f;
    return g_Gunners[soldierIndex].shotInterval.load(std::memory_order_relaxed);
}

float AntiAirGunRoute_TurnSpeed(std::uint32_t soldierIndex)
{
    if (soldierIndex >= kMaxSoldiers || g_Gunners[soldierIndex].gun.load(std::memory_order_relaxed) == kNoGun)
        return 0.0f;
    return g_Gunners[soldierIndex].turnSpeed.load(std::memory_order_relaxed);
}

void AntiAirGunRoute_NoteGunOnTarget(std::uint32_t soldierIndex, bool onTarget)
{
    if (soldierIndex < kMaxSoldiers)
        g_Gunners[soldierIndex].onTarget.store(onTarget, std::memory_order_relaxed);
}

bool Install_RouteAiImpl_StepEvent_Hook()
{
    if (g_HookTarget)
        return true;
    const RouteSites sites = RouteSitesForBuild();
    if (!sites.stepEvent)
        return true;

    auto* target               = static_cast<std::uint8_t*>(ResolveGameAddress(sites.stepEvent));
    auto* setAimingActionParam = static_cast<std::uint8_t*>(ResolveGameAddress(sites.setAimingActionParam));
    auto* getTargetPosition    = static_cast<std::uint8_t*>(ResolveGameAddress(sites.getTargetPosition));
    g_SetCommand = gAddr.ActionController_SetCommand
        ? reinterpret_cast<SetCommand_t>(ResolveGameAddress(gAddr.ActionController_SetCommand))
        : nullptr;
    const HookEntry stepEventEntry      = CheckHookEntry(target, kStepEventPrologue, sizeof(kStepEventPrologue));
    const HookEntry aimingParamEntry    = CheckHookEntry(setAimingActionParam, kSetAimingActionParamPrologue, sizeof(kSetAimingActionParamPrologue));
    const HookEntry targetPositionEntry = CheckHookEntry(getTargetPosition, kGetTargetPositionPrologue, sizeof(kGetTargetPositionPrologue));
    if (!g_SetCommand
        || stepEventEntry == HookEntry::Unknown
        || aimingParamEntry == HookEntry::Unknown
        || targetPositionEntry == HookEntry::Unknown)
    {
        LogError("AntiAirGunRoute: soldier route code does not match %s, AttackFireAAG and binocular route nodes stay a standing idle",
            GetGameBuildName(gGameBuild));
        return false;
    }
    if (stepEventEntry == HookEntry::ForeignHook)
        LogHookedOnTop("AntiAirGunRoute: RouteAiImpl::StepEvent", target);
    if (aimingParamEntry == HookEntry::ForeignHook)
        LogHookedOnTop("AntiAirGunRoute: SetAimingActionParam", setAimingActionParam);
    if (targetPositionEntry == HookEntry::ForeignHook)
        LogHookedOnTop("AntiAirGunRoute: GetTargetPosition", getTargetPosition);
    g_SetAimingActionParam = reinterpret_cast<SetAimingActionParam_t>(setAimingActionParam);
    g_GetTargetPosition    = reinterpret_cast<GetTargetPosition_t>(getTargetPosition);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_StepEvent), reinterpret_cast<void**>(&g_OrigStepEvent)))
    {
        LogError("AntiAirGunRoute: RouteAiImpl::StepEvent hook failed, AttackFireAAG and binocular route nodes stay a standing idle");
        return false;
    }
    g_HookTarget = target;
    return true;
}

bool Uninstall_RouteAiImpl_StepEvent_Hook()
{
    if (g_HookTarget)
    {
        DisableAndRemoveHook(g_HookTarget);
        g_HookTarget = nullptr;
        g_OrigStepEvent = nullptr;
    }
    AntiAirGunRoute_Clear();
    return true;
}
