#include <pch.h>
#include "PickUpActionPluginImpl_CheckLoadableForAmmo.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookArena.h"
#include "HookUtils.h"
#include "NativeCallPatch.h"
#include "log.h"
#include "../buddy/BuddyCodeCave.h"
#include "../equip/EquipAliasLease.h"

namespace
{
    constexpr std::uint32_t kCompareOffset = 0x147; // cmp edi,[rsp+0x80]: the slot's 11-bit equipId against the loadout cell, supply-drop boxes only
    constexpr std::uint32_t kJneOffset     = 0x14E; // other paths jump straight here, so it stays untouched

    const std::uint8_t kPrologue[] = {
        0x48, 0x89, 0x54, 0x24, 0x10, 0x53, 0x56, 0x41, 0x54, 0x41, 0x55,
        0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x28
    };
    const std::uint8_t kCompareAndJne[] = {
        0x3B, 0xBC, 0x24, 0x80, 0x00, 0x00, 0x00, 0x0F, 0x85
    };

    NativeCodePatch g_Patch{};

    std::uintptr_t CheckLoadableForAmmoAddr()
    {
        return gAddr.PickUpActionPluginImpl_CheckLoadableForAmmo;
    }

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

    std::uint32_t __fastcall LoadoutWeaponDiffers(std::uint32_t slotEquipId, std::uint32_t loadoutCell)
    {
        if (slotEquipId == loadoutCell)
            return 0;
        return static_cast<std::uint32_t>(equip::FromEngine(static_cast<std::int32_t>(slotEquipId))) == loadoutCell
            ? 0u : 1u;
    }

    CaveCode BuildCompareThunk(std::uintptr_t jne)
    {
        CaveCode code;
        code.Emit({ 0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53, 0x50 }); // push rcx rdx r8 r9 r10 r11 rax
        code.Emit({ 0x48, 0x81, 0xEC, 0x88, 0x00, 0x00, 0x00 });                         // sub rsp,0x88
        for (std::uint8_t x = 0; x < 6; ++x)
            code.Emit({ 0xF3, 0x0F, 0x7F, static_cast<std::uint8_t>(0x44 | (x << 3)), 0x24,
                        static_cast<std::uint8_t>(0x20 + x * 0x10) });                     // movdqu [rsp+0x20+x*0x10],xmmx
        code.Emit({ 0x89, 0xF9 });                                                         // mov ecx,edi
        code.Emit({ 0x8B, 0x94, 0x24, 0x40, 0x01, 0x00, 0x00 });                         // mov edx,[rsp+0x140] = the original [rsp+0x80]
        code.Emit({ 0x48, 0xB8 });                                                         // mov rax,LoadoutWeaponDiffers
        code.Imm64(reinterpret_cast<std::uintptr_t>(&LoadoutWeaponDiffers));
        code.Emit({ 0xFF, 0xD0 });                                                         // call rax
        code.Emit({ 0x85, 0xC0 });                                                         // test eax,eax: ZF set when the ids name the same weapon
        for (std::uint8_t x = 0; x < 6; ++x)
            code.Emit({ 0xF3, 0x0F, 0x6F, static_cast<std::uint8_t>(0x44 | (x << 3)), 0x24,
                        static_cast<std::uint8_t>(0x20 + x * 0x10) });                     // movdqu xmmx,[rsp+0x20+x*0x10]
        code.Emit({ 0x48, 0x8D, 0xA4, 0x24, 0x88, 0x00, 0x00, 0x00 });                   // lea rsp,[rsp+0x88] keeps the flags
        code.Emit({ 0x58, 0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59, 0x41, 0x58, 0x5A, 0x59 }); // pop rax r11 r10 r9 r8 rdx rcx
        code.JmpAbs(jne);
        return code;
    }
}

bool Install_PickUpActionPluginImpl_CheckLoadableForAmmo()
{
    const std::uintptr_t addr = CheckLoadableForAmmoAddr();
    if (addr == 0)
        return true;

    auto* fn = static_cast<std::uint8_t*>(ResolveGameAddress(addr));
    const HookEntry entry = CheckHookEntry(fn, kPrologue, sizeof(kPrologue));
    if (entry == HookEntry::Unknown
        || !BytesAtSEH(fn + kCompareOffset, kCompareAndJne, sizeof(kCompareAndJne)))
    {
        LogError("EquipAlias: CheckLoadableForAmmo does not match %s, ammo resupply skips weapons with equipIds above 0x7FF",
            GetGameBuildName(gGameBuild));
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("EquipAlias: CheckLoadableForAmmo", fn);

    const std::uintptr_t site = reinterpret_cast<std::uintptr_t>(fn) + kCompareOffset;
    const CaveCode code = BuildCompareThunk(reinterpret_cast<std::uintptr_t>(fn) + kJneOffset);
    auto* thunk = static_cast<std::uint8_t*>(HookArena::AllocateNear(site, code.bytes.size()));
    const std::int64_t rel = thunk
        ? static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(thunk)) - static_cast<std::int64_t>(site + 5)
        : 0;
    if (!thunk || rel < INT32_MIN || rel > INT32_MAX)
    {
        LogError("EquipAlias: no code page near CheckLoadableForAmmo, ammo resupply skips weapons with equipIds above 0x7FF");
        return false;
    }
    std::memcpy(thunk, code.bytes.data(), code.bytes.size());
    FlushInstructionCache(GetCurrentProcess(), thunk, code.bytes.size());

    g_Patch = NativeCodePatch{};
    g_Patch.target = reinterpret_cast<void*>(site);
    g_Patch.size = 6;
    std::memcpy(g_Patch.expected, kCompareAndJne, 6);
    std::memset(g_Patch.replacement, 0x90, sizeof(g_Patch.replacement));
    g_Patch.replacement[0] = 0xE9;
    const std::int32_t rel32 = static_cast<std::int32_t>(rel);
    std::memcpy(g_Patch.replacement + 1, &rel32, sizeof(rel32));

    for (int attempt = 0; attempt < 50; ++attempt)
    {
        if (InstallNativeCodePatches(&g_Patch, 1))
            return true;
        if (!NativePatchErrorIsTransient(GetLastError()))
            break;
        Sleep(4);
    }
    LogError("EquipAlias: CheckLoadableForAmmo patch refused (%s), ammo resupply skips weapons with equipIds above 0x7FF",
        NativePatchErrorName(GetLastError()));
    return false;
}

void Uninstall_PickUpActionPluginImpl_CheckLoadableForAmmo()
{
    if (g_Patch.installed)
        RemoveNativeCodePatches(&g_Patch, 1);
}
