#include <pch.h>
#include "SoldierAttackActionImpl_Execute.h"

#include <Windows.h>
#include <intrin.h>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "AddressSet.h"
#include "EquipAliasLease.h"
#include "EquipIdCompression.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"
#include "RideHeliActionImpl_State_GetInAction.h"
#include "RideHeliActionImpl_State_IdleAction.h"

namespace
{
    using Execute_t            = void(__fastcall*)(void* attackAction, std::int32_t phase, std::uint32_t realizedSlot);
    using ShootOneBullet_t     = void(__fastcall*)(void* impl, std::uint32_t realizedSlot, const float* muzzle, const float* target, std::uint8_t mock, std::uint8_t missScale, void* equipParams);
    using MuzzleMatrix_t       = void(__fastcall*)(void* equipController, std::uint32_t realizedSlot, float* matrix);
    using ShotTuning_t         = std::uint8_t*(__fastcall*)(void* owner);
    using GetQuarkSystemTable_t = std::uint8_t*(__fastcall*)();
    using GunInfo_t            = std::uint8_t*(__fastcall*)(void* equipSystem, std::uint32_t equipId);
    using SetCommand_t         = void(__fastcall*)(void* actionController, std::uint32_t soldierIndex, std::uint8_t* command);

    constexpr std::int32_t  kPhaseUpdate     = 1;
    constexpr std::size_t   kImplFromAction  = 0x20;    // Execute gets the action plugin interface, ShootOneBullet the impl before it
    constexpr std::size_t   kWork            = 0x1A0;   // on the interface: 0x2F0 per realized soldier
    constexpr std::size_t   kWorkBase        = 0x1BC;   // realized slot of entry 0
    constexpr std::size_t   kWorkSize        = 0x2F0;
    constexpr std::size_t   kWorkTask        = 0x290;   // his task, set by phase 0 every frame
    constexpr std::size_t   kWorkSoldier     = 0x2A0;
    constexpr std::size_t   kWorkBurstSerial = 0x2D8;   // ProcessFireControl sets a burst up when the task serial differs from this (0x14139076A)
    constexpr std::size_t   kWorkFireState   = 0x1E4;   // 0 idle, 1 and 2 pausing, 3 firing
    constexpr std::size_t   kTaskSerial      = 0xBA;
    constexpr std::size_t   kTaskFlags       = 0xC2;    // u16: 4 done, 0x800 hold fire
    constexpr std::size_t   kTaskType        = 0xC6;
    constexpr std::size_t   kTaskSubAction   = 0xC7;    // 1 = the attack action aims and fires alongside
    constexpr std::size_t   kTaskAimFire     = 0x5E;    // u16 in his aim at +0x40, 0x0103 = vanilla's burst on his target
    constexpr std::uint16_t kTaskDone        = 4;
    constexpr std::uint16_t kHoldFire        = 0x800;
    constexpr std::uint16_t kAimFireBytes    = 0x0103;
    constexpr std::uint8_t  kReloadAction    = 0x18;
    constexpr std::size_t   kImplSoldiers    = 0x78;    // the Soldier2 part
    constexpr std::size_t   kPartLife        = 0x38;    // +8 one 0x40 entry per soldier
    constexpr std::size_t   kLifeEntrySize   = 0x40;
    constexpr std::size_t   kLifeStatus      = 0x3E;
    constexpr std::size_t   kPartController  = 0x50;    // ActionController
    constexpr std::size_t   kPartEquip       = 0xA0;    // EquipController: +8 eight bytes per soldier (loaded u16, magazine u16, shots u16, slot u8), +0x10 three equip ids per soldier
    constexpr std::size_t   kPartReloadGrade = 0x198;   // one qword per soldier, the reload order carries the top of bits 17-19
    constexpr std::size_t   kPartEquipParams = 0x188;   // what the fire control hands ShootOneBullet
    constexpr std::size_t   kPartTuning      = 0xD0;
    constexpr std::size_t   kTuningGet       = 0x268;
    constexpr std::size_t   kForcedHitChance = 0x2EC;   // >= 0 replaces the hit chance roll, 1 = never misses
    constexpr float         kSureHit         = 1.0f;
    constexpr std::uint16_t kNoEquip         = 0x1BB;
    constexpr std::size_t   kQuarkEquip      = 0x98;    // QuarkSystemTable -> +0x1E8 the equip system
    constexpr std::size_t   kEquipSystem     = 0x1E8;
    constexpr std::size_t   kVtbl_GunInfo    = 0x158;   // EquipSystemImpl::GetGunInfoById(equipId)
    constexpr std::size_t   kGunRate         = 0x68;    // u16; ProcessFireControl's interval = 1 / (rate * 0.1 * 1/60) s (0x14139080A)
    constexpr std::size_t   kCommandSize     = 0xC0;
    constexpr std::uint32_t kReloadFlags     = 0x100012;   // what vanilla's AI reload order puts at +0xB0, its grade in bits 16-17
    constexpr std::uint64_t kReloadDelayMs   = 300;        // his own AI gets the first go at it
    constexpr std::uint64_t kReloadStaleMs   = 5000;       // a waiting order older than this was never collected

