#include "pch.h"
#include "MotherBaseSecurityShadow.h"

#include <Windows.h>
#include <algorithm>
#include <cstring>
#include <mutex>

#include "AddressSet.h"
#include "HookUtils.h"

namespace mbSecurity
{
    namespace
    {
        using CalcSecurityRank_t   = std::uint64_t(__fastcall*)(void* base, std::uint8_t platform);
        using ClampSecurityEquip_t = void(__fastcall*)(void* base);

        constexpr std::size_t kInterface       = 0x20;   // both controllers are used through obj+0x20
        constexpr std::size_t kBaseObjectSize  = 0xB368;
        constexpr std::size_t kStaffObjectSize = 0xE0D0;
        constexpr std::size_t kMbmCopySize     = 0xB00;

        constexpr std::size_t  kMbm_Staff     = 0xAC0;
        constexpr std::size_t  kMbm_Base      = 0xAD8;
        constexpr std::size_t  kBase_Mbm      = 0xB0A0;
        constexpr std::size_t  kBase_Flags    = 0xB099;
        constexpr std::uint8_t kLimitTableSet = 4;
        constexpr std::size_t  kBase_Construct = 0xCC6 * 8;
        constexpr std::size_t  kBase_Words     = 0xCC9 * 8;
        constexpr std::size_t  kSlot_CalcSecurityRank   = 0x2D0;
        constexpr std::size_t  kSlot_ClampSecurityEquip = 0x3F8;

        constexpr std::size_t kStaff_Buckets   = 0x9F18;
        constexpr std::size_t kStaff_UnitLevel = 0xA061;
        constexpr int         kBaseDevUnit     = 3;
        constexpr int         kBaseDevSection  = 3;

        constexpr int kClusterCount        = 4;
        constexpr int kPlatformsPerCluster = 7;
        constexpr int kPlatformCount       = kClusterCount * kPlatformsPerCluster;
        constexpr int kFirstPlatform       = 7;
        constexpr std::uint32_t kMaxBucketRank = 9;   // CalcSecurityParameter reads past the buckets above this

        constexpr int           kCountGroups     = 4;
        constexpr int           kCountKinds      = 7;
        constexpr std::size_t   kCountArrayBytes = 0x23;
        constexpr std::uint16_t kCountSlot[kCountGroups]  = { 0xCCA, 0xCD4, 0xCDE, 0xCE8 };   // unique, common1..3: soldier, ir, antitheft, camera, decoy, mine, uav
        constexpr std::size_t   kCountField[kCountGroups] = { 0x0C, 0xDC, 0x1AC, 0x27C };

        constexpr std::size_t   kReq_Bases          = 0x98;
        constexpr std::size_t   kReq_BaseStride     = 0x1734;
        constexpr std::size_t   kReq_BaseCount      = 0x5D68;
        constexpr std::size_t   kReq_Construct      = 0x8;
        constexpr std::size_t   kReq_SecurityRank   = 0xC;
        constexpr std::size_t   kReq_Platforms      = 0x18;
        constexpr std::size_t   kReq_PlatformCount  = 0x172C;
        constexpr std::size_t   kReq_PlatformStride = 0x34C;
        constexpr std::size_t   kPf_Build           = 0x0;
        constexpr std::size_t   kPf_Security        = 0x8;
        constexpr std::uint32_t kRankShift          = 12;
        constexpr std::uint32_t kRankBits           = 0x7F;

        struct LiveInputs
        {
            std::uint8_t*       mbm;
            std::uint8_t*       base;
            const std::uint8_t* ctrl;
            std::uintptr_t      rankSlot;
            std::uintptr_t      clampSlot;
            std::uint16_t       buckets[staffServer::kSecurityBuckets];
            std::uint32_t       baseDevSum;
            std::uint8_t        baseDevLevel;
            std::uint8_t        baseDevUnit;
            std::uint8_t        flags;
            std::uint32_t       words[kPlatformCount];
            std::uint32_t       construct[kClusterCount];
        };

        alignas(16) std::uint8_t g_BaseShadow[kBaseObjectSize];
        alignas(16) std::uint8_t g_StaffShadow[kStaffObjectSize];
        alignas(16) std::uint8_t g_MbmShadow[kMbmCopySize];
        std::uint8_t             g_Counts[kCountGroups][kCountKinds][kCountArrayBytes];
        std::uint8_t             g_Ranks[kPlatformCount];
        std::mutex               g_Lock;

        bool ExpectedSlots(std::uintptr_t& rank, std::uintptr_t& clamp)
        {
            switch (gGameBuild)
            {
            case ::AddressSetRuntime::GameBuild::En_1_0_15_3:  rank = 0x140F514A0; clamp = 0x140F55DB0; return true;
            case ::AddressSetRuntime::GameBuild::Jp_1_0_15_3:  rank = 0x140F515A0; clamp = 0x140F55EA0; return true;
            case ::AddressSetRuntime::GameBuild::En_1_0_15_4a:
            case ::AddressSetRuntime::GameBuild::En_1_0_15_4:  rank = 0x140F50D20; clamp = 0x140F55620; return true;
            case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4a:
            case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4:  rank = 0x140F50D70; clamp = 0x140F55680; return true;
            default:                                           return false;
            }
        }

