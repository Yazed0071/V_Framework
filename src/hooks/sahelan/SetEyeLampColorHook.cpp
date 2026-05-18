#include "pch.h"

#include <Windows.h>
#include <atomic>
#include <cmath>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "SetEyeLampColorHook.h"

namespace
{
    using UpdateEyeLampColor_t = void(__fastcall*)(void* self,
                                                   std::int32_t slot);

    using PushEyeColor_t = void(__fastcall*)(void* self,
                                             std::uint32_t slot,
                                             std::int32_t mode);

    using UpdateHeartLight_t = void(__fastcall*)(void* self,
                                                 std::uint32_t slot);

    static UpdateEyeLampColor_t g_OrigUpdateEyeLampColor = nullptr;
    static void*                g_UpdateHookTarget       = nullptr;
    static PushEyeColor_t       g_OrigPushEyeColor       = nullptr;
    static void*                g_PushHookTarget         = nullptr;
    static UpdateHeartLight_t   g_OrigUpdateHeartLight   = nullptr;
    static void*                g_HeartHookTarget        = nullptr;

    static constexpr int kMaxModes = 16;
    static std::atomic<bool>  g_PerModeEnabled[kMaxModes]{};
    static std::atomic<float> g_PerModeR[kMaxModes]{};
    static std::atomic<float> g_PerModeG[kMaxModes]{};
    static std::atomic<float> g_PerModeB[kMaxModes]{};
    static std::atomic<float> g_PerModePulse[kMaxModes]{};
    static std::atomic<int>   g_LastMode{ -1 };

    static std::atomic<bool>  g_DiscoEnabled{ false };
    static std::atomic<float> g_DiscoSpeed{ 2.0f };

    static std::atomic<bool>  g_HeartEnabled{ false };
    static std::atomic<float> g_HeartR{ 1.0f };
    static std::atomic<float> g_HeartG{ 1.0f };
    static std::atomic<float> g_HeartB{ 1.0f };
    static std::atomic<float> g_HeartPulse{ 1.0f };

    static std::atomic<bool> g_LoggingEnabled{ false };

    static float NowSeconds()
    {
        const ULONGLONG ms = GetTickCount64();
        return static_cast<float>(ms % 1000000ULL) / 1000.0f;
    }


    static float ComputePulseMultiplier(float pulseSpeed)
    {
        if (pulseSpeed >= 1.0f) return 1.0f;
        if (pulseSpeed < 0.0f)  pulseSpeed = 0.0f;
        constexpr float kDefaultHz = 1.0f;
        constexpr float kTwoPi     = 6.2831853f;
        const float hz = (1.0f - pulseSpeed) * kDefaultHz;
        if (hz <= 0.0f) return 1.0f;
        return 0.5f + 0.5f * std::sin(NowSeconds() * hz * kTwoPi);
    }


    static void HsvToRgb(float h, float s, float v, float& r, float& g, float& b)
    {
        h -= std::floor(h);
        const int   i = static_cast<int>(h * 6.0f);
        const float f = h * 6.0f - static_cast<float>(i);
        const float p = v * (1.0f - s);
        const float q = v * (1.0f - f * s);
        const float t = v * (1.0f - (1.0f - f) * s);
        switch (i % 6)
        {
            case 0:  r = v; g = t; b = p; break;
            case 1:  r = q; g = v; b = p; break;
            case 2:  r = p; g = v; b = t; break;
            case 3:  r = p; g = q; b = v; break;
            case 4:  r = t; g = p; b = v; break;
            default: r = v; g = p; b = q; break;
        }
    }


