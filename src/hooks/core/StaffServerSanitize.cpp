#include "pch.h"
#include "StaffServerSanitize.h"

#include <Windows.h>
#include <atomic>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "UniqueStaffRegistry.h"
#include "log.h"

namespace staffServer
{
    namespace
    {
        using QuarkTable_t = std::uint8_t* (__fastcall*)();
        using SectionPoint_t = std::uint16_t(__fastcall*)(const void* ctrl, std::uint32_t header,
                                                          std::uint32_t seed, std::uint32_t status);

        constexpr std::size_t kApp_Mbm             = 0x110;
        constexpr std::size_t kMbm_StaffController = 0xAC0;
        constexpr std::size_t kStaff_Headers       = 0x9C78;
        constexpr std::size_t kStaff_Seeds         = 0x9C80;
        constexpr std::size_t kStaff_Status        = 0x9C88;
        constexpr std::size_t kStaff_Count         = 0x9E6C;
        constexpr std::size_t kStaff_UnitUnlocks   = 0x9C70;
        constexpr std::size_t kStaff_UnitLines     = 0x8ACD;   // 7 level thresholds per unit
        constexpr std::size_t kStaff_LevelLines    = 0x54FC;   // u32 minimum section points per level
        constexpr int         kLevelLineLast       = 0xA1;
        constexpr std::uint32_t kPfSkillFirst      = 0x45;
        constexpr std::uint32_t kSkillLimit        = 0x57;

        constexpr std::size_t kSumOffset[kSectionCount + 1]   = { 0, 0x9E50, 0x9E54, 0x9E64, 0x9E58, 0x9E60, 0x9E5C, 0x9E68 };
        constexpr std::size_t kLevelOffset[kSectionCount + 1] = { 0, 0xA05A, 0xA05B, 0xA05F, 0xA05C, 0xA05E, 0xA05D, 0xA060 };

        constexpr std::uint16_t kBucketLines[kSecurityBuckets] = { 5, 16, 32, 48, 64, 80, 110, 185, 290, 448 };

        enum PointFunction { kCombatPoints, kDevelopPoints, kBaseDevPoints, kSupportPoints, kSpyPoints, kMedicalPoints, kPointFunctionCount };
        constexpr int kPointsOfSection[kSectionCount + 1] = {
            -1, kCombatPoints, kDevelopPoints, kBaseDevPoints, kSupportPoints, kSpyPoints, kMedicalPoints, kCombatPoints
        };

        constexpr std::uint8_t kCombatPrologue[] = {
            0x48, 0x89, 0x5C, 0x24, 0x20, 0x56, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x89, 0x6C, 0x24, 0x50
        };
        constexpr std::uint8_t kSectionPrologue[] = {   // 15.4 adds a 0x40 REX byte in front
            0x55, 0x57, 0x48, 0x83, 0xEC, 0x48, 0x48, 0x89, 0x74, 0x24, 0x68, 0x4C, 0x89, 0x74, 0x24, 0x70
        };

        std::atomic<bool> g_MismatchLogged{ false };
        std::atomic<bool> g_LevelMismatchLogged{ false };
        std::atomic<int>  g_PointsState{ 0 };   // 0 unresolved, 1 ready, -1 unavailable
        SectionPoint_t    g_Points[kPointFunctionCount] = {};

        struct LiveRoster
        {
            const std::uint8_t*  ctrl;
            const std::uint32_t* headers;
            const std::uint32_t* seeds;
            const std::uint32_t* status;
            std::uint32_t count;
        };

