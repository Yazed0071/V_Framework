#include <pch.h>
#include "RideHeliActionImpl_State_GetInAction.h"

#include <Windows.h>
#include <intrin.h>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"
#include "../fox/gm/GameObjectInstance.h"
#include "../fox/gm/GameObjectPosition.h"
#include "CcRootAdjustAction_Exec.h"
#include "RideHeliActionImpl_State_IdleAction.h"

namespace
{
    using GetInAction_t       = void(__fastcall*)(void* rideHeli, std::uint32_t realizedSlot, int proc, void* motionEvent);
    using SetCommand_t        = void(__fastcall*)(void* actionController, std::uint32_t soldierIndex, std::uint8_t* command);
    using OverrideCommand_t   = std::int32_t(__fastcall*)(void* actionController, std::uint32_t soldierIndex, std::uint8_t* command);
    using CancelOverride_t    = void(__fastcall*)(void* actionController, std::uint32_t soldierIndex, std::int32_t token);
    using LatestOwner_t       = std::uint16_t(__fastcall*)(void* soldiers, std::uint32_t soldierIndex);
    using RequestDamage_t     = void(__fastcall*)(void* soldiers, std::uint32_t soldierIndex, std::uint32_t attackId, std::uint32_t owner);

    constexpr std::uint16_t kSoldierType = 2;
    constexpr std::uint32_t kMaxSoldiers = 0x200;
    constexpr std::int32_t  kNoToken     = -1;
    constexpr int           kProcEnter   = 1;


    constexpr std::size_t kSoldierCount            = 0x28;   // Soldier2Impl view, its Soldier2 part starts at +0x20
    constexpr std::size_t kSoldierActionController = 0x70;
    constexpr std::size_t kVtbl_OverrideCommand    = 0x00;   // the soldier pointer holds the ActionController interface (impl+0x20), not the impl;
    constexpr std::size_t kVtbl_CancelOverride     = 0x08;   // its slot +0x30 is the impl's deleting destructor
    constexpr std::size_t kControllerSlots         = 0x10;   // 0x1E0 per soldier
    constexpr std::size_t kSlotSize                = 0x1E0;
    constexpr std::size_t kSlotFlags               = 0x19E;   // bit 0x100 while a command is overridden
    constexpr std::size_t kSlotOverrideToken       = 0x1A4;
    constexpr std::uint16_t kSlotOverridden        = 0x100;

    constexpr std::size_t  kCommandSize    = 0xC0;
    constexpr std::uint8_t kRideHeliAction = 0x1D;
    constexpr std::uint8_t kRideGetIn      = 0;
    constexpr std::uint8_t kRideGetOut     = 2;
    constexpr std::uint32_t kCommandFlags  = 0x110002;
    constexpr std::uint8_t kLeftDoorBit    = 0x40;

    constexpr std::size_t kSoldier2Part    = 0x20;    // what the action plugins hold
    constexpr std::size_t kPartFlagInfo    = 0x110;   // +8 one 0x28 record per soldier, +0x10 count
    constexpr std::size_t kPartStatusBytes = 0x220;   // 9 = riding, unrealized
    constexpr std::size_t kPartSignalMasks = 0x230;
    constexpr std::size_t kFlagRecordSize  = 0x28;
    constexpr std::size_t kControllerTasks = 0x08;    // 0xD0 per soldier
    constexpr std::size_t kTaskSize        = 0xD0;

    constexpr std::uint8_t kDamageAction  = 3;      // every hit, knock-out and death reaction comes as this command
    constexpr std::size_t  kPartLife      = 0x38;   // +8 one 0x40 entry per soldier
    constexpr std::size_t  kLifeEntrySize = 0x40;
    constexpr std::size_t  kLifeStatus    = 0x3E;   // 1 unconscious, 2 dead
    constexpr std::size_t  kLifeDownKind  = 0x12;   // low 2 bits: 1 sleep, 2 faint, 3 dying
    constexpr std::uint8_t kLifeDown      = 1;
    constexpr std::uint8_t kDownFaint     = 2;
    constexpr std::uint8_t kDownDying     = 3;
    constexpr std::uint8_t kDropsGun      = 0x10;   // in the life entry's +0x14 byte

    constexpr std::uint8_t kLifeDead      = 2;

    constexpr std::size_t   kMaxDeadHelis = 8;
    constexpr std::uint32_t kNoHeli       = 0;      // heli ids always carry their type
    constexpr std::uint64_t kShotWaitMs   = 1000;   // GZ fires on his next weapon update; not able to by then, the shot is dropped

    constexpr std::size_t   kAimFire          = 0x1E;     // u16 in the aim block, 0x0103 when the AI wants a shot
    constexpr std::uint16_t kAimFireBytes     = 0x0103;
    constexpr std::size_t   kDamageActionType = 0x20;     // u32 in a type-3 command
    constexpr std::uint32_t kFallPush         = 0x27;     // vanilla's fall off a ledge, DamageAiImpl::CheckFallDead
    constexpr std::uint32_t kBlastThrowFirst  = 0x2A;     // 0x2A-0x2D: an explosion blows him off his feet (front, back, right, left)
    constexpr std::uint32_t kBlastThrowCount  = 4;
    constexpr std::uint32_t kDamageFlags      = 0x112012; // what DamageAiImpl::SetCommand puts at +0xB0
    constexpr std::uint64_t kFallWaitMs       = 10000;    // no landing seen by then, he is down somewhere
    constexpr float         kFallKillDropM    = 8.0f;     // GZ's fall damage starts at 8 m
    constexpr float         kUnknownStartY    = 1.0e9f;   // he left a flying heli
    constexpr std::uint32_t kAttackKill       = 0x4F;     // Lua ChangeLifeState DEAD's attack, damage = his whole life
    constexpr std::uint16_t kNoOwner          = 0xFFFF;
    constexpr std::size_t   kVtbl_LatestOwner   = 0x178;  // Soldier2Impl::GetLatestDamageOffenseOwnerId(idx)
    constexpr std::size_t   kVtbl_RequestDamage = 0x200;  // Soldier2Impl::RequestDamage(idx, attackId, owner)
    constexpr std::size_t   kPartPositions      = 0x18;   // +0x20: one float4 per soldier

    constexpr std::size_t   kCommandSubAction = 0xB5;   // 1 = aim and fire alongside the main action
    constexpr std::size_t   kCommandAimBlock  = 0x40;
    constexpr std::size_t   kAimBlockSize     = 0x28;
    constexpr std::uint64_t kAiAimFreshMs     = 3000;   // the AI re-sends its aim order about every 1.5 s while it engages

    constexpr std::uint64_t kLeavingMs   = 20000;
    constexpr std::uint64_t kPumpEveryMs = 100;

    enum class Rider : std::uint8_t
    {
        None,
        Seated,
        Leaving,
    };

    GetInAction_t g_OrigGetInAction = nullptr;
    bool          g_Installed       = false;

    std::atomic<std::uint8_t>  g_Side[kMaxSoldiers];
    std::atomic<std::int32_t>  g_Token[kMaxSoldiers];
    std::atomic<std::uint16_t> g_Heli[kMaxSoldiers];
    std::atomic<std::uint8_t>  g_Rider[kMaxSoldiers];
    std::atomic<std::uint64_t> g_LeaveBy[kMaxSoldiers];
    std::atomic<std::uint8_t>  g_RideStep[kMaxSoldiers];
    std::atomic<int>           g_RiderCount{ 0 };
    std::uint64_t              g_LastPumpMs = 0;

