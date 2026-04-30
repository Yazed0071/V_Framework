#include "pch.h"
#include "EquipIdCompression.h"

#include <Windows.h>
#include <bitset>
#include <cstdint>
#include <mutex>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace EquipIdCompression
{
    namespace
    {


        std::bitset<kCompressedSlotBound> g_UsedSlots;
        std::mutex                        g_UsedSlotsMutex;


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
        if (!IsCompressedInBounds(compressed)) return true;
        std::lock_guard<std::mutex> lock(g_UsedSlotsMutex);
        return g_UsedSlots.test(static_cast<std::size_t>(compressed));
    }

    namespace
    {


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