    Execute_t             g_OrigExecute  = nullptr;
    ShootOneBullet_t      g_Shoot        = nullptr;
    MuzzleMatrix_t        g_MuzzleMatrix = nullptr;
    GetQuarkSystemTable_t g_QuarkTable   = nullptr;
    SetCommand_t          g_SetCommand   = nullptr;
    void*                 g_GunInfo      = nullptr;
    void*                 g_Target       = nullptr;
    bool                  g_Installed    = false;
    std::atomic_flag      g_TuningLock   = ATOMIC_FLAG_INIT;   // the hit chance lives in tuning every soldier shares
    std::uint64_t         g_LastShotMs[0x200];                 // the soldier's own job, one writer per soldier
    std::atomic<std::uint64_t> g_ReloadDueMs[0x200];          // 0 = no reload waiting
    std::atomic<int>           g_ReloadsDue{ 0 };
    std::atomic<std::uint64_t> g_ReloadPurgeMs{ 0 };

    struct Gun
    {
        std::uint16_t  equipId   = 0;
        std::uint16_t* rounds    = nullptr;   // loaded, magazine size, shots fired
        bool           reloading = false;
    };

    std::uint8_t* WorkOf(std::uint8_t* attackAction, std::uint32_t realizedSlot)
    {
        const std::uint32_t entry = realizedSlot - static_cast<std::uint32_t>(*reinterpret_cast<std::int32_t*>(attackAction + kWorkBase));
        return *reinterpret_cast<std::uint8_t**>(attackAction + kWork) + static_cast<std::size_t>(entry) * kWorkSize;
    }

    std::uint32_t SoldierOfSeh(std::uint8_t* attackAction, std::uint32_t realizedSlot)
    {
        __try
        {
            return *reinterpret_cast<std::uint32_t*>(WorkOf(attackAction, realizedSlot) + kWorkSoldier);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0xFFFFFFFFu;
        }
    }

