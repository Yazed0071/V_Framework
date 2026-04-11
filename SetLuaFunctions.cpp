#include "pch.h"
extern "C" {
    #include "lua.h"
    #include "lauxlib.h"
    #include "lualib.h"
}

#include <Windows.h>
#include <cstdint>
#include <unordered_set>
#include <mutex>
#include <string>
#include <vector>

#include "HookUtils.h"
#include "log.h"
#include "FoxHashes.h"
#include "UiTextureOverrides.h"
#include "CautionStepNormalTimerHook.h"
#include "PlayerVoiceFpkHook.h"
#include "VIPSleepFaintHook.h"
#include "VIPHoldupHook.h"
#include "VIPRadioHook.h"
#include "State_EnterStandHoldup1.h"
#include "GetVoiceParamWithCallSign.h"
#include "LostHostageHook.h"
#include "StepRadioDiscovery.h"
#include "tpp\gm\soldier\impl\ActionCoreImpl\ActionCoreImpl_UpdateOptCamo.h"
#include "tpp\ui\menu\impl\MbDvcCassetteTapeCallbackImpl\MbDvcCassetteTapeCallbackImpl_PlayOrPauseSelectedTrack.h"
#include "tpp\sd\SoundMusicPlayer\GetTapeTrackDirectPlayId.h"
#include <tpp\sd\impl\BeginSoundSystem\SoundSystemImpl_BeginSoundSystem.h>
#include "tpp\sd\SoundMusicPlayer\SoundMusicPlayer_SetupMusicInfos.h"
#include <tpp\sd\SoundMusicPlayer\CustomTapeOwnership.h>
#include <tpp\gm\pickable\TppPickableRuntime.h>

#include "AddressSet.h"
#include <tpp\gm\impl\equip\`anonymous_namespace'\RegisterConstantEquipId.h>
#include "tpp\gm\impl\equip\`anonymous_namespace'\DeclareWPs.h"
#include "tpp\gm\impl\equip\`anonymous_namespace'\EquipParameters_GunBasic.h"
#include <tpp\gm\impl\equip\`anonymous_namespace'\EquipIdTable_AddToEquipIdTable.h>
#include <tpp\gm\impl\equip\`anonymous_namespace'\EquipDevelop_AddToEquipDevelopTable.h>
#include <tpp\gm\impl\equip\`anonymous_namespace'\DeclareSWPs.h>
#include <tpp\gm\impl\equip\`anonymous_namespace'\SetSupportWeaponTypeId.h>
#include "tpp\gm\impl\equip\`anonymous_namespace'\DeclareRCs.h"
#include <tpp\gm\impl\equip\`anonymous_namespace'\DeclareAMs.h>
#include <tpp\gm\impl\equip\`anonymous_namespace'\SetEquipParameters.h>
#include "tpp/ui/utility/utility_GetIconFtexPath.h"


namespace
{
    using SetLuaFunctions_t = void(__fastcall*)(lua_State* L);
    using FoxLuaRegisterLibrary_t = void(__fastcall*)(lua_State* L, const char* libName, luaL_Reg* funcs);
    using lua_tolstring_t = const char* (__fastcall*)(lua_State* L, int idx, size_t* len);
    using lua_tointeger_t = long long(__fastcall*)(lua_State* L, int idx);
    using lua_tonumber_t = lua_Number(__fastcall*)(lua_State* L, int idx);
    using lua_pushnumber_t = void(__fastcall*)(lua_State* L, lua_Number n);
    using lua_toboolean_t = int(__fastcall*)(lua_State* L, int idx);
    using lua_gettop_t = int(__fastcall*)(lua_State* L);
    using lua_settop_t = void(__fastcall*)(lua_State* L, int idx);
    using lua_getfield_t = void(__fastcall*)(lua_State* L, int idx, char* k);
    using lua_rawgeti_t = void(__fastcall*)(lua_State* L, int idx, int n);
    using lua_type_t = int(__fastcall*)(lua_State* L, int idx);
    using lua_isstring_t = int(__fastcall*)(lua_State* L, int idx);
    using lua_isnumber_t = int(__fastcall*)(lua_State* L, int idx);
    using lua_objlen_t = size_t(__fastcall*)(lua_State* L, int idx);
    using lua_pushboolean_t = void(__fastcall*)(lua_State* L, int b);
    using lua_pushstring_t = void(__fastcall*)(lua_State* L, char* s);
    using lua_createtable_t = void(__fastcall*)(lua_State* L, int narr, int nrec);
    using lua_rawset_t = void(__fastcall*)(lua_State* L, int idx);
    using lua_settable_t = void(__fastcall*)(lua_State* L, int idx);
    using lua_pushnil_t = void(__fastcall*)(lua_State* L);
    using lua_next_t = int(__fastcall*)(lua_State* L, int idx);
    using lua_gettable_t = void(__fastcall*)(lua_State* L, int idx);
    using lua_pushvalue_t = void(__fastcall*)(lua_State* L, int idx);

    static constexpr int LUA_GLOBALSINDEX_51 = -10002;

    // English bootstrap addresses for the Lua bridge.
    // These are used before version_info.txt is resolved so the bridge can hook as early as the original build.
    static constexpr uintptr_t BOOTSTRAP_EN_SetLuaFunctions = 0x1408D78A0ull;
    static constexpr uintptr_t BOOTSTRAP_EN_FoxLuaRegisterLibrary = 0x14006B6D0ull;
    static constexpr uintptr_t BOOTSTRAP_EN_lua_tolstring = 0x141A123C0ull;
    static constexpr uintptr_t BOOTSTRAP_EN_lua_tointeger = 0x141A12390ull;
    static constexpr uintptr_t BOOTSTRAP_EN_lua_tonumber = 0x141A12460ull;
    static constexpr uintptr_t BOOTSTRAP_EN_lua_pushnumber = 0x141A11BC0ull;
    static constexpr uintptr_t BOOTSTRAP_EN_lua_toboolean = 0x141A12330ull;
    static constexpr uintptr_t BOOTSTRAP_EN_lua_gettop = 0x14C1D7D40ull;
    static constexpr uintptr_t BOOTSTRAP_EN_lua_settop = 0x14C1EBBE0ull;
    static constexpr uintptr_t BOOTSTRAP_EN_lua_getfield = 0x14C1D7320ull;
    static constexpr uintptr_t BOOTSTRAP_EN_lua_rawgeti = 0x14C1E9320ull;
    static constexpr uintptr_t BOOTSTRAP_EN_lua_type = 0x14C1ED760ull;
    static constexpr uintptr_t BOOTSTRAP_EN_lua_isstring = 0x14C1D9250ull;
    static constexpr uintptr_t BOOTSTRAP_EN_lua_isnumber = 0x14C1D8C90ull;
    static constexpr uintptr_t BOOTSTRAP_EN_lua_objlen = 0x14C1DA960ull;
    static constexpr uintptr_t BOOTSTRAP_EN_lua_pushboolean = 0x14C1DB230ull;
    static constexpr uintptr_t BOOTSTRAP_EN_lua_pushstring = 0x14C1E7EE0ull;
    static constexpr uintptr_t BOOTSTRAP_EN_lua_createtable = 0x14C1D6320ull;
    static constexpr uintptr_t BOOTSTRAP_EN_lua_rawset = 0x14C1E9CF0ull;
    static constexpr uintptr_t BOOTSTRAP_EN_lua_settable = 0x14C1EB2B0ull;
    static constexpr uintptr_t BOOTSTRAP_EN_lua_pushnil = 0x14C1E7CC0ull;
    static constexpr uintptr_t BOOTSTRAP_EN_lua_next = 0x14C1DA770ull;
    static constexpr uintptr_t BOOTSTRAP_EN_lua_gettable = 0x14C1D7C10ull;
    static constexpr uintptr_t BOOTSTRAP_EN_lua_pushvalue = 0x14C1E87E0ull;

