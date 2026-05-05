#include "pch.h"

#include "OutfitItemSelector.h"
#include "OutfitRegistry.h"

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{


    using DecideActMissionPrep_t = void* (__fastcall*)(
        void* self, void* out, void* p3, void* p4);

    static DecideActMissionPrep_t g_OrigDecideActMissionPrep = nullptr;
    static DecideActMissionPrep_t g_OrigDecideActSupplyDrop  = nullptr;
    static bool                    g_Installed                = false;
    static bool                    g_InstalledSupplyDrop      = false;


    using SetSupplyCBoxInfo_t = void (__fastcall*)(
        void* self, std::uint16_t flowIndex);

    static SetSupplyCBoxInfo_t g_OrigSetSupplyCBoxInfo = nullptr;
    static bool                g_InstalledSetSupplyCBox = false;


    constexpr std::uint32_t kEquipKindMissionPrepSuit = 0x80;


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


    static std::uint16_t MatchSelectionToOutfit(const SelectionSample& s)
    {


        if (s.selectorCode >= outfit::kCustomSelectorStart
            && s.selectorCode <= outfit::kCustomSelectorEnd)
        {
            const outfit::OutfitEntry* entry = nullptr;
            std::uint8_t variantIdx = 0;
            if (outfit::TryGetOutfitByVariantSelector(
                    s.selectorCode, &entry, &variantIdx) && entry)
            {

                outfit::SetActiveVariant(entry->partsType, variantIdx);
                return entry->developId;
            }
        }


        const outfit::OutfitEntry* byFlow = nullptr;
        if (outfit::TryGetOutfitByFlowIndex(s.selectedId, &byFlow) && byFlow)
        {
            outfit::SetActiveVariant(byFlow->partsType, 0);
            return byFlow->developId;
        }


        const outfit::OutfitEntry* byDev = nullptr;
        if (outfit::TryGetOutfitByDevelopId(s.selectedId, &byDev) && byDev)
        {
            outfit::SetActiveVariant(byDev->partsType, 0);
            return byDev->developId;
        }

        return 0;
    }


    constexpr std::uint32_t kEquipKindHeadOption = 0x201;


    static void ProcessSelectionAndPublish(void* self, const char* tag)
    {
        SelectionSample s{};
        const bool haveSample = TryReadSelection(self, s) && s.haveSample;

        if (haveSample)
        {
            const bool isSuitClick = (s.equipKind == kEquipKindMissionPrepSuit
                                   || s.equipKind == kEquipKindSupplyDropSuit);
            const bool isHeadOption = (s.equipKind == kEquipKindHeadOption);
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


            if (isHeadOption && s.selectedId != 0xFFFF)
            {
                outfit::SetPendingHeadOptionEquipId(s.selectedId);
                Log("[OutfitItemSelector:%s] head-option stash: "
                    "equipId=0x%X (will be re-injected into faceEquipId "
                    "during apply if orig drops it)\n",
                    tag,
                    static_cast<unsigned>(s.selectedId));
            }
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


        outfit::SetSupplyDropClickLatch();


        SelectionSample s{};
        if (TryReadSelection(self, s) && s.haveSample)
        {


            const bool isSuitClick = (s.equipKind == kEquipKindMissionPrepSuit
                                   || s.equipKind == kEquipKindSupplyDropSuit);
            const bool isCustomSelector =
                (s.selectorCode >= outfit::kCustomSelectorStart
                 && s.selectorCode <= outfit::kCustomSelectorEnd);

            if (isSuitClick || isCustomSelector)
            {


                const outfit::OutfitEntry* entry = nullptr;
                std::uint8_t variantIdx = 0;
                bool matched = false;

                if (isCustomSelector
                    && outfit::TryGetOutfitByVariantSelector(
                        s.selectorCode, &entry, &variantIdx)
                    && entry)
                {
                    matched = true;
                }
                else if (outfit::TryGetOutfitByFlowIndex(
                             s.selectedId, &entry) && entry)
                {
                    matched = true;
                    variantIdx = 0;
                }
                else if (outfit::TryGetOutfitByDevelopId(
                             s.selectedId, &entry) && entry)
                {
                    matched = true;
                    variantIdx = 0;
                }

                if (matched && entry)
                {
                    outfit::SetPendingSupplyDropDevelopId(entry->developId);
                    outfit::SetPendingSupplyDropVariantIdx(variantIdx);


                    outfit::SetPendingOutfitDevelopId(entry->developId);

                    Log("[OutfitItemSelector:supply] also stashed "
                        "pendingSupplyDropDevelopId=%u variantIdx=%u "
                        "(equipKind=0x%X, isSuitClick=%d, "
                        "isCustomSelector=%d) for crate-pickup "
                        "force-equip\n",
                        static_cast<unsigned>(entry->developId),
                        static_cast<unsigned>(variantIdx),
                        s.equipKind,
                        isSuitClick ? 1 : 0,
                        isCustomSelector ? 1 : 0);
                }
            }
        }

        return g_OrigDecideActSupplyDrop(self, out, p3, p4);
    }


    static void __fastcall hkSetSupplyCBoxInfo(
        void* self, std::uint16_t flowIndex)
    {
        const outfit::OutfitEntry* entry = nullptr;
        const bool isCustom =
            outfit::TryGetOutfitByFlowIndex(flowIndex, &entry) && entry;

        if (isCustom)
        {
            outfit::SetPendingOutfitDevelopId(entry->developId);
            outfit::SetPendingSupplyDropDevelopId(entry->developId);


            outfit::SetPendingSupplyDropVariantIdx(0);
            Log("[OutfitItemSelector:devmenu] R&D request for custom outfit "
                "flowIndex=%u developId=%u partsType=0x%02X selector=0x%02X "
                "— stashed both pendingOutfitDevelopId AND "
                "pendingSupplyDropDevelopId (variantIdx=0) for crate-pickup "
                "recovery\n",
                static_cast<unsigned>(flowIndex),
                static_cast<unsigned>(entry->developId),
                static_cast<unsigned>(entry->partsType),
                static_cast<unsigned>(entry->selectorCode));
        }

        if (g_OrigSetSupplyCBoxInfo)
            g_OrigSetSupplyCBoxInfo(self, flowIndex);


        if (isCustom && self)
        {
            __try
            {
                constexpr std::size_t kSupplyCboxLoadoutInfoOff = 0x23A0;

                auto* loadout =
                    reinterpret_cast<std::uint8_t*>(self) + kSupplyCboxLoadoutInfoOff;

                const std::uint8_t prevParts = loadout[0];
                const std::uint8_t prevCamo  = loadout[1];

                loadout[0] = entry->partsType;
                loadout[1] = entry->selectorCode;

                Log("[OutfitItemSelector:devmenu] post-orig SupplyCboxLoadoutInfo "
                    "rewrite: self+0x%X loadout[0]=0x%02X->0x%02X "
                    "loadout[1]=0x%02X->0x%02X — orig populated garbage from "
                    "OOB suit-info-table read for custom flowIndex=%u; "
                    "trigger event 0x5DB695F97E34 already fired with "
                    "loadoutInfo pointer — works if handler reads lazily\n",
                    static_cast<unsigned>(kSupplyCboxLoadoutInfoOff),
                    static_cast<unsigned>(prevParts),
                    static_cast<unsigned>(loadout[0]),
                    static_cast<unsigned>(prevCamo),
                    static_cast<unsigned>(loadout[1]),
                    static_cast<unsigned>(flowIndex));
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                Log("[OutfitItemSelector:devmenu] SEH writing SupplyCboxLoadoutInfo "
                    "at self+0x23A0 (self=%p) — offset assumption may be wrong\n",
                    self);
            }
        }
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

        if (!g_InstalledSetSupplyCBox)
        {
            void* target = ResolveGameAddress(
                gAddr.EquipDevelopCallbackImpl_SetSupplyCBoxInfo);
            if (!target)
            {
                Log("[OutfitItemSelector] R&D dev-menu SetSupplyCBoxInfo "
                    "target unresolved; R&D-menu request hook disabled\n");
            }
            else
            {
                g_InstalledSetSupplyCBox = CreateAndEnableHook(
                    target,
                    reinterpret_cast<void*>(&hkSetSupplyCBoxInfo),
                    reinterpret_cast<void**>(&g_OrigSetSupplyCBoxInfo));

                Log("[OutfitItemSelector] devmenu SetSupplyCBoxInfo installed: "
                    "%s (target=%p)\n",
                    g_InstalledSetSupplyCBox ? "OK" : "FAIL", target);
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
        if (g_InstalledSetSupplyCBox)
        {
            if (void* t = ResolveGameAddress(
                    gAddr.EquipDevelopCallbackImpl_SetSupplyCBoxInfo))
                DisableAndRemoveHook(t);
            g_OrigSetSupplyCBoxInfo  = nullptr;
            g_InstalledSetSupplyCBox = false;
        }
        Log("[OutfitItemSelector] removed\n");
    }
}
