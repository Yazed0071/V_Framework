#include <pch.h>
#include "CustomVehicles.h"

#include <Windows.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <random>
#include <string>
#include <vector>

#include "AddressSet.h"
#include "FoxHashes.h"
#include "FoxPath.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "V_FrameWorkState.h"
#include "log.h"
#include "../../lua/LuaApi.h"

namespace
{
    using CustomVehicles::kFirstType;
    using CustomVehicles::kLastType;
    using CustomVehicles::SpawnEdit;

    using ConvertIntoReliefType_t = std::uint32_t(__fastcall*)(std::uint8_t typeCode);
    using ConvertParameterID_t    = std::uint32_t(__cdecl*)(const char* name);

    constexpr std::size_t  kVehicleCount      = kLastType - kFirstType + 1;
    constexpr std::uint8_t kLastGameType      = 8;
    constexpr std::size_t  kMostAttachments   = 7;    // an attachment sits on sub type 1 to 7
    constexpr std::size_t  kMostGears         = 6;
    constexpr std::size_t  kDefenseValueCount = 4;
    constexpr std::size_t  kMostNameLength    = 48;
    constexpr std::size_t  kReliefTypeCount   = 10;   // the vehicles the player can deploy with, relief type 1 to 10
    constexpr float        kRadPerSecPerRpm   = 0.10471976f;
    constexpr std::uint64_t kLangIdMask       = 0xFFFFFFFFFFFF;   // lang ids are the low 48 bits of StrCode64
    constexpr const char*  kTypeSpace         = "VehicleType";
    constexpr std::uint8_t kResourceOfRelief[kReliefTypeCount + 1] = { 0xFF, 0x0E, 0x0D, 0x10, 0x0F, 0x12, 0x14, 0x11, 0x13, 0x16, 0x15 };   // relief type 1 to 10 -> its Mother Base resource id

    constexpr std::size_t kSystemBodies       = 0xB8;    // one body pointer per vehicle type
    constexpr std::size_t kBodyGearRatios     = 0x118;   // reverse first, then first gear and up
    constexpr std::size_t kBodySpeedStop      = 0x140;   // the Vehicle.speed steps as floats: four backward ones below it, four forward ones above
    constexpr std::size_t kBodyFinalGearRatio = 0x154;
    constexpr std::size_t kBodyShiftDownSpeed = 0x158;   // engine speeds in rad/s
    constexpr std::size_t kBodyShiftUpSpeed   = 0x15C;
    constexpr std::size_t kBodyGearCount      = 0x1BA;

    constexpr const char* kLifeFields[] = { "bodyLife", "wheelLife", "turretLife", "subWeaponLife", "defenseGrade" };
    constexpr std::size_t kLifeFieldCount = sizeof(kLifeFields) / sizeof(kLifeFields[0]);
    static_assert(kLifeFieldCount == sizeof(SpawnEdit::lifeAdded) / sizeof(bool), "one SpawnEdit flag per life field");

    struct SpeedStep
    {
        const char* name;
        int         step;
    };
    constexpr SpeedStep kSpeedSteps[] = {
        { "FORWARD_SLOW", 1 },   { "FORWARD_NORMAL", 2 },   { "FORWARD_FAST", 3 },   { "FORWARD_MAX", 4 },
        { "BACKWARD_SLOW", -1 }, { "BACKWARD_NORMAL", -2 }, { "BACKWARD_FAST", -3 }, { "BACKWARD_MAX", -4 },
    };
    constexpr std::size_t kSpeedStepCount = sizeof(kSpeedSteps) / sizeof(kSpeedSteps[0]);

    struct SoundField
    {
        const char* name;
        std::size_t bodyOffset;
    };
    constexpr SoundField kSoundFields[] = {
        { "engine", 0xF8 }, { "engineStop", 0xFC }, { "horn", 0x108 }, { "hornStop", 0x10C },
    };
    constexpr std::size_t kSoundFieldCount = sizeof(kSoundFields) / sizeof(kSoundFields[0]);

    struct Vehicle
    {
        std::string   name;
        std::uint64_t partsFile = 0;
        std::uint64_t attachmentFiles[kMostAttachments] = {};
        std::size_t   attachmentCount = 0;

        bool          hasLife[kLifeFieldCount] = {};
        std::int32_t  life[kLifeFieldCount] = {};
        bool          hasDefenseValues = false;
        std::int32_t  defenseValues[kDefenseValueCount] = {};

        std::size_t   gearCount = 0;
        float         gearRatios[kMostGears] = {};
        float         finalGearRatio = 0.0f;   // 0 on these three and on a sound = the body keeps its own
        float         shiftDownSpeed = 0.0f;
        float         shiftUpSpeed = 0.0f;
        bool          hasSpeed[kSpeedStepCount] = {};
        float         speeds[kSpeedStepCount] = {};
        std::uint32_t sounds[kSoundFieldCount] = {};

        std::uint64_t reliefPack = 0;   // 0 = no Sortie Prep row
        std::uint8_t  reliefType = 0;   // the game vehicle it is deployed as
        std::uint64_t nameLangId = 0;   // 0 on these two = that game vehicle's own
        std::uint64_t iconFile = 0;
    };

    struct SpawnChance
    {
        std::uint8_t type;
        std::uint8_t subType;
        std::uint8_t inPlaceOf;
        std::uint8_t inPlaceOfSubType;
        float        chance;
    };

