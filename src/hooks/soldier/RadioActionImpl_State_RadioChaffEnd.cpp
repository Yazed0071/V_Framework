#include "pch.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "RadioActionImpl_State_RadioChaffEnd.h"
#include "Soldier2SoundControllerImpl_CallVoiceWithoutSync.h"
#include "log.h"

namespace
{
    using State_t = void(__fastcall*)(void* self, std::uint32_t actionId, int stateProc, void* param);

    constexpr int         kStateEnter     = 1;      // plays the jammed-radio motion, then RadioActionImpl::CallVoice
    constexpr std::size_t kActionSoldiers = 0x70;

    State_t           g_OrigState = nullptr;
    thread_local bool g_InChaffEnter = false;
    thread_local bool g_ShoutSeen = false;

    const std::uint8_t* SoldiersOf(void* action)
    {
        __try
        {
            return *reinterpret_cast<std::uint8_t* const*>(static_cast<std::uint8_t*>(action) + kActionSoldiers);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    void __fastcall hk_State_RadioChaffEnd(void* self, std::uint32_t actionId, int stateProc, void* param)
    {
        if (stateProc != kStateEnter || MissionCodeGuard::ShouldBypassHooks())
        {
            g_OrigState(self, actionId, stateProc, param);
            return;
        }
        g_InChaffEnter = true;
        g_ShoutSeen = false;
        g_OrigState(self, actionId, stateProc, param);
        g_InChaffEnter = false;
        if (!g_ShoutSeen)   // radio types vanilla has no shout for still get the take
            SoldierVoiceSwap_SayJammedTake(SoldiersOf(self), actionId);
    }
}

bool RadioChaffEnd_IsShoutingInstead()
{
    return g_InChaffEnter;
}

void RadioChaffEnd_NoteShout()
{
    g_ShoutSeen = true;
}

bool Install_RadioActionImpl_State_RadioChaffEnd_Hook()
{
    void* target = ResolveGameAddress(gAddr.RadioActionImpl_State_RadioChaffEnd);
    if (!target)
    {
        LogError("ChaffRadioVoice: State_RadioChaffEnd address missing on this build, jammed radios keep vanilla voice");
        return false;
    }
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_State_RadioChaffEnd), reinterpret_cast<void**>(&g_OrigState)))
    {
        LogError("ChaffRadioVoice: State_RadioChaffEnd hook failed, jammed radios keep vanilla voice");
        return false;
    }
    return true;
}

bool Uninstall_RadioActionImpl_State_RadioChaffEnd_Hook()
{
    void* target = ResolveGameAddress(gAddr.RadioActionImpl_State_RadioChaffEnd);
    if (target)
        DisableAndRemoveHook(target);
    g_OrigState = nullptr;
    return true;
}
