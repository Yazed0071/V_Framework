#include "pch.h"
#include "EquipIdTableImpl_GetSupportWeaponTypeId.h"

#include <Windows.h>
#include <intrin.h>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "SupportWeaponRegistry.h"
#include "log.h"

#pragma intrinsic(_ReturnAddress)

namespace
{
    using GetSupportWeaponTypeId_t = int(__fastcall*)(void* equipIdTable, std::uint32_t equipId);

    using AS = AddressSetRuntime::AddressSet;

    struct SlotMatchSite
    {
        std::uintptr_t AS::* field;
        std::ptrdiff_t       returnOffset;   // the call's return address, relative to the stock site
    };

    const SlotMatchSite kSlotMatchSites[] = {   // DoPickUpPlaced / DoPickUpWithPickableInfo: a pickup replaces the carried slot of the same type
        { &AS::PickUpActionPluginImpl_DoPickUpPlaced_SlotStockSite,     -0x127 },
        { &AS::PickUpActionPluginImpl_DoPickUpPlaced_SlotStockSite,     -0xA2  },
        { &AS::PickUpActionPluginImpl_DoPickUpWithPickableInfo_StockSite, -0x17A },
        { &AS::PickUpActionPluginImpl_DoPickUpWithPickableInfo_StockSite, -0x12B },
    };
    constexpr std::size_t   kSlotMatchCount = sizeof(kSlotMatchSites) / sizeof(kSlotMatchSites[0]);
    constexpr std::uint8_t  kCallVtable48[] = { 0xFF, 0x50, 0x48 };   // call qword ptr [reg+0x48]
    constexpr int           kPickUpLineBase = 0x10000;                // above every vanilla support weapon type

    GetSupportWeaponTypeId_t g_Orig   = nullptr;
    void*                    g_Target = nullptr;
    std::uintptr_t           g_SlotMatchReturns[kSlotMatchCount] = {};

    bool BytesMatchSafely(const void* at, const std::uint8_t* expected, std::size_t length)
    {
        __try
        {
            return std::memcmp(at, expected, length) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void ResolveSlotMatchSites()
    {
        bool allFound = true;
        for (std::size_t i = 0; i < kSlotMatchCount; ++i)
        {
            g_SlotMatchReturns[i] = 0;
            auto* site = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.*(kSlotMatchSites[i].field)));
            if (!site)
            {
                allFound = false;
                continue;
            }
            std::uint8_t* returnAddress = site + kSlotMatchSites[i].returnOffset;
            if (!BytesMatchSafely(returnAddress - sizeof(kCallVtable48), kCallVtable48, sizeof(kCallVtable48)))
            {
                allFound = false;
                continue;
            }
            g_SlotMatchReturns[i] = reinterpret_cast<std::uintptr_t>(returnAddress);
        }
        if (!allFound)
            LogError("SupportWeapon: pickup slot check not found on this build, a picked-up custom support weapon can replace another one with the same donor");
    }

    bool IsSlotMatchCall(std::uintptr_t returnAddress)
    {
        for (const std::uintptr_t site : g_SlotMatchReturns)
            if (site != 0 && site == returnAddress)
                return true;
        return false;
    }

    __declspec(noinline) int __fastcall hkGetSupportWeaponTypeId(void* equipIdTable, std::uint32_t equipId)
    {
        if (IsSlotMatchCall(reinterpret_cast<std::uintptr_t>(_ReturnAddress())))
        {
            const std::uint32_t line = SupportWeapon::PickUpLine(equipId);
            if (line != 0)
                return kPickUpLineBase + static_cast<int>(line);
        }
        const std::uint32_t familyFrom = SupportWeapon::FamilyFrom(equipId);   // alias or real id
        if (familyFrom != 0)
            return g_Orig(equipIdTable, familyFrom);
        return g_Orig(equipIdTable, equipId);
    }
}

bool Install_EquipIdTableImpl_GetSupportWeaponTypeId_Hook()
{
    ResolveSlotMatchSites();
    g_Target = ResolveGameAddress(gAddr.EquipIdTableImpl_GetSupportWeaponTypeId);
    if (!g_Target)
    {
        LogError("SupportWeapon: GetSupportWeaponTypeId address missing for this build, custom support weapons do nothing");
        return true;
    }
    if (!CreateAndEnableHook(g_Target, reinterpret_cast<void*>(&hkGetSupportWeaponTypeId),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("SupportWeapon: GetSupportWeaponTypeId hook failed, custom support weapons do nothing");
        g_Target = nullptr;
        return false;
    }
    return true;
}

void Uninstall_EquipIdTableImpl_GetSupportWeaponTypeId_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig   = nullptr;
}
