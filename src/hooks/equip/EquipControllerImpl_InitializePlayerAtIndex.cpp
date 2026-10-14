#include "pch.h"
#include "EquipControllerImpl_InitializePlayerAtIndex.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "GoneEquipCleanup.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using InitializePlayerAtIndex_t = void(__fastcall*)(void* equipController, std::uint32_t playerIndex, std::uint8_t initialize);

    constexpr std::size_t kControllerVars = 0x20;

    InitializePlayerAtIndex_t g_Orig   = nullptr;
    void*                     g_Target = nullptr;

    std::uint8_t* ControllerVars(void* equipController)
    {
        __try
        {
            return *reinterpret_cast<std::uint8_t**>(static_cast<std::uint8_t*>(equipController) + kControllerVars);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    void __fastcall hkInitializePlayerAtIndex(void* equipController, std::uint32_t playerIndex, std::uint8_t initialize)
    {
        if (initialize != 0)
            GoneEquipCleanup::CleanVars(ControllerVars(equipController));
        g_Orig(equipController, playerIndex, initialize);
    }
}

bool Install_EquipControllerImpl_InitializePlayerAtIndex_Hook()
{
    g_Target = ResolveGameAddress(gAddr.EquipControllerImpl_InitializePlayerAtIndex);
    if (!g_Target)
    {
        LogError("GoneEquipCleanup: InitializePlayerAtIndex address missing for this build, removed mods' items stay in the player's slots");
        return true;
    }
    if (!CreateAndEnableHook(g_Target, reinterpret_cast<void*>(&hkInitializePlayerAtIndex),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("GoneEquipCleanup: InitializePlayerAtIndex hook failed, removed mods' items stay in the player's slots");
        g_Target = nullptr;
        return false;
    }
    return true;
}

void Uninstall_EquipControllerImpl_InitializePlayerAtIndex_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig   = nullptr;
}
