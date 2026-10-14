#include "pch.h"
#include "EquipSystemImpl_SetUpEquip.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "EquipAliasLease.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using SetUpEquipFn = std::uint32_t*(__fastcall*)(void* self, std::uint32_t* outHandle,
                                                     std::uint32_t equipId, std::uint32_t option);

    const std::uint8_t kPrologue[] = {
        0x44, 0x89, 0x4C, 0x24, 0x20, 0x53, 0x41, 0x54, 0x41, 0x55,
        0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x20
    };

    SetUpEquipFn g_Orig = nullptr;
    void*        g_Addr = nullptr;

    std::uintptr_t SetUpEquipAddr()
    {
        switch (gGameBuild)
        {
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4:  return 0x140DC3250ull;
        default:                                           return 0;
        }
    }

    bool PrologueMatchesSEH(const void* fn)
    {
        __try
        {
            return std::memcmp(fn, kPrologue, sizeof(kPrologue)) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint32_t* __fastcall hkSetUpEquip(void* self, std::uint32_t* outHandle,
                                           std::uint32_t equipId, std::uint32_t option)
    {
        equip::NoteEquipSystem(self);
        const auto engineId = equip::ToEngine(equip::ResolveReal(static_cast<std::int32_t>(equipId)));
        return g_Orig(self, outHandle, static_cast<std::uint32_t>(engineId), option);
    }
}

bool Install_EquipSystemImpl_SetUpEquip()
{
    const std::uintptr_t addr = SetUpEquipAddr();
    if (addr == 0)
        return true;

    void* fn = ResolveGameAddress(addr);
    if (!fn || !PrologueMatchesSEH(fn))
    {
        LogError("EquipAlias: SetUpEquip prologue mismatch on %s, equipIds above 0x7FF stay off",
            GetGameBuildName(gGameBuild));
        return false;
    }
    if (!CreateAndEnableHook(fn, reinterpret_cast<void*>(&hkSetUpEquip),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("EquipAlias: SetUpEquip hook failed on %s, equipIds above 0x7FF stay off",
            GetGameBuildName(gGameBuild));
        return false;
    }
    g_Addr = fn;
    equip::NoteGateInstalled(equip::AliasGate::SetUpEquip, true);
    return true;
}

void Uninstall_EquipSystemImpl_SetUpEquip()
{
    equip::NoteGateInstalled(equip::AliasGate::SetUpEquip, false);
    if (g_Addr)
    {
        DisableAndRemoveHook(g_Addr);
        g_Addr = nullptr;
    }
    g_Orig = nullptr;
}
