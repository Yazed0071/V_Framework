#include "pch.h"
extern "C" {
    #include "lua.h"
    #include "lauxlib.h"
    #include "lualib.h"
}

#include <Windows.h>
#include <cstdint>
#include <cstdlib>
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
#include "TppPickableRuntime.h"

#include "AddressSet.h"
#include "utility_GetIconFtexPath.h"
#include "PlayerVoiceFpkHook.h"
#include "SoldierRtpcHook.h"
#include "SoldierVoiceTypeQuery.h"
#include "SoldierObjectRtpc.h"
#include "../hooks/sound/VoicePitchOverride.h"
#include "V_FrameWorkModLoader.h"
#include "../hooks/sahelan/RealizedSahelanFovaHook.h"
#include "../hooks/sound/GameOverMusic.h"
#include "../hooks/sound/HeliVoice.h"
#include "../hooks/securitycamera/SecurityCameraFovaHook.h"
#include "../hooks/menupopup/MbDvcCustomPopupHook.h"


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


static void PushLuaString(lua_State* L, const char* s)
{
    if (!ResolveLuaApi() || !g_lua_pushstring)
        return;

    g_lua_pushstring(L, const_cast<char*>(s ? s : ""));
}


static void PushLuaNil(lua_State* L)
{
    if (!ResolveLuaApi() || !g_lua_pushnil)
        return;

    g_lua_pushnil(L);
}


