#include <pch.h>
#include "MechaActionImpl_StateOnPassenger.h"

#include <Windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"
#include "HeliKey.h"
#include "HeliSoundController.h"

namespace
{
    using StateProc_t = void(__fastcall*)(void* mecha, std::uint32_t realizedIndex, std::uint32_t side, int proc);

    constexpr std::uint32_t kDoorSides = 2;
    constexpr int           kProcDecideNext = 5;

    constexpr std::uint8_t kOnPassenger  = 0;   // closed, waits for a passenger to open it
    constexpr std::uint8_t kOpening      = 1;
    constexpr std::uint8_t kOpen         = 2;
    constexpr std::uint8_t kOffPassenger = 3;   // open, waits for a passenger to close it
    constexpr std::uint8_t kClosing      = 4;
    constexpr std::uint8_t kClosed       = 5;
    constexpr std::uint8_t kNoNextState  = 6;

    StateProc_t g_OrigOnPassenger  = nullptr;
    StateProc_t g_OrigOffPassenger = nullptr;
    bool        g_Installed        = false;

    std::atomic<std::uint8_t> g_Request[HeliKey::kCount][kDoorSides];
    std::atomic<std::uint8_t> g_LastNext[HeliKey::kCount][kDoorSides];

    void ForceDoor(std::uint8_t* door, HeliDoorRequest request)
    {
        const std::uint8_t state = door[0];   // per door: current state, next state, wanted state, bit0 of the last byte skips the clip
        if (request == HeliDoorRequest::Open)
        {
            if (state == kClosed || state == kOnPassenger)
            {
                door[3] = static_cast<std::uint8_t>(door[3] & ~1);
                door[2] = kOpen;
                door[1] = kOpening;
            }
            else if (state == kOpen || state == kOffPassenger)
            {
                door[2] = kOpen;
                door[1] = kNoNextState;
            }
            return;
        }
        if (state == kOpen || state == kOffPassenger)
        {
            door[3] = static_cast<std::uint8_t>(door[3] & ~1);
            door[2] = kClosed;
            door[1] = kClosing;
        }
        else if (state == kClosed || state == kOnPassenger)
        {
            door[2] = kClosed;
            door[1] = kNoNextState;
        }
    }

    void __fastcall hkOnPassenger(void* mecha, std::uint32_t realizedIndex, std::uint32_t side, int proc)
    {
        g_OrigOnPassenger(mecha, realizedIndex, side, proc);
        HeliDoor_ApplyRequest(mecha, realizedIndex, side, proc);
        HeliDoor_Announce(mecha, realizedIndex, side, proc);
    }

    void __fastcall hkOffPassenger(void* mecha, std::uint32_t realizedIndex, std::uint32_t side, int proc)
    {
        g_OrigOffPassenger(mecha, realizedIndex, side, proc);
        HeliDoor_ApplyRequest(mecha, realizedIndex, side, proc);
        HeliDoor_Announce(mecha, realizedIndex, side, proc);
    }
}

void HeliDoor_SetRequest(std::uint32_t heliKey, std::uint32_t side, HeliDoorRequest request)
{
    if (heliKey < HeliKey::kCount && side < kDoorSides)
        g_Request[heliKey][side].store(static_cast<std::uint8_t>(request), std::memory_order_relaxed);
}

