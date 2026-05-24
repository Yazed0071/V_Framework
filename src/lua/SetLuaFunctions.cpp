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
#include <unordered_map>
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
#include "../hooks/sahelan/SetEyeLampColorHook.h"
#include "../hooks/sahelan/PhaseSneakAiImpl_PreUpdate.h"
#include "../hooks/sound/GameOverMusic.h"
#include "../hooks/sound/HeliVoice.h"
#include "../hooks/securitycamera/SecurityCameraFovaHook.h"
#include "../hooks/menupopup/MbDvcCustomPopupHook.h"
#include "../hooks/ui/EnemyLangIdOverride.h"
#include "../hooks/ui/MissionEmergencyHook.h"


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
    using lua_pushcclosure_t = void(__fastcall*)(lua_State* L, lua_CFunction fn, int n);
    using lua_pcall_t = int(__fastcall*)(lua_State* L, int nargs, int nresults, int errfunc);

    static constexpr int LUA_GLOBALSINDEX_51 = -10002;
    static constexpr int LUA_UPVALUEINDEX_51_1 = -10003; // lua_upvalueindex(1) in Lua 5.1: -10002 - 1


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
    static constexpr uintptr_t BOOTSTRAP_EN_lua_pushcclosure = 0x14C1E67B0ull;
    static constexpr uintptr_t BOOTSTRAP_EN_lua_pcall = 0x141A11930ull;

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
    static lua_pushcclosure_t      g_lua_pushcclosure = nullptr;
    static lua_pcall_t             g_lua_pcall = nullptr;

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

    if (!g_lua_pushcclosure)
        g_lua_pushcclosure = reinterpret_cast<lua_pushcclosure_t>(
            ResolveGameAddress(GetLuaBridgeAddress(gAddr.lua_pushcclosure, BOOTSTRAP_EN_lua_pushcclosure)));

    if (!g_lua_pcall)
        g_lua_pcall = reinterpret_cast<lua_pcall_t>(
            ResolveGameAddress(GetLuaBridgeAddress(gAddr.lua_pcall, BOOTSTRAP_EN_lua_pcall)));

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

    if (LuaIsNumber(L, idx))
        return static_cast<std::uint32_t>(GetLuaInt64(L, idx));

    if (LuaIsString(L, idx))
    {
        const char* s = GetLuaString(L, idx);
        if (!s || !s[0])
            return 0u;
        return FoxHashes::StrCode32(s);
    }

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

lua_State* V_FrameWork_AnyLuaState()
{
    std::lock_guard<std::mutex> lock(g_RegisteredLuaStatesMutex);
    if (g_RegisteredLuaStates.empty()) return nullptr;
    return *g_RegisteredLuaStates.begin();
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


static int __cdecl l_GetSoldierAkObjId(lua_State* L)
{
    const std::uint32_t goId = static_cast<std::uint32_t>(GetLuaInt64(L, 1));
    const std::uint32_t akObjId = ::Get_SoldierAkObjId(goId);
    PushLuaNumber(L, static_cast<float>(akObjId));
    return 1;
}


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


// --------------------------------------------------------------------------
// TppMission.IsEmergencyMission Lua-side override.
//
// The engine's iDroid "accept emergency mission" path is gated entirely in
// Lua: TppMission.AcceptEmergencyMission(missionCode) does
//     if not this.IsEmergencyMission(missionCode) then return end
// and TppMission.IsEmergencyMission hardcodes only 10115 and 50050.
// Hooking C++'s GetMissionCodeCategory is enough to light up the badge but
// not enough to actually let the engine start the mission; the Lua check
// must also pass.
//
// To make V_FrameWork.SetMissionEmergency(missionCode, true) work end-to-end
// without homework on the user, the first time it's called we transparently
// rewrap TppMission.IsEmergencyMission with a C closure that consults the
// V_FrameWork allowlist first and otherwise forwards to the original.
// --------------------------------------------------------------------------

static bool g_VFI_IsEmergencyMissionOverrideInstalled = false;


// C closure body. Upvalue 1 is the original TppMission.IsEmergencyMission,
// captured by lua_pushcclosure at install time. Arg 1 is the mission code.
static int __cdecl l_VFI_IsEmergencyMissionOverride(lua_State* L)
{
    if (!ResolveLuaApi())
        return 0;

    // Fast path: if the mission is in V_FrameWork's allowlist, return true.
    if (g_lua_isnumber && g_lua_isnumber(L, 1))
    {
        const long long mc = g_lua_tointeger ? g_lua_tointeger(L, 1) : 0;
        if (mc > 0 && mc <= 0xFFFF &&
            MissionEmergency_IsEnabled(static_cast<std::uint16_t>(mc)))
        {
            g_lua_pushboolean(L, 1);
            return 1;
        }
    }

    // Fallback: forward to the original (captured as upvalue 1).
    if (!g_lua_pushvalue || !g_lua_pcall || !g_lua_gettop || !g_lua_settop || !g_lua_pushnil)
        return 0;

    const int nargs = g_lua_gettop(L);

    g_lua_pushvalue(L, LUA_UPVALUEINDEX_51_1);
    for (int i = 1; i <= nargs; ++i)
        g_lua_pushvalue(L, i);

    const int err = g_lua_pcall(L, nargs, 1, 0);
    if (err != 0)
    {
        const char* errMsg = g_lua_tolstring ? g_lua_tolstring(L, -1, nullptr) : nullptr;
        Log("[MissionEmergency] IsEmergencyMission OVERRIDE: original pcall err=%d: %s\n",
            err, errMsg ? errMsg : "<no message>");
        g_lua_settop(L, nargs);
        g_lua_pushnil(L);
    }
    return 1;
}


// Install the override on TppMission.IsEmergencyMission. Idempotent: succeeds
// silently if already installed; retries on subsequent calls if TppMission
// wasn't loaded yet on a prior attempt.
static bool InstallIsEmergencyMissionOverride(lua_State* L)
{
    if (g_VFI_IsEmergencyMissionOverrideInstalled)
        return true;

    if (!ResolveLuaApi() || !g_lua_pushcclosure || !g_lua_pcall)
    {
        Log("[MissionEmergency] InstallIsEmergencyMissionOverride: Lua FFI unavailable; skipped\n");
        return false;
    }

    const int top0 = g_lua_gettop(L);

    g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("TppMission"));
    if (g_lua_type(L, -1) != LUA_TTABLE)
    {
        g_lua_settop(L, top0);
        Log("[MissionEmergency] InstallIsEmergencyMissionOverride: TppMission not loaded yet; deferred\n");
        return false;
    }

    g_lua_getfield(L, -1, const_cast<char*>("IsEmergencyMission"));
    if (g_lua_type(L, -1) != LUA_TFUNCTION)
    {
        g_lua_settop(L, top0);
        Log("[MissionEmergency] InstallIsEmergencyMissionOverride: original function missing\n");
        return false;
    }

    g_lua_pushcclosure(L, &l_VFI_IsEmergencyMissionOverride, 1);

    g_lua_pushstring(L, const_cast<char*>("IsEmergencyMission"));
    g_lua_pushvalue(L, -2);
    g_lua_rawset(L, -4);

    g_lua_settop(L, top0);

    g_VFI_IsEmergencyMissionOverrideInstalled = true;
    Log("[MissionEmergency] InstallIsEmergencyMissionOverride: installed\n");
    return true;
}


