#include <pch.h>
#include "GameObjectReinitialize.h"

#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "../../../core/AddressSet.h"
#include "../../../core/FoxHashes.h"
#include "../../../core/HookUtils.h"
#include "../../../core/MissionCodeGuard.h"
#include "../../../core/log.h"
#include "../../../lua/LuaApi.h"

namespace
{
    using GameObjectReinitialize::LuaCommand_t;
    using GameObjectReinitialize::Result;

    using GetQuarkSystemTable_t = std::uint8_t*(__fastcall*)();
    using RestoreMarker_t       = void(__fastcall*)(void* markerSystem, std::uint8_t* marker, std::uint32_t savedMarker);

    constexpr std::uint32_t kEveryIndex = 0x1FFu;

    constexpr std::size_t kHandlerCount = 0x08;   // u32, cameras, UAVs, soldiers, hostages and Sahelanthropus
    constexpr std::size_t kLifeRecords  = 0x08;

    constexpr std::size_t kSecurityLifeController = 0x28;
    constexpr std::size_t kCameraMarkers          = 0xA0;
    constexpr std::size_t kCameraLifeSize         = 0x0C;
    constexpr std::size_t kUavMarkers             = 0xA8;
    constexpr std::size_t kUavLifeSize            = 0x10;

    constexpr std::size_t kVehicleMarkers = 0x1B8;
    constexpr std::size_t kVehicleInUse   = 0x1C8;   // -> +8 u32 bits, one per vehicle
    constexpr std::size_t kInUseBits      = 0x08;
    constexpr std::size_t kVehicleCount   = 0x1E0;   // u16

    constexpr std::size_t kHeliCount   = 0x0C;   // u32
    constexpr std::size_t kHeliMarkers = 0x68;

    constexpr std::size_t kPersonLifeController = 0x38;

    constexpr std::size_t  kWalkerCount          = 0x0C;   // u32
    constexpr std::size_t  kWalkerLifeController = 0x38;
    constexpr std::size_t  kWalkerLifeSize       = 0x3C;
    constexpr std::size_t  kWalkerMarkers        = 0x98;
    constexpr std::size_t  kWalkerLife           = 0x00;   // u32
    constexpr std::size_t  kWalkerMaxLife        = 0x04;   // u32
    constexpr std::size_t  kWalkerPendingDamage  = 0x08;   // u16, taken off the life on its next update
    constexpr std::size_t  kWalkerBroken         = 0x2E;
    constexpr std::uint8_t kWalkerDestroyed      = 1;

    constexpr std::size_t kSahelanLifeController = 0x20;
    constexpr std::size_t kSahelanLifeSize       = 0x2E0;
    constexpr std::size_t kSahelanMarkers        = 0xF8;
    constexpr std::size_t kSahelanBodyLife       = 0x00;   // u32
    constexpr std::size_t kSahelanBodyMaxLife    = 0x04;   // u32
    constexpr std::size_t kSahelanPartLives      = 0x10;   // u32 per part
    constexpr std::size_t kSahelanPartMaxLives   = 0x54;   // u32 per part
    constexpr std::size_t kSahelanPartCount      = 17;

    struct PersonLife
    {
        std::size_t recordSize;
        std::size_t life;
        std::size_t maxLife;
        std::size_t stamina;
        std::size_t maxStamina;
        std::size_t markers;   // offset on the command handler
    };

    constexpr PersonLife kSoldierLife = { 0x40, 0x1A, 0x1C, 0x20, 0x22, 0xD8 };
    constexpr PersonLife kHostageLife = { 0x38, 0x0C, 0x0E, 0x14, 0x16, 0xD0 };

    constexpr double kNoAnswer      = -1.0;
    constexpr double kStateDisabled = 0.0;   // GetStatus: 0 disabled (fultoned, vanished), 1 normal, 2-3 held up, 4 carried
    constexpr double kStateCarried  = 4.0;
    constexpr double kLifeDead      = 1.0;   // GetLifeStatus: 0 normal, 1 dead, 2 dying, 3 asleep, 4 fainted
    constexpr double kLifeDying     = 2.0;

