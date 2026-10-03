#include "pch.h"
#include "BarrierEffectLoadPatch.h"

#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "log.h"

namespace
{
    constexpr std::size_t kNumSites = 3;
    static void*        g_PatchAddr[kNumSites] = {};
    static std::uint8_t g_Original[kNumSites][2] = {};
    static bool         g_Applied = false;

    static bool WriteBytes(void* target, const std::uint8_t* src, std::size_t size)
    {
        DWORD oldProtect = 0;
        if (!VirtualProtect(target, size, PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            LogError("Barrier: VirtualProtect failed (%lu), shield effect patch not written", GetLastError());
            return false;
        }
        std::memcpy(target, src, size);
        DWORD restored = 0;
        VirtualProtect(target, size, oldProtect, &restored);
        FlushInstructionCache(GetCurrentProcess(), target, size);
        return true;
    }

    constexpr std::size_t kNumAllocGates = 2;
    static std::uint8_t g_AllocGateOrig[kNumAllocGates][6] = {};
    static void*        g_AllocGateAddr[kNumAllocGates]    = {};

    struct AllocGateSite { uintptr_t addr; const char* name; };
    static void BarrierAllocGateSites(AllocGateSite (&out)[kNumAllocGates])
    {
        switch (gGameBuild)
        {
            case ::AddressSetRuntime::GameBuild::En_1_0_15_4a:
            case ::AddressSetRuntime::GameBuild::En_1_0_15_4:
                out[0] = { 0x140FF04C1ull, "alloc" };
                out[1] = { 0x140FF1139ull, "populate" };
                return;
            case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4a:
            case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4:
                out[0] = { 0x140FF04E1ull, "alloc" };
                out[1] = { 0x140FF1159ull, "populate" };
                return;
            case ::AddressSetRuntime::GameBuild::En_1_0_15_3:
                out[0] = { 0x140FF0BF1ull, "alloc" };
                out[1] = { 0x140FF1869ull, "populate" };
                return;
            case ::AddressSetRuntime::GameBuild::Jp_1_0_15_3:
                out[0] = { 0x140FF0CB1ull, "alloc" };
                out[1] = { 0x140FF1929ull, "populate" };
                return;
            default:
                out[0] = { 0, "alloc" };
                out[1] = { 0, "populate" };
                return;
        }
    }

}

bool Install_BarrierEffectLoadPatch()
{
    if (g_Applied)
        return true;

    const struct { uintptr_t gate; const char* name; } sites[kNumSites] = {
        { gAddr.Barrier_LoadGate0, "slot12" },
        { gAddr.Barrier_LoadGate1, "itmsld01" },
        { gAddr.Barrier_LoadGate2, "itmsld02" },
    };
    static const std::uint8_t kNop[2] = { 0x90, 0x90 };

    bool any = false;
    for (std::size_t i = 0; i < kNumSites; ++i)
    {
        if (!sites[i].gate)
        {
            LogError("Barrier: load gate '%s' address missing on %s, that shield effect may not show",
                sites[i].name, GetGameBuildName(gGameBuild));
            continue;
        }
        std::uint8_t* jz = reinterpret_cast<std::uint8_t*>(sites[i].gate) + 2;
        if (jz[0] != 0x74)
        {
            LogError("Barrier: load gate '%s' code does not match %s, skipped",
                sites[i].name, GetGameBuildName(gGameBuild));
            continue;
        }
        g_Original[i][0] = jz[0];
        g_Original[i][1] = jz[1];
        g_PatchAddr[i]   = jz;
        if (WriteBytes(jz, kNop, 2))
            any = true;
    }

    AllocGateSite gates[kNumAllocGates];
    BarrierAllocGateSites(gates);
    static const std::uint8_t kNop6[6] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
    std::size_t patched = 0;
    for (std::size_t i = 0; i < kNumAllocGates; ++i)
    {
        if (!gates[i].addr)
        {
            LogError("Barrier: AllocateWork %s gate address missing on %s, real dome off",
                gates[i].name, GetGameBuildName(gGameBuild));
            break;
        }
        std::uint8_t* p = reinterpret_cast<std::uint8_t*>(gates[i].addr);
        if (p[0] != 0x0F || p[1] != 0x84)
        {
            LogError("Barrier: AllocateWork %s gate code does not match this build, real dome off",
                gates[i].name);
            break;
        }
        std::memcpy(g_AllocGateOrig[i], p, sizeof(g_AllocGateOrig[i]));
        if (!WriteBytes(p, kNop6, sizeof(kNop6)))
            break;
        g_AllocGateAddr[i] = p;
        ++patched;
    }
    if (patched == kNumAllocGates)
    {
        any = true;
    }
    else if (patched > 0)
    {
        for (std::size_t i = 0; i < kNumAllocGates; ++i)
        {
            if (g_AllocGateAddr[i])
            {
                WriteBytes(g_AllocGateAddr[i], g_AllocGateOrig[i], sizeof(g_AllocGateOrig[i]));
                g_AllocGateAddr[i] = nullptr;
            }
        }
        LogError("Barrier: only %zu/%zu AllocateWork gates patched, reverted, real dome off",
            patched, kNumAllocGates);
    }

    g_Applied = any;
    return any;
}

void Uninstall_BarrierEffectLoadPatch()
{
    if (!g_Applied)
        return;
    for (std::size_t i = 0; i < kNumSites; ++i)
        if (g_PatchAddr[i])
            WriteBytes(g_PatchAddr[i], g_Original[i], 2);
    for (std::size_t i = 0; i < kNumAllocGates; ++i)
    {
        if (g_AllocGateAddr[i])
        {
            WriteBytes(g_AllocGateAddr[i], g_AllocGateOrig[i], sizeof(g_AllocGateOrig[i]));
            g_AllocGateAddr[i] = nullptr;
        }
    }
    g_Applied = false;
}
