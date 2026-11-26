#include <pch.h>
#include "SoldierThrowCommand.h"

#include <Windows.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "AddressSet.h"
#include "FoxHashes.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"
#include "Soldier2CombatUtil_GrenadeUpdate.h"
#include "../equip/EquipAliasLease.h"
#include "../equip/EquipIdCompression.h"
#include "../equip/EquipPartParams.h"
#include "../fox/gm/GameObjectInstance.h"

namespace
{
    using OverrideCommand_t        = std::int32_t(__fastcall*)(void* controller, std::uint32_t index, std::uint8_t* command);
    using CancelOverride_t         = void(__fastcall*)(void* controller, std::uint32_t index, std::int32_t token);
    using HasOtherSoldierInPoint_t = bool(__fastcall*)(void* soldier, std::uint32_t index, const float* point, float radius, bool flag, float height);
    using GetDoorUnit_t            = std::uint8_t*(__fastcall*)(void* doorGameObject, std::uint64_t key);
    using GetDoorCenter_t          = float*(__fastcall*)(const void* doorUnit, float* center);
    using PathCodeFromString_t     = std::uint64_t(__fastcall*)(const char* path);
    using GetQuarkSystemTable_t    = std::uint8_t*(__fastcall*)();
    using SendMessage_t            = void*(__fastcall*)(void* manager, void* result, std::uint16_t gameObjectId, void* message);
    using DangerState_t            = std::uint8_t*(__fastcall*)(void* owner);

    constexpr std::uint32_t kSoldierType = 2;
    constexpr std::uint32_t kMaxSoldiers = 0x200;
    constexpr std::uint32_t kWholeType   = 0x1FF;
    constexpr std::int32_t  kNoToken     = -1;

    constexpr std::size_t kSoldier2Part            = 0x20;    // what the AI and Soldier2Util take
    constexpr std::size_t kSoldierCount            = 0x28;
    constexpr std::size_t kSoldierActionController = 0x70;
    constexpr std::size_t kPartLife                = 0x38;    // +8 one 0x40 entry per soldier
    constexpr std::size_t kLifeEntrySize           = 0x40;
    constexpr std::size_t kLifeStatus              = 0x3E;    // 1 unconscious, 2 dead
    constexpr std::size_t kPartDangerOwner         = 0xD0;    // +8 holds the indoor danger marks behind vt+0x268
    constexpr std::size_t kVtbl_DangerState        = 0x268;
    constexpr std::size_t kDangerSlots             = 0xC8;    // 16 x 0x18: x, y, z, radius, time, kind
    constexpr std::size_t kDangerSlotSize          = 0x18;
    constexpr int         kDangerSlotCount         = 16;
    constexpr std::size_t kDangerTimer             = 0x274;

    constexpr std::size_t   kControllerTasks      = 0x08;   // 0xD0 per soldier
    constexpr std::size_t   kTaskSize             = 0xD0;
    constexpr std::size_t   kTaskActionType       = 0xC6;
    constexpr std::size_t   kControllerSlots      = 0x10;   // 0x1E0 per soldier
    constexpr std::size_t   kSlotSize             = 0x1E0;
    constexpr std::size_t   kSlotFlags            = 0x19E;
    constexpr std::size_t   kSlotOverrideToken    = 0x1A4;
    constexpr std::uint16_t kSlotOverridden       = 0x100;
    constexpr std::size_t   kVtbl_OverrideCommand = 0x00;
    constexpr std::size_t   kVtbl_CancelOverride  = 0x08;

    constexpr std::size_t   kCommandSize    = 0xC0;
    constexpr std::size_t   kCommandType    = 0xB4;
    constexpr std::uint8_t  kThrowAction    = 0x0E;
    constexpr std::uint8_t  kDamageAction   = 3;
    constexpr std::uint8_t  kMeleeAction    = 0x17;   // hold-ups and CQC
    constexpr std::uint8_t  kRideHeliAction = 0x1D;
    constexpr std::uint32_t kThrowFlags     = 0x110012;
    constexpr std::uint32_t kThrowingType   = 12;     // EQP_TYPE Throwing

