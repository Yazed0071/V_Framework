#include <pch.h>

#include "RideWalkerGearActionImpl_GetVehicleEquipId.h"

#include "AddressSet.h"
#include "BuddyCodeCave.h"
#include "HookUtils.h"

namespace
{
    BuddyCodeCave g_ChangeWeaponCave{ "D-Walker sidearm weapon change" };
    BuddyCodeCave g_GetVehicleEquipIdCave{ "D-Walker sidearm HUD weapon" };
    BuddyCodeCave g_SetEquipWeaponIdCave{ "D-Walker sidearm weapon select" };
    BuddyCodeCave g_UpdateRiderUiCave{ "D-Walker sidearm crosshair" };

    constexpr std::uint8_t kChangeWeaponLoad[]      = { 0x0F, 0xB7, 0x83, 0x60, 0x03, 0x00, 0x00 };       // movzx eax, word ptr [rbx+0x360]
    constexpr std::uint8_t kGetVehicleEquipIdLoad[] = { 0x41, 0x0F, 0xB7, 0x80, 0x60, 0x03, 0x00, 0x00 }; // movzx eax, word ptr [r8+0x360]
    constexpr std::uint8_t kSetEquipWeaponIdLoad[]  = { 0x41, 0x0F, 0xB7, 0x86, 0x60, 0x03, 0x00, 0x00 }; // movzx eax, word ptr [r14+0x360]
    constexpr std::uint8_t kUpdateRiderUiLoad[]     = { 0x8B, 0x4C, 0xC8, 0x2C, 0xFF, 0xC9 };             // mov ecx, [rax+rcx*8+0x2c]; dec ecx

    constexpr std::uint8_t kLastVanillaSubWeaponType = 6;
    constexpr std::uint8_t kSubWeaponTypeSPistol     = 1;

    std::uint8_t g_VanillaSubWeaponTypeFor[256] = {};

    bool InstallSubWeaponTypeSwap(BuddyCodeCave& cave, std::uintptr_t siteAbs, const std::uint8_t* load, std::size_t size)
    {
        const auto site = reinterpret_cast<std::uintptr_t>(siteAbs ? ResolveGameAddress(siteAbs) : nullptr);

        CaveCode c;
        c.bytes.assign(load, load + size);
        c.Emit({ 0x83, 0xF8, kLastVanillaSubWeaponType });           // cmp eax, 6
        const std::size_t vanilla = c.Jcc8(0x76);                    // jbe vanilla
        c.Emit({ 0x3D, 0xFF, 0x00, 0x00, 0x00 });                    // cmp eax, 0xFF
        const std::size_t outOfRange = c.Jcc8(0x77);                 // ja vanilla
        c.Emit({ 0x51 });                                            // push rcx
        c.Emit({ 0x48, 0xB9 });                                      // mov rcx, g_VanillaSubWeaponTypeFor
        c.Imm64(reinterpret_cast<std::uint64_t>(g_VanillaSubWeaponTypeFor));
        c.Emit({ 0x0F, 0xB6, 0x04, 0x01 });                          // movzx eax, byte ptr [rcx+rax]
        c.Emit({ 0x59 });                                            // pop rcx
        c.Emit({ 0x85, 0xC0 });                                      // test eax, eax
        const std::size_t known = c.Jcc8(0x75);                      // jnz vanilla
        c.Emit({ 0xB8, kSubWeaponTypeSPistol, 0x00, 0x00, 0x00 });   // mov eax, 1
        c.Bind8(vanilla);
        c.Bind8(outOfRange);
        c.Bind8(known);
        c.JmpAbs(site + size);

        return InstallBuddyCodeCave(cave, siteAbs, load, size, c);
    }

    bool InstallRecordTypeSwap(BuddyCodeCave& cave, std::uintptr_t siteAbs)
    {
        const auto site = reinterpret_cast<std::uintptr_t>(siteAbs ? ResolveGameAddress(siteAbs) : nullptr);

        CaveCode c;
        c.Emit({ 0x8B, 0x4C, 0xC8, 0x2C });                          // mov ecx, [rax+rcx*8+0x2c]
        c.Emit({ 0x83, 0xF9, kLastVanillaSubWeaponType });           // cmp ecx, 6
        const std::size_t vanilla = c.Jcc8(0x76);                    // jbe vanilla
        c.Emit({ 0x81, 0xF9, 0xFF, 0x00, 0x00, 0x00 });              // cmp ecx, 0xFF
        const std::size_t outOfRange = c.Jcc8(0x77);                 // ja vanilla
        c.Emit({ 0x50 });                                            // push rax
        c.Emit({ 0x48, 0xB8 });                                      // mov rax, g_VanillaSubWeaponTypeFor
        c.Imm64(reinterpret_cast<std::uint64_t>(g_VanillaSubWeaponTypeFor));
        c.Emit({ 0x0F, 0xB6, 0x0C, 0x08 });                          // movzx ecx, byte ptr [rax+rcx]
        c.Emit({ 0x58 });                                            // pop rax
        c.Emit({ 0x85, 0xC9 });                                      // test ecx, ecx
        const std::size_t known = c.Jcc8(0x75);                      // jnz vanilla
        c.Emit({ 0xB9, kSubWeaponTypeSPistol, 0x00, 0x00, 0x00 });   // mov ecx, 1
        c.Bind8(vanilla);
        c.Bind8(outOfRange);
        c.Bind8(known);
        c.Emit({ 0xFF, 0xC9 });                                      // dec ecx
        c.JmpAbs(site + sizeof(kUpdateRiderUiLoad));

        return InstallBuddyCodeCave(cave, siteAbs, kUpdateRiderUiLoad, sizeof(kUpdateRiderUiLoad), c);
    }
}

void SetVanillaSubWeaponTypeFor(std::uint8_t subWeaponType, std::uint8_t vanillaSubWeaponType)
{
    g_VanillaSubWeaponTypeFor[subWeaponType] = vanillaSubWeaponType;
}

bool Install_RideWalkerGearActionImpl_GetVehicleEquipId_Patches()
{
    bool ok = InstallSubWeaponTypeSwap(g_ChangeWeaponCave, gAddr.RideWalkerGearActionImpl_ChangeWeapon_SubWeaponType,
                                       kChangeWeaponLoad, sizeof(kChangeWeaponLoad));
    ok &= InstallSubWeaponTypeSwap(g_GetVehicleEquipIdCave, gAddr.RideWalkerGearActionImpl_GetVehicleEquipId_SubWeaponType,
                                   kGetVehicleEquipIdLoad, sizeof(kGetVehicleEquipIdLoad));
    ok &= InstallSubWeaponTypeSwap(g_SetEquipWeaponIdCave, gAddr.RideWalkerGearActionImpl_SetEquipWeaponId_SubWeaponType,
                                   kSetEquipWeaponIdLoad, sizeof(kSetEquipWeaponIdLoad));
    ok &= InstallRecordTypeSwap(g_UpdateRiderUiCave, gAddr.RideWalkerGearActionImpl_UpdateRiderUi_SubWeaponType);
    return ok;
}

bool Uninstall_RideWalkerGearActionImpl_GetVehicleEquipId_Patches()
{
    UninstallBuddyCodeCave(g_UpdateRiderUiCave);
    UninstallBuddyCodeCave(g_SetEquipWeaponIdCave);
    UninstallBuddyCodeCave(g_GetVehicleEquipIdCave);
    UninstallBuddyCodeCave(g_ChangeWeaponCave);
    return true;
}
