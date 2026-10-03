#include "pch.h"

#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "SoldierFaction.h"
#include "log.h"

namespace
{
    using HasOtherSoldierOnVec_t = bool(__fastcall*)(void* self, std::uint32_t shooterIndex, const float* muzzle,
                                                     const float* shotTarget, const float* direction, float length,
                                                     void* ray);

    constexpr std::size_t   kAttackActionSoldierCore = 0x78;
    constexpr std::uint32_t kSoldierSlotCount        = 0x200u;

    HasOtherSoldierOnVec_t g_OrigHasOtherSoldierOnVec = nullptr;
    std::array<std::uint16_t, kSoldierSlotCount> g_loggedShotRival{};

    void LogFirstShotAtRival(std::uint32_t shooterIndex)
    {
        if (shooterIndex >= kSoldierSlotCount)
            return;
        const std::uint16_t rivalId = SoldierFaction_GetRivalTarget(shooterIndex);
        if (g_loggedShotRival[shooterIndex] == rivalId)
            return;
        g_loggedShotRival[shooterIndex] = rivalId;
        LogInfo("SoldierFaction: soldier %u is firing at rival %u", shooterIndex, rivalId & 0x1FFu);
    }

    bool __fastcall hk_HasOtherSoldierOnVec(void* self, std::uint32_t shooterIndex, const float* muzzle,
                                            const float* shotTarget, const float* direction, float length, void* ray)
    {
        MISSION_GUARD_ORIGINAL_RET(g_OrigHasOtherSoldierOnVec, self, shooterIndex, muzzle, shotTarget, direction, length, ray);

        if (SoldierFaction_GetRivalTarget(shooterIndex) == 0)
            return g_OrigHasOtherSoldierOnVec(self, shooterIndex, muzzle, shotTarget, direction, length, ray);

        const void* soldierCore = nullptr;
        std::memcpy(&soldierCore, static_cast<const std::uint8_t*>(self) + kAttackActionSoldierCore, sizeof(soldierCore));
        if (!SoldierFaction_IsShotAtRival(soldierCore, shooterIndex, shotTarget))
            return g_OrigHasOtherSoldierOnVec(self, shooterIndex, muzzle, shotTarget, direction, length, ray);

        SoldierFaction_MarkFiring(shooterIndex);
        LogFirstShotAtRival(shooterIndex);
        return false;
    }
}

bool Install_AttackActionImpl_HasOtherSoldierOnVec_Hook()
{
    void* target = ResolveGameAddress(gAddr.AttackActionImpl_HasOtherSoldierOnVec);
    if (!target)
        return false;
    return CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_HasOtherSoldierOnVec),
                               reinterpret_cast<void**>(&g_OrigHasOtherSoldierOnVec));
}

bool Uninstall_AttackActionImpl_HasOtherSoldierOnVec_Hook()
{
    void* target = ResolveGameAddress(gAddr.AttackActionImpl_HasOtherSoldierOnVec);
    if (target)
        DisableAndRemoveHook(target);
    g_OrigHasOtherSoldierOnVec = nullptr;
    return true;
}
