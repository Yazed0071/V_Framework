#include "pch.h"
#include "PlacedSystemImpl_CopyToScriptVarsPlayerPlacedWeapon.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "EquipAliasLease.h"
#include "GoneEquipCleanup.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using CopyToScriptVars_t = void(__fastcall*)(void* placedSystem, std::uint32_t index);

    const std::uint8_t kPrologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x18, 0x56, 0x48, 0x83, 0xEC, 0x20, 0xF6, 0x81, 0x00, 0x02, 0x00, 0x00, 0x01
    };

    constexpr std::size_t   kSystemFlags       = 0x200;    // bit0 = online, the original copies nothing then
    constexpr std::size_t   kFirstUserIndex    = 0x88;     // u32, first placed index the player owns
    constexpr std::size_t   kVarsPlacedWeapons = 0x2172;   // vars.placedWeapons, u16 per user-placed item
    constexpr std::uint32_t kPlacedCells       = 32;

    CopyToScriptVars_t g_Orig = nullptr;
    void*              g_Addr = nullptr;

    std::uintptr_t CopyToScriptVarsAddr()
    {
        switch (gGameBuild)
        {
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4:  return 0x140FC69A0ull;
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

    void KeepRealIdInVarsSEH(std::uint8_t* system, std::uint32_t index)
    {
        __try
        {
            if ((system[kSystemFlags] & 1) != 0)
                return;
            const std::uint32_t local = index - *reinterpret_cast<const std::uint32_t*>(system + kFirstUserIndex);
            if (local >= kPlacedCells)
                return;
            std::uint8_t* vars = GoneEquipCleanup::ScriptVars();
            if (!vars)
                return;
            auto* cell = reinterpret_cast<std::uint16_t*>(vars + kVarsPlacedWeapons) + local;
            const std::int32_t realId = equip::FromEngine(*cell);
            if (realId != *cell)
                *cell = static_cast<std::uint16_t>(realId);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void __fastcall hkCopyToScriptVars(void* placedSystem, std::uint32_t index)
    {
        g_Orig(placedSystem, index);
        if (placedSystem)
            KeepRealIdInVarsSEH(static_cast<std::uint8_t*>(placedSystem), index);
    }
}

bool Install_PlacedSystemImpl_CopyToScriptVarsPlayerPlacedWeapon_Hook()
{
    const std::uintptr_t addr = CopyToScriptVarsAddr();
    if (addr == 0)
        return true;

    void* fn = ResolveGameAddress(addr);
    if (!fn || !PrologueMatchesSEH(fn))
    {
        LogError("EquipAlias: CopyToScriptVarsPlayerPlacedWeapon prologue mismatch on %s, placed items above 0x7FF come back as another item after a reload",
            GetGameBuildName(gGameBuild));
        return false;
    }
    if (!CreateAndEnableHook(fn, reinterpret_cast<void*>(&hkCopyToScriptVars),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("EquipAlias: CopyToScriptVarsPlayerPlacedWeapon hook failed on %s, placed items above 0x7FF come back as another item after a reload",
            GetGameBuildName(gGameBuild));
        return false;
    }
    g_Addr = fn;
    return true;
}

void Uninstall_PlacedSystemImpl_CopyToScriptVarsPlayerPlacedWeapon_Hook()
{
    if (g_Addr)
    {
        DisableAndRemoveHook(g_Addr);
        g_Addr = nullptr;
    }
    g_Orig = nullptr;
}
