#include "pch.h"
#include "PickableSystemImpl_CreateInstanceFromVars.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "../equip/EquipAliasLease.h"

namespace
{
    using CreateInstanceFromVarsFn = void(__fastcall*)(void* self);
    using GetQuarkSystemTableFn    = std::uint8_t*(__fastcall*)();

    const std::uint8_t kPrologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x55, 0x56, 0x57, 0x41, 0x54,
        0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x70
    };

    constexpr std::size_t kQuarkApp           = 0x98;
    constexpr std::size_t kAppScriptVars      = 0x10;
    constexpr int         kDroppedWeaponSlots = equip::kDroppedWeaponCellCount;

    CreateInstanceFromVarsFn g_Orig = nullptr;
    void*                    g_Addr = nullptr;

    std::uintptr_t CreateInstanceFromVarsAddr()
    {
        switch (gGameBuild)
        {
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4:  return 0x140FB9810ull;
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

    std::uint16_t* DroppedWeaponCellsSEH()
    {
        auto getQuark = reinterpret_cast<GetQuarkSystemTableFn>(
            ResolveGameAddress(gAddr.GetQuarkSystemTable));
        if (!getQuark)
            return nullptr;
        __try
        {
            std::uint8_t* quark = getQuark();
            if (!quark)
                return nullptr;
            auto* app = *reinterpret_cast<std::uint8_t**>(quark + kQuarkApp);
            if (!app)
                return nullptr;
            auto* vars = *reinterpret_cast<std::uint8_t**>(app + kAppScriptVars);
            return vars ? reinterpret_cast<std::uint16_t*>(vars + equip::kVarsDroppedWeapons) : nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    bool ReadCellsSEH(const std::uint16_t* cells, std::uint16_t* out)
    {
        __try
        {
            for (int i = 0; i < kDroppedWeaponSlots; ++i)
                out[i] = cells[i];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void ReplaceCellsSEH(std::uint16_t* cells, const std::uint16_t* from, const std::uint16_t* to)
    {
        __try
        {
            for (int i = 0; i < kDroppedWeaponSlots; ++i)
                if (from[i] != to[i] && cells[i] == from[i])
                    cells[i] = to[i];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void __fastcall hkCreateInstanceFromVars(void* self)
    {
        std::uint16_t* cells = DroppedWeaponCellsSEH();
        std::uint16_t savedIds[kDroppedWeaponSlots] = {};
        std::uint16_t realIds[kDroppedWeaponSlots] = {};
        std::uint16_t engineIds[kDroppedWeaponSlots] = {};
        bool swapped = false;
        if (cells && ReadCellsSEH(cells, savedIds))
        {
            for (int i = 0; i < kDroppedWeaponSlots; ++i)
            {
                realIds[i] = static_cast<std::uint16_t>(equip::ResolveReal(savedIds[i]));
                engineIds[i] = static_cast<std::uint16_t>(equip::ToEngine(realIds[i]));
                swapped = swapped || engineIds[i] != savedIds[i];
            }
        }
        if (swapped)
            ReplaceCellsSEH(cells, savedIds, engineIds);
        g_Orig(self);
        if (swapped)
            ReplaceCellsSEH(cells, engineIds, realIds);
    }
}

bool Install_PickableSystemImpl_CreateInstanceFromVars()
{
    const std::uintptr_t addr = CreateInstanceFromVarsAddr();
    if (addr == 0)
        return true;

    void* fn = ResolveGameAddress(addr);
    if (!fn || !PrologueMatchesSEH(fn))
    {
        LogError("EquipAlias: CreateInstanceFromVars prologue mismatch on %s, equipIds above 0x7FF stay off",
            GetGameBuildName(gGameBuild));
        return false;
    }
    if (!CreateAndEnableHook(fn, reinterpret_cast<void*>(&hkCreateInstanceFromVars),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("EquipAlias: CreateInstanceFromVars hook failed on %s, equipIds above 0x7FF stay off",
            GetGameBuildName(gGameBuild));
        return false;
    }
    g_Addr = fn;
    equip::NoteGateInstalled(equip::AliasGate::CreateInstanceFromVars, true);
    return true;
}

void Uninstall_PickableSystemImpl_CreateInstanceFromVars()
{
    equip::NoteGateInstalled(equip::AliasGate::CreateInstanceFromVars, false);
    if (g_Addr)
    {
        DisableAndRemoveHook(g_Addr);
        g_Addr = nullptr;
    }
    g_Orig = nullptr;
}
