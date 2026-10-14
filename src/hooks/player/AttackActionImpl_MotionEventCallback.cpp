#include "pch.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "AttackActionImpl_MotionEventCallback.h"
#include "AttackActionImpl_SetEquipMotion.h"
#include "../fox/anim/EventCallback_ExecCallback.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "RealizedEquipObjectImpl_MotionEventCallbackImpl.h"
#include "log.h"

namespace
{
    using MotionEventCallback_t = void(__fastcall*)(std::uint32_t playerIndex, void* event);
    using RealizedEquipOfGunSlot_t = std::uint8_t*(__fastcall*)(void* realizedEquipTable, int gunSlot);

    constexpr std::uintptr_t kPlayerWorkBytes = 0x610;

    MotionEventCallback_t g_Orig = nullptr;
    void* g_Target = nullptr;

    struct HeldGun
    {
        void* equipSystem;
        std::uint32_t handle;
    };

    bool ReadHeldGun(void* attackAction, std::uint32_t playerIndex, HeldGun& gun)
    {
        if (!attackAction)
            return false;
        __try
        {
            const std::uint8_t* action = static_cast<const std::uint8_t*>(attackAction);
            const int firstPlayer = *reinterpret_cast<const std::int32_t*>(*reinterpret_cast<std::uint8_t* const*>(action + 0x38) + 0x24);
            const std::uint8_t* work = *reinterpret_cast<std::uint8_t* const*>(action + 0x58) + (playerIndex - firstPlayer) * kPlayerWorkBytes;
            gun.equipSystem = *reinterpret_cast<void* const*>(action + 0x60);
            gun.handle = *reinterpret_cast<const std::uint32_t*>(work + 0x240);   // equip handle of the gun in hand
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool FindRealizedGun(void* equipSystem, int gunSlot, void*& realizedEquip, std::uint32_t& slot)
    {
        __try
        {
            void* table = static_cast<std::uint8_t*>(equipSystem) + 0x40;   // the equip system's realized-gun table
            const auto lookup = *reinterpret_cast<RealizedEquipOfGunSlot_t*>(*reinterpret_cast<void**>(table));
            std::uint8_t* gun = lookup(table, gunSlot);
            if (!gun)
                return false;
            realizedEquip = gun - 0x20;   // the table hands back the interface 0x20 into the object
            slot = static_cast<std::uint32_t>(gunSlot - *reinterpret_cast<const std::int32_t*>(gun + 0x34));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void __fastcall hk_MotionEventCallback(std::uint32_t playerIndex, void* event)
    {
        g_Orig(playerIndex, event);
        if (MissionCodeGuard::ShouldBypassHooks())
            return;
        ClipEvent_NoteOwner(static_cast<int>(playerIndex));   // a player's gameObjectId is its index
        if (MotionEventName(event) != kEjectChamberRoundEvent)
            return;

        HeldGun held = {};
        if (!ReadHeldGun(MotionEventOwner(event), playerIndex, held))
            return;
        const int gunSlot = EquipSystem_GunSlotOfHandle(held.equipSystem, held.handle);
        void* realizedEquip = nullptr;
        std::uint32_t slot = 0;
        if (ChamberRound_TakeForGunSlot(gunSlot) && FindRealizedGun(held.equipSystem, gunSlot, realizedEquip, slot))
            RealizedEquip_EjectCasing(realizedEquip, slot);
    }
}

bool Install_AttackActionImpl_MotionEventCallback_Hook()
{
    if (g_Target)
        return true;
    if (!gAddr.AttackActionImpl_MotionEventCallback)
    {
        LogError("ChamberRound: player event address missing for this build, no chambered-round eject from body clips");
        return true;
    }
    void* target = ResolveGameAddress(gAddr.AttackActionImpl_MotionEventCallback);
    if (!target || !CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_MotionEventCallback), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("ChamberRound: player event hook failed, no chambered-round eject from body clips");
        return true;
    }
    g_Target = target;
    return true;
}

bool Uninstall_AttackActionImpl_MotionEventCallback_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig = nullptr;
    return true;
}
