#include "pch.h"

#include "FobPlayerCharacters.h"

#include <Windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"
#include "../outfit/OutfitRegistry.h"
#include "../outfit/StaffControllerImpl_AddStaff.h"

namespace
{
    constexpr std::size_t kMaxPatchLen = 8;

    struct PatchSite
    {
        const char*         name;
        const std::uint8_t* pattern;
        const char*         mask;
        std::size_t         patternLen;
        std::size_t         patchOffset;
        std::size_t         patchLen;
        bool                relativeJump;

        std::uint8_t*       site;
        std::uint8_t        original[kMaxPatchLen];
        bool                applied;
    };

    const std::uint8_t kUniqueCharaListGatePattern[] = {
        0x48, 0x8B, 0x07,
        0x48, 0x8B, 0xCF,
        0xFF, 0x90, 0xF0, 0x04, 0x00, 0x00,
        0x84, 0xC0,
        0x0F, 0x84
    };
    const char kUniqueCharaListGateMask[] = "xxxxxxxxxxxxxxxx";

    const std::uint8_t kPartsStatusResetPattern[] = {
        0x8D, 0x48, 0xFB,
        0x80, 0xF9, 0x01,
        0x0F, 0x87, 0x00, 0x00, 0x00, 0x00,
        0x8B, 0x8E, 0x00, 0x00, 0x00, 0x00,
        0xC1, 0xE9, 0x00,
        0xF6, 0xC1, 0x00,
        0x0F, 0x85, 0x00, 0x00, 0x00, 0x00,
        0xF6, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x0F, 0x85, 0x00, 0x00, 0x00, 0x00,
        0x32, 0xC0
    };
    const char kPartsStatusResetMask[] =
        "xxxxxxxx????xx????xx?xx?xx????xx?????xx????xx";

    PatchSite g_Sites[] = {
        {
            "unique-character list gate",
            kUniqueCharaListGatePattern, kUniqueCharaListGateMask,
            sizeof(kUniqueCharaListGatePattern), 6, 6, false,
            nullptr, {}, false
        },
        {
            "parts-status player type reset",
            kPartsStatusResetPattern, kPartsStatusResetMask,
            sizeof(kPartsStatusResetPattern), 37, 6, true,
            nullptr, {}, false
        },
    };

    bool MatchesAt(const std::uint8_t* at, const std::uint8_t* pattern,
                   const char* mask, std::size_t len)
    {
        for (std::size_t i = 0; i < len; ++i)
        {
            if (mask[i] == '?') continue;
            if (at[i] != pattern[i]) return false;
        }
        return true;
    }

    bool ScanRange(const std::uint8_t* begin, const std::uint8_t* end,
                   const std::uint8_t* pattern, const char* mask,
                   std::size_t len, std::uint8_t** hit, int* hitCount)
    {
        if (static_cast<std::size_t>(end - begin) < len)
            return true;

        const std::uint8_t  first = pattern[0];
        const std::uint8_t* p     = begin;
        const std::uint8_t* last  = end - len;

        while (p <= last)
        {
            const void* found =
                std::memchr(p, first, static_cast<std::size_t>(last - p) + 1);
            if (!found) break;
            p = static_cast<const std::uint8_t*>(found);
            if (MatchesAt(p, pattern, mask, len))
            {
                if (*hitCount == 0) *hit = const_cast<std::uint8_t*>(p);
                if (++(*hitCount) > 1) return false;
            }
            ++p;
        }
        return true;
    }

    std::uint8_t* FindUniqueCode(const std::uint8_t* pattern, const char* mask,
                                 std::size_t len)
    {
        auto* base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
        if (!base) return nullptr;

        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
        const auto* nt =
            reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;

        const auto* sec = IMAGE_FIRST_SECTION(nt);
        std::uint8_t* hit  = nullptr;
        int           hits = 0;

        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec)
        {
            if ((sec->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) continue;
            const DWORD size = sec->Misc.VirtualSize
                ? sec->Misc.VirtualSize : sec->SizeOfRawData;
            if (size == 0) continue;

            std::uint8_t* begin = base + sec->VirtualAddress;
            if (!ScanRange(begin, begin + size, pattern, mask, len, &hit, &hits))
                break;
        }

        if (hits != 1)
        {
            return nullptr;
        }
        return hit;
    }

