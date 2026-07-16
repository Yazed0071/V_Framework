#include "pch.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace equip
{
    extern bool g_DevelopMenuSeen;
}


namespace
{

    constexpr bool kEnableGridExpand = true;

    constexpr int kCols = 15;          // 0xf grade column
    constexpr int kRows = 512;         // was 0x72 = 114
    constexpr int kGridLen = kRows * kCols;   // 7680, was 0x6ae = 1710

    std::uint16_t g_Grid[kGridLen];


    using CountFn      = std::uint16_t (__fastcall*)(void*, std::uint32_t);
    using FillFn       = void          (__fastcall*)(void*, std::uint32_t,
                                                     std::uint32_t, std::uint16_t*);
    using SpanFn       = std::uint16_t (__fastcall*)(void*, std::uint16_t, int, int);
    using ChildCountFn = std::uint16_t (__fastcall*)(void*, std::uint16_t, int);
    using ChildFillFn  = void          (__fastcall*)(void*, std::uint16_t,
                                                     std::uint16_t, std::uint16_t*, int);
    using GradeFn      = std::uint8_t  (__fastcall*)(void*, std::uint16_t);
    using ChildBaseFn  = std::uint8_t  (__fastcall*)(void*, std::uint16_t, int);
    using FlagFn       = char          (__fastcall*)(void*, std::uint16_t);

    using FillGrid_t   = void (__fastcall*)(std::uintptr_t self, char selectInit);
    using CopyGrid_t   = void (__fastcall*)(std::uintptr_t self);
    using CountBadge_t = char (__fastcall*)(std::uintptr_t self, std::uint32_t typeId,
                                            int* developable, int* newDevelopable,
                                            int* developed);
    using GetQst_t     = std::uint8_t* (__fastcall*)();

    FillGrid_t   g_OrigFillGrid   = nullptr;
    CopyGrid_t   g_OrigCopyGrid   = nullptr;
    CountBadge_t g_OrigCountBadge = nullptr;
    FillGrid_t   g_OrigFillFlat   = nullptr;
    CopyGrid_t   g_OrigCopyFlat   = nullptr;
    GetQst_t     g_GetQst         = nullptr;

    std::uintptr_t g_FlatTabTableA = 0;
    std::uintptr_t g_FlatTabTableB = 0;

    template <typename T>
    T& At(std::uintptr_t base, std::size_t off)
    {
        return *reinterpret_cast<T*>(base + off);
    }

    void* VtMethod(void* obj, std::size_t off)
    {
        void** vt = *reinterpret_cast<void***>(obj);
        return vt[off / 8];
    }