    // vanilla's fire control: armed once, it runs bursts and pauses by itself; it only stops on a new order or when told to
    void SteerBurstSeh(std::uint8_t* attackAction, std::uint32_t realizedSlot, bool fire)
    {
        __try
        {
            auto* work = WorkOf(attackAction, realizedSlot);
            auto* state = work + kWorkFireState;
            if (!fire)
            {
                if (*state != 0)
                    *state = 0;
                return;
            }
            auto* task = *reinterpret_cast<std::uint8_t**>(work + kWorkTask);
            if (*state != 0 || !task || task[kTaskSubAction] != 1 || (*reinterpret_cast<std::uint16_t*>(task + kTaskFlags) & kHoldFire)
                || *reinterpret_cast<std::uint16_t*>(task + kTaskAimFire) != kAimFireBytes)
                return;
            *reinterpret_cast<std::uint16_t*>(work + kWorkBurstSerial) = static_cast<std::uint16_t>(*reinterpret_cast<std::uint16_t*>(task + kTaskSerial) + 1);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    // GZ fires only with an active weapon; he must be conscious and hold one
    bool ReadGunSeh(std::uint8_t* attackAction, std::uint32_t realizedSlot, std::uint32_t index, Gun& gun)
    {
        __try
        {
            auto* part = *reinterpret_cast<std::uint8_t**>(attackAction - kImplFromAction + kImplSoldiers);
            auto* life = *reinterpret_cast<std::uint8_t**>(*reinterpret_cast<std::uint8_t**>(part + kPartLife) + 8) + static_cast<std::size_t>(index) * kLifeEntrySize;
            if (life[kLifeStatus] != 0)
                return false;
            auto* equip = *reinterpret_cast<std::uint8_t**>(part + kPartEquip);
            auto* slot = *reinterpret_cast<std::uint8_t**>(equip + 8) + static_cast<std::size_t>(index) * 8;
            gun.equipId = *reinterpret_cast<std::uint16_t*>(*reinterpret_cast<std::uint8_t**>(equip + 0x10) + (static_cast<std::size_t>(index) * 3 + slot[6]) * 2);
            gun.rounds = reinterpret_cast<std::uint16_t*>(slot);
            auto* task = *reinterpret_cast<std::uint8_t**>(WorkOf(attackAction, realizedSlot) + kWorkTask);
            gun.reloading = task && task[kTaskType] == kReloadAction && !(*reinterpret_cast<std::uint16_t*>(task + kTaskFlags) & kTaskDone);
            return gun.equipId != 0 && gun.equipId != kNoEquip;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool NeedsReload(const Gun& gun)   // vanilla's combat AI: under a quarter of the magazine left
    {
        return gun.rounds[1] != 0 && static_cast<float>(gun.rounds[0]) < static_cast<float>(gun.rounds[1]) * 0.25f;
    }

    float GunIntervalSeh(std::uint16_t equipId)   // seconds between his weapon's shots, 0 = unknown
    {
        __try
        {
            auto* equipSystem = *reinterpret_cast<std::uint8_t**>(*reinterpret_cast<std::uint8_t**>(g_QuarkTable() + kQuarkEquip) + kEquipSystem);
            void** vtbl = *reinterpret_cast<void***>(equipSystem);
            if (vtbl[kVtbl_GunInfo / sizeof(void*)] != g_GunInfo)
                return 0.0f;
            std::uint32_t engineId = equipId;
            if (EquipIdCompression::IsWideEquipId(equipId))   // the gun infos are keyed by the alias
            {
                engineId = static_cast<std::uint32_t>(equip::CurrentAliasOf(equipId));
                if (engineId == 0)
                    return 0.0f;
            }
            const std::uint8_t* info = reinterpret_cast<GunInfo_t>(g_GunInfo)(equipSystem, engineId);
            const std::uint16_t rate = info ? *reinterpret_cast<const std::uint16_t*>(info + kGunRate) : 0;
            return rate ? 1.0f / (static_cast<float>(rate) * 0.1f * 0.016667f) : 0.0f;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0.0f;
        }
    }

    // ProcessFireControl's shot (0x141391802), held at a sure hit so it lands on the point; it uses a round as every shot does
    bool FireSeh(std::uint8_t* attackAction, std::uint32_t realizedSlot, const float* point)
    {
        __try
        {
            auto* impl = attackAction - kImplFromAction;
            auto* part = *reinterpret_cast<std::uint8_t**>(impl + kImplSoldiers);
            alignas(16) float muzzleMatrix[16];
            g_MuzzleMatrix(*reinterpret_cast<void**>(part + kPartEquip), realizedSlot, muzzleMatrix);
            alignas(16) const float muzzle[4] = { muzzleMatrix[12], muzzleMatrix[13], muzzleMatrix[14], muzzleMatrix[15] };
            alignas(16) const float target[4] = { point[0], point[1], point[2], point[3] };
            void* owner = *reinterpret_cast<void**>(*reinterpret_cast<std::uint8_t**>(part + kPartTuning) + 8);
            auto* tuning = reinterpret_cast<ShotTuning_t>((*reinterpret_cast<void***>(owner))[kTuningGet / sizeof(void*)])(owner);
            auto* hitChance = reinterpret_cast<float*>(tuning + kForcedHitChance);
            void* equipParams = *reinterpret_cast<void**>(part + kPartEquipParams);
            while (g_TuningLock.test_and_set(std::memory_order_acquire))
                _mm_pause();
            volatile float before = 0.0f;
            volatile bool  forced = false;
            __try
            {
                before = *hitChance;
                *hitChance = kSureHit;
                forced = true;
                g_Shoot(impl, realizedSlot, muzzle, target, 0, 0, equipParams);
            }
            __finally
            {
                if (forced)
                    *hitChance = before;
                g_TuningLock.clear(std::memory_order_release);
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // as vanilla's AI reload order (0x141405C10), silent: the crouched reload, no callout
    bool SendReloadSeh(std::uint8_t* attackAction, std::uint32_t index)
    {
        __try
        {
            auto* part = *reinterpret_cast<std::uint8_t**>(attackAction - kImplFromAction + kImplSoldiers);
            const std::uint64_t grade = (*reinterpret_cast<std::uint64_t**>(part + kPartReloadGrade))[index] >> 16;
            const std::uint32_t level = (grade & 8) ? 3 : (grade & 4) ? 2 : (grade & 2) ? 1 : 0;
            alignas(16) std::uint8_t command[kCommandSize] = {};
            command[0] = 1;
            *reinterpret_cast<std::uint32_t*>(command + 0xB0) = kReloadFlags | level << 16;
            command[0xB4] = kReloadAction;
            *reinterpret_cast<std::uint16_t*>(command + 0xB6) = 0xFFFF;
            g_SetCommand(*reinterpret_cast<void**>(part + kPartController), index, command);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void QueueReload(std::uint32_t index, std::uint64_t now)
    {
        std::uint64_t none = 0;
        if (g_ReloadDueMs[index].compare_exchange_strong(none, now + kReloadDelayMs, std::memory_order_acq_rel))
            g_ReloadsDue.fetch_add(1, std::memory_order_relaxed);
    }

    // a soldier on the ground whose scripted shots emptied his magazine; a rider reloads on his seat instead
    void TendReload(std::uint8_t* attackAction, std::uint32_t realizedSlot, std::uint32_t index, std::uint64_t now)
    {
        std::uint64_t due = g_ReloadDueMs[index].load(std::memory_order_acquire);
        if (due == 0 || now < due || !g_ReloadDueMs[index].compare_exchange_strong(due, 0, std::memory_order_acq_rel))
            return;
        g_ReloadsDue.fetch_sub(1, std::memory_order_relaxed);
        Gun gun;
        if (SoldierRideHeli_IsDoorRider(index) || !ReadGunSeh(attackAction, realizedSlot, index, gun) || gun.reloading || !NeedsReload(gun))
            return;
        SendReloadSeh(attackAction, index);
    }

    void __fastcall hkExecute(void* attackAction, std::int32_t phase, std::uint32_t realizedSlot)
    {
        auto* action = static_cast<std::uint8_t*>(attackAction);
        const bool update = phase == kPhaseUpdate;
        const bool riders = update && SoldierRideHeli_AnyRider();
        const bool shots = update && SoldierRideHeli_ShotsQueued();
        const bool reloads = update && g_ReloadsDue.load(std::memory_order_relaxed) > 0;
        const bool live = (riders || shots || reloads) && !MissionCodeGuard::ShouldBypassHooks();
        const std::uint32_t index = live ? SoldierOfSeh(action, realizedSlot) : 0xFFFFFFFFu;
        if (riders && index < 0x200 && SoldierRideHeli_IsDoorRider(index))
            SteerBurstSeh(action, realizedSlot, RideHeliDoorSeat_FireWanted(index));
        g_OrigExecute(attackAction, phase, realizedSlot);
        if (index >= 0x200)
            return;
        const std::uint64_t now = GetTickCount64();
        if (reloads)
        {
            if (now - g_ReloadPurgeMs.load(std::memory_order_relaxed) > kReloadStaleMs)   // orders left over from a soldier who is gone (mission ended, dead)
            {
                g_ReloadPurgeMs.store(now, std::memory_order_relaxed);
                for (auto& due : g_ReloadDueMs)
                {
                    std::uint64_t pending = due.load(std::memory_order_relaxed);
                    if (pending != 0 && now > pending + kReloadStaleMs && due.compare_exchange_strong(pending, 0, std::memory_order_acq_rel))
                        g_ReloadsDue.fetch_sub(1, std::memory_order_relaxed);
                }
            }
            TendReload(action, realizedSlot, index, now);
        }
        if (!shots || !SoldierRideHeli_HasShot(index))
            return;
        Gun gun;
        const bool onSeat = SoldierRideHeli_OnSeat(index);
        const bool armed = ReadGunSeh(action, realizedSlot, index, gun);
        const bool loaded = armed && gun.rounds[0] != 0 && !gun.reloading;
        const bool canFire = loaded && (!onSeat || RideHeliDoorSeat_Up(index));   // a rider lying at his door or reloading can't
        if (armed && !loaded && !onSeat && !gun.reloading)
            QueueReload(index, now);
        alignas(16) float point[4];
        if (!SoldierRideHeli_TakeShot(index, canFire, point))
            return;
        const float interval = GunIntervalSeh(gun.equipId);
        if (interval > 0.0f && static_cast<float>(now - g_LastShotMs[index]) < interval * 1000.0f)
            return;   // GZ drops an order that comes faster than his weapon fires
        g_LastShotMs[index] = now;
        if (!FireSeh(action, realizedSlot, point))
        {
            LogWarn("RequestToFireOnHelicopter: soldier %u's shot failed", index);
            return;
        }
        if (!onSeat && NeedsReload(gun))
            QueueReload(index, now);
    }
}

bool Install_SoldierAttackActionImpl_Execute_Hook()
{
    if (g_Installed)
        return true;

    g_Shoot        = gAddr.SoldierAttackActionImpl_ShootOneBullet ? reinterpret_cast<ShootOneBullet_t>(ResolveGameAddress(gAddr.SoldierAttackActionImpl_ShootOneBullet)) : nullptr;
    g_MuzzleMatrix = gAddr.SoldierEquipController_GetMuzzleWorldMatrix ? reinterpret_cast<MuzzleMatrix_t>(ResolveGameAddress(gAddr.SoldierEquipController_GetMuzzleWorldMatrix)) : nullptr;
    g_QuarkTable   = gAddr.GetQuarkSystemTable ? reinterpret_cast<GetQuarkSystemTable_t>(ResolveGameAddress(gAddr.GetQuarkSystemTable)) : nullptr;
    g_SetCommand   = gAddr.ActionController_SetCommand ? reinterpret_cast<SetCommand_t>(ResolveGameAddress(gAddr.ActionController_SetCommand)) : nullptr;
    g_GunInfo      = gAddr.EquipSystemImpl_GetGunInfoById ? ResolveGameAddress(gAddr.EquipSystemImpl_GetGunInfoById) : nullptr;
    g_Target       = gAddr.SoldierAttackActionImpl_Execute ? ResolveGameAddress(gAddr.SoldierAttackActionImpl_Execute) : nullptr;
    if (!g_Shoot || !g_MuzzleMatrix || !g_QuarkTable || !g_SetCommand || !g_GunInfo || !g_Target)
    {
        LogError("SoldierAttackActionImpl_Execute: address missing for this build, RequestToFireOnHelicopter and heli riders' bursts off");
        g_Target = nullptr;
        return false;
    }
    for (auto& due : g_ReloadDueMs)
        due.store(0, std::memory_order_relaxed);
    g_ReloadsDue.store(0, std::memory_order_relaxed);
    if (!CreateAndEnableHook(g_Target, reinterpret_cast<void*>(&hkExecute), reinterpret_cast<void**>(&g_OrigExecute)))
    {
        LogError("SoldierAttackActionImpl_Execute: hook failed, RequestToFireOnHelicopter and heli riders' bursts off");
        g_Target = nullptr;
        return false;
    }
    g_Installed = true;
    return true;
}

bool Uninstall_SoldierAttackActionImpl_Execute_Hook()
{
    if (!g_Installed)
        return true;
    DisableAndRemoveHook(g_Target);
    g_Target      = nullptr;
    g_OrigExecute = nullptr;
    g_Installed   = false;
    return true;
}
