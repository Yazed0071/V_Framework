#include <pch.h>
#include "GunBasicInject.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "AddressSet.h"
#include "EquipPartParams.h"
#include "MotionLoaderImpl_GetMtarIds.h"
#include "TppEquip_ReloadEquipIdTable.h"
#include "EquipAliasLease.h"
#include "PartIdWiden.h"
#include "HookUtils.h"
#include "log.h"
#include "LuaApi.h"
#include "MissionCodeGuard.h"
#include "../../core/V_FrameWorkState.h"

namespace
{
    struct GunBasicRow
    {
        std::int32_t f[13];
        std::int32_t logical[13];
        std::int32_t slotSpace[13];
    };

    using ReloadEquipParameterTables2_t = int(__fastcall*)(lua_State* L);
    static ReloadEquipParameterTables2_t g_OrigReload = nullptr;

    static std::recursive_mutex g_Mutex;
    static std::vector<GunBasicRow> g_Rows;
    static std::vector<GunBasicRow> g_PendingRows;
    static std::map<std::string, int> g_WpNameToId;
    static std::set<int> g_ClaimedIds;
    static std::set<int> g_ReservedIds;
    static bool g_PersistedReserved = false;

    static constexpr int kNoneValue = 0;

    static constexpr int kEssentialDefaultId = 1;

    static constexpr int kGunBasicMaxId = 65535;

    static constexpr int kPackedSubIdMax = 1023;

    static std::vector<std::uint8_t> g_GunBasicShadow;
    static bool g_GunBasicShadowActive = false;

    static constexpr int kNoRowCacheSize = 0x10000;
    static std::atomic<std::uint32_t> g_RowEpoch{ 1 };
    static std::atomic<std::uint64_t> g_NoRowCacheEpoch{ 0 };
    static std::atomic<std::uint8_t>  g_NoRowCache[kNoRowCacheSize];   // 0 unknown, 1 has a row or not a gun, 2 no row
    static std::atomic<std::uint64_t> g_NoRowLogged[kNoRowCacheSize / 64];

    static void** GunBasicPtrLoc()
    {
        auto* impl = static_cast<std::uint8_t*>(
            ResolveGameAddress(gAddr.EquipParameterTablesImpl_Instance));
        if (!impl)
            return nullptr;
        return reinterpret_cast<void**>(impl + 0x08);
    }

