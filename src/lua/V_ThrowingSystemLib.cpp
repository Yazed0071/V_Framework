#include "pch.h"
#include "V_ThrowingSystemLib.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "LuaApi.h"
#include "MissionCodeGuard.h"
#include "log.h"
#include "AddressSet.h"
#include "HookUtils.h"
#include "../hooks/equip/EquipParameterTablesImpl_ReloadEquipParameterTablesImpl.h"
#include "../hooks/equip/ThrowingSystemImpl_ExecuteSerially.h"
#include "../hooks/equip/ThrowingSystemLayout.h"

namespace
{
    using namespace ThrowingSystemLayout;

    struct alignas(16) ThrowingDesc
    {
        float         position[4];
        float         rotation[4];
        float         velocity[4];
        std::uint16_t fuseTicks;         // 1/299.7 s ticks, 0xFFFF = the SupportWeaponParameter fuse
        std::uint16_t owner;
        std::uint16_t hitIgnoreId;
        std::uint16_t soundOwner;
        std::uint16_t blastTag;
        std::uint8_t  syncIndex;         // 0x3F offline, the fuse only counts down then
        std::uint8_t  netPeer;
        std::uint16_t equipIdAndMode;    // bits 0-10 equipId, 11-12 velocity mode
        std::uint16_t options;           // bit 0 = trajectory preview only, bits 5-12 grade
    };
    static_assert(sizeof(ThrowingDesc) == 0x40, "the engine copies 0x40 bytes");

    struct alignas(16) ChaffRequest
    {
        std::uint16_t owner;
        std::uint8_t  unused0[14];
        float         position[4];
        float         duration;
        std::uint32_t noEffect;          // nonzero: no cloud effect, same jamming
        std::uint8_t  endless;           // bit 0
        std::uint8_t  unused1;
        std::uint16_t handle;
        std::uint8_t  unused2[4];
    };
    static_assert(sizeof(ChaffRequest) == 0x30, "RequestToChaff copies up to +0x2C");

    struct alignas(16) StunRequest
    {
        std::uint16_t owner;
        std::uint8_t  unused0[14];
        float         position[4];
        float         radius;
        std::uint16_t blastTag;
        std::uint8_t  unused1[2];
        std::uint32_t attackId;
        std::uint8_t  grade;
        std::uint8_t  unused2;
        std::uint16_t handle;
    };
    static_assert(sizeof(StunRequest) == 0x30, "RequestToStun copies up to +0x2F");

    struct alignas(16) SmokeRequest
    {
        std::uint16_t owner;
        std::uint8_t  unused0[14];
        float         position[4];
        float         radius;
        float         duration;          // the cloud then fades for a few more seconds
        std::uint32_t type;              // 0 smoke, 3 sleeping gas, 1 and 4 gas variants
        std::uint8_t  flags;
        std::uint8_t  unused1;
        std::uint16_t handle;
    };
    static_assert(sizeof(SmokeRequest) == 0x30, "RequestToSmoke copies up to +0x2F");

    struct alignas(16) FlameRequest
    {
        std::uint16_t owner;
        std::uint8_t  unused0[14];
        float         position[4];
        float         duration;
        float         look[4];           // the Molotov's fire look: 0.4, 1.0, 1.8, 2.2
        float         radius;
        std::uint32_t attackId;
        std::uint8_t  unused1;
        std::uint8_t  grade;
        std::uint8_t  unused2[2];
        std::uint16_t handle;
        std::uint8_t  unused3[14];
    };
    static_assert(sizeof(FlameRequest) == 0x50, "RequestToFlame copies up to +0x41");
    static_assert(offsetof(FlameRequest, radius) == 0x34, "FlameRequest radius");
    static_assert(offsetof(FlameRequest, grade) == 0x3D, "FlameRequest grade");
    static_assert(offsetof(FlameRequest, handle) == 0x40, "FlameRequest handle");

    using Throw_t                      = void(__fastcall*)(void* throwingSystem, const ThrowingDesc* desc);
    using GetPredictedPositionAtTime_t = bool(__fastcall*)(void* throwingSystem, std::uint8_t handle, float time, float* out);
    using GetThrowingCount_t           = std::uint32_t(__fastcall*)(void* throwingSystem, std::uint32_t owner, std::uint32_t supportWeaponType);
    using RequestSmoke_t               = void(__fastcall*)(void* operation, std::uint8_t handle);
    using IsObstructSight_t            = bool(__fastcall*)(void* impl, std::uint16_t equipId);
    using RangeAttackRequest_t         = std::uint16_t(__fastcall*)(void* rangeAttackSystem, const void* request);

    constexpr std::size_t kThrowSlot                      = 0x08 / sizeof(void*);
    constexpr std::size_t kGetPredictedPositionAtTimeSlot = 0x28 / sizeof(void*);
    constexpr std::size_t kGetThrowingCountSlot           = 0x78 / sizeof(void*);
    constexpr std::size_t kOperation                      = 0x40;   // ThrowingSystem -> ThrowingSystemOperation
    constexpr std::size_t kRequestSmokeSlot               = 0x08 / sizeof(void*);
    constexpr std::size_t kRequestToChaffSlot             = 0x10 / sizeof(void*);
    constexpr std::size_t kRequestToStunSlot              = 0x28 / sizeof(void*);
    constexpr std::size_t kRequestToFlameSlot             = 0x00 / sizeof(void*);
    constexpr std::size_t kRequestToSmokeSlot             = 0x08 / sizeof(void*);
    constexpr std::size_t kFlameQueuedCount               = 0xB0;
    constexpr std::size_t kSmokeQueuedCount               = 0xB4;
    constexpr std::uint32_t kAttackIdMolotovFire          = 0xBF;
    constexpr float       kMolotovFireLook[4]             = { 0.4f, 1.0f, 1.8f, 2.2f };
    constexpr float       kMolotovFireDuration            = 12.0f;
    constexpr float       kMolotovFireRadius              = 8.8f;
    constexpr std::uint32_t kMaxSmokeType                 = 4;
    constexpr std::size_t kChaffQueuedCount               = 0xB8;   // requests queued this frame, the engine reads past 4
    constexpr std::size_t kStunQueuedCount                = 0xC4;
    constexpr std::uint32_t kRangeAttackQueueSize         = 4;

