#include "pch.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "RadioControllerImpl_StepRadioWaitResponse.h"
#include "SoldierFaction.h"

namespace
{
    using SendStatusMessage_t = void(__fastcall*)(void* lifeController, std::uint32_t soldierIndex, void* life,
                                                  std::uint32_t oldStatus);

    constexpr std::uint32_t kStatusMask     = 0xFFu;
    constexpr std::size_t   kLifeStatusByte = 0x3E;
    constexpr std::uint8_t  kAlive          = 0u;
    constexpr std::uint8_t  kUnreadable     = 0xFFu;

    SendStatusMessage_t g_OrigSendStatusMessage = nullptr;

    std::uint8_t ReadStatus(const void* life)
    {
        __try
        {
            return static_cast<const std::uint8_t*>(life)[kLifeStatusByte];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return kUnreadable;
        }
    }

    void __fastcall hk_SendStatusMessage(void* lifeController, std::uint32_t soldierIndex, void* life,
                                         std::uint32_t oldStatus)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigSendStatusMessage, lifeController, soldierIndex, life, oldStatus);

        SoldierFaction_OnLifeStatusChanged(lifeController, soldierIndex, life, oldStatus & kStatusMask);
        const std::uint8_t status = life ? ReadStatus(life) : kUnreadable;
        if ((oldStatus & kStatusMask) == kAlive && status != kAlive && status != kUnreadable)
            CpRadioResponse::OnSoldierDown(static_cast<std::uint16_t>(soldierIndex));
        g_OrigSendStatusMessage(lifeController, soldierIndex, life, oldStatus);
    }
}

bool Install_LifeControllerImpl_SendStatusMessage_Hook()
{
    void* target = ResolveGameAddress(gAddr.LifeControllerImpl_SendStatusMessage);
    if (!target)
        return false;
    return CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_SendStatusMessage),
                               reinterpret_cast<void**>(&g_OrigSendStatusMessage));
}

bool Uninstall_LifeControllerImpl_SendStatusMessage_Hook()
{
    void* target = ResolveGameAddress(gAddr.LifeControllerImpl_SendStatusMessage);
    if (target)
        DisableAndRemoveHook(target);
    g_OrigSendStatusMessage = nullptr;
    return true;
}