    static SetLuaFunctions_t       g_OrigSetLuaFunctions = nullptr;
    static FoxLuaRegisterLibrary_t g_FoxLuaRegisterLibrary = nullptr;
    static lua_tolstring_t         g_lua_tolstring = nullptr;
    static lua_tointeger_t         g_lua_tointeger = nullptr;
    static lua_tonumber_t          g_lua_tonumber = nullptr;
    static lua_pushnumber_t        g_lua_pushnumber = nullptr;
    static lua_toboolean_t         g_lua_toboolean = nullptr;
    static lua_gettop_t            g_lua_gettop = nullptr;
    static lua_settop_t            g_lua_settop = nullptr;
    static lua_getfield_t          g_lua_getfield = nullptr;
    static lua_rawgeti_t           g_lua_rawgeti = nullptr;
    static lua_type_t              g_lua_type = nullptr;
    static lua_isstring_t          g_lua_isstring = nullptr;
    static lua_isnumber_t          g_lua_isnumber = nullptr;
    static lua_objlen_t            g_lua_objlen = nullptr;
    static lua_pushboolean_t       g_lua_pushboolean = nullptr;
    static lua_pushstring_t        g_lua_pushstring = nullptr;
    static lua_createtable_t       g_lua_createtable = nullptr;
    static lua_rawset_t            g_lua_rawset = nullptr;
    static lua_settable_t          g_lua_settable = nullptr;
    static lua_pushnil_t           g_lua_pushnil = nullptr;
    static lua_next_t              g_lua_next = nullptr;
    static lua_gettable_t          g_lua_gettable = nullptr;
    static lua_pushvalue_t         g_lua_pushvalue = nullptr;

    static std::unordered_set<lua_State*> g_RegisteredLuaStates;
    static std::mutex g_RegisteredLuaStatesMutex;
    static bool g_SetLuaFunctionsHookInstalled = false;
}

// Returns one Lua bridge address, using the resolved address set when available and the original English bootstrap address otherwise.
// Params: resolvedAddr, bootstrapAddr
static uintptr_t GetLuaBridgeAddress(uintptr_t resolvedAddr, uintptr_t bootstrapAddr)
{
    return resolvedAddr ? resolvedAddr : bootstrapAddr;
}

// Resolves the Lua/game functions used by this file.
// Params: none
static bool ResolveLuaApi()
{
    if (!g_FoxLuaRegisterLibrary)
        g_FoxLuaRegisterLibrary = reinterpret_cast<FoxLuaRegisterLibrary_t>(ResolveGameAddress(GetLuaBridgeAddress(gAddr.FoxLuaRegisterLibrary, BOOTSTRAP_EN_FoxLuaRegisterLibrary)));

    if (!g_lua_tolstring)
        g_lua_tolstring = reinterpret_cast<lua_tolstring_t>(ResolveGameAddress(GetLuaBridgeAddress(gAddr.lua_tolstring, BOOTSTRAP_EN_lua_tolstring)));

    if (!g_lua_tointeger)
        g_lua_tointeger = reinterpret_cast<lua_tointeger_t>(ResolveGameAddress(GetLuaBridgeAddress(gAddr.lua_tointeger, BOOTSTRAP_EN_lua_tointeger)));

    if (!g_lua_tonumber)
        g_lua_tonumber = reinterpret_cast<lua_tonumber_t>(ResolveGameAddress(GetLuaBridgeAddress(gAddr.lua_tonumber, BOOTSTRAP_EN_lua_tonumber)));

    if (!g_lua_toboolean)
        g_lua_toboolean = reinterpret_cast<lua_toboolean_t>(ResolveGameAddress(GetLuaBridgeAddress(gAddr.lua_toboolean, BOOTSTRAP_EN_lua_toboolean)));

    if (!g_lua_pushnumber)
        g_lua_pushnumber = reinterpret_cast<lua_pushnumber_t>(ResolveGameAddress(GetLuaBridgeAddress(gAddr.lua_pushnumber, BOOTSTRAP_EN_lua_pushnumber)));

    if (!g_lua_gettop)
        g_lua_gettop = reinterpret_cast<lua_gettop_t>(ResolveGameAddress(GetLuaBridgeAddress(gAddr.lua_gettop, BOOTSTRAP_EN_lua_gettop)));

    if (!g_lua_settop)
        g_lua_settop = reinterpret_cast<lua_settop_t>(ResolveGameAddress(GetLuaBridgeAddress(gAddr.lua_settop, BOOTSTRAP_EN_lua_settop)));

    if (!g_lua_getfield)
        g_lua_getfield = reinterpret_cast<lua_getfield_t>(ResolveGameAddress(GetLuaBridgeAddress(gAddr.lua_getfield, BOOTSTRAP_EN_lua_getfield)));

    if (!g_lua_rawgeti)
        g_lua_rawgeti = reinterpret_cast<lua_rawgeti_t>(ResolveGameAddress(GetLuaBridgeAddress(gAddr.lua_rawgeti, BOOTSTRAP_EN_lua_rawgeti)));

    if (!g_lua_type)
        g_lua_type = reinterpret_cast<lua_type_t>(ResolveGameAddress(GetLuaBridgeAddress(gAddr.lua_type, BOOTSTRAP_EN_lua_type)));

    if (!g_lua_isstring)
        g_lua_isstring = reinterpret_cast<lua_isstring_t>(ResolveGameAddress(GetLuaBridgeAddress(gAddr.lua_isstring, BOOTSTRAP_EN_lua_isstring)));

    if (!g_lua_isnumber)
        g_lua_isnumber = reinterpret_cast<lua_isnumber_t>(ResolveGameAddress(GetLuaBridgeAddress(gAddr.lua_isnumber, BOOTSTRAP_EN_lua_isnumber)));

    if (!g_lua_objlen)
        g_lua_objlen = reinterpret_cast<lua_objlen_t>(ResolveGameAddress(GetLuaBridgeAddress(gAddr.lua_objlen, BOOTSTRAP_EN_lua_objlen)));

    if (!g_lua_pushboolean)
        g_lua_pushboolean = reinterpret_cast<lua_pushboolean_t>(ResolveGameAddress(GetLuaBridgeAddress(gAddr.lua_pushboolean, BOOTSTRAP_EN_lua_pushboolean)));

    if (!g_lua_pushstring)
        g_lua_pushstring = reinterpret_cast<lua_pushstring_t>(ResolveGameAddress(GetLuaBridgeAddress(gAddr.lua_pushstring, BOOTSTRAP_EN_lua_pushstring)));

    if (!g_lua_createtable)
        g_lua_createtable = reinterpret_cast<lua_createtable_t>(ResolveGameAddress(GetLuaBridgeAddress(gAddr.lua_createtable, BOOTSTRAP_EN_lua_createtable)));

    if (!g_lua_rawset)
        g_lua_rawset = reinterpret_cast<lua_rawset_t>(ResolveGameAddress(GetLuaBridgeAddress(gAddr.lua_rawset, BOOTSTRAP_EN_lua_rawset)));

    if (!g_lua_settable)
        g_lua_settable = reinterpret_cast<lua_settable_t>(ResolveGameAddress(GetLuaBridgeAddress(gAddr.lua_settable, BOOTSTRAP_EN_lua_settable)));

    if (!g_lua_pushnil)
        g_lua_pushnil = reinterpret_cast<lua_pushnil_t>(ResolveGameAddress(GetLuaBridgeAddress(gAddr.lua_pushnil, BOOTSTRAP_EN_lua_pushnil)));

    if (!g_lua_next)
        g_lua_next = reinterpret_cast<lua_next_t>(ResolveGameAddress(GetLuaBridgeAddress(gAddr.lua_next, BOOTSTRAP_EN_lua_next)));

    if (!g_lua_gettable)
        g_lua_gettable = reinterpret_cast<lua_gettable_t>(
            ResolveGameAddress(GetLuaBridgeAddress(gAddr.lua_gettable, BOOTSTRAP_EN_lua_gettable)));

    if (!g_lua_pushvalue)
        g_lua_pushvalue = reinterpret_cast<lua_pushvalue_t>(
            ResolveGameAddress(GetLuaBridgeAddress(gAddr.lua_pushvalue, BOOTSTRAP_EN_lua_pushvalue)));

    return g_FoxLuaRegisterLibrary &&
        g_lua_tolstring &&
        g_lua_tointeger &&
        g_lua_tonumber &&
        g_lua_toboolean &&
        g_lua_pushnumber &&
        g_lua_gettop &&
        g_lua_settop &&
        g_lua_getfield &&
        g_lua_gettable &&
        g_lua_rawgeti &&
        g_lua_type &&
        g_lua_isstring &&
        g_lua_isnumber &&
        g_lua_objlen &&
        g_lua_pushboolean &&
        g_lua_pushvalue &&
        g_lua_pushstring &&
        g_lua_createtable &&
        g_lua_rawset &&
        g_lua_settable &&
        g_lua_pushnil &&
        g_lua_next;
}

