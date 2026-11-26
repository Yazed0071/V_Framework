#include <pch.h>
#include "ItemCount.h"

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>

#include "EquipControllerImpl_UpdateBattery.h"

namespace
{
    struct CountedItem
    {
        int           max          = 0;
        int           count        = 0;
        bool          consumeOnUse = true;
        bool          carried      = false;
    };

    std::mutex                           g_Lock;
    std::map<std::uint16_t, CountedItem> g_Items;
    std::atomic<bool>                    g_Any{ false };
}

void ItemCount::Set(std::uint16_t equipId, int count, bool consumeOnUse)
{
    std::lock_guard<std::mutex> lock(g_Lock);
    const auto it = g_Items.find(equipId);
    if (it == g_Items.end())
    {
        CountedItem item;
        item.max          = count;
        item.count        = count;
        item.consumeOnUse = consumeOnUse;
        g_Items[equipId]  = item;
    }
    else   // the mod's Lua runs again on every load: the carried count stays
    {
        it->second.max          = count;
        it->second.consumeOnUse = consumeOnUse;
        if (it->second.count > count)
            it->second.count = count;
    }
    g_Any.store(true, std::memory_order_release);
}

bool ItemCount::Get(std::uint16_t equipId, int& count, int& max)
{
    if (!g_Any.load(std::memory_order_acquire))
        return false;
    std::lock_guard<std::mutex> lock(g_Lock);
    const auto it = g_Items.find(equipId);
    if (it == g_Items.end())
        return false;
    count = it->second.count;
    max   = it->second.max;
    return true;
}

bool ItemCount::SetCount(std::uint16_t equipId, int count)
{
    std::lock_guard<std::mutex> lock(g_Lock);
    const auto it = g_Items.find(equipId);
    if (it == g_Items.end())
        return false;
    if (count < 0)
        count = 0;
    if (count > it->second.max)
        count = it->second.max;
    it->second.count = count;
    return true;
}

bool ItemCount::TakeUse(std::uint16_t equipId, bool& lastOne)
{
    lastOne = false;
    if (!g_Any.load(std::memory_order_acquire) || !ItemBattery::IsActiveHere())
        return true;
    std::lock_guard<std::mutex> lock(g_Lock);
    const auto it = g_Items.find(equipId);
    if (it == g_Items.end())
        return true;
    if (it->second.count == 0)
        return false;
    if (it->second.consumeOnUse)
        lastOne = --it->second.count == 0;
    return true;
}

void ItemCount::RefillAll()
{
    std::lock_guard<std::mutex> lock(g_Lock);
    for (auto& entry : g_Items)
        entry.second.count = entry.second.max;
}

void ItemCount::SetCarried(const std::uint16_t* equipIds, int count)
{
    std::lock_guard<std::mutex> lock(g_Lock);
    for (auto& entry : g_Items)
    {
        entry.second.carried = false;
        for (int i = 0; i < count; ++i)
            if (equipIds[i] == entry.first)
                entry.second.carried = true;
    }
}

bool ItemCount::RefillCarried()
{
    bool refilled = false;
    std::lock_guard<std::mutex> lock(g_Lock);
    for (auto& entry : g_Items)
    {
        if (!entry.second.carried || entry.second.count >= entry.second.max)
            continue;
        entry.second.count = entry.second.max;
        refilled = true;
    }
    return refilled;
}

bool ItemCount::Any()
{
    return g_Any.load(std::memory_order_acquire);
}
