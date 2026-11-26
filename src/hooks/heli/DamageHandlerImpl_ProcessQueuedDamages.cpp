#include <pch.h>
#include "DamageHandlerImpl_ProcessQueuedDamages.h"

#include <Windows.h>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "LuaBroadcaster.h"
#include "MissionCodeGuard.h"
#include "log.h"
#include "HeliKey.h"
#include "HeliSoundController.h"
#include "../soldier/RideHeliActionImpl_State_GetInAction.h"
#include "../soldier/SoldierFaction.h"

namespace
{
    using ProcessQueuedDamages_t = void(__fastcall*)(void* handler, std::uint32_t heliIndex, std::int16_t attackerId, void* damages, std::uint32_t count);

    ProcessQueuedDamages_t g_OrigProcessQueuedDamages = nullptr;
    bool                   g_Installed                = false;

    bool ReadLifeRate(void* handler, std::uint32_t heliIndex, float& rate, std::uint32_t& type)
    {
        __try
        {
            auto* heli = *reinterpret_cast<std::uint8_t**>(static_cast<std::uint8_t*>(handler) + 0x40);
            if (!heli)
                return false;
            type = *reinterpret_cast<std::uint16_t*>(heli + 0x130);   // game object type of the heli
            if (!HeliKey::IsHeliType(type))
                return false;
            auto* lifeController = *reinterpret_cast<std::uint8_t**>(heli + 0xC8);
            auto* lives = lifeController ? *reinterpret_cast<std::uint8_t**>(lifeController + 8) : nullptr;
            if (!lives)
                return false;
            const std::int32_t  life    = *reinterpret_cast<std::int32_t*>(lives + static_cast<std::size_t>(heliIndex) * 0x10);        // 0x10 per heli: life, then max life
            const std::uint32_t maxLife = *reinterpret_cast<std::uint32_t*>(lives + static_cast<std::size_t>(heliIndex) * 0x10 + 4);
            rate = maxLife ? static_cast<float>(life > 0 ? life : 0) / static_cast<float>(maxLife) : 1.0f;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ReadFirstDamage(const void* damages, std::uint32_t count, std::uint16_t& attackId, float* hit)
    {
        if (!damages || count == 0)
            return false;
        __try
        {
            const auto* damage = static_cast<const std::uint8_t*>(damages);
            attackId = *reinterpret_cast<const std::uint16_t*>(damage + 0x28);
            const float* at = reinterpret_cast<const float*>(damage + 0x08);   // where it hit, the hit sound plays there
            hit[0] = at[0];
            hit[1] = at[1];
            hit[2] = at[2];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool CrossedTenth(float before, float after)
    {
        const float step = std::floor(before * 10.0f + 0.0001f) / 10.0f;
        return after < step - 0.0001f;
    }

    void __fastcall hkProcessQueuedDamages(void* handler, std::uint32_t heliIndex, std::int16_t attackerId, void* damages, std::uint32_t count)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigProcessQueuedDamages, handler, heliIndex, attackerId, damages, count);

        float before = 0.0f;
        std::uint32_t type = 0;
        const bool known = ReadLifeRate(handler, heliIndex, before, type);
        if (known)
            SoldierFaction_OnHeliDamages(static_cast<std::uint16_t>((type << 9) | (heliIndex & 0x1FFu)), damages, count);
        std::uint16_t attackId = 0;
        float hit[3] = {};
        const bool hitKnown = known && ReadFirstDamage(damages, count, attackId, hit);
        g_OrigProcessQueuedDamages(handler, heliIndex, attackerId, damages, count);

        float after = 0.0f;
        if (!known || !ReadLifeRate(handler, heliIndex, after, type))
            return;
        if (hitKnown)
            PilotVoice_OnHeliDamaged(handler, heliIndex, attackId, hit, before, after);
        const std::uint32_t heliId = (type << 9) | (heliIndex & 0x1FFu);
        const bool destroyed = before > 0.0f && after <= 0.0f;
        if (CrossedTenth(before, after) || destroyed)   // a death from inside the last tenth still reports 0
            V_FrameWork::EmitMessage("GameObject", "NotifyLifeRate", heliId, static_cast<double>(after));
        if (destroyed)
            SoldierRideHeli_HeliDestroyed(heliId);   // GZ: riders of a heli at 0 life go limp at their doors
    }
}

bool Install_DamageHandlerImpl_ProcessQueuedDamages_Hook()
{
    if (g_Installed)
        return true;

    void* target = gAddr.HeliDamageHandlerImpl_ProcessQueuedDamages ? ResolveGameAddress(gAddr.HeliDamageHandlerImpl_ProcessQueuedDamages) : nullptr;
    if (!target)
    {
        LogError("DamageHandlerImpl_ProcessQueuedDamages: address missing for this build, NotifyLifeRate message off");
        return false;
    }
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkProcessQueuedDamages), reinterpret_cast<void**>(&g_OrigProcessQueuedDamages)))
    {
        LogError("DamageHandlerImpl_ProcessQueuedDamages: hook failed, NotifyLifeRate message off");
        return false;
    }
    g_Installed = true;
    return true;
}

bool Uninstall_DamageHandlerImpl_ProcessQueuedDamages_Hook()
{
    if (!g_Installed)
        return true;
    DisableAndRemoveHook(ResolveGameAddress(gAddr.HeliDamageHandlerImpl_ProcessQueuedDamages));
    g_OrigProcessQueuedDamages = nullptr;
    g_Installed                = false;
    return true;
}