// Returns the current Lua stack top.
// Params: L
static int GetLuaTop(lua_State* L)
{
    if (!ResolveLuaApi() || !g_lua_gettop)
        return 0;

    return g_lua_gettop(L);
}

// Sets the Lua stack top.
// Params: L, idx
static void SetLuaTop(lua_State* L, int idx)
{
    if (!ResolveLuaApi() || !g_lua_settop)
        return;

    g_lua_settop(L, idx);
}

// Pushes one table field onto the stack.
// Params: L, idx, fieldName
static void LuaGetField(lua_State* L, int idx, const char* fieldName)
{
    if (!ResolveLuaApi() || !g_lua_getfield || !fieldName)
        return;

    g_lua_getfield(L, idx, const_cast<char*>(fieldName));
}

// Pushes one array entry onto the stack.
// Params: L, idx, n
static void LuaRawGetI(lua_State* L, int idx, int n)
{
    if (!ResolveLuaApi() || !g_lua_rawgeti)
        return;

    g_lua_rawgeti(L, idx, n);
}

// Returns the Lua value type.
// Params: L, idx
static int LuaType(lua_State* L, int idx)
{
    if (!ResolveLuaApi() || !g_lua_type)
        return -1;

    return g_lua_type(L, idx);
}

// Returns true if one Lua value is a string.
// Params: L, idx
static bool LuaIsString(lua_State* L, int idx)
{
    if (!ResolveLuaApi() || !g_lua_isstring)
        return false;

    return g_lua_isstring(L, idx) != 0;
}

// Returns true if one Lua value is a number.
// Params: L, idx
static bool LuaIsNumber(lua_State* L, int idx)
{
    if (!ResolveLuaApi() || !g_lua_isnumber)
        return false;

    return g_lua_isnumber(L, idx) != 0;
}

// Returns the Lua object length.
// Params: L, idx
static size_t LuaObjLen(lua_State* L, int idx)
{
    if (!ResolveLuaApi() || !g_lua_objlen)
        return 0;

    return g_lua_objlen(L, idx);
}

// Pushes one boolean back to Lua.
// Params: L, value
static void PushLuaBool(lua_State* L, bool value)
{
    if (!ResolveLuaApi() || !g_lua_pushboolean)
        return;

    g_lua_pushboolean(L, value ? 1 : 0);
}

// Pops values from the Lua stack.
// Params: L, count
static void LuaPop(lua_State* L, int count)
{
    if (!ResolveLuaApi() || !g_lua_settop)
        return;

    g_lua_settop(L, -count - 1);
}

// Registers one C library into Fox Lua.
// Params: L, libName, funcs
static bool RegisterLuaLibrary(lua_State* L, const char* libName, luaL_Reg* funcs)
{
    if (!ResolveLuaApi() || !L || !libName || !funcs)
        return false;

    g_FoxLuaRegisterLibrary(L, libName, funcs);
    Log("[V_FrameWork] Registered library: %s (L=%p)\n", libName, L);
    return true;
}

// Returns a Lua string argument.
// Params: L, idx
static const char* GetLuaString(lua_State* L, int idx)
{
    if (!ResolveLuaApi() || !g_lua_tolstring)
        return nullptr;

    return g_lua_tolstring(L, idx, nullptr);
}

// Returns a Lua int argument.
// Params: L, idx
static int GetLuaInt(lua_State* L, int idx)
{
    if (!ResolveLuaApi() || !g_lua_tointeger)
        return 0;

    return static_cast<int>(g_lua_tointeger(L, idx));
}

// Returns a Lua int64 argument.
// Params: L, idx
static std::uint64_t GetLuaInt64(lua_State* L, int idx)
{
    if (!ResolveLuaApi() || !g_lua_tointeger)
        return 0;

    return static_cast<std::uint64_t>(g_lua_tointeger(L, idx));
}

// Returns a Lua bool argument.
// Params: L, idx
static bool GetLuaBool(lua_State* L, int idx)
{
    if (!ResolveLuaApi() || !g_lua_toboolean)
        return false;

    return g_lua_toboolean(L, idx) != 0;
}

// Returns a Lua float argument.
// Params: L, idx
static float GetLuaNumber(lua_State* L, int idx)
{
    if (!ResolveLuaApi() || !g_lua_tonumber)
        return 0.0f;

    return static_cast<float>(g_lua_tonumber(L, idx));
}

// Pushes one float back to Lua.
// Params: L, value
static void PushLuaNumber(lua_State* L, float value)
{
    if (!ResolveLuaApi() || !g_lua_pushnumber)
        return;

    g_lua_pushnumber(L, static_cast<lua_Number>(value));
}

// Returns true if this Lua state was already registered.
// Params: L
static bool IsLuaStateRegistered(lua_State* L)
{
    std::lock_guard<std::mutex> lock(g_RegisteredLuaStatesMutex);
    return g_RegisteredLuaStates.find(L) != g_RegisteredLuaStates.end();
}

// Tracks one Lua state after registration.
// Params: L
static void TrackLuaState(lua_State* L)
{
    std::lock_guard<std::mutex> lock(g_RegisteredLuaStatesMutex);
    g_RegisteredLuaStates.insert(L);
}

// Clears tracked Lua states.
// Params: none
static void ClearTrackedLuaStates()
{
    std::lock_guard<std::mutex> lock(g_RegisteredLuaStatesMutex);
    g_RegisteredLuaStates.clear();
}

// Reads one required string field from a Lua table.
// Params: L, fieldName, outValue
static bool LuaReadRequiredStringField(lua_State* L, const char* fieldName, std::string& outValue)
{
    outValue.clear();

    LuaGetField(L, -1, fieldName);
    const bool ok = LuaIsString(L, -1);

    if (ok)
    {
        const char* value = GetLuaString(L, -1);
        if (value && value[0] != '\0')
        {
            outValue = value;
        }
        else
        {
            LuaPop(L, 1);
            return false;
        }
    }

    LuaPop(L, 1);
    return ok && !outValue.empty();
}

static double LuaToNumber(lua_State* L, int idx)
{
    if (!ResolveLuaApi() || !g_lua_tonumber)
        return 0.0;

    return static_cast<double>(g_lua_tonumber(L, idx));
}

// Reads one optional signed integer field from a Lua table.
// Params: L, fieldName, defaultValue
static std::int32_t LuaReadOptionalIntField(lua_State* L, const char* fieldName, std::int32_t defaultValue)
{
    LuaGetField(L, -1, fieldName);

    std::int32_t value = defaultValue;
    if (LuaIsNumber(L, -1))
    {
        value = static_cast<std::int32_t>(GetLuaInt(L, -1));
    }

    LuaPop(L, 1);
    return value;
}

// Reads one optional unsigned integer field from a Lua table.
// Params: L, fieldName, defaultValue
static std::uint32_t LuaReadOptionalUIntField(lua_State* L, const char* fieldName, std::uint32_t defaultValue)
{
    LuaGetField(L, -1, fieldName);

    std::uint32_t value = defaultValue;
    if (LuaIsNumber(L, -1))
    {
        value = static_cast<std::uint32_t>(GetLuaInt(L, -1));
    }

    LuaPop(L, 1);
    return value;
}

static void LuaPushString(lua_State* L, const char* value)
{
    if (!ResolveLuaApi() || !g_lua_pushstring || !value)
        return;

    g_lua_pushstring(L, const_cast<char*>(value));
}

static void LuaCreateTable(lua_State* L, int narr, int nrec)
{
    if (!ResolveLuaApi() || !g_lua_createtable)
        return;

    g_lua_createtable(L, narr, nrec);
}

static void LuaGetTable(lua_State* L, int idx)
{
    if (!ResolveLuaApi() || !g_lua_gettable)
        return;

    g_lua_gettable(L, idx);
}

