#include "pch.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

#include "EquipParameters_ReceiverParameter2.h"

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
    #pragma pack(push, 1)
    struct ReceiverParameter2Row
    {
        std::int16_t attackId = 0;
        std::int8_t  receiverParamSetsBase = 0;
        std::int8_t  receiverParamSetsWobbling = 0;
        std::int8_t  receiverParamSetsSystem = 0;
        std::int8_t  receiverParamSetsSound = 0;
    };
    #pragma pack(pop)

    struct ReceiverParameter2Entry
    {
        std::int32_t receiverId = 0;
        std::int16_t attackId = 0;
        std::int8_t  receiverParamSetsBase = 0;
        std::int8_t  receiverParamSetsWobbling = 0;
        std::int8_t  receiverParamSetsSystem = 0;
        std::int8_t  receiverParamSetsSound = 0;
    };

    constexpr std::uint32_t kStockReceiverMaxId = 0xE9;
    constexpr std::size_t kReceiverParameter2RowSize = 0x06;

    std::vector<ReceiverParameter2Entry> g_CustomReceiverParameter2Entries;
    std::mutex g_CustomReceiverParameter2Mutex;

    using ReadReceiverParameter2_t =
        void(__cdecl*)(lua_State* L, int tableIndex, char* fieldName, void* outBuffer);

    static ReadReceiverParameter2_t g_OrigReadReceiverParameter2 = nullptr;
    static bool g_ReadReceiverParameter2HookInstalled = false;

    using lua_getfield_t = void(__fastcall*)(lua_State* L, int idx, char* k);
    using lua_isnumber_t = int(__fastcall*)(lua_State* L, int idx);
    using lua_tointeger_t = long long(__fastcall*)(lua_State* L, int idx);
    using lua_settop_t = void(__fastcall*)(lua_State* L, int idx);
    using lua_pushnumber_t = void(__fastcall*)(lua_State* L, lua_Number n);
    using lua_type_t = int(__fastcall*)(lua_State* L, int idx);

    static lua_getfield_t   g_lua_getfield = nullptr;
    static lua_isnumber_t   g_lua_isnumber = nullptr;
    static lua_tointeger_t  g_lua_tointeger = nullptr;
    static lua_settop_t     g_lua_settop = nullptr;
    static lua_pushnumber_t g_lua_pushnumber = nullptr;
    static lua_type_t       g_lua_type = nullptr;
}

static bool ResolveLuaApi()
{
    if (!g_lua_getfield)
        g_lua_getfield = reinterpret_cast<lua_getfield_t>(ResolveGameAddress(gAddr.lua_getfield));
    if (!g_lua_isnumber)
        g_lua_isnumber = reinterpret_cast<lua_isnumber_t>(ResolveGameAddress(gAddr.lua_isnumber));
    if (!g_lua_tointeger)
        g_lua_tointeger = reinterpret_cast<lua_tointeger_t>(ResolveGameAddress(gAddr.lua_tointeger));
    if (!g_lua_settop)
        g_lua_settop = reinterpret_cast<lua_settop_t>(ResolveGameAddress(gAddr.lua_settop));
    if (!g_lua_pushnumber)
        g_lua_pushnumber = reinterpret_cast<lua_pushnumber_t>(ResolveGameAddress(gAddr.lua_pushnumber));
    if (!g_lua_type)
        g_lua_type = reinterpret_cast<lua_type_t>(ResolveGameAddress(gAddr.lua_type));

    return g_lua_getfield &&
        g_lua_isnumber &&
        g_lua_tointeger &&
        g_lua_settop &&
        g_lua_pushnumber &&
        g_lua_type;
}

static void LuaGetField(lua_State* L, int idx, const char* fieldName)
{
    if (!ResolveLuaApi() || !fieldName)
        return;

    g_lua_getfield(L, idx, const_cast<char*>(fieldName));
}

