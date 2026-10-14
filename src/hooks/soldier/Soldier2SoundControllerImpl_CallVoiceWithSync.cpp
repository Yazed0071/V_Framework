#include "pch.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "Soldier2SoundControllerImpl_CallVoiceWithSync.h"
#include "SoldierCombatVoice.h"
#include "log.h"

namespace
{
    using CallVoiceWithSync_t = bool(__fastcall*)(void* self, std::uint32_t slot, std::uint32_t dialogueEvent,
                                                  std::uint32_t priority, std::uint32_t line, bool flag, float delay);

    constexpr std::size_t kSoundSoldiers = 0x88;

    CallVoiceWithSync_t g_OrigCallVoice = nullptr;

    const std::uint8_t* SoldiersOf(void* soundController)
    {
        __try
        {
            return *reinterpret_cast<const std::uint8_t* const*>(static_cast<const std::uint8_t*>(soundController) + kSoundSoldiers);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    bool __fastcall hk_CallVoiceWithSync(void* self, std::uint32_t slot, std::uint32_t dialogueEvent,
                                         std::uint32_t priority, std::uint32_t line, bool flag, float delay)
    {
        const std::uint8_t* soldiers = SoldiersOf(self);
        line = SoldierCombatVoice_SwapLine(soldiers, slot, dialogueEvent, line);
        const bool accepted = g_OrigCallVoice(self, slot, dialogueEvent, priority, line, flag, delay);
        if (accepted)
            SoldierCombatVoice_OnSaid(soldiers, slot, dialogueEvent, line);
        return accepted;
    }
}

bool Install_Soldier2SoundControllerImpl_CallVoiceWithSync_Hook()
{
    void* target = ResolveGameAddress(gAddr.Soldier2SoundControllerImpl_CallVoiceWithSync);
    if (!target)
    {
        LogError("SoldierCombatVoice: CallVoiceWithSync address missing on this build, combat voice variants off");
        return false;
    }
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_CallVoiceWithSync),
                             reinterpret_cast<void**>(&g_OrigCallVoice)))
    {
        LogError("SoldierCombatVoice: CallVoiceWithSync hook failed, combat voice variants off");
        return false;
    }
    return true;
}

bool Uninstall_Soldier2SoundControllerImpl_CallVoiceWithSync_Hook()
{
    void* target = ResolveGameAddress(gAddr.Soldier2SoundControllerImpl_CallVoiceWithSync);
    if (target)
        DisableAndRemoveHook(target);
    g_OrigCallVoice = nullptr;
    SoldierCombatVoice_Reset();
    return true;
}
