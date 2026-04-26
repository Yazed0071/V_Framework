#include "pch.h"

#include <array>
#include <cstdint>
#include <mutex>

#include "AddressSet.h"
#include "HookUtils.h"
#include "InitCamoufTable.h"
#include "log.h"

namespace
{
    using CamoRow   = std::array<std::int32_t, CamoufTable::kMaxMaterialTypes>;
    using CamoTable = std::array<CamoRow, CamoufTable::kMaxCamoTypes>;

    static CamoTable            g_Table{};
    static bool                 g_Dirty       = false;
    static std::mutex           g_Mutex;

    static CamoufTable::Deps    g_Deps{};
    static bool                 g_HasDeps     = false;

    // CamoSystemObject's vtable[1] is a function taking (self, lua_State*)
    // that reads the Lua table at the top of stack and applies it to the
    // internal camo parameters.
    using ApplyCamoTable_t = void(__fastcall*)(void* self, lua_State* L);

    // Wrapper that invokes the engine's camo-apply vtable slot under SEH.
    // Extracted into a plain function because MSVC C2712 forbids __try inside
    // any function that also contains a C++ object requiring stack unwinding
    // (PushCamoTableToGame holds a std::lock_guard in a nested block).
    static bool CallApplyCamoTableSeh(ApplyCamoTable_t fn, void* self, lua_State* L)
    {
        __try
        {
            fn(self, L);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static bool IsValidCamoIndex(std::int32_t camoType)
    {
        return camoType >= 0 &&
               static_cast<std::size_t>(camoType) < CamoufTable::kMaxCamoTypes;
    }

    static bool IsValidMaterialIndex(std::int32_t materialType)
    {
        return materialType >= 0 &&
               static_cast<std::size_t>(materialType) < CamoufTable::kMaxMaterialTypes;
    }
}

namespace CamoufTable
{
    void Bind(const Deps& deps)
    {
        g_Deps = deps;
        g_HasDeps = true;
    }

    bool Set_CamoValue(std::int32_t camoType, std::int32_t materialType, std::int32_t value)
    {
        if (!IsValidCamoIndex(camoType) || !IsValidMaterialIndex(materialType))
            return false;

        std::lock_guard<std::mutex> lock(g_Mutex);
        g_Table[camoType][materialType] = value;
        g_Dirty = true;
        return true;
    }

    bool Clone_CamoRow(std::int32_t dstCamoType, std::int32_t srcCamoType)
    {
        if (!IsValidCamoIndex(dstCamoType) || !IsValidCamoIndex(srcCamoType))
            return false;

        std::lock_guard<std::mutex> lock(g_Mutex);
        g_Table[dstCamoType] = g_Table[srcCamoType];
        g_Dirty = true;
        return true;
    }

    bool ImportCamoRow(std::int32_t camoType, const std::int32_t* values, std::size_t count)
    {
        if (!IsValidCamoIndex(camoType) || !values)
            return false;

        std::lock_guard<std::mutex> lock(g_Mutex);
        auto& row = g_Table[camoType];
        const std::size_t n = (count < row.size()) ? count : row.size();
        for (std::size_t i = 0; i < n; ++i)
            row[i] = values[i];
        for (std::size_t i = n; i < row.size(); ++i)
            row[i] = 0;
        g_Dirty = true;
        return true;
    }

    std::int32_t Get_CamoValue(std::int32_t camoType, std::int32_t materialType)
    {
        if (!IsValidCamoIndex(camoType) || !IsValidMaterialIndex(materialType))
            return 0;

        std::lock_guard<std::mutex> lock(g_Mutex);
        return g_Table[camoType][materialType];
    }

    bool HasCustomTable()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        return g_Dirty;
    }

    void Reset()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (auto& row : g_Table)
            row.fill(0);
        g_Dirty = false;
    }

    bool PushCamoTableToGame(lua_State* L)
    {
        if (!L || !g_HasDeps || !g_Dirty)
            return false;
        if (!g_Deps.LuaCreateTable || !g_Deps.LuaPushString ||
            !g_Deps.LuaPushNumber  || !g_Deps.LuaSetTable   ||
            !g_Deps.LuaGetTop      || !g_Deps.LuaSetTop)
        {
            Log("[CamoufTable] push skipped: Deps not bound\n");
            return false;
        }

        const uintptr_t camoSysAddr = gAddr.CamoSystemObject;
        if (camoSysAddr == 0)
        {
            Log("[CamoufTable] push skipped: CamoSystemObject unresolved\n");
            return false;
        }

        void* camoSysPtrSlot = ResolveGameAddress(camoSysAddr);
        if (!camoSysPtrSlot)
            return false;

        void* camoSysObj = *reinterpret_cast<void**>(camoSysPtrSlot);
        if (!camoSysObj)
        {
            Log("[CamoufTable] push skipped: CamoSystemObject is null\n");
            return false;
        }

        const int baseTop = g_Deps.LuaGetTop(L);

        // Build Lua table: [camoType] = { [materialType] = value, ... }
        g_Deps.LuaCreateTable(L, 0, static_cast<int>(kMaxCamoTypes));
        const int outerIdx = g_Deps.LuaGetTop(L);

        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            for (std::size_t c = 0; c < kMaxCamoTypes; ++c)
            {
                g_Deps.LuaPushNumber(L, static_cast<float>(c));

                g_Deps.LuaCreateTable(L, 0, static_cast<int>(kMaxMaterialTypes));
                const int innerIdx = g_Deps.LuaGetTop(L);

                for (std::size_t m = 0; m < kMaxMaterialTypes; ++m)
                {
                    g_Deps.LuaPushNumber(L, static_cast<float>(m));
                    g_Deps.LuaPushNumber(L, static_cast<float>(g_Table[c][m]));
                    g_Deps.LuaSetTable(L, innerIdx);
                }

                g_Deps.LuaSetTable(L, outerIdx);
            }
        }

        // Call vtable[1](self, L).
        auto** vtbl = *reinterpret_cast<void***>(camoSysObj);
        auto  fn    = reinterpret_cast<ApplyCamoTable_t>(vtbl[1]);
        if (!fn)
        {
            g_Deps.LuaSetTop(L, baseTop);
            return false;
        }

        if (!CallApplyCamoTableSeh(fn, camoSysObj, L))
            Log("[CamoufTable] ApplyCamoTable raised SEH; ignoring\n");

        g_Deps.LuaSetTop(L, baseTop);
        Log("[CamoufTable] pushed custom table to engine\n");
        return true;
    }
}
