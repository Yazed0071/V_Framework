#include "pch.h"
#include "SupportStockLease.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "AddressSet.h"
#include "EquipAliasLease.h"
#include "EquipIdCompression.h"
#include "EquipPartParams.h"
#include "GoneEquipCleanup.h"
#include "HookArena.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "SupportWeaponRegistry.h"
#include "V_FrameWorkState.h"
#include "log.h"

namespace
{
    constexpr std::uint32_t kEquipIdCount       = 0x10000;   // real ids, wide included
    constexpr std::uint32_t kNarrowEquipIdCount = 0x800;
    constexpr std::uint32_t kExtendedBase       = 0x600;   // stock = id - 0x50A from here, id - 0xEC below
    constexpr std::uint32_t kLowStockOffset     = 0xEC;
    constexpr std::uint32_t kHighStockOffset    = 0x50A;
    constexpr std::uint32_t kThrowingType       = 12;
    constexpr std::uint32_t kPlacedType         = 13;
    constexpr std::uint32_t kHandItemFirst      = 0x1C2;   // hand items: stock = id - 0x100
    constexpr std::uint32_t kHandItemLast       = 0x1F1;
    constexpr std::uint32_t kHandItemStockBase  = 0x100;
    constexpr std::uint16_t kVanillaMagazine    = 0x15D;
    constexpr std::uint64_t kProtectMs          = 10 * 60 * 1000;
    constexpr const char*   kPersistSpace       = "SWPSTOCK";

    constexpr std::uint8_t kPool[] = {   // stock bytes no vanilla gun, item or support weapon derives
        0xBC, 0xC0, 0xC4, 0xC5, 0xC6, 0xC7, 0xDB, 0xDC, 0xDD,
        0xDE, 0xDF, 0xE0, 0xE1, 0xE2, 0xEA, 0xEE, 0xEF,
    };

    constexpr std::size_t kSupportCells       = 0x584;
    constexpr std::size_t kInitSupportCells   = 0x526;
    constexpr std::size_t kItemCells          = 0x5A4;
    constexpr std::size_t kInitItemCells      = 0x536;
    constexpr std::size_t kAmmoStockIds       = 0x5E6;
    constexpr std::size_t kCboxEquipIds       = 0x2BE8;
    constexpr int         kSlotCells          = 8;
    constexpr int         kCboxCells          = 16;
    constexpr int         kStockSlots         = 23;
    constexpr std::size_t kPresetBuffers[]    = { 0xEF6, 0x12F6, 0x16F8 };
    constexpr int         kPresetSlots        = 4;
    constexpr int         kPresetSlotCells    = 36;
    constexpr int         kPresetCellFirst    = 4;
    constexpr int         kPresetCellLast     = 19;
    constexpr std::size_t kPlacedCells        = 0x2172;
    constexpr int         kPlacedCount        = 32;
    constexpr std::size_t kCboxRequestSupport = 0x14EEE;   // SupplyCboxDropRequest copy: support u16 x8, then items u16 x8
    constexpr std::size_t kCboxRequestItems   = 0x14EFE;
    constexpr std::size_t kCboxRequestStocks  = 0x14EBE;   // its 23 stock-lane bytes

    std::atomic<std::uint8_t>  g_ByteOf[kEquipIdCount] = {};
    std::atomic<std::uint16_t> g_EquipOf[256] = {};
    std::atomic<std::uint64_t> g_TouchMs[256] = {};
    bool                       g_Usable[256] = {};
    std::atomic<bool>          g_Live{ false };
    std::atomic<bool>          g_Loaded{ false };
    std::atomic<bool>          g_HasPending{ false };
    std::mutex                 g_Mutex;
    std::vector<std::pair<std::int32_t, std::int32_t>> g_PendingPersist;
    std::set<std::uint32_t>    g_WarnedExhausted;
    std::vector<std::pair<std::int32_t, std::int32_t>> g_LoadedRows;

    bool IsPoolByte(int value)
    {
        for (const std::uint8_t b : kPool)
            if (b == value)
                return true;
        return false;
    }

