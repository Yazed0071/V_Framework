#include "pch.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "EquipMotionDataTable.h"

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

extern "C"
{
    #include "lua.h"
    #include "lauxlib.h"
    #include "lualib.h"
}

namespace
{
    // One row in the vanilla MotionDataTable is { equipId, "/path/to.mtar" }.
    struct MotionRow
    {
        std::int32_t equipId = 0;
        std::string  mtarPath;
    };

    using ReloadEquipMotionData_t = int(__fastcall*)(lua_State* L);

    static ReloadEquipMotionData_t g_OrigReloadEquipMotionData = nullptr;
    static bool                    g_HookInstalled             = false;

    static std::vector<MotionRow> g_QueuedRows;
    static std::mutex             g_Mutex;

    using lua_getfield_t    = void(__fastcall*)(lua_State*, int, const char*);
    using lua_gettop_t      = int(__fastcall*)(lua_State*);
    using lua_isnumber_t    = int(__fastcall*)(lua_State*, int);
    using lua_isstring_t    = int(__fastcall*)(lua_State*, int);
    using lua_objlen_t      = std::size_t(__fastcall*)(lua_State*, int);
    using lua_settop_t      = void(__fastcall*)(lua_State*, int);
    using lua_tointeger_t   = long long(__fastcall*)(lua_State*, int);
    using lua_tolstring_t   = const char*(__fastcall*)(lua_State*, int, std::size_t*);
    using lua_type_t        = int(__fastcall*)(lua_State*, int);
    using lua_pushnumber_t  = void(__fastcall*)(lua_State*, lua_Number);
    using lua_pushstring_t  = void(__fastcall*)(lua_State*, const char*);
    using lua_pushvalue_t   = void(__fastcall*)(lua_State*, int);
    using lua_createtable_t = void(__fastcall*)(lua_State*, int, int);
    using lua_rawgeti_t     = void(__fastcall*)(lua_State*, int, int);
    using lua_settable_t    = void(__fastcall*)(lua_State*, int);

    static lua_getfield_t    g_lua_getfield    = nullptr;
    static lua_gettop_t      g_lua_gettop      = nullptr;
    static lua_isnumber_t    g_lua_isnumber    = nullptr;
    static lua_isstring_t    g_lua_isstring    = nullptr;
    static lua_objlen_t      g_lua_objlen      = nullptr;
    static lua_settop_t      g_lua_settop      = nullptr;
    static lua_tointeger_t   g_lua_tointeger   = nullptr;
    static lua_tolstring_t   g_lua_tolstring   = nullptr;
    static lua_type_t        g_lua_type        = nullptr;
    static lua_pushnumber_t  g_lua_pushnumber  = nullptr;
    static lua_pushstring_t  g_lua_pushstring  = nullptr;
    static lua_pushvalue_t   g_lua_pushvalue   = nullptr;
    static lua_createtable_t g_lua_createtable = nullptr;
    static lua_rawgeti_t     g_lua_rawgeti     = nullptr;
    static lua_settable_t    g_lua_settable    = nullptr;
}

static bool ResolveLuaApi()
{
    if (!g_lua_getfield)    g_lua_getfield    = reinterpret_cast<lua_getfield_t>   (ResolveGameAddress(gAddr.lua_getfield));
    if (!g_lua_gettop)      g_lua_gettop      = reinterpret_cast<lua_gettop_t>     (ResolveGameAddress(gAddr.lua_gettop));
    if (!g_lua_isnumber)    g_lua_isnumber    = reinterpret_cast<lua_isnumber_t>   (ResolveGameAddress(gAddr.lua_isnumber));
    if (!g_lua_isstring)    g_lua_isstring    = reinterpret_cast<lua_isstring_t>   (ResolveGameAddress(gAddr.lua_isstring));
    if (!g_lua_objlen)      g_lua_objlen      = reinterpret_cast<lua_objlen_t>     (ResolveGameAddress(gAddr.lua_objlen));
    if (!g_lua_settop)      g_lua_settop      = reinterpret_cast<lua_settop_t>     (ResolveGameAddress(gAddr.lua_settop));
    if (!g_lua_tointeger)   g_lua_tointeger   = reinterpret_cast<lua_tointeger_t>  (ResolveGameAddress(gAddr.lua_tointeger));
    if (!g_lua_tolstring)   g_lua_tolstring   = reinterpret_cast<lua_tolstring_t>  (ResolveGameAddress(gAddr.lua_tolstring));
    if (!g_lua_type)        g_lua_type        = reinterpret_cast<lua_type_t>       (ResolveGameAddress(gAddr.lua_type));
    if (!g_lua_pushnumber)  g_lua_pushnumber  = reinterpret_cast<lua_pushnumber_t> (ResolveGameAddress(gAddr.lua_pushnumber));
    if (!g_lua_pushstring)  g_lua_pushstring  = reinterpret_cast<lua_pushstring_t> (ResolveGameAddress(gAddr.lua_pushstring));
    if (!g_lua_pushvalue)   g_lua_pushvalue   = reinterpret_cast<lua_pushvalue_t>  (ResolveGameAddress(gAddr.lua_pushvalue));
    if (!g_lua_createtable) g_lua_createtable = reinterpret_cast<lua_createtable_t>(ResolveGameAddress(gAddr.lua_createtable));
    if (!g_lua_rawgeti)     g_lua_rawgeti     = reinterpret_cast<lua_rawgeti_t>    (ResolveGameAddress(gAddr.lua_rawgeti));
    if (!g_lua_settable)    g_lua_settable    = reinterpret_cast<lua_settable_t>   (ResolveGameAddress(gAddr.lua_settable));

    return g_lua_getfield && g_lua_gettop && g_lua_isnumber && g_lua_isstring &&
           g_lua_objlen && g_lua_settop && g_lua_tointeger && g_lua_tolstring &&
           g_lua_type && g_lua_pushnumber && g_lua_pushstring && g_lua_pushvalue &&
           g_lua_createtable && g_lua_rawgeti && g_lua_settable;
}

