#include "pch.h"

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "CustomSuitRegistry.h"

static char __fastcall hkLoadMarkCheck2b8(void* self);
static unsigned char __fastcall hkLoadMarkValue2e8(void* self);

namespace
{
    using UpdateLoadMark_t = void(__fastcall*)(void* self);
    using LoadMarkCheck2b8_t = char(__fastcall*)(void* self);
    using LoadMarkValue2e8_t = unsigned char(__fastcall*)(void* self);

    static UpdateLoadMark_t g_OrigUpdateLoadMark = nullptr;
    static LoadMarkCheck2b8_t g_OrigLoadMarkCheck2b8 = nullptr;
    static LoadMarkValue2e8_t g_OrigLoadMarkValue2e8 = nullptr;

    static void* g_LoadMarkCheck2b8Target = nullptr;
    static void* g_LoadMarkValue2e8Target = nullptr;

    static bool g_UpdateLoadMarkInstalled = false;
    static bool g_LoadMarkCheckInstalled = false;
    static bool g_LoadMarkValueInstalled = false;

    static thread_local bool g_InUpdateLoadMark = false;
    static thread_local std::uint8_t g_CurrentLoadMarkBranch548 = 0xFF;

    template <typename Fn>
    static Fn GetVFunc(void* obj, std::size_t byteOffset)
    {
        auto** vtbl = *reinterpret_cast<void***>(obj);
        return reinterpret_cast<Fn>(vtbl[byteOffset / sizeof(void*)]);
    }

    static void TryInstallLoadMarkHooksFromSystem(void* sysObj)
    {
        if (!sysObj)
            return;

        auto** vtbl = *reinterpret_cast<void***>(sysObj);
        if (!vtbl)
            return;

        if (!g_LoadMarkCheckInstalled)
        {
            void* target2b8 = vtbl[0x2B8 / sizeof(void*)];
            if (target2b8)
            {
                const bool ok = CreateAndEnableHook(
                    target2b8,
                    reinterpret_cast<void*>(&hkLoadMarkCheck2b8),
                    reinterpret_cast<void**>(&g_OrigLoadMarkCheck2b8)
                );

                Log(
                    "[Hook] MissionPrepLoadMarkCheck2b8: %s target=%p orig=%p\n",
                    ok ? "OK" : "FAIL",
                    target2b8,
                    g_OrigLoadMarkCheck2b8
                );

                if (ok)
                {
                    g_LoadMarkCheck2b8Target = target2b8;
                    g_LoadMarkCheckInstalled = true;
                }
            }
        }

        if (!g_LoadMarkValueInstalled)
        {
            void* target2e8 = vtbl[0x2E8 / sizeof(void*)];
            if (target2e8)
            {
                const bool ok = CreateAndEnableHook(
                    target2e8,
                    reinterpret_cast<void*>(&hkLoadMarkValue2e8),
                    reinterpret_cast<void**>(&g_OrigLoadMarkValue2e8)
                );

                Log(
                    "[Hook] MissionPrepLoadMarkValue2e8: %s target=%p orig=%p\n",
                    ok ? "OK" : "FAIL",
                    target2e8,
                    g_OrigLoadMarkValue2e8
                );

                if (ok)
                {
                    g_LoadMarkValue2e8Target = target2e8;
                    g_LoadMarkValueInstalled = true;
                }
            }
        }
    }
}

static char __fastcall hkLoadMarkCheck2b8(void* self)
{
    const char original = g_OrigLoadMarkCheck2b8(self);

    if (g_InUpdateLoadMark)
    {
        ActiveCustomSuitState active{};
        const bool haveActive = TryGetActiveCustomSuit(active) && active.valid;

        if (haveActive)
        {
            // Throttled: UpdateLoadMark fires every frame while a custom suit
            // is active, so this runs ~60x/sec. Only log on (developId,
            // original) change so the commit transition is visible without
            // per-frame flooding.
            static std::uint32_t s_lastKey = 0xFFFFFFFFu;
            const std::uint32_t key =
                (static_cast<std::uint32_t>(active.developId) << 8) |
                static_cast<std::uint32_t>(static_cast<std::uint8_t>(original));
            if (key != s_lastKey)
            {
                s_lastKey = key;
                Log(
                    "[LoadMarkCheck2b8] override original=%d -> forced=1 developId=%u parts=0x%02X selector=0x%02X\n",
                    static_cast<int>(original),
                    static_cast<unsigned>(active.developId),
                    static_cast<unsigned>(active.partsType),
                    static_cast<unsigned>(active.selectorCode)
                );
            }
            return 1;
        }
    }

    return original;
}

static unsigned char __fastcall hkLoadMarkValue2e8(void* self)
{
    const unsigned char original = g_OrigLoadMarkValue2e8(self);

    if (g_InUpdateLoadMark && g_CurrentLoadMarkBranch548 == 1)
    {
        ActiveCustomSuitState active{};
        const bool haveActive = TryGetActiveCustomSuit(active) && active.valid;

        if (haveActive && original == 0xFF)
        {
            // Throttled: same per-frame concern as Check2b8 above.
            static std::uint32_t s_lastKey = 0xFFFFFFFFu;
            const std::uint32_t key =
                (static_cast<std::uint32_t>(active.developId) << 8) |
                static_cast<std::uint32_t>(original);
            if (key != s_lastKey)
            {
                s_lastKey = key;
                Log(
                    "[LoadMarkValue2e8] override original=0x%02X -> forced=0x00 developId=%u parts=0x%02X selector=0x%02X\n",
                    static_cast<unsigned>(original),
                    static_cast<unsigned>(active.developId),
                    static_cast<unsigned>(active.partsType),
                    static_cast<unsigned>(active.selectorCode)
                );
            }
            return 0x00;
        }
    }

    return original;
}

