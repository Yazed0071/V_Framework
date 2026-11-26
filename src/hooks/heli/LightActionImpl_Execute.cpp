#include <pch.h>
#include "LightActionImpl_Execute.h"

#include <Windows.h>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"
#include "HeliKey.h"
#include "../fox/gm/GameObjectInstance.h"
#include "../fox/gm/GameObjectPosition.h"

namespace
{
    using Execute_t           = void(__fastcall*)(void* light, std::uint32_t phase, std::uint32_t slot);
    using IsAimTypePosition_t = bool(__fastcall*)(void* gun, std::uint32_t slot);
    using GetAimPosition_t    = float*(__fastcall*)(void* gun, float* out, std::uint32_t slot);
    using GetWorldMatrix_t    = const float*(__fastcall*)(void* animation, std::uint32_t slot);

    constexpr std::size_t kVtbl_GetWorldMatrix = 0x70;

    constexpr std::uint32_t kNoTarget         = 0xFFFFFFFFu;
    constexpr std::uint32_t kExecutePhase     = 4;
    constexpr float         kAngleAimDistance = 1000.0f;
    constexpr float         kDegreesToRadians = 0.017453292f;
    constexpr std::uint32_t kSoldierType      = 2;
    constexpr std::uint64_t kPumpIntervalMs   = 15;

    Execute_t           g_OrigExecute           = nullptr;
    IsAimTypePosition_t g_OrigIsAimTypePosition = nullptr;
    GetAimPosition_t    g_OrigGetAimPosition    = nullptr;
    bool                g_Installed             = false;

    std::atomic<std::uint32_t> g_Target[HeliKey::kCount];
    std::atomic<bool>          g_HasAngle[HeliKey::kCount];
    std::atomic<float>         g_Yaw[HeliKey::kCount];
    std::atomic<float>         g_Pitch[HeliKey::kCount];
    std::atomic<std::uint32_t> g_WarnedTarget[HeliKey::kCount];
    std::atomic<bool>          g_WarnedAngle[HeliKey::kCount];
    std::atomic<float>         g_Cached[HeliKey::kCount][3];
    std::atomic<std::uint32_t> g_CachedTarget[HeliKey::kCount];
    std::atomic<bool>          g_CachedValid[HeliKey::kCount];
    std::atomic<std::uint64_t> g_LastPumpMs{ 0 };

    struct AimOverride
    {
        bool          active;
        std::uint32_t slot;
        alignas(16) float point[4];
    };

    thread_local AimOverride t_Aim{};