// Append missionCode to TppDefine.EMERGENCY_MISSION_LIST if not already there.
// TppStory.CloseEmergencyMission and a few cleanup paths iterate this list,
// so it has to stay in sync.
static void AppendToEmergencyMissionList(lua_State* L, int missionCode)
{
    if (!ResolveLuaApi())
        return;

    const int top0 = g_lua_gettop(L);

    g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("TppDefine"));
    if (g_lua_type(L, -1) != LUA_TTABLE) { g_lua_settop(L, top0); return; }

    g_lua_getfield(L, -1, const_cast<char*>("EMERGENCY_MISSION_LIST"));
    if (g_lua_type(L, -1) != LUA_TTABLE) { g_lua_settop(L, top0); return; }

    const int len = static_cast<int>(g_lua_objlen(L, -1));

    for (int i = 1; i <= len; ++i)
    {
        g_lua_rawgeti(L, -1, i);
        const bool isMatch =
            g_lua_isnumber(L, -1) &&
            (static_cast<int>(g_lua_tointeger(L, -1)) == missionCode);
        g_lua_settop(L, -2);
        if (isMatch)
        {
            g_lua_settop(L, top0);
            return;
        }
    }

    g_lua_pushnumber(L, static_cast<lua_Number>(len + 1));
    g_lua_pushnumber(L, static_cast<lua_Number>(missionCode));
    g_lua_rawset(L, -3);

    g_lua_settop(L, top0);

    Log("[MissionEmergency] appended %d to TppDefine.EMERGENCY_MISSION_LIST\n",
        missionCode);
}


// Mirror of AppendToEmergencyMissionList — shifts down to keep ipairs happy.
static void RemoveFromEmergencyMissionList(lua_State* L, int missionCode)
{
    if (!ResolveLuaApi())
        return;

    const int top0 = g_lua_gettop(L);

    g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("TppDefine"));
    if (g_lua_type(L, -1) != LUA_TTABLE) { g_lua_settop(L, top0); return; }

    g_lua_getfield(L, -1, const_cast<char*>("EMERGENCY_MISSION_LIST"));
    if (g_lua_type(L, -1) != LUA_TTABLE) { g_lua_settop(L, top0); return; }

    const int len = static_cast<int>(g_lua_objlen(L, -1));
    int foundIdx = -1;

    for (int i = 1; i <= len; ++i)
    {
        g_lua_rawgeti(L, -1, i);
        const bool isMatch =
            g_lua_isnumber(L, -1) &&
            (static_cast<int>(g_lua_tointeger(L, -1)) == missionCode);
        g_lua_settop(L, -2);
        if (isMatch)
        {
            foundIdx = i;
            break;
        }
    }

    if (foundIdx < 0)
    {
        g_lua_settop(L, top0);
        return;
    }

    // list[i] = list[i+1] for i = foundIdx..len-1
    for (int i = foundIdx; i < len; ++i)
    {
        g_lua_rawgeti(L, -1, i + 1);
        g_lua_pushnumber(L, static_cast<lua_Number>(i));
        g_lua_pushvalue(L, -2);
        g_lua_rawset(L, -4);
        g_lua_settop(L, -2);
    }

    // list[len] = nil
    g_lua_pushnumber(L, static_cast<lua_Number>(len));
    g_lua_pushnil(L);
    g_lua_rawset(L, -3);

    g_lua_settop(L, top0);

    Log("[MissionEmergency] removed %d from TppDefine.EMERGENCY_MISSION_LIST\n",
        missionCode);
}


namespace
{
    static bool g_VFI_AcceptEmergencyMissionOverrideInstalled = false;
    static bool g_VFI_GoToEmergencyMissionOverrideInstalled = false;
}


