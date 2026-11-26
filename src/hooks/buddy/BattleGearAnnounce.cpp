#include <pch.h>

#include "BattleGearAnnounce.h"

#include <cstdint>

#include "AddressSet.h"
#include "BattleGearWeapons.h"
#include "BuddyCodeCave.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    struct AnnounceSwap
    {
        BuddyCodeCave cave;
        std::uint8_t  movabs[2];       // movabs rdx / rbx, imm64
        std::uint64_t dWalkerLangId;
        std::uint64_t battleGearLangId;
    };

    struct LangSwap
    {
        std::uint64_t dWalkerLangId;
        std::uint64_t battleGearLangId;
    };

    constexpr std::size_t kPatchSize = 10;

    AnnounceSwap g_Swaps[] = {
        { { "Battle Gear arrived announce" }, { 0x48, 0xBA }, 0x00000B5FE9557294ull, 0x000053C4D9B0406Dull },      // announce_dwalker_lz, announce_bgear_lz
        { { "Battle Gear left-area announce" }, { 0x48, 0xBB }, 0x0000C2C841EDB8B5ull, 0x00000A331EE836DCull },    // announce_dwalker_escape, announce_bgear_escape
        { { "Battle Gear repaired announce" }, { 0x48, 0xBA }, 0x000087C9C3777B11ull, 0x00007A723A1D41D0ull },     // announce_dwalker_recovery, announce_bgear_recovery
        { { "Battle Gear heavy damage announce" }, { 0x48, 0xBA }, 0x000077EDB3B4B79Eull, 0x0000CC0225A680F3ull }, // announce_dwalker_dying, announce_bgear_dying
        { { "Battle Gear enemy sighted announce" }, { 0x48, 0xBA }, 0x00008576A75D49BDull, 0x000071B2BEB45F88ull },  // announce_dwalker_find_enemy, V_announce_bgear_find_enemy
        { { "Battle Gear engaging announce" }, { 0x48, 0xBA }, 0x000003E9B16B4FEBull, 0x00006E2770C966FCull },       // announce_dwalker_attack_enemy, V_announce_bgear_attack_enemy
        { { "Battle Gear attack complete announce" }, { 0x48, 0xBA }, 0x00008FF001FD0FDBull, 0x000080CC2F2F8E10ull }, // announce_dwalker_attack_stop, V_announce_bgear_attack_stop
        { { "Battle Gear whistle order label" }, { 0x48, 0xBA }, 0x0000E9E4E15FFB33ull, 0x00000FECBDF0EB80ull },     // voice_cmd_wg_whistle_call, V_voice_cmd_bg_whistle_call
    };

    constexpr LangSwap kModeLines[] = {
        { 0x000018118958133Aull, 0x0000B05EC832B745ull },   // announce_dwalker_normal_start, V_announce_bgear_normal_start
        { 0x00007AF3148B5B7Cull, 0x0000F197ED40DFB8ull },   // announce_dwalker_search_start, V_announce_bgear_search_start
        { 0x00000A8BFAD310EEull, 0x00007B20E0644348ull },   // announce_dwalker_intercept_start, V_announce_bgear_intercept_start
        { 0x0000FDD125A3F355ull, 0x0000AB9B46430D69ull },   // announce_dwalker_stealth_start, V_announce_bgear_stealth_start
        { 0x000025B164268985ull, 0x0000575288348D78ull },   // announce_dwalker_mode_cancel, V_announce_bgear_mode_cancel
    };

    // the mode line call: mov rax, [r12] ; xor r8d, r8d ; mov rcx, r12 ; then call [rax+0x1A8] with the line id in rdx
    const std::uint8_t kModeCallSite[] = { 0x49, 0x8B, 0x04, 0x24, 0x45, 0x33, 0xC0, 0x49, 0x8B, 0xCC };

    BuddyCodeCave g_ModeCave{ "Battle Gear mode announces" };

    std::uintptr_t SiteOf(int i)
    {
        switch (i)
        {
        case 0:  return gAddr.WalkerGear_AnnounceArrivedSite;
        case 1:  return gAddr.WalkerGear_AnnounceLeftAreaSite;
        case 2:  return gAddr.WalkerGear_AnnounceRepairedSite;
        case 3:  return gAddr.WalkerGear_AnnounceHeavyDamageSite;
        case 4:  return gAddr.WalkerGear_AnnounceFindEnemySite;
        case 5:  return gAddr.WalkerGear_AnnounceAttackEnemySite;
        case 6:  return gAddr.WalkerGear_AnnounceAttackStopSite;
        default: return gAddr.BuddyCommand_WhistleLabelSite;
        }
    }

    bool InstallOne(AnnounceSwap& s, std::uintptr_t siteAbs)
    {
        std::uint8_t original[kPatchSize];
        original[0] = s.movabs[0];
        original[1] = s.movabs[1];
        for (int k = 0; k < 8; ++k)
            original[2 + k] = static_cast<std::uint8_t>(s.dWalkerLangId >> (k * 8));
        const auto back = reinterpret_cast<std::uintptr_t>(ResolveGameAddress(siteAbs)) + kPatchSize;

        CaveCode c;
        c.Emit({ 0x50 });                                                    // push rax
        c.Emit({ 0x48, 0xB8 });                                              // mov rax, &comm slot
        c.Imm64(reinterpret_cast<std::uint64_t>(battleGearWeapons::BattleGearCommSlot()));
        c.Emit({ 0x48, 0x83, 0x38, 0x00 });                                  // cmp qword [rax], 0
        c.Emit({ 0x58 });                                                    // pop rax
        const std::size_t dWalker = c.Jcc8(0x74);                            // je
        c.Emit({ s.movabs[0], s.movabs[1] });
        c.Imm64(s.battleGearLangId);
        c.JmpAbs(back);
        c.Bind8(dWalker);
        c.bytes.insert(c.bytes.end(), original, original + kPatchSize);
        c.JmpAbs(back);
        return InstallBuddyCodeCave(s.cave, siteAbs, original, kPatchSize, c);
    }

    bool InstallModeLines()
    {
        const std::uintptr_t siteAbs = gAddr.WalkerGear_AnnounceModeSite;
        if (!siteAbs)
            return false;
        const auto back = reinterpret_cast<std::uintptr_t>(ResolveGameAddress(siteAbs)) + sizeof(kModeCallSite);

        CaveCode c;
        c.Emit({ 0x50 });                                                    // push rax
        c.Emit({ 0x48, 0xB8 });                                              // mov rax, &comm slot
        c.Imm64(reinterpret_cast<std::uint64_t>(battleGearWeapons::BattleGearCommSlot()));
        c.Emit({ 0x48, 0x83, 0x38, 0x00 });                                  // cmp qword [rax], 0
        const std::size_t battleGear = c.Jcc8(0x75);                         // jne
        c.Emit({ 0x58 });                                                    // pop rax
        c.bytes.insert(c.bytes.end(), kModeCallSite, kModeCallSite + sizeof(kModeCallSite));
        c.JmpAbs(back);
        c.Bind8(battleGear);
        for (const LangSwap& s : kModeLines)
        {
            c.Emit({ 0x48, 0xB8 });                                          // mov rax, D-Walker line
            c.Imm64(s.dWalkerLangId);
            c.Emit({ 0x48, 0x39, 0xC2 });                                    // cmp rdx, rax
            const std::size_t other = c.Jcc8(0x75);                          // jne
            c.Emit({ 0x48, 0xBA });                                          // mov rdx, Battle Gear line
            c.Imm64(s.battleGearLangId);
            c.Bind8(other);
        }
        c.Emit({ 0x58 });                                                    // pop rax
        c.bytes.insert(c.bytes.end(), kModeCallSite, kModeCallSite + sizeof(kModeCallSite));
        c.JmpAbs(back);
        return InstallBuddyCodeCave(g_ModeCave, siteAbs, kModeCallSite, sizeof(kModeCallSite), c);
    }
}

bool Install_BattleGearAnnounce_Patches()
{
    bool ok = true;
    for (int i = 0; i < static_cast<int>(sizeof(g_Swaps) / sizeof(g_Swaps[0])); ++i)
        ok &= InstallOne(g_Swaps[i], SiteOf(i));
    ok &= InstallModeLines();
    return ok;
}

void Uninstall_BattleGearAnnounce_Patches()
{
    UninstallBuddyCodeCave(g_ModeCave);
    for (AnnounceSwap& s : g_Swaps)
        UninstallBuddyCodeCave(s.cave);
}
