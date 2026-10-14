#include "pch.h"
#include "CmdSyncMotherBaseOption_Pack.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "DevelopArrayGrow.h"
#include "HookUtils.h"
#include "MotherBaseSecurityShadow.h"
#include "StaffServerSanitize.h"
#include "log.h"

namespace
{
    using Pack_t = std::uint64_t(__fastcall*)(void* self);

    constexpr std::size_t   kEquipFlagWords    = 0x5E64;   // bit n = R&D row n developed
    constexpr std::size_t   kEquipFlagCount    = 0x5EE4;
    constexpr std::uint32_t kEquipFlagMaxWords = 32;
    constexpr std::size_t   kPfSkillStaff      = 0x5EEC;   // 18 skill counts, then all_staff_num
    constexpr int           kAllStaffSlot      = 18;
    constexpr int           kBaseDevSection    = 3;

    constexpr std::uint8_t kSlotOfSkill[staffServer::kPfSkillCount] = {   // skill 0x45.. -> JSON order ranger, medic, sentry, defender, lcm, interceptor
        9, 10, 11, 6, 7, 8, 0, 1, 2, 3, 4, 5, 12, 13, 14, 15, 16, 17
    };

    const std::uint8_t kPrologue154[] = {
        0x48, 0x8B, 0xC4, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
        0x48, 0x8D, 0xA8, 0x08, 0xF5, 0xFF, 0xFF, 0x48, 0x81, 0xEC, 0xD0, 0x0B, 0x00, 0x00
    };
    const std::uint8_t kPrologue153[] = {
        0x48, 0x89, 0xE0, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
        0x48, 0x8D, 0xA8, 0x08, 0xF5, 0xFF, 0xFF, 0x48, 0x81, 0xEC, 0xD0, 0x0B, 0x00, 0x00
    };

    struct PackTarget
    {
        std::uintptr_t      addr;
        const std::uint8_t* prologue;
        std::size_t         size;
    };

    Pack_t g_Orig = nullptr;
    void*  g_Addr = nullptr;

    std::atomic<bool> g_StaffMismatchLogged{ false };
    std::atomic<bool> g_SecurityMismatchLogged{ false };

    PackTarget Target()
    {
        switch (gGameBuild)
        {
        case ::AddressSetRuntime::GameBuild::En_1_0_15_3:  return { 0x145B05460ull, kPrologue153, sizeof(kPrologue153) };
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_3:  return { 0x1474DA240ull, kPrologue153, sizeof(kPrologue153) };
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4:  return { 0x14080F7B0ull, kPrologue154, sizeof(kPrologue154) };
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4:  return { 0x14080F800ull, kPrologue154, sizeof(kPrologue154) };
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

    void ClearCustomRowBitsSEH(std::uint8_t* self, std::uint32_t firstCustomRow)
    {
        __try
        {
            std::uint32_t words = *reinterpret_cast<const std::uint32_t*>(self + kEquipFlagCount);
            if (words > kEquipFlagMaxWords)
                words = kEquipFlagMaxWords;
            auto* bits = reinterpret_cast<std::uint32_t*>(self + kEquipFlagWords);
            for (std::uint32_t word = firstCustomRow / 32; word < words; ++word)
                bits[word] &= (word == firstCustomRow / 32)
                    ? (1u << (firstCustomRow % 32)) - 1u
                    : 0u;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    bool SubtractCustomStaffSEH(std::uint8_t* self, const staffServer::SectionTally& tally)
    {
        bool matched = true;
        __try
        {
            auto* counts = reinterpret_cast<std::uint32_t*>(self + kPfSkillStaff);
            for (int skill = 0; skill < staffServer::kPfSkillCount; ++skill)
            {
                const std::uint32_t custom = tally.skillCustom[skill];
                if (custom == 0)
                    continue;
                std::uint32_t& count = counts[kSlotOfSkill[skill]];
                if (count != tally.skillTotal[skill] || count < custom)
                    matched = false;
                else
                    count -= custom;
            }
            std::uint32_t& all = counts[kAllStaffSlot];
            if (all != tally.staffCount || all < tally.customAll)
                matched = false;
            else
                all -= tally.customAll;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
        return matched;
    }

    std::uint64_t __fastcall hkPack(void* self)
    {
        if (self)
        {
            auto* bytes = static_cast<std::uint8_t*>(self);
            ClearCustomRowBitsSEH(bytes, equip::FirstCustomFlowIndex());
            staffServer::SectionTally tally;
            if (staffServer::TallyLiveRoster(tally) && tally.customAll != 0)
            {
                if (!SubtractCustomStaffSEH(bytes, tally) && !g_StaffMismatchLogged.exchange(true))
                    LogWarn("StaffSyncGuard: CMD_SYNC_MOTHER_BASE pf_skill_staff does not match the roster, some counts left as is");
                staffServer::SectionPoints points;
                if ((tally.custom[kBaseDevSection] != 0 || tally.custom[staffServer::kSectionCount] != 0)
                    && staffServer::TallySectionPoints(points))
                {
                    const mbSecurity::Outcome outcome = mbSecurity::RemoveCustomStaffFromSecurity(bytes, points);
                    if (outcome == mbSecurity::Outcome::Mismatch && !g_SecurityMismatchLogged.exchange(true))
                        LogWarn("StaffSyncGuard: CMD_SYNC_MOTHER_BASE security values do not match the roster, sent as is");
                    else if (outcome == mbSecurity::Outcome::Unavailable && !g_SecurityMismatchLogged.exchange(true))
                        LogError("StaffSyncGuard: security rank code not recognised on %s, CMD_SYNC_MOTHER_BASE security values sent as is",
                            GetGameBuildName(gGameBuild));
                }
            }
        }
        return g_Orig(self);
    }
}

bool Install_CmdSyncMotherBaseOption_Pack()
{
    const PackTarget target = Target();
    if (target.addr == 0)
    {
        LogError("MbSyncGuard: CMD_SYNC_MOTHER_BASE pack address missing for %s, developed custom R&D rows reach the server",
            GetGameBuildName(gGameBuild));
        return false;
    }

    void* fn = ResolveGameAddress(target.addr);
    if (!fn || !PrologueMatchesSEH(fn, target))
    {
        LogError("MbSyncGuard: CMD_SYNC_MOTHER_BASE pack prologue mismatch on %s, developed custom R&D rows reach the server",
            GetGameBuildName(gGameBuild));
        return false;
    }

    if (!CreateAndEnableHook(fn, reinterpret_cast<void*>(&hkPack),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("MbSyncGuard: CMD_SYNC_MOTHER_BASE pack hook failed on %s, developed custom R&D rows reach the server",
            GetGameBuildName(gGameBuild));
        return false;
    }

    g_Addr = fn;
    return true;
}

void Uninstall_CmdSyncMotherBaseOption_Pack()
{
    if (g_Addr)
    {
        DisableAndRemoveHook(g_Addr);
        g_Addr = nullptr;
    }
    g_Orig = nullptr;
}