    bool WriteBytes(void* target, const std::uint8_t* src, std::size_t size)
    {
        DWORD oldProtect = 0;
        if (!VirtualProtect(target, size, PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            LogError("FobChars: VirtualProtect failed at %p (err %lu), byte patch skipped",
                target, GetLastError());
            return false;
        }
        std::memcpy(target, src, size);
        DWORD restored = 0;
        VirtualProtect(target, size, oldProtect, &restored);
        FlushInstructionCache(GetCurrentProcess(), target, size);
        return true;
    }

    bool ApplySite(PatchSite& s)
    {
        if (s.applied) return true;
        if (!s.site)
        {
            std::uint8_t* hit =
                FindUniqueCode(s.pattern, s.mask, s.patternLen);
            if (!hit)
            {
                return false;
            }
            s.site = hit + s.patchOffset;
        }

        std::memcpy(s.original, s.site, s.patchLen);

        std::uint8_t patch[kMaxPatchLen] = {};
        if (s.relativeJump)
        {
            std::int32_t rel = 0;
            std::memcpy(&rel, s.site + 2, sizeof(rel));
            const std::int32_t jmpRel = rel + 1;
            patch[0] = 0xE9;
            std::memcpy(patch + 1, &jmpRel, sizeof(jmpRel));
            patch[5] = 0x90;
        }
        else
        {
            patch[0] = 0xB0;
            patch[1] = 0x01;
            for (std::size_t i = 2; i < s.patchLen; ++i) patch[i] = 0x90;
        }

        if (!WriteBytes(s.site, patch, s.patchLen))
            return false;

        s.applied = true;
        return true;
    }

    void RestoreSite(PatchSite& s)
    {
        if (!s.applied || !s.site) return;
        if (WriteBytes(s.site, s.original, s.patchLen))
        {
            s.applied = false;
        }
    }
}

namespace
{
    using GetQuarkSystemTable_t = void* (__fastcall*)();

    constexpr std::size_t   kStateOff_PlayerType        = 0xFB;
    constexpr std::uint32_t kInfoFlag_CarriesPlayerType = 0x100u;

    static GetQuarkSystemTable_t g_GetQuarkSystemTable = nullptr;
    static std::atomic<int>      g_Selected{ -1 };

