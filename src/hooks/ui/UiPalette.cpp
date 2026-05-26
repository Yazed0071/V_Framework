#include "pch.h"
#include "UiPalette.h"

#include <Windows.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "MinHook.h"
#include "log.h"
#include "../../core/AddressSet.h"
#include "../../core/HookUtils.h"

namespace
{
    struct EngineColor
    {
        float r;
        float g;
        float b;
        float a;
    };
    static_assert(sizeof(EngineColor) == 16, "EngineColor must be 16 bytes");

    struct OriginalColor
    {
        float r;
        float g;
        float b;
        float a;
    };

    static std::mutex                                       g_Mutex;
    static std::unordered_map<std::uint32_t, OriginalColor> g_Originals;
    static std::unordered_map<std::uint32_t, EngineColor>   g_Overrides;
    static bool                                             g_Installed = false;

    using GetForName_t = bool (__fastcall*)(void*, std::uint32_t, void*);
    static GetForName_t      g_OrigGetForName    = nullptr;
    static std::atomic<bool> g_HookInstalled{ false };

    enum class AnimMode { Blink, Pulse };
    struct Animation
    {
        AnimMode                 mode;
        std::vector<EngineColor> colors;
        double                   periodSec;
        long long                startTicks;
    };
    static std::mutex                                  g_AnimMutex;
    static std::unordered_map<std::uint32_t, Animation> g_Animations;
    static std::atomic<int>                            g_AnimationCount{ 0 };

    static long long PerfFrequency()
    {
        static long long freq = []
        {
            LARGE_INTEGER li{};
            QueryPerformanceFrequency(&li);
            return static_cast<long long>(li.QuadPart ? li.QuadPart : 1);
        }();
        return freq;
    }

    static long long PerfCounter()
    {
        LARGE_INTEGER li{};
        QueryPerformanceCounter(&li);
        return static_cast<long long>(li.QuadPart);
    }

    static EngineColor LerpColor(const EngineColor& a, const EngineColor& b, double t)
    {
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;
        const float tf = static_cast<float>(t);
        return EngineColor{
            a.r + (b.r - a.r) * tf,
            a.g + (b.g - a.g) * tf,
            a.b + (b.b - a.b) * tf,
            a.a + (b.a - a.a) * tf,
        };
    }

    static EngineColor ComputeAnimatedColor(const Animation& anim, long long now)
    {
        const std::size_t n = anim.colors.size();
        if (n == 0) return EngineColor{ 1.f, 1.f, 1.f, 1.f };
        if (n == 1 || anim.periodSec <= 0.0) return anim.colors[0];

        const double elapsed = static_cast<double>(now - anim.startTicks) /
                               static_cast<double>(PerfFrequency());
        double phase = elapsed / anim.periodSec;
        phase = phase - std::floor(phase);

        const double scaled = phase * static_cast<double>(n);
        std::size_t  i      = static_cast<std::size_t>(scaled);
        if (i >= n) i = n - 1;
        const std::size_t j = (i + 1) % n;
        const double      t = scaled - static_cast<double>(i);

        if (anim.mode == AnimMode::Blink)
            return anim.colors[i];

        const double s = 0.5 - 0.5 * std::cos(t * 3.14159265358979323846);
        return LerpColor(anim.colors[i], anim.colors[j], s);
    }

    static constexpr std::uint32_t  kMaxPaletteCount = 4096;
    static constexpr std::uint32_t  kMaxChainSteps   = 65536;
    static constexpr std::size_t    kMaxMatches      = 32;
    static constexpr std::uintptr_t kMinUserAddr     = 0x10000ull;
    static constexpr std::uintptr_t kMaxUserAddr     = 0x7FFFFFFFFFFFull;

    static bool IsPlausiblePtr(std::uintptr_t p)
    {
        if (p < kMinUserAddr) return false;
        if (p >= kMaxUserAddr) return false;
        return true;
    }