    void __fastcall hkFillGrid(std::uintptr_t self, char selectInit)
    {
        equip::g_DevelopMenuSeen = true;
        for (int i = 0; i < kGridLen; ++i)
            g_Grid[i] = 0x400;

        void* prov = At<void*>(self, 0x38);
        void** vt  = *reinterpret_cast<void***>(prov);
        const std::uint32_t typeId = At<std::uint32_t>(self, 0x2144);

        const std::uint16_t rootCount =
            reinterpret_cast<CountFn>(vt[0x140 / 8])(prov, typeId);
        if (rootCount > kRows)
            return;

        static std::uint16_t rootBuf[kRows];
        reinterpret_cast<FillFn>(vt[0x148 / 8])(prov, typeId, rootCount, rootBuf);

        std::uint32_t rowAcc = 0;
        std::uint32_t local190 = 0;
        int validRoots = 0, garbageRoots = 0;

        if (rootCount == 0)
        {
            rowAcc = 3;
        }
        else
        {
            for (int r = 0; r < static_cast<int>(rootCount); ++r)
            {
                const std::uint16_t rootVal = rootBuf[r];
                if (rootVal >= 0x400)
                {
                    ++garbageRoots;
                    continue;
                }
                ++validRoots;
                const std::uint8_t col =
                    reinterpret_cast<GradeFn>(vt[0x180 / 8])(prov, rootVal);
                if (col != 0)
                {
                    const std::uint32_t rowBase = rowAcc & 0xffff;
                    const int idx = (col - 1) + static_cast<int>(rowBase) * kCols;
                    if (idx < kGridLen && rootVal < 0x400)
                    {
                        g_Grid[idx] = rootVal;

                        if (selectInit != 0 && r == 0)
                        {
                            const int c = col - 1;
                            if (c < 2)
                            {
                                At<int>(self, 0x214c) = c;
                                At<int>(self, 0x2154) = c;
                                At<std::uint32_t>(self, 0x215c) = 0;
                                At<std::uint32_t>(self, 0x2164) = 0;
                            }
                            else
                            {
                                At<std::uint32_t>(self, 0x214c) = 1;
                                At<std::uint32_t>(self, 0x2154) = 1;
                                At<std::uint32_t>(self, 0x215c) = col - 2;
                                At<std::uint32_t>(self, 0x2164) = col - 2;
                            }
                            int scr = 0;
                            if ((std::uint16_t)rowBase < 2)
                            {
                                At<std::uint32_t>(self, 0x2150) = rowBase;
                                At<std::uint32_t>(self, 0x2158) = rowBase;
                            }
                            else
                            {
                                At<std::uint32_t>(self, 0x2150) = 1;
                                At<std::uint32_t>(self, 0x2158) = 1;
                                scr = static_cast<int>(rowBase) - 1;
                            }
                            At<int>(self, 0x2168) = scr;
                            At<int>(self, 0x2160) = scr;
                        }

                        std::uint32_t maxSpan =
                            reinterpret_cast<SpanFn>(vt[0x150 / 8])(prov, rootVal, 0, 0);
                        const std::uint16_t childCount =
                            reinterpret_cast<ChildCountFn>(vt[0x160 / 8])(prov, rootVal, 0);

                        if (childCount < 0x2e)
                        {
                            static std::uint16_t childBuf[0x2e];
                            reinterpret_cast<ChildFillFn>(vt[0x168 / 8])(
                                prov, rootVal, childCount, childBuf, 0);

                            std::uint32_t lastSpan = maxSpan;
                            for (int k = 0; k < static_cast<int>(childCount); ++k)
                            {
                                const std::uint16_t childVal = childBuf[k];
                                if (childVal >= 0x400)
                                    continue;
                                const std::uint8_t cbase =
                                    reinterpret_cast<ChildBaseFn>(vt[0x188 / 8])(
                                        prov, childVal, 0);
                                const std::uint8_t ccol =
                                    reinterpret_cast<GradeFn>(vt[0x180 / 8])(
                                        prov, childVal);
                                const int cidx = (static_cast<int>(cbase)
                                                  + static_cast<int>(rowBase)) * kCols
                                                 - 1 + static_cast<int>(ccol);
                                if (cidx < kGridLen && childVal < 0x400)
                                {
                                    g_Grid[cidx] = childVal;
                                    std::uint16_t cspan =
                                        reinterpret_cast<SpanFn>(vt[0x150 / 8])(
                                            prov, childVal, 0, 0);
                                    std::uint32_t w = cspan ? cspan : 1u;
                                    if ((lastSpan & 0xffff) < w + cbase)
                                        lastSpan = cbase + w;
                                }
                                maxSpan = lastSpan;
                            }

                            std::uint32_t add = (maxSpan != 0) ? maxSpan : 1u;
                            rowAcc = static_cast<std::uint16_t>(rowAcc) + add;
                            local190 = rowAcc;
                        }
                    }
                }
            }
            if (static_cast<std::uint16_t>(rowAcc) < 3)
                rowAcc = 3;
        }
        (void)local190;

        static int s_lastLog = -1;
        const int logKey = validRoots * 10000 + garbageRoots;
        if (logKey != s_lastLog)
        {
            s_lastLog = logKey;
            Log("[MenuDevelopGrid] fill tab=%u: rootCount=%u valid=%d "
                "garbage-skipped=%d rows=%u\n",
                typeId, rootCount, validRoots, garbageRoots, rowAcc);
        }

        At<std::int16_t>(self, 0x216c) = static_cast<std::int16_t>(rowAcc);
        std::memcpy(reinterpret_cast<void*>(self + 0xe50), g_Grid, 0x6ae * 2);
    }

