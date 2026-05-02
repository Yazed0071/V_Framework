#include "pch.h"
extern "C" {
    #include "lua.h"
    #include "lauxlib.h"
    #include "lualib.h"
}

#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <unordered_set>
#include <mutex>
#include <string>
#include <vector>

#include "HookUtils.h"
#include "log.h"
#include "FoxHashes.h"
#include "UiTextureOverrides.h"
#include "CautionStepNormalTimerHook.h"
#include "VIPSleepFaintHook.h"
#include "VIPHoldupHook.h"
#include "VIPRadioHook.h"
#include "State_EnterStandHoldup1.h"
#include "GetVoiceParamWithCallSign.h"
#include "LostHostageHook.h"
#include "StepRadioDiscovery.h"
#include "ActionCoreImpl_UpdateOptCamo.h"
#include "MbDvcCassetteTapeCallbackImpl_PlayOrPauseSelectedTrack.h"
#include "GetTapeTrackDirectPlayId.h"
#include "SoundSystemImpl_BeginSoundSystem.h"
#include "SoundMusicPlayer_SetupMusicInfos.h"
#include "CustomTapeOwnership.h"
#include "TppPickableRuntime.h"

#include "AddressSet.h"
#include "RegisterConstantEquipId.h"
#include "DeclareWPs.h"
#include "EquipParameters_GunBasic.h"
#include "EquipIdTable_AddToEquipIdTable.h"
#include "EquipDevelop_AddToEquipDevelopTable.h"
#include "DeclareSWPs.h"
#include "SetSupportWeaponTypeId.h"
#include "DeclareRCs.h"
#include "DeclareAMs.h"
#include "SetEquipParameters.h"
#include "utility_GetIconFtexPath.h"
#include "PlayerVoiceFpkHook.h"
#include "SoldierRtpcHook.h"
#include "V_FrameWorkModLoader.h"
#include "V_FrameWorkState.h"
#include "InitCamoufTable.h"
#include "../hooks/outfit/CustomHeadRegistry.h"
#include "../hooks/outfit/OutfitRegistry.h"
#include "../hooks/outfit/OutfitRuntimeParts.h"
#include "../hooks/outfit/OutfitSuitConditionApply.h"


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


static uintptr_t GetLuaBridgeAddress(uintptr_t resolvedAddr, uintptr_t bootstrapAddr)
{
    return resolvedAddr ? resolvedAddr : bootstrapAddr;
}


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


static int GetLuaTop(lua_State* L)
{
    if (!ResolveLuaApi() || !g_lua_gettop)
        return 0;

    return g_lua_gettop(L);
}


static void SetLuaTop(lua_State* L, int idx)
{
    if (!ResolveLuaApi() || !g_lua_settop)
        return;

    g_lua_settop(L, idx);
}


static void LuaGetField(lua_State* L, int idx, const char* fieldName)
{
    if (!ResolveLuaApi() || !g_lua_getfield || !fieldName)
        return;

    g_lua_getfield(L, idx, const_cast<char*>(fieldName));
}


static void LuaRawGetI(lua_State* L, int idx, int n)
{
    if (!ResolveLuaApi() || !g_lua_rawgeti)
        return;

    g_lua_rawgeti(L, idx, n);
}


static int LuaType(lua_State* L, int idx)
{
    if (!ResolveLuaApi() || !g_lua_type)
        return -1;

    return g_lua_type(L, idx);
}


static bool LuaIsString(lua_State* L, int idx)
{
    if (!ResolveLuaApi() || !g_lua_isstring)
        return false;

    return g_lua_isstring(L, idx) != 0;
}


static bool LuaIsNumber(lua_State* L, int idx)
{
    if (!ResolveLuaApi() || !g_lua_isnumber)
        return false;

    return g_lua_isnumber(L, idx) != 0;
}


static size_t LuaObjLen(lua_State* L, int idx)
{
    if (!ResolveLuaApi() || !g_lua_objlen)
        return 0;

    return g_lua_objlen(L, idx);
}


static void PushLuaBool(lua_State* L, bool value)
{
    if (!ResolveLuaApi() || !g_lua_pushboolean)
        return;

    g_lua_pushboolean(L, value ? 1 : 0);
}


static void LuaPop(lua_State* L, int count)
{
    if (!ResolveLuaApi() || !g_lua_settop)
        return;

    g_lua_settop(L, -count - 1);
}


static bool RegisterLuaLibrary(lua_State* L, const char* libName, luaL_Reg* funcs)
{
    if (!ResolveLuaApi() || !L || !libName || !funcs)
        return false;

    g_FoxLuaRegisterLibrary(L, libName, funcs);
    Log("[V_FrameWork] Registered library: %s (L=%p)\n", libName, L);
    return true;
}


static const char* GetLuaString(lua_State* L, int idx)
{
    if (!ResolveLuaApi() || !g_lua_tolstring)
        return nullptr;

    return g_lua_tolstring(L, idx, nullptr);
}


static int GetLuaInt(lua_State* L, int idx)
{
    if (!ResolveLuaApi() || !g_lua_tointeger)
        return 0;

    return static_cast<int>(g_lua_tointeger(L, idx));
}


static std::uint64_t GetLuaInt64(lua_State* L, int idx)
{
    if (!ResolveLuaApi() || !g_lua_tointeger)
        return 0;

    return static_cast<std::uint64_t>(g_lua_tointeger(L, idx));
}


static bool GetLuaBool(lua_State* L, int idx)
{
    if (!ResolveLuaApi() || !g_lua_toboolean)
        return false;

    return g_lua_toboolean(L, idx) != 0;
}


static float GetLuaNumber(lua_State* L, int idx)
{
    if (!ResolveLuaApi() || !g_lua_tonumber)
        return 0.0f;

    return static_cast<float>(g_lua_tonumber(L, idx));
}


static void PushLuaNumber(lua_State* L, float value)
{
    if (!ResolveLuaApi() || !g_lua_pushnumber)
        return;

    g_lua_pushnumber(L, static_cast<lua_Number>(value));
}


static bool IsLuaStateRegistered(lua_State* L)
{
    std::lock_guard<std::mutex> lock(g_RegisteredLuaStatesMutex);
    return g_RegisteredLuaStates.find(L) != g_RegisteredLuaStates.end();
}


static void TrackLuaState(lua_State* L)
{
    std::lock_guard<std::mutex> lock(g_RegisteredLuaStatesMutex);
    g_RegisteredLuaStates.insert(L);
}


static void ClearTrackedLuaStates()
{
    std::lock_guard<std::mutex> lock(g_RegisteredLuaStatesMutex);
    g_RegisteredLuaStates.clear();
}


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

static bool TryReadTableIntField(lua_State* L, int tableIndex, const char* fieldName, int& outValue)
{
    outValue = 0;
    LuaGetField(L, tableIndex, const_cast<char*>(fieldName));

    const bool ok = (LuaType(L, -1) == 3);
    if (ok)
        outValue = GetLuaInt(L, -1);

    SetLuaTop(L, -2);
    return ok;
}