    constexpr float         kTicksPerSecond    = 299.7f;
    constexpr std::uint16_t kMaxFuseTicks      = 0xFFFE;
    constexpr std::uint16_t kNoGameObject      = 0xFFFF;
    constexpr std::uint16_t kEquipIdMask       = 0x7FF;
    constexpr int           kMaxEquipId        = 0xFFFF;
    constexpr int           kVelocityModeShift = 11;
    constexpr int           kGradeShift        = 5;
    constexpr int           kMaxGrade          = 0xFF;
    constexpr std::uint8_t  kOfflineSyncIndex  = 0x3F;
    constexpr std::uint8_t  kNoNetPeer         = 0xFF;
    constexpr std::uint32_t kAttackIdStunGrenade = 0xB6;   // ATK_StunGrenade

    enum VelocityMode : std::uint16_t
    {
        kVelocityGiven          = 0,
        kTargetAndAngle         = 1,
        kTargetAndFlightTime    = 2,
    };

    void* VtableSlot(void* object, std::size_t slot)
    {
        return (*reinterpret_cast<void***>(object))[slot];
    }

    bool InOnlineMission(const char* function)
    {
        if (!MissionCodeGuard::ShouldBypassHooks())
            return false;
        LogWarn("%s: not available in online missions, ignored", function);
        return true;
    }

    bool ReadVectorAt(lua_State* L, int index, float out[3])
    {
        if (LuaType(L, index) != LUA_TTABLE)
            return false;
        bool ok = true;
        for (int i = 0; i < 3; ++i)
        {
            LuaRawGetI(L, index, i + 1);
            if (LuaIsNumber(L, -1))
                out[i] = GetLuaNumber(L, -1);
            else
                ok = false;
            LuaPop(L, 1);
        }
        return ok;
    }

    bool ReadVectorField(lua_State* L, const char* field, float out[3])
    {
        LuaGetField(L, 1, field);
        const bool ok = ReadVectorAt(L, GetLuaTop(L), out);
        LuaPop(L, 1);
        return ok;
    }

    bool ReadNumberField(lua_State* L, const char* field, float& out)
    {
        LuaGetField(L, 1, field);
        const bool have = LuaIsNumber(L, -1);
        if (have)
            out = GetLuaNumber(L, -1);
        LuaPop(L, 1);
        return have;
    }

    bool ReadIntegerField(lua_State* L, const char* field, int& out)
    {
        LuaGetField(L, 1, field);
        const bool have = LuaIsNumber(L, -1);
        if (have)
            out = GetLuaInt(L, -1);
        LuaPop(L, 1);
        return have;
    }

    bool ReadBoolField(lua_State* L, const char* field, bool& out)
    {
        LuaGetField(L, 1, field);
        const bool have = LuaType(L, -1) == LUA_TBOOLEAN;
        if (have)
            out = GetLuaBool(L, -1);
        LuaPop(L, 1);
        return have;
    }

    void SetField(lua_State* L, const char* key, double value)
    {
        g_lua_pushstring(L, const_cast<char*>(key));
        g_lua_pushnumber(L, value);
        g_lua_settable(L, -3);
    }

    void SetField(lua_State* L, const char* key, bool value)
    {
        g_lua_pushstring(L, const_cast<char*>(key));
        g_lua_pushboolean(L, value);
        g_lua_settable(L, -3);
    }

    int PushVector(lua_State* L, const float* vector, int count)
    {
        for (int i = 0; i < count; ++i)
            g_lua_pushnumber(L, vector[i]);
        return count;
    }

    bool LiveHandleArg(lua_State* L, int index, std::uint8_t*& system, std::uint32_t& handle)
    {
        system = ThrowingSystem();
        if (!system || !LuaIsNumber(L, index))
            return false;
        const int value = GetLuaInt(L, index);
        if (value < 0 || static_cast<std::uint32_t>(value) >= Capacity(system))
            return false;
        handle = static_cast<std::uint32_t>(value);
        return (Read<std::uint32_t>(Record(system, handle), kRecordFlags) & kFlagAlive) != 0;
    }

    int QueueForHandle(lua_State* L, const char* function, ThrowingRequestKind kind, int option = 0, float value = 0.0f)
    {
        std::uint8_t* system = nullptr;
        std::uint32_t handle = 0;
        if (InOnlineMission(function) || !LiveHandleArg(L, 1, system, handle))
        {
            PushLuaBool(L, false);
            return 1;
        }
        ThrowingRequest request;
        request.kind      = kind;
        request.handle    = handle;
        request.equipId   = RealEquipId(system, handle);
        request.option    = option;
        request.values[0] = value;
        PushLuaBool(L, ThrowingSystem_QueueRequest(request));
        return 1;
    }