    std::uint32_t NativeStock(std::uint32_t equipId)
    {
        return equipId < kExtendedBase ? equipId - kLowStockOffset : equipId - kHighStockOffset;
    }

    bool IsAliasBand(std::uint32_t equipId)
    {
        return EquipIdCompression::WideWeaponsLive()
            && EquipIdCompression::IsAliasPoolId(static_cast<std::int32_t>(equipId));
    }

    bool IsSupportType(std::uint32_t equipId)
    {
        const std::uint32_t type = EquipParam_GetEquipType(equipId);
        return type == kThrowingType || type == kPlacedType;
    }

    bool IsCandidate(std::uint32_t equipId)
    {
        return equipId > 0 && equipId < kEquipIdCount
            && !IsAliasBand(equipId)
            && IsSupportType(equipId)
            && !SupportWeapon::IsVanillaSupportWeaponEquipId(equipId)
            && V_FrameWorkState::IsClaimedEquipId(static_cast<std::int32_t>(equipId));
    }

    void QueuePersist_NoLock(std::uint32_t equipId, std::uint8_t stock)
    {
        g_PendingPersist.emplace_back(static_cast<std::int32_t>(equipId), stock);
        g_HasPending.store(true, std::memory_order_release);
    }

    void Assign_NoLock(std::uint32_t equipId, std::uint8_t stock, bool persist)
    {
        g_EquipOf[stock].store(static_cast<std::uint16_t>(equipId), std::memory_order_release);
        g_ByteOf[equipId].store(stock, std::memory_order_release);
        g_TouchMs[stock].store(GetTickCount64(), std::memory_order_relaxed);
        if (persist)
            QueuePersist_NoLock(equipId, stock);
    }

    void Release_NoLock(std::uint8_t stock)
    {
        const std::uint16_t owner = g_EquipOf[stock].load(std::memory_order_acquire);
        if (owner != 0)
        {
            g_ByteOf[owner].store(0, std::memory_order_release);
            QueuePersist_NoLock(owner, 0);
        }
        g_EquipOf[stock].store(0, std::memory_order_release);
    }

    bool CellsHoldSafely(const std::uint8_t* vars, std::size_t offset, int count, std::uint16_t equipId)
    {
        __try
        {
            const auto* cells = reinterpret_cast<const std::uint16_t*>(vars + offset);
            for (int i = 0; i < count; ++i)
                if (cells[i] == equipId || equip::FromEngine(cells[i]) == equipId)
                    return true;
            return false;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return true;
        }
    }

    bool StockHeldSafely(const std::uint8_t* vars, std::uint8_t stock)
    {
        __try
        {
            for (int i = 0; i < kStockSlots; ++i)
                if (vars[kAmmoStockIds + i] == stock || vars[kCboxRequestStocks + i] == stock)
                    return true;
            return false;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return true;
        }
    }

    bool Held(std::uint16_t equipId, std::uint8_t stock)
    {
        const std::uint8_t* vars = GoneEquipCleanup::ScriptVars();
        if (!vars)
            return true;
        if (StockHeldSafely(vars, stock)
            || CellsHoldSafely(vars, kSupportCells, kSlotCells, equipId)
            || CellsHoldSafely(vars, kInitSupportCells, kSlotCells, equipId)
            || CellsHoldSafely(vars, kItemCells, kSlotCells, equipId)
            || CellsHoldSafely(vars, kInitItemCells, kSlotCells, equipId)
            || CellsHoldSafely(vars, kCboxEquipIds, kCboxCells, equipId)
            || CellsHoldSafely(vars, kCboxRequestSupport, kSlotCells, equipId)
            || CellsHoldSafely(vars, kCboxRequestItems, kSlotCells, equipId)
            || CellsHoldSafely(vars, kPlacedCells, kPlacedCount, equipId))
            return true;
        for (const std::size_t buffer : kPresetBuffers)
            for (int slot = 0; slot < kPresetSlots; ++slot)
            {
                const std::size_t first = buffer + (slot * kPresetSlotCells + kPresetCellFirst) * sizeof(std::uint16_t);
                if (CellsHoldSafely(vars, first, kPresetCellLast - kPresetCellFirst + 1, equipId))
                    return true;
            }
        return false;
    }

