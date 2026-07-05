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
            Log("[Barrier] ERROR: VirtualProtect failed (err=%lu) writing the shield-effect load patch; the dome may be invisible.\n", GetLastError());
            return false;
        }
        std::memcpy(target, src, size);
        DWORD restored = 0;
        VirtualProtect(target, size, oldProtect, &restored);
        FlushInstructionCache(GetCurrentProcess(), target, size);
        return true;
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
            Log("[Barrier] WARNING: shield-effect load gate '%s' has no address for %s; that effect may not show on this build.\n",
                sites[i].name, GetGameBuildName(gGameBuild));
            continue;
        }
        std::uint8_t* jz = reinterpret_cast<std::uint8_t*>(sites[i].gate) + 2;
        if (jz[0] != 0x74)
        {
            Log("[Barrier] ERROR: shield-effect load gate '%s' @ 0x%llX+2 is not a JZ (found 0x%02X); wrong address for %s -- skipped to avoid corrupting code.\n",
                sites[i].name, static_cast<unsigned long long>(sites[i].gate), jz[0], GetGameBuildName(gGameBuild));
            continue;
        }
        g_Original[i][0] = jz[0];
        g_Original[i][1] = jz[1];
        g_PatchAddr[i]   = jz;
        if (WriteBytes(jz, kNop, 2))
            any = true;
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
    g_Applied = false;
}