static bool TryReadTableStringField(lua_State* L, int tableIndex, const char* fieldName, const char*& outValue)
{
    outValue = nullptr;
    LuaGetField(L, tableIndex, const_cast<char*>(fieldName));

    const bool ok = (LuaType(L, -1) == 4);
    if (ok)
        outValue = GetLuaString(L, -1);

    SetLuaTop(L, -2);
    return ok;
}

static bool TryReadTableBoolField(lua_State* L, int tableIndex, const char* fieldName, bool defaultValue)
{
    LuaGetField(L, tableIndex, const_cast<char*>(fieldName));

    const int type = LuaType(L, -1);
    bool result = defaultValue;

    if (type != 0)
        result = GetLuaBool(L, -1) != 0;

    SetLuaTop(L, -2);
    return result;
}


static bool TryReadTableSubAssetField(
    lua_State* L, int tableIndex, const char* fieldName,
    const char*& outPath, bool& outVanilla, bool defaultVanilla)
{
    outPath = nullptr;
    outVanilla = defaultVanilla;

    LuaGetField(L, tableIndex, const_cast<char*>(fieldName));
    const int type = LuaType(L, -1);

    if (type == 4)
    {
        outPath = GetLuaString(L, -1);
        outVanilla = false;
    }
    else if (type == 1)
    {
        outVanilla = GetLuaBool(L, -1);
    }


    SetLuaTop(L, -2);
    return true;
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


static int __cdecl l_SetDefaultEquipBgTexturePath(lua_State* L)
{
    const char* rawPath = GetLuaString(L, 1);
    if (!rawPath || !*rawPath)
        return 0;

    EquipBg_SetDefaultTexture(FoxHashes::PathCode64Ext(rawPath));
    return 0;
}


static int __cdecl l_ClearDefaultEquipBgTexture(lua_State* L)
{
    UNREFERENCED_PARAMETER(L);
    EquipBg_ClearDefaultTexture();
    return 0;
}


static int __cdecl l_SetEnemyWeaponBgTexturePath(lua_State* L)
{
    const char* rawPath = GetLuaString(L, 1);
    if (!rawPath || !*rawPath)
        return 0;

    EquipBg_SetEnemyWeaponTexture(FoxHashes::PathCode64Ext(rawPath));
    return 0;
}


static int __cdecl l_ClearEnemyWeaponBgTexture(lua_State* L)
{
    UNREFERENCED_PARAMETER(L);
    EquipBg_ClearEnemyWeaponTexture();
    return 0;
}


static int __cdecl l_SetEnemyEquipBgTexturePath(lua_State* L)
{
    const int equipId = GetLuaInt(L, 1);
    const char* rawPath = GetLuaString(L, 2);

    if (!rawPath || !*rawPath)
        return 0;

    EquipBg_SetEnemyEquipTexture(equipId, FoxHashes::PathCode64Ext(rawPath));
    return 0;
}


static int __cdecl l_ClearEnemyEquipBgTexture(lua_State* L)
{
    const int equipId = GetLuaInt(L, 1);
    EquipBg_ClearEnemyEquipTexture(equipId);
    return 0;
}


static int __cdecl l_SetEquipBgTexturePath(lua_State* L)
{
    const int equipId = GetLuaInt(L, 1);
    const char* rawPath = GetLuaString(L, 2);

    if (!rawPath || !*rawPath)
        return 0;

    EquipBg_SetEquipTexture(equipId, FoxHashes::PathCode64Ext(rawPath));
    return 0;
}


static int __cdecl l_ClearEquipBgTexture(lua_State* L)
{
    const int equipId = GetLuaInt(L, 1);
    EquipBg_ClearEquipTexture(equipId);
    return 0;
}


static int __cdecl l_ClearAllEquipBgTextures(lua_State* L)
{
    UNREFERENCED_PARAMETER(L);
    EquipBg_ClearAllEquipTextures();
    return 0;
}


static int __cdecl l_SetLoadingSplashMainTexturePath(lua_State* L)
{
    const char* rawPath = GetLuaString(L, 1);
    if (!rawPath || !*rawPath)
        return 0;

    LoadingSplash_SetMainTexture(FoxHashes::PathCode64Ext(rawPath));
    return 0;
}


static int __cdecl l_SetLoadingSplashBlurTexturePath(lua_State* L)
{
    const char* rawPath = GetLuaString(L, 1);
    if (!rawPath || !*rawPath)
        return 0;

    LoadingSplash_SetBlurTexture(FoxHashes::PathCode64Ext(rawPath));
    return 0;
}


static int __cdecl l_ClearLoadingSplashTextures(lua_State* L)
{
    UNREFERENCED_PARAMETER(L);
    LoadingSplash_ClearTextures();
    return 0;
}


static int __cdecl l_SetGameOverSplashMainTexturePath(lua_State* L)
{
    const char* rawPath = GetLuaString(L, 1);
    if (!rawPath || !*rawPath)
        return 0;

    GameOverSplash_SetMainTexture(FoxHashes::PathCode64Ext(rawPath));
    return 0;
}


static int __cdecl l_SetGameOverSplashBlurTexturePath(lua_State* L)
{
    const char* rawPath = GetLuaString(L, 1);
    if (!rawPath || !*rawPath)
        return 0;

    GameOverSplash_SetBlurTexture(FoxHashes::PathCode64Ext(rawPath));
    return 0;
}


static int __cdecl l_ClearGameOverSplashTextures(lua_State* L)
{
    UNREFERENCED_PARAMETER(L);
    GameOverSplash_ClearTextures();
    return 0;
}


static int l_SetCautionStepNormalDurationSeconds(lua_State* L)
{
    const float seconds = GetLuaNumber(L, 1);
    Set_CautionStepNormalDurationSeconds(seconds);
    return 0;
}


static int l_GetCautionStepNormalDurationSeconds(lua_State* L)
{
    PushLuaNumber(L, Get_CautionStepNormalDurationSeconds());
    return 1;
}


static int l_UnsetCautionStepNormalDurationSeconds(lua_State* L)
{
    UNREFERENCED_PARAMETER(L);
    Unset_CautionStepNormalDurationSeconds();
    return 0;
}


static int l_GetCautionStepNormalRemainingSeconds(lua_State* L)
{
    PushLuaNumber(L, Get_CautionStepNormalRemainingSeconds());
    return 1;
}


static int __cdecl l_SetPlayerVoiceFpkPathForType(lua_State* L)
{
    const int playerType = GetLuaInt(L, 1);
    const char* rawPath = GetLuaString(L, 2);

    if (!rawPath || !*rawPath)
        return 0;

    Set_PlayerVoiceFpkPathForType(static_cast<std::uint32_t>(playerType), rawPath);
    return 0;
}


static int __cdecl l_ClearPlayerVoiceFpkPathForType(lua_State* L)
{
    const int playerType = GetLuaInt(L, 1);
    Clear_PlayerVoiceFpkPathForType(static_cast<std::uint32_t>(playerType));
    return 0;
}


static int __cdecl l_ClearAllPlayerVoiceFpkOverrides(lua_State* L)
{
    UNREFERENCED_PARAMETER(L);
    Clear_AllPlayerVoiceFpkOverrides();
    return 0;
}


static int __cdecl l_SetSoldierRtpc(lua_State* L)
{
    const std::uint32_t goId = static_cast<std::uint32_t>(GetLuaInt64(L, 1));
    const char* rtpcName = GetLuaString(L, 2);
    const float value = GetLuaNumber(L, 3);
    const long timeMs = static_cast<long>(GetLuaInt(L, 4));

    const int result = SoldierRtpc::SetSoldierRtpc(goId, rtpcName, value, timeMs);
    PushLuaNumber(L, static_cast<float>(result));
    return 1;
}


static int __cdecl l_SetGlobalRtpc(lua_State* L)
{
    const char* rtpcName = GetLuaString(L, 1);
    const float value = GetLuaNumber(L, 2);
    const long timeMs = static_cast<long>(GetLuaInt(L, 3));

    const int result = SoldierRtpc::SetGlobalRtpc(rtpcName, value, timeMs);
    PushLuaNumber(L, static_cast<float>(result));
    return 1;
}


static int __cdecl l_SetSoldierRtpcById(lua_State* L)
{
    const std::uint32_t goId   = static_cast<std::uint32_t>(GetLuaInt64(L, 1));
    const std::uint32_t rtpcId = static_cast<std::uint32_t>(GetLuaInt64(L, 2));
    const float         value  = GetLuaNumber(L, 3);
    const long          timeMs = static_cast<long>(GetLuaInt(L, 4));

    const int result = SoldierRtpc::SetSoldierRtpcById(goId, rtpcId, value, timeMs);
    PushLuaNumber(L, static_cast<float>(result));
    return 1;
}


static int __cdecl l_SetGlobalRtpcById(lua_State* L)
{
    const std::uint32_t rtpcId = static_cast<std::uint32_t>(GetLuaInt64(L, 1));
    const float         value  = GetLuaNumber(L, 2);
    const long          timeMs = static_cast<long>(GetLuaInt(L, 3));

    const int result = SoldierRtpc::SetGlobalRtpcById(rtpcId, value, timeMs);
    PushLuaNumber(L, static_cast<float>(result));
    return 1;
}


static int __cdecl l_SetVIPImportant(lua_State* L)
{
    const std::uint32_t gameObjectId = static_cast<std::uint32_t>(GetLuaInt64(L, 1));
    const bool isOfficer = GetLuaBool(L, 2);

    Add_VIPSleepFaintImportantGameObjectId(gameObjectId, isOfficer);
    Add_VIPHoldupImportantGameObjectId(gameObjectId, isOfficer);
    Add_VIPRadioImportantGameObjectId(gameObjectId, isOfficer);
    return 0;
}


static int __cdecl l_RemoveVIPImportant(lua_State* L)
{
    const std::uint32_t gameObjectId = static_cast<std::uint32_t>(GetLuaInt64(L, 1));

    Remove_VIPSleepFaintImportantGameObjectId(gameObjectId);
    Remove_VIPHoldupImportantGameObjectId(gameObjectId);
    Remove_VIPRadioImportantGameObjectId(gameObjectId);
    return 0;
}


static int __cdecl l_ClearVIPImportant(lua_State* L)
{
    UNREFERENCED_PARAMETER(L);

    Clear_VIPSleepFaintImportantGameObjectIds();
    Clear_VIPHoldupImportantGameObjectIds();
    Clear_VIPRadioImportantGameObjectIds();
    return 0;
}


static int l_SetUseConcernedHoldupRecovery(lua_State* L)
{
    const bool enabled = GetLuaBool(L, 1) != 0;
    Set_UseCustomNonVipHoldupRecovery(enabled);
    return 0;
}


static int l_HoldUpReactionCowardlyReactions(lua_State* L)
{
    const bool enabled = GetLuaBool(L, 1);
    Set_HoldUpReactionCowardlyReactions(enabled);
    return 0;
}


static int __cdecl l_AddCallSignExtraSoldier(lua_State* L)
{
    const std::uint32_t gameObjectId = static_cast<std::uint32_t>(GetLuaInt64(L, 1));
    Add_CallSignExtraSoldier(gameObjectId);
    return 0;
}


static int __cdecl l_RemoveCallSignExtraSoldier(lua_State* L)
{
    const std::uint32_t gameObjectId = static_cast<std::uint32_t>(GetLuaInt64(L, 1));
    Remove_CallSignExtraSoldier(gameObjectId);
    return 0;
}


static int __cdecl l_ClearCallSignExtraSoldiers(lua_State* L)
{
    UNREFERENCED_PARAMETER(L);
    Clear_CallSignExtraSoldiers();
    return 0;
}


static int __cdecl l_SetLostHostage(lua_State* L)
{
    const std::uint32_t gameObjectId = static_cast<std::uint32_t>(GetLuaInt64(L, 1));
    const int hostageType = GetLuaInt(L, 2);

    Add_LostHostageTrap(gameObjectId, hostageType);
    Add_LostHostageDiscovery(gameObjectId, hostageType);
    return 0;
}


static int __cdecl l_RemoveLostHostage(lua_State* L)
{
    const std::uint32_t gameObjectId = static_cast<std::uint32_t>(GetLuaInt64(L, 1));

    Remove_LostHostageTrap(gameObjectId);
    Remove_LostHostageDiscovery(gameObjectId);
    return 0;
}


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


static int __cdecl l_EnableSoldierStealthCamo(lua_State* L)
{
    const std::uint32_t mappedIndex = static_cast<std::uint32_t>(GetLuaInt(L, 1));
    const bool enabled = GetLuaBool(L, 2);
    Set_UpdateOptCamoEnableMappedIndex(mappedIndex, enabled);
    return 0;
}


static int __cdecl l_ClearSoldierStealthCamoOverrides(lua_State* L)
{
    UNREFERENCED_PARAMETER(L);
    Clear_UpdateOptCamoMappedIndexOverrides();
    return 0;
}


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


static int __cdecl l_SetCassetteSpeakerEnabled(lua_State* L)
{
    const bool enabled = GetLuaBool(L, 1);
    const bool ok = SetCassetteSpeakerEnabled(enabled);
    PushLuaBool(L, ok);
    return 1;
}


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


static int __cdecl l_ClearCustomTapes(lua_State* L)
{
    UNREFERENCED_PARAMETER(L);
    Clear_CustomTapes();
    return 0;
}


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


static int __cdecl l_SetEquipIdIconFtexPath(lua_State* L)
{
    const int equipId = GetLuaInt(L, 1);
    const char* rawPath = GetLuaString(L, 2);

    if (!rawPath || !*rawPath)
        return 0;

    EquipIcon_SetEquipIdIconFtexPath(equipId, FoxHashes::PathCode64Ext(rawPath));
    return 0;
}


static int __cdecl l_ClearIconFtexPath(lua_State* L)
{
    const int equipId = GetLuaInt(L, 1);
    EquipIcon_ClearIconFtexPath(equipId);
    return 0;
}


static int __cdecl l_ClearAllIconFtexPaths(lua_State* L)
{
    UNREFERENCED_PARAMETER(L);
    EquipIcon_ClearAllIconFtexPaths();
    return 0;
}

static int __cdecl l_GetModFiles(lua_State* L)
{
    const auto files = V_FrameWorkModLoader::FindModFiles();

    if (!ResolveLuaApi())
    {
        PushLuaBool(L, false);
        return 1;
    }

    g_lua_createtable(L, static_cast<int>(files.size()), 0);
    const int tableIdx = GetLuaTop(L);

    for (std::size_t i = 0; i < files.size(); ++i)
    {
        g_lua_pushnumber(L, static_cast<lua_Number>(i + 1));
        g_lua_pushstring(L, const_cast<char*>(files[i].c_str()));
        g_lua_settable(L, tableIdx);
    }

    return 1;
}


static int __cdecl l_Log(lua_State* L)
{
    const char* msg = GetLuaString(L, 1);
    if (msg && *msg)
    {


        Log("%s\n", msg);
    }
    return 0;
}


static int __cdecl l_SetCamoValue(lua_State* L)
{
    const int camoType     = GetLuaInt(L, 1);
    const int materialType = GetLuaInt(L, 2);
    const int value        = GetLuaInt(L, 3);

    const bool ok = CamoufTable::Set_CamoValue(camoType, materialType, value);
    if (ok) CamoufTable::PushCamoTableToGame(L);
    PushLuaBool(L, ok);
    return 1;
}


static int __cdecl l_CloneCamoRow(lua_State* L)
{
    const int dst = GetLuaInt(L, 1);
    const int src = GetLuaInt(L, 2);

    const bool ok = CamoufTable::Clone_CamoRow(dst, src);
    if (ok) CamoufTable::PushCamoTableToGame(L);
    PushLuaBool(L, ok);
    return 1;
}


static int __cdecl l_ImportCamoRow(lua_State* L)
{
    const int camoType = GetLuaInt(L, 1);
    if (LuaType(L, 2) != 5)
    {
        PushLuaBool(L, false);
        return 1;
    }

    const size_t count = LuaObjLen(L, 2);
    const size_t cap   = count < CamoufTable::kMaxMaterialTypes
        ? count : CamoufTable::kMaxMaterialTypes;

    std::int32_t buf[CamoufTable::kMaxMaterialTypes] = {};
    for (size_t i = 0; i < cap; ++i)
    {
        LuaRawGetI(L, 2, static_cast<int>(i + 1));
        buf[i] = GetLuaInt(L, -1);
        LuaPop(L, 1);
    }

    const bool ok = CamoufTable::ImportCamoRow(camoType, buf, cap);
    if (ok) CamoufTable::PushCamoTableToGame(L);
    PushLuaBool(L, ok);
    return 1;
}


static int __cdecl l_ImportCamoTable(lua_State* L)
{
    if (LuaType(L, 1) != 5)
    {
        PushLuaBool(L, false);
        return 1;
    }

    const size_t rowCount = LuaObjLen(L, 1);
    const size_t rowCap   = rowCount < CamoufTable::kMaxCamoTypes
        ? rowCount : CamoufTable::kMaxCamoTypes;

    static constexpr size_t kCols  = CamoufTable::kMaxMaterialTypes;
    static constexpr size_t kCells = CamoufTable::kMaxCamoTypes * kCols;
    std::int32_t buf[kCells] = {};

    for (size_t r = 0; r < rowCap; ++r)
    {
        LuaRawGetI(L, 1, static_cast<int>(r + 1));
        if (LuaType(L, -1) == 5)
        {
            const size_t colCount = LuaObjLen(L, -1);
            const size_t colCap   = colCount < kCols ? colCount : kCols;
            for (size_t c = 0; c < colCap; ++c)
            {
                LuaRawGetI(L, -1, static_cast<int>(c + 1));
                buf[r * kCols + c] = GetLuaInt(L, -1);
                LuaPop(L, 1);
            }
        }
        LuaPop(L, 1);
    }

    const bool ok = CamoufTable::ImportCamoTable(buf, rowCap, kCols);
    if (ok) CamoufTable::PushCamoTableToGame(L);
    PushLuaBool(L, ok);
    return 1;
}


static int __cdecl l_GetCamoValue(lua_State* L)
{
    const int camoType     = GetLuaInt(L, 1);
    const int materialType = GetLuaInt(L, 2);
    const std::int32_t v   = CamoufTable::Get_CamoValue(camoType, materialType);
    PushLuaNumber(L, static_cast<float>(v));
    return 1;
}


namespace
{


    std::uint8_t ParseOutfitPlayerType(lua_State* L, int tableIndex)
    {
        LuaGetField(L, tableIndex, "playerType");
        const int type = LuaType(L, -1);

        std::uint8_t result = 0xFF;

        if (type == 4)
        {
            const char* s = GetLuaString(L, -1);
            if (s)
            {
                if      (_stricmp(s, "Snake")    == 0) result = outfit::kPlayerType_Snake;
                else if (_stricmp(s, "DDMale")   == 0) result = outfit::kPlayerType_DDMale;
                else if (_stricmp(s, "DDFemale") == 0) result = outfit::kPlayerType_DDFemale;
                else if (_stricmp(s, "Avatar")   == 0) result = outfit::kPlayerType_Avatar;
            }
        }
        else if (type == 3)
        {
            const int v = GetLuaInt(L, -1);
            if (v >= 0 && v <= 3) result = static_cast<std::uint8_t>(v);
        }

        SetLuaTop(L, -2);
        return result;
    }


    std::uint64_t ReadSubAssetField(
        lua_State* L, int tableIndex, const char* fieldName,
        std::uint64_t defaultValue)
    {
        LuaGetField(L, tableIndex, fieldName);
        const int type = LuaType(L, -1);

        std::uint64_t result = defaultValue;

        if (type == 4)
        {
            const char* s = GetLuaString(L, -1);
            if (s && s[0] != '\0')
                result = FoxHashes::PathCode64Ext(s);
        }
        else if (type == 1)
        {
            const bool b = GetLuaBool(L, -1) != 0;
            result = b ? outfit::kSubAssetUseVanilla : outfit::kSubAssetDisabled;
        }


        SetLuaTop(L, -2);
        return result;
    }


    std::uint64_t ReadRequiredPathField(
        lua_State* L, int tableIndex, const char* fieldName)
    {
        LuaGetField(L, tableIndex, fieldName);
        const int type = LuaType(L, -1);

        std::uint64_t result = 0;
        if (type == 4)
        {
            const char* s = GetLuaString(L, -1);
            if (s && s[0] != '\0')
                result = FoxHashes::PathCode64Ext(s);
        }

        SetLuaTop(L, -2);
        return result;
    }


    struct HeadAlias
    {
        const char*    name;
        std::uint16_t  equipId;
    };
    static constexpr HeadAlias k_HeadAliases[] = {
        { "none",             0x400 },
        { "bandana",          0x20E },
        { "infinitebandana",  0x20F },
        { "balaclava",        0x210 },
        { "spheadgear",       0x211 },
        { "hpheadgear",       0x212 },
    };


    static void NormalizeHeadAlias(const char* in, char* out, std::size_t cap)
    {
        std::size_t j = 0;
        if (cap == 0) return;
        for (std::size_t i = 0; in && in[i] && j + 1 < cap; ++i)
        {
            const char c = in[i];
            if (c == '-' || c == '_' || c == ' ') continue;
            out[j++] = (c >= 'A' && c <= 'Z') ? (c - 'A' + 'a') : c;
        }
        out[j] = '\0';
    }

    static std::uint16_t TryResolveHeadAlias(const char* name)
    {
        if (!name || !name[0]) return 0;
        char norm[64];
        NormalizeHeadAlias(name, norm, sizeof(norm));
        for (const HeadAlias& a : k_HeadAliases)
        {
            if (std::strcmp(norm, a.name) == 0)
                return a.equipId;
        }
        return 0;
    }


    struct MaterialNameEntry
    {
        const char*     name;
        std::int32_t    index;
    };
    static constexpr MaterialNameEntry k_MaterialNames[] = {
        {"MTR_IRON_A", 0},  {"MTR_IRON_B", 1},  {"MTR_IRON_C", 2},  {"MTR_IRON_D", 3},
        {"MTR_IRON_E", 4},  {"MTR_IRON_F", 5},  {"MTR_IRON_G", 6},  {"MTR_IRON_M", 7},
        {"MTR_IRON_N", 8},  {"MTR_IRON_W", 9},
        {"MTR_PIPE_A", 10}, {"MTR_PIPE_B", 11}, {"MTR_PIPE_S", 12},
        {"MTR_TIN_A",  13},
        {"MTR_FENC_A", 14}, {"MTR_FENC_B", 15}, {"MTR_FENC_F", 16},
        {"MTR_CONC_A", 17}, {"MTR_CONC_B", 18},
        {"MTR_BRIC_A", 19},
        {"MTR_PLAS_A", 20}, {"MTR_PLAS_B", 21}, {"MTR_PLAS_W", 22},
        {"MTR_PAPE_A", 23}, {"MTR_PAPE_B", 24}, {"MTR_PAPE_C", 25}, {"MTR_PAPE_D", 26},
        {"MTR_RUBB_A", 27}, {"MTR_RUBB_B", 28},
        {"MTR_CLOT_A", 29}, {"MTR_CLOT_B", 30}, {"MTR_CLOT_C", 31}, {"MTR_CLOT_D", 32},
        {"MTR_CLOT_E", 33},
        {"MTR_GLAS_A", 34}, {"MTR_GLAS_B", 35}, {"MTR_GLAS_C", 36},
        {"MTR_VINL_A", 37}, {"MTR_VINL_W", 38},
        {"MTR_TILE_A", 39},
        {"MTR_TLRF_A", 40},
        {"MTR_ALRM_A", 41},
        {"MTR_COPS_A", 42}, {"MTR_COPS_B", 43},
        {"MTR_BRIR_A", 44},
        {"MTR_BLOD_A", 45},
        {"MTR_SOIL_A", 46}, {"MTR_SOIL_B", 47}, {"MTR_SOIL_C", 48}, {"MTR_SOIL_D", 49},
        {"MTR_SOIL_E", 50}, {"MTR_SOIL_F", 51}, {"MTR_SOIL_G", 52}, {"MTR_SOIL_H", 53},
        {"MTR_SOIL_R", 54}, {"MTR_SOIL_W", 55},
        {"MTR_GRAV_A", 56},
        {"MTR_SAND_A", 57}, {"MTR_SAND_B", 58}, {"MTR_SAND_C", 59},
        {"MTR_LEAF",   60}, {"MTR_RLEF",   61}, {"MTR_RLEF_B", 62},
        {"MTR_WOOD_A", 63}, {"MTR_WOOD_B", 64}, {"MTR_WOOD_C", 65}, {"MTR_WOOD_D", 66},
        {"MTR_WOOD_G", 67}, {"MTR_WOOD_M", 68}, {"MTR_WOOD_W", 69},
        {"MTR_FWOD_A", 70},
        {"MTR_PLNT_A", 71},
        {"MTR_ROCK_A", 72}, {"MTR_ROCK_B", 73}, {"MTR_ROCK_P", 74},
        {"MTR_MOSS_A", 75},
        {"MTR_TURF_A", 76},
        {"MTR_WATE_A", 77}, {"MTR_WATE_B", 78}, {"MTR_WATE_C", 79},
        {"MTR_AIR_A",  80},
        {"MTR_NONE_A", 81},
    };
    static_assert(sizeof(k_MaterialNames) / sizeof(k_MaterialNames[0])
                  == outfit::kCamoMaterialCount,
                  "k_MaterialNames must list all 82 MaterialType entries");

    static std::int32_t ResolveMaterialNameToIndex(const char* name)
    {
        if (!name) return -1;
        for (const auto& e : k_MaterialNames)
        {
            if (std::strcmp(e.name, name) == 0) return e.index;
        }
        return -1;
    }


    static void ReadCamoBonusValuesField(
        lua_State* L, int tableIndex, outfit::OutfitDefinition& def)
    {
        LuaGetField(L, tableIndex, "camoBonusValues");
        if (LuaType(L, -1) != LUA_TTABLE)
        {
            SetLuaTop(L, -2);
            return;
        }

        const int valuesTbl = GetLuaTop(L);
        std::size_t writeCount = 0;


        LuaPushNil(L);
        while (LuaNext(L, valuesTbl) != 0)
        {
            std::int32_t materialIdx = -1;

            const int keyType = LuaType(L, -2);
            if (keyType == LUA_TSTRING)
            {
                const char* keyName = GetLuaString(L, -2);
                materialIdx = ResolveMaterialNameToIndex(keyName);
            }
            else if (keyType == LUA_TNUMBER)
            {

                const int keyIdx = GetLuaInt(L, -2);
                if (keyIdx >= 1
                    && keyIdx <= static_cast<int>(outfit::kCamoMaterialCount))
                {
                    materialIdx = keyIdx - 1;
                }
            }

            if (materialIdx >= 0
                && materialIdx < static_cast<std::int32_t>(outfit::kCamoMaterialCount))
            {
                def.camoBonusValues[materialIdx] = GetLuaInt(L, -1);
                ++writeCount;
            }


            SetLuaTop(L, -2);
        }

        if (writeCount > 0)
            def.hasCamoBonusValues = true;

        SetLuaTop(L, -2);
    }


    void ReadHeadOptionsArray(
        lua_State* L, int tableIndex, outfit::OutfitDefinition& def)
    {
        def.headOptionCount = 0;

        LuaGetField(L, tableIndex, "headOptions");
        if (LuaType(L, -1) == LUA_TTABLE)
        {
            const std::size_t n = LuaObjLen(L, -1);
            const std::size_t cap = outfit::kMaxHeadOptionsPerOutfit;
            const std::size_t lim = (n < cap) ? n : cap;

            for (std::size_t i = 1; i <= lim; ++i)
            {
                LuaRawGetI(L, -1, static_cast<int>(i));
                if (LuaIsNumber(L, -1))
                {


                    const int v = GetLuaInt(L, -1);
                    if (v > 0 && v <= 0xFFFF)
                    {
                        def.headOptionEquipIds[def.headOptionCount++] =
                            static_cast<std::uint16_t>(v);
                    }
                }
                else if (LuaIsString(L, -1))
                {


                    const char* name = GetLuaString(L, -1);
                    if (const std::uint16_t alias = TryResolveHeadAlias(name);
                        alias != 0)
                    {
                        def.headOptionEquipIds[def.headOptionCount++] = alias;
                    }
                    else if (const auto* head =
                             outfit::TryGetCustomHeadByName(name))
                    {
                        def.headOptionEquipIds[def.headOptionCount++] =
                            head->equipId;
                    }
                    else
                    {
                        Log("[OutfitLua] headOptions: unknown name '%s' "
                            "(not a vanilla alias and not registered via "
                            "V_FrameWork.RegisterCustomHead); skipping\n",
                            name ? name : "(null)");
                    }
                }
                LuaPop(L, 1);
            }
        }
        LuaPop(L, 1);
    }


    void ReadVariantsArray(
        lua_State* L, int tableIndex, outfit::OutfitDefinition& def)
    {
        def.variantCount = 0;

        LuaGetField(L, tableIndex, "variants");
        if (LuaType(L, -1) == LUA_TTABLE)
        {
            const std::size_t n = LuaObjLen(L, -1);


            const std::size_t cap = outfit::kMaxVariantsPerOutfit - 1;
            const std::size_t lim = (n < cap) ? n : cap;

            std::uint8_t maxFilledSlot = 0;
            for (std::size_t i = 1; i <= lim; ++i)
            {
                LuaRawGetI(L, -1, static_cast<int>(i));
                if (LuaType(L, -1) == LUA_TTABLE)
                {
                    outfit::OutfitVariant v{};
                    v.used            = true;
                    v.partsPathCode64 = ReadRequiredPathField(L, -1, "partsPath");
                    v.fpkPathCode64   = ReadRequiredPathField(L, -1, "fpkPath");
                    v.camoFpk         = ReadSubAssetField(L, -1, "camoFpk",
                                            outfit::kSubAssetUseVanilla);
                    v.camoFv2         = ReadSubAssetField(L, -1, "camoFv2",
                                            outfit::kSubAssetUseVanilla);
                    v.diamondFpk      = ReadSubAssetField(L, -1, "diamondFpk",
                                            outfit::kSubAssetDisabled);


                    LuaGetField(L, -1, "displayName");
                    if (LuaType(L, -1) == LUA_TSTRING)
                    {
                        if (const char* s = GetLuaString(L, -1); s && *s)
                            v.displayNameHash = FoxHashes::StrCode64(s);
                    }
                    LuaPop(L, 1);

                    if (v.displayNameHash == 0)
                    {
                        LuaGetField(L, -1, "displayNameHash");
                        const int t = LuaType(L, -1);
                        if (t == LUA_TNUMBER)
                        {


                            v.displayNameHash =
                                static_cast<std::uint64_t>(GetLuaInt(L, -1));
                        }
                        LuaPop(L, 1);
                    }


                    def.variants[i] = v;
                    maxFilledSlot   = static_cast<std::uint8_t>(i);
                }
                LuaPop(L, 1);
            }

            if (maxFilledSlot > 0)
            {


                def.variantCount = static_cast<std::uint8_t>(maxFilledSlot + 1);
            }
        }
        LuaPop(L, 1);
    }
}


static int __cdecl l_RegisterOutfit(lua_State* L)
{
    if (LuaType(L, 1) != LUA_TTABLE)
    {
        Log("[OutfitLua] RegisterOutfit: arg 1 must be a table\n");
        PushLuaBool(L, false);
        return 1;
    }

    outfit::OutfitDefinition def{};


    const char* key = nullptr;
    TryReadTableStringField(L, 1, "key", key);
    def.key = key;

    int v = 0;


    if (TryReadTableIntField(L, 1, "developId", v) && v > 0 && v <= 0xFFFF)
    {
        def.developId = static_cast<std::uint16_t>(v);
    }
    else if (key && key[0])
    {
        std::int32_t newId = 0;
        if (V_FrameWorkState::ResolveOrCreateDevelopId(key, 0, newId)
            && newId > 0 && newId <= 0xFFFF)
        {
            def.developId = static_cast<std::uint16_t>(newId);
        }
    }

    if (def.developId == 0)
    {
        Log("[OutfitLua] RegisterOutfit: missing 'developId' AND no 'key' "
            "for auto-allocate\n");
        PushLuaBool(L, false);
        return 1;
    }


    constexpr std::int32_t kEdcRowCapacity = 0x400;

    if (TryReadTableIntField(L, 1, "flowIndex", v) && v > 0 && v < kEdcRowCapacity)
    {
        def.flowIndex = static_cast<std::uint16_t>(v);
    }
    else if (key && key[0])
    {
        if (v != 0 && (v < 0 || v >= kEdcRowCapacity))
        {
            Log("[OutfitLua] RegisterOutfit: explicit flowIndex=%d is "
                "out of EDC range (1..%d), auto-allocating instead "
                "(key=%s)\n", v, kEdcRowCapacity - 1, key);
        }
        std::int32_t newIdx = 0;
        if (V_FrameWorkState::ResolveOrCreateFlowIndex(key, 0, newIdx)
            && newIdx > 0 && newIdx < kEdcRowCapacity)
        {
            def.flowIndex = static_cast<std::uint16_t>(newIdx);
        }
    }

    if (def.flowIndex == 0)
    {
        Log("[OutfitLua] RegisterOutfit: missing 'flowIndex' AND no 'key' "
            "for auto-allocate\n");
        PushLuaBool(L, false);
        return 1;
    }

    def.playerType = ParseOutfitPlayerType(L, 1);
    if (def.playerType == 0xFF)
    {
        Log("[OutfitLua] RegisterOutfit: missing or invalid 'playerType' "
            "(expect 'Snake'/'DDMale'/'DDFemale'/'Avatar' or 0..3)\n");
        PushLuaBool(L, false);
        return 1;
    }

    def.partsPathCode64 = ReadRequiredPathField(L, 1, "partsPath");
    def.fpkPathCode64   = ReadRequiredPathField(L, 1, "fpkPath");
    if (def.partsPathCode64 == 0 || def.fpkPathCode64 == 0)
    {
        Log("[OutfitLua] RegisterOutfit: missing 'partsPath' or 'fpkPath'\n");
        PushLuaBool(L, false);
        return 1;
    }


    def.camoFpk    = ReadSubAssetField(L, 1, "camoFpk",    outfit::kSubAssetDisabled);
    def.faceFpk    = ReadSubAssetField(L, 1, "faceFpk",    outfit::kSubAssetUseVanilla);
    def.skinFv2    = ReadSubAssetField(L, 1, "skinFv2",    outfit::kSubAssetUseVanilla);
    def.diamondFpk = ReadSubAssetField(L, 1, "diamondFpk", outfit::kSubAssetDisabled);

    def.enableArm  = TryReadTableBoolField(L, 1, "enableArm", true);

    def.camoFv2    = ReadSubAssetField(L, 1, "camoFv2",    outfit::kSubAssetUseVanilla);
    def.diamondFv2 = ReadSubAssetField(L, 1, "diamondFv2", outfit::kSubAssetUseVanilla);


    def.enableHead = TryReadTableBoolField(L, 1, "enableHead", false);


    int defaultSoldierFaceId = 0;
    if (TryReadTableIntField(L, 1, "defaultSoldierFaceId", defaultSoldierFaceId)
        && defaultSoldierFaceId > 0 && defaultSoldierFaceId < 900)
    {
        def.defaultSoldierFaceId =
            static_cast<std::uint16_t>(defaultSoldierFaceId);
    }


    {
        const char* langEquipName = nullptr;
        if (TryReadTableStringField(L, 1, "langEquipName", langEquipName)
            && langEquipName && langEquipName[0] != '\0')
        {
            def.langEquipNameHash = FoxHashes::StrCode64(langEquipName);
        }
    }


    {
        int rawBonusType = 0;
        if (TryReadTableIntField(L, 1, "camoBonusType", rawBonusType)
            && rawBonusType >= 0 && rawBonusType <= 116)
        {
            def.camoBonusType = static_cast<std::uint8_t>(rawBonusType);
        }
    }


    ReadCamoBonusValuesField(L, 1, def);


    {
        const char* baseDisplayName = nullptr;
        if (TryReadTableStringField(L, 1, "displayName", baseDisplayName)
            && baseDisplayName && baseDisplayName[0] != '\0')
        {
            def.baseDisplayNameHash = FoxHashes::StrCode64(baseDisplayName);
        }
        else
        {
            int displayHashRaw = 0;
            if (TryReadTableIntField(L, 1, "displayNameHash", displayHashRaw)
                && displayHashRaw != 0)
            {
                def.baseDisplayNameHash =
                    static_cast<std::uint64_t>(displayHashRaw);
            }
        }
    }


    ReadHeadOptionsArray(L, 1, def);
    const bool autoImpliedSupports = (def.headOptionCount > 0);
    def.supportsHeadOptions = TryReadTableBoolField(
        L, 1, "supportsHeadOptions", autoImpliedSupports);


    ReadVariantsArray(L, 1, def);


    std::uint8_t allocatedPartsType = 0xFF;
    const bool ok = outfit::RegisterOutfit(def, &allocatedPartsType);

    if (!ok)
    {
        PushLuaBool(L, false);
        return 1;
    }


    PushLuaNumber(L, static_cast<float>(allocatedPartsType));
    PushLuaNumber(L, static_cast<float>(def.developId));
    PushLuaNumber(L, static_cast<float>(def.flowIndex));
    return 3;
}


static int __cdecl l_RegisterHeadOption(lua_State* L)
{
    if (LuaType(L, 1) != LUA_TTABLE)
    {
        Log("[CustomHead] RegisterHeadOption: arg 1 must be a table\n");
        PushLuaNumber(L, 0);
        return 1;
    }

    const char* name = nullptr;
    TryReadTableStringField(L, 1, "name", name);
    if (!name || !name[0])
    {
        Log("[CustomHead] RegisterHeadOption: missing 'name'\n");
        PushLuaNumber(L, 0);
        return 1;
    }

    int rawFaceId = 0;
    std::uint16_t TppEnemyFaceId = 0;
    if (TryReadTableIntField(L, 1, "TppEnemyFaceId", rawFaceId)
        && rawFaceId > 0 && rawFaceId <= 0xFFFF)
    {
        TppEnemyFaceId = static_cast<std::uint16_t>(rawFaceId);
    }

    const char* langName = nullptr;
    TryReadTableStringField(L, 1, "langName", langName);
    const std::uint64_t langNameHash =
        (langName && langName[0]) ? FoxHashes::StrCode64(langName) : 0;

    const std::uint64_t iconFtexCode =
        ReadRequiredPathField(L, 1, "iconFtex");

    const std::uint16_t equipId = outfit::RegisterHeadOption(
        name, TppEnemyFaceId, langNameHash, iconFtexCode);

    PushLuaNumber(L, static_cast<float>(equipId));
    return 1;
}


static int __cdecl l_SetCurrentOutfit(lua_State* L)
{
    const int developIdRaw = GetLuaInt(L, 1);

    if (developIdRaw == 0)
    {
        outfit::ClearPendingOutfitDevelopId();
        Log("[OutfitLua] SetCurrentOutfit: cleared pending\n");
        PushLuaBool(L, true);
        return 1;
    }

    if (developIdRaw < 0 || developIdRaw > 0xFFFF)
    {
        PushLuaBool(L, false);
        return 1;
    }

    const outfit::OutfitEntry* entry = nullptr;
    if (!outfit::TryGetOutfitByDevelopId(
            static_cast<std::uint16_t>(developIdRaw), &entry) || !entry)
    {
        Log("[OutfitLua] SetCurrentOutfit: developId=%d not registered\n",
            developIdRaw);
        PushLuaBool(L, false);
        return 1;
    }

    outfit::SetPendingOutfitDevelopId(static_cast<std::uint16_t>(developIdRaw));
    Log("[OutfitLua] SetCurrentOutfit: pending developId=%d "
        "(partsType=0x%02X selector=0x%02X playerType=%u)\n",
        developIdRaw,
        static_cast<unsigned>(entry->partsType),
        static_cast<unsigned>(entry->selectorCode),
        static_cast<unsigned>(entry->playerType));

    PushLuaBool(L, true);
    return 1;
}


static int __cdecl l_SetOutfitVariant(lua_State* L)
{
    const int developIdRaw   = GetLuaInt(L, 1);
    const int variantIdxRaw  = GetLuaInt(L, 2);

    if (developIdRaw <= 0 || developIdRaw > 0xFFFF)
    {
        PushLuaBool(L, false);
        return 1;
    }

    const outfit::OutfitEntry* entry = nullptr;
    if (!outfit::TryGetOutfitByDevelopId(
            static_cast<std::uint16_t>(developIdRaw), &entry) || !entry)
    {
        PushLuaBool(L, false);
        return 1;
    }

    const std::uint8_t v = (variantIdxRaw < 0)
        ? 0
        : static_cast<std::uint8_t>(variantIdxRaw & 0xFF);

    outfit::SetActiveVariant(entry->partsType, v);


    const std::uint8_t livePartsType = outfit::ReadLivePartsType();
    const bool         isLiveOutfit  = (livePartsType == entry->partsType);

    bool reloaded = false;
    if (isLiveOutfit)
    {
        reloaded = outfit::ForceLiveSuitReload(
            entry->playerType,
            entry->partsType,
            entry->selectorCode,
            v);
    }

    Log("[OutfitLua] SetOutfitVariant: developId=%d variantIndex=%d "
        "(live partsType=0x%02X — %s; ForceLiveSuitReload=%s)\n",
        developIdRaw, static_cast<int>(v),
        static_cast<unsigned>(livePartsType),
        isLiveOutfit ? "matches, triggering live re-equip via ReqLoadout"
                     : "different outfit equipped, change takes effect on next equip",
        isLiveOutfit ? (reloaded ? "OK" : "skip/fail") : "n/a");

    PushLuaBool(L, true);
    return 1;
}


static int __cdecl l_GetOutfitInfo(lua_State* L)
{
    const int developIdRaw = GetLuaInt(L, 1);
    if (developIdRaw <= 0 || developIdRaw > 0xFFFF)
        return 0;

    const outfit::OutfitEntry* entry = nullptr;
    if (!outfit::TryGetOutfitByDevelopId(
            static_cast<std::uint16_t>(developIdRaw), &entry) || !entry)
        return 0;

    if (!ResolveLuaApi() || !g_lua_createtable || !g_lua_settable
        || !g_lua_pushstring || !g_lua_pushnumber || !g_lua_pushboolean)
        return 0;

    g_lua_createtable(L, 0, 7);

    g_lua_pushstring(L, const_cast<char*>("partsType"));
    g_lua_pushnumber(L, static_cast<float>(entry->partsType));
    g_lua_settable(L, -3);

    g_lua_pushstring(L, const_cast<char*>("selectorCode"));
    g_lua_pushnumber(L, static_cast<float>(entry->selectorCode));
    g_lua_settable(L, -3);

    g_lua_pushstring(L, const_cast<char*>("flowIndex"));
    g_lua_pushnumber(L, static_cast<float>(entry->flowIndex));
    g_lua_settable(L, -3);

    g_lua_pushstring(L, const_cast<char*>("playerType"));
    g_lua_pushnumber(L, static_cast<float>(entry->playerType));
    g_lua_settable(L, -3);

    g_lua_pushstring(L, const_cast<char*>("variantCount"));
    g_lua_pushnumber(L, static_cast<float>(entry->variantCount));
    g_lua_settable(L, -3);

    g_lua_pushstring(L, const_cast<char*>("activeVariant"));
    g_lua_pushnumber(L, static_cast<float>(
        outfit::GetActiveVariant(entry->partsType)));
    g_lua_settable(L, -3);

    g_lua_pushstring(L, const_cast<char*>("supportsHeadOptions"));
    g_lua_pushboolean(L, entry->supportsHeadOptions ? 1 : 0);
    g_lua_settable(L, -3);

    return 1;
}


static int __cdecl l_EnableTornadoDual(lua_State* L)
{
    static constexpr std::uint8_t kOriginalBytes[2] = { 0x74, 0x10 };
    static constexpr std::uint8_t kEnabledBytes[2]  = { 0x90, 0x90 };

    const bool enable = GetLuaBool(L, 1);

    if (!gAddr.TornadoDualPatch)
    {
        Log("[TornadoDual] EnableTornadoDual(%s): patch address not set for current build\n",
            enable ? "true" : "false");
        PushLuaBool(L, false);
        return 1;
    }

    void* target = ResolveGameAddress(gAddr.TornadoDualPatch);
    if (!target)
    {
        Log("[TornadoDual] EnableTornadoDual(%s): ResolveGameAddress returned null\n",
            enable ? "true" : "false");
        PushLuaBool(L, false);
        return 1;
    }

    DWORD oldProtect = 0;
    if (!VirtualProtect(target, sizeof(kOriginalBytes), PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        Log("[TornadoDual] EnableTornadoDual(%s): VirtualProtect failed (err=%lu)\n",
            enable ? "true" : "false", GetLastError());
        PushLuaBool(L, false);
        return 1;
    }

    const std::uint8_t* src = enable ? kEnabledBytes : kOriginalBytes;
    std::memcpy(target, src, sizeof(kOriginalBytes));

    DWORD restored = 0;
    VirtualProtect(target, sizeof(kOriginalBytes), oldProtect, &restored);
    FlushInstructionCache(GetCurrentProcess(), target, sizeof(kOriginalBytes));

    Log("[TornadoDual] EnableTornadoDual(%s): wrote %02X %02X at %p\n",
        enable ? "true" : "false", src[0], src[1], target);

    PushLuaBool(L, true);
    return 1;
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
    { "SetSoldierRtpc",                         l_SetSoldierRtpc },
    { "SetGlobalRtpc",                          l_SetGlobalRtpc },
    { "SetSoldierRtpcById",                     l_SetSoldierRtpcById },
    { "SetGlobalRtpcById",                      l_SetGlobalRtpcById },
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
    { "Log",                                    l_Log },
    { "GetModFiles",                            l_GetModFiles },


    { "RegisterOutfit",                         l_RegisterOutfit },
    { "RegisterHeadOption",                     l_RegisterHeadOption },
    { "SetCurrentOutfit",                       l_SetCurrentOutfit },
    { "SetOutfitVariant",                       l_SetOutfitVariant },
    { "GetOutfitInfo",                          l_GetOutfitInfo },


    { "SetCamoValue",                           l_SetCamoValue },
    { "GetCamoValue",                           l_GetCamoValue },
    { "CloneCamoRow",                           l_CloneCamoRow },
    { "ImportCamoRow",                          l_ImportCamoRow },
    { "ImportCamoTable",                        l_ImportCamoTable },


    { "EnableTornadoDual",                      l_EnableTornadoDual },

    { nullptr, nullptr }
};


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


static void __fastcall hkSetLuaFunctions(lua_State* L)
{
    Log("[Hook] SetLuaFunctions invoked: L=%p\n", L);

    if (g_OrigSetLuaFunctions)
    {
        g_OrigSetLuaFunctions(L);
    }

    RegisterAllUiLuaLibraries(L);
}


extern "C" __declspec(dllexport) int __cdecl luaopen_V_FrameWork(lua_State* L)
{
    return RegisterLuaLibrary(L, "V_FrameWork", g_VFrameWorkLib) ? 1 : 0;
}


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


    CamoufTable::Deps camoDeps{};
    camoDeps.LuaCreateTable = &LuaCreateTable;
    camoDeps.LuaPushString  = &LuaPushString;
    camoDeps.LuaPushNumber  = &PushLuaNumber;
    camoDeps.LuaSetTable    = &LuaSetTable;
    camoDeps.LuaGetTop      = &GetLuaTop;
    camoDeps.LuaSetTop      = &SetLuaTop;
    CamoufTable::Bind(camoDeps);


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


bool Uninstall_SetLuaFunctions_Hook()
{
    const uintptr_t setLuaFunctionsAddr = GetLuaBridgeAddress(gAddr.SetLuaFunctions, BOOTSTRAP_EN_SetLuaFunctions);
    DisableAndRemoveHook(ResolveGameAddress(setLuaFunctionsAddr));
    g_OrigSetLuaFunctions = nullptr;
    ClearTrackedLuaStates();
    return true;
}