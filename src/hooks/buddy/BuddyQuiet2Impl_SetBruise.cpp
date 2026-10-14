#include "pch.h"

#include "BuddyQuiet2Impl_SetBruise.h"

#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "BuddyCodeCave.h"
#include "BuddyOutfitRegistry.h"
#include "HookUtils.h"

namespace
{
    BuddyCodeCave g_Cave{ "Quiet bruise costume check" };

    constexpr std::uint8_t kCheckBytes[] =
    {
        0x41, 0x80, 0xB9, 0x46, 0x2E, 0x00, 0x00, 0x00, // cmp byte [r9+0x2E46], 0 (r9 = vars, +0x2E46 = costume)
        0x0F, 0x85, 0x88, 0x00, 0x00, 0x00              // jne past the bruise
    };
}

bool Install_BuddyQuiet2Impl_SetBruise_Patch()
{
    const std::uintptr_t site = gAddr.BuddyQuiet2Impl_SetBruise_CostumeCheck;
    const std::uintptr_t next = site ? reinterpret_cast<std::uintptr_t>(ResolveGameAddress(site)) + sizeof(kCheckBytes) : 0;
    std::int32_t rel = 0;
    std::memcpy(&rel, kCheckBytes + 10, sizeof(rel));
    const std::uintptr_t skip = next + rel;

    CaveCode c;
    c.Emit({ 0x45, 0x0F, 0xB6, 0x91, 0x46, 0x2E, 0x00, 0x00 });     // movzx r10d, byte [r9+0x2E46]
    c.Emit({ 0x45, 0x85, 0xD2 });                                   // test r10d, r10d
    const std::size_t toBruise1 = c.Jcc8(0x74);                     // jz bruise
    c.Emit({ 0x49, 0xBB });                                         // mov r11, costumes that bruise
    c.Imm64(reinterpret_cast<std::uint64_t>(buddyoutfit::BruiseValueBits()));
    c.Emit({ 0x45, 0x0F, 0xA3, 0x13 });                             // bt [r11], r10d
    const std::size_t toBruise2 = c.Jcc8(0x72);                     // jc bruise
    c.JmpAbs(skip);
    c.Bind8(toBruise1);
    c.Bind8(toBruise2);
    c.JmpAbs(next);

    return InstallBuddyCodeCave(g_Cave, site, kCheckBytes, sizeof(kCheckBytes), c);
}

bool Uninstall_BuddyQuiet2Impl_SetBruise_Patch()
{
    UninstallBuddyCodeCave(g_Cave);
    return true;
}
