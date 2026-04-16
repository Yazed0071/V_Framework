#include "pch.h"

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "CustomSuitRegistry.h"
#include "PlayerHeadOptionSelectionCountHook.h"

namespace
{
    using GetSelectionNum_t = int(__fastcall*)(void* self);
    using GetQuarkSystemTable_t = void* (__fastcall*)();

    static GetSelectionNum_t g_OrigGetSelectionNum = nullptr;
    static GetQuarkSystemTable_t g_GetQuarkSystemTable = nullptr;
    static bool g_Installed = false;

    static bool ResolveApis()
    {
        if (!g_GetQuarkSystemTable)
        {
            g_GetQuarkSystemTable = reinterpret_cast<GetQuarkSystemTable_t>(
                ResolveGameAddress(gAddr.GetQuarkSystemTable));
        }
        return g_GetQuarkSystemTable != nullptr;
    }

    // Check the LIVE Quark state (what the player is currently browsing)
    // rather than the cached active suit (which only updates on confirm).
    static bool ShouldForceThreeSelections()
    {
        if (!ResolveApis())
            return false;

        auto* quarkTable = reinterpret_cast<std::uint8_t*>(g_GetQuarkSystemTable());
        if (!quarkTable) return false;

        auto* q98 = *reinterpret_cast<std::uint8_t**>(quarkTable + 0x98);
        if (!q98) return false;

        auto* state = *reinterpret_cast<std::uint8_t**>(q98 + 0x10);
        if (!state) return false;

        const std::uint8_t livePartsType = state[0xF8];

        const CustomSuitEntry* entry = nullptr;
        if (!TryGetCustomSuitByPartsType(livePartsType, &entry) || !entry)
            return false;

        // Force 3 selections only when enableHead is on (IsFaceEnabled)
        // This shows the NONE/BALACLAVA/HEADGEAR cycling in the HEAD OPTION slot.
        // Body variants (variant groups) are a separate system and don't use this slot.
        return entry->IsFaceEnabled();
    }
}

static int __fastcall hkGetSelectionNum(void* self)
{
    const int original = g_OrigGetSelectionNum(self);

    if (ShouldForceThreeSelections())
    {
        const int forced = (original < 3) ? 3 : original;

        Log(
            "[HeadOptionSelectionCount] original=%d forced=%d\n",
            original,
            forced
        );

        return forced;
    }

    // Only log on change to reduce spam
    static int s_lastPassthrough = -1;
    if (original != s_lastPassthrough)
    {
        Log("[HeadOptionSelectionCount] passthrough=%d\n", original);
        s_lastPassthrough = original;
    }

    return original;
}

bool Install_PlayerHeadOptionSelectionCount_Hook()
{
    Log("[Hook] PlayerHeadOptionSelectionCount: enter\n");
    if (g_Installed)
    {
        Log("[Hook] PlayerHeadOptionSelectionCount: already installed\n");
        return true;
    }

    void* target = ResolveGameAddress(gAddr.MissionPrep_GetSelectionNum);
    if (!target)
    {
        Log("[Hook] PlayerHeadOptionSelectionCount: failed to resolve GetSelectionNum\n");
        return false;
    }

    const bool ok = CreateAndEnableHook(
        target,
        reinterpret_cast<void*>(&hkGetSelectionNum),
        reinterpret_cast<void**>(&g_OrigGetSelectionNum)
    );

    Log("[Hook] PlayerHeadOptionSelectionCount: %s\n", ok ? "OK" : "FAIL");
    g_Installed = ok;
    return ok;
}

bool Uninstall_PlayerHeadOptionSelectionCount_Hook()
{
    if (!g_Installed)
        return true;

    if (void* target = ResolveGameAddress(gAddr.MissionPrep_GetSelectionNum))
        DisableAndRemoveHook(target);

    g_OrigGetSelectionNum = nullptr;
    g_Installed = false;

    Log("[Hook] PlayerHeadOptionSelectionCount: removed\n");
    return true;
}