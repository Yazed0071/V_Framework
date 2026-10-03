#include "pch.h"

#include <Windows.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "SoldierFaction.h"
#include "log.h"

namespace
{
    using HasOtherSoldierOnVec_t = bool(__fastcall*)(void* self, void* commonWork, std::uint32_t soldierIndex,
                                                     const float* start, const float* aim, const float* direction,
                                                     float length, void* context);

    constexpr std::uint32_t kSoldierSlotCount       = 0x200u;
    constexpr std::size_t   kRideVehicleSoldierCore = 0x88;
    constexpr float         kStaleAimSq             = 9.0f;

    HasOtherSoldierOnVec_t g_OrigHasOtherSoldierOnVec = nullptr;
    std::array<std::uint16_t, kSoldierSlotCount> g_loggedClear{};
    std::array<std::uint16_t, kSoldierSlotCount> g_loggedBlocked{};
    std::array<std::uint16_t, kSoldierSlotCount> g_loggedAimGap{};

    void LogAimGap(void* self, std::uint32_t soldierIndex, std::uint16_t rivalId, const float* aim)
    {
        if (!aim || soldierIndex >= kSoldierSlotCount || g_loggedAimGap[soldierIndex] == rivalId)
            return;
        const void* soldierCore = nullptr;
        std::memcpy(&soldierCore, static_cast<const std::uint8_t*>(self) + kRideVehicleSoldierCore, sizeof(soldierCore));
        float rival[3]{};
        if (!SoldierFaction_ReadRivalAimPoint(soldierCore, rivalId, rival))
            return;
        const float dx = aim[0] - rival[0];
        const float dy = aim[1] - rival[1];
        const float dz = aim[2] - rival[2];
        const float gapSq = dx * dx + dy * dy + dz * dz;
        if (gapSq < kStaleAimSq)
            return;
        g_loggedAimGap[soldierIndex] = rivalId;
        LogInfo("SoldierFaction: soldier %u vehicle aim is %.1f m from rival %u", soldierIndex, std::sqrt(gapSq),
                rivalId & 0x1FFu);
    }

    void LogOncePerRival(std::array<std::uint16_t, kSoldierSlotCount>& logged, std::uint32_t soldierIndex,
                         std::uint16_t rivalId, const char* what)
    {
        if (soldierIndex >= kSoldierSlotCount || logged[soldierIndex] == rivalId)
            return;
        logged[soldierIndex] = rivalId;
        LogInfo("SoldierFaction: soldier %u %s rival %u from vehicle", soldierIndex, what, rivalId & 0x1FFu);
    }

    bool __fastcall hk_HasOtherSoldierOnVec(void* self, void* commonWork, std::uint32_t soldierIndex,
                                            const float* start, const float* aim, const float* direction,
                                            float length, void* context)
    {
        MISSION_GUARD_ORIGINAL_RET(g_OrigHasOtherSoldierOnVec, self, commonWork, soldierIndex, start, aim, direction, length, context);

        const std::uint16_t rivalId = SoldierFaction_GetRivalTarget(soldierIndex);
        if (rivalId == 0)
            return g_OrigHasOtherSoldierOnVec(self, commonWork, soldierIndex, start, aim, direction, length, context);

        SoldierFaction_BeginShotScope(soldierIndex, rivalId);
        const bool blocked = g_OrigHasOtherSoldierOnVec(self, commonWork, soldierIndex, start, aim, direction, length, context);
        SoldierFaction_EndShotScope();

        LogOncePerRival(blocked ? g_loggedBlocked : g_loggedClear, soldierIndex, rivalId,
                        blocked ? "has a comrade in the line of fire at" : "has a clear line to");
        if (!blocked)
            LogAimGap(self, soldierIndex, rivalId, aim);
        return blocked;
    }
}

bool Install_RideVehicleActionImpl_HasOtherSoldierOnVec_Hook()
{
    void* target = ResolveGameAddress(gAddr.RideVehicleActionImpl_HasOtherSoldierOnVec);
    if (!target)
        return false;
    return CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_HasOtherSoldierOnVec),
                               reinterpret_cast<void**>(&g_OrigHasOtherSoldierOnVec));
}

bool Uninstall_RideVehicleActionImpl_HasOtherSoldierOnVec_Hook()
{
    void* target = ResolveGameAddress(gAddr.RideVehicleActionImpl_HasOtherSoldierOnVec);
    if (target)
        DisableAndRemoveHook(target);
    g_OrigHasOtherSoldierOnVec = nullptr;
    return true;
}
