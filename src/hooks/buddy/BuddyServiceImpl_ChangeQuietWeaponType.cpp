#include "pch.h"

#include "BuddyServiceImpl_ChangeQuietWeaponType.h"

#include <cstdint>

#include "AddressSet.h"
#include "BuddyCodeCave.h"
#include "BuddyOutfitRegistry.h"
#include "HookUtils.h"

namespace
{
    BuddyCodeCave g_ChangeCave{ "Quiet weapon clamp" };
    BuddyCodeCave g_SetVarsCave{ "Quiet weapon Lua setter clamp" };
    BuddyCodeCave g_CommandCave{ "Quiet weapon command clamp" };

    constexpr std::uint8_t kChangeClampBytes[] =
    {
        0x66, 0x83, 0xFF, 0x05, // cmp di, 5
        0x72, 0x02,             // jb past the reset
        0x33, 0xFF              // xor edi, edi
    };

    constexpr std::uint8_t kSetVarsClampBytes[] =
    {
        0x66, 0x83, 0xFF, 0x05, // cmp di, 5
        0x72, 0x05,             // jb past the reset
        0x33, 0xC9,             // xor ecx, ecx
        0x0F, 0xB7, 0xF9        // movzx edi, cx
    };

    constexpr std::uint8_t kCommandClampBytes[] =
    {
        0x41, 0x80, 0xFA, 0x05, // cmp r10b, 5 (eax already holds r10b)
        0x41, 0x0F, 0x43, 0xC4  // cmovae eax, r12d (r12d is 0 here)
    };

    std::uintptr_t After(std::uintptr_t site, std::size_t size)
    {
        return site ? reinterpret_cast<std::uintptr_t>(ResolveGameAddress(site)) + size : 0;
    }

    std::uint64_t WeaponBits()
    {
        return reinterpret_cast<std::uint64_t>(buddyoutfit::ValueBits(buddyoutfit::Kind::QuietWeapon));
    }

    CaveCode KeepRegisteredDi(std::uintptr_t resume)
    {
        CaveCode c;
        c.Emit({ 0x66, 0x83, 0xFF, 0x05 });                         // cmp di, 5
        const std::size_t toDone1 = c.Jcc8(0x72);                   // jb done
        c.Emit({ 0x66, 0x81, 0xFF, 0xFF, 0x00 });                   // cmp di, 0xFF
        const std::size_t toClear = c.Jcc8(0x73);                   // jae clear
        c.Emit({ 0x0F, 0xB7, 0xD7 });                               // movzx edx, di
        c.Emit({ 0x48, 0xB9 });                                     // mov rcx, registered weapon bits
        c.Imm64(WeaponBits());
        c.Emit({ 0x0F, 0xA3, 0x11 });                               // bt [rcx], edx
        const std::size_t toDone2 = c.Jcc8(0x72);                   // jc done
        c.Bind8(toClear);
        c.Emit({ 0x33, 0xFF });                                     // xor edi, edi
        c.Bind8(toDone1);
        c.Bind8(toDone2);
        c.JmpAbs(resume);
        return c;
    }
}

bool Install_BuddyServiceImpl_ChangeQuietWeaponType_Patch()
{
    const std::uintptr_t change = gAddr.BuddyServiceImpl_ChangeQuietWeaponType_Clamp;
    bool ok = InstallBuddyCodeCave(g_ChangeCave, change, kChangeClampBytes, sizeof(kChangeClampBytes),
                                   KeepRegisteredDi(After(change, sizeof(kChangeClampBytes))));

    const std::uintptr_t setVars = gAddr.BuddyServiceImpl_SetVarsQuietWeaponType_Clamp;
    ok &= InstallBuddyCodeCave(g_SetVarsCave, setVars, kSetVarsClampBytes, sizeof(kSetVarsClampBytes),
                               KeepRegisteredDi(After(setVars, sizeof(kSetVarsClampBytes))));

    const std::uintptr_t command = gAddr.BuddyQuiet2Impl_WeaponCommand_Clamp;
    CaveCode c;
    c.Emit({ 0x41, 0x80, 0xFA, 0x05 });                             // cmp r10b, 5
    const std::size_t toDone1 = c.Jcc8(0x72);                       // jb done
    c.Emit({ 0x48, 0xBB });                                         // mov rbx, registered weapon bits
    c.Imm64(WeaponBits());
    c.Emit({ 0x0F, 0xA3, 0x03 });                                   // bt [rbx], eax
    const std::size_t toDone2 = c.Jcc8(0x72);                       // jc done
    c.Emit({ 0x31, 0xC0 });                                         // xor eax, eax
    c.Bind8(toDone1);
    c.Bind8(toDone2);
    c.JmpAbs(After(command, sizeof(kCommandClampBytes)));
    ok &= InstallBuddyCodeCave(g_CommandCave, command, kCommandClampBytes, sizeof(kCommandClampBytes), c);
    return ok;
}

bool Uninstall_BuddyServiceImpl_ChangeQuietWeaponType_Patch()
{
    UninstallBuddyCodeCave(g_CommandCave);
    UninstallBuddyCodeCave(g_SetVarsCave);
    UninstallBuddyCodeCave(g_ChangeCave);
    return true;
}
