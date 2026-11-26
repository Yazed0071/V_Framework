#include <pch.h>

#include "BuddyServiceImpl_ChangeGearSubWeaponSettings.h"

#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "BuddyCodeCave.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    BuddyCodeCave g_Cave{ "D-Walker sidearm type limit" };

    constexpr std::uint8_t kSiteBytes[] =
    {
        0x80, 0xFA, 0x07,             // cmp dl, 7
        0x44, 0x0F, 0x43, 0xC8,       // cmovae r9d, eax (eax = 0): sidearm types above 6 become 0
        0x48, 0x8B, 0x01, 0x41, 0x0F, 0xB6, 0xF8, 0x41, 0x0F, 0xB6, 0xD1,
    };

    constexpr std::size_t kPatchSize = 7;
}

bool Install_BuddyServiceImpl_ChangeGearSubWeaponSettings_Patch()
{
    const std::uintptr_t siteAbs = gAddr.BuddyServiceImpl_ChangeGearSubWeaponSettings_TypeLimit;
    const auto* site = static_cast<const std::uint8_t*>(siteAbs ? ResolveGameAddress(siteAbs) : nullptr);
    if (!site || std::memcmp(site, kSiteBytes, sizeof(kSiteBytes)) != 0)
    {
        LogError("BuddyOutfit: %s code differs on this build, custom D-Walker sidearms off", g_Cave.name);
        return false;
    }

    CaveCode c;
    c.JmpAbs(reinterpret_cast<std::uintptr_t>(site) + kPatchSize); // r9d already holds the type
    return InstallBuddyCodeCave(g_Cave, siteAbs, kSiteBytes, kPatchSize, c);
}

bool Uninstall_BuddyServiceImpl_ChangeGearSubWeaponSettings_Patch()
{
    UninstallBuddyCodeCave(g_Cave);
    return true;
}