    constexpr float kLobAngle         = 0.785398f;    // PI/4, Soldier2CombatUtil::GrenadeUpdate
    constexpr float kLobAngleUphill   = 0.942478f;    // GrenadeUpdate's +20% for a target 2 m higher
    constexpr float kDoorAngle        = 0.0785398f;   // PI/4 * 0.1, ThrowGrenadeInDoor
    constexpr float kUphill           = 2.0f;
    constexpr float kFarThrowSq       = 225.0f;       // 15 m: the far throw
    constexpr float kAllyRadius       = 5.0f;
    constexpr float kAllyHeight       = 1.5f;
    constexpr float kRoomDepth        = 7.5f;
    constexpr float kDoorSearchRadius = 1.5f;
    constexpr float kDoorOpenSpeed    = 2.5f;
    constexpr float kMinDoorDistance  = 0.3f;

    constexpr std::uint64_t kMsgFindDoor        = 0xBC02A3A62EEFull;
    constexpr std::uint64_t kMsgDoorInfo        = 0x9C17DEAF1DFCull;
    constexpr std::uint64_t kMsgOpenDoor        = 0x9A610621F953ull;
    constexpr std::uint16_t kDoorTypeId         = 0xA600;   // GAME_OBJECT_TYPE_DOOR << 9
    constexpr std::size_t   kQuarkGameObjects   = 0x60;
    constexpr std::size_t   kVtbl_SendMessage   = 0x38;
    constexpr std::size_t   kDoorHandleObject   = 0x20;
    constexpr std::size_t   kDoorInfoTarget     = 0x08;
    constexpr unsigned      kExtensionShift     = 51;
    constexpr std::uint64_t kLuaExtension       = 0x31C;   // fox::Path keeps "lua" and 0xEF paths in another form
    constexpr std::uint64_t kOtherPathExtension = 0xEF;

    constexpr std::uint64_t kStartWaitMs = 3000;
    constexpr std::uint64_t kThrowWaitMs = 8000;
    constexpr std::uint64_t kPumpEveryMs = 50;

    constexpr std::uint8_t kHasOtherSoldierEntry[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x55, 0x56, 0x57, 0x48, 0x81, 0xEC, 0x70, 0x01, 0x00, 0x00,
                                                       0x0F, 0x29, 0xBC, 0x24, 0x50, 0x01, 0x00, 0x00 };
    constexpr std::uint8_t kGetDoorCenterEntry[]   = { 0x48, 0x8B, 0x41, 0x78, 0x0F, 0x28, 0x90, 0x90, 0x00, 0x00, 0x00, 0x0F, 0x58, 0x90, 0x80, 0x00, 0x00, 0x00 };
    constexpr std::uint8_t kGetDoorUnitEntry[]     = { 0xE9, 0x00, 0x00, 0x00, 0x00 };   // a jump thunk
    constexpr std::uint8_t kGetDoorUnitMask[]      = { 1, 0, 0, 0, 0 };

    enum class Throw : std::uint8_t { None, Waiting, Throwing };

    std::atomic<std::uint8_t>  g_State[kMaxSoldiers]   = {};
    std::atomic<std::int32_t>  g_Token[kMaxSoldiers]   = {};
    std::atomic<std::uint64_t> g_SinceMs[kMaxSoldiers] = {};
    std::atomic<int>           g_Held{ 0 };
    std::uint64_t              g_LastPumpMs = 0;

    struct alignas(16) FindDoorMessage
    {
        std::uint64_t id;
        std::int32_t  found;
        std::uint32_t pad0;
        void*         results;
        std::uint64_t pad1;
        float         position[4];   // w = search radius
        std::uint32_t flags;
        std::uint8_t  pad2[12];
        std::uint64_t tail0;
        std::uint32_t tail1;
        std::uint32_t tail2;
    };
    static_assert(sizeof(FindDoorMessage) == 0x50, "OpenDoor's find message is 0x50 bytes");

