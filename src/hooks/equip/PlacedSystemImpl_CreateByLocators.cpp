#include "pch.h"
#include "PlacedSystemImpl_CreateByLocators.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

#include "AddressSet.h"
#include "EquipAliasLease.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using CreateByLocators_t = void(__fastcall*)(void* placedSystem, std::uint32_t first, std::uint32_t count);

    const std::uint8_t kPrologue[] = {
        0x40, 0x53, 0x55, 0x56, 0x57, 0x48, 0x81, 0xEC, 0xA8, 0x00, 0x00, 0x00
    };

    constexpr std::size_t   kLocatorEquipOverrides = 0x220;   // u16* per placed index, ChangeEquipIdByLocatorName writes it, 0 = the locator's own id
    constexpr std::uint32_t kMaxLocatorCount       = 0x400;
    constexpr std::int32_t  kNoEquipId             = 0x7FF;

    struct OverrideSwap
    {
        std::uint16_t* cell;
        std::uint16_t  original;
        std::uint16_t  engineId;
    };

    CreateByLocators_t g_Orig = nullptr;
    void*              g_Addr = nullptr;
    std::atomic<bool>  g_RefusedLogged{ false };

    std::uintptr_t CreateByLocatorsAddr()
    {
        switch (gGameBuild)
        {
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4:  return 0x140FC71B0ull;
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

    std::uint16_t* OverridesSEH(const std::uint8_t* system)
    {
        __try
        {
            return *reinterpret_cast<std::uint16_t* const*>(system + kLocatorEquipOverrides);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    bool ReadU16SEH(const std::uint16_t* cell, std::uint16_t& out)
    {
        __try
        {
            out = *cell;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void WriteU16IfSEH(std::uint16_t* cell, std::uint16_t expect, std::uint16_t value)
    {
        __try
        {
            if (*cell == expect)
                *cell = value;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void SwapOverridesToEngine(void* placedSystem, std::uint32_t first, std::uint32_t count,
                               std::vector<OverrideSwap>& swaps)
    {
        std::uint16_t* overrides = OverridesSEH(static_cast<const std::uint8_t*>(placedSystem));
        if (!overrides || first >= kMaxLocatorCount || count > kMaxLocatorCount - first)
            return;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            std::uint16_t* cell = overrides + first + i;
            std::uint16_t value = 0;
            if (!ReadU16SEH(cell, value) || value == 0)
                continue;
            const std::int32_t engineId = equip::ToEngine(equip::ResolveReal(value));
            if (engineId == value)
                continue;
            if (engineId == kNoEquipId && !g_RefusedLogged.exchange(true))
                LogWarn("EquipAlias: locator placed item equipId 0x%X has no engine id now, it is placed empty", value);
            WriteU16IfSEH(cell, value, static_cast<std::uint16_t>(engineId));
            swaps.push_back({ cell, value, static_cast<std::uint16_t>(engineId) });
        }
    }

    void __fastcall hkCreateByLocators(void* placedSystem, std::uint32_t first, std::uint32_t count)
    {
        std::vector<OverrideSwap> swaps;
        if (placedSystem)
            SwapOverridesToEngine(placedSystem, first, count, swaps);
        g_Orig(placedSystem, first, count);
        for (const OverrideSwap& swap : swaps)
            WriteU16IfSEH(swap.cell, swap.engineId, swap.original);
    }
}

bool Install_PlacedSystemImpl_CreateByLocators_Hook()
{
    const std::uintptr_t addr = CreateByLocatorsAddr();
    if (addr == 0)
        return true;

    void* fn = ResolveGameAddress(addr);
    if (!fn || !PrologueMatchesSEH(fn))
    {
        LogError("EquipAlias: PlacedSystem CreateByLocators prologue mismatch on %s, ChangeEquipIdByLocatorName with equipIds above 0x7FF places the wrong item",
            GetGameBuildName(gGameBuild));
        return false;
    }
    if (!CreateAndEnableHook(fn, reinterpret_cast<void*>(&hkCreateByLocators),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("EquipAlias: PlacedSystem CreateByLocators hook failed on %s, ChangeEquipIdByLocatorName with equipIds above 0x7FF places the wrong item",
            GetGameBuildName(gGameBuild));
        return false;
    }
    g_Addr = fn;
    return true;
}

void Uninstall_PlacedSystemImpl_CreateByLocators_Hook()
{
    if (g_Addr)
    {
        DisableAndRemoveHook(g_Addr);
        g_Addr = nullptr;
    }
    g_Orig = nullptr;
}
