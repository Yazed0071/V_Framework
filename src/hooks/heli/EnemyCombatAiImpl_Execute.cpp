#include <pch.h>
#include "EnemyCombatAiImpl_Execute.h"

#include <Windows.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"
#include "HeliKey.h"
#include "../soldier/SoldierFaction.h"
#include "../soldier/SoldierFactionHeli.h"

namespace
{
    using NodeCall_t = void(__fastcall*)(void* node, std::uint32_t heliIndex, void* context);

    constexpr std::size_t kSlotShouldSleep = 0x10 / sizeof(void*);
    constexpr std::size_t kSlotDispatch    = 0x28 / sizeof(void*);   // runs the current step's check, then switches steps
    constexpr std::size_t kSlotExecute     = 0x30 / sizeof(void*);

    constexpr std::size_t kNodeHeli     = 0x50;
    constexpr std::size_t kNodeRecords  = 0x60;
    constexpr std::size_t kHeliType     = 0x130;
    constexpr std::size_t kRecordStride = 0x170;
    constexpr std::size_t kRecordNoTargetTime = 0x15C;   // the attack checks need it under 5 s; only a player or decoy target resets it
    constexpr std::size_t kRecordRuns   = 0x160;
    constexpr std::size_t kRecordLimit  = 0x164;
    constexpr std::size_t kRecordFamily = 0x168;
    constexpr std::size_t kRecordStep   = 0x169;
    constexpr std::size_t kRecordNext   = 0x16B;

    constexpr std::uint8_t  kStepDecide     = 1;
    constexpr std::uint8_t  kStepHover      = 3;
    constexpr std::uint8_t  kStepStrafe     = 4;
    constexpr std::uint8_t  kStepReposition = 8;
    constexpr std::uint8_t  kNoStep         = 0x0D;
    constexpr std::uint64_t kBlockedBurstMs = 1000;
    constexpr std::uint32_t kMaxRejected    = 64;

    constexpr std::uint8_t kShouldSleepBytes[] = {
        0x48, 0x8B, 0x49, 0x60, 0x44, 0x8B, 0xC2, 0x4D, 0x69, 0xC0, 0x70, 0x01, 0x00, 0x00, 0x41, 0x80,
        0xBC, 0x08, 0x69, 0x01, 0x00, 0x00, 0x0C, 0x0F, 0x94, 0xC0, 0xC3
    };

    enum : int { kUnseen, kBusy, kSwapped, kRefused };

    NodeCall_t       g_OrigDispatch = nullptr;
    NodeCall_t       g_OrigExecute  = nullptr;
    void**           g_Vtable       = nullptr;
    std::atomic<int> g_State{ kUnseen };

    std::array<std::atomic<void**>, kMaxRejected>           g_Rejected{};
    std::atomic<std::uint32_t>                              g_RejectedCount{ 0 };
    std::atomic<std::uint32_t>                              g_Coin{ 0x9E3779B9u };

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