static bool LuaIsNumber(lua_State* L, int idx)
{
    return ResolveLuaApi() && g_lua_isnumber(L, idx) != 0;
}

static std::int32_t LuaToInt(lua_State* L, int idx)
{
    if (!ResolveLuaApi())
        return 0;

    return static_cast<std::int32_t>(g_lua_tointeger(L, idx));
}

static void LuaSetTop(lua_State* L, int idx)
{
    if (!ResolveLuaApi())
        return;

    g_lua_settop(L, idx);
}

static int LuaType(lua_State* L, int idx)
{
    if (!ResolveLuaApi())
        return LUA_TNONE;

    return g_lua_type(L, idx);
}

static bool LuaIsTable(lua_State* L, int idx)
{
    return LuaType(L, idx) == LUA_TTABLE;
}

static bool IsValidReceiverParameter2Entry(const ReceiverParameter2Entry& entry)
{
    return entry.receiverId > 0;
}

static void QueueReceiverParameter2Entry(const ReceiverParameter2Entry& entry)
{
    if (!IsValidReceiverParameter2Entry(entry))
        return;

    std::lock_guard<std::mutex> lock(g_CustomReceiverParameter2Mutex);

    auto it = std::find_if(
        g_CustomReceiverParameter2Entries.begin(),
        g_CustomReceiverParameter2Entries.end(),
        [&](const ReceiverParameter2Entry& existing)
        {
            return existing.receiverId == entry.receiverId;
        });

    if (it != g_CustomReceiverParameter2Entries.end())
    {
        *it = entry;

        Log("[ReceiverParameter2] Updated queued entry receiverId=0x%X attackId=%d base=%d wobbling=%d system=%d sound=%d\n",
            entry.receiverId,
            static_cast<int>(entry.attackId),
            static_cast<int>(entry.receiverParamSetsBase),
            static_cast<int>(entry.receiverParamSetsWobbling),
            static_cast<int>(entry.receiverParamSetsSystem),
            static_cast<int>(entry.receiverParamSetsSound));
        return;
    }

    g_CustomReceiverParameter2Entries.push_back(entry);

    Log("[ReceiverParameter2] Queued new entry receiverId=0x%X attackId=%d base=%d wobbling=%d system=%d sound=%d\n",
        entry.receiverId,
        static_cast<int>(entry.attackId),
        static_cast<int>(entry.receiverParamSetsBase),
        static_cast<int>(entry.receiverParamSetsWobbling),
        static_cast<int>(entry.receiverParamSetsSystem),
        static_cast<int>(entry.receiverParamSetsSound));
}

static void ApplyQueuedReceiverParameter2(void* outBuffer)
{
    if (!outBuffer)
        return;

    std::lock_guard<std::mutex> lock(g_CustomReceiverParameter2Mutex);

    for (const auto& entry : g_CustomReceiverParameter2Entries)
    {
        if (entry.receiverId <= 0)
            continue;

        const std::uint32_t index = static_cast<std::uint32_t>(entry.receiverId - 1);
        auto* row = reinterpret_cast<ReceiverParameter2Row*>(
            reinterpret_cast<std::uint8_t*>(outBuffer) + index * kReceiverParameter2RowSize);

        row->attackId = entry.attackId;
        row->receiverParamSetsBase = entry.receiverParamSetsBase;
        row->receiverParamSetsWobbling = entry.receiverParamSetsWobbling;
        row->receiverParamSetsSystem = entry.receiverParamSetsSystem;
        row->receiverParamSetsSound = entry.receiverParamSetsSound;

        Log("[ReceiverParameter2] Applied receiverId=0x%X => {attackId=%d, base=%d, wobbling=%d, system=%d, sound=%d}\n",
            entry.receiverId,
            static_cast<int>(entry.attackId),
            static_cast<int>(entry.receiverParamSetsBase),
            static_cast<int>(entry.receiverParamSetsWobbling),
            static_cast<int>(entry.receiverParamSetsSystem),
            static_cast<int>(entry.receiverParamSetsSound));
    }
}

