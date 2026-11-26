#include <pch.h>
#include "ThrowingImpl_UpdateNextPosition.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <initializer_list>

#include "AddressSet.h"
#include "../buddy/BuddyCodeCave.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "SupportWeaponRegistry.h"
#include "ThrowingSystemLayout.h"
#include "log.h"

namespace
{
    constexpr std::uint8_t kMulssXmm0Rip[]  = { 0xF3, 0x0F, 0x59, 0x05 };   // mulss xmm0, [rip+disp32]
    constexpr std::uint8_t kMovapsXmm5Rip[] = { 0x0F, 0x28, 0x2D };         // movaps xmm5, [rip+disp32]
    constexpr std::size_t  kMulssLength     = 8;
    constexpr std::size_t  kMovapsLength    = 7;
    constexpr int          kGravityLane     = 1;                            // the vector constant is (-0, -9.8, -0, -0)

    BuddyCodeCave g_FlyingCave    = { "SupportWeapon: gravity while flying" };
    BuddyCodeCave g_RollingCave   = { "SupportWeapon: gravity while rolling" };
    BuddyCodeCave g_PredictedCave = { "SupportWeapon: gravity in landing predictions" };

    float ItemGravitySEH(const std::uint8_t* impl, std::uint32_t index)
    {
        __try
        {
            return SupportWeapon::Gravity(ThrowingSystemLayout::ImplEquipId(impl, index));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return SupportWeapon::kGameGravity;
        }
    }

    float __fastcall ItemGravity(const std::uint8_t* impl, std::uint32_t index)
    {
        if (MissionCodeGuard::ShouldBypassHooks())
            return SupportWeapon::kGameGravity;
        return ItemGravitySEH(impl, index);
    }

    void __fastcall ItemGravityVector(const std::uint8_t* impl, std::uint32_t index, float* out)
    {
        out[0] = -0.0f;
        out[1] = -ItemGravity(impl, index);
        out[2] = -0.0f;
        out[3] = -0.0f;
    }