    bool ReadHeliKey(void* light, std::uint32_t slot, std::uint32_t& heliKey)
    {
        __try
        {
            auto* self = static_cast<std::uint8_t*>(light);
            auto* heli = *reinterpret_cast<std::uint8_t**>(self + 0x40);
            if (!heli)
                return false;
            const std::uint32_t type = *reinterpret_cast<std::uint16_t*>(heli + 0x130);   // game object type of the heli this light sits on
            if (!HeliKey::IsHeliType(type))
                return false;
            auto* works = *reinterpret_cast<std::uint8_t**>(self + 0x50);
            if (!works)
                return false;
            const std::uint32_t entry = slot - static_cast<std::uint32_t>(*reinterpret_cast<std::int32_t*>(self + 0x58));
            const std::uint32_t index = *reinterpret_cast<std::uint32_t*>(works + static_cast<std::size_t>(entry) * 0x20 + 0x18);   // every light keeps 0x20 bytes, its heli index at +0x18
            if (index > 0x1FFu)
                return false;
            heliKey = HeliKey::Of(type, index);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool AngleAimPoint(void* light, std::uint32_t slot, std::uint32_t heliKey, float point[3])
    {
        const float yaw   = g_Yaw[heliKey].load(std::memory_order_relaxed) * kDegreesToRadians;
        const float pitch = g_Pitch[heliKey].load(std::memory_order_relaxed) * kDegreesToRadians;
        const float local[3] = { std::sin(yaw) * std::cos(pitch), -std::sin(pitch), std::cos(yaw) * std::cos(pitch) };
        __try
        {
            auto* context   = *reinterpret_cast<std::uint8_t**>(static_cast<std::uint8_t*>(light) + 0x48);
            auto* animation = context ? *reinterpret_cast<std::uint8_t**>(context + 8) : nullptr;   // the heli's animation controller
            if (!animation)
                return false;
            void** vtbl = *reinterpret_cast<void***>(animation);
            const float* model = reinterpret_cast<GetWorldMatrix_t>(vtbl[kVtbl_GetWorldMatrix / sizeof(void*)])(animation, slot);
            if (!model)
                return false;
            for (int i = 0; i < 3; ++i)   // columns 0-2 are the heli's axes, column 3 its position
                point[i] = model[12 + i] + kAngleAimDistance * (local[0] * model[i] + local[1] * model[4 + i] + local[2] * model[8 + i]);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool AimPoint(void* light, std::uint32_t slot, std::uint32_t heliKey, float point[3])
    {
        const std::uint32_t target = g_Target[heliKey].load(std::memory_order_relaxed);
        if (target != kNoTarget && g_CachedValid[heliKey].load(std::memory_order_acquire) && g_CachedTarget[heliKey].load(std::memory_order_relaxed) == target)
        {
            for (int i = 0; i < 3; ++i)
                point[i] = g_Cached[heliKey][i].load(std::memory_order_relaxed);
            return true;
        }
        if (g_HasAngle[heliKey].load(std::memory_order_relaxed))
        {
            if (AngleAimPoint(light, slot, heliKey, point))
                return true;
            if (!g_WarnedAngle[heliKey].exchange(true, std::memory_order_relaxed))
                LogWarn("SetSearchLightAngle: heli 0x%04X has no model matrix, the light keeps its own aim", HeliKey::GameObjectIdOf(heliKey));
        }
        return false;
    }

    void __fastcall hkExecute(void* light, std::uint32_t phase, std::uint32_t slot)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigExecute, light, phase, slot);

        std::uint32_t heliKey = 0;
        float point[3];
        if (phase != kExecutePhase || !ReadHeliKey(light, slot, heliKey) || !AimPoint(light, slot, heliKey, point))
        {
            g_OrigExecute(light, phase, slot);
            return;
        }

        t_Aim.active   = true;
        t_Aim.slot     = slot;
        t_Aim.point[0] = point[0];
        t_Aim.point[1] = point[1];
        t_Aim.point[2] = point[2];
        t_Aim.point[3] = 1.0f;
        g_OrigExecute(light, phase, slot);
        t_Aim.active = false;
    }

    bool __fastcall hkIsAimTypePosition(void* gun, std::uint32_t slot)
    {
        if (t_Aim.active && t_Aim.slot == slot)
            return true;
        return g_OrigIsAimTypePosition(gun, slot);
    }

    float* __fastcall hkGetAimPosition(void* gun, float* out, std::uint32_t slot)
    {
        if (t_Aim.active && t_Aim.slot == slot)
        {
            std::memcpy(out, t_Aim.point, sizeof(t_Aim.point));
            return out;
        }
        return g_OrigGetAimPosition(gun, out, slot);
    }
}

void HeliSearchLight_StartAutoAim(std::uint32_t heliKey, std::uint32_t targetId)
{
    if (heliKey >= HeliKey::kCount)
        return;
    g_CachedValid[heliKey].store(false, std::memory_order_relaxed);
    g_Target[heliKey].store(targetId, std::memory_order_relaxed);
}

void HeliSearchLight_StopAutoAim(std::uint32_t heliKey)
{
    if (heliKey >= HeliKey::kCount)
        return;
    g_Target[heliKey].store(kNoTarget, std::memory_order_relaxed);
    g_CachedValid[heliKey].store(false, std::memory_order_relaxed);
}

void HeliSearchLight_Pump()
{
    const std::uint64_t now = GetTickCount64();
    if (now - g_LastPumpMs.load(std::memory_order_relaxed) < kPumpIntervalMs)
        return;
    g_LastPumpMs.store(now, std::memory_order_relaxed);
    if (!g_Installed || MissionCodeGuard::ShouldBypassHooks())
        return;
    for (std::uint32_t i = 0; i < HeliKey::kCount; ++i)
    {
        const std::uint32_t target = g_Target[i].load(std::memory_order_relaxed);
        if (target == kNoTarget)
            continue;
        float position[3];
        const bool read = ((target >> 9) == kSoldierType && GameObjectInstance::SoldierPosition(target, position))
            || GameObjectPosition::Read(target, position);
        if (!read)
        {
            g_CachedValid[i].store(false, std::memory_order_relaxed);
            if (g_WarnedTarget[i].exchange(target, std::memory_order_relaxed) != target)
                LogWarn("StartSearchLightAutoAimToCharacter: position of 0x%04X can't be read, the light keeps its own aim", target);
            continue;
        }
        for (int k = 0; k < 3; ++k)
            g_Cached[i][k].store(position[k], std::memory_order_relaxed);
        g_CachedTarget[i].store(target, std::memory_order_relaxed);
        g_CachedValid[i].store(true, std::memory_order_release);
    }
}

void HeliSearchLight_SetAngle(std::uint32_t heliKey, float yawDegrees, float pitchDegrees)
{
    if (heliKey >= HeliKey::kCount)
        return;
    g_Yaw[heliKey].store(yawDegrees, std::memory_order_relaxed);
    g_Pitch[heliKey].store(pitchDegrees, std::memory_order_relaxed);
    g_HasAngle[heliKey].store(true, std::memory_order_relaxed);
}

void HeliSearchLight_ClearAngle(std::uint32_t heliKey)
{
    if (heliKey < HeliKey::kCount)
        g_HasAngle[heliKey].store(false, std::memory_order_relaxed);
}

void HeliSearchLight_Clear()
{
    for (std::uint32_t i = 0; i < HeliKey::kCount; ++i)
    {
        g_Target[i].store(kNoTarget, std::memory_order_relaxed);
        g_HasAngle[i].store(false, std::memory_order_relaxed);
        g_WarnedTarget[i].store(kNoTarget, std::memory_order_relaxed);
        g_WarnedAngle[i].store(false, std::memory_order_relaxed);
        g_CachedValid[i].store(false, std::memory_order_relaxed);
    }
}

bool Install_LightActionImpl_Execute_Hook()
{
    if (g_Installed)
        return true;

    HeliSearchLight_Clear();

    void* execute = gAddr.HeliLightActionImpl_Execute ? ResolveGameAddress(gAddr.HeliLightActionImpl_Execute) : nullptr;
    void* isAim   = gAddr.HeliGunActionImpl_IsAimTypePosition ? ResolveGameAddress(gAddr.HeliGunActionImpl_IsAimTypePosition) : nullptr;
    void* getAim  = gAddr.HeliGunActionImpl_GetAimPosition ? ResolveGameAddress(gAddr.HeliGunActionImpl_GetAimPosition) : nullptr;
    if (!execute || !isAim || !getAim)
    {
        LogError("LightActionImpl_Execute: address missing for this build, heli search light aim commands off");
        return false;
    }

    if (!CreateAndEnableHook(isAim, reinterpret_cast<void*>(&hkIsAimTypePosition), reinterpret_cast<void**>(&g_OrigIsAimTypePosition)))
    {
        LogError("LightActionImpl_Execute: gun aim type hook failed, heli search light aim commands off");
        return false;
    }
    if (!CreateAndEnableHook(getAim, reinterpret_cast<void*>(&hkGetAimPosition), reinterpret_cast<void**>(&g_OrigGetAimPosition)))
    {
        DisableAndRemoveHook(isAim);
        LogError("LightActionImpl_Execute: gun aim position hook failed, heli search light aim commands off");
        return false;
    }
    if (!CreateAndEnableHook(execute, reinterpret_cast<void*>(&hkExecute), reinterpret_cast<void**>(&g_OrigExecute)))
    {
        DisableAndRemoveHook(getAim);
        DisableAndRemoveHook(isAim);
        LogError("LightActionImpl_Execute: hook failed, heli search light aim commands off");
        return false;
    }

    g_Installed = true;
    return true;
}

bool Uninstall_LightActionImpl_Execute_Hook()
{
    if (!g_Installed)
        return true;

    DisableAndRemoveHook(ResolveGameAddress(gAddr.HeliLightActionImpl_Execute));
    DisableAndRemoveHook(ResolveGameAddress(gAddr.HeliGunActionImpl_GetAimPosition));
    DisableAndRemoveHook(ResolveGameAddress(gAddr.HeliGunActionImpl_IsAimTypePosition));
    g_OrigExecute           = nullptr;
    g_OrigIsAimTypePosition = nullptr;
    g_OrigGetAimPosition    = nullptr;
    g_Installed             = false;
    return true;
}
