#include "pch.h"
#include "EquipIdCompression.h"

#include <Windows.h>
#include <bitset>
#include <cstdint>
#include <mutex>

#include "AddressSet.h"
#include "HookUtils.h"   // ResolveGameAddress
#include "log.h"

namespace EquipIdCompression
{
    namespace
    {
        // Bitset indexed by compressed slot id [0, kCompressedSlotBound).
        // Marked set by:
        //   - SyncFromNativeTable (reads the native _s_internalInfoList_ array)
        //   - MarkCompressedSlotUsed (live observer hook on AddToEquipIdTable)
        // Cleared only by process restart (no Reset API).
        std::bitset<kCompressedSlotBound> g_UsedSlots;
        std::mutex                        g_UsedSlotsMutex;

        // Per-entry size of `_s_internalInfoList_` (verified at named-build
        // line 1357795-1357798: `lVar1 = uVar7 * 0x18; ... operator=
        // ((Path *)(&_s_internalInfoList_..._A + lVar1), ...)`). Each
        // entry's first 8 bytes hold the parts-path hash (a fox::Path's
        // PathCode64); a non-zero hash means the slot is populated by
        // vanilla.
        constexpr std::size_t kInternalInfoEntrySize = 0x18;
    }

    void MarkCompressedSlotUsed(std::int32_t compressed)
    {
        if (!IsCompressedInBounds(compressed)) return;
        std::lock_guard<std::mutex> lock(g_UsedSlotsMutex);
        g_UsedSlots.set(static_cast<std::size_t>(compressed));
    }

    bool IsCompressedSlotUsed(std::int32_t compressed)
    {
        if (!IsCompressedInBounds(compressed)) return true;  // OOB == "used" (refuse)
        std::lock_guard<std::mutex> lock(g_UsedSlotsMutex);
        return g_UsedSlots.test(static_cast<std::size_t>(compressed));
    }

    namespace
    {
        // SEH wrapper helper. Reads each slot's parts-path hash and
        // populates `outHashes[i]` with non-zero values for occupied
        // slots (zero for empty / on read fault). Lives in a separate
        // function with no C++ objects so MSVC's /EHsc model accepts the
        // __try/__except combination cleanly. Returns true on full scan
        // success, false on access-violation mid-scan.
        bool SafeReadHashes(const std::uint8_t* tableBase,
                            std::uint64_t* outHashes,
                            std::int32_t count)
        {
            __try
            {
                for (std::int32_t i = 0; i < count; ++i)
                {
                    const auto* entry = tableBase +
                        (static_cast<std::size_t>(i) * kInternalInfoEntrySize);
                    outHashes[i] =
                        *reinterpret_cast<const std::uint64_t*>(entry);
                }
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }
    }

    std::size_t SyncFromNativeTable()
    {
        // Resolve the native array address. If not resolved (e.g. JPN
        // build address is still 0), can't scan — return 0.
        const auto* tableBase =
            reinterpret_cast<const std::uint8_t*>(
                ResolveGameAddress(gAddr.EquipIdTableImpl_s_internalInfoList));
        if (!tableBase)
        {
            Log("[EquipIdCompression] SyncFromNativeTable: "
                "gAddr.EquipIdTableImpl_s_internalInfoList not resolved; "
                "cannot scan vanilla occupancy. Custom equipIds may "
                "collide with vanilla.\n");
            return 0;
        }

        // Read all slot hashes through the SEH wrapper before touching
        // the bitset, so we never end up with the lock held during a
        // partial-read fault. The local array fits comfortably on the
        // stack (0x289 * 8 = 5.13 KB).
        std::uint64_t hashes[kCompressedSlotBound] = {};
        const bool readOk =
            SafeReadHashes(tableBase, hashes, kCompressedSlotBound);

        if (!readOk)
        {
            Log("[EquipIdCompression] SyncFromNativeTable: SEH while "
                "reading native table at 0x%p — address may be wrong "
                "or page unmapped. Skipping scan.\n", tableBase);
            return 0;
        }

        std::size_t marked = 0;
        {
            std::lock_guard<std::mutex> lock(g_UsedSlotsMutex);
            for (std::int32_t i = 0; i < kCompressedSlotBound; ++i)
            {
                if (hashes[i] != 0
                    && !g_UsedSlots.test(static_cast<std::size_t>(i)))
                {
                    g_UsedSlots.set(static_cast<std::size_t>(i));
                    ++marked;
                }
            }
        }

        Log("[EquipIdCompression] SyncFromNativeTable: scanned 0x%X slots, "
            "newly-marked %zu (others were already marked or empty)\n",
            kCompressedSlotBound, marked);
        return marked;
    }
}
