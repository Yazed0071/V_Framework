#include "pch.h"

#include "BuddyDog2Impl_Initialize.h"

#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "BuddyCodeCave.h"
#include "BuddyOutfitRegistry.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    BuddyCodeCave g_GearCopyCave{ "D-Dog gear copy" };

    constexpr std::uint8_t kGearCopyBytes[] =
    {
        0x41, 0x0F, 0xB6, 0x81, 0x49, 0x2E, 0x00, 0x00,   // movzx eax, byte [r9+0x2E49]
        0x88, 0x86, 0xF4, 0x00, 0x00, 0x00,               // mov [rsi+0xF4], al
        0x45, 0x0F, 0xB6, 0x81, 0x6D, 0x2D, 0x00, 0x00,
    };
    constexpr std::size_t kPatchSize = 14;
}

bool Install_BuddyDog2Impl_Initialize_Patch()
{
    const std::uintptr_t siteAbs = gAddr.BuddyDog2Impl_Initialize_GearCopy;
    const auto* site = static_cast<const std::uint8_t*>(siteAbs ? ResolveGameAddress(siteAbs) : nullptr);
    if (!site || std::memcmp(site, kGearCopyBytes, sizeof(kGearCopyBytes)) != 0)
    {
        LogError("BuddyOutfit: %s code differs on this build, that part is off", g_GearCopyCave.name);
        return false;
    }

    CaveCode c;
    c.Emit({ 0x41, 0x0F, 0xB6, 0x81, 0x49, 0x2E, 0x00, 0x00 });   // movzx eax, byte [r9+0x2E49]
    c.Emit({ 0x83, 0xF8, 0x08 });                                   // cmp eax, 8: custom gear stores its order's vanilla gear, the copy picks the knife attack
    const std::size_t store = c.Jcc8(0x72);                        // jb store
    c.Emit({ 0x51 });                                               // push rcx
    c.Emit({ 0x48, 0xB9 });                                         // mov rcx, order gear table
    c.Imm64(reinterpret_cast<std::uint64_t>(buddyoutfit::DogCommandGears()));
    c.Emit({ 0x0F, 0xB6, 0x04, 0x01 });                             // movzx eax, byte [rcx+rax]
    c.Emit({ 0x59 });                                               // pop rcx
    c.Bind8(store);
    c.Emit({ 0x88, 0x86, 0xF4, 0x00, 0x00, 0x00 });                 // mov [rsi+0xF4], al
    c.JmpAbs(reinterpret_cast<std::uintptr_t>(site) + kPatchSize);

    return InstallBuddyCodeCave(g_GearCopyCave, siteAbs, kGearCopyBytes, kPatchSize, c);
}

bool Uninstall_BuddyDog2Impl_Initialize_Patch()
{
    UninstallBuddyCodeCave(g_GearCopyCave);
    return true;
}
