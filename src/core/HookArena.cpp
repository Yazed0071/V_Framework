#include "pch.h"
#include "HookArena.h"

#include <Windows.h>
#include <intrin.h>
#include <cstdint>

#include "log.h"

namespace HookArena
{
    namespace
    {
        constexpr std::size_t    kGranuleWant     = 64;
        constexpr std::size_t    kGranuleCap      = 256;
        constexpr std::size_t    kPagesPerGranule = 16;
        constexpr std::size_t    kPageSize        = 0x1000;
        constexpr std::size_t    kSlotSize        = 64;
        constexpr std::uintptr_t kReachNear       = 0x40000000ull;
        constexpr std::uintptr_t kReachFar        = 0x7E000000ull;

        enum PageKind : std::uint8_t
        {
            kPageFree  = 0,
            kPageSlots = 1,
            kPageThunk = 2,
        };

        struct Page
        {
            std::uint8_t  kind  = kPageFree;
            std::uint16_t bump  = 0;
            std::uint64_t slots = 0;
        };

        struct Granule
        {
            std::uintptr_t base = 0;
            Page           page[kPagesPerGranule];
        };

        Granule        g_Granules[kGranuleCap];
        std::size_t    g_Count         = 0;
        std::size_t    g_AtAttach      = 0;
        std::size_t    g_CommittedPages = 0;
        std::uintptr_t g_Gran          = 0x10000;
        std::uintptr_t g_ImageBase     = 0;
        std::uintptr_t g_ImageEnd      = 0;
        bool           g_Reserved      = false;
        bool           g_SaidExhausted = false;
        SRWLOCK        g_Lock          = SRWLOCK_INIT;