// AcceptEmergencyMission override. When sortie prep is enabled (default), forward to original (engine runs full AbortMission chain). When disabled, call ReserveMissionClear directly (IH-style, no sortie prep, no "Return to Mission").
static int __cdecl l_VFI_AcceptEmergencyMissionOverride(lua_State* L)
{
    if (!ResolveLuaApi() ||
        !g_lua_pushvalue || !g_lua_pcall || !g_lua_gettop ||
        !g_lua_isnumber || !g_lua_tointeger || !g_lua_type ||
        !g_lua_settop || !g_lua_getfield || !g_lua_createtable ||
        !g_lua_pushstring || !g_lua_pushnumber || !g_lua_settable)
    {
        return 0;
    }

    const int       nargs       = g_lua_gettop(L);
    const long long missionCode = (nargs >= 1 && g_lua_isnumber(L, 1)) ? g_lua_tointeger(L, 1) : 0;

    const bool isAllowlisted =
        missionCode > 0 && missionCode <= 0xFFFF &&
        MissionEmergency_IsEnabled(static_cast<std::uint16_t>(missionCode));

    const bool sortiePrepEnabled =
        isAllowlisted
            ? MissionEmergency_IsSortiePrepEnabled(static_cast<std::uint16_t>(missionCode))
            : true;

    // Default path: forward to original (engine handles AbortMission, sortie prep, etc.)
    if (sortiePrepEnabled)
    {
        Log("[MissionEmergency] AcceptEmergencyMission OVERRIDE mc=%lld -> forward (sortie prep on)\n",
            missionCode);

        g_lua_pushvalue(L, LUA_UPVALUEINDEX_51_1);
        for (int i = 1; i <= nargs; ++i)
            g_lua_pushvalue(L, i);

        const int err = g_lua_pcall(L, nargs, 0, 0);
        if (err != 0)
        {
            const char* errMsg = g_lua_tolstring ? g_lua_tolstring(L, -1, nullptr) : nullptr;
            Log("[MissionEmergency] AcceptEmergencyMission original pcall ERR=%d mc=%lld: %s\n",
                err, missionCode, errMsg ? errMsg : "<no message>");
        }
        return 0;
    }

    // IH-style: skip AbortMission chain, call TppMission.ReserveMissionClear directly.
    Log("[MissionEmergency] AcceptEmergencyMission OVERRIDE mc=%lld -> IH-style direct ReserveMissionClear (sortie prep off)\n",
        missionCode);

    const int top0 = g_lua_gettop(L);

    // Manually set the interrupt-mission state that AbortMission would normally set,
    // so "Return to Mission" on game-over still works even without sortie prep.
    long long currentMissionCode = 0;
    g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("vars"));
    if (g_lua_type(L, -1) == LUA_TTABLE)
    {
        g_lua_getfield(L, -1, const_cast<char*>("missionCode"));
        if (g_lua_isnumber(L, -1)) currentMissionCode = g_lua_tointeger(L, -1);
    }
    g_lua_settop(L, top0);

    g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("Ivars"));
    if (g_lua_type(L, -1) == LUA_TTABLE)
    {
        g_lua_pushstring(L, const_cast<char*>("prevMissionCode"));
        g_lua_pushnumber(L, static_cast<lua_Number>(currentMissionCode));
        g_lua_rawset(L, -3);
    }
    g_lua_settop(L, top0);

    g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("mvars"));
    if (g_lua_type(L, -1) == LUA_TTABLE)
    {
        g_lua_pushstring(L, const_cast<char*>("mis_isInterruptMission"));
        g_lua_pushboolean(L, 1);
        g_lua_rawset(L, -3);

        g_lua_pushstring(L, const_cast<char*>("mis_emergencyMissionCode"));
        g_lua_pushnumber(L, static_cast<lua_Number>(missionCode));
        g_lua_rawset(L, -3);

        g_lua_pushstring(L, const_cast<char*>("mis_nextMissionCodeForAbort"));
        g_lua_pushnumber(L, static_cast<lua_Number>(currentMissionCode));
        g_lua_rawset(L, -3);
    }
    g_lua_settop(L, top0);

    g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("gvars"));
    if (g_lua_type(L, -1) == LUA_TTABLE)
    {
        g_lua_pushstring(L, const_cast<char*>("usingNormalMissionSlot"));
        g_lua_pushboolean(L, 0);
        g_lua_rawset(L, -3);

        g_lua_pushstring(L, const_cast<char*>("mis_nextMissionCodeForEmergency"));
        g_lua_pushnumber(L, static_cast<lua_Number>(missionCode));
        g_lua_rawset(L, -3);
    }
    g_lua_settop(L, top0);

    Log("[MissionEmergency] interrupt-mission state set: prevMissionCode=%lld mis_isInterruptMission=true mis_emergencyMissionCode=%lld\n",
        currentMissionCode, missionCode);

    long long missionClearTypeFromHelispace = 1;
    g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("TppDefine"));
    if (g_lua_type(L, -1) == LUA_TTABLE)
    {
        g_lua_getfield(L, -1, const_cast<char*>("MISSION_CLEAR_TYPE"));
        if (g_lua_type(L, -1) == LUA_TTABLE)
        {
            g_lua_getfield(L, -1, const_cast<char*>("FROM_HELISPACE"));
            if (g_lua_isnumber(L, -1))
                missionClearTypeFromHelispace = g_lua_tointeger(L, -1);
        }
    }
    g_lua_settop(L, top0);

    long long clusterId = (nargs >= 3 && g_lua_isnumber(L, 3)) ? g_lua_tointeger(L, 3) : 2;
    if (clusterId <= 0) clusterId = 2;

    g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("TppMission"));
    if (g_lua_type(L, -1) != LUA_TTABLE)
    {
        g_lua_settop(L, top0);
        Log("[MissionEmergency] AcceptEmergencyMission OVERRIDE: TppMission missing\n");
        return 0;
    }
    g_lua_getfield(L, -1, const_cast<char*>("ReserveMissionClear"));
    if (g_lua_type(L, -1) != LUA_TFUNCTION)
    {
        g_lua_settop(L, top0);
        Log("[MissionEmergency] AcceptEmergencyMission OVERRIDE: ReserveMissionClear missing\n");
        return 0;
    }

    g_lua_createtable(L, 0, 3);
    g_lua_pushstring(L, const_cast<char*>("missionClearType"));
    g_lua_pushnumber(L, static_cast<lua_Number>(missionClearTypeFromHelispace));
    g_lua_settable(L, -3);
    g_lua_pushstring(L, const_cast<char*>("nextMissionId"));
    g_lua_pushnumber(L, static_cast<lua_Number>(missionCode));
    g_lua_settable(L, -3);
    g_lua_pushstring(L, const_cast<char*>("nextClusterId"));
    g_lua_pushnumber(L, static_cast<lua_Number>(clusterId));
    g_lua_settable(L, -3);

    const int err = g_lua_pcall(L, 1, 0, 0);
    if (err != 0)
    {
        const char* errMsg = g_lua_tolstring ? g_lua_tolstring(L, -1, nullptr) : nullptr;
        Log("[MissionEmergency] AcceptEmergencyMission OVERRIDE: ReserveMissionClear pcall ERR=%d mc=%lld: %s\n",
            err, missionCode, errMsg ? errMsg : "<no message>");
    }

    g_lua_settop(L, top0);
    return 0;
}