    struct SpawnPick
    {
        std::uint32_t place;
        std::uint8_t  inPlaceOf;
        std::uint8_t  inPlaceOfSubType;
        bool          swapped;
        std::uint8_t  type;
        std::uint8_t  subType;
    };

    std::mutex                g_Mutex;
    Vehicle                   g_Vehicles[kVehicleCount];
    std::atomic<std::uint8_t> g_ProxyTypes[kVehicleCount];
    std::vector<SpawnChance>  g_SpawnChances;
    std::vector<SpawnPick>    g_SpawnPicks;   // one roll per vehicle locator a mission, so a checkpoint reload brings the same vehicle back
    std::uint64_t             g_GameReliefPacks[kReliefTypeCount] = {};      // read the first time a relief type's pack is touched
    std::uint64_t             g_SwappedReliefPacks[kReliefTypeCount] = {};   // SetReliefPack's, 0 = none
    std::atomic<bool>         g_HooksInstalled{ false };
    std::vector<std::string>  g_SavedTypeNames;

    bool IsCustomType(std::int32_t type)
    {
        return type >= kFirstType && type <= kLastType;
    }

    Vehicle& VehicleOf(std::uint8_t type)
    {
        return g_Vehicles[type - kFirstType];
    }

    bool ValidName(const std::string& name)
    {
        if (name.empty() || name.size() > kMostNameLength || (name[0] >= '0' && name[0] <= '9'))
            return false;
        for (const char c : name)
        {
            const bool letter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
            if (!letter && !(c >= '0' && c <= '9') && c != '_')
                return false;
        }
        return true;
    }

    bool EndsWith(const std::string& text, const char* ending)
    {
        const std::size_t length = std::strlen(ending);
        return text.size() >= length && _stricmp(text.c_str() + text.size() - length, ending) == 0;
    }

    std::string StringField(lua_State* L, int table, const char* field)
    {
        const int top = GetLuaTop(L);
        LuaGetField(L, table, field);
        const char* text = LuaType(L, -1) == LUA_TSTRING ? GetLuaString(L, -1) : nullptr;
        std::string value = text ? text : "";
        g_lua_settop(L, top);
        return value;
    }

    bool NumberField(lua_State* L, int table, const char* field, double& value)
    {
        const int top = GetLuaTop(L);
        LuaGetField(L, table, field);
        const bool present = LuaType(L, -1) == LUA_TNUMBER;
        if (present)
            value = static_cast<double>(g_lua_tonumber(L, -1));
        g_lua_settop(L, top);
        return present;
    }

    bool HasField(lua_State* L, int table, const char* field)
    {
        const int top = GetLuaTop(L);
        LuaGetField(L, table, field);
        const bool present = LuaType(L, -1) != LUA_TNIL;
        g_lua_settop(L, top);
        return present;
    }

    void SetNumberField(lua_State* L, int table, const char* field, double value)
    {
        g_lua_pushstring(L, const_cast<char*>(field));
        g_lua_pushnumber(L, value);
        g_lua_rawset(L, table);
    }

    void ClearField(lua_State* L, int table, const char* field)
    {
        g_lua_pushstring(L, const_cast<char*>(field));
        g_lua_pushnil(L);
        g_lua_rawset(L, table);
    }

    bool NumberOrVehicleEnumField(lua_State* L, int table, const char* field, const char* enumName, std::int32_t& value)
    {
        const int top = GetLuaTop(L);
        LuaGetField(L, table, field);
        bool found = false;
        if (LuaType(L, -1) == LUA_TNUMBER)
        {
            value = static_cast<std::int32_t>(g_lua_tonumber(L, -1));
            found = true;
        }
        else if (LuaType(L, -1) == LUA_TSTRING)
        {
            const std::string name = GetLuaString(L, -1);
            LuaGetField(L, LUA_GLOBALSINDEX_51, "Vehicle");
            if (LuaType(L, -1) == LUA_TTABLE)
                LuaGetField(L, -1, enumName);
            if (LuaType(L, -1) == LUA_TTABLE)
                LuaGetField(L, -1, name.c_str());
            if (LuaType(L, -1) == LUA_TNUMBER)
            {
                value = static_cast<std::int32_t>(g_lua_tonumber(L, -1));
                found = true;
            }
        }
        g_lua_settop(L, top);
        return found;
    }

    bool GameTypeNamed(lua_State* L, const std::string& name)
    {
        const int top = GetLuaTop(L);
        bool taken = false;
        LuaGetField(L, LUA_GLOBALSINDEX_51, "Vehicle");
        if (LuaType(L, -1) == LUA_TTABLE)
            LuaGetField(L, -1, "type");
        if (LuaType(L, -1) == LUA_TTABLE)
            LuaGetField(L, -1, name.c_str());
        if (LuaType(L, -1) == LUA_TNUMBER)
            taken = !IsCustomType(static_cast<std::int32_t>(g_lua_tonumber(L, -1)));
        g_lua_settop(L, top);
        return taken;
    }

    bool PublishTypeName(lua_State* L, const std::string& name, std::uint8_t type)
    {
        const int top = GetLuaTop(L);
        bool published = false;
        LuaGetField(L, LUA_GLOBALSINDEX_51, "Vehicle");
        if (LuaType(L, -1) == LUA_TTABLE)
            LuaGetField(L, -1, "type");
        if (LuaType(L, -1) == LUA_TTABLE)
        {
            SetNumberField(L, GetLuaTop(L), name.c_str(), static_cast<double>(type));
            published = true;
        }
        g_lua_settop(L, top);
        return published;
    }