    static int SafeReadU32(const void* addr, std::uint32_t* out)
    {
        __try
        {
            *out = *reinterpret_cast<const volatile std::uint32_t*>(addr);
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static int SafeReadPtr(const void* addr, std::uintptr_t* out)
    {
        __try
        {
            *out = *reinterpret_cast<const volatile std::uintptr_t*>(addr);
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static int SafeReadColor(const void* addr, EngineColor* out)
    {
        __try
        {
            const EngineColor* src = reinterpret_cast<const EngineColor*>(addr);
            out->r = src->r;
            out->g = src->g;
            out->b = src->b;
            out->a = src->a;
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    struct PaletteWalkContext
    {
        EngineColor*  matches[kMaxMatches];
        std::uint32_t paletteIndexOfMatch[kMaxMatches];
        std::size_t   matchCount;
        std::uint32_t palettesScanned;
        std::uint32_t palettesWithKey;
    };

    static EngineColor* WalkOnePalette(std::uintptr_t palette, std::uint32_t keyHash)
    {
        std::uint32_t chainIdx = 0;
        if (!SafeReadU32(reinterpret_cast<const void*>(palette + 0x80), &chainIdx))
            return nullptr;

        std::uintptr_t hashArray = 0;
        if (!SafeReadPtr(reinterpret_cast<const void*>(palette + 0x98), &hashArray))
            return nullptr;
        if (!IsPlausiblePtr(hashArray)) return nullptr;

        std::uintptr_t colorArray = 0;
        if (!SafeReadPtr(reinterpret_cast<const void*>(palette + 0xA0), &colorArray))
            return nullptr;
        if (!IsPlausiblePtr(colorArray)) return nullptr;

        std::uint32_t steps = 0;
        while (chainIdx != 0xFFFFFFFFu && steps < kMaxChainSteps)
        {
            ++steps;

            const std::uintptr_t entry = hashArray + static_cast<std::size_t>(chainIdx) * 24u;

            std::uintptr_t colorData = 0;
            if (!SafeReadPtr(reinterpret_cast<const void*>(entry), &colorData))
                return nullptr;
            if (!IsPlausiblePtr(colorData)) return nullptr;

            std::uint32_t entryHash = 0;
            if (!SafeReadU32(reinterpret_cast<const void*>(colorData + 0x10), &entryHash))
                return nullptr;

            if (entryHash == keyHash)
            {
                return reinterpret_cast<EngineColor*>(
                    colorArray + static_cast<std::size_t>(chainIdx) * 16u);
            }

            std::uint32_t nextIdx = 0;
            if (!SafeReadU32(reinterpret_cast<const void*>(entry + 0x0C), &nextIdx))
                return nullptr;
            chainIdx = nextIdx;
        }

        return nullptr;
    }

    static bool CollectAllMatches_NoLock(std::uint32_t keyHash, PaletteWalkContext* ctx)
    {
        ctx->matchCount      = 0;
        ctx->palettesScanned = 0;
        ctx->palettesWithKey = 0;

        const std::uintptr_t gMgrAddr = gAddr.g_UiPaletteManager;
        if (!gMgrAddr) return false;

        std::uintptr_t mgr = 0;
        if (!SafeReadPtr(reinterpret_cast<const void*>(gMgrAddr), &mgr)
            || !IsPlausiblePtr(mgr))
            return false;

        std::uint32_t paletteCount = 0;
        if (!SafeReadU32(reinterpret_cast<const void*>(mgr + 0x08), &paletteCount))
            return false;
        if (paletteCount == 0 || paletteCount > kMaxPaletteCount) return false;

        std::uintptr_t paletteArray = 0;
        if (!SafeReadPtr(reinterpret_cast<const void*>(mgr + 0x10), &paletteArray)
            || !IsPlausiblePtr(paletteArray))
            return false;

        for (std::uint32_t i = 0; i < paletteCount; ++i)
        {
            std::uintptr_t entryPtr = 0;
            if (!SafeReadPtr(reinterpret_cast<const void*>(
                    paletteArray + static_cast<std::size_t>(i) * 8u), &entryPtr))
                continue;
            if (!IsPlausiblePtr(entryPtr)) continue;

            std::uintptr_t palette = 0;
            if (!SafeReadPtr(reinterpret_cast<const void*>(entryPtr + 0x18), &palette))
                continue;
            if (!IsPlausiblePtr(palette)) continue;

            ctx->palettesScanned++;

            EngineColor* hit = WalkOnePalette(palette, keyHash);
            if (hit)
            {
                ctx->palettesWithKey++;
                if (ctx->matchCount < kMaxMatches)
                {
                    ctx->matches[ctx->matchCount]              = hit;
                    ctx->paletteIndexOfMatch[ctx->matchCount]  = i;
                    ctx->matchCount++;
                }
            }
        }

        return ctx->matchCount > 0;
    }

    static EngineColor* FindFirstMatch_NoLock(std::uint32_t keyHash)
    {
        PaletteWalkContext ctx{};
        if (!CollectAllMatches_NoLock(keyHash, &ctx)) return nullptr;
        return ctx.matches[0];
    }

    static bool WritePaletteColor(EngineColor* entry, float r, float g, float b, float a)
    {
        if (!entry) return false;

        DWORD oldProtect = 0;
        if (!VirtualProtect(entry, sizeof(EngineColor), PAGE_READWRITE, &oldProtect))
        {
            Log("[UiPalette] VirtualProtect RW failed at %p\n", static_cast<void*>(entry));
            return false;
        }

        entry->r = r;
        entry->g = g;
        entry->b = b;
        entry->a = a;

        DWORD tmp = 0;
        VirtualProtect(entry, sizeof(EngineColor), oldProtect, &tmp);
        return true;
    }

    static int SafeWriteOutColor(void* outColor, const EngineColor& c)
    {
        __try
        {
            EngineColor* dst = reinterpret_cast<EngineColor*>(outColor);
            dst->r = c.r;
            dst->g = c.g;
            dst->b = c.b;
            dst->a = c.a;
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static std::mutex                          g_GetForNameSeenMutex;
    static std::unordered_map<std::uint32_t,int> g_GetForNameSeen;
    static std::atomic<int>                    g_GetForNameLogCount{ 0 };
    static constexpr int                       kGetForNameLogLimit = 100;

    static void LogGetForNameUnique(std::uint32_t hash, const EngineColor& c)
    {
        if (g_GetForNameLogCount.load(std::memory_order_relaxed) >= kGetForNameLogLimit)
            return;

        bool isNew = false;
        {
            std::lock_guard<std::mutex> lock(g_GetForNameSeenMutex);
            auto ins = g_GetForNameSeen.insert({hash, 1});
            isNew = ins.second;
            if (!isNew) ins.first->second++;
        }
        if (!isNew) return;

        const int n = g_GetForNameLogCount.fetch_add(1, std::memory_order_relaxed);
        if (n < kGetForNameLogLimit)
        {
            Log("[GetForName] hash=0x%08X color=(%.3f,%.3f,%.3f,%.3f)\n",
                hash, c.r, c.g, c.b, c.a);
        }
    }

    static bool __fastcall hk_GetForName(void* mgr, std::uint32_t hash, void* outColor)
    {
        if (!g_OrigGetForName)
            return false;

        if (g_AnimationCount.load(std::memory_order_relaxed) > 0)
        {
            EngineColor animColor{};
            bool hasAnim = false;
            {
                std::lock_guard<std::mutex> lock(g_AnimMutex);
                auto it = g_Animations.find(hash);
                if (it != g_Animations.end())
                {
                    animColor = ComputeAnimatedColor(it->second, PerfCounter());
                    hasAnim   = true;
                }
            }
            if (hasAnim)
            {
                if (IsPlausiblePtr(reinterpret_cast<std::uintptr_t>(outColor)))
                    SafeWriteOutColor(outColor, animColor);
                LogGetForNameUnique(hash, animColor);
                return true;
            }
        }

        EngineColor override{};
        bool hasOverride = false;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            auto it = g_Overrides.find(hash);
            if (it != g_Overrides.end())
            {
                override   = it->second;
                hasOverride = true;
            }
        }

        if (hasOverride)
        {
            if (IsPlausiblePtr(reinterpret_cast<std::uintptr_t>(outColor)))
                SafeWriteOutColor(outColor, override);
            LogGetForNameUnique(hash, override);
            return true;
        }

        const bool result = g_OrigGetForName(mgr, hash, outColor);
        if (result && outColor && IsPlausiblePtr(reinterpret_cast<std::uintptr_t>(outColor)))
        {
            EngineColor c{};
            if (SafeReadColor(outColor, &c))
                LogGetForNameUnique(hash, c);
        }
        return result;
    }

    static void InstallObserverHook_NoLock()
    {
        if (g_HookInstalled.load(std::memory_order_relaxed)) return;
        if (!gAddr.UiPaletteManager_GetForName) return;

        void* target = ResolveGameAddress(gAddr.UiPaletteManager_GetForName);
        if (!target) return;

        if (CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_GetForName),
                                reinterpret_cast<void**>(&g_OrigGetForName)))
        {
            g_HookInstalled.store(true, std::memory_order_relaxed);
            Log("[UiPalette] GetForName override hook installed @ %p\n", target);
        }
        else
        {
            Log("[UiPalette] GetForName hook install FAILED @ %p\n", target);
        }
    }

    static void UninstallObserverHook_NoLock()
    {
        if (!g_HookInstalled.load(std::memory_order_relaxed)) return;
        void* target = ResolveGameAddress(gAddr.UiPaletteManager_GetForName);
        if (target) DisableAndRemoveHook(target);
        g_OrigGetForName = nullptr;
        g_HookInstalled.store(false, std::memory_order_relaxed);
    }

    static std::atomic<int> g_OverrideCount{ 0 };

    using GetQuarkSystemTable_t = void* (*)();
    using HudVFn8_t  = void* (*)(void*);
    using HudVFn68_t = void  (*)(void*);

    static int SafeDerefPtr(void* base, std::uintptr_t off, void** out)
    {
        __try
        {
            *out = *reinterpret_cast<void* volatile*>(
                reinterpret_cast<std::uint8_t*>(base) + off);
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static int SafeAndChildFlags(void* children, int maxCount, std::uint16_t mask)
    {
        __try
        {
            void** arr = reinterpret_cast<void**>(children);
            for (int i = 0; i < maxCount; ++i)
            {
                void* child = arr[i];
                if (!child) return i;
                std::uint16_t* flagPtr = reinterpret_cast<std::uint16_t*>(
                    reinterpret_cast<std::uint8_t*>(child) + 0x38);
                *flagPtr = static_cast<std::uint16_t>(*flagPtr & mask);
            }
            return maxCount;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
    }

    static int SafeCallGetQst(GetQuarkSystemTable_t fn, void** out)
    {
        __try
        {
            *out = fn();
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static int SafeCallVFn8(HudVFn8_t fn, void* self, void** out)
    {
        __try
        {
            *out = fn(self);
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static int SafeCallVFn68(HudVFn68_t fn, void* self)
    {
        __try
        {
            fn(self);
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static std::atomic<bool> g_RebuildLogged{ false };

    static void RebuildHudWidgets()
    {
        if (!gAddr.GetQuarkSystemTable) return;

        auto getQst = reinterpret_cast<GetQuarkSystemTable_t>(
            ResolveGameAddress(gAddr.GetQuarkSystemTable));
        if (!getQst) return;

        void* qst = nullptr;
        if (!SafeCallGetQst(getQst, &qst) || !qst) return;
        if (!IsPlausiblePtr(reinterpret_cast<std::uintptr_t>(qst))) return;

        void* p1 = nullptr;
        if (!SafeDerefPtr(qst, 0x98, &p1) || !p1) return;
        if (!IsPlausiblePtr(reinterpret_cast<std::uintptr_t>(p1))) return;

        void* p2 = nullptr;
        if (!SafeDerefPtr(p1, 0x40, &p2) || !p2) return;
        if (!IsPlausiblePtr(reinterpret_cast<std::uintptr_t>(p2))) return;

        void* parent = nullptr;
        if (!SafeDerefPtr(p2, 0x50, &parent) || !parent) return;
        if (!IsPlausiblePtr(reinterpret_cast<std::uintptr_t>(parent))) return;

        void* vtable = nullptr;
        if (!SafeDerefPtr(parent, 0x0, &vtable) || !vtable) return;
        if (!IsPlausiblePtr(reinterpret_cast<std::uintptr_t>(vtable))) return;

        void* fn8 = nullptr;
        if (!SafeDerefPtr(vtable, 0x8, &fn8) || !fn8) return;
        void* fn68 = nullptr;
        if (!SafeDerefPtr(vtable, 0x68, &fn68) || !fn68) return;

        void* children = nullptr;
        if (!SafeCallVFn8(reinterpret_cast<HudVFn8_t>(fn8), parent, &children))
            return;

        if (children && IsPlausiblePtr(reinterpret_cast<std::uintptr_t>(children)))
        {
            const int cleared = SafeAndChildFlags(children, 200, 0xBFFFu);
            if (!g_RebuildLogged.exchange(true, std::memory_order_relaxed))
                Log("[UiPalette] RebuildHud: parent=%p children=%p cleared=%d\n",
                    parent, children, cleared);
        }
        else if (!g_RebuildLogged.exchange(true, std::memory_order_relaxed))
        {
            Log("[UiPalette] RebuildHud: parent=%p children=null (skip clear)\n", parent);
        }

        SafeCallVFn68(reinterpret_cast<HudVFn68_t>(fn68), parent);
    }

}

bool Install_UiPalette_Hook()
{
    g_Installed = true;
    Log("[UiPalette] install: manager-walk + GetForName override + HUD rebuild (mgr=0x%llX getfn=0x%llX)\n",
        static_cast<unsigned long long>(gAddr.g_UiPaletteManager),
        static_cast<unsigned long long>(gAddr.UiPaletteManager_GetForName));
    InstallObserverHook_NoLock();
    return true;
}

bool Uninstall_UiPalette_Hook()
{
    std::lock_guard<std::mutex> lock(g_Mutex);
    for (const auto& kv : g_Originals)
    {
        PaletteWalkContext ctx{};
        if (CollectAllMatches_NoLock(kv.first, &ctx))
        {
            for (std::size_t i = 0; i < ctx.matchCount; ++i)
                WritePaletteColor(ctx.matches[i],
                                  kv.second.r, kv.second.g, kv.second.b, kv.second.a);
        }
    }
    g_Originals.clear();
    g_Overrides.clear();
    UninstallObserverHook_NoLock();
    g_Installed = false;
    Log("[UiPalette] uninstall: originals restored, overrides cleared\n");
    return true;
}

namespace UiPalette
{
    bool SetColor(std::uint32_t keyHash, float r, float g, float b, float a)
    {
        std::size_t patched = 0;
        bool        wasNew  = false;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);

            PaletteWalkContext ctx{};
            const bool found = CollectAllMatches_NoLock(keyHash, &ctx) && ctx.matchCount > 0;

            if (found && g_Originals.find(keyHash) == g_Originals.end())
            {
                EngineColor snap{};
                if (SafeReadColor(ctx.matches[0], &snap))
                {
                    OriginalColor orig{ snap.r, snap.g, snap.b, snap.a };
                    g_Originals[keyHash] = orig;
                }
            }

            for (std::size_t i = 0; i < ctx.matchCount; ++i)
                WritePaletteColor(ctx.matches[i], r, g, b, a);

            wasNew                = g_Overrides.find(keyHash) == g_Overrides.end();
            g_Overrides[keyHash]  = EngineColor{ r, g, b, a };
            patched               = ctx.matchCount;
        }
        if (wasNew) g_OverrideCount.fetch_add(1, std::memory_order_relaxed);
        RebuildHudWidgets();

        Log("[UiPalette] SetColor 0x%08X -> (%.3f, %.3f, %.3f, %.3f) palette=%zu override=on\n",
            keyHash, r, g, b, a, patched);

        return true;
    }

    bool GetColor(std::uint32_t keyHash,
                  float* outR, float* outG, float* outB, float* outA)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);

        EngineColor* entry = FindFirstMatch_NoLock(keyHash);
        if (!entry) return false;

        EngineColor snap{};
        if (!SafeReadColor(entry, &snap)) return false;

        if (outR) *outR = snap.r;
        if (outG) *outG = snap.g;
        if (outB) *outB = snap.b;
        if (outA) *outA = snap.a;
        return true;
    }

    bool RestoreColor(std::uint32_t keyHash)
    {
        std::size_t patched     = 0;
        bool        any         = false;
        bool        removedOver = false;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);

            auto origIt = g_Originals.find(keyHash);
            const bool hasOriginal = origIt != g_Originals.end();
            const bool hasOverride = g_Overrides.find(keyHash) != g_Overrides.end();

            if (!hasOriginal && !hasOverride)
            {
                Log("[UiPalette] RestoreColor 0x%08X: nothing to restore\n", keyHash);
                return false;
            }

            if (hasOriginal)
            {
                PaletteWalkContext ctx{};
                if (CollectAllMatches_NoLock(keyHash, &ctx))
                {
                    for (std::size_t i = 0; i < ctx.matchCount; ++i)
                    {
                        if (WritePaletteColor(ctx.matches[i],
                                              origIt->second.r, origIt->second.g,
                                              origIt->second.b, origIt->second.a))
                            ++patched;
                    }
                }
                g_Originals.erase(origIt);
            }

            removedOver = g_Overrides.erase(keyHash) > 0;
            any         = true;
        }
        if (removedOver) g_OverrideCount.fetch_sub(1, std::memory_order_relaxed);
        RebuildHudWidgets();

        Log("[UiPalette] RestoreColor 0x%08X: restored palette=%zu override=cleared\n",
            keyHash, patched);

        return any;
    }

    void RestoreAll()
    {
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            for (const auto& kv : g_Originals)
            {
                PaletteWalkContext ctx{};
                if (CollectAllMatches_NoLock(kv.first, &ctx))
                {
                    for (std::size_t i = 0; i < ctx.matchCount; ++i)
                        WritePaletteColor(ctx.matches[i],
                                          kv.second.r, kv.second.g, kv.second.b, kv.second.a);
                }
            }
            g_Originals.clear();
            g_Overrides.clear();
        }
        g_OverrideCount.store(0, std::memory_order_relaxed);
        ClearAllAnimations();
        RebuildHudWidgets();
        Log("[UiPalette] RestoreAll: complete (originals + overrides + animations cleared)\n");
    }

    bool AnimateColor(std::uint32_t keyHash, const char* mode, double periodSec,
                      const float* rgba, int colorCount)
    {
        if (!rgba || colorCount < 1) return false;
        if (periodSec <= 0.0) periodSec = 1.0;

        AnimMode m = AnimMode::Blink;
        if (mode && std::strcmp(mode, "pulse") == 0) m = AnimMode::Pulse;

        Animation anim{};
        anim.mode       = m;
        anim.periodSec  = periodSec;
        anim.startTicks = PerfCounter();
        anim.colors.reserve(static_cast<std::size_t>(colorCount));
        for (int i = 0; i < colorCount; ++i)
        {
            anim.colors.push_back(EngineColor{
                rgba[i * 4 + 0], rgba[i * 4 + 1],
                rgba[i * 4 + 2], rgba[i * 4 + 3]
            });
        }

        bool wasNew = false;
        {
            std::lock_guard<std::mutex> lock(g_AnimMutex);
            wasNew = g_Animations.find(keyHash) == g_Animations.end();
            g_Animations[keyHash] = std::move(anim);
        }
        if (wasNew) g_AnimationCount.fetch_add(1, std::memory_order_relaxed);

        Log("[UiPalette] AnimateColor 0x%08X mode=%s period=%.3fs colors=%d\n",
            keyHash, m == AnimMode::Pulse ? "pulse" : "blink", periodSec, colorCount);
        return true;
    }

    bool ClearAnimation(std::uint32_t keyHash)
    {
        bool removed = false;
        {
            std::lock_guard<std::mutex> lock(g_AnimMutex);
            removed = g_Animations.erase(keyHash) > 0;
        }
        if (removed)
        {
            g_AnimationCount.fetch_sub(1, std::memory_order_relaxed);
            Log("[UiPalette] ClearAnimation 0x%08X\n", keyHash);
        }
        return removed;
    }

    void ClearAllAnimations()
    {
        std::size_t n = 0;
        {
            std::lock_guard<std::mutex> lock(g_AnimMutex);
            n = g_Animations.size();
            g_Animations.clear();
        }
        if (n > 0)
        {
            g_AnimationCount.store(0, std::memory_order_relaxed);
            Log("[UiPalette] ClearAllAnimations: %zu cleared\n", n);
        }
    }
}