static void LuaRawSet(lua_State* L, int idx)
{
    if (!ResolveLuaApi() || !g_lua_rawset)
        return;

    g_lua_rawset(L, idx);
}

static void LuaSetTable(lua_State* L, int idx)
{
    if (!ResolveLuaApi() || !g_lua_settable)
        return;

    g_lua_settable(L, idx);
}

static void LuaPushNil(lua_State* L)
{
    if (!ResolveLuaApi() || !g_lua_pushnil)
        return;

    g_lua_pushnil(L);
}

static int LuaNext(lua_State* L, int idx)
{
    if (!ResolveLuaApi() || !g_lua_next)
        return 0;

    return g_lua_next(L, idx);
}

static void LuaPushValue(lua_State* L, int idx)
{
    if (!ResolveLuaApi() || !g_lua_pushvalue)
        return;

    g_lua_pushvalue(L, idx);
}

// Sets the default equip background texture.
// Params: path
static int __cdecl l_SetDefaultEquipBgTexturePath(lua_State* L)
{
    const char* rawPath = GetLuaString(L, 1);
    if (!rawPath || !*rawPath)
        return 0;

    EquipBg_SetDefaultTexture(FoxHashes::PathCode64Ext(rawPath));
    return 0;
}

// Clears the default equip background texture.
// Params: none
static int __cdecl l_ClearDefaultEquipBgTexture(lua_State* L)
{
    UNREFERENCED_PARAMETER(L);
    EquipBg_ClearDefaultTexture();
    return 0;
}

// Sets the enemy-weapon equip background texture.
// Params: path
static int __cdecl l_SetEnemyWeaponBgTexturePath(lua_State* L)
{
    const char* rawPath = GetLuaString(L, 1);
    if (!rawPath || !*rawPath)
        return 0;

    EquipBg_SetEnemyWeaponTexture(FoxHashes::PathCode64Ext(rawPath));
    return 0;
}

// Clears the enemy-weapon equip background texture.
// Params: none
static int __cdecl l_ClearEnemyWeaponBgTexture(lua_State* L)
{
    UNREFERENCED_PARAMETER(L);
    EquipBg_ClearEnemyWeaponTexture();
    return 0;
}

// Sets one per-enemy equip background texture.
// Params: equipId, path
static int __cdecl l_SetEnemyEquipBgTexturePath(lua_State* L)
{
    const int equipId = GetLuaInt(L, 1);
    const char* rawPath = GetLuaString(L, 2);

    if (!rawPath || !*rawPath)
        return 0;

    EquipBg_SetEnemyEquipTexture(equipId, FoxHashes::PathCode64Ext(rawPath));
    return 0;
}

// Clears one per-enemy equip background texture.
// Params: equipId
static int __cdecl l_ClearEnemyEquipBgTexture(lua_State* L)
{
    const int equipId = GetLuaInt(L, 1);
    EquipBg_ClearEnemyEquipTexture(equipId);
    return 0;
}

// Sets one per-equip background texture.
// Params: equipId, path
static int __cdecl l_SetEquipBgTexturePath(lua_State* L)
{
    const int equipId = GetLuaInt(L, 1);
    const char* rawPath = GetLuaString(L, 2);

    if (!rawPath || !*rawPath)
        return 0;

    EquipBg_SetEquipTexture(equipId, FoxHashes::PathCode64Ext(rawPath));
    return 0;
}

// Clears one per-equip background texture.
// Params: equipId
static int __cdecl l_ClearEquipBgTexture(lua_State* L)
{
    const int equipId = GetLuaInt(L, 1);
    EquipBg_ClearEquipTexture(equipId);
    return 0;
}

// Clears all per-equip background textures.
// Params: none
static int __cdecl l_ClearAllEquipBgTextures(lua_State* L)
{
    UNREFERENCED_PARAMETER(L);
    EquipBg_ClearAllEquipTextures();
    return 0;
}

// Sets the loading splash main texture.
// Params: path
static int __cdecl l_SetLoadingSplashMainTexturePath(lua_State* L)
{
    const char* rawPath = GetLuaString(L, 1);
    if (!rawPath || !*rawPath)
        return 0;

    LoadingSplash_SetMainTexture(FoxHashes::PathCode64Ext(rawPath));
    return 0;
}

// Sets the loading splash blur texture.
// Params: path
static int __cdecl l_SetLoadingSplashBlurTexturePath(lua_State* L)
{
    const char* rawPath = GetLuaString(L, 1);
    if (!rawPath || !*rawPath)
        return 0;

    LoadingSplash_SetBlurTexture(FoxHashes::PathCode64Ext(rawPath));
    return 0;
}

// Clears loading splash textures.
// Params: none
static int __cdecl l_ClearLoadingSplashTextures(lua_State* L)
{
    UNREFERENCED_PARAMETER(L);
    LoadingSplash_ClearTextures();
    return 0;
}

// Sets the game over splash main texture.
// Params: path
static int __cdecl l_SetGameOverSplashMainTexturePath(lua_State* L)
{
    const char* rawPath = GetLuaString(L, 1);
    if (!rawPath || !*rawPath)
        return 0;

    GameOverSplash_SetMainTexture(FoxHashes::PathCode64Ext(rawPath));
    return 0;
}

// Sets the game over splash blur texture.
// Params: path
static int __cdecl l_SetGameOverSplashBlurTexturePath(lua_State* L)
{
    const char* rawPath = GetLuaString(L, 1);
    if (!rawPath || !*rawPath)
        return 0;

    GameOverSplash_SetBlurTexture(FoxHashes::PathCode64Ext(rawPath));
    return 0;
}

// Clears game over splash textures.
// Params: none
static int __cdecl l_ClearGameOverSplashTextures(lua_State* L)
{
    UNREFERENCED_PARAMETER(L);
    GameOverSplash_ClearTextures();
    return 0;
}

// Sets the caution timer override.
// Params: seconds
static int l_SetCautionStepNormalDurationSeconds(lua_State* L)
{
    const float seconds = GetLuaNumber(L, 1);
    Set_CautionStepNormalDurationSeconds(seconds);
    return 0;
}

// Gets the caution timer override.
// Params: none
static int l_GetCautionStepNormalDurationSeconds(lua_State* L)
{
    PushLuaNumber(L, Get_CautionStepNormalDurationSeconds());
    return 1;
}

// Clears the caution timer override.
// Params: none
static int l_UnsetCautionStepNormalDurationSeconds(lua_State* L)
{
    UNREFERENCED_PARAMETER(L);
    Unset_CautionStepNormalDurationSeconds();
    return 0;
}

// Gets the remaining caution timer.
// Params: none
static int l_GetCautionStepNormalRemainingSeconds(lua_State* L)
{
    PushLuaNumber(L, Get_CautionStepNormalRemainingSeconds());
    return 1;
}

// Sets one player voice FPK override.
// Params: playerType, path
static int __cdecl l_SetPlayerVoiceFpkPathForType(lua_State* L)
{
    const int playerType = GetLuaInt(L, 1);
    const char* rawPath = GetLuaString(L, 2);

    if (!rawPath || !*rawPath)
        return 0;

    Set_PlayerVoiceFpkPathForType(static_cast<std::uint32_t>(playerType), rawPath);
    return 0;
}

// Clears one player voice FPK override.
// Params: playerType
static int __cdecl l_ClearPlayerVoiceFpkPathForType(lua_State* L)
{
    const int playerType = GetLuaInt(L, 1);
    Clear_PlayerVoiceFpkPathForType(static_cast<std::uint32_t>(playerType));
    return 0;
}

// Clears all player voice FPK overrides.
// Params: none
static int __cdecl l_ClearAllPlayerVoiceFpkOverrides(lua_State* L)
{
    UNREFERENCED_PARAMETER(L);
    Clear_AllPlayerVoiceFpkOverrides();
    return 0;
}

// Marks one VIP-important soldier.
// Params: gameObjectId, isOfficer
static int __cdecl l_SetVIPImportant(lua_State* L)
{
    const std::uint32_t gameObjectId = static_cast<std::uint32_t>(GetLuaInt64(L, 1));
    const bool isOfficer = GetLuaBool(L, 2);

    Add_VIPSleepFaintImportantGameObjectId(gameObjectId, isOfficer);
    Add_VIPHoldupImportantGameObjectId(gameObjectId, isOfficer);
    Add_VIPRadioImportantGameObjectId(gameObjectId, isOfficer);
    return 0;
}

