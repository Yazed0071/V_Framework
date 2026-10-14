#include "pch.h"
#include "EquipDevelopControllerImpl_GetEquipIDFromDevelopIndex.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

#include "AddressSet.h"
#include "EquipAliasLease.h"
#include "HookArena.h"
#include "HookUtils.h"
#include "log.h"

namespace equip
{
    namespace
    {
        constexpr std::size_t kRecordDevelopId = 0x8;
        constexpr std::size_t kRecordEquipId   = 0xC;  // grade:4 | equipId:12
        constexpr std::size_t kFieldToDevelopId = kRecordEquipId - kRecordDevelopId;

        std::atomic<std::uint16_t> g_WideEquipByDevelopId[0x10000] = {};

        std::uint16_t WideEquipForField(const std::uint8_t* field)
        {
            const auto developId = *reinterpret_cast<const std::uint16_t*>(field - kFieldToDevelopId);
            return g_WideEquipByDevelopId[developId].load(std::memory_order_acquire);
        }

        std::uint32_t DevelopFieldForEngine(const std::uint8_t* field)
        {
            const std::uint16_t raw = *reinterpret_cast<const std::uint16_t*>(field);
            if ((raw >> 4) != kDevelopWideMark)
                return raw;
            const std::uint16_t wide = WideEquipForField(field);
            if (!wide)
                return raw;
            const auto engineId = static_cast<std::uint32_t>(ToEngine(wide));
            return (engineId << 4) | (raw & 0xF);
        }

        std::uint32_t DevelopFieldRealId(const std::uint8_t* field)
        {
            const std::uint16_t raw = *reinterpret_cast<const std::uint16_t*>(field);
            const std::uint16_t id = raw >> 4;
            if (id != kDevelopWideMark)
                return id;
            const std::uint16_t wide = WideEquipForField(field);
            return wide ? wide : id;
        }
    }

    void NoteWideDevelopRow(std::uint16_t developId, std::uint16_t wideEquipId)
    {
        g_WideEquipByDevelopId[developId].store(wideEquipId, std::memory_order_release);
    }

    std::uint16_t DevelopRecordEquipId(const std::uint8_t* record)
    {
        return static_cast<std::uint16_t>(DevelopFieldRealId(record + kRecordEquipId));
    }
}

namespace
{
    struct DevelopReadSite
    {
        std::uintptr_t movzx;       // MOVZX dst, word [record + 0xC]
        std::uint8_t   movzxBytes[6];
        std::uint8_t   movzxLen;
        std::uintptr_t shr16;       // SHR dst16,4 that goes away when the thunk hands back the id itself, 0 = keep the 32-bit SHR
        bool           realId;      // the id leaves the develop table (lists, loadouts), so it gets the real id, not the alias
    };

    const std::uint8_t kShrCx4[] = { 0x66, 0xC1, 0xE9, 0x04 };
    const std::uint8_t kShrAx4[] = { 0x66, 0xC1, 0xE8, 0x04 };

    const DevelopReadSite kSitesEn154[] = {
        { 0x140F658E2ull, { 0x0F, 0xB7, 0x4C, 0x32, 0x0C }, 5, 0x140F658EDull, true  }, // GetAvailableEquipIDArray
        { 0x140F65A73ull, { 0x0F, 0xB7, 0x54, 0x38, 0x0C }, 5, 0, false },
        { 0x140F65B03ull, { 0x0F, 0xB7, 0x54, 0x38, 0x0C }, 5, 0, false },
        { 0x140F65B93ull, { 0x0F, 0xB7, 0x54, 0x38, 0x0C }, 5, 0, false },
        { 0x140F65C23ull, { 0x0F, 0xB7, 0x54, 0x38, 0x0C }, 5, 0, false },
        { 0x140F666B8ull, { 0x44, 0x0F, 0xB7, 0x7C, 0x38, 0x0C }, 6, 0, false },
        { 0x140F667FCull, { 0x0F, 0xB7, 0x5C, 0x30, 0x0C }, 5, 0, false },
        { 0x140F68A73ull, { 0x0F, 0xB7, 0x54, 0x38, 0x0C }, 5, 0, false },
        { 0x140F69234ull, { 0x0F, 0xB7, 0x44, 0x08, 0x0C }, 5, 0x140F69239ull, true  }, // GetEquipID
        { 0x140F69254ull, { 0x0F, 0xB7, 0x44, 0x08, 0x0C }, 5, 0x140F69259ull, true  }, // GetEquipIDFromDevelopIndex
        { 0x140F6926Dull, { 0x0F, 0xB7, 0x5C, 0x08, 0x0C }, 5, 0, false },              // GetEquipPosition
        { 0x140F69873ull, { 0x0F, 0xB7, 0x54, 0x38, 0x0C }, 5, 0, false },
        { 0x140F69953ull, { 0x0F, 0xB7, 0x54, 0x38, 0x0C }, 5, 0, false },
        { 0x140F69EC6ull, { 0x0F, 0xB7, 0x47, 0x0C }, 4, 0x140F69ECAull, true  },       // GetHighestGradeEuipId compare
        { 0x140F69F12ull, { 0x0F, 0xB7, 0x44, 0x31, 0x0C }, 5, 0x140F69F17ull, true  }, // GetHighestGradeEuipId result
    };

