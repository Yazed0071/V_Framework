#include "pch.h"
#include "WideMagazine.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>

#include "AddressSet.h"
#include "DevelopArrayGrow.h"
#include "EquipPartParams.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using AmmoBlockFn = void* (__fastcall*)(void*, void*, std::uint32_t, std::uint32_t*);
    using WalkerAmmoFn = std::uint16_t (__fastcall*)(void*, std::uint32_t);
    using QuietSlotFn = void (__fastcall*)(void*, std::uint32_t);
    using SoldierSlotFn = void (__fastcall*)(void*, std::uint32_t, int);
    using DevelopCountFn = std::uint16_t (__fastcall*)(void*, std::uint16_t, bool);
    using DevelopTypeFn = std::uint8_t (__fastcall*)(void*, std::uint16_t);
    using InventoryAmmoFn = void (__fastcall*)(void*, std::uint32_t, std::uint32_t,
        std::uint16_t, std::uint32_t, std::uint32_t*, std::uint32_t*);
    using StockIndexFn = std::uint32_t (__fastcall*)(void*, std::uint32_t, std::uint32_t);
    using StockValueFn = std::uint32_t (__fastcall*)(void*, std::uint32_t);
    using QuarkTableFn = void* (__fastcall*)();

    AmmoBlockFn g_AmmoBlock = nullptr;
    WalkerAmmoFn g_WalkerAmmo = nullptr;
    QuietSlotFn g_BuddyQuietSlot = nullptr;
    SoldierSlotFn g_HostageSlot = nullptr;
    SoldierSlotFn g_SoldierSlot = nullptr;
    QuietSlotFn g_BossQuietSlot = nullptr;
    DevelopCountFn g_DevelopMagazine = nullptr;
    DevelopCountFn g_DevelopMax = nullptr;
    DevelopTypeFn g_DevelopType = nullptr;
    InventoryAmmoFn g_InventoryAmmo = nullptr;
    QuarkTableFn g_QuarkTable = nullptr;

    std::atomic<bool> g_Active{ false };
    std::atomic<bool> g_Planned{ false };
    std::mutex g_InstallMutex;
    bool g_Attempted = false;
    bool g_PlanAttempted = false;

    bool BytesMatch(const void* address, const std::uint8_t* expected, std::size_t size,
                    std::size_t skipAt, std::size_t skipLen)
    {
        if (!address || !expected || !size || skipAt + skipLen > size)
            return false;
        __try
        {
            const auto* bytes = static_cast<const std::uint8_t*>(address);
            if (!skipLen)
                return std::memcmp(bytes, expected, size) == 0;
            const std::size_t tail = skipAt + skipLen;
            return std::memcmp(bytes, expected, skipAt) == 0
                && std::memcmp(bytes + tail, expected + tail, size - tail) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ReadDevelopEquip(void* self, std::uint16_t index, std::uint16_t& equipId)
    {
        if (!self || !equip::IsValidFlowIndex(index))
            return false;
        __try
        {
            const auto* row = static_cast<const std::uint8_t*>(self)
                + static_cast<std::size_t>(index) * 0x68;
            equipId = static_cast<std::uint16_t>(
                *reinterpret_cast<const std::uint16_t*>(row + 0xC) >> 4);
            return equipId != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ReadBulletStats(std::uint16_t bulletId, std::uint16_t& speed, std::uint8_t& type)
    {
        if (!bulletId || !g_QuarkTable)
            return false;
        const auto* quark = static_cast<const std::uint8_t*>(g_QuarkTable());
        if (!quark)
            return false;
        __try
        {
            const auto* app = *reinterpret_cast<const std::uint8_t* const*>(quark + 0x98);
            if (!app)
                return false;
            const auto* equipSystem = *reinterpret_cast<const std::uint8_t* const*>(app + 0x1E8);
            if (!equipSystem)
                return false;
            const auto* tables = *reinterpret_cast<const std::uint8_t* const*>(equipSystem + 0x10);
            if (!tables)
                return false;
            const auto* bullets = *reinterpret_cast<const std::uint8_t* const*>(tables + 0x50);
            if (!bullets)
                return false;
            const auto* row = bullets + static_cast<std::size_t>(bulletId - 1) * 14;
            speed = *reinterpret_cast<const std::uint16_t*>(row + 2);
            type = row[10];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void WriteAmmoBlock(std::uint32_t* output, const MagazineParameters& primary,
                        const MagazineParameters& underbarrel)
    {
        if (!output)
            return;
        __try
        {
            if (primary.id > 0)
            {
                output[0] = primary.bulletId;
                output[1] = primary.capacity;
                output[2] = primary.carryMax;
            }
            if (underbarrel.id > 0)
            {
                output[3] = underbarrel.bulletId;
                output[4] = underbarrel.capacity;
                output[5] = underbarrel.carryMax;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    bool ReadQuietSlot(void* self, std::uint32_t index, bool boss,
                       std::uint16_t& equipId, std::uint8_t*& state)
    {
        if (!self)
            return false;
        __try
        {
            const auto* bytes = static_cast<const std::uint8_t*>(self);
            auto* states = *reinterpret_cast<std::uint8_t* const*>(bytes + 0x10);
            const auto* equipIds = *reinterpret_cast<const std::uint16_t* const*>(
                bytes + (boss ? 0x18 : 0x20));
            if (!states || !equipIds)
                return false;
            state = states + static_cast<std::size_t>(index) * 12;
            const std::uint8_t variant = state[8];
            if (variant > 1)
                return false;
            equipId = equipIds[static_cast<std::size_t>(index) * 2 + variant];
            return equipId != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ReadSoldierSlot(void* self, std::uint32_t index, int slotType, bool hostage,
                         std::uint16_t& equipId, std::uint8_t*& state)
    {
        if (!self || slotType < 0 || slotType > 2)
            return false;
        __try
        {
            const auto* bytes = static_cast<const std::uint8_t*>(self);
            auto* states = *reinterpret_cast<std::uint8_t* const*>(bytes + 8);
            const auto* equipIds = *reinterpret_cast<const std::uint16_t* const*>(bytes + 0x10);
            if (!states || !equipIds)
                return false;
            state = states + static_cast<std::size_t>(index) * 8;
            const std::size_t equipIndex = static_cast<std::size_t>(index)
                * (hostage ? 1 : 3) + static_cast<std::size_t>(slotType);
            equipId = equipIds[equipIndex];
            return equipId != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void WriteQuietSlot(std::uint8_t* state, std::uint16_t capacity,
                        bool haveSpeed, std::uint16_t speed)
    {
        __try
        {
            *reinterpret_cast<std::uint16_t*>(state + 4) = capacity;
            *reinterpret_cast<std::uint16_t*>(state + 6) = capacity;
            if (haveSpeed)
                *reinterpret_cast<float*>(state) = static_cast<float>(speed);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void WriteSoldierSlot(std::uint8_t* state, int slotType, std::uint16_t capacity)
    {
        __try
        {
            *reinterpret_cast<std::uint16_t*>(state) = capacity;
            *reinterpret_cast<std::uint16_t*>(state + 2) = capacity;
            state[6] = static_cast<std::uint8_t>(slotType);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void CorrectQuietSlot(void* self, std::uint32_t index, bool boss)
    {
        if (!g_Active.load(std::memory_order_acquire))
            return;
        std::uint16_t equipId = 0;
        std::uint8_t* state = nullptr;
        if (!ReadQuietSlot(self, index, boss, equipId, state))
            return;
        MagazineParameters primary{}, underbarrel{};
        if (!EquipParam_GetWeaponMagazines(equipId, primary, underbarrel) || primary.id <= 0)
            return;
        std::uint16_t speed = 0;
        std::uint8_t type = 0;
        const bool haveSpeed = ReadBulletStats(primary.bulletId, speed, type);
        WriteQuietSlot(state, primary.capacity, haveSpeed, speed);
    }

    void CorrectSoldierSlot(void* self, std::uint32_t index, int slotType, bool hostage)
    {
        if (!g_Active.load(std::memory_order_acquire))
            return;
        std::uint16_t equipId = 0;
        std::uint8_t* state = nullptr;
        if (!ReadSoldierSlot(self, index, slotType, hostage, equipId, state))
            return;
        MagazineParameters primary{}, underbarrel{};
        if (EquipParam_GetWeaponMagazines(equipId, primary, underbarrel) && primary.id > 0)
            WriteSoldierSlot(state, slotType, primary.capacity);
    }

    void* __fastcall HookAmmoBlock(void* self, void* error, std::uint32_t equipId,
                                   std::uint32_t* output)
    {
        void* result = g_AmmoBlock(self, error, equipId, output);
        if (g_Active.load(std::memory_order_acquire))
        {
            MagazineParameters primary{}, underbarrel{};
            if (EquipParam_GetWeaponMagazines(static_cast<int>(equipId), primary, underbarrel))
                WriteAmmoBlock(output, primary, underbarrel);
        }
        return result;
    }

    std::uint16_t __fastcall HookWalkerAmmo(void* self, std::uint32_t equipId)
    {
        const std::uint16_t result = g_WalkerAmmo(self, equipId);
        if (g_Active.load(std::memory_order_acquire))
        {
            MagazineParameters primary{}, underbarrel{};
            if (EquipParam_GetWeaponMagazines(static_cast<int>(equipId), primary, underbarrel)
                && primary.id > 0)
                return static_cast<std::uint16_t>(static_cast<unsigned>(primary.capacity) * 2);
        }
        return result;
    }

    void __fastcall HookBuddyQuietSlot(void* self, std::uint32_t index)
    {
        g_BuddyQuietSlot(self, index);
        CorrectQuietSlot(self, index, false);
    }

    void __fastcall HookHostageSlot(void* self, std::uint32_t index, int slotType)
    {
        g_HostageSlot(self, index, slotType);
        CorrectSoldierSlot(self, index, slotType, true);
    }

    void __fastcall HookSoldierSlot(void* self, std::uint32_t index, int slotType)
    {
        g_SoldierSlot(self, index, slotType);
        CorrectSoldierSlot(self, index, slotType, false);
    }

    void __fastcall HookBossQuietSlot(void* self, std::uint32_t index)
    {
        g_BossQuietSlot(self, index);
        CorrectQuietSlot(self, index, true);
    }

    bool GetDevelopMagazine(void* self, std::uint16_t index, bool secondary,
                            MagazineParameters& result)
    {
        if (!g_Active.load(std::memory_order_acquire))
            return false;
        std::uint16_t equipId = 0;
        if (!ReadDevelopEquip(self, index, equipId))
            return false;
        MagazineParameters primary{}, underbarrel{};
        if (!EquipParam_GetWeaponMagazines(equipId, primary, underbarrel))
            return false;
        if (primary.id > 0)
        {
            result = primary;
            return true;
        }
        if (secondary && underbarrel.id > 0)
        {
            result = underbarrel;
            return true;
        }
        return false;
    }

    std::uint16_t __fastcall HookDevelopMagazine(void* self, std::uint16_t index, bool secondary)
    {
        const std::uint16_t result = g_DevelopMagazine(self, index, secondary);
        MagazineParameters magazine{};
        return GetDevelopMagazine(self, index, secondary, magazine) ? magazine.capacity : result;
    }

    std::uint16_t __fastcall HookDevelopMax(void* self, std::uint16_t index, bool secondary)
    {
        const std::uint16_t result = g_DevelopMax(self, index, secondary);
        MagazineParameters magazine{};
        return GetDevelopMagazine(self, index, secondary, magazine) ? magazine.carryMax : result;
    }

    std::uint8_t __fastcall HookDevelopType(void* self, std::uint16_t index)
    {
        const std::uint8_t result = g_DevelopType(self, index);
        MagazineParameters magazine{};
        std::uint16_t speed = 0;
        std::uint8_t type = 0;
        return GetDevelopMagazine(self, index, false, magazine)
            && ReadBulletStats(magazine.bulletId, speed, type) ? type : result;
    }

    bool ReadInventoryAccessors(void* self, StockIndexFn& stockIndex,
                                StockValueFn& stockBullet, StockValueFn& stockAmount)
    {
        if (!self)
            return false;
        __try
        {
            const auto* table = *reinterpret_cast<void* const* const*>(self);
            if (!table)
                return false;
            stockIndex = reinterpret_cast<StockIndexFn>(table[0x68 / sizeof(void*)]);
            stockBullet = reinterpret_cast<StockValueFn>(table[0x80 / sizeof(void*)]);
            stockAmount = reinterpret_cast<StockValueFn>(table[0x88 / sizeof(void*)]);
            return stockIndex && stockBullet && stockAmount;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void WriteInventoryAmmo(std::uint32_t* loaded, std::uint32_t* reserve,
                            std::uint32_t loadedCount, std::uint32_t reserveCount)
    {
        if (!loaded || !reserve)
            return;
        __try
        {
            *loaded = loadedCount;
            *reserve = reserveCount;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void __fastcall HookInventoryAmmo(void* self, std::uint32_t slot, std::uint32_t equipId,
        std::uint16_t loadCount, std::uint32_t amount, std::uint32_t* loaded, std::uint32_t* reserve)
    {
        g_InventoryAmmo(self, slot, equipId, loadCount, amount, loaded, reserve);
        if (!g_Active.load(std::memory_order_acquire) || equipId - 0x367U < 6)
            return;
        MagazineParameters primary{}, underbarrel{};
        if (!EquipParam_GetWeaponMagazines(static_cast<int>(equipId), primary, underbarrel))
            return;
        const bool secondary = primary.id <= 0;
        const auto& magazine = secondary ? underbarrel : primary;
        if (magazine.id <= 0)
            return;
        StockIndexFn stockIndex = nullptr;
        StockValueFn stockBullet = nullptr, stockAmount = nullptr;
        if (!ReadInventoryAccessors(self, stockIndex, stockBullet, stockAmount))
            return;
        const std::uint32_t index = stockIndex(self, slot, secondary ? 1U : 0U);
        const std::uint32_t existing = stockBullet(self, index) == magazine.bulletId
            ? stockAmount(self, index) : 0U;
        const std::uint32_t available = amount + existing;
        const std::uint32_t total = available < magazine.carryMax ? available : magazine.carryMax;
        const std::uint32_t newLoaded = loadCount < total ? loadCount : total;
        WriteInventoryAmmo(loaded, reserve, newLoaded, total - newLoaded);
    }

    struct HookSpec
    {
        const char* name;
        std::uintptr_t address;
        void* detour;
        void** original;
        const std::uint8_t* prolog;
        std::size_t prologSize;
        std::size_t skipAt = 0;
        std::size_t skipLen = 0;
        void* target = nullptr;
        bool created = false;
    };

    std::array<HookSpec, 10>& HookTable()
    {
        static constexpr std::uint8_t ammoProlog[] = {
            0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x54,0x24,0x10,0x55,0x56,0x57,
            0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x83,0xEC,0x30 };
        static constexpr std::uint8_t walkerProlog[] = {
            0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x8B,0xF2 };
        static constexpr std::uint8_t buddyProlog[] = {
            0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,
            0x48,0x89,0x7C,0x24,0x20,0x41,0x56,0x48,0x83,0xEC,0x20,0x48,0x8B,0x41,0x10 };
        static constexpr std::uint8_t hostageProlog[] = {
            0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,
            0x48,0x89,0x7C,0x24,0x20,0x41,0x56,0x48,0x83,0xEC,0x20,0x48,0x8B,0x41,0x30 };
        static constexpr std::uint8_t soldierProlog[] = {
            0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,
            0x48,0x89,0x7C,0x24,0x20,0x41,0x56,0x48,0x83,0xEC,0x20,0x48,0x8B,0x41,0x50 };
        static constexpr std::uint8_t bossProlog[] = {
            0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,
            0x48,0x89,0x7C,0x24,0x20,0x41,0x56,0x48,0x83,0xEC,0x20,0x48,0x8B,0x41,0x38 };
        static constexpr std::uint8_t countProlog[] = {
            0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,
            0x57,0x48,0x83,0xEC,0x20,0x0F,0xB7,0xC2,0x41,0x0F,0xB6,0xE8,0x48,0x6B,0xC0,0x68 };
        static constexpr std::uint8_t typeProlog[] = {
            0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,
            0x0F,0xB7,0xC2,0x48,0x6B,0xC0,0x68,0x0F,0xB7,0x74,0x08,0x0C,0xC1,0xEE,0x04 };
        static constexpr std::uint8_t inventoryProlog[] = {
            0x40,0x53,0x56,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x83,0xEC,0x50,
            0x48,0x8B,0x05,0x2C,0x05,0xC6,0x01,0x48,0x33,0xC4,0x48,0x89,0x44,0x24,0x38 };

        static std::array<HookSpec, 10> hooks{{
            { "GetAmmoParameterBlock", gAddr.EquipParameterTablesImpl_GetAmmoParameterBlock,
              reinterpret_cast<void*>(&HookAmmoBlock), reinterpret_cast<void**>(&g_AmmoBlock), ammoProlog, sizeof(ammoProlog) },
            { "GetSubWeaponLoadAmmoMax", gAddr.WalkerGearSystemImpl_GetSubWeaponLoadAmmoMax,
              reinterpret_cast<void*>(&HookWalkerAmmo), reinterpret_cast<void**>(&g_WalkerAmmo), walkerProlog, sizeof(walkerProlog) },
            { "BuddyQuietSetupCurrentSlot", gAddr.BuddyQuietEquipControllerImpl_SetupCurrentSlot,
              reinterpret_cast<void*>(&HookBuddyQuietSlot), reinterpret_cast<void**>(&g_BuddyQuietSlot), buddyProlog, sizeof(buddyProlog) },
            { "HostageSetupCurrentSlot", gAddr.HostageEquipControllerImpl_SetupCurrentSlot,
              reinterpret_cast<void*>(&HookHostageSlot), reinterpret_cast<void**>(&g_HostageSlot), hostageProlog, sizeof(hostageProlog) },
            { "SoldierSetupCurrentSlot", gAddr.SoldierEquipControllerImpl_SetupCurrentSlot,
              reinterpret_cast<void*>(&HookSoldierSlot), reinterpret_cast<void**>(&g_SoldierSlot), soldierProlog, sizeof(soldierProlog) },
            { "BossQuietSetupCurrentSlot", gAddr.BossQuietEquipControllerImpl_SetupCurrentSlot,
              reinterpret_cast<void*>(&HookBossQuietSlot), reinterpret_cast<void**>(&g_BossQuietSlot), bossProlog, sizeof(bossProlog) },
            { "GetBulletCountMagazine", gAddr.EquipDevelopControllerImpl_GetBulletCountMagazine,
              reinterpret_cast<void*>(&HookDevelopMagazine), reinterpret_cast<void**>(&g_DevelopMagazine), countProlog, sizeof(countProlog) },
            { "GetBulletCountMax", gAddr.EquipDevelopControllerImpl_GetBulletCountMax,
              reinterpret_cast<void*>(&HookDevelopMax), reinterpret_cast<void**>(&g_DevelopMax), countProlog, sizeof(countProlog) },
            { "GetBulletType", gAddr.EquipDevelopControllerImpl_GetBulletType,
              reinterpret_cast<void*>(&HookDevelopType), reinterpret_cast<void**>(&g_DevelopType), typeProlog, sizeof(typeProlog) },
            { "CalcAmmoAndStockIfPickUp", gAddr.InventoryImpl_CalcAmmoAndStockIfPickUp,
              reinterpret_cast<void*>(&HookInventoryAmmo), reinterpret_cast<void**>(&g_InventoryAmmo), inventoryProlog, sizeof(inventoryProlog),
              16, 4 }
        }};
        return hooks;
    }

    bool PlanLocked()
    {
        if (g_Planned.load(std::memory_order_acquire))
            return true;
        if (g_PlanAttempted)
            return false;
        g_PlanAttempted = true;
        if (!AddressSetRuntime::IsEn154Family(gGameBuild) && !AddressSetRuntime::IsJp154Family(gGameBuild))
        {
            LogError("WideMagazine: game build %s not supported, wide magazines off",
                GetGameBuildName(gGameBuild));
            return false;
        }
        g_QuarkTable = reinterpret_cast<QuarkTableFn>(ResolveGameAddress(gAddr.GetQuarkSystemTable));
        if (!g_QuarkTable)
        {
            LogError("WideMagazine: GetQuarkSystemTable address missing, wide magazines off");
            return false;
        }
        for (auto& hook : HookTable())
        {
            hook.target = ResolveGameAddress(hook.address);
            if (!BytesMatch(hook.target, hook.prolog, hook.prologSize, hook.skipAt, hook.skipLen))
            {
                LogError("WideMagazine: %s code does not match this build, wide magazines off", hook.name);
                return false;
            }
        }
        g_Planned.store(true, std::memory_order_release);
        return true;
    }

    void RollBack(std::array<HookSpec, 10>& hooks)
    {
        g_Active.store(false, std::memory_order_release);
        for (auto& hook : hooks)
        {
            if (!hook.created)
                continue;
            const MH_STATUS queued = MH_QueueDisableHook(hook.target);
            if (queued != MH_OK)
                LogError("WideMagazine: rollback queue failed for %s (%s)", hook.name, MhStatusName(queued));
        }
        const MH_STATUS applied = MH_ApplyQueued();
        if (applied != MH_OK)
            LogError("WideMagazine: rollback apply failed (%s), hooks left as pass-through", MhStatusName(applied));
        for (auto& hook : hooks)
        {
            if (!hook.created)
                continue;
            const MH_STATUS disabled = MH_DisableHook(hook.target);
            if (disabled != MH_OK && disabled != MH_ERROR_DISABLED)
                LogError("WideMagazine: rollback disable failed for %s (%s)", hook.name, MhStatusName(disabled));
        }
    }
}

bool WideMagazine_IsActive()
{
    return g_Active.load(std::memory_order_acquire);
}

bool WideMagazine_Plan()
{
    std::lock_guard<std::mutex> lock(g_InstallMutex);
    return PlanLocked();
}

bool WideMagazine_IsPlanned()
{
    return g_Planned.load(std::memory_order_acquire);
}

bool WideMagazine_Install()
{
    std::lock_guard<std::mutex> lock(g_InstallMutex);
    if (g_Active.load(std::memory_order_acquire))
        return true;
    if (g_HookBatchMode)
    {
        LogError("WideMagazine: installed inside the hook batch, wide magazines off");
        return false;
    }
    if (g_Attempted)
        return false;
    g_Attempted = true;
    if (!PlanLocked())
        return false;
    std::array<HookSpec, 10>& hooks = HookTable();
    for (auto& hook : hooks)
    {
        const MH_STATUS status = MH_CreateHook(hook.target, hook.detour, hook.original);
        if (status != MH_OK)
        {
            LogError("WideMagazine: %s hook create failed (%s), wide magazines off",
                hook.name, MhStatusName(status));
            RollBack(hooks);
            return false;
        }
        hook.created = true;
    }
    for (auto& hook : hooks)
    {
        const MH_STATUS status = MH_QueueEnableHook(hook.target);
        if (status != MH_OK)
        {
            LogError("WideMagazine: %s hook enable failed (%s), wide magazines off",
                hook.name, MhStatusName(status));
            RollBack(hooks);
            return false;
        }
    }
    const MH_STATUS applied = MH_ApplyQueued();
    if (applied != MH_OK)
    {
        LogError("WideMagazine: hook apply failed (%s), wide magazines off", MhStatusName(applied));
        RollBack(hooks);
        return false;
    }
    g_Active.store(true, std::memory_order_release);
    return true;
}
