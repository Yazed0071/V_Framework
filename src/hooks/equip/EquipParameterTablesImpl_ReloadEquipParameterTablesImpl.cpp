#include "pch.h"
#include "EquipParameterTablesImpl_ReloadEquipParameterTablesImpl.h"

#include <Windows.h>
#include <atomic>
#include <cstring>
#include <map>
#include <mutex>

#include "AddressSet.h"
#include "HookUtils.h"
#include "LuaApi.h"
#include "log.h"

namespace
{
    using ReloadEquipParameterTablesImpl_t = int(__fastcall*)(void* tables, lua_State* L);

    constexpr std::size_t   kBlastTablePointer         = 0x98;
    constexpr std::size_t   kSupportWeaponTablePointer = 0xA0;
    constexpr std::uint32_t kVanillaBlastRows          = 49;

    constexpr std::uint8_t kSizePassVanilla[]    = { 0x48, 0x05, 0xD8, 0x02, 0x00, 0x00 };         // add rax, 0x2D8
    constexpr std::uint8_t kAllocPassVanilla[]   = { 0x48, 0x8D, 0x81, 0xD8, 0x02, 0x00, 0x00 };   // lea rax, [rcx+0x2D8]
    constexpr std::uint32_t kSupportWeaponBlockSize = EquipParameterTables::kSupportWeaponRowCount * 8;

    struct SupportWeaponRow
    {
        std::int16_t  p1;
        std::int16_t  p2;
        std::uint16_t ammo;
        std::int8_t   grade;
        std::uint8_t  unused;
    };
    static_assert(sizeof(SupportWeaponRow) == 8, "engine record is 8 bytes");

    struct BlastRow
    {
        std::int16_t effectRange;
        std::int16_t optimalRange;
        std::uint8_t blastFlag;
        std::uint8_t unused;
    };
    static_assert(sizeof(BlastRow) == 6, "engine record is 6 bytes");

    ReloadEquipParameterTablesImpl_t g_Orig   = nullptr;
    void*                            g_Target = nullptr;
    bool                             g_SupportWeaponBlockGrown = false;
    bool                             g_GrownBlockLoaded        = false;

    std::mutex                               g_Mutex;
    std::map<std::uint32_t, SupportWeaponRow> g_CustomSupportWeaponRows;
    std::map<std::uint32_t, BlastRow>         g_CustomBlastRows;
    BlastRow                                  g_BlastTable[EquipParameterTables::kBlastRowCount];
    std::uint32_t                             g_EngineBlastRows = kVanillaBlastRows;
    std::atomic<bool>                         g_BlastTableLive{ false };
    std::atomic<std::int8_t>                  g_DamageTypePlusOne[EquipParameterTables::kSupportWeaponRowCount] = {};
    std::map<std::uint32_t, EquipParameterTables::SupportWeaponBars> g_SupportWeaponBars;