    int l_Throw(lua_State* L)
    {
        if (LuaType(L, 1) != LUA_TTABLE)
        {
            LogWarn("V_ThrowingSystem.Throw: argument #1 must be a table, ignored");
            return 0;
        }
        if (InOnlineMission("V_ThrowingSystem.Throw"))
            return 0;
        std::uint8_t* system = ThrowingSystem();
        if (!system)
            return 0;

        int equipId = 0;
        ReadIntegerField(L, "equipId", equipId);
        if (equipId <= 0 || equipId > kMaxEquipId || equipId == kEquipIdMask)
        {
            LogWarn("V_ThrowingSystem.Throw: equipId %d invalid, ignored", equipId);
            return 0;
        }
        const std::int32_t engineId = equip::ToEngine(equip::ResolveReal(equipId));
        if (engineId <= 0 || engineId >= kEquipIdMask)
        {
            LogWarn("V_ThrowingSystem.Throw: equipId %d has no engine id right now, ignored", equipId);
            return 0;
        }

        ThrowingDesc desc{};
        if (!ReadVectorField(L, "pos", desc.position))
        {
            LogWarn("V_ThrowingSystem.Throw equipId %d: pos = { x, y, z } is required, ignored", equipId);
            return 0;
        }
        desc.rotation[3] = 1.0f;
        LuaGetField(L, 1, "rotation");
        const int rotationIndex = GetLuaTop(L);
        if (LuaType(L, rotationIndex) == LUA_TTABLE)
        {
            for (int i = 0; i < 4; ++i)
            {
                LuaRawGetI(L, rotationIndex, i + 1);
                if (LuaIsNumber(L, -1))
                    desc.rotation[i] = GetLuaNumber(L, -1);
                LuaPop(L, 1);
            }
        }
        LuaPop(L, 1);

        std::uint16_t mode = kVelocityGiven;
        float flightTime = 0.0f;
        float angle = 0.0f;
        if (ReadVectorField(L, "velocity", desc.velocity))
        {
            mode = kVelocityGiven;
        }
        else if (ReadVectorField(L, "target", desc.velocity))
        {
            if (ReadNumberField(L, "flightTime", flightTime) && flightTime > 0.0f)
            {
                mode = kTargetAndFlightTime;
                desc.velocity[3] = flightTime;
            }
            else if (ReadNumberField(L, "angle", angle))
            {
                mode = kTargetAndAngle;
                desc.velocity[3] = angle;
            }
            else
            {
                LogWarn("V_ThrowingSystem.Throw equipId %d: target needs flightTime or angle, ignored", equipId);
                return 0;
            }
        }
        else
        {
            LogWarn("V_ThrowingSystem.Throw equipId %d: velocity or target is required, ignored", equipId);
            return 0;
        }

        float fuseTime = 0.0f;
        desc.fuseTicks = kNoGameObject;
        if (ReadNumberField(L, "fuseTime", fuseTime) && fuseTime >= 0.0f)
        {
            const float ticks = std::round(fuseTime * kTicksPerSecond);
            desc.fuseTicks = static_cast<std::uint16_t>(ticks > kMaxFuseTicks ? kMaxFuseTicks : ticks);
        }

        int owner = 0;
        int hitIgnoreId = kNoGameObject;
        int grade = 0;
        ReadIntegerField(L, "owner", owner);
        ReadIntegerField(L, "hitIgnoreId", hitIgnoreId);
        ReadIntegerField(L, "grade", grade);
        grade = std::clamp(grade, 0, kMaxGrade);

        desc.owner          = static_cast<std::uint16_t>(owner);
        desc.hitIgnoreId    = static_cast<std::uint16_t>(hitIgnoreId);
        desc.soundOwner     = kNoGameObject;
        desc.blastTag       = kNoGameObject;
        desc.syncIndex      = kOfflineSyncIndex;
        desc.netPeer        = kNoNetPeer;
        desc.equipIdAndMode = static_cast<std::uint16_t>((engineId & kEquipIdMask) | (mode << kVelocityModeShift));
        desc.options        = static_cast<std::uint16_t>(grade << kGradeShift);

        reinterpret_cast<Throw_t>(VtableSlot(system, kThrowSlot))(system, &desc);
        PushLuaBool(L, true);
        return 1;
    }

    int l_Search(lua_State* L)
    {
        std::uint8_t* system = ThrowingSystem();
        float center[3] = {};
        if (!system || !ReadVectorAt(L, 1, center) || !LuaIsNumber(L, 2))
            return 0;
        const float radius = GetLuaNumber(L, 2);
        std::vector<std::pair<float, std::uint32_t>> found;
        const std::uint32_t capacity = Capacity(system);
        for (std::uint32_t handle = 0; handle < capacity; ++handle)
        {
            if ((Read<std::uint32_t>(Record(system, handle), kRecordFlags) & kFlagAlive) == 0)
                continue;
            const float* at = Position(system, handle);
            const float dx = at[0] - center[0];
            const float dy = at[1] - center[1];
            const float dz = at[2] - center[2];
            const float distanceSquared = dx * dx + dy * dy + dz * dz;
            if (distanceSquared <= radius * radius)
                found.emplace_back(distanceSquared, handle);
        }
        std::sort(found.begin(), found.end());
        g_lua_createtable(L, static_cast<int>(found.size()), 0);
        for (std::size_t i = 0; i < found.size(); ++i)
        {
            g_lua_pushnumber(L, static_cast<double>(i + 1));
            g_lua_pushnumber(L, found[i].second);
            g_lua_settable(L, -3);
        }
        return 1;
    }

    int l_GetPosition(lua_State* L)
    {
        std::uint8_t* system = nullptr;
        std::uint32_t handle = 0;
        if (!LiveHandleArg(L, 1, system, handle))
            return 0;
        return PushVector(L, Position(system, handle), 3);
    }

    int l_GetRotation(lua_State* L)
    {
        std::uint8_t* system = nullptr;
        std::uint32_t handle = 0;
        if (!LiveHandleArg(L, 1, system, handle))
            return 0;
        return PushVector(L, Rotation(system, handle), 4);
    }

