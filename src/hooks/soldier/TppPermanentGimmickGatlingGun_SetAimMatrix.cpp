#include <pch.h>
#include "TppPermanentGimmickGatlingGun_SetAimMatrix.h"

#include <Windows.h>
#include <atomic>
#include <cmath>
#include <cstdint>

#include "AddressSet.h"
#include "GameClock.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "RouteAiImpl_StepEvent.h"
#include "log.h"

namespace
{
    using SetAimMatrix_t = void(__fastcall*)(std::uint8_t* gun, const float* aimMatrix);

    constexpr std::size_t   kGunOwner      = 0x0A;
    constexpr std::size_t   kGunPitch      = 0x100;
    constexpr std::size_t   kGunYaw        = 0x104;
    constexpr std::size_t   kGunModel      = 0x10;
    constexpr std::size_t   kModelDataBody = 0x10;
    constexpr std::size_t   kDataBodyData  = 0x38;
    constexpr std::size_t   kDataInstance  = 0x18;
    constexpr std::size_t   kDataParams    = 0xB0;
    constexpr std::size_t   kParamYawMax   = 0x3C;  // degrees
    constexpr std::size_t   kParamYawMin   = 0x40;
    constexpr std::uint16_t kKindMask      = 0xFE00;
    constexpr std::uint16_t kSoldierIds    = 0x400;
    constexpr std::uint32_t kMaxSoldiers   = 0x200;

    constexpr float kPi            = 3.14159265f;
    constexpr float kTwoPi         = 2.0f * kPi;
    constexpr float kFullTurn      = 359.0f;
    constexpr float kOnTarget      = 3.0f * kPi / 180.0f;
    constexpr std::uint64_t kFreshStartMs = 250; // a longer gap is a new gunner, a pause or a hitch: no turn that frame

    const std::uint8_t kSetAimMatrixPrologue[] = {
        0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10, 0x48, 0x89, 0x68, 0x18, 0x56, 0x57, 0x41, 0x56, 0x48, 0x81, 0xEC, 0x90, 0x00, 0x00, 0x00
    };

    SetAimMatrix_t             g_OrigSetAimMatrix = nullptr;
    void*                      g_HookTarget       = nullptr;
    std::atomic<std::uint64_t> g_LastTurnMs[kMaxSoldiers] = {};

    struct GunAngles
    {
        float pitch;
        float yaw;
    };

