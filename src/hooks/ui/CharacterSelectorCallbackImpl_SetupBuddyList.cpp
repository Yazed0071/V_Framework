#include <pch.h>

#include "CharacterSelectorCallbackImpl_SetupBuddyList.h"

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "../buddy/BattleGearBuddy.h"
#include "../buddy/BuddyCodeCave.h"

namespace
{
    constexpr std::size_t kAskJoin  = 0x08;   // mov rcx,[rsi+0x120]: ask CanSortieBuddyType for this type
    constexpr std::size_t kSkipJoin = 0x62;   // inc ebx: next type, the vanilla jbe target

    const std::uint8_t kTypeSkipSite[] = { 0x8D, 0x43, 0xFB, 0x83, 0xF8, 0x01, 0x76, 0x5A };   // lea eax,[rbx-5] ; cmp eax,1 ; jbe +0x5a

    BuddyCodeCave g_TypeSkipCave{ "Battle Gear Sortie Prep row" };

    CaveCode TypeSkipCode(std::uintptr_t ask, std::uintptr_t skip)
    {
        CaveCode c;
        c.Emit({ 0x8D, 0x43, 0xFB });                           // lea eax,[rbx-5]
        c.Emit({ 0x83, 0xF8, 0x01 });                           // cmp eax,1
        const std::size_t vanillaType = c.Jcc8(0x77);           // ja: types 0-4 wrap above 1
        const std::size_t typeSix = c.Jcc8(0x74);               // je: type 6 stays skipped
        c.Emit({ 0x48, 0xB8 });                                 // mov rax, &hostActive
        c.Imm64(reinterpret_cast<std::uint64_t>(battleGear::HostActiveFlag()));
        c.Emit({ 0x80, 0x38, 0x00 });                           // cmp byte [rax], 0
        const std::size_t off = c.Jcc8(0x74);
        c.Bind8(vanillaType);
        c.JmpAbs(ask);
        c.Bind8(typeSix);
        c.Bind8(off);
        c.JmpAbs(skip);
        return c;
    }
}

bool Install_CharacterSelectorCallbackImpl_SetupBuddyList_Patch()
{
    if (g_TypeSkipCave.installed)
        return true;
    const std::uintptr_t site = gAddr.CharacterSelectorCallbackImpl_SetupBuddyList_TypeSkip;
    void* resolved = ResolveGameAddress(site);
    if (!resolved)
    {
        LogWarn("BattleGear: Sortie Prep buddy list site missing on this build, Battle Gear is not offered in Sortie Prep");
        return false;
    }
    const std::uintptr_t at = reinterpret_cast<std::uintptr_t>(resolved);
    return InstallBuddyCodeCave(g_TypeSkipCave, site, kTypeSkipSite, sizeof(kTypeSkipSite),
                                TypeSkipCode(at + kAskJoin, at + kSkipJoin));
}

void Uninstall_CharacterSelectorCallbackImpl_SetupBuddyList_Patch()
{
    UninstallBuddyCodeCave(g_TypeSkipCave);
}
