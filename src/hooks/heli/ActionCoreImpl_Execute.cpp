#include <pch.h>
#include "ActionCoreImpl_Execute.h"

#include <Windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"
#include "HeliKey.h"

namespace
{
    using Execute_t          = void(__fastcall*)(void* core, std::uint32_t phase, std::uint32_t action);
    using CreateEffect_t     = void(__fastcall*)(void* effects, std::uint32_t action, std::int32_t type);
    using CallSound_t        = void(__fastcall*)(void* sounds, std::uint32_t heliIndex, std::int32_t type);
    using DeleteAllEffects_t = void(__fastcall*)(void* effects, std::uint32_t action, std::int32_t type, bool immediately);
    using IsSoundPlaying_t   = bool(__fastcall*)(void* sounds, std::uint32_t heliIndex, std::int32_t type);
    using StopSound_t        = void(__fastcall*)(void* sounds, std::uint32_t heliIndex, std::int32_t type);
    using IsEffectValid_t    = bool(__fastcall*)(void* effects, std::uint32_t action, std::int32_t type, std::uint32_t index);

    constexpr std::uint32_t kExecutePhase  = 4;
    constexpr std::int32_t  kDamagedSmoke  = 0xC;   // effect type started below 60% life
    constexpr std::int32_t  kDamageAlarm   = 7;     // sound type of the cockpit alarm while the heli is dying
    constexpr std::int8_t   kRoomLightKeep = -1;
    constexpr std::int32_t  kRoomLight     = 6;   // cabin light effects
    constexpr std::int32_t  kRoomLight2    = 7;

    constexpr std::size_t kVtbl_CreateEffect     = 0x18;
    constexpr std::size_t kVtbl_DeleteAllEffects = 0x40;
    constexpr std::size_t kVtbl_IsEffectValid    = 0x58;
    constexpr std::size_t kVtbl_StopSound        = 0x48;
    constexpr std::size_t kVtbl_IsSoundPlaying   = 0x60;

    Execute_t      g_OrigExecute      = nullptr;
    CreateEffect_t g_OrigCreateEffect = nullptr;
    CallSound_t    g_OrigCallSound    = nullptr;
    bool           g_Installed        = false;

    std::atomic<bool>        g_NoDamageEffects[HeliKey::kCount];
    std::atomic<bool>        g_DamageEffectsStopped[HeliKey::kCount];
    std::atomic<std::int8_t> g_RoomLight[HeliKey::kCount];
    std::atomic<std::uint8_t> g_RoomLightRemoved[HeliKey::kCount];   // bit 0 RoomLight, bit 1 RoomLight2
    std::atomic<std::uint8_t> g_RoomLightCreated[HeliKey::kCount];

    thread_local bool t_SkipDamageEffects = false;

    struct HeliAction
    {
        std::uint8_t* work;
        std::uint32_t heliIndex;
        std::uint32_t heliKey;
    };

