#include <pch.h>

#include "FobPlayerCharacters.h"

#include <Windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "FoxHashes.h"
#include "FoxPath.h"
#include "../fox/gm/GameObjectInstance.h"
#include "../soldier/HostageGender.h"
#include "../soldier/SoldierVoiceType.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "Player2Impl_CallVoice.h"
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
        std::uint8_t        written[kMaxPatchLen];
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

        std::memcpy(s.written, patch, s.patchLen);
        s.applied = true;
        return true;
    }

    void RestoreSite(PatchSite& s)
    {
        if (!s.applied || !s.site) return;
        if (std::memcmp(s.site, s.written, s.patchLen) != 0)
        {
            LogWarn("FobChars: %s was changed by someone else, left as is", s.name);
            s.applied = false;
            return;
        }
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
    static std::uint8_t  g_SlotRowWritten[8]{};
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

        std::memcpy(g_SlotRowWritten,     &newA, 4);
        std::memcpy(g_SlotRowWritten + 4, &newB, 4);

        g_SlotRowGate    = hit;
        g_SlotRowStub    = stub;
        g_SlotRowApplied = true;
        return true;
    }

    void RestoreSlotRowGate()
    {
        if (!g_SlotRowApplied || !g_SlotRowGate) return;

        if (std::memcmp(g_SlotRowGate + kSlotRowRelVanillaA, g_SlotRowWritten, 4) == 0)
            WriteBytes(g_SlotRowGate + kSlotRowRelVanillaA, g_SlotRowOriginal, 4);
        else
            LogWarn("FobChars: sortie-prep row gate A was changed by someone else, left as is");

        if (std::memcmp(g_SlotRowGate + kSlotRowRelVanillaB, g_SlotRowWritten + 4, 4) == 0)
            WriteBytes(g_SlotRowGate + kSlotRowRelVanillaB, g_SlotRowOriginal + 4, 4);
        else
            LogWarn("FobChars: sortie-prep row gate B was changed by someone else, left as is");

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

    using SetPlayerLoadoutInfoFromStaffId_t =
        std::uint64_t (__fastcall*)(void*, std::uint8_t*, std::uint64_t);
    using PrepModeQuery_t = std::uint8_t (__fastcall*)(void*);
    using LangIdToKey_t   = void* (__fastcall*)(std::uint64_t*, const char*);
    using GetLangText_t   = const char* (__fastcall*)(std::uint64_t);

    constexpr std::size_t   kListEnabledOff     = 0x12C;  // one byte per row
    constexpr std::size_t   kListSeedsOff       = 0x328;  // one packed staff seed per row
    constexpr std::size_t   kListNamesOff       = 0x12E0; // 0x36 chars per row
    constexpr std::size_t   kListPortraitsOff   = 0x8CB8; // one texture path hash per row
    constexpr std::size_t   kListCountOff       = 0x9C8C;
    constexpr std::size_t   kListCursorOff      = 0x9C90;
    constexpr std::size_t   kListCurrentTypeOff = 0x9C94;
    constexpr std::size_t   kListNameSize       = 0x36;
    constexpr std::uint16_t kListMaxRows        = 0x1F7;

    constexpr std::size_t kPrepIsSnakeOnlyVtbl = 0x4D0;
    constexpr std::size_t kPrepIsFobSortieVtbl = 0x4F0;

    constexpr std::uint8_t kUniqueStaffId_Snake  = 0xF8;
    constexpr std::uint8_t kUniqueStaffId_Avatar = 0xF7;

    constexpr std::uint64_t kLiquidSeedBit = 1ull << 32; // lowest bit of the random part of a unique seed

    constexpr std::size_t kInfoPartsTypeOff  = 0x00;
    constexpr std::size_t kInfoCamoTypeOff   = 0x01;
    constexpr std::size_t kInfoHandTypeOff   = 0x02;
    constexpr std::size_t kInfoHeadOptionOff = 0x03;
    constexpr std::size_t kInfoPlayerTypeOff = 0xC0;

    constexpr const char* kLiquidPartsFpk    = "/Assets/tpp/pack/player/parts/plparts_liquid.fpk";
    constexpr const char* kLiquidNameLangId  = "marker_chara_eli";
    constexpr const char* kLiquidPortrait    = "/Assets/tpp/ui/texture/StaffImage/mb_staff_face_liquid.ftex";

    const std::uint8_t kSetPlayerLoadoutInfoEntry[] = {
        0x40, 0x53, 0x56, 0x57, 0x41, 0x56,
        0x48, 0x83, 0xEC, 0x28,
        0x48, 0x8B, 0xDA,
        0x4C, 0x89, 0x64, 0x24, 0x60,
        0x4C, 0x89, 0x6C, 0x24, 0x68,
        0x4D, 0x8B, 0xE8,
        0x33, 0xD2,
        0x41, 0xB8, 0xE8, 0x00, 0x00, 0x00
    };

    static SetPlayerLoadoutInfoFromStaffId_t g_SetPlayerLoadoutInfoOrig   = nullptr;
    static void*                             g_SetPlayerLoadoutInfoTarget = nullptr;
    static std::atomic<std::uint64_t>        g_LiquidSeed{ 0 };
    static std::atomic<int>                  g_LoggedLiquidPackMissing{ 0 };

    static std::uint8_t UniqueStaffIdOfSeed(std::uint64_t seed)
    {
        if (static_cast<std::uint8_t>((seed >> 7) & 0x3F) != kStaffUniqueMark) return 0;
        return static_cast<std::uint8_t>(seed >> 45); // the id is the top byte of the seed's high word
    }

    static bool LiquidRowAllowed(void* selector)
    {
        __try
        {
            auto* base = reinterpret_cast<std::uint8_t*>(selector);
            auto* host = *reinterpret_cast<std::uint8_t**>(base + 0x38);
            auto* menu = *reinterpret_cast<std::uint8_t**>(host + 0x40);
            void* prep = *reinterpret_cast<void**>(menu + 0x40);
            if (!prep) return false;

            auto** vtbl = *reinterpret_cast<void***>(prep);
            auto snakeOnly = reinterpret_cast<PrepModeQuery_t>(vtbl[kPrepIsSnakeOnlyVtbl / sizeof(void*)]);
            auto fobSortie = reinterpret_cast<PrepModeQuery_t>(vtbl[kPrepIsFobSortieVtbl / sizeof(void*)]);
            return snakeOnly(prep) == 0 && fobSortie(prep) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static bool InsertLiquidRow(void* selector, const char* name, std::uint64_t portrait, std::uint64_t* outSeed)
    {
        __try
        {
            auto* base      = reinterpret_cast<std::uint8_t*>(selector);
            auto* enabled   = base + kListEnabledOff;
            auto* seeds     = reinterpret_cast<std::uint64_t*>(base + kListSeedsOff);
            auto* names     = reinterpret_cast<char*>(base + kListNamesOff);
            auto* portraits = reinterpret_cast<std::uint64_t*>(base + kListPortraitsOff);
            auto* count     = reinterpret_cast<std::uint16_t*>(base + kListCountOff);
            auto* cursor    = reinterpret_cast<std::uint32_t*>(base + kListCursorOff);
            const int currentType = *reinterpret_cast<int*>(base + kListCurrentTypeOff);

            std::uint16_t rows = *count;
            if (rows == 0 || UniqueStaffIdOfSeed(seeds[0]) != kUniqueStaffId_Snake) return false;
            if (rows >= kListMaxRows) rows = kListMaxRows - 1;

            std::uint16_t at = 1;
            while (at < rows)
            {
                const std::uint8_t id = UniqueStaffIdOfSeed(seeds[at]);
                if (id != kUniqueStaffId_Avatar && id != kUniqueStaffId_Ocelot && id != kUniqueStaffId_Quiet) break;
                ++at;
            }

            const std::size_t moved = static_cast<std::size_t>(rows - at);
            std::memmove(enabled + at + 1, enabled + at, moved);
            std::memmove(seeds + at + 1, seeds + at, moved * sizeof(std::uint64_t));
            std::memmove(names + (at + 1) * kListNameSize, names + at * kListNameSize, moved * kListNameSize);
            std::memmove(portraits + at + 1, portraits + at, moved * sizeof(std::uint64_t));

            const std::uint64_t seed = seeds[0] ^ kLiquidSeedBit;
            enabled[at]   = 1;
            seeds[at]     = seed;
            portraits[at] = portrait ? portrait : portraits[0];
            std::memcpy(names + at * kListNameSize, names, kListNameSize);
            if (name)
            {
                char* row = names + at * kListNameSize;
                std::size_t i = 0;
                for (; i + 1 < kListNameSize && name[i]; ++i) row[i] = name[i];
                row[i] = 0;
            }

            if (currentType == fobchars::kPlayerType_Liquid) *cursor = at;
            else if (*cursor >= at && *cursor < rows) *cursor += 1;

            *count   = static_cast<std::uint16_t>(rows + 1);
            *outSeed = seed;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static const char* LiquidNameText()
    {
        auto toKey   = reinterpret_cast<LangIdToKey_t>(ResolveGameAddress(gAddr.Ui_LangIdToKey));
        auto getText = reinterpret_cast<GetLangText_t>(ResolveGameAddress(gAddr.Ui_GetLangText));
        if (!toKey || !getText) return nullptr;

        __try
        {
            std::uint64_t key = 0;
            toKey(&key, kLiquidNameLangId);
            const char* text = key ? getText(key) : nullptr;
            return (text && text[0]) ? text : nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    static void AddLiquidRow(void* selector)
    {
        g_LiquidSeed.store(0, std::memory_order_relaxed);

        if (!selector || !g_SetPlayerLoadoutInfoTarget) return;
        if (MissionCodeGuard::ShouldBypassHooks()) return;
        if (!LiquidRowAllowed(selector)) return;

        if (!fox::PathExists(kLiquidPartsFpk))
        {
            if (g_LoggedLiquidPackMissing.exchange(1, std::memory_order_relaxed) == 0)
                LogWarn("FobChars: %s not installed, Liquid missing from character list", kLiquidPartsFpk);
            return;
        }

        const std::uint64_t portrait =
            fox::PathExists(kLiquidPortrait) ? FoxHashes::PathCode64Ext(kLiquidPortrait) : 0;   // without the texture the row keeps Snake's photo

        std::uint64_t seed = 0;
        if (InsertLiquidRow(selector, LiquidNameText(), portrait, &seed))
            g_LiquidSeed.store(seed, std::memory_order_relaxed);
    }

    static std::uint64_t __fastcall hkSetPlayerLoadoutInfoFromStaffId(
        void* self, std::uint8_t* info, std::uint64_t staffSeed)
    {
        const std::uint64_t result = g_SetPlayerLoadoutInfoOrig(self, info, staffSeed);

        const std::uint64_t liquidSeed = g_LiquidSeed.load(std::memory_order_relaxed);
        if (!info || liquidSeed == 0 || staffSeed != liquidSeed) return result;
        if (MissionCodeGuard::ShouldBypassHooks()) return result;

        __try
        {
            info[kInfoPlayerTypeOff] = fobchars::kPlayerType_Liquid;
            info[kInfoPartsTypeOff]  = 0;
            info[kInfoCamoTypeOff]   = 0;
            info[kInfoHandTypeOff]   = 1; // PlayerHandType.NORMAL
            info[kInfoHeadOptionOff] = 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
        return result;
    }

    bool InstallLiquidRow()
    {
        if (g_SetPlayerLoadoutInfoTarget) return true;

        void* target = ResolveGameAddress(
            gAddr.MissionPreparationSystemImpl_SetPlayerLoadoutInfoFromStaffId);
        if (!target)
        {
            LogError("FobChars: no address for %s, Liquid missing from character list",
                GetGameBuildName(gGameBuild));
            return false;
        }

        const HookEntry state = CheckHookEntry(target, kSetPlayerLoadoutInfoEntry,
            sizeof(kSetPlayerLoadoutInfoEntry));
        if (state == HookEntry::Unknown)
        {
            char desc[160] = {};
            DescribeFunctionEntry(target, desc, sizeof(desc));
            LogError("FobChars: staff loadout code differs on this build (%s), Liquid missing from character list", desc);
            return false;
        }
        if (state == HookEntry::ForeignHook)
            LogHookedOnTop("FobChars: MissionPreparationSystemImpl::SetPlayerLoadoutInfoFromStaffId", target);

        if (!CreateAndEnableHook(target, &hkSetPlayerLoadoutInfoFromStaffId,
                reinterpret_cast<void**>(&g_SetPlayerLoadoutInfoOrig)))
        {
            LogError("FobChars: staff loadout hook failed at %p, Liquid missing from character list", target);
            return false;
        }

        g_SetPlayerLoadoutInfoTarget = target;
        return true;
    }

    void UninstallLiquidRow()
    {
        g_LiquidSeed.store(0, std::memory_order_relaxed);
        if (!g_SetPlayerLoadoutInfoTarget) return;
        DisableAndRemoveHook(g_SetPlayerLoadoutInfoTarget);
        g_SetPlayerLoadoutInfoTarget = nullptr;
        g_SetPlayerLoadoutInfoOrig   = nullptr;
    }

    using StaffSeedToNameLangMessageCode_t =
        std::uint64_t (__fastcall*)(void*, std::uint32_t, std::uint32_t, std::uint8_t);

    constexpr std::uint32_t kSeedHighWordMask = 0xFFFFF800u; // callers pass the seed's upper bits here, moved down by 21

    const std::uint8_t kStaffSeedToNameEntry[] = {
        0x48, 0x89, 0x6C, 0x24, 0x10,
        0x48, 0x89, 0x74, 0x24, 0x18,
        0x57,
        0x48, 0x83, 0xEC, 0x20,
        0x8B, 0xC2,
        0x41, 0x0F, 0xB6, 0xF9,
        0x8B, 0xF2,
        0xC1, 0xE8, 0x07
    };

    static StaffSeedToNameLangMessageCode_t g_StaffSeedToNameOrig   = nullptr;
    static void*                            g_StaffSeedToNameTarget = nullptr;

    static std::uint64_t __fastcall hkStaffSeedToNameLangMessageCode(
        void* staffController, std::uint32_t seedLow, std::uint32_t seedHigh, std::uint8_t fullName)
    {
        const std::uint64_t liquidSeed = g_LiquidSeed.load(std::memory_order_relaxed);
        if (liquidSeed == 0 || !fullName
            || seedLow != static_cast<std::uint32_t>(liquidSeed)
            || (seedHigh & kSeedHighWordMask) != (static_cast<std::uint32_t>(liquidSeed >> 21) & kSeedHighWordMask)
            || MissionCodeGuard::ShouldBypassHooks())
            return g_StaffSeedToNameOrig(staffController, seedLow, seedHigh, fullName);

        std::uint64_t key = 0;
        auto toKey = reinterpret_cast<LangIdToKey_t>(ResolveGameAddress(gAddr.Ui_LangIdToKey));
        if (toKey) toKey(&key, kLiquidNameLangId);
        if (key == 0) return g_StaffSeedToNameOrig(staffController, seedLow, seedHigh, fullName);
        return key;
    }

    bool InstallLiquidSeedName()
    {
        if (g_StaffSeedToNameTarget) return true;

        void* target = ResolveGameAddress(gAddr.StaffControllerImpl_StaffSeedToNameLangMessageCode);
        if (!target)
        {
            LogError("FobChars: no staff name address for %s, Liquid's details say Snake",
                GetGameBuildName(gGameBuild));
            return false;
        }

        const HookEntry state = CheckHookEntry(target, kStaffSeedToNameEntry, sizeof(kStaffSeedToNameEntry));
        if (state == HookEntry::Unknown)
        {
            char desc[160] = {};
            DescribeFunctionEntry(target, desc, sizeof(desc));
            LogError("FobChars: staff name code differs on this build (%s), Liquid's details say Snake", desc);
            return false;
        }
        if (state == HookEntry::ForeignHook)
            LogHookedOnTop("FobChars: StaffControllerImpl::StaffSeedToNameLangMessageCode", target);

        if (!CreateAndEnableHook(target, &hkStaffSeedToNameLangMessageCode,
                reinterpret_cast<void**>(&g_StaffSeedToNameOrig)))
        {
            LogError("FobChars: staff name hook failed at %p, Liquid's details say Snake", target);
            return false;
        }

        g_StaffSeedToNameTarget = target;
        return true;
    }

    void UninstallLiquidSeedName()
    {
        if (!g_StaffSeedToNameTarget) return;
        DisableAndRemoveHook(g_StaffSeedToNameTarget);
        g_StaffSeedToNameTarget = nullptr;
        g_StaffSeedToNameOrig   = nullptr;
    }

    using GetCharaSlotIconTexturePathId_t = std::uint64_t* (__fastcall*)(void*, std::uint64_t*, std::int32_t);

    const std::uint8_t kGetCharaSlotIconEntry[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08,
        0x57,
        0x48, 0x83, 0xEC, 0x20,
        0x48, 0x8B, 0x05
    };

    static GetCharaSlotIconTexturePathId_t g_GetCharaSlotIconOrig   = nullptr;
    static void*                           g_GetCharaSlotIconTarget = nullptr;

    static std::uint64_t* __fastcall hkGetCharaSlotIconTexturePathId(
        void* missionPreparation, std::uint64_t* pathId, std::int32_t playerType)
    {
        if (!pathId || playerType != fobchars::kPlayerType_Liquid
            || MissionCodeGuard::ShouldBypassHooks() || !fox::PathExists(kLiquidPortrait))
            return g_GetCharaSlotIconOrig(missionPreparation, pathId, playerType);

        *pathId = FoxHashes::PathCode64Ext(kLiquidPortrait);   // the game has no photo for this player type and shows Snake's
        return pathId;
    }

    bool InstallLiquidSlotPortrait()
    {
        if (g_GetCharaSlotIconTarget) return true;

        void* target = ResolveGameAddress(gAddr.MissionPreparationCallbackImpl_GetCharaSlotIconTexturePathId);
        if (!target)
        {
            LogError("FobChars: no character slot photo address for %s, Liquid's sortie slot shows Snake",
                GetGameBuildName(gGameBuild));
            return false;
        }

        const HookEntry state = CheckHookEntry(target, kGetCharaSlotIconEntry, sizeof(kGetCharaSlotIconEntry));
        if (state == HookEntry::Unknown)
        {
            char desc[160] = {};
            DescribeFunctionEntry(target, desc, sizeof(desc));
            LogError("FobChars: character slot photo code differs on this build (%s), Liquid's sortie slot shows Snake", desc);
            return false;
        }
        if (state == HookEntry::ForeignHook)
            LogHookedOnTop("FobChars: MissionPreparationCallbackImpl character slot photo", target);

        if (!CreateAndEnableHook(target, &hkGetCharaSlotIconTexturePathId,
                reinterpret_cast<void**>(&g_GetCharaSlotIconOrig)))
        {
            LogError("FobChars: character slot photo hook failed at %p, Liquid's sortie slot shows Snake", target);
            return false;
        }

        g_GetCharaSlotIconTarget = target;
        return true;
    }

    void UninstallLiquidSlotPortrait()
    {
        if (!g_GetCharaSlotIconTarget) return;
        DisableAndRemoveHook(g_GetCharaSlotIconTarget);
        g_GetCharaSlotIconTarget = nullptr;
        g_GetCharaSlotIconOrig   = nullptr;
    }

    static std::uint64_t __fastcall hkSetupStaffList(void* self)
    {
        const bool outermost = (g_QuietRowDepth == 0);
        ++g_QuietRowDepth;

        void* sc = StaffControllerAt(self, kStaffControllerOff);
        if (outermost) PrepareQuietRowOn(sc);

        std::uint64_t result = 0;
        __try
        {
            result = g_SetupStaffListOrig(self);
        }
        __finally
        {
            --g_QuietRowDepth;
            if (outermost) RestoreQuietRow(sc);
        }
        if (outermost) AddLiquidRow(self);
        return result;
    }

    static void __fastcall hkSetupCharacterSlotRow(void* self)
    {
        const bool outermost = (g_QuietRowDepth == 0);
        ++g_QuietRowDepth;

        void* sc = StaffControllerAt(self, kPrepStaffControllerOff);
        if (outermost) PrepareQuietRowOn(sc);

        __try
        {
            g_SlotRowPainterOrig(self);
        }
        __finally
        {
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

namespace
{
    using ExecActionButtonCarry_t =
        std::uint8_t (__fastcall*)(void*, std::uint32_t, std::uint8_t, std::uint8_t*);

    constexpr std::size_t   kCarryResultSize       = 0x44;
    constexpr std::size_t   kCarryResultTargetOff  = 0x20; // game object id of the body the prompt is for
    constexpr std::uint16_t kGameObjectTypeSoldier = 2;
    constexpr std::size_t   kSoldier2Part          = 0x20; // the voice lookup takes this part, not FindSoldier's Soldier2Impl

    const std::uint8_t kExecActionButtonCarryEntry[] = {
        0x40, 0x55, 0x53, 0x41, 0x56, 0x41, 0x57,
        0x48, 0x8D, 0xAC, 0x24, 0xA8, 0xFE, 0xFF, 0xFF,
        0x48, 0x81, 0xEC, 0x58, 0x02, 0x00, 0x00
    };

    static ExecActionButtonCarry_t g_ExecActionButtonCarryOrig   = nullptr;
    static void*                   g_ExecActionButtonCarryTarget = nullptr;

    static bool LiquidCanCarry(std::uint16_t gameObjectId)
    {
        if ((gameObjectId >> 9) != kGameObjectTypeSoldier)
        {
            const int gender = HostageGender::Read(gameObjectId);
            return gender == HostageGender::kChild || gender == HostageGender::kFemale;
        }

        const std::uint8_t* soldier2Impl = GameObjectInstance::FindSoldier(gameObjectId).object;
        if (!soldier2Impl) return false;

        switch (SoldierVoiceType::Of(soldier2Impl + kSoldier2Part, gameObjectId & 0x1FFu))
        {
        case SoldierVoiceType::kChildA:  case SoldierVoiceType::kChildB:
        case SoldierVoiceType::kChildC:  case SoldierVoiceType::kChildD:
        case SoldierVoiceType::kEneAFml: case SoldierVoiceType::kEneBFml:
        case SoldierVoiceType::kEneCFml: case SoldierVoiceType::kEneDFml:
            return true;
        default:
            return false;
        }
    }

    static std::uint8_t __fastcall hkExecActionButtonCarry(
        void* self, std::uint32_t playerIndex, std::uint8_t arg, std::uint8_t* result)
    {
        if (!result || MissionCodeGuard::ShouldBypassHooks()
            || outfit::ReadLivePlayerType() != fobchars::kPlayerType_Liquid)
            return g_ExecActionButtonCarryOrig(self, playerIndex, arg, result);

        std::uint8_t before[kCarryResultSize];
        std::memcpy(before, result, kCarryResultSize);

        const std::uint8_t found = g_ExecActionButtonCarryOrig(self, playerIndex, arg, result);
        if (!found) return found;

        std::uint16_t target = 0;
        std::memcpy(&target, result + kCarryResultTargetOff, sizeof(target));
        if (LiquidCanCarry(target)) return found;

        std::memcpy(result, before, kCarryResultSize);
        return 0;
    }

    bool InstallLiquidCarryLimit()
    {
        if (g_ExecActionButtonCarryTarget) return true;

        void* target = ResolveGameAddress(gAddr.CarryActionPluginImpl_ExecActionButtonCarry);
        if (!target)
        {
            LogError("FobChars: no carry address for %s, Liquid carries adults too",
                GetGameBuildName(gGameBuild));
            return false;
        }

        const HookEntry state = CheckHookEntry(target, kExecActionButtonCarryEntry,
            sizeof(kExecActionButtonCarryEntry));
        if (state == HookEntry::Unknown)
        {
            char desc[160] = {};
            DescribeFunctionEntry(target, desc, sizeof(desc));
            LogError("FobChars: carry code differs on this build (%s), Liquid carries adults too", desc);
            return false;
        }
        if (state == HookEntry::ForeignHook)
            LogHookedOnTop("FobChars: CarryActionPluginImpl::ExecActionButtonCarry", target);

        if (!CreateAndEnableHook(target, &hkExecActionButtonCarry,
                reinterpret_cast<void**>(&g_ExecActionButtonCarryOrig)))
        {
            LogError("FobChars: carry hook failed at %p, Liquid carries adults too", target);
            return false;
        }

        g_ExecActionButtonCarryTarget = target;
        return true;
    }

    void UninstallLiquidCarryLimit()
    {
        if (!g_ExecActionButtonCarryTarget) return;
        DisableAndRemoveHook(g_ExecActionButtonCarryTarget);
        g_ExecActionButtonCarryTarget = nullptr;
        g_ExecActionButtonCarryOrig   = nullptr;
    }
}

namespace
{
    using CqcStateHoldStart_t = void (__fastcall*)(std::uint8_t*, std::uint32_t, std::uint32_t, void*);

    constexpr std::size_t   kCqcPluginPlayers     = 0x38;
    constexpr std::size_t   kCqcPlayersFirstIndex = 0x24;
    constexpr std::size_t   kCqcPluginWorks       = 0x68;
    constexpr std::size_t   kCqcWorkSize          = 0x610;
    constexpr std::size_t   kCqcWorkTarget        = 0x78; // ExecCqc writes the target's game object id here
    constexpr std::size_t   kCqcWorkTargetIsChild = 0x87; // the held soldier's side sets it every update; it turns the hold into the escape
    constexpr std::uint32_t kCqcProcessUpdate     = 0xB;

    bool IsAdultSoldier(std::uint16_t gameObjectId)
    {
        if ((gameObjectId >> 9) != kGameObjectTypeSoldier)
            return false;

        const std::uint8_t* soldier2Impl = GameObjectInstance::FindSoldier(gameObjectId).object;
        if (!soldier2Impl) return false;

        switch (SoldierVoiceType::Of(soldier2Impl + kSoldier2Part, gameObjectId & 0x1FFu))
        {
        case SoldierVoiceType::kChildA: case SoldierVoiceType::kChildB:
        case SoldierVoiceType::kChildC: case SoldierVoiceType::kChildD:
            return false;
        default:
            return true;
        }
    }

    const std::uint8_t kCqcStateHoldStartEntry[] = {
        0x4C, 0x8B, 0xDC,
        0x55, 0x53, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57,
        0x48, 0x8D, 0x6C, 0x24, 0x98,
        0x48, 0x81, 0xEC, 0x68, 0x01, 0x00, 0x00
    };

    static CqcStateHoldStart_t g_CqcStateHoldStartOrig   = nullptr;
    static void*               g_CqcStateHoldStartTarget = nullptr;

    static void __fastcall hkCqcStateHoldStart(
        std::uint8_t* plugin, std::uint32_t playerIndex, std::uint32_t process, void* argument)
    {
        if (plugin && !MissionCodeGuard::ShouldBypassHooks()
            && outfit::ReadLivePlayerType() == fobchars::kPlayerType_Liquid)
        {
            const std::uint8_t* players = *reinterpret_cast<std::uint8_t**>(plugin + kCqcPluginPlayers);
            std::uint8_t*       works   = *reinterpret_cast<std::uint8_t**>(plugin + kCqcPluginWorks);
            if (players && works)
            {
                std::uint32_t firstPlayerIndex = 0;
                std::memcpy(&firstPlayerIndex, players + kCqcPlayersFirstIndex, sizeof(firstPlayerIndex));

                std::uint8_t* work = works + static_cast<std::size_t>(playerIndex - firstPlayerIndex) * kCqcWorkSize;
                std::uint16_t target = 0;
                std::memcpy(&target, work + kCqcWorkTarget, sizeof(target));
                const std::uint8_t heldIsChild = IsAdultSoldier(target) ? 1 : 0;   // the other way round from Snake: an adult slips out of his grab, a child stays
                if (work[kCqcWorkTargetIsChild] != heldIsChild)
                {
                    work[kCqcWorkTargetIsChild] = heldIsChild;
                    if (process != kCqcProcessUpdate)
                        LogDebug("FobChars: Liquid's hold on 0x%04X %s (hold start step %u)", target,
                            heldIsChild ? "escapes like a child from Snake" : "kept on a child", process);
                }
            }
        }

        g_CqcStateHoldStartOrig(plugin, playerIndex, process, argument);
    }

    bool InstallLiquidChildHold()
    {
        if (g_CqcStateHoldStartTarget) return true;

        void* target = ResolveGameAddress(gAddr.CqcActionPluginImpl_StateHoldStart);
        if (!target)
        {
            LogError("FobChars: no CQC hold address for %s, children escape Liquid's hold",
                GetGameBuildName(gGameBuild));
            return false;
        }

        const HookEntry state = CheckHookEntry(target, kCqcStateHoldStartEntry, sizeof(kCqcStateHoldStartEntry));
        if (state == HookEntry::Unknown)
        {
            char desc[160] = {};
            DescribeFunctionEntry(target, desc, sizeof(desc));
            LogError("FobChars: CQC hold code differs on this build (%s), children escape Liquid's hold", desc);
            return false;
        }
        if (state == HookEntry::ForeignHook)
            LogHookedOnTop("FobChars: CqcActionPluginImpl::StateHoldStart", target);

        if (!CreateAndEnableHook(target, &hkCqcStateHoldStart,
                reinterpret_cast<void**>(&g_CqcStateHoldStartOrig)))
        {
            LogError("FobChars: CQC hold hook failed at %p, children escape Liquid's hold", target);
            return false;
        }

        g_CqcStateHoldStartTarget = target;
        return true;
    }

    void UninstallLiquidChildHold()
    {
        if (!g_CqcStateHoldStartTarget) return;
        DisableAndRemoveHook(g_CqcStateHoldStartTarget);
        g_CqcStateHoldStartTarget = nullptr;
        g_CqcStateHoldStartOrig   = nullptr;
    }
}

namespace
{
    using SetTpsParams_t = void (__fastcall*)(void*, const std::uint8_t*);

    constexpr std::size_t kTpsParamsSize    = 0x7C;
    constexpr std::size_t kTpsParamsOffsetX = 0x10; // the aim view's offset, a Vector4
    constexpr std::size_t kTpsParamsOffsetY = 0x14;
    constexpr std::size_t kTpsParamsOffsetZ = 0x18;
    constexpr float       kLiquidAimHeight  = 0.8f;

    const std::uint8_t kSetTpsParamsEntry[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08,
        0x57,
        0x48, 0x83, 0xEC, 0x20,
        0x0F, 0xB6, 0x42, 0x20,
        0x48, 0x8B, 0xFA,
        0x48, 0x8B, 0xD9,
        0x89, 0x81, 0x0C, 0x02, 0x00, 0x00
    };

    static SetTpsParams_t g_SetTpsParamsOrig   = nullptr;
    static void*          g_SetTpsParamsTarget = nullptr;

    static void __fastcall hkSetTpsParams(void* camera, const std::uint8_t* params)
    {
        if (!params || MissionCodeGuard::ShouldBypassHooks()
            || outfit::ReadLivePlayerType() != fobchars::kPlayerType_Liquid)
        {
            g_SetTpsParamsOrig(camera, params);
            return;
        }

        alignas(16) std::uint8_t lowered[0x80] = {};
        std::memcpy(lowered, params, kTpsParamsSize);

        float height = 0.0f;
        std::memcpy(&height, lowered + kTpsParamsOffsetY, sizeof(height));
        const float loweredHeight = height * kLiquidAimHeight;
        std::memcpy(lowered + kTpsParamsOffsetY, &loweredHeight, sizeof(loweredHeight));

        static float s_loggedHeight = -1000.0f;
        if (height > s_loggedHeight + 0.01f || height < s_loggedHeight - 0.01f)
        {
            s_loggedHeight = height;
            float side = 0.0f;
            float depth = 0.0f;
            std::memcpy(&side, lowered + kTpsParamsOffsetX, sizeof(side));
            std::memcpy(&depth, lowered + kTpsParamsOffsetZ, sizeof(depth));
            LogDebug("FobChars: Liquid aim view height %.2f -> %.2f (side %.2f, depth %.2f)",
                height, loweredHeight, side, depth);
        }

        g_SetTpsParamsOrig(camera, lowered);
    }

    bool InstallLiquidAimCamera()
    {
        if (g_SetTpsParamsTarget) return true;

        void* target = ResolveGameAddress(gAddr.PlayerCameraImpl_SetTpsParams);
        if (!target)
        {
            LogError("FobChars: no aim camera address for %s, Liquid aims from Snake's height",
                GetGameBuildName(gGameBuild));
            return false;
        }

        const HookEntry state = CheckHookEntry(target, kSetTpsParamsEntry, sizeof(kSetTpsParamsEntry));
        if (state == HookEntry::Unknown)
        {
            char desc[160] = {};
            DescribeFunctionEntry(target, desc, sizeof(desc));
            LogError("FobChars: aim camera code differs on this build (%s), Liquid aims from Snake's height", desc);
            return false;
        }
        if (state == HookEntry::ForeignHook)
            LogHookedOnTop("FobChars: PlayerCameraImpl::SetTpsParams", target);

        if (!CreateAndEnableHook(target, &hkSetTpsParams,
                reinterpret_cast<void**>(&g_SetTpsParamsOrig)))
        {
            LogError("FobChars: aim camera hook failed at %p, Liquid aims from Snake's height", target);
            return false;
        }

        g_SetTpsParamsTarget = target;
        return true;
    }

    void UninstallLiquidAimCamera()
    {
        if (!g_SetTpsParamsTarget) return;
        DisableAndRemoveHook(g_SetTpsParamsTarget);
        g_SetTpsParamsTarget = nullptr;
        g_SetTpsParamsOrig   = nullptr;
    }
}

namespace
{
    using ExecCqc_t = void (__fastcall*)(void*, std::uint32_t, std::uint16_t, std::uint8_t, std::uint8_t);

    const std::uint8_t kExecCqcEntry[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08,
        0x48, 0x89, 0x6C, 0x24, 0x10,
        0x48, 0x89, 0x74, 0x24, 0x20,
        0x66, 0x44, 0x89, 0x44, 0x24, 0x18,
        0x57,
        0x48, 0x83, 0xEC, 0x20
    };

    static ExecCqc_t g_ExecCqcOrig   = nullptr;
    static void*     g_ExecCqcTarget = nullptr;

    static void __fastcall hkExecCqc(void* plugin, std::uint32_t playerIndex, std::uint16_t target,
        std::uint8_t command, std::uint8_t startsCqc)
    {
        if (!MissionCodeGuard::ShouldBypassHooks())
        {
            NotePlayerCqcCommand();
#ifdef _DEBUG   // TEMP probe: which CQC command a press on a child sends; remove once the hold on a child is tested
            std::uint32_t voice = 0;
            if ((target >> 9) == kGameObjectTypeSoldier)
            {
                const std::uint8_t* soldier2Impl = GameObjectInstance::FindSoldier(target).object;
                if (soldier2Impl)
                    voice = SoldierVoiceType::Of(soldier2Impl + kSoldier2Part, target & 0x1FFu);
            }

            const bool childVoice = voice == SoldierVoiceType::kChildA || voice == SoldierVoiceType::kChildB
                || voice == SoldierVoiceType::kChildC || voice == SoldierVoiceType::kChildD;

            LogDebug("FobChars: CQC command %u (8 hold, 5 throw) on 0x%04X type %u, child voice %d (0x%08X), "
                "hostage gender %d, starts %u, playerType %u",
                command, target, target >> 9, childVoice ? 1 : 0, voice,
                HostageGender::Read(target), startsCqc, outfit::ReadLivePlayerType());
#endif
        }

        g_ExecCqcOrig(plugin, playerIndex, target, command, startsCqc);
    }

    bool InstallCqcCommandProbe()
    {
        if (g_ExecCqcTarget) return true;

        void* target = ResolveGameAddress(gAddr.CqcActionPluginImpl_ExecCqc);
        if (!target) return false;

        const HookEntry state = CheckHookEntry(target, kExecCqcEntry, sizeof(kExecCqcEntry));
        if (state == HookEntry::Unknown)
        {
            char desc[160] = {};
            DescribeFunctionEntry(target, desc, sizeof(desc));
            LogError("FobChars: CQC command code differs on this build (%s), Liquid's punches keep the plain effort line", desc);
            return false;
        }
        if (state == HookEntry::ForeignHook)
            LogHookedOnTop("FobChars: CqcActionPluginImpl::ExecCqc", target);

        if (!CreateAndEnableHook(target, &hkExecCqc, reinterpret_cast<void**>(&g_ExecCqcOrig)))
        {
            LogError("FobChars: CQC command hook failed at %p, Liquid's punches keep the plain effort line", target);
            return false;
        }

        g_ExecCqcTarget = target;
        return true;
    }

    void UninstallCqcCommandProbe()
    {
        if (!g_ExecCqcTarget) return;
        DisableAndRemoveHook(g_ExecCqcTarget);
        g_ExecCqcTarget = nullptr;
        g_ExecCqcOrig   = nullptr;
    }
}

namespace fobchars
{
    void NoteLoadoutPlayerType(std::uint8_t playerType, std::uint32_t flags)
    {
        if ((flags & kInfoFlag_CarriesPlayerType) == 0) return;
        if (MissionCodeGuard::ShouldBypassHooks()) return;

        const bool keptAcrossLoads =
            playerType == kPlayerType_Liquid || playerType == kPlayerType_Ocelot || playerType == kPlayerType_Quiet;

        const int want = keptAcrossLoads ? static_cast<int>(playerType) : -1;
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
        if (MissionCodeGuard::ShouldBypassHooks())
        {
            std::uint8_t* fobState = QuarkPlayerState();
            std::uint8_t  fobLive  = 0;
            if (fobState && ReadPlayerTypeByte(fobState, &fobLive) && fobLive == kPlayerType_Liquid)
                WritePlayerTypeByte(fobState, 0);
            return;
        }

        const int want = g_Selected.load(std::memory_order_relaxed);
        if (want < 0) return;

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
    InstallLiquidRow();
    InstallLiquidSeedName();
    InstallLiquidSlotPortrait();
    InstallLiquidCarryLimit();
    InstallLiquidChildHold();
    InstallLiquidAimCamera();
    InstallCqcCommandProbe();

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
    UninstallCqcCommandProbe();
    UninstallLiquidAimCamera();
    UninstallLiquidChildHold();
    UninstallLiquidCarryLimit();
    UninstallLiquidSlotPortrait();
    UninstallLiquidSeedName();
    UninstallLiquidRow();
    UninstallQuietRowSynthesis();
    UninstallStaffLookupFallback();
    RestoreSlotRowGate();
    for (auto& s : g_Sites)
        RestoreSite(s);
}