    char __fastcall hkCountBadge(std::uintptr_t self, std::uint32_t typeId,
                                 int* developable, int* newDevelopable,
                                 int* developed)
    {
        *developable = 0;
        *newDevelopable = 0;

        void* prov = At<void*>(self, 0x38);
        void** vt  = *reinterpret_cast<void***>(prov);

        std::uint16_t rootCount =
            reinterpret_cast<CountFn>(vt[0x140 / 8])(prov, typeId);
        static std::uint16_t rootBuf[0x400];
        if (rootCount > 0x400)
            rootCount = 0x400;
        reinterpret_cast<FillFn>(vt[0x148 / 8])(prov, typeId, rootCount, rootBuf);

        const auto countOne = [&](std::uint16_t idx)
        {
            if (idx >= 0x400)
                return;
            const char c1 = reinterpret_cast<FlagFn>(vt[0x1c8 / 8])(prov, idx);
            const char c2 = reinterpret_cast<FlagFn>(vt[0x1b8 / 8])(prov, idx);
            const char c3 = reinterpret_cast<FlagFn>(vt[0x250 / 8])(prov, idx);
            if (c3 != 0)
            {
                ++*developable;
                if (c1 == 0 && c2 != 0)
                {
                    ++*newDevelopable;
                    ++*developed;
                }
            }
            else if (c1 == 0 && c2 != 0)
            {
                ++*developed;
            }
        };

        for (int r = 0; r < static_cast<int>(rootCount); ++r)
        {
            const std::uint16_t rootVal = rootBuf[r];
            if (rootVal >= 0x400)
                continue;
            countOne(rootVal);

            std::uint16_t childCount =
                reinterpret_cast<ChildCountFn>(vt[0x160 / 8])(prov, rootVal, 0);
            static std::uint16_t childBuf[0x40];
            if (childCount > 0x40)
                childCount = 0x40;
            if (childCount != 0)
            {
                reinterpret_cast<ChildFillFn>(vt[0x168 / 8])(
                    prov, rootVal, childCount, childBuf, 0);
                for (int k = 0; k < static_cast<int>(childCount); ++k)
                    countOne(childBuf[k]);
            }
        }

        return *developable != 0;
    }

    inline std::uint16_t GridAt(int shortIndex)
    {
        if (shortIndex < 0 || shortIndex >= kGridLen)
            return 0x400;
        const std::uint16_t v = g_Grid[shortIndex];
        return (v < 0x400) ? v : std::uint16_t{0x400};
    }

    void MirrorLegacyGrid(std::uintptr_t self)
    {
        std::memcpy(reinterpret_cast<void*>(self + 0xe50), g_Grid, 0x6ae * 2);
    }

