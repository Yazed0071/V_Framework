#include <pch.h>

#include "WalkerGearBasicActionImpl_Execute.h"

#include <cstdint>

#include "AddressSet.h"
#include "BattleGearWeapons.h"
#include "BuddyCodeCave.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    BuddyCodeCave g_FacingRead{ "Battle Gear hull heading" };

    constexpr std::size_t kPatchSize = 12;
    constexpr std::size_t kResume    = 12;   // je on the weapon-ready test made before the site: the cave keeps the flags

    // mov eax, [rsi+0x2E0] (camera heading) / mov [rbx+0x1D8], eax (the heading the walker faces); rsi = comm, rbx = work
    constexpr std::uint8_t kFacingReadSite[] = { 0x8B, 0x86, 0xE0, 0x02, 0x00, 0x00, 0x89, 0x83, 0xD8, 0x01, 0x00, 0x00 };
}

bool Install_WalkerGearBasicActionImpl_Execute_Patch()
{
    const std::uintptr_t siteAbs = gAddr.WalkerGearBasicActionImpl_Execute_FacingRead;
    const auto base = reinterpret_cast<std::uintptr_t>(siteAbs ? ResolveGameAddress(siteAbs) : nullptr);
    if (!base)
    {
        LogError("BattleGear: walker facing read address missing on this build, in fire posture the hull turns with the camera at once");
        return false;
    }
    CaveCode c;
    c.Emit({ 0x9C });                                         // pushfq
    c.Emit({ 0x51 });                                         // push rcx
    c.Emit({ 0x48, 0xB9 });                                   // mov rcx, &hull steering
    c.Imm64(reinterpret_cast<std::uint64_t>(battleGearWeapons::HullSteeringSlot()));
    c.Emit({ 0x48, 0x3B, 0x31 });                             // cmp rsi, [rcx]
    const std::size_t notSteered = c.Jcc8(0x75);              // jne
    c.Emit({ 0x8B, 0x41, 0x08 });                             // mov eax, [rcx+8] (hull heading)
    const std::size_t store = c.Jcc8(0xEB);                   // jmp
    c.Bind8(notSteered);
    c.Emit({ 0x8B, 0x86, 0xE0, 0x02, 0x00, 0x00 });           // mov eax, [rsi+0x2E0]
    c.Bind8(store);
    c.Emit({ 0x59 });                                         // pop rcx
    c.Emit({ 0x9D });                                         // popfq
    c.Emit({ 0x89, 0x83, 0xD8, 0x01, 0x00, 0x00 });           // mov [rbx+0x1D8], eax
    c.JmpAbs(base + kResume);
    return InstallBuddyCodeCave(g_FacingRead, siteAbs, kFacingReadSite, kPatchSize, c);
}

void Uninstall_WalkerGearBasicActionImpl_Execute_Patch()
{
    UninstallBuddyCodeCave(g_FacingRead);
}