    using GetHighestGradeEquipIdFn = std::uint16_t(__fastcall*)(void* self, std::uint16_t equipId, std::uint8_t newest);

    const std::uint8_t kHighestGradePrologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18
    };
    constexpr std::uintptr_t kHighestGradeEn154 = 0x140F69E70ull;

    struct AppliedSite
    {
        std::uint8_t* at;
        std::uint8_t  saved[16];
        std::size_t   len;
        std::uint8_t* shr;
        std::uint8_t  savedShr[4];
    };

    std::vector<AppliedSite>  g_Applied;
    GetHighestGradeEquipIdFn  g_OrigHighestGrade = nullptr;
    void*                     g_HighestGradeAddr = nullptr;

    bool BytesMatchSEH(const void* at, const std::uint8_t* want, std::size_t n)
    {
        __try
        {
            return std::memcmp(at, want, n) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool WriteCode(std::uint8_t* at, const std::uint8_t* bytes, std::size_t n)
    {
        DWORD old = 0;
        if (!VirtualProtect(at, n, PAGE_EXECUTE_READWRITE, &old))
            return false;
        std::memcpy(at, bytes, n);
        VirtualProtect(at, n, old, &old);
        FlushInstructionCache(GetCurrentProcess(), at, n);
        return true;
    }

    struct Emitter
    {
        std::uint8_t* out;
        std::size_t   n = 0;
        void b(std::initializer_list<std::uint8_t> bytes) { for (auto x : bytes) out[n++] = x; }
        void imm64(std::uint64_t v) { std::memcpy(out + n, &v, 8); n += 8; }
    };

    // stack slots of the saved volatile registers once RBP is popped: r11 r10 r9 r8 rdx rcx rax
    int SavedSlot(int reg)
    {
        switch (reg)
        {
        case 0:  return 0x30; // rax
        case 1:  return 0x28; // rcx
        case 2:  return 0x20; // rdx
        case 8:  return 0x18;
        case 9:  return 0x10;
        case 10: return 0x08;
        case 11: return 0x00;
        default: return -1;
        }
    }

    bool EmitThunk(const DevelopReadSite& site, std::uint8_t* code, std::size_t& len)
    {
        const std::uint8_t* m = site.movzxBytes;
        std::size_t p = 0;
        std::uint8_t rex = 0;
        if ((m[p] & 0xF0) == 0x40)
            rex = m[p++];
        if (m[p] != 0x0F || m[p + 1] != 0xB7)
            return false;
        p += 2;
        const std::uint8_t modrm = m[p++];
        const int mod = modrm >> 6;
        const int dst = ((modrm >> 3) & 7) | ((rex & 0x4) ? 8 : 0);
        const int rm  = modrm & 7;
        if (mod != 1)
            return false;
        std::uint8_t sib = 0;
        const bool hasSib = rm == 4;
        if (hasSib)
        {
            sib = m[p++];
            const int index = ((sib >> 3) & 7) | ((rex & 0x2) ? 8 : 0);
            const int base  = (sib & 7) | ((rex & 0x1) ? 8 : 0);
            if (index == 5 || base == 5 || base == 4)
                return false;
        }
        else if ((rm | ((rex & 0x1) ? 8 : 0)) == 5)
        {
            return false;
        }
        const std::uint8_t disp = m[p++];
        if (p != site.movzxLen)
            return false;

        const bool volatileDst = SavedSlot(dst) >= 0;
        if (!volatileDst && dst != 3 && dst != 15)
            return false;

        Emitter e{ code };
        e.b({ 0x9C, 0x50, 0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53 }); // pushfq, save volatiles
        e.b({ 0x55, 0x48, 0x89, 0xE5 });                                                  // push rbp / mov rbp,rsp
        e.b({ static_cast<std::uint8_t>(0x48 | (rex & 0x3)), 0x8D,
              static_cast<std::uint8_t>((modrm & 0xC7) | (1 << 3)) });                    // lea rcx, [same operand]
        if (hasSib)
            e.b({ sib });
        e.b({ disp });
        e.b({ 0x48, 0x83, 0xE4, 0xF0, 0x48, 0x83, 0xEC, 0x20, 0x48, 0xB8 });              // align, shadow space, mov rax, helper
        e.imm64(reinterpret_cast<std::uint64_t>(site.realId
            ? &equip::DevelopFieldRealId
            : &equip::DevelopFieldForEngine));
        e.b({ 0xFF, 0xD0, 0x48, 0x89, 0xEC, 0x5D, 0x89, 0xC0 });                          // call / restore rsp / pop rbp / zero-extend eax
        if (volatileDst)
            e.b({ 0x48, 0x89, 0x44, 0x24, static_cast<std::uint8_t>(SavedSlot(dst)) });   // result lands in dst's saved slot
        else if (dst == 3)
            e.b({ 0x89, 0xC3 });                                                          // mov ebx, eax
        else
            e.b({ 0x41, 0x89, 0xC7 });                                                    // mov r15d, eax
        e.b({ 0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59, 0x41, 0x58, 0x5A, 0x59, 0x58, 0x9D, 0xC3 });
        len = e.n;
        return true;
    }

    const DevelopReadSite* SitesForBuild(std::size_t& count)
    {
        switch (gGameBuild)
        {
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4:
            count = sizeof(kSitesEn154) / sizeof(kSitesEn154[0]);
            return kSitesEn154;
        default:
            count = 0;
            return nullptr;
        }
    }

    bool PatchSite(const DevelopReadSite& site)
    {
        auto* at = static_cast<std::uint8_t*>(ResolveGameAddress(site.movzx));
        auto* shr = site.shr16 ? static_cast<std::uint8_t*>(ResolveGameAddress(site.shr16)) : nullptr;
        const std::uint8_t* shrBytes = (shr && shr[2] == 0xE9) ? kShrCx4 : kShrAx4;
        if (!at || !BytesMatchSEH(at, site.movzxBytes, site.movzxLen)
            || (shr && !BytesMatchSEH(shr, shrBytes, sizeof(kShrAx4))))
            return false;

        const bool joined = shr && shr == at + site.movzxLen;
        const std::size_t span = joined ? site.movzxLen + sizeof(kShrAx4) : site.movzxLen;
        if (span < 5)
            return false;

        constexpr std::size_t kThunkCap = 96;
        auto* thunk = static_cast<std::uint8_t*>(
            HookArena::AllocateNear(reinterpret_cast<std::uintptr_t>(at), kThunkCap));
        std::size_t thunkLen = 0;
        if (!thunk || !EmitThunk(site, thunk, thunkLen) || thunkLen > kThunkCap)
            return false;
        FlushInstructionCache(GetCurrentProcess(), thunk, thunkLen);

        const std::int64_t rel = reinterpret_cast<std::int64_t>(thunk)
            - reinterpret_cast<std::int64_t>(at + 5);
        if (rel < INT32_MIN || rel > INT32_MAX)
            return false;

        AppliedSite applied{};
        applied.at = at;
        applied.len = span;
        std::memcpy(applied.saved, at, span);
        std::uint8_t patch[16];
        std::memset(patch, 0x90, sizeof(patch));
        patch[0] = 0xE8;
        const auto rel32 = static_cast<std::int32_t>(rel);
        std::memcpy(patch + 1, &rel32, 4);
        if (!WriteCode(at, patch, span))
            return false;
        if (shr && !joined)
        {
            applied.shr = shr;
            std::memcpy(applied.savedShr, shr, sizeof(applied.savedShr));
            const std::uint8_t nops[4] = { 0x90, 0x90, 0x90, 0x90 };
            WriteCode(shr, nops, sizeof(nops));
        }
        g_Applied.push_back(applied);
        return true;
    }

    std::uint16_t __fastcall hkGetHighestGradeEquipId(void* self, std::uint16_t equipId, std::uint8_t newest)
    {
        return g_OrigHighestGrade(self, static_cast<std::uint16_t>(equip::ResolveReal(equipId)), newest);
    }
}

bool Install_EquipDevelopControllerImpl_GetEquipIDFromDevelopIndex()
{
    std::size_t count = 0;
    const DevelopReadSite* sites = SitesForBuild(count);
    if (!sites)
        return true;

    std::size_t failed = 0;
    for (std::size_t i = 0; i < count; ++i)
        if (!PatchSite(sites[i]))
            ++failed;
    if (failed)
        LogError("EquipAlias: %zu of %zu develop row readers not patched on %s, weapons from equipId 0x1000 misread in R&D",
            failed, count, GetGameBuildName(gGameBuild));

    void* fn = ResolveGameAddress(kHighestGradeEn154);
    if (fn && BytesMatchSEH(fn, kHighestGradePrologue, sizeof(kHighestGradePrologue))
        && CreateAndEnableHook(fn, reinterpret_cast<void*>(&hkGetHighestGradeEquipId),
                               reinterpret_cast<void**>(&g_OrigHighestGrade)))
        g_HighestGradeAddr = fn;
    else
    {
        LogError("EquipAlias: GetHighestGradeEuipId hook failed on %s, upgraded wide weapons fall back to their own grade",
            GetGameBuildName(gGameBuild));
        ++failed;
    }
    return failed == 0;
}

void Uninstall_EquipDevelopControllerImpl_GetEquipIDFromDevelopIndex()
{
    if (g_HighestGradeAddr)
    {
        DisableAndRemoveHook(g_HighestGradeAddr);
        g_HighestGradeAddr = nullptr;
    }
    g_OrigHighestGrade = nullptr;
    for (const AppliedSite& applied : g_Applied)
    {
        WriteCode(applied.at, applied.saved, applied.len);
        if (applied.shr)
            WriteCode(applied.shr, applied.savedShr, sizeof(applied.savedShr));
    }
    g_Applied.clear();
}
