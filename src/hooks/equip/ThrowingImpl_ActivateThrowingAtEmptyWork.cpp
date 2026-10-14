#include "pch.h"
#include "ThrowingImpl_ActivateThrowingAtEmptyWork.h"

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "LuaBroadcaster.h"
#include "MissionCodeGuard.h"
#include "ThrowingImpl_UpdateAction.h"
#include "ThrowingImpl_UpdateStatus.h"
#include "ThrowingSystemLayout.h"
#include "log.h"

namespace
{
    using ActivateThrowingAtEmptyWork_t = std::uint32_t(__fastcall*)(void* impl, const void* desc);

    constexpr std::uint32_t kNoItem         = 0x3F;
    constexpr std::size_t   kDescOwner      = 0x32;
    constexpr std::size_t   kDescEquipId    = 0x3C;   // low 11 bits
    constexpr std::uint16_t kEquipIdMask    = 0x7FF;

    ActivateThrowingAtEmptyWork_t g_Orig   = nullptr;
    void*                         g_Target = nullptr;

    std::uint32_t __fastcall hkActivateThrowingAtEmptyWork(void* impl, const void* desc)
    {
        const std::uint32_t localIndex = g_Orig(impl, desc);
        if (localIndex == kNoItem)
            return localIndex;

        using namespace ThrowingSystemLayout;
        const auto* bytes = static_cast<const std::uint8_t*>(impl);
        const std::uint32_t handle = HandleOf(bytes, localIndex);
        ThrowingImpl_ForgetActivation(handle);
        ThrowingImpl_ForgetStop(handle);
        if (MissionCodeGuard::ShouldBypassHooks())
            return localIndex;

        const auto* descBytes = static_cast<const std::uint8_t*>(desc);
        const std::uint16_t equipId = static_cast<std::uint16_t>(
            equip::FromEngine(Read<std::uint16_t>(descBytes, kDescEquipId) & kEquipIdMask));
        const std::uint16_t owner   = Read<std::uint16_t>(descBytes, kDescOwner);
        V_FrameWork::QueueMessage("Throwing", "OnThrow", handle, equipId, owner);
        return localIndex;
    }
}

bool Install_ThrowingImpl_ActivateThrowingAtEmptyWork_Hook()
{
    g_Target = ResolveGameAddress(gAddr.ThrowingImpl_ActivateThrowingAtEmptyWork);
    if (!g_Target)
    {
        LogError("ThrowingSystem: ActivateThrowingAtEmptyWork address missing for this build, OnThrow is off");
        return true;
    }
    if (!CreateAndEnableHook(g_Target, reinterpret_cast<void*>(&hkActivateThrowingAtEmptyWork),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("ThrowingSystem: ActivateThrowingAtEmptyWork hook failed, OnThrow is off");
        g_Target = nullptr;
        return false;
    }
    return true;
}

void Uninstall_ThrowingImpl_ActivateThrowingAtEmptyWork_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig   = nullptr;
}
