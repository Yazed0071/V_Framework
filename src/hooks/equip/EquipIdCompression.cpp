#include "pch.h"
#include "EquipIdCompression.h"

#include <Windows.h>
#include <atomic>
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

        std::bitset<kExtendedEquipIdLast + 1> g_ExtendedUsed;
        std::mutex                            g_ExtendedMutex;
    }

    void MarkExtendedEquipIdUsed(std::int32_t equipId)
    {
        if (!IsExtendedEquipId(equipId)) return;
        std::lock_guard<std::mutex> lock(g_ExtendedMutex);
        g_ExtendedUsed.set(static_cast<std::size_t>(equipId));
    }

    bool IsExtendedEquipIdUsed(std::int32_t equipId)
    {
        if (!IsExtendedEquipId(equipId)) return true;
        std::lock_guard<std::mutex> lock(g_ExtendedMutex);
        return g_ExtendedUsed.test(static_cast<std::size_t>(equipId));
    }

    std::int32_t FindLowestFreeExtendedEquipId()
    {
        return FindLowestFreeExtendedEquipId(ExtendedReservedFn{});
    }

    std::int32_t FindLowestFreeExtendedEquipId(
        const ExtendedReservedFn& isReservedElsewhere)
    {
        std::lock_guard<std::mutex> lock(g_ExtendedMutex);
        for (std::int32_t id = kExtendedAllocFirst; id <= kExtendedAllocLast; ++id)
        {
            if (g_ExtendedUsed.test(static_cast<std::size_t>(id)))
                continue;
            if (isReservedElsewhere && isReservedElsewhere(id))
                continue;
            g_ExtendedUsed.set(static_cast<std::size_t>(id));
            return id;
        }
        static std::atomic<int> s_exhaustedLogged{ 0 };
        if (s_exhaustedLogged.fetch_add(1) < 2)
            LogError("EquipIdCompression: extended equipId range 0x%X-0x%X exhausted, "
                "item gets no equipId and appears in no menu",
                static_cast<unsigned>(kExtendedAllocFirst),
                static_cast<unsigned>(kExtendedAllocLast));
        return -1;
    }

    void MarkCompressedSlotUsed(std::int32_t compressed)
    {
        if (!IsCompressedInBounds(compressed)) return;
        std::lock_guard<std::mutex> lock(g_UsedSlotsMutex);
        g_UsedSlots.set(static_cast<std::size_t>(compressed));
    }

    void ClearCompressedSlotUsed(std::int32_t compressed)
    {
        if (!IsCompressedInBounds(compressed)) return;
        std::lock_guard<std::mutex> lock(g_UsedSlotsMutex);
        g_UsedSlots.reset(static_cast<std::size_t>(compressed));
    }

    bool IsCompressedSlotUsed(std::int32_t compressed)
    {
        if (!IsCompressedInBounds(compressed)) return true;
        std::lock_guard<std::mutex> lock(g_UsedSlotsMutex);
        return g_UsedSlots.test(static_cast<std::size_t>(compressed));
    }

    namespace
    {
        static_assert(kInternalInfoEntrySize == 3 * sizeof(std::uint64_t),
                      "equip-row occupancy must cover the whole native row");

        bool SafeReadOccupancy(const std::uint8_t* tableBase,
                               std::uint64_t* outOccupied,
                               std::int32_t count)
        {
            __try
            {
                for (std::int32_t i = 0; i < count; ++i)
                {
                    const auto* row = reinterpret_cast<const std::uint64_t*>(
                        tableBase +
                        (static_cast<std::size_t>(i) * kInternalInfoEntrySize));
                    outOccupied[i] = row[0] | row[1] | row[2];
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
                ResolveGameAddress(gAddr.EquipIdTable_InfoList));
        if (!tableBase)
        {
            static std::atomic<bool> s_unresolvedLogged{ false };
            if (!s_unresolvedLogged.exchange(true))
                LogError("EquipIdCompression: EquipIdTable_InfoList address missing "
                    "for this build, custom equipIds may collide");
            return 0;
        }

        std::uint64_t occupied[kCompressedSlotBound] = {};
        const bool readOk =
            SafeReadOccupancy(tableBase, occupied, kCompressedSlotBound);

        if (!readOk)
        {
            static std::atomic<bool> s_unreadableLogged{ false };
            if (!s_unreadableLogged.exchange(true))
                LogWarn("EquipIdCompression: native equip table at %p unreadable, "
                    "occupancy scan skipped", static_cast<const void*>(tableBase));
            return 0;
        }

        std::size_t occupiedRows = 0;
        {
            std::lock_guard<std::mutex> lock(g_UsedSlotsMutex);
            for (std::int32_t i = 0; i < kCompressedSlotBound; ++i)
            {
                if (occupied[i] == 0)
                    continue;
                ++occupiedRows;
                g_UsedSlots.set(static_cast<std::size_t>(i));
            }
        }
        return occupiedRows;
    }
}
