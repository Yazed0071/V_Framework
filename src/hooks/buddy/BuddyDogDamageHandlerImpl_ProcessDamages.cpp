#include "pch.h"

#include "BuddyDogDamageHandlerImpl_ProcessDamages.h"

#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "BuddyCodeCave.h"
#include "BuddyOutfitRegistry.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    BuddyCodeCave g_DamageCave{ "D-Dog gear damage" };
    BuddyCodeCave g_ReactionCave{ "D-Dog gear bullet stagger" };
    BuddyCodeCave g_HitEffectCave{ "D-Dog gear armour hit effect" };
    BuddyCodeCave g_HitSoundCave{ "D-Dog gear armour hit sound" };
    BuddyCodeCave g_EscapeCave{ "D-Dog gear escape distance" };
    BuddyCodeCave g_HitRetreatCave{ "D-Dog gear hit retreat distance" };
    BuddyCodeCave g_RetargetCave{ "D-Dog gear retarget range" };
    BuddyCodeCave g_SoldierThreatCave{ "D-Dog gear soldier reaction" };

    constexpr std::uint8_t kDamageBytes[] =
    {
        0x80, 0xB8, 0x49, 0x2E, 0x00, 0x00, 0x03, 0x75, 0x13, 0x8B, 0x83, 0xA0, 0x00, 0x00, 0x00, 0xC1,
        0xE8, 0x0B, 0x24, 0x01, 0x74, 0x06, 0x41, 0x0F, 0x28, 0xF1, 0xEB, 0x10, 0x8B, 0x83, 0xA0, 0x00,
        0x00, 0x00, 0xC1, 0xE8, 0x13, 0xA8, 0x01, 0x74, 0x03, 0x0F, 0x28, 0xF7,
    };
    constexpr std::uint8_t kReactionBytes[] =
    {
        0x80, 0xB8, 0x49, 0x2E, 0x00, 0x00, 0x03, 0x75, 0x0A, 0x40, 0xF6, 0xC6, 0x01, 0x74, 0x04, 0x32,
        0xC0, 0xEB, 0x25,
    };
    constexpr std::uint8_t kHitEffectBytes[] =
    {
        0x80, 0xB8, 0x49, 0x2E, 0x00, 0x00, 0x03, 0x0F, 0x85, 0xB3, 0x05, 0x00, 0x00,
    };
    constexpr std::uint8_t kHitSoundBytes[] =
    {
        0x80, 0xB8, 0x49, 0x2E, 0x00, 0x00, 0x03, 0x75, 0x16, 0xB8, 0xC9, 0x4E, 0xA7, 0xD6, 0xEB, 0x39,
        0x8D, 0x42, 0x97, 0x83, 0xF8, 0x04, 0x77, 0x07, 0xB8, 0x78, 0xDA, 0x53, 0x47, 0xEB, 0x2A,
    };
    constexpr std::uint8_t kEscapeBytes[] =
    {
        0x80, 0xB8, 0x49, 0x2E, 0x00, 0x00, 0x03, 0x75, 0x0A, 0xF3, 0x0F, 0x10, 0x05, 0x29, 0x8A, 0x41,
        0x01, 0xEB, 0x08, 0xF3, 0x0F, 0x10, 0x05, 0x53, 0x96, 0x46, 0x01,
    };
    constexpr std::uint8_t kHitRetreatBytes[] =
    {
        0x80, 0xB8, 0x49, 0x2E, 0x00, 0x00, 0x03, 0x75, 0x0A, 0xF3, 0x0F, 0x10, 0x05, 0xA4, 0xA3, 0x3F,
        0x01, 0xEB, 0x08, 0xF3, 0x0F, 0x10, 0x05, 0x56, 0xF0, 0x45, 0x01,
    };
    constexpr std::uint8_t kRetargetBytes[] =
    {
        0x80, 0xB8, 0x49, 0x2E, 0x00, 0x00, 0x03, 0x75, 0x08, 0xF3, 0x0F, 0x58, 0x3D, 0xD8, 0x3F, 0x54,
        0x01,
    };
    constexpr std::uint8_t kSoldierThreatBytes[] =
    {
        0x0F, 0xB6, 0x88, 0x49, 0x2E, 0x00, 0x00, 0x80, 0xE9, 0x02, 0x80, 0xF9, 0x02, 0x76, 0x1C, 0x41,
        0xBC, 0x01, 0x00, 0x00, 0x00, 0xB8, 0x07, 0x00, 0x00, 0x00, 0x44, 0x38, 0x64, 0x24, 0x31, 0x41,
        0x0F, 0x44, 0xC4, 0x88, 0x43, 0x2E, 0xE9, 0x78, 0x01, 0x00, 0x00, 0xC6, 0x43, 0x2E, 0x05,
    };

    constexpr std::size_t kGearCheckSize = 9;   // cmp byte [rax+0x2E49], 3 + jcc8

    std::uintptr_t ResolveWindow(std::uintptr_t siteAbs, const std::uint8_t* bytes, std::size_t size, const char* name)
    {
        const auto* site = static_cast<const std::uint8_t*>(siteAbs ? ResolveGameAddress(siteAbs) : nullptr);
        if (!site || std::memcmp(site, bytes, size) != 0)
        {
            LogError("BuddyOutfit: %s code differs on this build, that part is off", name);
            return 0;
        }
        return reinterpret_cast<std::uintptr_t>(site);
    }

    std::size_t EmitCustomGearTest(CaveCode& c)
    {
        c.Emit({ 0x80, 0xB8, 0x49, 0x2E, 0x00, 0x00, 0x08 });   // cmp byte [rax+0x2E49], 8
        return c.Jcc8(0x73);                                    // jae custom
    }

    void EmitVanillaBattleDressTest(CaveCode& c, std::uintptr_t ifBattleDress, std::uintptr_t ifOther)
    {
        c.Emit({ 0x80, 0xB8, 0x49, 0x2E, 0x00, 0x00, 0x03 });   // cmp byte [rax+0x2E49], 3
        const std::size_t other = c.Jcc8(0x75);                 // jne other
        c.JmpAbs(ifBattleDress);
        c.Bind8(other);
        c.JmpAbs(ifOther);
    }

    void EmitGearBitBranch(CaveCode& c, const std::uint32_t* bits, std::uintptr_t ifSet, std::uintptr_t ifClear)
    {
        c.Emit({ 0x51, 0x52 });                                 // push rcx, push rdx
        c.Emit({ 0x0F, 0xB6, 0x88, 0x49, 0x2E, 0x00, 0x00 });   // movzx ecx, byte [rax+0x2E49]
        c.Emit({ 0x48, 0xBA });                                 // mov rdx, per-gear bits
        c.Imm64(reinterpret_cast<std::uint64_t>(bits));
        c.Emit({ 0x0F, 0xA3, 0x0A });                           // bt [rdx], ecx
        c.Emit({ 0x5A, 0x59 });                                 // pop rdx, pop rcx
        const std::size_t clear = c.Jcc8(0x73);                 // jnc clear
        c.JmpAbs(ifSet);
        c.Bind8(clear);
        c.JmpAbs(ifClear);
    }

    struct SseOp
    {
        std::uint8_t opcode;
        std::uint8_t modrm;
    };

    constexpr SseOp kMovssXmm0 = { 0x10, 0x04 };   // movss xmm0, [rax+rcx*4]
    constexpr SseOp kAddssXmm7 = { 0x58, 0x3C };   // addss xmm7, [rax+rcx*4]

    void EmitLoadGearFloat(CaveCode& c, const float* table, SseOp op)
    {
        c.Emit({ 0x50, 0x51 });                                 // push rax, push rcx
        c.Emit({ 0x0F, 0xB6, 0x88, 0x49, 0x2E, 0x00, 0x00 });   // movzx ecx, byte [rax+0x2E49]
        c.Emit({ 0x48, 0xB8 });                                 // mov rax, per-gear table
        c.Imm64(reinterpret_cast<std::uint64_t>(table));
        c.Emit({ 0xF3, 0x0F, op.opcode, op.modrm, 0x88 });
        c.Emit({ 0x59, 0x58 });                                 // pop rcx, pop rax
    }

    bool InstallDamage()
    {
        const std::uintptr_t siteAbs = gAddr.BuddyDogDamageHandlerImpl_ProcessDamages_GearCheck;
        const std::uintptr_t base = ResolveWindow(siteAbs, kDamageBytes, sizeof(kDamageBytes), g_DamageCave.name);
        if (!base)
            return false;

        CaveCode c;
        const std::size_t custom = EmitCustomGearTest(c);
        EmitVanillaBattleDressTest(c, base + 9, base + 0x1C);
        c.Bind8(custom);
        c.Emit({ 0x51 });                                       // push rcx
        c.Emit({ 0x0F, 0xB6, 0x88, 0x49, 0x2E, 0x00, 0x00 });   // movzx ecx, byte [rax+0x2E49]
        c.Emit({ 0x48, 0xB8 });                                 // mov rax, damage rate table
        c.Imm64(reinterpret_cast<std::uint64_t>(buddyoutfit::DogDamageRates()));
        c.Emit({ 0xF3, 0x0F, 0x10, 0x34, 0x88 });               // movss xmm6, [rax+rcx*4]
        c.Emit({ 0x59 });                                       // pop rcx
        c.Emit({ 0x8B, 0x83, 0xA0, 0x00, 0x00, 0x00 });         // mov eax, [rbx+0xA0]  (BuddyService flags)
        c.Emit({ 0x0F, 0xBA, 0xE0, 0x13 });                     // bt eax, 19  (the doubled-damage state)
        const std::size_t single = c.Jcc8(0x73);                // jnc done
        c.Emit({ 0xF3, 0x0F, 0x59, 0xF7 });                     // mulss xmm6, xmm7  (xmm7 = 2.0)
        c.Bind8(single);
        c.JmpAbs(base + sizeof(kDamageBytes));

        return InstallBuddyCodeCave(g_DamageCave, siteAbs, kDamageBytes, kGearCheckSize, c);
    }

    bool InstallBitSite(BuddyCodeCave& cave, std::uintptr_t siteAbs, const std::uint8_t* window, std::size_t windowSize,
                        std::size_t patchSize, const std::uint32_t* bits, std::size_t battleDressOffset, std::size_t otherOffset)
    {
        const std::uintptr_t base = ResolveWindow(siteAbs, window, windowSize, cave.name);
        if (!base)
            return false;

        CaveCode c;
        const std::size_t custom = EmitCustomGearTest(c);
        EmitVanillaBattleDressTest(c, base + battleDressOffset, base + otherOffset);
        c.Bind8(custom);
        EmitGearBitBranch(c, bits, base + battleDressOffset, base + otherOffset);

        return InstallBuddyCodeCave(cave, siteAbs, window, patchSize, c);
    }

    bool InstallFloatSite(BuddyCodeCave& cave, std::uintptr_t siteAbs, const std::uint8_t* window, std::size_t windowSize,
                          const float* table, SseOp op, std::size_t battleDressOffset, std::size_t otherOffset,
                          std::size_t doneOffset)
    {
        const std::uintptr_t base = ResolveWindow(siteAbs, window, windowSize, cave.name);
        if (!base)
            return false;

        CaveCode c;
        const std::size_t custom = EmitCustomGearTest(c);
        EmitVanillaBattleDressTest(c, base + battleDressOffset, base + otherOffset);
        c.Bind8(custom);
        EmitLoadGearFloat(c, table, op);
        c.JmpAbs(base + doneOffset);

        return InstallBuddyCodeCave(cave, siteAbs, window, kGearCheckSize, c);
    }

    bool InstallSoldierThreat()
    {
        const std::uintptr_t siteAbs = gAddr.SoldierNoticeBuddyDogAiImpl_StepAware_GearCheck;
        const std::uintptr_t base = ResolveWindow(siteAbs, kSoldierThreatBytes, sizeof(kSoldierThreatBytes), g_SoldierThreatCave.name);
        if (!base)
            return false;

        CaveCode c;
        c.Emit({ 0x0F, 0xB6, 0x88, 0x49, 0x2E, 0x00, 0x00 });   // movzx ecx, byte [rax+0x2E49]
        c.Emit({ 0x80, 0xF9, 0x08 });                           // cmp cl, 8
        const std::size_t custom = c.Jcc8(0x73);                // jae custom
        c.JmpAbs(base + 7);
        c.Bind8(custom);
        c.Emit({ 0x52 });                                       // push rdx
        c.Emit({ 0x48, 0xBA });                                 // mov rdx, threat bits
        c.Imm64(reinterpret_cast<std::uint64_t>(buddyoutfit::DogSoldierThreatBits()));
        c.Emit({ 0x0F, 0xA3, 0x0A });                           // bt [rdx], ecx
        c.Emit({ 0x5A });                                       // pop rdx
        const std::size_t stray = c.Jcc8(0x73);                 // jnc stray
        c.JmpAbs(base + 0x2B);
        c.Bind8(stray);
        c.JmpAbs(base + 0x0F);

        return InstallBuddyCodeCave(g_SoldierThreatCave, siteAbs, kSoldierThreatBytes, 7, c);
    }
}

