#include "pch.h"
#include "ThrowingImpl_UpdateDeactivation.h"

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "LuaBroadcaster.h"
#include "MissionCodeGuard.h"
#include "ThrowingImpl_UpdateAction.h"
#include "ThrowingSystemLayout.h"
#include "log.h"

namespace
{
    using UpdateDeactivation_t = void(__fastcall*)(void* impl);

    constexpr std::size_t kImplItemCount = 0x50;

    UpdateDeactivation_t g_Orig   = nullptr;
    void*                g_Target = nullptr;

    void AnnounceRemovals(std::uint8_t* impl)
    {
        using namespace ThrowingSystemLayout;
        const std::uint32_t count = Read<std::uint32_t>(impl, kImplItemCount);
        bool announce = true;
        bool checkedMission = false;
        for (std::uint32_t local = 0; local < count; ++local)
        {
            const std::uint8_t* record = ImplRecord(impl, local);
            const std::uint32_t flags = Read<std::uint32_t>(record, kRecordFlags);
            if ((flags & kFlagAlive) == 0 || (flags & kFlagDone) == 0)
                continue;
            const std::uint32_t handle = HandleOf(impl, local);
            ThrowingImpl_ForgetActivation(handle);
            if (!checkedMission)
            {
                checkedMission = true;
                announce = !MissionCodeGuard::ShouldBypassHooks();
            }
            if (announce)
                V_FrameWork::QueueMessage("Throwing", "OnRemoveThrowing", handle, ImplRealEquipId(impl, local),
                                         Read<std::uint16_t>(record, kRecordOwner));
        }
    }

    void __fastcall hkUpdateDeactivation(void* impl)
    {
        AnnounceRemovals(static_cast<std::uint8_t*>(impl));
        g_Orig(impl);
    }
}

bool Install_ThrowingImpl_UpdateDeactivation_Hook()
{
    g_Target = ResolveGameAddress(gAddr.ThrowingImpl_UpdateDeactivation);
    if (!g_Target)
    {
        LogError("ThrowingSystem: UpdateDeactivation address missing for this build, OnRemoveThrowing is off");
        return true;
    }
    if (!CreateAndEnableHook(g_Target, reinterpret_cast<void*>(&hkUpdateDeactivation),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("ThrowingSystem: UpdateDeactivation hook failed, OnRemoveThrowing is off");
        g_Target = nullptr;
        return false;
    }
    return true;
}

void Uninstall_ThrowingImpl_UpdateDeactivation_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig   = nullptr;
}