    int l_GetInfo(lua_State* L)
    {
        std::uint8_t* system = nullptr;
        std::uint32_t handle = 0;
        if (!LiveHandleArg(L, 1, system, handle))
            return 0;
        const std::uint8_t* record = Record(system, handle);
        const std::uint32_t flags = Read<std::uint32_t>(record, kRecordFlags);
        g_lua_createtable(L, 0, 7);
        SetField(L, "equipId", static_cast<double>(RealEquipId(system, handle)));
        SetField(L, "owner", static_cast<double>(Read<std::uint16_t>(record, kRecordOwner)));
        SetField(L, "lifeLeft", static_cast<double>(Read<float>(record, kRecordLifeLeft)));
        SetField(L, "reboundCount", static_cast<double>((flags >> kReboundCountShift) & kReboundCountMask));
        SetField(L, "isActivated", (flags & kFlagActivated) != 0);
        SetField(L, "isStopped", (flags & kFlagsStopped) == kFlagsStopped);
        SetField(L, "isDone", (flags & kFlagDone) != 0);
        return 1;
    }

    int l_GetLifeLeft(lua_State* L)
    {
        std::uint8_t* system = nullptr;
        std::uint32_t handle = 0;
        if (!LiveHandleArg(L, 1, system, handle))
            return 0;
        g_lua_pushnumber(L, Read<float>(Record(system, handle), kRecordLifeLeft));
        return 1;
    }

    int l_GetHandleLatestThrownByPlayer(lua_State* L)
    {
        std::uint8_t* system = ThrowingSystem();
        if (!system)
            return 0;
        const std::uint32_t handle = Read<std::uint8_t>(system, kLatestThrownByPlayer);
        if (handle == kNoHandle)
            return 0;
        g_lua_pushnumber(L, handle);
        return 1;
    }

    int l_GetThrowingCount(lua_State* L)
    {
        std::uint8_t* system = ThrowingSystem();
        if (!system || !LuaIsNumber(L, 1) || !LuaIsNumber(L, 2))
            return 0;
        auto count = reinterpret_cast<GetThrowingCount_t>(VtableSlot(system, kGetThrowingCountSlot));
        g_lua_pushnumber(L, count(system, static_cast<std::uint32_t>(GetLuaInt(L, 1)), static_cast<std::uint32_t>(GetLuaInt(L, 2))));
        return 1;
    }

    int l_GetPredictedPositionAtTime(lua_State* L)
    {
        std::uint8_t* system = nullptr;
        std::uint32_t handle = 0;
        if (!LiveHandleArg(L, 1, system, handle) || !LuaIsNumber(L, 2))
            return 0;
        alignas(16) float predicted[4] = {};
        auto predict = reinterpret_cast<GetPredictedPositionAtTime_t>(VtableSlot(system, kGetPredictedPositionAtTimeSlot));
        if (!predict(system, static_cast<std::uint8_t>(handle), GetLuaNumber(L, 2), predicted))
            return 0;
        return PushVector(L, predicted, 3);
    }

    int l_OverwriteVelocity(lua_State* L)
    {
        std::uint8_t* system = nullptr;
        std::uint32_t handle = 0;
        float target[3] = {};
        if (InOnlineMission("V_ThrowingSystem.OverwriteVelocity") || !LiveHandleArg(L, 1, system, handle)
            || !ReadVectorAt(L, 2, target) || !LuaIsNumber(L, 3))
        {
            PushLuaBool(L, false);
            return 1;
        }
        ThrowingRequest request;
        request.kind      = ThrowingRequestKind::OverwriteVelocity;
        request.handle    = handle;
        request.equipId   = RealEquipId(system, handle);
        request.values[0] = target[0];
        request.values[1] = target[1];
        request.values[2] = target[2];
        request.values[3] = GetLuaNumber(L, 3);
        PushLuaBool(L, ThrowingSystem_QueueRequest(request));
        return 1;
    }

    int l_RequestSmoke(lua_State* L)
    {
        std::uint8_t* system = nullptr;
        std::uint32_t handle = 0;
        if (InOnlineMission("V_ThrowingSystem.RequestSmoke") || !LiveHandleArg(L, 1, system, handle))
            return 0;
        void* operation = system + kOperation;
        reinterpret_cast<RequestSmoke_t>(VtableSlot(operation, kRequestSmokeSlot))(operation, static_cast<std::uint8_t>(handle));
        return 0;
    }

    int l_Activate(lua_State* L)                    { return QueueForHandle(L, "V_ThrowingImpl.Activate", ThrowingRequestKind::Activate); }
    int l_Deactivate(lua_State* L)                  { return QueueForHandle(L, "V_ThrowingImpl.Deactivate", ThrowingRequestKind::Deactivate); }
    int l_UpdateActionSmoke(lua_State* L)           { return QueueForHandle(L, "V_ThrowingImpl.UpdateActionSmoke", ThrowingRequestKind::UpdateActionSmoke); }
    int l_UpdateActionMolotovCocktail(lua_State* L) { return QueueForHandle(L, "V_ThrowingImpl.UpdateActionMolotovCocktail", ThrowingRequestKind::UpdateActionMolotovCocktail); }
    int l_UpdateActionDecoy(lua_State* L)           { return QueueForHandle(L, "V_ThrowingImpl.UpdateActionDecoy", ThrowingRequestKind::UpdateActionDecoy); }
    int l_UpdateActionKibidango(lua_State* L)       { return QueueForHandle(L, "V_ThrowingImpl.UpdateActionKibidango", ThrowingRequestKind::UpdateActionKibidango); }
    int l_UpdateActionFakeSign(lua_State* L)        { return QueueForHandle(L, "V_ThrowingImpl.UpdateActionFakeSign", ThrowingRequestKind::UpdateActionFakeSign); }
    int l_AddReboundNoise(lua_State* L)             { return QueueForHandle(L, "V_ThrowingImpl.AddReboundNoise", ThrowingRequestKind::AddReboundNoise); }
    int l_PostReboundSound(lua_State* L)            { return QueueForHandle(L, "V_ThrowingImpl.PostReboundSound", ThrowingRequestKind::PostReboundSound); }

