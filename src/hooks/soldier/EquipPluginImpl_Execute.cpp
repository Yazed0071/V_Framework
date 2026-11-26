#include <pch.h>
#include "EquipPluginImpl_Execute.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookArena.h"
#include "HookUtils.h"
#include "NativeCallPatch.h"
#include "log.h"
#include "../buddy/BuddyCodeCave.h"
#include "../equip/EquipAliasLease.h"

namespace
{
    const std::uint8_t kPrologue[] = {
        0x4C, 0x8B, 0xDC, 0x55, 0x53, 0x57, 0x41, 0x55, 0x41, 0x57,
        0x49, 0x8D, 0xAB, 0xB8, 0xF4, 0xFF, 0xFF, 0x48, 0x81, 0xEC, 0x20, 0x0C, 0x00, 0x00
    };

    constexpr std::uint32_t kMainWeaponCompare = 0x69A;   // cmp [rsp+0x74],eax; je
    const std::uint8_t kMainWeaponCompareBytes[] = { 0x39, 0x44, 0x24, 0x74, 0x74, 0x13 };
    constexpr std::uint32_t kMainWeaponRebuild = 0x6A0;
    constexpr std::uint32_t kMainWeaponKept    = 0x6B3;

    constexpr std::uint32_t kCarriedWeaponCompare = 0x6F3; // cmp edx,r8d; je
    const std::uint8_t kCarriedWeaponCompareBytes[] = { 0x41, 0x3B, 0xD0, 0x74, 0x0F };
    constexpr std::uint32_t kCarriedWeaponRebuild = 0x6F8;
    constexpr std::uint32_t kCarriedWeaponKept    = 0x707;

    NativeCodePatch g_Patches[2]{};

    std::uintptr_t ExecuteAddr()
    {
        return gAddr.SoldierEquipPluginImpl_Execute;
    }

    bool BytesAtSEH(const std::uint8_t* at, const std::uint8_t* bytes, std::size_t size)
    {
        __try
        {
            return at && std::memcmp(at, bytes, size) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool __fastcall IsAliasOfWantedWeapon(std::uint32_t wanted, std::uint32_t current) // the soldier table keeps the wide id, the equip pool its alias
    {
        return wanted != 0 && current != wanted
            && static_cast<std::uint32_t>(equip::FromEngine(static_cast<std::int32_t>(current))) == wanted;
    }

    void EmitCallIsAlias(CaveCode& code)
    {
        code.Emit({ 0x48, 0xB8 });                                                 // mov rax,IsAliasOfWantedWeapon
        code.Imm64(reinterpret_cast<std::uintptr_t>(&IsAliasOfWantedWeapon));
        code.Emit({ 0xFF, 0xD0 });                                                 // call rax
    }

    CaveCode BuildMainWeaponThunk(std::uintptr_t rebuild, std::uintptr_t kept)
    {
        CaveCode code;
        code.Emit({ 0x39, 0x44, 0x24, 0x74 });                                     // cmp [rsp+0x74],eax
        const std::size_t same = code.Jcc8(0x74);
        code.Emit({ 0x89, 0xC2 });                                                 // mov edx,eax: the pool's id
        code.Emit({ 0x8B, 0x4C, 0x24, 0x74 });                                     // mov ecx,[rsp+0x74]: the wanted id
        code.Emit({ 0x48, 0x83, 0xEC, 0x20 });                                     // sub rsp,0x20
        EmitCallIsAlias(code);
        code.Emit({ 0x48, 0x83, 0xC4, 0x20 });                                     // add rsp,0x20
        code.Emit({ 0x84, 0xC0 });                                                 // test al,al
        const std::size_t alias = code.Jcc8(0x75);
        code.JmpAbs(rebuild);
        code.Bind8(same);
        code.Bind8(alias);
        code.JmpAbs(kept);
        return code;
    }

    CaveCode BuildCarriedWeaponThunk(std::uintptr_t rebuild, std::uintptr_t kept)
    {
        CaveCode code;
        code.Emit({ 0x41, 0x3B, 0xD0 });                                           // cmp edx,r8d
        const std::size_t same = code.Jcc8(0x74);
        code.Emit({ 0x41, 0x51 });                                                 // push r9: SetUpCarryingEquipWeapon's 4th argument
        code.Emit({ 0x89, 0xD1 });                                                 // mov ecx,edx: the wanted id
        code.Emit({ 0x44, 0x89, 0xC2 });                                           // mov edx,r8d: the pool's id
        code.Emit({ 0x48, 0x83, 0xEC, 0x28 });                                     // sub rsp,0x28
        EmitCallIsAlias(code);
        code.Emit({ 0x48, 0x83, 0xC4, 0x28 });                                     // add rsp,0x28
        code.Emit({ 0x41, 0x59 });                                                 // pop r9
        code.Emit({ 0x84, 0xC0 });                                                 // test al,al
        const std::size_t alias = code.Jcc8(0x75);
        code.JmpAbs(rebuild);
        code.Bind8(same);
        code.Bind8(alias);
        code.JmpAbs(kept);
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

bool Install_EquipPluginImpl_Execute()
{
    const std::uintptr_t addr = ExecuteAddr();
    if (addr == 0)
        return true;

    auto* fn = static_cast<std::uint8_t*>(ResolveGameAddress(addr));
    const HookEntry entry = CheckHookEntry(fn, kPrologue, sizeof(kPrologue));
    if (entry == HookEntry::Unknown
        || !BytesAtSEH(fn + kMainWeaponCompare, kMainWeaponCompareBytes, sizeof(kMainWeaponCompareBytes))
        || !BytesAtSEH(fn + kCarriedWeaponCompare, kCarriedWeaponCompareBytes, sizeof(kCarriedWeaponCompareBytes)))
    {
        LogError("SoldierWideWeapon: soldier EquipPluginImpl::Execute does not match %s, soldiers with custom weapons rebuild them every update and can crash the game",
            GetGameBuildName(gGameBuild));
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("SoldierWideWeapon: soldier EquipPluginImpl::Execute", fn);

    const auto base = reinterpret_cast<std::uintptr_t>(fn);
    if (!PrepareJmpPatch(g_Patches[0], base + kMainWeaponCompare, kMainWeaponCompareBytes,
                         BuildMainWeaponThunk(base + kMainWeaponRebuild, base + kMainWeaponKept))
        || !PrepareJmpPatch(g_Patches[1], base + kCarriedWeaponCompare, kCarriedWeaponCompareBytes,
                            BuildCarriedWeaponThunk(base + kCarriedWeaponRebuild, base + kCarriedWeaponKept)))
    {
        LogError("SoldierWideWeapon: no code page near soldier EquipPluginImpl::Execute, soldiers with custom weapons rebuild them every update and can crash the game");
        return false;
    }

    for (int attempt = 0; attempt < 50; ++attempt)
    {
        if (InstallNativeCodePatches(g_Patches, 2))
            return true;
        if (!NativePatchErrorIsTransient(GetLastError()))
            break;
        Sleep(4);
    }
    LogError("SoldierWideWeapon: soldier EquipPluginImpl::Execute patch refused (%s), soldiers with custom weapons rebuild them every update and can crash the game",
        NativePatchErrorName(GetLastError()));
    return false;
}

void Uninstall_EquipPluginImpl_Execute()
{
    for (const NativeCodePatch& patch : g_Patches)
        if (patch.installed)
        {
            RemoveNativeCodePatches(g_Patches, 2);
            return;
        }
}