    void __fastcall hkFillFlat(std::uintptr_t self, char selectInit)
    {
        equip::g_DevelopMenuSeen = true;
        for (int i = 0; i < kGridLen; ++i)
            g_Grid[i] = 0x400;

        void* prov = At<void*>(self, 0x38);
        void** vt  = *reinterpret_cast<void***>(prov);

        long long cell = 0;
        int included = 0;

        std::uint16_t* svars = nullptr;
        if (g_GetQst)
        {
            std::uint8_t* qst = g_GetQst();
            if (qst)
            {
                auto p1 = *reinterpret_cast<std::uint8_t**>(qst + 0x98);
                if (p1)
                {
                    auto p2 = *reinterpret_cast<std::uint8_t**>(p1 + 0x10);
                    if (p2)
                        svars = reinterpret_cast<std::uint16_t*>(p2 + 0xf6e2);
                }
            }
        }

        const auto includes = [&](std::uint16_t idx) -> bool
        {
            if (idx >= 0x400)
                return false;
            const char c1 = reinterpret_cast<FlagFn>(vt[0x1c8 / 8])(prov, idx);
            const char c3 = reinterpret_cast<FlagFn>(vt[0x190 / 8])(prov, idx);
            if (c1 != 0 || c3 != 0)
                return false;
            const char c2 = reinterpret_cast<FlagFn>(vt[0x1b8 / 8])(prov, idx);
            if (c2 != 0)
                return true;
            const char inDev = reinterpret_cast<FlagFn>(vt[0x470 / 8])(prov, idx);
            if (inDev == 0)
            {
                if (svars)
                    for (int s = 0; s < 10; ++s)
                        if (svars[s] != 0
                            && static_cast<std::uint32_t>(idx)
                               == static_cast<std::uint32_t>(svars[s]) - 1)
                            return true;
                return false;
            }
            return reinterpret_cast<FlagFn>(vt[0x200 / 8])(prov, idx) != 0;
        };

        const int sectionCount = At<std::uint8_t>(self, 0x2190);
        const std::uint8_t* sections =
            reinterpret_cast<const std::uint8_t*>(self + 0x2192);

        for (int sec = 1; sec < sectionCount; ++sec)
        {
            const std::uint8_t tabIdx = sections[sec - 1];
            const std::uintptr_t table = (At<std::int16_t>(self, 0x2140) == 0)
                ? g_FlatTabTableA : g_FlatTabTableB;
            const std::uint32_t typeId = *reinterpret_cast<std::uint32_t*>(
                table + static_cast<std::size_t>(tabIdx) * 4);

            std::uint16_t count =
                reinterpret_cast<CountFn>(vt[0x140 / 8])(prov, typeId);
            static std::uint16_t rootBuf[0x400];
            if (count > 0x400)
                count = 0x400;
            reinterpret_cast<FillFn>(vt[0x148 / 8])(prov, typeId, count, rootBuf);

            for (int r = 0; r < static_cast<int>(count); ++r)
            {
                if (cell > kGridLen - 1)
                    break;
                const std::uint16_t idx = rootBuf[r];
                if (idx >= 0x400)
                    continue;
                if (includes(idx))
                {
                    ++included;
                    g_Grid[cell++] = idx;
                }

                const std::uint16_t cc =
                    reinterpret_cast<ChildCountFn>(vt[0x160 / 8])(prov, idx, 0);
                if (cc < 0x2e)
                {
                    static std::uint16_t childBuf[0x2e];
                    reinterpret_cast<ChildFillFn>(vt[0x168 / 8])(
                        prov, idx, cc, childBuf, 0);
                    for (int k = 0; k < static_cast<int>(cc); ++k)
                    {
                        if (cell > kGridLen - 1)
                            break;
                        const std::uint16_t cidx = childBuf[k];
                        if (cidx >= 0x400)
                            continue;
                        if (includes(cidx))
                        {
                            ++included;
                            g_Grid[cell++] = cidx;
                        }
                    }
                }
            }
        }

        int rows = included / 3 + ((included % 3) ? 1 : 0);
        if (rows < 3)
            rows = 3;
        At<std::uint16_t>(self, 0x216c) = static_cast<std::uint16_t>(rows);

        if (selectInit != 0)
        {
            At<std::uint64_t>(self, 0x214c) = 0;
            At<std::uint64_t>(self, 0x2154) = 0;
            At<std::uint64_t>(self, 0x215c) = 0;
            At<std::uint64_t>(self, 0x2164) = 0;
        }

        MirrorLegacyGrid(self);
    }

    void __fastcall hkCopyFlat(std::uintptr_t self)
    {
        std::uint16_t* pv = reinterpret_cast<std::uint16_t*>(self + 0x1bac);
        for (int i = 0; i < 0x10; ++i)
            pv[i] = 0x400;

        const int topRow = At<int>(self, 0x2168);
        const int topCol = At<int>(self, 0x2164);
        constexpr long long kFlatRows = kGridLen / 3;

        {
            long long base = static_cast<long long>(topRow) * 3;
            std::uint16_t* out = reinterpret_cast<std::uint16_t*>(self + 0x1bb8);
            for (int row = 0; row < 3; ++row)
            {
                const long long g = topCol + base;
                base += 3;
                out[-1] = GridAt(static_cast<int>(g));
                out[0]  = GridAt(static_cast<int>(g) + 1);
                out[1]  = GridAt(static_cast<int>(g) + 2);
                out += 4;
            }
        }

        {
            long long uRow = static_cast<long long>(topRow) - 1;
            long long rowBase = uRow * 3;
            for (int i = 0; i < 4; ++i)
            {
                if (uRow >= 0 && uRow < kFlatRows)
                {
                    const int c0 = topCol - 1;
                    if (i == 0)
                    {
                        for (int j = 0; j < 4; ++j)
                        {
                            if (static_cast<unsigned>(c0 + j) < 3u)
                                At<std::uint16_t>(self, 0x1bac + static_cast<std::size_t>(j) * 2) =
                                    GridAt(static_cast<int>(c0 + j + rowBase));
                        }
                    }
                    else if (static_cast<unsigned>(c0) < 3u)
                    {
                        At<std::uint16_t>(self, 0x1bac + static_cast<std::size_t>(i) * 8) =
                            GridAt(static_cast<int>(c0 + rowBase));
                    }
                }
                uRow += 1;
                rowBase += 3;
            }
        }

        {
            using GetFlag1_t = char (__fastcall*)(void*, std::uint64_t);
            using Act1_t     = void (__fastcall*)(void*, std::uint64_t);
            using Act2_t     = void (__fastcall*)(void*, std::uint64_t, int);

            std::uintptr_t puVar11 = self + 0x1c08;
            for (int band = 0; band < 3; ++band)
            {
                std::uint64_t* pv12 = reinterpret_cast<std::uint64_t*>(puVar11);

                void* rm = At<void*>(self, 0x80);
                char cv = reinterpret_cast<GetFlag1_t>(VtMethod(rm, 0x6c0))(rm, pv12[0]);
                void* rm2 = At<void*>(self, 0x80);
                if (cv == 0)
                {
                    reinterpret_cast<Act2_t>(VtMethod(rm2, 0x680))(rm2, pv12[6], 1);
                }
                else
                {
                    reinterpret_cast<Act1_t>(VtMethod(rm2, 0x6a8))(rm2, pv12[0]);
                    reinterpret_cast<Act1_t>(VtMethod(rm2, 0x6a8))(rm2, pv12[3]);
                    reinterpret_cast<Act1_t>(VtMethod(rm2, 0x678))(rm2, pv12[6]);
                }

                void* rm3 = At<void*>(self, 0x80);
                char cv2 = reinterpret_cast<GetFlag1_t>(VtMethod(rm3, 0x6c0))(rm3, pv12[9]);
                void* rm4 = At<void*>(self, 0x80);
                if (cv2 == 0)
                {
                    reinterpret_cast<Act2_t>(VtMethod(rm4, 0x680))(rm4, pv12[0xf], 1);
                }
                else
                {
                    reinterpret_cast<Act1_t>(VtMethod(rm4, 0x6a8))(rm4, pv12[9]);
                    reinterpret_cast<Act1_t>(VtMethod(rm4, 0x6a8))(rm4, pv12[0xc]);
                    reinterpret_cast<Act1_t>(VtMethod(rm4, 0x678))(rm4, pv12[0xf]);
                }

                puVar11 += 8;
            }
        }
    }

