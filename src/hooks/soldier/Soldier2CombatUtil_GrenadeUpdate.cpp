#include <pch.h>
#include "Soldier2CombatUtil_GrenadeUpdate.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <initializer_list>

#include "AddressSet.h"
#include "ActionController_SetCommand.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"
#include "../buddy/BuddyCodeCave.h"
#include "../equip/EquipAliasLease.h"
#include "../equip/EquipIdCompression.h"

namespace
{
    constexpr std::int32_t  kVanillaGrenade = 0x15E;   // EQP_SWP_Grenade
    constexpr std::uint32_t kSoldierSlots   = 0x200;
    constexpr std::size_t   kPartGrenadeId  = 0x29C;   // RegistGrenadeId's shared ids in the Soldier2 part
    constexpr std::size_t   kPartStunId     = 0x2A0;

    constexpr std::uint8_t kGrenadeUpdateStore[]      = { 0x44, 0x89, 0x45, 0x08, 0x0F, 0xB6, 0x4D, 0x0C };   // mov [rbp+8],r8d; movzx ecx,byte [rbp+0xC]
    constexpr std::uint8_t kStepGrenadeStore[]        = { 0x89, 0x55, 0x38, 0x41, 0x0F, 0xB6, 0xC0 };         // mov [rbp+0x38],edx; movzx eax,r8b
    constexpr std::uint8_t kThrowGrenadeInDoorStore[] = { 0x89, 0x55, 0x98, 0x8B, 0xD6 };                     // mov [rbp-0x68],edx; mov edx,esi

    std::atomic<std::uint64_t> g_SoldierIds[kSoldierSlots] = {};   // stunId << 32 | grenadeId, 0 = the shared ids
    std::atomic<bool>          g_Installed{ false };

    BuddyCodeCave g_GrenadeUpdateCave      = { "SoldierGrenadeIds: combat grenade throw" };
    BuddyCodeCave g_StepGrenadeCave        = { "SoldierGrenadeIds: indoor grenade throw" };
    BuddyCodeCave g_ThrowGrenadeInDoorCave = { "SoldierGrenadeIds: grenade thrown through a door" };

    std::uint32_t __fastcall ThrowEquipId(std::uint32_t soldierIndex, std::uint32_t vanillaId, std::uint32_t grenadeOnly)   // called from the caves as the throw command is built
    {
        if (MissionCodeGuard::ShouldBypassHooks())
            return vanillaId;
        std::int32_t equipId = static_cast<std::int32_t>(vanillaId);
        const std::uint64_t ids = soldierIndex < kSoldierSlots ? g_SoldierIds[soldierIndex].load(std::memory_order_acquire) : 0;
        if (ids != 0)
        {
            const std::int32_t grenadeId = static_cast<std::int32_t>(static_cast<std::uint32_t>(ids));
            const std::int32_t stunId    = static_cast<std::int32_t>(static_cast<std::uint32_t>(ids >> 32));
            equipId = grenadeId != 0 ? grenadeId : kVanillaGrenade;
            if (stunId != 0 && grenadeOnly == 0)   // the game's own order: stunId wins unless this throw never takes it
                equipId = stunId;
        }
        if (!EquipIdCompression::IsWideEquipId(equipId))
            return static_cast<std::uint32_t>(equipId);
        const std::int32_t engineId = equip::ToEngine(equipId);   // the throw keeps 11 bits, so a wide id goes in as its alias
        if (engineId <= 0 || engineId == EquipIdCompression::kNoEquipId)
            return static_cast<std::uint32_t>(kVanillaGrenade);   // no alias free: the vanilla grenade
        return static_cast<std::uint32_t>(engineId);
    }