    std::uint8_t TypeNamed_NoLock(const std::string& name)
    {
        for (std::size_t i = 0; i < kVehicleCount; ++i)
        {
            if (!name.empty() && g_Vehicles[i].name == name)
                return static_cast<std::uint8_t>(kFirstType + i);
        }
        return 0;
    }

    std::uint8_t RegisteredVehicleField(lua_State* L, int table, const char* field)
    {
        double number = 0.0;
        const bool numbered = NumberField(L, table, field, number);
        const std::string name = numbered ? std::string() : StringField(L, table, field);
        std::lock_guard<std::mutex> lock(g_Mutex);
        if (!numbered)
            return TypeNamed_NoLock(name);
        const std::int32_t type = static_cast<std::int32_t>(number);
        return IsCustomType(type) && !g_Vehicles[type - kFirstType].name.empty() ? static_cast<std::uint8_t>(type) : 0;
    }

    void CollectSavedTypeName(const char* name, std::int32_t type)
    {
        if (type != 0)
            g_SavedTypeNames.push_back(name);
    }

    void ForgetVehicleNotRegisteredThisSession()
    {
        g_SavedTypeNames.clear();
        V_FrameWorkState::ForEachPersistedConstant(kTypeSpace, &CollectSavedTypeName);
        for (const std::string& saved : g_SavedTypeNames)
        {
            bool registered = false;
            {
                std::lock_guard<std::mutex> lock(g_Mutex);
                registered = TypeNamed_NoLock(saved) != 0;
            }
            if (!registered)
            {
                V_FrameWorkState::SetPersistedConstant(kTypeSpace, saved.c_str(), 0);
                LogWarn("RegisterVehicle: all 7 vehicle types were taken, the type of '%s' (not registered this session) was freed", saved.c_str());
                return;
            }
        }
    }

    std::uint8_t TypeForName(const std::string& name)
    {
        std::int32_t type = 0;
        if (!V_FrameWorkState::ResolveOrCreateConstantValue(kTypeSpace, name.c_str(), kFirstType, type, kLastType))
        {
            ForgetVehicleNotRegisteredThisSession();
            V_FrameWorkState::ResolveOrCreateConstantValue(kTypeSpace, name.c_str(), kFirstType, type, kLastType);
        }
        return IsCustomType(type) ? static_cast<std::uint8_t>(type) : 0;
    }

    std::uint32_t SoundIdOf(const std::string& eventName)
    {
        if (eventName.empty() || !gAddr.Fox_Sd_ConvertParameterID)
            return 0;
        const auto convert = reinterpret_cast<ConvertParameterID_t>(ResolveGameAddress(gAddr.Fox_Sd_ConvertParameterID));
        return convert ? convert(eventName.c_str()) : 0;
    }

    void ReadLife(lua_State* L, Vehicle& vehicle)
    {
        for (std::size_t i = 0; i < kLifeFieldCount; ++i)
        {
            double value = 0.0;
            vehicle.hasLife[i] = NumberField(L, 1, kLifeFields[i], value);
            vehicle.life[i] = static_cast<std::int32_t>(value);
        }

        const int top = GetLuaTop(L);
        LuaGetField(L, 1, "defenseValues");
        if (LuaType(L, -1) == LUA_TTABLE)
        {
            vehicle.hasDefenseValues = true;
            for (std::size_t i = 0; i < kDefenseValueCount; ++i)
            {
                LuaRawGetI(L, top + 1, static_cast<int>(i + 1));
                vehicle.defenseValues[i] = LuaType(L, -1) == LUA_TNUMBER ? static_cast<std::int32_t>(g_lua_tonumber(L, -1)) : 0;
                LuaPop(L, 1);
            }
        }
        g_lua_settop(L, top);
    }

    void ReadGearbox(lua_State* L, const std::string& name, Vehicle& vehicle)
    {
        const int top = GetLuaTop(L);
        LuaGetField(L, 1, "gearRatios");
        if (LuaType(L, -1) == LUA_TTABLE)
        {
            const std::size_t count = LuaObjLen(L, -1);
            if (count < 2 || count > kMostGears)
            {
                LogWarn("RegisterVehicle: '%s' gearRatios takes 2 to 6 numbers (reverse, first, second, ...), ignored", name.c_str());
            }
            else
            {
                vehicle.gearCount = count;
                for (std::size_t i = 0; i < count; ++i)
                {
                    LuaRawGetI(L, top + 1, static_cast<int>(i + 1));
                    vehicle.gearRatios[i] = static_cast<float>(g_lua_tonumber(L, -1));
                    LuaPop(L, 1);
                }
            }
        }
        g_lua_settop(L, top);

        double value = 0.0;
        if (NumberField(L, 1, "finalGearRatio", value))
            vehicle.finalGearRatio = static_cast<float>(value);
        if (NumberField(L, 1, "shiftDownRpm", value))
            vehicle.shiftDownSpeed = static_cast<float>(value) * kRadPerSecPerRpm;
        if (NumberField(L, 1, "shiftUpRpm", value))
            vehicle.shiftUpSpeed = static_cast<float>(value) * kRadPerSecPerRpm;
    }