    void __fastcall hkCopyGrid(std::uintptr_t self)
    {
        std::uint16_t* pv = reinterpret_cast<std::uint16_t*>(self + 0x1bac);
        for (int i = 0; i < 0x10; ++i)
            pv[i] = 0x400;
        for (std::size_t off = 0x1bcc; off <= 0x1c00; off += 4)
            At<std::uint32_t>(self, off) = 0x4000400u;

        const int topRow = At<int>(self, 0x2168);
        const int topCol = At<int>(self, 0x2164);

        {
            long long base = static_cast<long long>(topRow) * kCols;
            std::uint16_t* out = reinterpret_cast<std::uint16_t*>(self + 0x1bb8);
            for (int row = 0; row < 3; ++row)
            {
                long long g = topCol + base;
                base += kCols;
                out[-1] = GridAt(static_cast<int>(g));
                out[0]  = GridAt(static_cast<int>(g) + 1);
                out[1]  = GridAt(static_cast<int>(g) + 2);
                out += 4;
            }
        }

        {
            long long uRow = static_cast<long long>(topRow) - 1;
            long long rowBase = uRow * kCols;
            for (int i = 0; i < 4; ++i)
            {
                if (uRow >= 0 && uRow < kRows)
                {
                    const int c0 = topCol - 1;
                    if (i == 0)
                    {
                        for (int j = 0; j < 4; ++j)
                        {
                            if (static_cast<unsigned>(c0 + j) < static_cast<unsigned>(kCols))
                                At<std::uint16_t>(self, 0x1bac + static_cast<std::size_t>(j) * 2) =
                                    GridAt(static_cast<int>(c0 + j + rowBase));
                        }
                    }
                    else if (static_cast<unsigned>(c0) < static_cast<unsigned>(kCols))
                    {
                        At<std::uint16_t>(self, 0x1bac + static_cast<std::size_t>(i) * 8) =
                            GridAt(static_cast<int>(c0 + rowBase));
                    }
                }
                uRow += 1;
                rowBase += kCols;
            }
        }

        {
            long long rowBase = static_cast<long long>(topRow) * kCols;
            for (int band = 0; band < 7; ++band)
            {
                if (rowBase > (kGridLen - 1))
                    break;
                const int c = topCol;
                if (band < 3)
                {
                    const std::size_t d = static_cast<std::size_t>(band) * 2;
                    if (c + 3 < kCols)
                        At<std::uint16_t>(self, 0x1bcc + d) = GridAt(static_cast<int>((c + 3) + rowBase));
                    if (c + 4 < kCols)
                        At<std::uint16_t>(self, 0x1bd2 + d) = GridAt(static_cast<int>((c + 4) + rowBase));
                    if (c + 5 < kCols)
                        At<std::uint16_t>(self, 0x1bd8 + d) = GridAt(static_cast<int>((c + 5) + rowBase));
                    if (c + 6 < kCols)
                        At<std::uint16_t>(self, 0x1bde + d) = GridAt(static_cast<int>((c + 6) + rowBase));
                }
                else
                {
                    std::size_t rowOff;
                    switch (band)
                    {
                    case 3: rowOff = 0x1be4; break;
                    case 4: rowOff = 0x1bec; break;
                    case 5: rowOff = 0x1bf4; break;
                    default: rowOff = 0x1bfc; break;
                    }
                    if (c < kCols)
                        At<std::uint16_t>(self, rowOff) = GridAt(static_cast<int>(c + rowBase));
                    if (c + 1 < kCols)
                        At<std::uint16_t>(self, rowOff + 2) = GridAt(static_cast<int>((c + 1) + rowBase));
                    if (c + 2 < kCols)
                        At<std::uint16_t>(self, rowOff + 4) = GridAt(static_cast<int>((c + 2) + rowBase));
                    if (c + 3 < kCols)
                        At<std::uint16_t>(self, rowOff + 6) = GridAt(static_cast<int>((c + 3) + rowBase));
                }
                rowBase += kCols;
            }
        }

        {
            std::uintptr_t puVar12 = self + 0x1c08;
            long long rowBase = static_cast<long long>(topRow) * kCols;
            for (int band = 0; band < 3; ++band)
            {
                const int c = topCol;
                bool leftEdge = false, rightEdge = false;
                for (int j = 0; j < kCols; ++j)
                {
                    if ((j < c || c + 3 <= j)
                        && GridAt(static_cast<int>(j + rowBase)) != 0x400)
                    {
                        if (j < c) leftEdge = true; else rightEdge = true;
                    }
                }

                void* rm = At<void*>(self, 0x80);
                using GetFlag1_t = char (__fastcall*)(void*, std::uint64_t);
                using Act1_t     = void (__fastcall*)(void*, std::uint64_t);
                using Act2_t     = void (__fastcall*)(void*, std::uint64_t, int);
                std::uint64_t* pv12 = reinterpret_cast<std::uint64_t*>(puVar12);

                if (leftEdge)
                {
                    char cv = reinterpret_cast<GetFlag1_t>(VtMethod(rm, 0x6c0))(rm, pv12[0]);
                    if (cv == 0)
                    {
                        reinterpret_cast<Act1_t>(VtMethod(rm, 0x688))(rm, pv12[0]);
                        reinterpret_cast<Act1_t>(VtMethod(rm, 0x6a8))(rm, pv12[6]);
                        reinterpret_cast<Act1_t>(VtMethod(rm, 0x678))(rm, pv12[3]);
                    }
                }
                else
                {
                    char cv = reinterpret_cast<GetFlag1_t>(VtMethod(rm, 0x6c0))(rm, pv12[0]);
                    void* rm2 = At<void*>(self, 0x80);
                    if (cv == 0)
                    {
                        reinterpret_cast<Act2_t>(VtMethod(rm2, 0x680))(rm2, pv12[6], 1);
                    }
                    else
                    {
                        reinterpret_cast<Act1_t>(VtMethod(rm2, 0x6a8))(rm2, pv12[0]);
                        reinterpret_cast<Act1_t>(VtMethod(rm, 0x6a8))(rm, pv12[3]);
                        reinterpret_cast<Act1_t>(VtMethod(rm, 0x678))(rm, pv12[6]);
                    }
                }

                void* rm3 = At<void*>(self, 0x80);
                char cv2 = reinterpret_cast<GetFlag1_t>(VtMethod(rm3, 0x6c0))(rm3, pv12[9]);
                if (rightEdge)
                {
                    if (cv2 == 0)
                    {
                        reinterpret_cast<Act1_t>(VtMethod(rm3, 0x688))(rm3, pv12[9]);
                        reinterpret_cast<Act1_t>(VtMethod(rm3, 0x6a8))(rm3, pv12[0xf]);
                        reinterpret_cast<Act1_t>(VtMethod(rm3, 0x678))(rm3, pv12[0xc]);
                    }
                }
                else
                {
                    void* rm4 = At<void*>(self, 0x80);
                    if (cv2 == 0)
                    {
                        reinterpret_cast<Act2_t>(VtMethod(rm4, 0x680))(rm4, pv12[0xf], 1);
                    }
                    else
                    {
                        reinterpret_cast<Act1_t>(VtMethod(rm4, 0x6a8))(rm4, pv12[9]);
                        reinterpret_cast<Act1_t>(VtMethod(rm3, 0x6a8))(rm3, pv12[0xc]);
                        reinterpret_cast<Act1_t>(VtMethod(rm3, 0x678))(rm3, pv12[0xf]);
                    }
                }

                puVar12 += 8;
                rowBase += kCols;
            }
        }
    }
}

