#include "pch.h"
#include "EquipDevelopControllerImpl_GetDamageAmountRatio.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "DevelopArrayGrow.h"
#include "EquipDevelopControllerImpl_GetEquipIDFromDevelopIndex.h"
#include "EquipParameterTablesImpl_ReloadEquipParameterTablesImpl.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "TppEquip_ReloadEquipIdTable.h"
#include "V_FrameWorkState.h"
#include "log.h"

namespace
{
    using GetDamageAmountRatio_t = float(__fastcall*)(void* controller, std::uint16_t developIndex);

    constexpr std::size_t kDevelopRowSize    = 0x68;
    constexpr float       kMaxDrawnRatio     = 0.97f;  // the panel's own cap

    GetDamageAmountRatio_t g_Orig   = nullptr;
    void*                  g_Target = nullptr;

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

    bool CustomDamageBar(void* controller, std::uint16_t developIndex, float& ratio)
    {
        if (!equip::IsValidFlowIndex(developIndex) || MissionCodeGuard::ShouldBypassHooks())
            return false;
        const int equipId = EquipIdOfRow(controller, developIndex);
        if (equipId <= 0 || !V_FrameWorkState::IsClaimedEquipId(equipId))
            return false;
        const int swpId = TppEquip_GetSupportWeaponIdForEquipId(equipId);
        if (swpId < static_cast<int>(EquipParameterTables::kFirstCustomSupportWeaponId))
            return false;
        EquipParameterTables::SupportWeaponBars bars;
        if (!EquipParameterTables::GetSupportWeaponBars(static_cast<std::uint32_t>(swpId), bars) || bars.damage < 0.0f)
            return false;
        ratio = bars.damage > kMaxDrawnRatio ? kMaxDrawnRatio : bars.damage;
        return true;
    }

    float __fastcall hkGetDamageAmountRatio(void* controller, std::uint16_t developIndex)
    {
        float ratio = 0.0f;
        if (CustomDamageBar(controller, developIndex, ratio))
            return ratio;
        return g_Orig(controller, developIndex);
    }
}

bool Install_EquipDevelopControllerImpl_GetDamageAmountRatio_Hook()
{
    g_Target = ResolveGameAddress(gAddr.EquipDevelopControllerImpl_GetDamageAmountRatio);
    if (!g_Target)
    {
        LogError("SupportWeapon: GetDamageAmountRatio address missing for this build, barDamage is ignored");
        return true;
    }
    if (!CreateAndEnableHook(g_Target, reinterpret_cast<void*>(&hkGetDamageAmountRatio),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("SupportWeapon: GetDamageAmountRatio hook failed, barDamage is ignored");
        g_Target = nullptr;
        return false;
    }
    return true;
}

void Uninstall_EquipDevelopControllerImpl_GetDamageAmountRatio_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig   = nullptr;
}