    std::uint8_t* Tables()
    {
        return static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.EquipParameterTablesImpl_Instance));
    }

    template <class Row>
    Row*& TablePointer(std::uint8_t* tables, std::size_t offset)
    {
        return *reinterpret_cast<Row**>(tables + offset);
    }

    std::int16_t ToTenths(float value)
    {
        return static_cast<std::int16_t>(static_cast<long long>(value * 10.0f));
    }

    std::uint8_t BlastFlagByte(int blastFlag)
    {
        switch (blastFlag)
        {
        case 0:  return 0x20;
        case 1:  return 0x1C;
        case 2:  return 0x4B;
        case 3:  return 0x38;
        case 4:  return 0x4D;
        default: return 0;
        }
    }

    bool SiteMatches(std::uintptr_t site, const std::uint8_t* vanilla, std::size_t length)
    {
        const auto* code = static_cast<const std::uint8_t*>(ResolveGameAddress(site));
        return code && std::memcmp(code, vanilla, length) == 0;
    }

    void WriteImmediate(std::uintptr_t site, std::size_t immediateOffset, std::uint32_t value)
    {
        auto* immediate = static_cast<std::uint8_t*>(ResolveGameAddress(site)) + immediateOffset;
        DWORD oldProtect = 0;
        if (!VirtualProtect(immediate, sizeof(value), PAGE_EXECUTE_READWRITE, &oldProtect))
            return;
        std::memcpy(immediate, &value, sizeof(value));
        VirtualProtect(immediate, sizeof(value), oldProtect, &oldProtect);
        FlushInstructionCache(GetCurrentProcess(), immediate, sizeof(value));
    }

    bool GrowSupportWeaponBlock()
    {
        const std::uintptr_t sizePass  = gAddr.EquipParameterTablesImpl_SupportWeaponSizePass;
        const std::uintptr_t allocPass = gAddr.EquipParameterTablesImpl_SupportWeaponAllocPass;
        if (!sizePass || !allocPass)
            return false;
        if (!SiteMatches(sizePass, kSizePassVanilla, sizeof(kSizePassVanilla))
            || !SiteMatches(allocPass, kAllocPassVanilla, sizeof(kAllocPassVanilla)))
            return false;
        WriteImmediate(sizePass, 2, kSupportWeaponBlockSize);
        WriteImmediate(allocPass, 3, kSupportWeaponBlockSize);
        return true;
    }

    void ShrinkSupportWeaponBlock()
    {
        WriteImmediate(gAddr.EquipParameterTablesImpl_SupportWeaponSizePass, 2, 0x2D8);
        WriteImmediate(gAddr.EquipParameterTablesImpl_SupportWeaponAllocPass, 3, 0x2D8);
    }

    void WriteCustomSupportWeaponRows_NoLock(std::uint8_t* tables)
    {
        SupportWeaponRow* engineRows = TablePointer<SupportWeaponRow>(tables, kSupportWeaponTablePointer);
        if (!engineRows || !g_GrownBlockLoaded)
            return;
        for (const auto& kv : g_CustomSupportWeaponRows)
            engineRows[kv.first] = kv.second;
    }

    void PublishBlastTable_NoLock(std::uint8_t* tables)
    {
        BlastRow*& engineRows = TablePointer<BlastRow>(tables, kBlastTablePointer);
        if (!engineRows)
            return;
        if (engineRows != g_BlastTable)
        {
            std::memset(g_BlastTable, 0, sizeof(g_BlastTable));
            const std::uint32_t rows = g_EngineBlastRows < EquipParameterTables::kBlastRowCount
                                           ? g_EngineBlastRows : EquipParameterTables::kBlastRowCount;
            std::memcpy(g_BlastTable, engineRows, rows * sizeof(BlastRow));
        }
        for (const auto& kv : g_CustomBlastRows)
            g_BlastTable[kv.first] = kv.second;
        engineRows = g_BlastTable;
        g_BlastTableLive.store(true, std::memory_order_release);
    }

    std::uint32_t CountBlastRows(lua_State* L)
    {
        std::uint32_t rows = kVanillaBlastRows;
        if (LuaType(L, -1) != LUA_TTABLE)
            return rows;
        LuaGetField(L, -1, "BlastParameter");
        if (LuaType(L, -1) == LUA_TTABLE)
        {
            const std::size_t entries = LuaObjLen(L, -1);
            rows = entries > 0 ? static_cast<std::uint32_t>(entries - 1) : 0;
        }
        LuaPop(L, 1);
        return rows;
    }

    int __fastcall hkReloadEquipParameterTablesImpl(void* tables, lua_State* L)
    {
        const std::uint32_t blastRows = CountBlastRows(L);
        const int result = g_Orig(tables, L);

        std::lock_guard<std::mutex> lock(g_Mutex);
        g_EngineBlastRows  = blastRows;
        g_GrownBlockLoaded = g_SupportWeaponBlockGrown;
        auto* bytes = static_cast<std::uint8_t*>(tables);
        WriteCustomSupportWeaponRows_NoLock(bytes);
        PublishBlastTable_NoLock(bytes);
        return result;
    }
}

