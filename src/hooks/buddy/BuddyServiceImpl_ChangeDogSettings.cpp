#include "pch.h"

#include "BuddyServiceImpl_ChangeDogSettings.h"

#include <cstdint>

#include "AddressSet.h"
#include "BuddyCodeCave.h"
#include "BuddyOutfitRegistry.h"
#include "HookUtils.h"

namespace
{
    BuddyCodeCave g_GearCave{ "D-Dog gear check" };

    constexpr std::uint8_t kGearCheckBytes[] =
    {
        0x40, 0x80, 0xFF, 0x08, // cmp dil, 8
        0x73, 0x58              // jae to the early return
    };
}

bool Install_BuddyServiceImpl_ChangeDogSettings_Patch()
{
    const std::uintptr_t site = gAddr.BuddyServiceImpl_ChangeDogSettings_GearCheck;
    const std::uintptr_t next = site ? reinterpret_cast<std::uintptr_t>(ResolveGameAddress(site)) + sizeof(kGearCheckBytes) : 0;
    const std::uintptr_t reject = next + kGearCheckBytes[sizeof(kGearCheckBytes) - 1];

    CaveCode c;
    c.Emit({ 0x40, 0x80, 0xFF, 0x08 });                             // cmp dil, 8
    const std::size_t toOk1 = c.Jcc8(0x72);                         // jb ok
    c.Emit({ 0x49, 0xBA });                                         // mov r10, registered gear bits
    c.Imm64(reinterpret_cast<std::uint64_t>(buddyoutfit::ValueBits(buddyoutfit::Kind::DogGear)));
    c.Emit({ 0x44, 0x0F, 0xB6, 0xDF });                             // movzx r11d, dil
    c.Emit({ 0x45, 0x0F, 0xA3, 0x1A });                             // bt [r10], r11d
    const std::size_t toOk2 = c.Jcc8(0x72);                         // jc ok
    c.JmpAbs(reject);
    c.Bind8(toOk1);
    c.Bind8(toOk2);
    c.JmpAbs(next);
    return InstallBuddyCodeCave(g_GearCave, site, kGearCheckBytes, sizeof(kGearCheckBytes), c);
}

bool Uninstall_BuddyServiceImpl_ChangeDogSettings_Patch()
{
    UninstallBuddyCodeCave(g_GearCave);
    return true;
}
