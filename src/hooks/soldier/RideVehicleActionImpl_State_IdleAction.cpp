#include <pch.h>
#include "RideVehicleActionImpl_State_IdleAction.h"

#include <Windows.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"
#include "RouteAiImpl_SetSubActionParamWithNodeEvent.h"

namespace
{
    using State_IdleAction_t      = void(__fastcall*)(void* rideAction, std::uint32_t slot, std::uint32_t proc, void* param);
    using UpdateShootState_t      = void(__fastcall*)(void* rideAction, std::uint32_t slot);
    using GetVehicleTurretCount_t = std::int32_t(__fastcall*)(const void* commInfo);
    using GetVehicleId_t          = void(__fastcall*)(void* coreAi, std::uint16_t* vehicleId, std::uint32_t soldier);

    constexpr std::size_t   kWorkBase         = 0x1E8;
    constexpr std::size_t   kCommonWorks      = 0x1D0; // 0x28 per rider: +0 the vehicle's comm info, +0x24 the seat
    constexpr std::size_t   kCommonWorkSize   = 0x28;
    constexpr std::size_t   kCommonWorkSeat   = 0x24;
    constexpr std::size_t   kCommonWorkVehicle   = 0x18;
    constexpr std::size_t   kCommonWorkSerial    = 0x1C; // serial of the last order this ride took
    constexpr std::size_t   kCommonWorkNextState = 0x21;
    constexpr std::uint8_t  kRideStateGetOut     = 5;
    constexpr std::size_t   kSoldierHub       = 0x88;
    constexpr std::size_t   kHubCoreAi        = 0x48;  // vt+0xA8 GetVehicleId, 0xFFFF once the vehicle is released
    constexpr std::size_t   kCoreAiGetVehicleId = 0xA8;
    constexpr std::size_t   kTaskVehicle      = 0x14;
    constexpr std::size_t   kTaskSerial       = 0xBA;
    constexpr std::size_t   kRealWorks        = 0x1D8; // 0x80 per rider: +0x68 the soldier, +0x78 shoot flags
    constexpr std::size_t   kRealWorkSize     = 0x80;
    constexpr std::size_t   kRealWorkSoldier  = 0x68;
    constexpr std::size_t   kRealWorkFlags    = 0x78;
    constexpr std::size_t   kTaskTable        = 0x98;  // +8 one 0xD0 task per soldier
    constexpr std::size_t   kTaskSize         = 0xD0;
    constexpr std::size_t   kTaskType         = 0xC6;
    constexpr std::size_t   kTaskAlertRoute   = 0x1F;
    constexpr std::size_t   kTaskKeepNotice   = 0x1E;
    constexpr std::uint32_t kAlertRouteFlag   = 0x2000;
    constexpr std::uint32_t kKeepNoticeFlag   = 0x1000;
    constexpr std::uint8_t  kVehicleRideTask  = 0x19;
    constexpr std::size_t   kTaskTargetKind   = 0x4C;  // & 7: 0 none, 4 the player
    constexpr std::uint8_t  kTargetKindMask   = 0x07;
    constexpr std::size_t   kTaskSweep        = 0x5C;  // +-3 lateral sweep of a suppressing burst
    constexpr std::size_t   kTaskShootLevel   = 0x5E;  // spread step
    constexpr std::size_t   kTaskShootOn      = 0x5F;
    constexpr std::size_t   kTaskFeetShotHold = 0x63;
    constexpr std::size_t   kTaskFeetShot     = 0x65;  // 1 = walk-in shots that start short of the target
    constexpr std::size_t   kTaskOrderKind    = 0xC7;
    constexpr std::uint8_t  kAttackOrder      = 1;
    constexpr std::uint8_t  kVanillaShootLevel = 3;    // what the vehicle combat AI's fire orders carry
    constexpr std::size_t   kRealWorkShotPause = 0x58; // pause the walk-in shots set after a few hits
    constexpr std::uint32_t kTargetSeenFlag   = 0x400; // player in sight, or seen within the last 2 s
    constexpr std::uint32_t kShotRefusedFlag  = 0x100; // aim angle or line of fire refused the last check
    constexpr std::size_t   kCommInfoTurretBits = 0x74;
    constexpr std::uint32_t kTurretTrigger    = 0x8;   // turret 0
    constexpr std::uint32_t kFireStateAttackOrder = 1;
    constexpr std::uint32_t kFireStateFiring  = 2;
    constexpr std::uint32_t kFireStateSeen    = 4;
    constexpr std::uint32_t kFireStateTrigger = 8;
    constexpr std::uint32_t kFireStateRefused = 0x10;
    constexpr std::uint32_t kProcStateCheck   = 0;
    constexpr std::uint32_t kProcUpdate       = 3;
    constexpr std::size_t   kCommInfoCode     = 0x6C;  // vehicle type << 4 | sub type
    constexpr std::uint8_t  kWesternLightVehicle = 1;
    constexpr std::uint8_t  kEasternLightVehicle = 2;
    constexpr std::uint8_t  kMachineGunSubType   = 1;
    constexpr std::uint32_t kMaxSoldiers      = 512;
    constexpr ULONGLONG     kGunnerHoldMs     = 1500;
    constexpr std::size_t   kCommInfoFrameTime    = 0x38; // seconds the vehicle's last update covered
    constexpr std::size_t   kCommInfoTurretSpeed  = 0x4C; // turret 0's turn rate, radians per second
    constexpr std::size_t   kCommInfoTurretYaw    = 0x60; // where turret 0 is ordered to point, radians
    constexpr std::size_t   kCommInfoVehicleIndex = 0x78;
    constexpr std::uint32_t kVehicleIndexMask     = 0x1FF;
    constexpr std::uint32_t kMaxVehicles          = 512;
    constexpr ULONGLONG     kGunYawFreshMs        = 500;
    constexpr float         kFullTurn             = 6.2831853f;