static void __fastcall hkUpdateLoadMark(void* self)
{
    auto* uiObj = self ? *reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(self) + 0x40) : nullptr;
    auto* sysObj = self ? *reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(self) + 0x48) : nullptr;

    if (sysObj)
        TryInstallLoadMarkHooksFromSystem(sysObj);

    std::uint8_t branch548 = 0xFF;
    std::uint8_t check2b8 = 0xFF;
    std::uint8_t check2c0 = 0xFF;
    std::uint8_t value2e8 = 0xFF;

    if (uiObj && sysObj)
    {
        auto get548 = GetVFunc<char(__fastcall*)(void*)>(uiObj, 0x548);
        auto get2b8 = GetVFunc<char(__fastcall*)(void*)>(sysObj, 0x2B8);
        auto get2c0 = GetVFunc<char(__fastcall*)(void*)>(sysObj, 0x2C0);
        auto get2e8 = GetVFunc<unsigned char(__fastcall*)(void*)>(sysObj, 0x2E8);

        branch548 = static_cast<std::uint8_t>(get548(uiObj) ? 1 : 0);
        check2b8 = static_cast<std::uint8_t>(get2b8(sysObj) ? 1 : 0);
        check2c0 = static_cast<std::uint8_t>(get2c0(sysObj) ? 1 : 0);
        value2e8 = get2e8(sysObj);
    }

    ActiveCustomSuitState active{};
    const bool haveActive = TryGetActiveCustomSuit(active) && active.valid;

    // Only log when a custom suit is active (avoid per-frame spam for vanilla suits)
    // AND only when the observed state tuple changes. UpdateLoadMark ticks
    // ~60x/sec; without this throttle the log is unreadable.
    if (haveActive)
    {
        static std::uint64_t s_lastKey = 0xFFFFFFFFFFFFFFFFull;
        const std::uint64_t key =
            (static_cast<std::uint64_t>(active.developId) << 32) |
            (static_cast<std::uint64_t>(branch548)         << 24) |
            (static_cast<std::uint64_t>(check2b8)          << 16) |
            (static_cast<std::uint64_t>(check2c0)          << 8)  |
            (static_cast<std::uint64_t>(value2e8));
        if (key != s_lastKey)
        {
            s_lastKey = key;
            Log(
                "[UpdateLoadMarkFix] custom=1 developId=%u parts=0x%02X selector=0x%02X branch548=%u check2b8=%u check2c0=%u value2e8=0x%02X\n",
                static_cast<unsigned>(active.developId),
                static_cast<unsigned>(active.partsType),
                static_cast<unsigned>(active.selectorCode),
                static_cast<unsigned>(branch548),
                static_cast<unsigned>(check2b8),
                static_cast<unsigned>(check2c0),
                static_cast<unsigned>(value2e8)
            );
        }
    }

    g_CurrentLoadMarkBranch548 = branch548;
    g_InUpdateLoadMark = true;
    g_OrigUpdateLoadMark(self);
    g_InUpdateLoadMark = false;
    g_CurrentLoadMarkBranch548 = 0xFF;
}

bool Install_MissionPrepUpdateLoadMark_Hook()
{
    if (g_UpdateLoadMarkInstalled)
    {
        Log("[Hook] MissionPrepUpdateLoadMark: already installed\n");
        return true;
    }

    void* target = ResolveGameAddress(gAddr.MissionPrep_UpdateLoadMark);
    if (!target)
    {
        Log("[Hook] MissionPrepUpdateLoadMark: failed to resolve target\n");
        return false;
    }

    const bool ok = CreateAndEnableHook(
        target,
        reinterpret_cast<void*>(&hkUpdateLoadMark),
        reinterpret_cast<void**>(&g_OrigUpdateLoadMark)
    );

    Log("[Hook] MissionPrepUpdateLoadMark: %s\n", ok ? "OK" : "FAIL");

    if (ok)
        g_UpdateLoadMarkInstalled = true;

    return ok;
}

bool Uninstall_MissionPrepUpdateLoadMark_Hook()
{
    if (g_LoadMarkCheckInstalled && g_LoadMarkCheck2b8Target)
    {
        DisableAndRemoveHook(g_LoadMarkCheck2b8Target);
        g_LoadMarkCheck2b8Target = nullptr;
        g_OrigLoadMarkCheck2b8 = nullptr;
        g_LoadMarkCheckInstalled = false;
    }

    if (g_LoadMarkValueInstalled && g_LoadMarkValue2e8Target)
    {
        DisableAndRemoveHook(g_LoadMarkValue2e8Target);
        g_LoadMarkValue2e8Target = nullptr;
        g_OrigLoadMarkValue2e8 = nullptr;
        g_LoadMarkValueInstalled = false;
    }

    if (g_UpdateLoadMarkInstalled)
    {
        if (void* target = ResolveGameAddress(gAddr.MissionPrep_UpdateLoadMark))
            DisableAndRemoveHook(target);

        g_OrigUpdateLoadMark = nullptr;
        g_UpdateLoadMarkInstalled = false;
    }

    Log("[Hook] MissionPrepUpdateLoadMark: removed\n");
    return true;
}