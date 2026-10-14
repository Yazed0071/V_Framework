#include "pch.h"
#include "EquipAliasLease.h"

#include <atomic>
#include <mutex>
#include <unordered_set>

#include <Windows.h>

#include "AddressSet.h"
#include "EquipIdCompression.h"
#include "EquipPartParams.h"
#include "HookUtils.h"
#include "ThrowingSystemLayout.h"
#include "TppEquip_ReloadEquipIdTable.h"
#include "V_FrameWorkState.h"
#include "log.h"

namespace equip
{
    namespace
    {
        using namespace EquipIdCompression;
        using GetQuarkSystemTableFn = std::uint8_t*(__fastcall*)();

        constexpr std::size_t   kQuarkApp            = 0x98;
        constexpr std::size_t   kAppScriptVars       = 0x10;
        constexpr std::size_t   kVarsWeapons         = 0x548; // vars.weapons, the three carried primary/secondary ids
        constexpr int           kVarsWeaponCells     = 3;
        constexpr int           kEquipStateRowCells  = 0x3A / 2; // one player's EquipState row, all u16 cells
        constexpr std::size_t   kGunPoolCapacity     = 0x50;  // equip system: gun record pool size
        constexpr std::size_t   kGunPoolOwner        = 0x120; // its record table sits at owner+0x28, 0xE bytes a record, word0 = equipId (0x7FF when free)
        constexpr std::size_t   kGunPoolRecords      = 0x28;
        constexpr std::size_t   kGunRecordSize       = 0xE;
        constexpr std::size_t   kItemPoolCapacity    = 0x68;
        constexpr std::size_t   kItemPoolRecords     = 0x140; // 6 bytes a record, word0 = equipId
        constexpr std::size_t   kItemRecordSize      = 6;
        constexpr std::uint32_t kPoolCapacitySanity  = 0x10000;
        constexpr std::uint32_t kReuseIdleMs         = 5000;
        constexpr std::size_t   kAppPlacedSystem     = 0x208; // the interface sits at PlacedSystemImpl+0x20
        constexpr std::size_t   kPlacedInterface     = 0x20;
        constexpr std::size_t   kPlacedCells         = 0x38;  // {u16 equipId, u16 user} per record
        constexpr std::size_t   kPlacedCount         = 0x80;
        constexpr std::size_t   kPlacedFlags         = 0xB0;  // 3 bytes per record, bit0 alive
        constexpr std::uint32_t kPlacedSanity        = 0x100;
        constexpr std::uint32_t kThrowingSanity      = 0x100;
        constexpr std::size_t   kPickableInfos       = 0x38;  // 0x10 per pickable, word0 & 0x7FF = equipId
        constexpr std::size_t   kPickableLocators    = 0x74;
        constexpr std::size_t   kPickableDynamic     = 0x78;  // first dynamic index, 0x30 of them
        constexpr std::size_t   kPickableAllocBits   = 0x90;
        constexpr std::uint32_t kPickableDynamicCount = 0x30;
        constexpr std::uint32_t kPickableSanity      = 0x1000;
        constexpr std::size_t   kPickableInfoSize    = 0x10;

        std::atomic<std::uint16_t> g_WideOfAlias[kNoEquipId + 1] = {};
        std::atomic<std::uint16_t> g_AliasOfWide[kWideEquipIdLast + 1] = {};
        std::atomic<std::uint32_t> g_AliasTouchMs[kNoEquipId + 1] = {};
        std::atomic<std::uint16_t> g_MovedTo[kNoEquipId + 1] = {};
        std::atomic<std::uint16_t> g_GunOfAmmoAlias[kNoEquipId + 1] = {};
        std::atomic<std::size_t>   g_LiveLeases{ 0 };
        std::atomic<const std::uint16_t*> g_LocalEquipStateRow{ nullptr };
        std::atomic<std::uint8_t*> g_EquipSystem{ nullptr };
        std::atomic<std::uint8_t*> g_PickableSystem{ nullptr };
        std::atomic<std::uint32_t> g_InstalledGates{ 0 };
        std::mutex                 g_LeaseMutex;
        std::unordered_set<std::int32_t> g_RefusedWideIds;
        bool                       g_ReuseLogged = false;

