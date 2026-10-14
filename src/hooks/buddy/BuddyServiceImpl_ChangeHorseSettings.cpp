#include "pch.h"

#include "BuddyServiceImpl_ChangeHorseSettings.h"

#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "BuddyCodeCave.h"
#include "BuddyOutfitRegistry.h"
#include "HookUtils.h"

namespace
{
    BuddyCodeCave g_Cave{ "D-Horse tack check" };

    constexpr std::uint8_t kCheckBytes[] =
    {
        0x40, 0x80, 0xFE, 0x07,             // cmp sil, 7
        0x0F, 0x83, 0x8E, 0x00, 0x00, 0x00  // jae to the early return
    };
}

bool Install_BuddyServiceImpl_ChangeHorseSettings_Patch()
{
    const std::uintptr_t site = gAddr.BuddyServiceImpl_ChangeHorseSettings_TypeCheck;
    const std::uintptr_t next = site ? reinterpret_cast<std::uintptr_t>(ResolveGameAddress(site)) + sizeof(kCheckBytes) : 0;
    std::int32_t rel = 0;
    std::memcpy(&rel, kCheckBytes + 6, sizeof(rel));
    const std::uintptr_t reject = next + rel;

    CaveCode c;
    c.Emit({ 0x40, 0x80, 0xFE, 0x07 });                             // cmp sil, 7
    const std::size_t toOk1 = c.Jcc8(0x72);                         // jb ok
    c.Emit({ 0x49, 0xBA });                                         // mov r10, registered tack bits
    c.Imm64(reinterpret_cast<std::uint64_t>(buddyoutfit::ValueBits(buddyoutfit::Kind::HorseTack)));
    c.Emit({ 0x44, 0x0F, 0xB6, 0xDE });                             // movzx r11d, sil
    c.Emit({ 0x45, 0x0F, 0xA3, 0x1A });                             // bt [r10], r11d
    const std::size_t toOk2 = c.Jcc8(0x72);                         // jc ok
    c.JmpAbs(reject);
    c.Bind8(toOk1);
    c.Bind8(toOk2);
    c.JmpAbs(next);

    return InstallBuddyCodeCave(g_Cave, site, kCheckBytes, sizeof(kCheckBytes), c);
}

bool Uninstall_BuddyServiceImpl_ChangeHorseSettings_Patch()
{
    UninstallBuddyCodeCave(g_Cave);
    return true;
}
