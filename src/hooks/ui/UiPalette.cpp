#include "pch.h"
#include "UiPalette.h"

#include <Windows.h>
#include <algorithm>
#include <cstdint>
#include <mutex>
#include <unordered_map>

#include "log.h"

namespace
{
    struct PaletteEntry
    {
        std::uint32_t hash;
        std::uint32_t pad;
        float         r;
        float         g;
        float         b;
        float         a;
    };
    static_assert(sizeof(PaletteEntry) == 24, "PaletteEntry must be 24 bytes");

    struct OriginalColor
    {
        float r;
        float g;
        float b;
        float a;
    };

    static std::mutex                                       g_Mutex;
    static PaletteEntry*                                    g_Entries     = nullptr;
    static std::size_t                                      g_EntryCount  = 0;
    static std::uint32_t                                    g_AnchorKey   = 0;
    static std::unordered_map<std::uint32_t, OriginalColor> g_Originals;

    static constexpr std::size_t kMinEntries        = 4;
    static constexpr std::size_t kMaxEntries        = 256;
    static constexpr LONGLONG    kScanDeadlineMs    = 4000;
    static constexpr int         kMaxDiagHits       = 6;

    static bool IsFloatPlausible(float v)
    {
        return v >= -0.01f && v <= 4.0f;
    }

    static bool LooksLikePaletteEntry(const PaletteEntry& e)
    {
        if (e.hash == 0) return false;
        if (!IsFloatPlausible(e.r)) return false;
        if (!IsFloatPlausible(e.g)) return false;
        if (!IsFloatPlausible(e.b)) return false;
        if (e.a < 0.0f || e.a > 1.0001f) return false;
        return true;
    }

