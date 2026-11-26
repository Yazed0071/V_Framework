#include <pch.h>
#include "UiUtilityImpl_GetBatteryLevel.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "../equip/EquipControllerImpl_UpdateBattery.h"

namespace
{
    using GetBatteryLevel_t = std::uint8_t(__fastcall*)(void* self, int equipId);

    GetBatteryLevel_t g_Orig   = nullptr;
    void*             g_Target = nullptr;

    std::uint8_t __fastcall hkGetBatteryLevel(void* self, int equipId)
    {
        std::uint8_t level = 0;
        if (ItemBattery::GaugeLevel(equipId, level))
            return level;
        return g_Orig(self, equipId);
    }
}

bool Install_UiUtilityImpl_GetBatteryLevel_Hook()
{
    g_Target = ResolveGameAddress(gAddr.UiUtilityImpl_GetBatteryLevel);
    if (!g_Target)
    {
        LogError("ItemBattery: GetBatteryLevel address missing for this build, custom item battery gauges are not drawn");
        return false;
    }
    if (!CreateAndEnableHook(g_Target, reinterpret_cast<void*>(&hkGetBatteryLevel),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("ItemBattery: GetBatteryLevel hook failed, custom item battery gauges are not drawn");
        g_Target = nullptr;
        return false;
    }
    return true;
}

void Uninstall_UiUtilityImpl_GetBatteryLevel_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig   = nullptr;
}