        std::uintptr_t ImageEnd(std::uintptr_t base)
        {
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE)
                return 0;
            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
                base + static_cast<std::uintptr_t>(dos->e_lfanew));
            if (nt->Signature != IMAGE_NT_SIGNATURE)
                return 0;
            return base + nt->OptionalHeader.SizeOfImage;
        }

        std::uintptr_t AlignDown(std::uintptr_t a)
        {
            return a & ~(g_Gran - 1);
        }

        std::uintptr_t AlignUp(std::uintptr_t a)
        {
            return (a + g_Gran - 1) & ~(g_Gran - 1);
        }

        bool Within(const Granule& g, std::uintptr_t addr, std::uintptr_t maxDistance)
        {
            const std::uintptr_t lo  = g.base;
            const std::uintptr_t hi  = g.base + g_Gran;
            const std::uintptr_t dLo = lo > addr ? lo - addr : addr - lo;
            const std::uintptr_t dHi = hi > addr ? hi - addr : addr - hi;
            return (dLo > dHi ? dLo : dHi) <= maxDistance;
        }

        bool ReserveAt(std::uintptr_t addr)
        {
            if (g_Count >= kGranuleCap)
                return false;
            void* p = VirtualAlloc(reinterpret_cast<LPVOID>(addr), g_Gran,
                                   MEM_RESERVE, PAGE_NOACCESS);
            if (!p)
                return false;
            Granule& g = g_Granules[g_Count++];
            g = Granule{};
            g.base = reinterpret_cast<std::uintptr_t>(p);
            return true;
        }

        bool ReserveOneUpward(std::uintptr_t& cursor, std::uintptr_t hi)
        {
            std::uintptr_t a = AlignUp(cursor);
            while (a + g_Gran <= hi)
            {
                MEMORY_BASIC_INFORMATION mbi{};
                if (VirtualQuery(reinterpret_cast<LPCVOID>(a), &mbi, sizeof(mbi)) != sizeof(mbi))
                    return false;
                const auto rb = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
                const auto re = rb + mbi.RegionSize;
                if (mbi.State == MEM_FREE && a + g_Gran <= re)
                {
                    if (ReserveAt(a))
                    {
                        cursor = a + g_Gran;
                        return true;
                    }
                    a += g_Gran;
                    continue;
                }
                if (re <= a)
                    return false;
                a = AlignUp(re);
            }
            return false;
        }

        bool ReserveOneDownward(std::uintptr_t& cursor, std::uintptr_t lo)
        {
            std::uintptr_t a = AlignDown(cursor);
            while (a >= lo + g_Gran)
            {
                const std::uintptr_t c = a - g_Gran;
                MEMORY_BASIC_INFORMATION mbi{};
                if (VirtualQuery(reinterpret_cast<LPCVOID>(c), &mbi, sizeof(mbi)) != sizeof(mbi))
                    return false;
                const auto rb = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
                const auto re = rb + mbi.RegionSize;
                if (mbi.State == MEM_FREE && re >= a)
                {
                    if (ReserveAt(c))
                    {
                        cursor = c;
                        return true;
                    }
                    a = c;
                    continue;
                }
                if (rb >= a)
                    return false;
                a = AlignDown(rb);
            }
            return false;
        }

        bool Grow(std::uintptr_t origin)
        {
            const std::uintptr_t reaches[2] = { kReachNear, kReachFar };
            for (std::uintptr_t reach : reaches)
            {
                std::uintptr_t up = origin;
                if (ReserveOneUpward(up, origin + reach - g_Gran))
                    return true;
                std::uintptr_t down = origin;
                const std::uintptr_t lo = origin > reach ? origin - reach + g_Gran : g_Gran;
                if (ReserveOneDownward(down, lo))
                    return true;
            }
            return false;
        }

        bool CommitPages(const Granule& g, std::size_t first, std::size_t n)
        {
            void* p = VirtualAlloc(reinterpret_cast<LPVOID>(g.base + first * kPageSize),
                                   n * kPageSize, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
            if (p)
                g_CommittedPages += n;
            return p != nullptr;
        }

        void* TakeSlot(Granule& g, std::size_t p)
        {
            Page& pg = g.page[p];
            unsigned long bit = 0;
            _BitScanForward64(&bit, ~pg.slots);
            pg.slots |= (1ull << bit);
            return reinterpret_cast<void*>(g.base + p * kPageSize + bit * kSlotSize);
        }

        void* SlotLocked(std::uintptr_t a, std::size_t maxDistance)
        {
            for (std::size_t i = 0; i < g_Count; ++i)
            {
                Granule& g = g_Granules[i];
                if (!Within(g, a, maxDistance))
                    continue;
                for (std::size_t p = 0; p < kPagesPerGranule; ++p)
                    if (g.page[p].kind == kPageSlots && g.page[p].slots != ~0ull)
                        return TakeSlot(g, p);
            }
            for (std::size_t i = 0; i < g_Count; ++i)
            {
                Granule& g = g_Granules[i];
                if (!Within(g, a, maxDistance))
                    continue;
                for (std::size_t p = 0; p < kPagesPerGranule; ++p)
                {
                    if (g.page[p].kind != kPageFree)
                        continue;
                    if (!CommitPages(g, p, 1))
                        continue;
                    g.page[p].kind  = kPageSlots;
                    g.page[p].slots = 0;
                    return TakeSlot(g, p);
                }
            }
            return nullptr;
        }

        void* NearLocked(std::uintptr_t a, std::size_t bytes, std::size_t pagesNeeded)
        {
            if (pagesNeeded == 1)
            {
                for (std::size_t i = 0; i < g_Count; ++i)
                {
                    Granule& g = g_Granules[i];
                    if (!Within(g, a, kReachFar))
                        continue;
                    for (std::size_t p = 1; p < kPagesPerGranule; ++p)
                    {
                        Page& pg = g.page[p];
                        if (pg.kind != kPageThunk || kPageSize - pg.bump < bytes)
                            continue;
                        void* r = reinterpret_cast<void*>(g.base + p * kPageSize + pg.bump);
                        pg.bump = static_cast<std::uint16_t>(pg.bump + bytes);
                        return r;
                    }
                }
            }
            for (std::size_t i = 0; i < g_Count; ++i)
            {
                Granule& g = g_Granules[i];
                if (!Within(g, a, kReachFar))
                    continue;
                for (std::size_t p = 1; p + pagesNeeded <= kPagesPerGranule; ++p)
                {
                    bool run = true;
                    for (std::size_t k = 0; k < pagesNeeded && run; ++k)
                        run = g.page[p + k].kind == kPageFree;
                    if (!run)
                        continue;
                    if (!CommitPages(g, p, pagesNeeded))
                        continue;
                    for (std::size_t k = 0; k < pagesNeeded; ++k)
                    {
                        g.page[p + k].kind = kPageThunk;
                        g.page[p + k].bump = static_cast<std::uint16_t>(kPageSize);
                    }
                    if (pagesNeeded == 1)
                        g.page[p].bump = static_cast<std::uint16_t>(bytes);
                    return reinterpret_cast<void*>(g.base + p * kPageSize);
                }
            }
            return nullptr;
        }

        Granule* FindGranule(std::uintptr_t a)
        {
            for (std::size_t i = 0; i < g_Count; ++i)
                if (a >= g_Granules[i].base && a < g_Granules[i].base + g_Gran)
                    return &g_Granules[i];
            return nullptr;
        }
    }

    void ReserveEarly()
    {
        if (g_Reserved)
            return;
        g_Reserved = true;

        SYSTEM_INFO si{};
        GetSystemInfo(&si);
        if (si.dwAllocationGranularity)
            g_Gran = si.dwAllocationGranularity;

        g_ImageBase = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        if (!g_ImageBase)
            return;
        g_ImageEnd = ImageEnd(g_ImageBase);
        if (g_ImageEnd == 0)
            return;

        const std::uintptr_t hiNear = g_ImageBase + kReachNear;
        const std::uintptr_t loNear = g_ImageEnd > kReachNear ? g_ImageEnd - kReachNear : g_Gran;
        const std::uintptr_t hiFar  = g_ImageBase + kReachFar;
        const std::uintptr_t loFar  = g_ImageEnd > kReachFar ? g_ImageEnd - kReachFar : g_Gran;

        struct Pass
        {
            std::uintptr_t up, hi, down, lo;
        };
        Pass passes[2] = {
            { g_ImageEnd, hiNear, g_ImageBase, loNear },
            { hiNear,     hiFar,  loNear,      loFar  },
        };
        for (Pass& ps : passes)
        {
            bool upDone = false, downDone = false;
            while (g_Count < kGranuleWant && !(upDone && downDone))
            {
                if (!upDone)
                {
                    if (!ReserveOneUpward(ps.up, ps.hi))
                        upDone = true;
                }
                if (g_Count >= kGranuleWant)
                    break;
                if (!downDone)
                {
                    if (!ReserveOneDownward(ps.down, ps.lo))
                        downDone = true;
                }
            }
        }
        g_AtAttach = g_Count;
    }

    void LogSummary()
    {
        AcquireSRWLockExclusive(&g_Lock);
        const std::size_t atAttach = g_AtAttach;
        ReleaseSRWLockExclusive(&g_Lock);

        if (atAttach == 0)
            LogWarn("HookArena: no free trampoline space within 2 GB of the exe at attach, hooks may fail");
    }

    void NoteExhausted()
    {
        AcquireSRWLockExclusive(&g_Lock);
        const bool first = !g_SaidExhausted;
        g_SaidExhausted = true;
        const std::size_t count = g_Count;
        ReleaseSRWLockExclusive(&g_Lock);
        if (first)
            LogError("HookArena: trampoline arena exhausted (%zu granules), further hooks refused", count);
    }

    void* AllocateSlot(const void* origin, std::size_t maxDistance)
    {
        const auto a = reinterpret_cast<std::uintptr_t>(origin);
        AcquireSRWLockExclusive(&g_Lock);
        void* r = SlotLocked(a, maxDistance);
        if (!r && Grow(a))
            r = SlotLocked(a, maxDistance);
        ReleaseSRWLockExclusive(&g_Lock);
        return r;
    }

    void FreeSlot(void* slot)
    {
        const auto a = reinterpret_cast<std::uintptr_t>(slot);
        AcquireSRWLockExclusive(&g_Lock);
        if (Granule* g = FindGranule(a))
        {
            const std::size_t off = a - g->base;
            Page& pg = g->page[off / kPageSize];
            if (pg.kind == kPageSlots)
                pg.slots &= ~(1ull << ((off % kPageSize) / kSlotSize));
        }
        ReleaseSRWLockExclusive(&g_Lock);
    }

    void* AllocateNear(std::uintptr_t nearAddr, std::size_t bytes)
    {
        if (bytes == 0)
            return nullptr;
        bytes = (bytes + 15) & ~static_cast<std::size_t>(15);
        const std::size_t pagesNeeded = (bytes + kPageSize - 1) / kPageSize;
        if (pagesNeeded > kPagesPerGranule - 1)
            return nullptr;
        AcquireSRWLockExclusive(&g_Lock);
        void* r = NearLocked(nearAddr, bytes, pagesNeeded);
        if (!r && Grow(nearAddr))
            r = NearLocked(nearAddr, bytes, pagesNeeded);
        ReleaseSRWLockExclusive(&g_Lock);
        return r;
    }

    std::size_t Remaining()
    {
        AcquireSRWLockExclusive(&g_Lock);
        const std::size_t total = g_Count * kPagesPerGranule;
        const std::size_t r = total > g_CommittedPages ? total - g_CommittedPages : 0;
        ReleaseSRWLockExclusive(&g_Lock);
        return r;
    }
}
