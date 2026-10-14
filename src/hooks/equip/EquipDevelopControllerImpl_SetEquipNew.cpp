#include "pch.h"
#include "EquipDevelopControllerImpl_SetEquipNew.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>

#include "DevelopArrayGrow.h"
#include "EquipDevelop_SetEquipUndeveloped.h"
#include "HookUtils.h"
#include "V_FrameWorkState.h"
#include "log.h"

namespace
{
    constexpr std::size_t kSetEquipNewSlot          = 0x258 / sizeof(void*);
    constexpr std::size_t kSetEquipNewDevelopedSlot = 0x280 / sizeof(void*);
    constexpr std::size_t kEnableDevelopSlot        = 0x1B8 / sizeof(void*);
    constexpr int         kMaxThunkHops             = 4;
    constexpr std::uint8_t kJumpRel32               = 0xE9;

    using SetEquipNew_t = void(__fastcall*)(void* controller, std::uint16_t index,
                                            std::uint8_t isNew);
    using RowPredicate_t = char(__fastcall*)(void* controller, std::uint16_t index);

    SetEquipNew_t  g_OrigUndeveloped   = nullptr;
    SetEquipNew_t  g_OrigDeveloped     = nullptr;
    RowPredicate_t g_OrigEnableDevelop = nullptr;

    void* g_TargetUndeveloped   = nullptr;
    void* g_TargetDeveloped     = nullptr;
    void* g_TargetEnableDevelop = nullptr;

    std::atomic<bool> g_Installed{ false };

