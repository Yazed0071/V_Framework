#include <pch.h>
#include "UnrealRideHeliActionImpl_State_IdleAction.h"

#include <Windows.h>
#include <intrin.h>
#include <cstddef>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"
#include "RideHeliActionImpl_State_GetInAction.h"
#include "RideHeliActionImpl_State_IdleAction.h"

namespace
{
    using StateAction_t = void(__fastcall*)(void* unrealRideHeli, std::uint32_t soldierIndex, int proc);

    constexpr int           kProcUpdate     = 0;
    constexpr int           kProcEnter      = 1;
    constexpr std::size_t   kRideSoldiers   = 0x70;
    constexpr std::size_t   kRideWork       = 0x80;   // 0x18 per riding soldier, the heli seat record at +0
    constexpr std::size_t   kRideWorkBase   = 0x88;   // soldier index of entry 0
    constexpr std::size_t   kWorkSize       = 0x18;
    constexpr std::size_t   kPartFlagInfo   = 0x110;
    constexpr std::size_t   kFlagRecordSize = 0x28;
    constexpr std::uint64_t kHideBit        = 1ull << 50;   // keeps him unrealized for the whole ride
    constexpr std::size_t   kRecordNotice   = 0x20;         // dword
    constexpr std::uint32_t kIgnoresNotices = 0x8000;       // the ride's Wake sets it: everything he sees or hears is thrown away

    StateAction_t g_OrigIdleAction = nullptr;
    void*         g_Target         = nullptr;
    bool          g_Installed      = false;

    // he may come back while his heli is realized; with an unrealized heli the ride keeps him hidden as vanilla does
    bool LetHimRealizeSeh(std::uint8_t* unrealRideHeli, std::uint32_t soldierIndex)
    {
        __try
        {
            auto* flagInfo = *reinterpret_cast<std::uint8_t**>(*reinterpret_cast<std::uint8_t**>(unrealRideHeli + kRideSoldiers) + kPartFlagInfo);
            if (soldierIndex >= *reinterpret_cast<std::uint32_t*>(flagInfo + 0x10))
                return false;
            auto* record = reinterpret_cast<std::uint64_t*>(*reinterpret_cast<std::uint8_t**>(flagInfo + 8) + static_cast<std::size_t>(soldierIndex) * kFlagRecordSize);
            auto* notice = reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uint8_t*>(record) + kRecordNotice);
            if (*notice & kIgnoresNotices)
                _InterlockedAnd(reinterpret_cast<volatile long*>(notice), static_cast<long>(~kIgnoresNotices));
            if (!(*record & kHideBit))
                return false;
            const std::uint32_t entry = soldierIndex - static_cast<std::uint32_t>(*reinterpret_cast<std::int32_t*>(unrealRideHeli + kRideWorkBase));
            const auto* heliSeat = *reinterpret_cast<std::uint8_t**>(*reinterpret_cast<std::uint8_t**>(unrealRideHeli + kRideWork) + static_cast<std::size_t>(entry) * kWorkSize);
            if (!RideHeliDoorSeat_HeliRealized(heliSeat))
            {
                if (SoldierRideHeli_HeliDead(soldierIndex))
                    SoldierRideHeli_RequestFall(soldierIndex, "lost his seat, the wreck of his heli is gone");   // GZ: a heli that is gone takes nobody along
                return false;
            }
            _InterlockedAnd64(reinterpret_cast<volatile long long*>(record), static_cast<long long>(~kHideBit));   // the life controller writes this word too
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void __fastcall hkIdleAction(void* unrealRideHeli, std::uint32_t soldierIndex, int proc)
    {
        g_OrigIdleAction(unrealRideHeli, soldierIndex, proc);
        if ((proc != kProcEnter && proc != kProcUpdate) || !SoldierRideHeli_IsDoorRider(soldierIndex) || MissionCodeGuard::ShouldBypassHooks())
            return;
        if (LetHimRealizeSeh(static_cast<std::uint8_t*>(unrealRideHeli), soldierIndex))
            SoldierRideHeli_NoteStep(soldierIndex, 4);
    }
}

bool Install_UnrealRideHeliActionImpl_State_IdleAction_Hook()
{
    if (g_Installed)
        return true;

    g_Target = gAddr.UnrealRideHeliActionImpl_State_IdleAction ? ResolveGameAddress(gAddr.UnrealRideHeliActionImpl_State_IdleAction) : nullptr;
    if (!g_Target)
    {
        LogError("UnrealRideHeliActionImpl_State_IdleAction: address missing for this build, soldiers boarded by script while far away ride hidden");
        return false;
    }
    if (!CreateAndEnableHook(g_Target, reinterpret_cast<void*>(&hkIdleAction), reinterpret_cast<void**>(&g_OrigIdleAction)))
    {
        LogError("UnrealRideHeliActionImpl_State_IdleAction: hook failed, soldiers boarded by script while far away ride hidden");
        g_Target = nullptr;
        return false;
    }
    g_Installed = true;
    return true;
}

bool Uninstall_UnrealRideHeliActionImpl_State_IdleAction_Hook()
{
    if (!g_Installed)
        return true;
    DisableAndRemoveHook(g_Target);
    g_OrigIdleAction = nullptr;
    g_Target         = nullptr;
    g_Installed      = false;
    return true;
}
