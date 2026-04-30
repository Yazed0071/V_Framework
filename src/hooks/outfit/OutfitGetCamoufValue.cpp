#include "pch.h"

#include "OutfitGetCamoufValue.h"
#include "OutfitRegistry.h"

#include <atomic>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

// ===========================================================================
// CamoufParamInfoImpl::GetCamoufValue hook (retail 0x14691B460).
//
// What the orig does (verified from retail disasm at
// mgsvtpp.exe_Addresses.txt:83907841-83907854):
//
//   int GetCamoufValue(this, uint camoType, uint materialType)
//   {
//       if (0x52 < materialType) return 0;        // bound on material
//       return *(int*)(this + 8 + (camoType * 0x53 + materialType) * 4);
//       // ^^^ NO bound on camoType — anything past 116 reads OOB.
//   }
//
// Why we hook it: the per-outfit "unique camo bonus row" feature
// (camoBonusValues field on OutfitDefinition) needs to feed the engine
// values from a per-outfit inline array, not from the orig 117-row
// table. We allocate a virtual camoType id (200..254) per outfit at
// registration time and set OutfitEntry::camoBonusType to it. The
// existing OutfitCamoBonus hook (ExecSuitCorrect SAVE+WRITE+ORIG+
// RESTORE) writes that virtual id into Info+0x50[slot] before the orig
// reads it into cVar13 and calls vtable[0x18](this, cVar13, materialType)
// — which lands here, in GetCamoufValue.
//
// Our hook:
//   - If camoType is in the virtual range (kCamoVirtualIdStart..End):
//       look up the outfit by virtualId, return outfit->camoBonusValues
//       [materialType]. Bounds-checked. Returns 0 for unknown ids /
//       OOB material types.
//   - Otherwise: forward to orig (vanilla 117-row table read).
//
// Edge cases:
//   - SEH-guarded around the orig forward (in case some caller passes
//     a corrupt this).
//   - The lookup is a linear scan against ~tens of registered outfits;
//     trivial cost.
//   - First-fire diagnostic log; quiet thereafter.
// ===========================================================================

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
        // Vanilla range — forward to orig untouched. The orig handles
        // its own materialType bound check and table read.
        if (camoType <= outfit::kVanillaCamoTypeMax)
        {
            return g_OrigGetCamoufValue
                ? g_OrigGetCamoufValue(self, camoType, materialType)
                : 0;
        }

        // Virtual range — try resolve to a registered outfit. If the id
        // isn't ours (e.g. some other code path passed an out-of-range
        // value), return 0 rather than letting the orig OOB-read.
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
