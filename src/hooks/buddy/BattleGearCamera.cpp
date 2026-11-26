#include <pch.h>

#include "BattleGearCamera.h"

extern "C" {
    #include "lua.h"
}

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "BattleGearBuddy.h"
#include "BattleGearWeapons.h"
#include "HookUtils.h"
#include "LuaBroadcaster.h"
#include "log.h"
#include "../../lua/LuaApi.h"

namespace
{
    struct View
    {
        double distance;
        double across;        // metres the camera stands to the left of its pivot; the game's own offsets are negative, to the right
        double focalLength;
        double interpTime;
    };

    constexpr int       kNoGameObject   = 0xFFFF;
    constexpr int       kWalkerGearType = 0x2C;   // GAME_OBJECT_TYPE_WALKERGEAR2, the buddy walker that carries the Battle Gear
    constexpr ULONGLONG kCheckEveryMs   = 50;
    constexpr int       kLuaSlotsNeeded = 10;
    constexpr int       kMostWarnings   = 4;
    constexpr double    kPitchMin       = -30.0;   // ACT_StrykerDefault's rotXMin; rotXMax is the default
    constexpr double    kPitchMax       = 80.0;
    // the tanks' own two views. Driving: CameraAroundParams.lua's ACT_StrykerDefault, 10 m round the turret through an 18 mm lens.
    // Weapon ready: RideVehicleActionImpl::GetWeaponOffset's main gun view, 2 m right of the turret and 2.8 m back, with the aiming
    // camera's 1.2 m behind that. Where the view circles is the ride's own base point: the walker ride's camera hook raises it
    // over the turret (the manual mode's target offsets do not reach a ride's base point).
    constexpr View      kRideView       = { 10.0, 0.0, 18.0, 1.0 };
    constexpr View      kAimView        = { 4.0, -2.0, 21.0, 0.3 };

    bool      g_Busy = false;   // our own Lua calls come back through the lua_pcall hook
    bool      g_ManualOn = false;
    int       g_ViewAiming = -1;
    ULONGLONG g_NextCheck = 0;
    int       g_Warnings = 0;
#ifdef _DEBUG
    using GetPlayer2System_t = std::uint8_t*(__fastcall*)();
    ULONGLONG g_NextVehicleProbe = 0;   // TEMP probe: the player camera's distance, offset, focal length and base riding other vehicles

    void ProbeVehicleCameraSEH(int ridden)
    {
        const auto get = reinterpret_cast<GetPlayer2System_t>(gAddr.GetPlayer2System ? ResolveGameAddress(gAddr.GetPlayer2System) : nullptr);
        if (!get)
            return;
        __try
        {
            const std::uint8_t* system = get();
            const std::uint8_t* player = system ? *reinterpret_cast<const std::uint8_t* const*>(system + 0x08) : nullptr;
            const std::uint8_t* camera = player ? *reinterpret_cast<const std::uint8_t* const*>(player + 0xB8) : nullptr;
            if (!camera)
                return;
            const float* offset = reinterpret_cast<const float*>(camera + 0x150);
            const float* base = reinterpret_cast<const float*>(camera + 0x170);
            LogDebug("BattleGear camera probe: riding 0x%X (type 0x%X), mode %d | distance %.2f / %.2f, offset %.2f %.2f %.2f, focal %.1f, "
                     "base %.2f %.2f %.2f", ridden, ridden >> 9, *reinterpret_cast<const int*>(camera + 0x20C),
                     *reinterpret_cast<const float*>(camera + 0x230), *reinterpret_cast<const float*>(camera + 0x234), offset[0], offset[1],
                     offset[2], *reinterpret_cast<const float*>(camera + 0x2DC), base[0], base[1], base[2]);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }
#endif

    bool PushPlayerFunction(lua_State* L, const char* name)
    {
        g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("Player"));
        if (g_lua_type(L, -1) != LUA_TTABLE)
            return false;
        g_lua_getfield(L, -1, const_cast<char*>(name));
        return g_lua_type(L, -1) == LUA_TFUNCTION;
    }

    bool Call(lua_State* L, const char* name, int args, int results)
    {
        if (g_lua_pcall(L, args, results, 0) == 0)
            return true;
        if (g_Warnings++ < kMostWarnings)
        {
            const char* err = GetLuaString(L, -1);
            LogWarn("BattleGear: the camera's Lua call %s failed, %s", name, err ? err : "unknown error");
        }
        return false;
    }

    int RiddenObject(lua_State* L)
    {
        const int top = g_lua_gettop(L);
        int ridden = kNoGameObject;
        if (PushPlayerFunction(L, "GetGameObjectIdIsRiddenToLocal") && Call(L, "GetGameObjectIdIsRiddenToLocal", 0, 1))
            ridden = static_cast<int>(g_lua_tointeger(L, -1));
        g_lua_settop(L, top);
        return ridden;
    }