    static std::uint8_t* ReadPtrSEH(void** loc)
    {
        if (!loc)
            return nullptr;
        __try
        {
            return static_cast<std::uint8_t*>(*loc);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    static int RedirectShadowSEH(void** loc, std::uint8_t* shadow, size_t copyBytes)
    {
        __try
        {
            std::uint8_t* stock = static_cast<std::uint8_t*>(*loc);
            if (!stock)
                return 0;
            std::memcpy(shadow, stock, copyBytes);
            *loc = shadow;
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static int StockSlotCount()
    {
        return kGunBasicParameters2StockSlots;
    }

    static bool EnsureGunBasicShadow()
    {
        void** loc = GunBasicPtrLoc();
        if (!loc)
            return false;

        const int stock = StockSlotCount();
        if (stock <= 0 || stock > kGunBasicMaxId)
            return false;

        if (g_GunBasicShadow.empty())
            g_GunBasicShadow.assign(static_cast<size_t>(kGunBasicMaxId) * 12, 0);

        if (ReadPtrSEH(loc) == g_GunBasicShadow.data())
        {
            g_GunBasicShadowActive = true;
            return true;
        }

        if (RedirectShadowSEH(loc, g_GunBasicShadow.data(),
                              static_cast<size_t>(stock) * 12) != 1)
        {
            if (!g_GunBasicShadowActive)
                g_GunBasicShadow.clear();
            return false;
        }

        g_GunBasicShadowActive = true;
        return true;
    }

    static std::uint8_t* BufferBase()
    {
        std::uint8_t* live = ReadPtrSEH(GunBasicPtrLoc());
        if (live)
            return live;
        return static_cast<std::uint8_t*>(
            ResolveGameAddress(gAddr.GunBasicParameters2Buffer));
    }

    static int BufferSlotCount()
    {
        if (g_GunBasicShadowActive)
            return kGunBasicMaxId;
        return StockSlotCount();
    }

    static bool FobHidesVanillaRow(int weaponId)   // FOB: a vanilla weapon's mod row stays out of every lookup and write
    {
        return EquipParam_FobVanillaPartsOn() && weaponId >= 1 && weaponId <= StockSlotCount()
            && !g_ClaimedIds.count(weaponId);
    }

    static int SlotIsZeroSEH(const std::uint8_t* buf, int idx)
    {
        __try
        {
            const std::uint8_t* p = buf + static_cast<size_t>(idx) * 12;
            for (int k = 0; k < 12; ++k)
                if (p[k] != 0)
                    return 0;
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
    }

    static void WriteNativeRowSEH(std::uint8_t* buf, int weaponId, const std::int32_t* f12)
    {
        static const int kByteForField[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 10, 8, 9, 11 };
        __try
        {
            std::uint8_t* p = buf + static_cast<size_t>(weaponId - 1) * 12;
            for (int k = 0; k < 12; ++k)
                p[kByteForField[k]] = static_cast<std::uint8_t>(f12[k] & 0xFF);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    static int CopyRowBytesSEH(const std::uint8_t* src, unsigned char* dst)
    {
        __try
        {
            for (int k = 0; k < 12; ++k)
                dst[k] = src[k];
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static bool GunRowForWeapon(int weaponId, int logicalReceiver, unsigned char gunRow[12])
    {
        const unsigned int family = EquipParam_GetWeaponHandlingFamily(weaponId);
        if (family && EquipParam_GetEquipGunBasicRow(family, gunRow) && gunRow[0] != 0)
            return true;
        const int receiver = EquipParam_GetReceiverMotionFrom(logicalReceiver);
        std::uint8_t* buf = receiver > 0 ? BufferBase() : nullptr;
        for (int weapon = 1; buf && weapon <= StockSlotCount(); ++weapon)   // the first vanilla gun built on that receiver
            if (CopyRowBytesSEH(buf + static_cast<size_t>(weapon - 1) * 12, gunRow) == 1 && gunRow[0] == receiver)
                return true;
        return false;
    }

    static int GunMagazineForRow(int weaponId, int logicalReceiver)
    {
        unsigned char gunRow[12];
        return GunRowForWeapon(weaponId, logicalReceiver, gunRow) ? gunRow[2] : 0;
    }

    static int GunPartForSlot(const unsigned char gunRow[12], int slot)
    {
        static const int kNativeByteForSlot[11] = { 0, 1, 2, 3, 4, 5, 6, 7, 10, 8, 9 };
        return (slot >= 0 && slot < 11) ? gunRow[kNativeByteForSlot[slot]] : 0;
    }

    static int LentPart(const unsigned char* gunRow, int slot, int logicalPart)
    {
        static constexpr int kScope1Slot = 6;
        static constexpr int kScope2Slot = 7;
        if (slot == kScope1Slot || slot == kScope2Slot)
        {
            const int scopeDonor = EquipParam_GetSightScopeDonor(logicalPart);
            if (scopeDonor > 0)
                return scopeDonor;
        }
        return gunRow ? GunPartForSlot(gunRow, slot) : 0;
    }

    static int BorrowGunPart(int weaponId, int logicalReceiver, int slot, int logicalPart)
    {
        if (!PartIdWiden_IsPlanned())
            return 0;
        unsigned char gunRow[12];
        const bool haveGun = GunRowForWeapon(weaponId, logicalReceiver, gunRow);
        return LentPart(haveGun ? gunRow : nullptr, slot, logicalPart);
    }

    static void ReapplyAllNative()
    {
        std::vector<GunBasicRow> snapshot;
        {
            std::lock_guard<std::recursive_mutex> lock(g_Mutex);
            EnsureGunBasicShadow();
            snapshot = g_Rows;
        }

        std::uint8_t* buf = BufferBase();
        const int cap = BufferSlotCount();
        if (!buf || cap <= 0)
            return;

        for (const GunBasicRow& row : snapshot)
        {
            if (row.f[0] >= 1 && row.f[0] <= cap && !FobHidesVanillaRow(row.f[0]))
                WriteNativeRowSEH(buf, row.f[0], &row.f[1]);
        }
    }

    static int __fastcall hkReloadEquipParameterTables2(lua_State* L)
    {
        const int resultCount = g_OrigReload ? g_OrigReload(L) : 0;
        ReapplyAllNative();
        return resultCount;
    }

    static bool ReadNamedInt(lua_State* L, int tableIdx, const char* name, int& out)
    {
        g_lua_getfield(L, tableIdx, const_cast<char*>(name));
        const bool ok = g_lua_isnumber(L, -1) != 0;
        if (ok)
            out = static_cast<int>(g_lua_tointeger(L, -1));
        g_lua_settop(L, -2);
        return ok;
    }

    static bool ReadNamedFlag(lua_State* L, int tableIdx, const char* name)
    {
        g_lua_getfield(L, tableIdx, const_cast<char*>(name));
        bool on = false;
        const int t = g_lua_type(L, -1);
        if (t == LUA_TBOOLEAN)
            on = g_lua_toboolean && g_lua_toboolean(L, -1) != 0;
        else if (g_lua_isnumber(L, -1))
            on = g_lua_tointeger(L, -1) != 0;
        g_lua_settop(L, -2);
        return on;
    }
}

int GunBasic_AllocateWeaponIdForName(const char* name)
{
    if (!name || !name[0])
        return 0;

    std::lock_guard<std::recursive_mutex> lock(g_Mutex);

    auto it = g_WpNameToId.find(name);
    if (it != g_WpNameToId.end())
        return it->second;

    if (!g_PersistedReserved)
    {
        g_PersistedReserved = true;
        V_FrameWorkState::ForEachPersistedConstant("WP",
            [](const char* reservedName, std::int32_t v)
            {
                static_cast<void>(reservedName);
                if (v >= 2 && v <= kGunBasicMaxId)
                    g_ReservedIds.insert(static_cast<int>(v));
            });
    }

    EnsureGunBasicShadow();

    std::uint8_t* buf = BufferBase();
    const int cap = BufferSlotCount();
    if (!buf || cap <= 1)
        return 0;

    const int persisted = V_FrameWorkState::GetPersistedConstant("WP", name);
    if (persisted >= 2 && persisted <= cap &&
        !g_ClaimedIds.count(persisted) &&
        SlotIsZeroSEH(buf, persisted - 1) == 1)
    {
        g_ReservedIds.erase(persisted);
        g_ClaimedIds.insert(persisted);
        g_WpNameToId[name] = persisted;
        return persisted;
    }
    if (persisted != 0)
        LogWarn("GunBasic: saved weaponId %d for '%s' is taken, reallocating", persisted, name);

    const int packedHi = (cap < kPackedSubIdMax) ? cap : kPackedSubIdMax;
    for (int pass = 0; pass < 2; ++pass)
    {
        const int from = (pass == 0) ? packedHi - 1 : cap - 1;
        const int to   = (pass == 0) ? 1 : packedHi;
        if (from < to)
            continue;

        for (int idx = from; idx >= to; --idx)
        {
            const int weaponId = idx + 1;
            if (g_ClaimedIds.count(weaponId) || g_ReservedIds.count(weaponId))
                continue;
            if (SlotIsZeroSEH(buf, idx) != 1)
                continue;

            g_ClaimedIds.insert(weaponId);
            g_WpNameToId[name] = weaponId;
            V_FrameWorkState::SetPersistedConstant("WP", name, weaponId);
            return weaponId;
        }
    }

    LogError("GunBasic: weaponIds exhausted (%d), '%s' not allocated", cap, name);
    return 0;
}

int GunBasic_MaxWeaponId()
{
    return kGunBasicMaxId;
}

int GunBasic_PackedSubIdMax()
{
    return kPackedSubIdMax;
}

bool GunBasic_ReadRowBytes(int weaponId, unsigned char* out12)
{
    if (weaponId <= 0 || !out12)
        return false;

    std::lock_guard<std::recursive_mutex> lock(g_Mutex);

    std::uint8_t* buf = BufferBase();
    if (!buf || weaponId > BufferSlotCount())
        return false;

    return CopyRowBytesSEH(buf + static_cast<size_t>(weaponId - 1) * 12, out12) == 1;
}

bool GunBasic_WeaponHasNoRow(int equipId)
{
    equipId = equip::FromEngine(equipId);
    if (equipId <= 0 || equipId >= kNoRowCacheSize)
        return false;

    const std::uint64_t epoch =
        (static_cast<std::uint64_t>(TppEquip_EquipTableEpoch()) << 32)
        | g_RowEpoch.load(std::memory_order_acquire);
    if (g_NoRowCacheEpoch.load(std::memory_order_acquire) != epoch)
    {
        for (auto& c : g_NoRowCache)
            c.store(0, std::memory_order_relaxed);
        g_NoRowCacheEpoch.store(epoch, std::memory_order_release);
    }

    const std::uint8_t cached = g_NoRowCache[equipId].load(std::memory_order_relaxed);
    if (cached)
        return cached == 2;

    const int weaponId = TppEquip_GetSubIdForEquipId(equipId);
    unsigned char row[12] = {};
    const bool noRow = weaponId > 0 && GunBasic_ReadRowBytes(weaponId, row) && row[0] == 0;
    g_NoRowCache[equipId].store(noRow ? 2 : 1, std::memory_order_relaxed);

    if (noRow)
    {
        const std::uint64_t bit = 1ull << (equipId & 63);
        if ((g_NoRowLogged[equipId >> 6].fetch_or(bit) & bit) == 0)
            LogWarn("SetGunBasic: equipId=%d (weaponId=%d) has no gunBasic row, kept out of R&D",
                equipId, weaponId);
    }
    return noRow;
}

unsigned char* GunBasic_RowForFob(int weaponId)
{
    std::uint8_t* buf = BufferBase();
    if (!buf || weaponId <= 0 || weaponId > StockSlotCount() || weaponId > BufferSlotCount())
        return nullptr;
    return buf + static_cast<size_t>(weaponId - 1) * 12;
}

bool GunBasic_WeaponNeedsLaneBind(int weaponId)
{
    std::lock_guard<std::recursive_mutex> lock(g_Mutex);
    if (FobHidesVanillaRow(weaponId))
        return false;
    for (const auto& r : g_Rows)
    {
        if (r.f[0] != weaponId)
            continue;
        for (int i = 1; i <= 11; ++i)
        {
            if (r.slotSpace[i] < 0 || r.logical[i] < 0x100)
                continue;
            const int alias = EquipParam_GetWideAlias(r.slotSpace[i], r.logical[i]);
            if (alias <= 0 || r.f[i] != alias)
                return true;
        }
        return false;
    }
    return false;
}

// space-filtered on purpose: barrels/sights/etc are player-swappable in the
// gunsmith, so the desc can legitimately hold a customised part there and must
// not be overwritten. Only slots this build actually defers may be restamped.
static int RowByteSEH(const std::uint8_t* row, int offset)
{
    __try
    {
        return row[offset];
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
}

int GunBasic_LogicalMagazineForRow(const std::uint8_t* row)
{
    const int narrow = row ? RowByteSEH(row, 2) : 0;
    if (narrow == 0)
        return 0;

    std::lock_guard<std::recursive_mutex> lock(g_Mutex);
    const std::uint8_t* buf = BufferBase();
    if (!buf || row < buf)
        return narrow;
    const std::size_t offset = static_cast<std::size_t>(row - buf);
    if (offset % 12 != 0 || offset / 12 >= static_cast<std::size_t>(BufferSlotCount()))
        return narrow;
    const int weaponId = static_cast<int>(offset / 12) + 1;
    if (FobHidesVanillaRow(weaponId))
        return narrow;
    for (const auto& r : g_Rows)
    {
        if (r.f[0] != weaponId)
            continue;
        for (int i = 1; i <= 11; ++i)
            if (r.slotSpace[i] == kVanillaSpace_Magazine)
                return (r.logical[i] >= 0x100 && r.f[i] == narrow) ? r.logical[i] : narrow;
        return narrow;
    }
    return narrow;
}

int GunBasic_CustomMuzzleOptionForRow(const std::uint8_t* row)
{
    const int narrow = row ? RowByteSEH(row, 5) : 0;
    if (narrow == 0)
        return 0;

    std::lock_guard<std::recursive_mutex> lock(g_Mutex);
    const std::uint8_t* buf = BufferBase();
    if (!buf || row < buf)
        return 0;
    const std::size_t offset = static_cast<std::size_t>(row - buf);
    if (offset % 12 != 0 || offset / 12 >= static_cast<std::size_t>(BufferSlotCount()))
        return 0;
    const int weaponId = static_cast<int>(offset / 12) + 1;
    if (FobHidesVanillaRow(weaponId))
        return 0;
    for (const auto& r : g_Rows)
    {
        if (r.f[0] != weaponId)
            continue;
        for (int i = 1; i <= 11; ++i)
            if (r.slotSpace[i] == kVanillaSpace_MuzzleOption)
                return (r.logical[i] >= 0x100 && r.f[i] == narrow) ? r.logical[i] : narrow;
        return narrow;
    }
    return 0;
}

int GunBasic_LogicalMagazineForWeapon(int weaponId, int narrowMagazine)
{
    if (weaponId <= 0 || narrowMagazine <= 0)
        return narrowMagazine;
    std::lock_guard<std::recursive_mutex> lock(g_Mutex);
    if (FobHidesVanillaRow(weaponId))
        return narrowMagazine;
    for (const auto& r : g_Rows)
    {
        if (r.f[0] != weaponId)
            continue;
        for (int i = 1; i <= 11; ++i)
            if (r.slotSpace[i] == kVanillaSpace_Magazine)
                return (r.logical[i] >= 0x100 && r.f[i] == narrowMagazine) ? r.logical[i] : narrowMagazine;
        return narrowMagazine;
    }
    return narrowMagazine;
}

int GunBasic_GetLogicalPart(int weaponId, int space)
{
    std::lock_guard<std::recursive_mutex> lock(g_Mutex);
    if (FobHidesVanillaRow(weaponId))
        return 0;
    for (const auto& r : g_Rows)
    {
        if (r.f[0] != weaponId)
            continue;
        for (int i = 1; i <= 11; ++i)
            if (r.slotSpace[i] == space && r.logical[i] > 0)
                return r.logical[i];
        return 0;
    }
    return 0;
}

bool GunBasic_GetLogicalParts(int weaponId, int out11[11], const unsigned char* nativeDesc)
{
    static const int descLanes[11] = { 0, 1, 2, 3, 4, 5, 6, 7, 10, 8, 9 };
    for (int i = 0; i < 11; ++i)
        out11[i] = nativeDesc ? nativeDesc[descLanes[i]] : 0;

    std::lock_guard<std::recursive_mutex> lock(g_Mutex);
    if (FobHidesVanillaRow(weaponId))
        return false;
    for (const auto& r : g_Rows)
    {
        if (r.f[0] != weaponId)
            continue;
        for (int i = 1; i <= 11; ++i)
        {
            const int slot = i - 1;
            if (slot < 0 || slot >= 11)
                continue;
            if (!nativeDesc || nativeDesc[descLanes[slot]] == r.f[i])
                out11[slot] = (r.logical[i] > 0) ? r.logical[i] : r.f[i];
        }
        return true;
    }
    return false;
}

int GunBasic_GetWideSlotLanes(int weaponId, int space, unsigned char outLanes[12])
{
    for (int i = 0; i < 12; ++i)
        outLanes[i] = 0;

    std::lock_guard<std::recursive_mutex> lock(g_Mutex);
    if (FobHidesVanillaRow(weaponId))
        return 0;
    for (const auto& r : g_Rows)
    {
        if (r.f[0] != weaponId)
            continue;
        int n = 0;
        for (int i = 1; i <= 11; ++i)
        {
            if (r.slotSpace[i] != space || r.logical[i] < 0x100)
                continue;
            if (r.f[i] <= 0 || r.f[i] > 0xFF)
                continue;
            outLanes[i - 1] = static_cast<unsigned char>(r.f[i]);
            ++n;
        }
        return n;
    }
    return 0;
}

int GunBasic_ClearLaneFromRows(int space, int lane)
{
    if (space < 0 || lane <= 0 || lane > 0xFF)
        return 0;

    std::lock_guard<std::recursive_mutex> lock(g_Mutex);
    const int cap = BufferSlotCount();
    std::uint8_t* buf = BufferBase();

    int cleared = 0;
    for (auto& r : g_Rows)
    {
        bool dirty = false;
        for (int i = 1; i <= 11; ++i)
        {
            if (r.slotSpace[i] != space || r.logical[i] < 0x100)
                continue;
            if (r.f[i] != lane)
                continue;
            const int donor = EquipParam_GetWideReceiverDonor(r.logical[i]);
            r.f[i] = (donor > 0) ? donor : kEssentialDefaultId;
            dirty = true;
            ++cleared;
        }
        if (dirty && buf && r.f[0] >= 1 && r.f[0] <= cap && !FobHidesVanillaRow(r.f[0]))
            WriteNativeRowSEH(buf, r.f[0], &r.f[1]);
    }
    return cleared;
}

int GunBasic_ReNarrowReceiverRows(int wideRc)
{
    if (wideRc < 0x100)
        return 0;

    std::lock_guard<std::recursive_mutex> lock(g_Mutex);
    const int donor = EquipParam_GetWideReceiverDonor(wideRc);
    if (donor <= 0)
        return 0;

    const int cap = BufferSlotCount();
    std::uint8_t* buf = BufferBase();

    int changed = 0;
    for (auto& r : g_Rows)
    {
        bool dirty = false;
        for (int i = 1; i <= 11; ++i)
        {
            if (r.slotSpace[i] != kVanillaSpace_Receiver || r.logical[i] != wideRc)
                continue;
            if (r.f[i] == donor)
                continue;
            r.f[i] = donor;
            dirty = true;
            ++changed;
        }
        if (dirty && buf && r.f[0] >= 1 && r.f[0] <= cap && !FobHidesVanillaRow(r.f[0]))
            WriteNativeRowSEH(buf, r.f[0], &r.f[1]);
    }
    return changed;
}

int GunBasic_RefreshBorrowedParts(int ammoId, int receiverId, int weaponId)
{
    const int vanillaMagazines = EquipParam_VanillaPartCount(kVanillaSpace_Magazine);
    const bool borrowMagazines = EquipParam_WideMagazinesPlanned();
    const bool borrowParts = PartIdWiden_IsPlanned();

    std::lock_guard<std::recursive_mutex> lock(g_Mutex);
    std::uint8_t* buf = BufferBase();
    const int cap = BufferSlotCount();
    int changed = 0;
    for (auto& r : g_Rows)
    {
        if ((ammoId && r.logical[3] != ammoId) || (receiverId && r.logical[1] != receiverId)
            || (weaponId && r.f[0] != weaponId))
            continue;
        unsigned char gunRow[12] = {};
        const bool haveGun = GunRowForWeapon(r.f[0], r.logical[1], gunRow);
        bool dirty = false;
        for (int i = 2; i <= 11; ++i)
        {
            const int space = r.slotSpace[i];
            const int logical = r.logical[i];
            if (space < 0)
                continue;
            if (space == kVanillaSpace_Magazine)
            {
                if (logical <= vanillaMagazines)
                    continue;
                if (logical < 0x100)
                {
                    if (haveGun && gunRow[2] > 0)
                        EquipParam_InheritPartMotionTypes(0, 0, logical, gunRow[2], 0, 0, nullptr);
                    continue;
                }
                const int lane = borrowMagazines
                    ? EquipParam_MagazineLaneForWeapon(logical, haveGun ? gunRow[2] : 0) : 0;
                if (lane > 0 && lane != r.f[i])
                {
                    r.f[i] = lane;
                    dirty = true;
                }
                continue;
            }
            if (!borrowParts || logical < 0x100 || EquipParam_GetWideAlias(space, logical) > 0)
                continue;
            const int lent = LentPart(haveGun ? gunRow : nullptr, i - 1, logical);
            if (lent > 0 && lent != r.f[i])
            {
                r.f[i] = lent;
                dirty = true;
            }
        }
        if (!dirty)
            continue;
        if (buf && r.f[0] >= 1 && r.f[0] <= cap && !FobHidesVanillaRow(r.f[0]))
            WriteNativeRowSEH(buf, r.f[0], &r.f[1]);
        ++changed;
    }
    if (changed)
        g_RowEpoch.fetch_add(1, std::memory_order_release);
    return changed;
}

int GunBasic_RebindWidePartsForWeapon(int weaponId)
{
    std::lock_guard<std::recursive_mutex> lock(g_Mutex);

    GunBasicRow* row = nullptr;
    for (auto& r : g_Rows)
        if (r.f[0] == weaponId)
        {
            row = &r;
            break;
        }
    if (!row)
        return 0;

    int rebound = 0;
    for (int i = 1; i <= 11; ++i)
    {
        if (row->slotSpace[i] < 0 || row->logical[i] < 0x100)
            continue;
        const int b = EquipParam_ResolvePartByte(row->slotSpace[i], row->logical[i]);
        if (b > 0 && b != row->f[i])
        {
            row->f[i] = b;
            ++rebound;
        }
    }

    if (rebound)
    {
        const int cap = BufferSlotCount();
        std::uint8_t* buf = BufferBase();
        if (buf && weaponId >= 1 && weaponId <= cap && !FobHidesVanillaRow(weaponId))
            WriteNativeRowSEH(buf, weaponId, &row->f[1]);
    }
    return rebound;
}

static void CommitGunBasicRow(GunBasicRow& row)
{
    const int weaponId = row.f[0];
    for (int i = 1; i <= 12; ++i)
    {
        if (row.f[i] > 0xFF)
        {
            LogWarn("SetGunBasic weaponId=%d: field #%d value %d over 255, truncated",
                weaponId, i, row.f[i]);
        }
    }
    const int cap = BufferSlotCount();
    std::uint8_t* buf = BufferBase();

    std::lock_guard<std::recursive_mutex> lock(g_Mutex);
    bool replaced = false;
    for (auto& existing : g_Rows)
    {
        if (existing.f[0] == row.f[0])
        {
            existing = row;
            replaced = true;
            break;
        }
    }
    if (!replaced)
        g_Rows.push_back(row);

    if (buf && weaponId >= 1 && weaponId <= cap)
    {
        const bool vanillaRow = weaponId <= StockSlotCount()
            && !g_ClaimedIds.count(weaponId);
        const std::uint8_t* rowPtr =
            buf + static_cast<size_t>(weaponId - 1) * 12;
        if (vanillaRow)
            EquipParam_VanillaPreWrite(kVanillaSpace_Weapon, weaponId,
                                       rowPtr, 12);
        WriteNativeRowSEH(buf, weaponId, &row.f[1]);
        if (vanillaRow)
            EquipParam_VanillaPostWrite(kVanillaSpace_Weapon, weaponId,
                                        rowPtr, 12);
        g_RowEpoch.fetch_add(1, std::memory_order_release);
    }
    else
    {
        LogWarn("SetGunBasic weaponId=%d: out of range 1..%d, not written; declare it with DeclareWPs",
            weaponId, cap);
    }

    const int donorRc = EquipParam_GetWideReceiverDonor(row.f[1]);
    const int gunMagazine = GunMagazineForRow(row.f[0], row.logical[1]);
    if (buf && ((donorRc > 0 && donorRc != row.f[1]) || gunMagazine > 0))
    {
        const int stock = StockSlotCount();
        int donorBa = 0, donorAm = 0, donorSt = 0;
        for (int id = 1; donorRc > 0 && donorRc != row.f[1] && id <= stock && id <= cap; ++id)
        {
            const std::uint8_t* r =
                buf + static_cast<size_t>(id - 1) * 12;
            if (r[0] != static_cast<std::uint8_t>(donorRc))
                continue;
            donorBa = r[1];
            donorAm = r[2];
            donorSt = r[3];
            break;
        }
        if (gunMagazine > 0)
            donorAm = gunMagazine;
        int eligible = 0;
        const int applied = EquipParam_InheritPartMotionTypes(
            row.f[2], donorBa, row.f[3], donorAm, row.f[4], donorSt, &eligible);
        if (applied < eligible)
            LogWarn("SetGunBasic weaponId=%d: part anim type table unavailable, %d custom part(s) stay frozen",
                weaponId, eligible - applied);
    }
}

int GunBasic_ReplayPendingForMagazine(int ammoId)
{
    if (ammoId < 0x100)
        return 0;

    std::vector<GunBasicRow> due;
    {
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        for (size_t i = 0; i < g_PendingRows.size();)
        {
            if (g_PendingRows[i].slotSpace[3] == kVanillaSpace_Magazine
                && g_PendingRows[i].logical[3] == ammoId)
            {
                due.push_back(g_PendingRows[i]);
                g_PendingRows.erase(g_PendingRows.begin()
                                    + static_cast<std::ptrdiff_t>(i));
            }
            else
                ++i;
        }
    }

    int committed = 0;
    for (GunBasicRow& row : due)
    {
        for (int i = 1; i <= 11; ++i)
        {
            if (i == 3 || row.slotSpace[i] < 0 || row.logical[i] < 0x100)
                continue;
            if (row.slotSpace[i] == kVanillaSpace_Receiver)
            {
                const int donor = EquipParam_GetWideReceiverDonor(row.logical[i]);
                row.f[i] = (donor > 0) ? donor : kEssentialDefaultId;
                continue;
            }
            int b = EquipParam_ResolvePartByte(row.slotSpace[i], row.logical[i]);
            if (b <= 0)
                b = BorrowGunPart(row.f[0], row.logical[1], i - 1, row.logical[i]);
            if (b > 0)
                row.f[i] = b;
        }
        row.f[3] = EquipParam_WideMagazinesPlanned()
            ? EquipParam_MagazineLaneForWeapon(ammoId, GunMagazineForRow(row.f[0], row.logical[1]))
            : EquipParam_ResolvePartByte(kVanillaSpace_Magazine, ammoId);
        if (row.f[3] <= 0)
        {
            LogWarn("SetGunBasic weaponId=%d: ammoId=%d has no engine lane, rejected",
                row.f[0], ammoId);
            continue;
        }
        ReceiverMotion_NoteGunBasicRow(static_cast<unsigned>(row.f[0]), row.logical[1]);
        CommitGunBasicRow(row);
        ++committed;
    }
    return committed;
}

int GunBasic_PendingWideAmmoForWeapon(int weaponId)
{
    std::lock_guard<std::recursive_mutex> lock(g_Mutex);
    for (const auto& r : g_PendingRows)
        if (r.f[0] == weaponId)
            return r.logical[3];
    return 0;
}

int __cdecl l_SetGunBasic(lua_State* L)
{
    if (!ResolveLuaApi() || MissionCodeGuard::ShouldBypassHooks())
        return 0;

    if (g_lua_type(L, 1) != LUA_TTABLE)
    {
        LogWarn("SetGunBasic: argument #1 must be a table");
        return 0;
    }

    int weaponId = 0;
    if (!ReadNamedInt(L, 1, "weaponId", weaponId) || weaponId <= 0)
    {
        LogWarn("SetGunBasic: missing or invalid weaponId");
        return 0;
    }

    {
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        for (size_t i = 0; i < g_PendingRows.size();)
        {
            if (g_PendingRows[i].f[0] == weaponId)
                g_PendingRows.erase(g_PendingRows.begin()
                                    + static_cast<std::ptrdiff_t>(i));
            else
                ++i;
        }
    }

    GunBasicRow row;
    row.f[0] = weaponId;

    static const char* const kSlotNames[11] =
    {
        "receiverId", "barrelId", "ammoId", "stockId", "muzzleId",
        "muzzleOptionId", "scope1Id", "scope2Id", "underBarrelId",
        "laserFlash1Id", "laserFlash2Id"
    };

    static const char* const kEssentialLabel[3] = { "receiverId", "barrelId", "ammoId" };
    bool noBarrelDeclared = false;
    for (int i = 0; i < 11; ++i)
    {
        int v = kNoneValue;
        const bool present = ReadNamedInt(L, 1, kSlotNames[i], v);
        row.f[i + 1] = v;
        const bool explicitNoBarrel = (i == 1 && present && v == 0);
        if (explicitNoBarrel)
            noBarrelDeclared = true;
        if (i < 3 && !explicitNoBarrel && (!present || v <= 0))
        {
            if (i == 2)
            {
                LogWarn("SetGunBasic weaponId=%d: ammoId missing or invalid (%d), rejected",
                    weaponId, present ? v : 0);
                return 0;
            }
            row.f[i + 1] = kEssentialDefaultId;
            LogWarn("SetGunBasic weaponId=%d: %s missing, using vanilla id %d",
                weaponId, kEssentialLabel[i], kEssentialDefaultId);
        }
    }

    int grade = 1;
    ReadNamedInt(L, 1, "weaponGrade", grade);
    if (grade < 1)
        grade = 1;
    else if (grade > 15)
        grade = 15;
    row.f[12] = grade;

    static const int kSlotSpaces[11] =
    {
        kVanillaSpace_Receiver, kVanillaSpace_Barrel, kVanillaSpace_Magazine,
        kVanillaSpace_Stock, -1, kVanillaSpace_MuzzleOption,
        kVanillaSpace_Sight, kVanillaSpace_Sight, kVanillaSpace_UnderBarrel,
        kVanillaSpace_Option, kVanillaSpace_Option
    };
    for (int i = 0; i < 13; ++i)
    {
        row.logical[i]   = row.f[i];
        row.slotSpace[i] = -1;
    }
    for (int i = 0; i < 11; ++i)
        row.slotSpace[i + 1] = kSlotSpaces[i];

    for (int i = 0; i < 11; ++i)
    {
        if (kSlotSpaces[i] < 0 || row.f[i + 1] < 0x100)
            continue;
        if (kSlotSpaces[i] == kVanillaSpace_Receiver)
        {
            const int wideRc = row.f[i + 1];
            const int donor  = EquipParam_GetWideReceiverDonor(wideRc);
            row.f[i + 1] = (donor > 0) ? donor : kEssentialDefaultId;
            if (donor <= 0)
                LogWarn("SetGunBasic weaponId=%d: receiverId=%d has no motionFrom, using receiver %d; call SetReceiver first",
                    weaponId, wideRc, kEssentialDefaultId);
            continue;
        }
        if (kSlotSpaces[i] == kVanillaSpace_Magazine && EquipParam_WideMagazinesPlanned())
        {
            row.f[i + 1] = EquipParam_MagazineLaneForWeapon(
                row.f[i + 1], GunMagazineForRow(weaponId, row.logical[1]));
            continue;
        }
        row.f[i + 1] = EquipParam_ResolvePartByte(kSlotSpaces[i], row.f[i + 1]);
        if (row.f[i + 1] <= 0 && kSlotSpaces[i] != kVanillaSpace_Magazine && PartIdWiden_IsPlanned())
        {
            row.f[i + 1] = BorrowGunPart(weaponId, row.logical[1], i, row.logical[i + 1]);
            if (row.f[i + 1] <= 0)
                LogWarn("SetGunBasic weaponId=%d: %s=%d has no engine lane and its gun has no %s to lend, weapon gets none",
                    weaponId, kSlotNames[i], row.logical[i + 1], kSlotNames[i]);
        }
    }

    for (int i = 1; i <= 3; ++i)
    {
        if (row.slotSpace[i] == kVanillaSpace_Receiver
            && row.logical[i] >= 0x100)
            continue;   // deferred to the late bind, not a missing part
        if (i == 2 && noBarrelDeclared)
            continue;
        if (row.f[i] <= 0)
        {
            if (i == 3)
            {
                const int rowState = row.logical[i] >= 0x100
                    ? EquipParam_WidePartRowState(kVanillaSpace_Magazine,
                                                  row.logical[i])
                    : -1;
                if (rowState == 0)
                {
                    {
                        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
                        g_PendingRows.push_back(row);
                    }
                    return 0;
                }
                LogWarn("SetGunBasic weaponId=%d: ammoId=%d %s, rejected",
                    weaponId, row.logical[i],
                    rowState > 0 ? "has no engine lane" : "is not declared (DeclareAMs)");
                return 0;
            }
            LogWarn("SetGunBasic weaponId=%d: %s unresolved, using vanilla id %d",
                weaponId, kEssentialLabel[i - 1], kEssentialDefaultId);
            row.f[i] = kEssentialDefaultId;
        }
    }

    ReceiverMotion_NoteGunBasicRow(static_cast<unsigned>(weaponId), row.logical[1]);
    CommitGunBasicRow(row);
    return 0;
}

bool Install_TppEquip_ReloadEquipParameterTables2_Hook()
{
    void* target = ResolveGameAddress(gAddr.ReloadEquipParameterTables2);
    if (!target)
    {
        LogError("GunBasic: ReloadEquipParameterTables2 address missing, custom gunBasic rows lost on table reload");
        return true;
    }

    const bool ok = CreateAndEnableHook(
        target, &hkReloadEquipParameterTables2,
        reinterpret_cast<void**>(&g_OrigReload));
    if (!ok)
    {
        LogError("GunBasic: ReloadEquipParameterTables2 hook failed, custom gunBasic rows lost on table reload");
    }
    return ok;
}

bool Uninstall_TppEquip_ReloadEquipParameterTables2_Hook()
{
    if (gAddr.ReloadEquipParameterTables2)
        DisableAndRemoveHook(ResolveGameAddress(gAddr.ReloadEquipParameterTables2));
    g_OrigReload = nullptr;

    std::lock_guard<std::recursive_mutex> lock(g_Mutex);
    g_Rows.clear();
    g_PendingRows.clear();
    g_WpNameToId.clear();
    g_ClaimedIds.clear();
    g_ReservedIds.clear();
    g_PersistedReserved = false;
    g_RowEpoch.fetch_add(1, std::memory_order_release);
    return true;
}
