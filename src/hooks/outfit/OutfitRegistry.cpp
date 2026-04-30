#include "pch.h"

#include "OutfitRegistry.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "AddressSet.h"
#include "log.h"
#include <HookUtils.h>

namespace
{
    using outfit::kCustomPartsTypeStart;
    using outfit::kCustomPartsTypeEnd;
    using outfit::kCustomSelectorStart;
    using outfit::kCustomSelectorEnd;
    using outfit::kMaxOutfits;
    using outfit::OutfitEntry;
    using outfit::OutfitDefinition;

    using GetQuarkSystemTable_t = void* (__fastcall*)();

    static std::array<OutfitEntry, kMaxOutfits> g_Entries{};
    static std::mutex                            g_Mutex;
    static GetQuarkSystemTable_t                 g_GetQuarkSystemTable = nullptr;

    // Active variant per partsType slot. Index 0 = partsType 0x40.
    // Total slots = kCustomPartsTypeEnd - kCustomPartsTypeStart + 1 = 0x40.
    static std::uint8_t g_ActiveVariant[0x40] = {};

    // Pending developId published by OutfitItemSelector when the user
    // clicks a custom-outfit row. Consumed by OutfitCommit on the
    // broken-custom blob pattern.
    static std::uint16_t g_PendingDevelopId = 0;
    static std::uint16_t g_PendingHeadOptionEquipId = 0;

    static bool ResolveQuarkApi()
    {
        if (!g_GetQuarkSystemTable)
        {
            g_GetQuarkSystemTable = reinterpret_cast<GetQuarkSystemTable_t>(
                ResolveGameAddress(gAddr.GetQuarkSystemTable));
        }
        return g_GetQuarkSystemTable != nullptr;
    }

    static std::uint8_t* GetQuarkLiveState()
    {
        if (!ResolveQuarkApi()) return nullptr;
        auto* qt = reinterpret_cast<std::uint8_t*>(g_GetQuarkSystemTable());
        if (!qt) return nullptr;
        auto* q98 = *reinterpret_cast<std::uint8_t**>(qt + 0x98);
        if (!q98) return nullptr;
        return *reinterpret_cast<std::uint8_t**>(q98 + 0x10);
    }

    static bool IsCustomPartsType(std::uint8_t v)
    {
        return v >= kCustomPartsTypeStart && v <= kCustomPartsTypeEnd;
    }

    static bool IsCustomSelector(std::uint8_t v)
    {
        return v >= kCustomSelectorStart && v <= kCustomSelectorEnd;
    }

    // Allocate the next free partsType byte from the custom pool.
    // Returns 0xFF on exhaustion. Caller holds g_Mutex.
    static std::uint8_t AllocatePartsType_NoLock(std::uint8_t hint)
    {
        if (IsCustomPartsType(hint))
        {
            for (const auto& e : g_Entries)
            {
                if (e.used && e.partsType == hint)
                    return 0xFF;  // collision
            }
            return hint;
        }

        for (std::uint16_t v = kCustomPartsTypeStart; v <= kCustomPartsTypeEnd; ++v)
        {
            const auto candidate = static_cast<std::uint8_t>(v);
            bool taken = false;
            for (const auto& e : g_Entries)
            {
                if (e.used && e.partsType == candidate) { taken = true; break; }
            }
            if (!taken) return candidate;
        }
        return 0xFF;
    }

    // Returns true if `code` is currently held by ANY registered outfit
    // — either as the base selectorCode OR as one of the per-variant
    // cycle-button selectors. Both must be checked when allocating new
    // selectors; otherwise an outfit with N variants (which claims N+1
    // contiguous-by-availability codes from the pool) would have its
    // variant-slot codes silently stolen by a later outfit's base
    // allocation, producing collision-driven mis-routing in the click
    // handler (ItemSelectorCallbackImpl::TryReadSelection reads the cell
    // selector and TryGetOutfitBySelectorCode resolves to whichever
    // entry was registered first that owns the code — wrong outfit
    // gets equipped).
    static bool IsSelectorTaken_NoLock(std::uint8_t code)
    {
        for (const auto& e : g_Entries)
        {
            if (!e.used) continue;
            if (e.selectorCode == code) return true;
            // Slot 0 is always == selectorCode, already covered above.
            // Check explicit variant slots 1..variantCount-1 too — those
            // are the additional codes this outfit reserved for cycle.
            for (std::uint8_t k = 1; k < e.variantCount; ++k)
            {
                if (e.variantSelectorCodes[k] == code) return true;
            }
        }
        return false;
    }

