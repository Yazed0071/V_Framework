#include <pch.h>
#include "UavLifeControllerImpl_SendStatusMessage.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "CpRadioVariants.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "../fox/gm/GameObjectPosition.h"
#include "log.h"

namespace
{
    using SendStatusMessage_t = void(__fastcall*)(std::uint8_t* lifeController, std::uint32_t uavIndex, std::uint8_t state, std::uint8_t oldState);
    using MemberCp_t          = std::uint8_t(__fastcall*)(std::uint8_t* cpController, std::uint32_t typeSlot, std::uint32_t index);

    constexpr std::size_t   kLifeControllerUav  = 0x60;    // the TppUav component
    constexpr std::size_t   kUavHolder          = 0x88;
    constexpr std::size_t   kHolderCpController = 0x08;
    constexpr std::size_t   kHolderTypeSlot     = 0x30;
    constexpr std::size_t   kMemberCpSlot       = 0x148;   // what the UAV's own problem report asks
    constexpr std::uint8_t  kAlive              = 0;       // then 1 falling ("Dying"), 2/3 gone ("Dead")
    constexpr std::uint8_t  kNoCp               = 0xFF;
    constexpr std::uint32_t kUavType            = 0x3400;
    constexpr std::uint32_t kIndexMask          = 0x1FF;

    SendStatusMessage_t g_OrigSendStatusMessage = nullptr;

    bool CpOf(std::uint8_t* lifeController, std::uint32_t uavIndex, std::uint8_t*& cpController, std::uint8_t& cp)
    {
        __try
        {
            std::uint8_t* uav = *reinterpret_cast<std::uint8_t* const*>(lifeController + kLifeControllerUav);
            std::uint8_t* holder = uav ? *reinterpret_cast<std::uint8_t* const*>(uav + kUavHolder) : nullptr;
            cpController = holder ? *reinterpret_cast<std::uint8_t* const*>(holder + kHolderCpController) : nullptr;
            if (!cpController)
                return false;
            const auto memberCp = reinterpret_cast<MemberCp_t>((*reinterpret_cast<void* const* const*>(cpController))[kMemberCpSlot / sizeof(void*)]);
            cp = memberCp(cpController, *reinterpret_cast<const std::uint32_t*>(holder + kHolderTypeSlot), uavIndex);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void __fastcall hk_SendStatusMessage(std::uint8_t* lifeController, std::uint32_t uavIndex, std::uint8_t state, std::uint8_t oldState)
    {
        if (oldState == kAlive && state != kAlive && !MissionCodeGuard::ShouldBypassHooks())   // once, as it starts to fall or is gone at once
        {
            std::uint8_t* cpController = nullptr;
            std::uint8_t cp = kNoCp;
            float spot[3] = {};
            const std::uint32_t uavId = kUavType | (uavIndex & kIndexMask);
            if (CpOf(lifeController, uavIndex, cpController, cp) && cp != kNoCp && GameObjectPosition::Read(uavId, spot))
                CpRadioVariants_OnUavDown(cp, cpController, spot);
        }
        g_OrigSendStatusMessage(lifeController, uavIndex, state, oldState);
    }
}

bool Install_UavLifeControllerImpl_SendStatusMessage_Hook()
{
    void* target = ResolveGameAddress(gAddr.UavLifeControllerImpl_SendStatusMessage);
    if (!target)
    {
        LogError("CpRadioVariants: UAV LifeControllerImpl::SendStatusMessage address missing on this build, no CPR0510 when a UAV goes down");
        return false;
    }
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_SendStatusMessage), reinterpret_cast<void**>(&g_OrigSendStatusMessage)))
    {
        LogError("CpRadioVariants: UAV LifeControllerImpl::SendStatusMessage hook failed, no CPR0510 when a UAV goes down");
        return false;
    }
    return true;
}

bool Uninstall_UavLifeControllerImpl_SendStatusMessage_Hook()
{
    if (g_OrigSendStatusMessage)
    {
        if (void* target = ResolveGameAddress(gAddr.UavLifeControllerImpl_SendStatusMessage))
            DisableAndRemoveHook(target);
    }
    g_OrigSendStatusMessage = nullptr;
    return true;
}
