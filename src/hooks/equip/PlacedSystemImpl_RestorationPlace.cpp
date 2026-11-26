#include <pch.h>
#include "PlacedSystemImpl_RestorationPlace.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "EquipAliasLease.h"
#include "GoneEquipCleanup.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using RestorationPlace_t = void(__fastcall*)(void* placedSystem, void* vars);

    constexpr std::size_t  kVarsPlacedWeapons = 0x2172;   // vars.placedWeapons, u16 per user-placed item, real ids
    constexpr int          kPlacedCells       = 32;
    constexpr std::int32_t kNoEquipId         = 0x7FF;

    struct PlacedSwap
    {
        bool          swapped;
        std::uint16_t realId;
        std::uint16_t engineId;
    };

    RestorationPlace_t g_Orig   = nullptr;
    void*              g_Target = nullptr;

    bool ReadPlacedCellsSEH(const std::uint8_t* vars, std::uint16_t* out)
    {
        __try
        {
            const auto* cells = reinterpret_cast<const std::uint16_t*>(vars + kVarsPlacedWeapons);
            for (int i = 0; i < kPlacedCells; ++i)
                out[i] = cells[i];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void WritePlacedCellsSEH(std::uint8_t* vars, const PlacedSwap* swaps)
    {
        __try
        {
            auto* cells = reinterpret_cast<std::uint16_t*>(vars + kVarsPlacedWeapons);
            for (int i = 0; i < kPlacedCells; ++i)
                if (swaps[i].swapped)
                    cells[i] = swaps[i].engineId;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void SwapPlacedCellsToEngine(std::uint8_t* vars, PlacedSwap* swaps)
    {
        std::uint16_t cells[kPlacedCells] = {};
        if (!ReadPlacedCellsSEH(vars, cells))
            return;
        for (int i = 0; i < kPlacedCells; ++i)
        {
            const std::uint16_t value = cells[i];
            if (value == 0)
                continue;
            const std::int32_t realId = equip::ResolveReal(value);
            std::int32_t engineId = equip::ToEngine(realId);   // outside __try: it holds the lease lock while it binds an alias
            if (engineId == kNoEquipId && realId != kNoEquipId)
                engineId = 0;
            if (engineId == value)
                continue;
            swaps[i] = { true, static_cast<std::uint16_t>(realId), static_cast<std::uint16_t>(engineId) };
        }
        WritePlacedCellsSEH(vars, swaps);
    }

    void RestorePlacedCellsSEH(std::uint8_t* vars, const PlacedSwap* swaps)
    {
        __try
        {
            auto* cells = reinterpret_cast<std::uint16_t*>(vars + kVarsPlacedWeapons);
            for (int i = 0; i < kPlacedCells; ++i)
                if (swaps[i].swapped && cells[i] == swaps[i].engineId)
                    cells[i] = swaps[i].realId;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void __fastcall hkRestorationPlace(void* placedSystem, void* vars)
    {
        auto* bytes = static_cast<std::uint8_t*>(vars);
        GoneEquipCleanup::CleanVars(bytes);
        PlacedSwap swaps[kPlacedCells] = {};
        if (bytes)
            SwapPlacedCellsToEngine(bytes, swaps);
        g_Orig(placedSystem, vars);
        if (bytes)
            RestorePlacedCellsSEH(bytes, swaps);
    }
}

bool Install_PlacedSystemImpl_RestorationPlace_Hook()
{
    g_Target = ResolveGameAddress(gAddr.PlacedSystemImpl_RestorationPlace);
    if (!g_Target)
    {
        LogError("GoneEquipCleanup: RestorationPlace address missing for this build, removed mods' placed items come back on load");
        return true;
    }
    if (!CreateAndEnableHook(g_Target, reinterpret_cast<void*>(&hkRestorationPlace),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("GoneEquipCleanup: RestorationPlace hook failed, removed mods' placed items come back on load");
        g_Target = nullptr;
        return false;
    }
    return true;
}

void Uninstall_PlacedSystemImpl_RestorationPlace_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig   = nullptr;
}