    bool SetManualMode(lua_State* L, bool on)
    {
        const int top = g_lua_gettop(L);
        bool ok = false;
        if (PushPlayerFunction(L, "SetAroundCameraManualMode"))
        {
            g_lua_pushboolean(L, on ? 1 : 0);
            ok = Call(L, "SetAroundCameraManualMode", 1, 0);
        }
        g_lua_settop(L, top);
        return ok;
    }

    void SetNumber(lua_State* L, int table, const char* key, double value)
    {
        g_lua_pushstring(L, const_cast<char*>(key));
        g_lua_pushnumber(L, value);
        g_lua_rawset(L, table);
    }

    bool PushVector3(lua_State* L, double x, double y, double z)
    {
        g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("Vector3"));
        g_lua_pushnumber(L, x);
        g_lua_pushnumber(L, y);
        g_lua_pushnumber(L, z);
        return Call(L, "Vector3", 3, 1);
    }

    bool SendView(lua_State* L, int ridden, const View& view)
    {
        const int top = g_lua_gettop(L);
        bool ok = false;
        if (PushPlayerFunction(L, "SetAroundCameraManualModeParams"))
        {
            g_lua_createtable(L, 0, 10);
            const int params = g_lua_gettop(L);
            SetNumber(L, params, "distance", view.distance);
            SetNumber(L, params, "focalLength", view.focalLength);
            SetNumber(L, params, "targetInterpTime", view.interpTime);
            SetNumber(L, params, "rotationLimitMinX", kPitchMin);
            SetNumber(L, params, "rotationLimitMaxX", kPitchMax);
            SetNumber(L, params, "ignoreCollisionGameObjectId", static_cast<double>(ridden));
            g_lua_pushstring(L, const_cast<char*>("targetIsPlayer"));
            g_lua_pushboolean(L, 1);
            g_lua_rawset(L, params);
            g_lua_pushstring(L, const_cast<char*>("offset"));
            if (PushVector3(L, view.across, 0.0, 0.0))
                g_lua_rawset(L, params);
            g_lua_settop(L, params);
            ok = Call(L, "SetAroundCameraManualModeParams", 1, 0);
        }
        g_lua_settop(L, top);
        if (ok && PushPlayerFunction(L, "UpdateAroundCameraManualModeParams"))
            ok = Call(L, "UpdateAroundCameraManualModeParams", 0, 0);
        g_lua_settop(L, top);
        return ok;
    }
}

namespace battleGearCamera
{
    void Update(lua_State* L)
    {
        if (!L || g_Busy)
            return;
        const bool out = battleGear::IsBattleGearOut();
#ifdef _DEBUG
        const ULONGLONG probeNow = GetTickCount64();
        if (probeNow >= g_NextVehicleProbe && V_FrameWork::LuaStackHasRoom(L, kLuaSlotsNeeded))
        {
            g_NextVehicleProbe = probeNow + 1000;
            g_Busy = true;
            const int riddenNow = RiddenObject(L);
            g_Busy = false;
            if (riddenNow != kNoGameObject && (riddenNow >> 9) != kWalkerGearType)
                ProbeVehicleCameraSEH(riddenNow);
        }
#endif
        if (!out && !g_ManualOn)
            return;
        const ULONGLONG now = GetTickCount64();
        if (now < g_NextCheck || !V_FrameWork::LuaStackHasRoom(L, kLuaSlotsNeeded))
            return;
        g_NextCheck = now + kCheckEveryMs;
        g_Busy = true;
        const int top = g_lua_gettop(L);
        const int ridden = out ? RiddenObject(L) : kNoGameObject;
        const bool riding = ridden != kNoGameObject && (ridden >> 9) == kWalkerGearType;
        if (riding && !battleGearWeapons::FirstPerson())   // first person is the game's own camera, from the cupola
        {
            if (!g_ManualOn)
            {
                g_ManualOn = SetManualMode(L, true);
                g_ViewAiming = -1;
            }
            const int aiming = battleGearWeapons::Aiming() ? 1 : 0;
            if (g_ManualOn && aiming != g_ViewAiming)
            {
                if (SendView(L, ridden, aiming ? kAimView : kRideView))
                    g_ViewAiming = aiming;
                else if (g_ViewAiming < 0)
                {
                    SetManualMode(L, false);
                    g_ManualOn = false;
                }
            }
        }
        else if (g_ManualOn)
        {
            SetManualMode(L, false);
            g_ManualOn = false;
            g_ViewAiming = -1;
        }
        g_lua_settop(L, top);
        g_Busy = false;
    }
}