    void RefinePool(bool (&usable)[256])
    {
        for (const std::uint8_t b : kPool)
            usable[b] = !EquipParam_IsBulletIdTaken(b);
        for (std::uint32_t id = kHandItemFirst; id <= kHandItemLast; ++id)
            if (EquipParam_GetEquipType(id) != 0)
                usable[(id - kHandItemStockBase) & 0xFF] = false;
        for (std::uint32_t id = 1; id < kNarrowEquipIdCount; ++id)
            if (!IsAliasBand(id) && IsSupportType(id) && !IsCandidate(id))
                usable[NativeStock(id) & 0xFF] = false;
    }

    std::mutex g_LoadMutex;

    void CollectPersisted(const char* name, std::int32_t value)
    {
        g_LoadedRows.emplace_back(std::atoi(name), value);
    }

    bool EnsureLoaded()
    {
        if (g_Loaded.load(std::memory_order_acquire))
            return true;
        if (EquipParam_GetEquipType(kVanillaMagazine) == 0)
            return false;
        std::lock_guard<std::mutex> loadLock(g_LoadMutex);
        if (g_Loaded.load(std::memory_order_acquire))
            return true;
        bool usable[256] = {};
        RefinePool(usable);
        g_LoadedRows.clear();
        V_FrameWorkState::ForEachPersistedConstant(kPersistSpace, &CollectPersisted);
        struct LoadedLease { std::uint32_t equipId; std::int32_t stock; bool valid; };
        std::vector<LoadedLease> rows;
        std::vector<std::uint32_t> movedAway;
        for (const auto& row : g_LoadedRows)
        {
            if (row.second <= 0 || row.first <= 0 || row.first >= static_cast<std::int32_t>(kEquipIdCount))
                continue;
            auto equipId = static_cast<std::uint32_t>(row.first);
            if (!IsAliasBand(equipId))
                equipId = static_cast<std::uint32_t>(equip::ResolveReal(row.first));
            if (equipId != static_cast<std::uint32_t>(row.first))
                movedAway.push_back(static_cast<std::uint32_t>(row.first));
            rows.push_back({ equipId, row.second, row.second < 256 && IsCandidate(equipId) });
        }

        std::lock_guard<std::mutex> lock(g_Mutex);
        std::memcpy(g_Usable, usable, sizeof(g_Usable));
        for (const std::uint32_t equipId : movedAway)
            QueuePersist_NoLock(equipId, 0);
        for (const LoadedLease& row : rows)
        {
            if (row.valid && g_Usable[row.stock]
                && g_EquipOf[row.stock].load(std::memory_order_relaxed) == 0
                && g_ByteOf[row.equipId].load(std::memory_order_relaxed) == 0)
                Assign_NoLock(row.equipId, static_cast<std::uint8_t>(row.stock), true);
            else
                QueuePersist_NoLock(row.equipId, 0);
        }
        g_LoadedRows.clear();
        g_Loaded.store(true, std::memory_order_release);
        return true;
    }

    int PickFree_NoLock(std::uint32_t native)
    {
        if (native < 256 && IsPoolByte(static_cast<int>(native)) && g_Usable[native]
            && g_EquipOf[native].load(std::memory_order_relaxed) == 0)
            return static_cast<int>(native);
        for (const std::uint8_t b : kPool)
            if (g_Usable[b] && g_EquipOf[b].load(std::memory_order_relaxed) == 0)
                return b;
        return -1;
    }

    int MovedLease_NoLock(std::uint32_t equipId)
    {
        for (const std::uint8_t b : kPool)
        {
            const std::uint16_t owner = g_EquipOf[b].load(std::memory_order_relaxed);
            if (owner != 0 && owner != equipId && !IsAliasBand(owner)
                && static_cast<std::uint32_t>(equip::ResolveReal(owner)) == equipId)
                return b;
        }
        return -1;
    }