    static std::uint8_t AllocateSelector_NoLock(std::uint8_t hint)
    {
        if (IsCustomSelector(hint))
        {
            if (IsSelectorTaken_NoLock(hint)) return 0xFF;
            return hint;
        }

        for (std::uint16_t v = kCustomSelectorStart; v <= kCustomSelectorEnd; ++v)
        {
            const auto candidate = static_cast<std::uint8_t>(v);
            if (!IsSelectorTaken_NoLock(candidate)) return candidate;
        }
        return 0xFF;
    }
}

namespace outfit
{
    bool RegisterOutfit(const OutfitDefinition& def, std::uint8_t* outAllocatedPartsType)
    {
        if (def.partsPathCode64 == 0 || def.fpkPathCode64 == 0)
        {
            Log("[OutfitRegistry] reject: missing required parts/fpk path "
                "(developId=%u flowIndex=%u)\n",
                static_cast<unsigned>(def.developId),
                static_cast<unsigned>(def.flowIndex));
            return false;
        }

        if (def.developId == 0 || def.flowIndex == 0)
        {
            Log("[OutfitRegistry] reject: developId/flowIndex must be non-zero "
                "(developId=%u flowIndex=%u)\n",
                static_cast<unsigned>(def.developId),
                static_cast<unsigned>(def.flowIndex));
            return false;
        }

        // EDC's bit-array and row-array are sized 0x400 entries.
        // A flowIndex >= 0x400 causes the vanilla IsEquipDeveloped
        // (and any downstream code that indexes by flowIndex) to do
        // an OOB read — which presents as a hard freeze on equip.
        // Reject early with a loud log so the modder fixes their
        // script (typical bug: passing flowIndex == developId, where
        // developId is in the 51000+ range).
        constexpr std::uint16_t kEdcRowCapacity = 0x400;
        if (def.flowIndex >= kEdcRowCapacity)
        {
            Log("[OutfitRegistry] reject: flowIndex=%u is out of EDC row "
                "capacity (max 0x3FF = 1023). Custom outfits must use "
                "flowIndex in 922..1023. Either omit flowIndex (the "
                "framework auto-allocates from 922+) or pass a value "
                "in range. (developId=%u key=%s)\n",
                static_cast<unsigned>(def.flowIndex),
                static_cast<unsigned>(def.developId),
                def.key ? def.key : "(unkeyed)");
            return false;
        }

        std::lock_guard<std::mutex> lock(g_Mutex);

        // Duplicate handling — distinguish idempotent re-registration
        // (same outfit, same key fields → return success without
        // changing state) from real conflicts (different outfits
        // sharing a developId / flowIndex → reject).
        //
        // Idempotent re-registration happens commonly:
        //   - Lua hot-reload during gameplay (level transitions,
        //     save-load) re-runs the modder's RegisterOutfit calls
        //     with the same data
        //   - The other framework registries (EquipDevelop,
        //     RegisterConstantEquipId, GunBasic) all handle this
        //     gracefully with "Reusing key" / "Updated queued entry"
        //     messages — OutfitRegistry should match that pattern
        //
        // Conflict criteria: same developId OR same flowIndex AND any
        // outfit-defining field differs (playerType, partsPathCode64,
        // fpkPathCode64). If those identifying fields match, it's the
        // same outfit; we hand back the existing partsType and exit
        // success.
        for (const auto& e : g_Entries)
        {
            if (!e.used) continue;

            const bool sameDevelopId = (e.developId == def.developId);
            const bool sameFlowIndex = (e.flowIndex == def.flowIndex);
            if (!sameDevelopId && !sameFlowIndex) continue;

            const bool sameOutfit =
                sameDevelopId
             && sameFlowIndex
             && e.playerType      == def.playerType
             && e.partsPathCode64 == def.partsPathCode64
             && e.fpkPathCode64   == def.fpkPathCode64;

            if (sameOutfit)
            {
                Log("[OutfitRegistry] re-registration of same outfit "
                    "developId=%u flowIndex=%u partsType=0x%02X "
                    "selector=0x%02X — returning existing entry "
                    "(idempotent)\n",
                    static_cast<unsigned>(e.developId),
                    static_cast<unsigned>(e.flowIndex),
                    static_cast<unsigned>(e.partsType),
                    static_cast<unsigned>(e.selectorCode));
                if (outAllocatedPartsType) *outAllocatedPartsType = e.partsType;
                return true;
            }

            // Conflict — different outfit trying to claim the same
            // developId/flowIndex slot.
            if (sameDevelopId)
            {
                Log("[OutfitRegistry] reject: developId %u already registered "
                    "by a DIFFERENT outfit (existing partsType=0x%02X "
                    "playerType=%u, attempted playerType=%u)\n",
                    static_cast<unsigned>(def.developId),
                    static_cast<unsigned>(e.partsType),
                    static_cast<unsigned>(e.playerType),
                    static_cast<unsigned>(def.playerType));
            }
            else
            {
                Log("[OutfitRegistry] reject: flowIndex %u already registered "
                    "by a DIFFERENT outfit (existing partsType=0x%02X "
                    "developId=%u, attempted developId=%u)\n",
                    static_cast<unsigned>(def.flowIndex),
                    static_cast<unsigned>(e.partsType),
                    static_cast<unsigned>(e.developId),
                    static_cast<unsigned>(def.developId));
            }
            return false;
        }

        const std::uint8_t partsType = AllocatePartsType_NoLock(def.partsTypeHint);
        if (partsType == 0xFF)
        {
            Log("[OutfitRegistry] reject: no free custom partsType slot\n");
            return false;
        }

        const std::uint8_t selector = AllocateSelector_NoLock(def.selectorCodeHint);
        if (selector == 0xFF)
        {
            Log("[OutfitRegistry] reject: no free custom selector slot\n");
            return false;
        }

        // Reserve additional selectors for explicit variants. Variant 0
        // is the base outfit (uses `selector` already allocated above);
        // variants[1..variantCount-1] each need their own selector for
        // the UNIFORMS panel cycle to write distinct cell entries.
        //
        // Allocator picks free slots from the same 0x80..0xFE pool, so
        // we may not get contiguous codes — that's fine, the panel
        // cycle uses the per-cell stored selector, not arithmetic.
        const std::uint8_t variantSlots =
            (def.variantCount > outfit::kMaxVariantsPerOutfit)
                ? static_cast<std::uint8_t>(outfit::kMaxVariantsPerOutfit)
                : def.variantCount;

        std::uint8_t variantSelectors[outfit::kMaxVariantsPerOutfit] = {};
        for (auto& v : variantSelectors) v = 0xFF;
        variantSelectors[0] = selector;  // variant 0 = base
        for (std::uint8_t vi = 1; vi < variantSlots; ++vi)
        {
            // Pre-mark earlier ones as taken by inserting them into a
            // scratch tracker. AllocateSelector_NoLock walks g_Entries
            // for "taken" state, so we need a temp registration to
            // prevent double-allocation. Cheapest: do iterative scans
            // that include our just-allocated codes by checking
            // variantSelectors[0..vi-1] manually.
            std::uint8_t alloc = 0xFF;
            for (std::uint16_t cand = kCustomSelectorStart;
                 cand <= kCustomSelectorEnd; ++cand)
            {
                const auto c = static_cast<std::uint8_t>(cand);
                if (IsSelectorTaken_NoLock(c)) continue;
                // Exclude codes we just allocated to earlier siblings
                // of this same outfit's variants (variantSelectors[0..vi-1]).
                bool taken = false;
                for (std::uint8_t k = 0; k < vi; ++k)
                {
                    if (variantSelectors[k] == c) { taken = true; break; }
                }
                if (!taken) { alloc = c; break; }
            }
            if (alloc == 0xFF)
            {
                Log("[OutfitRegistry] reject: ran out of selector codes "
                    "while reserving variant %u of %u (variantCount=%u)\n",
                    static_cast<unsigned>(vi),
                    static_cast<unsigned>(variantSlots),
                    static_cast<unsigned>(def.variantCount));
                return false;
            }
            variantSelectors[vi] = alloc;
        }

        // Find a free entry slot.
        OutfitEntry* slot = nullptr;
        for (auto& e : g_Entries)
        {
            if (!e.used) { slot = &e; break; }
        }
        if (!slot)
        {
            Log("[OutfitRegistry] reject: registry is full (kMaxOutfits=%zu)\n",
                kMaxOutfits);
            return false;
        }

        *slot = OutfitEntry{};
        slot->used            = true;
        slot->developId       = def.developId;
        slot->flowIndex       = def.flowIndex;
        slot->playerType      = def.playerType;
        slot->partsType       = partsType;
        slot->selectorCode    = selector;
        slot->partsPathCode64 = def.partsPathCode64;
        slot->fpkPathCode64   = def.fpkPathCode64;
        slot->camoFpk         = def.camoFpk;
        slot->faceFpk         = def.faceFpk;
        slot->skinFv2         = def.skinFv2;
        slot->diamondFpk      = def.diamondFpk;
        slot->enableArm       = def.enableArm;

        // Phase 3 fields.
        slot->camoFv2             = def.camoFv2;
        slot->diamondFv2          = def.diamondFv2;
        slot->supportsHeadOptions = def.supportsHeadOptions;
        slot->headOptionCount     =
            def.headOptionCount > outfit::kMaxHeadOptionsPerOutfit
                ? static_cast<std::uint8_t>(outfit::kMaxHeadOptionsPerOutfit)
                : def.headOptionCount;
        for (std::size_t i = 0; i < slot->headOptionCount; ++i)
            slot->headOptionEquipIds[i] = def.headOptionEquipIds[i];

        slot->variantCount =
            def.variantCount > outfit::kMaxVariantsPerOutfit
                ? static_cast<std::uint8_t>(outfit::kMaxVariantsPerOutfit)
                : def.variantCount;
        for (std::size_t i = 0; i < slot->variantCount; ++i)
            slot->variants[i] = def.variants[i];

        // Copy the variant selectors we reserved above. Slot 0 always
        // holds the base selector; slots 1..variantCount-1 hold the
        // additional cycle selectors. Unused slots get 0xFF sentinel.
        for (std::size_t i = 0; i < outfit::kMaxVariantsPerOutfit; ++i)
            slot->variantSelectorCodes[i] = variantSelectors[i];

        // Variant 0 = base outfit (uses OutfitDefinition::baseDisplayNameHash).
        // Variants 1..variantCount-1 = OutfitVariant::displayNameHash.
        // Slots beyond variantCount stay at 0 (no override).
        slot->variantDisplayNameHashes[0] = def.baseDisplayNameHash;
        for (std::uint8_t vi = 1; vi < slot->variantCount; ++vi)
        {
            slot->variantDisplayNameHashes[vi] = def.variants[vi].displayNameHash;
        }

        slot->enableHead           = def.enableHead;
        slot->defaultSoldierFaceId = def.defaultSoldierFaceId;
        slot->langEquipNameHash    = def.langEquipNameHash;

        if (outAllocatedPartsType) *outAllocatedPartsType = partsType;

        // Build the variant-selector list as a single string so the
        // log is independent of kMaxVariantsPerOutfit's value.
        char variantBuf[16 * 5 + 1] = {};
        {
            std::size_t pos = 0;
            for (std::size_t i = 0;
                 i < outfit::kMaxVariantsPerOutfit && pos + 5 < sizeof(variantBuf);
                 ++i)
            {
                pos += static_cast<std::size_t>(std::snprintf(
                    variantBuf + pos, sizeof(variantBuf) - pos,
                    (i == 0) ? "0x%02X" : ",0x%02X",
                    static_cast<unsigned>(slot->variantSelectorCodes[i])));
            }
        }

        // Same idea for the per-variant displayName hashes — show
        // 16-bit-hi-half + 16-bit-mid-half so we don't bloat the log
        // with full 64-bit values for every slot. Empty slot is shown
        // as ".." so the column count matches selector count.
        char dispNameBuf[16 * 12 + 1] = {};
        {
            std::size_t pos = 0;
            for (std::size_t i = 0;
                 i < outfit::kMaxVariantsPerOutfit && pos + 12 < sizeof(dispNameBuf);
                 ++i)
            {
                const std::uint64_t h = slot->variantDisplayNameHashes[i];
                if (h == 0)
                {
                    pos += static_cast<std::size_t>(std::snprintf(
                        dispNameBuf + pos, sizeof(dispNameBuf) - pos,
                        (i == 0) ? "(none)" : ",(none)"));
                }
                else
                {
                    pos += static_cast<std::size_t>(std::snprintf(
                        dispNameBuf + pos, sizeof(dispNameBuf) - pos,
                        (i == 0) ? "0x%llX" : ",0x%llX",
                        static_cast<unsigned long long>(h)));
                }
            }
        }

        Log("[OutfitRegistry] registered key=%s developId=%u flowIndex=%u "
            "playerType=%u partsType=0x%02X selector=0x%02X "
            "enableHead=%d defaultSoldierFaceId=%u headOptions=%u(supports=%d) "
            "langEquipNameHash=0x%016llX "
            "variantCount=%u variantSelectors=[%s] "
            "variantDisplayNameHashes=[%s] "
            "parts=0x%016llX fpk=0x%016llX\n",
            def.key ? def.key : "(unkeyed)",
            static_cast<unsigned>(def.developId),
            static_cast<unsigned>(def.flowIndex),
            static_cast<unsigned>(def.playerType),
            static_cast<unsigned>(partsType),
            static_cast<unsigned>(selector),
            def.enableHead ? 1 : 0,
            static_cast<unsigned>(def.defaultSoldierFaceId),
            static_cast<unsigned>(def.headOptionCount),
            def.supportsHeadOptions ? 1 : 0,
            static_cast<unsigned long long>(def.langEquipNameHash),
            static_cast<unsigned>(slot->variantCount),
            variantBuf,
            dispNameBuf,
            static_cast<unsigned long long>(def.partsPathCode64),
            static_cast<unsigned long long>(def.fpkPathCode64));

        return true;
    }

