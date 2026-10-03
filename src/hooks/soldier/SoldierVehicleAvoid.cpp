#include "pch.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

#include "AddressSet.h"
#include "HookUtils.h"
#include "SoldierVehicleAvoid.h"
#include "log.h"

namespace
{
    constexpr std::uint32_t kSoldierIndexMask          = 0x1FFu;
    constexpr std::size_t   kSoldierSlotCount          = 0x200u;
    constexpr std::uint8_t  kNoticeTypeVehicleApproach = 3u;

    std::mutex                                   g_mtx;
    std::array<std::uint8_t, kSoldierSlotCount>  g_ignore{};
    std::atomic<bool>                            g_installed{ false };
}

void Set_SoldierIgnoreVehicle(std::uint32_t gameObjectId, bool enabled)
{
    const std::uint32_t slot = gameObjectId & kSoldierIndexMask;
    std::lock_guard<std::mutex> lk(g_mtx);
    g_ignore[slot] = enabled ? 1u : 0u;
}

bool Soldier_IgnoresVehicle(std::uint32_t soldierIndex)
{
    const std::uint32_t slot = soldierIndex & kSoldierIndexMask;
    std::lock_guard<std::mutex> lk(g_mtx);
    return g_ignore[slot] != 0u;
}

bool SoldierVehicleAvoid_ShouldDropNotice(std::uint32_t soldierIndex, std::uint8_t noticeType,
                                          const void* noticeBlob)
{
    if (noticeType != kNoticeTypeVehicleApproach || !noticeBlob)
        return false;
    if (!g_installed.load(std::memory_order_relaxed))
        return false;
    return Soldier_IgnoresVehicle(soldierIndex);
}

void Clear_AllSoldierIgnoreVehicle()
{
    std::lock_guard<std::mutex> lk(g_mtx);
    g_ignore.fill(0u);
}

bool Install_SoldierVehicleAvoid_Hook()
{
    if (!ResolveGameAddress(gAddr.AddNoticeInfo))
    {
        LogError("SoldierIgnoreVehicle: AddNoticeInfo address missing, SetIgnoreVehicle has no effect");
        return false;
    }

    g_installed.store(true, std::memory_order_relaxed);
    return true;
}

bool Uninstall_SoldierVehicleAvoid_Hook()
{
    g_installed.store(false, std::memory_order_relaxed);
    Clear_AllSoldierIgnoreVehicle();
    return true;
}
