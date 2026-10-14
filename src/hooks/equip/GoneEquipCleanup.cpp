#include "pch.h"
#include "GoneEquipCleanup.h"

#include <Windows.h>
#include <cstdint>
#include <mutex>
#include <set>

#include "AddressSet.h"
#include "EquipAliasLease.h"
#include "EquipIdCompression.h"
#include "EquipPartParams.h"
#include "HookUtils.h"
#include "TppEquip_ReloadEquipIdTable.h"
#include "V_FrameWorkState.h"
#include "log.h"

namespace
{
    using GetQuarkSystemTable_t = std::uint8_t*(__fastcall*)();

    constexpr std::size_t kGameSystemTable = 0x98;
    constexpr std::size_t kScriptVars      = 0x10;

    constexpr std::size_t kInitSupportCells = 0x526;
    constexpr std::size_t kInitItemCells    = 0x536;
    constexpr std::size_t kSupportCells     = 0x584;
    constexpr std::size_t kItemCells        = 0x5A4;
    constexpr std::size_t kAmmoStockIds     = 0x5E6;   // u8 per stock slot
    constexpr std::size_t kAmmoStockCounts  = 0x614;   // u16 per stock slot
    constexpr std::size_t kPlacedWeapons    = 0x2172;
    constexpr std::size_t kCboxEquipIds     = 0x2BE8;
    constexpr int         kSlotCells        = 8;
    constexpr int         kPlacedCells      = 32;
    constexpr int         kCboxCells        = 16;
    constexpr std::size_t kCboxRequestSupport = 0x14EEE;   // SupplyCboxDropRequest copy: support u16 x8, then items u16 x8
    constexpr std::size_t kCboxRequestItems   = 0x14EFE;
    constexpr int         kSupportStockFirst = 6;
    constexpr int         kItemStockFirst    = 0x0E;

    constexpr std::size_t kLoadoutPresetBuffers[] = { 0xEF6, 0x12F6, 0x16F8 };   // loadoutInfo, sortieLoadoutInfo, returnHeliLoadoutInfo U16 buffers
    constexpr int         kPresetSlots       = 4;
    constexpr int         kPresetSlotCells   = 36;
    constexpr int         kPresetSupportFirst = 4;
    constexpr int         kPresetItemLast     = 19;

    constexpr std::uint16_t kVanillaMagazineEquipId = 0x15D;
    constexpr std::uint16_t kAliasBandFirst         = 0x700;   // leased wide-id aliases and wide ids start here

    std::mutex              g_LoggedMutex;
    std::set<std::uint16_t> g_Logged;

    bool EquipTableLoaded()
    {
        return EquipParam_GetEquipType(kVanillaMagazineEquipId) != 0;
    }

    bool IsGone(std::uint16_t equipId)
    {
        if (equipId == 0)
            return false;
        if (equipId > EquipIdCompression::kNoEquipId)
        {
            V_ExtendedEquipRow row{};
            return !TppEquip_GetExtendedEquipRow(equipId, &row);
        }
        if (EquipIdCompression::WideWeaponsLive() && EquipIdCompression::IsAliasPoolId(equipId))
            return !equip::IsLeasedAlias(equipId)
                && !EquipIdCompression::IsExtendedEquipIdUsed(equipId)
                && !V_FrameWorkState::IsOwnedEquipId(equipId);
        if (equipId >= kAliasBandFirst)
            return false;
        return EquipParam_GetEquipType(equipId) == 0;
    }

    void NoteCleared(std::uint16_t equipId)
    {
        std::lock_guard<std::mutex> lock(g_LoggedMutex);
        if (g_Logged.insert(equipId).second)
            LogWarn("GoneEquipCleanup: equipId %u is in the save but no mod registers it now, removed from loadouts, supply drops and placed items",
                equipId);
    }

    bool ClearIfGone(std::uint16_t* cell)
    {
        const std::int32_t realId = equip::ResolveReal(*cell);
        if (realId != *cell && realId > 0 && realId <= EquipIdCompression::kWideEquipIdLast)
            *cell = static_cast<std::uint16_t>(realId);
        if (!IsGone(*cell))
            return false;
        NoteCleared(*cell);
        *cell = 0;
        return true;
    }

    std::uint16_t* Cells(std::uint8_t* vars, std::size_t offset)
    {
        return reinterpret_cast<std::uint16_t*>(vars + offset);
    }

    void ClearStock(std::uint8_t* vars, int stockSlot)
    {
        vars[kAmmoStockIds + stockSlot] = 0;
        Cells(vars, kAmmoStockCounts)[stockSlot] = 0;
    }

    void CleanVarsUnsafe(std::uint8_t* vars)
    {
        for (int i = 0; i < kSlotCells; ++i)
        {
            ClearIfGone(Cells(vars, kInitSupportCells) + i);
            ClearIfGone(Cells(vars, kInitItemCells) + i);
            if (ClearIfGone(Cells(vars, kSupportCells) + i))
                ClearStock(vars, kSupportStockFirst + i);
            if (ClearIfGone(Cells(vars, kItemCells) + i))
                ClearStock(vars, kItemStockFirst + i);
        }
        for (const std::size_t buffer : kLoadoutPresetBuffers)
            for (int slot = 0; slot < kPresetSlots; ++slot)
                for (int cell = kPresetSupportFirst; cell <= kPresetItemLast; ++cell)
                    ClearIfGone(Cells(vars, buffer) + slot * kPresetSlotCells + cell);
        for (int i = 0; i < kCboxCells; ++i)
            ClearIfGone(Cells(vars, kCboxEquipIds) + i);
        for (int i = 0; i < kSlotCells; ++i)
        {
            ClearIfGone(Cells(vars, kCboxRequestSupport) + i);
            ClearIfGone(Cells(vars, kCboxRequestItems) + i);
        }
        for (int i = 0; i < kPlacedCells; ++i)
            ClearIfGone(Cells(vars, kPlacedWeapons) + i);
    }
}

namespace GoneEquipCleanup
{
    std::uint8_t* ScriptVars()
    {
        auto getQuark = reinterpret_cast<GetQuarkSystemTable_t>(ResolveGameAddress(gAddr.GetQuarkSystemTable));
        if (!getQuark)
            return nullptr;
        std::uint8_t* quark = getQuark();
        if (!quark)
            return nullptr;
        std::uint8_t* gameSystems = *reinterpret_cast<std::uint8_t**>(quark + kGameSystemTable);
        return gameSystems ? *reinterpret_cast<std::uint8_t**>(gameSystems + kScriptVars) : nullptr;
    }

    void CleanVars(std::uint8_t* vars)
    {
        if (!vars || !EquipTableLoaded())
            return;
        __try
        {
            CleanVarsUnsafe(vars);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }
}
