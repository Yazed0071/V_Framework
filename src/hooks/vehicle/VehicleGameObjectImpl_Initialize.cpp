#include <pch.h>
#include "VehicleGameObjectImpl_Initialize.h"

#include <Windows.h>
#include <algorithm>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "CustomVehicles.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using Initialize_t = void(__fastcall*)(void* vehicles, std::uint8_t* creationDesc);

    constexpr std::size_t kDescRealizedCount = 0x2;   // u16: how many vehicles can be realized at once
    constexpr std::size_t kDescContextCount  = 0x4;   // u8: a vehicle body with realized vehicles needs one of these to be updated

    const std::uint8_t kInitializeEntry[] = {
        0x40, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24,
        0xD9, 0x48, 0x81, 0xEC, 0xE0, 0x00, 0x00, 0x00, 0x48, 0xC7, 0x45, 0x17, 0xFE, 0xFF, 0xFF, 0xFF,
    };

    Initialize_t g_OrigInitialize = nullptr;
    void*        g_HookTarget     = nullptr;

    void AddContextsForRegisteredVehiclesSEH(std::uint8_t* creationDesc, std::uint8_t registeredVehicles)
    {
        __try
        {
            std::uint16_t realizedCount = 0;
            std::memcpy(&realizedCount, creationDesc + kDescRealizedCount, sizeof(realizedCount));
            const std::uint32_t wanted = static_cast<std::uint32_t>(creationDesc[kDescContextCount]) + registeredVehicles;
            creationDesc[kDescContextCount] = static_cast<std::uint8_t>((std::min<std::uint32_t>)(wanted, realizedCount));   // the game never makes more contexts than realized vehicles either
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void __fastcall hk_Initialize(void* vehicles, std::uint8_t* creationDesc)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigInitialize, vehicles, creationDesc);
        const std::uint8_t registeredVehicles = CustomVehicles::RegisteredCount();
        if (creationDesc && registeredVehicles != 0)
            AddContextsForRegisteredVehiclesSEH(creationDesc, registeredVehicles);
        g_OrigInitialize(vehicles, creationDesc);
    }
}

bool Install_VehicleGameObjectImpl_Initialize_Hook()
{
    if (g_HookTarget)
        return true;
    auto* target = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.VehicleGameObjectImpl_Initialize));
    if (!target)
    {
        LogError("CustomVehicles: no address for the vehicle game object setup on %s, RegisterVehicle is off", GetGameBuildName(gGameBuild));
        return false;
    }
    const HookEntry entry = CheckHookEntry(target, kInitializeEntry, sizeof(kInitializeEntry));
    if (entry == HookEntry::Unknown)
    {
        char found[160] = {};
        DescribeFunctionEntry(target, found, sizeof(found));
        LogError("CustomVehicles: vehicle game object setup does not match %s (%s), RegisterVehicle is off", GetGameBuildName(gGameBuild), found);
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("CustomVehicles: vehicle GameObjectImpl::Initialize", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_Initialize), reinterpret_cast<void**>(&g_OrigInitialize)))
    {
        LogError("CustomVehicles: vehicle GameObjectImpl::Initialize hook failed, RegisterVehicle is off");
        return false;
    }
    g_HookTarget = target;
    return true;
}

void Uninstall_VehicleGameObjectImpl_Initialize_Hook()
{
    if (!g_HookTarget)
        return;
    DisableAndRemoveHook(g_HookTarget);
    g_HookTarget = nullptr;
    g_OrigInitialize = nullptr;
}
