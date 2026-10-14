#include "pch.h"

#include "BuddyServiceImpl_ChangeQuietCostume.h"

#include <cstdint>

#include "AddressSet.h"
#include "BuddyCodeCave.h"
#include "BuddyOutfitRegistry.h"
#include "HookUtils.h"

namespace
{
    BuddyCodeCave g_Cave{ "Quiet costume clamp" };
    BuddyCodeCave g_SetVarsCave{ "Quiet costume Lua setter clamp" };

    constexpr std::uint8_t kClampBytes[] =
    {
        0x80, 0xFB, 0x06,       // cmp bl, 6
        0x44, 0x0F, 0x43, 0xC0  // cmovae r8d, eax (eax is 0 here)
    };

    constexpr std::uint8_t kSetVarsClampBytes[] =
    {
        0x80, 0xFB, 0x06,       // cmp bl, 6 (ecx already holds bl)
        0x0F, 0x43, 0xCA        // cmovae ecx, edx (edx is 0 here)
    };

    std::uintptr_t After(std::uintptr_t site, std::size_t size)
    {
        return site ? reinterpret_cast<std::uintptr_t>(ResolveGameAddress(site)) + size : 0;
    }

    std::uint64_t CostumeBits()
    {
        return reinterpret_cast<std::uint64_t>(buddyoutfit::ValueBits(buddyoutfit::Kind::QuietCostume));
    }
}

bool Install_BuddyServiceImpl_ChangeQuietCostume_Patch()
{
    CaveCode c;
    c.Emit({ 0x80, 0xFB, 0x06 });                                   // cmp bl, 6
    const std::size_t toDone1 = c.Jcc8(0x72);                       // jb done
    c.Emit({ 0x48, 0xB9 });                                         // mov rcx, registered costume bits
    c.Imm64(CostumeBits());
    c.Emit({ 0x0F, 0xA3, 0x19 });                                   // bt [rcx], ebx
    const std::size_t toDone2 = c.Jcc8(0x72);                       // jc done
    c.Emit({ 0x45, 0x33, 0xC0 });                                   // xor r8d, r8d
    c.Bind8(toDone1);
    c.Bind8(toDone2);
    c.JmpAbs(After(gAddr.BuddyServiceImpl_ChangeQuietCostume_Clamp, sizeof(kClampBytes)));
    bool ok = InstallBuddyCodeCave(g_Cave, gAddr.BuddyServiceImpl_ChangeQuietCostume_Clamp,
                                   kClampBytes, sizeof(kClampBytes), c);

    CaveCode s;
    s.Emit({ 0x80, 0xFB, 0x06 });                                   // cmp bl, 6
    const std::size_t sDone1 = s.Jcc8(0x72);                        // jb done
    s.Emit({ 0x48, 0xBA });                                         // mov rdx, registered costume bits
    s.Imm64(CostumeBits());
    s.Emit({ 0x0F, 0xA3, 0x0A });                                   // bt [rdx], ecx
    const std::size_t sDone2 = s.Jcc8(0x72);                        // jc done
    s.Emit({ 0x33, 0xC9 });                                         // xor ecx, ecx
    s.Bind8(sDone1);
    s.Bind8(sDone2);
    s.JmpAbs(After(gAddr.BuddyServiceImpl_SetVarsQuietCostume_Clamp, sizeof(kSetVarsClampBytes)));
    ok &= InstallBuddyCodeCave(g_SetVarsCave, gAddr.BuddyServiceImpl_SetVarsQuietCostume_Clamp,
                               kSetVarsClampBytes, sizeof(kSetVarsClampBytes), s);
    return ok;
}

bool Uninstall_BuddyServiceImpl_ChangeQuietCostume_Patch()
{
    UninstallBuddyCodeCave(g_SetVarsCave);
    UninstallBuddyCodeCave(g_Cave);
    return true;
}
