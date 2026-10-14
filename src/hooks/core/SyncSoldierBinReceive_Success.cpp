#include "pch.h"
#include "SyncSoldierBinReceive_Success.h"

#include <Windows.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "AddressSet.h"
#include "HookUtils.h"
#include "StaffServerSanitize.h"
#include "log.h"

namespace
{
    using Success_t = void(__fastcall*)(void* self, void** result);

    constexpr std::size_t   kEchoEntries   = 0x6C;
    constexpr std::size_t   kEchoEntrySize = 0x10;   // header, seed (big-endian), section << 3
    constexpr std::size_t   kEchoCount     = 0xDB2C;
    constexpr std::uint32_t kEchoCapacity  = 3500;

    const std::uint8_t kPrologue[] = {
        0x48, 0x89, 0x54, 0x24, 0x10, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
        0x48, 0x83, 0xEC, 0x30, 0x48, 0xC7, 0x44, 0x24, 0x20, 0xFE, 0xFF, 0xFF, 0xFF,
        0x48, 0x89, 0x5C, 0x24, 0x70
    };

    Success_t g_Orig = nullptr;
    void*     g_Addr = nullptr;

    std::uintptr_t SuccessAddr()
    {
        switch (gGameBuild)
        {
        case ::AddressSetRuntime::GameBuild::En_1_0_15_3:  return 0x1459EC220ull;
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_3:  return 0x147437B00ull;
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4:  return 0x1407EA9E0ull;
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4:  return 0x1407EAAD0ull;
        default:                                           return 0;
        }
    }

    bool PrologueMatchesSEH(const void* fn)
    {
        __try
        {
            return std::memcmp(fn, kPrologue, sizeof(kPrologue)) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void EchoCustomStaffSEH(std::uint8_t* result, const staffServer::StaffWords* staff, std::size_t staffCount)
    {
        __try
        {
            std::uint32_t count = *reinterpret_cast<const std::uint16_t*>(result + kEchoCount);
            if (count == 0 || count > kEchoCapacity)   // empty echo wipes the whole roster in vanilla too
                return;
            const std::uint32_t served = count;
            for (std::size_t s = 0; s < staffCount && count < kEchoCapacity; ++s)
            {
                const std::uint32_t header = _byteswap_ulong(staff[s].header);
                const std::uint32_t seed = _byteswap_ulong(staff[s].seed);
                bool echoed = false;
                for (std::uint32_t i = 0; i < served && !echoed; ++i)
                {
                    const std::uint8_t* entry = result + kEchoEntries + static_cast<std::size_t>(i) * kEchoEntrySize;
                    echoed = *reinterpret_cast<const std::uint32_t*>(entry) == header
                          && *reinterpret_cast<const std::uint32_t*>(entry + 4) == seed;
                }
                if (echoed)
                    continue;
                std::uint8_t* entry = result + kEchoEntries + static_cast<std::size_t>(count) * kEchoEntrySize;
                std::memset(entry, 0, kEchoEntrySize);
                *reinterpret_cast<std::uint32_t*>(entry) = header;
                *reinterpret_cast<std::uint32_t*>(entry + 4) = seed;
                entry[8] = static_cast<std::uint8_t>(staffServer::SectionOfStatusWord(staff[s].status) << 3);
                ++count;
            }
            *reinterpret_cast<std::uint16_t*>(result + kEchoCount) = static_cast<std::uint16_t>(count);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void __fastcall hkSuccess(void* self, void** result)   // the game deletes local staff missing from the server's echo; BIN upload left custom ones out
    {
        if (result && *result)
        {
            std::vector<staffServer::StaffWords> custom(staffServer::kStaffCapacity);
            const std::size_t found = staffServer::CollectLiveCustomStaff(custom.data(), custom.size());
            if (found != 0)
                EchoCustomStaffSEH(static_cast<std::uint8_t*>(*result), custom.data(), found);
        }
        g_Orig(self, result);
    }
}

bool Install_SyncSoldierBinReceive_Success()
{
    const std::uintptr_t addr = SuccessAddr();
    if (addr == 0)
    {
        LogError("StaffSyncGuard: soldier sync reply handler address missing for %s, CMD_SYNC_SOLDIER_BIN left unfiltered",
            GetGameBuildName(gGameBuild));
        return false;
    }

    void* fn = ResolveGameAddress(addr);
    if (!fn || !PrologueMatchesSEH(fn))
    {
        LogError("StaffSyncGuard: soldier sync reply handler prologue mismatch on %s, CMD_SYNC_SOLDIER_BIN left unfiltered",
            GetGameBuildName(gGameBuild));
        return false;
    }

    if (!CreateAndEnableHook(fn, reinterpret_cast<void*>(&hkSuccess),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("StaffSyncGuard: soldier sync reply handler hook failed on %s, CMD_SYNC_SOLDIER_BIN left unfiltered",
            GetGameBuildName(gGameBuild));
        return false;
    }

    g_Addr = fn;
    return true;
}

void Uninstall_SyncSoldierBinReceive_Success()
{
    if (g_Addr)
    {
        DisableAndRemoveHook(g_Addr);
        g_Addr = nullptr;
    }
    g_Orig = nullptr;
}
