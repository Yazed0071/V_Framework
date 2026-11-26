#include <pch.h>
#include "ShellImpl_UpdateAction.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HeliKey.h"
#include "HookArena.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "NativeCallPatch.h"
#include "SupportHeliWeapon.h"
#include "log.h"
#include "../buddy/BuddyCodeCave.h"

namespace
{
    constexpr std::size_t kRecordOwner = 0x18;

    const std::uint8_t kUpdateActionEntry[] =
    {
        0x4C, 0x8B, 0xDC, 0x55, 0x41, 0x56, 0x41, 0x57, 0x49, 0x8D, 0xAB, 0x68, 0xFD, 0xFF, 0xFF, 0x48,
    };
    constexpr std::uint32_t kShellIdLoad     = 0x1F1;
    constexpr std::uint32_t kShellIdLoadSize = 5;
    const std::uint8_t kShellIdLoadBytes[] =
    {
        0x44, 0x0F, 0xB7, 0x6E, 0x14,                  // movzx r13d,[rsi+0x14]: the shell id the impact switch reads
        0x41, 0xB9, 0xFF, 0x03, 0x00, 0x00,            // mov r9d,0x3FF
        0x41, 0x81, 0xFD, 0x2C, 0x01, 0x00, 0x00,      // cmp r13d,0x12C
    };

    NativeCodePatch g_Patch{};

