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
    // ===================================================================
    //  STRATEGY (rewritten 2026-04-26 after substitute approach failed)
    // -------------------------------------------------------------------
    // Earlier strategies all tried to call AddListSuit cold from a post-
    // orig hook. Both the direct call (with custom flowIndex 922) and
    // the substitute call (with vanilla flowIndex 519, click-target
    // patched after) faulted inside AddListSuit because the orig has
    // setup state (counter scope, entryBuf, table-driven preconditions)
    // that a post-orig hook cannot satisfy.
    //
    // The orig SetupPrefabListElement (mgsvtpp.exe_Addresses.txt:15790388)
    // builds its iteration array via two vtable methods on a sub-system
    // pointer at *(ItemSelectorCallbackImpl + 0x50)->[0xAC8]:
    //
    //   vtable[0x230](sub) -> u16 count    (returns # of developed
    //                                       items, populates internal
    //                                       state for vtable[0x240])
    //   vtable[0x240](sub, count, outArr)  (fills outArr with `count`
    //                                       u16 flowIndices)
    //
    // After populating, orig walks outArr and for each entry checks
    // a per-flowIndex byte in a table returned by *(this+0x58)
    // ->vtable[0x718]. The check is:
    //   table[flowIndex*0x68 + 0x36] == 0x14   (suit category)
    //   table[flowIndex*0x68 + 0x37] != '['    (not a stub-row marker)
    //
    // Custom outfits with auto-allocated flowIndex >=768 don't have
    // table entries → category check OOBs (or reads garbage).
    //
    // Real fix: use the orig's natural pipeline by intercepting these
    // four vtable methods. Then AddListSuit is called by the orig
    // with proper counter scope, entryBuf, and (now) valid table data
    // — same path vanilla suits take.
    //
    // 1. Hook vtable[0x230](sub) → return origCount + N (N = our
    //    custom outfits matching the live playerType).
    // 2. Hook vtable[0x240](sub, count, outArr) → call orig with
    //    origCount, then append our flowIndices in slots
    //    outArr[origCount..origCount+N).
    // 3. Hook vtable[0x718](sub58) → return our extended suit-info
    //    table (orig data + our entries marked as suit category 0x14
    //    for the orig walker's check to pass).
    //
    // Vtable function pointers are unknown at static-init time. We
    // capture them on the first SetupPrefabListElement fire (it gives
    // us the instance, which gives us the vtables) and MinHook the
    // captured function addresses from there.
    //
    // To prevent the count/fill hooks from affecting unrelated callers
    // of those vtable methods, we gate the inflation on a thread-
    // local flag set/cleared inside hkSetupPrefabListElement.
    //
    // -------------------------------------------------------------------
    //  DUPLICATE-WALKER PROBLEM (discovered 2026-04-26)
    // -------------------------------------------------------------------
    // SetupPrefabListElement contains TWO walkers over local_838 that
    // both call AddListSuit on suit-category entries:
    //
    //   Walker A (mgsvtpp.exe.c:2956611, inside case 0x80) — runs when
    //            *(int*)(this+0x4430) == 0. Iterates count entries,
    //            calls AddListSuit on each that passes
    //              [0x36]==0x14 && [0x37]!='[' && quark[0x618] != 0
    //
    //   Walker B (mgsvtpp.exe.c:2956778, in post-switch IVar8==0x14
    //            branch) — runs unconditionally when this[0x4438]==0x14.
    //            Same predicate, same array.
    //
    // For vanilla suits, AddListSuit's internal early-return at
    // mgsvtpp.exe.c:2950131 (vtable[0x488] on sub58) suppresses the
    // second-pass duplicates. Our custom flowIndices return 0 from
    // vtable[0x488] (the per-flowIndex table entry is sparse / OOB),
    // so neither walker is suppressed → the row appears twice.
    //
    // Fix: hook AddListSuit and dedup per-Setup-scope. A 1024-bit
    // bitset (one bit per possible flowIndex) tracks whether an entry
    // was already added in this scope. Second invocation for the same
    // flowIndex returns immediately without calling orig — counter
    // doesn't advance, no cell write, no duplicate.
    //
    // -------------------------------------------------------------------
    //  REVERTED 2026-04-27 — vtable[0x1A0] hook removed
    // -------------------------------------------------------------------
    // I previously hooked vtable[0x1A0] to return 0x400 for our custom
    // flowIndices, hoping to suppress AddListWeapon's fold path. It did
    // NOT fix the FROGS-loads-Jill issue and INTRODUCED a regression:
    // by giving our outfits clean cell rows, the hook created valid
    // fold targets for OTHER custom-developId equips (like a custom AK-12
    // assault rifle the user had registered via AddToEquipDevelopTable).
    // AK-12's vtable[0x1A0] OOB-read garbage happened to point at our
    // outfit's flowIndex; AddListWeaponRevised then folded AK-12 as a
    // variant slot of the outfit's row, making AK-12 appear in the
    // UNIFORMS panel under one of the suits with mismatched display data.
    //
    // The actual "FROGS UI loaded Jill" issue is a display/cell
    // mismatch caused by the orig sorting entries ascending by flowIndex
    // for the visible row layout while our walker writes cells in a
    // different order. That needs a different fix (sort our injection
    // ascending) rather than vtable[0x1A0] meddling.
    // ===================================================================

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

    // ItemSelectorCallbackImpl::AddListBandana body (retail 0x14A53C210).
    // Three-arg signature: (this, uint *count, ushort equipId). Called
    // via raw function-pointer (no MinHook trampoline) post-orig of
    // SetupPrefabListElement to inject custom head-option entries.
    using AddListBandana_t = void (__fastcall*)(void* thisPtr,
                                                std::uint32_t* count,
                                                std::uint16_t equipId);
    static AddListBandana_t g_AddListBandana = nullptr;
    static std::atomic<bool> g_HeadOptionInjectFirstFire{ false };

    // ItemSelectorRecordCallFunc::UpdateRecords (retail 0x1416AF270).
    // Refreshes the visible elements of the focused suit row including
    // the variant cycle-button label. We hook it to overwrite the
    // label with our outfit's per-variant displayName hash post-orig.
    using UpdateRecords_t          = void  (__fastcall*)(void* thisPtr);

    static SetupPrefabListElement_t g_OrigSetupPrefab    = nullptr;
    static GetDevelopedCount_t      g_OrigGetCount       = nullptr;
    static FillDevelopedFlowIxs_t   g_OrigFill           = nullptr;
    static GetSuitInfoTable_t       g_OrigGetTable       = nullptr;
    // AddListSuit is hooked for per-scope dedup (see the duplicate-
    // walker comment above). g_OrigAddListSuit is the trampoline.
    static AddListSuit_t            g_OrigAddListSuit    = nullptr;
    // Resolved address; cached for uninstall.
    static void*                    g_AddListSuitAddr    = nullptr;

    static UpdateRecords_t          g_OrigUpdateRecords     = nullptr;
    static bool                     g_InstalledUpdateRecords = false;

    static bool       g_Installed       = false;

    // Thread-local flag: set while hkSetupPrefabListElement is on the
    // call stack, so hkGetCount/hkFill only inflate the count/array
    // when called from the panel-build path (not from unrelated game
    // code that might share these vtable methods).
    thread_local bool t_InsideSetupPrefab = false;

    // Per-Setup-scope bitset of flowIndices we've already passed to
    // AddListSuit. Reset on top-level hkSetupPrefabListElement entry.
    // 1024 bits matches kProxyTableEntries — every value our injection
    // can produce fits.
    thread_local std::array<std::uint64_t, 16> t_AddedFlowIxBits = {};

    static bool TestAndSetAddedBit(std::uint16_t flowIndex)
    {
        if (flowIndex >= 1024) return false;
        auto& word = t_AddedFlowIxBits[flowIndex >> 6];
        const std::uint64_t mask = 1ull << (flowIndex & 63);
        if (word & mask) return true;        // already added this scope
        word |= mask;
        return false;
    }

    static void ResetAddedFlowIxBits()
    {
        t_AddedFlowIxBits.fill(0);
    }

    // Captured function addresses for the deep hooks. Resolved on the
    // first SetupPrefabListElement call where the vtable chain is
    // valid. Some panel-open paths fire SetupPrefabListElement with
    // *(this+0x50) pointing to a partially-initialized subsystem;
    // we retry on every fire until the resolution succeeds, then
    // latch g_DeepHooksOK and stop trying.
    static void*           g_GetCountFunc = nullptr;
    static void*           g_FillFunc     = nullptr;
    static void*           g_GetTableFunc = nullptr;
    static std::atomic_bool g_DeepHooksOK{false};
    static std::atomic_int  g_DeepInstallAttempts{0};
    static constexpr int    kMaxInstallAttempts = 16;

    // Vtable indices (offsets in bytes / 8).
    constexpr std::size_t kVtblIx_GetCount = 0x230 / sizeof(void*);
    constexpr std::size_t kVtblIx_Fill     = 0x240 / sizeof(void*);
    constexpr std::size_t kVtblIx_GetTable = 0x718 / sizeof(void*);

    // Per-flowIndex entry size in the suit-info table. Verified from
    // the orig walker: `table[flowIndex*0x68 + 0x36] == 0x14`.
    constexpr std::size_t kSuitInfoEntrySize = 0x68;
    // Size we'll proxy. The orig table's actual size is unknown at
    // build time, but vanilla flowIndices top out around 758, so 0x300
    // entries (768) covers all vanilla data with margin. Our extension
    // beyond that is for custom outfits.
    constexpr std::size_t kProxyTableEntries = 0x400;  // 1024
    constexpr std::size_t kVanillaCopyEntries = 0x300; // 768
    constexpr std::size_t kProxyTableBytes   = kProxyTableEntries * kSuitInfoEntrySize;

    // Our extended suit-info table. Initialized once with vanilla data
    // copied from the orig table; overlaid with category=0x14 markers
    // for our custom flowIndices. Returned from hkGetTable.
    alignas(16) static std::uint8_t g_ExtendedTable[kProxyTableBytes] = {};
    static std::atomic_bool g_TableInitialized{false};

    // Helper: should this outfit be injected into the panel right now?
    // - playerType must match live character.
    // - flowIndex must be in the proxy-table range.
    // - **MUST be developed in the orig R&D bit-array.** Undeveloped
    //   outfits are hidden from panels until the player researches
    //   them in MotherBase R&D, mirroring vanilla equip flow. (Added
    //   2026-04-27 — previously injected unconditionally, which made
    //   undeveloped outfits appear and triggered downstream variant-
    //   fold pathologies when their sparse table entries collided
    //   with other custom developIds.)
    static bool ShouldInjectOutfit(const outfit::OutfitEntry* e, std::uint8_t livePT)
    {
        if (!e) return false;
        if (e->playerType != livePT) return false;
        if (e->flowIndex == 0 || e->flowIndex >= kProxyTableEntries) return false;
        if (!outfit::IsFlowIndexDevelopedByOrig(e->flowIndex)) return false;
        return true;
    }

    // Helper: count outfits we'll inject for the current live playerType.
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

    // Initialize g_ExtendedTable with orig data + our entries.
    // Re-applies our overlay on every call so newly-registered outfits
    // appear without needing a full reset.
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
                // Even copying 768 entries OOB-read is unlikely (vanilla
                // tables are typically larger), but guard anyway.
                std::memset(g_ExtendedTable, 0, kProxyTableBytes);
            }
            g_TableInitialized.store(true, std::memory_order_release);
        }

        // Overlay our outfits' table entries: mark as suit category.
        const outfit::OutfitEntry* entries[outfit::kMaxOutfits] = {};
        const std::size_t n = outfit::GetAllOutfits(entries, outfit::kMaxOutfits);
        for (std::size_t i = 0; i < n; ++i)
        {
            if (!entries[i]) continue;
            const std::uint16_t fi = entries[i]->flowIndex;
            if (fi == 0 || fi >= kProxyTableEntries) continue;
            // Only overlay if NOT already vanilla data — preserve any
            // vanilla entry that happens to live at this flowIndex.
            // (Auto-allocator picks 922+ to avoid this.)
            const std::size_t off = fi * kSuitInfoEntrySize;
            // Check vanilla copy: if [0x36] is already non-zero, the
            // entry is real vanilla data — don't stomp it.
            if (fi < kVanillaCopyEntries
             && g_ExtendedTable[off + 0x36] != 0) continue;
            g_ExtendedTable[off + 0x36] = 0x14; // suit category
            g_ExtendedTable[off + 0x37] = 0;    // not stub-row marker
        }
    }

    // ---- Hook bodies ----
    //
    // GATE RESTORED 2026-04-28 (after a brief experiment removing it
    // crashed the game at retail 0x141B82653 — a different caller of
    // these same vtable functions iterates flowIndices and dereferences
    // a parallel table at [RBP+0x20] with 8-byte stride. Vanilla flow-
    // Indices have valid pointers there; our custom flowIndices >768
    // point to NULL → null-deref of [NULL+0xB8] on first frame post-
    // SetSuit. The gate is required: the crashing caller doesn't see
    // our custom flowIndices, vanilla list only). Re-enabling.
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

        // Append our custom flowIndices in the trailing slots.
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

    // ---- AddListSuit dedup hook ----
    // Inside one SetupPrefabListElement call, the orig walks local_838
    // TWICE for case 0x80 — once in the case body (gated on
    // *(int*)(this+0x4430) == 0) and once in the post-switch IVar8==0x14
    // branch. Vanilla suits are deduped by AddListSuit's internal
    // vtable[0x488] early-return; our custom flowIndices are not. We
    // dedup ourselves so the second walker pass is a no-op for any
    // flowIndex already added by the first.
    static void __fastcall hkAddListSuit(
        void* thisPtr,
        std::uint32_t* rowCounter,
        std::uint16_t flowIndex,
        void* entryBuf)
    {
        // Defense-in-depth PT filter for OUR custom outfits.
        // `ShouldInjectOutfit` (called from hkFillDevelopedFlowIxs +
        // CountInjectionsForLivePT) is supposed to keep PT-mismatched
        // custom outfits out of local_838 in the first place. But the
        // orig SetupPrefabListElement also reads Quark state[0xFB]
        // and calls vtable[0x618](this+0x58, livePT, flowIndex) per
        // suit-row — for our custom flowIndices the vtable handler
        // doesn't have internal data and tends to return truthy by
        // default, so the orig DOESN'T filter them out, and they
        // reach AddListSuit anyway.
        //
        // We catch them here: if `flowIndex` matches one of our
        // registered outfits AND that outfit's `playerType` doesn't
        // match the live PT, suppress the orig call. This guarantees
        // the row never appears in the panel regardless of which
        // upstream path put it in local_838.
        //
        // For vanilla flowIndices (`TryGetOutfitByFlowIndex` returns
        // false) we fall through unchanged — vanilla AddListSuit's
        // own filtering handles them.
        if (t_InsideSetupPrefab)
        {
            const outfit::OutfitEntry* entry = nullptr;
            if (outfit::TryGetOutfitByFlowIndex(flowIndex, &entry) && entry)
            {
                const std::uint8_t livePT = outfit::ReadLivePlayerType();
                if (livePT != 0xFF && entry->playerType != livePT)
                {
                    // Wrong PT for this character — drop the row.
                    // Orig is NOT called, so the row counter doesn't
                    // advance and no cell is written. Effectively the
                    // outfit is invisible in the panel.
                    Log("[OutfitListInject:AddListSuit] suppressed PT-mismatch "
                        "flowIndex=%u outfit-PT=%u live-PT=%u "
                        "(developId=%u partsType=0x%02X)\n",
                        static_cast<unsigned>(flowIndex),
                        static_cast<unsigned>(entry->playerType),
                        static_cast<unsigned>(livePT),
                        static_cast<unsigned>(entry->developId),
                        static_cast<unsigned>(entry->partsType));
                    return;
                }
            }
        }

        if (t_InsideSetupPrefab && TestAndSetAddedBit(flowIndex))
        {
            // Already added this scope — skip orig entirely so the
            // counter doesn't advance and no cell write happens.
            return;
        }

        // Snapshot row index pre-orig so we know which row this call
        // is going to populate. Orig increments *param_2 at end, so
        // the just-added row is at *param_2 (pre-orig) == *rowCounter
        // (now). We re-read post-orig to confirm the increment fired
        // (orig may early-return for unknown flowIndices).
        const std::uint32_t rowPre =
            (rowCounter ? *rowCounter : 0xFFFFFFFFu);

        if (g_OrigAddListSuit)
            g_OrigAddListSuit(thisPtr, rowCounter, flowIndex, entryBuf);

        // Post-orig: if this is one of our registered outfits AND it
        // has more than one variant, inject additional cells into the
        // panel row so the UNIFORMS variant cycle button can move
        // between them like vanilla camo variants.
        //
        // Vanilla cell layout (verified from AddListSuit + AddListWeaponInner
        // at named-build line 2950302):
        //   self+0x4440+(row*15+var)*2  = u16 selectedId  (our flowIndex)
        //   self+0xCC40+(row*15+var)*12 = u32 selector(low byte)+flags+u8
        //   self+0xC040+row             = u8 current variant index (cycle)
        //   self+0xBC40+row             = u8 variant count for this row
        //   self+0xC440+row             = bool flag (param_6 from inner)
        //   self+0xC840+row             = byte flag (param_7 from inner)
        //   self+0x548+(row*15+var)     = byte (developed/owned)
        //   self+0x425A4+(row*15+var)   = byte (some skill/equip-list flag)
        //
        // We only inject when variantCount >= 2 (the orig's single cell
        // already covers the base for variant-less outfits).
        if (!thisPtr || !rowCounter) return;
        const std::uint32_t rowPost = *rowCounter;
        if (rowPost == rowPre)
        {
            // Orig didn't advance — flowIndex was rejected or routed to
            // weapon-list. Nothing to inject.
            return;
        }
        const std::uint32_t row = rowPost - 1;
        if (row > 0x3F) return;  // panel max ~64 rows

        const outfit::OutfitEntry* entry = nullptr;
        if (!outfit::TryGetOutfitByFlowIndex(flowIndex, &entry) || !entry)
            return;

        if (entry->variantCount < 2) return;  // no extras to inject

        __try
        {
            auto* base = reinterpret_cast<std::uint8_t*>(thisPtr);

            // Write cells (row, 0..variantCount-1). The orig already
            // wrote cell (row, 0); we re-write it for consistency
            // (same selectedId + base selector — should be a no-op
            // unless orig used a different code) and add 1..N-1.
            for (std::uint8_t var = 0; var < entry->variantCount; ++var)
            {
                const std::size_t cellIndex =
                    static_cast<std::size_t>(row) * 15 + var;

                // selectedId — same flowIndex for every variant cell
                // so the ItemSelector treats them as variants of one
                // suit row (vanilla pattern: TEU has multiple cells
                // with selectedId=697, different selectorCodes).
                *reinterpret_cast<std::uint16_t*>(
                    base + 0x4440 + cellIndex * 2) = flowIndex;

                // selectorCode cell (12 bytes): low byte of u32 +
                // type flag u32 + status byte. We use:
                //   [0] = our per-variant selectorCode
                //   [4] = type flag — orig uses 7 for base, 0/1 for
                //         additional variants. Mirror that pattern.
                //   [8] = unused / 0
                std::uint8_t* cell = base + 0xCC40 + cellIndex * 12;
                *reinterpret_cast<std::uint32_t*>(cell + 0) =
                    static_cast<std::uint32_t>(
                        entry->variantSelectorCodes[var]);
                *reinterpret_cast<std::uint32_t*>(cell + 4) =
                    (var == 0) ? 7u : 0u;
                *(cell + 8) = 0;

                // Mark this cell as developed/owned so the panel
                // doesn't gray it out. (+0x548 byte = 1 from vanilla
                // SUIT-path AddListWeaponInner.)
                *(base + 0x548 + cellIndex) = 1;
            }

            // Variant count for this row — drives the cycle button's
            // wrap-around. variantCount cells starting at index 0.
            *(base + 0xBC40 + row) =
                static_cast<std::uint8_t>(entry->variantCount);

            // Initial variant byte = the persisted active variant for
            // this outfit (set by OutfitCommit when the user last
            // committed a cycle, or by V_FrameWork.SetOutfitVariant
            // from Lua). Reopening UNIFORMS shows the cell at the
            // last-picked variant rather than always snapping back to
            // base — vanilla TEU works the same way (the panel
            // remembers which camo you had selected last time).
            //
            // Clamp defensively in case GetActiveVariant returned a
            // stale value beyond this outfit's current variantCount
            // (shouldn't happen — SetActiveVariant clamps internally
            // — but a registry reset / re-registration could leave
            // an inconsistency).
            std::uint8_t activeVar =
                outfit::GetActiveVariant(entry->partsType);
            if (activeVar >= entry->variantCount)
                activeVar = static_cast<std::uint8_t>(entry->variantCount - 1);
            *(base + 0xC040 + row) = activeVar;

            // Build the variant-selector list as a single string so the
            // log is independent of kMaxVariantsPerOutfit's value.
            char variantBuf[16 * 4 + 1] = {};
            {
                std::size_t pos = 0;
                for (std::size_t i = 0;
                     i < outfit::kMaxVariantsPerOutfit && pos + 4 < sizeof(variantBuf);
                     ++i)
                {
                    pos += static_cast<std::size_t>(std::snprintf(
                        variantBuf + pos, sizeof(variantBuf) - pos,
                        (i == 0) ? "%02X" : ",%02X",
                        static_cast<unsigned>(entry->variantSelectorCodes[i])));
                }
            }

            Log("[OutfitListInject:AddListSuit] post-orig variant cell "
                "injection: flowIndex=%u developId=%u row=%u "
                "variantCount=%u selectors=[%s]\n",
                static_cast<unsigned>(flowIndex),
                static_cast<unsigned>(entry->developId),
                static_cast<unsigned>(row),
                static_cast<unsigned>(entry->variantCount),
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

    // ---- One-shot deep-hook installer ----

    // Best-effort plausibility check for a captured pointer. Filters
    // obvious garbage so we don't deref into invalid memory.
    static bool LooksLikeValidPtr(const void* p)
    {
        const std::uintptr_t v = reinterpret_cast<std::uintptr_t>(p);
        if (v == 0) return false;
        if (v < 0x10000) return false;          // null page region
        if ((v & 0x7) != 0) return false;        // qword-aligned
        // x64 user-space addresses are typically below 0x7FFFFFFFFFFF.
        if (v >= 0x800000000000ull) return false;
        return true;
    }

    // Try to resolve and install the deep hooks. Returns true if
    // hooks installed successfully (in which case g_DeepHooksOK is
    // set). Returns false on any failure — caller may retry on a
    // later SetupPrefabListElement fire. Each step is wrapped in
    // its own SEH so we can pinpoint exactly which read faulted.
    static bool TryInstallDeepHooks(void* thisPtr)
    {
        if (!thisPtr) return false;
        if (g_DeepHooksOK.load(std::memory_order_acquire)) return true;

        const int attempt =
            g_DeepInstallAttempts.fetch_add(1, std::memory_order_relaxed);
        if (attempt >= kMaxInstallAttempts)
        {
            // Stop trying after kMax attempts to avoid log spam.
            return false;
        }

        auto* base = reinterpret_cast<std::uint8_t*>(thisPtr);

        // Step 1: read *(this+0x50)
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

        // Step 2: read *(sub50 + 0xAC8)
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

        // Step 3: read vtable of subAC8 and slots [0x230] [0x240]
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

        // Step 4: read *(this+0x58) and its vtable[0x718]
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

        // Step 5: install the three MinHooks
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
            // Roll back any successful hooks so we don't leave a
            // partial state behind.
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

    // ---- UpdateRecords hook (variant cycle-button label override) ----
    //
    // Vanilla `tpp::ui::menu::impl::ItemSelectorRecordCallFunc::UpdateRecords`
    // (retail 0x1416AF270, mgsvtpp.exe.c:2958850) refreshes the focused
    // row's visible elements. The cycle-button label specifically goes
    // through this path (mgsvtpp.exe.c:2959408-2959431):
    //
    //   iVar17 = *(int *)(*(this + 0x208) + 4 + cellIndex*0xc);
    //   switch (iVar17) {
    //       case 0:  uVar8 = 0x83c1bd133b29;  // suit_type_normal "STANDARD"
    //       case 1:  uVar8 = 0xfa0cacdc17d3;  // suit_type_scarf  "SCARF"
    //       case 7:  uVar8 = 0xc26636a539c1;  // suit_type_naked  "NAKED"
    //       default: (no label drawn)
    //   }
    //   lVar11 = *(*(this + 0x38));
    //   text = vtable[0x750](*(this+0x38), uVar8);
    //   vtable[0x708](*(this+0x38), *(this+0x180), *(this+0x80), text);
    //
    // Our hook runs orig first (lets it write its label or skip), then
    // post-orig: read the focused row's selectedId from
    // `*(this + 0x1e8) + cellIndex*2` (the u16 selectedId table the
    // orig uses earlier in the same function). If selectedId matches
    // a registered custom outfit AND we have a non-zero displayName
    // hash for the current variant, manually call the same vtable
    // chain with our hash to overwrite whatever orig wrote.
    //
    // Field offsets (verified from line 2959408+):
    //   *(this + 0x008) = u32 row index (focused row)
    //   *(this + 0x038) = manager pointer (for vtable[0x750]/[0x708])
    //   *(this + 0x080) = text-write target #2
    //   *(this + 0x180) = text-write target #1
    //   *(this + 0x1e8) = u16 selectedId table base
    //   *(this + 0x1f0) = u8  variant byte table base
    //   *(this + 0x208) = u8  cell base (12-byte stride per cell)

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
            if (row > 0x3F) return;  // sanity, panel max ~64 rows

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

            // Look up our outfit by flowIndex (the cell's selectedId).
            // If it's not ours OR we have no displayName override for
            // this variant, leave the orig label alone.
            const outfit::OutfitEntry* entry = nullptr;
            if (!outfit::TryGetOutfitByFlowIndex(selectedId, &entry) || !entry)
                return;

            if (variantIdx >= outfit::kMaxVariantsPerOutfit)
                return;

            // Read out of variantDisplayNameHashes regardless of the
            // current variantCount — modder may have set the base
            // (idx 0) only and still want the cycle to use it for
            // variant 0 reads.
            variantHash = entry->variantDisplayNameHashes[variantIdx];

            // Throttled "we matched our outfit" log so we can confirm
            // the hook is firing for the right row even when the user
            // hasn't set displayName fields yet (variantHash == 0).
            // Logs once per (selectedId, variantIdx, hash) tuple change,
            // so opening the panel produces a couple of lines and then
            // goes quiet.
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

        // Override the label by re-running the same vtable chain orig
        // uses — vtable[0x750] resolves the hash to a text pointer,
        // vtable[0x708] writes that text to the cycle-button UI element.
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

            // Throttle log: only emit when the resolved (selectedId,
            // variantIdx, hash) tuple changes — UpdateRecords runs
            // per-frame for the focused row, would flood otherwise.
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

    // ---- Top-level SetupPrefabListElement hook ----

    // Post-orig HEAD OPTION list injection. When equipKind=0x201
    // (HEAD OPTION submenu) and the live outfit is a registered custom
    // with non-empty headOptionEquipIds[], the orig's branch at retail
    // 2956068+ adds only the NONE entry (count=1) because the orig's
    // category-detection path (vtable[0x418/0x420/...] on this+0x70)
    // doesn't recognize our custom suit equipId — so the modder-supplied
    // headOptions never reach the UI.
    //
    // Fix: post-orig, find the current count by scanning this[0xbc40+i]
    // for the first 0 terminator, then call the orig AddListBandana for
    // each headOptionEquipId. AddListBandana writes one entry to
    // this+0x4440+idx*0x1e plus four parallel markers and increments
    // *count, exactly as if the orig had emitted it.
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
        if (!outfit::TryGetOutfitByPartsType(pt, &entry) || !entry
            || !entry->HasHeadOptions())
            return;

        // Read orig's authoritative count from this[0x442c]. The orig
        // commits the final count there at LAB_1416aa5f0 (retail
        // mgsvtpp.exe.c:2956937) BEFORE we run. For HEAD OPTION this is
        // usually 1 (just NONE).
        //
        // We CAN'T scan this[0xbc40+i] for the first 0 — the entry
        // buffer is shared with other equipKind passes (UNIFORMS uses
        // it too with many entries) and the orig HEAD OPTION case only
        // writes slot 0, leaving stale markers from a prior panel pass
        // intact. Without this fix, going UNIFORMS -> HEAD OPTION shows
        // the prior UNIFORMS list rendered as head-option items.
        std::uint32_t count = 0;
        __try
        {
            const std::uint32_t origCount =
                *reinterpret_cast<std::uint32_t*>(base + 0x442c);

            // Clear stale markers / parallel arrays from any prior pass
            // beyond origCount. The orig HEAD OPTION branch never zeros
            // them, so they linger and confuse downstream count logic.
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
                    break;  // already terminated past here
                }
                markersA[i] = 0;
                markersB[i] = 0;
                markersC[i] = 0;
                markersD[i] = 0;
            }

            count = origCount;

            // Dedup: if orig already added NONE at slot 0, don't add it
            // again from our list.
            const std::uint16_t firstSlotEquip =
                (count > 0)
                    ? *reinterpret_cast<std::uint16_t*>(base + 0x4440)
                    : 0;
            const bool origAddedNone =
                (count > 0 && firstSlotEquip == outfit::kHeadOption_None);

            const std::uint32_t startCount = count;

            for (std::uint8_t i = 0;
                 i < entry->headOptionCount
                 && i < outfit::kMaxHeadOptionsPerOutfit;
                 ++i)
            {
                const std::uint16_t equipId = entry->headOptionEquipIds[i];
                if (equipId == 0) continue;
                if (equipId == outfit::kHeadOption_None && origAddedNone)
                    continue;
                if (count >= 32) break;  // safety bound

                // Develop-gate for custom heads: hide modder-registered
                // heads from the submenu until the player has researched
                // them in MotherBase R&D. Vanilla head equipIds (BANDANA,
                // BALACLAVA, SP-/HP-HEADGEAR) fall through to the orig
                // pipeline, which gates them via its own R&D bit-array.
                // Our custom heads were never in that pipeline, so we
                // gate them here using the orig's IsEquipDeveloped on
                // the head's flowIndex.
                if (const auto* head =
                        outfit::TryGetCustomHeadByEquipId(equipId))
                {
                    if (head->flowIndex != 0
                        && !outfit::IsFlowIndexDevelopedByOrig(head->flowIndex))
                    {
                        continue;   // not yet researched — hide
                    }
                }

                const std::uint32_t addedIdx = count;  // AddListBandana writes to this index then increments
                g_AddListBandana(thisPtr, &count, equipId);

                // Force the per-row status / skill markers to "enabled
                // and no skill gate". AddListBandana derives them from
                // vtable[0x478] / vtable[0x670] on the equip-development
                // controller, which return 0 / "not found" for our
                // custom-suit context (the head option isn't recognized
                // as developed-FOR-this-suit-category). With status=0
                // the UI renders the row's name + icon + star as blank
                // even though the equipId itself is valid.
                //
                // Mirror what the orig writes for its initial NONE entry
                // (equipKind=0x201 branch in retail SetupPrefabListElement
                // mgsvtpp.exe.c:2956075-2956083):
                //   this[0xbc40] = 1;     // marker A (already 1 from AddListBandana)
                //   this[0xc040] = 0;     // col B (already 0)
                //   this[0xc440] = 0;     // marker C (already 0)
                //   this[0xc840] = 0xff;  // marker D ("no skill required")
                //   this[0x548]  = 1;     // status (display-enabled)
                //
                // AddListBandana wrote 0 to c840 and (vtable result) to
                // 0x548 — overwrite to known-good "enabled, no gate".
                if (addedIdx < 32)
                {
                    *reinterpret_cast<std::uint8_t*>(base + 0xc840 + addedIdx) = 0xff;
                    *reinterpret_cast<std::uint8_t*>(base + 0x548 + addedIdx * 0xf) = 1;

                    // Per-cell display data buffer at this+0xcc40 (stride
                    // 0xb4 per row, 0xc per cell; cell 0 per row carries
                    // the visible record). UpdateRecords' HEAD-OPTION
                    // branch (retail mgsvtpp.exe.c:2959408) reads
                    //   *(int*)(this[0x208] + 4 + (col + row*15)*0xc)
                    // to determine the row's display "kind" (e.g. equip
                    // mark / star / placeholder). AddListBandana never
                    // writes to 0xcc40 — the orig case 0x201 path relies
                    // on stale buffer state from a prior panel pass to
                    // happen to have the right values, but that state
                    // doesn't exist for our injected rows after the
                    // marker-clear in the count-recovery step above.
                    //
                    // Mirror what AddListWeaponBase writes (retail line
                    // 7418523-7418526) for cell 0 of the new row:
                    //   this[0xcc40 + row*0xb4] = display data (vtable[0x608])
                    //   this[0xcc44 + row*0xb4] = 0xff   (placeholder type)
                    // We don't have access to vtable[0x608]'s output for
                    // a HEAD OPTION equipId without calling it, so write
                    // 0 for the data field (the orig render reads it
                    // tolerantly) and 0xff for the type to flag this as
                    // a "no-special-mark" row.
                    const std::uint64_t cellOff = addedIdx * 0xb4;  // row*0xb4 + col*0xc with col=0
                    *reinterpret_cast<std::uint32_t*>(base + 0xcc40 + cellOff) = 0;
                    *reinterpret_cast<std::uint32_t*>(base + 0xcc44 + cellOff) = 0xff;
                }
            }

            // Commit the new count. The orig writes the final count to
            // this[0x442c] (and this[0x104]) AFTER the equipKind switch,
            // BEFORE we ran. The UI reads visible-row count from
            // this[0x442c] (verified mgsvtpp.exe.c:2956937 +
            // SetupPrefabListParameter:2956991 reads this[0x442c] back).
            // Without this update our injected rows aren't displayed.
            if (count != startCount)
            {
                *reinterpret_cast<std::uint32_t*>(base + 0x442c) = count;
                *reinterpret_cast<std::uint32_t*>(base + 0x104)  = count;
            }

            if (!g_HeadOptionInjectFirstFire.exchange(true))
            {
                Log("[OutfitListInject:HeadOption] FIRST INJECT: "
                    "partsType=0x%02X developId=%u declaredCount=%u "
                    "origCount=%u finalCount=%u (origHadNone=%d) — "
                    "committed to this[0x442c] and this[0x104]\n",
                    static_cast<unsigned>(pt),
                    static_cast<unsigned>(entry->developId),
                    static_cast<unsigned>(entry->headOptionCount),
                    static_cast<unsigned>(startCount),
                    static_cast<unsigned>(count),
                    origAddedNone ? 1 : 0);
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
        // Try to install deep hooks if we haven't already. Each panel
        // open is a fresh chance — some early calls may fire with
        // *(this+0x50) not yet pointing to a valid subsystem.
        if (!g_DeepHooksOK.load(std::memory_order_acquire))
        {
            (void)TryInstallDeepHooks(thisPtr);
        }

        // Activate the inflation flag for orig's call to GetCount/Fill/
        // GetTable. The orig calls them all synchronously, so we set
        // before and clear after.
        //
        // Top-level entry: clear the AddListSuit dedup bitset so each
        // panel build starts fresh. Nested calls (recursive Setup, e.g.
        // via callbacks) preserve the outer bitset to avoid resetting
        // mid-panel.
        const bool prev = t_InsideSetupPrefab;
        if (!prev) ResetAddedFlowIxBits();
        t_InsideSetupPrefab = true;
        if (g_OrigSetupPrefab) g_OrigSetupPrefab(thisPtr);
        t_InsideSetupPrefab = prev;

        if (!prev)
        {
            // Inject head-option entries when equipKind=0x201 and the
            // live outfit is a registered custom with HasHeadOptions().
            TryInjectHeadOptionList(thisPtr);
        }
    }
}

namespace outfit
{
    // Resolve and install the three list-helper deep hooks via static
    // AddressSet entries. Returns true if all three install OK.
    //
    // The OLD path was to defer these until the first SetupPrefabListElement
    // fire (which runs the first time the user opens the UNIFORMS panel),
    // resolving the function addresses dynamically by walking the
    // captured `this`'s vtables. That worked but had a real-world
    // consequence: when a custom outfit's R&D research finished, its
    // entry in the R&D screen wouldn't update to "developed" until the
    // user visited the UNIFORMS menu (which finally let our deep hooks
    // install and start filtering). Custom WEAPONS updated immediately
    // because their hooks aren't deferred.
    //
    // The runtime-captured addresses turned out to be stable across
    // runs (verified in two independent user logs, same retail build):
    //   GetCount   = 0x140F660C0
    //   Fill       = 0x140F65F70
    //   GetTable   = 0x14024D330
    // So we hardcode them in AddressSet and install at DLL init. R&D
    // updates immediately without needing a UNIFORMS-menu visit.
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

        // Resolve AddListSuit and install dedup hook. See the
        // duplicate-walker comment in the strategy block — without
        // this, every custom flowIndex appears twice in the panel.
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

        // Resolve AddListBandana for post-orig HEAD OPTION list injection.
        // Raw function-pointer call (no MinHook trampoline). If unresolved
        // (e.g. JP build placeholder 0), the post-orig branch silently
        // skips and the HEAD OPTION submenu falls back to whatever the
        // orig list-builder produced for the current suit category.
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

        // Try the fast path first: install the three deep hooks now
        // using the static addresses. If they're not in this build's
        // AddressSet (e.g. JP build with placeholders), fall back to
        // the legacy deferred-on-first-fire path which captures the
        // addresses dynamically from the live SetupPrefabListElement
        // `this` pointer.
        const bool deepStatic = TryInstallDeepHooksFromStaticAddresses();

        // UpdateRecords hook — variant cycle-button label override.
        // Independent of the AddListSuit/deep-hook plumbing; install
        // best-effort and continue regardless. JP build / unresolved
        // address → silent no-op (variants will fall back to vanilla
        // STANDARD/SCARF/NAKED labels for handled type values, blank
        // otherwise).
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

        // Disable deep hooks first (they're MinHook-installed on
        // captured function addresses). It's safe to call the disable
        // even if install failed — DisableAndRemoveHook handles null.
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
