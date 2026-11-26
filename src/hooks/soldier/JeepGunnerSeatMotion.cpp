#include <pch.h>
#include "JeepGunnerSeatMotion.h"

#include <Windows.h>
#include <cstdint>
#include <iterator>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "RideVehicleActionImpl_State_IdleAction.h"
#include "log.h"

namespace
{
    using SetVehicleInfo_t          = void(__fastcall*)(void* rideAction, std::uint32_t slot);
    using RideState_t               = void(__fastcall*)(void* rideAction, std::uint32_t slot, std::uint32_t proc, void* param);
    using GetVehicleEntryPosition_t = void(__fastcall*)(void* routeAi, std::uint32_t slot, std::uint8_t seat, float* position,
                                                        float* rotation, std::uint8_t forceSeatTwo);

    constexpr std::size_t   kCommonWorkSeat     = 0x24;
    constexpr std::uint8_t  kFrontPassengerSeat = 1;
    constexpr std::uint32_t kProcRecordRide     = 4; // writes the seat into the soldier's vehicle knowledge, which keeps the real one

    const std::uint8_t kSetVehicleInfoPrologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48,
        0x89, 0x7C, 0x24, 0x20, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x20
    };
    const std::uint8_t kGetInPrologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B, 0xA9, 0xD8,
        0x01, 0x00, 0x00, 0x8B, 0xC2, 0x8B, 0xFA, 0x2B, 0x81, 0xE8, 0x01, 0x00, 0x00
    };
    const std::uint8_t kGetOutPrologue[] = {
        0x4C, 0x8B, 0xDC, 0x53, 0x55, 0x56, 0x57, 0x48, 0x81, 0xEC, 0xA8, 0x00, 0x00, 0x00, 0x48, 0x8B,
        0x05, 0x00, 0x00, 0x00, 0x00, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x44, 0x24, 0x70, 0x8B, 0xC2, 0x48,
        0x8B, 0xD9, 0x8B, 0xEA, 0x2B, 0x81, 0xE8, 0x01, 0x00, 0x00
    };
    const std::uint8_t kGetOutMask[] = {
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1
    };
    const std::uint8_t kGetOutImmediatelyPrologue[] = {
        0x40, 0x53, 0x55, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x78, 0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00,
        0x48, 0x33, 0xC4, 0x48, 0x89, 0x44, 0x24, 0x50, 0x8B, 0xC2, 0x48, 0x8B, 0xF9, 0x8B, 0xEA, 0x2B,
        0x81, 0xE8, 0x01, 0x00, 0x00
    };
    const std::uint8_t kGetOutImmediatelyMask[] = {
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1
    };
    const std::uint8_t kEntryPositionPrologue[] = {
        0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x6C,
        0x24, 0xE8, 0x48, 0x81, 0xEC, 0x18, 0x01, 0x00, 0x00
    };
    static_assert(sizeof(kGetOutPrologue) == sizeof(kGetOutMask));
    static_assert(sizeof(kGetOutImmediatelyPrologue) == sizeof(kGetOutImmediatelyMask));

    SetVehicleInfo_t          g_OrigSetVehicleInfo          = nullptr;
    RideState_t               g_OrigGetIn                   = nullptr;
    RideState_t               g_OrigGetOut                  = nullptr;
    RideState_t               g_OrigGetOutImmediately       = nullptr;
    GetVehicleEntryPosition_t g_OrigGetVehicleEntryPosition = nullptr;
    thread_local int          t_StandScope                  = 0;

    bool SwapSeatSEH(std::uint8_t* common, std::uint8_t from, std::uint8_t to)
    {
        __try
        {
            if (common[kCommonWorkSeat] != from)
                return false;
            common[kCommonWorkSeat] = to;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    struct SeatOneStandIn
    {
        std::uint8_t* common;
        bool          active;
    };

    // For the call the gunner's seat (5) reads as the front passenger's (1): seat 1's clips, door, door-free check and
    // get-off side, played from his gun stand
    SeatOneStandIn BeginSeatOne(void* rideAction, std::uint32_t slot)
    {
        JeepGunnerRide ride{};
        if (!JeepGunner_ReadRide(rideAction, slot, ride) || ride.seat != kJeepGunnerSeat || !JeepGunner_IsArmedLightVehicle(ride.commInfo)
            || !SwapSeatSEH(ride.common, kJeepGunnerSeat, kFrontPassengerSeat))
            return {};
        ++t_StandScope;
        return { ride.common, true };
    }

    void EndSeatOne(const SeatOneStandIn& standIn)
    {
        if (!standIn.active)
            return;
        SwapSeatSEH(standIn.common, kFrontPassengerSeat, kJeepGunnerSeat);
        --t_StandScope;
    }

    void __fastcall hk_SetVehicleInfo(void* rideAction, std::uint32_t slot)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigSetVehicleInfo, rideAction, slot);
        const SeatOneStandIn standIn = BeginSeatOne(rideAction, slot);
        g_OrigSetVehicleInfo(rideAction, slot);
        EndSeatOne(standIn);
    }

    void __fastcall hk_State_GetInAction(void* rideAction, std::uint32_t slot, std::uint32_t proc, void* param)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigGetIn, rideAction, slot, proc, param);
        const SeatOneStandIn standIn = proc == kProcRecordRide ? SeatOneStandIn{} : BeginSeatOne(rideAction, slot);
        g_OrigGetIn(rideAction, slot, proc, param);
        EndSeatOne(standIn);
    }

    void __fastcall hk_State_GetOutAction(void* rideAction, std::uint32_t slot, std::uint32_t proc, void* param)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigGetOut, rideAction, slot, proc, param);
        const SeatOneStandIn standIn = proc == kProcRecordRide ? SeatOneStandIn{} : BeginSeatOne(rideAction, slot);
        g_OrigGetOut(rideAction, slot, proc, param);
        EndSeatOne(standIn);
    }

    void __fastcall hk_State_GetOutImmediatelyAction(void* rideAction, std::uint32_t slot, std::uint32_t proc, void* param)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigGetOutImmediately, rideAction, slot, proc, param);
        const SeatOneStandIn standIn = proc == kProcRecordRide ? SeatOneStandIn{} : BeginSeatOne(rideAction, slot);
        g_OrigGetOutImmediately(rideAction, slot, proc, param);
        EndSeatOne(standIn);
    }

    // Seat 5 has no walk-up spot: the gunner walks to seat 1's, taken from his gun stand
    void __fastcall hk_GetVehicleEntryPosition(void* routeAi, std::uint32_t slot, std::uint8_t seat, float* position, float* rotation,
                                               std::uint8_t forceSeatTwo)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigGetVehicleEntryPosition, routeAi, slot, seat, position, rotation, forceSeatTwo);
        if (seat != kJeepGunnerSeat)
        {
            g_OrigGetVehicleEntryPosition(routeAi, slot, seat, position, rotation, forceSeatTwo);
            return;
        }
        ++t_StandScope;
        g_OrigGetVehicleEntryPosition(routeAi, slot, kFrontPassengerSeat, position, rotation, forceSeatTwo);
        --t_StandScope;
    }

    struct SeatMotionHook
    {
        const char*                         name;
        uintptr_t AddressSetRuntime::AddressSet::* address;
        const std::uint8_t*                 prologue;
        const std::uint8_t*                 mask;
        std::size_t                         size;
        void*                               detour;
        void**                              original;
        void*                               target;
    };

    SeatMotionHook g_Hooks[] = {
        { "JeepGunner: RideVehicleActionImpl::SetVehicleInfo", &AddressSetRuntime::AddressSet::SoldierRideVehicleActionImpl_SetVehicleInfo,
          kSetVehicleInfoPrologue, nullptr, sizeof(kSetVehicleInfoPrologue),
          reinterpret_cast<void*>(&hk_SetVehicleInfo), reinterpret_cast<void**>(&g_OrigSetVehicleInfo), nullptr },
        { "JeepGunner: RideVehicleActionImpl::State_GetInAction", &AddressSetRuntime::AddressSet::SoldierRideVehicleActionImpl_State_GetInAction,
          kGetInPrologue, nullptr, sizeof(kGetInPrologue),
          reinterpret_cast<void*>(&hk_State_GetInAction), reinterpret_cast<void**>(&g_OrigGetIn), nullptr },
        { "JeepGunner: RideVehicleActionImpl::State_GetOutAction", &AddressSetRuntime::AddressSet::SoldierRideVehicleActionImpl_State_GetOutAction,
          kGetOutPrologue, kGetOutMask, sizeof(kGetOutPrologue),
          reinterpret_cast<void*>(&hk_State_GetOutAction), reinterpret_cast<void**>(&g_OrigGetOut), nullptr },
        { "JeepGunner: RideVehicleActionImpl::State_GetOutImmediatelyAction", &AddressSetRuntime::AddressSet::SoldierRideVehicleActionImpl_State_GetOutImmediatelyAction,
          kGetOutImmediatelyPrologue, kGetOutImmediatelyMask, sizeof(kGetOutImmediatelyPrologue),
          reinterpret_cast<void*>(&hk_State_GetOutImmediatelyAction), reinterpret_cast<void**>(&g_OrigGetOutImmediately), nullptr },
        { "JeepGunner: RouteAiImpl::GetVehicleEntryPosition", &AddressSetRuntime::AddressSet::SoldierRouteAiImpl_GetVehicleEntryPosition,
          kEntryPositionPrologue, nullptr, sizeof(kEntryPositionPrologue),
          reinterpret_cast<void*>(&hk_GetVehicleEntryPosition), reinterpret_cast<void**>(&g_OrigGetVehicleEntryPosition), nullptr },
    };
}

