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


    // tpp::gm::sahelan::impl::ActionCoreImpl::UpdateEyeLampColor (this, int slot).
    // Per-frame updater — reads color from *(this+0x48)+slot*0x60+0x20 and
    // publishes it to the renderer. This is the rail that paints the lamp.
    using UpdateEyeLampColor_t = void(__fastcall*)(void* self,
                                                   std::int32_t slot);

    // tpp::gm::sahelan::impl::PhaseSneakAiImpl FUN_14b84dec0 (this, slot, mode).
    // AI state-transition pump. We hook it solely to capture `mode` so the
    // per-frame updater knows which override preset to apply.
    using PushEyeColor_t = void(__fastcall*)(void* self,
                                             std::uint32_t slot,
                                             std::int32_t mode);


    static UpdateEyeLampColor_t g_OrigUpdateEyeLampColor = nullptr;
    static void*                g_UpdateHookTarget       = nullptr;
    static PushEyeColor_t       g_OrigPushEyeColor       = nullptr;
    static void*                g_PushHookTarget         = nullptr;


    static constexpr int kMaxModes = 16;
    static std::atomic<bool>  g_PerModeEnabled[kMaxModes]{};
    static std::atomic<float> g_PerModeR[kMaxModes]{};
    static std::atomic<float> g_PerModeG[kMaxModes]{};
    static std::atomic<float> g_PerModeB[kMaxModes]{};
    static std::atomic<float> g_PerModePulse[kMaxModes]{};   // Hz; 0 = steady
    static std::atomic<int>   g_LastMode{ -1 };

    static std::atomic<bool>  g_DiscoEnabled{ false };
    static std::atomic<float> g_DiscoSpeed{ 2.0f };          // hue cycles / sec

    static std::atomic<bool> g_LoggingEnabled{ false };


    static float NowSeconds()
    {
        const ULONGLONG ms = GetTickCount64();
        return static_cast<float>(ms % 1000000ULL) / 1000.0f;
    }


    // pulseSpeed semantics:
    //   1.0  -> steady (no pulse, full brightness)
    //   0.0  -> normal pulse rate (1 Hz — one full cycle per second)
    //   any other value lerps between (e.g. 0.5 = half-rate pulse).
    // Values outside [0, 1] are clamped.
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
        h -= std::floor(h);                          // wrap into [0, 1)
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
            default: r = v; g = p; b = q; break;     // case 5
        }
    }


    static void __fastcall hk_PushEyeColor(void* self,
                                           std::uint32_t slot,
                                           std::int32_t mode)
    {
        const int prev = g_LastMode.exchange(mode, std::memory_order_relaxed);

        // Log only on actual mode transitions, not every push.
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


    static void __fastcall hk_UpdateEyeLampColor(void* self, std::int32_t slot)
    {
        float outR = 0.0f, outG = 0.0f, outB = 0.0f;
        bool  haveColor = false;

        // Disco wins over everything — every lamp cycles the full rainbow
        // at full brightness (pulseSpeed=1 → steady).
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
    if (mode < 0 || mode >= kMaxModes) return;
    g_PerModeR[mode].store(r, std::memory_order_relaxed);
    g_PerModeG[mode].store(g, std::memory_order_relaxed);
    g_PerModeB[mode].store(b, std::memory_order_relaxed);
    g_PerModePulse[mode].store(pulseSpeed, std::memory_order_relaxed);
    g_PerModeEnabled[mode].store(true, std::memory_order_relaxed);

    // Setting a per-mode color implies the user wants that color, not the
    // rainbow — auto-disable disco.
    const bool wasDisco = g_DiscoEnabled.exchange(false, std::memory_order_relaxed);

    if (g_LoggingEnabled.load(std::memory_order_relaxed))
    {
        Log("[EyeLamp] SetEyeLampColor: mode=%d  R=%.3f  G=%.3f  B=%.3f  PULS=%.2f%s\n",
            mode, r, g, b, pulseSpeed,
            wasDisco ? "  (disco auto-disabled)" : "");
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


void Set_EyeLampDisco(bool enabled, float speed)
{
    if (speed < 0.0f) speed = 0.0f;
    g_DiscoSpeed.store(speed, std::memory_order_relaxed);
    g_DiscoEnabled.store(enabled, std::memory_order_relaxed);

    // Enabling disco implies the user wants the rainbow, not specific
    // per-mode colors — auto-clear those.
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
    for (int i = 0; i < kMaxModes; ++i)
        g_PerModeEnabled[i].store(false, std::memory_order_relaxed);
    g_LastMode.store(-1, std::memory_order_relaxed);
    g_DiscoEnabled.store(false, std::memory_order_relaxed);
    g_LoggingEnabled.store(false, std::memory_order_relaxed);
    return true;
}