// GoToEmergencyMission override — replicates the engine body but skips the
// route gate so non-50050 missions with route=0 can still load (on-foot).
// For non-allowlisted missions, forwards to the original verbatim.
//
// Original body (TppMission.lua:3593-3614):
//   local emergencyMissionCode = gvars.mis_nextMissionCodeForEmergency
//   local startRoute
//   if emergencyMissionCode ~= TppDefine.SYS_MISSION_ID.FOB then
//     if gvars.mis_nextMissionStartRouteForEmergency ~= 0 then
//       startRoute = gvars.mis_nextMissionStartRouteForEmergency
//     else
//       return  -- <-- the gate we bypass for allowlisted missions
//     end
//   end
//   -- mbLayoutCode is computed but unused (engine dead-store)
//   local clusterId = 2
//   if gvars.mis_nextClusterIdForEmergency ~= TppDefine.INVALID_CLUSTER_ID then
//     clusterId = gvars.mis_nextClusterIdForEmergency
//   end
//   this.ReserveMissionClear{ missionClearType=FROM_HELISPACE, nextMissionId=mc,
//                             nextHeliRoute=startRoute, nextClusterId=clusterId }
static int __cdecl l_VFI_GoToEmergencyMissionOverride(lua_State* L)
{
    if (!ResolveLuaApi() ||
        !g_lua_getfield || !g_lua_type || !g_lua_settop || !g_lua_gettop ||
        !g_lua_isnumber || !g_lua_tointeger || !g_lua_pushvalue || !g_lua_pcall ||
        !g_lua_createtable || !g_lua_pushstring || !g_lua_pushnumber || !g_lua_settable)
    {
        Log("[MissionEmergency] GoToEmergencyMission OVERRIDE: required FFI missing; bailing\n");
        return 0;
    }

    const int top0 = g_lua_gettop(L);

    // Read gvars.mis_nextMissionCodeForEmergency
    long long mc = 0;
    g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("gvars"));
    if (g_lua_type(L, -1) == LUA_TTABLE)
    {
        g_lua_getfield(L, -1, const_cast<char*>("mis_nextMissionCodeForEmergency"));
        if (g_lua_isnumber(L, -1)) mc = g_lua_tointeger(L, -1);
    }
    g_lua_settop(L, top0);

    const bool isAllowlisted =
        mc > 0 && mc <= 0xFFFF &&
        MissionEmergency_IsEnabled(static_cast<std::uint16_t>(mc));

    // Non-allowlisted: forward verbatim (preserves engine behavior for vanilla
    // emergency missions V_FrameWork doesn't have in its allowlist).
    if (!isAllowlisted)
    {
        g_lua_pushvalue(L, LUA_UPVALUEINDEX_51_1);
        const int err = g_lua_pcall(L, 0, 0, 0);
        if (err != 0)
        {
            const char* errMsg = g_lua_tolstring ? g_lua_tolstring(L, -1, nullptr) : nullptr;
            Log("[MissionEmergency] original GoToEmergencyMission pcall ERR=%d mc=%lld: %s\n",
                err, mc, errMsg ? errMsg : "<no message>");
            g_lua_settop(L, top0);
        }
        return 0;
    }

    // Allowlisted: replicate the body but skip the route-gate `return`.

    long long startRoute = 0;
    g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("gvars"));
    if (g_lua_type(L, -1) == LUA_TTABLE)
    {
        g_lua_getfield(L, -1, const_cast<char*>("mis_nextMissionStartRouteForEmergency"));
        if (g_lua_isnumber(L, -1)) startRoute = g_lua_tointeger(L, -1);
    }
    g_lua_settop(L, top0);
    const bool hasRoute = (startRoute != 0);

    long long clusterId = 2;
    g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("gvars"));
    if (g_lua_type(L, -1) == LUA_TTABLE)
    {
        g_lua_getfield(L, -1, const_cast<char*>("mis_nextClusterIdForEmergency"));
        if (g_lua_isnumber(L, -1))
        {
            const long long c = g_lua_tointeger(L, -1);
            if (c > 0) clusterId = c;
        }
    }
    g_lua_settop(L, top0);

    long long missionClearTypeFromHelispace = 1;
    g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("TppDefine"));
    if (g_lua_type(L, -1) == LUA_TTABLE)
    {
        g_lua_getfield(L, -1, const_cast<char*>("MISSION_CLEAR_TYPE"));
        if (g_lua_type(L, -1) == LUA_TTABLE)
        {
            g_lua_getfield(L, -1, const_cast<char*>("FROM_HELISPACE"));
            if (g_lua_isnumber(L, -1))
                missionClearTypeFromHelispace = g_lua_tointeger(L, -1);
        }
    }
    g_lua_settop(L, top0);

    g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("TppMission"));
    if (g_lua_type(L, -1) != LUA_TTABLE)
    {
        g_lua_settop(L, top0);
        Log("[MissionEmergency] GoToEmergencyMission OVERRIDE: TppMission missing\n");
        return 0;
    }
    g_lua_getfield(L, -1, const_cast<char*>("ReserveMissionClear"));
    if (g_lua_type(L, -1) != LUA_TFUNCTION)
    {
        g_lua_settop(L, top0);
        Log("[MissionEmergency] GoToEmergencyMission OVERRIDE: ReserveMissionClear missing\n");
        return 0;
    }

    g_lua_createtable(L, 0, hasRoute ? 4 : 3);

    g_lua_pushstring(L, const_cast<char*>("missionClearType"));
    g_lua_pushnumber(L, static_cast<lua_Number>(missionClearTypeFromHelispace));
    g_lua_settable(L, -3);

    g_lua_pushstring(L, const_cast<char*>("nextMissionId"));
    g_lua_pushnumber(L, static_cast<lua_Number>(mc));
    g_lua_settable(L, -3);

    if (hasRoute)
    {
        g_lua_pushstring(L, const_cast<char*>("nextHeliRoute"));
        g_lua_pushnumber(L, static_cast<lua_Number>(startRoute));
        g_lua_settable(L, -3);
    }

    g_lua_pushstring(L, const_cast<char*>("nextClusterId"));
    g_lua_pushnumber(L, static_cast<lua_Number>(clusterId));
    g_lua_settable(L, -3);

    Log("[MissionEmergency] GoToEmergencyMission OVERRIDE mc=%lld -> ReserveMissionClear(cluster=%lld%s)\n",
        mc, clusterId, hasRoute ? ", heli" : ", on-foot");

    const int err = g_lua_pcall(L, 1, 0, 0);
    if (err != 0)
    {
        const char* errMsg = g_lua_tolstring ? g_lua_tolstring(L, -1, nullptr) : nullptr;
        Log("[MissionEmergency] ReserveMissionClear pcall ERR=%d mc=%lld: %s\n",
            err, mc, errMsg ? errMsg : "<no message>");
    }

    g_lua_settop(L, top0);
    return 0;
}


