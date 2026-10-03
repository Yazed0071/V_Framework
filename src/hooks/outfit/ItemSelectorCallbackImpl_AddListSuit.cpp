#include "pch.h"

#include "ItemSelectorCallbackImpl_AddListSuit.h"
#include "OutfitRegistry.h"
#include "EquipDevelopControllerImpl_GetSuitDevelopInfoIndex.h"
#include "CustomHeadRegistry.h"
#include "MissionCodeGuard.h"
#include "UniqueCharacterOwnSuit.h"
#include "UniqueCharacterDefaultOutfit.h"
#include "../equip/DevelopArrayGrow.h"
#include "../equip/EquipDevelop_SetEquipUndeveloped.h"
#include "../equip/EquipDevelop_AddToEquipDevelopTable.h"
#include "../../core/V_FrameWorkState.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_set>

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

    struct NativeRowCell0
    {
        std::uint32_t color  = 0;
        std::uint32_t flags  = 0;
        std::uint8_t  enable = 0;
        bool          used   = false;
    };
    static NativeRowCell0 g_NativeRowCell0[equip::kMaxFlowSlots] = {};
    static std::unordered_set<std::uint16_t> g_RowWindowLogged;

    static void ResetNativeRowCell0()
    {
        std::memset(g_NativeRowCell0, 0, sizeof(g_NativeRowCell0));
    }

    using UpdateRecords_t          = void  (__fastcall*)(void* thisPtr);

    static SetupPrefabListElement_t g_OrigSetupPrefab    = nullptr;


    static AddListSuit_t            g_OrigAddListSuit    = nullptr;

    static void*                    g_AddListSuitAddr    = nullptr;

    static UpdateRecords_t          g_OrigUpdateRecords     = nullptr;
    static bool                     g_InstalledUpdateRecords = false;

    using AddRecord_t = void (__fastcall*)(void* thisPtr,
                                           std::uint16_t flowIndex,
                                           std::uint16_t* loadout,
                                           std::uint32_t row,
                                           std::uint32_t cell,
                                           std::uint8_t arg6,
                                           std::uint8_t arg7,
                                           std::uint8_t arg8);
    static AddRecord_t g_OrigAddRecord      = nullptr;
    static bool        g_InstalledAddRecord = false;


    using HeadBadgeCategory_t = std::uint32_t (__fastcall*)(void* self,
                                                            std::uint32_t equipId);
    static HeadBadgeCategory_t g_OrigHeadBadgeCategory = nullptr;
    static bool g_InstalledHeadBadgeCategory = false;
    static bool g_HeadBadgeBuildActive = false;

    static bool g_HeadEquipDecideActive = false;

    static bool       g_Installed       = false;


    thread_local bool t_InsideSetupPrefab = false;


    thread_local std::array<std::uint64_t,
                            (equip::kMaxFlowSlots + 63) / 64>
        t_AddedFlowIxBits = {};

    static bool TestAndSetAddedBit(std::uint16_t flowIndex)
    {
        if (flowIndex >= equip::kMaxFlowSlots) return false;
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

    struct VextCellInfo
    {
        std::uint64_t labelHash = 0;
        std::uint8_t  selector  = 0;
        bool          used      = false;
    };
    static VextCellInfo g_VextCellMap[equip::kMaxFlowSlots][15] = {};

    static void StoreVextCellLabel(std::uint16_t flowIndex, std::uint8_t cellPos,
                                   std::uint8_t selector, std::uint64_t labelHash)
    {
        if (flowIndex >= equip::kMaxFlowSlots || cellPos >= 15) return;
        g_VextCellMap[flowIndex][cellPos].labelHash = labelHash;
        g_VextCellMap[flowIndex][cellPos].selector  = selector;
        g_VextCellMap[flowIndex][cellPos].used       = true;
    }

    static std::uint64_t LookupVextCellLabel(std::uint16_t flowIndex,
                                             std::uint8_t cellPos)
    {
        if (flowIndex >= equip::kMaxFlowSlots || cellPos >= 15) return 0;
        const VextCellInfo& e = g_VextCellMap[flowIndex][cellPos];
        return e.used ? e.labelHash : 0;
    }

    static void SeedVextSwatchFlow(std::uint8_t selector, std::uint8_t sourceCamo)
    {
        if (selector < outfit::kCustomSelectorStart) return;
        using GetQuark_t = void* (__fastcall*)();
        auto getQuark = reinterpret_cast<GetQuark_t>(
            ResolveGameAddress(gAddr.GetQuarkSystemTable));
        if (!getQuark) return;
        __try
        {
            auto* quark = static_cast<std::uint8_t*>(getQuark());
            if (!quark) return;
            auto* app = *reinterpret_cast<std::uint8_t**>(quark + 0x98);
            if (!app) return;
            auto* tbl = *reinterpret_cast<std::uint8_t**>(app + 0x10);
            if (!tbl) return;
            auto* camoToFlow = reinterpret_cast<std::int16_t*>(tbl + 0x19ac);
            camoToFlow[selector] = camoToFlow[sourceCamo];
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }


    static constexpr int    kMaxInstallAttempts = 16;


    constexpr std::size_t kVtblIx_GetCount = 0x230 / sizeof(void*);
    constexpr std::size_t kVtblIx_Fill     = 0x240 / sizeof(void*);
    constexpr std::size_t kVtblIx_GetTable = 0x718 / sizeof(void*);






    static bool g_VariantInjectEnabled    = true;
    static bool g_HeadOptionInjectEnabled = true;

    static void StampVariantRowCells(std::uint8_t* base, std::uint32_t row,
                                     const outfit::OutfitEntry* entry,
                                     std::uint16_t flowIndex,
                                     std::uint8_t variantsForPT,
                                     std::uint8_t rowEnable)
    {
        if (variantsForPT > 15)
        {
            static std::atomic<int> s_varClamped{ 0 };
            if (s_varClamped.fetch_add(1, std::memory_order_relaxed) < 4)
                LogWarn("OutfitListInject: outfit has %u variants, row clamped to 15",
                    static_cast<unsigned>(variantsForPT));
            variantsForPT = 15;
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

            *(base + 0x548   + cellIndex) = rowEnable;
            *(base + 0x425a4 + cellIndex) = 0;
        }

        *(base + 0xBC40 + row) = variantsForPT;

        std::uint8_t wornPT = 0, wornSel = 0;
        const bool equipped =
            entry->bound
            && outfit::GetCurrentEquippedSuitBytes(&wornPT, &wornSel)
            && wornPT == entry->partsType;

        std::uint8_t displayVar;
        if (equipped)
        {
            displayVar = outfit::GetActiveVariant(entry->partsType);
        }
        else
        {
            displayVar = entry->defaultVariant;
            if (outfit::PeekCrateDeliveredDevelopId()
                    == entry->developId)
                displayVar = outfit::PeekCrateDeliveredVariantIdx();
            else if (outfit::PeekPendingSupplyDropDevelopId()
                     == entry->developId)
                displayVar = outfit::PeekPendingSupplyDropVariantIdx();
            outfit::SetActiveVariant(entry->partsType, displayVar);
        }
        if (displayVar >= variantsForPT)
            displayVar = static_cast<std::uint8_t>(variantsForPT - 1);
        *(base + 0xC040 + row) = displayVar;
        *(base + 0x425a4 + static_cast<std::size_t>(row) * 15 + displayVar) =
            equipped ? std::uint8_t{1} : std::uint8_t{0};
    }

    static bool IsWornCustomOutfit(const outfit::OutfitEntry* entry)
    {
        if (!entry || !entry->bound) return false;
        std::uint8_t wornPT = 0, wornSel = 0;
        return outfit::GetCurrentEquippedSuitBytes(&wornPT, &wornSel)
            && wornPT == entry->partsType;
    }

    static void __fastcall hkAddRecord(void* thisPtr,
                                       std::uint16_t flowIndex,
                                       std::uint16_t* loadout,
                                       std::uint32_t row,
                                       std::uint32_t cell,
                                       std::uint8_t arg6,
                                       std::uint8_t arg7,
                                       std::uint8_t arg8)
    {
        if (g_OrigAddRecord)
            g_OrigAddRecord(thisPtr, flowIndex, loadout, row, cell,
                            arg6, arg7, arg8);

        if (!thisPtr || row > outfit::kPanelRowMax || cell >= 15) return;

        __try
        {
            auto* base = reinterpret_cast<std::uint8_t*>(thisPtr);
            if (*reinterpret_cast<std::uint64_t*>(base + 0x461b8)
                    != 0xb8a0bf169f98ull)
                return;
            if (*reinterpret_cast<std::int32_t*>(base + 0x461b0) == 1) return;
            if (MissionCodeGuard::ShouldBypassHooks()) return;

            const std::uint8_t livePT = outfit::ReadLivePlayerType();
            const bool isDefaultRow =
                outfit::IsUniqueCharacterPlayerType(livePT)
                && uniquedefaultoutfit::IsDefaultOutfitRow(livePT, flowIndex);
            if (outfit::IsUniqueCharacterPlayerType(livePT)
                && (isDefaultRow
                    || uniqueownsuit::IsOwnSuitRow(livePT, flowIndex)))
            {
                std::uint8_t ownPT = 0, ownSel = 0;
                const bool haveWorn =
                    outfit::GetCurrentEquippedSuitBytes(&ownPT, &ownSel);
                bool worn = haveWorn && ownPT < outfit::kCustomPartsTypeStart;

                if (!worn && haveWorn && isDefaultRow)
                {
                    const outfit::OutfitEntry* def = nullptr;
                    if (outfit::TryGetOutfitByFlowIndex(flowIndex, &def) && def
                        && def->bound && def->partsType == ownPT)
                        worn = true;
                }

                *(base + 0x425a4 + static_cast<std::size_t>(row) * 15 + cell) =
                    worn ? std::uint8_t{1} : std::uint8_t{0};
                return;
            }

            const outfit::OutfitEntry* entry = nullptr;
            if (!outfit::TryGetOutfitByFlowIndex(flowIndex, &entry) || !entry)
                return;

            *(base + 0x425a4 + static_cast<std::size_t>(row) * 15 + cell) =
                IsWornCustomOutfit(entry) ? std::uint8_t{1} : std::uint8_t{0};
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            static std::atomic<bool> s_warned{ false };
            if (!s_warned.exchange(true))
                LogWarn("OutfitListInject: EQUIPPED mark write faulted, flowIndex=%u keeps vanilla mark",
                    static_cast<unsigned>(flowIndex));
        }
    }

    static void __fastcall hkAddListSuit(
        void* thisPtr,
        std::uint32_t* rowCounter,
        std::uint16_t flowIndex,
        void* entryBuf)
    {
        if (MissionCodeGuard::ShouldBypassHooks())
        {
            if (g_OrigAddListSuit)
                g_OrigAddListSuit(thisPtr, rowCounter, flowIndex, entryBuf);
            return;
        }

        if (outfit::IsCustomHeadEquipId(flowIndex))
            return;

        if (t_InsideSetupPrefab)
        {
            const outfit::OutfitEntry* entry = nullptr;
            if (outfit::TryGetOutfitByFlowIndex(flowIndex, &entry) && entry)
            {
                const std::uint8_t livePT = outfit::ReadLivePlayerType();
                if (livePT != 0xFF
                    && !entry->CanBeWornBy(livePT))
                    return;
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
        if (row > outfit::kPanelRowMax) return;

        const std::uint8_t livePT = outfit::ReadLivePlayerType();
        const outfit::OutfitEntry* entry = nullptr;
        bool isCustom =
            outfit::TryGetOutfitByFlowIndex(flowIndex, &entry) && entry;

        if (isCustom && entry && !entry->bound)
        {
            const std::uint8_t listVariants = (livePT != 0xFF)
                ? entry->GetVariantCountFor(livePT)
                : entry->variantCount;
            if (listVariants >= 2)
            {
                outfit::BindOutfitForVisibleRow(entry->developId);
                isCustom =
                    outfit::TryGetOutfitByFlowIndex(flowIndex, &entry) && entry;
            }
        }
        if (isCustom && entry)
            outfit::NoteOutfitMenuStamp(entry->developId);

        __try
        {
            auto* base = reinterpret_cast<std::uint8_t*>(thisPtr);

            if (*reinterpret_cast<std::uint64_t*>(base + 0x461b8)
                    != 0xb8a0bf169f98ull)
                return;

            if (isCustom && entry && flowIndex < equip::kMaxFlowSlots)
            {
                NativeRowCell0& snap = g_NativeRowCell0[flowIndex];
                if (!snap.used)
                {
                    const std::size_t c0 =
                        static_cast<std::size_t>(row) * 15;
                    snap.color  = *reinterpret_cast<std::uint32_t*>(
                        base + 0xCC40 + c0 * 12);
                    snap.flags  = *reinterpret_cast<std::uint32_t*>(
                        base + 0xCC40 + c0 * 12 + 4);
                    snap.enable = *(base + 0x548 + c0);
                    snap.used   = true;
                }
            }

            if (isCustom)
            {
                const std::uint8_t variantsForPT = (livePT != 0xFF)
                    ? entry->GetVariantCountFor(livePT)
                    : entry->variantCount;
                if (variantsForPT < 2)
                    return;

                if (!entry->bound)
                    return;

                const std::uint8_t rowEnable =
                    row <= 0x3F ? *(base + 0x548 + static_cast<std::size_t>(row) * 15) : 1;

                StampVariantRowCells(base, row, entry, flowIndex,
                                     variantsForPT, rowEnable);
                return;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            static std::atomic<bool> s_warned{ false };
            if (!s_warned.exchange(true))
                LogWarn("OutfitListInject: variant cell write faulted, flowIndex=%u row incomplete",
                    static_cast<unsigned>(flowIndex));
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

    using SetNodeTexture_t = void (__fastcall*)(void* manager,
                                                void* node,
                                                std::uint64_t texturePathHash,
                                                std::uint64_t slotNameHash);

    constexpr std::size_t kVtblSlot_PrepIsFobSortie   = 0x4F0 / 8;
    constexpr std::size_t kVtblSlot_DevIsFobAvailable = 0x478 / 8;
    constexpr std::size_t kVtblSlot_SetEquipNewDeveloped = 0x280 / 8;
    constexpr std::size_t kPanel_PendingNewMarkRow       = 0x40C4C;
    constexpr std::size_t kPanel_SelectorState           = 0x4428;
    constexpr std::size_t kVtblSlot_SetNodeTexture    = 0x518 / 8;
    constexpr std::uint64_t kIconTextureSlotHash      = 0xCAFB3BBF9889ull;

    using GetUixUtility_t = void** (__fastcall*)();
    static GetUixUtility_t g_GetUixUtility = nullptr;

    static void PrefetchIconTexture(std::uint64_t textureHash)
    {
        if (textureHash == 0 || gAddr.GetUixUtilityToFeedQuarkEnvironment == 0)
            return;
        if (!g_GetUixUtility)
            g_GetUixUtility = reinterpret_cast<GetUixUtility_t>(
                ResolveGameAddress(gAddr.GetUixUtilityToFeedQuarkEnvironment));
        if (!g_GetUixUtility) return;
        __try
        {
            void** util = g_GetUixUtility();
            if (!util) return;
            void** vtbl = *reinterpret_cast<void***>(util);
            if (!vtbl) return;
            auto fn = reinterpret_cast<void(__fastcall*)(void*, std::uint64_t, int)>(
                vtbl[0x548 / sizeof(void*)]);
            if (fn) fn(util, textureHash, 2);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    static bool IsPanelFobSortie(std::uint8_t* panel)
    {
        __try
        {
            void* sys = *reinterpret_cast<void**>(panel + 0x70);
            if (!sys) return false;
            using CtxFn_t = std::uint8_t (__fastcall*)(void*);
            auto ctx = reinterpret_cast<CtxFn_t>(
                (*reinterpret_cast<void***>(sys))[kVtblSlot_PrepIsFobSortie]);
            return ctx && ctx(sys) != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    static std::int32_t ReadCamoToFlow(std::uint8_t idx)
    {
        using GetQuark_t = void* (__fastcall*)();
        auto getQuark = reinterpret_cast<GetQuark_t>(
            ResolveGameAddress(gAddr.GetQuarkSystemTable));
        if (!getQuark) return -1;
        __try
        {
            auto* quark = static_cast<std::uint8_t*>(getQuark());
            if (!quark) return -1;
            auto* app = *reinterpret_cast<std::uint8_t**>(quark + 0x98);
            if (!app) return -1;
            auto* tbl = *reinterpret_cast<std::uint8_t**>(app + 0x10);
            if (!tbl) return -1;
            return *reinterpret_cast<std::int16_t*>(tbl + 0x19ac + idx * 2);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    }

    static void JitRefreshCustomRow(std::uint8_t* base, std::uint32_t row)
    {
        if (*reinterpret_cast<std::uint64_t*>(base + 0x461b8)
                != 0xb8a0bf169f98ull)
            return;
        const std::uint32_t equipKind =
            *reinterpret_cast<std::uint32_t*>(base + 0x4434);
        if (equipKind != 0x80u && equipKind != 0x100u)
            return;

        const std::uint16_t rowFlow = *reinterpret_cast<std::uint16_t*>(
            base + 0x4440 + static_cast<std::size_t>(row) * 15 * 2);
        if (rowFlow == 0 || rowFlow >= equip::kMaxFlowSlots)
            return;

        const outfit::OutfitEntry* entry = nullptr;
        if (!outfit::TryGetOutfitByFlowIndex(rowFlow, &entry) || !entry)
            return;

        if (IsPanelFobSortie(base))
            return;

        outfit::NoteOutfitRowRefresh(entry->developId);

        const std::uint8_t livePT = outfit::ReadLivePlayerType();
        const std::uint8_t variantsForPT = (livePT != 0xFF)
            ? entry->GetVariantCountFor(livePT)
            : entry->variantCount;
        if (variantsForPT < 2)
            return;

        const std::size_t c0 = static_cast<std::size_t>(row) * 15;

        if (!entry->bound)
        {
            if (!outfit::BindOutfitForVisibleRow(entry->developId))
            {
                const std::uint8_t count = *(base + 0xBC40 + row);
                const std::uint8_t sel0  = static_cast<std::uint8_t>(
                    *reinterpret_cast<std::uint32_t*>(
                        base + 0xCC40 + c0 * 12));
                const bool stampedStale =
                    count >= 2
                    || (sel0 >= outfit::kCustomSelectorStart
                        && sel0 <= outfit::kCustomSelectorEnd);
                if (!stampedStale)
                    return;

                const NativeRowCell0& snap = g_NativeRowCell0[rowFlow];
                if (!snap.used)
                {
                    if (g_RowWindowLogged.insert(entry->developId).second)
                        LogWarn("OutfitListInject: developId=%u has no native cell snapshot, stale swatches until rebuild",
                            static_cast<unsigned>(entry->developId));
                    return;
                }

                std::uint32_t color = snap.color;
                const std::uint8_t nb = static_cast<std::uint8_t>(color);
                if (nb >= outfit::kCustomSelectorStart
                    && nb <= outfit::kCustomSelectorEnd)
                    color &= 0xFFFFFF00u;

                std::uint8_t* cell = base + 0xCC40 + c0 * 12;
                *reinterpret_cast<std::uint32_t*>(cell + 0) = color;
                *reinterpret_cast<std::uint32_t*>(cell + 4) = snap.flags;
                *(cell + 8) = 0;
                *(base + 0x548 + c0) = snap.enable;
                for (std::uint8_t c = 0; c < 15; ++c)
                    *(base + 0x425a4 + c0 + c) = 0;
                for (std::uint8_t c = 1; c < 15; ++c)
                    *(base + 0x548 + c0 + c) = 0;
                *(base + 0xBC40 + row) = 1;
                *(base + 0xC040 + row) = 0;

                if (g_RowWindowLogged.insert(entry->developId).second)
                    LogWarn("OutfitListInject: developId=%u: no free selector byte for %u variants, shown single-variant",
                        static_cast<unsigned>(entry->developId),
                        static_cast<unsigned>(variantsForPT));
                return;
            }
            if (!outfit::TryGetOutfitByFlowIndex(rowFlow, &entry) || !entry
                || !entry->bound)
                return;
        }

        const std::uint8_t count = *(base + 0xBC40 + row);
        bool current = (count == variantsForPT);
        for (std::uint8_t v = 0; current && v < variantsForPT; ++v)
            current = static_cast<std::uint8_t>(
                          *reinterpret_cast<std::uint32_t*>(
                              base + 0xCC40 + (c0 + v) * 12))
                      == entry->variantSelectorCodes[v];
        if (current)
            return;

        const NativeRowCell0& snap = g_NativeRowCell0[rowFlow];
        const std::uint8_t rowEnable = snap.used
            ? snap.enable
            : (row <= 0x3F ? *(base + 0x548 + c0) : std::uint8_t{ 1 });
        StampVariantRowCells(base, row, entry, rowFlow, variantsForPT,
                             rowEnable);
    }

    static void __fastcall hkUpdateRecords(void* thisPtr)
    {
        if (thisPtr && !MissionCodeGuard::ShouldBypassHooks())
        {
            std::uint8_t seedSel[128];
            std::uint8_t seedSrc[128];
            const std::uint8_t seedCount =
                outfit::VanillaExtCollectSelectorSeeds(seedSel, seedSrc, 128);
            for (std::uint8_t i = 0; i < seedCount; ++i)
                SeedVextSwatchFlow(seedSel[i], seedSrc[i]);

            __try
            {
                auto* rb = reinterpret_cast<std::uint8_t*>(thisPtr);
                const std::uint32_t row =
                    *reinterpret_cast<std::uint32_t*>(rb + 0x008);
                const auto flowTable =
                    *reinterpret_cast<std::uint16_t* const*>(rb + 0x1E8);
                if (row <= outfit::kPanelRowMax && flowTable)
                {
                    auto* panel = reinterpret_cast<std::uint8_t*>(
                        reinterpret_cast<std::uintptr_t>(flowTable) - 0x4440);
                    JitRefreshCustomRow(panel, row);
                    {
                        const std::uint8_t ownPT = outfit::ReadLivePlayerType();
                        if (outfit::IsUniqueCharacterPlayerType(ownPT)
                            && uniqueownsuit::IsOwnSuitRow(
                                   ownPT,
                                   flowTable[static_cast<std::size_t>(row) * 15]))
                        {
                            if (*(panel + 0xBC40 + row) != 1)
                                *(panel + 0xBC40 + row) = 1;
                            if (*(panel + 0xC040 + row) != 0)
                                *(panel + 0xC040 + row) = 0;
                        }
                    }
                    for (std::uint8_t c = 0; c < 15; ++c)
                    {
                        auto* cell = panel + 0xCC40
                            + (static_cast<std::size_t>(row) * 15 + c) * 12;
                        const std::uint8_t camo = *cell;
                        if (camo < outfit::kCustomSelectorStart
                         || camo > outfit::kCustomSelectorEnd) continue;
                        std::uint8_t svpt = 0, svidx = 0;
                        if (!outfit::TryGetVanillaExtByVariantSelector(
                                camo, &svpt, &svidx)) continue;
                        const std::uint8_t src =
                            outfit::VanillaExtGetVariantSourceCamo(svpt, svidx);
                        if (src == 0xFF) continue;
                        *cell = src;
                    }
                    {
                        const std::uint16_t rowFlow =
                            flowTable[static_cast<std::size_t>(row) * 15];
                        bool isVextRow = false;
                        if (rowFlow < equip::kMaxFlowSlots)
                            for (std::uint8_t c = 0; c < 15 && !isVextRow; ++c)
                                if (outfit::VextLookupCellSelector(rowFlow, c)
                                        != 0)
                                    isVextRow = true;
                        if (isVextRow && !IsPanelFobSortie(panel))
                        {
                            const std::uint8_t rowCamo0 =
                                *(panel + 0xCC40
                                  + static_cast<std::size_t>(row) * 15 * 12);
                            const std::uint8_t fvpt =
                                outfit::ResolveVanillaPartsTypeForCamo(rowCamo0);
                            if (fvpt != 0xFF
                                && outfit::GetActiveVariant(fvpt) != 0)
                            {
                                const std::uint8_t factive =
                                    outfit::GetActiveVariant(fvpt);
                                const std::uint8_t fcnt =
                                    *(panel + 0xBC40 + row);
                                for (std::uint8_t c = 0;
                                     c < fcnt && c < 15; ++c)
                                {
                                    const std::size_t ci =
                                        static_cast<std::size_t>(row) * 15 + c;
                                    const std::uint8_t cSel =
                                        outfit::VextLookupCellSelector(
                                            rowFlow, c);
                                    std::uint8_t want = 1;
                                    if (cSel != 0)
                                    {
                                        std::uint8_t cvpt = 0, cvidx = 0;
                                        if (outfit::TryGetVanillaExtByVariantSelector(
                                                cSel, &cvpt, &cvidx)
                                            && cvpt == fvpt
                                            && cvidx == factive)
                                            want = 0;
                                    }
                                    if (*(panel + 0x548 + ci) != want)
                                        *(panel + 0x548 + ci) = want;
                                }
                            }
                            else if (fvpt != 0xFF)
                            {
                                const std::uint8_t fcnt =
                                    *(panel + 0xBC40 + row);
                                for (std::uint8_t c = 0;
                                     c < fcnt && c < 15; ++c)
                                {
                                    if (outfit::VextLookupCellSelector(
                                            rowFlow, c) == 0)
                                        continue;
                                    const std::size_t ci =
                                        static_cast<std::size_t>(row) * 15 + c;
                                    if (*(panel + 0x425a4 + ci) != 0)
                                        *(panel + 0x425a4 + ci) = 0;
                                    if (*(panel + 0x548 + ci) == 0)
                                        *(panel + 0x548 + ci) = 1;
                                }
                            }
                        }
                    }
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }

        if (g_OrigUpdateRecords) g_OrigUpdateRecords(thisPtr);

        if (!thisPtr) return;
        if (MissionCodeGuard::ShouldBypassHooks()) return;

        std::uint64_t variantHash = 0;
        std::uint64_t iconHash    = 0;
        std::uint8_t  variantIdx  = 0;
        std::uint16_t selectedId  = 0;

        __try
        {
            auto* base = reinterpret_cast<std::uint8_t*>(thisPtr);

            const std::uint32_t row = *reinterpret_cast<std::uint32_t*>(base + 0x008);
            if (row > outfit::kPanelRowMax) return;

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
            if (outfit::TryGetOutfitByFlowIndex(selectedId, &entry) && entry)
            {
                if (variantIdx >= outfit::kMaxVariantsPerOutfit)
                    return;

                const std::uint8_t livePT = outfit::ReadLivePlayerType();
                const std::uint8_t labelPT =
                    (livePT != 0xFF && entry->IsPlayerTypeSupported(livePT))
                        ? livePT
                        : outfit::kPlayerType_Snake;
                variantHash =
                    entry->GetVariantDisplayNameHash(labelPT, variantIdx);
                iconHash = entry->GetVariantIconPathHash(labelPT, variantIdx);
            }
            else
            {
                const std::uint8_t livePT = outfit::ReadLivePlayerType();
                if (!uniqueownsuit::TryGetLabel(livePT, selectedId,
                                                &variantHash, &iconHash))
                    variantHash = LookupVextCellLabel(selectedId, variantIdx);
            }
            if (variantHash == 0 && iconHash == 0) return;
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

            if (iconHash != 0)
            {
                void* iconNode1 = *reinterpret_cast<void**>(base + 0xA8);
                void* iconNode2 = *reinterpret_cast<void**>(base + 0xB0);
                auto setTex = reinterpret_cast<SetNodeTexture_t>(
                    managerVtable[kVtblSlot_SetNodeTexture]);
                if (setTex)
                {
                    PrefetchIconTexture(iconHash);
                    if (iconNode1)
                        setTex(manager, iconNode1, iconHash, kIconTextureSlotHash);
                    if (iconNode2)
                        setTex(manager, iconNode2, iconHash, kIconTextureSlotHash);
                }
            }

            if (variantHash == 0) return;

            auto getText  = reinterpret_cast<GetTextByHash_t>(
                managerVtable[0x750 / 8]);
            auto writeFn  = reinterpret_cast<WriteTextField_t>(
                managerVtable[0x708 / 8]);

            if (!getText || !writeFn) return;

            void* text = getText(manager, variantHash);
            if (!text) return;

            writeFn(manager, writeTarget1, writeTarget2, text);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            static std::atomic<bool> s_warned{ false };
            if (!s_warned.exchange(true))
                LogWarn("OutfitListInject: variant label write faulted, flowIndex=%u keeps vanilla label",
                    static_cast<unsigned>(selectedId));
        }
    }

    static bool TryInjectHeadOptionList(void* thisPtr)
    {
        outfit::DrainPendingHeads();
        if (!thisPtr || !g_AddListBandana) return false;

        const auto base = reinterpret_cast<std::uintptr_t>(thisPtr);

        std::uint32_t equipKind = 0;
        __try
        {
            equipKind = *reinterpret_cast<std::uint32_t*>(base + 0x4434);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        if (equipKind != 0x201) return false;

        const std::uint8_t pt     = outfit::ReadLivePartsType();
        const std::uint8_t livePT = outfit::ReadLivePlayerType();

        const outfit::OutfitEntry* entry = nullptr;
        const std::uint16_t*       headIds = nullptr;
        std::uint8_t               headCount = 0;
        bool                       isVanillaExt = false;
        std::uint16_t              vextHeadIdBuf[outfit::kMaxHeadOptionsPerOutfit] = {};

        if (pt >= outfit::kCustomPartsTypeStart && pt <= outfit::kCustomPartsTypeEnd)
        {
            if (!outfit::TryGetOutfitByPartsType(pt, &entry) || !entry)
                return false;
            const std::uint8_t variant = outfit::GetActiveVariant(pt);
            entry->GetHeadOptionsForVariant(livePT, variant, &headIds, &headCount);
        }
        else if (pt < outfit::kCustomPartsTypeStart)
        {
            if (!outfit::VanillaExtGetHeadOptions(
                    pt, livePT, outfit::ReadLiveSelectorCode(),
                    vextHeadIdBuf,
                    static_cast<std::uint8_t>(outfit::kMaxHeadOptionsPerOutfit),
                    &headCount))
                return false;
            headIds = vextHeadIdBuf;
            isVanillaExt = true;
        }
        else
        {
            return false;
        }
        if (headCount > 0 && !headIds) headCount = 0;


        std::uint32_t count = 0;
        bool listChanged = false;
        __try
        {
            if (*reinterpret_cast<std::uint64_t*>(base + 0x461b8)
                    != 0xb8a0bf169f98ull)
                return false;

            const std::uint32_t origCount =
                *reinterpret_cast<std::uint32_t*>(base + 0x442c);

            std::uint32_t keep = origCount;
            if (!isVanillaExt
                && origCount >= 1
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

            std::uint16_t origAdded[128] = {};
            std::uint8_t  origAddedCount = 0;
            for (std::uint32_t i = 0; i < startCount && origAddedCount < 128; ++i)
            {
                origAdded[origAddedCount++] =
                    *reinterpret_cast<std::uint16_t*>(base + 0x4440 + i * 0x1E);
            }

            auto isAlreadyInList = [&](std::uint16_t equipId) -> bool {
                for (std::uint8_t k = 0; k < origAddedCount; ++k)
                    if (origAdded[k] == equipId) return true;
                return false;
            };

            for (std::uint8_t i = 0;
                 i < headCount
                 && i < outfit::kMaxHeadOptionsPerOutfit;
                 ++i)
            {
                const std::uint16_t equipId = headIds[i];
                if (equipId == 0) continue;
                if (count >= 128) break;
                if (isAlreadyInList(equipId)) continue;


                if (const auto* head =
                        outfit::TryGetCustomHeadByEquipId(equipId))
                {
                    if (head->developId != 0
                        && V_FrameWorkState::IsExplicitlyUndevelopedByDevelopId(
                               head->developId))
                    {
                        continue;
                    }
                }

                const std::uint32_t addedIdx = count;
                g_AddListBandana(thisPtr, &count, equipId);


                if (origAddedCount < 128)
                    origAdded[origAddedCount++] = equipId;

                if (addedIdx < 128)
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
                listChanged = true;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            LogWarn("OutfitListInject: head option inject faulted, partsType=0x%02X list incomplete",
                static_cast<unsigned>(pt));
        }
        return listChanged;
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
        const outfit::CustomHeadEntry* byEquip =
            outfit::TryGetCustomHeadByEquipId(static_cast<std::uint16_t>(equipId));
        if (byEquip)
            return byEquip->slotByte;
        return g_OrigHeadBadgeCategory
            ? g_OrigHeadBadgeCategory(self, equipId) : 0;
    }

    static void ApplyFobAvailabilityToCustomRows(void* thisPtr)
    {
        __try
        {
            auto* base = reinterpret_cast<std::uint8_t*>(thisPtr);
            void* sys  = *reinterpret_cast<void**>(base + 0x70);
            void* ctrl = *reinterpret_cast<void**>(base + 0x58);
            if (!sys || !ctrl) return;

            using CtxFn_t = std::uint8_t (__fastcall*)(void*);
            using FobFn_t = std::uint8_t (__fastcall*)(void*, std::uint16_t);

            auto ctx = reinterpret_cast<CtxFn_t>(
                (*reinterpret_cast<void***>(sys))[kVtblSlot_PrepIsFobSortie]);
            if (!ctx || !ctx(sys)) return;

            auto fob = reinterpret_cast<FobFn_t>(
                (*reinterpret_cast<void***>(ctrl))[kVtblSlot_DevIsFobAvailable]);
            if (!fob) return;

            const std::uint32_t rows =
                *reinterpret_cast<std::uint32_t*>(base + 0x104);
            if (rows == 0) return;
            if (rows > outfit::kPanelRowMax + 1)
            {
                static std::atomic<int> s_rowsOob{ 0 };
                if (s_rowsOob.fetch_add(1, std::memory_order_relaxed) < 4)
                    LogWarn("OutfitListInject: %u rows past the %u-row window, FOB availability not applied",
                        rows, outfit::kPanelRowMax + 1);
                return;
            }

            for (std::uint32_t row = 0; row < rows; ++row)
            {
                std::uint8_t vars = *(base + 0xBC40 + row);
                if (vars == 0)  vars = 1;
                if (vars > 15)  vars = 15;
                for (std::uint8_t var = 0; var < vars; ++var)
                {
                    const std::size_t cell =
                        static_cast<std::size_t>(row) * 15 + var;
                    const std::uint16_t idx = *reinterpret_cast<std::uint16_t*>(
                        base + 0x4440 + cell * 2);
                    if (!EquipDevelopAdd::IsManagedFlowIndex(idx))
                        continue;
                    if (fob(ctrl, idx))
                        continue;
                    if (*(base + 0x548 + cell))
                        *(base + 0x548 + cell) = 0;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    using SetEquipNewDeveloped_t =
        void (__fastcall*)(void* controller, std::uint16_t index, std::uint8_t isNew);

    struct AbandonedNewMarkRow
    {
        void*                  controller = nullptr;
        SetEquipNewDeveloped_t setNew     = nullptr;
        std::uint16_t          cells[15]  = {};
        std::uint8_t           cellCount  = 0;
    };

    static bool ReadAbandonedNewMarkRow(void* thisPtr, AbandonedNewMarkRow& out)
    {
        __try
        {
            auto* base = reinterpret_cast<std::uint8_t*>(thisPtr);
            if (*reinterpret_cast<std::uint64_t*>(base + 0x461b8)
                    != 0xb8a0bf169f98ull)
                return false;

            const std::uint8_t selectorState = *(base + kPanel_SelectorState);
            if (selectorState != 0 && selectorState != 1)
                return false;

            auto* pendingRow = reinterpret_cast<std::int32_t*>(
                base + kPanel_PendingNewMarkRow);
            const std::int32_t row = *pendingRow;
            if (row < 0 || row > static_cast<std::int32_t>(outfit::kPanelRowMax))
                return false;
            *pendingRow = -1;

            void* ctrl = *reinterpret_cast<void**>(base + 0x58);
            if (!ctrl)
                return false;

            out.controller = ctrl;
            out.setNew = reinterpret_cast<SetEquipNewDeveloped_t>(
                (*reinterpret_cast<void***>(ctrl))[kVtblSlot_SetEquipNewDeveloped]);

            std::uint8_t count = *(base + 0xBC40 + static_cast<std::size_t>(row));
            if (count > 15)
                count = 15;
            for (std::uint8_t c = 0; c < count; ++c)
                out.cells[c] = *reinterpret_cast<std::uint16_t*>(
                    base + 0x4440 + (static_cast<std::size_t>(row) * 15 + c) * 2);
            out.cellCount = count;
            return out.setNew != nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static bool CallSetEquipNewDeveloped(const AbandonedNewMarkRow& row,
                                         std::uint16_t index)
    {
        __try
        {
            row.setNew(row.controller, index, 0);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static void ClearAbandonedNewMarkRow(void* thisPtr)
    {
        AbandonedNewMarkRow row{};
        if (!ReadAbandonedNewMarkRow(thisPtr, row))
            return;

        for (std::uint8_t c = 0; c < row.cellCount; ++c)
        {
            const std::uint16_t index = row.cells[c];
            if (!EquipDevelopAdd::IsManagedFlowIndex(index))
                continue;
            if (!CallSetEquipNewDeveloped(row, index))
            {
                LogWarn("OutfitListInject: NEW mark clear faulted, flowIndex=%u keeps NEW badge",
                    static_cast<unsigned>(index));
                return;
            }
        }
    }

    static void __fastcall hkSetupPrefabListElement(void* thisPtr)
    {
        if (MissionCodeGuard::ShouldBypassHooks())
        {
            EquipDevelop_BeginFobListSuppress();
            const bool prevFob = t_InsideSetupPrefab;
            t_InsideSetupPrefab = true;
            equip::BeginDevelopVisibilityCache();
            if (g_OrigSetupPrefab) g_OrigSetupPrefab(thisPtr);
            equip::EndDevelopVisibilityCache();
            t_InsideSetupPrefab = prevFob;
            EquipDevelop_EndFobListSuppress();
            ApplyFobAvailabilityToCustomRows(thisPtr);
            return;
        }

        const bool prevBadge = g_HeadBadgeBuildActive;
        g_HeadBadgeBuildActive = IsHeadOptionList(thisPtr);

        const bool prev = t_InsideSetupPrefab;
        if (!prev)
        {
            ClearAbandonedNewMarkRow(thisPtr);
            ResetAddedFlowIxBits();
            outfit::ClearOutfitMenuStamps();
            ResetNativeRowCell0();
        }
        t_InsideSetupPrefab = true;
        equip::BeginDevelopVisibilityCache();
        if (g_OrigSetupPrefab) g_OrigSetupPrefab(thisPtr);
        equip::EndDevelopVisibilityCache();
        t_InsideSetupPrefab = prev;

        if (!prev && g_VariantInjectEnabled && !g_HeadBadgeBuildActive)
        {
            __try
            {
                auto* base = reinterpret_cast<std::uint8_t*>(thisPtr);
                const std::uint32_t rowCount =
                    *reinterpret_cast<std::uint32_t*>(base + 0x442c);
                if (*reinterpret_cast<std::uint64_t*>(base + 0x461b8)
                        == 0xb8a0bf169f98ull
                    && rowCount != 0 && rowCount <= 0x40)
                {
                    const std::uint8_t livePT = outfit::ReadLivePlayerType();
                    const bool fobSortie = IsPanelFobSortie(base);
                    for (std::uint32_t row = 0;
                         !fobSortie && row < rowCount; ++row)
                    {
                        const std::size_t row0CellByte =
                            0xCC40 + (static_cast<std::size_t>(row) * 15) * 12;
                        const std::uint32_t colorCode0 =
                            *reinterpret_cast<std::uint32_t*>(base + row0CellByte);
                        const std::uint8_t rawCamo =
                            static_cast<std::uint8_t>(colorCode0 & 0xFF);
                        std::uint8_t rowCamo = rawCamo;
                        if (rowCamo >= outfit::kCustomSelectorStart)
                        {
                            std::uint8_t rvpt = 0, rvidx = 0;
                            if (outfit::TryGetVanillaExtByVariantSelector(
                                    rowCamo, &rvpt, &rvidx))
                                rowCamo = outfit::VanillaExtGetVariantSourceCamo(
                                              rvpt, rvidx);
                        }
                        const std::uint8_t vpt =
                            outfit::ResolveVanillaPartsTypeForCamo(rowCamo);
                        if (vpt == 0xFF) continue;
                        const std::uint8_t slotCount =
                            outfit::VanillaExtVariantSlotCount(vpt);
                        if (slotCount == 0) continue;
                        const std::uint8_t nativeCount = *(base + 0xBC40 + row);
                        if (nativeCount == 0 || nativeCount >= 15) continue;

                        const std::uint16_t flowIndex =
                            *reinterpret_cast<std::uint16_t*>(
                                base + 0x4440
                                + (static_cast<std::size_t>(row) * 15) * 2);

                        if (rawCamo >= outfit::kCustomSelectorStart)
                            *reinterpret_cast<std::uint32_t*>(base + row0CellByte) =
                                (colorCode0 & 0xFFFFFF00u) | rowCamo;

                        if (flowIndex < equip::kMaxFlowSlots)
                            for (std::uint8_t p = 0; p < 15; ++p)
                                g_VextCellMap[flowIndex][p] = VextCellInfo{};

                        const std::uint8_t activeVar = outfit::GetActiveVariant(vpt);
                        const std::uint8_t liveDonor =
                            outfit::VanillaExtResolveVariantDonor(vpt, livePT);
                        std::uint8_t appended   = nativeCount;
                        int          activeCell = -1;
                        for (std::uint8_t v = 1;
                             v <= slotCount && appended < 15; ++v)
                        {
                            if (outfit::VanillaExtGetVariantSourceCamo(vpt, v)
                                    != rowCamo)
                                continue;
                            const outfit::VanillaSuitVariantAsset* vasset =
                                (liveDonor == 0xFF)
                                ? nullptr
                                : outfit::VanillaExtGetVariant(vpt, liveDonor, v);
                            if (!vasset) continue;
                            const std::uint8_t sel =
                                outfit::VanillaExtGetVariantSelector(vpt, v);
                            if (sel == 0) continue;

                            const std::size_t cellIndex =
                                static_cast<std::size_t>(row) * 15 + appended;
                            *reinterpret_cast<std::uint16_t*>(
                                base + 0x4440 + cellIndex * 2) = flowIndex;

                            std::uint8_t* cell = base + 0xCC40 + cellIndex * 12;
                            *reinterpret_cast<std::uint32_t*>(cell + 0) = rowCamo;
                            *reinterpret_cast<std::uint32_t*>(cell + 4) = 0;
                            *(cell + 8) = 0;

                            const bool activeThis = (activeVar == v);
                            *(base + 0x548 + cellIndex) =
                                activeThis ? std::uint8_t{0} : std::uint8_t{1};
                            *(base + 0x425a4 + cellIndex) = activeThis ? 1 : 0;
                            if (activeThis) activeCell = static_cast<int>(appended);
                            StoreVextCellLabel(flowIndex,
                                               static_cast<std::uint8_t>(appended),
                                               sel, vasset->displayNameHash);
                            SeedVextSwatchFlow(sel, rowCamo);
                            ++appended;
                        }

                        if (appended == nativeCount) continue;

                        *(base + 0xBC40 + row) = appended;
                        if (activeVar != 0)
                            for (std::uint8_t nc = 0; nc < nativeCount; ++nc)
                            {
                                *(base + 0x425a4
                                  + static_cast<std::size_t>(row) * 15 + nc) = 0;
                                *(base + 0x548
                                  + static_cast<std::size_t>(row) * 15 + nc) = 1;
                            }
                        if (activeCell >= 0)
                            *(base + 0xC040 + row) =
                                static_cast<std::uint8_t>(activeCell);
                    }
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                LogWarn("OutfitListInject: ExtendVanillaOutfit variant pass faulted, variant cells incomplete");
            }
        }

        bool headRowsChanged = false;
        if (!prev && g_HeadOptionInjectEnabled)
        {
            if (!IsPanelFobSortie(reinterpret_cast<std::uint8_t*>(thisPtr)))
                headRowsChanged = TryInjectHeadOptionList(thisPtr);
        }

        if (!prev && g_HeadBadgeBuildActive)
        {
            __try
            {
                auto* base = reinterpret_cast<std::uint8_t*>(thisPtr);
                if (*reinterpret_cast<std::uint64_t*>(base + 0x461b8)
                        == 0xb8a0bf169f98ull)
                {
                    auto validSlot = [](std::uint8_t s) {
                        return s >= outfit::kCustomHeadSlotBase
                            && outfit::IsCustomHeadSlot(s);
                    };
                    const std::uint8_t srcTracker = outfit::GetWornCustomHeadSlot();
                    const std::uint16_t srcCat = outfit::ReadLiveWornHeadCategory();
                    std::uint8_t wornSlot = 0;
                    if (validSlot(srcTracker))
                        wornSlot = srcTracker;
                    else if (validSlot(static_cast<std::uint8_t>(srcCat)))
                        wornSlot = static_cast<std::uint8_t>(srcCat);
                    const outfit::CustomHeadEntry* worn =
                        validSlot(wornSlot)
                            ? outfit::TryGetCustomHeadBySlot(wornSlot)
                            : nullptr;

                    std::uint16_t wornEquipId = worn ? worn->equipId : 0;
                    if (wornEquipId == 0 && srcCat >= 1 && srcCat <= 5)
                        wornEquipId = static_cast<std::uint16_t>(0x20D + srcCat);
                    const std::uint32_t rowCount =
                        *reinterpret_cast<std::uint32_t*>(base + 0x442c);

                    int targetRow = -1;
                    if (wornEquipId != 0)
                    {
                        for (std::uint32_t r = 0; r < rowCount && r < 64; ++r)
                            if (*reinterpret_cast<std::uint16_t*>(
                                    base + 0x4440 + r * 0x1e) == wornEquipId)
                            { targetRow = static_cast<int>(r); break; }
                    }

                    if (targetRow >= 0)
                    {
                        for (std::uint32_t r = 0; r < rowCount && r < 64; ++r)
                            *(base + 0x425a4 + r * 0xf) =
                                (static_cast<int>(r) == targetRow) ? 1 : 0;
                    }

                    if (targetRow >= 0 || headRowsChanged)
                    {
                        if (void* scan = ResolveGameAddress(
                                gAddr.MissionPrep_SetInitialSelectRecord))
                            reinterpret_cast<void(__fastcall*)(
                                void*, std::uint32_t)>(scan)(thisPtr, rowCount);
                    }
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
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
                    const std::uint32_t scanRows = std::min<std::uint32_t>(
                        *reinterpret_cast<std::uint32_t*>(base + 0x442c),
                        outfit::kPanelRowMax + 1u);

                    int row = -1;
                    for (std::uint32_t r = 0; r < scanRows && row < 0; ++r)
                        if (*reinterpret_cast<std::uint16_t*>(
                                base + 0x4440
                                + (static_cast<std::size_t>(r) * 15) * 2)
                            == entry->flowIndex)
                            row = static_cast<int>(r);

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

        if (!prev)
            ApplyFobAvailabilityToCustomRows(thisPtr);
    }
}

namespace outfit
{

    void SetHeadEquipDecideActive(bool active)
    {
        g_HeadEquipDecideActive = active;
    }

    std::uint8_t VextLookupCellSelector(std::uint16_t flowIndex,
                                        std::uint8_t cellPos)
    {
        if (flowIndex >= equip::kMaxFlowSlots || cellPos >= 15) return 0;
        const VextCellInfo& e = g_VextCellMap[flowIndex][cellPos];
        return e.used ? e.selector : 0;
    }



    bool Install_OutfitListInject_Hook()
    {
        if (g_Installed) return true;

        void* target = ResolveGameAddress(
            gAddr.ItemSelectorCallbackImpl_SetupPrefabListElement);
        if (!target)
        {
            LogError("OutfitListInject: SetupPrefabListElement address missing, custom outfit rows off");
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
        }
        else
        {
            LogError("OutfitListInject: AddListBandana address missing, custom head options not listed");
        }


        if (void* tCat = ResolveGameAddress(gAddr.EquipDevCtrl_GetHeadBadgeCategory))
        {
            g_InstalledHeadBadgeCategory = CreateAndEnableHook(
                tCat,
                reinterpret_cast<void*>(&hkHeadBadgeCategory),
                reinterpret_cast<void**>(&g_OrigHeadBadgeCategory));
        }
        if (!g_InstalledHeadBadgeCategory)
            LogError("OutfitListInject: GetHeadBadgeCategory not hooked, custom head badges vanilla");

        if (void* arTarget = ResolveGameAddress(
                gAddr.ItemSelectorCallbackImpl_AddRecord))
        {
            g_InstalledAddRecord = CreateAndEnableHook(
                arTarget,
                reinterpret_cast<void*>(&hkAddRecord),
                reinterpret_cast<void**>(&g_OrigAddRecord));
            if (!g_InstalledAddRecord)
                LogError("OutfitListInject: AddRecord hook failed, EQUIPPED mark wrong on custom rows");
        }
        else
        {
            LogError("OutfitListInject: AddRecord address missing, EQUIPPED mark wrong on custom rows");
        }

        if (void* urTarget = ResolveGameAddress(
                gAddr.ItemSelectorRecordCallFunc_UpdateRecords))
        {
            g_InstalledUpdateRecords = CreateAndEnableHook(
                urTarget,
                reinterpret_cast<void*>(&hkUpdateRecords),
                reinterpret_cast<void**>(&g_OrigUpdateRecords));
            if (!g_InstalledUpdateRecords)
                LogError("OutfitListInject: UpdateRecords hook failed, variant labels stay vanilla");
        }
        else
        {
            LogError("OutfitListInject: UpdateRecords address missing, variant labels stay vanilla");
        }

        if (!setupHooked)
            LogError("OutfitListInject: SetupPrefabListElement hook failed, custom outfit rows off");
        if (!g_AddListSuitAddr)
            LogError("OutfitListInject: AddListSuit address missing, outfit variant rows off");
        else if (!addListSuitHooked)
            LogError("OutfitListInject: AddListSuit hook failed, outfit variant rows off");
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

        if (g_InstalledAddRecord)
        {
            if (void* t = ResolveGameAddress(
                    gAddr.ItemSelectorCallbackImpl_AddRecord))
                DisableAndRemoveHook(t);
            g_OrigAddRecord      = nullptr;
            g_InstalledAddRecord = false;
        }

        if (g_InstalledHeadBadgeCategory)
        {
            if (void* t = ResolveGameAddress(gAddr.EquipDevCtrl_GetHeadBadgeCategory))
                DisableAndRemoveHook(t);
            g_OrigHeadBadgeCategory      = nullptr;
            g_InstalledHeadBadgeCategory = false;
        }
        g_HeadBadgeBuildActive = false;

        if (void* t = ResolveGameAddress(
                gAddr.ItemSelectorCallbackImpl_SetupPrefabListElement))
            DisableAndRemoveHook(t);

        g_OrigSetupPrefab  = nullptr;
        g_Installed        = false;
    }
}
