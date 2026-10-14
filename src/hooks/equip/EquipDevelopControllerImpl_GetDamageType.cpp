#include "pch.h"
#include "EquipDevelopControllerImpl_GetDamageType.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "DevelopArrayGrow.h"
#include "EquipDevelopControllerImpl_GetEquipIDFromDevelopIndex.h"
#include "EquipParameterTablesImpl_ReloadEquipParameterTablesImpl.h"
#include "HookUtils.h"
#include "TppEquip_ReloadEquipIdTable.h"
#include "log.h"

namespace
{
    using GetDamageType_t = std::uint8_t(__fastcall*)(void* controller, std::uint16_t developIndex);

    constexpr std::size_t kDevelopRowSize    = 0x68;

    GetDamageType_t g_Orig   = nullptr;
    void*           g_Target = nullptr;

    int EquipIdOfRow(const void* controller, std::uint16_t developIndex)
    {
        __try
        {
            const auto* row = static_cast<const std::uint8_t*>(controller) + developIndex * kDevelopRowSize;
            return equip::DevelopRecordEquipId(row);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
    }

    std::uint8_t __fastcall hkGetDamageType(void* controller, std::uint16_t developIndex)
    {
        if (equip::IsValidFlowIndex(developIndex))
        {
            const int equipId = EquipIdOfRow(controller, developIndex);
            const int swpId = equipId > 0 ? TppEquip_GetSupportWeaponIdForEquipId(equipId) : -1;
            if (swpId >= 0)
            {
                const int damageType = EquipParameterTables::SupportWeaponDamageType(static_cast<std::uint32_t>(swpId));
                if (damageType != EquipParameterTables::kAutomaticDamageType)
                    return static_cast<std::uint8_t>(damageType);
            }
        }
        return g_Orig(controller, developIndex);
    }
}

bool Install_EquipDevelopControllerImpl_GetDamageType_Hook()
{
    g_Target = ResolveGameAddress(gAddr.EquipDevelopControllerImpl_GetDamageType);
    if (!g_Target)
    {
        LogError("SupportWeapon: GetDamageType address missing for this build, damageType is ignored");
        return true;
    }
    if (!CreateAndEnableHook(g_Target, reinterpret_cast<void*>(&hkGetDamageType),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("SupportWeapon: GetDamageType hook failed, damageType is ignored");
        g_Target = nullptr;
        return false;
    }
    return true;
}

void Uninstall_EquipDevelopControllerImpl_GetDamageType_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig   = nullptr;
}
