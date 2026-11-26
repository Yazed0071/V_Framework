#include <pch.h>
#include "GameObjectSendCommand.h"

extern "C" {
    #include "lua.h"
}

#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <algorithm>
#include <utility>
#include <vector>

#include "MinHook.h"
#include "log.h"
#include "../../../core/AddressSet.h"
#include "../../../core/HookUtils.h"
#include "../../../core/MissionCodeGuard.h"
#include "../../../core/MissionStateReset.h"
#include "../../../core/DeployGuard.h"
#include "../../player/QuietCqcPatches.h"
#include "../../player/ActionCoreImpl_ExecPostPutMotion.h"
#include "../../outfit/UniqueCharacterSuitSlot.h"
#include "../../sahelan/PhaseSneakAiImpl_PreUpdate.h"
#include "../../ui/HeadMarkMarkerEvCall_SetIconSubType.h"
#include "../../sahelan/RealizedSahelanFovaHook.h"
#include "../../sahelan/SetEyeLampColorHook.h"
#include "../../soldier/LostHostageHook.h"
#include "../../soldier/StepRadioDiscovery.h"
#include "../../soldier/HostageGender.h"
#include "../../soldier/CoreAiImpl_SetSpecialActionParam.h"
#include "../../soldier/VIPImportant.h"
#include "../../soldier/GetVoiceParamWithCallSign.h"
#include "../../soldier/ActionCoreImpl_UpdateOptCamo.h"
#include "../../soldier/NoticeControllerImpl_GetOccasionalChat.h"
#include "../../soldier/CautionStepNormalTimerHook.h"
#include "../../soldier/CommandPost2Impl_RequestInvestigation.h"
#include "../../soldier/CpRadioVariants.h"
#include "../../soldier/HoldupAiImpl_StepHoldupStand.h"
#include "../../soldier/SoldierObjectRtpc.h"
#include "../../soldier/NoticeControllerImpl_CheckSightNoticePlayer.h"
#include "../../soldier/SoldierVehicleAvoid.h"
#include "../../soldier/Soldier2Impl_UpdateMarkers.h"
#include "../../soldier/SoldierFaction.h"
#include "../../soldier/GameObjectLife.h"
#include "../../soldier/NoticeControllerImpl_CheckForceNotice.h"
#include "../../soldier/NoticeControllerImpl_CheckSpecialSaluteVoiceLabel.h"
#include "../../sound/SpeechSystemImpl_FindSpeechSequenceIndex.h"
#include "GetGameObjectIdWithIndex.h"
#include "GameObjectInstance.h"
#include "GameObjectPosition.h"
#include "GameObjectReinitialize.h"
#include "../../animal/AnimalInstance.h"
#include "../../soldier/InterrogationVoiceEvent.h"
#include "../../soldier/RadioControllerImpl_StepRadioWaitResponse.h"
#include "../../soldier/SoldierAkObjIdMap.h"
#include "../../outfit/StaffControllerImpl_AddStaff.h"
#include "../../bullet/Bullet3Impl_ActivateBulletAtEmptyWorkPatch.h"
#include "../../../core/FoxHashes.h"
#include "../../../lua/LuaApi.h"
#include "../../equip/RankLimitedItems.h"
#include "../../equip/EquipPartParams.h"
#include "../../heli/ActionCoreImpl_Execute.h"
#include "../../heli/HeliKey.h"
#include "../../heli/HeliSoundController.h"
#include "../../heli/LightActionImpl_Execute.h"
#include "../../heli/MechaActionImpl_StateOnPassenger.h"
#include "../../heli/TppHeli2_LuaCommands.h"
#include "../../vehicle/RideVehicleActionImpl_CheckVehicleIconArea.h"
#include "../../player/RideHeliActionPluginImpl_ExecActionButtonFunction.h"
#include "../../soldier/RideHeliActionImpl_State_GetInAction.h"
#include "../../soldier/RealizedSoldier2Impl_ExecuteSerially.h"
#include "../../soldier/Soldier2CombatUtil_GrenadeUpdate.h"
#include "../../soldier/SoldierThrowCommand.h"

namespace
{

    using GameObjectSendCommand_t = int (__fastcall*)(lua_State* L);

    static GameObjectSendCommand_t g_OrigSendCommand = nullptr;
    static bool                    g_Installed       = false;

    static const char* ReadCommandId(lua_State* L, int cmdStackIdx, std::string* out)
    {
        out->clear();
        g_lua_pushstring(L, const_cast<char*>("id"));
        g_lua_gettable(L, cmdStackIdx);
        if (g_lua_type(L, -1) != LUA_TSTRING) return nullptr;
        const char* s = g_lua_tolstring(L, -1, nullptr);
        if (!s) return nullptr;
        *out = s;
        return out->c_str();
    }

    static bool IsWholeTypeTarget(lua_State* L, const char* typeName)   // { type = "TppSoldier2" } with no index: every object of that type
    {
        if (g_lua_type(L, 1) != LUA_TTABLE)
            return false;
        const int top = g_lua_gettop(L);
        g_lua_pushstring(L, const_cast<char*>("type"));
        g_lua_gettable(L, 1);
        const char* type = g_lua_type(L, -1) == LUA_TSTRING ? g_lua_tolstring(L, -1, nullptr) : nullptr;
        const bool matches = type && std::strcmp(type, typeName) == 0;
        g_lua_settop(L, top);
        g_lua_pushstring(L, const_cast<char*>("index"));
        g_lua_gettable(L, 1);
        const bool noIndex = g_lua_type(L, -1) == LUA_TNIL;
        g_lua_settop(L, top);
        return matches && noIndex;
    }

    static double ReadCommandNumber(lua_State* L, int cmdStackIdx, const char* key)
    {
        g_lua_pushstring(L, const_cast<char*>(key));
        g_lua_gettable(L, cmdStackIdx);
        double v = 0.0;
        if (g_lua_type(L, -1) == LUA_TNUMBER)
            v = static_cast<double>(g_lua_tonumber(L, -1));
        return v;
    }

    static double ReadCommandNumberOr(lua_State* L, int cmdStackIdx, const char* key, double def)
    {
        g_lua_pushstring(L, const_cast<char*>(key));
        g_lua_gettable(L, cmdStackIdx);
        double v = def;
        if (g_lua_type(L, -1) == LUA_TNUMBER)
            v = static_cast<double>(g_lua_tonumber(L, -1));
        return v;
    }

    static bool ReadCommandBool(lua_State* L, int cmdStackIdx, const char* key)
    {
        g_lua_pushstring(L, const_cast<char*>(key));
        g_lua_gettable(L, cmdStackIdx);
        bool v = false;
        const int t = g_lua_type(L, -1);
        if (t == LUA_TBOOLEAN)
            v = g_lua_toboolean(L, -1) != 0;
        else if (t == LUA_TNUMBER)
            v = static_cast<int>(g_lua_tonumber(L, -1)) != 0;
        return v;
    }

    static bool ReadCommandBoolOr(lua_State* L, int cmdStackIdx, const char* key, bool def)
    {
        g_lua_pushstring(L, const_cast<char*>(key));
        g_lua_gettable(L, cmdStackIdx);
        const int t = g_lua_type(L, -1);
        if (t == LUA_TBOOLEAN)
            return g_lua_toboolean(L, -1) != 0;
        if (t == LUA_TNUMBER)
            return static_cast<int>(g_lua_tonumber(L, -1)) != 0;
        return def;
    }

    static std::uint32_t ReadCommandStrCode32(lua_State* L, int cmdStackIdx, const char* key)
    {
        g_lua_pushstring(L, const_cast<char*>(key));
        g_lua_gettable(L, cmdStackIdx);
        std::uint32_t v = 0;
        const int t = g_lua_type(L, -1);
        if (t == LUA_TNUMBER)
            v = static_cast<std::uint32_t>(static_cast<long long>(g_lua_tonumber(L, -1)));
        else if (t == LUA_TSTRING)
        {
            const char* s = g_lua_tolstring(L, -1, nullptr);
            if (s && s[0])
                v = FoxHashes::StrCode32(s);
        }
        return v;
    }

    static std::uint32_t FnvHash32Of(const char* s)
    {
        using FNV_t = unsigned int(__fastcall*)(const char*);
        static FNV_t fn = nullptr;
        if (!fn && gAddr.FNVHash32)
            fn = reinterpret_cast<FNV_t>(ResolveGameAddress(gAddr.FNVHash32));
        if (!fn || !s || !s[0])
            return 0;
        __try { return static_cast<std::uint32_t>(fn(s)); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }

    static std::uint32_t ReadCommandFnvHash(lua_State* L, int cmdStackIdx, const char* key,
                                            bool* present)
    {
        g_lua_pushstring(L, const_cast<char*>(key));
        g_lua_gettable(L, cmdStackIdx);
        std::uint32_t v = 0;
        bool have = false;
        const int t = g_lua_type(L, -1);
        if (t == LUA_TNUMBER)
        {
            v = static_cast<std::uint32_t>(static_cast<long long>(g_lua_tonumber(L, -1)));
            have = true;
        }
        else if (t == LUA_TSTRING)
        {
            const char* s = g_lua_tolstring(L, -1, nullptr);
            if (s && s[0])
                v = FnvHash32Of(s);
            have = true;
        }
        if (present)
            *present = have;
        return v;
    }

    static void SetTableString(lua_State* L, const char* key, const char* value)
    {
        g_lua_pushstring(L, const_cast<char*>(key));
        g_lua_pushstring(L, const_cast<char*>(value));
        g_lua_settable(L, -3);
    }

    struct HostageStruggleDisarm
    {
        ~HostageStruggleDisarm() { ::Disarm_HostageStruggle(); }
    };

    static void SendStruggleSpecialAction(lua_State* L)
    {
        HostageStruggleDisarm disarm;
        g_lua_settop(L, 1);
        g_lua_createtable(L, 0, 3);
        SetTableString(L, "id", "SpecialAction");
        SetTableString(L, "action", "PlayState");
        SetTableString(L, "state", "vfw_struggle");
        g_OrigSendCommand(L);
    }

    static std::uint32_t ReadCommandTargetId(lua_State* L)
    {
        if (g_lua_type(L, 1) == LUA_TNUMBER)
            return static_cast<std::uint32_t>(g_lua_tointeger(L, 1) & 0xFFFFFFFFLL);
        return 0;
    }

    static std::size_t ReadLabelArray(lua_State* L, int cmdStackIdx, std::uint32_t* out, const char* key)
    {
        std::size_t n = 0;

        if (g_lua_objlen && g_lua_rawgeti)
        {
            g_lua_pushstring(L, const_cast<char*>(key));
            g_lua_gettable(L, cmdStackIdx);
            if (g_lua_type(L, -1) == LUA_TTABLE)
            {
                const int tbl = g_lua_gettop(L);
                const std::size_t len = g_lua_objlen(L, tbl);
                for (std::size_t i = 1; i <= len && n < 255; ++i)
                {
                    g_lua_rawgeti(L, tbl, static_cast<int>(i));
                    const int et = g_lua_type(L, -1);
                    if (et == LUA_TNUMBER)
                        out[n++] = static_cast<std::uint32_t>(static_cast<long long>(g_lua_tonumber(L, -1)));
                    else if (et == LUA_TSTRING)
                    {
                        const char* s = g_lua_tolstring(L, -1, nullptr);
                        if (s) out[n++] = FoxHashes::StrCode32(s);
                    }
                    g_lua_settop(L, tbl);
                }
            }
        }

        return n;
    }

    static std::uint32_t GatherLabelBases(lua_State* L, int tblIdx, std::uint32_t* out,
                                          std::uint32_t cap, std::uint32_t n, int depth)
    {
        if (!g_lua_pushnil || !g_lua_next || depth > 1)
            return n;
        g_lua_pushnil(L);
        while (g_lua_next(L, tblIdx) != 0)
        {
            const int vt = g_lua_type(L, -1);
            if (vt == LUA_TSTRING)
            {
                const char* s = g_lua_tolstring(L, -1, nullptr);
                const std::uint32_t sc = (s && s[0]) ? FoxHashes::StrCode32(s) : 0;
                const std::uint32_t fv = FnvHash32Of(s);
                if (sc && n < cap)
                    out[n++] = sc;
                if (fv && fv != sc && n < cap)
                    out[n++] = fv;
            }
            else if (vt == LUA_TNUMBER)
            {
                const std::uint32_t num =
                    static_cast<std::uint32_t>(static_cast<long long>(g_lua_tonumber(L, -1)));
                if (num >= 0x10000u && n < cap)
                    out[n++] = num;
            }
            else if (vt == LUA_TTABLE)
            {
                n = GatherLabelBases(L, g_lua_gettop(L), out, cap, n, depth + 1);
            }
            g_lua_settop(L, g_lua_gettop(L) - 1);
        }
        return n;
    }

    static bool ReadCommandPosition(lua_State* L, int cmdStackIdx, const char* key, float* out)
    {
        g_lua_pushstring(L, const_cast<char*>(key));
        g_lua_gettable(L, cmdStackIdx);
        if (g_lua_type(L, -1) != LUA_TTABLE || !g_lua_rawgeti)
            return false;

        const int tbl = g_lua_gettop(L);
        for (int i = 0; i < 3; ++i)
        {
            g_lua_rawgeti(L, tbl, i + 1);
            if (g_lua_type(L, -1) != LUA_TNUMBER)
                return false;
            out[i] = static_cast<float>(g_lua_tonumber(L, -1));
            g_lua_settop(L, tbl);
        }
        return true;
    }

    static bool PushVector3(lua_State* L, const float* position)
    {
        const int top = g_lua_gettop(L);
        g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("Vector3"));
        g_lua_pushnumber(L, static_cast<double>(position[0]));
        g_lua_pushnumber(L, static_cast<double>(position[1]));
        g_lua_pushnumber(L, static_cast<double>(position[2]));
        if (g_lua_pcall(L, 3, 1, 0) == 0)
            return true;
        g_lua_settop(L, top);
        return false;
    }

