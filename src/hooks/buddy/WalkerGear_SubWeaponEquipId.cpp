#include <pch.h>

#include "WalkerGear_SubWeaponEquipId.h"

#include <atomic>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "BuddyCodeCave.h"
#include "BuddyOutfitRegistry.h"
#include "EquipAliasLease.h"
#include "HookUtils.h"
#include "EquipBlockController2_StartLoadingToEquipMissionBlock.h"
#include "EquipDevelopControllerImpl_GetDWalkerParts.h"
#include "EquipPartParams.h"
#include "RideWalkerGearActionImpl_GetVehicleEquipId.h"
#include "log.h"

namespace
{
    BuddyCodeCave g_Cave{ "D-Walker sidearm equip" };

    constexpr std::uint8_t kSiteBytes[] =
    {
        0x41, 0x83, 0xFB, 0x06, 0x77, 0x3E, 0x48, 0x8D, 0x05, 0x93, 0x73, 0xD3, 0x00, 0xB9, 0x01, 0x00,
        0x00, 0x00, 0x4D, 0x8B, 0xCB, 0x49, 0xC1, 0xE1, 0x04, 0x4C, 0x03, 0xC8, 0x44, 0x3A, 0xD1, 0x41,
        0x0F, 0xB6, 0xC2, 0x45, 0x0F, 0xB6, 0x41, 0x08, 0x0F, 0x47, 0xC8, 0x0F, 0xB6, 0xD1, 0x41, 0x8B,
        0xC0, 0x41, 0x3A, 0xD0, 0x0F, 0x42, 0xC2, 0x0F, 0xB6, 0xC8, 0x49, 0x8B, 0x01, 0xFF, 0xC9, 0x8B,
        0x2C, 0x88, 0xEB, 0x05, 0xBD, 0xFF, 0x07, 0x00, 0x00, 0x89, 0xAF, 0x50,
    };

    constexpr std::size_t kPatchSize    = 6;     // cmp r11d, 6 + ja
    constexpr std::size_t kVanillaTable = 0x06;  // lea rax, sidearm lists (r11 = SubWPType, r10 = SubWPGrade)
    constexpr std::size_t kListsRel     = 0x09;  // that lea's rel32, differs between EN and JP
    constexpr std::size_t kNoSidearm    = 0x44;  // mov ebp, 0x7FF
    constexpr std::size_t kStore        = 0x49;  // mov [rdi+0x350], ebp

    constexpr std::uint32_t kSidearmFallback = 7;     // EQP_WP_West_thg_010, the first DW S.PISTOL
    constexpr std::int32_t  kNoEquipId       = 0x7FF;
    constexpr std::uint32_t kEquipTypeSmg    = 3;

    constexpr std::uint8_t kSubWeaponTypeSPistol = 1;
    constexpr std::uint8_t kSubWeaponTypeD114    = 3;
    constexpr std::uint8_t kSubWeaponTypeZeev    = 5;

    std::atomic<std::uint32_t> g_LoggedTypes[256 / 32] = {};

    bool FirstTimeFor(std::uint32_t type)
    {
        const std::uint32_t bit = 1u << (type & 31);
        return (g_LoggedTypes[(type >> 5) & 7].fetch_or(bit, std::memory_order_relaxed) & bit) == 0;
    }

    std::uint32_t ResolveSidearm(std::uint32_t type)
    {
        SetVanillaSubWeaponTypeFor(static_cast<std::uint8_t>(type), kSubWeaponTypeSPistol);
        buddyoutfit::Entry e;
        if (!buddyoutfit::TryGetByValue(buddyoutfit::Kind::WalkerSidearm, static_cast<std::uint8_t>(type), &e))
        {
            if (FirstTimeFor(type))
                LogWarn("D-Walker sidearm type %u is not registered, the DW S.PISTOL is used", type);
            return kSidearmFallback;
        }
        if (!IsUsableWalkerSidearm(e))
            return kSidearmFallback;
        const std::int32_t engineId = equip::ToEngine(e.equipId);
        const bool usable = engineId > 0 && engineId < kNoEquipId;
        RequestWalkerSidearmPack(e.equipId, e.key.c_str());
        if (FirstTimeFor(type) && !usable)
            LogWarn("D-Walker sidearm %s: equipId %u got no engine id, the DW S.PISTOL is used", e.key.c_str(),
                static_cast<unsigned>(e.equipId));
        if (!usable)
            return kSidearmFallback;
        if (EquipParam_GetEquipType(e.equipId) == kEquipTypeSmg)
            SetVanillaSubWeaponTypeFor(static_cast<std::uint8_t>(type), kSubWeaponTypeZeev);
        else
            SetVanillaSubWeaponTypeFor(static_cast<std::uint8_t>(type), kSubWeaponTypeD114);
        return static_cast<std::uint32_t>(engineId);
    }
}