    int l_UpdateActionGrenade(lua_State* L)
    {
        const int supportWeaponType = LuaIsNumber(L, 2) ? GetLuaInt(L, 2) : 1;
        return QueueForHandle(L, "V_ThrowingImpl.UpdateActionGrenade", ThrowingRequestKind::UpdateActionGrenade, supportWeaponType);
    }

    int l_SetLifeLeft(lua_State* L)
    {
        if (!LuaIsNumber(L, 2))
        {
            PushLuaBool(L, false);
            return 1;
        }
        return QueueForHandle(L, "V_ThrowingImpl.SetLifeLeft", ThrowingRequestKind::SetLifeLeft, 0, GetLuaNumber(L, 2));
    }

    int l_GetVelocity(lua_State* L)
    {
        std::uint8_t* system = nullptr;
        std::uint32_t handle = 0;
        std::uint8_t* impl = nullptr;
        std::uint32_t local = 0;
        if (!LiveHandleArg(L, 1, system, handle) || !ResolveImpl(system, handle, impl, local))
            return 0;
        return PushVector(L, Read<const float*>(impl, kImplVelocities) + local * 4, 3);
    }

    int l_IsObstructSight(lua_State* L)
    {
        std::uint8_t* system = ThrowingSystem();
        std::uint8_t* impl = nullptr;
        std::uint32_t local = 0;
        auto isObstructSight = reinterpret_cast<IsObstructSight_t>(ResolveGameAddress(gAddr.ThrowingImpl_IsObstructSight));
        if (!system || !isObstructSight || !LuaIsNumber(L, 1) || !ResolveImpl(system, 0, impl, local))
            return 0;
        const std::int32_t engineId = equip::ToEngine(equip::ResolveReal(GetLuaInt(L, 1)));
        PushLuaBool(L, engineId > 0 && engineId < kEquipIdMask
                       && isObstructSight(impl, static_cast<std::uint16_t>(engineId)));
        return 1;
    }

    int QueueEffect(lua_State* L, const char* function, ThrowingRequestKind kind)
    {
        float position[3] = {};
        if (InOnlineMission(function) || !ReadVectorAt(L, 1, position))
        {
            PushLuaBool(L, false);
            return 1;
        }
        ThrowingRequest request;
        request.kind      = kind;
        request.values[0] = position[0];
        request.values[1] = position[1];
        request.values[2] = position[2];
        PushLuaBool(L, ThrowingSystem_QueueRequest(request));
        return 1;
    }

    int l_CreateEffectLanding(lua_State* L)     { return QueueEffect(L, "V_ThrowingImpl.CreateEffectLanding", ThrowingRequestKind::CreateEffectLanding); }
    int l_CreateEffectDropInWater(lua_State* L) { return QueueEffect(L, "V_ThrowingImpl.CreateEffectDropInWater", ThrowingRequestKind::CreateEffectDropInWater); }

    void* RangeAttackSystem()
    {
        std::uint8_t* gst = GameSystemTable();
        return gst ? *reinterpret_cast<void**>(gst + kRangeAttackSystem) : nullptr;
    }

    int PushRangeAttackHandle(lua_State* L, std::uint16_t handle)
    {
        if (handle == 0)
            return 0;
        g_lua_pushnumber(L, handle);
        return 1;
    }

    std::uint32_t ReadQueuedCountSafely(const void* system, std::size_t counterOffset)
    {
        __try
        {
            return *reinterpret_cast<const volatile std::uint32_t*>(
                static_cast<const std::uint8_t*>(system) + counterOffset);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return kRangeAttackQueueSize;
        }
    }

    bool RangeAttackQueueFull(const void* system, std::size_t counterOffset, const char* api)
    {
        if (ReadQueuedCountSafely(system, counterOffset) < kRangeAttackQueueSize)
            return false;
        LogWarn("%s: %u requests are already queued this frame, ignored", api, kRangeAttackQueueSize);
        return true;
    }

    int l_RequestToChaff(lua_State* L)
    {
        if (LuaType(L, 1) != LUA_TTABLE || InOnlineMission("V_RangeAttackSystem.RequestToChaff"))
            return 0;
        void* system = RangeAttackSystem();
        if (!system)
            return 0;
        ChaffRequest request{};
        if (!ReadVectorField(L, "pos", request.position))
        {
            LogWarn("V_RangeAttackSystem.RequestToChaff: pos = { x, y, z } is required, ignored");
            return 0;
        }
        int owner = 0;
        bool endless = false;
        bool noEffect = false;
        ReadIntegerField(L, "owner", owner);
        ReadNumberField(L, "duration", request.duration);
        ReadBoolField(L, "endless", endless);
        ReadBoolField(L, "noEffect", noEffect);
        if (!endless && !(request.duration > 0.0f))
        {
            LogWarn("V_RangeAttackSystem.RequestToChaff: duration must be above 0 (or endless = true), ignored");
            return 0;
        }
        if (RangeAttackQueueFull(system, kChaffQueuedCount, "V_RangeAttackSystem.RequestToChaff"))
            return 0;
        request.owner    = static_cast<std::uint16_t>(owner);
        request.endless  = endless ? 1 : 0;
        request.noEffect = noEffect ? 1 : 0;
        const auto send = reinterpret_cast<RangeAttackRequest_t>(VtableSlot(system, kRequestToChaffSlot));
        return PushRangeAttackHandle(L, send(system, &request));
    }

