#include <pch.h>
#include "PickUpActionPluginImpl_DoPickUpWithPickableInfo.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "../equip/EquipAliasLease.h"

namespace
{
    using DoPickUpWithPickableInfoFn = bool(__fastcall*)(
        void* self, std::int32_t playerIndex, std::uint16_t* info, std::uint32_t a4,
        char a5, char a6, std::uint32_t a7, std::uint8_t a8, char a9,
        std::uint16_t* a10, std::uint64_t a11, char a12);

    const std::uint8_t kPrologue[] = {
        0x48, 0x8B, 0xC4, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55,
        0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0xA8, 0xE8, 0xFC, 0xFF, 0xFF
    };

    DoPickUpWithPickableInfoFn g_Orig = nullptr;
    void*                      g_Addr = nullptr;

    std::uintptr_t DoPickUpWithPickableInfoAddr()
    {
        return gAddr.PickUpActionPluginImpl_DoPickUpWithPickableInfo;
    }

    bool __fastcall hkDoPickUpWithPickableInfo(
        void* self, std::int32_t playerIndex, std::uint16_t* info, std::uint32_t a4,
        char a5, char a6, std::uint32_t a7, std::uint8_t a8, char a9,
        std::uint16_t* a10, std::uint64_t a11, char a12)
    {
        const bool pickedUp = g_Orig(self, playerIndex, info, a4, a5, a6, a7, a8, a9, a10, a11, a12);
        equip::KeepRealIdsInVarsCells(equip::kVarsDroppedWeapons, equip::kDroppedWeaponCellCount);
        return pickedUp;
    }
}

bool Install_PickUpActionPluginImpl_DoPickUpWithPickableInfo()
{
    const std::uintptr_t addr = DoPickUpWithPickableInfoAddr();
    if (addr == 0)
        return true;

    void* fn = ResolveGameAddress(addr);
    const HookEntry entry = CheckHookEntry(fn, kPrologue, sizeof(kPrologue));
    if (entry == HookEntry::Unknown)
    {
        LogError("EquipAlias: DoPickUpWithPickableInfo prologue mismatch on %s, equipIds above 0x7FF stay off",
            GetGameBuildName(gGameBuild));
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("EquipAlias: DoPickUpWithPickableInfo", fn);
    if (!CreateAndEnableHook(fn, reinterpret_cast<void*>(&hkDoPickUpWithPickableInfo),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("EquipAlias: DoPickUpWithPickableInfo hook failed on %s, equipIds above 0x7FF stay off",
            GetGameBuildName(gGameBuild));
        return false;
    }
    g_Addr = fn;
    equip::NoteGateInstalled(equip::AliasGate::DoPickUpWithPickableInfo, true);
    return true;
}

void Uninstall_PickUpActionPluginImpl_DoPickUpWithPickableInfo()
{
    equip::NoteGateInstalled(equip::AliasGate::DoPickUpWithPickableInfo, false);
    if (g_Addr)
    {
        DisableAndRemoveHook(g_Addr);
        g_Addr = nullptr;
    }
    g_Orig = nullptr;
}