// Removes one VIP-important soldier.
// Params: gameObjectId
static int __cdecl l_RemoveVIPImportant(lua_State* L)
{
    const std::uint32_t gameObjectId = static_cast<std::uint32_t>(GetLuaInt64(L, 1));

    Remove_VIPSleepFaintImportantGameObjectId(gameObjectId);
    Remove_VIPHoldupImportantGameObjectId(gameObjectId);
    Remove_VIPRadioImportantGameObjectId(gameObjectId);
    return 0;
}

// Clears all VIP-important soldiers.
// Params: none
static int __cdecl l_ClearVIPImportant(lua_State* L)
{
    UNREFERENCED_PARAMETER(L);

    Clear_VIPSleepFaintImportantGameObjectIds();
    Clear_VIPHoldupImportantGameObjectIds();
    Clear_VIPRadioImportantGameObjectIds();
    return 0;
}

// Sets the custom non-VIP holdup recovery toggle.
// Params: enabled
static int l_SetUseConcernedHoldupRecovery(lua_State* L)
{
    const bool enabled = GetLuaBool(L, 1) != 0;
    Set_UseCustomNonVipHoldupRecovery(enabled);
    return 0;
}

// Sets cowardly holdup reactions toggle.
// Params: enabled
static int l_HoldUpReactionCowardlyReactions(lua_State* L)
{
    const bool enabled = GetLuaBool(L, 1);
    Set_HoldUpReactionCowardlyReactions(enabled);
    return 0;
}

// Adds one call-sign-extra soldier.
// Params: gameObjectId
static int __cdecl l_AddCallSignExtraSoldier(lua_State* L)
{
    const std::uint32_t gameObjectId = static_cast<std::uint32_t>(GetLuaInt64(L, 1));
    Add_CallSignExtraSoldier(gameObjectId);
    return 0;
}

// Removes one call-sign-extra soldier.
// Params: gameObjectId
static int __cdecl l_RemoveCallSignExtraSoldier(lua_State* L)
{
    const std::uint32_t gameObjectId = static_cast<std::uint32_t>(GetLuaInt64(L, 1));
    Remove_CallSignExtraSoldier(gameObjectId);
    return 0;
}

// Clears all call-sign-extra soldiers.
// Params: none
static int __cdecl l_ClearCallSignExtraSoldiers(lua_State* L)
{
    UNREFERENCED_PARAMETER(L);
    Clear_CallSignExtraSoldiers();
    return 0;
}

// Adds one lost hostage.
// Params: gameObjectId, hostageType
static int __cdecl l_SetLostHostage(lua_State* L)
{
    const std::uint32_t gameObjectId = static_cast<std::uint32_t>(GetLuaInt64(L, 1));
    const int hostageType = GetLuaInt(L, 2);

    Add_LostHostageTrap(gameObjectId, hostageType);
    Add_LostHostageDiscovery(gameObjectId, hostageType);
    return 0;
}

// Removes one lost hostage.
// Params: gameObjectId
static int __cdecl l_RemoveLostHostage(lua_State* L)
{
    const std::uint32_t gameObjectId = static_cast<std::uint32_t>(GetLuaInt64(L, 1));

    Remove_LostHostageTrap(gameObjectId);
    Remove_LostHostageDiscovery(gameObjectId);
    return 0;
}

// Clears all lost hostages.
// Params: none
static int __cdecl l_ClearLostHostages(lua_State* L)
{
    UNREFERENCED_PARAMETER(L);
    Clear_LostHostagesTrap();
    Clear_LostHostageDiscovery();
    return 0;
}

static int __cdecl l_SetLostHostageFromPlayer(lua_State* L)
{
    const std::uint32_t gameObjectId = static_cast<std::uint32_t>(GetLuaInt64(L, 1));
    const bool playerTookHostage = GetLuaBool(L, 2);
    PlayerTookHostage(gameObjectId, playerTookHostage);
    return 0;
}

// Enables or disables stealth camo for one mappedIndex.
// Params: mappedIndex, enabled
static int __cdecl l_EnableSoldierStealthCamo(lua_State* L)
{
    const std::uint32_t mappedIndex = static_cast<std::uint32_t>(GetLuaInt(L, 1));
    const bool enabled = GetLuaBool(L, 2);
    Set_UpdateOptCamoEnableMappedIndex(mappedIndex, enabled);
    return 0;
}

// Clears all per-soldier stealth camo overrides.
// Params: none
static int __cdecl l_ClearSoldierStealthCamoOverrides(lua_State* L)
{
    UNREFERENCED_PARAMETER(L);
    Clear_UpdateOptCamoMappedIndexOverrides();
    return 0;
}

// Plays a cassette directly using album and track indices.
// Params: albumIndex, trackIndex, loopPlay, playAll
static int __cdecl l_PlayCassetteTapeByAlbumAndTrack(lua_State* L)
{
    const std::uint32_t albumIndex = static_cast<std::uint32_t>(GetLuaInt(L, 1));
    const std::uint32_t trackIndex = static_cast<std::uint32_t>(GetLuaInt(L, 2));

    bool loopPlay = false;
    bool playAll = true;

    const int arg3Type = LuaType(L, 3);
    if (arg3Type != LUA_TNONE && arg3Type != LUA_TNIL)
    {
        loopPlay = GetLuaBool(L, 3);
    }

    const int arg4Type = LuaType(L, 4);
    if (arg4Type != LUA_TNONE && arg4Type != LUA_TNIL)
    {
        playAll = GetLuaBool(L, 4);
    }

    const bool ok = PlayCassetteByAlbumAndTrack(albumIndex, trackIndex, loopPlay, playAll);
    PushLuaBool(L, ok);
    return 1;
}

// Plays a cassette directly using a numeric track id.
// Params: trackId, loopPlay, playAll
static int __cdecl l_PlayCassetteTapeByTrackId(lua_State* L)
{
    const std::uint32_t albumIndex = static_cast<std::uint32_t>(GetLuaInt(L, 1));
    const std::uint32_t trackId = static_cast<std::uint32_t>(GetLuaInt(L, 2));

    bool loopPlay = false;
    bool playAll = false;

    const int arg3Type = LuaType(L, 3);
    if (arg3Type != LUA_TNONE && arg3Type != LUA_TNIL)
    {
        loopPlay = GetLuaBool(L, 3);
    }

    const int arg4Type = LuaType(L, 4);
    if (arg4Type != LUA_TNONE && arg4Type != LUA_TNIL)
    {
        playAll = GetLuaBool(L, 4);
    }

    const bool ok = PlayCassetteByTrackId(albumIndex, trackId, loopPlay, playAll);
    PushLuaBool(L, ok);
    return 1;
}

static int __cdecl l_GetTapeTrackDirectPlayId(lua_State* L)
{
    if (!LuaIsString(L, 1))
    {
        PushLuaNumber(L, -1.0f);
        return 1;
    }

    const char* trackName = GetLuaString(L, 1);
    const std::int32_t directPlayTrackId = ResolveTapeTrackDirectPlayId(trackName);

    PushLuaNumber(L, static_cast<float>(directPlayTrackId));
    return 1;
}

static int l_GetCassettePlayingTime(lua_State* L)
{
    UNREFERENCED_PARAMETER(L);

    const std::uint32_t value = GetCassettePlayingTime();
    PushLuaNumber(L, static_cast<float>(value));
    return 1;
}

static int l_GetCassettePlayingTrackId(lua_State* L)
{
    UNREFERENCED_PARAMETER(L);

    const std::uint32_t value = GetCassettePlayingTrackId();
    PushLuaNumber(L, static_cast<float>(value));
    return 1;
}

static int __cdecl l_PauseCassette(lua_State* L)
{
    std::uint32_t fadeMs = 0;

    const int arg1Type = LuaType(L, 1);
    if (arg1Type != LUA_TNONE && arg1Type != LUA_TNIL)
    {
        fadeMs = static_cast<std::uint32_t>(GetLuaInt(L, 1));
    }

    const std::int32_t errorCode = PauseCassette(fadeMs);
    PushLuaNumber(L, static_cast<float>(errorCode));
    return 1;
}