    constexpr std::uint64_t kFrontIdleClip    = 0xFC528240061626DAull; // /Assets/tpp/motion/SI_game/fani/bodies/enet/enetvcl/enetvcl_fr_idl.gani
    constexpr std::uint64_t kFrontIdleVibClip  = 0xFC53B5B6A528E356ull; // /Assets/tpp/motion/SI_game/fani/bodies/enet/enetvcl/enetvcl_fr_idl_vib.gani
    constexpr std::uint64_t kFrontReadyClip    = 0xFC51395048F0A529ull; // /Assets/tpp/motion/SI_game/fani/bodies/enet/enetvcl/enetvcl_fr_rdy_idl_p.gani
    constexpr std::uint64_t kFrontWipeClip     = 0xFC502ED320650006ull; // /Assets/tpp/motion/SI_game/fani/bodies/enet/enetvcl/enetvcl_fr_wipe.gani
    constexpr std::uint64_t kFrontDamageClip   = 0xFC52C902FFD85AECull; // /Assets/tpp/motion/SI_game/fani/bodies/enet/enetvcl/enetvcl_fr_dam.gani
    constexpr std::uint64_t kFrontDeathClip    = 0xFC501DE977684099ull; // /Assets/tpp/motion/SI_game/fani/bodies/enet/enetvcl/enetvcl_fr_dead_fall.gani
    constexpr std::uint64_t kMachineGunIdleClip = 0xFC517756B9697D9Bull; // /Assets/tpp/motion/SI_game/fani/bodies/enet/enethew/enethew_s_mgn_idl.gani
    constexpr std::uint64_t kRearDamageClip    = 0xFC52FDF8F9813D9Eull; // /Assets/tpp/motion/SI_game/fani/bodies/enet/enetvcl/enetvcl_br_dam.gani
    constexpr std::uint64_t kRearDeathClip     = 0xFC516B82BAB5BBE3ull; // /Assets/tpp/motion/SI_game/fani/bodies/enet/enetvcl/enetvcl_br_dead_fall_r.gani

