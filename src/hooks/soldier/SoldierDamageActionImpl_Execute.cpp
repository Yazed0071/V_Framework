#include <pch.h>
#include "SoldierDamageActionImpl_Execute.h"

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
    using Execute_t = void(__fastcall*)(void* damageAction, std::int32_t phase, std::uint32_t realizedSlot);

    constexpr std::int32_t kPhaseUpdate   = 1;
    constexpr std::size_t  kWork          = 0x1B0;   // on the interface: 0xF0 per realized soldier
    constexpr std::size_t  kWorkBase      = 0x1B8;   // realized slot of entry 0
    constexpr std::size_t  kWorkSize      = 0xF0;
    constexpr std::size_t  kWorkSoldier   = 0xDC;
    constexpr std::size_t  kWorkState     = 0xE2;
    constexpr std::uint8_t kStateFallBound = 0x28;   // State_FallBound 0x141447D50: FallIdle saw the ground and the landing clip plays

    Execute_t g_OrigExecute = nullptr;
    void*     g_Target      = nullptr;
    bool      g_Installed   = false;

    void NoteLandingSeh(std::uint8_t* damageAction, std::uint32_t realizedSlot)
    {
        __try
        {
            const std::uint32_t entry = realizedSlot - static_cast<std::uint32_t>(*reinterpret_cast<std::int32_t*>(damageAction + kWorkBase));
            auto* work = *reinterpret_cast<std::uint8_t**>(damageAction + kWork) + static_cast<std::size_t>(entry) * kWorkSize;
            const std::uint32_t index = *reinterpret_cast<std::uint32_t*>(work + kWorkSoldier);
            if (work[kWorkState] == kStateFallBound && SoldierRideHeli_Falling(index))
                SoldierRideHeli_Landed(index);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void __fastcall hkExecute(void* damageAction, std::int32_t phase, std::uint32_t realizedSlot)
    {
        g_OrigExecute(damageAction, phase, realizedSlot);
        if (phase == kPhaseUpdate && SoldierRideHeli_AnyFalling() && !MissionCodeGuard::ShouldBypassHooks())
            NoteLandingSeh(static_cast<std::uint8_t*>(damageAction), realizedSlot);
    }
}

bool Install_SoldierDamageActionImpl_Execute_Hook()
{
    if (g_Installed)
        return true;

    g_Target = gAddr.SoldierDamageActionImpl_Execute ? ResolveGameAddress(gAddr.SoldierDamageActionImpl_Execute) : nullptr;
    if (!g_Target)
    {
        LogError("SoldierDamageActionImpl_Execute: address missing for this build, heli riders who fall out are not killed on landing");
        return false;
    }
    if (!CreateAndEnableHook(g_Target, reinterpret_cast<void*>(&hkExecute), reinterpret_cast<void**>(&g_OrigExecute)))
    {
        LogError("SoldierDamageActionImpl_Execute: hook failed, heli riders who fall out are not killed on landing");
        g_Target = nullptr;
        return false;
    }
    g_Installed = true;
    return true;
}

bool Uninstall_SoldierDamageActionImpl_Execute_Hook()
{
    if (!g_Installed)
        return true;
    DisableAndRemoveHook(g_Target);
    g_Target      = nullptr;
    g_OrigExecute = nullptr;
    g_Installed   = false;
    return true;
}