namespace equip
{
    bool  g_MenuGridExpanded = false;
    bool  g_DevelopMenuSeen  = false;
    int   MenuGridRowCap() { return g_MenuGridExpanded ? kRows : 114; }

    bool Install_MenuDevelopGridExpand()
    {
        if (!kEnableGridExpand)
        {
            Log("[MenuDevelopGrid] grid expansion disabled in this build - "
                "develop menu runs the native 114-row grid with window paging.\n");
            return true;
        }

        void* fill     = ResolveGameAddress(gAddr.MenuDevelopGrid_FillGrid);
        void* copy     = ResolveGameAddress(gAddr.MenuDevelopGrid_CopyGrid);
        void* badge    = ResolveGameAddress(gAddr.MenuDevelopGrid_CountBadge);
        void* fillFlat = ResolveGameAddress(gAddr.MenuDevelopGrid_FillFlat);
        void* copyFlat = ResolveGameAddress(gAddr.MenuDevelopGrid_CopyFlat);
        if (!gAddr.MenuDevelopGrid_FillGrid || !gAddr.MenuDevelopGrid_CopyGrid
            || !gAddr.MenuDevelopGrid_CountBadge || !gAddr.MenuDevelopGrid_FillFlat
            || !gAddr.MenuDevelopGrid_CopyFlat
            || !fill || !copy || !badge || !fillFlat || !copyFlat)
        {
            Log("[MenuDevelopGrid] not installed (addresses unresolved on this "
                "build) - develop grid stays at the native 114-row cap.\n");
            return true;
        }

        g_GetQst = reinterpret_cast<GetQst_t>(
            ResolveGameAddress(gAddr.GetQuarkSystemTable));

        const std::uintptr_t slide =
            reinterpret_cast<std::uintptr_t>(fillFlat) - 0x141679220ull;
        g_FlatTabTableA = slide + 0x142438780ull;
        g_FlatTabTableB = slide + 0x1424387c0ull;

        const bool okFill = CreateAndEnableHook(
            fill, reinterpret_cast<void*>(&hkFillGrid),
            reinterpret_cast<void**>(&g_OrigFillGrid));
        const bool okCopy = CreateAndEnableHook(
            copy, reinterpret_cast<void*>(&hkCopyGrid),
            reinterpret_cast<void**>(&g_OrigCopyGrid));
        const bool okBadge = CreateAndEnableHook(
            badge, reinterpret_cast<void*>(&hkCountBadge),
            reinterpret_cast<void**>(&g_OrigCountBadge));
        const bool okFillFlat = CreateAndEnableHook(
            fillFlat, reinterpret_cast<void*>(&hkFillFlat),
            reinterpret_cast<void**>(&g_OrigFillFlat));
        const bool okCopyFlat = CreateAndEnableHook(
            copyFlat, reinterpret_cast<void*>(&hkCopyFlat),
            reinterpret_cast<void**>(&g_OrigCopyFlat));

        g_MenuGridExpanded = okFill && okCopy && okBadge && okFillFlat
                             && okCopyFlat;
        Log("[MenuDevelopGrid] grid expand install: fill=%s copy=%s badge=%s "
            "fillFlat=%s copyFlat=%s (grid grown 114 -> %d rows; render-row "
            "cap now %d)\n",
            okFill ? "OK" : "FAIL", okCopy ? "OK" : "FAIL",
            okBadge ? "OK" : "FAIL", okFillFlat ? "OK" : "FAIL",
            okCopyFlat ? "OK" : "FAIL", kRows, MenuGridRowCap());
        return g_MenuGridExpanded;
    }
}
