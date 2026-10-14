#include "pch.h"

#include "DamageAiImpl_StepBitDamage.h"

#include <cstdint>

#include "AddressSet.h"
#include "BuddyCodeCave.h"
#include "BuddyOutfitRegistry.h"
#include "HookUtils.h"

namespace
{
    BuddyCodeCave g_BiteCave{ "D-Dog bite hold check" };

    constexpr std::uint8_t kBiteCheckBytes[] =
    {
        0x80, 0xB8, 0x49, 0x2E, 0x00, 0x00, 0x00 // cmp byte [rax+0x2E49], 0 (rax = vars, +0x2E49 = dog gear)
    };
}

bool Install_DamageAiImpl_StepBitDamage_Patch()
{
    const std::uintptr_t site = gAddr.DamageAiImpl_StepBitDamage_DogGearCheck;
    const std::uintptr_t next = site ? reinterpret_cast<std::uintptr_t>(ResolveGameAddress(site)) + sizeof(kBiteCheckBytes) : 0;

    CaveCode c;
    c.Emit({ 0x80, 0xB8, 0x49, 0x2E, 0x00, 0x00, 0x08 });           // cmp byte [rax+0x2E49], 8
    const std::size_t toCustom = c.Jcc8(0x73);                      // jae custom
    c.Emit({ 0x80, 0xB8, 0x49, 0x2E, 0x00, 0x00, 0x00 });           // cmp byte [rax+0x2E49], 0
    c.JmpAbs(next);
    c.Bind8(toCustom);
    c.Emit({ 0x50, 0x51 });                                         // push rax, push rcx
    c.Emit({ 0x0F, 0xB6, 0x88, 0x49, 0x2E, 0x00, 0x00 });           // movzx ecx, byte [rax+0x2E49]
    c.Emit({ 0x48, 0xB8 });                                         // mov rax, holds-the-bite bits
    c.Imm64(reinterpret_cast<std::uint64_t>(buddyoutfit::DogHoldsBiteBits()));
    c.Emit({ 0x0F, 0xA3, 0x08 });                                   // bt [rax], ecx
    c.Emit({ 0x59, 0x58 });                                         // pop rcx, pop rax
    const std::size_t noHold = c.Jcc8(0x73);                        // jnc no hold
    c.Emit({ 0x39, 0xC0 });                                         // cmp eax, eax: ZF set, the soldier stays pinned like under NAKED
    c.JmpAbs(next);
    c.Bind8(noHold);
    c.Emit({ 0x48, 0x85, 0xE4 });                                   // test rsp, rsp: ZF clear
    c.JmpAbs(next);
    return InstallBuddyCodeCave(g_BiteCave, site, kBiteCheckBytes, sizeof(kBiteCheckBytes), c);
}

bool Uninstall_DamageAiImpl_StepBitDamage_Patch()
{
    UninstallBuddyCodeCave(g_BiteCave);
    return true;
}