        bool ReadLiveSEH(LiveInputs& in)
        {
            __try
            {
                in.base = *reinterpret_cast<std::uint8_t* const*>(in.mbm + kMbm_Base);
                in.ctrl = *reinterpret_cast<std::uint8_t* const*>(in.mbm + kMbm_Staff);
                if (!in.base || !in.ctrl)
                    return false;
                const auto vtbl = *reinterpret_cast<const std::uintptr_t*>(in.base);
                in.rankSlot = *reinterpret_cast<const std::uintptr_t*>(vtbl + kSlot_CalcSecurityRank);
                in.clampSlot = *reinterpret_cast<const std::uintptr_t*>(vtbl + kSlot_ClampSecurityEquip);
                for (int k = 0; k < staffServer::kSecurityBuckets; ++k)
                    in.buckets[k] = *reinterpret_cast<const std::uint16_t*>(in.ctrl + kStaff_Buckets + 2 * k);
                in.baseDevUnit = in.ctrl[kStaff_UnitLevel + kBaseDevUnit];
                in.flags = in.base[kBase_Flags];
                const auto* words = *reinterpret_cast<const std::uint32_t* const*>(in.base + kBase_Words);
                const auto* construct = *reinterpret_cast<const std::uint32_t* const*>(in.base + kBase_Construct);
                if (!words || !construct)
                    return false;
                for (int p = 0; p < kPlatformCount; ++p)
                    in.words[p] = words[p];
                for (int c = 0; c < kClusterCount; ++c)
                    in.construct[c] = construct[c];
                return staffServer::ReadSectionSumAndLevel(in.ctrl, kBaseDevSection, in.baseDevSum, in.baseDevLevel);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        bool ProjectSEH(const LiveInputs& in, const staffServer::SectionPoints& points,
                        std::uint16_t (&buckets)[staffServer::kSecurityBuckets], std::uint8_t& unit, bool& changed)
        {
            __try
            {
                if (in.baseDevSum != points.full[kBaseDevSection]
                    || staffServer::SectionLevelOfSum(in.ctrl, in.baseDevSum) != in.baseDevLevel
                    || staffServer::UnitLevelOfSectionLevel(in.ctrl, kBaseDevUnit, in.baseDevLevel) != in.baseDevUnit)
                    return false;
                changed = false;
                for (int k = 0; k < staffServer::kSecurityBuckets; ++k)
                {
                    if (in.buckets[k] != points.bucketFull[k])
                        return false;
                    buckets[k] = static_cast<std::uint16_t>(in.buckets[k] - points.bucketCustom[k]);
                    changed |= (in.buckets[k] != 0) != (buckets[k] != 0);   // only an emptied bucket moves the rank
                }
                const std::uint8_t level = staffServer::SectionLevelOfSum(
                    in.ctrl, in.baseDevSum - points.custom[kBaseDevSection]);
                unit = staffServer::UnitLevelOfSectionLevel(in.ctrl, kBaseDevUnit, level);
                changed |= unit != in.baseDevUnit;
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        bool EvaluateShadowSEH(const LiveInputs& in, const std::uint16_t (&buckets)[staffServer::kSecurityBuckets],
                               std::uint8_t unit)   // runs the game's rank and clamp code on copies; nothing live is written
        {
            __try
            {
                std::memcpy(g_BaseShadow, in.base - kInterface, kBaseObjectSize);
                std::memcpy(g_StaffShadow, in.ctrl - kInterface, kStaffObjectSize);
                std::memcpy(g_MbmShadow, in.mbm, kMbmCopySize);

                std::uint8_t* ctrl = g_StaffShadow + kInterface;
                for (int k = 0; k < staffServer::kSecurityBuckets; ++k)
                    *reinterpret_cast<std::uint16_t*>(ctrl + kStaff_Buckets + 2 * k) = buckets[k];
                ctrl[kStaff_UnitLevel + kBaseDevUnit] = unit;
                *reinterpret_cast<std::uint8_t**>(g_MbmShadow + kMbm_Staff) = ctrl;

                std::uint8_t* base = g_BaseShadow + kInterface;
                *reinterpret_cast<std::uint8_t**>(base + kBase_Mbm) = g_MbmShadow;
                for (int g = 0; g < kCountGroups; ++g)
                    for (int kind = 0; kind < kCountKinds; ++kind)
                    {
                        auto** slot = reinterpret_cast<std::uint8_t**>(base + (kCountSlot[g] + kind) * 8);
                        std::memcpy(g_Counts[g][kind], *slot, kCountArrayBytes);
                        *slot = g_Counts[g][kind];
                    }

                if (in.flags & kLimitTableSet)
                    reinterpret_cast<ClampSecurityEquip_t>(in.clampSlot)(base);
                const auto rank = reinterpret_cast<CalcSecurityRank_t>(in.rankSlot);
                for (int p = 0; p < kPlatformCount; ++p)
                    g_Ranks[p] = static_cast<std::uint8_t>(rank(base, static_cast<std::uint8_t>(kFirstPlatform + p)));
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        bool RequestMatchesSEH(const std::uint8_t* self, const LiveInputs& in)
        {
            __try
            {
                const std::uint32_t count = *reinterpret_cast<const std::uint32_t*>(self + kReq_BaseCount);
                std::uint32_t m = 0;
                for (int c = 0; c < kClusterCount; ++c)
                {
                    if ((in.construct[c] & 1) == 0)
                        continue;
                    const std::uint8_t* mb = self + kReq_Bases + m * kReq_BaseStride;
                    if (m >= count
                        || *reinterpret_cast<const std::uint32_t*>(mb + kReq_Construct) != in.construct[c]
                        || *reinterpret_cast<const std::uint32_t*>(mb + kReq_PlatformCount) != kPlatformsPerCluster)
                        return false;
                    ++m;
                }
                return m == count;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        void WriteRequestSEH(std::uint8_t* self, const LiveInputs& in)
        {
            __try
            {
                std::uint32_t m = 0;
                for (int c = 0; c < kClusterCount; ++c)
                {
                    if ((in.construct[c] & 1) == 0)
                        continue;
                    std::uint8_t* mb = self + kReq_Bases + m++ * kReq_BaseStride;
                    std::uint32_t rankSum = 0;
                    std::uint32_t built = 0;
                    for (int j = 0; j < kPlatformsPerCluster; ++j)
                    {
                        const int p = c * kPlatformsPerCluster + j;
                        std::uint8_t* pf = mb + kReq_Platforms + j * kReq_PlatformStride;
                        auto& security = *reinterpret_cast<std::uint32_t*>(pf + kPf_Security);
                        security = (security & ~(kRankBits << kRankShift)) | ((g_Ranks[p] & kRankBits) << kRankShift);
                        if (in.flags & kLimitTableSet)
                            for (int g = 0; g < kCountGroups; ++g)
                                for (int kind = 0; kind < kCountKinds; ++kind)
                                {
                                    auto& count = *reinterpret_cast<std::uint32_t*>(pf + kCountField[g] + 4 * kind);
                                    count = (std::min)(count, static_cast<std::uint32_t>(g_Counts[g][kind][kFirstPlatform + p]));
                                }
                        if ((*reinterpret_cast<const std::uint32_t*>(pf + kPf_Build) >> 12) != 0)
                        {
                            rankSum += g_Ranks[p] & kRankBits;
                            ++built;
                        }
                    }
                    *reinterpret_cast<std::uint32_t*>(mb + kReq_SecurityRank) = built ? rankSum / built : 0;
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }
    }

    Outcome RemoveCustomStaffFromSecurity(std::uint8_t* packSelf, const staffServer::SectionPoints& points)
    {
        if (!packSelf || (points.customCount[kBaseDevSection] == 0 && points.customCount[staffServer::kSectionCount] == 0))
            return Outcome::Unchanged;

        std::uintptr_t rankAddr = 0;
        std::uintptr_t clampAddr = 0;
        LiveInputs in{};
        in.mbm = staffServer::LiveMotherBaseManager();
        if (!in.mbm || !ExpectedSlots(rankAddr, clampAddr) || !ReadLiveSEH(in))
            return Outcome::Unavailable;
        if (in.rankSlot != reinterpret_cast<std::uintptr_t>(ResolveGameAddress(rankAddr))
            || in.clampSlot != reinterpret_cast<std::uintptr_t>(ResolveGameAddress(clampAddr)))
            return Outcome::Unavailable;

        std::uint16_t buckets[staffServer::kSecurityBuckets] = {};
        std::uint8_t unit = 0;
        bool changed = false;
        if (!ProjectSEH(in, points, buckets, unit, changed))
            return Outcome::Mismatch;
        if (!changed)
            return Outcome::Unchanged;
        for (std::uint32_t word : in.words)
            if (((word >> 6) & 0xF) > kMaxBucketRank)
                return Outcome::Mismatch;
        if (!RequestMatchesSEH(packSelf, in))
            return Outcome::Mismatch;

        std::lock_guard<std::mutex> lock(g_Lock);
        if (!EvaluateShadowSEH(in, buckets, unit))
            return Outcome::Mismatch;
        WriteRequestSEH(packSelf, in);
        return Outcome::Corrected;
    }
}