static int __cdecl l_ResumeCassette(lua_State* L)
{
    std::uint32_t fadeMs = 0;

    const int arg1Type = LuaType(L, 1);
    if (arg1Type != LUA_TNONE && arg1Type != LUA_TNIL)
    {
        fadeMs = static_cast<std::uint32_t>(GetLuaInt(L, 1));
    }

    const std::int32_t errorCode = ResumeCassette(fadeMs);
    PushLuaNumber(L, static_cast<float>(errorCode));
    return 1;
}

static int __cdecl l_StopCassette(lua_State* L)
{
    std::uint32_t fadeMs = 0;
    bool stopByUser = false;

    const int arg1Type = LuaType(L, 1);
    if (arg1Type != LUA_TNONE && arg1Type != LUA_TNIL)
    {
        fadeMs = static_cast<std::uint32_t>(GetLuaInt(L, 1));
    }

    const int arg2Type = LuaType(L, 2);
    if (arg2Type != LUA_TNONE && arg2Type != LUA_TNIL)
    {
        stopByUser = GetLuaBool(L, 2);
    }

    const std::int32_t errorCode = StopCassette(fadeMs, stopByUser);
    PushLuaNumber(L, static_cast<float>(errorCode));
    return 1;
}

// Gets the current cassette speaker state.
// Params: none
static int __cdecl l_IsCassetteSpeakerEnabled(lua_State* L)
{
    UNREFERENCED_PARAMETER(L);

    bool enabled = false;
    const bool ok = IsCassetteSpeakerEnabled(enabled);

    if (!ok)
    {
        PushLuaBool(L, false);
        return 1;
    }

    PushLuaBool(L, enabled);
    return 1;
}

// Sets the cassette speaker state.
// Params: enabled
static int __cdecl l_SetCassetteSpeakerEnabled(lua_State* L)
{
    const bool enabled = GetLuaBool(L, 1);
    const bool ok = SetCassetteSpeakerEnabled(enabled);
    PushLuaBool(L, ok);
    return 1;
}

// Registers custom cassette albums and tracks.
// Params: tapeInfoTable
static int __cdecl l_RegisterCustomTapes(lua_State* L)
{
    Log("[CustomTapes] l_RegisterCustomTapes entered\n");

    if (LuaType(L, 1) != LUA_TTABLE)
    {
        PushLuaBool(L, false);
        return 1;
    }

    std::vector<CustomTapeAlbumDefinition> albums;
    std::vector<CustomTapeTrackDefinition> tracks;

    LuaGetField(L, 1, "albums");
    if (LuaType(L, -1) == LUA_TTABLE)
    {
        const std::size_t albumCount = LuaObjLen(L, -1);

        for (std::size_t i = 1; i <= albumCount; ++i)
        {
            LuaRawGetI(L, -1, static_cast<int>(i));
            if (LuaType(L, -1) == LUA_TTABLE)
            {
                CustomTapeAlbumDefinition def;

                const bool hasAlbumId = LuaReadRequiredStringField(L, "albumId", def.albumId);
                const bool hasLangId = LuaReadRequiredStringField(L, "langId", def.langId);
                const bool hasType = LuaReadRequiredStringField(L, "type", def.type);
                def.typeValue = LuaReadOptionalIntField(L, "typeValue", -1);

                if (hasAlbumId && hasLangId && (hasType || def.typeValue >= 0))
                {
                    albums.push_back(def);
                }
            }

            LuaPop(L, 1);
        }
    }
    LuaPop(L, 1);

    LuaGetField(L, 1, "tracks");
    if (LuaType(L, -1) == LUA_TTABLE)
    {
        const std::size_t trackCount = LuaObjLen(L, -1);

        for (std::size_t i = 1; i <= trackCount; ++i)
        {
            LuaRawGetI(L, -1, static_cast<int>(i));
            if (LuaType(L, -1) == LUA_TTABLE)
            {
                CustomTapeTrackDefinition def;

                const bool hasAlbumId = LuaReadRequiredStringField(L, "albumId", def.albumId);
                const bool hasLangId = LuaReadRequiredStringField(L, "langId", def.langId);
                const bool hasFileName = LuaReadRequiredStringField(L, "fileName", def.fileName);

                def.saveIndex = static_cast<std::int16_t>(LuaReadOptionalIntField(L, "saveIndex", -1));
                def.dataTimeJp = LuaReadOptionalUIntField(L, "dataTimeJp", 0);
                def.dataTimeEn = LuaReadOptionalUIntField(L, "dataTimeEn", 0);
                def.important = static_cast<std::uint16_t>(LuaReadOptionalUIntField(L, "important", 0));
                def.special = static_cast<std::uint16_t>(LuaReadOptionalUIntField(L, "special", 0));

                // Optional. If omitted in Lua, defaults to false.
                def.unlocked = LuaReadOptionalUIntField(L, "unlocked", 0) != 0;

                if (hasAlbumId && hasLangId && hasFileName)
                {
                    tracks.push_back(def);
                }
            }

            LuaPop(L, 1);
        }
    }
    LuaPop(L, 1);

    Log("[CustomTapes] parsed albums=%zu tracks=%zu\n", albums.size(), tracks.size());

    const bool ok = Register_CustomTapes(albums, tracks);
    PushLuaBool(L, ok);
    return 1;
}

// Clears all registered custom cassette albums and tracks.
// Params: none
static int __cdecl l_ClearCustomTapes(lua_State* L)
{
    UNREFERENCED_PARAMETER(L);
    Clear_CustomTapes();
    return 0;
}

// Sets one pickable countRaw override by locator index.
// Params: locatorIndex, countRaw
static int __cdecl l_SetPickableCountRawByIndex(lua_State* L)
{
    const int locatorIndex = GetLuaInt(L, 1);
    const int countRaw = GetLuaInt(L, 2);

    const bool ok = Set_TppPickableCountRawByIndex(
        static_cast<std::uint32_t>(locatorIndex),
        static_cast<std::uint32_t>(countRaw));

    PushLuaBool(L, ok);
    return 1;
}

// Gets one pickable countRaw override by locator index.
// Params: locatorIndex
static int __cdecl l_GetPickableCountRawByIndex(lua_State* L)
{
    const int locatorIndex = GetLuaInt(L, 1);

    std::uint16_t countRaw = 0;
    const bool ok = Get_TppPickableCountRawByIndex(
        static_cast<std::uint32_t>(locatorIndex),
        countRaw);

    if (!ok)
    {
        PushLuaBool(L, false);
        return 1;
    }

    PushLuaNumber(L, static_cast<float>(countRaw));
    return 1;
}

// Sets one per-equip icon FTEX path.
// Params: equipId, path
static int __cdecl l_SetEquipIdIconFtexPath(lua_State* L)
{
    const int equipId = GetLuaInt(L, 1);
    const char* rawPath = GetLuaString(L, 2);

    if (!rawPath || !*rawPath)
        return 0;

    EquipIcon_SetEquipIdIconFtexPath(equipId, FoxHashes::PathCode64Ext(rawPath));
    return 0;
}

// Clears one per-equip icon FTEX path.
// Params: equipId
static int __cdecl l_ClearIconFtexPath(lua_State* L)
{
    const int equipId = GetLuaInt(L, 1);
    EquipIcon_ClearIconFtexPath(equipId);
    return 0;
}

// Clears all per-equip icon FTEX paths.
// Params: none
static int __cdecl l_ClearAllIconFtexPaths(lua_State* L)
{
    UNREFERENCED_PARAMETER(L);
    EquipIcon_ClearAllIconFtexPaths();
    return 0;
}

