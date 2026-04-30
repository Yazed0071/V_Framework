#include "pch.h"

#include "OutfitSupplyDropSetup.h"
#include "OutfitRegistry.h"
#include "OutfitRuntimeParts.h"

#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{


    using SupplyDropSuitSetup_t = void (__fastcall*)(void* self, std::uint32_t row);

    static SupplyDropSuitSetup_t g_OrigSupplyDropSuitSetup = nullptr;
    static bool                  g_Installed                = false;


    constexpr std::size_t kVariantTableOff = 0xC040;
    constexpr std::size_t kFlowIndexTableOff = 0x4440;


    constexpr std::size_t kModeFieldOff      = 0x46240;
    constexpr std::size_t kStateBufferOff    = 0x46250;
    constexpr std::size_t kStateBufferSize   = 0xE8;
    constexpr std::size_t kStateOff_FlowIdx  = 0x04;
    constexpr std::size_t kStateOff_Camo     = 0x08;
    constexpr std::size_t kStateOff_Flags    = 0xBC;
    constexpr std::uint16_t kFlowIndexSentinel = 0x400;


    static std::uint16_t ReadSelectedFlowIndex(void* self, std::uint32_t row)
    {
        if (!self) return 0xFFFF;
        auto* base = reinterpret_cast<std::uint8_t*>(self);

        __try
        {
            const std::uint8_t  variant   = *(base + kVariantTableOff + row);
            const std::size_t   cellIndex = static_cast<std::size_t>(row) * 15
                                          + static_cast<std::size_t>(variant);
            return *reinterpret_cast<std::uint16_t*>(
                base + kFlowIndexTableOff + cellIndex * 2);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0xFFFF;
        }
    }

    static void __fastcall hkSupplyDropSuitSetup(void* self, std::uint32_t row)
    {
        if (!self)
        {
            if (g_OrigSupplyDropSuitSetup) g_OrigSupplyDropSuitSetup(self, row);
            return;
        }

        const std::uint16_t selFlow = ReadSelectedFlowIndex(self, row);
        if (selFlow == 0xFFFF || selFlow == kFlowIndexSentinel)
        {

            g_OrigSupplyDropSuitSetup(self, row);
            return;
        }

        const outfit::OutfitEntry* entry = nullptr;
        if (!outfit::TryGetOutfitByFlowIndex(selFlow, &entry) || !entry)
        {

            g_OrigSupplyDropSuitSetup(self, row);
            return;
        }


        __try
        {
            auto* base = reinterpret_cast<std::uint8_t*>(self);

            *reinterpret_cast<std::uint32_t*>(base + kModeFieldOff) = 0x3;

            std::uint8_t* state = base + kStateBufferOff;
            std::memset(state, 0, kStateBufferSize);

            *reinterpret_cast<std::uint32_t*>(state + kStateOff_FlowIdx) =
                static_cast<std::uint32_t>(selFlow);
            *(state + kStateOff_Camo) = entry->selectorCode;


            *reinterpret_cast<std::uint32_t*>(state + kStateOff_Flags) = 0x2;

            Log("[OutfitSupplyDropSetup] custom-outfit short-circuit: "
                "row=%u selectedFlowIndex=%u developId=%u partsType=0x%02X "
                "selector=0x%02X — wrote state buffer at this+0x%X\n",
                row,
                static_cast<unsigned>(selFlow),
                static_cast<unsigned>(entry->developId),
                static_cast<unsigned>(entry->partsType),
                static_cast<unsigned>(entry->selectorCode),
                static_cast<unsigned>(kStateBufferOff));


            if (outfit::ConsumeSupplyDropClickLatch())
            {
                outfit::SetPendingSupplyDropDevelopId(entry->developId);
                Log("[OutfitSupplyDropSetup] confirm latched: stashed "
                    "developId=%u for pickup-time force-equip\n",
                    static_cast<unsigned>(entry->developId));
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("[OutfitSupplyDropSetup] SEH writing state buffer for "
                "selectedFlowIndex=%u — falling through to orig\n",
                static_cast<unsigned>(selFlow));
            g_OrigSupplyDropSuitSetup(self, row);
        }
    }
}

namespace outfit
{
    bool Install_OutfitSupplyDropSetup_Hook()
    {
        if (g_Installed) return true;

        void* target = ResolveGameAddress(gAddr.SupplyDropSuitSetup);
        if (!target)
        {
            Log("[OutfitSupplyDropSetup] target unresolved; module disabled\n");
            return false;
        }

        g_Installed = CreateAndEnableHook(
            target,
            reinterpret_cast<void*>(&hkSupplyDropSuitSetup),
            reinterpret_cast<void**>(&g_OrigSupplyDropSuitSetup));

        Log("[OutfitSupplyDropSetup] installed: %s (target=%p)\n",
            g_Installed ? "OK" : "FAIL", target);
        return g_Installed;
    }

    void Uninstall_OutfitSupplyDropSetup_Hook()
    {
        if (!g_Installed) return;

        if (void* t = ResolveGameAddress(gAddr.SupplyDropSuitSetup))
            DisableAndRemoveHook(t);

        g_OrigSupplyDropSuitSetup = nullptr;
        g_Installed               = false;
        Log("[OutfitSupplyDropSetup] removed\n");
    }
}