    void ReadSpeedsAndSounds(lua_State* L, Vehicle& vehicle)
    {
        const int top = GetLuaTop(L);
        LuaGetField(L, 1, "speeds");
        if (LuaType(L, -1) == LUA_TTABLE)
        {
            for (std::size_t i = 0; i < kSpeedStepCount; ++i)
            {
                double value = 0.0;
                vehicle.hasSpeed[i] = NumberField(L, top + 1, kSpeedSteps[i].name, value);
                const float speed = std::fabs(static_cast<float>(value));
                vehicle.speeds[i] = kSpeedSteps[i].step < 0 ? -speed : speed;   // the body keeps backward speeds as negative m/s
            }
        }
        g_lua_settop(L, top);

        LuaGetField(L, 1, "sounds");
        if (LuaType(L, -1) == LUA_TTABLE)
        {
            for (std::size_t i = 0; i < kSoundFieldCount; ++i)
                vehicle.sounds[i] = SoundIdOf(StringField(L, top + 1, kSoundFields[i].name));
        }
        g_lua_settop(L, top);
    }

    void ReadAttachments(lua_State* L, const std::string& name, Vehicle& vehicle)
    {
        const int top = GetLuaTop(L);
        LuaGetField(L, 1, "attachments");
        if (LuaType(L, -1) == LUA_TTABLE)
        {
            const std::size_t count = LuaObjLen(L, -1);
            for (std::size_t i = 0; i < count; ++i)
            {
                LuaRawGetI(L, top + 1, static_cast<int>(i + 1));
                const char* file = LuaType(L, -1) == LUA_TSTRING ? GetLuaString(L, -1) : nullptr;
                if (file && vehicle.attachmentCount < kMostAttachments)
                    vehicle.attachmentFiles[vehicle.attachmentCount++] = FoxHashes::PathCode64Ext(file);
                else
                    LogWarn("RegisterVehicle: '%s' attachments takes up to 7 .parts paths, entry %u ignored", name.c_str(), static_cast<unsigned>(i + 1));
                LuaPop(L, 1);
            }
        }
        g_lua_settop(L, top);
    }