        bool PointAddresses(std::uintptr_t (&out)[kPointFunctionCount])
        {
            static constexpr std::uintptr_t kEn154[] = { 0x1407E8FF0, 0x1407E9170, 0x1407E8E30, 0x1407E9710, 0x1407E9520, 0x1407E9310 };
            static constexpr std::uintptr_t kJp154[] = { 0x1407E90E0, 0x1407E9260, 0x1407E8F20, 0x1407E9800, 0x1407E9610, 0x1407E9400 };
            static constexpr std::uintptr_t kEn153[] = { 0x1407E84B0, 0x1407E8630, 0x1407E82F0, 0x1407E8BD0, 0x1407E89E0, 0x1407E87D0 };
            static constexpr std::uintptr_t kJp153[] = { 0x1407E80E0, 0x1407E8260, 0x1407E7F20, 0x1407E8800, 0x1407E8610, 0x1407E8400 };
            const std::uintptr_t* table = nullptr;
            switch (gGameBuild)
            {
            case ::AddressSetRuntime::GameBuild::En_1_0_15_3:  table = kEn153; break;
            case ::AddressSetRuntime::GameBuild::Jp_1_0_15_3:  table = kJp153; break;
            case ::AddressSetRuntime::GameBuild::En_1_0_15_4a:
            case ::AddressSetRuntime::GameBuild::En_1_0_15_4:  table = kEn154; break;
            case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4a:
            case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4:  table = kJp154; break;
            default:                                           return false;
            }
            for (int i = 0; i < kPointFunctionCount; ++i)
                out[i] = table[i];
            return true;
        }