static luaL_Reg g_VFrameWorkLib[] =
{
    { "SetDefaultEquipBgTexturePath",           l_SetDefaultEquipBgTexturePath },
    { "ClearDefaultEquipBgTexture",             l_ClearDefaultEquipBgTexture },
    { "SetEquipBgTexturePath",                  l_SetEquipBgTexturePath },
    { "ClearEquipBgTexture",                    l_ClearEquipBgTexture },
    { "SetEnemyWeaponBgTexturePath",            l_SetEnemyWeaponBgTexturePath },
    { "ClearEnemyWeaponBgTexture",              l_ClearEnemyWeaponBgTexture },
    { "SetEnemyEquipBgTexturePath",             l_SetEnemyEquipBgTexturePath },
    { "ClearEnemyEquipBgTexture",               l_ClearEnemyEquipBgTexture },
    { "ClearAllEquipBgTextures",                l_ClearAllEquipBgTextures },
    { "SetLoadingSplashMainTexturePath",        l_SetLoadingSplashMainTexturePath },
    { "SetLoadingSplashBlurTexturePath",        l_SetLoadingSplashBlurTexturePath },
    { "ClearLoadingSplashTextures",             l_ClearLoadingSplashTextures },
    { "SetGameOverSplashMainTexturePath",       l_SetGameOverSplashMainTexturePath },
    { "SetGameOverSplashBlurTexturePath",       l_SetGameOverSplashBlurTexturePath },
    { "ClearGameOverSplashTextures",            l_ClearGameOverSplashTextures },
    { "SetCautionStepNormalDurationSeconds",    l_SetCautionStepNormalDurationSeconds },
    { "GetCautionStepNormalDurationSeconds",    l_GetCautionStepNormalDurationSeconds },
    { "UnsetCautionStepNormalDurationSeconds",  l_UnsetCautionStepNormalDurationSeconds },
    { "GetCautionStepNormalRemainingSeconds",   l_GetCautionStepNormalRemainingSeconds },
    { "SetPlayerVoiceFpkPathForType",           l_SetPlayerVoiceFpkPathForType },
    { "ClearPlayerVoiceFpkPathForType",         l_ClearPlayerVoiceFpkPathForType },
    { "ClearAllPlayerVoiceFpkOverrides",        l_ClearAllPlayerVoiceFpkOverrides },
    { "SetVIPImportant",                        l_SetVIPImportant },
    { "SetUseConcernedHoldupRecovery",          l_SetUseConcernedHoldupRecovery },
    { "RemoveVIPImportant",                     l_RemoveVIPImportant },
    { "ClearVIPImportant",                      l_ClearVIPImportant },
    { "HoldUpReactionCowardlyReaction",         l_HoldUpReactionCowardlyReactions },
    { "AddCallSignPatrolSoldier",               l_AddCallSignExtraSoldier },
    { "RemoveCallSignPatrolSoldier",            l_RemoveCallSignExtraSoldier },
    { "ClearCallSignPatrolSoldiers",            l_ClearCallSignExtraSoldiers },
    { "SetLostHostage",                         l_SetLostHostage },
    { "RemoveLostHostage",                      l_RemoveLostHostage },
    { "ClearLostHostages",                      l_ClearLostHostages },
    { "SetLostHostageFromPlayer",               l_SetLostHostageFromPlayer },
    { "EnableSoldierStealthCamo",               l_EnableSoldierStealthCamo },
    { "ClearSoldierStealthCamoOverrides",       l_ClearSoldierStealthCamoOverrides },
    { "PlayCassetteTapeByTrackId",              l_PlayCassetteTapeByTrackId },
    { "GetTapeTrackDirectPlayId",               l_GetTapeTrackDirectPlayId },
    { "GetCassettePlayingTime",                 l_GetCassettePlayingTime },
    { "GetCassettePlayingTrackId",              l_GetCassettePlayingTrackId },
    { "PauseCassette",                          l_PauseCassette },
    { "ResumeCassette",                         l_ResumeCassette },
    { "StopCassette",                           l_StopCassette },
    { "IsCassetteSpeakerEnabled",               l_IsCassetteSpeakerEnabled },
    { "SetCassetteSpeakerEnabled",              l_SetCassetteSpeakerEnabled },
    { "RegisterCustomTapes",                    l_RegisterCustomTapes },
    //{ "ClearCustomTapes",                     l_ClearCustomTapes }, automated
    { "SetPickableCountRawByIndex",             l_SetPickableCountRawByIndex },
    { "GetPickableCountRawByIndex",             l_GetPickableCountRawByIndex },
    { "RegisterConstantEquipId",                RegisterConstantEquipId::Lua_RegisterConstantEquipId },
    { "DeclareWPs",                             DeclareWPs::Lua_DeclareWPs },
    { "SetGunBasic",                            l_SetGunBasic },
    { "AddToEquipIdTable",                      EquipIdTableAdd::Lua_AddToEquipIdTable },
    { "AddToEquipDevelopTable",                 EquipDevelopAdd::Lua_AddToEquipDevelopTable },
    { "DeclareSWPs",                            DeclareSWPs::Lua_DeclareSWPs },
    { "SetSupportWeaponType",                   SupportWeaponType::Lua_SetSupportWeaponType },
    { "RemoveSupportWeaponType",                SupportWeaponType::Lua_RemoveSupportWeaponType },
    { "ClearSupportWeaponTypes",                SupportWeaponType::Lua_ClearSupportWeaponTypes },
    { "DeclareRCs",                             DeclareRCs::Lua_DeclareRCs },
    { "DeclareAMs",                             DeclareAMs::Lua_DeclareAMs },
    { "SetEquipParameters",                     EquipParams::Lua_SetEquipParameters },
    { "SetEquipIdIconFtexPath",                 l_SetEquipIdIconFtexPath },
    { "ClearIconFtexPath",                      l_ClearIconFtexPath },
    { "ClearAllIconFtexPaths",                  l_ClearAllIconFtexPaths },
    { nullptr, nullptr }
};

// Registers V_FrameWork into one Lua state once.
// Params: L
static void RegisterAllUiLuaLibraries(lua_State* L)
{
    if (!L)
        return;

    if (IsLuaStateRegistered(L))
        return;

    if (RegisterLuaLibrary(L, "V_FrameWork", g_VFrameWorkLib))
    {
        TrackLuaState(L);
    }
}

// Hooked SetLuaFunctions.
// Params: L
static void __fastcall hkSetLuaFunctions(lua_State* L)
{
    Log("[Hook] SetLuaFunctions invoked: L=%p\n", L);

    if (g_OrigSetLuaFunctions)
    {
        g_OrigSetLuaFunctions(L);
    }

    RegisterAllUiLuaLibraries(L);
}

// Exported require("V_FrameWork") loader.
// Params: L
extern "C" __declspec(dllexport) int __cdecl luaopen_V_FrameWork(lua_State* L)
{
    return RegisterLuaLibrary(L, "V_FrameWork", g_VFrameWorkLib) ? 1 : 0;
}