    bool BodySlotFilledSEH(const std::uint8_t* system, std::uint8_t type)
    {
        __try
        {
            return *reinterpret_cast<void* const*>(system + kSystemBodies + type * sizeof(void*)) != nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool BodyLoaded(std::uint8_t type)
    {
        auto** instance = static_cast<std::uint8_t**>(ResolveGameAddress(gAddr.VehicleSystemImpl_Instance));
        return instance && *instance && BodySlotFilledSEH(*instance, type);
    }

    struct BodySpecs
    {
        std::size_t   gearCount = 0;
        float         gearRatios[kMostGears] = {};
        float         finalGearRatio = 0.0f;
        float         shiftDownSpeed = 0.0f;
        float         shiftUpSpeed = 0.0f;
        bool          hasSpeed[kSpeedStepCount] = {};
        float         speeds[kSpeedStepCount] = {};
        std::uint32_t sounds[kSoundFieldCount] = {};
    };

    bool WriteBodySpecsSEH(std::uint8_t* body, const BodySpecs& specs)
    {
        __try
        {
            if (specs.gearCount != 0)
            {
                std::memcpy(body + kBodyGearRatios, specs.gearRatios, specs.gearCount * sizeof(float));
                body[kBodyGearCount] = static_cast<std::uint8_t>(specs.gearCount);
            }
            if (specs.finalGearRatio != 0.0f)
                std::memcpy(body + kBodyFinalGearRatio, &specs.finalGearRatio, sizeof(float));
            if (specs.shiftDownSpeed != 0.0f)
                std::memcpy(body + kBodyShiftDownSpeed, &specs.shiftDownSpeed, sizeof(float));
            if (specs.shiftUpSpeed != 0.0f)
                std::memcpy(body + kBodyShiftUpSpeed, &specs.shiftUpSpeed, sizeof(float));
            for (std::size_t i = 0; i < kSpeedStepCount; ++i)
            {
                if (specs.hasSpeed[i])
                    std::memcpy(body + kBodySpeedStop + kSpeedSteps[i].step * static_cast<int>(sizeof(float)), &specs.speeds[i], sizeof(float));
            }
            for (std::size_t i = 0; i < kSoundFieldCount; ++i)
            {
                if (specs.sounds[i] != 0)
                    std::memcpy(body + kSoundFields[i].bodyOffset, &specs.sounds[i], sizeof(std::uint32_t));
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint32_t ReliefTypeOf(std::int32_t type, std::int32_t subType)
    {
        const auto convert = reinterpret_cast<ConvertIntoReliefType_t>(ResolveGameAddress(gAddr.Vehicle_ConvertIntoReliefType));
        return convert ? convert(static_cast<std::uint8_t>((type << 4) | (subType & 0xF))) : 0;
    }

    bool SwapReliefPackSEH(std::uint64_t* slot, std::uint64_t pack, std::uint64_t& previous)
    {
        __try
        {
            previous = *slot;
            *slot = pack;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint64_t* ReliefPackSlot(std::uint32_t reliefType)
    {
        auto* field = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.ReliefBlockController_ReliefPackFilePaths));
        if (!field || reliefType == 0 || reliefType > kReliefTypeCount)
            return nullptr;
        std::uint64_t* paths = nullptr;
        std::uint32_t count = 0;
        if (!ReadEntryBytesSEH(field, &paths, sizeof(paths)) || !ReadEntryBytesSEH(field - 8, &count, sizeof(count)))   // the path count sits right before the pointer to the paths
            return nullptr;
        if (!paths || count != kReliefTypeCount)
            return nullptr;
        return paths + (reliefType - 1);
    }

    bool WriteReliefPack_NoLock(std::uint32_t reliefType, std::uint64_t pack, bool& changed)
    {
        changed = false;
        std::uint64_t* slot = ReliefPackSlot(reliefType);
        std::uint64_t current = 0;
        if (!slot || !ReadEntryBytesSEH(slot, &current, sizeof(current)))
            return false;
        std::uint64_t& gamePack = g_GameReliefPacks[reliefType - 1];
        if (gamePack == 0)
            gamePack = current;
        const std::uint64_t swapped = g_SwappedReliefPacks[reliefType - 1];
        const std::uint64_t wanted = pack != 0 ? pack : (swapped != 0 ? swapped : gamePack);
        if (wanted == current)
            return true;
        std::uint64_t previous = 0;
        changed = SwapReliefPackSEH(slot, wanted, previous);
        return changed;
    }

    void ReadSortie(lua_State* L, const std::string& name, Vehicle& vehicle)
    {
        const int top = GetLuaTop(L);
        LuaGetField(L, 1, "sortie");
        if (LuaType(L, -1) == LUA_TTABLE)
        {
            const int sortie = top + 1;
            const std::string pack = StringField(L, sortie, "pack");
            std::int32_t countsAs = 0;
            std::int32_t subType = 0;
            const bool typed = NumberOrVehicleEnumField(L, sortie, "countsAs", "type", countsAs) && countsAs >= 1 && countsAs <= kLastGameType;
            NumberOrVehicleEnumField(L, sortie, "subType", "subType", subType);
            const std::uint32_t reliefType = typed ? ReliefTypeOf(countsAs, subType) : 0;
            if (!EndsWith(pack, ".fpk") || !fox::PathExists(pack.c_str()))
            {
                LogWarn("RegisterVehicle: '%s' sortie.pack '%s' is not an installed .fpk, no Sortie Prep row", name.c_str(), pack.c_str());
            }
            else if (reliefType == 0 || reliefType > kReliefTypeCount)
            {
                LogWarn("RegisterVehicle: '%s' sortie.countsAs / subType is not a vehicle the player can deploy with, no Sortie Prep row", name.c_str());
            }
            else
            {
                std::string icon = StringField(L, sortie, "icon");
                if (!icon.empty() && !EndsWith(icon, ".ftex"))
                    icon += ".ftex";
                const std::string nameLangId = StringField(L, sortie, "nameLangId");
                vehicle.reliefPack = FoxHashes::PathCode64Ext(pack);
                vehicle.reliefType = static_cast<std::uint8_t>(reliefType);
                vehicle.nameLangId = nameLangId.empty() ? 0 : FoxHashes::StrCode64(nameLangId) & kLangIdMask;
                vehicle.iconFile = icon.empty() ? 0 : FoxHashes::PathCode64Ext(icon);
            }
        }
        g_lua_settop(L, top);
    }

    void FillSortieVehicle(std::uint8_t type, const Vehicle& vehicle, CustomVehicles::SortieVehicle& sortieVehicle)
    {
        sortieVehicle = { type, vehicle.reliefType, kResourceOfRelief[vehicle.reliefType], vehicle.reliefPack, vehicle.nameLangId, vehicle.iconFile, {} };
        strncpy_s(sortieVehicle.name, vehicle.name.c_str(), _TRUNCATE);
    }

    std::mt19937& Random()
    {
        static std::mt19937 engine{ std::random_device{}() };
        return engine;
    }

    enum class Roll { NoCandidates, GameVehicle, RegisteredVehicle };

    Roll RollSpawnChance(std::int32_t type, std::int32_t subType, SpawnChance& picked)
    {
        std::vector<SpawnChance> candidates;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            for (const SpawnChance& chance : g_SpawnChances)
            {
                if (chance.inPlaceOf == type && chance.inPlaceOfSubType == subType)
                    candidates.push_back(chance);
            }
        }
        float total = 0.0f;
        for (auto it = candidates.begin(); it != candidates.end();)
        {
            if (!BodyLoaded(it->type))
            {
                it = candidates.erase(it);
                continue;
            }
            total += it->chance;
            ++it;
        }
        if (candidates.empty())
            return Roll::NoCandidates;

        const float scale = total > 100.0f ? 100.0f / total : 1.0f;   // chances that add up past 100 share the 100
        float roll = std::uniform_real_distribution<float>(0.0f, 100.0f)(Random());
        for (const SpawnChance& chance : candidates)
        {
            roll -= chance.chance * scale;
            if (roll < 0.0f)
            {
                picked = chance;
                return Roll::RegisteredVehicle;
            }
        }
        return Roll::GameVehicle;
    }

    std::uint32_t SpawnPlace(lua_State* L)
    {
        std::string place = StringField(L, 1, "locator");
        if (place.empty())
            place = StringField(L, 1, "name");
        return place.empty() ? 0 : FoxHashes::StrCode32(place);
    }

    bool PickSpawnChance(lua_State* L, std::int32_t type, std::int32_t subType, SpawnChance& picked)
    {
        const std::uint32_t place = SpawnPlace(L);
        if (place != 0)
        {
            bool remembered = false;
            bool swapped = false;
            {
                std::lock_guard<std::mutex> lock(g_Mutex);
                for (const SpawnPick& pick : g_SpawnPicks)
                {
                    if (pick.place != place || pick.inPlaceOf != type || pick.inPlaceOfSubType != subType)
                        continue;
                    picked = { pick.type, pick.subType, pick.inPlaceOf, pick.inPlaceOfSubType, 0.0f };
                    swapped = pick.swapped;
                    remembered = true;
                    break;
                }
            }
            if (remembered)
                return swapped && BodyLoaded(picked.type);
        }

        const Roll roll = RollSpawnChance(type, subType, picked);
        if (roll == Roll::NoCandidates)
            return false;
        if (place != 0)
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            g_SpawnPicks.push_back({ place, static_cast<std::uint8_t>(type), static_cast<std::uint8_t>(subType),
                                     roll == Roll::RegisteredVehicle, picked.type, picked.subType });
        }
        return roll == Roll::RegisteredVehicle;
    }

    void AddLifeFields(lua_State* L, std::uint8_t type, SpawnEdit& edit)
    {
        Vehicle vehicle;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            vehicle = VehicleOf(type);
        }
        if (vehicle.name.empty())
            return;

        for (std::size_t i = 0; i < kLifeFieldCount; ++i)
        {
            if (!vehicle.hasLife[i] || HasField(L, 1, kLifeFields[i]))
                continue;
            SetNumberField(L, 1, kLifeFields[i], static_cast<double>(vehicle.life[i]));
            edit.lifeAdded[i] = true;
        }
        if (vehicle.hasDefenseValues && !HasField(L, 1, "defenseValues"))
        {
            g_lua_pushstring(L, const_cast<char*>("defenseValues"));
            g_lua_createtable(L, static_cast<int>(kDefenseValueCount), 0);
            const int values = GetLuaTop(L);
            for (std::size_t i = 0; i < kDefenseValueCount; ++i)
            {
                g_lua_pushnumber(L, static_cast<double>(i + 1));
                g_lua_pushnumber(L, static_cast<double>(vehicle.defenseValues[i]));
                g_lua_rawset(L, values);
            }
            g_lua_rawset(L, 1);
            edit.defenseAdded = true;
        }
    }
}

namespace CustomVehicles
{
    void SetHooksInstalled(bool installed)
    {
        g_HooksInstalled.store(installed, std::memory_order_relaxed);
    }

    std::uint8_t RegisteredCount()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        std::uint8_t count = 0;
        for (const Vehicle& vehicle : g_Vehicles)
            count += vehicle.name.empty() ? 0 : 1;
        return count;
    }

    void ForgetSpawnPicks()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_SpawnPicks.clear();
    }

