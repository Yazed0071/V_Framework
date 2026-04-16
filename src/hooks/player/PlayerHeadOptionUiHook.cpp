#include "pch.h"

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "CustomSuitRegistry.h"
#include "PlayerHeadOptionUiHook.h"

namespace
{
    using IsEnableCurrentHeadOption_t = bool(__fastcall*)(void* self);
    using GetQuarkSystemTable_t = void* (__fastcall*)();

    static IsEnableCurrentHeadOption_t g_OrigIsEnableCurrentHeadOption = nullptr;
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
}

static bool __fastcall hkIsEnableCurrentHeadOption(void* self)
{
    // Check live Quark state (browsed suit) rather than committed active suit
    if (ResolveApis())
    {
        auto* quarkTable = reinterpret_cast<std::uint8_t*>(g_GetQuarkSystemTable());
        if (quarkTable)
        {
            auto* q98 = *reinterpret_cast<std::uint8_t**>(quarkTable + 0x98);
            if (q98)
            {
                auto* state = *reinterpret_cast<std::uint8_t**>(q98 + 0x10);
                if (state)
                {
                    const std::uint8_t livePartsType = state[0xF8];

                    const CustomSuitEntry* entry = nullptr;
                    if (TryGetCustomSuitByPartsType(livePartsType, &entry) &&
                        entry && entry->IsFaceEnabled())
                    {
                        return true;
                    }
                }
            }
        }
    }

    return g_OrigIsEnableCurrentHeadOption(self);
}

bool Install_PlayerHeadOptionUi_Hook()
{
    if (g_Installed)
    {
        Log("[Hook] PlayerHeadOptionUI: already installed\n");
        return true;
    }

    void* target = ResolveGameAddress(gAddr.MissionPrep_IsEnableCurrentHeadOption);
    if (!target)
    {
        Log("[Hook] PlayerHeadOptionUI: failed to resolve IsEnableCurrentHeadOption\n");
        return false;
    }

    const bool ok = CreateAndEnableHook(
        target,
        reinterpret_cast<void*>(&hkIsEnableCurrentHeadOption),
        reinterpret_cast<void**>(&g_OrigIsEnableCurrentHeadOption)
    );

    Log("[Hook] PlayerHeadOptionUI: %s\n", ok ? "OK" : "FAIL");
    g_Installed = ok;
    return ok;
}

bool Uninstall_PlayerHeadOptionUi_Hook()
{
    if (!g_Installed)
        return true;

    if (void* target = ResolveGameAddress(gAddr.MissionPrep_IsEnableCurrentHeadOption))
        DisableAndRemoveHook(target);

    g_OrigIsEnableCurrentHeadOption = nullptr;
    g_Installed = false;

    Log("[Hook] PlayerHeadOptionUI: removed\n");
    return true;
}