    static std::uint8_t* QuarkPlayerState()
    {
        if (!g_GetQuarkSystemTable)
        {
            g_GetQuarkSystemTable = reinterpret_cast<GetQuarkSystemTable_t>(
                ResolveGameAddress(gAddr.GetQuarkSystemTable));
        }
        if (!g_GetQuarkSystemTable) return nullptr;

        __try
        {
            auto* table =
                reinterpret_cast<std::uint8_t*>(g_GetQuarkSystemTable());
            if (!table) return nullptr;
            auto* holder = *reinterpret_cast<std::uint8_t**>(table + 0x98);
            if (!holder) return nullptr;
            return *reinterpret_cast<std::uint8_t**>(holder + 0x10);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    static bool ReadPlayerTypeByte(std::uint8_t* state, std::uint8_t* out)
    {
        __try
        {
            *out = state[kStateOff_PlayerType];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static bool WritePlayerTypeByte(std::uint8_t* state, std::uint8_t value)
    {
        __try
        {
            state[kStateOff_PlayerType] = value;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
}

namespace
{
    const std::uint8_t kSlotListRowGatePattern[] = {
        0x49, 0x8B, 0x4F, 0x48,
        0x48, 0x8B, 0x01,
        0xFF, 0x90, 0xA0, 0x01, 0x00, 0x00,
        0x83, 0xF8, 0x05,
        0x0F, 0x84, 0x00, 0x00, 0x00, 0x00,
        0x49, 0x8B, 0x4F, 0x48,
        0x48, 0x8B, 0x01,
        0xFF, 0x90, 0xA0, 0x01, 0x00, 0x00,
        0x83, 0xF8, 0x06,
        0x0F, 0x84, 0x00, 0x00, 0x00, 0x00,
        0x41, 0x8B, 0x87, 0xA0, 0x3C, 0x00, 0x00,
        0x85, 0xC0,
        0x0F, 0x85, 0x00, 0x00, 0x00, 0x00
    };
    const char kSlotListRowGateMask[] =
        "xxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxx????xxxxxxxxxxx????";

    constexpr std::size_t kSlotRowRelVanillaA = 18;
    constexpr std::size_t kSlotRowRelVanillaB = 40;
    constexpr std::size_t kSlotRowRelPerRow   = 55;
    constexpr std::size_t kSlotRowStubSize    = 128;

    static std::uint8_t* g_SlotRowGate    = nullptr;
    static std::uint8_t* g_SlotRowStub    = nullptr;
    static std::uint8_t  g_SlotRowOriginal[8]{};
    static bool          g_SlotRowApplied = false;

    static std::uint8_t __fastcall SlotRowKeepVanillaFill()
    {
        return MissionCodeGuard::ShouldBypassHooks()
            ? static_cast<std::uint8_t>(1)
            : static_cast<std::uint8_t>(0);
    }

    static void* AllocateStubNear(std::uintptr_t nearAddr, std::size_t size)
    {
        if (void* arena = HookArena::AllocateNear(nearAddr, size))
            return arena;
        SYSTEM_INFO si{};
        GetSystemInfo(&si);
        const std::uintptr_t granularity = si.dwAllocationGranularity;
        const std::uintptr_t rounded     = nearAddr & ~(granularity - 1);
        const std::uintptr_t maxDistance = 0x60000000ull;

        for (std::uintptr_t off = granularity;
             off < maxDistance; off += granularity)
        {
            if (rounded >= off)
            {
                if (void* p = VirtualAlloc(
                        reinterpret_cast<LPVOID>(rounded - off), size,
                        MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE))
                    return p;
            }
            if (void* p = VirtualAlloc(
                    reinterpret_cast<LPVOID>(rounded + off), size,
                    MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE))
                return p;
        }
        return nullptr;
    }

    static void BuildSlotRowStub(std::uint8_t* stub,
                                 std::uintptr_t vanillaTarget,
                                 std::uintptr_t perRowTarget)
    {
        static const std::uint8_t kCode[] = {
            0x41, 0x8B, 0x87, 0xA0, 0x3C, 0x00, 0x00,
            0x85, 0xC0,
            0x74, 0x35,
            0x55,
            0x48, 0x8B, 0xEC,
            0x48, 0x83, 0xE4, 0xF0,
            0x48, 0x83, 0xEC, 0x20,
            0x49, 0xBB, 0, 0, 0, 0, 0, 0, 0, 0,
            0x41, 0xFF, 0xD3,
            0x48, 0x8B, 0xE5,
            0x5D,
            0x84, 0xC0,
            0x75, 0x14,
            0x41, 0x8B, 0x87, 0xA0, 0x3C, 0x00, 0x00,
            0x49, 0xBB, 0, 0, 0, 0, 0, 0, 0, 0,
            0x41, 0xFF, 0xE3,
            0x49, 0xBB, 0, 0, 0, 0, 0, 0, 0, 0,
            0x41, 0xFF, 0xE3
        };

        std::memset(stub, 0xCC, kSlotRowStubSize);
        std::memcpy(stub, kCode, sizeof(kCode));

        const auto fn =
            reinterpret_cast<std::uint64_t>(&SlotRowKeepVanillaFill);
        std::memcpy(stub + 25, &fn, sizeof(fn));

        const auto perRow = static_cast<std::uint64_t>(perRowTarget);
        std::memcpy(stub + 53, &perRow, sizeof(perRow));

        const auto vanilla = static_cast<std::uint64_t>(vanillaTarget);
        std::memcpy(stub + 66, &vanilla, sizeof(vanilla));
    }

    bool ApplySlotRowGate()
    {
        if (g_SlotRowApplied) return true;

        std::uint8_t* hit = FindUniqueCode(
            kSlotListRowGatePattern, kSlotListRowGateMask,
            sizeof(kSlotListRowGatePattern));
        if (!hit)
        {
            LogError("FobChars: sortie-prep slot row fill not found, buddy and vehicle rows show Ocelot or Quiet");
            return false;
        }

        std::int32_t relVanilla = 0;
        std::int32_t relPerRow  = 0;
        std::memcpy(&relVanilla, hit + kSlotRowRelVanillaA, sizeof(relVanilla));
        std::memcpy(&relPerRow,  hit + kSlotRowRelPerRow,   sizeof(relPerRow));

        const auto vanillaTarget =
            reinterpret_cast<std::uintptr_t>(hit + kSlotRowRelVanillaA + 4)
            + static_cast<std::intptr_t>(relVanilla);
        const auto perRowTarget =
            reinterpret_cast<std::uintptr_t>(hit + kSlotRowRelPerRow + 4)
            + static_cast<std::intptr_t>(relPerRow);

        auto* stub = static_cast<std::uint8_t*>(AllocateStubNear(
            reinterpret_cast<std::uintptr_t>(hit), kSlotRowStubSize));
        if (!stub)
        {
            LogError("FobChars: no stub page near %p, buddy and vehicle rows show Ocelot or Quiet", hit);
            return false;
        }

        const auto stubAddr = reinterpret_cast<std::intptr_t>(stub);
        const std::intptr_t deltaA = stubAddr
            - reinterpret_cast<std::intptr_t>(hit + kSlotRowRelVanillaA + 4);
        const std::intptr_t deltaB = stubAddr
            - reinterpret_cast<std::intptr_t>(hit + kSlotRowRelVanillaB + 4);

        if (deltaA > INT32_MAX || deltaA < INT32_MIN ||
            deltaB > INT32_MAX || deltaB < INT32_MIN)
        {
            LogError("FobChars: stub page %p out of reach of %p, buddy and vehicle rows show Ocelot or Quiet", stub, hit);
            VirtualFree(stub, 0, MEM_RELEASE);
            return false;
        }

        BuildSlotRowStub(stub, vanillaTarget, perRowTarget);
        FlushInstructionCache(GetCurrentProcess(), stub, kSlotRowStubSize);

        std::memcpy(g_SlotRowOriginal,     hit + kSlotRowRelVanillaA, 4);
        std::memcpy(g_SlotRowOriginal + 4, hit + kSlotRowRelVanillaB, 4);

        const auto newA = static_cast<std::int32_t>(deltaA);
        const auto newB = static_cast<std::int32_t>(deltaB);

        if (!WriteBytes(hit + kSlotRowRelVanillaA,
                reinterpret_cast<const std::uint8_t*>(&newA), 4))
            return false;

        if (!WriteBytes(hit + kSlotRowRelVanillaB,
                reinterpret_cast<const std::uint8_t*>(&newB), 4))
        {
            WriteBytes(hit + kSlotRowRelVanillaA, g_SlotRowOriginal, 4);
            return false;
        }

        g_SlotRowGate    = hit;
        g_SlotRowStub    = stub;
        g_SlotRowApplied = true;
        return true;
    }

    void RestoreSlotRowGate()
    {
        if (!g_SlotRowApplied || !g_SlotRowGate) return;

        WriteBytes(g_SlotRowGate + kSlotRowRelVanillaA, g_SlotRowOriginal, 4);
        WriteBytes(g_SlotRowGate + kSlotRowRelVanillaB,
            g_SlotRowOriginal + 4, 4);

        g_SlotRowApplied = false;
    }
}

namespace
{
    using FindStaffByUniqueTypeId_t =
        bool (__fastcall*)(void*, std::uint16_t*, std::uint8_t);

    constexpr std::uint8_t kUniqueStaffId_Ocelot = 0xF9;
    constexpr std::uint8_t kUniqueStaffId_Quiet  = 0xFB;

    constexpr std::uint8_t kStaffUniqueMark = 0x3F;

    constexpr std::size_t  kStaffCountOff  = 0x9E6C;
    constexpr std::size_t  kStaffSeedLoOff = 0x9C78;
    constexpr std::size_t  kStaffSeedHiOff = 0x9C80;

    const std::uint8_t kFindStaffPattern[] = {
        0x48, 0x89, 0x5C, 0x24, 0x10,
        0x57,
        0x0F, 0xB7, 0x81, 0x6C, 0x9E, 0x00, 0x00,
        0x41, 0x0F, 0xB6, 0xD8,
        0x48, 0x00, 0x00,
        0x00, 0x00, 0x00,
        0x66, 0x85, 0xC0
    };
    const char kFindStaffMask[] = "xxxxxxxxxxxxxxxxxx?????xxx";

    constexpr std::size_t kFindStaffArgMovesOff = 18;

    const std::uint8_t kFindStaffArgMovesNative[] = {
        0x8B, 0xFA,
        0x4C, 0x8B, 0xD1
    };
    const std::uint8_t kFindStaffArgMovesArxan[] = {
        0x89, 0xD7,
        0x49, 0x89, 0xCA
    };

    static FindStaffByUniqueTypeId_t g_FindStaffOrig   = nullptr;
    static void*                     g_FindStaffTarget = nullptr;

    static bool ReadRoster(void* sc, std::uint16_t* count,
                           const std::uint32_t** lo, const std::uint32_t** hi)
    {
        __try
        {
            auto* base = reinterpret_cast<const std::uint8_t*>(sc);
            *count = *reinterpret_cast<const std::uint16_t*>(
                base + kStaffCountOff);
            *lo = *reinterpret_cast<const std::uint32_t* const*>(
                base + kStaffSeedLoOff);
            *hi = *reinterpret_cast<const std::uint32_t* const*>(
                base + kStaffSeedHiOff);
            return *count != 0 && *lo != nullptr && *hi != nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static bool ScanRosterForUniqueId(const std::uint32_t* lo,
                                      const std::uint32_t* hi,
                                      std::uint16_t count,
                                      std::uint8_t wantId,
                                      std::uint16_t* outIdx,
                                      std::uint8_t* outMark)
    {
        __try
        {
            for (std::uint16_t i = 0; i < count; ++i)
            {
                const auto mark = static_cast<std::uint8_t>((lo[i] >> 7) & 0x3F);
                if (mark != kStaffUniqueMark) continue;
                if (static_cast<std::uint8_t>(hi[i] >> 24) != wantId) continue;
                *outIdx  = i;
                *outMark = mark;
                return true;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
        return false;
    }

    static bool __fastcall hkFindStaffByUniqueTypeId(
        void* sc, std::uint16_t* out, std::uint8_t id)
    {
        bool found = g_FindStaffOrig(sc, out, id);

        const bool isQuiet  = (id == kUniqueStaffId_Quiet);
        const bool isOcelot = (id == kUniqueStaffId_Ocelot);
        if (!isQuiet && !isOcelot) return found;
        if (MissionCodeGuard::ShouldBypassHooks()) return found;

        std::uint16_t idx  = 0;
        std::uint8_t  mark = 0;

        if (!found && sc && out)
        {
            std::uint16_t        count = 0;
            const std::uint32_t* lo    = nullptr;
            const std::uint32_t* hi    = nullptr;
            if (ReadRoster(sc, &count, &lo, &hi) &&
                ScanRosterForUniqueId(lo, hi, count, id, &idx, &mark))
            {
                *out  = idx;
                found = true;
            }
        }

        return found;
    }

    bool InstallStaffLookupFallback()
    {
        if (g_FindStaffTarget) return true;

        std::uint8_t* hit = FindUniqueCode(kFindStaffPattern, kFindStaffMask,
            sizeof(kFindStaffPattern));
        if (!hit)
        {
            LogError("FobChars: unique-staff roster lookup not found, Ocelot or Quiet may miss their list row");
            return false;
        }

        if (std::memcmp(hit + kFindStaffArgMovesOff, kFindStaffArgMovesNative,
                sizeof(kFindStaffArgMovesNative)) != 0 &&
            std::memcmp(hit + kFindStaffArgMovesOff, kFindStaffArgMovesArxan,
                sizeof(kFindStaffArgMovesArxan)) != 0)
        {
            LogError("FobChars: roster lookup at %p has unknown arg encoding, Ocelot or Quiet may miss their list row", hit);
            return false;
        }

        if (!CreateAndEnableHook(hit, &hkFindStaffByUniqueTypeId,
                reinterpret_cast<void**>(&g_FindStaffOrig)))
        {
            LogError("FobChars: roster lookup hook failed at %p, Ocelot or Quiet may miss their list row", hit);
            return false;
        }

        g_FindStaffTarget = hit;
        return true;
    }

    void UninstallStaffLookupFallback()
    {
        if (!g_FindStaffTarget) return;
        DisableAndRemoveHook(g_FindStaffTarget);
        g_FindStaffTarget = nullptr;
        g_FindStaffOrig   = nullptr;
    }
}

namespace
{
    using SetupStaffList_t = std::uint64_t (__fastcall*)(void*);
    using SlotRowPainter_t = void (__fastcall*)(void*);

    using GenerateStaffParameter_t = std::uint64_t (__fastcall*)(
        void*, std::uint8_t, std::uint8_t, std::uint8_t, std::uint8_t,
        std::uint8_t);

    using CanSortieBuddyType_t = std::uint8_t (__fastcall*)(void*, std::uint8_t);

    constexpr std::size_t kStaffControllerOff     = 0x118;
    constexpr std::size_t kGenerateStaffParamVtbl = 0x1B0;

    constexpr std::size_t  kPrepStaffControllerOff = 0x60;
    constexpr std::size_t  kQuarkAppInterfaceOff    = 0x98;
    constexpr std::size_t  kBuddyServiceQuarkOff    = 0xE8;
    constexpr std::size_t  kCanSortieBuddyTypeVtbl  = 0x3E0;
    constexpr std::uint8_t kBuddyType_Quiet         = 3;

    constexpr std::uint8_t kQuietScratchIds[] = { 0xFA, 0xFC, 0xFD };

    const std::uint8_t kSlotRowPainterPrologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x20,
        0x55,
        0x56,
        0x57,
        0x41, 0x54,
        0x41, 0x57,
        0x48, 0x8B, 0xEC,
        0x48, 0x83, 0xEC, 0x30
    };
    const char kSlotRowPainterPrologueMask[] = "xxxxxxxxxxxxxxxxxxx";

    const std::uint8_t kSetupStaffListPrologue[] = {
        0x48, 0x8B, 0xC4,
        0x57,
        0x41, 0x54,
        0x41, 0x55,
        0x41, 0x56,
        0x41, 0x57,
        0x48, 0x81, 0xEC
    };
    const char kSetupStaffListPrologueMask[] = "xxxxxxxxxxxxxxx";

    constexpr std::size_t kSetupStaffListScanBack = 0x800;

    static SetupStaffList_t g_SetupStaffListOrig   = nullptr;
    static void*            g_SetupStaffListTarget = nullptr;

    static SlotRowPainter_t g_SlotRowPainterOrig   = nullptr;
    static void*            g_SlotRowPainterTarget = nullptr;

    static bool          g_QuietSeedValid = false;
    static std::uint32_t g_QuietSeedLo    = 0;
    static std::uint32_t g_QuietSeedHi    = 0;

    static bool          g_QuietRowActive = false;
    static std::uint16_t g_QuietRowIndex  = 0;
    static std::uint32_t g_QuietRowLo     = 0;
    static std::uint32_t g_QuietRowHi     = 0;

    static int g_QuietRowDepth = 0;

    static std::atomic<int> g_LoggedQuietRowFail{ 0 };

    static void* StaffControllerAt(void* menuThis, std::size_t off)
    {
        if (!menuThis) return nullptr;
        __try
        {
            return *reinterpret_cast<void**>(
                reinterpret_cast<std::uint8_t*>(menuThis) + off);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    static bool GenerateUniqueStaffSeed(void* sc, std::uint8_t uniqueId,
                                        std::uint32_t* outLo,
                                        std::uint32_t* outHi)
    {
        __try
        {
            auto** vtbl = *reinterpret_cast<void***>(sc);
            if (!vtbl) return false;

            auto gen = reinterpret_cast<GenerateStaffParameter_t>(
                vtbl[kGenerateStaffParamVtbl / sizeof(void*)]);
            if (!gen) return false;

            const std::uint64_t seed = gen(sc, 0, 0x3F, 0x0A, 0, uniqueId);

            const auto lo = static_cast<std::uint32_t>(seed);
            const auto hi = static_cast<std::uint32_t>(
                (seed >> 0x15) & 0xFFFFF800u);

            if (static_cast<std::uint8_t>((lo >> 7) & 0x3F) != kStaffUniqueMark)
                return false;
            if (static_cast<std::uint8_t>(hi >> 24) != uniqueId)
                return false;

            *outLo = lo;
            *outHi = hi;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static bool FindQuietScratchRow(const std::uint32_t* lo,
                                    const std::uint32_t* hi,
                                    std::uint16_t count,
                                    std::uint16_t* outRow,
                                    std::uint8_t* outId)
    {
        bool found = false;
        __try
        {
            for (std::uint16_t i = 0; i < count; ++i)
            {
                if (static_cast<std::uint8_t>((lo[i] >> 7) & 0x3F)
                        != kStaffUniqueMark)
                    continue;

                const auto id = static_cast<std::uint8_t>(hi[i] >> 24);
                for (std::size_t c = 0;
                     c < sizeof(kQuietScratchIds) / sizeof(kQuietScratchIds[0]);
                     ++c)
                {
                    if (id != kQuietScratchIds[c]) continue;
                    *outRow = i;
                    *outId  = id;
                    found   = true;
                    break;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
        return found;
    }

    static bool SwapRosterSeed(void* sc, std::uint16_t row,
                               std::uint32_t newLo, std::uint32_t newHi,
                               std::uint32_t* savedLo, std::uint32_t* savedHi)
    {
        __try
        {
            auto* base = reinterpret_cast<std::uint8_t*>(sc);
            auto* lo = *reinterpret_cast<std::uint32_t**>(base + kStaffSeedLoOff);
            auto* hi = *reinterpret_cast<std::uint32_t**>(base + kStaffSeedHiOff);
            if (!lo || !hi) return false;

            if (savedLo) *savedLo = lo[row];
            if (savedHi) *savedHi = hi[row];

            lo[row] = newLo;
            hi[row] = newHi;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static void RestoreQuietRow(void* sc)
    {
        if (!g_QuietRowActive) return;
        g_QuietRowActive = false;
        if (!sc) return;

        SwapRosterSeed(sc, g_QuietRowIndex, g_QuietRowLo, g_QuietRowHi,
            nullptr, nullptr);
    }

    static std::uint8_t* QuarkAppInterface()
    {
        if (!g_GetQuarkSystemTable)
        {
            g_GetQuarkSystemTable = reinterpret_cast<GetQuarkSystemTable_t>(
                ResolveGameAddress(gAddr.GetQuarkSystemTable));
        }
        if (!g_GetQuarkSystemTable) return nullptr;

        __try
        {
            auto* table =
                reinterpret_cast<std::uint8_t*>(g_GetQuarkSystemTable());
            if (!table) return nullptr;
            return *reinterpret_cast<std::uint8_t**>(
                table + kQuarkAppInterfaceOff);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    static bool QuietCanSortie()
    {
        std::uint8_t* app = QuarkAppInterface();
        if (!app) return false;

        __try
        {
            void* svc = *reinterpret_cast<void**>(app + kBuddyServiceQuarkOff);
            if (!svc) return false;

            auto** vtbl = *reinterpret_cast<void***>(svc);
            if (!vtbl) return false;

            auto canSortie = reinterpret_cast<CanSortieBuddyType_t>(
                vtbl[kCanSortieBuddyTypeVtbl / sizeof(void*)]);
            if (!canSortie) return false;

            return canSortie(svc, kBuddyType_Quiet) != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static void PrepareQuietRowOn(void* sc)
    {
        g_QuietRowActive = false;

        if (MissionCodeGuard::ShouldBypassHooks()) return;

        if (!QuietCanSortie())
        {
            return;
        }

        if (!sc) return;

        std::uint16_t        count = 0;
        const std::uint32_t* lo    = nullptr;
        const std::uint32_t* hi    = nullptr;
        if (!ReadRoster(sc, &count, &lo, &hi)) return;

        std::uint16_t present = 0;
        std::uint8_t  mark    = 0;
        if (ScanRosterForUniqueId(lo, hi, count, kUniqueStaffId_Quiet,
                &present, &mark))
            return;

        std::uint16_t row      = 0;
        std::uint8_t  borrowed = 0;
        if (!FindQuietScratchRow(lo, hi, count, &row, &borrowed))
        {
            if (g_LoggedQuietRowFail.exchange(1, std::memory_order_relaxed) == 0)
                LogWarn("FobChars: no spare unique roster row (FA/FC/FD), Quiet missing from character list");
            return;
        }

        if (!g_QuietSeedValid)
        {
            std::uint32_t newLo = 0;
            std::uint32_t newHi = 0;
            if (!GenerateUniqueStaffSeed(sc, kUniqueStaffId_Quiet,
                    &newLo, &newHi))
            {
                if (g_LoggedQuietRowFail.exchange(1, std::memory_order_relaxed)
                        == 0)
                    LogWarn("FobChars: staff generator refused unique id FB, Quiet missing from character list");
                return;
            }

            g_QuietSeedLo    = newLo;
            g_QuietSeedHi    = newHi;
            g_QuietSeedValid = true;
        }

        if (!SwapRosterSeed(sc, row, g_QuietSeedLo, g_QuietSeedHi,
                &g_QuietRowLo, &g_QuietRowHi))
        {
            if (g_LoggedQuietRowFail.exchange(1, std::memory_order_relaxed) == 0)
                LogWarn("FobChars: roster row %u write faulted, Quiet missing from character list",
                    static_cast<unsigned>(row));
            return;
        }

        g_QuietRowIndex  = row;
        g_QuietRowActive = true;
    }

    static std::uint64_t __fastcall hkSetupStaffList(void* self)
    {
        const bool outermost = (g_QuietRowDepth == 0);
        ++g_QuietRowDepth;

        void* sc = StaffControllerAt(self, kStaffControllerOff);
        if (outermost) PrepareQuietRowOn(sc);
        void* previousPortraitContext = outfit::EnterOwnRosterPortraitContext(sc);

        std::uint64_t result = 0;
        __try
        {
            result = g_SetupStaffListOrig(self);
        }
        __finally
        {
            outfit::LeaveOwnRosterPortraitContext(previousPortraitContext);
            --g_QuietRowDepth;
            if (outermost) RestoreQuietRow(sc);
        }
        return result;
    }

    static void __fastcall hkSetupCharacterSlotRow(void* self)
    {
        const bool outermost = (g_QuietRowDepth == 0);
        ++g_QuietRowDepth;

        void* sc = StaffControllerAt(self, kPrepStaffControllerOff);
        if (outermost) PrepareQuietRowOn(sc);
        void* previousPortraitContext = outfit::EnterOwnRosterPortraitContext(sc);

        __try
        {
            g_SlotRowPainterOrig(self);
        }
        __finally
        {
            outfit::LeaveOwnRosterPortraitContext(previousPortraitContext);
            --g_QuietRowDepth;
            if (outermost) RestoreQuietRow(sc);
        }
    }

    static std::uint8_t* ResolveEntryBefore(std::uint8_t* gate,
                                            const std::uint8_t* prologue,
                                            const char* mask, std::size_t len)
    {
        if (!gate) return nullptr;

        __try
        {
            for (std::size_t back = len;
                 back <= kSetupStaffListScanBack; ++back)
            {
                std::uint8_t* p = gate - back;
                if (MatchesAt(p, prologue, mask, len)) return p;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
        return nullptr;
    }

    bool InstallQuietRowSynthesis()
    {
        if (g_SetupStaffListTarget) return true;

        std::uint8_t* entry = ResolveEntryBefore(g_Sites[0].site,
            kSetupStaffListPrologue, kSetupStaffListPrologueMask,
            sizeof(kSetupStaffListPrologue));
        if (!entry)
        {
            if (g_Sites[0].site)
                LogError("FobChars: character list build not found near %p, Quiet missing without an FB record",
                    g_Sites[0].site);
            return false;
        }

        if (!CreateAndEnableHook(entry, &hkSetupStaffList,
                reinterpret_cast<void**>(&g_SetupStaffListOrig)))
        {
            LogError("FobChars: character list build hook failed at %p, Quiet missing without an FB record", entry);
            return false;
        }

        g_SetupStaffListTarget = entry;
        return true;
    }

    bool InstallQuietPrepRowSynthesis()
    {
        if (g_SlotRowPainterTarget) return true;

        std::uint8_t* entry = ResolveEntryBefore(g_SlotRowGate,
            kSlotRowPainterPrologue, kSlotRowPainterPrologueMask,
            sizeof(kSlotRowPainterPrologue));
        if (!entry)
        {
            if (g_SlotRowGate)
                LogError("FobChars: sortie-prep row painter not found near %p, Quiet prep rows show Snake",
                    g_SlotRowGate);
            return false;
        }

        if (!CreateAndEnableHook(entry, &hkSetupCharacterSlotRow,
                reinterpret_cast<void**>(&g_SlotRowPainterOrig)))
        {
            LogError("FobChars: sortie-prep row painter hook failed at %p, Quiet prep rows show Snake", entry);
            return false;
        }

        g_SlotRowPainterTarget = entry;
        return true;
    }

    void UninstallQuietRowSynthesis()
    {
        if (g_SlotRowPainterTarget)
        {
            DisableAndRemoveHook(g_SlotRowPainterTarget);
            g_SlotRowPainterTarget = nullptr;
            g_SlotRowPainterOrig   = nullptr;
        }

        if (!g_SetupStaffListTarget) return;
        DisableAndRemoveHook(g_SetupStaffListTarget);
        g_SetupStaffListTarget = nullptr;
        g_SetupStaffListOrig   = nullptr;
        g_QuietRowActive       = false;
    }
}

namespace fobchars
{
    void NoteLoadoutPlayerType(std::uint8_t playerType, std::uint32_t flags)
    {
        if ((flags & kInfoFlag_CarriesPlayerType) == 0) return;
        if (MissionCodeGuard::ShouldBypassHooks()) return;

        const bool fobOnly =
            playerType == kPlayerType_Ocelot || playerType == kPlayerType_Quiet;

        const int want = fobOnly ? static_cast<int>(playerType) : -1;
        g_Selected.store(want, std::memory_order_relaxed);
    }

    std::uint8_t GetSelectedPlayerType()
    {
        const int want = g_Selected.load(std::memory_order_relaxed);
        if (want < 0) return 0xFF;
        return static_cast<std::uint8_t>(want);
    }

    void ReassertSelectedCharacter()
    {
        const int want = g_Selected.load(std::memory_order_relaxed);
        if (want < 0) return;
        if (MissionCodeGuard::ShouldBypassHooks()) return;

        std::uint8_t* state = QuarkPlayerState();
        if (!state) return;

        std::uint8_t live = 0;
        if (!ReadPlayerTypeByte(state, &live)) return;
        if (live == static_cast<std::uint8_t>(want)) return;

        WritePlayerTypeByte(state, static_cast<std::uint8_t>(want));
    }
}

bool Install_FobPlayerCharacters_Patches()
{
    int applied = 0;
    for (auto& s : g_Sites)
        if (ApplySite(s)) ++applied;

    ApplySlotRowGate();
    InstallStaffLookupFallback();
    InstallQuietRowSynthesis();
    InstallQuietPrepRowSynthesis();

    if (applied == 0)
    {
        LogError("FobChars: no patch site matched, Ocelot and Quiet stay FOB only");
        return true;
    }

    if (!g_Sites[0].applied)
    {
        LogError("FobChars: %s not patched, Ocelot and Quiet not offered outside FOB", g_Sites[0].name);
    }
    else if (!g_Sites[1].applied)
    {
        LogError("FobChars: %s not patched, choice reverts to Snake at mission start", g_Sites[1].name);
    }

    return true;
}

void Uninstall_FobPlayerCharacters_Patches()
{
    g_Selected.store(-1, std::memory_order_relaxed);
    UninstallQuietRowSynthesis();
    UninstallStaffLookupFallback();
    RestoreSlotRowGate();
    for (auto& s : g_Sites)
        RestoreSite(s);
}