static bool InstallAcceptEmergencyMissionOverride(lua_State* L)
{
    if (g_VFI_AcceptEmergencyMissionOverrideInstalled)
        return true;

    if (!ResolveLuaApi() || !g_lua_pushcclosure || !g_lua_pcall)
    {
        Log("[MissionEmergency] InstallAcceptEmergencyMissionOverride: Lua FFI unavailable; skipped\n");
        return false;
    }

    const int top0 = g_lua_gettop(L);

    g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("TppMission"));
    if (g_lua_type(L, -1) != LUA_TTABLE)
    {
        g_lua_settop(L, top0);
        Log("[MissionEmergency] InstallAcceptEmergencyMissionOverride: TppMission not loaded; deferred\n");
        return false;
    }

    g_lua_getfield(L, -1, const_cast<char*>("AcceptEmergencyMission"));
    if (g_lua_type(L, -1) != LUA_TFUNCTION)
    {
        g_lua_settop(L, top0);
        Log("[MissionEmergency] InstallAcceptEmergencyMissionOverride: original function missing\n");
        return false;
    }

    g_lua_pushcclosure(L, &l_VFI_AcceptEmergencyMissionOverride, 1);

    g_lua_pushstring(L, const_cast<char*>("AcceptEmergencyMission"));
    g_lua_pushvalue(L, -2);
    g_lua_rawset(L, -4);

    g_lua_settop(L, top0);

    g_VFI_AcceptEmergencyMissionOverrideInstalled = true;
    Log("[MissionEmergency] InstallAcceptEmergencyMissionOverride: installed\n");
    return true;
}


static bool InstallGoToEmergencyMissionOverride(lua_State* L)
{
    if (g_VFI_GoToEmergencyMissionOverrideInstalled)
        return true;

    if (!ResolveLuaApi() || !g_lua_pushcclosure || !g_lua_pcall)
    {
        Log("[MissionEmergency] InstallGoToEmergencyMissionOverride: Lua FFI unavailable; skipped\n");
        return false;
    }

    const int top0 = g_lua_gettop(L);

    g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("TppMission"));
    if (g_lua_type(L, -1) != LUA_TTABLE)
    {
        g_lua_settop(L, top0);
        Log("[MissionEmergency] InstallGoToEmergencyMissionOverride: TppMission not loaded; deferred\n");
        return false;
    }

    g_lua_getfield(L, -1, const_cast<char*>("GoToEmergencyMission"));
    if (g_lua_type(L, -1) != LUA_TFUNCTION)
    {
        g_lua_settop(L, top0);
        Log("[MissionEmergency] InstallGoToEmergencyMissionOverride: original function missing\n");
        return false;
    }

    g_lua_pushcclosure(L, &l_VFI_GoToEmergencyMissionOverride, 1);

    g_lua_pushstring(L, const_cast<char*>("GoToEmergencyMission"));
    g_lua_pushvalue(L, -2);
    g_lua_rawset(L, -4);

    g_lua_settop(L, top0);

    g_VFI_GoToEmergencyMissionOverrideInstalled = true;
    Log("[MissionEmergency] InstallGoToEmergencyMissionOverride: installed\n");
    return true;
}