static bool IsTable(lua_State* L, int idx)  { return g_lua_type(L, idx) == LUA_TTABLE; }
static bool IsNumber(lua_State* L, int idx) { return g_lua_isnumber(L, idx) != 0; }
static bool IsString(lua_State* L, int idx) { return g_lua_isstring(L, idx) != 0; }
static void Pop(lua_State* L)               { g_lua_settop(L, -2); }

static int AbsIndex(lua_State* L, int idx)
{
    return idx > 0 ? idx : g_lua_gettop(L) + idx + 1;
}

static std::int32_t ReadArrayInt(lua_State* L, int tableIdx, int i1, std::int32_t dflt = 0)
{
    const int absIdx = AbsIndex(L, tableIdx);
    g_lua_rawgeti(L, absIdx, i1);

    std::int32_t out = dflt;
    if (IsNumber(L, -1))
        out = static_cast<std::int32_t>(g_lua_tointeger(L, -1));

    Pop(L);
    return out;
}

static std::string ReadArrayString(lua_State* L, int tableIdx, int i1)
{
    const int absIdx = AbsIndex(L, tableIdx);
    g_lua_rawgeti(L, absIdx, i1);

    std::string out;
    if (IsString(L, -1))
    {
        std::size_t len = 0;
        if (const char* s = g_lua_tolstring(L, -1, &len))
            out.assign(s, len);
    }

    Pop(L);
    return out;
}

// Queues one row in the DLL-side list, upserting by equipId.
// Params: row
static void QueueRow(const MotionRow& row)
{
    if (row.equipId <= 0 || row.mtarPath.empty())
        return;

    std::lock_guard<std::mutex> lock(g_Mutex);

    auto it = std::find_if(
        g_QueuedRows.begin(),
        g_QueuedRows.end(),
        [&](const MotionRow& existing) { return existing.equipId == row.equipId; });

    if (it != g_QueuedRows.end())
    {
        *it = row;
        Log("[MotionData] Updated queued row equipId=0x%X path=%s\n",
            row.equipId, row.mtarPath.c_str());
        return;
    }

    g_QueuedRows.push_back(row);
    Log("[MotionData] Queued new row equipId=0x%X path=%s\n",
        row.equipId, row.mtarPath.c_str());
}

// Parses the flat Lua payload ({ {equipId, path}, ... }) and queues every row.
// Params: L, rootIdx
static void ParsePayload(lua_State* L, int rootIdx)
{
    const int absRoot = AbsIndex(L, rootIdx);
    const int count   = static_cast<int>(g_lua_objlen(L, absRoot));

    for (int i = 1; i <= count; ++i)
    {
        g_lua_rawgeti(L, absRoot, i);

        if (IsTable(L, -1))
        {
            const int rowIdx = g_lua_gettop(L);

            MotionRow row{};
            row.equipId  = ReadArrayInt   (L, rowIdx, 1);
            row.mtarPath = ReadArrayString(L, rowIdx, 2);

            QueueRow(row);
        }

        Pop(L);
    }
}

// Pushes arg1.MotionDataTable onto the stack and returns its absolute index.
// Caller must Pop once when done. Returns 0 if the field is missing or not a table.
// Params: L
static int PushMotionDataTable(lua_State* L)
{
    if (!IsTable(L, 1))
    {
        Log("[MotionData] Apply skipped: arg #1 is not a table\n");
        return 0;
    }

    g_lua_getfield(L, 1, "MotionDataTable");

    if (!IsTable(L, -1))
    {
        Log("[MotionData] Apply skipped: MotionDataTable field missing or wrong type\n");
        Pop(L);
        return 0;
    }

    return g_lua_gettop(L);
}

