#include <pch.h>
#include "EquipSystemImpl_SetAmmoCount.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookArena.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "NativeCallPatch.h"
#include "TppEquip_ReloadEquipIdTable.h"
#include "log.h"
#include "../buddy/BuddyCodeCave.h"

namespace
{
    constexpr std::uint64_t kMeshAmo = 0x41B84E943DFC;             // MESH_AMO
    constexpr std::uint64_t kMeshAmoUnderBarrel = 0x8AAB6606DF43;  // MESH_AMO_UB

    const std::uint8_t kPrologue[] = {
        0x89, 0x54, 0x24, 0x10, 0x56, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x40,
        0x8B, 0xC2, 0x45, 0x0F, 0xB6, 0xF1                                            // movzx r14d,r9b: the under-barrel flag
    };
    constexpr std::uint32_t kAmmoMeshSite = 0x1A2;
    const std::uint8_t kAmmoMeshSiteBytes[] = {
        0x45, 0x85, 0xFF, 0x4C, 0x8B, 0x10,                                           // test r15d,r15d; mov r10,[rax]
        0x49, 0xB8, 0xFC, 0x3D, 0x94, 0x4E, 0xB8, 0x41, 0x00, 0x00,                   // mov r8,MESH_AMO
        0x41, 0x0F, 0x95, 0xC1                                                        // setne r9b
    };
    constexpr std::uint32_t kAmmoMeshLoad = kAmmoMeshSite + 6;
    constexpr std::uint32_t kAmmoMeshLoadSize = 10;

    NativeCodePatch g_Patch{};

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

    bool __fastcall UsesUnderBarrelAmmoMesh(std::uint32_t equipHandle)
    {
        if (MissionCodeGuard::ShouldBypassHooks())
            return false;
        return TppEquip_GetSubIdForEquipId(static_cast<int>((equipHandle >> 5) & 0x7FF)) != 0;
    }

    CaveCode BuildAmmoMeshThunk(std::uintptr_t resume)
    {
        CaveCode code;
        code.Emit({ 0x45, 0x84, 0xF6 });                                           // test r14b,r14b
        const std::size_t mainBarrel = code.Jcc8(0x74);
        code.Emit({ 0x50, 0x52, 0x41, 0x52 });                                     // push rax rdx r10
        code.Emit({ 0x48, 0x83, 0xEC, 0x28 });                                     // sub rsp,0x28
        code.Emit({ 0x8B, 0x8C, 0x24, 0xA8, 0x00, 0x00, 0x00 });                   // mov ecx,[rsp+0xa8]: the equip handle
        code.Emit({ 0x48, 0xB8 });                                                 // mov rax,UsesUnderBarrelAmmoMesh
        code.Imm64(reinterpret_cast<std::uintptr_t>(&UsesUnderBarrelAmmoMesh));
        code.Emit({ 0xFF, 0xD0 });                                                 // call rax
        code.Emit({ 0x48, 0x83, 0xC4, 0x28 });                                     // add rsp,0x28
        code.Emit({ 0x84, 0xC0 });                                                 // test al,al
        code.Emit({ 0x41, 0x5A, 0x5A, 0x58 });                                     // pop r10 rdx rax
        const std::size_t vanillaMesh = code.Jcc8(0x74);
        code.Emit({ 0x49, 0xB8 });                                                 // mov r8,MESH_AMO_UB
        code.Imm64(kMeshAmoUnderBarrel);
        const std::size_t meshSet = code.Jcc8(0xEB);
        code.Bind8(mainBarrel);
        code.Bind8(vanillaMesh);
        code.Emit({ 0x49, 0xB8 });                                                 // mov r8,MESH_AMO
        code.Imm64(kMeshAmo);
        code.Bind8(meshSet);
        code.Emit({ 0x45, 0x85, 0xFF });                                           // test r15d,r15d: setne r9b reads it
        code.JmpAbs(resume);
        return code;
    }
}

bool Install_EquipSystemImpl_SetAmmoCount()
{
    if (!AddressSetRuntime::IsEn154Family(gGameBuild) && !AddressSetRuntime::IsJp154Family(gGameBuild))
        return true;

    auto* fn = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.EquipSystemImpl_SetAmmoCount));
    const HookEntry entry = CheckHookEntry(fn, kPrologue, sizeof(kPrologue));
    if (entry == HookEntry::Unknown
        || !BytesAtSEH(fn + kAmmoMeshSite, kAmmoMeshSiteBytes, sizeof(kAmmoMeshSiteBytes)))
    {
        LogError("UnderBarrelAmmoMesh: SetAmmoCount does not match %s, custom weapons with an under-barrel launcher hide their magazine while the launcher is empty",
            GetGameBuildName(gGameBuild));
        return false;
    }

    const std::uintptr_t site = reinterpret_cast<std::uintptr_t>(fn) + kAmmoMeshLoad;
    const CaveCode code = BuildAmmoMeshThunk(site + kAmmoMeshLoadSize);
    auto* thunk = static_cast<std::uint8_t*>(HookArena::AllocateNear(site, code.bytes.size()));
    const std::int64_t rel = thunk
        ? static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(thunk)) - static_cast<std::int64_t>(site + 5)
        : 0;
    if (!thunk || rel < INT32_MIN || rel > INT32_MAX)
    {
        LogError("UnderBarrelAmmoMesh: no code page near SetAmmoCount, custom weapons with an under-barrel launcher hide their magazine while the launcher is empty");
        return false;
    }
    std::memcpy(thunk, code.bytes.data(), code.bytes.size());
    FlushInstructionCache(GetCurrentProcess(), thunk, code.bytes.size());

    g_Patch = NativeCodePatch{};
    g_Patch.target = reinterpret_cast<void*>(site);
    g_Patch.size = 5;
    std::memcpy(g_Patch.expected, fn + kAmmoMeshLoad, 5);
    g_Patch.replacement[0] = 0xE9;
    const std::int32_t rel32 = static_cast<std::int32_t>(rel);
    std::memcpy(g_Patch.replacement + 1, &rel32, sizeof(rel32));

    for (int attempt = 0; attempt < 50; ++attempt)
    {
        if (InstallNativeCodePatches(&g_Patch, 1))
        {
            if (entry == HookEntry::ForeignHook)
                LogHookedOnTop("UnderBarrelAmmoMesh: SetAmmoCount", fn);
            return true;
        }
        if (!NativePatchErrorIsTransient(GetLastError()))
            break;
        Sleep(4);
    }
    LogError("UnderBarrelAmmoMesh: SetAmmoCount patch refused (%s), custom weapons with an under-barrel launcher hide their magazine while the launcher is empty",
        NativePatchErrorName(GetLastError()));
    return false;
}

void Uninstall_EquipSystemImpl_SetAmmoCount()
{
    if (g_Patch.installed)
        RemoveNativeCodePatches(&g_Patch, 1);
}
