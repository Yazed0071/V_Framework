#include "pch.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "AttackActionImpl_SetEquipMotion.h"
#include "../fox/anim/EventCallback_ExecCallback.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "RealizedEquipObjectImpl_MotionEventCallbackImpl.h"
#include "log.h"

namespace
{
    using MotionEventCallbackImpl_t = void(__fastcall*)(std::uint32_t slot, void* event);
    using EjectCasing_t = void(__fastcall*)(void* realizedEquip, std::uint32_t slot, bool underBarrel);
    using EventName_t = std::uint32_t(__fastcall*)(void* event);
    using EventOwner_t = void*(__fastcall*)(void* event);

    MotionEventCallbackImpl_t g_Orig = nullptr;
    EjectCasing_t g_EjectCasing = nullptr;
    void* g_Target = nullptr;

    int GunSlotOf(void* realizedEquip, std::uint32_t slot)
    {
        __try
        {
            return *reinterpret_cast<const std::int32_t*>(static_cast<const std::uint8_t*>(realizedEquip) + 0x54) + static_cast<int>(slot);   // first gun slot this object owns
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
    }

    void __fastcall hk_MotionEventCallbackImpl(std::uint32_t slot, void* event)
    {
        g_Orig(slot, event);
        if (MissionCodeGuard::ShouldBypassHooks())
            return;
        const bool addedEvent = ClipEvent_IsFiring();
        const bool ejectEvent = MotionEventName(event) == kEjectChamberRoundEvent;
        if (!addedEvent && !ejectEvent)
            return;

        void* realizedEquip = MotionEventOwner(event);
        if (!realizedEquip)
            return;
        const int gunSlot = GunSlotOf(realizedEquip, slot);
        if (addedEvent)
            ClipEvent_NoteOwner(PlayerIndexHoldingGunSlot(gunSlot));
        if (ejectEvent && ChamberRound_TakeForGunSlot(gunSlot))
            RealizedEquip_EjectCasing(realizedEquip, slot);
    }
}

std::uint32_t MotionEventName(void* event)
{
    if (!event)
        return 0;
    __try
    {
        void** vtable = *static_cast<void***>(event);
        return reinterpret_cast<EventName_t>(vtable[0])(event);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
}

void* MotionEventOwner(void* event)
{
    if (!event)
        return nullptr;
    __try
    {
        void** vtable = *static_cast<void***>(event);
        return reinterpret_cast<EventOwner_t>(vtable[0x90 / 8])(event);   // the object whose clip sent the event
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
}

void RealizedEquip_EjectCasing(void* realizedEquip, std::uint32_t slot)
{
    if (g_EjectCasing && realizedEquip)
        g_EjectCasing(realizedEquip, slot, false);
}

bool Install_RealizedEquipObjectImpl_MotionEventCallbackImpl_Hook()
{
    if (g_Target)
        return true;
    if (!gAddr.RealizedEquipObjectImpl_MotionEventCallbackImpl || !gAddr.RealizedEquipObjectImpl_EjectCasing)
    {
        LogError("ChamberRound: gun event / EjectCasing address missing for this build, no chambered-round eject");
        return true;
    }
    g_EjectCasing = reinterpret_cast<EjectCasing_t>(ResolveGameAddress(gAddr.RealizedEquipObjectImpl_EjectCasing));
    void* target = ResolveGameAddress(gAddr.RealizedEquipObjectImpl_MotionEventCallbackImpl);
    if (!g_EjectCasing || !target || !CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_MotionEventCallbackImpl), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("ChamberRound: gun event hook failed, no chambered-round eject from gun clips");
        return true;
    }
    g_Target = target;
    return true;
}

bool Uninstall_RealizedEquipObjectImpl_MotionEventCallbackImpl_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig = nullptr;
    return true;
}
