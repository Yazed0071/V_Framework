#include <pch.h>

#include "Buddy2BlockControllerImpl_GetBuddyGameObjectType.h"

#include <cstdint>

#include "AddressSet.h"
#include "BattleGearBuddy.h"
#include "BuddyCodeCave.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    constexpr std::uint8_t kWalkerObjectType = 0x2C;   // GAME_OBJECT_TYPE_WALKERGEAR2
    constexpr std::uint8_t kNoObjectType     = 0x5E;
    constexpr std::size_t  kProcSignalJoin   = 0x21;   // jmp +0x1A after the 7 site bytes

    const std::uint8_t kDefaultSite[]    = { 0xB8, 0x5E, 0x00, 0x00, 0x00, 0xC3 };               // mov eax,0x5e ; ret
    const std::uint8_t kProcSignalSite[] = { 0xBF, 0x5E, 0x00, 0x00, 0x00, 0xEB, 0x1A };         // mov edi,0x5e ; jmp short +0x1a

    BuddyCodeCave g_DefaultCave{ "Battle Gear object type" };
    BuddyCodeCave g_ProcSignalCave{ "Battle Gear buddy signal" };

    void EmitFlagTest(CaveCode& c, bool keepRax)
    {
        if (keepRax)
            c.Emit({ 0x50 });                                   // push rax
        c.Emit({ 0x48, 0xB8 });                                 // mov rax, &hostActive
        c.Imm64(reinterpret_cast<std::uint64_t>(battleGear::HostActiveFlag()));
        c.Emit({ 0x80, 0x38, 0x00 });                           // cmp byte [rax], 0
        if (keepRax)
            c.Emit({ 0x58 });                                   // pop rax
    }

    CaveCode DefaultCode()
    {
        CaveCode c;
        c.Emit({ 0x83, 0xF9, 0x01 });                           // cmp ecx, 1  (type 5 after four dec)
        const std::size_t notFive = c.Jcc8(0x75);
        EmitFlagTest(c, false);
        const std::size_t off = c.Jcc8(0x74);
        c.Emit({ 0xB8, kWalkerObjectType, 0x00, 0x00, 0x00, 0xC3 });   // mov eax,0x2c ; ret
        c.Bind8(notFive);
        c.Bind8(off);
        c.Emit({ 0xB8, kNoObjectType, 0x00, 0x00, 0x00, 0xC3 });       // mov eax,0x5e ; ret
        return c;
    }

    CaveCode ProcSignalCode(std::uintptr_t join)
    {
        CaveCode c;
        c.Emit({ 0x83, 0xF9, 0x01 });                           // cmp ecx, 1  (type 5 after four dec)
        const std::size_t notFive = c.Jcc8(0x75);
        EmitFlagTest(c, true);
        const std::size_t off = c.Jcc8(0x74);
        c.Emit({ 0xBF, kWalkerObjectType, 0x00, 0x00, 0x00 });  // mov edi,0x2c
        c.JmpAbs(join);
        c.Bind8(notFive);
        c.Bind8(off);
        c.Emit({ 0xBF, kNoObjectType, 0x00, 0x00, 0x00 });      // mov edi,0x5e
        c.JmpAbs(join);
        return c;
    }
}

bool Install_Buddy2BlockControllerImpl_GetBuddyGameObjectType_Patches()
{
    if (g_DefaultCave.installed && g_ProcSignalCave.installed)
        return true;

    const std::uintptr_t signalSite = gAddr.Buddy2BlockControllerImpl_ProcSignal_GameObjectType;
    const std::uintptr_t join = signalSite
        ? reinterpret_cast<std::uintptr_t>(ResolveGameAddress(signalSite)) + kProcSignalJoin
        : 0;
    if (!join || !InstallBuddyCodeCave(g_ProcSignalCave, signalSite, kProcSignalSite, sizeof(kProcSignalSite),
                                       ProcSignalCode(join)))
        return false;

    if (!InstallBuddyCodeCave(g_DefaultCave, gAddr.Buddy2BlockControllerImpl_GetBuddyGameObjectType_Default,
                              kDefaultSite, sizeof(kDefaultSite), DefaultCode()))
    {
        UninstallBuddyCodeCave(g_ProcSignalCave);
        return false;
    }
    return true;
}

void Uninstall_Buddy2BlockControllerImpl_GetBuddyGameObjectType_Patches()
{
    UninstallBuddyCodeCave(g_DefaultCave);
    UninstallBuddyCodeCave(g_ProcSignalCave);
}