    static void __fastcall hk_PushEyeColor(void* self,
                                           std::uint32_t slot,
                                           std::int32_t mode)
    {
        const int prev = g_LastMode.exchange(mode, std::memory_order_relaxed);

        if (prev != mode && g_LoggingEnabled.load(std::memory_order_relaxed))
        {
            if (g_DiscoEnabled.load(std::memory_order_relaxed))
            {
                Log("[EyeLamp] engine mode=%d (disco active — rainbow @ %.2f Hz)\n",
                    mode, g_DiscoSpeed.load(std::memory_order_relaxed));
            }
            else if (mode >= 0 && mode < kMaxModes)
            {
                const bool active =
                    g_PerModeEnabled[mode].load(std::memory_order_relaxed);
                Log("[EyeLamp] engine mode=%d  R=%.3f  G=%.3f  B=%.3f  PULS=%.2f%s\n",
                    mode,
                    g_PerModeR[mode].load(std::memory_order_relaxed),
                    g_PerModeG[mode].load(std::memory_order_relaxed),
                    g_PerModeB[mode].load(std::memory_order_relaxed),
                    g_PerModePulse[mode].load(std::memory_order_relaxed),
                    active ? "" : "  (no override — engine color)");
            }
            else
            {
                Log("[EyeLamp] engine mode=%d (out of range — no override)\n", mode);
            }
        }
        (void)self; (void)slot;

        if (g_OrigPushEyeColor)
        {
            __try { g_OrigPushEyeColor(self, slot, mode); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
    }


    using EffectBuilderPush_t = void(__fastcall*)(void*           self,
                                                  std::uint32_t   idx,
                                                  std::int32_t    slot,
                                                  std::uint64_t   hash,
                                                  float*          color);


    static void __fastcall hk_UpdateHeartLight(void* self, std::uint32_t slot)
    {
        if (g_HeartEnabled.load(std::memory_order_relaxed) && self)
        {
            const float r0    = g_HeartR.load(std::memory_order_relaxed);
            const float g0    = g_HeartG.load(std::memory_order_relaxed);
            const float b0    = g_HeartB.load(std::memory_order_relaxed);
            const float ps    = g_HeartPulse.load(std::memory_order_relaxed);
            const float pulse = ComputePulseMultiplier(ps);

            __try
            {
                auto base = *reinterpret_cast<std::uint8_t**>(
                    reinterpret_cast<std::uintptr_t>(self) + 0x48);
                const std::int32_t baseSlot = *reinterpret_cast<std::int32_t*>(
                    reinterpret_cast<std::uintptr_t>(self) + 0x58);
                if (!base) return;

                const std::int64_t offset =
                    static_cast<std::int64_t>(
                        static_cast<std::uint32_t>(slot - baseSlot)) * 0x60 + 0x10;
                auto color = reinterpret_cast<float*>(base + offset);
                color[0] = r0 * pulse;
                color[1] = g0 * pulse;
                color[2] = b0 * pulse;
                color[3] = 1.0f;

                auto outer = *reinterpret_cast<void**>(
                    reinterpret_cast<std::uintptr_t>(self) + 0x98);
                if (!outer) return;
                auto plVar2 = *reinterpret_cast<void**>(
                    reinterpret_cast<std::uintptr_t>(outer) + 0x18);
                if (!plVar2) return;
                auto vt = *reinterpret_cast<void***>(plVar2);
                if (!vt) return;
                auto push = reinterpret_cast<EffectBuilderPush_t>(
                    vt[0xB0 / sizeof(void*)]);
                if (!push) return;
                push(plVar2, slot, 0x14, 0xCFF50B635575ULL, color);
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
            return;
        }

        if (g_OrigUpdateHeartLight)
        {
            __try { g_OrigUpdateHeartLight(self, slot); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
    }


    static void __fastcall hk_UpdateEyeLampColor(void* self, std::int32_t slot)
    {
        float outR = 0.0f, outG = 0.0f, outB = 0.0f;
        bool  haveColor = false;

        if (g_DiscoEnabled.load(std::memory_order_relaxed))
        {
            const float speed = g_DiscoSpeed.load(std::memory_order_relaxed);
            const float hue   = NowSeconds() * speed;
            HsvToRgb(hue, 1.0f, 1.0f, outR, outG, outB);
            haveColor = true;
        }
        else
        {
            const int mode = g_LastMode.load(std::memory_order_relaxed);
            const bool active = (mode >= 0 && mode < kMaxModes) &&
                g_PerModeEnabled[mode].load(std::memory_order_relaxed);
            if (active)
            {
                const float r0 = g_PerModeR[mode].load(std::memory_order_relaxed);
                const float g0 = g_PerModeG[mode].load(std::memory_order_relaxed);
                const float b0 = g_PerModeB[mode].load(std::memory_order_relaxed);
                const float ps = g_PerModePulse[mode].load(std::memory_order_relaxed);
                const float pulse = ComputePulseMultiplier(ps);
                outR = r0 * pulse;
                outG = g0 * pulse;
                outB = b0 * pulse;
                haveColor = true;
            }
        }

        if (haveColor && self)
        {
            __try
            {
                auto base = *reinterpret_cast<std::uint8_t**>(
                    reinterpret_cast<std::uintptr_t>(self) + 0x48);
                const std::int32_t baseSlot = *reinterpret_cast<std::int32_t*>(
                    reinterpret_cast<std::uintptr_t>(self) + 0x58);
                if (base)
                {
                    const std::int64_t offset =
                        static_cast<std::int64_t>(
                            static_cast<std::uint32_t>(slot - baseSlot)) * 0x60 + 0x20;
                    auto color = reinterpret_cast<float*>(base + offset);
                    color[0] = outR;
                    color[1] = outG;
                    color[2] = outB;
                    color[3] = 0.0f;
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }

        if (g_OrigUpdateEyeLampColor)
        {
            __try { g_OrigUpdateEyeLampColor(self, slot); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
    }
}

void Set_EyeLampColor(int mode, float r, float g, float b, float pulseSpeed)
{
    const bool allModes = (mode < 0);
    if (!allModes && mode >= kMaxModes) return;

    const int start = allModes ? 0           : mode;
    const int end   = allModes ? kMaxModes   : (mode + 1);
    for (int m = start; m < end; ++m)
    {
        g_PerModeR[m].store(r, std::memory_order_relaxed);
        g_PerModeG[m].store(g, std::memory_order_relaxed);
        g_PerModeB[m].store(b, std::memory_order_relaxed);
        g_PerModePulse[m].store(pulseSpeed, std::memory_order_relaxed);
        g_PerModeEnabled[m].store(true, std::memory_order_relaxed);
    }

    const bool wasDisco = g_DiscoEnabled.exchange(false, std::memory_order_relaxed);

    if (g_LoggingEnabled.load(std::memory_order_relaxed))
    {
        if (allModes)
        {
            Log("[EyeLamp] SetEyeLampColor: mode=ALL  R=%.3f  G=%.3f  B=%.3f  PULS=%.2f%s\n",
                r, g, b, pulseSpeed,
                wasDisco ? "  (disco auto-disabled)" : "");
        }
        else
        {
            Log("[EyeLamp] SetEyeLampColor: mode=%d  R=%.3f  G=%.3f  B=%.3f  PULS=%.2f%s\n",
                mode, r, g, b, pulseSpeed,
                wasDisco ? "  (disco auto-disabled)" : "");
        }
    }
}


void Clear_EyeLampColor()
{
    for (int i = 0; i < kMaxModes; ++i)
        g_PerModeEnabled[i].store(false, std::memory_order_relaxed);
    const bool wasDisco = g_DiscoEnabled.exchange(false, std::memory_order_relaxed);

    if (g_LoggingEnabled.load(std::memory_order_relaxed))
    {
        Log("[EyeLamp] ClearEyeLampColor: cleared all overrides%s\n",
            wasDisco ? " (incl. disco)" : "");
    }
}


void Set_HeartLightColor(float r, float g, float b, float pulseSpeed)
{
    g_HeartR.store(r, std::memory_order_relaxed);
    g_HeartG.store(g, std::memory_order_relaxed);
    g_HeartB.store(b, std::memory_order_relaxed);
    g_HeartPulse.store(pulseSpeed, std::memory_order_relaxed);
    g_HeartEnabled.store(true, std::memory_order_relaxed);

    if (g_LoggingEnabled.load(std::memory_order_relaxed))
    {
        Log("[EyeLamp] SetHeartLightColor:  R=%.3f  G=%.3f  B=%.3f  PULS=%.2f\n",
            r, g, b, pulseSpeed);
    }
}


void Clear_HeartLightColor()
{
    g_HeartEnabled.store(false, std::memory_order_relaxed);

    if (g_LoggingEnabled.load(std::memory_order_relaxed))
    {
        Log("[EyeLamp] ClearHeartLightColor: HP-driven heart color resumed\n");
    }
}


void Set_EyeLampDisco(bool enabled, float speed)
{
    if (speed < 0.0f) speed = 0.0f;
    g_DiscoSpeed.store(speed, std::memory_order_relaxed);
    g_DiscoEnabled.store(enabled, std::memory_order_relaxed);

    bool clearedPerMode = false;
    if (enabled)
    {
        for (int i = 0; i < kMaxModes; ++i)
        {
            if (g_PerModeEnabled[i].exchange(false, std::memory_order_relaxed))
                clearedPerMode = true;
        }
    }

    if (g_LoggingEnabled.load(std::memory_order_relaxed))
    {
        Log("[EyeLamp] Disco %s (speed=%.2f hue cycles/sec)%s\n",
            enabled ? "ENABLED" : "DISABLED", speed,
            clearedPerMode ? " (per-mode overrides cleared)" : "");
    }
}


void Set_EyeLampColorLogging(bool enabled)
{
    g_LoggingEnabled.store(enabled, std::memory_order_relaxed);
}


bool Is_EyeLampColorLogging()
{
    return g_LoggingEnabled.load(std::memory_order_relaxed);
}


bool Install_SetEyeLampColor_Hook()
{
    if (gAddr.Sahelan_ActionCoreImpl_UpdateEyeLampColor)
    {
        if (void* t = ResolveGameAddress(gAddr.Sahelan_ActionCoreImpl_UpdateEyeLampColor))
        {
            if (CreateAndEnableHook(t,
                    reinterpret_cast<void*>(&hk_UpdateEyeLampColor),
                    reinterpret_cast<void**>(&g_OrigUpdateEyeLampColor)))
            {
                g_UpdateHookTarget = t;
            }
        }
    }

    if (gAddr.Sahelan_PhaseSneakAi_PushEyeColor)
    {
        if (void* t = ResolveGameAddress(gAddr.Sahelan_PhaseSneakAi_PushEyeColor))
        {
            if (CreateAndEnableHook(t,
                    reinterpret_cast<void*>(&hk_PushEyeColor),
                    reinterpret_cast<void**>(&g_OrigPushEyeColor)))
            {
                g_PushHookTarget = t;
            }
        }
    }

    if (gAddr.Sahelan_ActionCoreImpl_UpdateHeartLight)
    {
        if (void* t = ResolveGameAddress(gAddr.Sahelan_ActionCoreImpl_UpdateHeartLight))
        {
            if (CreateAndEnableHook(t,
                    reinterpret_cast<void*>(&hk_UpdateHeartLight),
                    reinterpret_cast<void**>(&g_OrigUpdateHeartLight)))
            {
                g_HeartHookTarget = t;
            }
        }
    }

    return g_UpdateHookTarget != nullptr;
}


bool Uninstall_SetEyeLampColor_Hook()
{
    if (g_UpdateHookTarget)
    {
        DisableAndRemoveHook(g_UpdateHookTarget);
        g_UpdateHookTarget = nullptr;
        g_OrigUpdateEyeLampColor = nullptr;
    }
    if (g_PushHookTarget)
    {
        DisableAndRemoveHook(g_PushHookTarget);
        g_PushHookTarget = nullptr;
        g_OrigPushEyeColor = nullptr;
    }
    if (g_HeartHookTarget)
    {
        DisableAndRemoveHook(g_HeartHookTarget);
        g_HeartHookTarget = nullptr;
        g_OrigUpdateHeartLight = nullptr;
    }
    for (int i = 0; i < kMaxModes; ++i)
        g_PerModeEnabled[i].store(false, std::memory_order_relaxed);
    g_LastMode.store(-1, std::memory_order_relaxed);
    g_DiscoEnabled.store(false, std::memory_order_relaxed);
    g_HeartEnabled.store(false, std::memory_order_relaxed);
    g_LoggingEnabled.store(false, std::memory_order_relaxed);
    return true;
}
