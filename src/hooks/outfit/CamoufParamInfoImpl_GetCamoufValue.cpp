#include "pch.h"

#include "CamoufParamInfoImpl_GetCamoufValue.h"
#include "OutfitRegistry.h"

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
        std::uint8_t                ownerPT = 0;
        if (!outfit::TryGetOutfitByCamoVirtualId(
                static_cast<std::uint8_t>(camoType), &entry, &ownerPT)
            || !entry)
        {
            return 0;
        }

        if (materialType >= outfit::kCamoMaterialCount)
            return 0;

        const std::int32_t* row = entry->GetCamoBonusValuesFor(ownerPT);
        if (!row) return 0;

        const std::int32_t v = row[materialType];

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
            LogError("OutfitGetCamoufValue: GetCamoufValue address missing for this "
                "build, camoBonusValues off");
            return false;
        }

        g_Installed = CreateAndEnableHook(
            target,
            reinterpret_cast<void*>(&hkGetCamoufValue),
            reinterpret_cast<void**>(&g_OrigGetCamoufValue));

        if (!g_Installed)
            LogError("OutfitGetCamoufValue: GetCamoufValue hook failed, camoBonusValues off");
        return g_Installed;
    }

    void Uninstall_OutfitGetCamoufValue_Hook()
    {
        if (!g_Installed) return;
        if (void* t = ResolveGameAddress(gAddr.CamoufParamInfo_GetCamoufValue))
            DisableAndRemoveHook(t);
        g_OrigGetCamoufValue = nullptr;
        g_Installed          = false;
    }
}