static std::uint32_t GetLuaStrCode32Arg(lua_State* L, int idx)
{
    if (GetLuaTop(L) < idx)
        return 0u;

    if (LuaIsString(L, idx))
    {
        const char* s = GetLuaString(L, idx);
        if (!s || !s[0])
            return 0u;
        return FoxHashes::StrCode32(s);
    }

    if (LuaIsNumber(L, idx))
        return static_cast<std::uint32_t>(GetLuaInt64(L, idx));

    return 0u;
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


// Direct passthrough — caller knows the Wwise AkObjectID.
static int __cdecl l_SetRtpcByAkObjId(lua_State* L)
{
    const std::uint64_t akObjId  = GetLuaInt64(L, 1);
    const char*         rtpcName = GetLuaString(L, 2);
    const float         value    = GetLuaNumber(L, 3);
    const long          timeMs   = static_cast<long>(GetLuaInt(L, 4));

    const int result = SoldierRtpc::SetRtpcByAkObjId(akObjId, rtpcName, value, timeMs);
    PushLuaNumber(L, static_cast<float>(result));
    return 1;
}


// Direct passthrough by precomputed RTPC id.
static int __cdecl l_SetRtpcByAkObjIdById(lua_State* L)
{
    const std::uint64_t akObjId = GetLuaInt64(L, 1);
    const std::uint32_t rtpcId  = static_cast<std::uint32_t>(GetLuaInt64(L, 2));
    const float         value   = GetLuaNumber(L, 3);
    const long          timeMs  = static_cast<long>(GetLuaInt(L, 4));

    const int result = SoldierRtpc::SetRtpcByAkObjIdById(akObjId, rtpcId, value, timeMs);
    PushLuaNumber(L, static_cast<float>(result));
    return 1;
}


// Toggle the AK::SoundEngine::SetRTPCValue logging hook.
static int __cdecl l_SetRtpcLoggingEnabled(lua_State* L)
{
    const bool enabled = GetLuaBool(L, 1);
    const bool prev    = SoldierRtpc::SetRtpcLoggingEnabled(enabled);
    PushLuaBool(L, prev);
    return 1;
}


static int __cdecl l_IsRtpcLoggingEnabled(lua_State* L)
{
    PushLuaBool(L, SoldierRtpc::IsRtpcLoggingEnabled());
    return 1;
}


// Per-soldier RTPC via the SoundController chain. rtpcId pre-hashed.
static int __cdecl l_SetSoldierObjectRtpc(lua_State* L)
{
    const std::uint32_t goId   = static_cast<std::uint32_t>(GetLuaInt64(L, 1));
    const std::uint32_t rtpcId = static_cast<std::uint32_t>(GetLuaInt64(L, 2));
    const float         value  = GetLuaNumber(L, 3);
    const long          timeMs = static_cast<long>(GetLuaInt(L, 4));
    const bool ok = ::Set_SoldierObjectRtpc(goId, rtpcId, value, timeMs);
    PushLuaBool(L, ok);
    return 1;
}


// Per-soldier RTPC by name (DLL hashes via fox::sd::ConvertParameterID).
static int __cdecl l_SetSoldierObjectRtpcByName(lua_State* L)
{
    const std::uint32_t goId     = static_cast<std::uint32_t>(GetLuaInt64(L, 1));
    const char*         rtpcName = GetLuaString(L, 2);
    const float         value    = GetLuaNumber(L, 3);
    const long          timeMs   = static_cast<long>(GetLuaInt(L, 4));
    const bool ok = ::Set_SoldierObjectRtpcByName(goId, rtpcName, value, timeMs);
    PushLuaBool(L, ok);
    return 1;
}


// PHASE 0 — global pitch bias on every CAkResampler::SetPitch call.
// Affects ALL audio (voice, sfx, bgm). Use to verify the mechanism works.
static int __cdecl l_SetGlobalVoicePitch(lua_State* L)
{
    const float cents = GetLuaNumber(L, 1);
    ::Set_GlobalVoicePitchBiasCents(cents);
    return 0;
}


static int __cdecl l_GetGlobalVoicePitch(lua_State* L)
{
    PushLuaNumber(L, ::Get_GlobalVoicePitchBiasCents());
    return 1;
}


static int __cdecl l_SetVoicePitchHookLogging(lua_State* L)
{
    const bool enabled = GetLuaBool(L, 1);
    ::Set_PitchHookLoggingEnabled(enabled);
    return 0;
}


// PHASE 1 — per-AkObjId pitch bias.
static int __cdecl l_SetPitchByAkObjId(lua_State* L)
{
    const std::uint64_t akObjId = GetLuaInt64(L, 1);
    const float         cents   = GetLuaNumber(L, 2);
    ::Set_PitchBiasForAkObjId(akObjId, cents);
    return 0;
}


static int __cdecl l_ClearPitchByAkObjId(lua_State* L)
{
    const std::uint64_t akObjId = GetLuaInt64(L, 1);
    ::Clear_PitchBiasForAkObjId(akObjId);
    return 0;
}


static int __cdecl l_ClearAllPerAkObjIdPitchBiases(lua_State* L)
{
    UNREFERENCED_PARAMETER(L);
    ::Clear_AllPerAkObjIdPitchBiases();
    return 0;
}


// Toggle chain logging — resolves akObjId for each SetPitch call so you can
// correlate it with [AK] log output before applying per-akObjId bias.
static int __cdecl l_SetVoicePitchChainLogging(lua_State* L)
{
    const bool enabled = GetLuaBool(L, 1);
    ::Set_PitchChainLoggingEnabled(enabled);
    return 0;
}


// Translate a soldier gameObjectId to its Wwise AkGameObjectID.
// Returns 0 if soldier not yet voice-resolved or chain failed.
static int __cdecl l_GetSoldierAkObjId(lua_State* L)
{
    const std::uint32_t goId = static_cast<std::uint32_t>(GetLuaInt64(L, 1));
    const std::uint32_t akObjId = ::Get_SoldierAkObjId(goId);
    PushLuaNumber(L, static_cast<float>(akObjId));
    return 1;
}


// Set per-soldier voice pitch — applies immediately if resolvable, else
// queues the request and applies on the next voice-type resolution.
// Returns true if applied immediately, false if queued.
static int __cdecl l_SetSoldierVoicePitch(lua_State* L)
{
    const std::uint32_t goId  = static_cast<std::uint32_t>(GetLuaInt64(L, 1));
    const float         cents = GetLuaNumber(L, 2);
    const bool ok = ::Set_SoldierVoicePitch(goId, cents);
    PushLuaBool(L, ok);
    return 1;
}


static int __cdecl l_SetVIPImportant(lua_State* L)
{
    const std::uint32_t gameObjectId = static_cast<std::uint32_t>(GetLuaInt64(L, 1));
    const bool isOfficer = GetLuaBool(L, 2);
    const std::uint32_t customDeadBodyLabel = GetLuaStrCode32Arg(L, 3);

    Add_VIPSleepFaintImportantGameObjectId(gameObjectId, isOfficer);
    Add_VIPHoldupImportantGameObjectId(gameObjectId, isOfficer);
    Add_VIPRadioImportantGameObjectId(gameObjectId, isOfficer, customDeadBodyLabel);
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
    const std::uint32_t customLostLabel = GetLuaStrCode32Arg(L, 3);

    Add_LostHostageTrap(gameObjectId, hostageType, customLostLabel);
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



static std::uint64_t ParseSahelanFovaArg(const char* text)
{
    if (!text || !*text)
        return 0;


    const bool hasHexPrefix = (text[0] == '0') && (text[1] == 'x' || text[1] == 'X');

    bool looksLikePath = false;
    for (const char* p = text; *p; ++p)
    {
        const char c = *p;
        if (c == '/' || c == '\\')
        {
            looksLikePath = true;
            break;
        }
    }

    if (!looksLikePath)
    {
        const char* hexStart = hasHexPrefix ? text + 2 : text;
        bool allHex = true;
        size_t hexLen = 0;
        for (const char* p = hexStart; *p; ++p)
        {
            const char c = *p;
            const bool isHexDigit =
                (c >= '0' && c <= '9') ||
                (c >= 'a' && c <= 'f') ||
                (c >= 'A' && c <= 'F');
            if (!isHexDigit)
            {
                allHex = false;
                break;
            }
            ++hexLen;
        }

        if (allHex && hexLen > 0 && hexLen <= 16)
        {
            return std::strtoull(hexStart, nullptr, 16);
        }
    }


    return FoxHashes::PathCode64Ext(text);
}


static int __cdecl l_SetSahelanFova(lua_State* L)
{
    const char* arg = GetLuaString(L, 1);
    if (!arg || !*arg)
    {
        Log("[SahelanFova] SetSahelanFova: missing argument; expected hex hash like \"0x60887fe72aa5c04b\" or asset path\n");
        PushLuaBool(L, false);
        return 1;
    }

    const std::uint64_t hash = ParseSahelanFovaArg(arg);
    if (hash == 0)
    {
        Log("[SahelanFova] SetSahelanFova: parsed hash is zero (input=\"%s\")\n", arg);
        PushLuaBool(L, false);
        return 1;
    }

    Set_SahelanFovaHash(hash);
    PushLuaBool(L, true);
    return 1;
}


static int __cdecl l_ClearSahelanFova(lua_State* L)
{
    UNREFERENCED_PARAMETER(L);
    Clear_SahelanFovaOverride();
    return 0;
}


static std::int32_t ResolveSecurityCameraVariant(lua_State* L, int idx)
{

    if (LuaType(L, idx) == 3)
        return static_cast<std::int32_t>(GetLuaInt(L, idx));


    if (LuaType(L, idx) == 4)
    {
        const char* s = GetLuaString(L, idx);
        if (!s || !*s)
            return -1;


        char lower[32] = {};
        size_t n = 0;
        while (s[n] && n + 1 < sizeof(lower))
        {
            char c = s[n];
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            lower[n] = c;
            ++n;
        }
        lower[n] = 0;

        if (std::strcmp(lower, "normalcamera") == 0 || std::strcmp(lower, "normal") == 0)
            return 0;
        if (std::strcmp(lower, "guncamera") == 0 || std::strcmp(lower, "gun") == 0)
            return 1;
    }

    return -1;
}


static int __cdecl l_SetSecurityCameraFova(lua_State* L)
{
    const std::int32_t variantIndex = ResolveSecurityCameraVariant(L, 1);
    if (variantIndex < 0)
    {
        Log("[SecCamFova] SetSecurityCameraFova: bad variant arg (expected number 0/1 or \"NormalCamera\"/\"GunCamera\")\n");
        PushLuaBool(L, false);
        return 1;
    }

    const char* arg = GetLuaString(L, 2);
    if (!arg || !*arg)
    {
        Log("[SecCamFova] SetSecurityCameraFova: missing fova argument (variant=%d)\n",
            static_cast<int>(variantIndex));
        PushLuaBool(L, false);
        return 1;
    }


    const bool hasHexPrefix = (arg[0] == '0') && (arg[1] == 'x' || arg[1] == 'X');
    bool hasPathSep = false;
    for (const char* p = arg; *p; ++p)
    {
        if (*p == '/' || *p == '\\') { hasPathSep = true; break; }
    }

    if (hasHexPrefix || !hasPathSep)
    {

        const std::uint64_t hash = ParseSahelanFovaArg(arg);
        if (hash == 0)
        {
            Log("[SecCamFova] SetSecurityCameraFova: parsed hash is zero (input=\"%s\")\n", arg);
            PushLuaBool(L, false);
            return 1;
        }
        Set_SecurityCameraFovaHash(variantIndex, hash);
    }
    else
    {

        Set_SecurityCameraFovaPath(variantIndex, arg);
    }

    PushLuaBool(L, true);
    return 1;
}


static int __cdecl l_ClearSecurityCameraFova(lua_State* L)
{
    const std::int32_t variantIndex = ResolveSecurityCameraVariant(L, 1);
    if (variantIndex < 0)
    {
        Log("[SecCamFova] ClearSecurityCameraFova: bad variant arg\n");
        return 0;
    }
    Clear_SecurityCameraFova(variantIndex);
    return 0;
}


static int __cdecl l_HashPathNoExt(lua_State* L)
{
    const char* path = GetLuaString(L, 1);
    if (!path || !*path)
    {
        LuaPushString(L, const_cast<char*>(""));
        return 1;
    }

    const std::uint64_t hash = FoxHashes::PathCode64Ext(path);

    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%016llX", static_cast<unsigned long long>(hash));
    LuaPushString(L, buf);
    return 1;
}


static int __cdecl l_HashPathWithExt(lua_State* L)
{
    const char* path = GetLuaString(L, 1);
    if (!path || !*path)
    {
        LuaPushString(L, const_cast<char*>(""));
        return 1;
    }

    const std::uint64_t hash = FoxHashes::PathCode64Ext(path);

    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%016llX", static_cast<unsigned long long>(hash));
    LuaPushString(L, buf);
    return 1;
}


static int __cdecl l_ClearAllSecurityCameraFovas(lua_State* L)
{
    UNREFERENCED_PARAMETER(L);
    Clear_AllSecurityCameraFovas();
    return 0;
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


static int __cdecl l_SetGameOverMusic(lua_State* L)
{
    const bool isEnable = GetLuaBool(L, 1);
    const int  typeRaw  = GetLuaInt(L, 2);
    const char* playEvt = GetLuaString(L, 3);
    const char* stopEvt = GetLuaString(L, 4);

    if (typeRaw < GAME_OVER_GENERAL || typeRaw > GAME_OVER_CYPRUS)
    {
        Log("[GameOverMusic] SetGameOverMusic: invalid type=%d (expected 0..3)\n", typeRaw);
        PushLuaBool(L, false);
        return 1;
    }

    if (isEnable && (!playEvt || !*playEvt || !stopEvt || !*stopEvt))
    {
        Log("[GameOverMusic] SetGameOverMusic: enable=true requires non-empty play/stop event strings\n");
        PushLuaBool(L, false);
        return 1;
    }

    const bool ok = SetGameOverMusic(isEnable,
                                     static_cast<GAME_OVER_TYPE>(typeRaw),
                                     playEvt ? playEvt : "",
                                     stopEvt ? stopEvt : "");
    PushLuaBool(L, ok);
    return 1;
}


static int __cdecl l_SetEnableHeliVoice(lua_State* L)
{
    const bool isEnable = GetLuaBool(L, 1);
    const char* voiceEvt = GetLuaString(L, 2);
    const char* radioEvt = GetLuaString(L, 3);

    if (isEnable && (!voiceEvt || !*voiceEvt || !radioEvt || !*radioEvt))
    {
        Log("[HeliVoice] SetEnableHeliVoice: enable=true requires non-empty voice/radio event strings\n");
        PushLuaBool(L, false);
        return 1;
    }

    const bool ok = SetEnableHeliVoice(isEnable,
                                       voiceEvt ? voiceEvt : "",
                                       radioEvt ? radioEvt : "");
    PushLuaBool(L, ok);
    return 1;
}


// Queue popup with literal text.
static int __cdecl l_ShowMbDvcAnnouncePopupReport(lua_State* L)
{
    const char* title = GetLuaString(L, 1);
    const char* body  = GetLuaString(L, 2);
    if (!title) title = "";
    if (!body)  body  = "";

    const bool ok = Show_MbDvcAnnouncePopupReport(title, body);
    PushLuaBool(L, ok);
    return 1;
}


// Queue popup using LangId label names.
static int __cdecl l_ShowMbDvcAnnouncePopupReportLangId(lua_State* L)
{
    const char* titleLabel = GetLuaString(L, 1);
    const char* bodyLabel  = GetLuaString(L, 2);
    if (!titleLabel) titleLabel = "";
    if (!bodyLabel)  bodyLabel  = "";

    const bool ok = Show_MbDvcAnnouncePopupByLangId(titleLabel, bodyLabel);
    PushLuaBool(L, ok);
    return 1;
}


// Queue Server popup (slot picked internally).
static int __cdecl l_ShowMbDvcAnnouncePopupReward(lua_State* L)
{
    const char* title = GetLuaString(L, 1);
    const char* body  = GetLuaString(L, 2);
    if (!title) title = "";
    if (!body)  body  = "";

    const bool ok = Show_MbDvcAnnouncePopupReward(title, body);
    PushLuaBool(L, ok);
    return 1;
}


// Queue Server popup using LangId label names.
static int __cdecl l_ShowMbDvcAnnouncePopupRewardLangId(lua_State* L)
{
    const char* titleLabel = GetLuaString(L, 1);
    const char* bodyLabel  = GetLuaString(L, 2);
    if (!titleLabel) titleLabel = "";
    if (!bodyLabel)  bodyLabel  = "";

    const bool ok = Show_MbDvcAnnouncePopupRewardLangId(titleLabel, bodyLabel);
    PushLuaBool(L, ok);
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
    { "SetRtpcByAkObjId",                       l_SetRtpcByAkObjId },
    { "SetRtpcByAkObjIdById",                   l_SetRtpcByAkObjIdById },
    { "SetRtpcLoggingEnabled",                  l_SetRtpcLoggingEnabled },
    { "IsRtpcLoggingEnabled",                   l_IsRtpcLoggingEnabled },

    { "SetSoldierObjectRtpc",                   l_SetSoldierObjectRtpc },
    { "SetSoldierObjectRtpcByName",             l_SetSoldierObjectRtpcByName },

    { "SetGlobalVoicePitch",                    l_SetGlobalVoicePitch },
    { "GetGlobalVoicePitch",                    l_GetGlobalVoicePitch },
    { "SetVoicePitchHookLogging",               l_SetVoicePitchHookLogging },
    { "SetPitchByAkObjId",                      l_SetPitchByAkObjId },
    { "ClearPitchByAkObjId",                    l_ClearPitchByAkObjId },
    { "ClearAllPerAkObjIdPitchBiases",          l_ClearAllPerAkObjIdPitchBiases },
    { "SetVoicePitchChainLogging",              l_SetVoicePitchChainLogging },
    { "GetSoldierAkObjId",                      l_GetSoldierAkObjId },
    { "SetSoldierVoicePitch",                   l_SetSoldierVoicePitch },
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
    { "SetPickableCountRawByIndex",             l_SetPickableCountRawByIndex },
    { "GetPickableCountRawByIndex",             l_GetPickableCountRawByIndex },
    { "SetEquipIdIconFtexPath",                 l_SetEquipIdIconFtexPath },
    { "ClearIconFtexPath",                      l_ClearIconFtexPath },
    { "ClearAllIconFtexPaths",                  l_ClearAllIconFtexPaths },
    { "Log",                                    l_Log },
    { "GetModFiles",                            l_GetModFiles },


    { "EnableTornadoDual",                      l_EnableTornadoDual },


    { "SetSahelanFova",                         l_SetSahelanFova },
    { "ClearSahelanFova",                       l_ClearSahelanFova },


    { "SetSecurityCameraFova",                  l_SetSecurityCameraFova },
    { "ClearSecurityCameraFova",                l_ClearSecurityCameraFova },
    { "ClearAllSecurityCameraFovas",            l_ClearAllSecurityCameraFovas },
    { "HashPathNoExt",                          l_HashPathNoExt },
    { "HashPathWithExt",                        l_HashPathWithExt },


    { "SetGameOverMusic",                       l_SetGameOverMusic },
    { "SetEnableHeliVoice",                     l_SetEnableHeliVoice },


    { "ShowMbDvcAnnouncePopupReport",           l_ShowMbDvcAnnouncePopupReport },
    { "ShowMbDvcAnnouncePopupReportLangId",     l_ShowMbDvcAnnouncePopupReportLangId },
    { "ShowMbDvcAnnouncePopupReward",           l_ShowMbDvcAnnouncePopupReward },
    { "ShowMbDvcAnnouncePopupRewardLangId",     l_ShowMbDvcAnnouncePopupRewardLangId },


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
    if (!L)
        return 0;

    // Guard against double registration. The SetLuaFunctions hook may have
    // already registered the V_FrameWork library on this state during the
    // game's own bootstrap; a subsequent `require "V_FrameWork"` from mod
    // Lua then routes here. Calling FoxLuaRegisterLibrary a second time
    // trips the game's "library already registered" assertion (int 3 →
    // EXCEPTION_BREAKPOINT).
    if (IsLuaStateRegistered(L))
        return 0;

    if (!RegisterLuaLibrary(L, "V_FrameWork", g_VFrameWorkLib))
        return 0;

    TrackLuaState(L);
    return 1;
}


bool Install_SetLuaFunctions_Hook()
{
    if (g_SetLuaFunctionsHookInstalled)
    {
        Log("[Hook] SetLuaFunctions: already installed\n");
        return true;
    }

    ResolveLuaApi();


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
