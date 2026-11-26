#include <pch.h>
#include "MissionPreparationSystemImpl_CalcEquipWeaponItemCostFromLoadout.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "EquipPartParams.h"
#include "GunBasicInject.h"
#include "HookArena.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "NativeCallPatch.h"
#include "TppEquip_ReloadEquipIdTable.h"
#include "log.h"
#include "../buddy/BuddyCodeCave.h"

namespace
{
    constexpr std::uint32_t kSuppressorLane = 0xA7; // SupplyCboxLoadoutInfo supply lanes start at +0xA2, lane 5 = suppressor box

    constexpr std::uint32_t kWeapon18RowRead = 0x461;  // loadout weapon at +0x18: movzx eax,byte [r8+0xa], row in r8
    constexpr std::uint32_t kWeapon2CRowRead = 0x1755; // loadout weapon at +0x2C: same
    constexpr std::uint32_t kWeapon40RowLeas = 0x10DF; // loadout weapon at +0x40: the two leas that leave the row in rax

    constexpr std::uint32_t kWeapon18AmmoDispatch = 0x990;    // test r8b,r8b; jne: the +0x18 weapon's under-barrel-only write vs main write
    constexpr std::uint32_t kWeapon18MainAmmoWrite = 0x999;
    constexpr std::uint32_t kWeapon18UnderBarrelAmmoWrite = 0xC03;

    const std::uint8_t kPrologue[] = {
        0x4C, 0x8B, 0xDC, 0x55, 0x41, 0x55, 0x49, 0x8D, 0xAB, 0xD8, 0xFC, 0xFF, 0xFF  // lea rbp,[r11-0x328]
    };
    constexpr std::uint32_t kInfoSave = 0x8F;
    const std::uint8_t kInfoSaveBytes[] = { 0x48, 0x89, 0x55, 0x90 };                  // mov [rbp-0x70],rdx
    const std::uint8_t kRowRead[] = { 0x41, 0x0F, 0xB6, 0x40, 0x0A };                  // movzx eax,byte [r8+0xa]
    const std::uint8_t kRowLeas[] = { 0x48, 0x8D, 0x14, 0x49, 0x48, 0x8D, 0x04, 0x90 }; // lea rdx,[rcx+rcx*2]; lea rax,[rax+rdx*4]
    const std::uint8_t kAmmoDispatch[] = { 0x45, 0x84, 0xC0, 0x0F, 0x85, 0x6A, 0x02, 0x00, 0x00 };
    const std::uint8_t kMainAmmoWrite[] = { 0x0F, 0xB6, 0x54, 0x24, 0x31 };          // movzx edx,byte [rsp+0x31]
    const std::uint8_t kUnderBarrelAmmoWrite[] = { 0x0F, 0xB6, 0x44, 0x24, 0x30 };   // movzx eax,byte [rsp+0x30]

    NativeCodePatch g_Patches[4]{};

