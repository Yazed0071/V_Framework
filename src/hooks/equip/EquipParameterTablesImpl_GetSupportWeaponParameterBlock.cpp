#include "pch.h"
#include "EquipParameterTablesImpl_GetSupportWeaponParameterBlock.h"

#include <intrin.h>
#include <cstdint>

#include "AddressSet.h"
#include "EquipAliasLease.h"
#include "EquipParameterTablesImpl_ReloadEquipParameterTablesImpl.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "TppEquip_ReloadEquipIdTable.h"
#include "V_FrameWorkState.h"
#include "log.h"

#pragma intrinsic(_ReturnAddress)

namespace
{
    using GetSupportWeaponParameterBlock_t = std::int32_t*(__fastcall*)(void* tables, std::int32_t* result,
                                                                        std::uint32_t equipId, float* block);

    constexpr std::uintptr_t kBarPanelSize   = 0x580;   // SetupSupportParameterDetails body
    constexpr int            kDurability     = 0;       // block: p1 value, p1 max, p2 value, p2 max
    constexpr int            kDurabilityMax  = 1;
    constexpr int            kEffectTime     = 2;
    constexpr int            kEffectTimeMax  = 3;

    GetSupportWeaponParameterBlock_t g_Orig     = nullptr;
    void*                            g_Target   = nullptr;
    std::uintptr_t                   g_BarPanel = 0;

    void ApplyBars(std::uint32_t swpId, float* block)
    {
        EquipParameterTables::SupportWeaponBars bars;
        if (!EquipParameterTables::GetSupportWeaponBars(swpId, bars))
            return;
        float p1 = 0.0f;
        float p2 = 0.0f;
        EquipParameterTables::SupportWeaponRowValues(swpId, p1, p2);

        if (bars.durability >= 0.0f)
        {
            block[kDurability]    = bars.durability;
            block[kDurabilityMax] = 1.0f;
        }
        else if (bars.durabilityMax > 0.0f)
        {
            block[kDurability]    = p1;
            block[kDurabilityMax] = bars.durabilityMax;
        }

        if (bars.effectTime >= 0.0f)
        {
            block[kEffectTime]    = bars.effectTime;
            block[kEffectTimeMax] = 1.0f;
        }
        else if (bars.effectTimeMax > 0.0f)
        {
            block[kEffectTime]    = p2;
            block[kEffectTimeMax] = bars.effectTimeMax;
        }
    }

    __declspec(noinline) std::int32_t* __fastcall hkGetSupportWeaponParameterBlock(void* tables, std::int32_t* result,
                                                                                   std::uint32_t equipId, float* block)
    {
        const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        std::int32_t* r = g_Orig(tables, result, equipId, block);
        if (!r || *r < 0 || !block || caller < g_BarPanel || caller >= g_BarPanel + kBarPanelSize)
            return r;
        const std::int32_t realId = equip::ResolveReal(static_cast<std::int32_t>(equipId));
        if (MissionCodeGuard::ShouldBypassHooks() || !V_FrameWorkState::IsClaimedEquipId(realId))
            return r;
        const int swpId = TppEquip_GetSupportWeaponIdForEquipId(realId);
        if (swpId >= static_cast<int>(EquipParameterTables::kFirstCustomSupportWeaponId))
            ApplyBars(static_cast<std::uint32_t>(swpId), block);
        return r;
    }
}

bool Install_EquipParameterTablesImpl_GetSupportWeaponParameterBlock_Hook()
{
    g_Target   = ResolveGameAddress(gAddr.EquipParameterTablesImpl_GetSupportWeaponParameterBlock);
    g_BarPanel = reinterpret_cast<std::uintptr_t>(ResolveGameAddress(gAddr.EquipDetailsCallbackImpl_SetupSupportParameterDetails));
    if (!g_Target || !g_BarPanel)
    {
        LogError("SupportWeapon: stat bar addresses missing for this build, barDurability and barEffectTime are ignored");
        g_Target = nullptr;
        return true;
    }
    if (!CreateAndEnableHook(g_Target, reinterpret_cast<void*>(&hkGetSupportWeaponParameterBlock),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("SupportWeapon: stat bar hook failed, barDurability and barEffectTime are ignored");
        g_Target = nullptr;
        return false;
    }
    return true;
}

void Uninstall_EquipParameterTablesImpl_GetSupportWeaponParameterBlock_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target   = nullptr;
    g_Orig     = nullptr;
    g_BarPanel = 0;
}
