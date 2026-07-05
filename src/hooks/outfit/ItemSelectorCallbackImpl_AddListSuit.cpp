#include "pch.h"

#include "ItemSelectorCallbackImpl_AddListSuit.h"
#include "OutfitRegistry.h"
#include "EquipDevelopControllerImpl_GetSuitDevelopInfoIndex.h"
#include "CustomHeadRegistry.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{


    using SetupPrefabListElement_t = void  (__fastcall*)(void* thisPtr);
    using AddListSuit_t            = void  (__fastcall*)(
                                            void* thisPtr,
                                            std::uint32_t* rowCounter,
                                            std::uint16_t flowIndex,
                                            void* entryBuf);


    using AddListBandana_t = void (__fastcall*)(void* thisPtr,
                                                std::uint32_t* count,
                                                std::uint16_t equipId);
    static AddListBandana_t g_AddListBandana = nullptr;
#ifdef _DEBUG
    static std::atomic<bool> g_HeadOptionInjectFirstFire{ false };
#endif


    using UpdateRecords_t          = void  (__fastcall*)(void* thisPtr);

    static SetupPrefabListElement_t g_OrigSetupPrefab    = nullptr;


    static AddListSuit_t            g_OrigAddListSuit    = nullptr;

    static void*                    g_AddListSuitAddr    = nullptr;

    static UpdateRecords_t          g_OrigUpdateRecords     = nullptr;
    static bool                     g_InstalledUpdateRecords = false;


    using HeadBadgeCategory_t = std::uint32_t (__fastcall*)(void* self,
                                                            std::uint32_t equipId);
    using WornHeadCategory_t  = std::uint8_t  (__fastcall*)(void* self);
    static HeadBadgeCategory_t g_OrigHeadBadgeCategory = nullptr;
    static WornHeadCategory_t  g_OrigWornHeadCategory  = nullptr;
    static bool g_InstalledHeadBadgeCategory = false;
    static bool g_InstalledWornHeadCategory  = false;
    static bool g_HeadBadgeBuildActive = false;

    static bool g_HeadEquipDecideActive = false;

    static bool       g_Installed       = false;


    thread_local bool t_InsideSetupPrefab = false;


    thread_local std::array<std::uint64_t, 16> t_AddedFlowIxBits = {};

    static bool TestAndSetAddedBit(std::uint16_t flowIndex)
    {
        if (flowIndex >= 1024) return false;
        auto& word = t_AddedFlowIxBits[flowIndex >> 6];
        const std::uint64_t mask = 1ull << (flowIndex & 63);
        if (word & mask) return true;
        word |= mask;
        return false;
    }

    static void ResetAddedFlowIxBits()
    {
        t_AddedFlowIxBits.fill(0);
    }


    static constexpr int    kMaxInstallAttempts = 16;


    constexpr std::size_t kVtblIx_GetCount = 0x230 / sizeof(void*);
    constexpr std::size_t kVtblIx_Fill     = 0x240 / sizeof(void*);
    constexpr std::size_t kVtblIx_GetTable = 0x718 / sizeof(void*);






    static bool g_VariantInjectEnabled    = true;
    static bool g_HeadOptionInjectEnabled = true;

    static void __fastcall hkAddListSuit(
        void* thisPtr,
        std::uint32_t* rowCounter,
        std::uint16_t flowIndex,
        void* entryBuf)
    {
        if (outfit::IsCustomHeadEquipId(flowIndex))
        {
#ifdef _DEBUG
            Log("[OutfitListInject:AddListSuit] suppressed custom-HEAD row "
                "flowIndex=%u (heads belong in the 0x201 submenu, not the "
                "uniform list)\n", static_cast<unsigned>(flowIndex));
#endif
            return;
        }

        if (t_InsideSetupPrefab)
        {
            const outfit::OutfitEntry* entry = nullptr;
            if (outfit::TryGetOutfitByFlowIndex(flowIndex, &entry) && entry)
            {
                const std::uint8_t livePT = outfit::ReadLivePlayerType();
                if (livePT != 0xFF
                    && !entry->IsPlayerTypeSupported(livePT))
                {
#ifdef _DEBUG
                    Log("[OutfitListInject:AddListSuit] suppressed PT-unsupported "
                        "flowIndex=%u live-PT=%u (developId=%u partsType=0x%02X)\n",
                        static_cast<unsigned>(flowIndex),
                        static_cast<unsigned>(livePT),
                        static_cast<unsigned>(entry->developId),
                        static_cast<unsigned>(entry->partsType));
#endif
                    return;
                }
            }
        }

        if (!g_VariantInjectEnabled)
        {
            if (g_OrigAddListSuit)
                g_OrigAddListSuit(thisPtr, rowCounter, flowIndex, entryBuf);
            return;
        }

        if (t_InsideSetupPrefab && TestAndSetAddedBit(flowIndex))
        {


            return;
        }


        const std::uint32_t rowPre =
            (rowCounter ? *rowCounter : 0xFFFFFFFFu);

        if (g_OrigAddListSuit)
            g_OrigAddListSuit(thisPtr, rowCounter, flowIndex, entryBuf);


        if (!thisPtr || !rowCounter) return;
        const std::uint32_t rowPost = *rowCounter;
        if (rowPost == rowPre)
        {


            return;
        }
        const std::uint32_t row = rowPost - 1;
        if (row > 0x3F) return;

        const outfit::OutfitEntry* entry = nullptr;
        if (!outfit::TryGetOutfitByFlowIndex(flowIndex, &entry) || !entry)
            return;


        const std::uint8_t livePT = outfit::ReadLivePlayerType();
        const std::uint8_t variantsForPT = (livePT != 0xFF)
            ? entry->GetVariantCountFor(livePT)
            : entry->variantCount;

        if (variantsForPT < 2) return;

        __try
        {
            auto* base = reinterpret_cast<std::uint8_t*>(thisPtr);

            if (*reinterpret_cast<std::uint64_t*>(base + 0x461b8)
                    != 0xb8a0bf169f98ull)
            {
                Log("[OutfitListInject:AddListSuit] wrong-object guard: "
                    "thisPtr=%p is not the suit panel (+0x461b8 mismatch) -> "
                    "skipped variant-cell writes (flowIndex=%u)\n",
                    thisPtr, static_cast<unsigned>(flowIndex));
                return;
            }

            for (std::uint8_t var = 0; var < variantsForPT; ++var)
            {
                const std::size_t cellIndex =
                    static_cast<std::size_t>(row) * 15 + var;


                *reinterpret_cast<std::uint16_t*>(
                    base + 0x4440 + cellIndex * 2) = flowIndex;


                std::uint8_t* cell = base + 0xCC40 + cellIndex * 12;
                *reinterpret_cast<std::uint32_t*>(cell + 0) =
                    static_cast<std::uint32_t>(
                        entry->variantSelectorCodes[var]);
                *reinterpret_cast<std::uint32_t*>(cell + 4) =
                    (var == 0) ? 7u : 0u;
                *(cell + 8) = 0;


                *(base + 0x548   + cellIndex) = 1;
                *(base + 0x425a4 + cellIndex) = 0;
            }


            *(base + 0xBC40 + row) = variantsForPT;


            std::uint8_t activeVar =
                outfit::GetActiveVariant(entry->partsType);
            if (activeVar >= variantsForPT)
                activeVar = static_cast<std::uint8_t>(variantsForPT - 1);
            *(base + 0xC040 + row) = activeVar;


#ifdef _DEBUG
            char variantBuf[16 * 4 + 1] = {};
            {
                std::size_t pos = 0;
                for (std::size_t i = 0;
                     i < variantsForPT && pos + 4 < sizeof(variantBuf);
                     ++i)
                {
                    pos += static_cast<std::size_t>(std::snprintf(
                        variantBuf + pos, sizeof(variantBuf) - pos,
                        (i == 0) ? "%02X" : ",%02X",
                        static_cast<unsigned>(entry->variantSelectorCodes[i])));
                }
            }

            Log("[OutfitListInject:AddListSuit] post-orig variant cell "
                "injection: flowIndex=%u developId=%u row=%u livePT=%u "
                "variantsForPT=%u selectors=[%s]\n",
                static_cast<unsigned>(flowIndex),
                static_cast<unsigned>(entry->developId),
                static_cast<unsigned>(row),
                static_cast<unsigned>(livePT),
                static_cast<unsigned>(variantsForPT),
                variantBuf);
#endif
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("[OutfitListInject:AddListSuit] SEH writing variant "
                "cells (flowIndex=%u row=%u)\n",
                static_cast<unsigned>(flowIndex),
                static_cast<unsigned>(row));
        }
    }


    static bool LooksLikeValidPtr(const void* p)
    {
        const std::uintptr_t v = reinterpret_cast<std::uintptr_t>(p);
        if (v == 0) return false;
        if (v < 0x10000) return false;
        if ((v & 0x7) != 0) return false;

        if (v >= 0x800000000000ull) return false;
        return true;
    }




    using GetTextByHash_t  = void* (__fastcall*)(void* manager,
                                                  std::uint64_t hash);
    using WriteTextField_t = void  (__fastcall*)(void* manager,
                                                  void* dst1,
                                                  void* dst2,
                                                  void* text);

    static void __fastcall hkUpdateRecords(void* thisPtr)
    {
        if (g_OrigUpdateRecords) g_OrigUpdateRecords(thisPtr);

        if (!thisPtr) return;

        std::uint64_t variantHash = 0;
        std::uint8_t  variantIdx  = 0;
        std::uint16_t selectedId  = 0;

        __try
        {
            auto* base = reinterpret_cast<std::uint8_t*>(thisPtr);

            const std::uint32_t row = *reinterpret_cast<std::uint32_t*>(base + 0x008);
            if (row > 0x3F) return;

            const auto variantTable =
                *reinterpret_cast<std::uint8_t* const*>(base + 0x1F0);
            const auto selectedIdTable =
                *reinterpret_cast<std::uint16_t* const*>(base + 0x1E8);
            if (!variantTable || !selectedIdTable) return;

            variantIdx = *(variantTable + row);
            if (variantIdx > 14) return;

            const std::size_t cellIndex =
                static_cast<std::size_t>(row) * 15 + variantIdx;
            selectedId = *(selectedIdTable + cellIndex);


            const outfit::OutfitEntry* entry = nullptr;
            if (!outfit::TryGetOutfitByFlowIndex(selectedId, &entry) || !entry)
                return;

            if (variantIdx >= outfit::kMaxVariantsPerOutfit)
                return;


            const std::uint8_t livePT = outfit::ReadLivePlayerType();
            const std::uint8_t labelPT =
                (livePT != 0xFF && entry->IsPlayerTypeSupported(livePT))
                    ? livePT
                    : outfit::kPlayerType_Snake;
            variantHash = entry->GetVariantDisplayNameHash(labelPT, variantIdx);


#ifdef _DEBUG
            {
                static std::uint16_t s_lastMatchSelId  = 0xFFFF;
                static std::uint8_t  s_lastMatchVarIdx = 0xFF;
                static std::uint64_t s_lastMatchHash   = 0xFFFFFFFFFFFFFFFFull;
                if (s_lastMatchSelId  != selectedId
                 || s_lastMatchVarIdx != variantIdx
                 || s_lastMatchHash   != variantHash)
                {
                    Log("[OutfitListInject:UpdateRecords] matched custom "
                        "outfit row: selectedId=%u developId=%u variantIdx=%u "
                        "variantHash=0x%016llX %s\n",
                        static_cast<unsigned>(selectedId),
                        static_cast<unsigned>(entry->developId),
                        static_cast<unsigned>(variantIdx),
                        static_cast<unsigned long long>(variantHash),
                        variantHash == 0
                            ? "(no displayName set in Lua — orig label kept)"
                            : "(will override)");
                    s_lastMatchSelId  = selectedId;
                    s_lastMatchVarIdx = variantIdx;
                    s_lastMatchHash   = variantHash;
                }
            }
#endif

            if (variantHash == 0) return;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return;
        }


        __try
        {
            auto* base = reinterpret_cast<std::uint8_t*>(thisPtr);

            void* manager     = *reinterpret_cast<void**>(base + 0x38);
            void* writeTarget1 = *reinterpret_cast<void**>(base + 0x180);
            void* writeTarget2 = *reinterpret_cast<void**>(base + 0x80);

            if (!manager) return;

            void** managerVtable = *reinterpret_cast<void***>(manager);
            if (!managerVtable) return;

            auto getText  = reinterpret_cast<GetTextByHash_t>(
                managerVtable[0x750 / 8]);
            auto writeFn  = reinterpret_cast<WriteTextField_t>(
                managerVtable[0x708 / 8]);

            if (!getText || !writeFn) return;

            void* text = getText(manager, variantHash);
            if (!text) return;

            writeFn(manager, writeTarget1, writeTarget2, text);

#ifdef _DEBUG
            static std::uint16_t s_lastSelectedId = 0xFFFF;
            static std::uint8_t  s_lastVariantIdx = 0xFF;
            static std::uint64_t s_lastHash       = 0;
            if (s_lastSelectedId != selectedId
             || s_lastVariantIdx != variantIdx
             || s_lastHash       != variantHash)
            {
                Log("[OutfitListInject:UpdateRecords] cycle-button label "
                    "override: selectedId=%u variantIdx=%u hash=0x%016llX\n",
                    static_cast<unsigned>(selectedId),
                    static_cast<unsigned>(variantIdx),
                    static_cast<unsigned long long>(variantHash));
                s_lastSelectedId = selectedId;
                s_lastVariantIdx = variantIdx;
                s_lastHash       = variantHash;
            }
#endif
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("[OutfitListInject:UpdateRecords] SEH writing variant "
                "label (selectedId=%u variantIdx=%u)\n",
                static_cast<unsigned>(selectedId),
                static_cast<unsigned>(variantIdx));
        }
    }


    static void TryInjectHeadOptionList(void* thisPtr)
    {
        outfit::DrainPendingHeads();
        if (!thisPtr || !g_AddListBandana) return;

        const auto base = reinterpret_cast<std::uintptr_t>(thisPtr);

        std::uint32_t equipKind = 0;
        __try
        {
            equipKind = *reinterpret_cast<std::uint32_t*>(base + 0x4434);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return;
        }
        if (equipKind != 0x201) return;

        const std::uint8_t pt = outfit::ReadLivePartsType();
        if (!(pt >= outfit::kCustomPartsTypeStart && pt <= outfit::kCustomPartsTypeEnd))
            return;

        const outfit::OutfitEntry* entry = nullptr;
        if (!outfit::TryGetOutfitByPartsType(pt, &entry) || !entry)
            return;


        const std::uint8_t      livePT  = outfit::ReadLivePlayerType();
        const std::uint8_t      variant = outfit::GetActiveVariant(pt);
        const std::uint16_t*    headIds = nullptr;
        std::uint8_t            headCount = 0;
        entry->GetHeadOptionsForVariant(livePT, variant, &headIds, &headCount);
        if (headCount > 0 && !headIds) headCount = 0;


        std::uint32_t count = 0;
        __try
        {
            if (*reinterpret_cast<std::uint64_t*>(base + 0x461b8)
                    != 0xb8a0bf169f98ull)
            {
                Log("[OutfitListInject:HeadOption] wrong-object guard: "
                    "base=%p is not the suit panel (+0x461b8 mismatch) -> "
                    "skipped head-marker writes\n", base);
                return;
            }

            const std::uint32_t origCount =
                *reinterpret_cast<std::uint32_t*>(base + 0x442c);

            std::uint32_t keep = origCount;
            if (origCount >= 1
                && *reinterpret_cast<std::uint16_t*>(base + 0x4440)
                       == outfit::kHeadOption_None)
            {
                keep = 1;
            }


            std::uint8_t* markersA =
                reinterpret_cast<std::uint8_t*>(base + 0xbc40);
            std::uint8_t* markersB =
                reinterpret_cast<std::uint8_t*>(base + 0xc040);
            std::uint8_t* markersC =
                reinterpret_cast<std::uint8_t*>(base + 0xc440);
            std::uint8_t* markersD =
                reinterpret_cast<std::uint8_t*>(base + 0xc840);
            for (std::uint32_t i = keep; i < 256; ++i)
            {
                if (markersA[i] == 0
                    && markersB[i] == 0
                    && markersC[i] == 0
                    && markersD[i] == 0)
                {
                    break;
                }
                markersA[i] = 0;
                markersB[i] = 0;
                markersC[i] = 0;
                markersD[i] = 0;
            }

            count = keep;


            const std::uint32_t startCount = count;

            std::uint16_t origAdded[32] = {};
            std::uint8_t  origAddedCount = 0;
            for (std::uint32_t i = 0; i < startCount && origAddedCount < 32; ++i)
            {
                origAdded[origAddedCount++] =
                    *reinterpret_cast<std::uint16_t*>(base + 0x4440 + i * 2);
            }

            auto isAlreadyInList = [&](std::uint16_t equipId) -> bool {
                for (std::uint8_t k = 0; k < origAddedCount; ++k)
                    if (origAdded[k] == equipId) return true;
                return false;
            };

            const bool livePT_IsSnakeOrAvatar =
                   (livePT == outfit::kPlayerType_Snake)
                || (livePT == outfit::kPlayerType_Avatar);

            auto isOrigLateBandana = [&](std::uint16_t equipId) -> bool {
                if (!livePT_IsSnakeOrAvatar) return false;
                return equipId == 0x20Eu
                    || equipId == 0x20Fu;
            };

            for (std::uint8_t i = 0;
                 i < headCount
                 && i < outfit::kMaxHeadOptionsPerOutfit;
                 ++i)
            {
                const std::uint16_t equipId = headIds[i];
                if (equipId == 0) continue;
                if (count >= 32) break;
                if (isAlreadyInList(equipId)) continue;
                if (isOrigLateBandana(equipId)) continue;


                if (const auto* head =
                        outfit::TryGetCustomHeadByEquipId(equipId))
                {
                    if (head->flowIndex != 0
                        && !outfit::IsFlowIndexDevelopedByOrig(head->flowIndex))
                    {
                        continue;
                    }
                }

                const std::uint32_t addedIdx = count;
                g_AddListBandana(thisPtr, &count, equipId);


                if (origAddedCount < 32)
                    origAdded[origAddedCount++] = equipId;

                if (addedIdx < 32)
                {
                    *reinterpret_cast<std::uint8_t*>(base + 0xc840 + addedIdx) = 0xff;
                    *reinterpret_cast<std::uint8_t*>(base + 0x548 + addedIdx * 0xf) = 1;


                    const std::uint64_t cellOff = addedIdx * 0xb4;
                    *reinterpret_cast<std::uint32_t*>(base + 0xcc40 + cellOff) = 0;
                    *reinterpret_cast<std::uint32_t*>(base + 0xcc44 + cellOff) = 0xff;
                }
            }


            if (count != origCount)
            {
                *reinterpret_cast<std::uint32_t*>(base + 0x442c) = count;
                *reinterpret_cast<std::uint32_t*>(base + 0x104)  = count;
            }

#ifdef _DEBUG
            if (!g_HeadOptionInjectFirstFire.exchange(true))
            {
                Log("[OutfitListInject:HeadOption] FIRST INJECT: "
                    "partsType=0x%02X livePT=%u developId=%u declaredCount=%u "
                    "origCount=%u finalCount=%u — "
                    "committed to this[0x442c] and this[0x104]\n",
                    static_cast<unsigned>(pt),
                    static_cast<unsigned>(livePT),
                    static_cast<unsigned>(entry->developId),
                    static_cast<unsigned>(headCount),
                    static_cast<unsigned>(startCount),
                    static_cast<unsigned>(count));
            }
#endif
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("[OutfitListInject:HeadOption] inject faulted; "
                "partsType=0x%02X count-at-fault=%u\n",
                static_cast<unsigned>(pt),
                static_cast<unsigned>(count));
        }
    }

    static bool IsHeadOptionList(void* thisPtr)
    {
        if (!thisPtr) return false;
        __try
        {
            return *reinterpret_cast<std::uint32_t*>(
                reinterpret_cast<std::uintptr_t>(thisPtr) + 0x4434) == 0x201u;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static std::uint32_t __fastcall hkHeadBadgeCategory(void* self,
                                                        std::uint32_t equipId)
    {
        if (g_HeadBadgeBuildActive || g_HeadEquipDecideActive)
        {
            if (const auto* head = outfit::TryGetCustomHeadByEquipId(
                    static_cast<std::uint16_t>(equipId)))
                return head->slotByte;
        }
        return g_OrigHeadBadgeCategory
            ? g_OrigHeadBadgeCategory(self, equipId) : 0;
    }

    static std::uint8_t __fastcall hkWornHeadCategory(void* self)
    {
        if (g_HeadBadgeBuildActive)
        {
            const std::uint16_t worn = outfit::GetCurrentWornHeadEquipId();
            if (worn)
                if (const auto* head = outfit::TryGetCustomHeadByEquipId(worn))
                    return head->slotByte;
        }
        return g_OrigWornHeadCategory ? g_OrigWornHeadCategory(self) : 0;
    }

    static void __fastcall hkSetupPrefabListElement(void* thisPtr)
    {




        const bool prevBadge = g_HeadBadgeBuildActive;
        g_HeadBadgeBuildActive = IsHeadOptionList(thisPtr);

        const bool prev = t_InsideSetupPrefab;
        if (!prev) ResetAddedFlowIxBits();
        t_InsideSetupPrefab = true;
        if (g_OrigSetupPrefab) g_OrigSetupPrefab(thisPtr);
        t_InsideSetupPrefab = prev;

        if (!prev && g_HeadOptionInjectEnabled)
        {


            TryInjectHeadOptionList(thisPtr);
        }

        if (!prev && !g_HeadBadgeBuildActive)
        {
            __try
            {
                auto* base = reinterpret_cast<std::uint8_t*>(thisPtr);
                const std::uint8_t pt  = outfit::ReadLivePartsType();
                const std::uint8_t ppt = outfit::ReadLivePlayerType();
                const outfit::OutfitEntry* entry = nullptr;
                if (*reinterpret_cast<std::uint64_t*>(base + 0x461b8)
                        == 0xb8a0bf169f98ull
                    && pt >= outfit::kCustomPartsTypeStart
                    && pt <= outfit::kCustomPartsTypeEnd
                    && outfit::TryGetOutfitByPartsType(pt, &entry) && entry)
                {
                    int row = -1;
                    for (int r = 0; r < 64 && row < 0; ++r)
                        if (*reinterpret_cast<std::uint16_t*>(
                                base + 0x4440 + (r * 15) * 2) == entry->flowIndex)
                            row = r;

                    const std::uint8_t vcount = (ppt != 0xFF)
                        ? entry->GetVariantCountFor(ppt) : entry->variantCount;
                    if (vcount >= 2)
                    {
                        const std::uint8_t worn =
                            outfit::GetActiveVariant(entry->partsType);
                        if (row >= 0)
                        {
                            for (std::uint8_t v = 0; v < vcount && v < 15; ++v)
                            {
                                const std::size_t c =
                                    static_cast<std::size_t>(row) * 15 + v;
                                const bool on = (v == worn);
                                *(base + 0x425a4 + c) = on ? 1 : 0;
                                *(base + 0x548   + c) = on ? 0 : 1;
                            }
                        }
                    }
                    else if (row >= 0)
                    {
                        const std::size_t c = static_cast<std::size_t>(row) * 15;
                        *(base + 0x425a4 + c) = 1;
                        *(base + 0x548   + c) = 0;
                    }

                    if (void* scan = ResolveGameAddress(
                            gAddr.MissionPrep_SetInitialSelectRecord))
                    {
                        const std::uint32_t rowCount =
                            *reinterpret_cast<std::uint32_t*>(base + 0x442c);
                        reinterpret_cast<void(__fastcall*)(void*, std::uint32_t)>(
                            scan)(thisPtr, rowCount);
                    }
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }

        g_HeadBadgeBuildActive = prevBadge;
    }
}

namespace outfit
{

    void SetHeadEquipDecideActive(bool active)
    {
        g_HeadEquipDecideActive = active;
    }



    bool Install_OutfitListInject_Hook()
    {
        if (g_Installed) return true;

        void* target = ResolveGameAddress(
            gAddr.ItemSelectorCallbackImpl_SetupPrefabListElement);
        if (!target)
        {
            Log("[OutfitListInject] target unresolved; module disabled\n");
            return false;
        }


        g_AddListSuitAddr = ResolveGameAddress(gAddr.AddListSuit);
        bool addListSuitHooked = false;
        if (g_AddListSuitAddr)
        {
            addListSuitHooked = CreateAndEnableHook(
                g_AddListSuitAddr,
                reinterpret_cast<void*>(&hkAddListSuit),
                reinterpret_cast<void**>(&g_OrigAddListSuit));
        }

        const bool setupHooked = CreateAndEnableHook(
            target,
            reinterpret_cast<void*>(&hkSetupPrefabListElement),
            reinterpret_cast<void**>(&g_OrigSetupPrefab));
        g_Installed = setupHooked;


        if (void* addBandanaAddr = ResolveGameAddress(
                gAddr.ItemSelector_AddListBandana))
        {
            g_AddListBandana =
                reinterpret_cast<AddListBandana_t>(addBandanaAddr);
#ifdef _DEBUG
            Log("[OutfitListInject:HeadOption] AddListBandana resolved: "
                "%p — post-orig HEAD OPTION (equipKind=0x201) list "
                "injection enabled for custom outfits with HasHeadOptions()\n",
                addBandanaAddr);
#endif
        }
        else
        {
            Log("[OutfitListInject:HeadOption] AddListBandana unresolved; "
                "HEAD OPTION submenu will not be injected for custom "
                "outfits (JP build?)\n");
        }


        if (void* tCat = ResolveGameAddress(gAddr.EquipDevCtrl_GetHeadBadgeCategory))
        {
            g_InstalledHeadBadgeCategory = CreateAndEnableHook(
                tCat,
                reinterpret_cast<void*>(&hkHeadBadgeCategory),
                reinterpret_cast<void**>(&g_OrigHeadBadgeCategory));
        }
        if (void* tWorn = ResolveGameAddress(gAddr.MissionPrep_GetWornHeadCategory))
        {
            g_InstalledWornHeadCategory = CreateAndEnableHook(
                tWorn,
                reinterpret_cast<void*>(&hkWornHeadCategory),
                reinterpret_cast<void**>(&g_OrigWornHeadCategory));
        }
#ifdef _DEBUG
        Log("[OutfitListInject:HeadBadge] category-feed=%s worn-feed=%s\n",
            g_InstalledHeadBadgeCategory ? "OK" : "skip",
            g_InstalledWornHeadCategory  ? "OK" : "skip");
#endif




        if (void* urTarget = ResolveGameAddress(
                gAddr.ItemSelectorRecordCallFunc_UpdateRecords))
        {
            g_InstalledUpdateRecords = CreateAndEnableHook(
                urTarget,
                reinterpret_cast<void*>(&hkUpdateRecords),
                reinterpret_cast<void**>(&g_OrigUpdateRecords));
#ifdef _DEBUG
            Log("[OutfitListInject] UpdateRecords installed: %s "
                "(target=%p)\n",
                g_InstalledUpdateRecords ? "OK" : "FAIL", urTarget);
#endif
        }
        else
        {
            Log("[OutfitListInject] UpdateRecords target unresolved "
                "(JP build?) — variant cycle-button labels will fall "
                "back to vanilla hardcoded mapping\n");
        }

#ifdef _DEBUG
        Log("[OutfitListInject] installed: setup=%s addListSuit=%s "
            "(target=%p addListSuitAddr=%p)\n",
            setupHooked ? "OK" : "FAIL",
            addListSuitHooked ? "OK" : (g_AddListSuitAddr ? "FAIL" : "UNRESOLVED"),
            target, g_AddListSuitAddr);
#endif
        return g_Installed;
    }

    void Uninstall_OutfitListInject_Hook()
    {
        if (!g_Installed) return;



        if (g_AddListSuitAddr) DisableAndRemoveHook(g_AddListSuitAddr);
        g_AddListSuitAddr = nullptr;
        g_OrigAddListSuit = nullptr;

        if (g_InstalledUpdateRecords)
        {
            if (void* t = ResolveGameAddress(
                    gAddr.ItemSelectorRecordCallFunc_UpdateRecords))
                DisableAndRemoveHook(t);
            g_OrigUpdateRecords      = nullptr;
            g_InstalledUpdateRecords = false;
        }

        if (g_InstalledHeadBadgeCategory)
        {
            if (void* t = ResolveGameAddress(gAddr.EquipDevCtrl_GetHeadBadgeCategory))
                DisableAndRemoveHook(t);
            g_OrigHeadBadgeCategory      = nullptr;
            g_InstalledHeadBadgeCategory = false;
        }
        if (g_InstalledWornHeadCategory)
        {
            if (void* t = ResolveGameAddress(gAddr.MissionPrep_GetWornHeadCategory))
                DisableAndRemoveHook(t);
            g_OrigWornHeadCategory      = nullptr;
            g_InstalledWornHeadCategory = false;
        }
        g_HeadBadgeBuildActive = false;

        if (void* t = ResolveGameAddress(
                gAddr.ItemSelectorCallbackImpl_SetupPrefabListElement))
            DisableAndRemoveHook(t);

        g_OrigSetupPrefab  = nullptr;
        g_Installed        = false;

#ifdef _DEBUG
        Log("[OutfitListInject] removed\n");
#endif
    }
}
