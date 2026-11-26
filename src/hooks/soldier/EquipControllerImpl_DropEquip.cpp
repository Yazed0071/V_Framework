#include <pch.h>
#include "EquipControllerImpl_DropEquip.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "../equip/EquipAliasLease.h"
#include "../equip/EquipIdCompression.h"

namespace
{
    using DropEquipFn = void(__fastcall*)(void* self, std::uint32_t soldierIndex, std::uint32_t handleIndex,
                                          std::uint32_t slot, std::uint16_t gameObjectId, float turn,
                                          std::uint32_t throwStyle);

    const std::uint8_t kPrologue[] = {
        0x40, 0x55, 0x56, 0x41, 0x56, 0x48, 0x8D, 0xAC, 0x24, 0x10, 0xFF, 0xFF, 0xFF,
        0x48, 0x81, 0xEC, 0xF0, 0x01, 0x00, 0x00
    };

    constexpr std::size_t   kWantedIds       = 0x30;   // u16 per soldier and slot, the id SetEquipId asked for: a wide id stays wide here
    constexpr std::uint32_t kSlotsPerSoldier = 3;

    DropEquipFn g_Orig = nullptr;
    void*       g_Addr = nullptr;

    std::uintptr_t DropEquipAddr()
    {
        return gAddr.SoldierEquipControllerImpl_DropEquip;
    }

    std::uint16_t* WantedIdCellSEH(void* self, std::uint32_t soldierIndex, std::uint32_t slot)
    {
        __try
        {
            auto* cells = *reinterpret_cast<std::uint16_t**>(static_cast<std::uint8_t*>(self) + kWantedIds);
            return cells ? cells + slot + soldierIndex * kSlotsPerSoldier : nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    bool ReadCellSEH(const std::uint16_t* cell, std::uint16_t& value)
    {
        __try
        {
            value = *cell;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void ReplaceCellSEH(std::uint16_t* cell, std::uint16_t from, std::uint16_t to)
    {
        __try
        {
            if (*cell == from)
                *cell = to;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void __fastcall hkDropEquip(void* self, std::uint32_t soldierIndex, std::uint32_t handleIndex, std::uint32_t slot,
                                std::uint16_t gameObjectId, float turn, std::uint32_t throwStyle)   // the pickable gets id & 0x7FF, so a wide weapon goes in as its alias
    {
        std::uint16_t* cell = WantedIdCellSEH(self, soldierIndex, slot);
        std::uint16_t wanted = 0;
        std::uint16_t engine = 0;
        const bool swapped = cell && ReadCellSEH(cell, wanted) && EquipIdCompression::IsWideEquipId(wanted);
        if (swapped)
        {
            engine = static_cast<std::uint16_t>(equip::ToEngine(wanted));
            if (engine == EquipIdCompression::kNoEquipId)
                engine = 0;   // no alias free: nothing drops rather than a vanilla gun
            ReplaceCellSEH(cell, wanted, engine);
        }
        g_Orig(self, soldierIndex, handleIndex, slot, gameObjectId, turn, throwStyle);
        if (swapped)
            ReplaceCellSEH(cell, engine, wanted);   // a carried weapon the drop cleared stays cleared
    }
}

bool Install_EquipControllerImpl_DropEquip()
{
    const std::uintptr_t addr = DropEquipAddr();
    if (addr == 0)
        return true;

    void* fn = ResolveGameAddress(addr);
    const HookEntry entry = CheckHookEntry(fn, kPrologue, sizeof(kPrologue));
    if (entry == HookEntry::Unknown)
    {
        char found[160] = {};
        DescribeFunctionEntry(fn, found, sizeof(found));
        LogError("SoldierWideWeapon: soldier EquipControllerImpl::DropEquip does not match %s (%s), soldiers drop custom weapons as vanilla ones",
            GetGameBuildName(gGameBuild), found);
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("SoldierWideWeapon: soldier EquipControllerImpl::DropEquip", fn);
    if (!CreateAndEnableHook(fn, reinterpret_cast<void*>(&hkDropEquip), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("SoldierWideWeapon: soldier EquipControllerImpl::DropEquip hook failed, soldiers drop custom weapons as vanilla ones");
        return false;
    }
    g_Addr = fn;
    return true;
}

void Uninstall_EquipControllerImpl_DropEquip()
{
    if (g_Addr)
    {
        DisableAndRemoveHook(g_Addr);
        g_Addr = nullptr;
    }
    g_Orig = nullptr;
}
