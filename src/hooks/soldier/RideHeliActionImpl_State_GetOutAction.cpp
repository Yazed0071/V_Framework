#include <pch.h>
#include "RideHeliActionImpl_State_GetOutAction.h"

#include <Windows.h>
#include <cstddef>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"
#include "RideHeliActionImpl_State_GetInAction.h"

namespace
{
    using StateAction_t = void(__fastcall*)(void* rideHeli, std::uint32_t realizedSlot, int proc, void* motionEvent);

    constexpr int          kProcEnter    = 1;
    constexpr std::size_t  kRideWork     = 0x88;   // 0x28 per riding soldier
    constexpr std::size_t  kRideWorkBase = 0x90;   // realized slot of entry 0
    constexpr std::size_t  kWorkSize     = 0x28;
    constexpr std::size_t  kWorkSoldier  = 0x10;
    constexpr std::size_t  kWorkFlags    = 0x26;
    constexpr std::uint8_t kLeftDoorBit  = 0x40;

    StateAction_t g_OrigGetOutAction = nullptr;
    void*         g_Target           = nullptr;
    bool          g_Installed        = false;

    void KeepDoorSeh(std::uint8_t* rideHeli, std::uint32_t realizedSlot)
    {
        __try
        {
            const std::uint32_t entry = realizedSlot - static_cast<std::uint32_t>(*reinterpret_cast<std::int32_t*>(rideHeli + kRideWorkBase));
            auto* work = *reinterpret_cast<std::uint8_t**>(rideHeli + kRideWork) + static_cast<std::size_t>(entry) * kWorkSize;
            const std::uint32_t index = *reinterpret_cast<std::uint32_t*>(work + kWorkSoldier);
            if (!SoldierRideHeli_IsDoorRider(index))
                return;
            const HeliSeatSide side = SoldierRideHeli_Side(index);
            if (side == HeliSeatSide::Left)
                work[kWorkFlags] = static_cast<std::uint8_t>(work[kWorkFlags] | kLeftDoorBit);
            else if (side == HeliSeatSide::Right)
                work[kWorkFlags] = static_cast<std::uint8_t>(work[kWorkFlags] & ~kLeftDoorBit);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void __fastcall hkGetOutAction(void* rideHeli, std::uint32_t realizedSlot, int proc, void* motionEvent)
    {
        g_OrigGetOutAction(rideHeli, realizedSlot, proc, motionEvent);
        if (proc == kProcEnter && !MissionCodeGuard::ShouldBypassHooks())
            KeepDoorSeh(static_cast<std::uint8_t*>(rideHeli), realizedSlot);   // vanilla picks the door from where he would land, not the side he sat on
    }
}

bool Install_RideHeliActionImpl_State_GetOutAction_Hook()
{
    if (g_Installed)
        return true;

    g_Target = gAddr.RideHeliActionImpl_State_GetOutAction ? ResolveGameAddress(gAddr.RideHeliActionImpl_State_GetOutAction) : nullptr;
    if (!g_Target)
    {
        LogError("RideHeliActionImpl_State_GetOutAction: address missing for this build, soldiers boarded by script may get off on the other side");
        return false;
    }
    if (!CreateAndEnableHook(g_Target, reinterpret_cast<void*>(&hkGetOutAction), reinterpret_cast<void**>(&g_OrigGetOutAction)))
    {
        LogError("RideHeliActionImpl_State_GetOutAction: hook failed, soldiers boarded by script may get off on the other side");
        g_Target = nullptr;
        return false;
    }
    g_Installed = true;
    return true;
}

bool Uninstall_RideHeliActionImpl_State_GetOutAction_Hook()
{
    if (!g_Installed)
        return true;
    DisableAndRemoveHook(g_Target);
    g_OrigGetOutAction = nullptr;
    g_Target           = nullptr;
    g_Installed        = false;
    return true;
}
