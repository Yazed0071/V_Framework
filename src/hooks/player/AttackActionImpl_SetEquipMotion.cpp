#include "pch.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>

#include "AddressSet.h"
#include "AttackActionImpl_SetEquipMotion.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using SetEquipMotion_t = void(__fastcall*)(void* attackAction, std::uint32_t motionKind, void* desc, std::uint8_t* work);

    constexpr std::uint32_t kReloadMotion = 2;

    SetEquipMotion_t g_Orig = nullptr;
    void* g_Target = nullptr;

    std::atomic<int> g_ChamberedGunSlot{-1};
    std::atomic<void*> g_PlayerAttackAction{nullptr};

    struct LocalPlayerGun
    {
        int playerIndex;
        void* equipSystem;
        std::uint32_t handle;
    };

    bool ReadLocalPlayerGun(void* attackAction, LocalPlayerGun& gun)
    {
        __try
        {
            const std::uint8_t* action = static_cast<const std::uint8_t*>(attackAction);
            gun.playerIndex = *reinterpret_cast<const std::int32_t*>(*reinterpret_cast<std::uint8_t* const*>(action + 0x38) + 0x24);   // index of the player in the first work slot
            const std::uint8_t* work = *reinterpret_cast<std::uint8_t* const*>(action + 0x58);
            gun.equipSystem = *reinterpret_cast<void* const*>(action + 0x60);
            gun.handle = *reinterpret_cast<const std::uint32_t*>(work + 0x240);   // equip handle of the gun in hand
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    struct ReloadingGun
    {
        void* equipSystem;
        std::uint32_t handle;
        bool underBarrel;
        bool roundInChamber;
    };

    bool ReadReloadingGun(void* attackAction, const std::uint8_t* work, ReloadingGun& gun)
    {
        __try
        {
            gun.equipSystem = *reinterpret_cast<void* const*>(static_cast<const std::uint8_t*>(attackAction) + 0x60);
            gun.handle = *reinterpret_cast<const std::uint32_t*>(work + 0x240);                   // equip handle of the gun in hand
            gun.underBarrel = (*reinterpret_cast<const std::uint32_t*>(work + 0x27C) & 0x200) != 0;
            gun.roundInChamber = (*reinterpret_cast<const std::uint32_t*>(work + 0x3C8) & 0x40) != 0;   // the reload sets this when the magazine wasn't empty as it started
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void NoteReloadStart(void* attackAction, const std::uint8_t* work)
    {
        ReloadingGun gun = {};
        int gunSlot = -1;
        if (ReadReloadingGun(attackAction, work, gun) && gun.roundInChamber && !gun.underBarrel)
            gunSlot = EquipSystem_GunSlotOfHandle(gun.equipSystem, gun.handle);
        g_ChamberedGunSlot.store(gunSlot, std::memory_order_release);
    }

    void __fastcall hk_SetEquipMotion(void* attackAction, std::uint32_t motionKind, void* desc, std::uint8_t* work)
    {
        if (!MissionCodeGuard::ShouldBypassHooks())
        {
            g_PlayerAttackAction.store(attackAction, std::memory_order_release);
            if (motionKind == kReloadMotion)
                NoteReloadStart(attackAction, work);
        }
        g_Orig(attackAction, motionKind, desc, work);
    }
}

int EquipSystem_GunSlotOfHandle(void* equipSystem, std::uint32_t equipHandle)
{
    const std::uint32_t kind = equipHandle & 0x1F;   // equip handle: low 5 bits = kind (1-8 are guns), top 16 bits = index
    const std::uint32_t index = equipHandle >> 16;
    if (!equipSystem || kind < 1 || kind > 8 || index == 0xFFFF)
        return -1;

    __try
    {
        const std::uint8_t* handleTable = *reinterpret_cast<std::uint8_t* const*>(static_cast<const std::uint8_t*>(equipSystem) + 0x128);
        const std::int32_t* gunSlots = *reinterpret_cast<std::int32_t* const*>(handleTable + 0x18);
        return gunSlots[index];
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
}

bool ChamberRound_TakeForGunSlot(int gunSlot)
{
    int expected = gunSlot;
    return gunSlot >= 0 && g_ChamberedGunSlot.compare_exchange_strong(expected, -1, std::memory_order_acq_rel);
}

int PlayerIndexHoldingGunSlot(int gunSlot)
{
    void* attackAction = g_PlayerAttackAction.load(std::memory_order_acquire);
    LocalPlayerGun gun = {};
    if (gunSlot < 0 || !attackAction || !ReadLocalPlayerGun(attackAction, gun))
        return -1;
    return EquipSystem_GunSlotOfHandle(gun.equipSystem, gun.handle) == gunSlot ? gun.playerIndex : -1;
}

void PlayerAttackAction_Forget()
{
    g_PlayerAttackAction.store(nullptr, std::memory_order_release);
    g_ChamberedGunSlot.store(-1, std::memory_order_release);
}

bool Install_AttackActionImpl_SetEquipMotion_Hook()
{
    if (g_Target)
        return true;
    if (!gAddr.AttackActionImpl_SetEquipMotion)
    {
        LogError("ChamberRound: SetEquipMotion address missing for this build, no chambered-round eject");
        return true;
    }
    void* target = ResolveGameAddress(gAddr.AttackActionImpl_SetEquipMotion);
    if (!target || !CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_SetEquipMotion), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("ChamberRound: SetEquipMotion hook failed, no chambered-round eject");
        return true;
    }
    g_Target = target;
    return true;
}

bool Uninstall_AttackActionImpl_SetEquipMotion_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig = nullptr;
    g_ChamberedGunSlot.store(-1);
    return true;
}