    int l_RequestToStun(lua_State* L)
    {
        if (LuaType(L, 1) != LUA_TTABLE || InOnlineMission("V_RangeAttackSystem.RequestToStun"))
            return 0;
        void* system = RangeAttackSystem();
        if (!system)
            return 0;
        StunRequest request{};
        if (!ReadVectorField(L, "pos", request.position))
        {
            LogWarn("V_RangeAttackSystem.RequestToStun: pos = { x, y, z } is required, ignored");
            return 0;
        }
        int owner = 0;
        int attackId = static_cast<int>(kAttackIdStunGrenade);
        int grade = 1;
        ReadIntegerField(L, "owner", owner);
        ReadNumberField(L, "radius", request.radius);
        ReadIntegerField(L, "attackId", attackId);
        ReadIntegerField(L, "grade", grade);
        request.owner    = static_cast<std::uint16_t>(owner);
        request.blastTag = kNoGameObject;
        request.attackId = static_cast<std::uint32_t>(attackId);
        request.grade    = static_cast<std::uint8_t>(std::clamp(grade, 0, kMaxGrade));
        if (RangeAttackQueueFull(system, kStunQueuedCount, "V_RangeAttackSystem.RequestToStun"))
            return 0;
        const auto send = reinterpret_cast<RangeAttackRequest_t>(VtableSlot(system, kRequestToStunSlot));
        return PushRangeAttackHandle(L, send(system, &request));
    }

    struct alignas(16) BlastDesc
    {
        const float*  position;
        const float*  capsuleEnd;        // null = a sphere
        const float*  capsuleEnd2;
        const float*  damageOrigin;      // null = position
        const float*  direction;         // null = none
        void*         ignore;
        float         radius;            // metres
        std::uint16_t blastSerial;       // 0 = the engine picks one
        std::uint16_t hitIgnoreId;
        std::uint16_t owner;
        std::uint16_t reserved;          // 0xFFFF, as the grenade sends it
        std::uint16_t attackId;
        std::uint8_t  blastFlag;         // the blast row's flag byte
        std::uint8_t  grade;
        std::uint8_t  flags;             // bit0 on land, bit4 the 1.5x shockwave sphere
        std::uint8_t  pad[0xF];
    };
    static_assert(offsetof(BlastDesc, radius) == 0x30, "BlastDesc radius");
    static_assert(offsetof(BlastDesc, hitIgnoreId) == 0x36, "BlastDesc hitIgnoreId");
    static_assert(offsetof(BlastDesc, attackId) == 0x3C, "BlastDesc attackId");
    static_assert(offsetof(BlastDesc, flags) == 0x40, "BlastDesc flags");

    struct BlastRow
    {
        std::int16_t effectRange;        // tenths of a metre
        std::int16_t optimalRange;
        std::uint8_t blastFlag;
        std::uint8_t unused;
    };

    using Blast_t = void(__fastcall*)(void* blastSystem, const BlastDesc* desc);

    constexpr std::uint8_t kBlastOnLand    = 0x01;
    constexpr std::uint8_t kBlastShockwave = 0x10;
    constexpr float        kTenths         = 0.1f;
    constexpr float        kMaxBlastRadius = 100.0f;

    void* BlastSystem()
    {
        std::uint8_t* gst = GameSystemTable();
        return gst ? *reinterpret_cast<void**>(gst + kBlastSystem) : nullptr;
    }