bool Install_BuddyDogDamageHandlerImpl_ProcessDamages_Patches()
{
    bool ok = InstallDamage();
    ok &= InstallBitSite(g_ReactionCave, gAddr.BuddyDogDamageHandlerImpl_IsNeedReaction_GearCheck,
                         kReactionBytes, sizeof(kReactionBytes), kGearCheckSize,
                         buddyoutfit::DogNoBulletFlinchBits(), 9, 0x13);
    ok &= InstallBitSite(g_HitEffectCave, gAddr.BuddyDogDamageHandlerImpl_CallDamageEffect_GearCheck,
                         kHitEffectBytes, sizeof(kHitEffectBytes), sizeof(kHitEffectBytes),
                         buddyoutfit::DogArmorHitEffectBits(), 0x0D, 0x5C0);
    ok &= InstallBitSite(g_HitSoundCave, gAddr.BuddyDogDamageHandlerImpl_CallDamageSound_GearCheck,
                         kHitSoundBytes, sizeof(kHitSoundBytes), kGearCheckSize,
                         buddyoutfit::DogArmorHitSoundBits(), 9, 0x1F);
    ok &= InstallFloatSite(g_EscapeCave, gAddr.BuddyDogEscapeIdleAiImpl_StepSearch_GearCheck,
                           kEscapeBytes, sizeof(kEscapeBytes), buddyoutfit::DogEscapeDistances(), kMovssXmm0, 9, 0x13, 0x1B);
    ok &= InstallFloatSite(g_HitRetreatCave, gAddr.BuddyDogNoticeDamageAiImpl_Setup_GearCheck,
                           kHitRetreatBytes, sizeof(kHitRetreatBytes), buddyoutfit::DogHitRetreatDistances(), kMovssXmm0, 9, 0x13, 0x1B);
    ok &= InstallFloatSite(g_RetargetCave, gAddr.BuddyDogAttackAiImpl_ReTarget_GearCheck,
                           kRetargetBytes, sizeof(kRetargetBytes), buddyoutfit::DogRetargetRanges(), kAddssXmm7, 9, 0x11, 0x11);
    ok &= InstallSoldierThreat();
    return ok;
}

bool Uninstall_BuddyDogDamageHandlerImpl_ProcessDamages_Patches()
{
    UninstallBuddyCodeCave(g_SoldierThreatCave);
    UninstallBuddyCodeCave(g_RetargetCave);
    UninstallBuddyCodeCave(g_HitRetreatCave);
    UninstallBuddyCodeCave(g_EscapeCave);
    UninstallBuddyCodeCave(g_HitSoundCave);
    UninstallBuddyCodeCave(g_HitEffectCave);
    UninstallBuddyCodeCave(g_ReactionCave);
    UninstallBuddyCodeCave(g_DamageCave);
    return true;
}