namespace EquipParameterTables
{
    void SetSupportWeaponRow(std::uint32_t swpId, int ammo, float p1, float p2, int grade)
    {
        SupportWeaponRow row{};
        row.p1    = ToTenths(p1);
        row.p2    = ToTenths(p2);
        row.ammo  = static_cast<std::uint16_t>(ammo < 1 ? 0x3FFF : (ammo > 0x3FFE ? 0x3FFE : ammo));   // 0x3FFF is infinite
        row.grade = static_cast<std::int8_t>(grade);

        std::lock_guard<std::mutex> lock(g_Mutex);
        g_CustomSupportWeaponRows[swpId] = row;
        if (std::uint8_t* tables = Tables())
            WriteCustomSupportWeaponRows_NoLock(tables);
    }

    void SetBlastRow(std::uint32_t blastId, int blastFlag, float effectRange, float optimalRange)
    {
        BlastRow row{};
        row.effectRange  = ToTenths(effectRange);
        row.optimalRange = ToTenths(optimalRange);
        row.blastFlag    = BlastFlagByte(blastFlag);

        std::lock_guard<std::mutex> lock(g_Mutex);
        g_CustomBlastRows[blastId] = row;
        if (std::uint8_t* tables = Tables())
            PublishBlastTable_NoLock(tables);
    }

    const void* GetBlastRow(std::uint32_t blastId)
    {
        if (blastId >= kBlastRowCount || !g_BlastTableLive.load(std::memory_order_acquire))
            return nullptr;
        return &g_BlastTable[blastId];
    }

    bool SupportWeaponRowsAvailable()
    {
        return g_SupportWeaponBlockGrown;
    }

    bool BlastRowsAvailable()
    {
        return g_Target != nullptr;
    }

    void SetSupportWeaponDamageType(std::uint32_t swpId, int damageType)
    {
        if (swpId >= kSupportWeaponRowCount)
            return;
        const int stored = damageType < 0 ? 0 : damageType + 1;
        g_DamageTypePlusOne[swpId].store(static_cast<std::int8_t>(stored), std::memory_order_relaxed);
    }

    int SupportWeaponDamageType(std::uint32_t swpId)
    {
        if (swpId >= kSupportWeaponRowCount)
            return kAutomaticDamageType;
        return g_DamageTypePlusOne[swpId].load(std::memory_order_relaxed) - 1;
    }

    void SetSupportWeaponBars(std::uint32_t swpId, const SupportWeaponBars& bars)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_SupportWeaponBars[swpId] = bars;
    }

    bool GetSupportWeaponBars(std::uint32_t swpId, SupportWeaponBars& bars)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        const auto it = g_SupportWeaponBars.find(swpId);
        if (it == g_SupportWeaponBars.end())
            return false;
        bars = it->second;
        return true;
    }

    bool SupportWeaponRowValues(std::uint32_t swpId, float& p1, float& p2)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        const auto it = g_CustomSupportWeaponRows.find(swpId);
        if (it == g_CustomSupportWeaponRows.end())
            return false;
        p1 = it->second.p1 * 0.1f;
        p2 = it->second.p2 * 0.1f;
        return true;
    }
}

bool Install_EquipParameterTablesImpl_ReloadEquipParameterTablesImpl_Hook()
{
    g_SupportWeaponBlockGrown = GrowSupportWeaponBlock();
    if (!g_SupportWeaponBlockGrown)
        LogError("SupportWeapon: SupportWeaponParameter stays at 91 rows on this build, custom SWP_ rows are ignored");

    g_Target = ResolveGameAddress(gAddr.EquipParameterTablesImpl_ReloadEquipParameterTablesImpl);
    if (!g_Target)
    {
        LogError("SupportWeapon: ReloadEquipParameterTables address missing for this build, custom BLA_ rows are ignored");
        return true;
    }
    if (!CreateAndEnableHook(g_Target, reinterpret_cast<void*>(&hkReloadEquipParameterTablesImpl),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("SupportWeapon: ReloadEquipParameterTables hook failed, custom SWP_/BLA_ rows are ignored");
        g_Target = nullptr;
        return false;
    }
    return true;
}

void Uninstall_EquipParameterTablesImpl_ReloadEquipParameterTablesImpl_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig   = nullptr;

    std::lock_guard<std::mutex> lock(g_Mutex);
    if (g_SupportWeaponBlockGrown)
        ShrinkSupportWeaponBlock();
    g_SupportWeaponBlockGrown = false;
    g_GrownBlockLoaded        = false;
}
