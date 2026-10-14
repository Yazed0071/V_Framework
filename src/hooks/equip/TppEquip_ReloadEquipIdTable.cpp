#include "pch.h"
#include "TppEquip_ReloadEquipIdTable.h"

#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "FoxHashes.h"
#include "LuaApi.h"
#include "EquipIdCompression.h"
#include "FeatureModule.h"
#include "DeployGuard.h"
#include "EquipAliasLease.h"
#include "WideMagazine.h"

namespace
{
    static constexpr size_t kRowStride = 0x18;

    static std::atomic<std::uint32_t> g_TableEpoch{ 1 };

    struct EquipIdRow
    {
        std::int32_t equipId = 0;
        std::int32_t equipType = 0;
        std::int32_t subId = 0;
        std::int32_t nativeSubId = -1;
        std::int32_t block = 0;
        std::uint64_t partsHash = 0;
        std::uint64_t packHash = 0;
        bool resident = false;
        bool released = false;
    };

    static bool SafeStampRow(std::uint8_t* dst, std::uint16_t* word,
                             const EquipIdRow& row)
    {
        __try
        {
            *reinterpret_cast<std::uint64_t*>(dst + 0x00) = row.partsHash;
            *reinterpret_cast<std::uint64_t*>(dst + 0x08) = row.packHash;
            dst[0x10] = static_cast<std::uint8_t>(row.block);
            *word = static_cast<std::uint16_t>(
                (row.equipType & 0x3F)
                | (((row.nativeSubId >= 0 ? row.nativeSubId : row.subId) & 0x3FF) << 6));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static bool SafeZeroRow(std::uint8_t* dst, std::uint16_t* word)
    {
        __try
        {
            *reinterpret_cast<std::uint64_t*>(dst + 0x00) = 0;
            *reinterpret_cast<std::uint64_t*>(dst + 0x08) = 0;
            dst[0x10] = 0;
            *word = 0;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    using ReloadEquipIdTable_t = int(__fastcall*)(lua_State* L);
    static ReloadEquipIdTable_t g_OrigReloadEquipIdTable = nullptr;

    static std::mutex g_Mutex;
    static std::vector<EquipIdRow> g_Rows;
    static std::map<int, EquipIdRow> g_Extended;
    static std::map<int, int> g_WideOfAliasRow;
    struct AmmoRootParam
    {
        int logical;
        int native;
    };
    static std::map<std::int32_t, AmmoRootParam> g_AmmoRootParams;

    static std::uint8_t* g_InfoMirror = nullptr;
    static std::uint8_t* g_InfoMirrorReserved = nullptr;
    static bool g_MirrorSitesPatched = false;
    static std::size_t g_MirrorSitesSkipped = 0;
    static constexpr std::int32_t kMirrorRows =
        EquipIdCompression::kWideFoldedLast + 1;

    static bool MirrorCopyNativeSEH()
    {
        const auto* infoList = static_cast<const std::uint8_t*>(
            ResolveGameAddress(gAddr.EquipIdTable_InfoList));
        if (!g_InfoMirror || !infoList)
            return false;
        __try
        {
            std::memset(g_InfoMirror, 0,
                        static_cast<size_t>(kMirrorRows) * kRowStride);
            std::memcpy(g_InfoMirror, infoList,
                        static_cast<size_t>(
                            EquipIdCompression::kCompressedSlotBound) * kRowStride);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static void MirrorStampRowNoLock(const EquipIdRow& row)
    {
        if (!g_InfoMirror)
            return;
        const std::int32_t idx =
            EquipIdCompression::ComputeCompressed(row.equipId);
        if (idx < 0 || idx >= kMirrorRows)
            return;
        std::uint16_t dummy = 0;
        SafeStampRow(g_InfoMirror + static_cast<size_t>(idx) * kRowStride,
                     &dummy, row);
    }

    static void MirrorStampAliasRowNoLock(int aliasId, const EquipIdRow& wideRow)
    {
        EquipIdRow aliasRow = wideRow;
        aliasRow.equipId = aliasId;
        MirrorStampRowNoLock(aliasRow);
    }

    static void RestampAliasesOfNoLock(const EquipIdRow& wideRow)
    {
        for (const auto& kv : g_WideOfAliasRow)
            if (kv.second == wideRow.equipId)
                MirrorStampAliasRowNoLock(kv.first, wideRow);
    }

    static void RefreshInfoMirror()
    {
        if (!MirrorCopyNativeSEH())
            return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const auto& kv : g_Extended)
        {
            if (kv.second.released)
                continue;
            MirrorStampRowNoLock(kv.second);
        }
        for (const auto& kv : g_WideOfAliasRow)
        {
            const auto wide = g_Extended.find(kv.second);
            if (wide != g_Extended.end() && !wide->second.released)
                MirrorStampAliasRowNoLock(kv.first, wide->second);
        }
    }

    struct MirrorAbsDispSite
    {
        std::uintptr_t va;
        std::uint8_t   prefix[4];
        std::uint8_t   prefixLen;
        std::uint8_t   dispOffset;
        std::uint32_t  expectedDisp;
        std::uint32_t  rowOffset;
    };

    struct MirrorRipSite
    {
        std::uintptr_t va;
        std::uint32_t  rowOffset;
    };

    struct MirrorSiteTable
    {
        const MirrorRipSite*      rip;
        std::size_t               ripCount;
        const MirrorAbsDispSite*  abs;
        std::size_t               absCount;
        std::uintptr_t            nativeBase;
    };

    static const MirrorRipSite kMirrorRipSitesEn154[] = {
        { 0x140a03587ull, 0x00u }, { 0x140a03ca4ull, 0x00u },
        { 0x140a047a4ull, 0x10u }, { 0x140a04832ull, 0x10u },
        { 0x140a05487ull, 0x00u }, { 0x140a05b80ull, 0x00u },
        { 0x140a05dd2ull, 0x00u }, { 0x140a05e15ull, 0x00u },
        { 0x140a0676cull, 0x00u }, { 0x140a07556ull, 0x00u },
        { 0x140a079d8ull, 0x00u }, { 0x140a081bbull, 0x00u },
    };
    static const MirrorAbsDispSite kMirrorAbsSitesEn154[] = {
        { 0x140a0588aull, { 0x4C, 0x8D, 0xBA, 0x00 }, 3, 3, 0x02C20FD0u, 0x00u },
        { 0x140a058beull, { 0x0F, 0xB6, 0x94, 0xCA }, 4, 4, 0x02C20FE0u, 0x10u },
    };

    static const MirrorRipSite kMirrorRipSitesJp154[] = {
        { 0x140a03397ull, 0x00u }, { 0x140a03ab4ull, 0x00u },
        { 0x140a045b4ull, 0x10u }, { 0x140a04642ull, 0x10u },
        { 0x140a05297ull, 0x00u }, { 0x140a05990ull, 0x00u },
        { 0x140a05be2ull, 0x00u }, { 0x140a05c25ull, 0x00u },
        { 0x140a0657cull, 0x00u }, { 0x140a07366ull, 0x00u },
        { 0x140a077e8ull, 0x00u }, { 0x140a07fcbull, 0x00u },
    };
    static const MirrorAbsDispSite kMirrorAbsSitesJp154[] = {
        { 0x140a0569aull, { 0x4C, 0x8D, 0xBA, 0x00 }, 3, 3, 0x02C20FD0u, 0x00u },
        { 0x140a056ceull, { 0x0F, 0xB6, 0x94, 0xCA }, 4, 4, 0x02C20FE0u, 0x10u },
    };

    static const MirrorRipSite kMirrorRipSitesEn153[] = {
        { 0x140a02be7ull, 0x00u }, { 0x1463c0ec3ull, 0x00u },
        { 0x1463c4494ull, 0x10u }, { 0x1463c47e2ull, 0x10u },
        { 0x1463c5437ull, 0x00u }, { 0x1463c763full, 0x00u },
        { 0x1463c7762ull, 0x00u }, { 0x1463c79a5ull, 0x00u },
        { 0x140a0607cull, 0x00u }, { 0x1463dfde6ull, 0x00u },
        { 0x140a07298ull, 0x00u }, { 0x140a07a7bull, 0x00u },
    };
    static const MirrorAbsDispSite kMirrorAbsSitesEn153[] = {
        { 0x140a050daull, { 0x4C, 0x8D, 0xBA, 0x00 }, 3, 3, 0x02C20FB0u, 0x00u },
        { 0x140a0510eull, { 0x0F, 0xB6, 0x94, 0xCA }, 4, 4, 0x02C20FC0u, 0x10u },
    };

    static const MirrorRipSite kMirrorRipSitesJp153[] = {
        { 0x140a026b7ull, 0x00u }, { 0x14801b7f3ull, 0x00u },
        { 0x14801f744ull, 0x10u }, { 0x14801f972ull, 0x10u },
        { 0x1480212b7ull, 0x00u }, { 0x148023cffull, 0x00u },
        { 0x148024182ull, 0x00u }, { 0x148024995ull, 0x00u },
        { 0x140a05b4cull, 0x00u }, { 0x14802a756ull, 0x00u },
        { 0x140a06d68ull, 0x00u }, { 0x140a0754bull, 0x00u },
    };
    static const MirrorAbsDispSite kMirrorAbsSitesJp153[] = {
        { 0x140a04baaull, { 0x4C, 0x8D, 0xBA, 0x00 }, 3, 3, 0x02C20FB0u, 0x00u },
        { 0x140a04bdeull, { 0x0F, 0xB6, 0x94, 0xCA }, 4, 4, 0x02C20FC0u, 0x10u },
    };

    static const MirrorSiteTable kMirrorTableEn154 = {
        kMirrorRipSitesEn154, sizeof(kMirrorRipSitesEn154) / sizeof(kMirrorRipSitesEn154[0]),
        kMirrorAbsSitesEn154, sizeof(kMirrorAbsSitesEn154) / sizeof(kMirrorAbsSitesEn154[0]),
        0x142c20fd0ull,
    };

    static const MirrorSiteTable kMirrorTableJp154 = {
        kMirrorRipSitesJp154, sizeof(kMirrorRipSitesJp154) / sizeof(kMirrorRipSitesJp154[0]),
        kMirrorAbsSitesJp154, sizeof(kMirrorAbsSitesJp154) / sizeof(kMirrorAbsSitesJp154[0]),
        0x142c20fd0ull,
    };
    static const MirrorSiteTable kMirrorTableEn153 = {
        kMirrorRipSitesEn153, sizeof(kMirrorRipSitesEn153) / sizeof(kMirrorRipSitesEn153[0]),
        kMirrorAbsSitesEn153, sizeof(kMirrorAbsSitesEn153) / sizeof(kMirrorAbsSitesEn153[0]),
        0x142c20fb0ull,
    };
    static const MirrorSiteTable kMirrorTableJp153 = {
        kMirrorRipSitesJp153, sizeof(kMirrorRipSitesJp153) / sizeof(kMirrorRipSitesJp153[0]),
        kMirrorAbsSitesJp153, sizeof(kMirrorAbsSitesJp153) / sizeof(kMirrorAbsSitesJp153[0]),
        0x142c20fb0ull,
    };

    static const MirrorSiteTable* MirrorTableForBuild()
    {
        switch (gGameBuild)
        {
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4:
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4a:
            return &kMirrorTableEn154;
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4:
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4a:
            return &kMirrorTableJp154;
        case ::AddressSetRuntime::GameBuild::En_1_0_15_3:
            return &kMirrorTableEn153;
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_3:
            return &kMirrorTableJp153;
        default:
            return nullptr;
        }
    }
    static constexpr std::uintptr_t kMirrorImageBase  = 0x140000000ull;
    static constexpr std::uintptr_t kMirrorNearBand   = 0x40000000ull;
    static constexpr std::uintptr_t kMirrorMaxRowDisp = 0x10ull;

    static std::uintptr_t MirrorExeBase()
    {
        const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        return base ? base : kMirrorImageBase;
    }

    static bool MirrorReadSiteSEH(const void* at, std::uint8_t* out, std::size_t len)
    {
        __try
        {
            std::memcpy(out, at, len);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static void MirrorReaderReach(std::size_t size, std::uintptr_t& lo, std::uintptr_t& hi)
    {
        const MirrorSiteTable* table = MirrorTableForBuild();
        std::uintptr_t minNext = ~0ull;
        std::uintptr_t maxNext = 0;
        for (std::size_t i = 0; table && i < table->ripCount; ++i)
        {
            const auto next = reinterpret_cast<std::uintptr_t>(
                ResolveGameAddress(table->rip[i].va)) + 7;
            minNext = (std::min)(minNext, next);
            maxNext = (std::max)(maxNext, next);
        }
        if (!table || minNext > maxNext)
        {
            lo = 0;
            hi = 0;
            return;
        }
        const std::uintptr_t exeBase = MirrorExeBase();
        std::uintptr_t lower = maxNext - 0x80000000ull;
        std::uintptr_t upper = minNext + 0x7FFFFFFFull - kMirrorMaxRowDisp;
        lower = (std::max)(lower, exeBase - 0x80000000ull);
        upper = (std::min)(upper, exeBase + 0x7FFFFFFFull - kMirrorMaxRowDisp);
        lo = lower;
        hi = upper > size ? upper - size : 0;
    }

    static std::uintptr_t MirrorImageEnd()
    {
        const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        if (!base)
            return kMirrorImageBase;
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            return base;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
            base + static_cast<std::uintptr_t>(dos->e_lfanew));
        if (nt->Signature != IMAGE_NT_SIGNATURE)
            return base;
        return base + nt->OptionalHeader.SizeOfImage;
    }

    struct MirrorWalkStats
    {
        std::size_t largestFree[3] = { 0, 0, 0 };
        DWORD       lastError      = 0;
        bool        commitFailed   = false;
    };

    static bool MirrorCommitCharge(DWORD err)
    {
        return err == ERROR_COMMITMENT_LIMIT || err == ERROR_NOT_ENOUGH_MEMORY
            || err == ERROR_OUTOFMEMORY || err == ERROR_NO_SYSTEM_RESOURCES
            || err == ERROR_PAGEFILE_QUOTA || err == ERROR_NOT_ENOUGH_QUOTA;
    }

    static void* MirrorWalkUp(std::uintptr_t from, std::uintptr_t to, std::size_t size,
                              std::uintptr_t gran, std::uintptr_t lo, std::uintptr_t hi,
                              bool reserveOnly, std::size_t& largestFree,
                              MirrorWalkStats& stats)
    {
        if (to < from + size)
            return nullptr;
        const DWORD kind = reserveOnly ? MEM_RESERVE : (MEM_RESERVE | MEM_COMMIT);
        const DWORD prot = reserveOnly ? PAGE_NOACCESS : PAGE_READWRITE;
        const std::uintptr_t lastBase = (std::min)(hi, to - size);
        for (std::uintptr_t a = (std::max)(from, lo); a <= lastBase; )
        {
            MEMORY_BASIC_INFORMATION mbi{};
            if (VirtualQuery(reinterpret_cast<LPCVOID>(a), &mbi, sizeof(mbi)) != sizeof(mbi))
                break;
            const auto regionBase = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
            const std::uintptr_t regionEnd = regionBase + mbi.RegionSize;
            if (mbi.State == MEM_FREE)
            {
                const std::uintptr_t runStart = (std::max)(regionBase, a);
                const std::uintptr_t runEnd   = (std::min)(regionEnd, to);
                if (runEnd > runStart)
                    largestFree = (std::max)(largestFree,
                                             static_cast<std::size_t>(runEnd - runStart));
                std::uintptr_t base = (runStart + gran - 1) & ~(gran - 1);
                for (; base + size <= regionEnd && base <= lastBase; base += gran)
                {
                    if (void* p = VirtualAlloc(reinterpret_cast<LPVOID>(base), size, kind, prot))
                        return p;
                    stats.lastError = GetLastError();
                    if (MirrorCommitCharge(stats.lastError))
                    {
                        stats.commitFailed = true;
                        return nullptr;
                    }
                }
            }
            if (regionEnd <= a)
                break;
            a = regionEnd;
        }
        return nullptr;
    }

    static void* AllocateMirrorInReaderReach(std::size_t size, bool reserveOnly,
                                             MirrorWalkStats& stats)
    {
        SYSTEM_INFO si{};
        GetSystemInfo(&si);
        const std::uintptr_t gran = si.dwAllocationGranularity ? si.dwAllocationGranularity
                                                               : 0x10000ull;
        std::uintptr_t lo = 0, hi = 0;
        MirrorReaderReach(size, lo, hi);
        if (hi < lo)
            return nullptr;

        const std::uintptr_t imageEnd = MirrorImageEnd();
        const std::uintptr_t exeBase  = MirrorExeBase();
        const std::uintptr_t farFrom  = exeBase + kMirrorNearBand;
        if (void* p = MirrorWalkUp(farFrom, hi + size, size, gran, lo, hi, reserveOnly,
                                   stats.largestFree[0], stats))
            return p;
        if (stats.commitFailed)
            return nullptr;
        if (void* p = MirrorWalkUp(imageEnd, farFrom + size, size, gran, lo, hi, reserveOnly,
                                   stats.largestFree[1], stats))
            return p;
        if (stats.commitFailed)
            return nullptr;
        return MirrorWalkUp(lo, exeBase, size, gran, lo, hi, reserveOnly,
                            stats.largestFree[2], stats);
    }

    static std::size_t PatchInfoListLeaSites()
    {
        const MirrorSiteTable* table = MirrorTableForBuild();
        if (!table)
            return 0;
        const auto nativeBase =
            reinterpret_cast<std::uintptr_t>(ResolveGameAddress(table->nativeBase));
        const std::uintptr_t imageBase = MirrorExeBase();
        std::size_t patched = 0;
        std::size_t skipped = 0;

        for (std::size_t s = 0; s < table->ripCount; ++s)
        {
            const MirrorRipSite& site = table->rip[s];
            const auto va = reinterpret_cast<std::uintptr_t>(ResolveGameAddress(site.va));
            auto* p = reinterpret_cast<std::uint8_t*>(va);
            std::uint8_t head[7] = {};
            if (!p || !MirrorReadSiteSEH(p, head, sizeof(head)))
            {
                ++skipped;
                continue;
            }
            if ((head[0] != 0x48 && head[0] != 0x4C) || head[1] != 0x8D)
            {
                ++skipped;
                continue;
            }
            std::int32_t disp = 0;
            std::memcpy(&disp, head + 3, sizeof(disp));
            if (va + 7 + static_cast<std::intptr_t>(disp)
                != nativeBase + site.rowOffset)
            {
                ++skipped;
                continue;
            }
            const std::intptr_t delta =
                reinterpret_cast<std::intptr_t>(g_InfoMirror) + site.rowOffset
                - static_cast<std::intptr_t>(va + 7);
            if (delta < INT32_MIN || delta > INT32_MAX)
            {
                LogError("EquipIdTable: InfoList mirror out of rel32 range of the readers, extended weapons never load");
                break;
            }
            DWORD old = 0;
            if (VirtualProtect(p + 3, 4, PAGE_EXECUTE_READWRITE, &old))
            {
                *reinterpret_cast<std::int32_t*>(p + 3) =
                    static_cast<std::int32_t>(delta);
                VirtualProtect(p + 3, 4, old, &old);
                ++patched;
            }
        }

        for (std::size_t a = 0; a < table->absCount; ++a)
        {
            const MirrorAbsDispSite& site = table->abs[a];
            auto* p = static_cast<std::uint8_t*>(ResolveGameAddress(site.va));
            std::uint8_t head[8] = {};
            if (!p || !MirrorReadSiteSEH(p, head, sizeof(head)))
            {
                ++skipped;
                continue;
            }
            std::uint32_t curDisp = 0;
            std::memcpy(&curDisp, head + site.dispOffset, sizeof(curDisp));
            if (std::memcmp(head, site.prefix, site.prefixLen) != 0
                || curDisp != site.expectedDisp)
            {
                ++skipped;
                continue;
            }
            const std::intptr_t delta =
                reinterpret_cast<std::intptr_t>(g_InfoMirror) + site.rowOffset
                - static_cast<std::intptr_t>(imageBase);
            if (delta < INT32_MIN || delta > INT32_MAX)
            {
                LogError("EquipIdTable: InfoList mirror out of disp32 range of the image base, extended weapons never realize");
                break;
            }
            DWORD old = 0;
            if (VirtualProtect(p + site.dispOffset, 4, PAGE_EXECUTE_READWRITE,
                               &old))
            {
                *reinterpret_cast<std::int32_t*>(p + site.dispOffset) =
                    static_cast<std::int32_t>(delta);
                VirtualProtect(p + site.dispOffset, 4, old, &old);
                ++patched;
            }
        }

        g_MirrorSitesSkipped = skipped;
        return patched;
    }

    static void WriteNativeRow(const EquipIdRow& row)
    {
        const std::int32_t index = EquipIdCompression::ComputeCompressed(row.equipId);
        if (!EquipIdCompression::IsCompressedInBounds(index))
        {
            if (EquipIdCompression::IsWideEquipId(row.equipId))
            {
                TppEquip_EnsureInfoListMirror();
                std::lock_guard<std::mutex> lock(g_Mutex);
                g_Extended[row.equipId] = row;
                MirrorStampRowNoLock(row);
                RestampAliasesOfNoLock(row);
                return;
            }
            if (EquipIdCompression::IsExtendedEquipId(row.equipId))
            {
                if (!EquipIdCompression::MarkExtendedEquipIdUsed(row.equipId))
                {
                    LogError("EquipIdTable: equipId %d shares InfoList row 0x%X with another custom equipId, row dropped",
                        row.equipId, static_cast<unsigned>(index));
                    return;
                }
                TppEquip_EnsureInfoListMirror();
                std::lock_guard<std::mutex> lock(g_Mutex);
                g_Extended[row.equipId] = row;
                MirrorStampRowNoLock(row);
                return;
            }
            static bool s_foldWarned = false;
            if (!s_foldWarned)
            {
                s_foldWarned = true;
                LogWarn("EquipIdTable: equipId %d folds to 0x%X, outside native and extended range, row dropped",
                    row.equipId, static_cast<unsigned>(index));
            }
            return;
        }

        auto* infoList = static_cast<std::uint8_t*>(
            ResolveGameAddress(gAddr.EquipIdTable_InfoList));
        auto* typeWords = static_cast<std::uint16_t*>(
            ResolveGameAddress(gAddr.EquipIdTable_TypeWords));
        if (!infoList || !typeWords)
        {
            static bool s_warned = false;
            if (!s_warned)
            {
                s_warned = true;
                LogError("EquipIdTable: native InfoList address missing, custom equip rows not written");
            }
            return;
        }

        if ((row.nativeSubId >= 0 ? row.nativeSubId : row.subId) > 0x3FF)
        {
            static bool s_subIdWarned = false;
            if (!s_subIdWarned)
            {
                s_subIdWarned = true;
                LogWarn("EquipIdTable: equipId %d subId %d exceeds 1023, needs an extended equipId, row dropped",
                    row.equipId, row.subId);
            }
            return;
        }

        std::uint8_t* dst = infoList + static_cast<size_t>(index) * kRowStride;
        if (!SafeStampRow(dst, typeWords + index, row))
        {
            static bool s_stampWarned = false;
            if (!s_stampWarned)
            {
                s_stampWarned = true;
                LogWarn("EquipIdTable: equipId %d native row write faulted, row skipped", row.equipId);
            }
            return;
        }

        EquipIdCompression::MarkCompressedSlotUsed(index);
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            MirrorStampRowNoLock(row);
            for (auto& existing : g_Rows)
                if (existing.equipId == row.equipId)
                {
                    existing.resident = true;
                    break;
                }
        }
    }

    static bool EraseNativeRow(std::int32_t equipId)
    {
        const std::int32_t index = EquipIdCompression::ComputeCompressed(equipId);
        if (!EquipIdCompression::IsCompressedInBounds(index))
        {
            return false;
        }
        auto* infoList = static_cast<std::uint8_t*>(
            ResolveGameAddress(gAddr.EquipIdTable_InfoList));
        auto* typeWords = static_cast<std::uint16_t*>(
            ResolveGameAddress(gAddr.EquipIdTable_TypeWords));
        if (!infoList || !typeWords)
        {
            LogError("EquipIdTable: native InfoList address missing, equipId %d row not released", equipId);
            return false;
        }
        std::uint8_t* dst = infoList + static_cast<size_t>(index) * kRowStride;
        if (!SafeZeroRow(dst, typeWords + index))
        {
            LogWarn("EquipIdTable: equipId %d native row erase faulted, row kept", equipId);
            return false;
        }
        EquipIdCompression::ClearCompressedSlotUsed(index);
        return true;
    }

    static bool ReadRowNumber(lua_State* L, int n, double& out)
    {
        out = 0.0;
        g_lua_rawgeti(L, -1, n);
        const bool ok = g_lua_isnumber(L, -1) != 0;
        if (ok)
            out = static_cast<double>(g_lua_tonumber(L, -1));
        g_lua_settop(L, -2);
        return ok;
    }

    static bool ReadRowPathHash(lua_State* L, int n, std::uint64_t& out)
    {
        out = 0;
        g_lua_rawgeti(L, -1, n);
        const bool ok = g_lua_type(L, -1) == LUA_TSTRING;
        if (ok)
        {
            const char* path = g_lua_tolstring(L, -1, nullptr);
            if (path && path[0])
                out = FoxHashes::PathCode64Ext(path);
        }
        g_lua_settop(L, -2);
        return ok;
    }

    static void QueueAndWrite(const EquipIdRow& rowIn)
    {
        EquipIdRow row = rowIn;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            if (const auto it = g_AmmoRootParams.find(row.equipId); it != g_AmmoRootParams.end())
            {
                row.subId = it->second.logical;
                row.nativeSubId = it->second.native;
            }
            bool replaced = false;
            for (auto& existing : g_Rows)
            {
                if (existing.equipId == row.equipId)
                {
                    if (existing.partsHash != row.partsHash || existing.equipType != row.equipType)
                        LogWarn("AddToEquipIdTable: equipId %d registered twice (type %d->%d, parts %016llX->%016llX), first row overwritten",
                            row.equipId, existing.equipType, row.equipType,
                            static_cast<unsigned long long>(existing.partsHash),
                            static_cast<unsigned long long>(row.partsHash));
                    existing = row;
                    replaced = true;
                    break;
                }
            }
            if (!replaced)
                g_Rows.push_back(row);
        }

        WriteNativeRow(row);
        g_TableEpoch.fetch_add(1, std::memory_order_release);
    }

    static void ReapplyAll()
    {
        std::vector<EquipIdRow> snapshot;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            snapshot = g_Rows;
        }
        for (const auto& row : snapshot)
        {
            if (row.released)
                continue;
            WriteNativeRow(row);
        }
        RefreshInfoMirror();
    }

    static int __fastcall hkReloadEquipIdTable(lua_State* L)
    {
        const int ret = g_OrigReloadEquipIdTable ? g_OrigReloadEquipIdTable(L) : 0;
        ReapplyAll();
        return ret;
    }

    using GetEquipTypeId_t = unsigned int(__fastcall*)(void* self, unsigned int equipId);
    static GetEquipTypeId_t g_OrigGetEquipTypeId = nullptr;

    static bool IsCustomBandEquipId(std::int32_t equipId)
    {
        return EquipIdCompression::IsExtendedEquipId(equipId)
            || EquipIdCompression::IsWideEquipId(equipId);
    }

    static unsigned int __fastcall hkGetEquipTypeId(void* self, unsigned int equipId)
    {
        const std::int32_t realId = equip::FromEngine(static_cast<std::int32_t>(equipId));
        if (IsCustomBandEquipId(realId))
        {
            V_ExtendedEquipRow ext;
            if (TppEquip_GetExtendedEquipRow(realId, &ext))
                return static_cast<unsigned int>(ext.equipType & 0x3F);
            if (EquipIdCompression::IsWideEquipId(realId))
                return 0;
        }
        return g_OrigGetEquipTypeId ? g_OrigGetEquipTypeId(self, equipId) : 0;
    }

    using GetEquipSubId_t = unsigned int(__fastcall*)(void* self, unsigned int equipId);
    static GetEquipSubId_t g_OrigGetEquipSubId = nullptr;

    static uintptr_t GetEquipSubIdAddr()
    {
        return gAddr.EquipIdTable_GetRootParameterId;
    }

    static bool EquipIdAccessorHasFold(const void* fn, std::size_t span,
                                       const std::uint8_t* fold, std::size_t foldLen)
    {
        std::uint8_t head[16] = {};
        if (!fn || span > sizeof(head) || !MirrorReadSiteSEH(fn, head, span))
            return false;
        for (std::size_t i = 0; i + foldLen <= span; ++i)
            if (std::memcmp(head + i, fold, foldLen) == 0)
                return true;
        return false;
    }

    static bool IsGetEquipSubIdBody(const void* fn)
    {
        static const std::uint8_t kFold[] = { 0x8D, 0x82, 0xD0, 0xFD, 0xFF, 0xFF };
        return EquipIdAccessorHasFold(fn, 8, kFold, sizeof(kFold));
    }

    static bool IsGetEquipTypeIdBody(const void* fn)
    {
        static const std::uint8_t kFold[] = { 0x8D, 0x82, 0x99, 0xFC, 0xFF, 0xFF };
        return EquipIdAccessorHasFold(fn, 12, kFold, sizeof(kFold));
    }

    static unsigned int __fastcall hkGetEquipSubId(void* self, unsigned int engineEquipId)
    {
        const unsigned int equipId =
            static_cast<unsigned int>(equip::FromEngine(static_cast<std::int32_t>(engineEquipId)));
        if (WideMagazine_IsActive())
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            const auto ammo = g_AmmoRootParams.find(static_cast<int>(equipId));
            if (ammo != g_AmmoRootParams.end())
            {
                bool owned = false;
                bool released = false;
                const auto extended = g_Extended.find(static_cast<int>(equipId));
                if (extended != g_Extended.end())
                {
                    owned = true;
                    released = extended->second.released;
                }
                for (const auto& row : g_Rows)
                    if (row.equipId == static_cast<int>(equipId))
                    {
                        owned = true;
                        released = released || row.released;
                        break;
                    }
                if (owned)
                    return released ? 0u : static_cast<unsigned>(ammo->second.logical);
            }
        }
        if (IsCustomBandEquipId(static_cast<std::int32_t>(equipId)))
        {
            V_ExtendedEquipRow ext;
            if (TppEquip_GetExtendedEquipRow(static_cast<int>(equipId), &ext))
            {
                if (ext.subId < 0 || ext.subId > 0xFFFF)
                    return 0;
                return static_cast<unsigned int>(ext.subId);
            }
            if (EquipIdCompression::IsWideEquipId(static_cast<std::int32_t>(equipId)))
                return 0;
        }
        return g_OrigGetEquipSubId ? g_OrigGetEquipSubId(self, engineEquipId) : 0;
    }
}

bool TppEquip_WideAmmoRootReady()
{
    return g_OrigGetEquipSubId != nullptr;
}

std::uint32_t TppEquip_EquipTableEpoch()
{
    return g_TableEpoch.load(std::memory_order_acquire);
}

int TppEquip_GetSubIdForEquipId(int equipId)
{
    equipId = equip::FromEngine(equipId);
    std::lock_guard<std::mutex> lock(g_Mutex);
    for (const auto& r : g_Rows)
        if (r.equipId == equipId)
            return (r.equipType >= 1 && r.equipType <= 8) ? r.subId : 0;
    auto it = g_Extended.find(equipId);
    if (it != g_Extended.end() && !it->second.released)
        return (it->second.equipType >= 1 && it->second.equipType <= 8)
                   ? it->second.subId : 0;
    return 0;
}

int TppEquip_GetSupportWeaponIdForEquipId(int equipId)
{
    constexpr int kEquipTypeThrowing = 12;
    constexpr int kEquipTypePlaced   = 13;
    const auto isSupportWeapon = [](int equipType) {
        return equipType == kEquipTypeThrowing || equipType == kEquipTypePlaced;
    };
    equipId = equip::FromEngine(equipId);
    std::lock_guard<std::mutex> lock(g_Mutex);
    for (const auto& r : g_Rows)
        if (r.equipId == equipId)
            return isSupportWeapon(r.equipType) ? r.subId : -1;
    auto it = g_Extended.find(equipId);
    if (it != g_Extended.end() && !it->second.released)
        return isSupportWeapon(it->second.equipType) ? it->second.subId : -1;
    return -1;
}

void TppEquip_GetCustomWeaponEquipIds(std::vector<int>& out)
{
    out.clear();
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        out.reserve(g_Rows.size() + g_Extended.size());
        for (const auto& r : g_Rows)
            if (r.equipType >= 1 && r.equipType <= 8 && r.subId != 0)
                out.push_back(r.equipId);
        for (const auto& kv : g_Extended)
            if (!kv.second.released && kv.second.equipType >= 1
                && kv.second.equipType <= 8 && kv.second.subId != 0)
                out.push_back(kv.first);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
}

bool TppEquip_GetExtendedEquipRow(int equipId, V_ExtendedEquipRow* out)
{
    if (!out)
        return false;
    equipId = equip::FromEngine(equipId);
    std::lock_guard<std::mutex> lock(g_Mutex);
    auto it = g_Extended.find(equipId);
    if (it == g_Extended.end() || it->second.released)
        return false;
    out->equipType = it->second.equipType;
    out->subId     = it->second.subId;
    out->block     = it->second.block;
    out->partsHash = it->second.partsHash;
    out->packHash  = it->second.packHash;
    return true;
}

bool TppEquip_GetCustomEquipPack(int equipId, std::uint64_t* outPack)
{
    if (!outPack)
        return false;
    *outPack = 0;
    equipId = equip::FromEngine(equipId);
    std::lock_guard<std::mutex> lock(g_Mutex);
    for (const auto& r : g_Rows)
    {
        if (r.equipId == equipId && !r.released)
        {
            *outPack = r.packHash;
            return r.packHash != 0;
        }
    }
    auto it = g_Extended.find(equipId);
    if (it == g_Extended.end() || it->second.released)
        return false;
    *outPack = it->second.packHash;
    return it->second.packHash != 0;
}

bool TppEquip_BindAliasRow(int aliasId, int wideId)
{
    if (!TppEquip_EnsureInfoListMirror())
        return false;
    std::lock_guard<std::mutex> lock(g_Mutex);
    const auto wide = g_Extended.find(wideId);
    if (wide == g_Extended.end() || wide->second.released || !g_InfoMirror)
        return false;
    g_WideOfAliasRow[aliasId] = wideId;
    MirrorStampAliasRowNoLock(aliasId, wide->second);
    return true;
}

void TppEquip_ReserveInfoListMirrorEarly()
{
    if (!MirrorTableForBuild())
        return;
    MirrorWalkStats stats;
    g_InfoMirrorReserved = static_cast<std::uint8_t*>(AllocateMirrorInReaderReach(
        static_cast<size_t>(kMirrorRows) * kRowStride, true, stats));
}

bool TppEquip_EnsureInfoListMirror()
{
    if (g_MirrorSitesPatched)
        return true;
    if (FeatureIsDisabled("ExtendedInfoListMirror"))
    {
        g_MirrorSitesPatched = true;
        DeployGuard::ForceDropExtendedIds();
        if (g_InfoMirrorReserved)
        {
            VirtualFree(g_InfoMirrorReserved, 0, MEM_RELEASE);
            g_InfoMirrorReserved = nullptr;
        }
        LogWarn("EquipIdTable: InfoList mirror disabled by disabled_modules.txt, extended equipIds (649+) dropped from loadout");
        return false;
    }
    if (!MirrorTableForBuild())
    {
        g_MirrorSitesPatched = true;
        DeployGuard::ForceDropExtendedIds();
        if (g_InfoMirrorReserved)
        {
            VirtualFree(g_InfoMirrorReserved, 0, MEM_RELEASE);
            g_InfoMirrorReserved = nullptr;
        }
        LogError("EquipIdTable: no InfoList reader-site table for %s, extended equipIds (649+) dropped from loadout",
            GetGameBuildName(gGameBuild));
        return false;
    }
    if (!g_InfoMirror)
    {
        const std::size_t bytes = static_cast<size_t>(kMirrorRows) * kRowStride;
        MirrorWalkStats stats;
        if (g_InfoMirrorReserved)
        {
            g_InfoMirror = static_cast<std::uint8_t*>(VirtualAlloc(
                g_InfoMirrorReserved, bytes, MEM_COMMIT, PAGE_READWRITE));
            if (!g_InfoMirror)
            {
                stats.lastError    = GetLastError();
                stats.commitFailed = MirrorCommitCharge(stats.lastError);
                VirtualFree(g_InfoMirrorReserved, 0, MEM_RELEASE);
            }
            g_InfoMirrorReserved = nullptr;
        }
        if (!g_InfoMirror && !stats.commitFailed)
            g_InfoMirror = static_cast<std::uint8_t*>(
                AllocateMirrorInReaderReach(bytes, false, stats));
        if (!g_InfoMirror)
        {
            std::uintptr_t lo = 0, hi = 0;
            MirrorReaderReach(bytes, lo, hi);
            g_MirrorSitesPatched = true;
            DeployGuard::ForceDropExtendedIds();
            if (stats.commitFailed)
                LogError("EquipIdTable: out of commit charge (error %lu) for the %zu KB InfoList mirror, extended equipIds dropped",
                    stats.lastError, bytes / 1024);
            else
                LogError("EquipIdTable: no free %zu KB run in reader reach 0x%llX-0x%llX (largest %zu/%zu/%zu KB, error %lu), extended equipIds dropped",
                    bytes / 1024, static_cast<unsigned long long>(lo),
                    static_cast<unsigned long long>(hi + bytes),
                    stats.largestFree[0] / 1024, stats.largestFree[1] / 1024,
                    stats.largestFree[2] / 1024, stats.lastError);
            return false;
        }
    }
    RefreshInfoMirror();
    const std::size_t patched = PatchInfoListLeaSites();
    g_MirrorSitesPatched = true;
    if (patched == 0)
    {
        DeployGuard::ForceDropExtendedIds();
        VirtualFree(g_InfoMirror, 0, MEM_RELEASE);
        g_InfoMirror = nullptr;
        LogError("EquipIdTable: no InfoList reader site repointed, extended equipIds (649+) dropped from loadout");
        return false;
    }
    if (g_MirrorSitesSkipped != 0)
        LogWarn("EquipIdTable: %zu InfoList reader site(s) not repointed, extended weapons through them never load",
            g_MirrorSitesSkipped);
    return true;
}

int __cdecl l_AddToEquipIdTable(lua_State* L)
{
    if (!ResolveLuaApi() || !g_lua_objlen || !g_lua_rawgeti)
        return 0;
    if (g_lua_type(L, 1) != LUA_TTABLE)
    {
        LogWarn("AddToEquipIdTable: argument #1 must be a table");
        return 0;
    }

    const int rowCount = static_cast<int>(g_lua_objlen(L, 1));
    for (int i = 1; i <= rowCount; ++i)
    {
        g_lua_rawgeti(L, 1, i);
        if (g_lua_type(L, -1) == LUA_TTABLE)
        {
            double idN, typeN, subN, blockN;
            EquipIdRow row;
            if (ReadRowNumber(L, 1, idN) &&
                ReadRowNumber(L, 2, typeN) &&
                ReadRowNumber(L, 3, subN) &&
                ReadRowNumber(L, 4, blockN) &&
                ReadRowPathHash(L, 5, row.partsHash) &&
                ReadRowPathHash(L, 6, row.packHash))
            {
                row.equipId   = static_cast<std::int32_t>(idN);
                row.equipType = static_cast<std::int32_t>(typeN);
                row.subId     = static_cast<std::int32_t>(subN);
                row.block     = static_cast<std::int32_t>(blockN);
                if (row.equipId > 0)
                    QueueAndWrite(row);
            }
            else
            {
                LogWarn("AddToEquipIdTable: malformed row %d ignored", i);
            }
        }
        g_lua_settop(L, -2);
    }

    return 0;
}

bool Install_TppEquip_ReloadEquipIdTable_Hook()
{
    void* target = ResolveGameAddress(gAddr.EquipIdTable_ReloadEquipIdTable);
    if (!target)
    {
        LogError("EquipIdTable: ReloadEquipIdTable address missing, custom equip rows lost on table reload");
        return true;
    }

    const bool ok = CreateAndEnableHook(
        target, &hkReloadEquipIdTable,
        reinterpret_cast<void**>(&g_OrigReloadEquipIdTable));
    if (!ok)
    {
        LogError("EquipIdTable: ReloadEquipIdTable hook failed at %p, custom equip rows lost on table reload", target);
    }

    void* typeTarget = ResolveGameAddress(gAddr.EquipIdTable_GetEquipTypeId);
    if (typeTarget && !IsGetEquipTypeIdBody(typeTarget))
    {
        LogError("EquipIdTable: GetEquipTypeId %p fold missing on %s, extended equipIds get no type",
            typeTarget, GetGameBuildName(gGameBuild));
        typeTarget = nullptr;
    }
    if (typeTarget)
    {
        const bool okT = CreateAndEnableHook(
            typeTarget, &hkGetEquipTypeId,
            reinterpret_cast<void**>(&g_OrigGetEquipTypeId));
        if (!okT)
            LogError("EquipIdTable: GetEquipTypeId hook failed at %p, extended equipIds report type 0", typeTarget);
    }

    if (const uintptr_t subAddr = GetEquipSubIdAddr())
    {
        void* subTarget = ResolveGameAddress(subAddr);
        if (subTarget && !IsGetEquipSubIdBody(subTarget))
        {
            LogError("EquipIdTable: GetEquipSubId %p fold missing on %s, extended weapons build a different gun",
                subTarget, GetGameBuildName(gGameBuild));
            subTarget = nullptr;
        }
        const bool okS = subTarget && CreateAndEnableHook(
            subTarget, &hkGetEquipSubId,
            reinterpret_cast<void**>(&g_OrigGetEquipSubId));
        if (!okS)
            LogError("EquipIdTable: GetEquipSubId hook failed at %p, extended weapons build a different gun", subTarget);
    }
    else
        LogError("EquipIdTable: GetEquipSubId address missing, extended weapons build a different gun");
    return ok;
}

bool Uninstall_TppEquip_ReloadEquipIdTable_Hook()
{
    if (gAddr.EquipIdTable_GetEquipTypeId && g_OrigGetEquipTypeId)
        DisableAndRemoveHook(ResolveGameAddress(gAddr.EquipIdTable_GetEquipTypeId));
    g_OrigGetEquipTypeId = nullptr;
    if (g_OrigGetEquipSubId)
        DisableAndRemoveHook(ResolveGameAddress(GetEquipSubIdAddr()));
    g_OrigGetEquipSubId = nullptr;
    if (gAddr.EquipIdTable_ReloadEquipIdTable)
        DisableAndRemoveHook(ResolveGameAddress(gAddr.EquipIdTable_ReloadEquipIdTable));
    g_OrigReloadEquipIdTable = nullptr;

    std::vector<std::int32_t> resident;
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const auto& row : g_Rows)
            if (row.resident)
                resident.push_back(row.equipId);
        g_Rows.clear();
    }
    g_TableEpoch.fetch_add(1, std::memory_order_release);
    for (const std::int32_t id : resident)
        EraseNativeRow(id);
    return true;
}

