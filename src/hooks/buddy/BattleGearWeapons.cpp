#include <pch.h>

#include "BattleGearWeapons.h"

#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "BattleGearBuddy.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"
#include "../player/Fova2ControllerImpl_ApplyPlayerFova.h"

namespace
{
    constexpr std::uint16_t kHeliSpaceBand = 4;   // missionCode / 10000 == 4: the ACC and every heli space
    constexpr std::size_t   kSystemBuddyComm = 0x18;    // walkergear SystemImpl: the buddy walker's comm block
    constexpr std::size_t   kCommRider       = 0x2C8;   // rider game object id
    constexpr std::size_t   kCommRideState   = 0x55C;   // 1 while the rider sits on it
    constexpr std::size_t   kCommAimTarget   = 0x370;   // the rider's aim point
    constexpr std::size_t   kAttackOwner     = 0x70;
    constexpr std::size_t   kOwnerType       = 0x08;
    constexpr std::uint16_t kNoRider         = 0xFFFF;
    constexpr std::uint16_t kTypeMask        = 0xFE00;
    constexpr std::uint8_t  kRiding          = 1;
    constexpr std::uint16_t kBuddyWalkerType = 0x2C;
    constexpr ULONGLONG     kAiAttackHoldMs  = 3000;
    constexpr std::uint8_t  kStealthGrade    = 2;   // the support head's second grade adds the stealth wait
    constexpr std::uint8_t  kBaseGrade       = 1;
    constexpr std::uint8_t  kNormal          = 0;
    constexpr std::uint8_t  kUpright         = 1;
    constexpr std::uint8_t  kFirePosture     = 2;
    constexpr float         kStanceHoldTime  = 0.4f;   // seconds the crouch button stays down before a press counts as a hold
    constexpr std::size_t   kCommActions     = 0x560;
    constexpr std::size_t   kCommCarry       = 0x564;
    constexpr std::uint32_t kInteracting     = 0x4000000;    // the walker's interactive action runs (carry or stab)
    constexpr std::uint32_t kPickingUp       = 0x8000000;
    constexpr std::uint32_t kPuttingDown     = 0x10000000;
    constexpr std::uint32_t kHolding         = 0x8000;
    constexpr std::uint32_t kLifting         = 0x10;      // the walker's Fulton state runs
    constexpr std::uint32_t kLifted          = 0x2000;    // and has ended: the balloon carries it off
    constexpr std::uint32_t kDropping        = 0x1000;    // its drop-in runs: HaloActionImpl's fall, until its landing has ended
    constexpr float         kFastestGun      = 40.0f;     // m/s; a faster roof gun is a warp, not a speed
    constexpr float         kRoundHeat       = 1.0f / 30.0f;   // WeaponCupolaHeavyMachineGunImpl::FireSpecs, for each round
    constexpr float         kCoolRate        = 0.125f;         // and its update, each second
    constexpr float         kOverheated      = 1.0f;
    constexpr std::uint32_t kCupolaGunAmmo   = 500;            // VehicleCommunicationInfo::GetVehicleDefaultAmmoCount, tanks, turret 1
    constexpr ULONGLONG     kRiderAimFreshMs = 250;

    struct Aim
    {
        float target[3];
        bool  ridden;
    };

    struct Muzzles
    {
        float railGun[3];
        float heavyMg[3];
        float hatches[3][3];
        float railGunAim[3];
        float heavyMgAim[3];
    };

    struct WeaponName
    {
        const char*  name;
        std::uint8_t type;
    };

    struct alignas(16) CarryZone
    {
        float centre[4];
        float half[4];
    };
    static_assert(offsetof(CarryZone, half) == 16, "the carry-search cave reads the half sizes 16 bytes after the centre");

    struct alignas(16) CarryFind
    {
        float        position[4];
        std::uint8_t found;
    };
    static_assert(offsetof(CarryFind, found) == 16, "the carry-check cave sets the found byte 16 bytes after the position");

    struct HullSteeringState
    {
        std::atomic<std::uintptr_t> comm{ 0 };
        std::atomic<float>          heading{ 0.0f };
    };
    static_assert(offsetof(HullSteeringState, heading) == 8, "the facing-read cave reads the heading 8 bytes after the comm");

    constexpr WeaponName kMainWeapons[] = { { "RAIL_GUN", battleGearWeapons::kRailGun }, { "H_MISSILE", battleGearWeapons::kHMissile } };
    constexpr WeaponName kHeads[] = { { "SUPPORT", battleGearWeapons::kSupportHead }, { "SCOUTING", battleGearWeapons::kScoutingHead },
                                      { "INTERCEPT", battleGearWeapons::kInterceptHead } };

