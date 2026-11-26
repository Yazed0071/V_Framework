#include <pch.h>
#include "NoticeTurretAiImpl_ShouldSleep.h"

#include <Windows.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"
#include "SoldierFaction.h"
#include "SoldierFactionAntiAir.h"

namespace
{
    using ShouldSleep_t = bool(__fastcall*)(void* node, std::uint32_t soldierIndex, void* const* context);

    constexpr std::size_t  kSlotShouldWake  = 0x08 / sizeof(void*);
    constexpr std::size_t  kSlotShouldSleep = 0x10 / sizeof(void*);
    constexpr std::size_t  kNodeSoldierCore = 0x50;
    constexpr std::size_t  kCoreCommandPost = 0xD0;
    constexpr std::size_t  kPostMembers     = 0x10;
    constexpr std::size_t  kMemberStride    = 0x20;
    constexpr std::size_t  kMemberRole      = 0x1D;
    constexpr std::uint8_t kRoleAntiAir     = 6;
    constexpr std::size_t  kWorkStep        = 0x54;
    constexpr std::uint8_t kStepGaveUp      = 5;    // could not reserve the gun in time
    constexpr std::uint32_t kMaxRejected    = 64;
    constexpr std::uint32_t kSoldierCount   = 0x200u;

    constexpr std::uint8_t kShouldWakeBytes[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x41, 0x50, 0x48, 0x8B,
        0xF9, 0x8B, 0xDA, 0x48, 0x8B, 0x48, 0x68, 0x48, 0x8B, 0x01, 0xFF, 0x50, 0x10, 0x48, 0x85, 0xC0,
        0x74, 0x2E, 0x80, 0x38, 0x07, 0x75, 0x29, 0x48
    };

    enum : int { kUnseen, kBusy, kSwapped, kRefused };

    ShouldSleep_t    g_OrigShouldSleep = nullptr;
    void**           g_Vtable          = nullptr;
    std::atomic<int> g_State{ kUnseen };

    std::array<std::atomic<void**>, kMaxRejected> g_Rejected{};
    std::atomic<std::uint32_t>                    g_RejectedCount{ 0 };

    void** VtableSEH(void* node)
    {
        __try
        {
            return *reinterpret_cast<void***>(node);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    bool Rejected(void** vtbl)
    {
        const std::uint32_t count = g_RejectedCount.load(std::memory_order_acquire);
        for (std::uint32_t i = 0; i < count && i < kMaxRejected; ++i)
        {
            if (g_Rejected[i].load(std::memory_order_relaxed) == vtbl)
                return true;
        }
        return false;
    }

    void Reject(void** vtbl)
    {
        const std::uint32_t slot = g_RejectedCount.fetch_add(1, std::memory_order_acq_rel);
        if (slot < kMaxRejected)
            g_Rejected[slot].store(vtbl, std::memory_order_release);
    }

    bool OnGunSEH(void* node, std::uint32_t soldierIndex, void* const* context, bool* gaveUp)
    {
        __try
        {
            const std::uint8_t* core    = *reinterpret_cast<const std::uint8_t* const*>(static_cast<const std::uint8_t*>(node) + kNodeSoldierCore);
            const std::uint8_t* post    = *reinterpret_cast<const std::uint8_t* const*>(core + kCoreCommandPost);
            const std::uint8_t* members = *reinterpret_cast<const std::uint8_t* const*>(post + kPostMembers);
            if (members[soldierIndex * kMemberStride + kMemberRole] != kRoleAntiAir)
                return false;
            const std::uint8_t* work = static_cast<const std::uint8_t*>(context[0]);
            *gaveUp = work && work[kWorkStep] == kStepGaveUp;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool __fastcall hkShouldSleep(void* node, std::uint32_t soldierIndex, void* const* context)
    {
        if (!MissionCodeGuard::ShouldBypassHooks() && soldierIndex < kSoldierCount && context
            && SoldierFactionAntiAir_IsGunner(soldierIndex))
        {
            bool gaveUp = false;
            if (OnGunSEH(node, soldierIndex, context, &gaveUp))   // vanilla also wants the support heli's anti-air flag on his post
            {
                if (gaveUp)
                    SoldierFactionAntiAir_OnGaveUp(soldierIndex);
                return gaveUp;
            }
        }
        return g_OrigShouldSleep(node, soldierIndex, context);
    }

    bool Matches(const void* at)
    {
        std::uint8_t read[sizeof(kShouldWakeBytes)] = {};
        return at && ReadEntryBytesSEH(at, read, sizeof(read)) && std::memcmp(read, kShouldWakeBytes, sizeof(read)) == 0;
    }
}

void NoticeTurretAiImpl_OnNodeWake(void* node)
{
    if (!node || g_State.load(std::memory_order_acquire) != kUnseen || !SoldierFaction_IsModuleActive())
        return;
    void** vtbl = VtableSEH(node);
    if (!vtbl || Rejected(vtbl))
        return;
    void* shouldWake = nullptr;
    if (!ReadEntryBytesSEH(&vtbl[kSlotShouldWake], &shouldWake, sizeof(shouldWake)) || !Matches(shouldWake))
    {
        Reject(vtbl);
        return;
    }
    int expected = kUnseen;
    if (!g_State.compare_exchange_strong(expected, kBusy, std::memory_order_acq_rel))
        return;
    if (!SwapVtableSlot(&vtbl[kSlotShouldSleep], reinterpret_cast<void*>(&hkShouldSleep), reinterpret_cast<void**>(&g_OrigShouldSleep)))
    {
        LogError("SoldierFaction: anti-air gunner node hook failed, faction soldiers leave the anti-air guns at once");
        g_State.store(kRefused, std::memory_order_release);
        return;
    }
    g_Vtable = vtbl;
    g_State.store(kSwapped, std::memory_order_release);
}

void Uninstall_NoticeTurretAiImpl_ShouldSleep_Hook()
{
    if (g_State.load(std::memory_order_acquire) == kSwapped && g_Vtable)
        RestoreVtableSlot(&g_Vtable[kSlotShouldSleep], reinterpret_cast<void*>(g_OrigShouldSleep), reinterpret_cast<void*>(&hkShouldSleep));
    g_Vtable = nullptr;
    g_State.store(kUnseen, std::memory_order_release);
}