bool JeepGunner_InStandScope()
{
    return t_StandScope > 0;
}

bool JeepGunnerSeatMotion_Install()
{
    for (SeatMotionHook& hook : g_Hooks)
    {
        if (hook.target)
            continue;
        void* target = ResolveGameAddress(gAddr.*hook.address);
        HookEntry entry = HookEntry::Unknown;
        if (target)
            entry = hook.mask ? CheckHookEntryMasked(target, hook.prologue, hook.mask, hook.size) : CheckHookEntry(target, hook.prologue, hook.size);
        if (entry == HookEntry::Unknown)
        {
            LogError("%s: code does not match %s, armed jeeps carry no gunner", hook.name, GetGameBuildName(gGameBuild));
            JeepGunnerSeatMotion_Uninstall();
            return false;
        }
        if (entry == HookEntry::ForeignHook)
            LogHookedOnTop(hook.name, target);
        if (!CreateAndEnableHook(target, hook.detour, hook.original))
        {
            LogError("%s: hook failed, armed jeeps carry no gunner", hook.name);
            JeepGunnerSeatMotion_Uninstall();
            return false;
        }
        hook.target = target;
    }
    return true;
}

void JeepGunnerSeatMotion_Uninstall()
{
    for (auto it = std::rbegin(g_Hooks); it != std::rend(g_Hooks); ++it)
    {
        if (!it->target)
            continue;
        DisableAndRemoveHook(it->target);
        it->target = nullptr;
        *it->original = nullptr;
    }
}
