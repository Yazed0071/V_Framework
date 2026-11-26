#include <pch.h>
#include "ResourceManagementCallbackImpl_UpdateState.h"

#include <Windows.h>
#include <cstddef>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "../player/IdroidVoiceLines.h"
#include "../vehicle/VehicleSortieRows.h"

namespace
{
    using UpdateState_t = void(__fastcall*)(void* resourcePageState);

    const std::uint8_t kUpdateStateEntry[] = {
        0x4C, 0x8B, 0xDC, 0x56, 0x48, 0x81, 0xEC, 0xA0, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x05, 0xEE, 0x57,
        0x47, 0x01, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x44, 0x24, 0x70, 0x0F, 0xB6, 0x81, 0x54, 0x02, 0x00,
    };
    const std::uint8_t kUpdateStateEntryMask[] = {
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0,   // the stack cookie's rel32 differs on each build
        0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    };
    static_assert(sizeof(kUpdateStateEntry) == sizeof(kUpdateStateEntryMask), "one mask byte per entry byte");

    UpdateState_t g_OrigUpdateState = nullptr;
    void*         g_Target          = nullptr;
    int           g_Users           = 0;

    void __fastcall hkUpdateState(void* resourcePageState)
    {
        const VehicleSortieRows::ResourceStepScope vehicleStep = VehicleSortieRows::BeginResourceStep(resourcePageState);
        const IdroidVoiceResourceStep voiceStep = IdroidVoice_BeforeResourceStep();
        g_OrigUpdateState(resourcePageState);
        IdroidVoice_AfterResourceStep(resourcePageState, voiceStep);
        VehicleSortieRows::EndResourceStep(vehicleStep);
    }
}

bool Acquire_ResourceManagementCallbackImpl_UpdateState_Hook()
{
    if (g_Target)
    {
        ++g_Users;
        return true;
    }
    void* target = ResolveGameAddress(gAddr.ResourceManagementCallbackImpl_UpdateState);
    if (!target)
    {
        LogError("ResourceManagementCallbackImpl::UpdateState: no address on %s", GetGameBuildName(gGameBuild));
        return false;
    }
    const HookEntry state = CheckHookEntryMasked(target, kUpdateStateEntry, kUpdateStateEntryMask, sizeof(kUpdateStateEntry));
    if (state == HookEntry::Unknown)
    {
        LogError("ResourceManagementCallbackImpl::UpdateState: entry mismatch on %s", GetGameBuildName(gGameBuild));
        return false;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("ResourceManagementCallbackImpl::UpdateState", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkUpdateState), reinterpret_cast<void**>(&g_OrigUpdateState)))
    {
        LogError("ResourceManagementCallbackImpl::UpdateState: hook failed at %p", target);
        return false;
    }
    g_Target = target;
    g_Users = 1;
    return true;
}

void Release_ResourceManagementCallbackImpl_UpdateState_Hook()
{
    if (!g_Target || --g_Users > 0)
        return;
    DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Users = 0;
}
