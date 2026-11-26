#include <pch.h>

#include <cmath>
#include <cstdint>

#include "GameCamera.h"
#include "LuaApi.h"
#include "MissionCodeGuard.h"
#include "log.h"
#include "V_TppDataUtilityLib.h"

#include "../hooks/core/AnimationControllerImpl_SetGameObjectIdToCharaControl.h"
#include "../hooks/player/Fova2ControllerImpl_ApplyPlayerFova.h"
#include "../hooks/soldier/Soldier2Impl_RealizeRealizedSoldier.h"

namespace
{
    constexpr std::uint32_t kPlayerType  = 0;
    constexpr std::uint32_t kSoldierType = 2;
    constexpr std::uint32_t kVehicleType = 0x32;

    static int __cdecl l_SetMeshVisible(lua_State* L)
    {
        if (GetLuaTop(L) < 3 || !LuaIsNumber(L, 1))
        {
            LogWarn("SetMeshVisible: needs (gameObjectId, meshName, visible), ignored");
            PushLuaBool(L, false);
            return 1;
        }
        if (MissionCodeGuard::ShouldBypassHooks())
        {
            PushLuaBool(L, false);
            return 1;
        }

        const std::uint32_t gameObjectId = static_cast<std::uint32_t>(GetLuaInt(L, 1));
        const std::uint32_t meshId       = GetLuaStrCode32Arg(L, 2);
        const bool          visible      = GetLuaBool(L, 3);
        if (meshId == 0)
        {
            LogWarn("SetMeshVisible: mesh name missing, ignored");
            PushLuaBool(L, false);
            return 1;
        }

        bool ok = false;
        switch (gameObjectId >> 9)
        {
        case kPlayerType:  ok = PlayerMesh::SetVisible(gameObjectId, meshId, visible); break;
        case kSoldierType: ok = SoldierMesh::SetVisible(gameObjectId, meshId, visible); break;
        case kVehicleType:
            LogWarn("SetMeshVisible: vehicles are not supported (game object 0x%X), ignored", gameObjectId);
            break;
        default:           ok = GameObjectMesh::SetVisible(gameObjectId, meshId, visible); break;
        }
        PushLuaBool(L, ok);
        return 1;
    }

    static int __cdecl l_IsMeshVisible(lua_State* L)
    {
        if (GetLuaTop(L) < 2 || !LuaIsNumber(L, 1) || MissionCodeGuard::ShouldBypassHooks())
        {
            PushLuaNil(L);
            return 1;
        }

        const std::uint32_t gameObjectId = static_cast<std::uint32_t>(GetLuaInt(L, 1));
        const std::uint32_t meshId       = GetLuaStrCode32Arg(L, 2);

        bool visible = false;
        bool known = false;
        switch (gameObjectId >> 9)
        {
        case kPlayerType:  known = PlayerMesh::IsVisible(gameObjectId, meshId, visible); break;
        case kSoldierType: known = SoldierMesh::IsVisible(gameObjectId, meshId, visible); break;
        case kVehicleType: break;
        default:           known = GameObjectMesh::IsVisible(gameObjectId, meshId, visible); break;
        }
        if (known)
            PushLuaBool(L, visible);
        else
            PushLuaNil(L);
        return 1;
    }

    static bool ReadPosition(lua_State* L, int idx, float* out)   // a Vector3 or {x, y, z}
    {
        const int top = g_lua_gettop(L);
        const int type = g_lua_type(L, idx);
        bool ok = false;
        if (type == LUA_TTABLE && g_lua_rawgeti)
        {
            ok = true;
            for (int i = 0; i < 3 && ok; ++i)
            {
                g_lua_rawgeti(L, idx, i + 1);
                ok = g_lua_type(L, -1) == LUA_TNUMBER;
                if (ok)
                    out[i] = static_cast<float>(g_lua_tonumber(L, -1));
                g_lua_settop(L, top);
            }
        }
        else if (type == LUA_TUSERDATA && g_lua_pcall && g_lua_pushvalue)
        {
            const char* const getters[3] = { "GetX", "GetY", "GetZ" };
            ok = true;
            for (int i = 0; i < 3 && ok; ++i)
            {
                g_lua_getfield(L, idx, const_cast<char*>(getters[i]));
                g_lua_pushvalue(L, idx);
                ok = g_lua_pcall(L, 1, 1, 0) == 0 && g_lua_type(L, -1) == LUA_TNUMBER;
                if (ok)
                    out[i] = static_cast<float>(g_lua_tonumber(L, -1));
                g_lua_settop(L, top);
            }
        }
        g_lua_settop(L, top);
        return ok;
    }

    static int __cdecl l_IsOnScreen(lua_State* L)
    {
        float position[3] = {};
        if (GetLuaTop(L) < 1 || !ReadPosition(L, 1, position))
        {
            static bool s_warned = false;
            if (!s_warned)
            {
                s_warned = true;
                LogWarn("IsOnScreen: needs a position (Vector3 or {x, y, z}), returns nil");
            }
            PushLuaNil(L);
            return 1;
        }
        if (MissionCodeGuard::ShouldBypassHooks())
        {
            PushLuaBool(L, true);
            return 1;
        }

        float radius = GameCamera::kSoldierRadius;
        if (GetLuaTop(L) >= 2 && LuaIsNumber(L, 2) && GetLuaNumber(L, 2) > 0.0f)
            radius = GetLuaNumber(L, 2);
        PushLuaBool(L, GameCamera::IsOnScreen(position, radius));
        return 1;
    }

    static int __cdecl l_GetCameraDistance(lua_State* L)
    {
        float position[3] = {};
        if (GetLuaTop(L) < 1 || !ReadPosition(L, 1, position))
        {
            static bool s_warned = false;
            if (!s_warned)
            {
                s_warned = true;
                LogWarn("GetCameraDistance: needs a position (Vector3 or {x, y, z}), returns nil");
            }
            PushLuaNil(L);
            return 1;
        }
        if (MissionCodeGuard::ShouldBypassHooks())
        {
            PushLuaNumber(L, 0.0f);
            return 1;
        }

        PushLuaNumber(L, std::sqrt(GameCamera::DistanceSq(position)));
        return 1;
    }

    static luaL_Reg g_VTppDataUtilityLib[] =
    {
        { "SetMeshVisible",    l_SetMeshVisible },
        { "IsMeshVisible",     l_IsMeshVisible },
        { "IsOnScreen",        l_IsOnScreen },
        { "GetCameraDistance", l_GetCameraDistance },

        { nullptr, nullptr }
    };
}

bool Register_V_TppDataUtilityLibrary(lua_State* L)
{
    return RegisterLuaLibrary(L, "V_TppDataUtility", g_VTppDataUtilityLib);
}