    std::uint8_t TypeOfPartsFile(std::uint64_t partsFile)
    {
        if (partsFile == 0)
            return 0;
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (std::size_t i = 0; i < kVehicleCount; ++i)
        {
            if (!g_Vehicles[i].name.empty() && g_Vehicles[i].partsFile == partsFile)
                return static_cast<std::uint8_t>(kFirstType + i);
        }
        return 0;
    }

    void SetProxyType(std::uint8_t type, std::uint8_t proxyType)
    {
        if (IsCustomType(type))
            g_ProxyTypes[type - kFirstType].store(proxyType, std::memory_order_relaxed);
    }

    std::uint8_t TypeOfAttachmentFile(std::uint64_t attachmentFile)
    {
        if (attachmentFile == 0)
            return 0;
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (std::size_t i = 0; i < kVehicleCount; ++i)
        {
            if (g_Vehicles[i].name.empty())
                continue;
            for (std::size_t a = 0; a < g_Vehicles[i].attachmentCount; ++a)
            {
                if (g_Vehicles[i].attachmentFiles[a] == attachmentFile)
                    return static_cast<std::uint8_t>(kFirstType + i);
            }
        }
        return 0;
    }

    bool ProxyTypeCode(std::uint8_t typeCode, std::uint8_t& proxyCode)
    {
        const std::uint8_t type = typeCode >> 4;
        if (!IsCustomType(type))
            return false;
        const std::uint8_t proxy = g_ProxyTypes[type - kFirstType].load(std::memory_order_relaxed);
        if (proxy == 0 || proxy > kLastGameType)
            return false;
        proxyCode = static_cast<std::uint8_t>((proxy << 4) | (typeCode & 0xF));
        return true;
    }