    bool ReadSharedIdsSeh(const std::uint8_t* part, std::int32_t& grenadeId, std::int32_t& stunId)
    {
        __try
        {
            grenadeId = *reinterpret_cast<const std::int32_t*>(part + kPartGrenadeId);
            stunId    = *reinterpret_cast<const std::int32_t*>(part + kPartStunId);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    CaveCode ThrowIdCave(std::initializer_list<std::uint8_t> loadArgs, std::initializer_list<std::uint8_t> storeResult,
                         std::initializer_list<std::uint8_t> secondOriginal, std::uintptr_t resume)
    {
        CaveCode c;
        c.Emit({ 0x50, 0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53, 0x53 });   // push rax, rcx, rdx, r8-r11, rbx
        c.Emit({ 0x48, 0x89, 0xE3, 0x48, 0x83, 0xE4, 0xF0 });                               // mov rbx, rsp; and rsp, -16
        c.Emit({ 0x48, 0x83, 0xEC, 0x60 });                                                 // sub rsp, 0x60
        c.Emit({ 0xF3, 0x0F, 0x7F, 0x04, 0x24 });                                           // movdqu [rsp], xmm0
        c.Emit({ 0xF3, 0x0F, 0x7F, 0x4C, 0x24, 0x10 });
        c.Emit({ 0xF3, 0x0F, 0x7F, 0x54, 0x24, 0x20 });
        c.Emit({ 0xF3, 0x0F, 0x7F, 0x5C, 0x24, 0x30 });
        c.Emit({ 0xF3, 0x0F, 0x7F, 0x64, 0x24, 0x40 });
        c.Emit({ 0xF3, 0x0F, 0x7F, 0x6C, 0x24, 0x50 });                                     // movdqu [rsp+0x50], xmm5
        c.Emit(loadArgs);                                                                   // ecx soldier index, edx vanilla id, r8d grenade-only
        c.Emit({ 0x48, 0x83, 0xEC, 0x20, 0x48, 0xB8 });                                     // sub rsp, 0x20; mov rax, imm64
        c.Imm64(reinterpret_cast<std::uintptr_t>(&ThrowEquipId));
        c.Emit({ 0xFF, 0xD0, 0x48, 0x83, 0xC4, 0x20 });                                     // call rax; add rsp, 0x20
        c.Emit({ 0xF3, 0x0F, 0x6F, 0x04, 0x24 });                                           // movdqu xmm0, [rsp]
        c.Emit({ 0xF3, 0x0F, 0x6F, 0x4C, 0x24, 0x10 });
        c.Emit({ 0xF3, 0x0F, 0x6F, 0x54, 0x24, 0x20 });
        c.Emit({ 0xF3, 0x0F, 0x6F, 0x5C, 0x24, 0x30 });
        c.Emit({ 0xF3, 0x0F, 0x6F, 0x64, 0x24, 0x40 });
        c.Emit({ 0xF3, 0x0F, 0x6F, 0x6C, 0x24, 0x50 });                                     // movdqu xmm5, [rsp+0x50]
        c.Emit({ 0x48, 0x89, 0xDC });                                                       // mov rsp, rbx
        c.Emit({ 0x5B, 0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59, 0x41, 0x58, 0x5A, 0x59 });       // pop rbx, r11-r8, rdx, rcx
        c.Emit(storeResult);                                                                // the throw command's equipId = eax
        c.Emit({ 0x58 });                                                                   // pop rax
        c.Emit(secondOriginal);
        c.JmpAbs(resume);
        return c;
    }

    bool InstallThrowIdCave(BuddyCodeCave& cave, std::uintptr_t site, const std::uint8_t* expected, std::size_t size,
                            std::initializer_list<std::uint8_t> loadArgs, std::initializer_list<std::uint8_t> storeResult,
                            std::initializer_list<std::uint8_t> secondOriginal)
    {
        if (!site)
        {
            LogError("%s: address missing on this build, soldiers there throw the shared RegistGrenadeId ids", cave.name);
            return false;
        }
        void* live = ResolveGameAddress(site);
        if (!live)
            return false;
        const std::uintptr_t resume = reinterpret_cast<std::uintptr_t>(live) + size;
        return InstallBuddyCodeCave(cave, site, expected, size, ThrowIdCave(loadArgs, storeResult, secondOriginal, resume));
    }
}

bool Install_Soldier2CombatUtil_GrenadeUpdate_Hook()
{
    if (g_Installed.load(std::memory_order_acquire))
        return true;
    const bool combat = InstallThrowIdCave(g_GrenadeUpdateCave, gAddr.Soldier2CombatUtil_GrenadeUpdateThrowIdSite,
        kGrenadeUpdateStore, sizeof(kGrenadeUpdateStore),
        { 0x44, 0x89, 0xE1, 0x44, 0x89, 0xC2, 0x44, 0x0F, 0xB6, 0x03 },   // mov ecx,r12d; mov edx,r8d; movzx r8d,byte [rbx] (the saved bl: 1 = grenade only)
        { 0x89, 0x45, 0x08 },                                             // mov [rbp+8],eax
        { 0x0F, 0xB6, 0x4D, 0x0C });                                      // movzx ecx,byte [rbp+0xC]
    const bool inDoor = InstallThrowIdCave(g_StepGrenadeCave, gAddr.CombatInDoorImpl_StepGrenadeThrowIdSite,
        kStepGrenadeStore, sizeof(kStepGrenadeStore),
        { 0x44, 0x89, 0xE1, 0x45, 0x31, 0xC0 },                           // mov ecx,r12d; xor r8d,r8d
        { 0x89, 0x45, 0x38 },                                             // mov [rbp+0x38],eax
        { 0x41, 0x0F, 0xB6, 0xC0 });                                      // movzx eax,r8b
    const bool door = InstallThrowIdCave(g_ThrowGrenadeInDoorCave, gAddr.CombatInDoorImpl_ThrowGrenadeInDoorThrowIdSite,
        kThrowGrenadeInDoorStore, sizeof(kThrowGrenadeInDoorStore),
        { 0x89, 0xF1, 0x45, 0x31, 0xC0 },                                 // mov ecx,esi; xor r8d,r8d
        { 0x89, 0x45, 0x98 },                                             // mov [rbp-0x68],eax
        { 0x8B, 0xD6 });                                                  // mov edx,esi
    if (!combat && !inDoor && !door)
    {
        LogError("SoldierGrenadeIds: no soldier throw site could be patched, soldiers throw vanilla ids only");
        return false;
    }
    if (!Install_ActionController_SetCommand_Hook())   // shared: a hit ends a ThrowGrenade command's hold on the soldier
        LogWarn("SoldierGrenadeIds: no SetCommand hook, a soldier hit during ThrowGrenade reacts once the throw ends");
    g_Installed.store(true, std::memory_order_release);
    return true;
}

bool Uninstall_Soldier2CombatUtil_GrenadeUpdate_Hook()
{
    g_Installed.store(false, std::memory_order_release);
    UninstallBuddyCodeCave(g_ThrowGrenadeInDoorCave);
    UninstallBuddyCodeCave(g_StepGrenadeCave);
    UninstallBuddyCodeCave(g_GrenadeUpdateCave);
    SoldierGrenadeIds_ClearForNewMission();
    return true;
}

bool SoldierGrenadeIds_SetForSoldier(std::uint32_t soldierIndex, std::int32_t grenadeId, std::int32_t stunId)
{
    if (!g_Installed.load(std::memory_order_acquire) || soldierIndex >= kSoldierSlots)
        return false;
    const std::uint64_t ids = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(stunId)) << 32)
                            | static_cast<std::uint32_t>(grenadeId);
    g_SoldierIds[soldierIndex].store(ids, std::memory_order_release);
    return true;
}

void SoldierGrenadeIds_ClearForNewMission()
{
    for (std::atomic<std::uint64_t>& ids : g_SoldierIds)
        ids.store(0, std::memory_order_relaxed);
}

std::int32_t SoldierGrenadeIds_Pick(std::uint32_t soldierIndex, const std::uint8_t* soldier2Part)
{
    std::int32_t grenadeId = 0;
    std::int32_t stunId = 0;
    const std::uint64_t ids = soldierIndex < kSoldierSlots ? g_SoldierIds[soldierIndex].load(std::memory_order_acquire) : 0;
    if (ids != 0)
    {
        grenadeId = static_cast<std::int32_t>(static_cast<std::uint32_t>(ids));
        stunId    = static_cast<std::int32_t>(static_cast<std::uint32_t>(ids >> 32));
    }
    else if (!soldier2Part || !ReadSharedIdsSeh(soldier2Part, grenadeId, stunId))
        return kVanillaGrenade;
    if (stunId != 0)
        return stunId;
    return grenadeId != 0 ? grenadeId : kVanillaGrenade;
}

bool SoldierGrenadeIds_Active()
{
    return g_Installed.load(std::memory_order_acquire);
}