    bool ReadsGameGravitySEH(const std::uint8_t* site, const std::uint8_t* opcode, std::size_t opcodeSize,
                             std::size_t length, int lane, float expected, std::uint8_t* live)
    {
        __try
        {
            if (std::memcmp(site, opcode, opcodeSize) != 0)
                return false;
            const std::int32_t disp = *reinterpret_cast<const std::int32_t*>(site + opcodeSize);
            const float* constant = reinterpret_cast<const float*>(site + length + disp);
            if (constant[lane] != expected)
                return false;
            std::memcpy(live, site, length);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    CaveCode ScalarGravityCave(std::initializer_list<std::uint8_t> loadImplAndIndex, std::uintptr_t resume)
    {
        CaveCode c;
        c.Emit({ 0x9C, 0x53, 0x48, 0x89, 0xE3, 0x48, 0x83, 0xE4, 0xF0 });           // pushfq; push rbx; mov rbx, rsp; and rsp, -16
        c.Emit({ 0x50, 0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53 }); // push rax, rcx, rdx, r8-r11
        c.Emit({ 0x48, 0x83, 0xEC, 0x68 });                                        // sub rsp, 0x68
        c.Emit({ 0xF3, 0x0F, 0x7F, 0x04, 0x24 });                                  // movdqu [rsp], xmm0
        c.Emit({ 0xF3, 0x0F, 0x7F, 0x4C, 0x24, 0x10 });
        c.Emit({ 0xF3, 0x0F, 0x7F, 0x54, 0x24, 0x20 });
        c.Emit({ 0xF3, 0x0F, 0x7F, 0x5C, 0x24, 0x30 });
        c.Emit({ 0xF3, 0x0F, 0x7F, 0x64, 0x24, 0x40 });
        c.Emit({ 0xF3, 0x0F, 0x7F, 0x6C, 0x24, 0x50 });                            // movdqu [rsp+0x50], xmm5
        c.Emit(loadImplAndIndex);                                                  // rcx = ThrowingImpl, edx = local index
        c.Emit({ 0x48, 0x83, 0xEC, 0x20, 0x48, 0xB8 });                            // sub rsp, 0x20; mov rax, imm64
        c.Imm64(reinterpret_cast<std::uintptr_t>(&ItemGravity));
        c.Emit({ 0xFF, 0xD0, 0x48, 0x83, 0xC4, 0x20 });                            // call rax; add rsp, 0x20
        c.Emit({ 0xF3, 0x0F, 0x11, 0x44, 0x24, 0x60 });                            // movss [rsp+0x60], xmm0
        c.Emit({ 0xF3, 0x0F, 0x6F, 0x04, 0x24 });                                  // movdqu xmm0, [rsp]
        c.Emit({ 0xF3, 0x0F, 0x6F, 0x4C, 0x24, 0x10 });
        c.Emit({ 0xF3, 0x0F, 0x6F, 0x54, 0x24, 0x20 });
        c.Emit({ 0xF3, 0x0F, 0x6F, 0x5C, 0x24, 0x30 });
        c.Emit({ 0xF3, 0x0F, 0x6F, 0x64, 0x24, 0x40 });
        c.Emit({ 0xF3, 0x0F, 0x6F, 0x6C, 0x24, 0x50 });                            // movdqu xmm5, [rsp+0x50]
        c.Emit({ 0xF3, 0x0F, 0x59, 0x44, 0x24, 0x60 });                            // mulss xmm0, [rsp+0x60]: the item's gravity instead of 9.8
        c.Emit({ 0x48, 0x83, 0xC4, 0x68 });                                        // add rsp, 0x68
        c.Emit({ 0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59, 0x41, 0x58, 0x5A, 0x59, 0x58 }); // pop r11-r8, rdx, rcx, rax
        c.Emit({ 0x48, 0x89, 0xDC, 0x5B, 0x9D });                                  // mov rsp, rbx; pop rbx; popfq
        c.JmpAbs(resume);
        return c;
    }

    CaveCode RollingGravityCave(std::uintptr_t resume)
    {
        CaveCode c;
        c.Emit({ 0x9C, 0x53, 0x48, 0x89, 0xE3, 0x48, 0x83, 0xE4, 0xF0 });           // pushfq; push rbx; mov rbx, rsp; and rsp, -16
        c.Emit({ 0x50, 0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53 }); // push rax, rcx, rdx, r8-r11
        c.Emit({ 0x48, 0x83, 0xEC, 0x68 });                                        // sub rsp, 0x68
        c.Emit({ 0xF3, 0x0F, 0x7F, 0x04, 0x24 });                                  // movdqu [rsp], xmm0
        c.Emit({ 0xF3, 0x0F, 0x7F, 0x4C, 0x24, 0x10 });
        c.Emit({ 0xF3, 0x0F, 0x7F, 0x54, 0x24, 0x20 });
        c.Emit({ 0xF3, 0x0F, 0x7F, 0x5C, 0x24, 0x30 });
        c.Emit({ 0xF3, 0x0F, 0x7F, 0x64, 0x24, 0x40 });                            // movdqu [rsp+0x40], xmm4
        c.Emit({ 0x4C, 0x8D, 0x44, 0x24, 0x50 });                                  // lea r8, [rsp+0x50]; rcx, edx are still the impl and index
        c.Emit({ 0x48, 0x83, 0xEC, 0x20, 0x48, 0xB8 });                            // sub rsp, 0x20; mov rax, imm64
        c.Imm64(reinterpret_cast<std::uintptr_t>(&ItemGravityVector));
        c.Emit({ 0xFF, 0xD0, 0x48, 0x83, 0xC4, 0x20 });                            // call rax; add rsp, 0x20
        c.Emit({ 0xF3, 0x0F, 0x6F, 0x04, 0x24 });                                  // movdqu xmm0, [rsp]
        c.Emit({ 0xF3, 0x0F, 0x6F, 0x4C, 0x24, 0x10 });
        c.Emit({ 0xF3, 0x0F, 0x6F, 0x54, 0x24, 0x20 });
        c.Emit({ 0xF3, 0x0F, 0x6F, 0x5C, 0x24, 0x30 });
        c.Emit({ 0xF3, 0x0F, 0x6F, 0x64, 0x24, 0x40 });                            // movdqu xmm4, [rsp+0x40]
        c.Emit({ 0x0F, 0x10, 0x6C, 0x24, 0x50 });                                  // movups xmm5, [rsp+0x50]: (-0, -gravity, -0, -0)
        c.Emit({ 0x48, 0x83, 0xC4, 0x68 });                                        // add rsp, 0x68
        c.Emit({ 0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59, 0x41, 0x58, 0x5A, 0x59, 0x58 }); // pop r11-r8, rdx, rcx, rax
        c.Emit({ 0x48, 0x89, 0xDC, 0x5B, 0x9D });                                  // mov rsp, rbx; pop rbx; popfq
        c.JmpAbs(resume);
        return c;
    }

    bool InstallScalarCave(BuddyCodeCave& cave, std::uintptr_t siteAddress, std::initializer_list<std::uint8_t> loadImplAndIndex)
    {
        auto* site = static_cast<std::uint8_t*>(ResolveGameAddress(siteAddress));
        std::uint8_t live[kMulssLength] = {};
        if (!site || !ReadsGameGravitySEH(site, kMulssXmm0Rip, sizeof(kMulssXmm0Rip), kMulssLength, 0, SupportWeapon::kGameGravity, live))
        {
            LogError("%s: the site does not read the game's 9.8 on this build, per-item gravity is off there", cave.name);
            return false;
        }
        const std::uintptr_t resume = reinterpret_cast<std::uintptr_t>(site) + kMulssLength;
        return InstallBuddyCodeCave(cave, siteAddress, live, kMulssLength, ScalarGravityCave(loadImplAndIndex, resume));
    }

    bool InstallRollingCave()
    {
        auto* site = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.ThrowingImpl_UpdateNextPositionRolling_GravitySite));
        std::uint8_t live[kMovapsLength] = {};
        if (!site || !ReadsGameGravitySEH(site, kMovapsXmm5Rip, sizeof(kMovapsXmm5Rip), kMovapsLength, kGravityLane,
                                          -SupportWeapon::kGameGravity, live))
        {
            LogError("%s: the site does not read the game's gravity vector on this build, per-item gravity is off there", g_RollingCave.name);
            return false;
        }
        const std::uintptr_t resume = reinterpret_cast<std::uintptr_t>(site) + kMovapsLength;
        return InstallBuddyCodeCave(g_RollingCave, gAddr.ThrowingImpl_UpdateNextPositionRolling_GravitySite, live, kMovapsLength,
                                    RollingGravityCave(resume));
    }
}

bool Install_ThrowingImpl_UpdateNextPosition_Caves()
{
    bool ok = InstallScalarCave(g_FlyingCave, gAddr.ThrowingImpl_UpdateNextPositionHopping_GravitySite,
                                { 0x48, 0x89, 0xE9, 0x48, 0x89, 0xF2, 0x48, 0xC1, 0xEA, 0x04 });   // mov rcx, rbp; mov rdx, rsi; shr rdx, 4
    ok = InstallRollingCave() && ok;
    ok = InstallScalarCave(g_PredictedCave, gAddr.ThrowingImpl_GetPredictedPositionAtTime_GravitySite,
                           { 0x49, 0x8D, 0x4A, 0xE0, 0x4C, 0x89, 0xC2, 0x48, 0xD1, 0xEA }) && ok;   // lea rcx, [r10-0x20]; mov rdx, r8; shr rdx, 1
    return ok;
}

void Uninstall_ThrowingImpl_UpdateNextPosition_Caves()
{
    UninstallBuddyCodeCave(g_PredictedCave);
    UninstallBuddyCodeCave(g_RollingCave);
    UninstallBuddyCodeCave(g_FlyingCave);
}

bool ThrowingImpl_ItemGravityActive()
{
    return g_FlyingCave.installed;
}
