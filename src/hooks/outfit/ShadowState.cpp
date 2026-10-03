#include "pch.h"

#include "ShadowState.h"
#include "AdditionalMotionTable_GetMtarPathId.h"

#include <atomic>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <mutex>

#include "log.h"

namespace
{
    std::mutex                                    g_Mutex;
    outfit::shadow::Slot                          g_Slots[outfit::shadow::kMaxSlots] = {};

    std::uint8_t                                  g_ArmTier[outfit::shadow::kPlayerTypeMax]      = {};
    bool                                          g_ArmTierCaptured[outfit::shadow::kPlayerTypeMax] = {};

    thread_local std::size_t                      tl_CurrentSlot = SIZE_MAX;
}

namespace outfit::shadow
{
    void Set(std::size_t slot, const Slot& s)
    {
        if (slot >= kMaxSlots)
        {
            static std::atomic<int> s_oobSet{ 0 };
            if (s_oobSet.fetch_add(1, std::memory_order_relaxed) < 8)
                LogWarn("ShadowState: parts slot %zu past the %zu-slot mirror, outfit "
                    "write lost", slot, kMaxSlots);
            return;
        }
        bool changed = false;
        {
        std::lock_guard<std::mutex> lock(g_Mutex);

        const Slot& prev = g_Slots[slot];
        changed =
               !prev.used
            ||  prev.realPartsType  != s.realPartsType
            ||  prev.realCamoType   != s.realCamoType
            ||  prev.realArmType    != s.realArmType
            ||  prev.realPlayerType != s.realPlayerType
            ||  prev.developId      != s.developId
            ||  prev.variantIdx     != s.variantIdx;

        g_Slots[slot] = s;
        g_Slots[slot].used = true;
        }

        if (changed)
            outfit::RequestAdditionalMotionReresolve();
    }

    void Clear(std::size_t slot)
    {
        if (slot >= kMaxSlots)
        {
            static std::atomic<int> s_oobClear{ 0 };
            if (s_oobClear.fetch_add(1, std::memory_order_relaxed) < 8)
                LogWarn("ShadowState: parts slot %zu past the %zu-slot mirror, drop "
                    "lost", slot, kMaxSlots);
            return;
        }
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_Slots[slot] = Slot{};
    }

    void ResetAll(const char*)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (auto& s : g_Slots) s = Slot{};
    }

    bool Get(std::size_t slot, Slot* out)
    {
        if (slot >= kMaxSlots) return false;
        std::lock_guard<std::mutex> lock(g_Mutex);
        if (!g_Slots[slot].used) return false;
        if (out) *out = g_Slots[slot];
        return true;
    }

    bool IsActive(std::size_t slot)
    {
        if (slot >= kMaxSlots) return false;
        std::lock_guard<std::mutex> lock(g_Mutex);
        return g_Slots[slot].used;
    }

    void SetArmTier(std::uint32_t playerType, std::uint8_t tier)
    {
        const std::uint32_t pt = playerType & 0xFFu;
        if (pt >= kPlayerTypeMax) return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_ArmTier[pt]         = tier;
        g_ArmTierCaptured[pt] = true;
    }

    std::uint8_t GetArmTier(std::uint32_t playerType, bool* outCaptured)
    {
        const std::uint32_t pt = playerType & 0xFFu;
        if (pt >= kPlayerTypeMax)
        {
            if (outCaptured) *outCaptured = false;
            return 0;
        }
        std::lock_guard<std::mutex> lock(g_Mutex);
        if (outCaptured) *outCaptured = g_ArmTierCaptured[pt];
        return g_ArmTier[pt];
    }

    void ResetArmTierCache()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (auto& v : g_ArmTier)         v = 0;
        for (auto& v : g_ArmTierCaptured) v = false;
    }

    void         SetCurrentSlot(std::size_t slot) { tl_CurrentSlot = slot; }
    void         ClearCurrentSlot()               { tl_CurrentSlot = SIZE_MAX; }
    std::size_t  GetCurrentSlot()                 { return tl_CurrentSlot; }
    bool         HasCurrentSlot()                 { return tl_CurrentSlot != SIZE_MAX; }
}