void TppEquip_NoteAmmoRootParam(int eqpAmmoEquipId, int ammoId, int nativeAmmoId)
{
    if (eqpAmmoEquipId <= 0 || ammoId <= 0 || ammoId > 0xFFFF)
        return;
    if (!nativeAmmoId && ammoId <= 0x3FF)
        nativeAmmoId = ammoId;
    if (nativeAmmoId <= 0 || nativeAmmoId > 0x3FF
        || (ammoId > 0x3FF && (!WideMagazine_IsActive() || !TppEquip_WideAmmoRootReady())))
        return;
    EquipIdRow updated{};
    bool restamp = false;
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_AmmoRootParams[eqpAmmoEquipId] = { ammoId, nativeAmmoId };
        for (auto& r : g_Rows)
        {
            if (r.equipId != eqpAmmoEquipId || r.released)
                continue;
            if (r.subId != ammoId || r.nativeSubId != nativeAmmoId)
            {
                r.subId = ammoId;
                r.nativeSubId = nativeAmmoId;
                updated = r;
                restamp = true;
            }
            break;
        }
        auto it = g_Extended.find(eqpAmmoEquipId);
        if (it != g_Extended.end() && !it->second.released
            && (it->second.subId != ammoId || it->second.nativeSubId != nativeAmmoId))
        {
            it->second.subId = ammoId;
            it->second.nativeSubId = nativeAmmoId;
            MirrorStampRowNoLock(it->second);
            RestampAliasesOfNoLock(it->second);
        }
    }
    if (restamp)
    {
        WriteNativeRow(updated);
    }
}

int TppEquip_ReleaseEquipRow(int equipId)
{
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (auto& row : g_Rows)
            if (row.equipId == equipId)
            {
                row.resident = false;
                row.released = true;
                found = true;
                break;
            }
    }
    if (!found)
        return 0;
    g_TableEpoch.fetch_add(1, std::memory_order_release);
    return EraseNativeRow(equipId) ? 1 : 0;
}