    static bool ReadCommandPoint(lua_State* L, int cmdStackIdx, const char* key, float* out)   // {x,y,z} or a Vector3
    {
        g_lua_pushstring(L, const_cast<char*>(key));
        g_lua_gettable(L, cmdStackIdx);
        const int at = g_lua_gettop(L);
        if (g_lua_type(L, at) == LUA_TTABLE)
            return ReadCommandPosition(L, cmdStackIdx, key, out);
        if (g_lua_type(L, at) != LUA_TUSERDATA || !g_lua_pcall || !g_lua_pushvalue)
            return false;
        const char* const getters[3] = { "GetX", "GetY", "GetZ" };
        for (int i = 0; i < 3; ++i)
        {
            g_lua_getfield(L, at, const_cast<char*>(getters[i]));
            g_lua_pushvalue(L, at);
            if (g_lua_pcall(L, 1, 1, 0) != 0 || g_lua_type(L, -1) != LUA_TNUMBER)
            {
                g_lua_settop(L, at);
                return false;
            }
            out[i] = static_cast<float>(g_lua_tonumber(L, -1));
            g_lua_settop(L, at);
        }
        return true;
    }

    static std::uint32_t ReadCommandSoldierIndices(lua_State* L, int cmdStackIdx, const char* key, std::uint32_t* out, std::uint32_t cap,
                                                   bool& given)
    {
        std::uint32_t n = 0;
        g_lua_pushstring(L, const_cast<char*>(key));
        g_lua_gettable(L, cmdStackIdx);
        given = g_lua_type(L, -1) != LUA_TNIL;
        if (g_lua_type(L, -1) != LUA_TTABLE || !g_lua_objlen || !g_lua_rawgeti)
            return 0;
        const int tbl = g_lua_gettop(L);
        const std::size_t len = g_lua_objlen(L, tbl);
        for (std::size_t i = 1; i <= len && n < cap; ++i)
        {
            g_lua_rawgeti(L, tbl, static_cast<int>(i));
            const std::uint32_t id = g_lua_type(L, -1) == LUA_TNUMBER
                ? static_cast<std::uint32_t>(static_cast<long long>(g_lua_tonumber(L, -1))) : 0xFFFFFFFFu;
            g_lua_settop(L, tbl);
            if ((id >> 9) != TppGameObjectType::kSoldier2)
            {
                LogWarn("RequestInvestigation: soldiers[%u] is not a soldier id, left out", static_cast<unsigned>(i));
                continue;
            }
            bool twice = false;
            for (std::uint32_t k = 0; k < n; ++k)
                twice = twice || out[k] == (id & 0x1FFu);
            if (!twice)
                out[n++] = id & 0x1FFu;
        }
        return n;
    }

    static bool ReadCommandCpIndex(lua_State* L, std::uint32_t& cp)   // one CP: its id or {type="TppCommandPost2", index=N}, never the whole type
    {
        const int top = g_lua_gettop(L);
        bool isCp = false;
        if (g_lua_type(L, 1) == LUA_TNUMBER)
        {
            const std::uint32_t id = ReadCommandTargetId(L);
            cp = id & 0x1FFu;
            isCp = (id >> 9) == TppGameObjectType::kCommandPost;
        }
        else if (g_lua_type(L, 1) == LUA_TTABLE)
        {
            g_lua_pushstring(L, const_cast<char*>("type"));
            g_lua_gettable(L, 1);
            const char* type = g_lua_type(L, -1) == LUA_TSTRING ? g_lua_tolstring(L, -1, nullptr) : nullptr;
            const bool typeOk = type && std::strcmp(type, "TppCommandPost2") == 0;
            g_lua_settop(L, top);
            g_lua_pushstring(L, const_cast<char*>("index"));
            g_lua_gettable(L, 1);
            if (typeOk && g_lua_type(L, -1) == LUA_TNUMBER)
            {
                const std::uint32_t index = static_cast<std::uint32_t>(static_cast<long long>(g_lua_tonumber(L, -1)));
                cp = index & 0x1FFu;   // vanilla scripts pass the CP's whole gameObjectId as index
                isCp = index < 0x200 || (index >> 9) == TppGameObjectType::kCommandPost;
            }
        }
        g_lua_settop(L, top);
        return isCp && cp < 0x100;
    }