    struct alignas(16) FindDoorResults
    {
        std::uint8_t  data[0x40];
        std::uint32_t doorId;
        std::uint8_t  pad[0x3C];
    };

    struct alignas(16) DoorInfoMessage
    {
        std::uint64_t id;
        std::uint8_t* info;
        std::uint8_t  pad[0x10];
    };

    struct alignas(16) OpenDoorMessage
    {
        std::uint64_t id;
        std::uint8_t  close;
        std::uint8_t  pad0;
        std::uint16_t noTarget;
        std::uint32_t pad1;
        float         towards[4];
        float         speed;
        std::uint16_t zero;
        std::uint8_t  pad2[10];
    };
    static_assert(sizeof(OpenDoorMessage) == 0x30, "OpenDoor's open message is 0x30 bytes");

    struct GameFunctions
    {
        HasOtherSoldierInPoint_t hasOtherSoldier = nullptr;
        GetDoorUnit_t            getDoorUnit     = nullptr;
        GetDoorCenter_t          getDoorCenter   = nullptr;
        PathCodeFromString_t     pathCode        = nullptr;
        GetQuarkSystemTable_t    quarkTable      = nullptr;
        std::uint8_t**           doorHandle      = nullptr;
    };

    void* CheckedFunction(std::uintptr_t address, const std::uint8_t* entry, const std::uint8_t* mask, std::size_t size, const char* name)
    {
        void* target = address ? ResolveGameAddress(address) : nullptr;
        if (!target)
        {
            LogError("SoldierThrow: %s address missing on this build", name);
            return nullptr;
        }
        const HookEntry state = mask ? CheckHookEntryMasked(target, entry, mask, size) : CheckHookEntry(target, entry, size);
        if (state == HookEntry::Unknown)
        {
            char desc[160] = {};
            DescribeFunctionEntry(target, desc, sizeof(desc));
            LogError("SoldierThrow: %s differs on this build (%s)", name, desc);
            return nullptr;
        }
        return target;
    }

