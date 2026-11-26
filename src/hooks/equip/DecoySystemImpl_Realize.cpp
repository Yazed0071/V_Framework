#include <pch.h>
#include "DecoySystemImpl_Realize.h"

#include <Windows.h>
#include <array>
#include <atomic>
#include <bitset>
#include <cstdint>
#include <mutex>

#include "AddressSet.h"
#include "DecoyResourceManagerImpl_InitializePlayerDecoyModel.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "TppEquip_ReloadEquipIdTable.h"
#include "UixUtilityControl.h"
#include "log.h"

namespace
{
    using Realize_t          = void(__fastcall*)(std::uint8_t* system, std::uint16_t record, std::uint32_t slot);
    using SetUp_t            = void(__fastcall*)(void* controller, std::uint32_t slot, const void* desc, bool noMotion);
    using TearDown_t         = void(__fastcall*)(void* controller, std::uint32_t slot, bool freeUnusedFile);
    using SetSlotFlag_t      = void(__fastcall*)(void* controller, std::uint32_t slot, bool on);
    using SetSpeedRate_t     = void(__fastcall*)(void* controller, std::uint32_t slot, float rate);
    using DeleteEffects_t    = void(__fastcall*)(void* effects, std::uint32_t slot);
    using GetFileForPathId_t = void*(__fastcall*)(void* block, std::uint64_t pathId);

    constexpr std::size_t kPlayerSlots      = 0x98;    // DecoySystemImpl
    constexpr std::size_t kEnemySlots       = 0xA0;    // realize slots below this one are the enemy decoys'
    constexpr std::size_t kPlayerController = 0x148;

    constexpr std::size_t kSlotCount      = 0x50;      // SimplePartsControllerImpl
    constexpr std::size_t kPartsFileCount = 0x08;
    constexpr std::size_t kSlotModels     = 0x58;

    constexpr std::size_t kSetUp              = 0x10;
    constexpr std::size_t kTearDown           = 0x20;
    constexpr std::size_t kSetVisibility      = 0xB0;
    constexpr std::size_t kSetThermoGraphy    = 0x1A0;
    constexpr std::size_t kSetMotionSpeedRate = 0x1D8;
    constexpr std::size_t kDeleteImmediately  = 0x38;  // SimplePartsEffectControllerImpl

    constexpr std::size_t   kPlacedEquipIds  = 0x38;   // u16 equipId + u16 owner per placed item
    constexpr std::uint32_t kRecordLimit     = 0x200;  // decoy object ids keep 9 bits
    constexpr std::uint32_t kSlotLimit       = 64;
    constexpr float         kFigureSpeedRate = 1.0f;

    constexpr std::uint8_t kEntry[] = {
        0x4C, 0x8B, 0xDC, 0x55, 0x56, 0x57, 0x41, 0x55, 0x48, 0x81, 0xEC, 0xE8, 0x00, 0x00, 0x00,
    };

    struct MotionDesc
    {
        void* file;
        std::uint64_t options;
    };

    struct alignas(16) PartsDesc
    {
        void* file;
        void* block;
        const MotionDesc* motion;
        std::uint64_t reserved;
        std::uint32_t flags;
    };

    struct RecordItem
    {
        const std::uint8_t* placed = nullptr;
        const std::uint16_t* correlation = nullptr;
        std::uint32_t placedIndex = 0;
        std::uint16_t equipId = 0;
    };

    struct SlotModels   // nullptr = the game's own
    {
        void* canister = nullptr;
        void* figure = nullptr;
    };

    struct PlayerSlot
    {
        std::uint8_t* controller = nullptr;
        std::uint32_t canister = 0;
        std::uint32_t figure = 0;
        bool roomForFigures = false;
    };

    Realize_t g_Orig = nullptr;
    void* g_Target = nullptr;
    void* g_PartsVtable = nullptr;
    void* g_DeleteImmediately = nullptr;
    GetFileForPathId_t g_GetFileForPathId = nullptr;
    std::atomic<bool> g_On{ false };

    std::mutex g_Lock;
    std::array<RecordItem, kRecordLimit> g_Records{};
    std::array<SlotModels, kSlotLimit> g_Slots{};
    std::uint32_t g_SlotsGeneration = 0;
    std::bitset<0x10000> g_ToldMissing;
    bool g_ToldNoRoom = false;
    bool g_ToldEmptySlot = false;