        constexpr std::uint32_t kAllGates =
            (1u << static_cast<std::uint32_t>(AliasGate::Count)) - 1;

        std::uint32_t NowMs()
        {
            return static_cast<std::uint32_t>(GetTickCount64());
        }

        void Touch(std::int32_t alias)
        {
            g_AliasTouchMs[alias].store(NowMs(), std::memory_order_relaxed);
        }

        bool AliasIsUnclaimed_NoLock(std::int32_t alias)
        {
            return IsAliasPoolId(alias)
                && g_WideOfAlias[alias].load(std::memory_order_relaxed) == 0
                && !IsExtendedEquipIdUsed(alias)
                && !V_FrameWorkState::IsOwnedEquipId(alias);
        }

        void MarkHeld(std::int32_t value, bool* held)
        {
            if (IsWideEquipId(value))
                value = g_AliasOfWide[value].load(std::memory_order_relaxed);
            else
                value &= kNoEquipId;
            if (value >= kAliasPoolFirst && value <= kAliasPoolLast)
                held[value] = true;
        }

        void MarkHeldCellsSEH(const std::uint16_t* cells, int count, bool* held)
        {
            __try
            {
                for (int i = 0; i < count; ++i)
                    MarkHeld(cells[i], held);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }

        std::uint8_t* ScriptVarsSEH(GetQuarkSystemTableFn getQuark)
        {
            __try
            {
                std::uint8_t* quark = getQuark();
                auto* app = quark ? *reinterpret_cast<std::uint8_t**>(quark + kQuarkApp) : nullptr;
                return app ? *reinterpret_cast<std::uint8_t**>(app + kAppScriptVars) : nullptr;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return nullptr;
            }
        }

        std::uint8_t* ScriptVars()
        {
            auto getQuark = reinterpret_cast<GetQuarkSystemTableFn>(
                ResolveGameAddress(gAddr.GetQuarkSystemTable));
            return getQuark ? ScriptVarsSEH(getQuark) : nullptr;
        }

        std::uint8_t* AppSEH(GetQuarkSystemTableFn getQuark)
        {
            __try
            {
                std::uint8_t* quark = getQuark();
                return quark ? *reinterpret_cast<std::uint8_t**>(quark + kQuarkApp) : nullptr;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return nullptr;
            }
        }

        std::uint8_t* App()
        {
            auto getQuark = reinterpret_cast<GetQuarkSystemTableFn>(
                ResolveGameAddress(gAddr.GetQuarkSystemTable));
            return getQuark ? AppSEH(getQuark) : nullptr;
        }

        void MarkHeldThrowingSEH(std::uint8_t* app, bool* held)
        {
            __try
            {
                namespace tsl = ThrowingSystemLayout;
                std::uint8_t* system = *reinterpret_cast<std::uint8_t**>(app + tsl::kThrowingSystem);
                if (!system)
                    return;
                const std::uint32_t capacity = tsl::Capacity(system);
                if (capacity > kThrowingSanity)
                    return;
                for (std::uint32_t handle = 0; handle < capacity; ++handle)
                    if ((tsl::Read<std::uint32_t>(tsl::Record(system, handle), tsl::kRecordFlags) & tsl::kFlagAlive) != 0)
                        MarkHeld(tsl::EquipId(system, handle), held);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }

        void MarkHeldPlacedSEH(std::uint8_t* app, bool* held)
        {
            __try
            {
                std::uint8_t* iface = *reinterpret_cast<std::uint8_t**>(app + kAppPlacedSystem);
                if (!iface)
                    return;
                std::uint8_t* system = iface - kPlacedInterface;
                const std::uint32_t count = *reinterpret_cast<std::uint32_t*>(system + kPlacedCount);
                const std::uint8_t* cells = *reinterpret_cast<std::uint8_t**>(system + kPlacedCells);
                const std::uint8_t* flags = *reinterpret_cast<std::uint8_t**>(system + kPlacedFlags);
                if (!cells || !flags || count > kPlacedSanity)
                    return;
                for (std::uint32_t i = 0; i < count; ++i)
                    if ((flags[i * 3] & 1) != 0)
                        MarkHeld(*reinterpret_cast<const std::uint16_t*>(cells + i * 4), held);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }

        void MarkHeldPickablesSEH(std::uint8_t* system, bool* held)
        {
            __try
            {
                const std::uint8_t* infos = *reinterpret_cast<std::uint8_t**>(system + kPickableInfos);
                const std::uint8_t* bits = *reinterpret_cast<std::uint8_t**>(system + kPickableAllocBits);
                if (!infos)
                    return;
                const std::uint32_t locators = *reinterpret_cast<std::uint32_t*>(system + kPickableLocators);
                if (locators <= kPickableSanity)
                    for (std::uint32_t i = 0; i < locators; ++i)
                        MarkHeld(*reinterpret_cast<const std::uint16_t*>(infos + i * kPickableInfoSize) & kNoEquipId, held);
                const std::uint32_t first = *reinterpret_cast<std::uint32_t*>(system + kPickableDynamic);
                if (!bits || first > kPickableSanity)
                    return;
                for (std::uint32_t k = 0; k < kPickableDynamicCount; ++k)
                {
                    const std::uint32_t index = first + k;
                    if ((bits[index >> 3] & (1u << (index & 7))) != 0)
                        MarkHeld(*reinterpret_cast<const std::uint16_t*>(infos + index * kPickableInfoSize) & kNoEquipId, held);
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }

        void MarkHeldPoolSEH(std::uint8_t* records, std::size_t stride, std::uint32_t count, bool* held,
                             bool* liveGuns)
        {
            __try
            {
                for (std::uint32_t i = 0; i < count; ++i)
                {
                    const std::uint16_t id = *reinterpret_cast<const std::uint16_t*>(records + i * stride);
                    MarkHeld(id, held);
                    if (liveGuns)
                        liveGuns[id & kNoEquipId] = true;
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }

        bool ReadPoolSEH(std::uint8_t* equipSystem, std::uint8_t*& gunRecords, std::uint32_t& gunCount,
                         std::uint8_t*& itemRecords, std::uint32_t& itemCount)
        {
            __try
            {
                auto* gunOwner = *reinterpret_cast<std::uint8_t**>(equipSystem + kGunPoolOwner);
                gunRecords = gunOwner ? *reinterpret_cast<std::uint8_t**>(gunOwner + kGunPoolRecords) : nullptr;
                gunCount = *reinterpret_cast<std::uint32_t*>(equipSystem + kGunPoolCapacity);
                itemRecords = *reinterpret_cast<std::uint8_t**>(equipSystem + kItemPoolRecords);
                itemCount = *reinterpret_cast<std::uint32_t*>(equipSystem + kItemPoolCapacity);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        void MarkHeldInEquipPools(bool* held)
        {
            std::uint8_t* equipSystem = g_EquipSystem.load(std::memory_order_acquire);
            std::uint8_t* gunRecords = nullptr;
            std::uint8_t* itemRecords = nullptr;
            std::uint32_t gunCount = 0;
            std::uint32_t itemCount = 0;
            if (!equipSystem || !ReadPoolSEH(equipSystem, gunRecords, gunCount, itemRecords, itemCount))
                return;
            bool liveGuns[kNoEquipId + 1] = {};
            if (gunRecords && gunCount <= kPoolCapacitySanity)
                MarkHeldPoolSEH(gunRecords, kGunRecordSize, gunCount, held, liveGuns);
            if (itemRecords && itemCount <= kPoolCapacitySanity)
                MarkHeldPoolSEH(itemRecords, kItemRecordSize, itemCount, held, nullptr);
            for (std::int32_t alias = kAliasPoolFirst; alias <= kAliasPoolLast; ++alias)
            {
                const std::uint16_t gun = g_GunOfAmmoAlias[alias].load(std::memory_order_relaxed);
                if (gun && liveGuns[gun & kNoEquipId])
                    held[alias] = true;
            }
        }

        void CollectHeldAliases(bool* held)
        {
            MarkHeldInEquipPools(held);
            if (std::uint8_t* app = App())
            {
                MarkHeldThrowingSEH(app, held);
                MarkHeldPlacedSEH(app, held);
            }
            if (std::uint8_t* pickables = g_PickableSystem.load(std::memory_order_acquire))
                MarkHeldPickablesSEH(pickables, held);
            if (const std::uint16_t* row = g_LocalEquipStateRow.load(std::memory_order_acquire))
                MarkHeldCellsSEH(row, kEquipStateRowCells, held);
            if (std::uint8_t* vars = ScriptVars())
            {
                MarkHeldCellsSEH(reinterpret_cast<const std::uint16_t*>(vars + kVarsWeapons),
                                 kVarsWeaponCells, held);
                MarkHeldCellsSEH(reinterpret_cast<const std::uint16_t*>(vars + kVarsDroppedWeapons),
                                 kDroppedWeaponCellCount, held);
            }
        }

        std::int32_t ReclaimIdlestAlias_NoLock()
        {
            bool held[kNoEquipId + 1] = {};
            CollectHeldAliases(held);
            const std::uint32_t now = NowMs();
            std::int32_t idlest = -1;
            std::uint32_t idlestFor = 0;
            for (std::int32_t alias = kAliasPoolFirst; alias <= kAliasPoolLast; ++alias)
            {
                if (held[alias] || g_WideOfAlias[alias].load(std::memory_order_relaxed) == 0)
                    continue;
                const std::uint32_t idleFor = now - g_AliasTouchMs[alias].load(std::memory_order_relaxed);
                if (idleFor >= kReuseIdleMs && idleFor > idlestFor)
                {
                    idlest = alias;
                    idlestFor = idleFor;
                }
            }
            if (idlest < 0)
                return -1;
            const std::uint16_t oldWide = g_WideOfAlias[idlest].load(std::memory_order_relaxed);
            g_AliasOfWide[oldWide].store(0, std::memory_order_release);
            g_WideOfAlias[idlest].store(0, std::memory_order_release);
            g_GunOfAmmoAlias[idlest].store(0, std::memory_order_relaxed);
            g_LiveLeases.fetch_sub(1, std::memory_order_relaxed);
            if (!g_ReuseLogged)
            {
                g_ReuseLogged = true;
                LogWarn("EquipAlias: over %zu wide weapons in use this session, engine ids idle %u s get reused",
                    g_LiveLeases.load(std::memory_order_relaxed) + 1, kReuseIdleMs / 1000);
            }
            return idlest;
        }

        bool Bind_NoLock(std::int32_t alias, std::int32_t wideEquipId)
        {
            if (!TppEquip_BindAliasRow(alias, wideEquipId))
                return false;
            EquipParam_BindAliasMotion(alias, wideEquipId);
            Touch(alias);
            g_WideOfAlias[alias].store(static_cast<std::uint16_t>(wideEquipId), std::memory_order_release);
            g_AliasOfWide[wideEquipId].store(static_cast<std::uint16_t>(alias), std::memory_order_release);
            g_LiveLeases.fetch_add(1, std::memory_order_relaxed);
            return true;
        }

        std::int32_t ClaimAlias_NoLock(std::int32_t wideEquipId)
        {
            V_ExtendedEquipRow wideRow{};
            if (!TppEquip_GetExtendedEquipRow(wideEquipId, &wideRow))
                return -1;
            for (std::int32_t alias = kAliasPoolLast; alias >= kAliasPoolFirst; --alias)
            {
                if (!AliasIsUnclaimed_NoLock(alias) || !MarkExtendedEquipIdUsed(alias))
                    continue;
                return Bind_NoLock(alias, wideEquipId) ? alias : -1;
            }
            const std::int32_t reused = ReclaimIdlestAlias_NoLock();
            if (reused < 0)
                return -1;
            return Bind_NoLock(reused, wideEquipId) ? reused : -1;
        }
    }

    std::int32_t ToEngine(std::int32_t equipId)
    {
        if (!IsWideEquipId(equipId))
            return equipId;
        if (const std::uint16_t alias = g_AliasOfWide[equipId].load(std::memory_order_acquire))
        {
            Touch(alias);
            return alias;
        }

        std::lock_guard<std::mutex> lock(g_LeaseMutex);
        if (const std::uint16_t alias = g_AliasOfWide[equipId].load(std::memory_order_relaxed))
            return alias;
        const std::int32_t alias = ClaimAlias_NoLock(equipId);
        if (alias >= 0)
            return alias;
        if (g_RefusedWideIds.insert(equipId).second)
            LogWarn("EquipAlias: equipId 0x%X has no engine id (%zu busy or its row is missing), shows as an empty slot",
                static_cast<unsigned>(equipId), g_LiveLeases.load(std::memory_order_relaxed));
        return kNoEquipId;
    }

    std::int32_t FromEngine(std::int32_t equipId)
    {
        if (equipId < kAliasPoolFirst || equipId > kAliasPoolLast)
            return equipId;
        const std::uint16_t wide = g_WideOfAlias[equipId].load(std::memory_order_acquire);
        if (!wide)
            return equipId;
        Touch(equipId);
        return wide;
    }

    std::int32_t ResolveReal(std::int32_t equipId)
    {
        if (equipId <= 0 || equipId > kNoEquipId)
            return equipId;
        const std::int32_t real = FromEngine(equipId);
        if (real != equipId)
            return real;
        const std::uint16_t moved = g_MovedTo[equipId].load(std::memory_order_acquire);
        if (moved && !V_FrameWorkState::IsClaimedEquipId(equipId))
            return moved;
        return equipId;
    }

    void NoteEquipIdMoved(std::int32_t oldEquipId, std::int32_t newEquipId)
    {
        const bool supportWeaponHole = oldEquipId >= kSupportWeaponHoleFirst && oldEquipId <= kItemBandLast;
        if ((!supportWeaponHole && (oldEquipId < kExtendedAllocFirst || oldEquipId > kExtendedEquipIdLast))
            || newEquipId <= 0 || newEquipId > kWideEquipIdLast)
            return;
        g_MovedTo[oldEquipId].store(static_cast<std::uint16_t>(newEquipId), std::memory_order_release);
    }

    std::int32_t CurrentAliasOf(std::int32_t wideEquipId)
    {
        if (!IsWideEquipId(wideEquipId))
            return 0;
        return g_AliasOfWide[wideEquipId].load(std::memory_order_acquire);
    }

    bool IsLeasedAlias(std::int32_t equipId)
    {
        return equipId >= kAliasPoolFirst && equipId <= kAliasPoolLast
            && g_WideOfAlias[equipId].load(std::memory_order_acquire) != 0;
    }

    std::size_t LiveLeaseCount()
    {
        return g_LiveLeases.load(std::memory_order_relaxed);
    }

    void NoteAmmoAliasFor(std::int32_t gunEngineId, std::int32_t ammoAlias)
    {
        if (ammoAlias < kAliasPoolFirst || ammoAlias > kAliasPoolLast
            || gunEngineId <= 0 || gunEngineId > kNoEquipId)
            return;
        g_GunOfAmmoAlias[ammoAlias].store(static_cast<std::uint16_t>(gunEngineId), std::memory_order_relaxed);
    }

    void NoteEquipSystem(void* equipSystem)
    {
        g_EquipSystem.store(static_cast<std::uint8_t*>(equipSystem), std::memory_order_release);
    }

    void NotePickableSystem(void* pickableSystem)
    {
        g_PickableSystem.store(static_cast<std::uint8_t*>(pickableSystem), std::memory_order_release);
    }

    void NoteLocalEquipStateRow(const std::uint16_t* row)
    {
        g_LocalEquipStateRow.store(row, std::memory_order_release);
    }

    void NoteGateInstalled(AliasGate gate, bool installed)
    {
        const std::uint32_t bit = 1u << static_cast<std::uint32_t>(gate);
        if (installed)
            g_InstalledGates.fetch_or(bit, std::memory_order_release);
        else
            g_InstalledGates.fetch_and(~bit, std::memory_order_release);
    }

    bool WideEquipIdsReady()
    {
        return g_InstalledGates.load(std::memory_order_acquire) == kAllGates;
    }

    namespace
    {
        void KeepRealIdsInCellsSEH(std::uint16_t* cells, int cellCount)
        {
            __try
            {
                for (int i = 0; i < cellCount; ++i)
                    cells[i] = static_cast<std::uint16_t>(ResolveReal(cells[i]));
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }
    }

    void KeepRealIdsInVarsCells(std::size_t varsOffset, int cellCount)
    {
        if (std::uint8_t* vars = ScriptVars())
            KeepRealIdsInCellsSEH(reinterpret_cast<std::uint16_t*>(vars + varsOffset), cellCount);
    }
}