    std::atomic<std::uint8_t*>  g_Soldiers{ nullptr };   // Soldier2Impl, the same object for every soldier of the mission
    alignas(16) std::uint8_t    g_Hit[kMaxSoldiers][kCommandSize];   // his last hit reaction, played if he dies or leaves asleep
    std::atomic<std::uint32_t>  g_HitSeq[kMaxSoldiers];
    std::atomic<std::uint8_t>   g_HitKept[kMaxSoldiers];
    std::uint8_t                g_AiAim[kMaxSoldiers][kAimBlockSize];
    std::atomic<std::uint32_t>  g_AiAimSeq[kMaxSoldiers];
    std::atomic<std::uint64_t>  g_AiAimMs[kMaxSoldiers];
    std::atomic<std::uint8_t*>  g_GunFlags[kMaxSoldiers];
    std::atomic<std::uint8_t>   g_GunDropped[kMaxSoldiers];   // what the drop-gun bit was before the ride, 0xFF untouched
    std::atomic<std::uint8_t>   g_NoFlinch[kMaxSoldiers];     // SetFlagDisableBulletDamageReactionInHelicopter, for the mission
    std::atomic<const char*>    g_FallWhy[kMaxSoldiers];      // his seat asks the pump to let him go
    std::atomic<std::uint8_t>   g_HeliDead[kMaxSoldiers];
    std::atomic<std::uint32_t>  g_DeadHelis[kMaxDeadHelis];
    alignas(16) float           g_ShotPoint[kMaxSoldiers][4];
    std::atomic<std::uint32_t>  g_ShotSeq[kMaxSoldiers];
    std::atomic<std::uint64_t>  g_ShotAtMs[kMaxSoldiers];     // 0 = no shot waiting
    std::atomic<int>            g_ShotsQueued{ 0 };
    std::atomic<std::uint16_t>  g_AimTarget[kMaxSoldiers];
    std::atomic<std::uint32_t>  g_AiShots[kMaxSoldiers];
    std::atomic<float>          g_DoorOutX[kMaxSoldiers];
    std::atomic<float>          g_DoorOutZ[kMaxSoldiers];
    std::atomic<std::uint64_t>  g_FallSinceMs[kMaxSoldiers];   // 0 = not falling out of a heli
    std::atomic<float>          g_FallStartY[kMaxSoldiers];
    std::atomic<std::uint8_t>   g_FallLanded[kMaxSoldiers];
    std::atomic<int>            g_FallingCount{ 0 };
    alignas(16) std::uint8_t    g_FallHit[kMaxSoldiers][kCommandSize];   // a hit taken in mid-air, its reaction waits until he is down
    std::atomic<std::uint32_t>  g_FallHitSeq[kMaxSoldiers];
    std::atomic<std::uint8_t>   g_FallHitKept[kMaxSoldiers];
    std::recursive_mutex        g_DoorMutex;                  // GetOn and the get-in's nearer-door pick see each other's claims

    // one writer per soldier, readers copy and retry on a change
    void WriteGuarded(std::atomic<std::uint32_t>& seq, void* to, const void* from, std::size_t size)
    {
        seq.fetch_add(1, std::memory_order_acq_rel);
        std::atomic_thread_fence(std::memory_order_release);
        std::memcpy(to, from, size);
        std::atomic_thread_fence(std::memory_order_release);
        seq.fetch_add(1, std::memory_order_release);
    }

    bool ReadGuarded(const std::atomic<std::uint32_t>& seq, void* to, const void* from, std::size_t size)
    {
        const std::uint32_t before = seq.load(std::memory_order_acquire);
        if (before & 1)
            return false;
        std::memcpy(to, from, size);
        std::atomic_thread_fence(std::memory_order_acquire);
        return seq.load(std::memory_order_relaxed) == before;
    }

