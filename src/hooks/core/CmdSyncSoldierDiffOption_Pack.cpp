#include <pch.h>
#include "CmdSyncSoldierDiffOption_Pack.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "StaffServerSanitize.h"
#include "log.h"

namespace
{
    using Pack_t = std::uint64_t(__fastcall*)(void* self);

    constexpr std::size_t   kSectionLevels   = 0x50;
    constexpr std::size_t   kSectionCounts   = 0x6C;
    constexpr std::size_t   kEntries         = 0x88;
    constexpr std::size_t   kEntrySize       = 0x1C;   // 6 params + param count
    constexpr std::size_t   kEntryCount      = 0x17F58;
    constexpr std::size_t   kSoldierNum      = 0x17F5C;
    constexpr std::size_t   kRemovers        = 0x17F60;
    constexpr std::size_t   kRemoverSize     = 0xC;
    constexpr std::size_t   kRemoverCount    = 0x22370;
    constexpr std::size_t   kRemoverNum      = 0x22374;
    constexpr std::uint32_t kCapacity        = staffServer::kStaffCapacity;

    const std::uint8_t kPrologue154[] = {
        0x48, 0x8B, 0xC4, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8B, 0xEC, 0x48, 0x83, 0xEC, 0x60,
        0x48, 0xC7, 0x45, 0xC0, 0xFE, 0xFF, 0xFF, 0xFF, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x70
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
        return { gAddr.CmdSyncSoldierDiffOption_Pack, kPrologue154, sizeof(kPrologue154) };
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

    void CompactListSEH(std::uint8_t* self, std::size_t listOff, std::size_t stride,
                        std::size_t headerOff, std::size_t seedOff,
                        std::size_t countOff, std::size_t numOff)
    {
        __try
        {
            std::uint32_t count = *reinterpret_cast<const std::uint32_t*>(self + countOff);
            if (count > kCapacity)
                count = kCapacity;
            std::uint8_t* list = self + listOff;
            std::uint32_t kept = 0;
            for (std::uint32_t i = 0; i < count; ++i)
            {
                std::uint8_t* item = list + static_cast<std::size_t>(i) * stride;
                const std::uint32_t header = *reinterpret_cast<const std::uint32_t*>(item + headerOff);
                const std::uint32_t seed = *reinterpret_cast<const std::uint32_t*>(item + seedOff);
                if (staffServer::IsCustomUniqueStaff(header, seed))
                    continue;
                if (kept != i)
                    std::memcpy(list + static_cast<std::size_t>(kept) * stride, item, stride);
                ++kept;
            }
            if (kept == count)
                return;
            std::memset(list + static_cast<std::size_t>(kept) * stride, 0,
                        static_cast<std::size_t>(count - kept) * stride);
            *reinterpret_cast<std::uint32_t*>(self + countOff) = kept;
            *reinterpret_cast<std::uint32_t*>(self + numOff) = kept;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    std::uint64_t __fastcall hkPack(void* self)
    {
        if (self)
        {
            auto* bytes = static_cast<std::uint8_t*>(self);
            CompactListSEH(bytes, kEntries, kEntrySize, 0x8, 0xC, kEntryCount, kSoldierNum);
            CompactListSEH(bytes, kRemovers, kRemoverSize, 0x0, 0x4, kRemoverCount, kRemoverNum);
            staffServer::SectionTally tally;
            if (staffServer::TallyLiveRoster(tally) && tally.customAll != 0)
            {
                staffServer::SubtractCustomFromSectionCounts(
                    reinterpret_cast<std::uint32_t*>(bytes + kSectionCounts), tally, "CMD_SYNC_SOLDIER_DIFF");
                staffServer::SectionPoints points;
                if (staffServer::TallySectionPoints(points))
                    staffServer::FixSectionLevels(
                        reinterpret_cast<std::uint32_t*>(bytes + kSectionLevels), points, "CMD_SYNC_SOLDIER_DIFF");
            }
        }
        return g_Orig(self);
    }
}

bool Install_CmdSyncSoldierDiffOption_Pack()
{
    const PackTarget target = Target();
    if (target.addr == 0)
    {
        LogError("StaffSyncGuard: CMD_SYNC_SOLDIER_DIFF pack address missing for %s, custom unique staff reach the server",
            GetGameBuildName(gGameBuild));
        return false;
    }

    void* fn = ResolveGameAddress(target.addr);
    char entry[160] = {};
    const bool matches = fn && PrologueMatchesSEH(fn, target);
    const bool onTop   = fn && !matches && EntryHookedByOtherModule(fn);
    if (!matches && !onTop)
    {
        DescribeFunctionEntry(fn, entry, sizeof(entry));
        LogError("StaffSyncGuard: CMD_SYNC_SOLDIER_DIFF pack prologue mismatch on %s (%s), custom unique staff reach the server",
            GetGameBuildName(gGameBuild), entry);
        return false;
    }

    if (!CreateAndEnableHook(fn, reinterpret_cast<void*>(&hkPack),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("StaffSyncGuard: CMD_SYNC_SOLDIER_DIFF pack hook failed on %s, custom unique staff reach the server",
            GetGameBuildName(gGameBuild));
        return false;
    }

    if (onTop)
    {
        DescribeFunctionEntry(fn, entry, sizeof(entry));
        LogInfo("StaffSyncGuard: CMD_SYNC_SOLDIER_DIFF pack already hooked (%s), guard installed on top", entry);
    }
    g_Addr = fn;
    return true;
}

void Uninstall_CmdSyncSoldierDiffOption_Pack()
{
    if (g_Addr)
    {
        DisableAndRemoveHook(g_Addr);
        g_Addr = nullptr;
    }
    g_Orig = nullptr;
}
