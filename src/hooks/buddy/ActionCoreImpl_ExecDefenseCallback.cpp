#include "pch.h"

#include "ActionCoreImpl_ExecDefenseCallback.h"

#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "BuddyCodeCave.h"
#include "BuddyOutfitRegistry.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    BuddyCodeCave g_DefenseCave{ "D-Horse tack damage" };
    BuddyCodeCave g_GallopCave{ "D-Horse tack gallop speed" };
    BuddyCodeCave g_RamCave{ "D-Horse tack body ram" };
    BuddyCodeCave g_SoundCave{ "D-Horse tack sounds" };

    constexpr std::uint8_t kTackFirstCustom = buddyoutfit::kHorseVanillaTackCount;

    constexpr std::uint8_t kDefenseBytes[] =
    {
        0x8B, 0x88, 0xD4, 0x02, 0x00, 0x00, 0x83, 0xF9, 0x01, 0x74, 0x2C, 0x83, 0xF9, 0x02, 0x75, 0x07,
        0xC6, 0x44, 0x24, 0x53, 0xB3, 0xEB, 0x20, 0x83, 0xF9, 0x03, 0x75, 0x07, 0xC6, 0x44, 0x24, 0x53,
        0x96, 0xEB, 0x14, 0x0F, 0xB6, 0x44, 0x24, 0x53, 0xBA, 0x64, 0x00, 0x00, 0x00, 0x83, 0xF9, 0x04,
        0x0F, 0x44, 0xC2, 0x88, 0x44, 0x24, 0x53,
    };
    constexpr std::uint8_t kGallopBytes[] =
    {
        0x8B, 0x88, 0xD4, 0x02, 0x00, 0x00, 0x83, 0xF9, 0x01, 0x74, 0x29, 0x83, 0xF9, 0x02, 0x75, 0x09,
        0xF3, 0x0F, 0x59, 0x05, 0x00, 0x8C, 0x2B, 0x01, 0xC3, 0x83, 0xF9, 0x03, 0x75, 0x09, 0xF3, 0x0F,
        0x59, 0x05, 0x02, 0xA0, 0x37, 0x01, 0xC3, 0x83, 0xF9, 0x04, 0x75, 0x08, 0xF3, 0x0F, 0x59, 0x05,
        0x3C, 0xC9, 0x4F, 0x01, 0xF3, 0xC3,
    };
    constexpr std::uint8_t kRamBytes[] =
    {
        0x8B, 0x81, 0xD4, 0x02, 0x00, 0x00, 0x83, 0xF8, 0x01, 0x74, 0x3C, 0x83, 0xF8, 0x02, 0x75, 0x14,
        0xB8, 0x6A, 0x00, 0x00, 0x00, 0x66, 0x89, 0x47, 0x28, 0x48, 0x8B, 0x5C, 0x24, 0x30, 0x48, 0x83,
        0xC4, 0x20, 0x5F, 0xC3, 0x83, 0xF8, 0x03, 0x75, 0x14, 0xB8, 0x6B, 0x00, 0x00, 0x00, 0x66, 0x89,
        0x47, 0x28, 0x48, 0x8B, 0x5C, 0x24, 0x30, 0x48, 0x83, 0xC4, 0x20, 0x5F, 0xC3, 0x83, 0xF8, 0x04,
        0xB8, 0x6C, 0x00, 0x00, 0x00, 0x74, 0x05, 0xB8, 0x69, 0x00, 0x00, 0x00, 0x66, 0x89, 0x47, 0x28,
        0x48, 0x8B, 0x5C, 0x24, 0x30, 0x48, 0x83, 0xC4, 0x20, 0x5F, 0xC3,
    };
    constexpr std::uint8_t kSoundBytes[] =
    {
        0x49, 0x63, 0x80, 0xD4, 0x02, 0x00, 0x00, 0x83, 0xF8, 0x06, 0x77, 0x42, 0x48, 0x8D, 0x0D, 0x52,
        0x40, 0x18, 0xFF, 0x8B, 0x8C, 0x81, 0x74, 0xC1, 0xE7, 0x00, 0x48, 0x8D, 0x05, 0x44, 0x40, 0x18,
        0xFF, 0x48, 0x03, 0xC8, 0xFF, 0xE1, 0xC6, 0x45, 0xB8, 0x00, 0xEB, 0x22, 0xC6, 0x45, 0xB8, 0x07,
        0xEB, 0x1C, 0xC6, 0x45, 0xB8, 0x01, 0xEB, 0x16, 0xC6, 0x45, 0xB8, 0x02, 0xEB, 0x10, 0xC6, 0x45,
        0xB8, 0x03, 0xEB, 0x0A, 0xC6, 0x45, 0xB8, 0x05, 0xEB, 0x04, 0xC6, 0x45, 0xB8, 0x04,
    };

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

    bool InstallDefense()
    {
        const std::uintptr_t siteAbs = gAddr.HorseActionCoreImpl_ExecDefenseCallback_TackCheck;
        const std::uintptr_t base = ResolveWindow(siteAbs, kDefenseBytes, sizeof(kDefenseBytes), g_DefenseCave.name);
        if (!base)
            return false;
        const std::uintptr_t done = base + sizeof(kDefenseBytes);

        CaveCode c;
        c.Emit({ 0x8B, 0x88, 0xD4, 0x02, 0x00, 0x00 });           // mov ecx, [rax+0x2D4]
        c.Emit({ 0x83, 0xF9, kTackFirstCustom });                 // cmp ecx, 7
        const std::size_t low = c.Jcc8(0x72);                     // jb vanilla
        c.Emit({ 0x81, 0xF9, 0xFF, 0x00, 0x00, 0x00 });           // cmp ecx, 0xFF
        const std::size_t high = c.Jcc8(0x77);                    // ja vanilla
        c.Emit({ 0x48, 0xB8 });                                   // mov rax, damage rate table
        c.Imm64(reinterpret_cast<std::uint64_t>(buddyoutfit::HorseDamageRates()));
        c.Emit({ 0x0F, 0xB7, 0x04, 0x48 });                       // movzx eax, word [rax+rcx*2]
        c.Emit({ 0x66, 0x3D, 0xFF, 0xFF });                       // cmp ax, keep
        const std::size_t keep = c.Jcc8(0x74);                    // je done
        c.Emit({ 0x88, 0x44, 0x24, 0x53 });                       // mov [rsp+0x53], al  (the hit's damage rate)
        c.Bind8(keep);
        c.JmpAbs(done);
        c.Bind8(low);
        c.Bind8(high);
        c.Emit({ 0x83, 0xF9, 0x01 });                             // cmp ecx, 1
        const std::size_t notOne = c.Jcc8(0x75);                  // jne next
        c.JmpAbs(done);
        c.Bind8(notOne);
        c.JmpAbs(base + 11);

        return InstallBuddyCodeCave(g_DefenseCave, siteAbs, kDefenseBytes, 11, c);
    }

    bool InstallGallop()
    {
        const std::uintptr_t siteAbs = gAddr.HorseBasicAction2Impl_UpdateGallopMotionSpeed_TackCheck;
        const std::uintptr_t base = ResolveWindow(siteAbs, kGallopBytes, sizeof(kGallopBytes), g_GallopCave.name);
        if (!base)
            return false;

        CaveCode c;
        c.Emit({ 0x8B, 0x88, 0xD4, 0x02, 0x00, 0x00 });           // mov ecx, [rax+0x2D4]
        c.Emit({ 0x83, 0xF9, kTackFirstCustom });                 // cmp ecx, 7
        const std::size_t low = c.Jcc8(0x72);                     // jb vanilla
        c.Emit({ 0x81, 0xF9, 0xFF, 0x00, 0x00, 0x00 });           // cmp ecx, 0xFF
        const std::size_t high = c.Jcc8(0x77);                    // ja vanilla
        c.Emit({ 0x48, 0xB8 });                                   // mov rax, gallop rate table
        c.Imm64(reinterpret_cast<std::uint64_t>(buddyoutfit::HorseGallopRates()));
        c.Emit({ 0xF3, 0x0F, 0x59, 0x04, 0x88 });                 // mulss xmm0, [rax+rcx*4]
        c.Emit({ 0xC3 });                                         // ret
        c.Bind8(low);
        c.Bind8(high);
        c.Emit({ 0x83, 0xF9, 0x01 });                             // cmp ecx, 1
        const std::size_t notOne = c.Jcc8(0x75);                  // jne next
        c.Emit({ 0xC3 });                                         // ret
        c.Bind8(notOne);
        c.JmpAbs(base + 11);

        return InstallBuddyCodeCave(g_GallopCave, siteAbs, kGallopBytes, 11, c);
    }

    bool InstallRam()
    {
        const std::uintptr_t siteAbs = gAddr.HorseBasicAction2Impl_EnterOffenseTarget_TackCheck;
        const std::uintptr_t base = ResolveWindow(siteAbs, kRamBytes, sizeof(kRamBytes), g_RamCave.name);
        if (!base)
            return false;
        const std::uintptr_t plainAttack = base + 0x47;
        const std::uintptr_t storeAttack = base + 0x4C;

        CaveCode c;
        c.Emit({ 0x8B, 0x81, 0xD4, 0x02, 0x00, 0x00 });           // mov eax, [rcx+0x2D4]
        c.Emit({ 0x83, 0xF8, kTackFirstCustom });                 // cmp eax, 7
        const std::size_t low = c.Jcc8(0x72);                     // jb vanilla
        c.Emit({ 0x3D, 0xFF, 0x00, 0x00, 0x00 });                 // cmp eax, 0xFF
        const std::size_t high = c.Jcc8(0x77);                    // ja vanilla
        c.Emit({ 0x48, 0xB9 });                                   // mov rcx, attack id table
        c.Imm64(reinterpret_cast<std::uint64_t>(buddyoutfit::HorseBodyAttackIds()));
        c.Emit({ 0x0F, 0xB7, 0x04, 0x41 });                       // movzx eax, word [rcx+rax*2]
        c.JmpAbs(storeAttack);
        c.Bind8(low);
        c.Bind8(high);
        c.Emit({ 0x83, 0xF8, 0x01 });                             // cmp eax, 1
        const std::size_t notOne = c.Jcc8(0x75);                  // jne next
        c.JmpAbs(plainAttack);
        c.Bind8(notOne);
        c.JmpAbs(base + 11);

        return InstallBuddyCodeCave(g_RamCave, siteAbs, kRamBytes, 11, c);
    }

    bool InstallSound()
    {
        const std::uintptr_t siteAbs = gAddr.Horse2_MotionSoundTackSwitch;
        const std::uintptr_t base = ResolveWindow(siteAbs, kSoundBytes, sizeof(kSoundBytes), g_SoundCave.name);
        if (!base)
            return false;
        const std::uintptr_t done = base + sizeof(kSoundBytes);

        CaveCode c;
        c.Emit({ 0x49, 0x63, 0x80, 0xD4, 0x02, 0x00, 0x00 });     // movsxd rax, [r8+0x2D4]
        c.Emit({ 0x83, 0xF8, kTackFirstCustom });                 // cmp eax, 7
        const std::size_t low = c.Jcc8(0x72);                     // jb vanilla
        c.Emit({ 0x3D, 0xFF, 0x00, 0x00, 0x00 });                 // cmp eax, 0xFF
        const std::size_t high = c.Jcc8(0x77);                    // ja vanilla
        c.Emit({ 0x48, 0xB9 });                                   // mov rcx, sound code table
        c.Imm64(reinterpret_cast<std::uint64_t>(buddyoutfit::HorseSoundCodes()));
        c.Emit({ 0x0F, 0xB6, 0x0C, 0x01 });                       // movzx ecx, byte [rcx+rax]
        c.Emit({ 0x88, 0x4D, 0xB8 });                             // mov [rbp-0x48], cl
        c.JmpAbs(done);
        c.Bind8(low);
        c.Bind8(high);
        c.Emit({ 0x83, 0xF8, 0x06 });                             // cmp eax, 6
        const std::size_t inTable = c.Jcc8(0x76);                 // jbe jump table
        c.JmpAbs(done);
        c.Bind8(inTable);
        c.JmpAbs(base + 12);

        return InstallBuddyCodeCave(g_SoundCave, siteAbs, kSoundBytes, 12, c);
    }
}

bool Install_ActionCoreImpl_ExecDefenseCallback_Patches()
{
    bool ok = InstallDefense();
    ok &= InstallGallop();
    ok &= InstallRam();
    ok &= InstallSound();
    return ok;
}

bool Uninstall_ActionCoreImpl_ExecDefenseCallback_Patches()
{
    UninstallBuddyCodeCave(g_SoundCave);
    UninstallBuddyCodeCave(g_RamCave);
    UninstallBuddyCodeCave(g_GallopCave);
    UninstallBuddyCodeCave(g_DefenseCave);
    return true;
}