    bool BytesPresent(const std::uint8_t* code, std::size_t span,
                      const std::uint8_t* want, std::size_t wantLen)
    {
        __try
        {
            for (std::size_t i = 0; i + wantLen <= span; ++i)
            {
                std::size_t j = 0;
                while (j < wantLen && code[i + j] == want[j])
                    ++j;
                if (j == wantLen)
                    return true;
            }
            return false;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool LooksLikeEnableDevelop(void* target)
    {
        static const std::uint8_t kRecordBit[] =
            { 0x0F, 0xB6, 0x44, 0x08, 0x15, 0xC1, 0xE8, 0x04, 0x83, 0xE0, 0x01 };
        return BytesPresent(static_cast<const std::uint8_t*>(target), 0x20,
                            kRecordBit, sizeof(kRecordBit));
    }

    bool InGameImage(std::uintptr_t addr)
    {
        const std::uintptr_t base = GetExeBase();
        if (!base || addr <= base)
            return false;
        __try
        {
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE)
                return false;
            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
                base + static_cast<std::uintptr_t>(dos->e_lfanew));
            if (nt->Signature != IMAGE_NT_SIGNATURE)
                return false;
            return addr < base + nt->OptionalHeader.SizeOfImage;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool FollowJumpThunks(void* at, void*& out)
    {
        std::uintptr_t p = reinterpret_cast<std::uintptr_t>(at);
        for (int hop = 0; hop < kMaxThunkHops; ++hop)
        {
            if (!InGameImage(p))
                return false;
            std::uint8_t opcode = 0;
            std::int32_t rel    = 0;
            __try
            {
                opcode = *reinterpret_cast<const std::uint8_t*>(p);
                if (opcode == kJumpRel32)
                    rel = *reinterpret_cast<const std::int32_t*>(p + 1);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
            if (opcode != kJumpRel32)
            {
                out = reinterpret_cast<void*>(p);
                return true;
            }
            p += static_cast<std::uintptr_t>(5 + static_cast<std::intptr_t>(rel));
        }
        return false;
    }

    bool ReadVtableSlot(void* controller, std::size_t slot, void*& out)
    {
        __try
        {
            void** vtbl = *reinterpret_cast<void***>(controller);
            if (!vtbl)
                return false;
            out = vtbl[slot];
            return out != nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ResolveManagedRow(std::uint16_t index, std::int32_t& developId)
    {
        if (index < equip::FirstCustomFlowIndex())
            return false;
        std::uint32_t rowDevelopId = 0;
        if (!equip::TryReadRowDevelopId(index, rowDevelopId) || rowDevelopId == 0)
            return false;
        developId = static_cast<std::int32_t>(rowDevelopId);
        return V_FrameWorkState::IsManagedDevelopId(developId);
    }

    void GuardedSet(SetEquipNew_t orig, void* controller,
                    std::uint16_t index, std::uint8_t isNew)
    {
        if (!orig)
            return;

        std::int32_t developId = 0;
        if (!ResolveManagedRow(index, developId))
        {
            orig(controller, index, isNew);
            return;
        }

        if (!isNew)
        {
            orig(controller, index, 0);
            V_FrameWorkState::SetNewByDevelopId(developId, false);
            return;
        }

        orig(controller, index,
             V_FrameWorkState::GetNewByDevelopId(developId) ? 1 : 0);
    }

    void __fastcall hkSetEquipNew(void* controller, std::uint16_t index,
                                  std::uint8_t isNew)
    {
        GuardedSet(g_OrigUndeveloped, controller, index, isNew);
    }

    void __fastcall hkSetEquipNewDeveloped(void* controller, std::uint16_t index,
                                           std::uint8_t isNew)
    {
        GuardedSet(g_OrigDeveloped, controller, index, isNew);
    }

    char __fastcall hkIsEnableEquipDevelop(void* controller, std::uint16_t index)
    {
        const char answer = g_OrigEnableDevelop
                            ? g_OrigEnableDevelop(controller, index) : 0;
        if (answer == 0)
            return answer;

        std::int32_t developId = 0;
        if (!ResolveManagedRow(index, developId))
            return answer;

        if (EquipDevelop_IsDevelopedByDevelopId(static_cast<std::uint32_t>(developId)))
            return 0;
        return EquipDevelop_IsParentDeveloped(controller, static_cast<std::uint32_t>(developId))
               ? answer : 0;   // a grade stays shut until the one before it has finished developing
    }

    void InstallDevelopAvailableGate(void* controller)
    {
        void* slotTarget = nullptr;
        void* target     = nullptr;
        if (!ReadVtableSlot(controller, kEnableDevelopSlot, slotTarget)
            || !FollowJumpThunks(slotTarget, target)
            || !LooksLikeEnableDevelop(target))
        {
            LogError("EquipDevelop: develop-available gate not installed (vtbl+0x1B8 = "
                     "%p), developed custom rows keep the NEW ribbon", slotTarget);
            return;
        }
        if (!CreateAndEnableHook(target, &hkIsEnableEquipDevelop,
                                 reinterpret_cast<void**>(&g_OrigEnableDevelop)))
        {
            g_OrigEnableDevelop = nullptr;
            LogError("EquipDevelop: develop-available gate hook failed at %p, developed "
                     "custom rows keep the NEW ribbon", target);
            return;
        }
        g_TargetEnableDevelop = target;
    }

    void InstallNewBadgeGuard(void* undevelopedSlot, void* developedSlot)
    {
        void* undeveloped = nullptr;
        void* developed   = nullptr;
        if (!FollowJumpThunks(undevelopedSlot, undeveloped)
            || !FollowJumpThunks(developedSlot, developed))
        {
            LogError("EquipDevelop: NEW-badge guard not installed (vtbl+0x258/0x280 = "
                     "%p/%p), seen custom rows badge again",
                     undevelopedSlot, developedSlot);
            return;
        }

        const bool a = CreateAndEnableHook(
            undeveloped, &hkSetEquipNew,
            reinterpret_cast<void**>(&g_OrigUndeveloped));
        const bool b = CreateAndEnableHook(
            developed, &hkSetEquipNewDeveloped,
            reinterpret_cast<void**>(&g_OrigDeveloped));

        if (a)
            g_TargetUndeveloped = undeveloped;
        else
            g_OrigUndeveloped = nullptr;
        if (b)
            g_TargetDeveloped = developed;
        else
            g_OrigDeveloped = nullptr;

        if (!a || !b)
            LogError("EquipDevelop: NEW-badge guard hook failed (undeveloped %p=%d "
                     "developed %p=%d), seen custom rows badge again",
                     undeveloped, a ? 1 : 0, developed, b ? 1 : 0);
    }
}

void EquipDevelop_ArmSetEquipNewGuard(void* controller)
{
    if (!controller || g_Installed.load(std::memory_order_relaxed))
        return;

    void* undeveloped = nullptr;
    void* developed   = nullptr;
    if (!ReadVtableSlot(controller, kSetEquipNewSlot, undeveloped)
        || !ReadVtableSlot(controller, kSetEquipNewDevelopedSlot, developed))
        return;

    bool expected = false;
    if (!g_Installed.compare_exchange_strong(expected, true,
                                             std::memory_order_relaxed))
        return;

    InstallDevelopAvailableGate(controller);
    InstallNewBadgeGuard(undeveloped, developed);
}

void EquipDevelop_ReleaseSetEquipNewGuard()
{
    if (!g_Installed.load(std::memory_order_relaxed))
        return;

    if (g_TargetUndeveloped)
        DisableAndRemoveHook(g_TargetUndeveloped);
    if (g_TargetDeveloped)
        DisableAndRemoveHook(g_TargetDeveloped);
    if (g_TargetEnableDevelop)
        DisableAndRemoveHook(g_TargetEnableDevelop);

    g_TargetUndeveloped   = nullptr;
    g_TargetDeveloped     = nullptr;
    g_TargetEnableDevelop = nullptr;

    g_OrigUndeveloped   = nullptr;
    g_OrigDeveloped     = nullptr;
    g_OrigEnableDevelop = nullptr;

    g_Installed.store(false, std::memory_order_relaxed);
}