    void RestoreGunDropSeh(std::uint8_t* soldiers, std::uint32_t index)
    {
        __try
        {
            auto* life = *reinterpret_cast<std::uint8_t**>(soldiers + kSoldier2Part + kPartLife);
            auto* flags = *reinterpret_cast<std::uint8_t**>(life + 8) + static_cast<std::size_t>(index) * kLifeEntrySize + 0x14;
            _InterlockedOr8(reinterpret_cast<char*>(flags), static_cast<char>(kDropsGun));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    // soldiers must come from a fresh lookup: after a mission ends the old life entry is gone
    void RestoreGun(std::uint32_t index, std::uint8_t* soldiers)
    {
        const std::uint8_t dropped = g_GunDropped[index].exchange(0xFF, std::memory_order_acq_rel);
        g_GunFlags[index].store(nullptr, std::memory_order_release);
        if (dropped == 1 && soldiers)
            RestoreGunDropSeh(soldiers, index);
    }

    void SetRider(std::uint32_t index, Rider rider)
    {
        const auto before = static_cast<Rider>(g_Rider[index].exchange(static_cast<std::uint8_t>(rider), std::memory_order_acq_rel));
        if (before == Rider::None && rider != Rider::None)
            g_RiderCount.fetch_add(1, std::memory_order_relaxed);
        else if (before != Rider::None && rider == Rider::None)
            g_RiderCount.fetch_sub(1, std::memory_order_relaxed);
        if (rider == Rider::None)
        {
            g_Side[index].store(static_cast<std::uint8_t>(HeliSeatSide::Nearest), std::memory_order_relaxed);
            g_HitKept[index].store(0, std::memory_order_relaxed);
            g_AiAimMs[index].store(0, std::memory_order_relaxed);
            g_GunFlags[index].store(nullptr, std::memory_order_release);   // the paths that can still reach him put the bit back first
            g_GunDropped[index].store(0xFF, std::memory_order_release);
            g_FallWhy[index].store(nullptr, std::memory_order_relaxed);
            g_HeliDead[index].store(0, std::memory_order_relaxed);
            g_AimTarget[index].store(kRideHeliNoTarget, std::memory_order_relaxed);
            RootAdjustPin_Forget(index);
        }
    }

    struct Doors
    {
        std::uint32_t left   = kMaxSoldiers;   // the rider on it, kMaxSoldiers when free
        std::uint32_t right  = kMaxSoldiers;
        int           unsure = 0;              // riders still on their way to the nearer door
    };

    Doors DoorsOf(std::uint16_t heli, std::uint32_t self)
    {
        Doors doors;
        for (std::uint32_t i = 0; i < kMaxSoldiers; ++i)
        {
            if (i == self || g_Rider[i].load(std::memory_order_acquire) != static_cast<std::uint8_t>(Rider::Seated) || g_Heli[i].load(std::memory_order_relaxed) != heli)
                continue;
            const auto side = static_cast<HeliSeatSide>(g_Side[i].load(std::memory_order_relaxed));
            if (side == HeliSeatSide::Left)
                doors.left = i;
            else if (side == HeliSeatSide::Right)
                doors.right = i;
            else
                ++doors.unsure;
        }
        return doors;
    }

    bool HeliIsDead(std::uint32_t heliId)
    {
        for (std::size_t i = 0; i < kMaxDeadHelis; ++i)
            if (g_DeadHelis[i].load(std::memory_order_acquire) == (heliId & 0xFFFFu))
                return true;
        return false;
    }

    // the get-in picked the nearer door, a rider already on it sends him to the other one
    __declspec(noinline) bool ClaimNearerDoor(std::uint32_t index, bool nearerIsLeft)   // the get-in hook is an SEH function, the lock can't unwind there
    {
        std::lock_guard<std::recursive_mutex> lock(g_DoorMutex);
        const Doors doors = DoorsOf(g_Heli[index].load(std::memory_order_relaxed), index);
        const bool leftFree = doors.left == kMaxSoldiers;
        const bool rightFree = doors.right == kMaxSoldiers;
        bool left = nearerIsLeft;
        if (left && !leftFree && rightFree)
            left = false;
        else if (!left && !rightFree && leftFree)
            left = true;
        g_Side[index].store(static_cast<std::uint8_t>(left ? HeliSeatSide::Left : HeliSeatSide::Right), std::memory_order_relaxed);
        return left;
    }

    struct RideTrace
    {
        std::uint8_t  taskType;
        std::uint8_t  taskFlags;
        std::uint16_t taskSerial;
        std::uint16_t slotFlags;
        std::uint8_t  status;
        bool          riding;
        bool          idleStable;    // the ride command waits for it before it starts
        bool          forceRealize;
        bool          hide;          // the realizer drops him while it is set
        bool          seatAsked;
    };

    bool ReadRideTrace(std::uint8_t* soldiers, std::uint32_t index, RideTrace& out)
    {
        __try
        {
            auto* part       = soldiers + kSoldier2Part;
            auto* controller = *reinterpret_cast<std::uint8_t**>(soldiers + kSoldierActionController);
            auto* task       = *reinterpret_cast<std::uint8_t**>(controller + kControllerTasks) + static_cast<std::size_t>(index) * kTaskSize;
            auto* slot       = *reinterpret_cast<std::uint8_t**>(controller + kControllerSlots) + static_cast<std::size_t>(index) * kSlotSize;
            auto* flagInfo   = *reinterpret_cast<std::uint8_t**>(part + kPartFlagInfo);
            if (index >= *reinterpret_cast<std::uint32_t*>(flagInfo + 0x10))
                return false;
            const std::uint64_t record = *reinterpret_cast<std::uint64_t*>(*reinterpret_cast<std::uint8_t**>(flagInfo + 8) + static_cast<std::size_t>(index) * kFlagRecordSize);
            out.taskType     = task[0xC6];
            out.taskFlags    = task[0xC2];
            out.taskSerial   = *reinterpret_cast<std::uint16_t*>(task + 0xBA);
            out.slotFlags    = *reinterpret_cast<std::uint16_t*>(slot + kSlotFlags);
            out.status       = (*reinterpret_cast<std::uint8_t**>(part + kPartStatusBytes))[index];
            out.riding       = (record & 1) != 0;
            out.idleStable   = (record >> 17 & 1) != 0;
            out.forceRealize = (record >> 47 & 1) != 0;
            out.hide         = (record >> 50 & 1) != 0;
            out.seatAsked    = ((*reinterpret_cast<std::uint64_t**>(part + kPartSignalMasks))[index] & 0x100000) != 0;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void BuildRideCommand(std::uint8_t* command, std::uint16_t heliId, std::uint8_t mode)
    {
        std::memset(command, 0, kCommandSize);
        *reinterpret_cast<std::uint16_t*>(command) = heliId;
        command[2] = mode;   // 0 get in, 2 get out, bit 2 skips the get-in clip
        *reinterpret_cast<std::uint32_t*>(command + 0xB0) = kCommandFlags;
        command[0xB4] = kRideHeliAction;   // action type of a command
        *reinterpret_cast<std::uint16_t*>(command + 0xB6) = 0xFFFF;
    }

    SetCommand_t SetCommand()
    {
        static const SetCommand_t setCommand = gAddr.ActionController_SetCommand
            ? reinterpret_cast<SetCommand_t>(ResolveGameAddress(gAddr.ActionController_SetCommand))
            : nullptr;
        return setCommand;
    }

    std::uint32_t SoldierCountSeh(std::uint8_t* soldiers)
    {
        __try
        {
            return *reinterpret_cast<std::uint32_t*>(soldiers + kSoldierCount);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    std::uint8_t* SoldiersOf(std::uint32_t soldierId, const char** reason)
    {
        thread_local char why[128];
        const std::uint32_t index = soldierId & 0x1FFu;
        const GameObjectInstance::Found found = GameObjectInstance::FindSoldier(soldierId);
        switch (found.step)
        {
        case GameObjectInstance::Step::Found:
            break;
        case GameObjectInstance::Step::PastInstanceCount:
            std::snprintf(why, sizeof(why), "soldier index %u is past the game's %u soldier instances", index, found.instanceCount);
            *reason = why;
            return nullptr;
        default:
            std::snprintf(why, sizeof(why), "soldier lookup stopped at step %u.%u", static_cast<unsigned>(found.step), static_cast<unsigned>(found.soldierStep));
            *reason = why;
            return nullptr;
        }
        const std::uint32_t count = SoldierCountSeh(found.object);
        if (index >= count)
        {
            std::snprintf(why, sizeof(why), "soldier index %u is past the soldier object's %u soldiers", index, count);
            *reason = why;
            return nullptr;
        }
        return found.object;
    }

    std::int32_t OverrideCommand(std::uint8_t* soldiers, std::uint32_t index, std::uint8_t* command)
    {
        __try
        {
            void* controller = *reinterpret_cast<void**>(soldiers + kSoldierActionController);
            void** vtbl = *reinterpret_cast<void***>(controller);
            return reinterpret_cast<OverrideCommand_t>(vtbl[kVtbl_OverrideCommand / sizeof(void*)])(controller, index, command);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return kNoToken;
        }
    }

    std::uint8_t* ControllerSeh(std::uint8_t* soldiers)
    {
        __try
        {
            return *reinterpret_cast<std::uint8_t**>(soldiers + kSoldierActionController);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    bool OverrideHeldSeh(std::uint8_t* controller, std::uint32_t index, std::int32_t token)
    {
        __try
        {
            auto* slot = *reinterpret_cast<std::uint8_t**>(controller + kControllerSlots) + static_cast<std::size_t>(index) * kSlotSize;
            return (*reinterpret_cast<std::uint16_t*>(slot + kSlotFlags) & kSlotOverridden) != 0
                && *reinterpret_cast<std::int32_t*>(slot + kSlotOverrideToken) == token;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool OverrideStillOurs(std::uint8_t* soldiers, std::uint32_t index, std::int32_t token)
    {
        std::uint8_t* controller = ControllerSeh(soldiers);
        return controller && OverrideHeldSeh(controller, index, token);
    }

    struct OverrideHolder
    {
        std::uint8_t  actionType;
        std::uint8_t  stackedActionType;
        std::uint16_t target;
        std::uint32_t commandFlags;
        std::int32_t  token;
    };

    bool ReadOverrideHolder(std::uint8_t* soldiers, std::uint32_t index, OverrideHolder& holder)
    {
        __try
        {
            auto* controller = *reinterpret_cast<std::uint8_t**>(soldiers + kSoldierActionController);
            auto* slot = *reinterpret_cast<std::uint8_t**>(controller + kControllerSlots) + static_cast<std::size_t>(index) * kSlotSize;
            holder.actionType        = slot[0xB4];   // the command running now, the AI's own one waits at +0xC0
            holder.stackedActionType = slot[kCommandSize + 0xB4];
            holder.target            = *reinterpret_cast<std::uint16_t*>(slot);
            holder.commandFlags      = *reinterpret_cast<std::uint32_t*>(slot + 0xB0);
            holder.token             = *reinterpret_cast<std::int32_t*>(slot + kSlotOverrideToken);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool SendGetOut(std::uint8_t* soldiers, std::uint32_t index, std::int32_t token, std::uint8_t* command)
    {
        const SetCommand_t setCommand = SetCommand();
        if (!setCommand)
            return false;
        __try
        {
            void* controller = *reinterpret_cast<void**>(soldiers + kSoldierActionController);
            void** vtbl = *reinterpret_cast<void***>(controller);
            reinterpret_cast<CancelOverride_t>(vtbl[kVtbl_CancelOverride / sizeof(void*)])(controller, index, token);
            setCommand(controller, index, command);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool CancelOverrideSeh(std::uint8_t* controller, std::uint32_t index, std::int32_t token)
    {
        __try
        {
            void** vtbl = *reinterpret_cast<void***>(controller);
            reinterpret_cast<CancelOverride_t>(vtbl[kVtbl_CancelOverride / sizeof(void*)])(controller, index, token);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint8_t LifeStatusSeh(std::uint8_t* soldiers, std::uint32_t index)
    {
        __try
        {
            auto* life = *reinterpret_cast<std::uint8_t**>(soldiers + kSoldier2Part + kPartLife);
            return (*reinterpret_cast<std::uint8_t**>(life + 8))[static_cast<std::size_t>(index) * kLifeEntrySize + kLifeStatus];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    std::uint8_t LifeDownKindSeh(std::uint8_t* soldiers, std::uint32_t index)
    {
        __try
        {
            auto* life = *reinterpret_cast<std::uint8_t**>(soldiers + kSoldier2Part + kPartLife);
            return (*reinterpret_cast<std::uint8_t**>(life + 8))[static_cast<std::size_t>(index) * kLifeEntrySize + kLifeDownKind] & 3;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    void ReplaySeh(std::uint8_t* controller, std::uint32_t index, std::uint8_t* command)
    {
        const SetCommand_t setCommand = SetCommand();
        if (!setCommand)
            return;
        __try
        {
            setCommand(controller, index, command);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    bool LetGo(std::uint8_t* controller, std::uint8_t* soldiers, std::uint32_t index, std::int32_t token, const char* why)
    {
        std::int32_t expected = token;
        if (!g_Token[index].compare_exchange_strong(expected, kNoToken, std::memory_order_acq_rel))
            return false;   // claimed first, so the cancel below can't bring us back here for him
        CancelOverrideSeh(controller, index, token);
        RestoreGun(index, soldiers);
        SetRider(index, Rider::None);
        return true;
    }

    float SoldierYSeh(std::uint8_t* soldiers, std::uint32_t index, bool& known)
    {
        __try
        {
            auto* part = soldiers + kSoldier2Part;
            const float* position = *reinterpret_cast<float**>(*reinterpret_cast<std::uint8_t**>(part + kPartPositions) + 0x20) + static_cast<std::size_t>(index) * 4;
            known = true;
            return position[1];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            known = false;
            return 0.0f;
        }
    }

    // vanilla's fall push (DamageAiImpl::SetNormalBulletDamageActionCommand 0x1414576B1): falling loop until he lands, then the landing clip
    void MakeFall(std::uint8_t* command, std::uint32_t index)
    {
        const float x = g_DoorOutX[index].load(std::memory_order_relaxed);
        const float z = g_DoorOutZ[index].load(std::memory_order_relaxed);
        if (x != 0.0f || z != 0.0f)
        {
            const float direction[4] = { x, 0.0f, z, 0.0f };
            std::memcpy(command, direction, sizeof(direction));
        }
        *reinterpret_cast<std::uint32_t*>(command + kDamageActionType) = kFallPush;
    }

    void ThrowOutOfDoor(std::uint8_t* command, std::uint32_t index)
    {
        const float x = g_DoorOutX[index].load(std::memory_order_relaxed);
        const float z = g_DoorOutZ[index].load(std::memory_order_relaxed);
        auto* heading = reinterpret_cast<float*>(command);   // the blow carries him along x, z
        const float length = std::sqrt(heading[0] * heading[0] + heading[2] * heading[2]);
        if ((x == 0.0f && z == 0.0f) || length < 0.0001f || heading[0] * x + heading[2] * z >= 0.0f)
            return;
        heading[0] = x * length;
        heading[2] = z * length;
    }

    void BuildFall(std::uint8_t* command, std::uint32_t index)   // laid out as DamageAiImpl::SetCommand does
    {
        std::memset(command, 0, kCommandSize);
        const float ahead[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
        std::memcpy(command, ahead, sizeof(ahead));
        *reinterpret_cast<std::uint32_t*>(command + 0xB0) = kDamageFlags;
        command[0xB4] = kDamageAction;
        *reinterpret_cast<std::uint16_t*>(command + 0xB6) = 0xFFFF;
        MakeFall(command, index);
    }

    void StartFall(std::uint8_t* soldiers, std::uint32_t index)
    {
        bool known = false;
        const float y = soldiers ? SoldierYSeh(soldiers, index, known) : 0.0f;
        g_FallStartY[index].store(known ? y : kUnknownStartY, std::memory_order_relaxed);
        g_FallHitKept[index].store(0, std::memory_order_relaxed);
        g_FallLanded[index].store(0, std::memory_order_release);
        if (g_FallSinceMs[index].exchange(GetTickCount64(), std::memory_order_acq_rel) == 0)
            g_FallingCount.fetch_add(1, std::memory_order_relaxed);
    }

    void EndFall(std::uint32_t index)
    {
        g_FallHitKept[index].store(0, std::memory_order_relaxed);
        g_FallLanded[index].store(0, std::memory_order_relaxed);
        if (g_FallSinceMs[index].exchange(0, std::memory_order_acq_rel) != 0)
            g_FallingCount.fetch_sub(1, std::memory_order_relaxed);
    }

    void* GameFunction(std::uintptr_t address)
    {
        return address ? ResolveGameAddress(address) : nullptr;
    }

    // as Lua ChangeLifeState DEAD, through Soldier2Impl's own vtable once it is proven to be the one this build has
    bool KillSeh(std::uint8_t* soldiers, std::uint32_t index, void* latestOwner, void* requestDamage, std::uint16_t& owner)
    {
        __try
        {
            void** vtbl = *reinterpret_cast<void***>(soldiers);
            if (!latestOwner || !requestDamage || vtbl[kVtbl_LatestOwner / sizeof(void*)] != latestOwner || vtbl[kVtbl_RequestDamage / sizeof(void*)] != requestDamage)
                return false;
            owner = reinterpret_cast<LatestOwner_t>(latestOwner)(soldiers, index);
            reinterpret_cast<RequestDamage_t>(requestDamage)(soldiers, index, kAttackKill, owner);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void FallOut(std::uint8_t* soldiers, std::uint32_t index, std::int32_t token, const char* why)
    {
        std::uint8_t* controller = ControllerSeh(soldiers);
        alignas(16) std::uint8_t fall[kCommandSize];
        if (!(g_HitKept[index].load(std::memory_order_acquire) && ReadGuarded(g_HitSeq[index], fall, g_Hit[index], kCommandSize)))
            BuildFall(fall, index);
        else
            MakeFall(fall, index);   // his last hit, played as the fall
        if (controller && LetGo(controller, soldiers, index, token, why))
        {
            StartFall(soldiers, index);
            ReplaySeh(controller, index, fall);
        }
    }

    // after he is down: dead already = his mid-air death plays on the ground; a drop of 8 m or more kills him, credited to whoever hit him last
    void TendFall(std::uint32_t index, std::uint64_t now)
    {
        const std::uint64_t since = g_FallSinceMs[index].load(std::memory_order_acquire);
        const bool landed = g_FallLanded[index].load(std::memory_order_acquire) != 0;
        if (since == 0 || (!landed && (since > now || now - since < kFallWaitMs)))   // a fall started after the pump read the clock is not due
            return;
        const char* lookupReason = nullptr;
        std::uint8_t* soldiers = SoldiersOf((static_cast<std::uint32_t>(kSoldierType) << 9) | index, &lookupReason);
        bool known = false;
        const float y = soldiers ? SoldierYSeh(soldiers, index, known) : 0.0f;
        const float drop = known ? g_FallStartY[index].load(std::memory_order_relaxed) - y : kFallKillDropM;
        alignas(16) std::uint8_t hit[kCommandSize];
        const bool replay = g_FallHitKept[index].load(std::memory_order_acquire) && ReadGuarded(g_FallHitSeq[index], hit, g_FallHit[index], kCommandSize);
        EndFall(index);   // before the replay, so it is not kept again
        if (!soldiers)
            return;
        if (LifeStatusSeh(soldiers, index) == kLifeDead)
        {
            if (replay)
                if (std::uint8_t* controller = ControllerSeh(soldiers))
                    ReplaySeh(controller, index, hit);
            return;
        }
        if (drop < kFallKillDropM)
        {
            if (replay && LifeStatusSeh(soldiers, index) == kLifeDown)   // hit in mid-air and down now: his reaction plays on the ground
                if (std::uint8_t* controller = ControllerSeh(soldiers))
                    ReplaySeh(controller, index, hit);
            return;
        }
        static void* const latestOwner = GameFunction(gAddr.Soldier2Impl_GetLatestDamageOffenseOwnerId);
        static void* const requestDamage = GameFunction(gAddr.Soldier2Impl_RequestDamage);
        std::uint16_t owner = kNoOwner;
        if (!KillSeh(soldiers, index, latestOwner, requestDamage, owner))
            LogError("RequestToGetOnHelicopterAndAimTarget: soldier %u's fall cannot kill him, Soldier2Impl::RequestDamage is not where this build expects it", index);
    }

    void __fastcall hkGetInAction(void* rideHeli, std::uint32_t realizedSlot, int proc, void* motionEvent)
    {
        g_OrigGetInAction(rideHeli, realizedSlot, proc, motionEvent);
        if (proc != kProcEnter || MissionCodeGuard::ShouldBypassHooks())
            return;
        __try
        {
            auto* self = static_cast<std::uint8_t*>(rideHeli);
            const std::uint32_t entry = realizedSlot - static_cast<std::uint32_t>(*reinterpret_cast<std::int32_t*>(self + 0x90));
            auto* work = *reinterpret_cast<std::uint8_t**>(self + 0x88) + static_cast<std::size_t>(entry) * 0x28;   // 0x28 per riding soldier, his index at +0x10
            const std::uint32_t index = *reinterpret_cast<std::uint32_t*>(work + 0x10);
            if (index >= kMaxSoldiers || g_Rider[index].load(std::memory_order_acquire) == static_cast<std::uint8_t>(Rider::None))
                return;
            g_RideStep[index].store(1, std::memory_order_relaxed);
            const auto side = static_cast<HeliSeatSide>(g_Side[index].load(std::memory_order_relaxed));
            if (side == HeliSeatSide::Left)
                work[0x26] = static_cast<std::uint8_t>(work[0x26] | kLeftDoorBit);   // set = he uses the left door and seat
            else if (side == HeliSeatSide::Right)
                work[0x26] = static_cast<std::uint8_t>(work[0x26] & ~kLeftDoorBit);
            else if (ClaimNearerDoor(index, (work[0x26] & kLeftDoorBit) != 0))   // kept, unrealizing wipes the bit
                work[0x26] = static_cast<std::uint8_t>(work[0x26] | kLeftDoorBit);
            else
                work[0x26] = static_cast<std::uint8_t>(work[0x26] & ~kLeftDoorBit);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }
}

bool SoldierRideHeli_GetOn(std::uint32_t soldierId, std::uint32_t heliId, HeliSeatSide side, std::uint16_t aimTarget, const char** reason)
{
    const std::uint32_t index = soldierId & 0x1FFu;
    if ((soldierId >> 9) != kSoldierType || index >= kMaxSoldiers)
    {
        *reason = "target is not a soldier";
        return false;
    }
    std::uint8_t* soldiers = SoldiersOf(soldierId, reason);
    if (!soldiers)
        return false;
    const std::int32_t previous = g_Token[index].load(std::memory_order_relaxed);
    if (previous != kNoToken && OverrideStillOurs(soldiers, index, previous))
    {
        *reason = "he is already on a helicopter";
        return false;
    }
    if (g_Rider[index].load(std::memory_order_acquire) == static_cast<std::uint8_t>(Rider::Leaving))
    {
        *reason = "he is still getting off a helicopter";
        return false;
    }
    if (g_FallSinceMs[index].load(std::memory_order_acquire) != 0)
    {
        *reason = "he is falling out of a helicopter";
        return false;
    }
    if (HeliIsDead(heliId))
    {
        *reason = "that helicopter is destroyed";
        return false;
    }

    std::lock_guard<std::recursive_mutex> lock(g_DoorMutex);
    const Doors doors = DoorsOf(static_cast<std::uint16_t>(heliId), index);
    thread_local char taken[96];
    if (side == HeliSeatSide::Left && doors.left != kMaxSoldiers)
    {
        std::snprintf(taken, sizeof(taken), "soldier %u already rides the LEFT door", doors.left);
        *reason = taken;
        return false;
    }
    if (side == HeliSeatSide::Right && doors.right != kMaxSoldiers)
    {
        std::snprintf(taken, sizeof(taken), "soldier %u already rides the RIGHT door", doors.right);
        *reason = taken;
        return false;
    }
    if ((doors.left != kMaxSoldiers) + (doors.right != kMaxSoldiers) + doors.unsure >= 2)
    {
        *reason = "both doors of that helicopter are taken";
        return false;
    }
    if (side == HeliSeatSide::Nearest && doors.left != kMaxSoldiers)
        side = HeliSeatSide::Right;
    else if (side == HeliSeatSide::Nearest && doors.right != kMaxSoldiers)
        side = HeliSeatSide::Left;

    alignas(16) std::uint8_t command[kCommandSize];
    BuildRideCommand(command, static_cast<std::uint16_t>(heliId), kRideGetIn);
    g_Side[index].store(static_cast<std::uint8_t>(side), std::memory_order_relaxed);
    const std::int32_t token = OverrideCommand(soldiers, index, command);
    if (token == kNoToken)
    {
        g_Side[index].store(static_cast<std::uint8_t>(HeliSeatSide::Nearest), std::memory_order_relaxed);
        g_Token[index].store(kNoToken, std::memory_order_relaxed);
        thread_local char why[160];
        OverrideHolder holder{};
        if (ReadOverrideHolder(soldiers, index, holder))
        {
            std::snprintf(why, sizeof(why), "his actions are already taken over by command type 0x%02X (target 0x%04X, flags 0x%X, token %d, AI command waiting 0x%02X)",
                holder.actionType, holder.target, holder.commandFlags, holder.token, holder.stackedActionType);
            *reason = why;
        }
        else
            *reason = "his actions are already taken over by something else";
        return false;
    }
    g_Soldiers.store(soldiers, std::memory_order_release);
    g_Heli[index].store(static_cast<std::uint16_t>(heliId), std::memory_order_relaxed);
    g_Token[index].store(token, std::memory_order_relaxed);
    g_RideStep[index].store(0, std::memory_order_relaxed);
    g_HitKept[index].store(0, std::memory_order_relaxed);   // a rider whose last ride was never cleaned up starts fresh too
    g_AiAimMs[index].store(0, std::memory_order_relaxed);
    g_FallWhy[index].store(nullptr, std::memory_order_relaxed);
    g_HeliDead[index].store(0, std::memory_order_relaxed);
    SetRider(index, Rider::Seated);
    g_AimTarget[index].store(aimTarget, std::memory_order_relaxed);
    if (HeliIsDead(heliId))
        g_HeliDead[index].store(1, std::memory_order_release);   // it died while he was being sent
    return true;
}

bool SoldierRideHeli_GetOff(std::uint32_t soldierId, const char** reason)
{
    const std::uint32_t index = soldierId & 0x1FFu;
    if ((soldierId >> 9) != kSoldierType || index >= kMaxSoldiers)
    {
        *reason = "target is not a soldier";
        return false;
    }
    const std::int32_t token = g_Token[index].load(std::memory_order_relaxed);
    if (token == kNoToken)
    {
        *reason = "he did not get on with RequestToGetOnHelicopterAndAimTarget";
        return false;
    }
    std::uint8_t* soldiers = SoldiersOf(soldierId, reason);
    if (!soldiers)
        return false;
    if (!OverrideStillOurs(soldiers, index, token))
    {
        g_Token[index].store(kNoToken, std::memory_order_relaxed);
        g_Side[index].store(static_cast<std::uint8_t>(HeliSeatSide::Nearest), std::memory_order_relaxed);
        RestoreGun(index, soldiers);
        SetRider(index, Rider::None);
        *reason = "he is no longer on the helicopter";
        return false;
    }

    alignas(16) std::uint8_t hit[kCommandSize];
    if (LifeStatusSeh(soldiers, index) != 0 && g_HitKept[index].load(std::memory_order_acquire)
        && ReadGuarded(g_HitSeq[index], hit, g_Hit[index], kCommandSize))
    {
        std::uint8_t* controller = ControllerSeh(soldiers);
        if (controller && LetGo(controller, soldiers, index, token, "is asleep, his knock-out plays instead of the get-out"))
        {
            ReplaySeh(controller, index, hit);
            return true;
        }
    }

    alignas(16) std::uint8_t command[kCommandSize];
    BuildRideCommand(command, g_Heli[index].load(std::memory_order_relaxed), kRideGetOut);
    if (!SendGetOut(soldiers, index, token, command))
    {
        *reason = "the get-out command failed";   // token kept, the pump lets him go once the command is really gone
        return false;
    }
    RestoreGun(index, soldiers);
    g_Token[index].store(kNoToken, std::memory_order_relaxed);
    g_LeaveBy[index].store(GetTickCount64() + kLeavingMs, std::memory_order_relaxed);
    SetRider(index, Rider::Leaving);
    return true;
}

bool SoldierRideHeli_AnyRider()
{
    return g_RiderCount.load(std::memory_order_relaxed) > 0;
}

bool SoldierRideHeli_IsDoorRider(std::uint32_t soldierIndex)
{
    return soldierIndex < kMaxSoldiers
        && g_RiderCount.load(std::memory_order_relaxed) > 0
        && g_Rider[soldierIndex].load(std::memory_order_acquire) != static_cast<std::uint8_t>(Rider::None);
}

std::uint16_t SoldierRideHeli_Heli(std::uint32_t soldierIndex)
{
    if (!SoldierRideHeli_OnSeat(soldierIndex))
        return 0;
    return g_Heli[soldierIndex].load(std::memory_order_relaxed);
}

void SoldierRideHeli_NoteStep(std::uint32_t soldierIndex, std::uint8_t step)
{
    if (soldierIndex < kMaxSoldiers)
        g_RideStep[soldierIndex].store(step, std::memory_order_relaxed);
}

bool SoldierRideHeli_OnSeat(std::uint32_t soldierIndex)
{
    return SoldierRideHeli_IsDoorRider(soldierIndex)
        && g_Rider[soldierIndex].load(std::memory_order_acquire) == static_cast<std::uint8_t>(Rider::Seated)
        && g_RideStep[soldierIndex].load(std::memory_order_relaxed) == 2;
}

HeliSeatSide SoldierRideHeli_Side(std::uint32_t soldierIndex)
{
    return soldierIndex < kMaxSoldiers ? static_cast<HeliSeatSide>(g_Side[soldierIndex].load(std::memory_order_relaxed)) : HeliSeatSide::Nearest;
}

bool SoldierRideHeli_OnSetCommand(std::uint8_t* actionController, std::uint32_t soldierIndex, std::uint8_t* command)
{
    if (soldierIndex >= kMaxSoldiers)
        return false;
    if (g_FallingCount.load(std::memory_order_relaxed) > 0 && g_FallSinceMs[soldierIndex].load(std::memory_order_acquire) != 0
        && !g_FallLanded[soldierIndex].load(std::memory_order_acquire) && command[0xB4] == kDamageAction
        && *reinterpret_cast<const std::uint32_t*>(command + kDamageActionType) != kFallPush)
    {
        WriteGuarded(g_FallHitSeq[soldierIndex], g_FallHit[soldierIndex], command, kCommandSize);
        g_FallHitKept[soldierIndex].store(1, std::memory_order_release);
        return true;   // the hit still counts, his fall keeps playing
    }
    if (g_RiderCount.load(std::memory_order_relaxed) <= 0)
        return false;
    if (g_Rider[soldierIndex].load(std::memory_order_acquire) != static_cast<std::uint8_t>(Rider::Seated))
        return false;
    const std::int32_t token = g_Token[soldierIndex].load(std::memory_order_relaxed);
    if (token == kNoToken || !OverrideHeldSeh(actionController, soldierIndex, token))
        return false;
    if (command[kCommandSubAction] == 1)
    {
        WriteGuarded(g_AiAimSeq[soldierIndex], g_AiAim[soldierIndex], command + kCommandAimBlock, kAimBlockSize);
        g_AiAimMs[soldierIndex].store(GetTickCount64(), std::memory_order_release);
        if (*reinterpret_cast<const std::uint16_t*>(command + kCommandAimBlock + kAimFire) == kAimFireBytes)
            g_AiShots[soldierIndex].fetch_add(1, std::memory_order_release);   // RouteAiImpl::SetAimingActionParam's shot
    }
    if (command[0xB4] != kDamageAction)
        return false;   // the AI's own orders stay parked behind the ride, as vanilla does
    std::uint8_t* soldiers = g_Soldiers.load(std::memory_order_acquire);
    const std::uint8_t step = g_RideStep[soldierIndex].load(std::memory_order_relaxed);
    const bool thrown = *reinterpret_cast<const std::uint32_t*>(command + kDamageActionType) - kBlastThrowFirst < kBlastThrowCount;
    if (step != 2 && step != 4)   // still walking to the heli or climbing in: the seat can't play it
    {
        if (thrown)
            LetGo(actionController, soldiers, soldierIndex, token, "was blown away before reaching his seat");
        else if (soldiers && LifeStatusSeh(soldiers, soldierIndex) != 0)
            LetGo(actionController, soldiers, soldierIndex, token, "went down before reaching his seat");
        return false;
    }
    if (thrown)
    {
        if (LetGo(actionController, soldiers, soldierIndex, token, "was blown out of his door"))
        {
            ThrowOutOfDoor(command, soldierIndex);
            StartFall(soldiers, soldierIndex);
        }
        return false;   // vanilla's blast clip throws him, then his fall and landing
    }
    if (soldiers && LifeStatusSeh(soldiers, soldierIndex) == kLifeDown)
    {
        const std::uint8_t kind = LifeDownKindSeh(soldiers, soldierIndex);
        if (kind == kDownFaint || kind == kDownDying)
        {
            if (LetGo(actionController, soldiers, soldierIndex, token, kind == kDownFaint ? "fainted on his seat" : "is dying on his seat"))
            {
                MakeFall(command, soldierIndex);   // as GZ: he drops out of the door and the landing kills him
                StartFall(soldiers, soldierIndex);
            }
            return false;   // lost the claim = already let go
        }
    }
    WriteGuarded(g_HitSeq[soldierIndex], g_Hit[soldierIndex], command, kCommandSize);
    g_HitKept[soldierIndex].store(1, std::memory_order_release);
    return true;   // as GZ's rider: hurt, asleep or dead, he stays on his seat and the seat plays it
}

void SoldierRideHeli_SetFlinchDisabled(std::uint32_t soldierIndex, bool disabled)
{
    if (soldierIndex < kMaxSoldiers)
        g_NoFlinch[soldierIndex].store(disabled ? 1 : 0, std::memory_order_relaxed);
}

bool SoldierRideHeli_FlinchDisabled(std::uint32_t soldierIndex)
{
    return soldierIndex < kMaxSoldiers && g_NoFlinch[soldierIndex].load(std::memory_order_relaxed) != 0;
}

void SoldierRideHeli_RequestFall(std::uint32_t soldierIndex, const char* why)
{
    if (soldierIndex < kMaxSoldiers && g_Rider[soldierIndex].load(std::memory_order_acquire) == static_cast<std::uint8_t>(Rider::Seated))
        g_FallWhy[soldierIndex].store(why, std::memory_order_release);
}

void SoldierRideHeli_HeliDestroyed(std::uint32_t heliId)
{
    const std::uint32_t heli = heliId & 0xFFFFu;
    if (heli == kNoHeli)
        return;
    if (!HeliIsDead(heli))
    {
        for (std::size_t i = 0; i < kMaxDeadHelis; ++i)
        {
            std::uint32_t free = kNoHeli;
            if (g_DeadHelis[i].compare_exchange_strong(free, heli, std::memory_order_acq_rel))
                break;
        }
    }
    if (g_RiderCount.load(std::memory_order_relaxed) <= 0)
        return;
    for (std::uint32_t index = 0; index < kMaxSoldiers; ++index)
    {
        if (g_Rider[index].load(std::memory_order_acquire) != static_cast<std::uint8_t>(Rider::Seated) || g_Heli[index].load(std::memory_order_relaxed) != heli)
            continue;
        g_HeliDead[index].store(1, std::memory_order_release);
    }
}

bool SoldierRideHeli_HeliDead(std::uint32_t soldierIndex)
{
    return soldierIndex < kMaxSoldiers && g_HeliDead[soldierIndex].load(std::memory_order_acquire) != 0;
}

bool SoldierRideHeli_RequestFire(std::uint32_t soldierIndex, std::uint32_t targetId, float angle, float radius, const char** reason)
{
    if (soldierIndex >= kMaxSoldiers)
    {
        *reason = "target is not a soldier";
        return false;
    }
    float from[3] = {};
    float to[3] = {};
    if (!GameObjectPosition::Read((static_cast<std::uint32_t>(kSoldierType) << 9) | soldierIndex, from) || !GameObjectPosition::Read(targetId, to))
    {
        *reason = "his or the target's position could not be read";
        return false;
    }

    // GZ 0x140680A30: the shot lands radius away from the target, at angle degrees round the line of fire (0 = above it);
    // its frame is turned by yaw, then pitch, from the soldier to the target
    float forward[3] = { to[0] - from[0], to[1] - from[1], to[2] - from[2] };
    const float length = std::sqrt(forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2]);
    if (length > 0.0001f)
    {
        forward[0] /= length;
        forward[1] /= length;
        forward[2] /= length;
    }
    else
    {
        forward[0] = 0.0f;
        forward[1] = 0.0f;
        forward[2] = 1.0f;
    }
    float frameX[3] = { 1.0f, 0.0f, 0.0f };
    const float flat = std::sqrt(forward[0] * forward[0] + forward[2] * forward[2]);
    if (flat > 0.0001f)
    {
        frameX[0] = forward[2] / flat;
        frameX[2] = -forward[0] / flat;
    }
    const float frameY[3] = { forward[1] * frameX[2], forward[2] * frameX[0] - forward[0] * frameX[2], -forward[1] * frameX[0] };
    const float turn = angle * 0.0174532925f;
    const float alongX = -radius * std::sin(turn);
    const float alongY = radius * std::cos(turn);
    alignas(16) const float point[4] = {
        to[0] + alongX * frameX[0] + alongY * frameY[0],
        to[1] + alongX * frameX[1] + alongY * frameY[1],
        to[2] + alongX * frameX[2] + alongY * frameY[2],
        1.0f,
    };

    WriteGuarded(g_ShotSeq[soldierIndex], g_ShotPoint[soldierIndex], point, sizeof(point));   // one shot waits, a newer one replaces it as GZ's gun latch does
    if (g_ShotAtMs[soldierIndex].exchange(GetTickCount64(), std::memory_order_acq_rel) == 0)
        g_ShotsQueued.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool SoldierRideHeli_ShotsQueued()
{
    return g_ShotsQueued.load(std::memory_order_relaxed) > 0;
}

bool SoldierRideHeli_HasShot(std::uint32_t soldierIndex)
{
    return soldierIndex < kMaxSoldiers && g_ShotAtMs[soldierIndex].load(std::memory_order_acquire) != 0;
}

bool SoldierRideHeli_TakeShot(std::uint32_t soldierIndex, bool canFire, float* point)
{
    if (soldierIndex >= kMaxSoldiers || !canFire)
        return false;
    std::uint64_t at = g_ShotAtMs[soldierIndex].load(std::memory_order_acquire);
    if (at == 0)
        return false;
    alignas(16) float copy[4];
    if (!ReadGuarded(g_ShotSeq[soldierIndex], copy, g_ShotPoint[soldierIndex], sizeof(copy)))
        return false;   // being rewritten, next frame
    if (!g_ShotAtMs[soldierIndex].compare_exchange_strong(at, 0, std::memory_order_acq_rel))
        return false;   // a newer shot came in, next frame
    g_ShotsQueued.fetch_sub(1, std::memory_order_relaxed);
    std::memcpy(point, copy, sizeof(copy));
    return true;
}

std::uint32_t SoldierRideHeli_AiShots(std::uint32_t soldierIndex)
{
    return soldierIndex < kMaxSoldiers ? g_AiShots[soldierIndex].load(std::memory_order_acquire) : 0;
}

bool SoldierRideHeli_AimTarget(std::uint32_t soldierIndex, std::uint16_t& gameObjectId)
{
    if (soldierIndex >= kMaxSoldiers)
        return false;
    gameObjectId = g_AimTarget[soldierIndex].load(std::memory_order_relaxed);
    return gameObjectId != kRideHeliNoTarget;
}

void SoldierRideHeli_SetDoorOut(std::uint32_t soldierIndex, float x, float z)
{
    const float length = std::sqrt(x * x + z * z);
    if (soldierIndex >= kMaxSoldiers || length < 0.0001f)
        return;
    g_DoorOutX[soldierIndex].store(x / length, std::memory_order_relaxed);
    g_DoorOutZ[soldierIndex].store(z / length, std::memory_order_relaxed);
}

bool SoldierRideHeli_AnyFalling()
{
    return g_FallingCount.load(std::memory_order_relaxed) > 0;
}

bool SoldierRideHeli_Falling(std::uint32_t soldierIndex)
{
    return soldierIndex < kMaxSoldiers && g_FallSinceMs[soldierIndex].load(std::memory_order_acquire) != 0
        && !g_FallLanded[soldierIndex].load(std::memory_order_acquire);
}

void SoldierRideHeli_Landed(std::uint32_t soldierIndex)
{
    if (soldierIndex < kMaxSoldiers && g_FallSinceMs[soldierIndex].load(std::memory_order_acquire) != 0)
        g_FallLanded[soldierIndex].store(1, std::memory_order_release);
}

bool SoldierRideHeli_AiAim(std::uint32_t soldierIndex, std::uint8_t* block)
{
    if (soldierIndex >= kMaxSoldiers)
        return false;
    const std::uint64_t at = g_AiAimMs[soldierIndex].load(std::memory_order_acquire);
    if (at == 0 || GetTickCount64() - at > kAiAimFreshMs)
        return false;
    return ReadGuarded(g_AiAimSeq[soldierIndex], block, g_AiAim[soldierIndex], kAimBlockSize);
}

void SoldierRideHeli_KeepGun(std::uint32_t soldierIndex, std::uint8_t* lifeFlags)
{
    if (soldierIndex >= kMaxSoldiers || g_GunFlags[soldierIndex].load(std::memory_order_acquire) == lifeFlags)
        return;
    if (g_Rider[soldierIndex].load(std::memory_order_acquire) != static_cast<std::uint8_t>(Rider::Seated))
        return;
    std::uint8_t untouched = 0xFF;
    if (!g_GunDropped[soldierIndex].compare_exchange_strong(untouched, (*lifeFlags & kDropsGun) ? 1 : 0, std::memory_order_acq_rel))
        return;
    g_GunFlags[soldierIndex].store(lifeFlags, std::memory_order_release);
    _InterlockedAnd8(reinterpret_cast<char*>(lifeFlags), static_cast<char>(~kDropsGun));   // the life controller drops it the moment he falls asleep
    if (g_Rider[soldierIndex].load(std::memory_order_acquire) != static_cast<std::uint8_t>(Rider::Seated))   // let go while we took the bit: put it back
    {
        if (g_GunDropped[soldierIndex].exchange(0xFF, std::memory_order_acq_rel) == 1)
            _InterlockedOr8(reinterpret_cast<char*>(lifeFlags), static_cast<char>(kDropsGun));
        g_GunFlags[soldierIndex].store(nullptr, std::memory_order_release);
    }
}

void SoldierRideHeli_Pump()
{
    const bool riders = g_RiderCount.load(std::memory_order_relaxed) > 0;
    const bool shots = g_ShotsQueued.load(std::memory_order_relaxed) > 0;
    const bool falls = g_FallingCount.load(std::memory_order_relaxed) > 0;
    if (!riders && !shots && !falls)
        return;
    const std::uint64_t now = GetTickCount64();
    if (now - g_LastPumpMs < kPumpEveryMs)
        return;
    g_LastPumpMs = now;
    if (MissionCodeGuard::ShouldBypassHooks())
        return;
    for (std::uint32_t index = 0; index < kMaxSoldiers; ++index)
    {
        if (falls)
            TendFall(index, now);
        std::uint64_t shotAt = shots ? g_ShotAtMs[index].load(std::memory_order_acquire) : 0;
        if (shotAt != 0 && now - shotAt > kShotWaitMs && g_ShotAtMs[index].compare_exchange_strong(shotAt, 0, std::memory_order_acq_rel))
        {
            g_ShotsQueued.fetch_sub(1, std::memory_order_relaxed);
            LogWarn("RequestToFireOnHelicopter: soldier %u could not fire (not loaded in, down, or no weapon in hand), shot dropped", index);
        }
        const auto rider = static_cast<Rider>(g_Rider[index].load(std::memory_order_acquire));
        if (rider == Rider::None)
            continue;
        const char* lookupReason = nullptr;
        std::uint8_t* soldiers = SoldiersOf((static_cast<std::uint32_t>(kSoldierType) << 9) | index, &lookupReason);
        RideTrace trace{};
        const bool read = soldiers && ReadRideTrace(soldiers, index, trace);
        if (rider == Rider::Leaving && (!read || trace.taskType != kRideHeliAction || now > g_LeaveBy[index].load(std::memory_order_relaxed)))
        {
            SetRider(index, Rider::None);
            continue;
        }
        if (rider == Rider::Seated)
        {
            const std::int32_t token = g_Token[index].load(std::memory_order_relaxed);
            if (!soldiers || token == kNoToken || !OverrideStillOurs(soldiers, index, token))
            {
                g_Token[index].store(kNoToken, std::memory_order_relaxed);
                g_Side[index].store(static_cast<std::uint8_t>(HeliSeatSide::Nearest), std::memory_order_relaxed);
                RestoreGun(index, soldiers);
                SetRider(index, Rider::None);
                LogWarn("RequestToGetOnHelicopterAndAimTarget: soldier %u lost the heli command to something else", index);
                continue;
            }
            if (const char* why = g_FallWhy[index].exchange(nullptr, std::memory_order_acq_rel))
            {
                FallOut(soldiers, index, token, why);
                continue;
            }
        }
    }
}

void SoldierRideHeli_Clear()
{
    for (std::uint32_t i = 0; i < kMaxSoldiers; ++i)
    {
        g_Side[i].store(static_cast<std::uint8_t>(HeliSeatSide::Nearest), std::memory_order_relaxed);
        g_Token[i].store(kNoToken, std::memory_order_relaxed);
        g_Heli[i].store(0, std::memory_order_relaxed);
        g_Rider[i].store(static_cast<std::uint8_t>(Rider::None), std::memory_order_relaxed);
        g_RideStep[i].store(0, std::memory_order_relaxed);
    }
    g_RiderCount.store(0, std::memory_order_relaxed);
    for (std::uint32_t i = 0; i < kMaxSoldiers; ++i)
    {
        g_HitKept[i].store(0, std::memory_order_relaxed);
        g_AiAimMs[i].store(0, std::memory_order_relaxed);
        g_GunFlags[i].store(nullptr, std::memory_order_relaxed);
        g_GunDropped[i].store(0xFF, std::memory_order_relaxed);
        g_NoFlinch[i].store(0, std::memory_order_relaxed);
        g_FallWhy[i].store(nullptr, std::memory_order_relaxed);
        g_HeliDead[i].store(0, std::memory_order_relaxed);
        g_ShotAtMs[i].store(0, std::memory_order_relaxed);
        g_AimTarget[i].store(kRideHeliNoTarget, std::memory_order_relaxed);
        g_FallSinceMs[i].store(0, std::memory_order_relaxed);
        g_FallLanded[i].store(0, std::memory_order_relaxed);
        g_FallHitKept[i].store(0, std::memory_order_relaxed);
    }
    for (std::size_t i = 0; i < kMaxDeadHelis; ++i)
        g_DeadHelis[i].store(kNoHeli, std::memory_order_relaxed);
    g_ShotsQueued.store(0, std::memory_order_relaxed);
    g_FallingCount.store(0, std::memory_order_relaxed);
    RideHeliDoorSeat_Clear();
    g_Soldiers.store(nullptr, std::memory_order_release);
    RootAdjustPin_Clear();
}

bool Install_RideHeliActionImpl_State_GetInAction_Hook()
{
    if (g_Installed)
        return true;

    SoldierRideHeli_Clear();

    void* target = gAddr.RideHeliActionImpl_State_GetInAction ? ResolveGameAddress(gAddr.RideHeliActionImpl_State_GetInAction) : nullptr;
    if (!target)
    {
        LogError("RideHeliActionImpl_State_GetInAction: address missing for this build, soldier heli seat side ignored");
        return false;
    }
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkGetInAction), reinterpret_cast<void**>(&g_OrigGetInAction)))
    {
        LogError("RideHeliActionImpl_State_GetInAction: hook failed, soldier heli seat side ignored");
        return false;
    }
    g_Installed = true;
    return true;
}

bool Uninstall_RideHeliActionImpl_State_GetInAction_Hook()
{
    if (!g_Installed)
        return true;
    DisableAndRemoveHook(ResolveGameAddress(gAddr.RideHeliActionImpl_State_GetInAction));
    g_OrigGetInAction = nullptr;
    g_Installed       = false;
    return true;
}