// Returns true if any row in the live array already carries the given equipId
// in its field #1, so we don't double-append on repeat reloads.
// Params: L, arrayAbsIdx, equipId
static bool TableContainsEquipId(lua_State* L, int arrayAbsIdx, std::int32_t equipId)
{
    const int rowCount = static_cast<int>(g_lua_objlen(L, arrayAbsIdx));

    for (int i = 1; i <= rowCount; ++i)
    {
        g_lua_rawgeti(L, arrayAbsIdx, i);

        bool match = false;
        if (IsTable(L, -1))
            match = (ReadArrayInt(L, -1, 1, 0) == equipId);

        Pop(L);

        if (match)
            return true;
    }

    return false;
}

// Appends one { equipId, path } row to the MotionDataTable array in place.
// Params: L, arrayAbsIdx, row
static void AppendRow(lua_State* L, int arrayAbsIdx, const MotionRow& row)
{
    const int currentLen = static_cast<int>(g_lua_objlen(L, arrayAbsIdx));
    const int newIdx     = currentLen + 1;

    g_lua_createtable(L, 2, 0);
    const int rowIdx = g_lua_gettop(L);

    // row[1] = equipId
    g_lua_pushnumber(L, 1.0);
    g_lua_pushnumber(L, static_cast<lua_Number>(row.equipId));
    g_lua_settable(L, rowIdx);

    // row[2] = mtarPath
    g_lua_pushnumber(L, 2.0);
    g_lua_pushstring(L, row.mtarPath.c_str());
    g_lua_settable(L, rowIdx);

    // arr[newIdx] = row
    g_lua_pushnumber(L, static_cast<lua_Number>(newIdx));
    g_lua_pushvalue(L, rowIdx);
    g_lua_settable(L, arrayAbsIdx);

    Pop(L);
}

// Applies every queued row into the live MotionDataTable Lua array, skipping
// rows whose equipId is already present.
// Params: L
static void ApplyQueuedRows(lua_State* L)
{
    if (!L || !ResolveLuaApi())
        return;

    std::lock_guard<std::mutex> lock(g_Mutex);

    if (g_QueuedRows.empty())
        return;

    const int arr = PushMotionDataTable(L);
    if (!arr)
        return;

    int appended = 0;
    int skipped  = 0;

    for (const auto& row : g_QueuedRows)
    {
        if (TableContainsEquipId(L, arr, row.equipId))
        {
            ++skipped;
            continue;
        }

        AppendRow(L, arr, row);
        ++appended;
    }

    Pop(L);

    Log("[MotionData] Applied queued rows: appended=%d skipped=%d total=%zu\n",
        appended, skipped, g_QueuedRows.size());
}

// Hook body. Injects queued rows into arg1.MotionDataTable, then calls the
// stock reloader so the engine processes the full (vanilla + custom) array.
// Params: L
static int __fastcall hkReloadEquipMotionData(lua_State* L)
{
    if (L)
        ApplyQueuedRows(L);

    if (g_OrigReloadEquipMotionData)
        return g_OrigReloadEquipMotionData(L);

    return 0;
}

namespace EquipMotionData
{
    int __cdecl Lua_AddEquipMotionDataTable(lua_State* L)
    {
        if (!L || !ResolveLuaApi())
            return 0;

        if (!IsTable(L, 1))
        {
            Log("[MotionData] AddEquipMotionDataTable: arg #1 is not a table\n");
            return 0;
        }

        ParsePayload(L, 1);
        return 0;
    }

    bool Install_EquipMotionDataTableImpl_ReloadEquipMotionData_Hook()
    {
        if (g_HookInstalled)
        {
            Log("[MotionData] Hook already installed\n");
            return true;
        }

        if (!ResolveLuaApi())
        {
            Log("[MotionData] Install failed: Lua API resolve failed\n");
            return false;
        }

        void* target = ResolveGameAddress(
            gAddr.EquipMotionDataTableImpl_ReloadEquipMotionData);

        if (!target)
        {
            Log("[MotionData] Install failed: address not resolved\n");
            return false;
        }

        const bool ok = CreateAndEnableHook(
            target,
            reinterpret_cast<void*>(&hkReloadEquipMotionData),
            reinterpret_cast<void**>(&g_OrigReloadEquipMotionData));

        if (!ok)
        {
            Log("[MotionData] Install failed: CreateAndEnableHook returned false\n");
            return false;
        }

        g_HookInstalled = true;
        Log("[MotionData] Hook installed on ReloadEquipMotionData\n");
        return true;
    }

    bool Uninstall_EquipMotionDataTableImpl_ReloadEquipMotionData_Hook()
    {
        if (!g_HookInstalled)
            return true;

        void* target = ResolveGameAddress(
            gAddr.EquipMotionDataTableImpl_ReloadEquipMotionData);

        if (target)
            DisableAndRemoveHook(target);

        g_OrigReloadEquipMotionData = nullptr;
        g_HookInstalled             = false;

        Log("[MotionData] Hook removed\n");
        return true;
    }
}
