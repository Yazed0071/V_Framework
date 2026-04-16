#include "pch.h"

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "CustomSuitRegistry.h"

namespace
{
    // Most likely shape:
    // int __fastcall Func(void* self, uint32_t equipKind)
    using GetCurrentEquippedSuitFlowIndex_t =
        int(__fastcall*)(void* self, std::uint32_t equipKind);

    static GetCurrentEquippedSuitFlowIndex_t g_OrigGetCurrentEquippedSuitFlowIndex = nullptr;
    static bool g_Installed = false;

    static bool TryGetActiveCustomSuitFlowIndex(std::uint32_t& outFlowIndex)
    {
        outFlowIndex = 0;

        ActiveCustomSuitState active{};
        if (!TryGetActiveCustomSuit(active) || !active.valid)
            return false;

        const CustomSuitEntry* entry = nullptr;
        if (!TryGetCustomSuitByPartsType(active.partsType, &entry) || !entry)
            return false;

        if (entry->linkedFlowIndex == 0)
            return false;

        outFlowIndex = entry->linkedFlowIndex;
        return true;
    }
}

static int __fastcall hkGetCurrentEquippedSuitFlowIndex(
    void* self,
    std::uint32_t equipKind)
{
    const int original =
        g_OrigGetCurrentEquippedSuitFlowIndex(self, equipKind);

    // Only touch suit rows.
    if (equipKind != 0x80)
        return original;

    std::uint32_t customFlowIndex = 0;
    if (!TryGetActiveCustomSuitFlowIndex(customFlowIndex))
        return original;

    Log(
        "[CurrentSuitFlowIndex] override equipKind=0x%X original=%d -> customFlowIndex=%u\n",
        static_cast<unsigned>(equipKind),
        original,
        static_cast<unsigned>(customFlowIndex)
    );

    return static_cast<int>(customFlowIndex);
}

bool Install_MissionPreparationCurrentSuitFlowIndex_Hook()
{
    if (g_Installed)
    {
        Log("[Hook] MissionPreparationCurrentSuitFlowIndex: already installed\n");
        return true;
    }

    void* target =
        ResolveGameAddress(gAddr.MissionPrep_GetCurrentEquippedSuitFlowIndex);

    if (!target)
    {
        Log("[Hook] MissionPreparationCurrentSuitFlowIndex: failed to resolve target\n");
        return false;
    }

    const bool ok = CreateAndEnableHook(
        target,
        reinterpret_cast<void*>(&hkGetCurrentEquippedSuitFlowIndex),
        reinterpret_cast<void**>(&g_OrigGetCurrentEquippedSuitFlowIndex)
    );

    Log(
        "[Hook] MissionPreparationCurrentSuitFlowIndex: %s target=%p orig=%p\n",
        ok ? "OK" : "FAIL",
        target,
        g_OrigGetCurrentEquippedSuitFlowIndex
    );

    g_Installed = ok;
    return ok;
}

bool Uninstall_MissionPreparationCurrentSuitFlowIndex_Hook()
{
    if (!g_Installed)
        return true;

    if (void* target =
        ResolveGameAddress(gAddr.MissionPrep_GetCurrentEquippedSuitFlowIndex))
    {
        DisableAndRemoveHook(target);
    }

    g_OrigGetCurrentEquippedSuitFlowIndex = nullptr;
    g_Installed = false;

    Log("[Hook] MissionPreparationCurrentSuitFlowIndex: removed\n");
    return true;
}