    constexpr std::size_t   kQuarkGameSystems  = 0x98;
    constexpr std::size_t   kGameMarkerSystem  = 0x80;
    constexpr std::size_t   kRestoreMarkerSlot = 0x120;   // puts a marker back to a saved svars value
    constexpr std::size_t   kMarkerRecords     = 0x10;
    constexpr std::size_t   kMarkerFirst       = 0x18;    // u32, the type's first record in the marker table
    constexpr std::size_t   kMarkerSize        = 0x30;
    constexpr std::uint32_t kNoSavedMarker     = 0;

    struct Range
    {
        std::uint32_t first = 0;
        std::uint32_t end   = 0;
        bool          every = false;
    };

    struct MarkerSystem
    {
        void*           object  = nullptr;
        RestoreMarker_t restore = nullptr;
    };

    thread_local bool   t_Armed  = false;
    thread_local Result t_Result = Result::NotReached;

    template <typename T>
    T& At(std::uint8_t* base, std::size_t offset)
    {
        return *reinterpret_cast<T*>(base + offset);
    }

    void* VirtualOf(void* object, std::size_t slot)
    {
        return (*reinterpret_cast<void* const* const*>(object))[slot / sizeof(void*)];
    }

    std::uint32_t Command(const char* name)
    {
        return FoxHashes::StrCode32(name);
    }

    bool RangeOf(std::uint32_t index, std::uint32_t count, Range& range)
    {
        if (index == kEveryIndex)
        {
            range.first = 0;
            range.end   = count;
            range.every = true;
            return true;
        }
        if (index >= count)
            return false;
        range.first = index;
        range.end   = index + 1;
        return true;
    }

