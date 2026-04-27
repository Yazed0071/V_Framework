#include "pch.h"

#include "OutfitListInject.h"
#include "OutfitRegistry.h"
#include "OutfitEquippedState.h"

#include <array>
#include <atomic>
#include <cstdint>
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

    static SetupPrefabListElement_t g_OrigSetupPrefab = nullptr;
    static GetDevelopedCount_t      g_OrigGetCount    = nullptr;
    static FillDevelopedFlowIxs_t   g_OrigFill        = nullptr;
    static GetSuitInfoTable_t       g_OrigGetTable    = nullptr;
    // AddListSuit is hooked for per-scope dedup (see the duplicate-
    // walker comment above). g_OrigAddListSuit is the trampoline.
    static AddListSuit_t            g_OrigAddListSuit = nullptr;
    // Resolved address; cached for uninstall.
    static void*                    g_AddListSuitAddr = nullptr;

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
        if (t_InsideSetupPrefab && TestAndSetAddedBit(flowIndex))
        {
            // Already added this scope — skip orig entirely so the
            // counter doesn't advance and no cell write happens.
            return;
        }
        if (g_OrigAddListSuit)
            g_OrigAddListSuit(thisPtr, rowCounter, flowIndex, entryBuf);
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

    // ---- Top-level SetupPrefabListElement hook ----

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
    }
}

namespace outfit
{
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

        Log("[OutfitListInject] installed: setup=%s addListSuit=%s "
            "(target=%p addListSuitAddr=%p) — deep hooks deferred to "
            "first fire\n",
            setupHooked ? "OK" : "FAIL",
            addListSuitHooked ? "OK" : (g_AddListSuitAddr ? "FAIL" : "UNRESOLVED"),
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

        if (void* t = ResolveGameAddress(
                gAddr.ItemSelectorCallbackImpl_SetupPrefabListElement))
            DisableAndRemoveHook(t);

        g_OrigSetupPrefab  = nullptr;
        g_Installed        = false;

        Log("[OutfitListInject] removed\n");
    }
}