        bool PointBodyMatchesSEH(const std::uint8_t* fn, bool combat)
        {
            __try
            {
                if (fn[0] == 0xE9)   // 15.3 thunk into the Arxan band
                    fn = fn + 5 + *reinterpret_cast<const std::int32_t*>(fn + 1);
                if (combat)
                    return std::memcmp(fn, kCombatPrologue, sizeof(kCombatPrologue)) == 0;
                if (fn[0] == 0x40)
                    ++fn;
                return std::memcmp(fn, kSectionPrologue, sizeof(kSectionPrologue)) == 0;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        bool ResolvePointFunctions()
        {
            const int state = g_PointsState.load();
            if (state != 0)
                return state > 0;
            std::uintptr_t addrs[kPointFunctionCount] = {};
            bool ok = PointAddresses(addrs);
            for (int i = 0; ok && i < kPointFunctionCount; ++i)
            {
                auto* fn = static_cast<const std::uint8_t*>(ResolveGameAddress(addrs[i]));
                ok = fn && PointBodyMatchesSEH(fn, i == kCombatPoints);
                g_Points[i] = reinterpret_cast<SectionPoint_t>(const_cast<std::uint8_t*>(fn));
            }
            if (!ok)
                LogError("StaffSyncGuard: section point functions not found on %s, section levels and security rank sent as is",
                    GetGameBuildName(gGameBuild));
            g_PointsState.store(ok ? 1 : -1);
            return ok;
        }

        std::uint8_t* ReadMbmSEH(std::uint8_t* quark)
        {
            __try
            {
                const std::uint8_t* app = *reinterpret_cast<std::uint8_t* const*>(quark + 0x98);
                return app ? *reinterpret_cast<std::uint8_t* const*>(app + kApp_Mbm) : nullptr;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return nullptr;
            }
        }

        std::uint8_t* ReadPointerSEH(const std::uint8_t* base, std::size_t offset)
        {
            __try
            {
                return base ? *reinterpret_cast<std::uint8_t* const*>(base + offset) : nullptr;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return nullptr;
            }
        }

        bool ReadLiveRosterSEH(const std::uint8_t* staff, LiveRoster& out)
        {
            __try
            {
                out.ctrl = staff;
                out.headers = *reinterpret_cast<const std::uint32_t* const*>(staff + kStaff_Headers);
                out.seeds = *reinterpret_cast<const std::uint32_t* const*>(staff + kStaff_Seeds);
                out.status = *reinterpret_cast<const std::uint32_t* const*>(staff + kStaff_Status);
                out.count = *reinterpret_cast<const std::uint16_t*>(staff + kStaff_Count);
                return out.headers && out.seeds && out.status && out.count <= kStaffCapacity;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        bool ReadLiveRoster(LiveRoster& out)
        {
            const std::uint8_t* staff = LiveStaffController();
            return staff && ReadLiveRosterSEH(staff, out);
        }

        bool TallyLiveRosterSEH(const LiveRoster& roster, SectionTally& out)
        {
            __try
            {
                out.staffCount = roster.count;
                for (std::uint32_t i = 0; i < roster.count; ++i)
                {
                    const bool custom = IsCustomUniqueStaff(roster.headers[i], roster.seeds[i]);
                    if (custom)
                        ++out.customAll;
                    const std::uint32_t section = SectionOfStatusWord(roster.status[i]);
                    if (section < 1 || section > kSectionCount)
                        continue;
                    ++out.total[section];
                    if (custom)
                        ++out.custom[section];
                    const int skill = PfSkillIndexOfHeader(roster.headers[i]);
                    if (skill < 0)
                        continue;
                    ++out.skillTotal[skill];
                    if (custom)
                        ++out.skillCustom[skill];
                }
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        int SecurityBucketOfPoints(std::uint16_t points)
        {
            for (int bucket = kSecurityBuckets - 1; bucket >= 1; --bucket)
                if (points >= kBucketLines[bucket])
                    return bucket;
            return 0;
        }

        bool TallySectionPointsSEH(const LiveRoster& roster, SectionPoints& out)
        {
            __try
            {
                for (std::uint32_t i = 0; i < roster.count; ++i)
                {
                    const std::uint32_t section = SectionOfStatusWord(roster.status[i]);
                    if (section < 1 || section > kSectionCount)
                        continue;
                    const std::uint16_t points = g_Points[kPointsOfSection[section]](
                        roster.ctrl, roster.headers[i], roster.seeds[i], roster.status[i]);
                    const bool custom = IsCustomUniqueStaff(roster.headers[i], roster.seeds[i]);
                    out.full[section] += points;
                    if (custom)
                    {
                        out.custom[section] += points;
                        ++out.customCount[section];
                    }
                    if (section != kSectionCount)
                        continue;
                    const int bucket = SecurityBucketOfPoints(points);
                    ++out.bucketFull[bucket];
                    if (custom)
                        ++out.bucketCustom[bucket];
                }
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        std::size_t CollectCustomSEH(const LiveRoster& roster, StaffWords* out, std::size_t capacity)
        {
            std::size_t found = 0;
            __try
            {
                for (std::uint32_t i = 0; i < roster.count && found < capacity; ++i)
                {
                    if (!IsCustomUniqueStaff(roster.headers[i], roster.seeds[i]))
                        continue;
                    out[found].header = roster.headers[i];
                    out[found].seed = roster.seeds[i];
                    out[found].status = roster.status[i];
                    ++found;
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
            return found;
        }

        bool ApplyLevelsSEH(std::uint32_t* levels, const std::uint8_t* ctrl, const SectionPoints& points)
        {
            bool matched = true;
            __try
            {
                for (int section = 1; section <= kSectionCount; ++section)
                {
                    if (points.customCount[section] == 0)
                        continue;
                    const std::uint32_t sum = *reinterpret_cast<const std::uint32_t*>(ctrl + kSumOffset[section]);
                    const std::uint8_t level = ctrl[kLevelOffset[section]];
                    if (sum != points.full[section] || SectionLevelOfSum(ctrl, sum) != level
                        || levels[section - 1] != level)
                    {
                        matched = false;
                        continue;
                    }
                    levels[section - 1] = SectionLevelOfSum(ctrl, sum - points.custom[section]);
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                matched = false;
            }
            return matched;
        }
    }

    std::uint8_t* LiveMotherBaseManager()
    {
        const auto quarkTable = reinterpret_cast<QuarkTable_t>(ResolveGameAddress(gAddr.GetQuarkSystemTable));
        std::uint8_t* quark = quarkTable ? quarkTable() : nullptr;
        return quark ? ReadMbmSEH(quark) : nullptr;
    }

    std::uint8_t* LiveStaffController()
    {
        return ReadPointerSEH(LiveMotherBaseManager(), kMbm_StaffController);
    }

    bool IsCustomUniqueStaff(std::uint32_t header, std::uint32_t seed)
    {
        return ((header >> 7) & 0x3F) == 0x3F
            && uniqueStaff::IsNonVanillaTypeId(static_cast<std::int32_t>((seed >> 24) & 0xFF));
    }

    std::uint32_t SectionOfStatusWord(std::uint32_t status)
    {
        const std::uint32_t section = (status >> 27) & 0xF;
        return section < 11 ? section : 0;
    }

    int PfSkillIndexOfHeader(std::uint32_t header)
    {
        const std::uint32_t skill = (header >> 13) & 0x7F;
        if (skill >= kSkillLimit || skill < kPfSkillFirst)
            return -1;
        return static_cast<int>(skill - kPfSkillFirst);
    }

    bool TallyLiveRoster(SectionTally& out)
    {
        out = SectionTally{};
        LiveRoster roster{};
        return ReadLiveRoster(roster) && TallyLiveRosterSEH(roster, out);
    }

    bool TallySectionPoints(SectionPoints& out)
    {
        out = SectionPoints{};
        LiveRoster roster{};
        return ResolvePointFunctions() && ReadLiveRoster(roster) && TallySectionPointsSEH(roster, out);
    }

    std::size_t CollectLiveCustomStaff(StaffWords* out, std::size_t capacity)
    {
        LiveRoster roster{};
        if (!out || !ReadLiveRoster(roster))
            return 0;
        return CollectCustomSEH(roster, out, capacity);
    }

    bool ReadSectionSumAndLevel(const std::uint8_t* ctrl, int section, std::uint32_t& sum, std::uint8_t& level)
    {
        if (!ctrl || section < 1 || section > kSectionCount)
            return false;
        __try
        {
            sum = *reinterpret_cast<const std::uint32_t*>(ctrl + kSumOffset[section]);
            level = ctrl[kLevelOffset[section]];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint8_t SectionLevelOfSum(const std::uint8_t* ctrl, std::uint32_t sum)   // same scan as UpdateSections, read live
    {
        const auto* lines = reinterpret_cast<const std::uint32_t*>(ctrl + kStaff_LevelLines);
        for (int i = kLevelLineLast; i >= 0; --i)
            if (sum >= lines[i])
                return static_cast<std::uint8_t>(i + 1);
        return 0;
    }

    std::uint8_t UnitLevelOfSectionLevel(const std::uint8_t* ctrl, int unit, std::uint8_t sectionLevel)
    {
        const std::uint8_t* unlocks = *reinterpret_cast<const std::uint8_t* const*>(ctrl + kStaff_UnitUnlocks);
        if (!unlocks || (unlocks[unit] & 0x20) == 0)
            return 0;
        const std::uint8_t* lines = ctrl + kStaff_UnitLines + static_cast<std::size_t>(unit) * 7;
        for (int i = 6; i >= 1; --i)
            if (sectionLevel >= lines[i])
                return static_cast<std::uint8_t>(i + 1);
        return lines[0] <= sectionLevel ? 1 : 0;
    }

    void FixSectionLevels(std::uint32_t* levels, const SectionPoints& points, const char* command)
    {
        const std::uint8_t* ctrl = LiveStaffController();
        if (!ctrl || ApplyLevelsSEH(levels, ctrl, points))
            return;
        if (!g_LevelMismatchLogged.exchange(true))
            LogWarn("StaffSyncGuard: %s section levels do not match the roster, some left as is", command);
    }

    void SubtractCustomFromSectionCounts(std::uint32_t* counts, const SectionTally& tally,
                                         const char* command)
    {
        for (int section = 1; section <= kSectionCount; ++section)
        {
            const std::uint32_t custom = tally.custom[section];
            if (custom == 0)
                continue;
            std::uint32_t& count = counts[section - 1];
            if (count != tally.total[section] || count < custom)
            {
                if (!g_MismatchLogged.exchange(true))
                    LogWarn("StaffSyncGuard: %s section %d count %u does not match the roster (%u), left as is",
                        command, section, count, tally.total[section]);
                continue;
            }
            count -= custom;
        }
    }
}