    bool RouteGunnerOf(const std::uint8_t* gun, std::uint32_t& soldierIndex, float& turnSpeed)
    {
        __try
        {
            const std::uint16_t owner = *reinterpret_cast<const std::uint16_t*>(gun + kGunOwner);
            if ((owner & kKindMask) != kSoldierIds)
                return false;
            soldierIndex = owner & (kMaxSoldiers - 1);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        turnSpeed = AntiAirGunRoute_TurnSpeed(soldierIndex);
        return turnSpeed > 0.0f;
    }

    bool ReadAngles(const std::uint8_t* gun, GunAngles& angles)
    {
        __try
        {
            angles.pitch = *reinterpret_cast<const float*>(gun + kGunPitch);
            angles.yaw   = *reinterpret_cast<const float*>(gun + kGunYaw);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool TurnsAllTheWayRound(const std::uint8_t* gun)
    {
        __try
        {
            const std::uint8_t* model    = *reinterpret_cast<std::uint8_t* const*>(gun + kGunModel);
            const std::uint8_t* dataBody = *reinterpret_cast<std::uint8_t* const*>(model + kModelDataBody);
            const std::uint8_t* data     = *reinterpret_cast<std::uint8_t* const*>(dataBody + kDataBodyData);
            if (!data)
                return false;
            const std::uint8_t* params = *reinterpret_cast<std::uint8_t* const*>(*reinterpret_cast<std::uint8_t* const*>(data + kDataInstance) + kDataParams);
            const float yawMax = *reinterpret_cast<const float*>(params + kParamYawMax);
            const float yawMin = *reinterpret_cast<const float*>(params + kParamYawMin);
            return yawMax - yawMin >= kFullTurn;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false; // the straight way between two allowed angles never leaves the allowed range
        }
    }

    float WrapAngle(float angle)
    {
        while (angle > kPi)
            angle -= kTwoPi;
        while (angle <= -kPi)
            angle += kTwoPi;
        return angle;
    }

    float SecondsSinceLastTurn(std::uint32_t soldierIndex)
    {
        const std::uint64_t now  = GameClock::NowMs();
        const std::uint64_t last = g_LastTurnMs[soldierIndex].exchange(now, std::memory_order_relaxed);
        if (!last || now < last || now - last > kFreshStartMs)
            return 0.0f;
        return static_cast<float>(now - last) * 0.001f;
    }

    void TurnTowardTarget(std::uint8_t* gun, std::uint32_t soldierIndex, float turnSpeed, const GunAngles& before)
    {
        GunAngles target;
        if (!ReadAngles(gun, target))
            return;
        const bool fullTurn = TurnsAllTheWayRound(gun);
        const float yawLeft   = fullTurn ? WrapAngle(target.yaw - before.yaw) : target.yaw - before.yaw;
        const float pitchLeft = target.pitch - before.pitch;
        const float left = std::sqrt(yawLeft * yawLeft + pitchLeft * pitchLeft);
        const float step = turnSpeed * SecondsSinceLastTurn(soldierIndex);
        if (left > step)
        {
            const float part = left > 0.0f ? step / left : 0.0f;
            const float yaw = before.yaw + yawLeft * part;
            __try
            {
                *reinterpret_cast<float*>(gun + kGunPitch) = before.pitch + pitchLeft * part;
                *reinterpret_cast<float*>(gun + kGunYaw)   = fullTurn ? WrapAngle(yaw) : yaw;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return;
            }
        }
        AntiAirGunRoute_NoteGunOnTarget(soldierIndex, left - step <= kOnTarget);
    }

    void __fastcall hk_SetAimMatrix(std::uint8_t* gun, const float* aimMatrix)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigSetAimMatrix, gun, aimMatrix);
        std::uint32_t soldierIndex = 0;
        float turnSpeed = 0.0f;
        GunAngles before{};
        if (!gun || !RouteGunnerOf(gun, soldierIndex, turnSpeed) || !ReadAngles(gun, before))
        {
            g_OrigSetAimMatrix(gun, aimMatrix);
            return;
        }
        g_OrigSetAimMatrix(gun, aimMatrix); // writes the aim straight into the gun's angles
        TurnTowardTarget(gun, soldierIndex, turnSpeed, before);
    }
}

bool GatlingGunTurn_IsActive()
{
    return g_HookTarget != nullptr;
}

bool Install_TppPermanentGimmickGatlingGun_SetAimMatrix_Hook()
{
    if (g_HookTarget)
        return true;
    const std::uintptr_t addr = gAddr.TppPermanentGimmickGatlingGun_SetAimMatrix;
    if (!addr)
        return true;

    auto* target = static_cast<std::uint8_t*>(ResolveGameAddress(addr));
    const HookEntry entry = CheckHookEntry(target, kSetAimMatrixPrologue, sizeof(kSetAimMatrixPrologue));
    if (entry == HookEntry::Unknown)
    {
        LogError("AntiAirGunRoute: gatling gun aim code does not match %s, AttackFireAAG guns snap to their target",
            GetGameBuildName(gGameBuild));
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("AntiAirGunRoute: gatling gun SetAimMatrix", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_SetAimMatrix), reinterpret_cast<void**>(&g_OrigSetAimMatrix)))
    {
        LogError("AntiAirGunRoute: gatling gun SetAimMatrix hook failed, AttackFireAAG guns snap to their target");
        return false;
    }
    g_HookTarget = target;
    return true;
}

bool Uninstall_TppPermanentGimmickGatlingGun_SetAimMatrix_Hook()
{
    if (g_HookTarget)
    {
        DisableAndRemoveHook(g_HookTarget);
        g_HookTarget = nullptr;
        g_OrigSetAimMatrix = nullptr;
    }
    for (auto& last : g_LastTurnMs)
        last.store(0, std::memory_order_relaxed);
    return true;
}
