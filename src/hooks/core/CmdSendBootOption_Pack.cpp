#include <pch.h>
#include "CmdSendBootOption_Pack.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "DevelopArrayGrow.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using Pack_t = std::uint64_t(__fastcall*)(void* self);

    constexpr std::size_t kDevelopCountList    = 0x2DC;   // send_record.develop_count: developed R&D rows per develop type
    constexpr std::size_t kDevelopCountListNum = 0x36C;

    constexpr std::uint8_t kDevelopCountTypes[] = {   // develop type behind each develop_count entry, in send order
        1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13,
        15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26,
        27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38
    };
    constexpr std::uint32_t kDevelopCountMax = sizeof(kDevelopCountTypes);

    const std::uint8_t kPrologue154[] = {
        0x40, 0x55, 0x48, 0x8B, 0xEC, 0x48, 0x83, 0xEC, 0x40,
        0x48, 0xC7, 0x45, 0xE0, 0xFE, 0xFF, 0xFF, 0xFF,
        0x48, 0x89, 0x5C, 0x24, 0x50
    };

    struct PackTarget
    {
        std::uintptr_t      addr;
        const std::uint8_t* prologue;
        std::size_t         size;
    };

    Pack_t g_Orig = nullptr;
    void*  g_Addr = nullptr;

    std::atomic<bool> g_ScanFailLogged{ false };
    std::atomic<bool> g_CountMismatchLogged{ false };

    PackTarget Target()
    {
        return { gAddr.CmdSendBootOption_Pack, kPrologue154, sizeof(kPrologue154) };
    }

    bool PrologueMatchesSEH(const void* fn, const PackTarget& target)
    {
        __try
        {
            return std::memcmp(fn, target.prologue, target.size) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool SendVanillaCountsSEH(std::uint8_t* self, const std::uint32_t* vanillaByType,
                              const std::uint32_t* customByType)
    {
        bool matched = true;
        __try
        {
            std::uint32_t entries = *reinterpret_cast<const std::uint32_t*>(self + kDevelopCountListNum);
            if (entries > kDevelopCountMax)
                entries = kDevelopCountMax;
            auto* counts = reinterpret_cast<std::uint32_t*>(self + kDevelopCountList);
            for (std::uint32_t i = 0; i < entries; ++i)
            {
                const std::uint8_t  type    = kDevelopCountTypes[i];
                const std::uint32_t vanilla = vanillaByType[type];
                if (counts[i] != vanilla && counts[i] != vanilla + customByType[type])
                    matched = false;
                counts[i] = vanilla;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            matched = false;
        }
        return matched;
    }

    std::uint64_t __fastcall hkPack(void* self)
    {
        if (self)
        {
            std::uint32_t vanillaByType[equip::kDevelopTypeSlots];
            std::uint32_t customByType[equip::kDevelopTypeSlots];
            if (!equip::CountDevelopedRowsByType(vanillaByType, customByType, equip::kDevelopTypeSlots))
            {
                if (!g_ScanFailLogged.exchange(true))
                    LogWarn("BootRecordGuard: R&D table unreadable, CMD_SEND_BOOT develop_count sent as is");
            }
            else if (!SendVanillaCountsSEH(static_cast<std::uint8_t*>(self), vanillaByType, customByType)
                     && !g_CountMismatchLogged.exchange(true))
            {
                LogWarn("BootRecordGuard: CMD_SEND_BOOT develop_count did not match the R&D table, sent the counts of the game's own rows");
            }
        }
        return g_Orig(self);
    }
}

bool Install_CmdSendBootOption_Pack()
{
    const PackTarget target = Target();
    if (target.addr == 0)
    {
        LogWarn("BootRecordGuard: no CMD_SEND_BOOT pack address for %s, developed custom R&D rows are counted in develop_count",
            GetGameBuildName(gGameBuild));
        return true;
    }

    void* fn = ResolveGameAddress(target.addr);
    char entry[160] = {};
    const bool matches = fn && PrologueMatchesSEH(fn, target);
    const bool onTop   = fn && !matches && EntryHookedByOtherModule(fn);
    if (!matches && !onTop)
    {
        DescribeFunctionEntry(fn, entry, sizeof(entry));
        LogError("BootRecordGuard: CMD_SEND_BOOT pack prologue mismatch on %s (%s), developed custom R&D rows are counted in develop_count",
            GetGameBuildName(gGameBuild), entry);
        return false;
    }

    if (!CreateAndEnableHook(fn, reinterpret_cast<void*>(&hkPack),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("BootRecordGuard: CMD_SEND_BOOT pack hook failed on %s, developed custom R&D rows are counted in develop_count",
            GetGameBuildName(gGameBuild));
        return false;
    }

    if (onTop)
    {
        DescribeFunctionEntry(fn, entry, sizeof(entry));
        LogInfo("BootRecordGuard: CMD_SEND_BOOT pack already hooked (%s), guard installed on top", entry);
    }
    g_Addr = fn;
    return true;
}

void Uninstall_CmdSendBootOption_Pack()
{
    if (g_Addr)
    {
        DisableAndRemoveHook(g_Addr);
        g_Addr = nullptr;
    }
    g_Orig = nullptr;
}