    std::uint8_t* RecordSEH(void* node, std::uint32_t heliIndex, std::uint16_t* heliId)
    {
        __try
        {
            const std::uint8_t* object = static_cast<const std::uint8_t*>(node);
            const std::uint8_t* heli = *reinterpret_cast<const std::uint8_t* const*>(object + kNodeHeli);
            std::uint8_t* records = *reinterpret_cast<std::uint8_t* const*>(object + kNodeRecords);
            const std::uint16_t type = heli ? *reinterpret_cast<const std::uint16_t*>(heli + kHeliType) : 0;
            if (!records || !HeliKey::IsHeliType(type) || heliIndex > 0x1FFu)
                return nullptr;
            *heliId = static_cast<std::uint16_t>((type << 9) | heliIndex);
            return records + static_cast<std::size_t>(heliIndex) * kRecordStride;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    std::uint8_t FightingRecord(void* node, std::uint32_t heliIndex, std::uint8_t** record, std::uint16_t* heliId)
    {
        if (MissionCodeGuard::ShouldBypassHooks())
            return 0;
        *record = RecordSEH(node, heliIndex, heliId);
        return *record && SoldierFactionHeli_IsFighting(*heliId) ? 1 : 0;
    }

    std::uint8_t ReadByteSEH(const std::uint8_t* at)
    {
        __try
        {
            return *at;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0xFFu;
        }
    }

    std::uint8_t AttackStep(std::uint8_t family)   // the engine's own pick: strafe after a hover, else either
    {
        std::uint32_t coin = g_Coin.load(std::memory_order_relaxed);
        coin ^= coin << 13;
        coin ^= coin >> 7;
        coin ^= coin << 5;
        g_Coin.store(coin, std::memory_order_relaxed);
        if (family == kStepHover)
            return kStepStrafe;
        return (coin & 1u) ? kStepHover : kStepStrafe;
    }

    bool ReadRunsSEH(const std::uint8_t* record, std::uint32_t* runs, std::uint32_t* limit, std::uint8_t* family)
    {
        __try
        {
            *runs   = *reinterpret_cast<const std::uint32_t*>(record + kRecordRuns);
            *limit  = *reinterpret_cast<const std::uint32_t*>(record + kRecordLimit);
            *family = record[kRecordFamily];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void WriteNextSEH(std::uint8_t* record, std::uint8_t next)
    {
        __try
        {
            record[kRecordNext] = next;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    bool TendSEH(std::uint8_t* record, std::uint8_t* step, std::uint8_t* next)
    {
        __try
        {
            *reinterpret_cast<float*>(record + kRecordNoTargetTime) = 0.0f;
            *step = record[kRecordStep];
            *next = record[kRecordNext];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void Redirect(std::uint8_t* record, std::uint8_t chosen, std::uint16_t heliId)
    {
        std::uint32_t runs = 0;
        std::uint32_t limit = 0;
        std::uint8_t family = 0;
        if ((chosen != kStepHover && chosen != kStepStrafe && chosen != kStepReposition) || !ReadRunsSEH(record, &runs, &limit, &family))
            return;
        const bool clear = SoldierFactionHeli_LineClear(heliId);
        if (chosen != kStepReposition && !clear)
        {
            WriteNextSEH(record, kStepReposition);   // no clear line: fly for one instead of shooting a wall
        }
        else if (chosen == kStepReposition && clear && runs < limit && SoldierFactionHeli_OutsidePlayerGate(heliId))
        {
            WriteNextSEH(record, AttackStep(family));   // only the 20 m player gate sent it away, not its own ray
        }
    }

    void __fastcall hkDispatch(void* node, std::uint32_t heliIndex, void* context)
    {
        std::uint8_t* record = nullptr;
        std::uint16_t heliId = 0;
        if (!FightingRecord(node, heliIndex, &record, &heliId))
        {
            g_OrigDispatch(node, heliIndex, context);
            return;
        }
        const std::uint8_t before = ReadByteSEH(record + kRecordStep);
        g_OrigDispatch(node, heliIndex, context);
        const std::uint8_t after = ReadByteSEH(record + kRecordStep);
        if (after == before || after == 0xFFu)
            return;
        if (after == kStepDecide)
        {
            SoldierFactionHeli_OnDecision(heliId);
            return;
        }
        if (before == kStepDecide)
            Redirect(record, after, heliId);
    }

    void __fastcall hkExecute(void* node, std::uint32_t heliIndex, void* context)
    {
        g_OrigExecute(node, heliIndex, context);
        std::uint8_t* record = nullptr;
        std::uint16_t heliId = 0;
        std::uint8_t step = 0;
        std::uint8_t next = 0;
        if (!FightingRecord(node, heliIndex, &record, &heliId) || !TendSEH(record, &step, &next))
            return;
        if ((step == kStepHover || step == kStepStrafe) && next == kNoStep
            && SoldierFactionHeli_BlockedForMs(heliId, GetTickCount64()) >= kBlockedBurstMs)
            WriteNextSEH(record, kStepReposition);   // the target slipped behind cover mid-burst
    }

    bool Matches(const void* at, const std::uint8_t* bytes, std::size_t size)
    {
        std::uint8_t read[32] = {};
        return at && size <= sizeof(read) && ReadEntryBytesSEH(at, read, size) && std::memcmp(read, bytes, size) == 0;
    }
}

void EnemyCombatAiImpl_OnNodeWake(void* node)
{
    if (!node || g_State.load(std::memory_order_acquire) != kUnseen || !SoldierFaction_IsModuleActive())
        return;
    void** vtbl = VtableSEH(node);
    if (!vtbl || Rejected(vtbl))
        return;
    void* shouldSleep = nullptr;
    if (!ReadEntryBytesSEH(&vtbl[kSlotShouldSleep], &shouldSleep, sizeof(shouldSleep))
        || !Matches(shouldSleep, kShouldSleepBytes, sizeof(kShouldSleepBytes)))
    {
        Reject(vtbl);
        return;
    }
    int expected = kUnseen;
    if (!g_State.compare_exchange_strong(expected, kBusy, std::memory_order_acq_rel))
        return;
    if (!SwapVtableSlot(&vtbl[kSlotExecute], reinterpret_cast<void*>(&hkExecute), reinterpret_cast<void**>(&g_OrigExecute))
        || !SwapVtableSlot(&vtbl[kSlotDispatch], reinterpret_cast<void*>(&hkDispatch), reinterpret_cast<void**>(&g_OrigDispatch)))
    {
        RestoreVtableSlot(&vtbl[kSlotExecute], reinterpret_cast<void*>(g_OrigExecute));
        LogError("SoldierFaction: heli combat AI hook failed, faction helis attack only near the player");
        g_State.store(kRefused, std::memory_order_release);
        return;
    }
    g_Vtable = vtbl;
    g_State.store(kSwapped, std::memory_order_release);
}

void Uninstall_EnemyCombatAiImpl_Execute_Hook()
{
    if (g_State.load(std::memory_order_acquire) == kSwapped && g_Vtable)
    {
        RestoreVtableSlot(&g_Vtable[kSlotDispatch], reinterpret_cast<void*>(g_OrigDispatch));
        RestoreVtableSlot(&g_Vtable[kSlotExecute], reinterpret_cast<void*>(g_OrigExecute));
    }
    g_Vtable = nullptr;
    g_State.store(kUnseen, std::memory_order_release);
}