// Optional helper: register an on-foot start position for missionCode.
// Patches the engine-side TppDefine.NO_HELICOPTER_MISSION_START_POSITION
// table (consumed by TppMain.LoadingPositionFromHeliSpace) and appends to
// TppDefine.NO_HELICOPTER_ROUTE_MISSION_LIST. Also flags the mission in
// V_FrameWork's C++ side so the AcceptEmergencyMission override takes the
// IH-style direct ReserveMissionClear path (skips the GoToEmergencyMission
// route gate).
//
// Args: missionCode (int), x (number), y (number), z (number), rotY (number, default 0).
static int __cdecl l_SetMissionStartPos(lua_State* L)
{
    const int missionCodeRaw = GetLuaInt(L, 1);
    const float x    = LuaIsNumber(L, 2) ? GetLuaNumber(L, 2) : 0.0f;
    const float y    = LuaIsNumber(L, 3) ? GetLuaNumber(L, 3) : 0.0f;
    const float z    = LuaIsNumber(L, 4) ? GetLuaNumber(L, 4) : 0.0f;
    const float rotY = LuaIsNumber(L, 5) ? GetLuaNumber(L, 5) : 0.0f;

    if (missionCodeRaw <= 0 || missionCodeRaw > 0xFFFF)
    {
        Log("[MissionEmergency] SetMissionStartPos: missionCode %d out of range; bailing\n",
            missionCodeRaw);
        return 0;
    }

    Log("[MissionEmergency] SetMissionStartPos mc=%d pos=(%g, %g, %g, %g)\n",
        missionCodeRaw, x, y, z, rotY);

    if (!ResolveLuaApi() ||
        !g_lua_getfield || !g_lua_type || !g_lua_settop ||
        !g_lua_createtable || !g_lua_pushnumber || !g_lua_rawset ||
        !g_lua_pushvalue || !g_lua_objlen || !g_lua_rawgeti ||
        !g_lua_isstring || !g_lua_tolstring || !g_lua_pushstring)
    {
        Log("[MissionEmergency] SetMissionStartPos: required FFI missing\n");
        return 0;
    }

    const int top0 = g_lua_gettop(L);

    g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("TppDefine"));
    if (g_lua_type(L, -1) != LUA_TTABLE)
    {
        Log("[MissionEmergency] SetMissionStartPos: TppDefine missing\n");
        g_lua_settop(L, top0);
        return 0;
    }
    // Stack: [TppDefine]

    // Patch TppDefine.NO_HELICOPTER_MISSION_START_POSITION[mc] = {x, y, z, rotY}
    g_lua_getfield(L, -1, const_cast<char*>("NO_HELICOPTER_MISSION_START_POSITION"));
    if (g_lua_type(L, -1) != LUA_TTABLE)
    {
        Log("[MissionEmergency] SetMissionStartPos: NO_HELICOPTER_MISSION_START_POSITION missing\n");
        g_lua_settop(L, top0);
        return 0;
    }
    // Stack: [TppDefine, NO_HELICOPTER_MISSION_START_POSITION]

    g_lua_createtable(L, 4, 0);                                // posTable
    g_lua_pushnumber(L, 1);  g_lua_pushnumber(L, x);    g_lua_rawset(L, -3);
    g_lua_pushnumber(L, 2);  g_lua_pushnumber(L, y);    g_lua_rawset(L, -3);
    g_lua_pushnumber(L, 3);  g_lua_pushnumber(L, z);    g_lua_rawset(L, -3);
    g_lua_pushnumber(L, 4);  g_lua_pushnumber(L, rotY); g_lua_rawset(L, -3);
    // Stack: [TppDefine, NO_HELI_..._POSITION, posTable]

    g_lua_pushnumber(L, static_cast<lua_Number>(missionCodeRaw));
    g_lua_pushvalue(L, -2);                                    // copy posTable
    g_lua_rawset(L, -4);                                       // [NO_HELI..._POSITION][mc] = posTable
    g_lua_settop(L, top0 + 1);                                 // keep only TppDefine on stack

    // Append tostring(missionCode) to NO_HELICOPTER_ROUTE_MISSION_LIST if absent.
    g_lua_getfield(L, -1, const_cast<char*>("NO_HELICOPTER_ROUTE_MISSION_LIST"));
    if (g_lua_type(L, -1) == LUA_TTABLE)
    {
        char mcStr[16];
        std::snprintf(mcStr, sizeof(mcStr), "%d", missionCodeRaw);

        const int len = static_cast<int>(g_lua_objlen(L, -1));
        bool found = false;
        for (int i = 1; i <= len; ++i)
        {
            g_lua_rawgeti(L, -1, i);
            if (g_lua_isstring(L, -1))
            {
                const char* s = g_lua_tolstring(L, -1, nullptr);
                if (s && std::strcmp(s, mcStr) == 0)
                    found = true;
            }
            g_lua_settop(L, -2);
            if (found) break;
        }

        if (!found)
        {
            g_lua_pushnumber(L, static_cast<lua_Number>(len + 1));
            g_lua_pushstring(L, mcStr);
            g_lua_rawset(L, -3);
        }
    }

    g_lua_settop(L, top0);

    // Make sure our Lua-side overrides are installed (idempotent) in case
    // the user calls SetMissionStartPos before SetMissionEmergency.
    InstallIsEmergencyMissionOverride(L);
    InstallAcceptEmergencyMissionOverride(L);
    InstallGoToEmergencyMissionOverride(L);

    return 0;
}


