#include <pch.h>
#include "CpControllerImpl_GetTargetPosition.h"

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
    using GetCpPhase_t        = std::int32_t(__fastcall*)(void* cpController, std::uint32_t heliIndex);
    using GetCpPhaseCount_t   = float(__fastcall*)(void* cpController, std::uint32_t heliIndex);
    using GetTargetPosition_t = float*(__fastcall*)(void* cpController, float* out, std::uint32_t heliIndex);

    constexpr std::size_t  kHeliCpController      = 0xC0;
    constexpr std::size_t  kCpControllerHeli      = 0x38;
    constexpr std::size_t  kHeliType              = 0x130;
    constexpr std::size_t  kSlotGetCpPhase        = 0x10 / sizeof(void*);
    constexpr std::size_t  kSlotGetCpPhaseCount   = 0x18 / sizeof(void*);
    constexpr std::size_t  kSlotGetTargetPosition = 0x20 / sizeof(void*);
    constexpr std::int32_t kPhaseCombat           = 3;
    constexpr float        kPhaseCountCombat      = 1.0f;

    constexpr std::uint8_t kGetCpPhaseBytes[] = {
        0x48, 0x83, 0xEC, 0x28, 0x8B, 0xC2, 0x44, 0x8B, 0xCA, 0x48, 0x03, 0x41, 0x40, 0x44, 0x0F, 0xB6,
        0x00, 0x41, 0xF6, 0xC0, 0x02, 0x74, 0x0A, 0xB8, 0x03, 0x00, 0x00, 0x00, 0x48, 0x83, 0xC4, 0x28
    };
    constexpr std::uint8_t kGetCpPhaseCountBytes[] = {
        0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0x41, 0x40, 0x44, 0x8B, 0xCA, 0x41, 0xF6, 0x04, 0x01, 0x01,
        0x75, 0x08, 0x0F, 0x57, 0xC0, 0x48, 0x83, 0xC4, 0x28, 0xC3
    };
    constexpr std::size_t  kGetTargetPositionBody    = 0x1B;   // past the stack-cookie load
    constexpr std::uint8_t kGetTargetPositionBytes[] = {
        0x48, 0x8B, 0x41, 0x38, 0x48, 0x8B, 0xF1, 0xB9, 0x00, 0xFE, 0x00, 0x00, 0x48, 0x8B, 0x80, 0xA0, 0x00, 0x00, 0x00
    };

    enum : int { kUnseen, kBusy, kSwapped, kRefused };

    GetCpPhase_t        g_OrigGetCpPhase        = nullptr;
    GetCpPhaseCount_t   g_OrigGetCpPhaseCount   = nullptr;
    GetTargetPosition_t g_OrigGetTargetPosition = nullptr;
    void**              g_Vtable                = nullptr;
    std::atomic<int>    g_State{ kUnseen };

    void** VtableSEH(const void* heli)
    {
        __try
        {
            void* cpController = *reinterpret_cast<void* const*>(static_cast<const std::uint8_t*>(heli) + kHeliCpController);
            return cpController ? *reinterpret_cast<void***>(cpController) : nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    bool Matches(const void* at, const std::uint8_t* bytes, std::size_t size)
    {
        std::uint8_t read[32] = {};
        return at && size <= sizeof(read) && ReadEntryBytesSEH(at, read, size) && std::memcmp(read, bytes, size) == 0;
    }

    std::uint16_t HeliIdSEH(void* cpController, std::uint32_t heliIndex)
    {
        __try
        {
            const std::uint8_t* heli = *reinterpret_cast<const std::uint8_t* const*>(static_cast<std::uint8_t*>(cpController) + kCpControllerHeli);
            const std::uint16_t type = heli ? *reinterpret_cast<const std::uint16_t*>(heli + kHeliType) : 0;
            if (!HeliKey::IsHeliType(type) || heliIndex > 0x1FFu)
                return 0;
            return static_cast<std::uint16_t>((type << 9) | heliIndex);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    std::uint16_t FightingHeli(void* cpController, std::uint32_t heliIndex)
    {
        if (MissionCodeGuard::ShouldBypassHooks())
            return 0;
        const std::uint16_t heliId = HeliIdSEH(cpController, heliIndex);
        return heliId != 0 && SoldierFactionHeli_IsFighting(heliId) ? heliId : 0;
    }

    float* __fastcall hkGetTargetPosition(void* cpController, float* out, std::uint32_t heliIndex)
    {
        alignas(16) float point[4];
        const std::uint16_t heliId = FightingHeli(cpController, heliIndex);
        if (heliId != 0 && SoldierFactionHeli_AimPoint(heliId, point))   // aim, flight and the attack checks all ask here
        {
            std::memcpy(out, point, sizeof(point));
            return out;
        }
        return g_OrigGetTargetPosition(cpController, out, heliIndex);
    }

    std::int32_t __fastcall hkGetCpPhase(void* cpController, std::uint32_t heliIndex)
    {
        if (FightingHeli(cpController, heliIndex) != 0)   // its combat AI runs only in phase 3
            return kPhaseCombat;
        return g_OrigGetCpPhase(cpController, heliIndex);
    }

    float __fastcall hkGetCpPhaseCount(void* cpController, std::uint32_t heliIndex)
    {
        const float count = g_OrigGetCpPhaseCount(cpController, heliIndex);
        if (FightingHeli(cpController, heliIndex) != 0 && count < kPhaseCountCombat)   // at 0 the combat AI only searches
            return kPhaseCountCombat;
        return count;
    }
}

void CpControllerImpl_GetTargetPosition_Watch(void* heli)
{
    if (!heli || g_State.load(std::memory_order_acquire) != kUnseen)
        return;
    void** vtbl = VtableSEH(heli);
    if (!vtbl)
        return;
    int expected = kUnseen;
    if (!g_State.compare_exchange_strong(expected, kBusy, std::memory_order_acq_rel))
        return;

    if (!Matches(vtbl[kSlotGetCpPhase], kGetCpPhaseBytes, sizeof(kGetCpPhaseBytes))
        || !Matches(vtbl[kSlotGetCpPhaseCount], kGetCpPhaseCountBytes, sizeof(kGetCpPhaseCountBytes))
        || !Matches(static_cast<std::uint8_t*>(vtbl[kSlotGetTargetPosition]) + kGetTargetPositionBody,
                    kGetTargetPositionBytes, sizeof(kGetTargetPositionBytes)))
    {
        LogWarn("SoldierFaction: heli CP controller not recognised, faction helis keep the vanilla target and alert phase");
        g_State.store(kRefused, std::memory_order_release);
        return;
    }
    if (!SwapVtableSlot(&vtbl[kSlotGetTargetPosition], reinterpret_cast<void*>(&hkGetTargetPosition), reinterpret_cast<void**>(&g_OrigGetTargetPosition))
        || !SwapVtableSlot(&vtbl[kSlotGetCpPhaseCount], reinterpret_cast<void*>(&hkGetCpPhaseCount), reinterpret_cast<void**>(&g_OrigGetCpPhaseCount))
        || !SwapVtableSlot(&vtbl[kSlotGetCpPhase], reinterpret_cast<void*>(&hkGetCpPhase), reinterpret_cast<void**>(&g_OrigGetCpPhase)))
    {
        RestoreVtableSlot(&vtbl[kSlotGetTargetPosition], reinterpret_cast<void*>(g_OrigGetTargetPosition));
        RestoreVtableSlot(&vtbl[kSlotGetCpPhaseCount], reinterpret_cast<void*>(g_OrigGetCpPhaseCount));
        LogError("SoldierFaction: heli CP controller hook failed, faction helis keep the vanilla target and alert phase");
        g_State.store(kRefused, std::memory_order_release);
        return;
    }
    g_Vtable = vtbl;
    g_State.store(kSwapped, std::memory_order_release);
}

void Uninstall_CpControllerImpl_GetTargetPosition_Hook()
{
    if (g_State.load(std::memory_order_acquire) == kSwapped && g_Vtable)
    {
        RestoreVtableSlot(&g_Vtable[kSlotGetCpPhase], reinterpret_cast<void*>(g_OrigGetCpPhase));
        RestoreVtableSlot(&g_Vtable[kSlotGetCpPhaseCount], reinterpret_cast<void*>(g_OrigGetCpPhaseCount));
        RestoreVtableSlot(&g_Vtable[kSlotGetTargetPosition], reinterpret_cast<void*>(g_OrigGetTargetPosition));
    }
    g_Vtable = nullptr;
    g_State.store(kUnseen, std::memory_order_release);
}