    std::atomic<bool>           g_LoadoutActive{ false };
    std::atomic<bool>           g_LoadoutLatched{ false };
    std::atomic<std::uint8_t>   g_Chosen{ battleGearWeapons::kRailGun };
    std::atomic<std::uint8_t>   g_Latched{ battleGearWeapons::kRailGun };
    std::atomic<std::uintptr_t> g_Comm{ 0 };
    std::atomic<std::uint8_t>   g_FireMode{ 0 };
    std::atomic<std::uint8_t>   g_Aiming{ 0 };
    std::atomic<std::uint8_t>   g_StancePresses{ 0 };
    std::atomic<std::uint8_t>   g_StanceHeld{ 0 };
    std::atomic<std::uint8_t>   g_Stance{ kNormal };
    std::atomic<bool>           g_InputKnown{ false };   // cleared when the stance resets: the next ride frame starts counting afresh
    std::atomic<bool>           g_Ridden{ false };
    static_assert(sizeof(std::atomic<bool>) == 1 && std::atomic<bool>::is_always_lock_free, "code caves read the ridden flag as a byte");
    std::atomic<bool>           g_HideMainGunAtSetup{ false };
    std::atomic<std::uint8_t>   g_ChosenHead{ battleGearWeapons::kSupportHead };
    std::atomic<bool>           g_ScriptChoseMain{ false };
    std::atomic<bool>           g_ScriptChoseHead{ false };
    std::atomic<std::uint8_t>   g_PageMain{ 0 };
    std::atomic<std::uint8_t>   g_PageHead{ 0 };
    std::atomic<ULONGLONG>      g_AiAttackUntil{ 0 };
    HullSteeringState           g_Hull{};
    std::atomic<float>          g_BodyHeading{ 0.0f };
    std::atomic<bool>           g_BodyHeadingKnown{ false };
    std::atomic<int>            g_RailShotLevel{ 3 };
    std::atomic<std::uint32_t>  g_RailShots{ 0 };
    std::atomic<bool>           g_MgOverheated{ false };
    std::atomic<bool>           g_RailCharging{ false };
    std::atomic<std::uint32_t>  g_MgShots{ 0 };

    SRWLOCK g_SurgeLock = SRWLOCK_INIT;
    float   g_Surge = 0.0f;
    std::atomic<float> g_DriveSpeed{ 0.0f };
    std::atomic<float> g_DriveTurn{ 0.0f };
    std::atomic<bool>  g_Pivoting{ false };
    std::atomic<bool>  g_FirstPerson{ false };
    std::atomic<bool>  g_SidearmInHand{ false };

    SRWLOCK g_AimLock = SRWLOCK_INIT;
    Aim     g_Aim{};
    SRWLOCK g_MuzzleLock = SRWLOCK_INIT;
    Muzzles g_Muzzles{};
    bool    g_MuzzlesValid = false;
    float   g_RoofGun[16] = {};
    float   g_RoofGunVelocity[3] = {};
    bool    g_RoofGunValid = false;
    std::atomic<bool>  g_RoofGunTurning{ false };
    std::atomic<float> g_MgHeat{ 0.0f };
    std::atomic<bool>  g_Lifted{ false };
    std::atomic<float> g_GroundSpeed{ 0.0f };
    float              g_RiderAim[3] = {};          // under g_AimLock
    ULONGLONG          g_RiderAimAt = 0;
    std::atomic<bool>  g_MgSelected{ false };
    std::atomic<bool>  g_MgTrigger{ false };
    constexpr ULONGLONG    kWalkerClockFreshMs = 250;   // no walker frame for this long: its clock does not run
    std::atomic<float>     g_WalkerTime{ 0.0f };
    std::atomic<ULONGLONG> g_WalkerTimeAt{ 0 };
    std::atomic<std::uint32_t> g_MgAmmo{ kCupolaGunAmmo };
    SRWLOCK            g_HeatLock = SRWLOCK_INIT;
    float              g_Heat = 0.0f;
    bool               g_Overheated = false;

    CarryZone g_CarryZone{};   // the carry-search cave reads it unlocked: a torn read only shifts one search a little
    CarryFind g_CarryFind{};   // the carry-check cave writes it unlocked: a torn read only shifts one reach of the tail a little
    SRWLOCK   g_HoldLock = SRWLOCK_INIT;
    float     g_HoldPoint[16] = {};
    bool      g_HoldPointValid = false;

    std::atomic<bool> g_Climbing{ false };
    SRWLOCK           g_HatchLock = SRWLOCK_INIT;
    float             g_Hatch[16] = {};
    bool              g_HatchValid = false;
    float             g_Lid[16] = {};
    bool              g_LidValid = false;
    std::atomic<float> g_LidHold{ -1.0f };
    float             g_HullFrame[16] = {};
    float             g_HullClearance = 0.0f;
    bool              g_HullFrameValid = false;

    // only the ride update touches these
    std::uint8_t g_SeenPresses = 0;
    bool         g_PressPending = false;   // the button went down and is neither a tap nor a hold yet
    float        g_PressHeldFor = 0.0f;
    bool         g_DashWas = false;

    void SetStance(std::uint8_t stance)
    {
        g_Stance.store(stance, std::memory_order_relaxed);
        g_FireMode.store(stance == kFirePosture ? 1 : 0, std::memory_order_relaxed);
    }