    int Reclaim_NoLock()
    {
        const std::uint64_t now = GetTickCount64();
        int best = -1;
        std::uint64_t bestTouch = UINT64_MAX;
        for (const std::uint8_t b : kPool)
        {
            if (!g_Usable[b])
                continue;
            const std::uint16_t owner = g_EquipOf[b].load(std::memory_order_relaxed);
            const std::uint64_t touch = g_TouchMs[b].load(std::memory_order_relaxed);
            if (owner != 0 && IsCandidate(owner) && (now - touch < kProtectMs || Held(owner, b)))
                continue;
            if (touch < bestTouch)
            {
                best = b;
                bestTouch = touch;
            }
        }
        if (best >= 0)
            Release_NoLock(static_cast<std::uint8_t>(best));
        return best;
    }

    std::uint32_t Acquire(std::uint32_t equipId, std::uint32_t native)
    {
        if (!EnsureLoaded())
            return native;
        std::lock_guard<std::mutex> lock(g_Mutex);
        const std::uint8_t existing = g_ByteOf[equipId].load(std::memory_order_acquire);
        if (existing != 0)
            return existing;
        int stock = MovedLease_NoLock(equipId);
        if (stock >= 0)
            Release_NoLock(static_cast<std::uint8_t>(stock));
        else
            stock = PickFree_NoLock(native);
        if (stock < 0)
            stock = Reclaim_NoLock();
        if (stock < 0)
        {
            if (g_WarnedExhausted.insert(equipId).second)
                LogWarn("SupportWeapon: no free ammo stock byte for equipId %u (more than %zu custom support weapons in use), its ammo may mix with another item",
                    equipId, sizeof(kPool));
            return native;
        }
        Assign_NoLock(equipId, static_cast<std::uint8_t>(stock), true);
        return static_cast<std::uint32_t>(stock);
    }

    enum class Reg : std::uint8_t { Eax, Ecx, R8d, R9d, Ebx, Esi, Edi, R15d };

    struct SiteSpec
    {
        const char*           name;
        std::uintptr_t AddressSetRuntime::AddressSet::* field;
        std::uint8_t          high[7];
        std::uint8_t          length;
        Reg                   source;
        Reg                   destination;
        bool                  repeated;    // a second identical pair follows (support and item cells)
    };

    using AS = AddressSetRuntime::AddressSet;
    const SiteSpec kSites[] = {
        { "stock id lookup",         &AS::EquipIdTable_GetAmmoStockIdOfSupportOrItem_StockSite,             { 0x8D, 0x83, 0xF6, 0xFA, 0xFF, 0xFF },       6, Reg::Ebx,  Reg::Eax,  false },
        { "supply request",          &AS::MissionPreparationSystemImpl_CalcEquipWeaponItemCost_StockSite,    { 0x44, 0x8D, 0x8B, 0xF6, 0xFA, 0xFF, 0xFF }, 7, Reg::Ebx,  Reg::R9d,  false },
        { "loadout store",           &AS::LoadoutManagerImpl_StoreLoadoutInfoToPlayerVars_StockSite,        { 0x81, 0xC7, 0xF6, 0xFA, 0xFF, 0xFF },       6, Reg::Edi,  Reg::Edi,  true  },
        { "mission start",           &AS::EquipControllerImpl_InitializePlayerAtIndex_StockSite,            { 0x44, 0x8D, 0x83, 0xF6, 0xFA, 0xFF, 0xFF }, 7, Reg::Ebx,  Reg::R8d,  false },
        { "ammo box pickup",         &AS::PickUpActionPluginImpl_CheckLoadableForSupportOrItem_StockSite,   { 0x44, 0x8D, 0x83, 0xF6, 0xFA, 0xFF, 0xFF }, 7, Reg::Ebx,  Reg::R8d,  false },
        { "placed pickup slot",      &AS::PickUpActionPluginImpl_DoPickUpPlaced_SlotStockSite,              { 0x8D, 0x86, 0xF6, 0xFA, 0xFF, 0xFF },       6, Reg::Esi,  Reg::Eax,  false },
        { "placed pickup item",      &AS::PickUpActionPluginImpl_DoPickUpPlaced_PickedStockSite,            { 0x41, 0x8D, 0x8F, 0xF6, 0xFA, 0xFF, 0xFF }, 7, Reg::R15d, Reg::Ecx,  false },
        { "item pickup slot",        &AS::PickUpActionPluginImpl_DoPickUpWithPickableInfo_StockSite,        { 0x8D, 0x8E, 0xF6, 0xFA, 0xFF, 0xFF },       6, Reg::Esi,  Reg::Ecx,  false },
        { "supply menu ammo",        &AS::ItemSelectorCallbackImpl_GetCurrentAmmoFromVarsForSupportWeapon_StockSite, { 0x81, 0xC3, 0xF6, 0xFA, 0xFF, 0xFF }, 6, Reg::Ebx, Reg::Ebx, false },
        { "ammo parameters",         &AS::EquipParameterTablesImpl_GetAmmoParameterBlock_StockSite,         { 0x8D, 0xBB, 0xF6, 0xFA, 0xFF, 0xFF },       6, Reg::Ebx,  Reg::Edi,  false },
    };