    bool CountSeh(std::uint8_t* handler, std::size_t offset, std::uint32_t& count)
    {
        __try
        {
            count = At<std::uint32_t>(handler, offset);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    GetQuarkSystemTable_t QuarkSystemTableGetter()
    {
        static const GetQuarkSystemTable_t getter = gAddr.GetQuarkSystemTable
            ? reinterpret_cast<GetQuarkSystemTable_t>(ResolveGameAddress(gAddr.GetQuarkSystemTable))
            : nullptr;
        return getter;
    }

    bool MarkerSystemSeh(GetQuarkSystemTable_t getQuarkSystemTable, MarkerSystem& markerSystem)
    {
        __try
        {
            std::uint8_t* quark = getQuarkSystemTable();
            std::uint8_t* gameSystems = quark ? At<std::uint8_t*>(quark, kQuarkGameSystems) : nullptr;
            void* object = gameSystems ? At<void*>(gameSystems, kGameMarkerSystem) : nullptr;
            if (!object)
                return false;
            markerSystem.object = object;
            markerSystem.restore = reinterpret_cast<RestoreMarker_t>(VirtualOf(object, kRestoreMarkerSlot));
            return markerSystem.restore != nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool MarkerRecordSeh(std::uint8_t* markers, std::uint32_t index, std::uint8_t*& marker)
    {
        __try
        {
            std::uint8_t* records = At<std::uint8_t*>(markers, kMarkerRecords);
            const std::size_t first = At<std::uint32_t>(markers, kMarkerFirst);
            marker = records + (first + index) * kMarkerSize;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void WriteFreshCameraLife(std::uint8_t* life)
    {
        At<std::uint64_t>(life, 0x0) = 0x12C012Cull;   // life 300 of 300
        At<std::uint16_t>(life, 0x8) = 0;              // 1-3 = the ways it can be broken
        At<std::uint8_t>(life, 0xB)  = 0;              // broken flag
    }

    void WriteFreshUavLife(std::uint8_t* life)
    {
        At<std::uint32_t>(life, 0x0) = 0;
        At<std::uint32_t>(life, 0x4) = 0x08FC08FCu;   // life 2300 of 2300
        At<std::uint32_t>(life, 0x8) = 0xFFFF0000u;   // what a fresh pool holds (real meaning unknown)
        At<std::uint16_t>(life, 0xC) = 0;             // state byte: 0 alive, 1 falling, 2-3 gone
    }

    bool CameraLifeSeh(std::uint8_t* handler, std::uint32_t index, std::uint8_t*& markers)
    {
        __try
        {
            std::uint8_t* lifeController = At<std::uint8_t*>(handler, kSecurityLifeController);
            std::uint8_t* records = At<std::uint8_t*>(lifeController, kLifeRecords);
            WriteFreshCameraLife(records + static_cast<std::size_t>(index) * kCameraLifeSize);
            markers = At<std::uint8_t*>(handler, kCameraMarkers);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool UavLifeSeh(std::uint8_t* handler, std::uint32_t index, std::uint8_t*& markers)
    {
        __try
        {
            std::uint8_t* lifeController = At<std::uint8_t*>(handler, kSecurityLifeController);
            std::uint8_t* records = At<std::uint8_t*>(lifeController, kLifeRecords);
            WriteFreshUavLife(records + static_cast<std::size_t>(index) * kUavLifeSize);
            markers = At<std::uint8_t*>(handler, kUavMarkers);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool VehicleStateSeh(std::uint8_t* vehicles, std::uint32_t index, bool& spawned, std::uint8_t*& markers)
    {
        __try
        {
            std::uint8_t* inUse = At<std::uint8_t*>(vehicles, kVehicleInUse);
            const std::uint32_t* bits = At<const std::uint32_t*>(inUse, kInUseBits);
            spawned = (bits[index >> 5] >> (index & 0x1Fu)) & 1u;
            markers = At<std::uint8_t*>(vehicles, kVehicleMarkers);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool VehicleCountSeh(std::uint8_t* vehicles, std::uint32_t& count)
    {
        __try
        {
            count = At<std::uint16_t>(vehicles, kVehicleCount);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool HeliStateSeh(std::uint8_t* handler, std::uint32_t& count, std::uint8_t*& markers)
    {
        __try
        {
            count   = At<std::uint32_t>(handler, kHeliCount);
            markers = At<std::uint8_t*>(handler, kHeliMarkers);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool FillPersonLifeSeh(std::uint8_t* handler, std::uint32_t index, const PersonLife& layout, std::uint8_t*& markers)
    {
        __try
        {
            std::uint8_t* lifeController = At<std::uint8_t*>(handler, kPersonLifeController);
            std::uint8_t* records = At<std::uint8_t*>(lifeController, kLifeRecords);
            std::uint8_t* life = records + static_cast<std::size_t>(index) * layout.recordSize;
            At<std::uint16_t>(life, layout.life)    = At<std::uint16_t>(life, layout.maxLife);
            At<std::uint16_t>(life, layout.stamina) = At<std::uint16_t>(life, layout.maxStamina);
            markers = At<std::uint8_t*>(handler, layout.markers);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool WalkerLifeSeh(std::uint8_t* handler, std::uint32_t index, bool& intact, std::uint8_t*& markers)
    {
        __try
        {
            std::uint8_t* lifeController = At<std::uint8_t*>(handler, kWalkerLifeController);
            std::uint8_t* records = At<std::uint8_t*>(lifeController, kLifeRecords);
            std::uint8_t* life = records + static_cast<std::size_t>(index) * kWalkerLifeSize;
            intact = At<std::uint8_t>(life, kWalkerBroken) != kWalkerDestroyed;
            if (intact)
            {
                At<std::uint32_t>(life, kWalkerLife)          = At<std::uint32_t>(life, kWalkerMaxLife);
                At<std::uint16_t>(life, kWalkerPendingDamage) = 0;
            }
            markers = At<std::uint8_t*>(handler, kWalkerMarkers);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool SahelanLifeSeh(std::uint8_t* handler, std::uint32_t index, bool& standing, std::uint8_t*& markers)
    {
        __try
        {
            std::uint8_t* lifeController = At<std::uint8_t*>(handler, kSahelanLifeController);
            std::uint8_t* records = At<std::uint8_t*>(lifeController, kLifeRecords);
            std::uint8_t* life = records + static_cast<std::size_t>(index) * kSahelanLifeSize;
            standing = At<std::uint32_t>(life, kSahelanBodyLife) > 0;
            if (standing)
            {
                At<std::uint32_t>(life, kSahelanBodyLife) = At<std::uint32_t>(life, kSahelanBodyMaxLife);
                for (std::size_t part = 0; part < kSahelanPartCount; ++part)
                {
                    const std::size_t offset = part * sizeof(std::uint32_t);
                    At<std::uint32_t>(life, kSahelanPartLives + offset) = At<std::uint32_t>(life, kSahelanPartMaxLives + offset);
                }
            }
            markers = At<std::uint8_t*>(handler, kSahelanMarkers);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    double AskNumber(std::uint8_t* handler, lua_State* L, std::uint32_t index, LuaCommand_t original, std::uint32_t command)
    {
        const int top = g_lua_gettop(L);
        original(handler, L, index, command);
        double answer = kNoAnswer;
        if (g_lua_gettop(L) > top && g_lua_type(L, -1) == LUA_TNUMBER)
            answer = static_cast<double>(g_lua_tonumber(L, -1));
        g_lua_settop(L, top);
        return answer;
    }

    void RunCommand(std::uint8_t* handler, lua_State* L, std::uint32_t index, LuaCommand_t original, std::uint32_t command)
    {
        const int top = g_lua_gettop(L);
        original(handler, L, index, command);
        g_lua_settop(L, top);
    }

    void SetCommandIndex(lua_State* L, std::uint32_t index)
    {
        g_lua_pushstring(L, const_cast<char*>("index"));
        g_lua_pushnumber(L, static_cast<lua_Number>(index));
        g_lua_rawset(L, 1);
    }

    void RemoveCommandIndex(lua_State* L)
    {
        g_lua_pushstring(L, const_cast<char*>("index"));
        g_lua_pushnil(L);
        g_lua_rawset(L, 1);
    }

    bool RespawnVehicle(std::uint8_t* vehicles, lua_State* L, std::uint32_t index, LuaCommand_t original)
    {
        static const std::uint32_t kRespawn = Command("Respawn");
        if (g_lua_type(L, 1) != LUA_TTABLE)
            return false;
        const int top = g_lua_gettop(L);
        SetCommandIndex(L, index);
        original(vehicles, L, index, kRespawn);
        const bool respawned = g_lua_gettop(L) > top && g_lua_type(L, -1) == LUA_TBOOLEAN && g_lua_toboolean(L, -1) != 0;
        g_lua_settop(L, top);
        RemoveCommandIndex(L);
        return respawned;
    }

    Result ReinitializePerson(std::uint8_t* handler, lua_State* L, std::uint32_t index, LuaCommand_t original, const PersonLife& layout)
    {
        static const std::uint32_t kGetStatus       = Command("GetStatus");
        static const std::uint32_t kGetLifeStatus   = Command("GetLifeStatus");
        static const std::uint32_t kRecoveryStamina = Command("RecoveryStamina");

        const double state = AskNumber(handler, L, index, original, kGetStatus);
        const double life  = AskNumber(handler, L, index, original, kGetLifeStatus);
        if (state == kNoAnswer || life == kNoAnswer)
            return Result::Fault;
        if (state == kStateDisabled || state == kStateCarried || life == kLifeDead || life == kLifeDying)
            return Result::NotAlive;

        RunCommand(handler, L, index, original, kRecoveryStamina);
        std::uint8_t* markers = nullptr;
        if (!FillPersonLifeSeh(handler, index, layout, markers))
            return Result::Fault;
        if (markers && !GameObjectReinitialize::ClearMarker(markers, index))
            return Result::Fault;
        return Result::Done;
    }

    Result ReinitializePeople(std::uint8_t* handler, lua_State* L, std::uint32_t index, LuaCommand_t original, const PersonLife& layout)
    {
        std::uint32_t count = 0;
        Range range;
        if (!CountSeh(handler, kHandlerCount, count))
            return Result::Fault;
        if (!RangeOf(index, count, range))
            return Result::PastInstanceCount;
        for (std::uint32_t i = range.first; i < range.end; ++i)
        {
            const Result result = ReinitializePerson(handler, L, i, original, layout);
            if (result == Result::Fault || (!range.every && result != Result::Done))
                return result;
        }
        return Result::Done;
    }
}

namespace GameObjectReinitialize
{
    void Arm()
    {
        t_Armed  = true;
        t_Result = Result::NotReached;
    }

    Result Take()
    {
        const Result result = t_Result;
        t_Armed  = false;
        t_Result = Result::NotReached;
        return result;
    }

    bool Claims(std::uint32_t command)
    {
        static const std::uint32_t kReinitialize = Command("Reinitialize");
        if (!t_Armed || command != kReinitialize || MissionCodeGuard::ShouldBypassHooks())
            return false;
        t_Armed = false;
        return true;
    }

    void Report(Result result)
    {
        t_Result = result;
    }

    bool ClearMarker(std::uint8_t* markers, std::uint32_t index)
    {
        const GetQuarkSystemTable_t getQuarkSystemTable = QuarkSystemTableGetter();
        MarkerSystem markerSystem;
        std::uint8_t* marker = nullptr;
        if (!getQuarkSystemTable || !MarkerSystemSeh(getQuarkSystemTable, markerSystem))
            return false;
        if (!MarkerRecordSeh(markers, index, marker))
            return false;
        markerSystem.restore(markerSystem.object, marker, kNoSavedMarker);
        return true;
    }

    Result SecurityCameras(std::uint8_t* handler, std::uint32_t index)
    {
        std::uint32_t count = 0;
        Range range;
        if (!CountSeh(handler, kHandlerCount, count))
            return Result::Fault;
        if (!RangeOf(index, count, range))
            return Result::PastInstanceCount;
        for (std::uint32_t i = range.first; i < range.end; ++i)
        {
            std::uint8_t* markers = nullptr;
            if (!CameraLifeSeh(handler, i, markers))
                return Result::Fault;
            if (markers && !ClearMarker(markers, i))
                return Result::Fault;
        }
        return Result::Done;
    }

    Result Uavs(std::uint8_t* handler, std::uint32_t index)
    {
        std::uint32_t count = 0;
        Range range;
        if (!CountSeh(handler, kHandlerCount, count))
            return Result::Fault;
        if (!RangeOf(index, count, range))
            return Result::PastInstanceCount;
        for (std::uint32_t i = range.first; i < range.end; ++i)
        {
            std::uint8_t* markers = nullptr;
            if (!UavLifeSeh(handler, i, markers))
                return Result::Fault;
            if (markers && !ClearMarker(markers, i))
                return Result::Fault;
        }
        return Result::Done;
    }

    Result Vehicles(std::uint8_t* vehicles, void* lua, std::uint32_t index, LuaCommand_t original)
    {
        lua_State* L = static_cast<lua_State*>(lua);
        std::uint32_t count = 0;
        Range range;
        if (!VehicleCountSeh(vehicles, count))
            return Result::Fault;
        if (!RangeOf(index, count, range))
            return Result::PastInstanceCount;
        for (std::uint32_t i = range.first; i < range.end; ++i)
        {
            bool spawned = false;
            std::uint8_t* markers = nullptr;
            if (!VehicleStateSeh(vehicles, i, spawned, markers))
                return Result::Fault;
            if (!spawned)
            {
                if (range.every)
                    continue;
                return Result::NotSpawned;
            }
            if (!RespawnVehicle(vehicles, L, i, original))
                return Result::Fault;
            if (markers && !ClearMarker(markers, i))
                return Result::Fault;
        }
        return Result::Done;
    }

    Result Helis(std::uint8_t* handler, void* lua, std::uint32_t index, LuaCommand_t original)
    {
        static const std::uint32_t kRecover = Command("Recover");
        std::uint32_t count = 0;
        std::uint8_t* markers = nullptr;
        Range range;
        if (!HeliStateSeh(handler, count, markers))
            return Result::Fault;
        if (!RangeOf(index, count, range))
            return Result::PastInstanceCount;
        for (std::uint32_t i = range.first; i < range.end; ++i)
        {
            original(handler, lua, i, kRecover);
            if (markers && !ClearMarker(markers, i))
                return Result::Fault;
        }
        return Result::Done;
    }

    Result Soldiers(std::uint8_t* handler, void* lua, std::uint32_t index, LuaCommand_t original)
    {
        return ReinitializePeople(handler, static_cast<lua_State*>(lua), index, original, kSoldierLife);
    }

    Result Hostages(std::uint8_t* handler, void* lua, std::uint32_t index, LuaCommand_t original)
    {
        return ReinitializePeople(handler, static_cast<lua_State*>(lua), index, original, kHostageLife);
    }

    Result WalkerGears(std::uint8_t* handler, std::uint32_t index)
    {
        std::uint32_t count = 0;
        Range range;
        if (!CountSeh(handler, kWalkerCount, count))
            return Result::Fault;
        if (!RangeOf(index, count, range))
            return Result::PastInstanceCount;
        for (std::uint32_t i = range.first; i < range.end; ++i)
        {
            bool intact = false;
            std::uint8_t* markers = nullptr;
            if (!WalkerLifeSeh(handler, i, intact, markers))
                return Result::Fault;
            if (!intact)
            {
                if (range.every)
                    continue;
                return Result::NotAlive;
            }
            if (markers && !ClearMarker(markers, i))
                return Result::Fault;
        }
        return Result::Done;
    }

    Result Sahelans(std::uint8_t* handler, std::uint32_t index)
    {
        std::uint32_t count = 0;
        Range range;
        if (!CountSeh(handler, kHandlerCount, count))
            return Result::Fault;
        if (!RangeOf(index, count, range))
            return Result::PastInstanceCount;
        for (std::uint32_t i = range.first; i < range.end; ++i)
        {
            bool standing = false;
            std::uint8_t* markers = nullptr;
            if (!SahelanLifeSeh(handler, i, standing, markers))
                return Result::Fault;
            if (!standing)
            {
                if (range.every)
                    continue;
                return Result::NotAlive;
            }
            if (markers && !ClearMarker(markers, i))
                return Result::Fault;
        }
        return Result::Done;
    }

    void* InstallHandlerHook(const char* handlerName, std::uintptr_t address, const std::uint8_t* entry, const std::uint8_t* mask,
                             std::size_t entrySize, void* detour, void** original)
    {
        if (!address)
        {
            LogWarn("Reinitialize: %s unknown for %s, its type can't be reinitialized", handlerName, GetGameBuildName(gGameBuild));
            return nullptr;
        }
        void* target = ResolveGameAddress(address);
        const HookEntry state = mask ? CheckHookEntryMasked(target, entry, mask, entrySize) : CheckHookEntry(target, entry, entrySize);
        if (state == HookEntry::Unknown)
        {
            char described[160] = {};
            DescribeFunctionEntry(target, described, sizeof(described));
            LogError("Reinitialize: %s differs on %s (%s), its type can't be reinitialized", handlerName, GetGameBuildName(gGameBuild), described);
            return nullptr;
        }
        if (state == HookEntry::ForeignHook)
        {
            char what[96] = {};
            sprintf_s(what, "Reinitialize: %s", handlerName);
            LogHookedOnTop(what, target);
        }
        if (!CreateAndEnableHook(target, detour, original))
        {
            LogError("Reinitialize: %s hook failed at %p, its type can't be reinitialized", handlerName, target);
            *original = nullptr;
            return nullptr;
        }
        return target;
    }

    void UninstallHandlerHook(void*& target)
    {
        if (target)
            DisableAndRemoveHook(target);
        target = nullptr;
    }
}