    bool BytesAtSEH(const std::uint8_t* at, const std::uint8_t* bytes, std::size_t size)
    {
        __try
        {
            return std::memcmp(at, bytes, size) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ReadOwnerSEH(const std::uint8_t* record, std::uint16_t& owner)
    {
        __try
        {
            owner = *reinterpret_cast<const std::uint16_t*>(record + kRecordOwner);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint32_t __fastcall CloudShellForPequodShell(const std::uint8_t* record)
    {
        std::uint16_t owner = 0;
        if (MissionCodeGuard::ShouldBypassHooks() || !ReadOwnerSEH(record, owner) || (owner >> 9) != HeliKey::kFirstType)
            return 0;
        SupportHeliWeapon::Weapon weapon{};
        if (!SupportHeliWeapon::GetSortieWeapon(weapon) || weapon.cloudShell == 0)
            return 0;
        return weapon.cloudShell;
    }

    CaveCode BuildShellIdThunk(std::uintptr_t resume)
    {
        CaveCode code;
        code.Emit({ 0x44, 0x0F, 0xB7, 0x6E, 0x14 });                            // movzx r13d,[rsi+0x14]
        code.Emit({ 0x41, 0x81, 0xFD });                                        // cmp r13d,EQP_BL_uth0_ammo0
        code.Emit({ 0x33, 0x01, 0x00, 0x00 });
        const std::size_t missileShell = code.Jcc8(0x74);
        code.Emit({ 0x41, 0x81, 0xFD });                                        // cmp r13d,EQP_BL_uth0_ammo1
        code.Emit({ 0x34, 0x01, 0x00, 0x00 });
        const std::size_t rocketShell = code.Jcc8(0x74);
        code.JmpAbs(resume);

        code.Bind8(missileShell);
        code.Bind8(rocketShell);
        code.Emit({ 0x50, 0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53 });   // push rax rcx rdx r8 r9 r10 r11
        code.Emit({ 0x55, 0x48, 0x8B, 0xEC });                                  // push rbp; mov rbp,rsp
        code.Emit({ 0x48, 0x83, 0xE4, 0xF0 });                                  // and rsp,-16
        code.Emit({ 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00 });                // sub rsp,0x80
        code.Emit({ 0xF3, 0x0F, 0x7F, 0x44, 0x24, 0x20 });                      // movdqu [rsp+0x20..0x70],xmm0-5
        code.Emit({ 0xF3, 0x0F, 0x7F, 0x4C, 0x24, 0x30 });
        code.Emit({ 0xF3, 0x0F, 0x7F, 0x54, 0x24, 0x40 });
        code.Emit({ 0xF3, 0x0F, 0x7F, 0x5C, 0x24, 0x50 });
        code.Emit({ 0xF3, 0x0F, 0x7F, 0x64, 0x24, 0x60 });
        code.Emit({ 0xF3, 0x0F, 0x7F, 0x6C, 0x24, 0x70 });
        code.Emit({ 0x48, 0x8B, 0xCE });                                        // mov rcx,rsi: the shell record
        code.Emit({ 0x48, 0xB8 });                                              // mov rax,CloudShellForPequodShell
        code.Imm64(reinterpret_cast<std::uintptr_t>(&CloudShellForPequodShell));
        code.Emit({ 0xFF, 0xD0 });                                              // call rax
        code.Emit({ 0x85, 0xC0 });                                              // test eax,eax
        const std::size_t keepShell = code.Jcc8(0x74);
        code.Emit({ 0x44, 0x8B, 0xE8 });                                        // mov r13d,eax
        code.Bind8(keepShell);
        code.Emit({ 0xF3, 0x0F, 0x6F, 0x44, 0x24, 0x20 });                      // movdqu xmm0-5,[rsp+0x20..0x70]
        code.Emit({ 0xF3, 0x0F, 0x6F, 0x4C, 0x24, 0x30 });
        code.Emit({ 0xF3, 0x0F, 0x6F, 0x54, 0x24, 0x40 });
        code.Emit({ 0xF3, 0x0F, 0x6F, 0x5C, 0x24, 0x50 });
        code.Emit({ 0xF3, 0x0F, 0x6F, 0x64, 0x24, 0x60 });
        code.Emit({ 0xF3, 0x0F, 0x6F, 0x6C, 0x24, 0x70 });
        code.Emit({ 0x48, 0x8B, 0xE5, 0x5D });                                  // mov rsp,rbp; pop rbp
        code.Emit({ 0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59, 0x41, 0x58, 0x5A, 0x59, 0x58 });   // pop r11 r10 r9 r8 rdx rcx rax
        code.JmpAbs(resume);
        return code;
    }
}

bool Install_ShellImpl_UpdateAction_Patch()
{
    if (g_Patch.installed)
        return true;
    auto* fn = static_cast<std::uint8_t*>(gAddr.ShellImpl_UpdateAction ? ResolveGameAddress(gAddr.ShellImpl_UpdateAction) : nullptr);
    const HookEntry entry = fn ? CheckHookEntry(fn, kUpdateActionEntry, sizeof(kUpdateActionEntry)) : HookEntry::Unknown;
    if (entry == HookEntry::Unknown || !BytesAtSEH(fn + kShellIdLoad, kShellIdLoadBytes, sizeof(kShellIdLoadBytes)))
    {
        LogError("SupportHeliWeapon: shell impact code differs on %s, Pequod weapons with a gas or smoke blastId explode instead",
            GetGameBuildName(gGameBuild));
        return false;
    }

    const std::uintptr_t site = reinterpret_cast<std::uintptr_t>(fn) + kShellIdLoad;
    const CaveCode code = BuildShellIdThunk(site + kShellIdLoadSize);
    auto* thunk = static_cast<std::uint8_t*>(HookArena::AllocateNear(site, code.bytes.size()));
    const std::int64_t rel = thunk
        ? static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(thunk)) - static_cast<std::int64_t>(site + 5)
        : 0;
    if (!thunk || rel < INT32_MIN || rel > INT32_MAX)
    {
        LogError("SupportHeliWeapon: no code page near the shell impact code, Pequod weapons with a gas or smoke blastId explode instead");
        return false;
    }
    std::memcpy(thunk, code.bytes.data(), code.bytes.size());
    FlushInstructionCache(GetCurrentProcess(), thunk, code.bytes.size());

    g_Patch = NativeCodePatch{};
    g_Patch.target = reinterpret_cast<void*>(site);
    g_Patch.size = kShellIdLoadSize;
    std::memcpy(g_Patch.expected, fn + kShellIdLoad, kShellIdLoadSize);
    g_Patch.replacement[0] = 0xE9;
    const std::int32_t rel32 = static_cast<std::int32_t>(rel);
    std::memcpy(g_Patch.replacement + 1, &rel32, sizeof(rel32));

    for (int attempt = 0; attempt < 50; ++attempt)
    {
        if (InstallNativeCodePatches(&g_Patch, 1))
        {
            if (entry == HookEntry::ForeignHook)
                LogHookedOnTop("SupportHeliWeapon: shell impact code", fn);
            return true;
        }
        if (!NativePatchErrorIsTransient(GetLastError()))
            break;
        Sleep(4);
    }
    LogError("SupportHeliWeapon: shell impact patch refused (%s), Pequod weapons with a gas or smoke blastId explode instead",
        NativePatchErrorName(GetLastError()));
    return false;
}

void Uninstall_ShellImpl_UpdateAction_Patch()
{
    if (g_Patch.installed)
        RemoveNativeCodePatches(&g_Patch, 1);
}