    static double ReadGlobalNumber(lua_State* L, const char* table, const char* field)   // -1 when it isn't there
    {
        const int top = g_lua_gettop(L);
        double value = -1.0;
        g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>(table));
        const int t = g_lua_type(L, -1);
        if (t == LUA_TTABLE || t == LUA_TUSERDATA)
        {
            g_lua_getfield(L, -1, const_cast<char*>(field));
            if (g_lua_type(L, -1) == LUA_TNUMBER)
                value = static_cast<double>(g_lua_tonumber(L, -1));
        }
        g_lua_settop(L, top);
        return value;
    }

    static void RaiseCpToCaution(lua_State* L, std::uint32_t cp)   // vanilla SetPhase lowers a phase as readily as it raises it: only from below Caution
    {
        double caution = ReadGlobalNumber(L, "TppGameObject", "PHASE_CAUTION");
        if (caution < 0.0)
            caution = 1.0;
        g_lua_settop(L, 0);
        g_lua_pushnumber(L, static_cast<double>((TppGameObjectType::kCommandPost << 9) | cp));   // the vanilla SendCommand pops the target, so it goes back before every call
        g_lua_createtable(L, 0, 1);
        SetTableString(L, "id", "GetPhase");
        const int results = g_OrigSendCommand(L);
        const double phase = results > 0 && g_lua_type(L, -1) == LUA_TNUMBER ? static_cast<double>(g_lua_tonumber(L, -1)) : -1.0;
        g_lua_settop(L, 0);
        if (phase < 0.0)
        {
            LogWarn("RequestInvestigation: the CP's phase can't be read, it stays as it is");
            return;
        }
        if (phase >= caution)
            return;
        g_lua_pushnumber(L, static_cast<double>((TppGameObjectType::kCommandPost << 9) | cp));
        g_lua_createtable(L, 0, 2);
        SetTableString(L, "id", "SetPhase");
        g_lua_pushstring(L, const_cast<char*>("phase"));
        g_lua_pushnumber(L, caution);
        g_lua_settable(L, -3);
        g_OrigSendCommand(L);
        g_lua_settop(L, 0);
    }

    static float SmartScaleA(float a)
    {
        return (a > 1.0f) ? (a * (1.0f / 255.0f)) : a;
    }

    static void SmartScaleRgb(float* r, float* g, float* b)
    {
        if (*r > 1.0f || *g > 1.0f || *b > 1.0f)
        {
            *r *= (1.0f / 255.0f);
            *g *= (1.0f / 255.0f);
            *b *= (1.0f / 255.0f);
        }
    }

    static void ReadColor(lua_State* L, int cmdStackIdx, float* r, float* g, float* b, float* a, float defaultA)
    {
        *r = 0.0f; *g = 0.0f; *b = 0.0f; *a = defaultA;
        g_lua_pushstring(L, const_cast<char*>("color"));
        g_lua_gettable(L, cmdStackIdx);
        if (g_lua_type(L, -1) == LUA_TTABLE)
        {
            const int t = g_lua_gettop(L);
            g_lua_pushstring(L, const_cast<char*>("r")); g_lua_gettable(L, t); if (g_lua_type(L, -1) == LUA_TNUMBER) *r = static_cast<float>(g_lua_tonumber(L, -1)); g_lua_settop(L, t);
            g_lua_pushstring(L, const_cast<char*>("g")); g_lua_gettable(L, t); if (g_lua_type(L, -1) == LUA_TNUMBER) *g = static_cast<float>(g_lua_tonumber(L, -1)); g_lua_settop(L, t);
            g_lua_pushstring(L, const_cast<char*>("b")); g_lua_gettable(L, t); if (g_lua_type(L, -1) == LUA_TNUMBER) *b = static_cast<float>(g_lua_tonumber(L, -1)); g_lua_settop(L, t);
            g_lua_pushstring(L, const_cast<char*>("a")); g_lua_gettable(L, t); if (g_lua_type(L, -1) == LUA_TNUMBER) *a = static_cast<float>(g_lua_tonumber(L, -1)); g_lua_settop(L, t);
        }
        SmartScaleRgb(r, g, b);
        *a = SmartScaleA(*a);
    }

    static int ReadHeadMarkState(lua_State* L, int cmdStackIdx)

    {
        g_lua_pushstring(L, const_cast<char*>("state"));
        g_lua_gettable(L, cmdStackIdx);

        const int t = g_lua_type(L, -1);

        if (t == LUA_TNIL) return -1;

        if (t == LUA_TNUMBER)
        {
            const int n = static_cast<int>(g_lua_tonumber(L, -1));
            return (n >= 0 && n <= 4) ? n : -2;
        }


        if (t != LUA_TSTRING) return -2;

        const char* v = g_lua_tolstring(L, -1, nullptr);
        if (!v || !v[0])                        return -2;
        if (!_stricmp(v, "neutral"))            return 0;
        if (!_stricmp(v, "none"))               return 0;
        if (!_stricmp(v, "enemy"))              return 1;
        if (!_stricmp(v, "friendly"))           return 2;
        if (!_stricmp(v, "friend"))             return 2;
        if (!_stricmp(v, "powerless"))          return 3;
        if (!_stricmp(v, "neutralized"))        return 3;
        if (!_stricmp(v, "dying"))              return 4;
        return -2;
    }

    static bool ReadHeadMarkStop(lua_State* L, int cmdStackIdx, const char* key,
                                 HeadMarkColourStop& out)
    {
        g_lua_pushstring(L, const_cast<char*>(key));
        g_lua_gettable(L, cmdStackIdx);

        const int t = g_lua_type(L, -1);

        if (t == LUA_TNUMBER)
        {
            out.isRgb     = false;
            out.paletteId = static_cast<std::uint32_t>(
                static_cast<long long>(g_lua_tonumber(L, -1)));
            return true;
        }

        if (t == LUA_TSTRING)
        {
            const char* v = g_lua_tolstring(L, -1, nullptr);
            if (!v || !v[0])
                return false;
            out.isRgb     = false;
            out.paletteId = ::ResolveHeadMarkColourId(v);
            return out.paletteId != 0u;
        }

        if (t == LUA_TTABLE)
        {
            const int tbl = g_lua_gettop(L);
            float r = 0.0f, g = 0.0f, b = 0.0f;
            g_lua_pushstring(L, const_cast<char*>("r")); g_lua_gettable(L, tbl);
            if (g_lua_type(L, -1) == LUA_TNUMBER) r = static_cast<float>(g_lua_tonumber(L, -1));
            g_lua_settop(L, tbl);
            g_lua_pushstring(L, const_cast<char*>("g")); g_lua_gettable(L, tbl);
            if (g_lua_type(L, -1) == LUA_TNUMBER) g = static_cast<float>(g_lua_tonumber(L, -1));
            g_lua_settop(L, tbl);
            g_lua_pushstring(L, const_cast<char*>("b")); g_lua_gettable(L, tbl);
            if (g_lua_type(L, -1) == LUA_TNUMBER) b = static_cast<float>(g_lua_tonumber(L, -1));
            g_lua_settop(L, tbl);

            SmartScaleRgb(&r, &g, &b);
            out.isRgb = true;
            out.r = r;
            out.g = g;
            out.b = b;
            return true;
        }

        return false;
    }

    static unsigned HeadMarkStopNumber(const char* key)   // color = 1, color2 = 2, ...; colour spelled either way; 0 = another field
    {
        const char* digits = nullptr;
        if (std::strncmp(key, "colour", 6) == 0)
            digits = key + 6;
        else if (std::strncmp(key, "color", 5) == 0)
            digits = key + 5;
        else
            return 0;
        if (*digits == '\0')
            return 1;
        unsigned number = 0;
        for (int length = 0; *digits; ++digits, ++length)
        {
            if (*digits < '0' || *digits > '9' || length == 9)
                return 0;
            number = number * 10 + static_cast<unsigned>(*digits - '0');
        }
        return number >= 2 ? number : 0;
    }

    static std::vector<HeadMarkColourStop> ReadHeadMarkStops(lua_State* L, int cmdStackIdx)
    {
        std::vector<std::pair<unsigned, std::string>> keys;
        g_lua_pushnil(L);
        while (g_lua_next(L, cmdStackIdx) != 0)
        {
            if (g_lua_type(L, -2) == LUA_TSTRING)
            {
                const char*    key    = g_lua_tolstring(L, -2, nullptr);
                const unsigned number = key ? HeadMarkStopNumber(key) : 0;
                if (number)
                    keys.emplace_back(number, key);
            }
            g_lua_settop(L, -2);
        }
        std::sort(keys.begin(), keys.end(), [](const auto& a, const auto& b)
        {
            return a.first != b.first ? a.first < b.first : a.second.size() < b.second.size();   // "color2" before "colour2"
        });

        std::vector<HeadMarkColourStop> stops;
        unsigned taken = 0;
        for (const auto& key : keys)
        {
            HeadMarkColourStop stop{};
            if (key.first != taken && ReadHeadMarkStop(L, cmdStackIdx, key.second.c_str(), stop))
            {
                stops.push_back(stop);
                taken = key.first;
            }
        }
        return stops;
    }

    static bool ReadHeadMarkBlend(lua_State* L, int cmdStackIdx)
    {
        g_lua_pushstring(L, const_cast<char*>("blend"));
        g_lua_gettable(L, cmdStackIdx);
        const int t = g_lua_type(L, -1);
        if (t == LUA_TBOOLEAN)
            return g_lua_toboolean(L, -1) != 0;
        if (t == LUA_TNUMBER)
            return static_cast<int>(g_lua_tonumber(L, -1)) != 0;
        return true;
    }

    constexpr std::uint16_t kVehicle2Type   = 0x32;
    constexpr std::uint32_t kNullId       = 0xFFFF;
    constexpr std::uint32_t kWholeTypeIndex = 0x1FF;   // the index of a { type = ... } target without one: every object of the type

    static bool CommandHasField(lua_State* L, int cmdStackIdx, const char* key)
    {
        const int top = g_lua_gettop(L);
        g_lua_pushstring(L, const_cast<char*>(key));
        g_lua_gettable(L, cmdStackIdx);
        const bool has = g_lua_type(L, -1) != LUA_TNIL;
        g_lua_settop(L, top);
        return has;
    }

    static bool CommandHasNumber(lua_State* L, int cmdStackIdx, const char* key)
    {
        const int top = g_lua_gettop(L);
        g_lua_pushstring(L, const_cast<char*>(key));
        g_lua_gettable(L, cmdStackIdx);
        const bool has = g_lua_type(L, -1) == LUA_TNUMBER;
        g_lua_settop(L, top);
        return has;
    }

    static bool ReadTypedTargetIndex(lua_State* L, const char* typeName, std::uint16_t typeId, std::uint32_t& index)   // a gameObjectId or { type = typeName, index = N }
    {
        const int top = g_lua_gettop(L);
        bool ok = false;
        if (g_lua_type(L, 1) == LUA_TNUMBER)
        {
            const std::uint32_t id = ReadCommandTargetId(L);
            ok = (id >> 9) == typeId;
            index = id & 0x1FFu;
        }
        else if (g_lua_type(L, 1) == LUA_TTABLE)
        {
            g_lua_pushstring(L, const_cast<char*>("type"));
            g_lua_gettable(L, 1);
            const char* type = g_lua_type(L, -1) == LUA_TSTRING ? g_lua_tolstring(L, -1, nullptr) : nullptr;
            const bool typeOk = type && std::strcmp(type, typeName) == 0;
            g_lua_settop(L, top);
            g_lua_pushstring(L, const_cast<char*>("index"));
            g_lua_gettable(L, 1);
            if (typeOk && g_lua_type(L, -1) == LUA_TNUMBER)
            {
                const double value = static_cast<double>(g_lua_tonumber(L, -1));
                ok = value >= 0.0 && value < 512.0;
                index = ok ? static_cast<std::uint32_t>(value) : 0;
            }
        }
        g_lua_settop(L, top);
        return ok;
    }

    static bool ReadSoldierTarget(lua_State* L, std::uint32_t& soldier, bool& everySoldier)   // true = one soldier: an id or { type = "TppSoldier2", index = N }; everySoldier = { type = "TppSoldier2" }
    {
        const bool isSoldier = ReadTypedTargetIndex(L, "TppSoldier2", TppGameObjectType::kSoldier2, soldier);
        everySoldier = IsWholeTypeTarget(L, "TppSoldier2") || (isSoldier && soldier == kWholeTypeIndex);
        return isSoldier && !everySoldier;
    }

    static bool ReadTargetIdOfAnyType(lua_State* L, std::uint32_t& id)   // a gameObjectId, { type = T, index = N }, or { type = T } = the id that stands for every object of T
    {
        if (g_lua_type(L, 1) == LUA_TNUMBER)
        {
            id = ReadCommandTargetId(L);
            return true;
        }
        if (g_lua_type(L, 1) != LUA_TTABLE)
            return false;

        const int top = g_lua_gettop(L);
        g_lua_getfield(L, 1, const_cast<char*>("type"));
        const char* type = g_lua_type(L, -1) == LUA_TSTRING ? g_lua_tolstring(L, -1, nullptr) : nullptr;
        g_lua_getfield(L, 1, const_cast<char*>("index"));
        std::uint32_t index = kWholeTypeIndex;
        bool indexOk = g_lua_type(L, -1) == LUA_TNIL;
        if (g_lua_type(L, -1) == LUA_TNUMBER)
        {
            const double value = static_cast<double>(g_lua_tonumber(L, -1));
            indexOk = value >= 0.0 && value < 512.0;
            index = indexOk ? static_cast<std::uint32_t>(value) : 0;
        }
        bool ok = type && indexOk && GetGameObjectIdWithIndex(type, index, id);
        if (!ok && type && indexOk && index == kWholeTypeIndex && GetGameObjectIdWithIndex(type, 0, id))
        {
            id |= kWholeTypeIndex;
            ok = true;
        }
        g_lua_settop(L, top);
        return ok;
    }

    static bool ReadHeliTarget(lua_State* L, std::uint32_t& heliKey)   // a gameObjectId or { type = "TppHeli2"/"TppEnemyHeli"/..., index = N }
    {
        const int top = g_lua_gettop(L);
        std::uint32_t id = kNullId;
        if (g_lua_type(L, 1) == LUA_TNUMBER)
        {
            id = ReadCommandTargetId(L);
        }
        else if (g_lua_type(L, 1) == LUA_TTABLE && g_lua_pcall)
        {
            g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("GameObject"));
            const int gameObject = g_lua_type(L, -1);
            if (gameObject == LUA_TTABLE || gameObject == LUA_TUSERDATA)
            {
                g_lua_getfield(L, -1, const_cast<char*>("GetGameObjectIdByIndex"));
                g_lua_getfield(L, 1, const_cast<char*>("type"));
                g_lua_getfield(L, 1, const_cast<char*>("index"));
                const bool argsOk = g_lua_type(L, -2) == LUA_TSTRING && g_lua_type(L, -1) == LUA_TNUMBER;
                if (argsOk && g_lua_pcall(L, 2, 1, 0) == 0 && g_lua_type(L, -1) == LUA_TNUMBER)
                    id = static_cast<std::uint32_t>(static_cast<long long>(g_lua_tonumber(L, -1)));
            }
        }
        g_lua_settop(L, top);
        if (id >= kNullId || !HeliKey::IsHeliType(id >> 9))
            return false;
        heliKey = HeliKey::OfGameObjectId(id);
        return true;
    }

    static bool ReadCommandGameObject(lua_State* L, int cmdStackIdx, const char* key, std::uint32_t& id, std::string& name)   // a gameObjectId or a locator name
    {
        static const char* const kNamedTypes[] = {
            "TppSoldier2", "TppHostage2", "TppHostageUnique", "TppHostageUnique2", "TppHostageKaz",
            "TppHeli2", "TppEnemyHeli", "TppVehicle2", "TppCommonWalkerGear2",
        };

        const int top = g_lua_gettop(L);
        bool ok = false;
        name.clear();
        g_lua_pushstring(L, const_cast<char*>(key));
        g_lua_gettable(L, cmdStackIdx);
        const int value = g_lua_gettop(L);
        const int t = g_lua_type(L, value);
        if (t == LUA_TNUMBER)
        {
            id = static_cast<std::uint32_t>(static_cast<long long>(g_lua_tonumber(L, value)));
            ok = id < kNullId;
        }
        else if (t == LUA_TSTRING)
        {
            const char* s = g_lua_tolstring(L, value, nullptr);
            if (s)
                name = s;
            for (const char* typeName : kNamedTypes)
            {
                if (!ok && !name.empty() && GetGameObjectIdByName(typeName, name.c_str(), id))
                    ok = id < kNullId;
            }
        }
        if (!ok && t == LUA_TSTRING && g_lua_pcall && g_lua_pushvalue)
        {
            g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("GameObject"));
            const int gameObject = g_lua_type(L, -1);
            if (gameObject == LUA_TTABLE || gameObject == LUA_TUSERDATA)
            {
                g_lua_getfield(L, -1, const_cast<char*>("GetGameObjectId"));
                g_lua_pushvalue(L, value);
                if (g_lua_pcall(L, 1, 1, 0) == 0 && g_lua_type(L, -1) == LUA_TNUMBER)
                {
                    id = static_cast<std::uint32_t>(static_cast<long long>(g_lua_tonumber(L, -1)));
                    ok = id < kNullId;
                }
            }
        }
        g_lua_settop(L, top);
        return ok;
    }

    static bool CpPhasePerCpTarget(lua_State* L)
    {
        const int top = g_lua_gettop(L);
        bool isPerCp = false;
        const int a1 = g_lua_type(L, 1);
        if (a1 == LUA_TNUMBER)
        {
            isPerCp = true;
        }
        else if (a1 == LUA_TTABLE)
        {
            g_lua_pushstring(L, const_cast<char*>("index"));
            g_lua_gettable(L, 1);
            isPerCp = (g_lua_type(L, -1) != LUA_TNIL);
        }
        g_lua_settop(L, top);
        return isPerCp;
    }

    static bool PhaseTimerOfCommand(const std::string& id, const char* cautionId, const char* evasionId,
                                    const char* alertId, CpPhaseTimer* phase)
    {
        if (id == cautionId)
            *phase = CpPhaseTimer::Caution;
        else if (id == evasionId)
            *phase = CpPhaseTimer::Evasion;
        else if (id == alertId)
            *phase = CpPhaseTimer::Alert;
        else
            return false;
        return true;
    }

    static int __fastcall hk_SendCommand(lua_State* L)
    {
        if (!g_OrigSendCommand) return 0;

        if (MissionCodeGuard::ShouldBypassHooks())
        {
            DeployGuard::OnMissionCode(MissionCodeGuard::GetCurrentMissionCode());
            QuietCqc_EnforceMissionGuard();   // FOB: revert the byte patches that stay live from single player
            UniqueCharacterSuitSlot_EnforceMissionGuard();
            UavEmp_EnforceMissionGuard();
            EquipParam_EnforceFobVanilla();
        }
        MISSION_GUARD_ORIGINAL_RET(g_OrigSendCommand, L);

        MissionStateReset::PollMissionChange();
        outfit::PollStaffFaceHeal(MissionCodeGuard::GetCurrentMissionCode());

        if (!ResolveLuaApi()) return g_OrigSendCommand(L);

        MissionStateReset::EnsureFinalizerRegistered(L);
        RankLimitedItems::EnsureResultHookInstalled(L);
        MotherBaseSalutes::EnsureMissionRankHookInstalled(L);
        PilotVoice_EnsureBoardingRadioHookInstalled(L);

        const int top = g_lua_gettop(L);
        if (top < 2 || g_lua_type(L, 2) != LUA_TTABLE)
            return g_OrigSendCommand(L);

        std::string idStr;
        const char* id = ReadCommandId(L, 2, &idStr);
        if (!id || !*id)
        {
            g_lua_settop(L, top);
            return g_OrigSendCommand(L);
        }
        g_lua_settop(L, top);

        if (idStr == "SetSahelanPhase")
        {
            const std::int32_t phase =
                static_cast<std::int32_t>(ReadCommandNumber(L, 2, "phase"));
            g_lua_settop(L, top);
            ::Set_SahelanForcePhase(phase);
            return 0;
        }
        if (idStr == "GetSahelanPhase")
        {
            const double phase = static_cast<double>(::Get_SahelanCurrentPhase());
            g_lua_pushnumber(L, phase);
            return 1;
        }
        if (idStr == "SetOccasionalChatList")
        {
            std::uint32_t labels[256];
            const std::size_t n = ReadLabelArray(L, 2, labels, "labels");
            g_lua_settop(L, top);
            ::SetOccasionalChatList(labels, n);
            return 0;
        }
        if (idStr == "InsertToOccasionalChatList")
        {
            std::uint32_t labels[256];
            const std::size_t n = ReadLabelArray(L, 2, labels, "labels");
            g_lua_settop(L, top);
            ::InsertToOccasionalChatList(labels, n);
            return 0;
        }
        if (idStr == "RemoveFromOccasionalChatList")
        {
            std::uint32_t labels[256];
            const std::size_t n = ReadLabelArray(L, 2, labels, "labels");
            g_lua_settop(L, top);
            ::RemoveFromOccasionalChatList(labels, n);
            return 0;
        }

        CpPhaseTimer phaseTimer = CpPhaseTimer::Caution;
        if (PhaseTimerOfCommand(idStr, "SetCautionPhaseDuration", "SetEvasionPhaseDuration", "SetAlertPhaseDuration", &phaseTimer))
        {
            const float duration = static_cast<float>(ReadCommandNumber(L, 2, "duration"));
            g_lua_settop(L, top);

            if (CpPhasePerCpTarget(L))
            {
                ::Arm_CpIndexCapture();
                const int r = g_OrigSendCommand(L);
                ::Set_CpPhaseDurationSecondsForCp(phaseTimer, ::Take_CapturedCpIndex(), duration);
                return r;
            }

            ::Set_CpPhaseDurationSeconds(phaseTimer, duration);
            return 0;
        }
        if (PhaseTimerOfCommand(idStr, "GetCautionPhaseDuration", "GetEvasionPhaseDuration", "GetAlertPhaseDuration", &phaseTimer))
        {
            const bool perCp = CpPhasePerCpTarget(L);
            ::Arm_CpIndexCapture();
            g_OrigSendCommand(L);
            const std::uint32_t cp = ::Take_CapturedCpIndex();
            g_lua_settop(L, top);

            const float seconds = perCp ? ::Get_CpPhaseDurationSecondsForCp(phaseTimer, cp)
                                        : ::Get_CpPhaseDurationSeconds(phaseTimer);
            g_lua_pushnumber(L, static_cast<double>(seconds));
            return 1;
        }
        if (PhaseTimerOfCommand(idStr, "UnsetCautionPhaseDuration", "UnsetEvasionPhaseDuration", "UnsetAlertPhaseDuration", &phaseTimer))
        {
            if (CpPhasePerCpTarget(L))
            {
                ::Arm_CpIndexCapture();
                g_OrigSendCommand(L);
                ::Unset_CpPhaseDurationSecondsForCp(phaseTimer, ::Take_CapturedCpIndex());
                g_lua_settop(L, top);
                return 0;
            }
            g_lua_settop(L, top);
            ::Unset_CpPhaseDurationSeconds(phaseTimer);
            return 0;
        }
        if (PhaseTimerOfCommand(idStr, "GetCautionPhaseRemaining", "GetEvasionPhaseRemaining", "GetAlertPhaseRemaining", &phaseTimer))
        {
            if (CpPhasePerCpTarget(L))
            {
                ::Arm_CpIndexCapture();
                g_OrigSendCommand(L);
                const std::uint32_t cp = ::Take_CapturedCpIndex();
                g_lua_settop(L, top);
                g_lua_pushnumber(L, static_cast<double>(::Get_CpPhaseRemainingSecondsForCp(phaseTimer, cp)));
                return 1;
            }
            g_lua_pushnumber(L, -1.0);
            return 1;
        }

        if (idStr == "SetRestrictNotice")
        {
            std::uint32_t soldier = 0;
            bool everySoldier = false;
            const bool oneSoldier = ReadSoldierTarget(L, soldier, everySoldier);

            std::uint8_t mask = 0;
            if (ReadCommandBool(L, 2, "enabled"))
            {
                if (ReadCommandBool(L, 2, "ignorePlayer"))
                    mask |= SoldierNoticeIgnore::kPlayer;
            }
            g_lua_settop(L, top);

            const int r = g_OrigSendCommand(L);

            if (everySoldier)
                ::Set_EverySoldierNoticeIgnoreMask(mask);
            else if (oneSoldier)
                ::Set_SoldierNoticeIgnoreMask(soldier, mask);
            return r;
        }
        if (idStr == "SetIgnoreVehicle")
        {
            std::uint32_t soldier = 0;
            bool everySoldier = false;
            const bool oneSoldier = ReadSoldierTarget(L, soldier, everySoldier);

            bool enabled = true;
            g_lua_pushstring(L, const_cast<char*>("enabled"));
            g_lua_gettable(L, 2);
            const int enabledType = g_lua_type(L, -1);
            if (enabledType == LUA_TBOOLEAN)
                enabled = g_lua_toboolean(L, -1) != 0;
            else if (enabledType == LUA_TNUMBER)
                enabled = static_cast<int>(g_lua_tonumber(L, -1)) != 0;
            g_lua_settop(L, top);

            if (everySoldier)
                ::Set_EverySoldierIgnoreVehicle(enabled);
            else if (oneSoldier)
                ::Set_SoldierIgnoreVehicle(soldier, enabled);
            else
                LogWarn("SetIgnoreVehicle: target is not a TppSoldier2 (an id, { type = \"TppSoldier2\", index = N }, or { type = \"TppSoldier2\" } for every soldier), ignored");
            return 0;
        }
        if (idStr == "AddNotice")
        {
            const bool          toAll = IsWholeTypeTarget(L, "TppSoldier2");
            const std::uint32_t id    = ReadCommandTargetId(L);
            const int           kind  = static_cast<int>(ReadCommandNumberOr(L, 2, "kind", -1.0));
            g_lua_settop(L, top);
            const float         range = static_cast<float>(ReadCommandNumberOr(L, 2, "range", 0.0));
            g_lua_settop(L, top);

            g_lua_pushstring(L, const_cast<char*>("targetId"));
            g_lua_gettable(L, 2);
            const bool hasTarget = g_lua_type(L, -1) == LUA_TNUMBER;
            const std::uint16_t targetId = hasTarget
                ? static_cast<std::uint16_t>(static_cast<long long>(g_lua_tonumber(L, -1)) & 0xFFFF)
                : SoldierNotice::kNoTarget;
            g_lua_settop(L, top);

            float position[3] = {};
            const bool hasPosition = ReadCommandPosition(L, 2, "pos", position);
            g_lua_settop(L, top);

            const char* problem = SoldierNotice::Problem(kind, targetId, hasPosition);
            if (toAll)
            {
                if (problem)
                    LogWarn("AddNotice: kind 0x%02X %s, ignored", kind & 0xFF, problem);
                else if (!hasTarget && !hasPosition)
                    LogWarn("AddNotice: sent to every soldier, it needs pos or targetId to know who is near; ignored");
                else if (!SoldierNotice::AddToAll(kind, targetId, hasPosition ? position : nullptr, range))
                    LogWarn("AddNotice: too many notices for every soldier at once, ignored");
                return 0;
            }
            if ((id >> 9) != TppGameObjectType::kSoldier2)
                LogWarn("AddNotice: target 0x%08X is not a soldier, ignored", id);
            else if (problem)
                LogWarn("AddNotice: kind 0x%02X %s, ignored", kind & 0xFF, problem);
            else if (!SoldierNotice::Add(id & 0x1FFu, kind, targetId, hasPosition ? position : nullptr))
                LogWarn("AddNotice: soldier 0x%08X can't take notices, ignored", id);
            return 0;
        }
        if (idStr == "LieToRadio")
        {
            const std::uint32_t id = ReadCommandTargetId(L);
            CpRadioResponse::MemberRadio radio;
            radio.label = ReadCommandStrCode32(L, 2, "label");
            radio.reply = ReadCommandStrCode32(L, 2, "reply");
            g_lua_settop(L, top);

            bool queued = false;
            if ((id >> 9) != TppGameObjectType::kSoldier2)
                LogWarn("LieToRadio: target 0x%08X is not a soldier, ignored", id);
            else if (radio.label == 0)
                LogWarn("LieToRadio: needs a label, ignored");
            else
            {
                queued = CpRadioResponse::RequestMemberRadio(static_cast<std::uint16_t>(id), radio);
                if (!queued)
                    LogWarn("LieToRadio: soldier 0x%08X can't radio now, ignored", id);
            }
            g_lua_pushboolean(L, queued ? 1 : 0);
            return 1;
        }
        if (idStr == "AssignRadioInCombat")
        {
            std::uint32_t cp = 0;
            const bool isCp = ReadCommandCpIndex(L, cp);
            const std::uint32_t id = ReadCommandTargetId(L);
            const std::uint32_t label = ReadCommandStrCode32(L, 2, "label");
            g_lua_settop(L, top);
            const bool noPose = ReadCommandBool(L, 2, "noPose");
            g_lua_settop(L, top);

            bool queued = false;
            if (isCp)
                queued = CpRadioVariants_AssignRadioInCombat(cp, 0xFFFFFFFFu, label, noPose);
            else if ((id >> 9) == TppGameObjectType::kSoldier2)
                queued = CpRadioVariants_AssignRadioInCombat(0xFFFFFFFFu, id & 0x1FFu, label, noPose);
            else
                LogWarn("AssignRadioInCombat: target is neither one command post nor a soldier, ignored");
            g_lua_pushboolean(L, queued ? 1 : 0);
            return 1;
        }
        if (idStr == "RequestInvestigation")
        {
            std::uint32_t cp = 0;
            const bool isCp = ReadCommandCpIndex(L, cp);
            CpInvestigationRequest request;
            const bool hasSpot = ReadCommandPoint(L, 2, "pos", request.spot);
            g_lua_settop(L, top);
            const double memberCount = ReadCommandNumberOr(L, 2, "memberCount", 0.0);
            request.endLabel = ReadCommandStrCode32(L, 2, "endLabel");
            request.silent = ReadCommandBool(L, 2, "silent");
            request.takeOver = ReadCommandBoolOr(L, 2, "takeOver", true);
            const bool noCaution = ReadCommandBool(L, 2, "noCaution");
            g_lua_settop(L, top);
            request.memberCount = memberCount >= 1.0 ? static_cast<std::uint32_t>(memberCount > 1000.0 ? 1000.0 : memberCount) : 0;
            bool soldiersGiven = false;
            request.soldierCount = ReadCommandSoldierIndices(L, 2, "soldiers", request.soldiers, kCpInvestigationMost, soldiersGiven);
            g_lua_settop(L, top);

            std::uint32_t picked[kCpInvestigationMost] = {};
            std::uint32_t count = 0;
            if (!isCp)
                LogWarn("RequestInvestigation: target is not one command post, ignored");
            else if (!hasSpot)
                LogWarn("RequestInvestigation: needs pos ({x,y,z} or Vector3), ignored");
            else if (soldiersGiven && request.soldierCount == 0)
                LogWarn("RequestInvestigation: soldiers has no soldier ids, ignored");
            else
                count = CpInvestigation_Request(cp, request, picked);
            if (count && !noCaution)
                RaiseCpToCaution(L, cp);
            g_lua_createtable(L, static_cast<int>(count), 0);
            for (std::uint32_t i = 0; i < count; ++i)
            {
                g_lua_pushnumber(L, static_cast<double>(i + 1));
                g_lua_pushnumber(L, static_cast<double>((TppGameObjectType::kSoldier2 << 9) | picked[i]));
                g_lua_settable(L, -3);
            }
            return 1;
        }
        if (idStr == "SetInvestigationMemberCount")
        {
            std::uint32_t cp = 0;
            const bool isCp = ReadCommandCpIndex(L, cp);
            const bool everyCp = IsWholeTypeTarget(L, "TppCommandPost2")
                || ReadCommandTargetId(L) == ((static_cast<std::uint32_t>(TppGameObjectType::kCommandPost) << 9) | kWholeTypeIndex);
            g_lua_pushstring(L, const_cast<char*>("memberCount"));
            g_lua_gettable(L, 2);
            const int memberCountType = g_lua_type(L, -1);
            g_lua_settop(L, top);
            const double memberCount = ReadCommandNumberOr(L, 2, "memberCount", -1.0);
            g_lua_settop(L, top);
            const double capped = memberCount > 1000.0 ? 1000.0 : memberCount;
            if (!isCp && !everyCp)
                LogWarn("SetInvestigationMemberCount: target is not a command post (an id, { type = \"TppCommandPost2\", index = N }, or { type = \"TppCommandPost2\" } for every command post), ignored");
            else if (memberCount < 0.0)
                LogWarn("SetInvestigationMemberCount: memberCount is %s, needs a number 0 or more (0 = back to 4), ignored",
                    memberCountType == LUA_TNIL ? "missing" : memberCountType == LUA_TNUMBER ? "below 0"
                    : memberCountType == LUA_TSTRING ? "a string" : "not a number");
            else if (everyCp)
                CpInvestigation_SetEveryMemberCount(static_cast<std::uint32_t>(capped));
            else
                CpInvestigation_SetMemberCount(cp, static_cast<std::uint32_t>(capped));
            return 0;
        }
        if (idStr == "SetHoldupSelfRelease")
        {
            const bool toAll = IsWholeTypeTarget(L, "TppSoldier2");
            const std::uint32_t id = ReadCommandTargetId(L);
            g_lua_pushstring(L, const_cast<char*>("enabled"));
            g_lua_gettable(L, 2);
            const int enabledType = g_lua_type(L, -1);
            g_lua_settop(L, top);
            const bool enabled = ReadCommandBool(L, 2, "enabled");
            g_lua_settop(L, top);
            if (enabledType != LUA_TBOOLEAN && enabledType != LUA_TNUMBER)
                LogWarn("SetHoldupSelfRelease: needs enabled (true or false), ignored");
            else if (toAll)
                ::HoldupSelfRelease_SetEnabled(enabled);
            else if ((id >> 9) == TppGameObjectType::kSoldier2)
                ::HoldupSelfRelease_SetEnabledFor(id & 0x1FFu, enabled);
            else
                LogWarn("SetHoldupSelfRelease: target 0x%08X is not a soldier, ignored", id);
            return 0;
        }
        if (idStr == "SetFaction")
        {
            std::uint32_t soldier = 0;
            const bool isSoldier = ReadTypedTargetIndex(L, "TppSoldier2", TppGameObjectType::kSoldier2, soldier);

            std::string factionName;
            g_lua_pushstring(L, const_cast<char*>("name"));
            g_lua_gettable(L, 2);
            const int nameType = g_lua_type(L, -1);
            if (nameType == LUA_TSTRING)
            {
                const char* s = g_lua_tolstring(L, -1, nullptr);
                if (s) factionName = s;
            }
            g_lua_settop(L, top);

            std::uint32_t heli = 0;
            const bool isHeli = !isSoldier && ReadHeliTarget(L, heli);
            const bool supportHeli = isHeli && (heli >> 9) == 0;   // HeliKey 0x000-0x1FF = TppHeli2, the player's support heli
            if (!isSoldier && !isHeli)
                LogWarn("SetFaction: target is not one soldier or enemy heli (an id, { type = \"TppSoldier2\", index = N } or { type = \"TppEnemyHeli\", index = N }), ignored");
            else if (supportHeli)
                LogWarn("SetFaction: the support heli can't have a faction, ignored");
            else if (nameType != LUA_TSTRING)
                LogWarn("SetFaction: needs name (a string, \"\" removes the faction), ignored");
            else if (isHeli)
                ::Set_HeliFaction(heli, factionName.c_str());
            else
                ::Set_SoldierFaction((static_cast<std::uint32_t>(TppGameObjectType::kSoldier2) << 9) | soldier, factionName.c_str());
            return 0;
        }
        if (idStr == "GetLife")
        {
            const std::uint32_t id = ReadCommandTargetId(L);
            g_lua_settop(L, top);

            GameObjectLife::Values life;
            if (!GameObjectLife::Read(id, life))
            {
                g_lua_pushnil(L);
                g_lua_pushnil(L);
                g_lua_pushnil(L);
                g_lua_pushnil(L);
                return 4;
            }
            g_lua_pushnumber(L, static_cast<double>(life.life));
            g_lua_pushnumber(L, static_cast<double>(life.maxLife));
            if (life.maxStamina == 0)
            {
                g_lua_pushnil(L);
                g_lua_pushnil(L);
            }
            else
            {
                g_lua_pushnumber(L, static_cast<double>(life.stamina));
                g_lua_pushnumber(L, static_cast<double>(life.maxStamina));
            }
            return 4;
        }
        if (idStr == "SetLife")
        {
            const std::uint32_t id = ReadCommandTargetId(L);
            const double life = ReadCommandNumberOr(L, 2, "life", -1.0);
            const double stamina = ReadCommandNumberOr(L, 2, "stamina", -1.0);
            const bool sleep = ReadCommandBool(L, 2, "sleep");
            g_lua_settop(L, top);

            const auto toValue = [](double v) { return static_cast<std::uint32_t>(v > 65535.0 ? 65535.0 : v); };
            const bool hasLife = life >= 0.0;
            const bool hasStamina = stamina >= 0.0;
            const char* reason = nullptr;
            if (!hasLife && !hasStamina)
                LogWarn("SetLife: needs life or stamina (0 or more), ignored");
            if (hasLife && !GameObjectLife::SetLife(id, toValue(life), &reason))
                LogWarn("SetLife: life of 0x%08X not set, %s", id, reason);
            if (hasStamina && !GameObjectLife::SetStamina(id, toValue(stamina), sleep, &reason))
                LogWarn("SetLife: stamina of 0x%08X not set, %s", id, reason);
            return 0;
        }
        if (idStr == "Reinitialize")
        {
            const bool hasId = g_lua_type(L, 1) == LUA_TNUMBER;
            const std::uint32_t id = ReadCommandTargetId(L) & 0xFFFFu;
            g_lua_settop(L, top);
            if (!hasId)
            {
                LogWarn("Reinitialize: needs a gameObjectId, ignored");
                g_lua_pushboolean(L, 0);
                return 1;
            }

            using GameObjectReinitialize::Result;
            const bool inMission = GameObjectInstance::Find(id).typeObject != nullptr;
            Result result = Result::NotReached;
            if (inMission)
            {
                GameObjectReinitialize::Arm();
                g_OrigSendCommand(L);
                result = GameObjectReinitialize::Take();
            }
            g_lua_settop(L, 0);
            if (!inMission)
                LogWarn("Reinitialize: 0x%04X is not a game object in this mission, ignored", id);
            else if (result == Result::PastInstanceCount)
                LogWarn("Reinitialize: 0x%04X is past the last instance of its type, ignored", id);
            else if (result == Result::NotSpawned)
                LogWarn("Reinitialize: vehicle 0x%04X is not spawned, ignored", id);
            else if (result == Result::NotAlive)
                LogWarn("Reinitialize: 0x%04X is dead, dying, fultoned, carried or destroyed, ignored", id);
            else if (result == Result::Fault)
                LogWarn("Reinitialize: 0x%04X could not be fully reset", id);
            else if (result == Result::NotReached)
            {
                static bool s_warnedType[128] = {};
                const std::uint32_t type = (id >> 9) & 0x7Fu;
                if (!s_warnedType[type])
                {
                    s_warnedType[type] = true;
                    LogWarn("Reinitialize: game object type %u can't be reinitialized (yet: cameras, UAVs, vehicles, helis, soldiers, hostages, walker gears and Sahelanthropus can; helis need the SupportHeliWeapon module), ignored", type);
                }
            }
            g_lua_pushboolean(L, result == Result::Done ? 1 : 0);
            return 1;
        }
        if (idStr == "GetPosition")
        {
            const std::uint32_t id = ReadCommandTargetId(L);
            if (!AnimalInstance::IsAnimal(id))
                return g_OrigSendCommand(L);

            float position[3] = {};
            if (!GameObjectPosition::Read(id, position))
                return g_OrigSendCommand(L);
            if (AnimalInstance::Check(id, position) == AnimalInstance::Presence::Gone)
            {
                g_lua_pushnil(L);
                return 1;
            }
            if (PushVector3(L, position))
                return 1;

            static bool s_logged = false;
            if (!s_logged)
            {
                s_logged = true;
                LogWarn("GetPosition: Vector3 could not be made, animal positions return nothing");
            }
            return g_OrigSendCommand(L);
        }
        if (idStr == "AddDamage")
        {
            const std::uint32_t id = ReadCommandTargetId(L);
            if (!AnimalInstance::IsAnimal(id))
                return g_OrigSendCommand(L);

            const double attackId = ReadCommandNumberOr(L, 2, "attackId", 0.0);
            const double attackerId = ReadCommandNumberOr(L, 2, "ownerGameObjectId", 65535.0);
            AnimalInstance::DamageRequest request;
            request.attackId = static_cast<std::uint16_t>(attackId >= 0.0 && attackId <= 65535.0 ? attackId : 0.0);
            request.attackerId = static_cast<std::uint16_t>(attackerId >= 0.0 && attackerId <= 65535.0 ? attackerId : 65535.0);
            request.oneHitKill = ReadCommandBool(L, 2, "isOneHitKill");
            request.headShot = ReadCommandBool(L, 2, "isHeadShot");
            g_lua_settop(L, top);

            const char* reason = nullptr;
            if (!AnimalInstance::AddDamage(id, request, &reason))
                LogWarn("AddDamage: animal 0x%04X not damaged, %s", id, reason);
            return 0;
        }
        if (idStr == "SetHeadMarkColor")
        {
            std::uint32_t       id    = 0;
            const bool          hasId = ReadTargetIdOfAnyType(L, id);
            const int           state = ReadHeadMarkState(L, 2);

            const std::vector<HeadMarkColourStop> stops = ReadHeadMarkStops(L, 2);
            const unsigned     count = static_cast<unsigned>(stops.size());
            const float        speed = static_cast<float>(ReadCommandNumberOr(L, 2, "speed", 1.0));
            const bool         blend = ReadHeadMarkBlend(L, 2);
            const float        fade  = static_cast<float>(ReadCommandNumberOr(L, 2, "fade", 0.0));
            g_lua_settop(L, top);

            if (!hasId)
            {
                LogWarn("SetHeadMarkColor: target is not a game object of this mission (an id, { type = T, index = N }, or { type = T } for every object of T), ignored");
                return 0;
            }
            if (state == -2)
            {
                LogWarn("SetHeadMarkColor: bad state for 0x%X, marker keeps vanilla colour", id);
                return 0;
            }

            ::SetHeadMarkEntityColour(id, state, stops.data(), count, speed, blend, fade);
            return 0;
        }
        if (idStr == "ShowStaffName")
        {
            std::uint32_t soldier = 0;
            bool everySoldier = false;
            const bool oneSoldier = ReadSoldierTarget(L, soldier, everySoldier);
            const bool enable = ReadCommandBool(L, 2, "enable");
            const bool showOnNeutralized = ReadCommandBool(L, 2, "showOnNeutralized");

            std::string langId;
            g_lua_pushstring(L, const_cast<char*>("langId"));
            g_lua_gettable(L, 2);
            const int langIdType = g_lua_type(L, -1);
            if (langIdType == LUA_TSTRING)
            {
                const char* s = g_lua_tolstring(L, -1, nullptr);
                if (s) langId = s;
            }
            else if (langIdType != LUA_TNIL)
                LogWarn("ShowStaffName: langId is not a string, the head-mark keeps the real staff name");
            g_lua_settop(L, top);

            if (everySoldier)
                ::Set_EveryHeadMarkStaffName(enable, showOnNeutralized, langId.c_str());
            else if (oneSoldier)
                ::Set_HeadMarkStaffName(soldier, enable, showOnNeutralized, langId.c_str());
            else
                LogWarn("ShowStaffName: target is not a TppSoldier2 (an id, { type = \"TppSoldier2\", index = N }, or { type = \"TppSoldier2\" } for every soldier), ignored");
            return 0;
        }
        if (idStr == "SetVoicePitch")
        {
            const std::uint32_t id = ReadCommandTargetId(L);
            std::uint32_t soldier = 0;
            bool everySoldier = false;
            const bool oneSoldier = ReadSoldierTarget(L, soldier, everySoldier);
            const float cents = static_cast<float>(ReadCommandNumber(L, 2, "pitch"));
            g_lua_settop(L, top);

            if ((id >> 9) == TppGameObjectType::kCommandPost)
                SoldierAkObjIdMap::SetCommandPostVoiceCents(id & 0x1FFu, cents);
            else if (everySoldier)
                ::Set_EverySoldierVoicePitch(cents);
            else if (oneSoldier)
                ::Set_SoldierVoicePitch((static_cast<std::uint32_t>(TppGameObjectType::kSoldier2) << 9) | soldier, cents);
            else
                LogWarn("SetVoicePitch: target is not a TppSoldier2 (an id, { type = \"TppSoldier2\", index = N }, or { type = \"TppSoldier2\" } for every soldier) or a command post id, ignored");
            return 0;
        }
        if (idStr == "SetVIPImportant")
        {
            const std::uint32_t id = ReadCommandTargetId(L);
            const bool isOfficer = ReadCommandBool(L, 2, "isOfficer");
            const std::uint32_t deadBodyLabel = ReadCommandStrCode32(L, 2, "deadBodyLabel");
            g_lua_settop(L, top);
            if (!::VIPImportant_Set(id, isOfficer, deadBodyLabel))
                LogWarn("SetVIPImportant: 0x%08X is not a soldier, ignored", id);
            return 0;
        }
        if (idStr == "RemoveVIPImportant")
        {
            const std::uint32_t id = ReadCommandTargetId(L);
            g_lua_settop(L, top);
            if (!::VIPImportant_Remove(id))
                LogWarn("RemoveVIPImportant: 0x%08X is not a soldier, ignored", id);
            return 0;
        }
        if (idStr == "SetDeadBodyLabel")
        {
            const std::uint32_t id = ReadCommandTargetId(L);
            const std::uint32_t deadBodyLabel = ReadCommandStrCode32(L, 2, "deadBodyLabel");
            g_lua_settop(L, top);
            if (!::DeadBodyLabel_Set(id, deadBodyLabel))
                LogWarn("SetDeadBodyLabel: 0x%08X is not a soldier, ignored", id);
            return 0;
        }
        if (idStr == "SetRadioCallSign")
        {
            const std::uint32_t id = ReadCommandTargetId(L);
            const int callSign = static_cast<int>(ReadCommandNumber(L, 2, "callSign"));
            g_lua_settop(L, top);
            if (callSign <= 0)
                ::Remove_SoldierCallSign(id);
            else
                ::Set_SoldierCallSign(id, static_cast<std::uint8_t>(callSign > 255 ? 255 : callSign));
            return 0;
        }
        if (idStr == "SetLostHostage")
        {
            const std::uint32_t id = ReadCommandTargetId(L);
            const int hostageType = static_cast<int>(ReadCommandNumber(L, 2, "hostageType"));
            const std::uint32_t customLostLabel = ReadCommandStrCode32(L, 2, "customLostLabel");
            const std::uint32_t customLostLabelTaken =
                ReadCommandStrCode32(L, 2, "customLostLabelTaken");
            g_lua_settop(L, top);
            ::Add_LostHostageTrap(id, hostageType, customLostLabel, customLostLabelTaken);
            ::Add_LostHostageDiscovery(id, hostageType);
            return 0;
        }
        if (idStr == "RemoveLostHostage")
        {
            const std::uint32_t id = ReadCommandTargetId(L);
            g_lua_settop(L, top);
            ::Remove_LostHostageTrap(id);
            ::Remove_LostHostageDiscovery(id);
            return 0;
        }
        if (idStr == "SetOpticalCamo")
        {
            std::uint32_t soldier = 0;
            bool everySoldier = false;
            const bool oneSoldier = ReadSoldierTarget(L, soldier, everySoldier);
            const bool enable = ReadCommandBool(L, 2, "enable");
            const double seconds = ReadCommandNumberOr(L, 2, "time", 0.0);
            g_lua_settop(L, top);
            if (everySoldier)
                ::Set_UpdateOptCamoEnableEverySoldier(enable, seconds);
            else if (oneSoldier)
                ::Set_UpdateOptCamoEnableMappedIndex(soldier, enable, seconds);
            else
                LogWarn("SetOpticalCamo: target is not a TppSoldier2 (an id, { type = \"TppSoldier2\", index = N }, or { type = \"TppSoldier2\" } for every soldier), ignored");
            return 0;
        }
        if (idStr == "SetSahelanFova")
        {
            std::string fv2;
            g_lua_pushstring(L, const_cast<char*>("fv2"));
            g_lua_gettable(L, 2);
            if (g_lua_type(L, -1) == LUA_TSTRING)
            {
                const char* s = g_lua_tolstring(L, -1, nullptr);
                if (s) fv2 = s;
            }
            g_lua_settop(L, top);
            ::Set_SahelanFovaPath(fv2.c_str());
            return 0;
        }
        if (idStr == "SetEyeLampColor")
        {
            float r, g, b, a;
            ReadColor(L, 2, &r, &g, &b, &a, 1.0f);
            const int   mode = static_cast<int>(ReadCommandNumberOr(L, 2, "phase", -1.0));
            g_lua_settop(L, top);
            ::Set_EyeLampColor(mode, r, g, b, a);
            return 0;
        }
        if (idStr == "SetEyeLampDisco")
        {
            const bool  enabled = ReadCommandBool(L, 2, "enabled");
            const float speed   = static_cast<float>(ReadCommandNumber(L, 2, "speed"));
            const float a       = SmartScaleA(static_cast<float>(ReadCommandNumberOr(L, 2, "a", 1.0)));
            g_lua_settop(L, top);
            ::Set_EyeLampDisco(enabled, speed, a);
            return 0;
        }
        if (idStr == "SetHeartLightColor")
        {
            float r, g, b, a;
            ReadColor(L, 2, &r, &g, &b, &a, 1.0f);
            const int mode = static_cast<int>(ReadCommandNumberOr(L, 2, "phase", -1.0));
            g_lua_settop(L, top);
            ::Set_HeartLightColor(mode, r, g, b, a);
            return 0;
        }
        if (idStr == "SetHeartLightDisco")
        {
            const bool  enabled = ReadCommandBool(L, 2, "enabled");
            const float speed   = static_cast<float>(ReadCommandNumber(L, 2, "speed"));
            const float a       = SmartScaleA(static_cast<float>(ReadCommandNumberOr(L, 2, "a", 1.0)));
            g_lua_settop(L, top);
            ::Set_HeartLightDisco(enabled, speed, a);
            return 0;
        }

        if (idStr == "SetFriendlyFire")
        {
            const bool enable = ReadCommandBool(L, 2, "enable");
            g_lua_settop(L, top);
            ::Set_FriendlyFire(enable);
            return 0;
        }
        if (idStr == "GetHostageGender")
        {
            const std::uint32_t id = ReadCommandTargetId(L);
            const int gender = HostageGender::Read(id);
            g_lua_settop(L, top);
            if (gender == HostageGender::kUnknown)
                g_lua_pushnil(L);
            else
                g_lua_pushnumber(L, static_cast<double>(gender));
            return 1;
        }
        if (idStr == "SetForceStruggle")
        {
            const std::uint32_t id = ReadCommandTargetId(L);
            const bool struggle = ReadCommandBool(L, 2, "struggle");
            const bool ever = ReadCommandBool(L, 2, "ever");
            g_lua_settop(L, top);
            if (::Arm_HostageStruggle(id, struggle, ever))
                SendStruggleSpecialAction(L);
            return 0;
        }
        if (idStr == "IsFriendlyFire")
        {
            g_lua_settop(L, top);
            g_lua_pushnumber(L, ::Get_FriendlyFire() ? 1.0 : 0.0);
            return 1;
        }

        if (idStr == "AssignInterrogationWithVoice")
        {
            bool hasEvent = false;
            bool hasMarkerEvent = false;
            const std::uint32_t ev =
                ReadCommandFnvHash(L, 2, "soundDialogueEvent", &hasEvent);
            const std::uint32_t evMarker =
                ReadCommandFnvHash(L, 2, "soundDialogueEventMarker", &hasMarkerEvent);
            std::uint32_t labelBases[16] = {};
            const std::uint32_t labelBaseCount =
                GatherLabelBases(L, 2, labelBases, 16, 0, 0);
            g_lua_settop(L, top);

            ::Arm_CpIndexCapture();
            const int r = g_OrigSendCommand(L);
            const std::uint32_t cp = ::Take_CapturedCpIndex();

            if (hasEvent || hasMarkerEvent)
            {
                if (cp == 0xFFFFFFFFu)
                    LogWarn("AssignInterrogationWithVoice: CP not captured, events 0x%X/0x%X not registered, CP keeps vanilla voice",
                        ev, evMarker);
                else
                {
                    ::Register_InterrogationVoiceEvent(cp, ev, evMarker, labelBases,
                                                       labelBaseCount);
                }
            }
            return r;
        }

        if (idStr == "SetSaluteVoiceList")
        {
            MotherBaseSalutes::AddRestoredSalutes(L, 2);
            g_lua_settop(L, top);
            return g_OrigSendCommand(L);
        }

        if (idStr == "SetConversationList")
        {
            SoldierConversations::AddRestoredConversations(L, 2);
            g_lua_settop(L, top);
            return g_OrigSendCommand(L);
        }

        if (idStr == "StartSearchLightAutoAimToCharacter")
        {
            std::uint32_t heli = 0;
            std::uint32_t target = 0;
            std::string targetName;
            const bool isHeli = ReadHeliTarget(L, heli);
            const bool hasTarget = ReadCommandGameObject(L, 2, "name", target, targetName);
            g_lua_settop(L, top);
            if (!isHeli)
                LogWarn("StartSearchLightAutoAimToCharacter: target is not a helicopter, ignored");
            else if (!hasTarget)
                LogWarn("StartSearchLightAutoAimToCharacter: name \"%s\" is not a game object in this mission, ignored", targetName.c_str());
            else
                HeliSearchLight_StartAutoAim(heli, target);
            return 0;
        }
        if (idStr == "StopSearchLightAutoAim")
        {
            std::uint32_t heli = 0;
            if (ReadHeliTarget(L, heli))
                HeliSearchLight_StopAutoAim(heli);
            else
                LogWarn("StopSearchLightAutoAim: target is not a helicopter, ignored");
            g_lua_settop(L, top);
            return 0;
        }
        if (idStr == "SetSearchLightAngle")
        {
            std::uint32_t heli = 0;
            const bool isHeli = ReadHeliTarget(L, heli);
            const bool hasYaw = CommandHasNumber(L, 2, "yaw");
            const bool hasPitch = CommandHasNumber(L, 2, "pitch");
            const float yaw = static_cast<float>(ReadCommandNumberOr(L, 2, "yaw", 0.0));
            const float pitch = static_cast<float>(ReadCommandNumberOr(L, 2, "pitch", 0.0));
            g_lua_settop(L, top);
            if (!isHeli)
                LogWarn("SetSearchLightAngle: target is not a helicopter, ignored");
            else if (!hasYaw && !hasPitch)
                HeliSearchLight_ClearAngle(heli);
            else
                HeliSearchLight_SetAngle(heli, yaw, pitch);
            return 0;
        }
        if (idStr == "ChangeTargetSpeed")
        {
            std::uint32_t heli = 0;
            const bool isHeli = ReadHeliTarget(L, heli);
            const bool hasSpeed = CommandHasNumber(L, 2, "targetSpeed");
            const float targetSpeed = static_cast<float>(ReadCommandNumberOr(L, 2, "targetSpeed", 0.0));
            const float changeTime = static_cast<float>(ReadCommandNumberOr(L, 2, "changeTime", 0.0));
            g_lua_settop(L, top);
            if (!isHeli)
                LogWarn("ChangeTargetSpeed: target is not a helicopter, ignored");
            else if (!hasSpeed)
                LogWarn("ChangeTargetSpeed: needs targetSpeed (km/h), ignored");
            else
                Set_HeliTargetSpeed(heli, targetSpeed, changeTime);
            return 0;
        }
        if (idStr == "OpenLeftDoor" || idStr == "CloseLeftDoor" || idStr == "OpenRightDoor" || idStr == "CloseRightDoor")
        {
            std::uint32_t heli = 0;
            const bool isHeli = ReadHeliTarget(L, heli);
            g_lua_settop(L, top);
            const std::uint32_t side = (idStr == "OpenLeftDoor" || idStr == "CloseLeftDoor") ? 0u : 1u;
            const HeliDoorRequest request = (idStr == "OpenLeftDoor" || idStr == "OpenRightDoor") ? HeliDoorRequest::Open : HeliDoorRequest::Close;
            if (isHeli)
                HeliDoor_SetRequest(heli, side, request);
            else
                LogWarn("%s: target is not a helicopter, ignored", idStr.c_str());
            return 0;
        }
        if (idStr == "ResetDoors")
        {
            std::uint32_t heli = 0;
            const bool isHeli = ReadHeliTarget(L, heli);
            g_lua_settop(L, top);
            if (isHeli)
            {
                HeliDoor_SetRequest(heli, 0, HeliDoorRequest::None);
                HeliDoor_SetRequest(heli, 1, HeliDoorRequest::None);
            }
            else
                LogWarn("ResetDoors: target is not a helicopter, ignored");
            return 0;
        }
        if (idStr == "DisableDamageEffects" || idStr == "EnableDamageEffects")
        {
            std::uint32_t heli = 0;
            const bool isHeli = ReadHeliTarget(L, heli);
            g_lua_settop(L, top);
            if (isHeli)
                HeliDamageEffects_SetEnabled(heli, idStr == "EnableDamageEffects");
            else
                LogWarn("%s: target is not a helicopter, ignored", idStr.c_str());
            return 0;
        }
        if (idStr == "SetRoomLightMode")
        {
            std::uint32_t heli = 0;
            const bool isHeli = ReadHeliTarget(L, heli);
            const bool hasMode = CommandHasNumber(L, 2, "mode");
            const bool modeGiven = CommandHasField(L, 2, "mode");
            const double mode = ReadCommandNumberOr(L, 2, "mode", -1.0);
            g_lua_settop(L, top);
            if (!isHeli)
                LogWarn("SetRoomLightMode: target is not a helicopter, ignored");
            else if (!hasMode)
                LogWarn("SetRoomLightMode: mode is %s, needs the number 0, 1 or 2, ignored", modeGiven ? "not a number" : "missing");
            else if (mode != 0.0 && mode != 1.0 && mode != 2.0)
                LogWarn("SetRoomLightMode: mode is %g, needs 0, 1 or 2, ignored", mode);
            else
                HeliRoomLight_SetMode(heli, static_cast<std::int32_t>(mode));
            return 0;
        }
        if (idStr == "SetEnablePilotAiVoice")
        {
            std::uint32_t heli = 0;
            const bool isHeli = ReadHeliTarget(L, heli);
            const bool enabled = ReadCommandBool(L, 2, "enabled");
            g_lua_settop(L, top);
            if (isHeli)
                Set_HeliPilotAiVoiceEnabled(heli, enabled);
            else
                LogWarn("SetEnablePilotAiVoice: target is not a helicopter, ignored");
            return 0;
        }
        if (idStr == "SetPlayerMachineGunEnabled")
        {
            std::uint32_t heli = 0;
            const bool isHeli = ReadHeliTarget(L, heli);
            g_lua_pushstring(L, const_cast<char*>("enabled"));
            g_lua_gettable(L, 2);
            const int enabledType = g_lua_type(L, -1);
            g_lua_settop(L, top);
            const bool enabled = ReadCommandBool(L, 2, "enabled");
            g_lua_settop(L, top);
            if (!isHeli || (heli >> 9) != 0)
                LogWarn("SetPlayerMachineGunEnabled: target is not the support heli (TppHeli2), ignored");
            else if (enabledType != LUA_TBOOLEAN && enabledType != LUA_TNUMBER)
                LogWarn("SetPlayerMachineGunEnabled: needs enabled (true or false), ignored");
            else
                Set_HeliPlayerMachineGunEnabled(heli & 0x1FFu, enabled);
            return 0;
        }
        if (idStr == "SetCannotRide")
        {
            std::uint32_t vehicle = 0;
            const bool isVehicle = ReadTypedTargetIndex(L, "TppVehicle2", kVehicle2Type, vehicle);
            const bool everyVehicle = IsWholeTypeTarget(L, "TppVehicle2") || (isVehicle && vehicle == kWholeTypeIndex);
            const bool cannotRide = !CommandHasField(L, 2, "enabled") || ReadCommandBool(L, 2, "enabled");
            g_lua_settop(L, top);
            if (everyVehicle)
                Set_EveryVehicleCannotRide(cannotRide);
            else if (isVehicle)
                Set_VehicleCannotRide(vehicle, cannotRide);
            else
                LogWarn("SetCannotRide: target is not a TppVehicle2 (an id, { type = \"TppVehicle2\", index = N }, or { type = \"TppVehicle2\" } for every vehicle), ignored");
            return 0;
        }
        if (idStr == "RequestToGetOnHelicopterAndAimTarget")
        {
            std::uint32_t soldier = 0;
            const bool isSoldier = ReadTypedTargetIndex(L, "TppSoldier2", TppGameObjectType::kSoldier2, soldier);
            std::uint32_t heli = 0;
            std::string heliName;
            const bool hasHeli = ReadCommandGameObject(L, 2, "heli", heli, heliName);
            std::string side;
            g_lua_pushstring(L, const_cast<char*>("side"));
            g_lua_gettable(L, 2);
            if (g_lua_type(L, -1) == LUA_TSTRING)
            {
                const char* s = g_lua_tolstring(L, -1, nullptr);
                if (s) side = s;
            }
            g_lua_settop(L, top);
            std::uint32_t aimTarget = kRideHeliNoTarget;
            std::string aimTargetName;
            const bool wantsTarget = CommandHasField(L, 2, "target");
            if (wantsTarget && !ReadCommandGameObject(L, 2, "target", aimTarget, aimTargetName))
            {
                LogWarn("RequestToGetOnHelicopterAndAimTarget: target \"%s\" is not a game object in this mission, he rides without one", aimTargetName.c_str());
                aimTarget = kRideHeliNoTarget;
            }
            g_lua_settop(L, top);

            HeliSeatSide seat = HeliSeatSide::Nearest;
            if (side == "LEFT")
                seat = HeliSeatSide::Left;
            else if (side == "RIGHT")
                seat = HeliSeatSide::Right;
            else if (!side.empty())
                LogWarn("RequestToGetOnHelicopterAndAimTarget: side \"%s\" is not LEFT or RIGHT, he takes the nearer seat", side.c_str());

            const char* reason = nullptr;
            if (!isSoldier)
                LogWarn("RequestToGetOnHelicopterAndAimTarget: target is not a TppSoldier2, ignored");
            else if (!hasHeli || !HeliKey::IsHeliType(heli >> 9))
                LogWarn("RequestToGetOnHelicopterAndAimTarget: heli \"%s\" is not a helicopter in this mission, ignored", heliName.c_str());
            else if (!SoldierRideHeli_GetOn((static_cast<std::uint32_t>(TppGameObjectType::kSoldier2) << 9) | soldier, heli, seat, static_cast<std::uint16_t>(aimTarget), &reason))
                LogWarn("RequestToGetOnHelicopterAndAimTarget: soldier %u not sent, %s", soldier, reason);
            return 0;
        }
        if (idStr == "RequestToGetOffHelicopter")
        {
            std::uint32_t soldier = 0;
            const bool isSoldier = ReadTypedTargetIndex(L, "TppSoldier2", TppGameObjectType::kSoldier2, soldier);
            g_lua_settop(L, top);
            const char* reason = nullptr;
            if (!isSoldier)
                LogWarn("RequestToGetOffHelicopter: target is not a TppSoldier2, ignored");
            else if (!SoldierRideHeli_GetOff((static_cast<std::uint32_t>(TppGameObjectType::kSoldier2) << 9) | soldier, &reason))
                LogWarn("RequestToGetOffHelicopter: soldier %u not sent, %s", soldier, reason);
            return 0;
        }
        if (idStr == "SetFlagDisableBulletDamageReactionInHelicopter")
        {
            std::uint32_t soldier = 0;
            const bool isSoldier = ReadTypedTargetIndex(L, "TppSoldier2", TppGameObjectType::kSoldier2, soldier);
            g_lua_settop(L, top);
            g_lua_pushstring(L, const_cast<char*>("flag"));
            g_lua_gettable(L, 2);
            const int flagType = g_lua_type(L, -1);
            g_lua_settop(L, top);
            const bool flag = ReadCommandBool(L, 2, "flag");
            g_lua_settop(L, top);
            if (!isSoldier)
                LogWarn("SetFlagDisableBulletDamageReactionInHelicopter: target is not a TppSoldier2, ignored");
            else if (flagType != LUA_TBOOLEAN && flagType != LUA_TNUMBER)
                LogWarn("SetFlagDisableBulletDamageReactionInHelicopter: needs flag (true or false), ignored");
            else
                SoldierRideHeli_SetFlinchDisabled(soldier, flag);
            return 0;
        }
        if (idStr == "RequestToFireOnHelicopter")
        {
            std::uint32_t soldier = 0;
            const bool isSoldier = ReadTypedTargetIndex(L, "TppSoldier2", TppGameObjectType::kSoldier2, soldier);
            std::uint32_t heli = 0;
            std::string heliName;
            const bool hasHeli = ReadCommandGameObject(L, 2, "heli", heli, heliName);
            g_lua_settop(L, top);
            std::uint32_t target = 0;
            std::string targetName;
            const bool hasTarget = ReadCommandGameObject(L, 2, "target", target, targetName);
            g_lua_settop(L, top);
            const float angle = static_cast<float>(ReadCommandNumberOr(L, 2, "angle", 0.0));
            g_lua_settop(L, top);
            const float radius = static_cast<float>(ReadCommandNumberOr(L, 2, "radius", 0.0));
            g_lua_settop(L, top);
            const char* reason = nullptr;
            if (!isSoldier)
                LogWarn("RequestToFireOnHelicopter: target is not a TppSoldier2, ignored");
            else if (!hasHeli || !HeliKey::IsHeliType(heli >> 9))
                LogWarn("RequestToFireOnHelicopter: heli \"%s\" is not a helicopter in this mission, ignored", heliName.c_str());
            else if (!hasTarget)
                LogWarn("RequestToFireOnHelicopter: target \"%s\" is not a game object in this mission, ignored", targetName.c_str());
            else if (!SoldierRideHeli_RequestFire(soldier, target, angle, radius, &reason))
                LogWarn("RequestToFireOnHelicopter: soldier %u does not fire, %s", soldier, reason);
            return 0;
        }
        if (idStr == "SetActionCommandFireToTarget")
        {
            std::uint32_t heli = 0;
            const bool isHeli = ReadHeliTarget(L, heli);
            SupportHeliFireRequest request{};
            std::string targetName;
            request.hasTarget = ReadCommandGameObject(L, 2, "targetId", request.targetId, targetName);
            g_lua_settop(L, top);
            request.hasPosition = ReadCommandPoint(L, 2, "position", request.position);
            g_lua_settop(L, top);
            if (!isHeli || (heli >> 9) != 0)
            {
                LogWarn("SetActionCommandFireToTarget: target is not the support heli (TppHeli2), ignored");
                return 0;
            }
            if (!request.hasTarget && !request.hasPosition)
            {
                LogWarn("SetActionCommandFireToTarget: needs targetId (a game object) or position (a Vector3), ignored");
                return 0;
            }
            SupportHeliFire_SetPending(request);
            const int results = g_OrigSendCommand(L);
            if (!SupportHeliFire_TakeHandled())
                LogWarn("SetActionCommandFireToTarget: the support heli did not take the command, ignored");
            return results;
        }
        if (idStr == "ChangeFova" || idStr == "SetWearEquip")
        {
            const bool applyOffScreen = ReadCommandBool(L, 2, "applyOffScreen");
            g_lua_settop(L, top);
            if (!applyOffScreen && !ChangeFovaOffScreen::AnyHeld())
                return g_OrigSendCommand(L);

            std::uint32_t soldier = 0;
            const bool isSoldier = ReadTypedTargetIndex(L, "TppSoldier2", TppGameObjectType::kSoldier2, soldier);
            const bool everySoldier = IsWholeTypeTarget(L, "TppSoldier2") || (isSoldier && soldier == 0x1FFu);
            if (!applyOffScreen)
            {
                if (isSoldier || everySoldier)
                    ChangeFovaOffScreen::Cancel(everySoldier, soldier);
                return g_OrigSendCommand(L);
            }
            if (!isSoldier && !everySoldier)
            {
                LogWarn("%s: applyOffScreen needs a TppSoldier2 target, applied right away", idStr.c_str());
                return g_OrigSendCommand(L);
            }
            ChangeFovaOffScreen::BeginHold(everySoldier, soldier);
            const int results = g_OrigSendCommand(L);
            ChangeFovaOffScreen::EndHold(everySoldier, soldier);
            return results;
        }
        if (idStr == "RegistGrenadeId")
        {
            std::uint32_t soldier = 0;
            const bool isSoldier = ReadTypedTargetIndex(L, "TppSoldier2", TppGameObjectType::kSoldier2, soldier);
            if (!isSoldier || soldier == 0x1FFu)
                return g_OrigSendCommand(L);   // { type = "TppSoldier2" }: vanilla, every soldier
            const std::int32_t grenadeId = static_cast<std::int32_t>(ReadCommandNumberOr(L, 2, "grenadeId", 0x15E));
            g_lua_settop(L, top);
            const std::int32_t stunId = static_cast<std::int32_t>(ReadCommandNumberOr(L, 2, "stunId", 0.0));
            g_lua_settop(L, top);
            if (!SoldierGrenadeIds_SetForSoldier(soldier, grenadeId, stunId))
                LogWarn("RegistGrenadeId: the SoldierGrenadeIds module is off, soldier %u keeps the shared ids", soldier);
            return 0;
        }
        if (idStr == "ThrowGrenade" || idStr == "ThrowGrenadeInDoor")
        {
            std::uint32_t soldier = 0;
            const bool isSoldier = ReadTypedTargetIndex(L, "TppSoldier2", TppGameObjectType::kSoldier2, soldier);
            std::int32_t equipId = -1;
            if (CommandHasNumber(L, 2, "equipId"))
                equipId = static_cast<std::int32_t>(ReadCommandNumber(L, 2, "equipId"));
            g_lua_settop(L, top);
            const std::uint32_t soldierId = (static_cast<std::uint32_t>(TppGameObjectType::kSoldier2) << 9) | soldier;
            const char* reason = "the target is not one TppSoldier2";
            bool thrown = false;
            if (isSoldier && soldier != 0x1FFu && idStr == "ThrowGrenade")
            {
                float position[3] = {};
                const bool hasPos = ReadCommandPoint(L, 2, "pos", position);
                g_lua_settop(L, top);
                if (!hasPos)
                    reason = "needs pos (a Vector3 or {x, y, z})";
                else
                    thrown = SoldierThrow_ToPosition(soldierId, position, equipId, &reason);
            }
            else if (isSoldier && soldier != 0x1FFu)
            {
                std::string gimmickId;
                std::string dataSetName;
                g_lua_pushstring(L, const_cast<char*>("gimmickId"));
                g_lua_gettable(L, 2);
                if (g_lua_type(L, -1) == LUA_TSTRING)
                {
                    const char* s = g_lua_tolstring(L, -1, nullptr);
                    if (s) gimmickId = s;
                }
                g_lua_settop(L, top);
                g_lua_pushstring(L, const_cast<char*>("dataSetName"));
                g_lua_gettable(L, 2);
                if (g_lua_type(L, -1) == LUA_TSTRING)
                {
                    const char* s = g_lua_tolstring(L, -1, nullptr);
                    if (s) dataSetName = s;
                }
                g_lua_settop(L, top);
                thrown = SoldierThrow_InDoor(soldierId, gimmickId.c_str(), dataSetName.c_str(), equipId, &reason);
            }
            g_lua_settop(L, top);
            if (!thrown)
                LogWarn("%s: soldier %u does not throw, %s", idStr.c_str(), soldier, reason);
            g_lua_pushboolean(L, thrown ? 1 : 0);
            return 1;
        }

        return g_OrigSendCommand(L);
    }
}