    const GameFunctions& Game()
    {
        static const GameFunctions game = []
        {
            GameFunctions g;
            g.hasOtherSoldier = reinterpret_cast<HasOtherSoldierInPoint_t>(CheckedFunction(gAddr.Soldier2Util_HasOtherSoldierInPoint,
                kHasOtherSoldierEntry, nullptr, sizeof(kHasOtherSoldierEntry), "Soldier2Util::HasOtherSoldierInPoint"));
            g.getDoorUnit = reinterpret_cast<GetDoorUnit_t>(CheckedFunction(gAddr.DoorGameObject_GetUnit64,
                kGetDoorUnitEntry, kGetDoorUnitMask, sizeof(kGetDoorUnitEntry), "door GetUnit64"));
            g.getDoorCenter = reinterpret_cast<GetDoorCenter_t>(CheckedFunction(gAddr.TppPermanentGimmickDoorUnit_GetDoorCenter,
                kGetDoorCenterEntry, nullptr, sizeof(kGetDoorCenterEntry), "TppPermanentGimmickDoorUnit::GetDoorCenter"));
            g.pathCode   = gAddr.Fox_PathCodeFromString ? reinterpret_cast<PathCodeFromString_t>(ResolveGameAddress(gAddr.Fox_PathCodeFromString)) : nullptr;
            g.quarkTable = gAddr.GetQuarkSystemTable ? reinterpret_cast<GetQuarkSystemTable_t>(ResolveGameAddress(gAddr.GetQuarkSystemTable)) : nullptr;
            g.doorHandle = gAddr.DoorGameObject_Handle ? reinterpret_cast<std::uint8_t**>(ResolveGameAddress(gAddr.DoorGameObject_Handle)) : nullptr;
            return g;
        }();
        return game;
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
        const GameObjectInstance::Found found = GameObjectInstance::FindSoldier(soldierId);
        if (found.step != GameObjectInstance::Step::Found || !found.object)
        {
            *reason = "that soldier is not in this mission";
            return nullptr;
        }
        if ((soldierId & 0x1FFu) >= SoldierCountSeh(found.object))
        {
            *reason = "that soldier index is past this mission's soldiers";
            return nullptr;
        }
        return found.object;
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

    std::uint8_t LifeStatusSeh(std::uint8_t* part, std::uint32_t index)
    {
        __try
        {
            auto* life = *reinterpret_cast<std::uint8_t**>(part + kPartLife);
            return (*reinterpret_cast<std::uint8_t**>(life + 8))[static_cast<std::size_t>(index) * kLifeEntrySize + kLifeStatus];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0xFF;
        }
    }

    bool ReadActionSeh(std::uint8_t* controller, std::uint32_t index, std::uint16_t& slotFlags, std::uint8_t& slotType, std::uint8_t& taskType)
    {
        __try
        {
            auto* slot = *reinterpret_cast<std::uint8_t**>(controller + kControllerSlots) + static_cast<std::size_t>(index) * kSlotSize;
            auto* task = *reinterpret_cast<std::uint8_t**>(controller + kControllerTasks) + static_cast<std::size_t>(index) * kTaskSize;
            slotFlags = *reinterpret_cast<std::uint16_t*>(slot + kSlotFlags);
            slotType  = slot[kCommandType];
            taskType  = task[kTaskActionType];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint8_t TaskTypeSeh(std::uint8_t* controller, std::uint32_t index)
    {
        __try
        {
            return (*reinterpret_cast<std::uint8_t**>(controller + kControllerTasks))[static_cast<std::size_t>(index) * kTaskSize + kTaskActionType];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0xFF;
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

    std::int32_t OverrideSeh(std::uint8_t* controller, std::uint32_t index, std::uint8_t* command)
    {
        __try
        {
            void** vtbl = *reinterpret_cast<void***>(controller);
            return reinterpret_cast<OverrideCommand_t>(vtbl[kVtbl_OverrideCommand / sizeof(void*)])(controller, index, command);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return kNoToken;
        }
    }

    void CancelSeh(std::uint8_t* controller, std::uint32_t index, std::int32_t token)
    {
        __try
        {
            void** vtbl = *reinterpret_cast<void***>(controller);
            reinterpret_cast<CancelOverride_t>(vtbl[kVtbl_CancelOverride / sizeof(void*)])(controller, index, token);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    bool HasOtherSoldierSeh(HasOtherSoldierInPoint_t hasOtherSoldier, std::uint8_t* part, std::uint32_t index, const float* point, bool& found)
    {
        __try
        {
            found = hasOtherSoldier(part, index, point, kAllyRadius, true, kAllyHeight);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint64_t PathCodeSeh(PathCodeFromString_t pathCode, const char* path)
    {
        __try
        {
            return pathCode(path);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    enum class DoorLookup : std::uint8_t { Found, NoDoors, NotFound, Fault };

    DoorLookup DoorCenterSeh(const GameFunctions& game, std::uint64_t key, float center[3])
    {
        __try
        {
            std::uint8_t* handle = *game.doorHandle;
            if (!handle)
                return DoorLookup::NoDoors;
            void* doors = *reinterpret_cast<void**>(handle + kDoorHandleObject);
            if (!doors)
                return DoorLookup::NoDoors;
            std::uint8_t* unit = game.getDoorUnit(doors, key);
            if (!unit)
                return DoorLookup::NotFound;
            alignas(16) float out[4] = {};
            game.getDoorCenter(unit, out);
            center[0] = out[0];
            center[1] = out[1];
            center[2] = out[2];
            return DoorLookup::Found;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return DoorLookup::Fault;
        }
    }

    bool OpenDoorSeh(GetQuarkSystemTable_t quarkTable, const float center[3], const float towards[3])   // CombatInDoorImpl::OpenDoor's three messages
    {
        __try
        {
            std::uint8_t* table = quarkTable();
            void* manager = table ? *reinterpret_cast<void**>(table + kQuarkGameObjects) : nullptr;
            if (!manager)
                return false;
            const SendMessage_t send = reinterpret_cast<SendMessage_t>((*reinterpret_cast<void***>(manager))[kVtbl_SendMessage / sizeof(void*)]);
            alignas(16) std::uint8_t result[0x20] = {};

            FindDoorResults found{};
            found.doorId = 0xFFFFFFFFu;
            FindDoorMessage find{};
            find.id          = kMsgFindDoor;
            find.found       = 1;
            find.results     = &found;
            find.position[0] = center[0];
            find.position[1] = center[1];
            find.position[2] = center[2];
            find.position[3] = kDoorSearchRadius;
            find.flags       = 1;
            find.tail2       = 0xFFFFFFFFu;
            send(manager, result, kDoorTypeId, &find);
            if (find.found == 0)
                return false;

            DoorInfoMessage info{};
            info.id = kMsgDoorInfo;
            send(manager, result, static_cast<std::uint16_t>(found.doorId), &info);
            if (!info.info)
                return false;

            OpenDoorMessage open{};
            open.id         = kMsgOpenDoor;
            open.close      = 0;
            open.noTarget   = 0xFFFF;
            open.towards[0] = towards[0];
            open.towards[1] = towards[1];
            open.towards[2] = towards[2];
            open.speed      = kDoorOpenSpeed;
            send(manager, result, *reinterpret_cast<std::uint16_t*>(info.info + kDoorInfoTarget), &open);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void MarkDangerSeh(std::uint8_t* part, const float target[3])   // ThrowGrenadeInDoor's 5 m mark for the other soldiers
    {
        __try
        {
            std::uint8_t* link = *reinterpret_cast<std::uint8_t**>(part + kPartDangerOwner);
            void* owner = link ? *reinterpret_cast<void**>(link + 8) : nullptr;
            if (!owner)
                return;
            void** vtbl = *reinterpret_cast<void***>(owner);
            std::uint8_t* state = reinterpret_cast<DangerState_t>(vtbl[kVtbl_DangerState / sizeof(void*)])(owner);
            if (!state)
                return;
            for (int i = 0; i < kDangerSlotCount; ++i)
            {
                std::uint8_t* slot = state + kDangerSlots + static_cast<std::size_t>(i) * kDangerSlotSize;
                if (slot[0x14] != 0)
                    continue;
                slot[0x14] = 2;
                *reinterpret_cast<float*>(slot + 0x10) = 2.0f;
                *reinterpret_cast<float*>(slot + 0x0C) = kAllyRadius;
                *reinterpret_cast<float*>(slot + 0x00) = target[0];
                *reinterpret_cast<float*>(slot + 0x04) = target[1];
                *reinterpret_cast<float*>(slot + 0x08) = target[2];
                break;
            }
            *reinterpret_cast<float*>(state + kDangerTimer) = 20.0f;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void BuildThrowCommand(std::uint8_t* command, const float* target, std::int32_t engineId, float angle, bool farThrow)
    {
        std::memset(command, 0, kCommandSize);
        *reinterpret_cast<float*>(command + 0x10) = target[0];
        *reinterpret_cast<float*>(command + 0x14) = target[1];
        *reinterpret_cast<float*>(command + 0x18) = target[2];
        command[0x1C] = 1;                                         // the target is a position
        *reinterpret_cast<float*>(command + 0x20) = angle;
        *reinterpret_cast<float*>(command + 0x24) = -1.0f;
        *reinterpret_cast<std::int32_t*>(command + 0x28) = engineId;
        command[0x2C] = farThrow ? 1 : 0;
        *reinterpret_cast<std::uint32_t*>(command + 0xB0) = kThrowFlags;
        command[kCommandType] = kThrowAction;
        *reinterpret_cast<std::uint16_t*>(command + 0xB6) = 0xFFFF;
    }

    void Hold(std::uint32_t index, std::int32_t token)
    {
        g_Token[index].store(token, std::memory_order_relaxed);
        g_SinceMs[index].store(GetTickCount64(), std::memory_order_relaxed);
        if (g_State[index].exchange(static_cast<std::uint8_t>(Throw::Waiting), std::memory_order_acq_rel) == static_cast<std::uint8_t>(Throw::None))
            g_Held.fetch_add(1, std::memory_order_relaxed);
    }

    bool Claim(std::uint32_t index)   // whoever claims the throw gives the soldier his own orders back
    {
        std::uint8_t state = g_State[index].load(std::memory_order_acquire);
        while (state != static_cast<std::uint8_t>(Throw::None))
        {
            if (g_State[index].compare_exchange_weak(state, static_cast<std::uint8_t>(Throw::None), std::memory_order_acq_rel))
            {
                g_Held.fetch_sub(1, std::memory_order_relaxed);
                return true;
            }
        }
        return false;
    }

    bool StartThrow(std::uint32_t soldierId, const float target[3], std::int32_t equipId, const float* doorCenter, const float* doorTowards,
                    const char** reason)
    {
        thread_local char why[192];
        const std::uint32_t index = soldierId & 0x1FFu;
        if ((soldierId >> 9) != kSoldierType || index >= kWholeType)
        {
            *reason = "the target is not one TppSoldier2";
            return false;
        }
        const GameFunctions& game = Game();
        if (!game.hasOtherSoldier)
        {
            *reason = "the ally check is missing on this build";
            return false;
        }
        std::uint8_t* soldiers = SoldiersOf(soldierId, reason);
        if (!soldiers)
            return false;
        std::uint8_t* part = soldiers + kSoldier2Part;
        std::uint8_t* controller = ControllerSeh(soldiers);
        if (!controller)
        {
            *reason = "his action controller is not there";
            return false;
        }
        const std::uint8_t life = LifeStatusSeh(part, index);
        if (life != 0)
        {
            *reason = life == 2 ? "he is dead" : (life == 1 ? "he is unconscious" : "his life state could not be read");
            return false;
        }
        std::uint16_t slotFlags = 0;
        std::uint8_t slotType = 0;
        std::uint8_t taskType = 0;
        if (!ReadActionSeh(controller, index, slotFlags, slotType, taskType))
        {
            *reason = "his actions could not be read";
            return false;
        }
        if ((slotFlags & kSlotOverridden) != 0)
        {
            std::snprintf(why, sizeof(why), "his actions are already taken over (command type 0x%02X)", slotType);
            *reason = why;
            return false;
        }
        if (slotType == kThrowAction || taskType == kThrowAction)
        {
            *reason = "he is already throwing";
            return false;
        }
        if (slotType == kDamageAction || taskType == kDamageAction)
        {
            *reason = "he is reacting to a hit";
            return false;
        }
        if (slotType == kMeleeAction || taskType == kMeleeAction)
        {
            *reason = "he is in a hold-up or CQC";
            return false;
        }
        if (slotType == kRideHeliAction || taskType == kRideHeliAction)
        {
            *reason = "he is on a helicopter";
            return false;
        }

        const std::int32_t realId = equipId >= 0 ? equipId : SoldierGrenadeIds_Pick(index, part);
        const std::uint32_t equipType = EquipParam_GetEquipType(static_cast<std::uint32_t>(realId));
        if (equipType != kThrowingType)
        {
            std::snprintf(why, sizeof(why), "equipId 0x%X is not a throwing item (EQP_TYPE %u)", static_cast<unsigned>(realId), equipType);
            *reason = why;
            return false;
        }
        const std::int32_t engineId = EquipIdCompression::IsWideEquipId(realId) ? equip::ToEngine(realId) : realId;
        if (engineId <= 0 || engineId >= EquipIdCompression::kNoEquipId)
        {
            std::snprintf(why, sizeof(why), "equipId 0x%X has no engine id free right now", static_cast<unsigned>(realId));
            *reason = why;
            return false;
        }

        alignas(16) float point[4] = { target[0], target[1], target[2], 0.0f };
        bool ally = false;
        if (!HasOtherSoldierSeh(game.hasOtherSoldier, part, index, point, ally))
        {
            *reason = "the ally check failed";
            return false;
        }
        if (ally)
        {
            *reason = "another soldier stands within 5 m of the target";
            return false;
        }

        float angle = kDoorAngle;
        bool farThrow = false;
        if (!doorCenter)
        {
            angle = kLobAngle;
            float from[3] = {};
            if (GameObjectInstance::SoldierPosition(soldierId, from))
            {
                const float dx = target[0] - from[0];
                const float dy = target[1] - from[1];
                const float dz = target[2] - from[2];
                farThrow = dx * dx + dy * dy + dz * dz >= kFarThrowSq;
                if (dy > kUphill)
                    angle = kLobAngleUphill;
            }
        }

        alignas(16) std::uint8_t command[kCommandSize];
        BuildThrowCommand(command, point, engineId, angle, farThrow);
        if (doorCenter && game.quarkTable && !OpenDoorSeh(game.quarkTable, doorCenter, doorTowards))
            LogWarn("ThrowGrenadeInDoor: soldier %u could not open the door, he throws anyway", index);
        const std::int32_t token = OverrideSeh(controller, index, command);
        if (token == kNoToken)
        {
            *reason = "the game refused the throw";
            return false;
        }
        if (doorCenter)
            MarkDangerSeh(part, point);
        Hold(index, token);
        return true;
    }
}

bool SoldierThrow_ToPosition(std::uint32_t soldierId, const float position[3], std::int32_t equipId, const char** reason)
{
    if (MissionCodeGuard::ShouldBypassHooks())
    {
        *reason = "not in online missions";
        return false;
    }
    if (!SoldierGrenadeIds_Active())
    {
        *reason = "the SoldierGrenadeIds module is off";
        return false;
    }
    return StartThrow(soldierId, position, equipId, nullptr, nullptr, reason);
}

bool SoldierThrow_InDoor(std::uint32_t soldierId, const char* gimmickId, const char* dataSetName, std::int32_t equipId, const char** reason)
{
    thread_local char why[256];
    if (MissionCodeGuard::ShouldBypassHooks())
    {
        *reason = "not in online missions";
        return false;
    }
    if (!SoldierGrenadeIds_Active())
    {
        *reason = "the SoldierGrenadeIds module is off";
        return false;
    }
    if (!gimmickId || !*gimmickId || !dataSetName || !*dataSetName)
    {
        *reason = "needs gimmickId and dataSetName (the door's locator name and its .fox2 path)";
        return false;
    }
    const GameFunctions& game = Game();
    if (!game.getDoorUnit || !game.getDoorCenter || !game.pathCode || !game.doorHandle)
    {
        *reason = "the door lookup is missing on this build";
        return false;
    }
    const std::uint64_t path = PathCodeSeh(game.pathCode, dataSetName);
    const std::uint64_t extension = path >> kExtensionShift;
    if (path == 0 || extension == kLuaExtension || extension == kOtherPathExtension)
    {
        *reason = "dataSetName is not a .fox2 path";
        return false;
    }
    const std::uint64_t key = (path << 32) | FoxHashes::StrCode32(gimmickId);   // the key Gimmick.CloseDoor builds

    float center[3] = {};
    switch (DoorCenterSeh(game, key, center))
    {
    case DoorLookup::Found:
        break;
    case DoorLookup::NoDoors:
        *reason = "this mission has no doors";
        return false;
    case DoorLookup::NotFound:
        std::snprintf(why, sizeof(why), "door \"%s\" of \"%s\" is not in this mission", gimmickId, dataSetName);
        *reason = why;
        return false;
    default:
        *reason = "the door lookup failed";
        return false;
    }

    float from[3] = {};
    if (!GameObjectInstance::SoldierPosition(soldierId, from))
    {
        *reason = "his position could not be read";
        return false;
    }
    const float dx = center[0] - from[0];
    const float dz = center[2] - from[2];
    const float length = std::sqrt(dx * dx + dz * dz);
    if (length < kMinDoorDistance)
    {
        *reason = "he stands in the doorway, put him in front of the door";
        return false;
    }
    const float nx = dx / length;
    const float nz = dz / length;
    const float target[3]  = { center[0] + nx * kRoomDepth, center[1], center[2] + nz * kRoomDepth };   // 7.5 m into the room, away from him
    const float towards[3] = { center[0] - nx, center[1], center[2] - nz };
    return StartThrow(soldierId, target, equipId, center, towards, reason);
}

void SoldierThrow_Pump()
{
    if (g_Held.load(std::memory_order_relaxed) <= 0)
        return;
    const std::uint64_t now = GetTickCount64();
    if (now - g_LastPumpMs < kPumpEveryMs)
        return;
    g_LastPumpMs = now;
    for (std::uint32_t index = 0; index < kMaxSoldiers; ++index)
    {
        const auto state = static_cast<Throw>(g_State[index].load(std::memory_order_acquire));
        if (state == Throw::None)
            continue;
        const char* reason = nullptr;
        std::uint8_t* soldiers = SoldiersOf((kSoldierType << 9) | index, &reason);
        std::uint8_t* controller = soldiers ? ControllerSeh(soldiers) : nullptr;
        const std::int32_t token = g_Token[index].load(std::memory_order_relaxed);
        if (!controller || !OverrideHeldSeh(controller, index, token))
        {
            Claim(index);   // a hit or the game already gave him his orders back
            continue;
        }
        const std::uint8_t taskType = TaskTypeSeh(controller, index);
        const std::uint64_t since = g_SinceMs[index].load(std::memory_order_relaxed);
        if (state == Throw::Waiting)
        {
            if (taskType == kThrowAction)
            {
                std::uint8_t expected = static_cast<std::uint8_t>(Throw::Waiting);
                if (g_State[index].compare_exchange_strong(expected, static_cast<std::uint8_t>(Throw::Throwing), std::memory_order_acq_rel))
                    g_SinceMs[index].store(now, std::memory_order_relaxed);
            }
            else if (now - since > kStartWaitMs && Claim(index))
            {
                CancelSeh(controller, index, token);
                LogWarn("ThrowGrenade: soldier %u never started the throw, his own orders are back", index);
            }
            continue;
        }
        if (taskType != kThrowAction && Claim(index))
            CancelSeh(controller, index, token);
        else if (now - since > kThrowWaitMs && Claim(index))
        {
            CancelSeh(controller, index, token);
            LogWarn("ThrowGrenade: soldier %u was still throwing after %llu ms, his own orders are back", index,
                    static_cast<unsigned long long>(kThrowWaitMs));
        }
    }
}

void SoldierThrow_OnSetCommand(std::uint8_t* actionController, std::uint32_t soldierIndex, const std::uint8_t* command)
{
    if (g_Held.load(std::memory_order_relaxed) <= 0 || soldierIndex >= kMaxSoldiers || command[kCommandType] != kDamageAction)
        return;
    if (g_State[soldierIndex].load(std::memory_order_acquire) == static_cast<std::uint8_t>(Throw::None))
        return;
    const std::int32_t token = g_Token[soldierIndex].load(std::memory_order_relaxed);
    if (Claim(soldierIndex))
        CancelSeh(actionController, soldierIndex, token);   // a hit, knock-out or death plays now instead of waiting behind the throw
}

void SoldierThrow_ClearForNewMission()
{
    for (std::uint32_t i = 0; i < kMaxSoldiers; ++i)
    {
        g_State[i].store(static_cast<std::uint8_t>(Throw::None), std::memory_order_relaxed);
        g_Token[i].store(kNoToken, std::memory_order_relaxed);
        g_SinceMs[i].store(0, std::memory_order_relaxed);
    }
    g_Held.store(0, std::memory_order_relaxed);
}