    constexpr std::size_t kDispOffsetFromEnd = 4;          // disp32 is the last 4 bytes of each pattern
    constexpr std::uint8_t kLowDisp[4] = { 0x14, 0xFF, 0xFF, 0xFF };   // -0xEC
    constexpr std::size_t kPairSearch   = 0x20;
    constexpr std::size_t kRepeatSearch = 0x100;
    constexpr int         kMaxCaves     = 64;

    struct Cave
    {
        std::uint8_t* site = nullptr;
        std::uint8_t  original[8] = {};
        std::size_t   length = 0;
    };
    Cave g_Caves[kMaxCaves];
    int  g_CaveCount = 0;

    void Emit(std::vector<std::uint8_t>& code, std::initializer_list<std::uint8_t> bytes)
    {
        code.insert(code.end(), bytes);
    }

    void Emit64(std::vector<std::uint8_t>& code, std::uint64_t value)
    {
        for (int i = 0; i < 8; ++i)
            code.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
    }

    void EmitMovEcxFrom(std::vector<std::uint8_t>& code, Reg source)
    {
        switch (source)
        {
        case Reg::Ebx:  Emit(code, { 0x89, 0xD9 }); break;         // mov ecx, ebx
        case Reg::Esi:  Emit(code, { 0x89, 0xF1 }); break;         // mov ecx, esi
        case Reg::Edi:  Emit(code, { 0x89, 0xF9 }); break;         // mov ecx, edi
        case Reg::R15d: Emit(code, { 0x44, 0x89, 0xF9 }); break;   // mov ecx, r15d
        default: break;
        }
    }

    void EmitStoreResult(std::vector<std::uint8_t>& code, Reg destination)
    {
        const auto slot = [&](std::uint8_t offset) {                 // mov [rsp+offset], rax into the saved register
            Emit(code, { 0x48, 0x89, 0x84, 0x24, offset, 0x00, 0x00, 0x00 });
        };
        switch (destination)
        {
        case Reg::Eax: slot(0xB0); break;
        case Reg::Ecx: slot(0xA8); break;
        case Reg::R8d: slot(0x98); break;
        case Reg::R9d: slot(0x90); break;
        case Reg::Ebx: Emit(code, { 0x89, 0xC3 }); break;           // mov ebx, eax
        case Reg::Edi: Emit(code, { 0x89, 0xC7 }); break;           // mov edi, eax
        default: break;
        }
    }

