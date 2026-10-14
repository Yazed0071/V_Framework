#include "pch.h"
#include "CmdSyncSoldierBinOption_Pack.h"

#include <Windows.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "StaffServerSanitize.h"
#include "log.h"

namespace
{
    using Pack_t = std::uint64_t(__fastcall*)(void* self);

    constexpr std::size_t   kSectionLevels = 0x50;
    constexpr std::size_t   kSectionCounts = 0x6C;
    constexpr std::size_t   kEntries       = 0x88;
    constexpr std::size_t   kEntrySize     = 0x18;   // section word, rank buckets, header, seed, status, flags - big-endian
    constexpr std::uint32_t kEntryCapacity = 3500;
    constexpr std::size_t   kSoldierNum    = 0x148A8;

    const std::uint8_t kPrologue154[] = {
        0x48, 0x8B, 0xC4, 0x55, 0x57, 0x41, 0x56, 0x48, 0x8B, 0xEC, 0x48, 0x83, 0xEC, 0x70,
        0x48, 0xC7, 0x45, 0xC0, 0xFE, 0xFF, 0xFF, 0xFF, 0x48, 0x89, 0x58, 0x18, 0x48, 0x89, 0x70, 0x20
    };
    const std::uint8_t kPrologue153[] = {
        0x48, 0x89, 0xE0, 0x55, 0x57, 0x41, 0x56, 0x48, 0x89, 0xE5, 0x48, 0x83, 0xEC, 0x70,
        0x48, 0xC7, 0x85, 0xC0, 0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF,
        0x48, 0x89, 0x58, 0x18, 0x48, 0x89, 0x70, 0x20
    };

    struct PackTarget
    {
        std::uintptr_t      addr;
        const std::uint8_t* prologue;
        std::size_t         size;
    };

    Pack_t g_Orig = nullptr;
    void*  g_Addr = nullptr;

    PackTarget Target()
    {
        switch (gGameBuild)
        {
        case ::AddressSetRuntime::GameBuild::En_1_0_15_3:  return { 0x145B0B710ull, kPrologue153, sizeof(kPrologue153) };
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_3:  return { 0x1474E11F0ull, kPrologue153, sizeof(kPrologue153) };
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4:  return { 0x1408132A0ull, kPrologue154, sizeof(kPrologue154) };
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4:  return { 0x1408132F0ull, kPrologue154, sizeof(kPrologue154) };
        default:                                           return {};
        }
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

    bool DropCustomStaffSEH(std::uint8_t* self, staffServer::SectionTally& tally)
    {
        __try
        {
            std::uint32_t count = *reinterpret_cast<const std::uint32_t*>(self + kSoldierNum);
            if (count > kEntryCapacity)
                count = kEntryCapacity;
            std::uint8_t* entries = self + kEntries;
            std::uint32_t kept = 0;
            for (std::uint32_t i = 0; i < count; ++i)
            {
                std::uint8_t* entry = entries + static_cast<std::size_t>(i) * kEntrySize;
                const std::uint32_t section = *reinterpret_cast<const std::uint32_t*>(entry) >> 24;
                const std::uint32_t header = _byteswap_ulong(*reinterpret_cast<const std::uint32_t*>(entry + 8));
                const std::uint32_t seed = _byteswap_ulong(*reinterpret_cast<const std::uint32_t*>(entry + 0xC));
                const bool custom = staffServer::IsCustomUniqueStaff(header, seed);
                if (section >= 1 && section <= staffServer::kSectionCount)
                {
                    ++tally.total[section];
                    if (custom)
                        ++tally.custom[section];
                }
                if (custom)
                    continue;
                if (kept != i)
                    std::memcpy(entries + static_cast<std::size_t>(kept) * kEntrySize, entry, kEntrySize);
                ++kept;
            }
            if (kept == count)
                return false;
            std::memset(entries + static_cast<std::size_t>(kept) * kEntrySize, 0,
                        static_cast<std::size_t>(count - kept) * kEntrySize);
            *reinterpret_cast<std::uint32_t*>(self + kSoldierNum) = kept;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint64_t __fastcall hkPack(void* self)
    {
        if (self)
        {
            auto* bytes = static_cast<std::uint8_t*>(self);
            staffServer::SectionTally tally;
            if (DropCustomStaffSEH(bytes, tally))
            {
                staffServer::SubtractCustomFromSectionCounts(
                    reinterpret_cast<std::uint32_t*>(bytes + kSectionCounts), tally, "CMD_SYNC_SOLDIER_BIN");
                staffServer::SectionPoints points;
                if (staffServer::TallySectionPoints(points))
                    staffServer::FixSectionLevels(
                        reinterpret_cast<std::uint32_t*>(bytes + kSectionLevels), points, "CMD_SYNC_SOLDIER_BIN");
            }
        }
        return g_Orig(self);
    }
}

bool Install_CmdSyncSoldierBinOption_Pack()
{
    const PackTarget target = Target();
    if (target.addr == 0)
    {
        LogError("StaffSyncGuard: CMD_SYNC_SOLDIER_BIN pack address missing for %s, custom unique staff reach the server",
            GetGameBuildName(gGameBuild));
        return false;
    }

    void* fn = ResolveGameAddress(target.addr);
    if (!fn || !PrologueMatchesSEH(fn, target))
    {
        LogError("StaffSyncGuard: CMD_SYNC_SOLDIER_BIN pack prologue mismatch on %s, custom unique staff reach the server",
            GetGameBuildName(gGameBuild));
        return false;
    }

    if (!CreateAndEnableHook(fn, reinterpret_cast<void*>(&hkPack),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("StaffSyncGuard: CMD_SYNC_SOLDIER_BIN pack hook failed on %s, custom unique staff reach the server",
            GetGameBuildName(gGameBuild));
        return false;
    }

    g_Addr = fn;
    return true;
}

void Uninstall_CmdSyncSoldierBinOption_Pack()
{
    if (g_Addr)
    {
        DisableAndRemoveHook(g_Addr);
        g_Addr = nullptr;
    }
    g_Orig = nullptr;
}