    std::uint8_t Tapped(std::uint8_t stance)
    {
        return stance == kNormal ? kUpright : kNormal;
    }

    std::uint8_t Held(std::uint8_t stance)
    {
        return stance == kFirePosture ? kUpright : kFirePosture;
    }

    std::uint8_t* BuddyCommSEH()
    {
        __try
        {
            auto* const* system = static_cast<std::uint8_t* const*>(ResolveGameAddress(gAddr.WalkerGear_SystemImpl_Instance));
            if (!system || !*system)
                return nullptr;
            return *reinterpret_cast<std::uint8_t* const*>(*system + kSystemBuddyComm);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    bool ReadAimSEH(const std::uint8_t* comm, Aim& aim)
    {
        __try
        {
            const std::uint16_t rider = *reinterpret_cast<const std::uint16_t*>(comm + kCommRider);
            aim.ridden = rider != kNoRider && (rider & kTypeMask) == 0 && comm[kCommRideState] == kRiding;
            std::memcpy(aim.target, comm + kCommAimTarget, sizeof(aim.target));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ReadCarryBitsSEH(const std::uint8_t* comm, std::uint32_t& actions, std::uint32_t& carry)
    {
        __try
        {
            actions = *reinterpret_cast<const std::uint32_t*>(comm + kCommActions);
            carry = *reinterpret_cast<const std::uint32_t*>(comm + kCommCarry);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint16_t OwnerTypeSEH(const void* attackAction)
    {
        __try
        {
            const auto* owner = *reinterpret_cast<const std::uint8_t* const*>(static_cast<const std::uint8_t*>(attackAction) + kAttackOwner);
            return owner ? *reinterpret_cast<const std::uint16_t*>(owner + kOwnerType) : 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    void Copy3(float* to, const float* from)
    {
        to[0] = from[0];
        to[1] = from[1];
        to[2] = from[2];
    }
}

namespace battleGearWeapons
{
    void SetLoadoutActive(bool active)
    {
        g_LoadoutActive.store(active, std::memory_order_release);
    }

    void LatchLoadout()
    {
        g_Latched.store(EffectiveMainWeapon(), std::memory_order_relaxed);
        g_LoadoutLatched.store(g_LoadoutActive.load(std::memory_order_acquire) && battleGear::IsBattleGearOut()
                                   && MissionCodeGuard::GetCurrentMissionCode() / 10000 != kHeliSpaceBand,
                               std::memory_order_release);
    }

    bool LoadoutApplies()
    {
        return g_LoadoutLatched.load(std::memory_order_acquire) && g_LoadoutActive.load(std::memory_order_acquire)
            && battleGear::IsBattleGearOut();
    }

    bool SetChosenMainWeapon(const char* name)
    {
        if (!name)
            return false;
        for (const WeaponName& w : kMainWeapons)
        {
            if (std::strcmp(name, w.name) == 0)
            {
                g_Chosen.store(w.type, std::memory_order_relaxed);
                g_ScriptChoseMain.store(true, std::memory_order_relaxed);
                return true;
            }
        }
        return false;
    }

    const char* ChosenMainWeaponName()
    {
        const std::uint8_t type = EffectiveMainWeapon();
        for (const WeaponName& w : kMainWeapons)
            if (w.type == type)
                return w.name;
        return kMainWeapons[0].name;
    }

    std::uint8_t MainWeaponType()
    {
        return g_Latched.load(std::memory_order_relaxed);
    }

    bool SetChosenHead(const char* name)
    {
        if (!name)
            return false;
        for (const WeaponName& h : kHeads)
        {
            if (std::strcmp(name, h.name) == 0)
            {
                g_ChosenHead.store(h.type, std::memory_order_relaxed);
                g_ScriptChoseHead.store(true, std::memory_order_relaxed);
                return true;
            }
        }
        return false;
    }

    const char* ChosenHeadName()
    {
        const std::uint8_t type = HeadType();
        for (const WeaponName& h : kHeads)
            if (h.type == type)
                return h.name;
        return kHeads[0].name;
    }

    std::uint8_t HeadType()
    {
        const std::uint8_t page = g_PageHead.load(std::memory_order_relaxed);
        return g_ScriptChoseHead.load(std::memory_order_relaxed) || !page ? g_ChosenHead.load(std::memory_order_relaxed) : page;
    }

    void SetPagePicks(std::uint8_t mainWeapon, std::uint8_t head)
    {
        for (const WeaponName& w : kMainWeapons)
            if (w.type == mainWeapon)
                g_PageMain.store(mainWeapon, std::memory_order_relaxed);
        for (const WeaponName& h : kHeads)
            if (h.type == head)
                g_PageHead.store(head, std::memory_order_relaxed);
    }

    std::uint8_t PageMainWeapon()
    {
        return g_PageMain.load(std::memory_order_relaxed);
    }

    std::uint8_t PageHead()
    {
        return g_PageHead.load(std::memory_order_relaxed);
    }

    std::uint8_t EffectiveMainWeapon()
    {
        const std::uint8_t page = g_PageMain.load(std::memory_order_relaxed);
        return g_ScriptChoseMain.load(std::memory_order_relaxed) || !page ? g_Chosen.load(std::memory_order_relaxed) : page;
    }

    void ClearScriptPicks()
    {
        g_ScriptChoseMain.store(false, std::memory_order_relaxed);
        g_ScriptChoseHead.store(false, std::memory_order_relaxed);
        g_Chosen.store(kRailGun, std::memory_order_relaxed);
        g_ChosenHead.store(kSupportHead, std::memory_order_relaxed);
    }

    std::uint8_t HeadGrade()
    {
        return HeadType() == kSupportHead ? kStealthGrade : kBaseGrade;
    }

    void SetHideMainGunAtSetup(bool hide)
    {
        g_HideMainGunAtSetup.store(hide, std::memory_order_release);
    }

    bool HideMainGunAtSetup()
    {
        return g_HideMainGunAtSetup.load(std::memory_order_acquire);
    }

    void OnFieldFrame()
    {
        std::uint8_t* comm = BuddyCommSEH();
        g_Comm.store(reinterpret_cast<std::uintptr_t>(comm), std::memory_order_release);
        Aim aim{};
        aim.ridden = comm && ReadAimSEH(comm, aim) && aim.ridden;
        if (!aim.ridden)
        {
            SetStance(kNormal);
            ClearSurge();
            PublishDriveSpeed(0.0f);
            PublishDriveTurn(0.0f);
            PublishPivot(false);
            SetFirstPerson(false);
            PublishSidearmInHand(false);
            g_InputKnown.store(false, std::memory_order_release);
            g_Aiming.store(0, std::memory_order_relaxed);
            g_MgSelected.store(false, std::memory_order_relaxed);
            g_MgTrigger.store(false, std::memory_order_relaxed);
            StopHullSteering();
            if (!Climbing() && PlayerMesh::AllHidden())
                PlayerMesh::SetAllHidden(false);   // a ride that ended without its own end (checkpoint reload, death)
        }
        g_Ridden.store(aim.ridden, std::memory_order_relaxed);
        AcquireSRWLockExclusive(&g_AimLock);
        if (aim.ridden && GetTickCount64() - g_RiderAimAt <= kRiderAimFreshMs)
            Copy3(aim.target, g_RiderAim);
        g_Aim = aim;
        ReleaseSRWLockExclusive(&g_AimLock);
    }

    void OnNotOut()
    {
        if (PlayerMesh::AllHidden())
            PlayerMesh::SetAllHidden(false);
        if (!g_Comm.load(std::memory_order_relaxed) && !g_Ridden.load(std::memory_order_relaxed))
            return;
        StopHullSteering();
        g_BodyHeadingKnown.store(false, std::memory_order_relaxed);
        g_Comm.store(0, std::memory_order_release);
        SetStance(kNormal);
        ClearSurge();
        PublishDriveSpeed(0.0f);
        PublishDriveTurn(0.0f);
        PublishPivot(false);
        SetFirstPerson(false);
        PublishSidearmInHand(false);
        g_InputKnown.store(false, std::memory_order_release);
        g_Aiming.store(0, std::memory_order_relaxed);
        g_Ridden.store(false, std::memory_order_relaxed);
        g_MgOverheated.store(false, std::memory_order_relaxed);
        g_RailCharging.store(false, std::memory_order_relaxed);
        AcquireSRWLockExclusive(&g_AimLock);
        g_Aim = {};
        ReleaseSRWLockExclusive(&g_AimLock);
        AcquireSRWLockExclusive(&g_MuzzleLock);
        g_MuzzlesValid = false;
        g_RoofGunValid = false;
        ReleaseSRWLockExclusive(&g_MuzzleLock);
        g_RoofGunTurning.store(false, std::memory_order_relaxed);
        ResetMgHeat();
        g_MgSelected.store(false, std::memory_order_relaxed);
        g_MgTrigger.store(false, std::memory_order_relaxed);
        g_MgAmmo.store(kCupolaGunAmmo, std::memory_order_relaxed);
        g_GroundSpeed.store(0.0f, std::memory_order_relaxed);
        g_Lifted.store(false, std::memory_order_relaxed);
        AcquireSRWLockExclusive(&g_HoldLock);
        g_HoldPointValid = false;
        ReleaseSRWLockExclusive(&g_HoldLock);
        g_Climbing.store(false, std::memory_order_relaxed);
        AcquireSRWLockExclusive(&g_HatchLock);
        g_HatchValid = false;
        g_LidValid = false;
        g_HullFrameValid = false;
        ReleaseSRWLockExclusive(&g_HatchLock);
        g_LidHold.store(-1.0f, std::memory_order_relaxed);
    }

    const void* BattleGearCommSlot()
    {
        return &g_Comm;
    }

    const void* RiddenSlot()
    {
        return &g_Ridden;
    }

    CarryPhase Carry()
    {
        const auto* comm = reinterpret_cast<const std::uint8_t*>(g_Comm.load(std::memory_order_acquire));
        std::uint32_t actions = 0;
        std::uint32_t carry = 0;
        if (!comm || !ReadCarryBitsSEH(comm, actions, carry) || !(actions & kInteracting))
            return CarryPhase::None;
        if (carry & kPuttingDown)
            return CarryPhase::PuttingDown;
        if (carry & kPickingUp)
            return CarryPhase::PickingUp;
        return carry & kHolding ? CarryPhase::Holding : CarryPhase::None;
    }

    const void* CarryZoneSlot()
    {
        return &g_CarryZone;
    }

    void PublishCarryZone(const float* centre, float halfSize)
    {
        for (int i = 0; i < 3; ++i)
        {
            g_CarryZone.centre[i] = centre[i];
            g_CarryZone.half[i] = halfSize;
        }
        g_CarryZone.centre[3] = 1.0f;
        g_CarryZone.half[3] = halfSize;
    }

    void* CarryFindSlot()
    {
        return &g_CarryFind;
    }

    bool CarryFound(float* position)
    {
        for (int i = 0; i < 3; ++i)
            position[i] = g_CarryFind.position[i];
        return g_CarryFind.found != 0;
    }

    void PublishHoldPoint(const float* matrix)
    {
        AcquireSRWLockExclusive(&g_HoldLock);
        std::memcpy(g_HoldPoint, matrix, sizeof(g_HoldPoint));
        g_HoldPointValid = true;
        ReleaseSRWLockExclusive(&g_HoldLock);
    }

    bool HoldPoint(float* matrix)
    {
        AcquireSRWLockShared(&g_HoldLock);
        const bool valid = g_HoldPointValid;
        std::memcpy(matrix, g_HoldPoint, sizeof(g_HoldPoint));
        ReleaseSRWLockShared(&g_HoldLock);
        return valid;
    }

    void SetClimbing(bool climbing)
    {
        g_Climbing.store(climbing, std::memory_order_relaxed);
    }

    bool Climbing()
    {
        return g_Climbing.load(std::memory_order_relaxed);
    }

    void PublishHatch(const float* matrix)
    {
        AcquireSRWLockExclusive(&g_HatchLock);
        std::memcpy(g_Hatch, matrix, sizeof(g_Hatch));
        g_HatchValid = true;
        ReleaseSRWLockExclusive(&g_HatchLock);
    }

    bool Hatch(float* matrix)
    {
        AcquireSRWLockShared(&g_HatchLock);
        const bool valid = g_HatchValid;
        std::memcpy(matrix, g_Hatch, sizeof(g_Hatch));
        ReleaseSRWLockShared(&g_HatchLock);
        return valid;
    }

    void PublishLid(const float* matrix)
    {
        AcquireSRWLockExclusive(&g_HatchLock);
        std::memcpy(g_Lid, matrix, sizeof(g_Lid));
        g_LidValid = true;
        ReleaseSRWLockExclusive(&g_HatchLock);
    }

    bool Lid(float* matrix)
    {
        AcquireSRWLockShared(&g_HatchLock);
        const bool valid = g_LidValid;
        std::memcpy(matrix, g_Lid, sizeof(g_Lid));
        ReleaseSRWLockShared(&g_HatchLock);
        return valid;
    }

    void HoldLid(float angle)
    {
        g_LidHold.store(angle, std::memory_order_relaxed);
    }

    void PublishHullFrame(const float* matrix, float clearance)
    {
        AcquireSRWLockExclusive(&g_HatchLock);
        std::memcpy(g_HullFrame, matrix, sizeof(g_HullFrame));
        g_HullClearance = clearance;
        g_HullFrameValid = true;
        ReleaseSRWLockExclusive(&g_HatchLock);
    }

    bool HullFrame(float* matrix, float* clearance)
    {
        AcquireSRWLockShared(&g_HatchLock);
        const bool valid = g_HullFrameValid;
        std::memcpy(matrix, g_HullFrame, sizeof(g_HullFrame));
        *clearance = g_HullClearance;
        ReleaseSRWLockShared(&g_HatchLock);
        return valid;
    }

    float LidHold()
    {
        return g_LidHold.load(std::memory_order_relaxed);
    }

    const void* FireModeSlot()
    {
        return &g_FireMode;
    }

    const void* AimingSlot()
    {
        return &g_Aiming;
    }

    bool FireMode()
    {
        return g_FireMode.load(std::memory_order_relaxed) != 0;
    }

    bool Aiming()
    {
        return g_Aiming.load(std::memory_order_relaxed) != 0;
    }

    bool Ridden()
    {
        return g_Ridden.load(std::memory_order_relaxed);
    }

    const void* StancePressSlot()
    {
        return &g_StancePresses;
    }

    const void* StanceHeldSlot()
    {
        return &g_StanceHeld;
    }

    void OnRideInput(bool dash, float dt)
    {
        const std::uint8_t presses = g_StancePresses.load(std::memory_order_relaxed);
        const bool held = g_StanceHeld.exchange(0, std::memory_order_relaxed) != 0;
        if (!g_InputKnown.exchange(true, std::memory_order_acq_rel))
        {
            g_SeenPresses = presses;
            g_PressPending = false;
            g_DashWas = dash;
            return;
        }
        std::uint8_t stance = g_Stance.load(std::memory_order_relaxed);
        if (presses != g_SeenPresses)
        {
            if (g_PressPending)   // let go and pressed again within one frame: the first press was a tap
                stance = Tapped(stance);
            g_SeenPresses = presses;
            g_PressPending = true;
            g_PressHeldFor = 0.0f;
        }
        else if (g_PressPending && !held)
        {
            g_PressPending = false;
            stance = Tapped(stance);
        }
        else if (g_PressPending)
        {
            g_PressHeldFor += dt;
            if (g_PressHeldFor >= kStanceHoldTime)
            {
                g_PressPending = false;
                stance = Held(stance);
            }
        }
        if (dash && !g_DashWas && stance == kUpright)
            stance = kNormal;
        g_DashWas = dash;
        SetStance(stance);
    }

    bool Upright()
    {
        return g_Stance.load(std::memory_order_relaxed) == kUpright;
    }

    bool IsBattleGearComm(const void* comm)
    {
        return comm && reinterpret_cast<std::uintptr_t>(comm) == g_Comm.load(std::memory_order_acquire);
    }

    void RequestSurge(float metres)
    {
        AcquireSRWLockExclusive(&g_SurgeLock);
        g_Surge += metres;
        ReleaseSRWLockExclusive(&g_SurgeLock);
    }

    float TakeSurge(float most)
    {
        AcquireSRWLockExclusive(&g_SurgeLock);
        const float taken = (std::max)(-most, (std::min)(g_Surge, most));
        g_Surge -= taken;
        ReleaseSRWLockExclusive(&g_SurgeLock);
        return taken;
    }

    void ClearSurge()
    {
        AcquireSRWLockExclusive(&g_SurgeLock);
        g_Surge = 0.0f;
        ReleaseSRWLockExclusive(&g_SurgeLock);
    }

    void PublishDriveSpeed(float speed)
    {
        g_DriveSpeed.store(speed, std::memory_order_relaxed);
    }

    float DriveSpeed()
    {
        return g_DriveSpeed.load(std::memory_order_relaxed);
    }

    void PublishDriveTurn(float rate)
    {
        g_DriveTurn.store(rate, std::memory_order_relaxed);
    }

    float DriveTurn()
    {
        return g_DriveTurn.load(std::memory_order_relaxed);
    }

    void PublishPivot(bool pivoting)
    {
        g_Pivoting.store(pivoting, std::memory_order_relaxed);
    }

    bool Pivoting()
    {
        return g_Pivoting.load(std::memory_order_relaxed);
    }

    void SetFirstPerson(bool on)
    {
        g_FirstPerson.store(on, std::memory_order_relaxed);
    }

    bool FirstPerson()
    {
        return g_FirstPerson.load(std::memory_order_relaxed);
    }

    const void* FirstPersonSlot()
    {
        return &g_FirstPerson;
    }

    void PublishSidearmInHand(bool inHand)
    {
        g_SidearmInHand.store(inHand, std::memory_order_relaxed);
    }

    bool SidearmInHand()
    {
        return g_SidearmInHand.load(std::memory_order_relaxed);
    }

    const void* HullSteeringSlot()
    {
        return &g_Hull;
    }

    void SteerHull(const void* comm, float heading)
    {
        g_Hull.heading.store(heading, std::memory_order_relaxed);
        g_Hull.comm.store(reinterpret_cast<std::uintptr_t>(comm), std::memory_order_release);
    }

    void StopHullSteering()
    {
        g_Hull.comm.store(0, std::memory_order_release);
    }

    bool HullSteering(float* heading)
    {
        if (!g_Hull.comm.load(std::memory_order_acquire))
            return false;
        *heading = g_Hull.heading.load(std::memory_order_relaxed);
        return true;
    }

    void PublishBodyHeading(float heading)
    {
        g_BodyHeading.store(heading, std::memory_order_relaxed);
        g_BodyHeadingKnown.store(true, std::memory_order_release);
    }

    bool BodyHeading(float* heading)
    {
        if (!g_BodyHeadingKnown.load(std::memory_order_acquire))
            return false;
        *heading = g_BodyHeading.load(std::memory_order_relaxed);
        return true;
    }

    bool OnField()
    {
        return g_Comm.load(std::memory_order_acquire) != 0;
    }

    bool AimTarget(float* target)
    {
        AcquireSRWLockShared(&g_AimLock);
        const bool ridden = g_Aim.ridden;
        Copy3(target, g_Aim.target);
        ReleaseSRWLockShared(&g_AimLock);
        return ridden;
    }

    bool AimPoint(float* target)
    {
        AcquireSRWLockShared(&g_AimLock);
        Copy3(target, g_Aim.target);
        ReleaseSRWLockShared(&g_AimLock);
        return OnField();
    }

    void NoteAiAttack()
    {
        g_AiAttackUntil.store(GetTickCount64() + kAiAttackHoldMs, std::memory_order_relaxed);
    }

    bool AiAttacking()
    {
        return !Ridden() && GetTickCount64() < g_AiAttackUntil.load(std::memory_order_relaxed);
    }

    void PublishMuzzles(const float* railGun, const float* heavyMg, const float (*hatches)[3], const float* railGunAim,
                        const float* heavyMgAim)
    {
        AcquireSRWLockExclusive(&g_MuzzleLock);
        Copy3(g_Muzzles.railGun, railGun);
        Copy3(g_Muzzles.heavyMg, heavyMg);
        for (int i = 0; i < 3; ++i)
            Copy3(g_Muzzles.hatches[i], hatches[i]);
        Copy3(g_Muzzles.railGunAim, railGunAim);
        Copy3(g_Muzzles.heavyMgAim, heavyMgAim);
        g_MuzzlesValid = true;
        ReleaseSRWLockExclusive(&g_MuzzleLock);
    }

    bool RailGunMuzzle(float* position)
    {
        AcquireSRWLockShared(&g_MuzzleLock);
        const bool valid = g_MuzzlesValid;
        Copy3(position, g_Muzzles.railGun);
        ReleaseSRWLockShared(&g_MuzzleLock);
        return valid;
    }

    bool MuzzleFor(std::int32_t arm, std::int32_t missileShot, float* position)
    {
        AcquireSRWLockShared(&g_MuzzleLock);
        const bool valid = g_MuzzlesValid;
        if (arm != 0)
            Copy3(position, g_Muzzles.heavyMg);
        else if (MainWeaponType() == kHMissile)
            Copy3(position, g_Muzzles.hatches[static_cast<std::uint32_t>(missileShot) % 3]);
        else
            Copy3(position, g_Muzzles.railGun);
        ReleaseSRWLockShared(&g_MuzzleLock);
        return valid;
    }

    bool BarrelFor(std::int32_t arm, float* direction)
    {
        if (arm == 0 && MainWeaponType() == kHMissile)
            return false;
        AcquireSRWLockShared(&g_MuzzleLock);
        const bool valid = g_MuzzlesValid;
        Copy3(direction, arm != 0 ? g_Muzzles.heavyMgAim : g_Muzzles.railGunAim);
        ReleaseSRWLockShared(&g_MuzzleLock);
        return valid;
    }

    bool IsBattleGearAttack(const void* attackAction)
    {
        return attackAction && OwnerTypeSEH(attackAction) == kBuddyWalkerType && battleGear::IsBattleGearOut();
    }

    void NoteRailShot(int level)
    {
        g_RailShotLevel.store(level, std::memory_order_relaxed);
        g_RailShots.fetch_add(1, std::memory_order_release);
    }

    int LastRailShotLevel()
    {
        return g_RailShotLevel.load(std::memory_order_relaxed);
    }

    std::uint32_t RailShots()
    {
        return g_RailShots.load(std::memory_order_acquire);
    }

    void PublishMgOverheat(bool overheated)
    {
        g_MgOverheated.store(overheated, std::memory_order_relaxed);
    }

    bool MgOverheated()
    {
        return g_MgOverheated.load(std::memory_order_relaxed);
    }

    bool HeatMg(std::uint32_t rounds)
    {
        AcquireSRWLockExclusive(&g_HeatLock);
        if (!g_Overheated && rounds)
        {
            g_Heat += kRoundHeat * static_cast<float>(rounds);
            if (g_Heat >= kOverheated)
            {
                g_Heat = kOverheated;
                g_Overheated = true;
            }
        }
        const bool locked = g_Overheated;
        g_MgHeat.store(g_Heat, std::memory_order_relaxed);
        g_MgOverheated.store(locked, std::memory_order_relaxed);
        ReleaseSRWLockExclusive(&g_HeatLock);
        return locked;
    }

    void CoolMg(float dt)
    {
        AcquireSRWLockExclusive(&g_HeatLock);
        if (g_Heat > 0.0f)
        {
            g_Heat -= kCoolRate * dt;
            if (g_Heat <= 0.0f)
            {
                g_Heat = 0.0f;
                g_Overheated = false;
            }
        }
        g_MgHeat.store(g_Heat, std::memory_order_relaxed);
        g_MgOverheated.store(g_Overheated, std::memory_order_relaxed);
        ReleaseSRWLockExclusive(&g_HeatLock);
    }

    void ResetMgHeat()
    {
        AcquireSRWLockExclusive(&g_HeatLock);
        g_Heat = 0.0f;
        g_Overheated = false;
        g_MgHeat.store(0.0f, std::memory_order_relaxed);
        g_MgOverheated.store(false, std::memory_order_relaxed);
        ReleaseSRWLockExclusive(&g_HeatLock);
    }

    float MgHeat()
    {
        return g_MgHeat.load(std::memory_order_relaxed);
    }

    void PublishGroundSpeed(float speed)
    {
        g_GroundSpeed.store(speed, std::memory_order_relaxed);
    }

    float GroundSpeed()
    {
        return g_GroundSpeed.load(std::memory_order_relaxed);
    }

    void PublishRoofGun(const float* matrix, float dt)
    {
        AcquireSRWLockExclusive(&g_MuzzleLock);
        bool warped = !g_RoofGunValid || dt <= 0.001f;
        float velocity[3] = {};
        for (int i = 0; i < 3 && !warped; ++i)
        {
            velocity[i] = (matrix[12 + i] - g_RoofGun[12 + i]) / dt;
            warped = velocity[i] > kFastestGun || velocity[i] < -kFastestGun;
        }
        for (int i = 0; i < 3; ++i)
            g_RoofGunVelocity[i] = warped ? 0.0f : velocity[i];
        std::memcpy(g_RoofGun, matrix, sizeof(g_RoofGun));
        g_RoofGunValid = true;
        ReleaseSRWLockExclusive(&g_MuzzleLock);
    }

    void DropRoofGun()
    {
        AcquireSRWLockExclusive(&g_MuzzleLock);
        g_RoofGunValid = false;
        ReleaseSRWLockExclusive(&g_MuzzleLock);
        g_RoofGunTurning.store(false, std::memory_order_relaxed);
    }

    bool RoofGun(float* matrix, float* velocity)
    {
        AcquireSRWLockShared(&g_MuzzleLock);
        const bool valid = g_RoofGunValid;
        std::memcpy(matrix, g_RoofGun, sizeof(g_RoofGun));
        if (velocity)
            Copy3(velocity, g_RoofGunVelocity);
        ReleaseSRWLockShared(&g_MuzzleLock);
        return valid;
    }

    void PublishRoofGunTurning(bool turning)
    {
        g_RoofGunTurning.store(turning, std::memory_order_relaxed);
    }

    bool RoofGunTurning()
    {
        return g_RoofGunTurning.load(std::memory_order_relaxed);
    }

    void PublishRiderAim(const float* point)
    {
        AcquireSRWLockExclusive(&g_AimLock);
        Copy3(g_RiderAim, point);
        g_RiderAimAt = GetTickCount64();
        if (g_Aim.ridden)
            Copy3(g_Aim.target, point);
        ReleaseSRWLockExclusive(&g_AimLock);
    }

    void SelectMg(bool selected)
    {
        g_MgSelected.store(selected, std::memory_order_relaxed);
    }

    void AddWalkerTime(float seconds)
    {
        float had = g_WalkerTime.load(std::memory_order_relaxed);
        while (!g_WalkerTime.compare_exchange_weak(had, had + seconds, std::memory_order_relaxed))
        {
        }
        g_WalkerTimeAt.store(GetTickCount64(), std::memory_order_relaxed);
    }

    bool TakeWalkerTime(float* seconds)
    {
        if (GetTickCount64() - g_WalkerTimeAt.load(std::memory_order_relaxed) > kWalkerClockFreshMs)
        {
            g_WalkerTime.store(0.0f, std::memory_order_relaxed);
            return false;
        }
        *seconds = g_WalkerTime.exchange(0.0f, std::memory_order_relaxed);
        return true;
    }

    bool MgSelected()
    {
        return g_MgSelected.load(std::memory_order_relaxed);
    }

    void PublishMgTrigger(bool held)
    {
        g_MgTrigger.store(held, std::memory_order_relaxed);
    }

    bool MgTrigger()
    {
        return g_MgTrigger.load(std::memory_order_relaxed);
    }

    bool TakeMgRound()
    {
        std::uint32_t left = g_MgAmmo.load(std::memory_order_relaxed);
        while (left > 0)
            if (g_MgAmmo.compare_exchange_weak(left, left - 1, std::memory_order_relaxed))
                return true;
        return false;
    }

    std::uint32_t MgAmmo()
    {
        return g_MgAmmo.load(std::memory_order_relaxed);
    }

    bool Hanging()
    {
        const auto* comm = reinterpret_cast<const std::uint8_t*>(g_Comm.load(std::memory_order_acquire));
        std::uint32_t actions = 0;
        std::uint32_t carry = 0;
        if (!comm || !ReadCarryBitsSEH(comm, actions, carry))
        {
            g_Lifted.store(false, std::memory_order_relaxed);
            return false;
        }
        const bool lifted = (actions & kLifting) != 0 || (g_Lifted.load(std::memory_order_relaxed) && (actions & kLifted) != 0);
        g_Lifted.store(lifted, std::memory_order_relaxed);
        return lifted || (actions & kDropping) != 0;
    }

    void PublishRailCharging(bool charging)
    {
        g_RailCharging.store(charging, std::memory_order_relaxed);
    }

    bool RailCharging()
    {
        return g_RailCharging.load(std::memory_order_relaxed);
    }

    void NoteMgShots(std::uint32_t count)
    {
        g_MgShots.fetch_add(count, std::memory_order_release);
    }

    std::uint32_t MgShots()
    {
        return g_MgShots.load(std::memory_order_acquire);
    }
}
