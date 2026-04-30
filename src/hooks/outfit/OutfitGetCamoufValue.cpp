#include "pch.h"

#include "OutfitGetCamoufValue.h"
#include "OutfitRegistry.h"

#include <atomic>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"


namespace
{
    using GetCamoufValue_t =
        int (__fastcall*)(void* self,
                          std::uint32_t camoType,
                          std::uint32_t materialType);

    static GetCamoufValue_t g_OrigGetCamoufValue = nullptr;
    static bool             g_Installed          = false;

    static std::atomic<bool> g_FirstVirtualHitLogged{ false };

    static int __fastcall hkGetCamoufValue(
        void* self, std::uint32_t camoType, std::uint32_t materialType)
    {


        if (camoType <= outfit::kVanillaCamoTypeMax)
        {
            return g_OrigGetCamoufValue
                ? g_OrigGetCamoufValue(self, camoType, materialType)
                : 0;
        }


        const outfit::OutfitEntry* entry = nullptr;
        if (!outfit::TryGetOutfitByCamoVirtualId(
                static_cast<std::uint8_t>(camoType), &entry)
            || !entry || !entry->hasCamoBonusValues)
        {
            return 0;
        }

        if (materialType >= outfit::kCamoMaterialCount)
            return 0;

        const std::int32_t v = entry->camoBonusValues[materialType];

        if (!g_FirstVirtualHitLogged.exchange(true))
        {
            Log("[OutfitGetCamoufValue] FIRST VIRTUAL HIT: "
                "virtualId=%u materialType=%u value=%d "
                "(developId=%u flowIndex=%u)\n",
                static_cast<unsigned>(camoType),
                static_cast<unsigned>(materialType),
                static_cast<int>(v),
                static_cast<unsigned>(entry->developId),
                static_cast<unsigned>(entry->flowIndex));
        }

        return v;
    }
}

namespace outfit
{
    bool Install_OutfitGetCamoufValue_Hook()
    {
        if (g_Installed) return true;

        void* target = ResolveGameAddress(gAddr.CamoufParamInfo_GetCamoufValue);
        if (!target)
        {
            Log("[OutfitGetCamoufValue] target unresolved; module disabled "
                "(camoBonusValues unique-row feature will be inactive)\n");
            return false;
        }

        g_Installed = CreateAndEnableHook(
            target,
            reinterpret_cast<void*>(&hkGetCamoufValue),
            reinterpret_cast<void**>(&g_OrigGetCamoufValue));

        Log("[OutfitGetCamoufValue] installed: %s (target=%p)\n",
            g_Installed ? "OK" : "FAIL", target);
        return g_Installed;
    }

    void Uninstall_OutfitGetCamoufValue_Hook()
    {
        if (!g_Installed) return;
        if (void* t = ResolveGameAddress(gAddr.CamoufParamInfo_GetCamoufValue))
            DisableAndRemoveHook(t);
        g_OrigGetCamoufValue = nullptr;
        g_Installed          = false;
        Log("[OutfitGetCamoufValue] removed\n");
    }
}
