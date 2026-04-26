#include "pch.h"

#include "OutfitItemSelector.h"
#include "OutfitRegistry.h"

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    // 4-arg signature: (self, ErrorCode* out, void* p3, void* p4).
    // Verified from prior framework's signature audit.
    using DecideActMissionPrep_t = void* (__fastcall*)(
        void* self, void* out, void* p3, void* p4);

    static DecideActMissionPrep_t g_OrigDecideActMissionPrep = nullptr;
    static DecideActMissionPrep_t g_OrigDecideActSupplyDrop  = nullptr;
    static bool                    g_Installed                = false;
    static bool                    g_InstalledSupplyDrop      = false;

    // ItemSelector internal layout (offsets verified 2026-04-26 by
    // tracing DecideActMissionPreparationSetEquipMode @ 0x1416A3670):
    //   self + 0x4434 = u32 equipKind. 0x80 = MissionPrep suit equip.
    //   self + 0x10C  = i32 scroll-page base row index. Cursor scrolling
    //                   migrates the row from +0x110 into +0x10C while
    //                   the on-screen cursor offset returns to 0. Missed
    //                   in legacy framework — caused confirm to read
    //                   cell at row 0 instead of the user's actual row,
    //                   silently equipping vanilla suit at slot 0.
    //   self + 0x110  = i32 on-screen cursor row offset (relative to
    //                   the scroll base). Effective row = 0x10C + 0x110.
    //   self + 0xC040 = u8[64] per-row variant byte.
    //   self + 0x4440 = u16[?] selectedId table, stride (row*15+var)*2.
    //   self + 0xCC40 = u32[?] selectorCode table, stride (row*15+var)*12;
    //                    low byte is the selector we want.

    constexpr std::uint32_t kEquipKindMissionPrepSuit = 0x80;
    // Supply-drop's equivalent equipKind for suit-row clicks (verified
    // mgsvtpp.exe_Addresses.txt:15774601 — `CMP EAX,0x100` branch in
    // DecideActMotherBaseDeviceSupportDropMode is the suit path).
    constexpr std::uint32_t kEquipKindSupplyDropSuit = 0x100;

    struct SelectionSample
    {
        bool          haveSample   = false;
        std::uint32_t equipKind    = 0;
        std::uint16_t selectedId   = 0xFFFF;
        std::uint8_t  selectorCode = 0xFF;
    };

    static bool TryReadSelection(void* self, SelectionSample& out)
    {
        out = {};
        if (!self) return false;

        auto* base = reinterpret_cast<std::uint8_t*>(self);

        __try
        {
            const std::uint32_t equipKind = *reinterpret_cast<std::uint32_t*>(base + 0x4434);
            // Effective row = scroll-page base (+0x10C) + on-screen
            // cursor offset (+0x110). The orig
            // DecideActMissionPreparationSetEquipMode (line 2951220 of
            // mgsvtpp.exe.c) uses the SUM. Reading just +0x110 misses
            // any row past the first scroll page — the scroll commit
            // moves the value from +0x110 into +0x10C while +0x110
            // returns to 0. Hover-time reads happened to land on page 0
            // (where 0x10C==0), masking this for years.
            const std::int32_t baseRow   = *reinterpret_cast<std::int32_t*>(base + 0x10C);
            const std::int32_t cursorRow = *reinterpret_cast<std::int32_t*>(base + 0x110);
            const std::int32_t row       = baseRow + cursorRow;

            if (row < 0 || row > 0x3F) return false;

            const std::uint8_t variant = *(base + 0xC040 + row);
            if (variant > 14) return false;

            const std::size_t cellIndex = static_cast<std::size_t>(row) * 15 + variant;

            const std::uint16_t selectedId =
                *reinterpret_cast<std::uint16_t*>(base + 0x4440 + cellIndex * 2);

            const std::uint32_t selectorCodeU32 =
                *reinterpret_cast<std::uint32_t*>(base + 0xCC40 + cellIndex * 12);

            out.haveSample    = true;
            out.equipKind     = equipKind;
            out.selectedId    = selectedId;
            out.selectorCode  = static_cast<std::uint8_t>(selectorCodeU32 & 0xFF);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // Locate a registered outfit from the selection state. Returns
    // the matching developId (>0) or 0 if no custom outfit matches.
    // Match is attempted in this order:
    //   1. selectorCode in custom range → byPartsType lookup
    //   2. selectedId == outfit.flowIndex
    //   3. selectedId == outfit.developId
    static std::uint16_t MatchSelectionToOutfit(const SelectionSample& s)
    {
        // Path 1: selector byte already in custom range.
        if (s.selectorCode >= outfit::kCustomSelectorStart
            && s.selectorCode <= outfit::kCustomSelectorEnd)
        {
            const outfit::OutfitEntry* entry = nullptr;
            if (outfit::TryGetOutfitBySelectorCode(s.selectorCode, &entry) && entry)
                return entry->developId;
        }

        // Path 2: selectedId equals our registered flowIndex.
        const outfit::OutfitEntry* byFlow = nullptr;
        if (outfit::TryGetOutfitByFlowIndex(s.selectedId, &byFlow) && byFlow)
            return byFlow->developId;

        // Path 3: selectedId equals our developId (mod-author convenience).
        const outfit::OutfitEntry* byDev = nullptr;
        if (outfit::TryGetOutfitByDevelopId(s.selectedId, &byDev) && byDev)
            return byDev->developId;

        return 0;
    }

    // Shared body — reads the selection state, publishes pending devId
    // if it matches a registered outfit. Used by both the MissionPrep
    // suit-click hook and the Supply-Drop suit-click hook.
    static void ProcessSelectionAndPublish(void* self, const char* tag)
    {
        SelectionSample s{};
        const bool haveSample = TryReadSelection(self, s) && s.haveSample;

        if (haveSample)
        {
            const bool isSuitClick = (s.equipKind == kEquipKindMissionPrepSuit
                                   || s.equipKind == kEquipKindSupplyDropSuit);
            const std::uint16_t devId =
                isSuitClick ? MatchSelectionToOutfit(s) : 0;

            Log("[OutfitItemSelector:%s] fire: equipKind=0x%X selectedId=%u "
                "selector=0x%02X -> matched developId=%u\n",
                tag,
                s.equipKind,
                static_cast<unsigned>(s.selectedId),
                static_cast<unsigned>(s.selectorCode),
                static_cast<unsigned>(devId));

            if (devId != 0)
                outfit::SetPendingOutfitDevelopId(devId);
            else if (isSuitClick)
                outfit::ClearPendingOutfitDevelopId();
        }
        else
        {
            Log("[OutfitItemSelector:%s] fire: selection-state read failed\n", tag);
        }
    }

    static void* __fastcall hkDecideActMissionPrep(
        void* self, void* out, void* p3, void* p4)
    {
        ProcessSelectionAndPublish(self, "prep");
        return g_OrigDecideActMissionPrep(self, out, p3, p4);
    }

    static void* __fastcall hkDecideActSupplyDrop(
        void* self, void* out, void* p3, void* p4)
    {
        ProcessSelectionAndPublish(self, "supply");

        // Latch a "user just clicked confirm in supply-drop UI" signal.
        // OutfitSupplyDropSetup consumes this to distinguish a real
        // confirm from a hover/preview SupplyDropSuitSetup fire — the
        // hover doesn't go through this DecideAct callback, so the
        // latch stays clear and the framework skips the immediate
        // ForcePartsReload.
        outfit::SetSupplyDropClickLatch();

        return g_OrigDecideActSupplyDrop(self, out, p3, p4);
    }
}

namespace outfit
{
    bool Install_OutfitItemSelector_Hook()
    {
        if (g_Installed && g_InstalledSupplyDrop) return true;

        if (!g_Installed)
        {
            void* target = ResolveGameAddress(
                gAddr.ItemSelectorCallbackImpl_DecideActMissionPreparationSetEquipMode);
            if (!target)
            {
                Log("[OutfitItemSelector] prep target unresolved; module disabled\n");
                return false;
            }

            g_Installed = CreateAndEnableHook(
                target,
                reinterpret_cast<void*>(&hkDecideActMissionPrep),
                reinterpret_cast<void**>(&g_OrigDecideActMissionPrep));

            Log("[OutfitItemSelector] prep installed: %s (target=%p)\n",
                g_Installed ? "OK" : "FAIL", target);
        }

        if (!g_InstalledSupplyDrop)
        {
            void* target = ResolveGameAddress(
                gAddr.ItemSelectorCallbackImpl_DecideActMotherBaseDeviceSupportDropMode);
            if (!target)
            {
                Log("[OutfitItemSelector] supply-drop target unresolved; "
                    "supply-drop hook disabled\n");
            }
            else
            {
                g_InstalledSupplyDrop = CreateAndEnableHook(
                    target,
                    reinterpret_cast<void*>(&hkDecideActSupplyDrop),
                    reinterpret_cast<void**>(&g_OrigDecideActSupplyDrop));

                Log("[OutfitItemSelector] supply installed: %s (target=%p)\n",
                    g_InstalledSupplyDrop ? "OK" : "FAIL", target);
            }
        }

        return g_Installed;
    }

    void Uninstall_OutfitItemSelector_Hook()
    {
        if (g_Installed)
        {
            if (void* t = ResolveGameAddress(
                    gAddr.ItemSelectorCallbackImpl_DecideActMissionPreparationSetEquipMode))
                DisableAndRemoveHook(t);
            g_OrigDecideActMissionPrep = nullptr;
            g_Installed                = false;
        }
        if (g_InstalledSupplyDrop)
        {
            if (void* t = ResolveGameAddress(
                    gAddr.ItemSelectorCallbackImpl_DecideActMotherBaseDeviceSupportDropMode))
                DisableAndRemoveHook(t);
            g_OrigDecideActSupplyDrop = nullptr;
            g_InstalledSupplyDrop     = false;
        }
        Log("[OutfitItemSelector] removed\n");
    }
}
