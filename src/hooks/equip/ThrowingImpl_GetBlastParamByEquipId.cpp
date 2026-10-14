#include "pch.h"
#include "ThrowingImpl_GetBlastParamByEquipId.h"

#include <cstdint>

#include "AddressSet.h"
#include "EquipParameterTablesImpl_ReloadEquipParameterTablesImpl.h"
#include "HookUtils.h"
#include "SupportWeaponRegistry.h"
#include "log.h"

namespace
{
    using GetBlastParamByEquipId_t = const void*(__fastcall*)(void* throwing, std::uint32_t equipId);

    GetBlastParamByEquipId_t g_Orig   = nullptr;
    void*                    g_Target = nullptr;

    const void* __fastcall hkGetBlastParamByEquipId(void* throwing, std::uint32_t equipId)
    {
        const std::uint32_t item       = SupportWeapon::RunningItem(equipId);
        const std::uint32_t familyFrom = SupportWeapon::FamilyFrom(item);
        if (familyFrom == 0)
            return g_Orig(throwing, equipId);

        const int blastId = SupportWeapon::BlastId(item);
        if (blastId != SupportWeapon::kNoBlastId)
        {
            if (const void* row = EquipParameterTables::GetBlastRow(static_cast<std::uint32_t>(blastId)))
                return row;
        }
        return g_Orig(throwing, familyFrom);
    }
}

bool Install_ThrowingImpl_GetBlastParamByEquipId_Hook()
{
    g_Target = ResolveGameAddress(gAddr.ThrowingImpl_GetBlastParamByEquipId);
    if (!g_Target)
    {
        LogError("SupportWeapon: GetBlastParamByEquipId address missing for this build, thrown items use their family's blast");
        return true;
    }
    if (!CreateAndEnableHook(g_Target, reinterpret_cast<void*>(&hkGetBlastParamByEquipId),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("SupportWeapon: GetBlastParamByEquipId hook failed, thrown items use their family's blast");
        g_Target = nullptr;
        return false;
    }
    return true;
}

void Uninstall_ThrowingImpl_GetBlastParamByEquipId_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig   = nullptr;
}
