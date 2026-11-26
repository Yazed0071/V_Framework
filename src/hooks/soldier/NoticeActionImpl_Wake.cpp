#include <pch.h>
#include "NoticeActionImpl_Wake.h"

#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "MissionCodeGuard.h"
#include "VIPImportant.h"

namespace
{
    using Wake_t = void(__fastcall*)(void* self, std::uint32_t soldierIndex);

    Wake_t g_OrigWake = nullptr;
    void*  g_WakeTarget = nullptr;

    constexpr std::uint8_t kWakeEntry[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18,
        0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x40 };

    constexpr std::uintptr_t kWorks          = 0x70;  // per-soldier work records, 0x68 each
    constexpr std::uintptr_t kFirstSoldier   = 0x78;
    constexpr std::uintptr_t kSoldier        = 0x58;  // Soldier2Impl
    constexpr std::size_t    kWorkSize       = 0x68;
    constexpr std::uintptr_t kWorkFlags      = 0x5F;  // bit 0 = the comrade being woken is an officer
    constexpr std::uintptr_t kTaskKind       = 0xC6;
    constexpr std::uint8_t   kComradeNotice  = 0x0B;
    constexpr std::uintptr_t kTaskOfficer    = 0x03;  // bit 0, the vanilla AI always clears it
    constexpr std::uintptr_t kSoldierStatus  = 0x110; // status records: array +8, count +0x10, 0x28 each
    constexpr std::size_t    kStatusSize     = 0x28;
    constexpr std::uint64_t  kVipStatusBits  = 0x20000003800ull; // what SetVip and SetVipSpecial both set

    struct ComradeNotice
    {
        std::uint8_t* task = nullptr;
        std::uint8_t* work = nullptr;
        std::uint16_t comrade = 0;
        std::uint64_t comradeStatus = 0;
    };

    std::uintptr_t WakeAddress()
    {
        return gAddr.SoldierNoticeActionImpl_Wake;
    }

    bool ReadComradeNoticeSEH(void* self, std::uint32_t soldierIndex, ComradeNotice& out)
    {
        __try
        {
            auto* base = static_cast<std::uint8_t*>(self);
            auto* works = *reinterpret_cast<std::uint8_t**>(base + kWorks);
            if (!works)
                return false;
            const std::uint32_t first = *reinterpret_cast<std::uint32_t*>(base + kFirstSoldier);
            auto* work = works + static_cast<std::size_t>(soldierIndex - first) * kWorkSize;
            auto* task = *reinterpret_cast<std::uint8_t**>(work);
            if (!task || task[kTaskKind] != kComradeNotice)
                return false;

            const std::uint16_t comradeId = *reinterpret_cast<std::uint16_t*>(task);
            if ((comradeId & 0xFE00u) != 0x0400u) // soldier game object ids are 0x400 | index
                return false;

            out.task = task;
            out.work = work;
            out.comrade = static_cast<std::uint16_t>(comradeId & 0x1FFu);

            auto* soldier = *reinterpret_cast<std::uint8_t**>(base + kSoldier);
            auto* status = soldier ? *reinterpret_cast<std::uint8_t**>(soldier + kSoldierStatus) : nullptr;
            if (status)
            {
                auto* records = *reinterpret_cast<std::uint8_t**>(status + 0x08);
                const std::uint32_t count = *reinterpret_cast<std::uint32_t*>(status + 0x10);
                if (records && out.comrade < count)
                    out.comradeStatus = *reinterpret_cast<std::uint64_t*>(records + out.comrade * kStatusSize);
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void MarkOfficerSEH(const ComradeNotice& notice)
    {
        __try
        {
            notice.task[kTaskOfficer] |= 1;
            notice.work[kWorkFlags] |= 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    void __fastcall hkWake(void* self, std::uint32_t soldierIndex)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigWake, self, soldierIndex);

        g_OrigWake(self, soldierIndex);

        ComradeNotice notice;
        if (!ReadComradeNoticeSEH(self, soldierIndex, notice))
            return;

        if (IsVIPImportantSoldier(notice.comrade) || (notice.comradeStatus & kVipStatusBits) == kVipStatusBits)
            MarkOfficerSEH(notice);
    }
}

bool Install_NoticeActionImpl_Wake_Hook()
{
    const std::uintptr_t address = WakeAddress();
    if (address == 0)
        return true;

    void* target = ResolveGameAddress(address);
    const HookEntry entry = CheckHookEntry(target, kWakeEntry, sizeof(kWakeEntry));
    if (entry == HookEntry::Unknown)
    {
        LogError("OfficerWake: NoticeActionImpl::Wake entry differs at 0x%llX, VIP officer wake lines off",
            static_cast<unsigned long long>(address));
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("OfficerWake: NoticeActionImpl::Wake", target);

    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkWake), reinterpret_cast<void**>(&g_OrigWake)))
    {
        LogError("OfficerWake: NoticeActionImpl::Wake hook failed, VIP officer wake lines off");
        return false;
    }

    g_WakeTarget = target;
    return true;
}

bool Uninstall_NoticeActionImpl_Wake_Hook()
{
    if (g_WakeTarget)
    {
        DisableAndRemoveHook(g_WakeTarget);
        g_WakeTarget = nullptr;
    }
    g_OrigWake = nullptr;
    return true;
}