    void ApplyBodySpecs(std::uint8_t type, std::uint8_t* body)
    {
        if (!body || !IsCustomType(type))
            return;
        BodySpecs specs;
        std::string name;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            const Vehicle& vehicle = VehicleOf(type);
            name = vehicle.name;
            specs.gearCount = vehicle.gearCount;
            std::memcpy(specs.gearRatios, vehicle.gearRatios, sizeof(specs.gearRatios));
            specs.finalGearRatio = vehicle.finalGearRatio;
            specs.shiftDownSpeed = vehicle.shiftDownSpeed;
            specs.shiftUpSpeed = vehicle.shiftUpSpeed;
            std::memcpy(specs.hasSpeed, vehicle.hasSpeed, sizeof(specs.hasSpeed));
            std::memcpy(specs.speeds, vehicle.speeds, sizeof(specs.speeds));
            std::memcpy(specs.sounds, vehicle.sounds, sizeof(specs.sounds));
        }
        if (!WriteBodySpecsSEH(body, specs))
            LogWarn("RegisterVehicle: '%s' gears, speeds and sounds could not be written to its body, it keeps the game's own", name.c_str());
    }

    bool IsSpawnCommand(std::uint32_t command)
    {
        static const std::uint32_t spawn = FoxHashes::StrCode32("Spawn");
        static const std::uint32_t respawn = FoxHashes::StrCode32("Respawn");
        return command == spawn || command == respawn;
    }

    SpawnEdit BeginSpawnCommand(lua_State* L)
    {
        SpawnEdit edit;
        if (!L || MissionCodeGuard::ShouldBypassHooks() || !ResolveLuaApi() || LuaType(L, 1) != LUA_TTABLE)
            return edit;

        double type = 0.0;
        if (!NumberField(L, 1, "type", type))
            return edit;
        double subType = 0.0;
        const bool hadSubType = NumberField(L, 1, "subType", subType);

        std::int32_t spawnType = static_cast<std::int32_t>(type);
        SpawnChance picked{};
        if (spawnType >= 1 && spawnType <= kLastGameType && PickSpawnChance(L, spawnType, static_cast<std::int32_t>(subType), picked))
        {
            edit.typeChanged = true;
            edit.requestedType = type;
            edit.hadSubType = hadSubType;
            edit.requestedSubType = subType;
            SetNumberField(L, 1, "type", static_cast<double>(picked.type));
            SetNumberField(L, 1, "subType", static_cast<double>(picked.subType));
            spawnType = picked.type;
        }
        if (IsCustomType(spawnType))
            AddLifeFields(L, static_cast<std::uint8_t>(spawnType), edit);
        return edit;
    }

    void EndSpawnCommand(lua_State* L, const SpawnEdit& edit)
    {
        if (!L || LuaType(L, 1) != LUA_TTABLE)
            return;
        if (edit.typeChanged)
        {
            SetNumberField(L, 1, "type", edit.requestedType);
            if (edit.hadSubType)
                SetNumberField(L, 1, "subType", edit.requestedSubType);
            else
                ClearField(L, 1, "subType");
        }
        for (std::size_t i = 0; i < kLifeFieldCount; ++i)
        {
            if (edit.lifeAdded[i])
                ClearField(L, 1, kLifeFields[i]);
        }
        if (edit.defenseAdded)
            ClearField(L, 1, "defenseValues");
    }

    int __cdecl Lua_RegisterVehicle(lua_State* L)
    {
        if (!ResolveLuaApi() || LuaType(L, 1) != LUA_TTABLE)
        {
            LogWarn("RegisterVehicle: expects { name = \"MY_VEHICLE\", partsFile = \"/Assets/....parts\" }, ignored");
            return 0;
        }
        const std::string name = StringField(L, 1, "name");
        const std::string partsFile = StringField(L, 1, "partsFile");
        if (!ValidName(name) || GameTypeNamed(L, name))
        {
            LogWarn("RegisterVehicle: name '%s' must be letters, digits and _ (up to 48) and not a game vehicle type, ignored", name.c_str());
            return 0;
        }
        if (!EndsWith(partsFile, ".parts"))
        {
            LogWarn("RegisterVehicle: '%s' needs partsFile = the .parts path its TppVehicle2BodyData names, ignored", name.c_str());
            return 0;
        }
        if (!g_HooksInstalled.load(std::memory_order_relaxed))
        {
            LogError("RegisterVehicle: '%s' not registered, V_FrameWork's vehicle hooks are not installed on this game build", name.c_str());
            return 0;
        }
        const std::uint64_t partsFileCode = FoxHashes::PathCode64Ext(partsFile);
        if (partsFileCode == 0)
        {
            LogError("RegisterVehicle: '%s' not registered, the game's path hash is not available on this game build", name.c_str());
            return 0;
        }
        const std::uint8_t type = TypeForName(name);
        if (type == 0)
        {
            LogError("RegisterVehicle: '%s' not registered, all 7 vehicle types are taken by other vehicles", name.c_str());
            return 0;
        }

        Vehicle vehicle;
        vehicle.name = name;
        vehicle.partsFile = partsFileCode;
        ReadAttachments(L, name, vehicle);
        ReadLife(L, vehicle);
        ReadGearbox(L, name, vehicle);
        ReadSpeedsAndSounds(L, vehicle);
        ReadSortie(L, name, vehicle);
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            VehicleOf(type) = vehicle;
        }
        if (!PublishTypeName(L, name, type))
            LogWarn("RegisterVehicle: '%s' has no Vehicle.type.%s, the game's Vehicle table is not there yet", name.c_str(), name.c_str());
        LogInfo("RegisterVehicle: '%s' registered as vehicle type %u", name.c_str(), static_cast<unsigned>(type));
        g_lua_pushnumber(L, static_cast<double>(type));
        return 1;
    }

    int __cdecl Lua_SetSpawnChance(lua_State* L)
    {
        if (!ResolveLuaApi() || LuaType(L, 1) != LUA_TTABLE)
        {
            LogWarn("SetSpawnChance: expects { vehicle = \"MY_VEHICLE\", inPlaceOf = \"WESTERN_LIGHT_VEHICLE\", chance = 50 }, ignored");
            PushLuaBool(L, false);
            return 1;
        }
        const std::uint8_t type = RegisteredVehicleField(L, 1, "vehicle");
        std::int32_t inPlaceOf = 0;
        std::int32_t subType = 0;
        std::int32_t inPlaceOfSubType = 0;
        double chance = 0.0;
        const bool known = type != 0
            && NumberOrVehicleEnumField(L, 1, "inPlaceOf", "type", inPlaceOf) && inPlaceOf >= 1 && inPlaceOf <= kLastGameType
            && NumberField(L, 1, "chance", chance) && chance >= 0.0 && chance <= 100.0;
        NumberOrVehicleEnumField(L, 1, "subType", "subType", subType);
        NumberOrVehicleEnumField(L, 1, "inPlaceOfSubType", "subType", inPlaceOfSubType);
        if (!known || subType < 0 || subType > 7 || inPlaceOfSubType < 0 || inPlaceOfSubType > 7)
        {
            LogWarn("SetSpawnChance: needs vehicle = a registered vehicle, inPlaceOf = a game vehicle type, chance = 0 to 100, ignored");
            PushLuaBool(L, false);
            return 1;
        }

        std::lock_guard<std::mutex> lock(g_Mutex);
        g_SpawnPicks.clear();
        for (auto it = g_SpawnChances.begin(); it != g_SpawnChances.end(); ++it)
        {
            if (it->type == type && it->inPlaceOf == inPlaceOf && it->inPlaceOfSubType == inPlaceOfSubType)
            {
                g_SpawnChances.erase(it);
                break;
            }
        }
        if (chance > 0.0)
            g_SpawnChances.push_back({ type, static_cast<std::uint8_t>(subType), static_cast<std::uint8_t>(inPlaceOf),
                                       static_cast<std::uint8_t>(inPlaceOfSubType), static_cast<float>(chance) });
        PushLuaBool(L, true);
        return 1;
    }

    int __cdecl Lua_SetReliefPack(lua_State* L)
    {
        if (!ResolveLuaApi() || LuaType(L, 1) != LUA_TTABLE)
        {
            LogWarn("SetReliefPack: expects { type = \"WESTERN_LIGHT_VEHICLE\", pack = \"/Assets/....fpk\" }, ignored");
            PushLuaBool(L, false);
            return 1;
        }
        std::int32_t type = 0;
        std::int32_t subType = 0;
        const bool typed = NumberOrVehicleEnumField(L, 1, "type", "type", type) && type >= 1 && type <= kLastGameType;
        NumberOrVehicleEnumField(L, 1, "subType", "subType", subType);
        const std::uint32_t reliefType = typed ? ReliefTypeOf(type, subType) : 0;
        if (reliefType == 0 || reliefType > kReliefTypeCount)
        {
            LogWarn("SetReliefPack: type / subType is not a vehicle the player can deploy with, ignored");
            PushLuaBool(L, false);
            return 1;
        }
        std::uint64_t* slot = ReliefPackSlot(reliefType);
        if (!slot)
        {
            LogWarn("SetReliefPack: the game's list of player vehicle packs is not there yet, ignored");
            PushLuaBool(L, false);
            return 1;
        }

        const std::string pack = StringField(L, 1, "pack");
        if (!pack.empty() && (!EndsWith(pack, ".fpk") || !fox::PathExists(pack.c_str())))
        {
            LogWarn("SetReliefPack: pack '%s' is not an installed .fpk, the game's own vehicle stays", pack.c_str());
            PushLuaBool(L, false);
            return 1;
        }

        std::lock_guard<std::mutex> lock(g_Mutex);
        g_SwappedReliefPacks[reliefType - 1] = pack.empty() ? 0 : FoxHashes::PathCode64Ext(pack);
        bool changed = false;
        PushLuaBool(L, WriteReliefPack_NoLock(reliefType, 0, changed));
        return 1;
    }

    std::size_t SortieVehicles(SortieVehicle* vehicles, std::size_t capacity)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        std::size_t count = 0;
        for (std::size_t i = 0; i < kVehicleCount && count < capacity; ++i)
        {
            const Vehicle& vehicle = g_Vehicles[i];
            if (vehicle.name.empty() || vehicle.reliefPack == 0)
                continue;
            FillSortieVehicle(static_cast<std::uint8_t>(kFirstType + i), vehicle, vehicles[count++]);
        }
        return count;
    }

    bool SortieVehicleOf(std::uint8_t type, SortieVehicle& sortieVehicle)
    {
        if (!IsCustomType(type))
            return false;
        std::lock_guard<std::mutex> lock(g_Mutex);
        const Vehicle& vehicle = VehicleOf(type);
        if (vehicle.name.empty() || vehicle.reliefPack == 0)
            return false;
        FillSortieVehicle(type, vehicle, sortieVehicle);
        return true;
    }

    std::uint8_t ReliefTypeOfResource(std::uint8_t resourceId)
    {
        for (std::uint8_t reliefType = 1; reliefType <= kReliefTypeCount; ++reliefType)
        {
            if (kResourceOfRelief[reliefType] == resourceId)
                return reliefType;
        }
        return 0;
    }

    bool ShowReliefPack(std::uint8_t reliefType, std::uint64_t pack)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        bool changed = false;
        WriteReliefPack_NoLock(reliefType, pack, changed);
        return changed;
    }

    int __cdecl Lua_IsLoaded(lua_State* L)
    {
        std::int32_t type = 0;
        if (!ResolveLuaApi())
            return 0;
        if (LuaType(L, 1) == LUA_TNUMBER)
            type = static_cast<std::int32_t>(g_lua_tonumber(L, 1));
        else if (LuaType(L, 1) == LUA_TSTRING)
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            type = TypeNamed_NoLock(GetLuaString(L, 1));
        }
        PushLuaBool(L, IsCustomType(type) && BodyLoaded(static_cast<std::uint8_t>(type)));
        return 1;
    }
}
