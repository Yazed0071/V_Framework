#pragma once

#include <cstddef>   // std::size_t
#include <cstdint>

// ----------------------------------------------------------------
// EquipId compression — matches the native game's
// tpp::gm::impl::equip::EquipIdTableImpl::AddToEquipIdTable
// (mgsvtpp.exe.c lines 1357726-1357733). The native function
// hashes the lua-side equipId into a compressed table index used by
// four parallel arrays:
//   - _s_internalInfoList  (path data, 0x18 bytes/entry)
//   - DAT_142c20fb8        (path data, 0x18 bytes/entry)
//   - DAT_142c20fc0        (byte at +8 within above stride)
//   - DAT_142a70928        (uint16 type/value, 2 bytes/entry)
//
// All four arrays are sized **0x289 entries** (verified at
// `memset(&DAT_142a70928, 0, 0x512)` line 1358057 — 0x512 / 2 = 0x289).
//
// Compression formula:
//   equipId in [0x000, 0x400)   compressed = equipId
//   equipId in [0x400, 0x600)   compressed = equipId - 0x1D0
//   equipId in [0x600, ...)     compressed = equipId - 0x380
//
// The valid equipId ranges that compress to in-bounds slots are:
//   0x000..0x288 (compressed 0x000..0x288 directly)
//   0x400..0x458 (compressed 0x230..0x288)
//   0x600..0x608 (compressed 0x280..0x288)
// Anything else compresses past the bound and OOB-writes when the
// native AddToEquipIdTable runs — corrupting whatever vanilla data
// lives immediately after these `.data` arrays (UI icon paths,
// equipment metadata, etc.).
// ----------------------------------------------------------------

namespace EquipIdCompression
{
    // Total in-bounds compressed-index space.
    constexpr std::int32_t kCompressedSlotBound = 0x289;

    // Maps a lua-side equipId to the native compressed table index.
    // Returns -1 for negative input. Returns >= kCompressedSlotBound
    // when the equipId would OOB-write (callers MUST check before
    // forwarding to native AddToEquipIdTable).
    inline std::int32_t ComputeCompressed(std::int32_t equipId)
    {
        if (equipId < 0)         return -1;
        if (equipId < 0x400)     return equipId;
        if (equipId < 0x600)     return equipId - 0x1D0;
        return equipId - 0x380;
    }

    inline bool IsCompressedInBounds(std::int32_t compressed)
    {
        return compressed >= 0 && compressed < kCompressedSlotBound;
    }

    inline bool IsEquipIdSafeForNativeTable(std::int32_t equipId)
    {
        return IsCompressedInBounds(ComputeCompressed(equipId));
    }

    // ----------------------------------------------------------------
    // Vanilla-occupancy tracking.
    //
    // The framework needs to know which compressed equip-id slots vanilla
    // MGSV has already populated, so the custom-equipId allocator can
    // pick truly-free slots and avoid colliding with vanilla weapons /
    // ammo / items.
    //
    // Two channels feed the bitset:
    //
    // 1. **Direct table scan** (`SyncFromNativeTable`) — reads the native
    //    `_s_internalInfoList_` array (gAddr.EquipIdTableImpl_s_internalInfoList,
    //    0x289 entries × 0x18 bytes; entry's first 8 bytes = parts-path
    //    hash, non-zero = populated). Vanilla MGSV's TppEquipParts.lua
    //    runs BEFORE our DLL injects, so the array is already populated
    //    by the time we can read it. This is the PRIMARY data source.
    //
    // 2. **Live observer hook** (`MarkCompressedSlotUsed` from
    //    `hkStockAddToEquipIdTable_Observer`) — captures any LATER calls
    //    to AddToEquipIdTable (e.g. mid-game data reloads, or a future
    //    user-mod calling it via Lua). Belt-and-suspenders coverage on
    //    top of the direct scan.
    //
    // `IsCompressedSlotUsed` returns the bitset value; OOB inputs always
    // return true ("treat as used = refuse"). All functions are
    // thread-safe (internal mutex).
    // ----------------------------------------------------------------
    void  MarkCompressedSlotUsed(std::int32_t compressed);
    bool  IsCompressedSlotUsed(std::int32_t compressed);

    // Reads the native `_s_internalInfoList_` array via
    // gAddr.EquipIdTableImpl_s_internalInfoList and marks every slot
    // whose entry has a non-zero parts-path hash as used. Idempotent —
    // safe to call multiple times. Returns the number of slots marked
    // (0 if the address isn't resolved or all slots are empty).
    //
    // Call once before the first allocation request — `V_FrameWorkState`'s
    // allocator does this on first use.
    std::size_t SyncFromNativeTable();

    // Convenience: scan the full bound for the lowest unused slot whose
    // *equipId form* (slot index, since compressed==equipId in [0..0x400))
    // is also not present in the caller-supplied "session-used" set.
    // Returns -1 if every in-bounds slot is occupied by vanilla or the
    // session.
    //
    // The lambda receives the candidate equipId; return true if the
    // session already uses that equipId. Caller is responsible for
    // any external locking on its session map.
    template <typename SessionUsedFn>
    inline std::int32_t FindLowestFreeEquipId(SessionUsedFn isSessionUsed,
                                               std::int32_t minimumEquipId = 0)
    {
        // Walk compressed slots [minimumEquipId, kCompressedSlotBound).
        // For each free vanilla slot, the corresponding equipId is the
        // slot index itself (since equipId < 0x400 maps 1:1 to compressed
        // index). We don't try to address the same compressed slot via
        // the 0x400+/0x600+ ranges — those collide with vanilla and
        // would only confuse downstream lookups.
        const std::int32_t start =
            (minimumEquipId > 0) ? minimumEquipId : 0;
        for (std::int32_t equipId = start;
             equipId < kCompressedSlotBound;
             ++equipId)
        {
            if (IsCompressedSlotUsed(equipId)) continue;
            if (isSessionUsed(equipId))        continue;
            return equipId;
        }
        return -1;
    }
}