    static int SafeProbeEntry(const std::uint8_t* p, PaletteEntry* out)
    {
        __try
        {
            *out = *reinterpret_cast<const PaletteEntry*>(p);
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static int SafeWalkArray(std::uint8_t* anchorBytes,
                             std::uint8_t* regionBase,
                             std::size_t   regionSize,
                             PaletteEntry** outArr,
                             std::size_t*  outCount)
    {
        *outArr   = nullptr;
        *outCount = 0;

        std::uint8_t* regionEnd = regionBase + regionSize;
        std::uint8_t* start     = anchorBytes;

        while (start - sizeof(PaletteEntry) >= regionBase)
        {
            PaletteEntry e{};
            std::uint8_t* prev = start - sizeof(PaletteEntry);
            if (!SafeProbeEntry(prev, &e)) break;
            if (!LooksLikePaletteEntry(e)) break;
            start = prev;
        }

        std::uint8_t* end = anchorBytes + sizeof(PaletteEntry);
        while (end + sizeof(PaletteEntry) <= regionEnd)
        {
            PaletteEntry e{};
            if (!SafeProbeEntry(end, &e)) break;
            if (!LooksLikePaletteEntry(e)) break;
            end += sizeof(PaletteEntry);
        }

        const std::size_t spanBytes = static_cast<std::size_t>(end - start);
        const std::size_t count     = spanBytes / sizeof(PaletteEntry);
        if (count < kMinEntries || count > kMaxEntries) return 0;

        *outArr   = reinterpret_cast<PaletteEntry*>(start);
        *outCount = count;
        return 1;
    }

    struct DiagHit
    {
        std::uint8_t* addr;
        std::uint32_t bytes[6];
        bool          validatorPassed;
    };

    static int SafeScanRegion(std::uint32_t  key,
                              std::uint8_t*  base,
                              std::size_t    size,
                              PaletteEntry** outArr,
                              std::size_t*   outCount,
                              DiagHit*       diagHits,
                              int*           diagHitsFound)
    {
        __try
        {
            std::uint8_t* end     = base + size;
            std::uint8_t* scanEnd = end - sizeof(PaletteEntry);
            for (std::uint8_t* p = base; p <= scanEnd; p += sizeof(std::uint32_t))
            {
                if (*reinterpret_cast<std::uint32_t*>(p) != key) continue;

                PaletteEntry probe{};
                const int probed = SafeProbeEntry(p, &probe);
                const bool valid = probed && LooksLikePaletteEntry(probe);

                if (diagHits && diagHitsFound && *diagHitsFound < kMaxDiagHits)
                {
                    DiagHit& h = diagHits[*diagHitsFound];
                    h.addr            = p;
                    h.validatorPassed = valid;
                    for (int i = 0; i < 6; ++i) h.bytes[i] = 0;
                    if (probed)
                    {
                        for (int i = 0; i < 6; ++i)
                            h.bytes[i] = reinterpret_cast<std::uint32_t*>(p)[i];
                    }
                    ++(*diagHitsFound);
                }

                if (!valid) continue;

                PaletteEntry* arr = nullptr;
                std::size_t   count = 0;
                if (SafeWalkArray(p, base, size, &arr, &count))
                {
                    *outArr   = arr;
                    *outCount = count;
                    return 1;
                }
            }
            return 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static bool IsAcceptableProtection(DWORD protect)
    {
        if (protect & PAGE_GUARD)    return false;
        if (protect & PAGE_NOACCESS) return false;
        const DWORD base = protect & 0xFF;
        return base == PAGE_READWRITE      ||
               base == PAGE_WRITECOPY      ||
               base == PAGE_EXECUTE_READWRITE ||
               base == PAGE_EXECUTE_WRITECOPY;
    }

    static PaletteEntry* ScanForKey(std::uint32_t key, std::size_t& outCount)
    {
        outCount = 0;

        LARGE_INTEGER freq{};
        QueryPerformanceFrequency(&freq);
        LARGE_INTEGER startCounter{};
        QueryPerformanceCounter(&startCounter);
        const LONGLONG deadlineTicks =
            startCounter.QuadPart + (freq.QuadPart * kScanDeadlineMs) / 1000;

        SYSTEM_INFO si{};
        GetSystemInfo(&si);

        std::uint8_t* addr = reinterpret_cast<std::uint8_t*>(si.lpMinimumApplicationAddress);
        std::uint8_t* max  = reinterpret_cast<std::uint8_t*>(si.lpMaximumApplicationAddress);

        std::size_t regionsScanned = 0;
        std::size_t bytesScanned   = 0;
        std::size_t regionsSkipped = 0;
        DiagHit     diag[kMaxDiagHits]{};
        int         diagCount = 0;

        Log("[UiPalette] scan begin: key=0x%08X deadline=%lldms\n", key, kScanDeadlineMs);

        while (addr < max)
        {
            MEMORY_BASIC_INFORMATION mbi{};
            if (VirtualQuery(addr, &mbi, sizeof(mbi)) != sizeof(mbi)) break;

            std::uint8_t* nextAddr =
                reinterpret_cast<std::uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;

            LARGE_INTEGER now{};
            QueryPerformanceCounter(&now);
            if (now.QuadPart > deadlineTicks)
            {
                const double elapsedSec =
                    static_cast<double>(now.QuadPart - startCounter.QuadPart) /
                    static_cast<double>(freq.QuadPart);
                Log("[UiPalette] scan deadline hit for key 0x%08X after %.2fs (regions=%zu skipped=%zu bytes=%zu diagHits=%d)\n",
                    key, elapsedSec, regionsScanned, regionsSkipped, bytesScanned, diagCount);
                for (int i = 0; i < diagCount; ++i)
                {
                    Log("[UiPalette]   diag hit[%d] @ %p bytes=%08X %08X %08X %08X %08X %08X validator=%s\n",
                        i,
                        static_cast<void*>(diag[i].addr),
                        diag[i].bytes[0], diag[i].bytes[1], diag[i].bytes[2],
                        diag[i].bytes[3], diag[i].bytes[4], diag[i].bytes[5],
                        diag[i].validatorPassed ? "PASS" : "FAIL");
                }
                return nullptr;
            }

            if (mbi.State != MEM_COMMIT)               { ++regionsSkipped; addr = nextAddr; continue; }
            if (!IsAcceptableProtection(mbi.Protect))  { ++regionsSkipped; addr = nextAddr; continue; }
            if (mbi.RegionSize < sizeof(PaletteEntry)) { ++regionsSkipped; addr = nextAddr; continue; }

            ++regionsScanned;
            bytesScanned += mbi.RegionSize;

            PaletteEntry* arr   = nullptr;
            std::size_t   count = 0;
            if (SafeScanRegion(key,
                               reinterpret_cast<std::uint8_t*>(mbi.BaseAddress),
                               mbi.RegionSize,
                               &arr,
                               &count,
                               diag,
                               &diagCount))
            {
                QueryPerformanceCounter(&now);
                const double elapsedSec =
                    static_cast<double>(now.QuadPart - startCounter.QuadPart) /
                    static_cast<double>(freq.QuadPart);
                Log("[UiPalette] anchor 0x%08X found at %p; array %p count=%zu (region prot=0x%X type=0x%X size=%zu) (%.2fs)\n",
                    key,
                    static_cast<void*>(reinterpret_cast<std::uint8_t*>(mbi.BaseAddress)),
                    static_cast<void*>(arr),
                    count,
                    mbi.Protect,
                    mbi.Type,
                    static_cast<std::size_t>(mbi.RegionSize),
                    elapsedSec);
                outCount = count;
                return arr;
            }

            addr = nextAddr;
        }

        LARGE_INTEGER endCounter{};
        QueryPerformanceCounter(&endCounter);
        const double elapsedSec =
            static_cast<double>(endCounter.QuadPart - startCounter.QuadPart) /
            static_cast<double>(freq.QuadPart);
        Log("[UiPalette] key 0x%08X not found (regions=%zu skipped=%zu bytes=%zu diagHits=%d, %.2fs)\n",
            key, regionsScanned, regionsSkipped, bytesScanned, diagCount, elapsedSec);
        for (int i = 0; i < diagCount; ++i)
        {
            Log("[UiPalette]   diag hit[%d] @ %p bytes=%08X %08X %08X %08X %08X %08X validator=%s\n",
                i,
                static_cast<void*>(diag[i].addr),
                diag[i].bytes[0], diag[i].bytes[1], diag[i].bytes[2],
                diag[i].bytes[3], diag[i].bytes[4], diag[i].bytes[5],
                diag[i].validatorPassed ? "PASS" : "FAIL");
        }
        return nullptr;
    }

    static bool EnsurePaletteFor_NoLock(std::uint32_t key)
    {
        if (g_Entries) return true;

        std::size_t count = 0;
        PaletteEntry* arr = ScanForKey(key, count);
        if (!arr) return false;

        g_Entries    = arr;
        g_EntryCount = count;
        g_AnchorKey  = key;
        return true;
    }

    static PaletteEntry* FindEntry_NoLock(std::uint32_t key)
    {
        if (!g_Entries) return nullptr;
        for (std::size_t i = 0; i < g_EntryCount; ++i)
        {
            if (g_Entries[i].hash == key) return &g_Entries[i];
        }
        return nullptr;
    }

    static bool WritePaletteColor(PaletteEntry* entry,
                                  float r, float g, float b, float a)
    {
        if (!entry) return false;

        const std::size_t patchSize = sizeof(float) * 4;
        std::uint8_t* target = reinterpret_cast<std::uint8_t*>(&entry->r);

        DWORD oldProtect = 0;
        if (!VirtualProtect(target, patchSize, PAGE_READWRITE, &oldProtect))
        {
            Log("[UiPalette] VirtualProtect RW failed for entry 0x%08X\n", entry->hash);
            return false;
        }

        entry->r = r;
        entry->g = g;
        entry->b = b;
        entry->a = a;

        DWORD tmp = 0;
        VirtualProtect(target, patchSize, oldProtect, &tmp);
        return true;
    }
}

bool Install_UiPalette_Hook()
{
    Log("[UiPalette] install: deferred scan (will scan on first SetColor)\n");
    return true;
}

bool Uninstall_UiPalette_Hook()
{
    std::lock_guard<std::mutex> lock(g_Mutex);
    if (g_Entries)
    {
        for (const auto& kv : g_Originals)
        {
            PaletteEntry* entry = FindEntry_NoLock(kv.first);
            if (entry)
            {
                WritePaletteColor(entry, kv.second.r, kv.second.g, kv.second.b, kv.second.a);
            }
        }
    }
    g_Originals.clear();
    g_Entries    = nullptr;
    g_EntryCount = 0;
    g_AnchorKey  = 0;
    Log("[UiPalette] uninstall: originals restored, cache dropped\n");
    return true;
}

namespace UiPalette
{
    bool SetColor(std::uint32_t keyHash, float r, float g, float b, float a)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        if (!EnsurePaletteFor_NoLock(keyHash))
        {
            Log("[UiPalette] SetColor 0x%08X: palette not in memory yet (open the iDroid first)\n", keyHash);
            return false;
        }

        PaletteEntry* entry = FindEntry_NoLock(keyHash);
        if (!entry)
        {
            Log("[UiPalette] SetColor 0x%08X: key not in cached palette (anchored on 0x%08X). "
                "Call RestoreUiPalette() to drop cache and retry with this key as anchor.\n",
                keyHash, g_AnchorKey);
            return false;
        }

        if (g_Originals.find(keyHash) == g_Originals.end())
        {
            g_Originals[keyHash] = OriginalColor{ entry->r, entry->g, entry->b, entry->a };
        }

        const bool ok = WritePaletteColor(entry, r, g, b, a);
        if (ok)
        {
            Log("[UiPalette] SetColor 0x%08X -> (%.3f, %.3f, %.3f, %.3f)\n",
                keyHash, r, g, b, a);
        }
        return ok;
    }

    bool GetColor(std::uint32_t keyHash, float* outR, float* outG, float* outB, float* outA)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        if (!g_Entries) return false;

        PaletteEntry* entry = FindEntry_NoLock(keyHash);
        if (!entry) return false;

        if (outR) *outR = entry->r;
        if (outG) *outG = entry->g;
        if (outB) *outB = entry->b;
        if (outA) *outA = entry->a;
        return true;
    }

    void RestoreAll()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        if (g_Entries)
        {
            for (const auto& kv : g_Originals)
            {
                PaletteEntry* entry = FindEntry_NoLock(kv.first);
                if (entry)
                {
                    WritePaletteColor(entry, kv.second.r, kv.second.g, kv.second.b, kv.second.a);
                }
            }
        }
        g_Originals.clear();
        g_Entries    = nullptr;
        g_EntryCount = 0;
        g_AnchorKey  = 0;
        Log("[UiPalette] RestoreAll: originals restored, cache dropped (next SetColor will rescan)\n");
    }
}