// Installs the SetLuaFunctions hook.
// Params: none
bool Install_SetLuaFunctions_Hook()
{
    if (g_SetLuaFunctionsHookInstalled)
    {
        Log("[Hook] SetLuaFunctions: already installed\n");
        return true;
    }

    ResolveLuaApi();

    RegisterConstantEquipId::Deps deps{};
    deps.ResolveLuaApi = &ResolveLuaApi;
    deps.GetLuaTop = &GetLuaTop;
    deps.LuaGetField = &LuaGetField;
    deps.LuaType = &LuaType;
    deps.LuaIsString = &LuaIsString;
    deps.LuaIsNumber = &LuaIsNumber;
    deps.LuaPop = &LuaPop;
    deps.GetLuaString = &GetLuaString;
    deps.GetLuaInt = &GetLuaInt;
    deps.PushLuaNumber = &PushLuaNumber;
    deps.LuaPushString = &LuaPushString;
    deps.LuaCreateTable = &LuaCreateTable;
    deps.LuaRawSet = &LuaRawSet;
    deps.LuaSetTable = &LuaSetTable;
    deps.LuaPushNil = &LuaPushNil;
    deps.LuaNext = &LuaNext;

    DeclareWPs::Deps declareWpDeps{};
    declareWpDeps.ResolveLuaApi = &ResolveLuaApi;

    declareWpDeps.GetLuaTop = &GetLuaTop;
    declareWpDeps.LuaGetField = &LuaGetField;
    declareWpDeps.LuaType = &LuaType;
    declareWpDeps.LuaPop = &LuaPop;

    declareWpDeps.GetLuaString = &GetLuaString;
    declareWpDeps.PushLuaNumber = &PushLuaNumber;

    declareWpDeps.LuaPushString = &LuaPushString;
    declareWpDeps.LuaCreateTable = &LuaCreateTable;
    declareWpDeps.LuaRawSet = &LuaRawSet;
    declareWpDeps.LuaSetTable = &LuaSetTable;
    declareWpDeps.GetLuaInt = &GetLuaInt;
    declareWpDeps.LuaPushNil = &LuaPushNil;
    declareWpDeps.LuaNext = &LuaNext;
    RegisterConstantEquipId::Bind(deps);
    DeclareWPs::Bind(declareWpDeps);

    EquipIdTableAdd::Deps equipIdDeps{};
    equipIdDeps.ResolveLuaApi = &ResolveLuaApi;
    equipIdDeps.GetLuaTop = &GetLuaTop;
    equipIdDeps.LuaType = &LuaType;
    equipIdDeps.LuaIsNumber = &LuaIsNumber;
    equipIdDeps.LuaIsString = &LuaIsString;
    equipIdDeps.LuaObjLen = &LuaObjLen;
    equipIdDeps.LuaPop = &LuaPop;

    equipIdDeps.GetLuaString = &GetLuaString;
    equipIdDeps.GetLuaInt = &GetLuaInt;
    equipIdDeps.PushLuaNumber = &PushLuaNumber;

    equipIdDeps.LuaPushString = &LuaPushString;
    equipIdDeps.LuaCreateTable = &LuaCreateTable;
    equipIdDeps.LuaRawSet = &LuaRawSet;
    equipIdDeps.LuaSetTable = &LuaSetTable;
    equipIdDeps.LuaRawGetI = &LuaRawGetI;
    equipIdDeps.LuaPushValue = &LuaPushValue;
    EquipIdTableAdd::Bind(equipIdDeps);


    EquipDevelopAdd::Deps equipDevelopDeps{};
    equipDevelopDeps.ResolveLuaApi = &ResolveLuaApi;
    equipDevelopDeps.GetLuaTop = &GetLuaTop;
    equipDevelopDeps.LuaType = &LuaType;
    equipDevelopDeps.LuaSetTop = &SetLuaTop;
    equipDevelopDeps.GetLuaString = &GetLuaString;
    equipDevelopDeps.GetLuaInt = &GetLuaInt;
    equipDevelopDeps.PushLuaNumber = &PushLuaNumber;
    equipDevelopDeps.LuaPushString = &LuaPushString;
    equipDevelopDeps.LuaCreateTable = &LuaCreateTable;
    equipDevelopDeps.LuaGetTable = &LuaGetTable;
    equipDevelopDeps.LuaSetTable = &LuaSetTable;
    EquipDevelopAdd::Bind(equipDevelopDeps);


    DeclareSWPs::Deps declareSwpDeps{};
    declareSwpDeps.ResolveLuaApi = &ResolveLuaApi;
    declareSwpDeps.GetLuaTop = &GetLuaTop;
    declareSwpDeps.LuaGetField = &LuaGetField;
    declareSwpDeps.LuaType = &LuaType;
    declareSwpDeps.LuaPop = &LuaPop;
    declareSwpDeps.GetLuaString = &GetLuaString;
    declareSwpDeps.PushLuaNumber = &PushLuaNumber;
    declareSwpDeps.LuaPushString = &LuaPushString;
    declareSwpDeps.LuaCreateTable = &LuaCreateTable;
    declareSwpDeps.LuaRawSet = &LuaRawSet;
    declareSwpDeps.LuaSetTable = &LuaSetTable;
    DeclareSWPs::Bind(declareSwpDeps);

    SupportWeaponType::Deps supportWeaponTypeDeps{};
    supportWeaponTypeDeps.ResolveLuaApi = &ResolveLuaApi;
    supportWeaponTypeDeps.LuaType = &LuaType;
    supportWeaponTypeDeps.GetLuaInt = &GetLuaInt;
    SupportWeaponType::Bind(supportWeaponTypeDeps);

    DeclareRCs::Deps declareRcDeps{};
    declareRcDeps.ResolveLuaApi = &ResolveLuaApi;

    declareRcDeps.GetLuaTop = &GetLuaTop;
    declareRcDeps.LuaGetField = &LuaGetField;
    declareRcDeps.LuaType = &LuaType;
    declareRcDeps.LuaPop = &LuaPop;

    declareRcDeps.GetLuaString = &GetLuaString;
    declareRcDeps.GetLuaInt = &GetLuaInt;
    declareRcDeps.PushLuaNumber = &PushLuaNumber;

    declareRcDeps.LuaPushString = &LuaPushString;
    declareRcDeps.LuaCreateTable = &LuaCreateTable;
    declareRcDeps.LuaRawSet = &LuaRawSet;
    declareRcDeps.LuaSetTable = &LuaSetTable;

    declareRcDeps.LuaPushNil = &LuaPushNil;
    declareRcDeps.LuaNext = &LuaNext;

    DeclareRCs::Bind(declareRcDeps);


    DeclareAMs::Deps declareAmDeps{};
    declareAmDeps.ResolveLuaApi = &ResolveLuaApi;
    declareAmDeps.GetLuaTop = &GetLuaTop;
    declareAmDeps.LuaType = &LuaType;
    declareAmDeps.GetLuaString = &GetLuaString;
    declareAmDeps.GetLuaInt = &GetLuaInt;
    declareAmDeps.LuaSetTop = &SetLuaTop;
    declareAmDeps.PushLuaNumber = &PushLuaNumber;
    declareAmDeps.LuaPushString = &LuaPushString;
    declareAmDeps.LuaCreateTable = &LuaCreateTable;
    declareAmDeps.LuaGetField = &LuaGetField;
    declareAmDeps.LuaSetTable = &LuaSetTable;
    declareAmDeps.LuaPushNil = &LuaPushNil;
    declareAmDeps.LuaNext = &LuaNext;
    declareAmDeps.LuaRawSet = &LuaRawSet;
    DeclareAMs::Bind(declareAmDeps);

    EquipParams::Deps equipParamsDeps{};
    equipParamsDeps.ResolveLuaApi = &ResolveLuaApi;
    equipParamsDeps.GetLuaTop = &GetLuaTop;
    equipParamsDeps.LuaType = &LuaType;
    equipParamsDeps.GetLuaInt = &GetLuaInt;
    equipParamsDeps.GetLuaNumber = &LuaToNumber;
    equipParamsDeps.GetLuaString = &GetLuaString;
    equipParamsDeps.LuaObjLen = &LuaObjLen;
    equipParamsDeps.LuaSetTop = &SetLuaTop;
    equipParamsDeps.PushLuaNumber = &PushLuaNumber;
    equipParamsDeps.LuaPushString = &LuaPushString;
    equipParamsDeps.LuaCreateTable = &LuaCreateTable;
    equipParamsDeps.LuaGetField = &LuaGetField;
    equipParamsDeps.LuaRawGetI = &LuaRawGetI;
    equipParamsDeps.LuaGetTable = &LuaGetTable;
    equipParamsDeps.LuaSetTable = &LuaSetTable;
    equipParamsDeps.LuaPushValue = &LuaPushValue;
    EquipParams::Bind(equipParamsDeps);


    const uintptr_t setLuaFunctionsAddr = GetLuaBridgeAddress(gAddr.SetLuaFunctions, BOOTSTRAP_EN_SetLuaFunctions);
    void* target = ResolveGameAddress(setLuaFunctionsAddr);
    if (!target)
        return false;

    const bool ok = CreateAndEnableHook(
        target,
        reinterpret_cast<void*>(&hkSetLuaFunctions),
        reinterpret_cast<void**>(&g_OrigSetLuaFunctions));

    if (ok)
    {
        g_SetLuaFunctionsHookInstalled = true;
    }

    Log("[Hook] SetLuaFunctions: %s target=%p orig=%p\n",
        ok ? "OK" : "FAIL",
        target,
        g_OrigSetLuaFunctions);
    return ok;
}

// Removes the SetLuaFunctions hook.
// Params: none
bool Uninstall_SetLuaFunctions_Hook()
{
    const uintptr_t setLuaFunctionsAddr = GetLuaBridgeAddress(gAddr.SetLuaFunctions, BOOTSTRAP_EN_SetLuaFunctions);
    DisableAndRemoveHook(ResolveGameAddress(setLuaFunctionsAddr));
    g_OrigSetLuaFunctions = nullptr;
    ClearTrackedLuaStates();
    return true;
}