    bool BytesAtSEH(const std::uint8_t* at, const std::uint8_t* bytes, std::size_t size)
    {
        __try
        {
            return std::memcmp(at, bytes, size) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void WriteByteSEH(std::uint8_t* at, std::uint8_t value)
    {
        __try
        {
            *at = value;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void __fastcall NoteRowSuppressor(const std::uint8_t* gunBasicRow, std::uint8_t* supplyInfo)
    {
        if (!gunBasicRow || !supplyInfo || MissionCodeGuard::ShouldBypassHooks())
            return;
        const int muzzleOption = GunBasic_CustomMuzzleOptionForRow(gunBasicRow);
        if (muzzleOption <= 0)
            return;
        const int durability = EquipParam_GetMuzzleOptionDurability(muzzleOption);
        if (durability < 1 || durability > 0xFE) // the test vanilla runs on a parts desc's muzzle row
            return;
        WriteByteSEH(supplyInfo + kSuppressorLane, 0xFF);
    }

    void EmitSaveXmm(CaveCode& code)
    {
        for (std::uint8_t x = 0; x < 6; ++x)
            code.Emit({ 0xF3, 0x0F, 0x7F, static_cast<std::uint8_t>(0x44 | (x << 3)), 0x24,
                        static_cast<std::uint8_t>(0x20 + x * 0x10) });             // movdqu [rsp+0x20+x*0x10],xmmx
    }

    void EmitCallNote(CaveCode& code)
    {
        code.Emit({ 0x48, 0x8B, 0x55, 0x90 });                                     // mov rdx,[rbp-0x70]
        code.Emit({ 0x48, 0xB8 });                                                 // mov rax,NoteRowSuppressor
        code.Imm64(reinterpret_cast<std::uintptr_t>(&NoteRowSuppressor));
        code.Emit({ 0xFF, 0xD0 });                                                 // call rax
        for (std::uint8_t x = 0; x < 6; ++x)
            code.Emit({ 0xF3, 0x0F, 0x6F, static_cast<std::uint8_t>(0x44 | (x << 3)), 0x24,
                        static_cast<std::uint8_t>(0x20 + x * 0x10) });             // movdqu xmmx,[rsp+0x20+x*0x10]
    }

    CaveCode BuildRowReadThunk(std::uintptr_t resume)
    {
        CaveCode code;
        code.Emit({ 0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53 }); // push rcx rdx r8 r9 r10 r11
        code.Emit({ 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00 });                 // sub rsp,0x80
        EmitSaveXmm(code);
        code.Emit({ 0x4C, 0x89, 0xC1 });                                           // mov rcx,r8
        EmitCallNote(code);
        code.Emit({ 0x48, 0x81, 0xC4, 0x80, 0x00, 0x00, 0x00 });                 // add rsp,0x80
        code.Emit({ 0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59, 0x41, 0x58, 0x5A, 0x59 }); // pop r11 r10 r9 r8 rdx rcx
        code.Emit({ 0x41, 0x0F, 0xB6, 0x40, 0x0A });                               // movzx eax,byte [r8+0xa]
        code.JmpAbs(resume);
        return code;
    }

    CaveCode BuildRowLeasThunk(std::uintptr_t resume)
    {
        CaveCode code;
        code.Emit({ 0x48, 0x8D, 0x14, 0x49, 0x48, 0x8D, 0x04, 0x90 });           // lea rdx,[rcx+rcx*2]; lea rax,[rax+rdx*4]
        code.Emit({ 0x50, 0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53 }); // push rax rcx rdx r8 r9 r10 r11
        code.Emit({ 0x48, 0x81, 0xEC, 0x88, 0x00, 0x00, 0x00 });                 // sub rsp,0x88
        EmitSaveXmm(code);
        code.Emit({ 0x48, 0x89, 0xC1 });                                           // mov rcx,rax
        EmitCallNote(code);
        code.Emit({ 0x48, 0x81, 0xC4, 0x88, 0x00, 0x00, 0x00 });                 // add rsp,0x88
        code.Emit({ 0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59, 0x41, 0x58, 0x5A, 0x59, 0x58 }); // pop r11 r10 r9 r8 rdx rcx rax
        code.JmpAbs(resume);
        return code;
    }

    int __fastcall OwnsUnderBarrelStartAmmo(std::uint32_t equipId)
    {
        if (MissionCodeGuard::ShouldBypassHooks())
            return 0;
        const int weaponId = TppEquip_GetSubIdForEquipId(static_cast<int>(equipId));
        int parts[11] = {};
        return weaponId > 0 && GunBasic_GetLogicalParts(weaponId, parts) ? 1 : 0;
    }

    CaveCode BuildUnderBarrelStartAmmoThunk(std::uintptr_t mainWrite, std::uintptr_t underBarrelOnlyWrite)
    {
        CaveCode code;
        code.Emit({ 0x45, 0x84, 0xC0 });                                           // test r8b,r8b
        const std::size_t underBarrelOnly = code.Jcc8(0x75);
        code.Emit({ 0x80, 0x7C, 0x24, 0x64, 0x00 });                               // cmp byte [rsp+0x64],0: the parts-desc path already wrote it
        const std::size_t descPath = code.Jcc8(0x75);
        code.Emit({ 0x80, 0x7C, 0x24, 0x30, 0x00 });                               // cmp byte [rsp+0x30],0: under-barrel bullet
        const std::size_t hasUnderBarrel = code.Jcc8(0x75);
        code.Bind8(descPath);
        code.JmpAbs(mainWrite);
        code.Bind8(underBarrelOnly);
        code.JmpAbs(underBarrelOnlyWrite);

        code.Bind8(hasUnderBarrel);
        code.Emit({ 0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53 }); // push rcx rdx r8 r9 r10 r11
        code.Emit({ 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00 });                 // sub rsp,0x80
        EmitSaveXmm(code);
        code.Emit({ 0x8B, 0x4D, 0x58 });                                           // mov ecx,[rbp+0x58]: the +0x18 weapon's equipId
        code.Emit({ 0x48, 0xB8 });                                                 // mov rax,OwnsUnderBarrelStartAmmo
        code.Imm64(reinterpret_cast<std::uintptr_t>(&OwnsUnderBarrelStartAmmo));
        code.Emit({ 0xFF, 0xD0 });                                                 // call rax
        for (std::uint8_t x = 0; x < 6; ++x)
            code.Emit({ 0xF3, 0x0F, 0x6F, static_cast<std::uint8_t>(0x44 | (x << 3)), 0x24,
                        static_cast<std::uint8_t>(0x20 + x * 0x10) });             // movdqu xmmx,[rsp+0x20+x*0x10]
        code.Emit({ 0x48, 0x81, 0xC4, 0x80, 0x00, 0x00, 0x00 });                 // add rsp,0x80
        code.Emit({ 0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59, 0x41, 0x58, 0x5A, 0x59 }); // pop r11 r10 r9 r8 rdx rcx
        code.Emit({ 0x85, 0xC0 });                                                 // test eax,eax
        const std::size_t owned = code.Jcc8(0x75);
        code.JmpAbs(mainWrite);

        code.Bind8(owned);
        code.Emit({ 0x51, 0x41, 0x51, 0x41, 0x54 });                               // push rcx r9 r12: the main write still needs rcx and r12
        code.Emit({ 0x44, 0x0F, 0xB7, 0x64, 0x24, 0x60 });                         // movzx r12d,word [rsp+0x60]: under-barrel carry
        code.Emit({ 0x66, 0x41, 0x83, 0xFC, 0xFF });                               // cmp r12w,0xffff
        const std::size_t unlimited1 = code.Jcc8(0x74);
        code.Emit({ 0x66, 0x45, 0x85, 0xE4 });                                     // test r12w,r12w
        const std::size_t unlimited2 = code.Jcc8(0x74);
        code.Emit({ 0x66, 0x41, 0x81, 0xFC, 0xFF, 0x3F });                         // cmp r12w,0x3fff
        const std::size_t unlimited3 = code.Jcc8(0x74);
        code.Emit({ 0x41, 0x0F, 0xB7, 0xC4 });                                     // movzx eax,r12w
        code.Emit({ 0x66, 0x0F, 0x6E, 0xC0 });                                     // movd xmm0,eax
        code.Emit({ 0x0F, 0x5B, 0xC0 });                                           // cvtdq2ps xmm0,xmm0
        code.Emit({ 0xF3, 0x0F, 0x59, 0xC6 });                                     // mulss xmm0,xmm6: refill ratio
        code.Emit({ 0xF3, 0x41, 0x0F, 0x58, 0xC5 });                               // addss xmm0,xmm13: +0.5
        code.Emit({ 0xF3, 0x48, 0x0F, 0x2C, 0xC0 });                               // cvttss2si rax,xmm0
        code.Emit({ 0x66, 0x44, 0x01, 0xE8 });                                     // add ax,r13w: rounds kept from last sortie
        const std::size_t nothing = code.Jcc8(0x74);
        code.Emit({ 0x4C, 0x8B, 0x8C, 0x24, 0x90, 0x00, 0x00, 0x00 });             // mov r9,[rsp+0x90]: ScriptVars
        code.Emit({ 0x66, 0x3D, 0xFF, 0x3F });                                     // cmp ax,0x3fff
        const std::size_t addStock = code.Jcc8(0x75);
        code.Emit({ 0x66, 0x41, 0xC7, 0x81, 0x16, 0x06, 0x00, 0x00, 0xFF, 0x3F }); // mov word [r9+0x616],0x3fff
        const std::size_t stockSet = code.Jcc8(0xEB);
        code.Bind8(addStock);
        code.Emit({ 0x66, 0x41, 0x01, 0x81, 0x16, 0x06, 0x00, 0x00 });             // add word [r9+0x616],ax
        const std::size_t stockAdded = code.Jcc8(0xEB);
        code.Bind8(unlimited1);
        code.Bind8(unlimited2);
        code.Bind8(unlimited3);
        code.Emit({ 0x4C, 0x8B, 0x8C, 0x24, 0x90, 0x00, 0x00, 0x00 });             // mov r9,[rsp+0x90]
        code.Emit({ 0x66, 0x41, 0xC7, 0x81, 0x16, 0x06, 0x00, 0x00, 0xFF, 0x3F }); // mov word [r9+0x616],0x3fff
        code.Bind8(stockSet);
        code.Bind8(stockAdded);
        code.Emit({ 0x0F, 0xB7, 0x4C, 0x24, 0x62 });                               // movzx ecx,word [rsp+0x62]: under-barrel capacity
        code.Emit({ 0x66, 0x41, 0x89, 0x89, 0x60, 0x05, 0x00, 0x00 });             // mov word [r9+0x560],cx
        code.Emit({ 0x41, 0x0F, 0xB7, 0x81, 0x16, 0x06, 0x00, 0x00 });             // movzx eax,word [r9+0x616]
        code.Emit({ 0x66, 0x39, 0xC1 });                                           // cmp cx,ax
        const std::size_t loadedFits = code.Jcc8(0x76);
        code.Emit({ 0x66, 0x41, 0x89, 0x81, 0x60, 0x05, 0x00, 0x00 });             // mov word [r9+0x560],ax
        code.Bind8(loadedFits);
        code.Bind8(nothing);
        code.Emit({ 0x41, 0x5C, 0x41, 0x59, 0x59 });                               // pop r12 r9 rcx
        code.JmpAbs(mainWrite);
        return code;
    }

    bool PrepareJmpPatch(NativeCodePatch& patch, std::uintptr_t site, const std::uint8_t* expected, const CaveCode& code)
    {
        auto* thunk = static_cast<std::uint8_t*>(HookArena::AllocateNear(site, code.bytes.size()));
        const std::int64_t rel = thunk
            ? static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(thunk)) - static_cast<std::int64_t>(site + 5)
            : 0;
        if (!thunk || rel < INT32_MIN || rel > INT32_MAX)
            return false;
        std::memcpy(thunk, code.bytes.data(), code.bytes.size());
        FlushInstructionCache(GetCurrentProcess(), thunk, code.bytes.size());

        patch = NativeCodePatch{};
        patch.target = reinterpret_cast<void*>(site);
        patch.size = 5;
        std::memcpy(patch.expected, expected, 5);
        patch.replacement[0] = 0xE9;
        const std::int32_t rel32 = static_cast<std::int32_t>(rel);
        std::memcpy(patch.replacement + 1, &rel32, sizeof(rel32));
        return true;
    }
}

bool Install_MissionPreparationSystemImpl_CalcEquipWeaponItemCostFromLoadout()
{
    if (!AddressSetRuntime::IsEn154Family(gGameBuild) && !AddressSetRuntime::IsJp154Family(gGameBuild))
        return true;

    auto* fn = static_cast<std::uint8_t*>(
        ResolveGameAddress(gAddr.MissionPreparationSystemImpl_CalcEquipWeaponItemCostFromLoadout));
    const HookEntry entry = CheckHookEntry(fn, kPrologue, sizeof(kPrologue));
    if (entry == HookEntry::Unknown
        || !BytesAtSEH(fn + kInfoSave, kInfoSaveBytes, sizeof(kInfoSaveBytes))
        || !BytesAtSEH(fn + kWeapon18RowRead, kRowRead, sizeof(kRowRead))
        || !BytesAtSEH(fn + kWeapon2CRowRead, kRowRead, sizeof(kRowRead))
        || !BytesAtSEH(fn + kWeapon40RowLeas, kRowLeas, sizeof(kRowLeas))
        || !BytesAtSEH(fn + kWeapon18AmmoDispatch, kAmmoDispatch, sizeof(kAmmoDispatch))
        || !BytesAtSEH(fn + kWeapon18MainAmmoWrite, kMainAmmoWrite, sizeof(kMainAmmoWrite))
        || !BytesAtSEH(fn + kWeapon18UnderBarrelAmmoWrite, kUnderBarrelAmmoWrite, sizeof(kUnderBarrelAmmoWrite)))
    {
        LogError("LoadoutCost: CalcEquipWeaponItemCostFromLoadout does not match %s, custom weapons get no suppressor box in the iDroid resupply and deploy with an empty under-barrel",
            GetGameBuildName(gGameBuild));
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("LoadoutCost: CalcEquipWeaponItemCostFromLoadout", fn);

    const auto base = reinterpret_cast<std::uintptr_t>(fn);
    if (!PrepareJmpPatch(g_Patches[0], base + kWeapon18RowRead, kRowRead,
                         BuildRowReadThunk(base + kWeapon18RowRead + sizeof(kRowRead)))
        || !PrepareJmpPatch(g_Patches[1], base + kWeapon2CRowRead, kRowRead,
                            BuildRowReadThunk(base + kWeapon2CRowRead + sizeof(kRowRead)))
        || !PrepareJmpPatch(g_Patches[2], base + kWeapon40RowLeas, kRowLeas,
                            BuildRowLeasThunk(base + kWeapon40RowLeas + sizeof(kRowLeas)))
        || !PrepareJmpPatch(g_Patches[3], base + kWeapon18AmmoDispatch, kAmmoDispatch,
                            BuildUnderBarrelStartAmmoThunk(base + kWeapon18MainAmmoWrite,
                                                           base + kWeapon18UnderBarrelAmmoWrite)))
    {
        LogError("LoadoutCost: no code page near CalcEquipWeaponItemCostFromLoadout, custom weapons get no suppressor box in the iDroid resupply and deploy with an empty under-barrel");
        return false;
    }

    for (int attempt = 0; attempt < 50; ++attempt)
    {
        if (InstallNativeCodePatches(g_Patches, 4))
            return true;
        if (!NativePatchErrorIsTransient(GetLastError()))
            break;
        Sleep(4);
    }
    LogError("LoadoutCost: CalcEquipWeaponItemCostFromLoadout patch refused (%s), custom weapons get no suppressor box in the iDroid resupply and deploy with an empty under-barrel",
        NativePatchErrorName(GetLastError()));
    return false;
}

void Uninstall_MissionPreparationSystemImpl_CalcEquipWeaponItemCostFromLoadout()
{
    for (const NativeCodePatch& patch : g_Patches)
        if (patch.installed)
        {
            RemoveNativeCodePatches(g_Patches, 4);
            return;
        }
}
