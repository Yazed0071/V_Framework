#include "pch.h"

#include "ItemSelectorCallbackImpl_SetupPrefabListElement.h"

#include <cstdint>

#include "AddressSet.h"
#include "BuddyCodeCave.h"
#include "BuddyOutfitRegistry.h"
#include "HookUtils.h"

namespace
{
    BuddyCodeCave g_CostumeGate{ "Quiet costume list gate" };
    BuddyCodeCave g_WeaponGate{ "Quiet weapon list gate" };

    constexpr std::uint8_t kCostumeGateBytes[] =
    {
        0x05, 0xC8, 0x38, 0xFF, 0xFF, // add eax, -51000 (eax = row developId)
        0x83, 0xF8, 0x05,             // cmp eax, 5
        0x77, 0x2A                    // ja to the next row
    };

    constexpr std::uint8_t kWeaponGateBytes[] =
    {
        0x05, 0x36, 0xE8, 0xFF, 0xFF, // add eax, -6090 (eax = row developId)
        0x83, 0xF8, 0x04,             // cmp eax, 4
        0x77, 0x2A                    // ja to the next row
    };

    bool InstallGate(BuddyCodeCave& cave, std::uintptr_t site, const std::uint8_t (&bytes)[10],
                     buddyoutfit::Kind kind)
    {
        const std::uintptr_t keep = site ? reinterpret_cast<std::uintptr_t>(ResolveGameAddress(site)) + sizeof(bytes) : 0;
        const std::uintptr_t skip = keep + bytes[sizeof(bytes) - 1];

        CaveCode c;
        c.Emit({ 0x8D, 0x88, bytes[1], bytes[2], bytes[3], bytes[4] }); // lea ecx, [rax-firstVanillaId]
        c.Emit({ 0x83, 0xF9, bytes[7] });                               // cmp ecx, lastVanilla
        const std::size_t toKeep1 = c.Jcc8(0x76);                       // jbe keep
        c.Emit({ 0x48, 0xBA });                                         // mov rdx, registered developId bits
        c.Imm64(reinterpret_cast<std::uint64_t>(buddyoutfit::DevelopIdBits(kind)));
        c.Emit({ 0x0F, 0xA3, 0x02 });                                   // bt [rdx], eax
        const std::size_t toKeep2 = c.Jcc8(0x72);                       // jc keep
        c.JmpAbs(skip);
        c.Bind8(toKeep1);
        c.Bind8(toKeep2);
        c.JmpAbs(keep);
        return InstallBuddyCodeCave(cave, site, bytes, sizeof(bytes), c);
    }
}

bool Install_ItemSelectorCallbackImpl_SetupPrefabListElement_BuddyPatches()
{
    bool ok = InstallGate(g_CostumeGate, gAddr.ItemSelector_QuietCostumeListGate, kCostumeGateBytes,
                          buddyoutfit::Kind::QuietCostume);
    ok &= InstallGate(g_WeaponGate, gAddr.ItemSelector_QuietWeaponListGate, kWeaponGateBytes,
                      buddyoutfit::Kind::QuietWeapon);
    return ok;
}

bool Uninstall_ItemSelectorCallbackImpl_SetupPrefabListElement_BuddyPatches()
{
    UninstallBuddyCodeCave(g_WeaponGate);
    UninstallBuddyCodeCave(g_CostumeGate);
    return true;
}
