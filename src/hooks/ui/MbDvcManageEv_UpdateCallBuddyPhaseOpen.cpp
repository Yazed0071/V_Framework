#include <pch.h>

#include "MbDvcManageEv_UpdateCallBuddyPhaseOpen.h"

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "../buddy/BattleGearBuddy.h"
#include "../buddy/BuddyCodeCave.h"

namespace
{
    constexpr std::uint8_t kWalkerCallPhase = 0x0B;
    constexpr std::size_t  kPhaseJoin       = 0x15;   // jmp +0x0F after the 6 site bytes

    const std::uint8_t kPhaseSite[] = { 0x41, 0x8D, 0x51, 0x09, 0xEB, 0x0F };   // lea edx,[r9+9] ; jmp short +0xf

    BuddyCodeCave g_PhaseCave{ "Battle Gear iDroid call phase" };

    CaveCode PhaseCode(std::uintptr_t join)
    {
        CaveCode c;
        c.Emit({ 0x83, 0xFF, battleGear::kBuddyType });         // cmp edi, 5  (buddy type being called)
        const std::size_t notFive = c.Jcc8(0x75);
        c.Emit({ 0x50 });                                       // push rax
        c.Emit({ 0x48, 0xB8 });                                 // mov rax, &hostActive
        c.Imm64(reinterpret_cast<std::uint64_t>(battleGear::HostActiveFlag()));
        c.Emit({ 0x80, 0x38, 0x00 });                           // cmp byte [rax], 0
        c.Emit({ 0x58 });                                       // pop rax
        const std::size_t off = c.Jcc8(0x74);
        c.Emit({ 0xBA, kWalkerCallPhase, 0x00, 0x00, 0x00 });   // mov edx,0xb
        c.JmpAbs(join);
        c.Bind8(notFive);
        c.Bind8(off);
        c.Emit({ 0x41, 0x8D, 0x51, 0x09 });                     // lea edx,[r9+9]
        c.JmpAbs(join);
        return c;
    }
}

bool Install_MbDvcManageEv_UpdateCallBuddyPhaseOpen_Patch()
{
    if (g_PhaseCave.installed)
        return true;
    const std::uintptr_t site = gAddr.MbDvcManageEv_UpdateCallBuddyPhaseOpen_Phase;
    void* resolved = ResolveGameAddress(site);
    if (!resolved)
    {
        LogWarn("BattleGear: iDroid call phase site missing on this build, the call map uses the horse layout");
        return false;
    }
    return InstallBuddyCodeCave(g_PhaseCave, site, kPhaseSite, sizeof(kPhaseSite),
                                PhaseCode(reinterpret_cast<std::uintptr_t>(resolved) + kPhaseJoin));
}

void Uninstall_MbDvcManageEv_UpdateCallBuddyPhaseOpen_Patch()
{
    UninstallBuddyCodeCave(g_PhaseCave);
}
