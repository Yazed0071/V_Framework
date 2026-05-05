#include "pch.h"

#include "OutfitListInject.h"
#include "OutfitRegistry.h"
#include "OutfitEquippedState.h"
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
    using GetDevelopedCount_t      = std::uint16_t (__fastcall*)(void* sub);
    using FillDevelopedFlowIxs_t   = void  (__fastcall*)(void* sub,
                                                          std::uint16_t count,
                                                          std::uint16_t* outArr);
    using GetSuitInfoTable_t       = std::uint8_t* (__fastcall*)(void* sub58);
    using AddListSuit_t            = void  (__fastcall*)(
                                            void* thisPtr,
                                            std::uint32_t* rowCounter,
                                            std::uint16_t flowIndex,
                                            void* entryBuf);


    using AddListBandana_t = void (__fastcall*)(void* thisPtr,
                                                std::uint32_t* count,
                                                std::uint16_t equipId);
    static AddListBandana_t g_AddListBandana = nullptr;
    static std::atomic<bool> g_HeadOptionInjectFirstFire{ false };


    using UpdateRecords_t          = void  (__fastcall*)(void* thisPtr);

    static SetupPrefabListElement_t g_OrigSetupPrefab    = nullptr;
    static GetDevelopedCount_t      g_OrigGetCount       = nullptr;
    static FillDevelopedFlowIxs_t   g_OrigFill           = nullptr;
    static GetSuitInfoTable_t       g_OrigGetTable       = nullptr;


    static AddListSuit_t            g_OrigAddListSuit    = nullptr;

    static void*                    g_AddListSuitAddr    = nullptr;

    static UpdateRecords_t          g_OrigUpdateRecords     = nullptr;
    static bool                     g_InstalledUpdateRecords = false;

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


    static void*           g_GetCountFunc = nullptr;
    static void*           g_FillFunc     = nullptr;
    static void*           g_GetTableFunc = nullptr;
    static std::atomic_bool g_DeepHooksOK{false};
    static std::atomic_int  g_DeepInstallAttempts{0};
    static constexpr int    kMaxInstallAttempts = 16;


    constexpr std::size_t kVtblIx_GetCount = 0x230 / sizeof(void*);
    constexpr std::size_t kVtblIx_Fill     = 0x240 / sizeof(void*);
    constexpr std::size_t kVtblIx_GetTable = 0x718 / sizeof(void*);


    constexpr std::size_t kSuitInfoEntrySize = 0x68;


    constexpr std::size_t kProxyTableEntries = 0x400;
    constexpr std::size_t kVanillaCopyEntries = 0x300;
    constexpr std::size_t kProxyTableBytes   = kProxyTableEntries * kSuitInfoEntrySize;


    alignas(16) static std::uint8_t g_ExtendedTable[kProxyTableBytes] = {};
    static std::atomic_bool g_TableInitialized{false};


    static bool ShouldInjectOutfit(const outfit::OutfitEntry* e, std::uint8_t livePT)
    {
        if (!e) return false;
        if (!e->IsPlayerTypeSupported(livePT)) return false;
        if (e->flowIndex == 0 || e->flowIndex >= kProxyTableEntries) return false;
        if (!outfit::IsFlowIndexDevelopedByOrig(e->flowIndex)) return false;
        return true;
    }


    static std::uint16_t CountInjectionsForLivePT()
    {
        const std::uint8_t pt = outfit::ReadLivePlayerType();
        if (pt == 0xFF) return 0;
        const outfit::OutfitEntry* entries[outfit::kMaxOutfits] = {};
        const std::size_t n = outfit::GetAllOutfits(entries, outfit::kMaxOutfits);
        std::uint16_t count = 0;
        for (std::size_t i = 0; i < n; ++i)
        {
            if (ShouldInjectOutfit(entries[i], pt))
                ++count;
        }
        return count;
    }


    static void EnsureExtendedTable(const std::uint8_t* origTable)
    {
        if (!g_TableInitialized.load(std::memory_order_acquire))
        {
            __try
            {
                std::memcpy(g_ExtendedTable, origTable,
                            kVanillaCopyEntries * kSuitInfoEntrySize);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {


                std::memset(g_ExtendedTable, 0, kProxyTableBytes);
            }
            g_TableInitialized.store(true, std::memory_order_release);
        }


        const outfit::OutfitEntry* entries[outfit::kMaxOutfits] = {};
        const std::size_t n = outfit::GetAllOutfits(entries, outfit::kMaxOutfits);
        for (std::size_t i = 0; i < n; ++i)
        {
            if (!entries[i]) continue;
            const std::uint16_t fi = entries[i]->flowIndex;
            if (fi == 0 || fi >= kProxyTableEntries) continue;


            const std::size_t off = fi * kSuitInfoEntrySize;


            if (fi < kVanillaCopyEntries
             && g_ExtendedTable[off + 0x36] != 0) continue;
            g_ExtendedTable[off + 0x36] = 0x14;
            g_ExtendedTable[off + 0x37] = 0;
        }
    }


    static std::uint16_t __fastcall hkGetDevelopedCount(void* sub)
    {
        const std::uint16_t orig = g_OrigGetCount ? g_OrigGetCount(sub) : 0;
        if (!t_InsideSetupPrefab) return orig;
        return static_cast<std::uint16_t>(orig + CountInjectionsForLivePT());
    }

    static void __fastcall hkFillDevelopedFlowIxs(
        void* sub, std::uint16_t count, std::uint16_t* outArr)
    {
        if (!t_InsideSetupPrefab)
        {
            if (g_OrigFill) g_OrigFill(sub, count, outArr);
            return;
        }

        const std::uint16_t injCount = CountInjectionsForLivePT();
        const std::uint16_t origCount =
            (count >= injCount) ? static_cast<std::uint16_t>(count - injCount)
                                : count;

        if (g_OrigFill) g_OrigFill(sub, origCount, outArr);


        const std::uint8_t pt = outfit::ReadLivePlayerType();
        if (pt == 0xFF) return;

        const outfit::OutfitEntry* entries[outfit::kMaxOutfits] = {};
        const std::size_t n = outfit::GetAllOutfits(entries, outfit::kMaxOutfits);
        std::uint16_t writeIdx = origCount;
        for (std::size_t i = 0; i < n && writeIdx < count; ++i)
        {
            if (!ShouldInjectOutfit(entries[i], pt)) continue;
            outArr[writeIdx++] = entries[i]->flowIndex;
        }
    }

    static std::uint8_t* __fastcall hkGetSuitInfoTable(void* sub58)
    {
        std::uint8_t* origTable = g_OrigGetTable ? g_OrigGetTable(sub58) : nullptr;
        if (!origTable) return origTable;
        if (!t_InsideSetupPrefab) return origTable;

        EnsureExtendedTable(origTable);
        return g_ExtendedTable;
    }


    static void __fastcall hkAddListSuit(
        void* thisPtr,
        std::uint32_t* rowCounter,
        std::uint16_t flowIndex,
        void* entryBuf)
    {


        if (t_InsideSetupPrefab)
        {
            const outfit::OutfitEntry* entry = nullptr;
            if (outfit::TryGetOutfitByFlowIndex(flowIndex, &entry) && entry)
            {
                const std::uint8_t livePT = outfit::ReadLivePlayerType();
                if (livePT != 0xFF
                    && !entry->IsPlayerTypeSupported(livePT))
                {


                    Log("[OutfitListInject:AddListSuit] suppressed PT-unsupported "
                        "flowIndex=%u live-PT=%u (developId=%u partsType=0x%02X)\n",
                        static_cast<unsigned>(flowIndex),
                        static_cast<unsigned>(livePT),
                        static_cast<unsigned>(entry->developId),
                        static_cast<unsigned>(entry->partsType));
                    return;
                }
            }
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


        // Per-PT variant count: each playerType branch can have its own count.
        // The cycle button stops at the active branch's variantCount, even
        // though the outfit-level entry->variantCount may be higher.
        const std::uint8_t livePT = outfit::ReadLivePlayerType();
        const std::uint8_t variantsForPT = (livePT != 0xFF)
            ? entry->GetVariantCountFor(livePT)
            : entry->variantCount;

        if (variantsForPT < 2) return;

        __try
        {
            auto* base = reinterpret_cast<std::uint8_t*>(thisPtr);


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


                *(base + 0x548 + cellIndex) = 1;
            }


            *(base + 0xBC40 + row) = variantsForPT;


            std::uint8_t activeVar =
                outfit::GetActiveVariant(entry->partsType);
            if (activeVar >= variantsForPT)
                activeVar = static_cast<std::uint8_t>(variantsForPT - 1);
            *(base + 0xC040 + row) = activeVar;


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


    static bool TryInstallDeepHooks(void* thisPtr)
    {
        if (!thisPtr) return false;
        if (g_DeepHooksOK.load(std::memory_order_acquire)) return true;

        const int attempt =
            g_DeepInstallAttempts.fetch_add(1, std::memory_order_relaxed);
        if (attempt >= kMaxInstallAttempts)
        {

            return false;
        }

        auto* base = reinterpret_cast<std::uint8_t*>(thisPtr);


        void* sub50 = nullptr;
        __try { sub50 = *reinterpret_cast<void**>(base + 0x50); }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("[OutfitListInject:Deep] attempt=%d step1 SEH reading "
                "*(this+0x50)\n", attempt);
            return false;
        }
        if (!LooksLikeValidPtr(sub50))
        {
            Log("[OutfitListInject:Deep] attempt=%d step1 sub50=%p invalid\n",
                attempt, sub50);
            return false;
        }


        void* subAC8 = nullptr;
        __try
        {
            subAC8 = *reinterpret_cast<void**>(
                reinterpret_cast<std::uint8_t*>(sub50) + 0xAC8);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("[OutfitListInject:Deep] attempt=%d step2 SEH reading "
                "*(sub50+0xAC8); sub50=%p\n", attempt, sub50);
            return false;
        }
        if (!LooksLikeValidPtr(subAC8))
        {
            Log("[OutfitListInject:Deep] attempt=%d step2 subAC8=%p invalid "
                "(sub50=%p)\n", attempt, subAC8, sub50);
            return false;
        }


        void* getCount = nullptr;
        void* fill     = nullptr;
        __try
        {
            void** vtbl = *reinterpret_cast<void***>(subAC8);
            if (!LooksLikeValidPtr(vtbl))
            {
                Log("[OutfitListInject:Deep] attempt=%d step3 vtbl=%p "
                    "invalid (subAC8=%p)\n", attempt, vtbl, subAC8);
                return false;
            }
            getCount = vtbl[kVtblIx_GetCount];
            fill     = vtbl[kVtblIx_Fill];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("[OutfitListInject:Deep] attempt=%d step3 SEH reading "
                "vtable slots; subAC8=%p\n", attempt, subAC8);
            return false;
        }
        if (!LooksLikeValidPtr(getCount) || !LooksLikeValidPtr(fill))
        {
            Log("[OutfitListInject:Deep] attempt=%d step3 vtable slots "
                "invalid: getCount=%p fill=%p\n", attempt, getCount, fill);
            return false;
        }


        void* sub58 = nullptr;
        __try { sub58 = *reinterpret_cast<void**>(base + 0x58); }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("[OutfitListInject:Deep] attempt=%d step4a SEH reading "
                "*(this+0x58)\n", attempt);
            return false;
        }
        if (!LooksLikeValidPtr(sub58))
        {
            Log("[OutfitListInject:Deep] attempt=%d step4a sub58=%p invalid\n",
                attempt, sub58);
            return false;
        }
        void* getTable = nullptr;
        __try
        {
            void** vtbl = *reinterpret_cast<void***>(sub58);
            if (!LooksLikeValidPtr(vtbl))
            {
                Log("[OutfitListInject:Deep] attempt=%d step4b vtbl=%p "
                    "invalid (sub58=%p)\n", attempt, vtbl, sub58);
                return false;
            }
            getTable = vtbl[kVtblIx_GetTable];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("[OutfitListInject:Deep] attempt=%d step4b SEH reading "
                "vtable[0x718]; sub58=%p\n", attempt, sub58);
            return false;
        }
        if (!LooksLikeValidPtr(getTable))
        {
            Log("[OutfitListInject:Deep] attempt=%d step4b getTable=%p "
                "invalid\n", attempt, getTable);
            return false;
        }


        Log("[OutfitListInject:Deep] attempt=%d resolved: GetCount=%p "
            "Fill=%p GetTable=%p; installing\n",
            attempt, getCount, fill, getTable);

        const bool h1 = CreateAndEnableHook(
            getCount,
            reinterpret_cast<void*>(&hkGetDevelopedCount),
            reinterpret_cast<void**>(&g_OrigGetCount));
        const bool h2 = CreateAndEnableHook(
            fill,
            reinterpret_cast<void*>(&hkFillDevelopedFlowIxs),
            reinterpret_cast<void**>(&g_OrigFill));
        const bool h3 = CreateAndEnableHook(
            getTable,
            reinterpret_cast<void*>(&hkGetSuitInfoTable),
            reinterpret_cast<void**>(&g_OrigGetTable));

        Log("[OutfitListInject:Deep] attempt=%d hooks: GetCount=%s "
            "Fill=%s GetTable=%s\n",
            attempt,
            h1 ? "OK" : "FAIL",
            h2 ? "OK" : "FAIL",
            h3 ? "OK" : "FAIL");

        if (!(h1 && h2 && h3))
        {


            if (h1) DisableAndRemoveHook(getCount);
            if (h2) DisableAndRemoveHook(fill);
            if (h3) DisableAndRemoveHook(getTable);
            g_OrigGetCount = nullptr;
            g_OrigFill     = nullptr;
            g_OrigGetTable = nullptr;
            return false;
        }

        g_GetCountFunc        = getCount;
        g_FillFunc            = fill;
        g_GetTableFunc = getTable;
        g_DeepHooksOK.store(true, std::memory_order_release);
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


        // Resolve the head-option list for the LIVE playerType, with per-PT
        // override → outfit-level fallback inside GetHeadOptionsFor.
        const std::uint8_t      livePT  = outfit::ReadLivePlayerType();
        const std::uint16_t*    headIds = nullptr;
        std::uint8_t            headCount = 0;
        if (!entry->GetHeadOptionsFor(livePT, &headIds, &headCount)
            || headCount == 0 || !headIds)
            return;


        std::uint32_t count = 0;
        __try
        {
            const std::uint32_t origCount =
                *reinterpret_cast<std::uint32_t*>(base + 0x442c);


            std::uint8_t* markersA =
                reinterpret_cast<std::uint8_t*>(base + 0xbc40);
            std::uint8_t* markersB =
                reinterpret_cast<std::uint8_t*>(base + 0xc040);
            std::uint8_t* markersC =
                reinterpret_cast<std::uint8_t*>(base + 0xc440);
            std::uint8_t* markersD =
                reinterpret_cast<std::uint8_t*>(base + 0xc840);
            for (std::uint32_t i = origCount; i < 256; ++i)
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

            count = origCount;


            const std::uint32_t startCount = count;

            // Dynamically scan what orig already added to slots [0..count-1] —
            // this is exactly the right "vanilla skip set" for the LIVE PT,
            // covering Snake's BANDANA / INFINITE BANDANA, anyone's NONE, and
            // whatever BALACLAVA-family / SP-HEADGEAR / HP-HEADGEAR rows orig
            // populated based on R&D state. Hardcoding equipIds breaks the
            // moment a PT doesn't get them by default (e.g. DDFemale where
            // orig only adds NONE — a hardcoded skip of 0x210/0x211/0x212
            // would silently drop the user's whole list).
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

            // Snake and Avatar branches: orig adds BANDANA (0x20E) and
            // INFINITY BANDANA (0x20F) for these PTs through a code path
            // that runs after our hook returns, so the dynamic scan above
            // can't see them yet. Skip them preemptively in the user's
            // list — otherwise we'd produce visible duplicates because
            // orig adds its copies on top of ours after we've finished.
            // NONE (0x400) is added by orig in SetupPrefabListElement
            // before we run, so it shows up in origAdded[] correctly and
            // doesn't need a preemptive skip.
            const bool livePT_IsSnakeOrAvatar =
                   (livePT == outfit::kPlayerType_Snake)
                || (livePT == outfit::kPlayerType_Avatar);

            auto isOrigLateBandana = [&](std::uint16_t equipId) -> bool {
                if (!livePT_IsSnakeOrAvatar) return false;
                return equipId == 0x20Eu     // BANDANA
                    || equipId == 0x20Fu;    // INFINITY BANDANA
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


                // Track the just-added equipId in our scan set so a later
                // duplicate inside the same modder list is also skipped.
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


            if (count != startCount)
            {
                *reinterpret_cast<std::uint32_t*>(base + 0x442c) = count;
                *reinterpret_cast<std::uint32_t*>(base + 0x104)  = count;
            }

            // DIAG: dump per-row state so we can identify which marker byte
            // corresponds to the EQP chip. Open the head submenu twice — once
            // with NONE equipped, once with BALACLAVA — and compare row state
            // between the row that visually shows EQP and those that don't.
            // Fires every time the head submenu opens.
            Log("[HeadOption:DIAG] ====== dump (livePT=%u outfit_pt=0x%02X "
                "origCount=%u finalCount=%u) ======\n",
                static_cast<unsigned>(livePT),
                static_cast<unsigned>(pt),
                static_cast<unsigned>(startCount),
                static_cast<unsigned>(count));
            for (std::uint32_t i = 0; i < count && i < 32; ++i)
            {
                const std::uint16_t rowEquipId =
                    *reinterpret_cast<std::uint16_t*>(base + 0x4440 + i * 2);
                const std::uint8_t  bc40 =
                    *reinterpret_cast<std::uint8_t*>(base + 0xbc40 + i);
                const std::uint8_t  c040 =
                    *reinterpret_cast<std::uint8_t*>(base + 0xc040 + i);
                const std::uint8_t  c440 =
                    *reinterpret_cast<std::uint8_t*>(base + 0xc440 + i);
                const std::uint8_t  c840 =
                    *reinterpret_cast<std::uint8_t*>(base + 0xc840 + i);
                const std::uint8_t  cell548 =
                    *reinterpret_cast<std::uint8_t*>(base + 0x548 + i * 0xf);
                const std::uint32_t cellCC40 =
                    *reinterpret_cast<std::uint32_t*>(base + 0xcc40 + i * 0xb4);
                const std::uint32_t cellCC44 =
                    *reinterpret_cast<std::uint32_t*>(base + 0xcc44 + i * 0xb4);
                const char* origin = (i < startCount) ? "ORIG" : "OURS";
                Log("[HeadOption:DIAG] %s row=%2u equipId=0x%04X "
                    "bc40=0x%02X c040=0x%02X c440=0x%02X c840=0x%02X "
                    "cell548=0x%02X cellCC40=0x%08X cellCC44=0x%08X\n",
                    origin,
                    static_cast<unsigned>(i),
                    static_cast<unsigned>(rowEquipId),
                    static_cast<unsigned>(bc40),
                    static_cast<unsigned>(c040),
                    static_cast<unsigned>(c440),
                    static_cast<unsigned>(c840),
                    static_cast<unsigned>(cell548),
                    static_cast<unsigned>(cellCC40),
                    static_cast<unsigned>(cellCC44));
            }

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
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("[OutfitListInject:HeadOption] inject faulted; "
                "partsType=0x%02X count-at-fault=%u\n",
                static_cast<unsigned>(pt),
                static_cast<unsigned>(count));
        }
    }

    static void __fastcall hkSetupPrefabListElement(void* thisPtr)
    {


        if (!g_DeepHooksOK.load(std::memory_order_acquire))
        {
            (void)TryInstallDeepHooks(thisPtr);
        }


        const bool prev = t_InsideSetupPrefab;
        if (!prev) ResetAddedFlowIxBits();
        t_InsideSetupPrefab = true;
        if (g_OrigSetupPrefab) g_OrigSetupPrefab(thisPtr);
        t_InsideSetupPrefab = prev;

        if (!prev)
        {


            TryInjectHeadOptionList(thisPtr);
        }
    }
}

namespace outfit
{


    static bool TryInstallDeepHooksFromStaticAddresses()
    {
        if (g_DeepHooksOK.load(std::memory_order_acquire))
            return true;

        void* getCount = ResolveGameAddress(gAddr.SuitList_GetDevelopedCount);
        void* fill     = ResolveGameAddress(gAddr.SuitList_FillDevelopedFlowIxs);
        void* getTable = ResolveGameAddress(gAddr.SuitList_GetSuitInfoTable);

        if (!getCount || !fill || !getTable)
        {
            Log("[OutfitListInject:Deep] static-address install: one or "
                "more targets unresolved (GetCount=%p Fill=%p GetTable=%p) "
                "— falling back to deferred-on-first-fire path\n",
                getCount, fill, getTable);
            return false;
        }

        Log("[OutfitListInject:Deep] static-address install: GetCount=%p "
            "Fill=%p GetTable=%p; installing\n",
            getCount, fill, getTable);

        const bool h1 = CreateAndEnableHook(
            getCount,
            reinterpret_cast<void*>(&hkGetDevelopedCount),
            reinterpret_cast<void**>(&g_OrigGetCount));
        const bool h2 = CreateAndEnableHook(
            fill,
            reinterpret_cast<void*>(&hkFillDevelopedFlowIxs),
            reinterpret_cast<void**>(&g_OrigFill));
        const bool h3 = CreateAndEnableHook(
            getTable,
            reinterpret_cast<void*>(&hkGetSuitInfoTable),
            reinterpret_cast<void**>(&g_OrigGetTable));

        Log("[OutfitListInject:Deep] static-address hooks: GetCount=%s "
            "Fill=%s GetTable=%s\n",
            h1 ? "OK" : "FAIL",
            h2 ? "OK" : "FAIL",
            h3 ? "OK" : "FAIL");

        if (!(h1 && h2 && h3))
        {
            if (h1) DisableAndRemoveHook(getCount);
            if (h2) DisableAndRemoveHook(fill);
            if (h3) DisableAndRemoveHook(getTable);
            g_OrigGetCount = nullptr;
            g_OrigFill     = nullptr;
            g_OrigGetTable = nullptr;
            return false;
        }

        g_GetCountFunc = getCount;
        g_FillFunc     = fill;
        g_GetTableFunc = getTable;
        g_DeepHooksOK.store(true, std::memory_order_release);
        return true;
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
            Log("[OutfitListInject:HeadOption] AddListBandana resolved: "
                "%p — post-orig HEAD OPTION (equipKind=0x201) list "
                "injection enabled for custom outfits with HasHeadOptions()\n",
                addBandanaAddr);
        }
        else
        {
            Log("[OutfitListInject:HeadOption] AddListBandana unresolved; "
                "HEAD OPTION submenu will not be injected for custom "
                "outfits (JP build?)\n");
        }


        const bool deepStatic = TryInstallDeepHooksFromStaticAddresses();


        if (void* urTarget = ResolveGameAddress(
                gAddr.ItemSelectorRecordCallFunc_UpdateRecords))
        {
            g_InstalledUpdateRecords = CreateAndEnableHook(
                urTarget,
                reinterpret_cast<void*>(&hkUpdateRecords),
                reinterpret_cast<void**>(&g_OrigUpdateRecords));
            Log("[OutfitListInject] UpdateRecords installed: %s "
                "(target=%p)\n",
                g_InstalledUpdateRecords ? "OK" : "FAIL", urTarget);
        }
        else
        {
            Log("[OutfitListInject] UpdateRecords target unresolved "
                "(JP build?) — variant cycle-button labels will fall "
                "back to vanilla hardcoded mapping\n");
        }

        Log("[OutfitListInject] installed: setup=%s addListSuit=%s "
            "deepHooks=%s (target=%p addListSuitAddr=%p)\n",
            setupHooked ? "OK" : "FAIL",
            addListSuitHooked ? "OK" : (g_AddListSuitAddr ? "FAIL" : "UNRESOLVED"),
            deepStatic ? "static-OK" : "deferred-to-first-fire",
            target, g_AddListSuitAddr);
        return g_Installed;
    }

    void Uninstall_OutfitListInject_Hook()
    {
        if (!g_Installed) return;


        if (g_GetCountFunc) DisableAndRemoveHook(g_GetCountFunc);
        if (g_FillFunc)     DisableAndRemoveHook(g_FillFunc);
        if (g_GetTableFunc) DisableAndRemoveHook(g_GetTableFunc);
        g_GetCountFunc = nullptr;
        g_FillFunc     = nullptr;
        g_GetTableFunc = nullptr;
        g_OrigGetCount = nullptr;
        g_OrigFill     = nullptr;
        g_OrigGetTable = nullptr;
        g_DeepHooksOK.store(false, std::memory_order_release);
        g_TableInitialized.store(false, std::memory_order_release);

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

        if (void* t = ResolveGameAddress(
                gAddr.ItemSelectorCallbackImpl_SetupPrefabListElement))
            DisableAndRemoveHook(t);

        g_OrigSetupPrefab  = nullptr;
        g_Installed        = false;

        Log("[OutfitListInject] removed\n");
    }
}