void HeliDoor_ApplyRequest(void* mecha, std::uint32_t realizedIndex, std::uint32_t side, int proc)
{
    if (proc != kProcDecideNext || side >= kDoorSides || MissionCodeGuard::ShouldBypassHooks())
        return;
    __try
    {
        auto* self = static_cast<std::uint8_t*>(mecha);
        auto* heli = *reinterpret_cast<std::uint8_t**>(self + 0x68);
        if (!heli)
            return;
        const std::uint32_t type = *reinterpret_cast<std::uint16_t*>(heli + 0x130);   // game object type of the heli
        if (!HeliKey::IsHeliType(type))
            return;
        const std::uint32_t entry = realizedIndex - static_cast<std::uint32_t>(*reinterpret_cast<std::int32_t*>(self + 0x88));
        auto* work = *reinterpret_cast<std::uint8_t**>(self + 0x80) + static_cast<std::size_t>(entry) * 0x18;   // 0x18 per realized heli
        const std::uint32_t heliKey = HeliKey::Of(type, work[0x11]);
        const auto request = static_cast<HeliDoorRequest>(g_Request[heliKey][side].load(std::memory_order_relaxed));
        if (request == HeliDoorRequest::None)
            return;
        auto* doors = *reinterpret_cast<std::uint8_t**>(work + 8);   // 4 bytes per layer: 0 left door, 1 right door
        if (doors)
            ForceDoor(doors + static_cast<std::size_t>(side) * 4, request);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

void HeliDoor_Announce(void* mecha, std::uint32_t realizedIndex, std::uint32_t side, int proc)   // the pilot's door lines, from all four door states: with Snake outside the door never enters the passenger ones
{
    if (proc != kProcDecideNext || side >= kDoorSides)
        return;
    std::uint32_t type = 0;
    std::uint32_t index = 0;
    std::uint8_t next = kNoNextState;
    __try
    {
        auto* self = static_cast<std::uint8_t*>(mecha);
        auto* heli = *reinterpret_cast<std::uint8_t**>(self + 0x68);
        if (!heli)
            return;
        type = *reinterpret_cast<std::uint16_t*>(heli + 0x130);
        if (!HeliKey::IsHeliType(type))
            return;
        const std::uint32_t entry = realizedIndex - static_cast<std::uint32_t>(*reinterpret_cast<std::int32_t*>(self + 0x88));
        auto* work = *reinterpret_cast<std::uint8_t**>(self + 0x80) + static_cast<std::size_t>(entry) * 0x18;
        index = work[0x11];
        auto* doors = *reinterpret_cast<std::uint8_t**>(work + 8);
        if (!doors)
            return;
        next = doors[static_cast<std::size_t>(side) * 4 + 1];
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return;
    }
    const std::uint8_t last = g_LastNext[HeliKey::Of(type, index)][side].exchange(next, std::memory_order_relaxed);
    if (next != last && (next == kOpening || next == kClosing))
        PilotVoice_OnDoorMoving(type, index, next == kOpening);
}

void HeliDoor_Clear()
{
    for (std::uint32_t i = 0; i < HeliKey::kCount; ++i)
        for (std::uint32_t s = 0; s < kDoorSides; ++s)
        {
            g_Request[i][s].store(static_cast<std::uint8_t>(HeliDoorRequest::None), std::memory_order_relaxed);
            g_LastNext[i][s].store(kNoNextState, std::memory_order_relaxed);
        }
}

void HeliDoor_ClearForReload()
{
    for (std::uint32_t i = 0; i < HeliKey::kCount; ++i)
        for (std::uint32_t s = 0; s < kDoorSides; ++s)
            g_LastNext[i][s].store(kNoNextState, std::memory_order_relaxed);
}

bool Install_MechaActionImpl_StateOnPassenger_Hook()
{
    if (g_Installed)
        return true;

    HeliDoor_Clear();

    void* onPassenger  = gAddr.MechaActionImpl_StateOnPassenger ? ResolveGameAddress(gAddr.MechaActionImpl_StateOnPassenger) : nullptr;
    void* offPassenger = gAddr.MechaActionImpl_StateOffPassenger ? ResolveGameAddress(gAddr.MechaActionImpl_StateOffPassenger) : nullptr;
    if (!onPassenger || !offPassenger)
    {
        LogError("MechaActionImpl_StateOnPassenger: address missing for this build, heli door commands off");
        return false;
    }
    if (!CreateAndEnableHook(onPassenger, reinterpret_cast<void*>(&hkOnPassenger), reinterpret_cast<void**>(&g_OrigOnPassenger)))
    {
        LogError("MechaActionImpl_StateOnPassenger: hook failed, heli door commands off");
        return false;
    }
    if (!CreateAndEnableHook(offPassenger, reinterpret_cast<void*>(&hkOffPassenger), reinterpret_cast<void**>(&g_OrigOffPassenger)))
    {
        DisableAndRemoveHook(onPassenger);
        LogError("MechaActionImpl_StateOnPassenger: StateOffPassenger hook failed, heli door commands off");
        return false;
    }
    g_Installed = true;
    return true;
}

bool Uninstall_MechaActionImpl_StateOnPassenger_Hook()
{
    if (!g_Installed)
        return true;
    DisableAndRemoveHook(ResolveGameAddress(gAddr.MechaActionImpl_StateOffPassenger));
    DisableAndRemoveHook(ResolveGameAddress(gAddr.MechaActionImpl_StateOnPassenger));
    g_OrigOnPassenger  = nullptr;
    g_OrigOffPassenger = nullptr;
    g_Installed        = false;
    return true;
}