    const std::uint8_t kStateIdleActionPrologue[] = {
        0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8B, 0xEC, 0x48, 0x81,
        0xEC, 0x80, 0x00, 0x00, 0x00, 0x8B, 0xC2, 0x44, 0x8B, 0xE2, 0x4C, 0x8B, 0xF1
    };
    const std::uint8_t kGetVehicleTurretCountPrologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57,
        0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xE9, 0x32, 0xDB
    };

    State_IdleAction_t      g_OrigStateIdleAction   = nullptr;
    UpdateShootState_t      g_UpdateShootState      = nullptr;
    GetVehicleTurretCount_t g_GetVehicleTurretCount = nullptr;
    void*                   g_HookTarget            = nullptr;
    std::atomic<ULONGLONG>  g_GunnerSeen[kMaxSoldiers] = {};
    std::atomic<float>      g_GunYaw[kMaxVehicles]     = {};
    std::atomic<ULONGLONG>  g_GunYawTick[kMaxVehicles] = {};

    bool BytesAtSEH(const void* at, const std::uint8_t* bytes, std::size_t size)
    {
        __try
        {
            return at && std::memcmp(at, bytes, size) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::int32_t TurretCountSEH(const void* commInfo)
    {
        __try
        {
            return g_GetVehicleTurretCount(commInfo);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    std::uint8_t VehicleCodeSEH(const void* commInfo)
    {
        __try
        {
            return static_cast<const std::uint8_t*>(commInfo)[kCommInfoCode];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    bool VehicleRideTaskSEH(const void* rideAction, const JeepGunnerRide& ride, std::uint32_t& flags)
    {
        __try
        {
            const auto* tasks = *reinterpret_cast<const std::uint8_t* const*>(*reinterpret_cast<const std::uint8_t* const*>(
                static_cast<const std::uint8_t*>(rideAction) + kTaskTable) + 8);
            const std::uint8_t* task = tasks + static_cast<std::size_t>(ride.soldier) * kTaskSize;
            if (task[kTaskType] != kVehicleRideTask)
                return false;
            flags = *reinterpret_cast<const std::uint32_t*>(ride.realWork + kRealWorkFlags) & ~(kAlertRouteFlag | kKeepNoticeFlag);
            if (task[kTaskAlertRoute])
                flags |= kAlertRouteFlag;
            if (task[kTaskKeepNotice])
                flags |= kKeepNoticeFlag;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    struct GunnerCadence
    {
        ULONGLONG     phaseEnd;
        std::uint32_t seed;
        bool          firing;
    };
    GunnerCadence g_Cadence[kMaxSoldiers] = {};

    // The emplaced machine gun rhythm (CombatTurretAi): fire 2.5-3.0 s, pause 1.0-2.0 s, only while the player is in sight
    // or was within the last 2 s
    void StepGunnerCadence(GunnerCadence& cadence, bool targetSeen)
    {
        const ULONGLONG now = GetTickCount64();
        if (!targetSeen)
        {
            cadence.firing = false;
            cadence.phaseEnd = now;
            return;
        }
        if (now < cadence.phaseEnd)
            return;
        cadence.seed = cadence.seed * 1664525u + 1013904223u;
        const std::uint32_t roll = cadence.seed >> 16;
        cadence.firing = !cadence.firing;
        cadence.phaseEnd = now + (cadence.firing ? 2500 + 100 * (roll % 6) : 1000 + 100 * (roll % 11));
    }

    // Runs the vanilla shoot state on the gunner's vehicle order with the fire bytes set by our cadence: aimed at the player
    // (no walk-in feet shots, no sweep, no pause after them). Returns kFireState* bits, 0 when no attack order is running.
    std::uint32_t ShootAsGunnerSEH(void* rideAction, std::uint32_t slot, const JeepGunnerRide& ride)
    {
        std::uint32_t flags = 0;
        if (!g_UpdateShootState || ride.soldier >= kMaxSoldiers || !VehicleRideTaskSEH(rideAction, ride, flags))
            return 0;
        __try
        {
            auto* realFlags = reinterpret_cast<std::uint32_t*>(ride.realWork + kRealWorkFlags);
            const bool targetSeen = (*realFlags & kTargetSeenFlag) != 0;
            *realFlags = flags;
            auto* tasks = *reinterpret_cast<std::uint8_t**>(*reinterpret_cast<std::uint8_t**>(
                static_cast<std::uint8_t*>(rideAction) + kTaskTable) + 8);
            std::uint8_t* task = tasks + static_cast<std::size_t>(ride.soldier) * kTaskSize;
            if (task[kTaskOrderKind] != kAttackOrder || (task[kTaskTargetKind] & kTargetKindMask) == 0)
            {
                g_UpdateShootState(rideAction, slot);
                return 0;
            }

            GunnerCadence& cadence = g_Cadence[ride.soldier];
            StepGunnerCadence(cadence, targetSeen);
            const std::uint16_t serial = *reinterpret_cast<const std::uint16_t*>(task + kTaskSerial);
            const std::uint8_t sweep = task[kTaskSweep];
            const std::uint8_t level = task[kTaskShootLevel];
            const std::uint8_t shootOn = task[kTaskShootOn];
            const std::uint8_t feetShotHold = task[kTaskFeetShotHold];
            const std::uint8_t feetShot = task[kTaskFeetShot];
            if (cadence.firing)
            {
                task[kTaskSweep] = 0;
                task[kTaskShootLevel] = kVanillaShootLevel;
                task[kTaskShootOn] = 1;
                task[kTaskFeetShotHold] = 0;
                task[kTaskFeetShot] = 0;
                *reinterpret_cast<float*>(ride.realWork + kRealWorkShotPause) = 0.0f;
            }
            else
            {
                task[kTaskShootOn] = 0;
            }
            g_UpdateShootState(rideAction, slot);
            if (*reinterpret_cast<const std::uint16_t*>(task + kTaskSerial) == serial)
            {
                task[kTaskSweep] = sweep;
                task[kTaskShootLevel] = level;
                task[kTaskShootOn] = shootOn;
                task[kTaskFeetShotHold] = feetShotHold;
                task[kTaskFeetShot] = feetShot;
            }

            std::uint32_t state = kFireStateAttackOrder;
            if (cadence.firing)
                state |= kFireStateFiring;
            if (targetSeen)
                state |= kFireStateSeen;
            if (*reinterpret_cast<const std::uint32_t*>(ride.commInfo + kCommInfoTurretBits) & kTurretTrigger)
                state |= kFireStateTrigger;
            if (*realFlags & kShotRefusedFlag)
                state |= kFireStateRefused;
            return state;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    struct RideOrderState
    {
        std::uint16_t serial;
        std::uint8_t  nextState;
    };

    bool ReadRideOrderStateSEH(const JeepGunnerRide& ride, RideOrderState& state)
    {
        __try
        {
            state.serial = *reinterpret_cast<const std::uint16_t*>(ride.common + kCommonWorkSerial);
            state.nextState = ride.common[kCommonWorkNextState];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // Idle proc 0 puts a rider out of a slow vehicle when his order is not a vehicle order, or names his vehicle with another
    // seat: every combat and notice order says seat 0. The gunner stays for those while his CoreAi still owns the vehicle; a
    // get-off sub-command, an order for another vehicle or a released vehicle (Lua unset, get-off step) still take him out.
    // Returns the order type he was kept against, -1 when nothing changed.
    int KeepGunnerAboardSEH(void* rideAction, const JeepGunnerRide& ride, const RideOrderState& before)
    {
        __try
        {
            std::uint8_t* common = ride.common;
            if (common[kCommonWorkNextState] != kRideStateGetOut || before.nextState == kRideStateGetOut
                || *reinterpret_cast<const std::uint16_t*>(common + kCommonWorkSerial) != before.serial)
                return -1;
            auto* self = static_cast<std::uint8_t*>(rideAction);
            const auto* tasks = *reinterpret_cast<const std::uint8_t* const*>(*reinterpret_cast<const std::uint8_t* const*>(
                self + kTaskTable) + 8);
            const std::uint8_t* task = tasks + static_cast<std::size_t>(ride.soldier) * kTaskSize;
            const std::uint16_t vehicle = *reinterpret_cast<const std::uint16_t*>(common + kCommonWorkVehicle);
            const bool vehicleOrder = task[kTaskType] == kVehicleRideTask;
            if (vehicleOrder && *reinterpret_cast<const std::uint16_t*>(task + kTaskVehicle) != vehicle)
                return -1;
            auto* hub = *reinterpret_cast<std::uint8_t**>(self + kSoldierHub);
            auto* coreAi = *reinterpret_cast<std::uint8_t**>(hub + kHubCoreAi);
            const auto getVehicleId = *reinterpret_cast<GetVehicleId_t*>(*reinterpret_cast<std::uint8_t**>(coreAi) + kCoreAiGetVehicleId);
            std::uint16_t owned = 0xFFFF;
            getVehicleId(coreAi, &owned, ride.soldier);
            if (owned != vehicle)
                return -1;
            common[kCommonWorkNextState] = before.nextState;
            if (vehicleOrder)
                *reinterpret_cast<std::uint16_t*>(common + kCommonWorkSerial) = *reinterpret_cast<const std::uint16_t*>(task + kTaskSerial);
            return task[kTaskType];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
    }

#ifdef _DEBUG
    std::atomic<std::uint32_t> g_ProbeLast[kMaxSoldiers] = {};
    std::atomic<ULONGLONG>     g_ProbeKeptTick[kMaxSoldiers] = {};
    std::atomic<std::uint32_t> g_ProbeFireState[kMaxSoldiers] = {};
    std::atomic<std::uint32_t> g_ProbeLineChecks[kMaxSoldiers] = {};
    std::atomic<std::uint32_t> g_ProbeLineBlocked[kMaxSoldiers] = {};

    void TurretAnglesSEH(const std::uint8_t* commInfo, float& yaw, float& pitch)
    {
        __try
        {
            yaw = *reinterpret_cast<const float*>(commInfo + 0x60) * 57.29578f;
            pitch = *reinterpret_cast<const float*>(commInfo + 0x54) * 57.29578f;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            yaw = pitch = 0.0f;
        }
    }

    std::uint16_t TaskTypeAndSubSEH(const void* rideAction, std::uint32_t soldier)
    {
        __try
        {
            const auto* tasks = *reinterpret_cast<const std::uint8_t* const*>(*reinterpret_cast<const std::uint8_t* const*>(
                static_cast<const std::uint8_t*>(rideAction) + kTaskTable) + 8);
            const auto* task = tasks + static_cast<std::size_t>(soldier) * kTaskSize;
            return static_cast<std::uint16_t>(task[kTaskType] << 8 | task[0x1B]); // +0x1B = the vehicle order's sub-command, 2 = get out
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0xFFFF;
        }
    }

    std::uint32_t ShootFlagsSEH(const std::uint8_t* realWork)
    {
        __try
        {
            return *reinterpret_cast<const std::uint32_t*>(realWork + kRealWorkFlags);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    void ProbeGunner(const void* rideAction, std::uint32_t proc, const JeepGunnerRide& ride)
    {
        if (ride.soldier >= kMaxSoldiers)
            return;
        const std::uint16_t task = TaskTypeAndSubSEH(rideAction, ride.soldier);
        const std::uint32_t enterOrLeave = proc == 1 || proc == 2 ? proc : 0;
        const std::uint32_t key = static_cast<std::uint32_t>(task) << 8 | enterOrLeave;
        if (g_ProbeLast[ride.soldier].exchange(key, std::memory_order_relaxed) == key)
            return;
        LogDebug("JeepGunner probe: soldier %u idle proc %u task 0x%02X sub %u seat %u flags 0x%08X",
            ride.soldier, proc, task >> 8, task & 0xFF, ride.seat, ShootFlagsSEH(ride.realWork));
    }
#endif

    struct GunTurn
    {
        std::uint32_t vehicle;
        float         orderedYaw;
        float         step; // radians the gun can turn in one update
    };

    bool ReadGunTurnSEH(const void* commInfo, GunTurn& turn)
    {
        __try
        {
            const auto* info = static_cast<const std::uint8_t*>(commInfo);
            turn.vehicle = *reinterpret_cast<const std::uint16_t*>(info + kCommInfoVehicleIndex) & kVehicleIndexMask;
            turn.orderedYaw = *reinterpret_cast<const float*>(info + kCommInfoTurretYaw);
            turn.step = *reinterpret_cast<const float*>(info + kCommInfoTurretSpeed) * *reinterpret_cast<const float*>(info + kCommInfoFrameTime);
            return std::isfinite(turn.orderedYaw) && std::isfinite(turn.step) && turn.step >= 0.0f;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // The gun swings to its ordered yaw at its own turn rate (WeaponBase::UpdateTurretRotationSpecs). The gunner turns by
    // the same step each update, so he stays behind the gun instead of jumping to where it is ordered to point.
    void TurnGunnerWithGun(const void* commInfo)
    {
        GunTurn turn{};
        if (!ReadGunTurnSEH(commInfo, turn))
            return;
        const ULONGLONG now = GetTickCount64();
        const bool fresh = now - g_GunYawTick[turn.vehicle].exchange(now, std::memory_order_relaxed) < kGunYawFreshMs;
        float shown = fresh ? g_GunYaw[turn.vehicle].load(std::memory_order_relaxed) : turn.orderedYaw;
        const float left = std::remainder(turn.orderedYaw - shown, kFullTurn);
        shown = std::fabs(left) <= turn.step ? turn.orderedYaw : std::remainder(shown + std::copysign(turn.step, left), kFullTurn);
        g_GunYaw[turn.vehicle].store(shown, std::memory_order_relaxed);
    }

    void __fastcall hk_State_IdleAction(void* rideAction, std::uint32_t slot, std::uint32_t proc, void* param)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigStateIdleAction, rideAction, slot, proc, param);
        JeepGunnerRide ride{};
        const bool gunner = JeepGunner_ReadRide(rideAction, slot, ride) && ride.seat == kJeepGunnerSeat
            && JeepGunner_IsArmedLightVehicle(ride.commInfo);
        if (gunner && ride.soldier < kMaxSoldiers)
            g_GunnerSeen[ride.soldier].store(GetTickCount64(), std::memory_order_release);
#ifdef _DEBUG
        if (gunner)
            ProbeGunner(rideAction, proc, ride);
#endif
        RideOrderState before{};
        const bool guardStateCheck = gunner && proc == kProcStateCheck && ReadRideOrderStateSEH(ride, before);
        if (gunner && proc == kProcUpdate)
            TurnGunnerWithGun(ride.commInfo);
        g_OrigStateIdleAction(rideAction, slot, proc, param);
        if (guardStateCheck)
        {
            const int keptAgainst = KeepGunnerAboardSEH(rideAction, ride, before);
#ifdef _DEBUG
            const ULONGLONG now = GetTickCount64();
            if (keptAgainst >= 0 && ride.soldier < kMaxSoldiers
                && now - g_ProbeKeptTick[ride.soldier].load(std::memory_order_relaxed) > 1000)
            {
                g_ProbeKeptTick[ride.soldier].store(now, std::memory_order_relaxed);
                LogDebug("JeepGunner probe: kept soldier %u aboard against order type 0x%02X", ride.soldier, keptAgainst);
            }
#else
            (void)keptAgainst;
#endif
        }
        if (gunner && proc == kProcUpdate)
        {
            const std::uint32_t fireState = ShootAsGunnerSEH(rideAction, slot, ride);
#ifdef _DEBUG
            if (ride.soldier < kMaxSoldiers && g_ProbeFireState[ride.soldier].exchange(fireState, std::memory_order_relaxed) != fireState)
            {
                float yaw = 0.0f;
                float pitch = 0.0f;
                TurretAnglesSEH(ride.commInfo, yaw, pitch);
                LogDebug("JeepGunner fire: soldier %u attack order %u firing %u seen %u trigger %u refused %u | turret yaw %.0f pitch %.0f deg | soldier line checks %u, blocked %u",
                    ride.soldier, fireState & kFireStateAttackOrder ? 1 : 0, fireState & kFireStateFiring ? 1 : 0,
                    fireState & kFireStateSeen ? 1 : 0, fireState & kFireStateTrigger ? 1 : 0, fireState & kFireStateRefused ? 1 : 0,
                    yaw, pitch, g_ProbeLineChecks[ride.soldier].exchange(0, std::memory_order_relaxed),
                    g_ProbeLineBlocked[ride.soldier].exchange(0, std::memory_order_relaxed));
            }
#else
            (void)fireState;
#endif
        }
    }
}

bool JeepGunner_ReadRide(const void* rideAction, std::uint32_t slot, JeepGunnerRide& ride)
{
    __try
    {
        const auto* self = static_cast<const std::uint8_t*>(rideAction);
        const std::uint32_t work = slot - *reinterpret_cast<const std::uint32_t*>(self + kWorkBase);
        ride.common = *reinterpret_cast<std::uint8_t* const*>(self + kCommonWorks) + work * kCommonWorkSize;
        ride.commInfo = *reinterpret_cast<const std::uint8_t* const*>(ride.common);
        ride.seat = ride.common[kCommonWorkSeat];
        ride.realWork = *reinterpret_cast<std::uint8_t* const*>(self + kRealWorks) + work * kRealWorkSize;
        ride.soldier = *reinterpret_cast<const std::uint32_t*>(ride.realWork + kRealWorkSoldier);
        return ride.commInfo != nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool JeepGunner_IsArmedLightVehicle(const void* commInfo)
{
    if (!commInfo || !g_GetVehicleTurretCount)
        return false;
    const std::uint8_t code = VehicleCodeSEH(commInfo);
    const std::uint8_t type = code >> 4;
    if ((type != kWesternLightVehicle && type != kEasternLightVehicle) || (code & 0xF) != kMachineGunSubType)
        return false;
    return TurretCountSEH(commInfo) > 0;
}

bool JeepGunner_IsGunner(std::uint32_t soldierIndex)
{
    if (soldierIndex >= kMaxSoldiers || MissionCodeGuard::ShouldBypassHooks())
        return false;
    const ULONGLONG seen = g_GunnerSeen[soldierIndex].load(std::memory_order_acquire);
    return seen && GetTickCount64() - seen < kGunnerHoldMs;
}

float JeepGunner_GunYaw(const void* commInfo)
{
    GunTurn turn{};
    if (!ReadGunTurnSEH(commInfo, turn))
        return 0.0f;
    const bool fresh = GetTickCount64() - g_GunYawTick[turn.vehicle].load(std::memory_order_relaxed) < kGunYawFreshMs;
    return fresh ? g_GunYaw[turn.vehicle].load(std::memory_order_relaxed) : turn.orderedYaw;
}

namespace
{
    constexpr std::size_t   kCommInfoRiders    = 0x30; // 4 rider game object ids by seat, 0xFFFF = empty
    constexpr std::size_t   kRiderCount        = 4;
    constexpr std::size_t   kSearchIds         = 0x00;
    constexpr std::size_t   kSearchEntries     = 0x08;
    constexpr std::size_t   kSearchCount       = 0x14;
    constexpr std::size_t   kSearchEntryStride = 0x1C;
    constexpr std::uint32_t kSoldierIndexMask  = 0x1FF;

    struct CrewScope
    {
        bool          active;
        std::uint32_t gunner;
        std::uint16_t riders[kRiderCount];
    };
    thread_local CrewScope t_CrewScope{};

    bool ReadCrewSEH(const void* commonWork, CrewScope& scope)
    {
        __try
        {
            const auto* commInfo = *static_cast<const std::uint8_t* const*>(commonWork);
            if (!JeepGunner_IsArmedLightVehicle(commInfo))
                return false;
            std::memcpy(scope.riders, commInfo + kCommInfoRiders, sizeof(scope.riders));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint32_t FilterCrewSEH(std::uint8_t* result, std::uint32_t count, const CrewScope& scope)
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
                bool crew = (ids[i] & kSoldierIndexMask) == scope.gunner;
                for (std::size_t seat = 0; seat < kRiderCount && !crew; ++seat)
                    crew = scope.riders[seat] != 0xFFFF && scope.riders[seat] == ids[i];
                if (crew)
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
}

bool JeepGunner_BeginCrewScope(std::uint32_t shooterIndex, const void* commonWork)
{
    const std::uint32_t gunner = shooterIndex & kSoldierIndexMask;
    if (!commonWork || !JeepGunner_IsGunner(gunner))
        return false;
    CrewScope scope{ true, gunner, {} };
    if (!ReadCrewSEH(commonWork, scope))
        return false;
    t_CrewScope = scope;
    return true;
}

void JeepGunner_EndCrewScope()
{
    t_CrewScope.active = false;
}

#ifdef _DEBUG
void JeepGunner_ProbeLineCheck(std::uint32_t shooterIndex, bool blocked)
{
    const std::uint32_t gunner = shooterIndex & kSoldierIndexMask;
    if (gunner >= kMaxSoldiers)
        return;
    g_ProbeLineChecks[gunner].fetch_add(1, std::memory_order_relaxed);
    if (blocked)
        g_ProbeLineBlocked[gunner].fetch_add(1, std::memory_order_relaxed);
}
#endif

std::uint32_t JeepGunner_FilterCrewSearch(std::uint8_t* result, std::uint32_t count)
{
    if (!t_CrewScope.active || !result || count == 0)
        return count;
    return FilterCrewSEH(result, count, t_CrewScope);
}

std::uint64_t JeepGunner_Clip(std::uint64_t clip, void* animControl)
{
    std::uint64_t gunnerClip = 0;
    switch (clip)
    {
    case kFrontIdleClip:
    case kFrontIdleVibClip:
    case kFrontReadyClip:
    case kFrontWipeClip:   gunnerClip = kMachineGunIdleClip; break;
    case kFrontDamageClip: gunnerClip = kRearDamageClip; break;
    case kFrontDeathClip:  gunnerClip = kRearDeathClip; break;
    default:               return 0;
    }
    return JeepGunner_IsGunner(SquatIdleRoute_SoldierOfAnimControl(animControl)) ? gunnerClip : 0;
}

bool Install_RideVehicleActionImpl_State_IdleAction_Hook()
{
    if (g_HookTarget)
        return true;
    auto* idleAction = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.SoldierRideVehicleActionImpl_State_IdleAction));
    auto* turretCount = ResolveGameAddress(gAddr.VehicleCommunicationInfo_GetVehicleTurretCount);
    g_UpdateShootState = reinterpret_cast<UpdateShootState_t>(ResolveGameAddress(gAddr.SoldierRideVehicleActionImpl_UpdateShootState));
    if (!idleAction || !turretCount || !g_UpdateShootState)
        return true;
    if (!BytesAtSEH(turretCount, kGetVehicleTurretCountPrologue, sizeof(kGetVehicleTurretCountPrologue)))
    {
        LogError("JeepGunner: vehicle turret count code does not match %s, armed jeeps have no gunner", GetGameBuildName(gGameBuild));
        return false;
    }
    const HookEntry entry = CheckHookEntry(idleAction, kStateIdleActionPrologue, sizeof(kStateIdleActionPrologue));
    if (entry == HookEntry::Unknown)
    {
        LogError("JeepGunner: soldier vehicle idle code does not match %s, armed jeeps have no gunner", GetGameBuildName(gGameBuild));
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("JeepGunner: RideVehicleActionImpl::State_IdleAction", idleAction);
    g_GetVehicleTurretCount = reinterpret_cast<GetVehicleTurretCount_t>(turretCount);
    if (!CreateAndEnableHook(idleAction, reinterpret_cast<void*>(&hk_State_IdleAction), reinterpret_cast<void**>(&g_OrigStateIdleAction)))
    {
        g_GetVehicleTurretCount = nullptr;
        LogError("JeepGunner: RideVehicleActionImpl::State_IdleAction hook failed, armed jeeps have no gunner");
        return false;
    }
    g_HookTarget = idleAction;
    return true;
}

bool Uninstall_RideVehicleActionImpl_State_IdleAction_Hook()
{
    if (g_HookTarget)
    {
        DisableAndRemoveHook(g_HookTarget);
        g_HookTarget = nullptr;
        g_OrigStateIdleAction = nullptr;
    }
    g_GetVehicleTurretCount = nullptr;
    for (auto& seen : g_GunnerSeen)
        seen.store(0, std::memory_order_relaxed);
    for (auto& tick : g_GunYawTick)
        tick.store(0, std::memory_order_relaxed);
    return true;
}
