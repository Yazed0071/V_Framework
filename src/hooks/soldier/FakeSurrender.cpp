#include "pch.h"
#include "FakeSurrender.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace
{
    constexpr std::size_t   kCommandVariant       = 0x14;
    constexpr std::size_t   kCommandKeyActionType = 0xB4;
    constexpr std::uint8_t  kKeyActionMelee       = 0x17;
    constexpr std::uint8_t  kMeleeHoldUpStart     = 2;        // HoldupAi's counter-attack: hands up first, the strike after

    constexpr std::size_t   kSoldierStatus        = 0x110;
    constexpr std::size_t   kStatusRecords        = 0x08;
    constexpr std::size_t   kStatusCount          = 0x10;
    constexpr std::size_t   kStatusRecordSize     = 0x28;
    constexpr std::size_t   kStatusFlags          = 0x24;
    constexpr std::uint32_t kStatusCounterAttack  = 0x4000;   // HoldupAi sets it when it picks the counter-attack

    constexpr std::uint32_t kMostSoldiers         = 0x200;

    std::atomic<bool> g_InFakeSurrender[kMostSoldiers];

    template <typename T>
    T& At(std::uint8_t* base, std::size_t offset)
    {
        return *reinterpret_cast<T*>(base + offset);
    }
}

void FakeSurrender::OnSetCommand(std::uint32_t soldierIndex, const std::uint8_t* command)
{
    if (soldierIndex >= kMostSoldiers || !command)
        return;

    const bool faking = command[kCommandKeyActionType] == kKeyActionMelee && command[kCommandVariant] == kMeleeHoldUpStart;
    std::atomic<bool>& slot = g_InFakeSurrender[soldierIndex];
    if (slot.load(std::memory_order_relaxed) != faking)
        slot.store(faking, std::memory_order_release);
}

bool FakeSurrender::IsFaking(std::uint8_t* soldier, std::uint32_t soldierIndex)
{
    if (soldierIndex < kMostSoldiers && g_InFakeSurrender[soldierIndex].load(std::memory_order_acquire))
        return true;
    if (!soldier)
        return false;

    std::uint8_t* status = At<std::uint8_t*>(soldier, kSoldierStatus);
    if (soldierIndex >= At<std::uint32_t>(status, kStatusCount))
        return false;
    std::uint8_t* record = At<std::uint8_t*>(status, kStatusRecords) + soldierIndex * kStatusRecordSize;
    return (At<std::uint32_t>(record, kStatusFlags) & kStatusCounterAttack) != 0;
}

void FakeSurrender::Clear()
{
    for (std::atomic<bool>& slot : g_InFakeSurrender)
        slot.store(false, std::memory_order_release);
}
