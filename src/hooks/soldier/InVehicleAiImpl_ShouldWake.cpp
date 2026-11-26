#include <pch.h>
#include "InVehicleAiImpl_ShouldWake.h"

#include <atomic>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "RideVehicleActionImpl_State_IdleAction.h"
#include "log.h"

namespace
{
    using ShouldWake_t          = bool(__fastcall*)(void* inVehicleAi, std::uint32_t soldier, const void* context);
    using GetQuarkSystemTable_t = std::uint8_t*(__fastcall*)();
    using CheckFlag_t           = bool(__fastcall*)(void* holder, std::uint32_t holderId, std::uint32_t soldier);
    using GetVehicleId_t        = void(__fastcall*)(void* rider, std::uint16_t* vehicleId, std::uint32_t soldier);

    struct GameObjectQuery
    {
        std::uint64_t  hash;
        std::uint8_t*  result;
    };
    using QueryGameObject_t = void(__fastcall*)(void* objects, std::uint32_t* out, std::uint16_t id, GameObjectQuery* query);

    constexpr std::uint64_t kRideStateQuery     = 0x757B8EC6C179;
    constexpr std::uint64_t kForcedGetOff       = 1ull << 38;
    constexpr std::uint32_t kVehicleCannotCarry = 0x800004; // the vehicle state InVehicleAi always dismounts on

    const std::uint8_t kShouldWakePrologue[] = {
        0x40, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x68, 0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00, 0x48, 0x33,
        0xC4, 0x48, 0x89, 0x44, 0x24, 0x50, 0x4C, 0x8B, 0x41, 0x50, 0x8B, 0xFA, 0x48, 0x8B, 0xF1
    };
    const std::uint8_t kShouldWakeMask[] = {
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
    };
    static_assert(sizeof(kShouldWakePrologue) == sizeof(kShouldWakeMask));

    ShouldWake_t          g_OrigShouldWake       = nullptr;
    GetQuarkSystemTable_t g_GetQuarkSystemTable  = nullptr;
    void*                 g_HookTarget           = nullptr;
#ifdef _DEBUG
    std::atomic<std::uint64_t> g_ProbeKeptTick = 0;
#endif

    // The vanilla reasons InVehicleAi has to take a rider out, less the one that fires in combat within 70 m of the target
    bool MustGetOffAnywaySEH(void* inVehicleAi, std::uint32_t soldier)
    {
        __try
        {
            auto* hub = *reinterpret_cast<std::uint8_t**>(static_cast<std::uint8_t*>(inVehicleAi) + 0x50);

            auto* rideTable = *reinterpret_cast<std::uint8_t**>(hub + 0x110);
            auto* rideRecords = *reinterpret_cast<std::uint8_t**>(rideTable + 0x08);
            const std::uint32_t rideCount = *reinterpret_cast<std::uint32_t*>(rideTable + 0x10);
            const std::uint8_t* rideRecord = soldier < rideCount ? rideRecords + soldier * 0x28ull : rideRecords;
            if (*reinterpret_cast<const std::uint64_t*>(rideRecord + 0x08) & kForcedGetOff)
                return true;

            auto* holderInfo = *reinterpret_cast<std::uint8_t**>(hub + 0xD0);
            auto* holder = *reinterpret_cast<std::uint8_t**>(holderInfo + 0x08);
            const auto checkFlag = *reinterpret_cast<CheckFlag_t*>(*reinterpret_cast<std::uint8_t**>(holder) + 0x158);
            if (checkFlag(holder, *reinterpret_cast<std::uint32_t*>(holderInfo + 0x30), soldier))
                return true;

            if (!g_GetQuarkSystemTable)
                return false;
            auto* rider = *reinterpret_cast<std::uint8_t**>(hub + 0x48);
            const auto getVehicleId = *reinterpret_cast<GetVehicleId_t*>(*reinterpret_cast<std::uint8_t**>(rider) + 0xA8);
            std::uint16_t vehicleId = 0;
            getVehicleId(rider, &vehicleId, soldier);
            auto* objects = *reinterpret_cast<std::uint8_t**>(g_GetQuarkSystemTable() + 0x60);
            const auto queryGameObject = *reinterpret_cast<QueryGameObject_t*>(*reinterpret_cast<std::uint8_t**>(objects) + 0x38);
            std::uint32_t out = 0;
            GameObjectQuery query{ kRideStateQuery, nullptr };
            queryGameObject(objects, &out, vehicleId, &query);
            return query.result && (*reinterpret_cast<const std::uint32_t*>(query.result + 0x70) & kVehicleCannotCarry);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return true;
        }
    }

    bool __fastcall hk_ShouldWake(void* inVehicleAi, std::uint32_t soldier, const void* context)
    {
        MISSION_GUARD_ORIGINAL_RET(g_OrigShouldWake, inVehicleAi, soldier, context);
        const bool wake = g_OrigShouldWake(inVehicleAi, soldier, context);
        if (!wake || !JeepGunner_IsGunner(soldier))
            return wake;
        const bool getOff = MustGetOffAnywaySEH(inVehicleAi, soldier);
#ifdef _DEBUG
        const std::uint64_t now = GetTickCount64();
        if (!getOff && now - g_ProbeKeptTick.load(std::memory_order_relaxed) > 2000)
        {
            g_ProbeKeptTick.store(now, std::memory_order_relaxed);
            LogDebug("JeepGunner probe: kept soldier %u aboard, InVehicleAi wanted him out", soldier);
        }
#endif
        return getOff;
    }
}

bool Install_InVehicleAiImpl_ShouldWake_Hook()
{
    if (g_HookTarget)
        return true;
    auto* target = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.SoldierInVehicleAiImpl_ShouldWake));
    if (!target)
        return true;
    const HookEntry entry = CheckHookEntryMasked(target, kShouldWakePrologue, kShouldWakeMask, sizeof(kShouldWakePrologue));
    if (entry == HookEntry::Unknown)
    {
        LogError("JeepGunner: soldier get-off code does not match %s, the jeep gunner gets out in combat", GetGameBuildName(gGameBuild));
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("JeepGunner: InVehicleAiImpl::ShouldWake", target);
    g_GetQuarkSystemTable = reinterpret_cast<GetQuarkSystemTable_t>(ResolveGameAddress(gAddr.GetQuarkSystemTable));
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_ShouldWake), reinterpret_cast<void**>(&g_OrigShouldWake)))
    {
        LogError("JeepGunner: InVehicleAiImpl::ShouldWake hook failed, the jeep gunner gets out in combat");
        return false;
    }
    g_HookTarget = target;
    return true;
}

bool Uninstall_InVehicleAiImpl_ShouldWake_Hook()
{
    if (g_HookTarget)
    {
        DisableAndRemoveHook(g_HookTarget);
        g_HookTarget = nullptr;
        g_OrigShouldWake = nullptr;
    }
    return true;
}