    bool ReadBlastRowSEH(int blastId, BlastRow& out)
    {
        __try
        {
            const void* row = EquipParameterTables::GetBlastRow(static_cast<std::uint32_t>(blastId));
            if (!row)
                return false;
            out = *static_cast<const BlastRow*>(row);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool SendBlastSEH(void* system, const BlastDesc& desc)
    {
        __try
        {
            reinterpret_cast<Blast_t>(VtableSlot(system, 0))(system, &desc);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    int l_Blast(lua_State* L)
    {
        if (LuaType(L, 1) != LUA_TTABLE || InOnlineMission("V_BlastSystem.Blast"))
            return 0;
        void* system = BlastSystem();
        if (!system)
            return 0;
        alignas(16) float position[4] = {};
        if (!ReadVectorField(L, "pos", position))
        {
            LogWarn("V_BlastSystem.Blast: pos = { x, y, z } is required, ignored");
            return 0;
        }
        int blastId = -1;
        int attackId = 0;
        ReadIntegerField(L, "blastId", blastId);
        ReadIntegerField(L, "attackId", attackId);
        BlastRow row{};
        if (blastId < 0 || blastId >= static_cast<int>(EquipParameterTables::kBlastRowCount) || !ReadBlastRowSEH(blastId, row))
        {
            LogWarn("V_BlastSystem.Blast: blastId %d is not a blast row (TppEquip.BLA_*), ignored", blastId);
            return 0;
        }
        if (attackId <= 0 || attackId > 0xFFFF)
        {
            LogWarn("V_BlastSystem.Blast: attackId %d is required (TppDamage.ATK_*), ignored", attackId);
            return 0;
        }
        float radius = static_cast<float>(row.optimalRange) * kTenths;
        ReadNumberField(L, "radius", radius);
        if (!(radius > 0.0f) || radius > kMaxBlastRadius)
        {
            LogWarn("V_BlastSystem.Blast: radius %.2f must be above 0 and at most %.0f, ignored", radius, kMaxBlastRadius);
            return 0;
        }
        int owner = 0;
        int hitIgnoreId = kNoGameObject;
        int grade = 0;
        bool shockwave = true;
        ReadIntegerField(L, "owner", owner);
        ReadIntegerField(L, "hitIgnoreId", hitIgnoreId);
        ReadIntegerField(L, "grade", grade);
        ReadBoolField(L, "shockwave", shockwave);

        BlastDesc desc{};
        desc.position    = position;
        desc.radius      = radius;
        desc.hitIgnoreId = static_cast<std::uint16_t>(hitIgnoreId);
        desc.owner       = static_cast<std::uint16_t>(owner);
        desc.reserved    = kNoGameObject;
        desc.attackId    = static_cast<std::uint16_t>(attackId);
        desc.blastFlag   = row.blastFlag;
        desc.grade       = static_cast<std::uint8_t>(std::clamp(grade, 0, kMaxGrade));
        desc.flags       = static_cast<std::uint8_t>(kBlastOnLand | (shockwave ? kBlastShockwave : 0));
        PushLuaBool(L, SendBlastSEH(system, desc));
        return 1;
    }

    struct alignas(16) NoiseDesc
    {
        float         position[4];
        std::uint8_t  type;
        std::uint8_t  flags;             // 1, as the throwing rebound sends it
        std::uint16_t owner;
        std::uint8_t  pad[12];
    };
    static_assert(offsetof(NoiseDesc, type) == 0x10, "NoiseDesc type");
    static_assert(offsetof(NoiseDesc, owner) == 0x12, "NoiseDesc owner");

    using AddNoise_t = void(__fastcall*)(void* noiseSystem, const NoiseDesc* desc);
    using GetQuarkSystemTable_t = std::uint8_t*(__fastcall*)();

    constexpr std::size_t  kQuarkNoiseSystems = 0xA8;    // QuarkSystemTable -> table, +8 = fox::gs::NoiseSystem
    constexpr std::size_t  kNoiseSystem       = 0x08;
    constexpr int          kMaxNoiseType      = 0x7F;

    void* NoiseSystemSEH()
    {
        __try
        {
            auto getQuark = reinterpret_cast<GetQuarkSystemTable_t>(ResolveGameAddress(gAddr.GetQuarkSystemTable));
            std::uint8_t* quark = getQuark ? getQuark() : nullptr;
            std::uint8_t* systems = quark ? *reinterpret_cast<std::uint8_t**>(quark + kQuarkNoiseSystems) : nullptr;
            return systems ? *reinterpret_cast<void**>(systems + kNoiseSystem) : nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    bool SendNoiseSEH(void* system, const NoiseDesc& desc)
    {
        __try
        {
            reinterpret_cast<AddNoise_t>(VtableSlot(system, 0))(system, &desc);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    int l_AddNoise(lua_State* L)
    {
        if (LuaType(L, 1) != LUA_TTABLE || InOnlineMission("V_NoiseSystem.AddNoise"))
            return 0;
        void* system = NoiseSystemSEH();
        if (!system)
            return 0;
        NoiseDesc desc{};
        if (!ReadVectorField(L, "pos", desc.position))
        {
            LogWarn("V_NoiseSystem.AddNoise: pos = { x, y, z } is required, ignored");
            return 0;
        }
        int type = -1;
        int owner = 0;
        ReadIntegerField(L, "type", type);
        ReadIntegerField(L, "owner", owner);
        if (type < 0 || type > kMaxNoiseType)
        {
            LogWarn("V_NoiseSystem.AddNoise: type %d must be 0-%d, ignored", type, kMaxNoiseType);
            return 0;
        }
        desc.type  = static_cast<std::uint8_t>(type);
        desc.flags = 1;
        desc.owner = static_cast<std::uint16_t>(owner);
        PushLuaBool(L, SendNoiseSEH(system, desc));
        return 1;
    }

    bool ReadSmokeType(lua_State* L, std::uint32_t& out)
    {
        LuaGetField(L, 1, "type");
        bool ok = true;
        if (LuaType(L, -1) == LUA_TSTRING)
        {
            const char* name = GetLuaString(L, -1);
            if (name && _stricmp(name, "Smoke") == 0)
                out = 0;
            else if (name && _stricmp(name, "SleepGas") == 0)
                out = 3;
            else
                ok = false;
        }
        else if (LuaIsNumber(L, -1))
        {
            const int value = GetLuaInt(L, -1);
            ok = value >= 0 && static_cast<std::uint32_t>(value) <= kMaxSmokeType;
            if (ok)
                out = static_cast<std::uint32_t>(value);
        }
        LuaPop(L, 1);
        return ok;
    }

    int l_RequestToSmoke(lua_State* L)
    {
        if (LuaType(L, 1) != LUA_TTABLE || InOnlineMission("V_RangeAttackSystem.RequestToSmoke"))
            return 0;
        void* system = RangeAttackSystem();
        if (!system)
            return 0;
        SmokeRequest request{};
        if (!ReadVectorField(L, "pos", request.position))
        {
            LogWarn("V_RangeAttackSystem.RequestToSmoke: pos = { x, y, z } is required, ignored");
            return 0;
        }
        if (!ReadNumberField(L, "radius", request.radius) || !(request.radius > 0.0f)
            || !ReadNumberField(L, "duration", request.duration) || !(request.duration > 0.0f))
        {
            LogWarn("V_RangeAttackSystem.RequestToSmoke: radius and duration above 0 are required, ignored");
            return 0;
        }
        if (!ReadSmokeType(L, request.type))
        {
            LogWarn("V_RangeAttackSystem.RequestToSmoke: type must be \"Smoke\", \"SleepGas\" or 0-%u, ignored", kMaxSmokeType);
            return 0;
        }
        int owner = 0;
        ReadIntegerField(L, "owner", owner);
        if (RangeAttackQueueFull(system, kSmokeQueuedCount, "V_RangeAttackSystem.RequestToSmoke"))
            return 0;
        request.owner = static_cast<std::uint16_t>(owner);
        const auto send = reinterpret_cast<RangeAttackRequest_t>(VtableSlot(system, kRequestToSmokeSlot));
        return PushRangeAttackHandle(L, send(system, &request));
    }

    int l_RequestToFlame(lua_State* L)
    {
        if (LuaType(L, 1) != LUA_TTABLE || InOnlineMission("V_RangeAttackSystem.RequestToFlame"))
            return 0;
        void* system = RangeAttackSystem();
        if (!system)
            return 0;
        FlameRequest request{};
        if (!ReadVectorField(L, "pos", request.position))
        {
            LogWarn("V_RangeAttackSystem.RequestToFlame: pos = { x, y, z } is required, ignored");
            return 0;
        }
        request.duration = kMolotovFireDuration;
        request.radius   = kMolotovFireRadius;
        ReadNumberField(L, "duration", request.duration);
        ReadNumberField(L, "radius", request.radius);
        if (!(request.duration > 0.0f) || !(request.radius > 0.0f))
        {
            LogWarn("V_RangeAttackSystem.RequestToFlame: radius and duration must be above 0, ignored");
            return 0;
        }
        int owner = 0;
        int attackId = static_cast<int>(kAttackIdMolotovFire);
        int grade = 0;
        ReadIntegerField(L, "owner", owner);
        ReadIntegerField(L, "attackId", attackId);
        ReadIntegerField(L, "grade", grade);
        if (RangeAttackQueueFull(system, kFlameQueuedCount, "V_RangeAttackSystem.RequestToFlame"))
            return 0;
        request.owner    = static_cast<std::uint16_t>(owner);
        request.attackId = static_cast<std::uint32_t>(attackId);
        request.grade    = static_cast<std::uint8_t>(std::clamp(grade, 0, kMaxGrade));
        for (int i = 0; i < 4; ++i)
            request.look[i] = kMolotovFireLook[i];
        const auto send = reinterpret_cast<RangeAttackRequest_t>(VtableSlot(system, kRequestToFlameSlot));
        return PushRangeAttackHandle(L, send(system, &request));
    }

    int l_Stop(lua_State* L)                        { return QueueForHandle(L, "V_ThrowingImpl.Stop", ThrowingRequestKind::Stop); }

    luaL_Reg g_ThrowingSystemLib[] =
    {
        { "Throw",                          l_Throw },
        { "Search",                         l_Search },
        { "GetPosition",                    l_GetPosition },
        { "GetRotation",                    l_GetRotation },
        { "GetInfo",                        l_GetInfo },
        { "GetLifeLeft",                    l_GetLifeLeft },
        { "GetHandleLatestThrownByPlayer",  l_GetHandleLatestThrownByPlayer },
        { "GetThrowingCount",               l_GetThrowingCount },
        { "GetPredictedPositionAtTime",     l_GetPredictedPositionAtTime },
        { "OverwriteVelocity",              l_OverwriteVelocity },
        { "RequestSmoke",                   l_RequestSmoke },
        { nullptr, nullptr }
    };

    luaL_Reg g_ThrowingImplLib[] =
    {
        { "Activate",                       l_Activate },
        { "Deactivate",                     l_Deactivate },
        { "SetLifeLeft",                    l_SetLifeLeft },
        { "Stop",                           l_Stop },
        { "GetVelocity",                    l_GetVelocity },
        { "UpdateActionGrenade",            l_UpdateActionGrenade },
        { "UpdateActionSmoke",              l_UpdateActionSmoke },
        { "UpdateActionMolotovCocktail",    l_UpdateActionMolotovCocktail },
        { "UpdateActionDecoy",              l_UpdateActionDecoy },
        { "UpdateActionKibidango",          l_UpdateActionKibidango },
        { "UpdateActionFakeSign",           l_UpdateActionFakeSign },
        { "AddReboundNoise",                l_AddReboundNoise },
        { "PostReboundSound",               l_PostReboundSound },
        { "IsObstructSight",                l_IsObstructSight },
        { "CreateEffectLanding",            l_CreateEffectLanding },
        { "CreateEffectDropInWater",        l_CreateEffectDropInWater },
        { nullptr, nullptr }
    };

    luaL_Reg g_RangeAttackSystemLib[] =
    {
        { "RequestToChaff",                 l_RequestToChaff },
        { "RequestToStun",                  l_RequestToStun },
        { "RequestToSmoke",                 l_RequestToSmoke },
        { "RequestToFlame",                 l_RequestToFlame },
        { nullptr, nullptr }
    };

    luaL_Reg g_BlastSystemLib[] =
    {
        { "Blast",                          l_Blast },
        { nullptr, nullptr }
    };

    luaL_Reg g_NoiseSystemLib[] =
    {
        { "AddNoise",                       l_AddNoise },
        { nullptr, nullptr }
    };
}

bool Register_V_ThrowingSystemLibrary(lua_State* L)
{
    bool ok = RegisterLuaLibrary(L, "V_ThrowingSystem", g_ThrowingSystemLib);
    ok = RegisterLuaLibrary(L, "V_ThrowingImpl", g_ThrowingImplLib) && ok;
    ok = RegisterLuaLibrary(L, "V_RangeAttackSystem", g_RangeAttackSystemLib) && ok;
    ok = RegisterLuaLibrary(L, "V_BlastSystem", g_BlastSystemLib) && ok;
    ok = RegisterLuaLibrary(L, "V_NoiseSystem", g_NoiseSystemLib) && ok;
    return ok;
}