    void ClearAllOutfits()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (auto& e : g_Entries) e = OutfitEntry{};
    }

    bool TryGetOutfitByPartsType(std::uint8_t partsType, const OutfitEntry** outEntry)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const auto& e : g_Entries)
        {
            if (e.used && e.partsType == partsType)
            {
                if (outEntry) *outEntry = &e;
                return true;
            }
        }
        return false;
    }

    bool TryGetOutfitBySelectorCode(std::uint8_t selectorCode, const OutfitEntry** outEntry)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const auto& e : g_Entries)
        {
            if (!e.used) continue;
            // Match base or any variant selector — the caller usually
            // just wants the entry regardless of which variant cell
            // was clicked. Use TryGetOutfitByVariantSelector if you
            // also need the variant index.
            if (e.selectorCode == selectorCode)
            {
                if (outEntry) *outEntry = &e;
                return true;
            }
            for (std::uint8_t vi = 1; vi < e.variantCount; ++vi)
            {
                if (e.variantSelectorCodes[vi] == selectorCode)
                {
                    if (outEntry) *outEntry = &e;
                    return true;
                }
            }
        }
        return false;
    }

    bool TryGetOutfitByFlowIndex(std::uint16_t flowIndex, const OutfitEntry** outEntry)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const auto& e : g_Entries)
        {
            if (e.used && e.flowIndex == flowIndex)
            {
                if (outEntry) *outEntry = &e;
                return true;
            }
        }
        return false;
    }

    bool TryGetOutfitByVariantSelector(std::uint8_t selectorCode,
                                       const OutfitEntry** outEntry,
                                       std::uint8_t* outVariantIndex)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const auto& e : g_Entries)
        {
            if (!e.used) continue;
            // Variant 0 is always the base selector.
            if (e.selectorCode == selectorCode)
            {
                if (outEntry) *outEntry = &e;
                if (outVariantIndex) *outVariantIndex = 0;
                return true;
            }
            // Variants 1..variantCount-1 use the reserved per-variant
            // selectors. Skip slot 0 (already checked via base).
            for (std::uint8_t vi = 1; vi < e.variantCount; ++vi)
            {
                if (e.variantSelectorCodes[vi] == selectorCode)
                {
                    if (outEntry) *outEntry = &e;
                    if (outVariantIndex) *outVariantIndex = vi;
                    return true;
                }
            }
        }
        return false;
    }

    std::uint64_t GetVariantDisplayNameHash(std::uint8_t partsType,
                                            std::uint8_t variantIndex)
    {
        if (variantIndex >= outfit::kMaxVariantsPerOutfit) return 0;
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const auto& e : g_Entries)
        {
            if (e.used && e.partsType == partsType)
                return e.variantDisplayNameHashes[variantIndex];
        }
        return 0;
    }

    bool TryGetOutfitByDevelopId(std::uint16_t developId, const OutfitEntry** outEntry)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const auto& e : g_Entries)
        {
            if (e.used && e.developId == developId)
            {
                if (outEntry) *outEntry = &e;
                return true;
            }
        }
        return false;
    }

    bool TryGetOutfitByFlowIndexForPlayerType(
            std::uint16_t flowIndex, std::uint8_t playerType,
            const OutfitEntry** outEntry)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const auto& e : g_Entries)
        {
            if (e.used && e.flowIndex == flowIndex && e.playerType == playerType)
            {
                if (outEntry) *outEntry = &e;
                return true;
            }
        }
        return false;
    }

    bool TryGetOutfitByDevelopIdForPlayerType(
            std::uint16_t developId, std::uint8_t playerType,
            const OutfitEntry** outEntry)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const auto& e : g_Entries)
        {
            if (e.used && e.developId == developId && e.playerType == playerType)
            {
                if (outEntry) *outEntry = &e;
                return true;
            }
        }
        return false;
    }

    std::size_t GetAllOutfits(const OutfitEntry** outEntries, std::size_t maxEntries)
    {
        if (!outEntries || maxEntries == 0) return 0;

        std::lock_guard<std::mutex> lock(g_Mutex);
        std::size_t count = 0;
        for (const auto& e : g_Entries)
        {
            if (count >= maxEntries) break;
            if (e.used) outEntries[count++] = &e;
        }
        return count;
    }

    // ---------------------------------------------------------------
    // Variant accessors (cascade: variant slot → base outfit field).
    // variantIndex 0 always returns the base outfit values.
    // ---------------------------------------------------------------

    std::uint64_t OutfitEntry::GetVariantPartsPath(std::uint8_t idx) const
    {
        if (idx == 0 || idx >= variantCount || !variants[idx].used) return partsPathCode64;
        return variants[idx].partsPathCode64 != 0
             ? variants[idx].partsPathCode64
             : partsPathCode64;
    }

    std::uint64_t OutfitEntry::GetVariantFpkPath(std::uint8_t idx) const
    {
        if (idx == 0 || idx >= variantCount || !variants[idx].used) return fpkPathCode64;
        return variants[idx].fpkPathCode64 != 0
             ? variants[idx].fpkPathCode64
             : fpkPathCode64;
    }

    std::uint64_t OutfitEntry::GetVariantCamoFpk(std::uint8_t idx) const
    {
        if (idx == 0 || idx >= variantCount || !variants[idx].used) return camoFpk;
        const auto v = variants[idx].camoFpk;
        return v == kSubAssetUseVanilla ? camoFpk : v;
    }

    std::uint64_t OutfitEntry::GetVariantCamoFv2(std::uint8_t idx) const
    {
        if (idx == 0 || idx >= variantCount || !variants[idx].used) return camoFv2;
        const auto v = variants[idx].camoFv2;
        return v == kSubAssetUseVanilla ? camoFv2 : v;
    }

    std::uint64_t OutfitEntry::GetVariantDiamondFpk(std::uint8_t idx) const
    {
        if (idx == 0 || idx >= variantCount || !variants[idx].used) return diamondFpk;
        return variants[idx].diamondFpk != kSubAssetDisabled
             ? variants[idx].diamondFpk
             : diamondFpk;
    }

    std::uint8_t ReadLivePartsType()
    {
        auto* state = GetQuarkLiveState();
        return state ? state[0xF8] : static_cast<std::uint8_t>(0xFF);
    }

    std::uint8_t ReadLiveSelectorCode()
    {
        auto* state = GetQuarkLiveState();
        return state ? state[0xF9] : static_cast<std::uint8_t>(0xFF);
    }

    std::uint8_t ReadLivePlayerType()
    {
        auto* state = GetQuarkLiveState();
        return state ? state[0xFB] : static_cast<std::uint8_t>(0xFF);
    }

    bool WriteLivePlayerOutfit(std::uint8_t partsType,
                               std::uint8_t selectorCode,
                               std::uint8_t playerType)
    {
        auto* state = GetQuarkLiveState();
        if (!state) return false;

        __try
        {
            state[0xF8] = partsType;     // playerPartsType
            state[0xF9] = selectorCode;  // playerCamoType
            state[0xFB] = playerType;    // playerType
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // ---------------------------------------------------------------
    // Active-variant tracker.
    // ---------------------------------------------------------------

    void SetActiveVariant(std::uint8_t partsType, std::uint8_t variantIndex)
    {
        if (partsType < kCustomPartsTypeStart || partsType > kCustomPartsTypeEnd)
            return;

        std::uint8_t clamped = variantIndex;

        // Clamp against the registered outfit's variantCount (if any).
        // Read entry under lock — but g_ActiveVariant write itself is
        // a single byte and benefits from the same lock guard for
        // ordering with reads.
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const auto& e : g_Entries)
        {
            if (e.used && e.partsType == partsType)
            {
                if (e.variantCount == 0) clamped = 0;
                else if (clamped >= e.variantCount)
                    clamped = static_cast<std::uint8_t>(e.variantCount - 1);
                break;
            }
        }
        g_ActiveVariant[partsType - kCustomPartsTypeStart] = clamped;
    }

    std::uint8_t GetActiveVariant(std::uint8_t partsType)
    {
        if (partsType < kCustomPartsTypeStart || partsType > kCustomPartsTypeEnd)
            return 0;
        std::lock_guard<std::mutex> lock(g_Mutex);
        return g_ActiveVariant[partsType - kCustomPartsTypeStart];
    }

    void ClearActiveVariant(std::uint8_t partsType)
    {
        if (partsType < kCustomPartsTypeStart || partsType > kCustomPartsTypeEnd)
            return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_ActiveVariant[partsType - kCustomPartsTypeStart] = 0;
    }

    // ---------------------------------------------------------------
    // Pending-developId bridge (atomic byte writes, no lock needed).
    // ---------------------------------------------------------------

    void SetPendingOutfitDevelopId(std::uint16_t developId)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_PendingDevelopId = developId;
    }

    std::uint16_t GetPendingOutfitDevelopId()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        return g_PendingDevelopId;
    }

    void ClearPendingOutfitDevelopId()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_PendingDevelopId = 0;
    }

    void SetPendingHeadOptionEquipId(std::uint16_t equipId)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_PendingHeadOptionEquipId = equipId;
    }

    std::uint16_t GetPendingHeadOptionEquipId()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        return g_PendingHeadOptionEquipId;
    }

    void ClearPendingHeadOptionEquipId()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_PendingHeadOptionEquipId = 0;
    }

    static bool g_SupplyDropClickLatch = false;

    void SetSupplyDropClickLatch()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_SupplyDropClickLatch = true;
    }

    bool ConsumeSupplyDropClickLatch()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        const bool was = g_SupplyDropClickLatch;
        g_SupplyDropClickLatch = false;
        return was;
    }

    static std::uint16_t g_PendingSupplyDropDevelopId = 0;

    void SetPendingSupplyDropDevelopId(std::uint16_t developId)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_PendingSupplyDropDevelopId = developId;
    }

    std::uint16_t ConsumePendingSupplyDropDevelopId()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        const std::uint16_t was = g_PendingSupplyDropDevelopId;
        g_PendingSupplyDropDevelopId = 0;
        return was;
    }

    std::uint16_t PeekPendingSupplyDropDevelopId()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        return g_PendingSupplyDropDevelopId;
    }

    static std::uint8_t g_PendingSupplyDropVariantIdx = 0;

    void SetPendingSupplyDropVariantIdx(std::uint8_t variantIndex)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_PendingSupplyDropVariantIdx = variantIndex;
    }

    std::uint8_t ConsumePendingSupplyDropVariantIdx()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        const std::uint8_t was = g_PendingSupplyDropVariantIdx;
        g_PendingSupplyDropVariantIdx = 0;
        return was;
    }
}
