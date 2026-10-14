#include "pch.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "CpRadioVariants.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "SoldierFaction.h"
#include "Soldier2SoundControllerImpl_CallVoiceWithoutSync.h"
#include "SoldierCombatVoice.h"

namespace
{
    using ProcessDamages_t = void(__fastcall*)(void* self, std::uint32_t soldierIndex, void* damages, std::uint32_t count);

    ProcessDamages_t g_OrigProcessDamages = nullptr;

    void __fastcall hk_ProcessDamages(void* self, std::uint32_t soldierIndex, void* damages, std::uint32_t count)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigProcessDamages, self, soldierIndex, damages, count);

        SoldierFaction_OnSoldierDamages(self, soldierIndex, damages, count);
        SoldierVoiceSwap_OnSoldierDamages(self, soldierIndex, damages, count);
        SoldierCombatVoice_OnSoldierDamaged(soldierIndex);
        CpRadioVariants_OnSoldierDamages(soldierIndex, damages, count);
        g_OrigProcessDamages(self, soldierIndex, damages, count);
    }
}

bool Install_DamageHandlerImpl_ProcessDamages_Hook()
{
    void* target = ResolveGameAddress(gAddr.DamageHandlerImpl_ProcessDamages);
    if (!target)
        return false;
    return CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_ProcessDamages),
                               reinterpret_cast<void**>(&g_OrigProcessDamages));
}

bool Uninstall_DamageHandlerImpl_ProcessDamages_Hook()
{
    void* target = ResolveGameAddress(gAddr.DamageHandlerImpl_ProcessDamages);
    if (target)
        DisableAndRemoveHook(target);
    g_OrigProcessDamages = nullptr;
    return true;
}