static int __cdecl l_SetMissionEmergency(lua_State* L)
{
    const int  missionCodeRaw = GetLuaInt(L, 1);
    const bool enabled        = GetLuaBool(L, 2) != 0;

    // Arg 3 = enableSortiePrep, default false (IH-style direct path).
    bool enableSortiePrep = false;
    if (g_lua_gettop && g_lua_gettop(L) >= 3 && g_lua_type)
    {
        const int t = g_lua_type(L, 3);
        if (t == LUA_TBOOLEAN) enableSortiePrep = GetLuaBool(L, 3);
    }

    if (missionCodeRaw <= 0 || missionCodeRaw > 0xFFFF)
    {
        Log("[MissionEmergency] SetMissionEmergency: missionCode %d out of range; bailing\n",
            missionCodeRaw);
        return 0;
    }

    Log("[MissionEmergency] SetMissionEmergency mc=%d enabled=%d sortiePrep=%d\n",
        missionCodeRaw, enabled ? 1 : 0, enableSortiePrep ? 1 : 0);

    const std::uint16_t missionCode = static_cast<std::uint16_t>(missionCodeRaw);

    MissionEmergency_SetEnabled(missionCode, enabled);

    if (enabled)
        MissionEmergency_SetSortiePrepEnabled(missionCode, enableSortiePrep);
    else
        MissionEmergency_SetSortiePrepEnabled(missionCode, true); // reset to default

    InstallIsEmergencyMissionOverride(L);
    InstallAcceptEmergencyMissionOverride(L);
    InstallGoToEmergencyMissionOverride(L);

    if (enabled)
        AppendToEmergencyMissionList(L, missionCodeRaw);
    else
        RemoveFromEmergencyMissionList(L, missionCodeRaw);

    return 0;
}


static int __cdecl l_IsMissionEmergency(lua_State* L)
{
    const int missionCodeRaw = GetLuaInt(L, 1);

    bool result = false;
    if (missionCodeRaw > 0 && missionCodeRaw <= 0xFFFF)
    {
        result = MissionEmergency_IsEnabled(
            static_cast<std::uint16_t>(missionCodeRaw));
    }

    PushLuaBool(L, result);
    return 1;
}


static int __cdecl l_ClearAllMissionEmergencies(lua_State* L)
{
    UNREFERENCED_PARAMETER(L);
    MissionEmergency_ClearAll();
    return 0;
}


static int __cdecl l_ShowEmergencyMissionPopup(lua_State* L)
{
    const char* title = LuaIsString(L, 1) ? GetLuaString(L, 1) : nullptr;
    const char* body  = LuaIsString(L, 2) ? GetLuaString(L, 2) : nullptr;

    const bool ok = Show_MbDvcEmergencyPopup(title, body);
    PushLuaBool(L, ok);
    return 1;
}


static int __cdecl l_ShowEmergencyMissionPopupLangId(lua_State* L)
{
    const char* titleLabel = LuaIsString(L, 1) ? GetLuaString(L, 1) : nullptr;
    const char* bodyLabel  = LuaIsString(L, 2) ? GetLuaString(L, 2) : nullptr;

    const bool ok = Show_MbDvcEmergencyPopupLangId(titleLabel, bodyLabel);
    PushLuaBool(L, ok);
    return 1;
}