    bool PlacedItemStillOwnsSEH(const RecordItem& item, std::uint16_t record)
    {
        __try
        {
            if (!item.placed || !item.correlation || *item.correlation != record)
                return false;
            const auto* equipIds = *reinterpret_cast<const std::uint8_t* const*>(item.placed + kPlacedEquipIds);
            return equipIds && *reinterpret_cast<const std::uint16_t*>(equipIds + item.placedIndex * 4) == item.equipId;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ReadPlayerSlotSEH(const std::uint8_t* system, std::uint32_t slot, PlayerSlot* out)
    {
        __try
        {
            const std::uint32_t enemySlots = *reinterpret_cast<const std::uint32_t*>(system + kEnemySlots);
            const std::uint32_t playerSlots = *reinterpret_cast<const std::uint32_t*>(system + kPlayerSlots);
            if (slot < enemySlots || slot - enemySlots >= playerSlots || playerSlots > kSlotLimit)
                return false;
            auto* controller = *reinterpret_cast<std::uint8_t* const*>(system + kPlayerController);
            if (!controller || *reinterpret_cast<void* const*>(controller) != g_PartsVtable
                || *reinterpret_cast<const std::uint32_t*>(controller + kSlotCount) < playerSlots * 2)
                return false;
            out->controller = controller;
            out->canister = slot - enemySlots;
            out->figure = out->canister + playerSlots;
            out->roomForFigures = *reinterpret_cast<const std::uint32_t*>(controller + kPartsFileCount) > playerSlots;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool SlotHasModelSEH(const std::uint8_t* controller, std::uint32_t slot)
    {
        __try
        {
            return (*reinterpret_cast<void* const* const*>(controller + kSlotModels))[slot] != nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool IsEffectControllerSEH(void* effects)
    {
        __try
        {
            return effects && (*reinterpret_cast<void***>(effects))[kDeleteImmediately / sizeof(void*)] == g_DeleteImmediately;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    SlotModels CustomModelsOf(std::uint16_t record, void** block)
    {
        RecordItem item;
        {
            std::lock_guard<std::mutex> lock(g_Lock);
            if (record < kRecordLimit)
                item = g_Records[record];
        }
        V_DecoyModels models{};
        if (!PlacedItemStillOwnsSEH(item, record) || !TppEquip_GetDecoyModels(item.equipId, &models))
            return {};
        *block = UixUtilityControl::GetResidentPackBlock(models.pack);
        if (!*block)
            return {};
        SlotModels custom;
        if (models.canister)
            custom.canister = g_GetFileForPathId(*block, models.canister);
        if (models.figure)
            custom.figure = g_GetFileForPathId(*block, models.figure);
        if ((models.canister && !custom.canister) || (models.figure && !custom.figure))
        {
            std::lock_guard<std::mutex> lock(g_Lock);
            if (!g_ToldMissing.test(item.equipId))
            {
                g_ToldMissing.set(item.equipId);
                LogWarn("DecoyModel: equipId %u: its canister or decoyParts is not in its own pack, that part keeps the game's model",
                        static_cast<unsigned>(item.equipId));
            }
        }
        return custom;
    }

    bool SetUpModel(std::uint8_t* controller, std::uint32_t slot, void* file, void* block, const MotionDesc* motion)
    {
        void** vtable = *reinterpret_cast<void***>(controller);
        const PartsDesc desc{ file, block, motion, 0, 0 };
        reinterpret_cast<SetUp_t>(vtable[kSetUp / sizeof(void*)])(controller, slot, &desc, false);
        return SlotHasModelSEH(controller, slot);
    }

    void* PutModel(std::uint8_t* controller, std::uint32_t slot, void* custom, void* customBlock, void* game, void* gameBlock,
                   const MotionDesc* motion, bool* filled)   // returns the custom file it holds, nullptr = the game's
    {
        void** vtable = *reinterpret_cast<void***>(controller);
        if (SlotHasModelSEH(controller, slot))
            reinterpret_cast<TearDown_t>(vtable[kTearDown / sizeof(void*)])(controller, slot, true);
        if (custom && SetUpModel(controller, slot, custom, customBlock, motion))
        {
            *filled = true;
            return custom;
        }
        *filled = SetUpModel(controller, slot, game, gameBlock, motion);
        return nullptr;
    }

    bool DressPlayerSlot(std::uint8_t* system, std::uint16_t record, std::uint32_t slot)
    {
        DecoyPlayerModels game;
        PlayerSlot player;
        if (!DecoyResourceManagerImpl_PlayerModels(&game) || !ReadPlayerSlotSEH(system, slot, &player)
            || player.controller != game.controller)
            return true;

        void* block = nullptr;
        SlotModels wanted = CustomModelsOf(record, &block);
        SlotModels held;
        {
            std::lock_guard<std::mutex> lock(g_Lock);
            if (wanted.figure && !player.roomForFigures)
            {
                wanted.figure = nullptr;
                if (!g_ToldNoRoom)
                    LogWarn("DecoyModel: the decoy parts table was not raised this mission, custom decoy figures are off");
                g_ToldNoRoom = true;
            }
            if (g_SlotsGeneration != game.generation)
            {
                g_Slots.fill({});
                g_SlotsGeneration = game.generation;
            }
            held = g_Slots[player.canister];
        }

        void** vtable = *reinterpret_cast<void***>(player.controller);
        const auto setVisibility = reinterpret_cast<SetSlotFlag_t>(vtable[kSetVisibility / sizeof(void*)]);
        const auto setThermoGraphy = reinterpret_cast<SetSlotFlag_t>(vtable[kSetThermoGraphy / sizeof(void*)]);
        bool canisterFilled = SlotHasModelSEH(player.controller, player.canister);
        bool figureFilled = SlotHasModelSEH(player.controller, player.figure);

        if (!canisterFilled || held.canister != wanted.canister)
        {
            held.canister = PutModel(player.controller, player.canister, wanted.canister, block, game.canister, game.block,
                                     nullptr, &canisterFilled);
            if (canisterFilled)
            {
                setVisibility(player.controller, player.canister, false);
                setThermoGraphy(player.controller, player.canister, true);
            }
        }

        if (canisterFilled && (!figureFilled || held.figure != wanted.figure))
        {
            const MotionDesc motion{ game.motion, 0 };
            if (IsEffectControllerSEH(game.effects))
                reinterpret_cast<DeleteEffects_t>(g_DeleteImmediately)(game.effects, player.figure);
            held.figure = PutModel(player.controller, player.figure, wanted.figure, block, game.figure, game.block,
                                   &motion, &figureFilled);
            if (figureFilled)
            {
                setVisibility(player.controller, player.figure, false);
                setThermoGraphy(player.controller, player.figure, true);
                reinterpret_cast<SetSpeedRate_t>(vtable[kSetMotionSpeedRate / sizeof(void*)])(
                    player.controller, player.figure, kFigureSpeedRate);
            }
        }

        std::lock_guard<std::mutex> lock(g_Lock);
        if (g_SlotsGeneration == game.generation)
            g_Slots[player.canister] = held;
        return canisterFilled && figureFilled;
    }

    void __fastcall hkRealize(std::uint8_t* system, std::uint16_t record, std::uint32_t slot)
    {
        if (g_On.load(std::memory_order_acquire) && system && !MissionCodeGuard::ShouldBypassHooks()
            && !DressPlayerSlot(system, record, slot))
        {
            if (!g_ToldEmptySlot)
                LogError("DecoyModel: a player decoy slot could not get a model, that decoy stays hidden");
            g_ToldEmptySlot = true;
            return;
        }
        g_Orig(system, record, slot);
    }
}

bool Install_DecoySystemImpl_Realize_Hook()
{
    if (g_Target)
        return true;
    void* target = ResolveGameAddress(gAddr.DecoySystemImpl_Realize);
    g_PartsVtable = ResolveGameAddress(gAddr.SimplePartsControllerImpl_Vtable);
    g_DeleteImmediately = ResolveGameAddress(gAddr.SimplePartsEffectControllerImpl_DeleteImmediately);
    g_GetFileForPathId = reinterpret_cast<GetFileForPathId_t>(ResolveGameAddress(gAddr.Fox_Block_GetFileForPathId));
    if (!target || !g_PartsVtable || !g_DeleteImmediately || !g_GetFileForPathId)
    {
        LogError("DecoyModel: decoy realize addresses missing for this build, decoys keep the game's models");
        return false;
    }
    const HookEntry entry = CheckHookEntry(target, kEntry, sizeof(kEntry));
    if (entry == HookEntry::Unknown)
    {
        char desc[160] = {};
        DescribeFunctionEntry(target, desc, sizeof(desc));
        LogError("DecoyModel: DecoySystemImpl::Realize differs on this build (%s), decoys keep the game's models", desc);
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("DecoyModel: DecoySystemImpl::Realize", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkRealize), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("DecoyModel: DecoySystemImpl::Realize hook failed, decoys keep the game's models");
        return false;
    }
    g_Target = target;
    g_On.store(true, std::memory_order_release);
    return true;
}

void Uninstall_DecoySystemImpl_Realize_Hook()
{
    g_On.store(false, std::memory_order_release);
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig = nullptr;
}

void DecoySystemImpl_NoteRecordItem(std::uint16_t record, const std::uint8_t* placed, std::uint32_t placedIndex,
                                    std::uint16_t equipId, const std::uint16_t* correlation)
{
    if (record >= kRecordLimit)
        return;
    std::lock_guard<std::mutex> lock(g_Lock);
    g_Records[record] = { placed, correlation, placedIndex, equipId };
}