    std::vector<std::uint8_t> BuildThunk(const SiteSpec& spec, std::uintptr_t resume)
    {
        std::vector<std::uint8_t> code;
        Emit(code, { 0x9C, 0x50, 0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53 });   // pushfq, push rax rcx rdx r8-r11
        Emit(code, { 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00 });                               // sub rsp, 0x80
        Emit(code, { 0xF3, 0x0F, 0x7F, 0x44, 0x24, 0x20, 0xF3, 0x0F, 0x7F, 0x4C, 0x24, 0x30,     // movdqu [rsp+0x20..0x70], xmm0-5
                     0xF3, 0x0F, 0x7F, 0x54, 0x24, 0x40, 0xF3, 0x0F, 0x7F, 0x5C, 0x24, 0x50,
                     0xF3, 0x0F, 0x7F, 0x64, 0x24, 0x60, 0xF3, 0x0F, 0x7F, 0x6C, 0x24, 0x70 });
        EmitMovEcxFrom(code, spec.source);
        Emit(code, { 0x48, 0xB8 });                                                              // mov rax, StockOf
        Emit64(code, reinterpret_cast<std::uint64_t>(&SupportStockLease::StockOf));
        Emit(code, { 0xFF, 0xD0, 0x89, 0xC0 });                                                  // call rax; mov eax, eax
        EmitStoreResult(code, spec.destination);
        Emit(code, { 0xF3, 0x0F, 0x6F, 0x44, 0x24, 0x20, 0xF3, 0x0F, 0x6F, 0x4C, 0x24, 0x30,     // movdqu xmm0-5, [rsp+0x20..0x70]
                     0xF3, 0x0F, 0x6F, 0x54, 0x24, 0x40, 0xF3, 0x0F, 0x6F, 0x5C, 0x24, 0x50,
                     0xF3, 0x0F, 0x6F, 0x64, 0x24, 0x60, 0xF3, 0x0F, 0x6F, 0x6C, 0x24, 0x70 });
        Emit(code, { 0x48, 0x81, 0xC4, 0x80, 0x00, 0x00, 0x00 });                               // add rsp, 0x80
        Emit(code, { 0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59, 0x41, 0x58, 0x5A, 0x59, 0x58, 0x9D });   // pop r11-r8 rdx rcx rax, popfq
        Emit(code, { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 });                                      // jmp [rip+0]
        Emit64(code, resume);
        return code;
    }

    bool BytesMatchSafely(const std::uint8_t* at, const std::uint8_t* expected, std::size_t length)
    {
        __try
        {
            return std::memcmp(at, expected, length) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint8_t* FindForward(std::uint8_t* from, std::size_t range, const std::uint8_t* pattern, std::size_t length)
    {
        for (std::size_t i = 0; i < range; ++i)
            if (BytesMatchSafely(from + i, pattern, length))
                return from + i;
        return nullptr;
    }

    bool InstallCave(const SiteSpec& spec, std::uint8_t* site)
    {
        if (g_CaveCount >= kMaxCaves)
            return false;
        const std::vector<std::uint8_t> code = BuildThunk(spec, reinterpret_cast<std::uintptr_t>(site) + spec.length);
        auto* thunk = static_cast<std::uint8_t*>(HookArena::AllocateNear(reinterpret_cast<std::uintptr_t>(site), code.size()));
        if (!thunk)
            return false;
        std::memcpy(thunk, code.data(), code.size());
        FlushInstructionCache(GetCurrentProcess(), thunk, code.size());

        const std::int64_t rel = static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(thunk))
                               - static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(site) + 5);
        if (rel < INT32_MIN || rel > INT32_MAX)
            return false;
        std::uint8_t patch[8];
        std::memset(patch, 0x90, sizeof(patch));
        patch[0] = 0xE9;
        const std::int32_t rel32 = static_cast<std::int32_t>(rel);
        std::memcpy(patch + 1, &rel32, sizeof(rel32));

        DWORD old = 0;
        if (!VirtualProtect(site, spec.length, PAGE_EXECUTE_READWRITE, &old))
            return false;
        Cave& cave = g_Caves[g_CaveCount++];
        cave.site = site;
        cave.length = spec.length;
        std::memcpy(cave.original, site, spec.length);
        std::memcpy(site, patch, spec.length);
        VirtualProtect(site, spec.length, old, &old);
        FlushInstructionCache(GetCurrentProcess(), site, spec.length);
        return true;
    }

