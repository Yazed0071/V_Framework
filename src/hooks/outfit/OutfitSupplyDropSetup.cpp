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
    // Verified signature (mgsvtpp.exe_Addresses.txt:15785048+):
    //   RCX = this (ItemSelectorCallbackImpl*)
    //   RDX = u32 row index
    using SupplyDropSuitSetup_t = void (__fastcall*)(void* self, std::uint32_t row);

    static SupplyDropSuitSetup_t g_OrigSupplyDropSuitSetup = nullptr;
    static bool                  g_Installed                = false;

    // Verified offsets from the orig disasm (1416a761f-1416a7637):
    //   variant byte:        this[0xC040 + row]            (u8)
    //   selectedFlowIndex:   this[0x4440 + (variant + row*15) * 2]  (u16)
    constexpr std::size_t kVariantTableOff = 0xC040;
    constexpr std::size_t kFlowIndexTableOff = 0x4440;

    // Supply-drop state buffer (verified 2026-04-26 via runtime data
    // capture across selFlow=0/500..507/517..522/758/761/898 — see
    // memory project_outfit_supply_drop_path.md):
    //
    //   this[0x46240]            u32  mode (3 = active drop)
    //   this[0x46250 + 0x04]     u32  selectedFlowIndex (DL=0x13 SUIT
    //                                  branch only — zero for DL=0x14)
    //   this[0x46250 + 0x08..0B] u8x4 param-ID lookups on equipId
    //                                  (DL=0x13 SUIT branch only —
    //                                  zero for DL=0x14)
    //   this[0x46250 + 0xBC]     u32  flag bitmask:
    //                                  0x10 = empty/cancel
    //                                  0x81 = DL=0x14 selection made
    //                                  0x82/0x83 = DL=0x13 SUIT made
    //                                  0x02 = our hook (suit-active)
    //
    // The DL=0x13 SUIT branch is reserved for stock suits (flowIndex
    // 0x1FD..0x200 + 0x207). All other "suit" rows (custom and most
    // vanilla camos) take DL=0x14 which writes different fields. State
    // buffer doesn't drive equip — that goes through LoadPartsNew,
    // which our OutfitRuntimeParts hook rewrites for broken-custom
    // signals. This hook's primary value is preventing the OOB read
    // at array[selFlow * 0x68 + 0x36] when selFlow is outside the
    // vanilla per-equipId info table.
    constexpr std::size_t kModeFieldOff      = 0x46240;
    constexpr std::size_t kStateBufferOff    = 0x46250;
    constexpr std::size_t kStateBufferSize   = 0xE8;
    constexpr std::size_t kStateOff_FlowIdx  = 0x04;
    constexpr std::size_t kStateOff_Camo     = 0x08;
    constexpr std::size_t kStateOff_Flags    = 0xBC;
    constexpr std::uint16_t kFlowIndexSentinel = 0x400;

    // Read the selected flow-index the same way the orig does. Returns
    // 0xFFFF on read fault.
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
            // Read failed or sentinel — let orig handle.
            g_OrigSupplyDropSuitSetup(self, row);
            return;
        }

        const outfit::OutfitEntry* entry = nullptr;
        if (!outfit::TryGetOutfitByFlowIndex(selFlow, &entry) || !entry)
        {
            // Not one of ours — vanilla path.
            g_OrigSupplyDropSuitSetup(self, row);
            return;
        }

        // Custom outfit. Skip the orig's vtable[0x718] OOB lookup and
        // manually populate the supply-drop state buffer so downstream
        // fulfillment sees a coherent record.
        //
        // We can't perfectly emulate the param-ID conversion bytes
        // (0x1FE/0x1FF/0x200) — those map equipId to material/arm/face
        // category enums, and our custom outfit doesn't have an equipId
        // entry in the vanilla table. We zero those fields and rely on
        // the LoadPartsNew rewrite (which keys off the broken-custom
        // signal) to fix appearance bytes when the actual equip fires.
        __try
        {
            auto* base = reinterpret_cast<std::uint8_t*>(self);

            *reinterpret_cast<std::uint32_t*>(base + kModeFieldOff) = 0x3;

            std::uint8_t* state = base + kStateBufferOff;
            std::memset(state, 0, kStateBufferSize);

            *reinterpret_cast<std::uint32_t*>(state + kStateOff_FlowIdx) =
                static_cast<std::uint32_t>(selFlow);
            *(state + kStateOff_Camo) = entry->selectorCode;
            // state[0x9..0xb] left at 0 (zeroed by memset) — vanilla
            // material/arm/face conversion bytes have no meaning for
            // a custom outfit; the runtime parts hooks override these
            // anyway when the equip happens.

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

            // We DON'T equip immediately here — supply-drop semantics
            // are "order now, equip on box pickup."
            //
            // On the actual confirm-click (latch set by the supply-
            // drop ItemSelector), stash the developId so the pickup
            // hook (OutfitSupplyDropPickup) knows to force-equip THIS
            // specific outfit when the player interacts with the
            // delivered crate. Hover-only fires don't set the latch
            // and don't stash anything, so opening unrelated boxes
            // (vanilla orders) leaves the stash clear and vanilla
            // apply runs untouched.
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