bool Install_GameObjectSendCommand_Hook()
{
    if (g_Installed) return true;

    if (!gAddr.GameObject_SendCommand)
    {
        LogError("GameObjectSendCommand: address missing for this build, custom SendCommand ids off");
        return false;
    }

    void* target = ResolveGameAddress(gAddr.GameObject_SendCommand);
    if (!target)
    {
        LogError("GameObjectSendCommand: address resolve failed, custom SendCommand ids off");
        return false;
    }

    if (!ResolveLuaApi())
    {
        LogError("GameObjectSendCommand: Lua API resolve failed, custom SendCommand ids off");
        return false;
    }

    const bool ok = CreateAndEnableHook(
        target,
        reinterpret_cast<void*>(&hk_SendCommand),
        reinterpret_cast<void**>(&g_OrigSendCommand));

    if (ok)
    {
        g_Installed = true;
    }
    else
    {
        LogError("GameObjectSendCommand: hook failed, custom SendCommand ids off");
    }
    return ok;
}

bool Uninstall_GameObjectSendCommand_Hook()
{
    if (!g_Installed) return true;
    void* target = ResolveGameAddress(gAddr.GameObject_SendCommand);
    if (target) DisableAndRemoveHook(target);
    g_OrigSendCommand = nullptr;
    g_Installed       = false;
    return true;
}
