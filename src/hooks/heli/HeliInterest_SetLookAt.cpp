#include <pch.h>
#include "HeliInterest_SetLookAt.h"

#include <Windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"
#include "HeliKey.h"
#include "../soldier/SoldierFactionHeli.h"

namespace
{
    using SetLookAt_t = void(__fastcall*)(void* interest, std::uint32_t heliIndex, const float* point);

    constexpr std::size_t   kHeliInterest  = 0xA8;   // what the heli looks at; the last attacker wins it in vanilla
    constexpr std::size_t   kSlotSetLookAt = 0x20 / sizeof(void*);
    constexpr std::uint32_t kHeliTypes     = HeliKey::kLastType - HeliKey::kFirstType + 1;

    constexpr std::uint8_t kSetLookAtBytes[] = {
        0x41, 0x0F, 0x28, 0x00, 0x8B, 0xC2, 0x48, 0x8D, 0x14, 0x40, 0x48, 0x8B, 0x41, 0x40, 0x48, 0xC1,
        0xE2, 0x05, 0x0F, 0x29, 0x44, 0x02, 0x40
    };

    enum : int { kUnseen, kBusy, kSwapped, kRefused };

    SetLookAt_t      g_OrigSetLookAt = nullptr;
    void**           g_Vtable        = nullptr;
    std::atomic<int> g_State{ kUnseen };

    std::atomic<void*> g_Interest[kHeliTypes] = {};   // the interest object of each heli type, to tell whose point is set

    void* InterestSEH(const void* heli)
    {
        __try
        {
            return *reinterpret_cast<void* const*>(static_cast<const std::uint8_t*>(heli) + kHeliInterest);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    void** VtableSEH(void* object)
    {
        __try
        {
            return *reinterpret_cast<void***>(object);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    std::uint16_t TypeOf(void* interest)
    {
        for (std::uint32_t i = 0; i < kHeliTypes; ++i)
        {
            if (g_Interest[i].load(std::memory_order_relaxed) == interest)
                return static_cast<std::uint16_t>(HeliKey::kFirstType + i);
        }
        return 0;
    }

    void __fastcall hkSetLookAt(void* interest, std::uint32_t heliIndex, const float* point)
    {
        const std::uint16_t type = MissionCodeGuard::ShouldBypassHooks() ? 0 : TypeOf(interest);
        if (type != 0 && heliIndex <= 0x1FFu)
        {
            const std::uint16_t heliId = static_cast<std::uint16_t>((type << 9) | heliIndex);
            alignas(16) float target[4];
            if (SoldierFactionHeli_IsFighting(heliId) && SoldierFactionHeli_AimPoint(heliId, target))   // keeps its eyes on the target, not on whoever hit it last
            {
                g_OrigSetLookAt(interest, heliIndex, target);
                return;
            }
        }
        g_OrigSetLookAt(interest, heliIndex, point);
    }

    bool Matches(const void* at)
    {
        std::uint8_t read[sizeof(kSetLookAtBytes)] = {};
        return at && ReadEntryBytesSEH(at, read, sizeof(read)) && std::memcmp(read, kSetLookAtBytes, sizeof(read)) == 0;
    }
}

void HeliInterest_SetLookAt_Watch(void* heli, std::uint16_t heliType)
{
    if (!heli || !HeliKey::IsHeliType(heliType))
        return;
    void* interest = InterestSEH(heli);
    if (!interest)
        return;
    g_Interest[heliType - HeliKey::kFirstType].store(interest, std::memory_order_relaxed);
    if (g_State.load(std::memory_order_acquire) != kUnseen)
        return;
    void** vtbl = VtableSEH(interest);
    if (!vtbl)
        return;
    int expected = kUnseen;
    if (!g_State.compare_exchange_strong(expected, kBusy, std::memory_order_acq_rel))
        return;
    void* setLookAt = nullptr;
    if (!ReadEntryBytesSEH(&vtbl[kSlotSetLookAt], &setLookAt, sizeof(setLookAt)) || !Matches(setLookAt))
    {
        LogWarn("SoldierFaction: heli look-at not recognised, faction helis turn to whoever hit them last");
        g_State.store(kRefused, std::memory_order_release);
        return;
    }
    if (!SwapVtableSlot(&vtbl[kSlotSetLookAt], reinterpret_cast<void*>(&hkSetLookAt), reinterpret_cast<void**>(&g_OrigSetLookAt)))
    {
        LogError("SoldierFaction: heli look-at hook failed, faction helis turn to whoever hit them last");
        g_State.store(kRefused, std::memory_order_release);
        return;
    }
    g_Vtable = vtbl;
    g_State.store(kSwapped, std::memory_order_release);
}

void HeliInterest_SetLookAt_Forget()
{
    for (auto& interest : g_Interest)
        interest.store(nullptr, std::memory_order_relaxed);
}

void Uninstall_HeliInterest_SetLookAt_Hook()
{
    if (g_State.load(std::memory_order_acquire) == kSwapped && g_Vtable)
        RestoreVtableSlot(&g_Vtable[kSlotSetLookAt], reinterpret_cast<void*>(g_OrigSetLookAt));
    g_Vtable = nullptr;
    HeliInterest_SetLookAt_Forget();
    g_State.store(kUnseen, std::memory_order_release);
}