    bool InstallPair(const SiteSpec& spec, std::uint8_t* high)
    {
        std::uint8_t low[7];
        std::memcpy(low, spec.high, spec.length);
        std::memcpy(low + spec.length - kDispOffsetFromEnd, kLowDisp, sizeof(kLowDisp));
        std::uint8_t* lowSite = FindForward(high + spec.length, kPairSearch, low, spec.length);
        return lowSite && InstallCave(spec, high) && InstallCave(spec, lowSite);
    }

    bool InstallSite(const SiteSpec& spec)
    {
        auto* high = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.*(spec.field)));
        if (!high || !BytesMatchSafely(high, spec.high, spec.length))
        {
            LogError("SupportWeapon: %s ammo stock site missing or changed on this build, custom support weapons keep the 5-id limit",
                spec.name);
            return false;
        }
        if (!InstallPair(spec, high))
        {
            LogError("SupportWeapon: %s ammo stock patch failed, custom support weapons keep the 5-id limit", spec.name);
            return false;
        }
        if (spec.repeated)
        {
            std::uint8_t* second = FindForward(high + spec.length, kRepeatSearch, spec.high, spec.length);
            if (second && !InstallPair(spec, second))
            {
                LogError("SupportWeapon: %s second ammo stock patch failed, custom support weapons keep the 5-id limit", spec.name);
                return false;
            }
        }
        return true;
    }

    void RestoreAll()
    {
        while (g_CaveCount > 0)
        {
            Cave& cave = g_Caves[--g_CaveCount];
            DWORD old = 0;
            if (cave.site && VirtualProtect(cave.site, cave.length, PAGE_EXECUTE_READWRITE, &old))
            {
                std::memcpy(cave.site, cave.original, cave.length);
                VirtualProtect(cave.site, cave.length, old, &old);
                FlushInstructionCache(GetCurrentProcess(), cave.site, cave.length);
            }
            cave = Cave{};
        }
    }
}

namespace SupportStockLease
{
    bool Install()
    {
        for (const SiteSpec& spec : kSites)
        {
            if (!InstallSite(spec))
            {
                RestoreAll();
                return true;
            }
        }
        g_Live.store(true, std::memory_order_release);
        return true;
    }

    void Uninstall()
    {
        g_Live.store(false, std::memory_order_release);
        RestoreAll();
    }

    bool Live()
    {
        return g_Live.load(std::memory_order_acquire);
    }

    void Pump()
    {
        if (!g_HasPending.load(std::memory_order_acquire))
            return;
        std::vector<std::pair<std::int32_t, std::int32_t>> pending;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            pending.swap(g_PendingPersist);
            g_HasPending.store(false, std::memory_order_release);
        }
        for (const auto& row : pending)
            V_FrameWorkState::SetPersistedConstant(kPersistSpace, std::to_string(row.first).c_str(), row.second);
    }

    bool IsReservedStockByte(int value)
    {
        return IsPoolByte(value);
    }

    std::int32_t EquipOfLentByte(std::uint8_t stock)
    {
        if (!g_Live.load(std::memory_order_acquire))
            return 0;
        return g_EquipOf[stock].load(std::memory_order_acquire);
    }

    std::uint32_t __fastcall StockOf(std::uint32_t equipId)
    {
        const std::uint32_t native = NativeStock(equipId);
        if (!g_Live.load(std::memory_order_acquire) || equipId == 0 || equipId >= kEquipIdCount
            || MissionCodeGuard::ShouldBypassHooks())
            return native;
        equipId = static_cast<std::uint32_t>(equip::FromEngine(static_cast<std::int32_t>(equipId)));
        const std::uint8_t stock = g_ByteOf[equipId].load(std::memory_order_acquire);
        if (stock != 0)
        {
            g_TouchMs[stock].store(GetTickCount64(), std::memory_order_relaxed);
            return stock;
        }
        if (!IsCandidate(equipId))
            return native;
        return Acquire(equipId, native);
    }
}