static void __cdecl hkReadReceiverParameter2(lua_State* L, int tableIndex, char* fieldName, void* outBuffer)
{
    if (g_OrigReadReceiverParameter2)
        g_OrigReadReceiverParameter2(L, tableIndex, fieldName, outBuffer);

    ApplyQueuedReceiverParameter2(outBuffer);
}



int __cdecl l_SetReceiverParameter2(lua_State* L)
{
    if (!L || !LuaIsTable(L, 1))
        return 0;

    ReceiverParameter2Entry entry{};

    LuaGetField(L, 1, "receiverId");
    if (!LuaIsNumber(L, -1))
    {
        LuaSetTop(L, -2);
        Log("[ReceiverParameter2] receiverId is required\n");
        return 0;
    }
    entry.receiverId = LuaToInt(L, -1);
    LuaSetTop(L, -2);

    if (entry.receiverId <= 0)
    {
        Log("[ReceiverParameter2] receiverId must be > 0\n");
        return 0;
    }

    LuaGetField(L, 1, "attackId");
    entry.attackId = static_cast<std::int16_t>(LuaIsNumber(L, -1) ? LuaToInt(L, -1) : 0);
    LuaSetTop(L, -2);

    LuaGetField(L, 1, "receiverParamSetsBase");
    entry.receiverParamSetsBase = static_cast<std::int8_t>(LuaIsNumber(L, -1) ? LuaToInt(L, -1) : 0);
    LuaSetTop(L, -2);

    LuaGetField(L, 1, "receiverParamSetsWobbling");
    entry.receiverParamSetsWobbling = static_cast<std::int8_t>(LuaIsNumber(L, -1) ? LuaToInt(L, -1) : 0);
    LuaSetTop(L, -2);

    LuaGetField(L, 1, "receiverParamSetsSystem");
    entry.receiverParamSetsSystem = static_cast<std::int8_t>(LuaIsNumber(L, -1) ? LuaToInt(L, -1) : 0);
    LuaSetTop(L, -2);

    LuaGetField(L, 1, "receiverParamSetsSound");
    entry.receiverParamSetsSound = static_cast<std::int8_t>(LuaIsNumber(L, -1) ? LuaToInt(L, -1) : 0);
    LuaSetTop(L, -2);

    QueueReceiverParameter2Entry(entry);
    return 0;
}

bool Install_ReadReceiverParameter2_Hook()
{
    if (g_ReadReceiverParameter2HookInstalled)
        return true;

    if (!ResolveLuaApi())
    {
        Log("[ReceiverParameter2] Lua API resolve failed during install\n");
        return false;
    }

    void* target = ResolveGameAddress(gAddr.ReadReceiverParameter2);
    if (!target)
    {
        Log("[ReceiverParameter2] ReadReceiverParameter2 target resolve failed\n");
        return false;
    }

    if (!CreateAndEnableHook(
        target,
        reinterpret_cast<void*>(&hkReadReceiverParameter2),
        reinterpret_cast<void**>(&g_OrigReadReceiverParameter2)))
    {
        Log("[ReceiverParameter2] hook install failed\n");
        return false;
    }

    g_ReadReceiverParameter2HookInstalled = true;
    Log("[ReceiverParameter2] hook installed\n");
    return true;
}

bool Uninstall_ReadReceiverParameter2_Hook()
{
    if (!g_ReadReceiverParameter2HookInstalled)
        return true;

    void* target = ResolveGameAddress(gAddr.ReadReceiverParameter2);
    if (target)
        DisableAndRemoveHook(target);

    g_OrigReadReceiverParameter2 = nullptr;
    g_ReadReceiverParameter2HookInstalled = false;

    Log("[ReceiverParameter2] hook removed\n");
    return true;
}