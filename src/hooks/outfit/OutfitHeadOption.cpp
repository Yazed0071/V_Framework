#include "pch.h"

#include "OutfitHeadOption.h"
#include "OutfitRegistry.h"

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    // Real signature: bool __fastcall(MissionPreparationCallbackImpl*)
    // (verified mgsvtpp.exe_Addresses.txt:139983616).
    using IsEnableCurrentHeadOption_t = std::uint8_t (__fastcall*)(void* self);

    static IsEnableCurrentHeadOption_t g_OrigIsEnableHead = nullptr;
    static bool                        g_Installed        = false;

    static std::uint8_t __fastcall hkIsEnableCurrentHeadOption(void* self)
    {
        // If the live partsType is custom and the matching outfit
        // declares head-option support, force-enable the submenu.
        // Otherwise pass through to the vanilla gate.
        const std::uint8_t pt = outfit::ReadLivePartsType();

        if (pt >= outfit::kCustomPartsTypeStart && pt <= outfit::kCustomPartsTypeEnd)
        {
            const outfit::OutfitEntry* entry = nullptr;
            if (outfit::TryGetOutfitByPartsType(pt, &entry) && entry)
            {
                return entry->HasHeadOptions() ? 1 : 0;
            }
        }

        return g_OrigIsEnableHead(self);
    }
}

namespace outfit
{
    bool Install_OutfitHeadOption_Hook()
    {
        if (g_Installed) return true;

        void* target = ResolveGameAddress(gAddr.MissionPrep_IsEnableCurrentHeadOption);
        if (!target)
        {
            Log("[OutfitHeadOption] target unresolved; module disabled\n");
            return false;
        }

        g_Installed = CreateAndEnableHook(
            target,
            reinterpret_cast<void*>(&hkIsEnableCurrentHeadOption),
            reinterpret_cast<void**>(&g_OrigIsEnableHead));

        Log("[OutfitHeadOption] installed: %s (target=%p, gate-only — submenu list "
            "build deferred to follow-up runtime probe)\n",
            g_Installed ? "OK" : "FAIL", target);
        return g_Installed;
    }

    void Uninstall_OutfitHeadOption_Hook()
    {
        if (!g_Installed) return;
        if (void* t = ResolveGameAddress(gAddr.MissionPrep_IsEnableCurrentHeadOption))
            DisableAndRemoveHook(t);
        g_OrigIsEnableHead = nullptr;
        g_Installed        = false;
        Log("[OutfitHeadOption] removed\n");
    }
}