    bool ReadHeliAction(void* core, std::uint32_t action, HeliAction& out)
    {
        __try
        {
            auto* self = static_cast<std::uint8_t*>(core);
            auto* heli = *reinterpret_cast<std::uint8_t**>(self + 0x50);
            if (!heli)
                return false;
            const std::uint32_t type = *reinterpret_cast<std::uint16_t*>(heli + 0x130);   // game object type of the heli
            if (!HeliKey::IsHeliType(type))
                return false;
            auto* works = *reinterpret_cast<std::uint8_t**>(self + 0x70);
            if (!works)
                return false;
            const std::uint32_t entry = action - static_cast<std::uint32_t>(*reinterpret_cast<std::int32_t*>(self + 0x90));
            out.work      = works + static_cast<std::size_t>(entry) * 0x48;
            out.heliIndex = *reinterpret_cast<std::uint32_t*>(out.work + 0x3C);   // every heli action keeps 0x48 bytes, its heli index at +0x3C
            if (out.heliIndex > 0x1FFu)
                return false;
            out.heliKey = HeliKey::Of(type, out.heliIndex);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void ApplyRoomLight(const HeliAction& heli)
    {
        const std::int8_t mode = g_RoomLight[heli.heliKey].load(std::memory_order_relaxed);
        if (mode == kRoomLightKeep)
            return;
        __try
        {
            *reinterpret_cast<std::int32_t*>(heli.work + 0x10) = mode;   // cabin light: 0 lit while the player is aboard, 1 always lit, 2 always off
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
        if (mode == 0)
        {
            std::int8_t expected = 0;
            g_RoomLight[heli.heliKey].compare_exchange_strong(expected, kRoomLightKeep, std::memory_order_relaxed);
        }
    }

    void StopDamageEffects(void* core, std::uint32_t action, std::uint32_t heliIndex)
    {
        __try
        {
            auto* self    = static_cast<std::uint8_t*>(core);
            auto* effects = *reinterpret_cast<std::uint8_t**>(*reinterpret_cast<std::uint8_t**>(self + 0x58) + 0x10);
            auto* sounds  = *reinterpret_cast<std::uint8_t**>(*reinterpret_cast<std::uint8_t**>(self + 0x50) + 0x78);
            if (effects)
            {
                void** vtbl = *reinterpret_cast<void***>(effects);
                reinterpret_cast<DeleteAllEffects_t>(vtbl[kVtbl_DeleteAllEffects / sizeof(void*)])(effects, action, kDamagedSmoke, false);
            }
            if (sounds)
            {
                void** vtbl = *reinterpret_cast<void***>(sounds);
                if (reinterpret_cast<IsSoundPlaying_t>(vtbl[kVtbl_IsSoundPlaying / sizeof(void*)])(sounds, heliIndex, kDamageAlarm))
                    reinterpret_cast<StopSound_t>(vtbl[kVtbl_StopSound / sizeof(void*)])(sounds, heliIndex, kDamageAlarm);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void ApplyRoomLightEffects(void* core, std::uint32_t action, std::uint32_t heliKey, std::int8_t mode)
    {
        if (mode == kRoomLightKeep)
            return;
        __try
        {
            auto* effects = *reinterpret_cast<std::uint8_t**>(*reinterpret_cast<std::uint8_t**>(static_cast<std::uint8_t*>(core) + 0x58) + 0x10);
            if (!effects)
                return;
            void** vtbl = *reinterpret_cast<void***>(effects);
            const auto isValid   = reinterpret_cast<IsEffectValid_t>(vtbl[kVtbl_IsEffectValid / sizeof(void*)]);
            const auto create    = reinterpret_cast<CreateEffect_t>(vtbl[kVtbl_CreateEffect / sizeof(void*)]);
            const auto deleteAll = reinterpret_cast<DeleteAllEffects_t>(vtbl[kVtbl_DeleteAllEffects / sizeof(void*)]);
            const std::int32_t types[2] = { kRoomLight, kRoomLight2 };
            for (int k = 0; k < 2; ++k)
            {
                const std::uint8_t bit = static_cast<std::uint8_t>(1u << k);
                const bool shown = isValid(effects, action, types[k], 0);
                if (mode == 1 && !shown)
                {
                    create(effects, action, types[k]);
                    if (g_RoomLightRemoved[heliKey].fetch_and(static_cast<std::uint8_t>(~bit), std::memory_order_relaxed) & bit)
                        continue;
                    g_RoomLightCreated[heliKey].fetch_or(bit, std::memory_order_relaxed);
                }
                else if (mode == 2 && shown)
                {
                    deleteAll(effects, action, types[k], false);
                    if (g_RoomLightCreated[heliKey].fetch_and(static_cast<std::uint8_t>(~bit), std::memory_order_relaxed) & bit)
                        continue;
                    g_RoomLightRemoved[heliKey].fetch_or(bit, std::memory_order_relaxed);
                }
                else if (mode == 0)
                {
                    if ((g_RoomLightRemoved[heliKey].load(std::memory_order_relaxed) & bit) && !shown)
                        create(effects, action, types[k]);
                    if ((g_RoomLightCreated[heliKey].load(std::memory_order_relaxed) & bit) && shown)
                        deleteAll(effects, action, types[k], false);
                }
            }
            if (mode == 0)
            {
                g_RoomLightRemoved[heliKey].store(0, std::memory_order_relaxed);
                g_RoomLightCreated[heliKey].store(0, std::memory_order_relaxed);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void __fastcall hkExecute(void* core, std::uint32_t phase, std::uint32_t action)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigExecute, core, phase, action);

        HeliAction heli{};
        if (phase != kExecutePhase || !ReadHeliAction(core, action, heli))
        {
            g_OrigExecute(core, phase, action);
            return;
        }

        const std::int8_t roomLight = g_RoomLight[heli.heliKey].load(std::memory_order_relaxed);
        ApplyRoomLight(heli);

        const bool noDamageEffects = g_NoDamageEffects[heli.heliKey].load(std::memory_order_relaxed);
        t_SkipDamageEffects = noDamageEffects;
        g_OrigExecute(core, phase, action);
        t_SkipDamageEffects = false;

        if (noDamageEffects && !g_DamageEffectsStopped[heli.heliKey].exchange(true, std::memory_order_relaxed))
            StopDamageEffects(core, action, heli.heliIndex);
        ApplyRoomLightEffects(core, action, heli.heliKey, roomLight);
    }

    void __fastcall hkCreateEffect(void* effects, std::uint32_t action, std::int32_t type)
    {
        if (t_SkipDamageEffects && type == kDamagedSmoke)
            return;
        g_OrigCreateEffect(effects, action, type);
    }

    void __fastcall hkCallSound(void* sounds, std::uint32_t heliIndex, std::int32_t type)
    {
        if (t_SkipDamageEffects && type == kDamageAlarm)
            return;
        g_OrigCallSound(sounds, heliIndex, type);
    }
}

void HeliDamageEffects_SetEnabled(std::uint32_t heliKey, bool enabled)
{
    if (heliKey >= HeliKey::kCount)
        return;
    g_NoDamageEffects[heliKey].store(!enabled, std::memory_order_relaxed);
    g_DamageEffectsStopped[heliKey].store(false, std::memory_order_relaxed);
}

void HeliRoomLight_SetMode(std::uint32_t heliKey, std::int32_t mode)
{
    if (heliKey < HeliKey::kCount && mode >= 0 && mode <= 2)
        g_RoomLight[heliKey].store(static_cast<std::int8_t>(mode), std::memory_order_relaxed);
}

void HeliActionCore_Clear()
{
    for (std::uint32_t i = 0; i < HeliKey::kCount; ++i)
    {
        g_NoDamageEffects[i].store(false, std::memory_order_relaxed);
        g_DamageEffectsStopped[i].store(false, std::memory_order_relaxed);
        g_RoomLight[i].store(kRoomLightKeep, std::memory_order_relaxed);
        g_RoomLightRemoved[i].store(0, std::memory_order_relaxed);
        g_RoomLightCreated[i].store(0, std::memory_order_relaxed);
    }
}

bool Install_ActionCoreImpl_Execute_Hook()
{
    if (g_Installed)
        return true;

    HeliActionCore_Clear();

    void* execute      = gAddr.HeliActionCoreImpl_Execute ? ResolveGameAddress(gAddr.HeliActionCoreImpl_Execute) : nullptr;
    void* createEffect = gAddr.HeliEffectControllerImpl_CreateEffect ? ResolveGameAddress(gAddr.HeliEffectControllerImpl_CreateEffect) : nullptr;
    void* callSound    = gAddr.HeliSoundControllerImpl_CallSound ? ResolveGameAddress(gAddr.HeliSoundControllerImpl_CallSound) : nullptr;
    if (!execute || !createEffect || !callSound)
    {
        LogError("ActionCoreImpl_Execute: address missing for this build, heli damage effect and room light commands off");
        return false;
    }

    if (!CreateAndEnableHook(createEffect, reinterpret_cast<void*>(&hkCreateEffect), reinterpret_cast<void**>(&g_OrigCreateEffect)))
    {
        LogError("ActionCoreImpl_Execute: effect hook failed, heli damage effect and room light commands off");
        return false;
    }
    if (!CreateAndEnableHook(callSound, reinterpret_cast<void*>(&hkCallSound), reinterpret_cast<void**>(&g_OrigCallSound)))
    {
        DisableAndRemoveHook(createEffect);
        LogError("ActionCoreImpl_Execute: sound hook failed, heli damage effect and room light commands off");
        return false;
    }
    if (!CreateAndEnableHook(execute, reinterpret_cast<void*>(&hkExecute), reinterpret_cast<void**>(&g_OrigExecute)))
    {
        DisableAndRemoveHook(callSound);
        DisableAndRemoveHook(createEffect);
        LogError("ActionCoreImpl_Execute: hook failed, heli damage effect and room light commands off");
        return false;
    }

    g_Installed = true;
    return true;
}

bool Uninstall_ActionCoreImpl_Execute_Hook()
{
    if (!g_Installed)
        return true;

    DisableAndRemoveHook(ResolveGameAddress(gAddr.HeliActionCoreImpl_Execute));
    DisableAndRemoveHook(ResolveGameAddress(gAddr.HeliSoundControllerImpl_CallSound));
    DisableAndRemoveHook(ResolveGameAddress(gAddr.HeliEffectControllerImpl_CreateEffect));
    g_OrigExecute      = nullptr;
    g_OrigCreateEffect = nullptr;
    g_OrigCallSound    = nullptr;
    g_Installed        = false;
    return true;
}