static int __cdecl l_ClearEmergencyMissionPopupOverride(lua_State* L)
{
    UNREFERENCED_PARAMETER(L);
    Clear_MbDvcEmergencyPopupOverride();
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


static int __cdecl l_SetEyeLampColor(lua_State* L)
{
    const float r          = GetLuaNumber(L, 1);
    const float g          = GetLuaNumber(L, 2);
    const float b          = GetLuaNumber(L, 3);
    const float pulseSpeed = GetLuaNumber(L, 4);
    const int   mode       = static_cast<int>(GetLuaInt64(L, 5));
    ::Set_EyeLampColor(mode, r, g, b, pulseSpeed);
    return 0;
}


static int __cdecl l_ClearEyeLampColor(lua_State* L)
{
    UNREFERENCED_PARAMETER(L);
    ::Clear_EyeLampColor();
    return 0;
}


static int __cdecl l_SetEyeLampColorLogging(lua_State* L)
{
    const bool enabled = GetLuaBool(L, 1);
    ::Set_EyeLampColorLogging(enabled);
    return 0;
}


static int __cdecl l_SetSahelanPhase(lua_State* L)
{
    const std::int32_t phase = static_cast<std::int32_t>(GetLuaInt64(L, 1));
    ::Set_SahelanForcePhase(phase);
    return 0;
}

static int __cdecl l_ClearSahelanPhase(lua_State* L)
{
    UNREFERENCED_PARAMETER(L);
    ::Clear_SahelanForcePhase();
    return 0;
}

static int __cdecl l_GetSahelanPhase(lua_State* L)
{
    PushLuaNumber(L, static_cast<float>(::Get_SahelanCurrentPhase()));
    return 1;
}


static int __cdecl l_SetEyeLampDisco(lua_State* L)
{
    const bool  enabled = GetLuaBool(L, 1);
    const float speed   = GetLuaNumber(L, 2);
    ::Set_EyeLampDisco(enabled, speed);
    return 0;
}


static int __cdecl l_SetHeartLightColor(lua_State* L)
{
    const float r          = GetLuaNumber(L, 1);
    const float g          = GetLuaNumber(L, 2);
    const float b          = GetLuaNumber(L, 3);
    const float pulseSpeed = GetLuaNumber(L, 4);
    ::Set_HeartLightColor(r, g, b, pulseSpeed);
    return 0;
}


static int __cdecl l_ClearHeartLightColor(lua_State* L)
{
    UNREFERENCED_PARAMETER(L);
    ::Clear_HeartLightColor();
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


static int __cdecl l_SetEnemyInformationLangId(lua_State* L)
{
    const char* name = GetLuaString(L, 1);
    if (!name || !*name)
        return 0;

    EnemyLangId_SetMapOverride(FoxHashes::StrCode64(name));
    return 0;
}


static int __cdecl l_ClearEnemyInformationLangId(lua_State* L)
{
    UNREFERENCED_PARAMETER(L);
    EnemyLangId_ClearMapOverride();
    return 0;
}


static int __cdecl l_SetEnemyUnitName(lua_State* L)
{
    const char* name = GetLuaString(L, 1);
    if (!name || !*name)
        return 0;

    EnemyLangId_SetBinoOverride(FoxHashes::StrCode64(name));
    return 0;
}


static int __cdecl l_ClearEnemyUnitName(lua_State* L)
{
    UNREFERENCED_PARAMETER(L);
    EnemyLangId_ClearBinoOverride();
    return 0;
}


static int __cdecl l_SetEnemyInformationLangIdForSoldier(lua_State* L)
{
    const std::uint32_t gameObjectId = static_cast<std::uint32_t>(GetLuaInt64(L, 1));
    const char* name = GetLuaString(L, 2);
    if (!name || !*name)
        return 0;

    EnemyLangId_SetMapOverrideForSoldier(gameObjectId, FoxHashes::StrCode64(name));
    return 0;
}


static int __cdecl l_ClearEnemyInformationLangIdForSoldier(lua_State* L)
{
    const std::uint32_t gameObjectId = static_cast<std::uint32_t>(GetLuaInt64(L, 1));
    EnemyLangId_ClearMapOverrideForSoldier(gameObjectId);
    return 0;
}


static int __cdecl l_ClearAllEnemyInformationLangIdForSoldiers(lua_State* L)
{
    UNREFERENCED_PARAMETER(L);
    EnemyLangId_ClearAllMapOverridesForSoldier();
    return 0;
}


static int __cdecl l_SetEnemyUnitNameForSoldier(lua_State* L)
{
    const std::uint32_t gameObjectId = static_cast<std::uint32_t>(GetLuaInt64(L, 1));
    const char* name = GetLuaString(L, 2);
    if (!name || !*name)
        return 0;

    EnemyLangId_SetBinoOverrideForSoldier(gameObjectId, FoxHashes::StrCode64(name));
    return 0;
}


static int __cdecl l_ClearEnemyUnitNameForSoldier(lua_State* L)
{
    const std::uint32_t gameObjectId = static_cast<std::uint32_t>(GetLuaInt64(L, 1));
    EnemyLangId_ClearBinoOverrideForSoldier(gameObjectId);
    return 0;
}


static int __cdecl l_ClearAllEnemyUnitNameForSoldiers(lua_State* L)
{
    UNREFERENCED_PARAMETER(L);
    EnemyLangId_ClearAllBinoOverridesForSoldier();
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
    { "SetPitchByAkObjId",                      l_SetPitchByAkObjId },
    { "ClearPitchByAkObjId",                    l_ClearPitchByAkObjId },
    { "ClearAllPerAkObjIdPitchBiases",          l_ClearAllPerAkObjIdPitchBiases },
    { "GetSoldierAkObjId",                      l_GetSoldierAkObjId },
    { "SetSoldierVoicePitch",                   l_SetSoldierVoicePitch },
    { "SetVIPImportant",                        l_SetVIPImportant },
    { "SetUseConcernedHoldupRecovery",          l_SetUseConcernedHoldupRecovery },
    { "RemoveVIPImportant",                     l_RemoveVIPImportant },
    { "ClearVIPImportant",                      l_ClearVIPImportant },
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
    { "SetMissionEmergency",                    l_SetMissionEmergency },
    { "SetMissionStartPos",                     l_SetMissionStartPos },
    { "IsMissionEmergency",                     l_IsMissionEmergency },
    { "ClearAllMissionEmergencies",             l_ClearAllMissionEmergencies },
    { "ShowEmergencyMissionPopup",              l_ShowEmergencyMissionPopup },
    { "ShowEmergencyMissionPopupLangId",        l_ShowEmergencyMissionPopupLangId },
    { "ClearEmergencyMissionPopupOverride",     l_ClearEmergencyMissionPopupOverride },

    { "Log",                                    l_Log },
    { "GetModFiles",                            l_GetModFiles },


    { "EnableTornadoDual",                      l_EnableTornadoDual },


    { "SetSahelanFova",                         l_SetSahelanFova },
    { "ClearSahelanFova",                       l_ClearSahelanFova },
    { "SetEyeLampColor",                        l_SetEyeLampColor },
    { "ClearEyeLampColor",                      l_ClearEyeLampColor },
    { "SetEyeLampDisco",                        l_SetEyeLampDisco },
    { "SetHeartLightColor",                     l_SetHeartLightColor },
    { "ClearHeartLightColor",                   l_ClearHeartLightColor },
    { "SetEyeLampColorLogging",                 l_SetEyeLampColorLogging },

    { "SetSahelanPhase",                        l_SetSahelanPhase },
    { "ClearSahelanPhase",                      l_ClearSahelanPhase },
    { "GetSahelanPhase",                        l_GetSahelanPhase },

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


    { "SetEnemyInformationLangId",              l_SetEnemyInformationLangId },
    { "ClearEnemyInformationLangId",            l_ClearEnemyInformationLangId },
    { "SetEnemyUnitName",                       l_SetEnemyUnitName },
    { "ClearEnemyUnitName",                     l_ClearEnemyUnitName },

    { "SetEnemyInformationLangIdForSoldier",      l_SetEnemyInformationLangIdForSoldier },
    { "ClearEnemyInformationLangIdForSoldier",    l_ClearEnemyInformationLangIdForSoldier },
    { "ClearAllEnemyInformationLangIdForSoldiers",l_ClearAllEnemyInformationLangIdForSoldiers },
    { "SetEnemyUnitNameForSoldier",               l_SetEnemyUnitNameForSoldier },
    { "ClearEnemyUnitNameForSoldier",             l_ClearEnemyUnitNameForSoldier },
    { "ClearAllEnemyUnitNameForSoldiers",         l_ClearAllEnemyUnitNameForSoldiers },

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