bool Install_WalkerGear_SubWeaponEquipId_Patch()
{
    const std::uintptr_t siteAbs = gAddr.WalkerGear_SubWeaponTypeCheck;
    const auto* site = static_cast<const std::uint8_t*>(siteAbs ? ResolveGameAddress(siteAbs) : nullptr);
    std::uint8_t mask[sizeof(kSiteBytes)];
    std::memset(mask, 1, sizeof(mask));
    std::memset(mask + kListsRel, 0, 4);
    if (!site || CheckHookEntryMasked(site, kSiteBytes, mask, sizeof(kSiteBytes)) != HookEntry::Expected)
    {
        LogError("BuddyOutfit: %s code differs on this build, custom D-Walker sidearms off", g_Cave.name);
        return false;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(site);

    CaveCode c;
    c.Emit({ 0x41, 0x83, 0xFB, 0x06 });                          // cmp r11d, 6
    const std::size_t custom = c.Jcc8(0x77);                     // ja custom
    c.JmpAbs(base + kVanillaTable);
    c.Bind8(custom);
    c.Emit({ 0x41, 0x81, 0xFB, 0xFF, 0x00, 0x00, 0x00 });        // cmp r11d, 0xFF
    const std::size_t resolve = c.Jcc8(0x76);                    // jbe resolve
    c.JmpAbs(base + kNoSidearm);
    c.Bind8(resolve);
    c.Emit({ 0x50, 0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53 }); // push rax, rcx, rdx, r8-r11
    c.Emit({ 0x48, 0x81, 0xEC, 0x88, 0x00, 0x00, 0x00 });        // sub rsp, 0x88 (shadow + xmm0-5, keeps 16-byte alignment)
    c.Emit({ 0xF3, 0x0F, 0x7F, 0x44, 0x24, 0x20 });              // movdqu [rsp+0x20], xmm0
    c.Emit({ 0xF3, 0x0F, 0x7F, 0x4C, 0x24, 0x30 });              // movdqu [rsp+0x30], xmm1
    c.Emit({ 0xF3, 0x0F, 0x7F, 0x54, 0x24, 0x40 });              // movdqu [rsp+0x40], xmm2
    c.Emit({ 0xF3, 0x0F, 0x7F, 0x5C, 0x24, 0x50 });              // movdqu [rsp+0x50], xmm3
    c.Emit({ 0xF3, 0x0F, 0x7F, 0x64, 0x24, 0x60 });              // movdqu [rsp+0x60], xmm4
    c.Emit({ 0xF3, 0x0F, 0x7F, 0x6C, 0x24, 0x70 });              // movdqu [rsp+0x70], xmm5
    c.Emit({ 0x44, 0x89, 0xD9 });                                // mov ecx, r11d
    c.Emit({ 0x48, 0xB8 });                                      // mov rax, ResolveSidearm
    c.Imm64(reinterpret_cast<std::uint64_t>(&ResolveSidearm));
    c.Emit({ 0xFF, 0xD0 });                                      // call rax
    c.Emit({ 0x89, 0xC5 });                                      // mov ebp, eax
    c.Emit({ 0xF3, 0x0F, 0x6F, 0x44, 0x24, 0x20 });              // movdqu xmm0, [rsp+0x20]
    c.Emit({ 0xF3, 0x0F, 0x6F, 0x4C, 0x24, 0x30 });              // movdqu xmm1, [rsp+0x30]
    c.Emit({ 0xF3, 0x0F, 0x6F, 0x54, 0x24, 0x40 });              // movdqu xmm2, [rsp+0x40]
    c.Emit({ 0xF3, 0x0F, 0x6F, 0x5C, 0x24, 0x50 });              // movdqu xmm3, [rsp+0x50]
    c.Emit({ 0xF3, 0x0F, 0x6F, 0x64, 0x24, 0x60 });              // movdqu xmm4, [rsp+0x60]
    c.Emit({ 0xF3, 0x0F, 0x6F, 0x6C, 0x24, 0x70 });              // movdqu xmm5, [rsp+0x70]
    c.Emit({ 0x48, 0x81, 0xC4, 0x88, 0x00, 0x00, 0x00 });        // add rsp, 0x88
    c.Emit({ 0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59, 0x41, 0x58, 0x5A, 0x59, 0x58 }); // pop r11-r8, rdx, rcx, rax
    c.JmpAbs(base + kStore);

    return InstallBuddyCodeCave(g_Cave, siteAbs, kSiteBytes, kPatchSize, c);
}

bool Uninstall_WalkerGear_SubWeaponEquipId_Patch()
{
    UninstallBuddyCodeCave(g_Cave);
    return true;
}
