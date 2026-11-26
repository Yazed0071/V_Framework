#include <pch.h>
#include "SupportHeliWeapon.h"

#include <Windows.h>
#include <atomic>
#include <cstring>
#include <iterator>
#include <mutex>
#include <string>
#include <unordered_map>

#include "AddressSet.h"
#include "FoxHashes.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "V_FrameWorkState.h"
#include "log.h"
#include "../../lua/LuaApi.h"
#include "../equip/EquipDevelop_SetEquipUndeveloped.h"
#include "../equip/EquipParameterTablesImpl_ReloadEquipParameterTablesImpl.h"

namespace
{
    using GetQuarkSystemTable_t = std::uint8_t*(__fastcall*)();

    struct PartsTableRow
    {
        std::uint16_t developId;
        std::uint8_t  slot;
        std::uint8_t  level;
        std::uint32_t specAttackId;
    };

    struct PodLocator
    {
        const char*  name;
        std::uint8_t part;
    };

    struct BlastRow
    {
        std::int16_t effectRange;   // tenths of a metre
        std::int16_t optimalRange;
        std::uint8_t blastFlag;
        std::uint8_t unused;
    };

    constexpr std::size_t   kPartsTableRows      = 20;
    constexpr std::uint8_t  kSubWeaponSlot       = 1;
    constexpr std::size_t   kQuarkGame           = 0x98;
    constexpr std::size_t   kGameVars            = 0x10;
    constexpr std::size_t   kVarsMenuHeliParts   = 0x1BAC;   // userPresetHeliParts, one byte per slot
    constexpr std::size_t   kVarsSortieCustomize = 0x2E80;   // supportHeliCustomize
    constexpr std::uint16_t kAttackIdMask        = 0x3FF;
    constexpr std::int32_t  kMaxRocketShots      = 8;
    constexpr int           kMostParents         = 8;
    constexpr const char*   kStateSpace          = "SupportHeliWeapon";
    constexpr const char*   kStateMenuChoice     = "subWeaponChoice";

    constexpr std::uint16_t kLauncherAttackByLevel[] = { 0, 0x117, 0x118, 0x119, 0x114, 0x115, 0x116 };

    constexpr std::int32_t  kBlaSleepingGusMine    = 24;      // TppEquip.BLA_SleepingGusMine
    constexpr std::int32_t  kBlaSleepGusAssist     = 46;      // TppEquip.BLA_SleepGusAssist
    constexpr std::int32_t  kBlaSmokeAssist        = 47;      // TppEquip.BLA_SmokeAssist
    constexpr std::uint32_t kSmokeAttackLine       = 0x21754E51u;   // SHR0270
    constexpr std::uint32_t kSleepGasAttackLine    = 0x1A75436Cu;   // SHR0280
    constexpr float         kTenths                = 0.1f;

    constexpr PodLocator kPodLocators[] =
    {
        { "MissileLauncherLL", 8 },
        { "MissileLauncherLR", 9 },
        { "MissileLauncherRL", 10 },
        { "MissileLauncherRR", 11 },
        { "RocketLauncherLL", 12 },
        { "RocketLauncherLR", 13 },
        { "RocketLauncherRL", 14 },
        { "RocketLauncherRR", 15 },
    };

    struct Registered
    {
        std::string                 key;
        SupportHeliWeapon::Weapon   weapon;
        bool                        parentKnown = false;
    };

    std::mutex                                       g_Mutex;
    std::unordered_map<std::uint8_t, Registered>     g_ByCustomId;
    std::uint8_t                                     g_MenuChoice       = 0;
    bool                                             g_MenuChoiceLoaded = false;
    std::uint8_t                                     g_SortieCustomId   = 0;
    std::atomic<bool>                                g_EmptyBlastRowWarned[SupportHeliWeapon::kCustomIdMask + 1] = {};

    std::uint8_t* GameVarsSEH()
    {
        static GetQuarkSystemTable_t s_GetTable = nullptr;
        if (!s_GetTable)
            s_GetTable = reinterpret_cast<GetQuarkSystemTable_t>(ResolveGameAddress(gAddr.GetQuarkSystemTable));
        if (!s_GetTable)
            return nullptr;
        __try
        {
            std::uint8_t* table = s_GetTable();
            if (!table)
                return nullptr;
            std::uint8_t* game = *reinterpret_cast<std::uint8_t**>(table + kQuarkGame);
            return game ? *reinterpret_cast<std::uint8_t**>(game + kGameVars) : nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    std::uint8_t VanillaSubWeaponLevel(std::uint32_t developId)
    {
        const auto* rows = static_cast<const PartsTableRow*>(ResolveGameAddress(gAddr.SupportHeli_PartsTable));
        if (!rows || developId == 0)
            return 0;
        for (std::size_t i = 0; i < kPartsTableRows; ++i)
        {
            if (rows[i].developId == developId && rows[i].slot == kSubWeaponSlot)
                return rows[i].level;
        }
        return 0;
    }

    void ResolveDevelopRow(Registered& entry)
    {
        if (entry.weapon.developId == 0)
        {
            const std::int32_t developId = V_FrameWorkState::GetDevelopIdByKey(entry.key.c_str());
            if (developId <= 0)
                return;
            entry.weapon.developId = static_cast<std::uint32_t>(developId);
        }
        if (entry.parentKnown)
            return;
        std::uint32_t parent = EquipDevelop_GetDevelopParent(entry.weapon.developId);
        for (int depth = 0; parent != 0 && depth < kMostParents; ++depth)   // a higher grade hangs off another custom row
        {
            const std::uint8_t level = VanillaSubWeaponLevel(parent);
            if (level != 0)
            {
                entry.parentKnown          = true;
                entry.weapon.fallbackLevel = level;
                return;
            }
            parent = EquipDevelop_GetDevelopParent(parent);
        }
    }

    void LoadMenuChoice()
    {
        if (g_MenuChoiceLoaded)
            return;
        g_MenuChoiceLoaded = true;
        const std::int32_t saved = V_FrameWorkState::GetPersistedConstant(kStateSpace, kStateMenuChoice);
        g_MenuChoice = (saved > 0 && saved <= static_cast<std::int32_t>(SupportHeliWeapon::kCustomIdMask))
            ? static_cast<std::uint8_t>(saved) : 0;
    }

    float SpecNumber(lua_State* L, const char* key, float fallback)
    {
        const int top = GetLuaTop(L);
        LuaGetField(L, 2, key);
        const float value = LuaIsNumber(L, -1) ? GetLuaNumber(L, -1) : fallback;
        g_lua_settop(L, top);
        return value;
    }

    bool SpecHasField(lua_State* L, const char* key)
    {
        const int top = GetLuaTop(L);
        LuaGetField(L, 2, key);
        const bool present = LuaType(L, -1) != LUA_TNIL;
        g_lua_settop(L, top);
        return present;
    }

    std::string SpecString(lua_State* L, const char* key)
    {
        const int top = GetLuaTop(L);
        LuaGetField(L, 2, key);
        const char* text = LuaType(L, -1) == LUA_TSTRING ? GetLuaString(L, -1) : nullptr;
        std::string value = text ? text : "";
        g_lua_settop(L, top);
        return value;
    }

    bool ReadPods(lua_State* L, const char* key, std::uint8_t& mask)
    {
        const int top = GetLuaTop(L);
        LuaGetField(L, 2, "pods");
        if (LuaType(L, -1) != LUA_TTABLE)
        {
            g_lua_settop(L, top);
            return false;
        }
        const int pods = GetLuaTop(L);
        mask = 0;
        for (int i = 1;; ++i)
        {
            LuaRawGetI(L, pods, i);
            if (LuaType(L, -1) != LUA_TSTRING)
                break;
            const char* name = GetLuaString(L, -1);
            bool known = false;
            for (const PodLocator& pod : kPodLocators)
            {
                if (name && std::strcmp(name, pod.name) == 0)
                {
                    mask |= static_cast<std::uint8_t>(1u << (pod.part - SupportHeliWeapon::kFirstPodPart));
                    known = true;
                }
            }
            if (!known)
                LogWarn("RegisterSupportHeliWeapon: '%s' pod '%s' is not a heli pod, ignored", key, name ? name : "?");
            LuaPop(L, 1);
        }
        g_lua_settop(L, top);
        return true;
    }

    std::uint8_t FireLevel(bool isMissile, int podCount)
    {
        if (isMissile)
            return podCount == 4 ? 6 : 4;
        return podCount == 4 ? 3 : 1;
    }

    bool ReadBlastRowSEH(std::int32_t blastId, BlastRow& out)
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

    std::uint16_t CloudShellForBlastRow(std::int32_t blastId)
    {
        switch (blastId)
        {
        case SupportHeliWeapon::kBlaSmokeGrenade:
        case kBlaSmokeAssist:
            return SupportHeliWeapon::kSmokeCloudShell;
        case SupportHeliWeapon::kBlaSleepingGusGrenade:
        case kBlaSleepingGusMine:
        case kBlaSleepGusAssist:
            return SupportHeliWeapon::kSleepCloudShell;
        default:
            return 0;
        }
    }

    void ReadAttackLine(lua_State* L, const char* key, SupportHeliWeapon::Weapon& weapon)
    {
        const int top = GetLuaTop(L);
        LuaGetField(L, 2, "voice");
        const char* code = LuaType(L, -1) == LUA_TSTRING ? GetLuaString(L, -1) : nullptr;
        if (LuaType(L, -1) == LUA_TBOOLEAN && !GetLuaBool(L, -1))
            weapon.silentAttack = true;
        else if (code && _strnicmp(code, "SHR", 3) == 0)
        {
            weapon.attackLine     = FoxHashes::FNVHash32(code);
            weapon.attackLineType = SupportHeliWeapon::kPilotRadio;
        }
        else if (code && _strnicmp(code, "SHV", 3) == 0)
        {
            weapon.attackLine     = FoxHashes::FNVHash32(code);
            weapon.attackLineType = SupportHeliWeapon::kPilotVoice;
        }
        else
            LogWarn("RegisterSupportHeliWeapon: '%s' voice must be a pilot line code (\"SHR0240\", \"SHV0090\", ...) or false, vanilla line kept", key);
        g_lua_settop(L, top);
    }

    void SetBlastRow(SupportHeliWeapon::Weapon& weapon, std::int32_t blastId)
    {
        weapon.blastId    = blastId;
        weapon.cloudShell = CloudShellForBlastRow(blastId);
    }

    void SetCloudAttackLine(SupportHeliWeapon::Weapon& weapon)
    {
        if (weapon.cloudShell == SupportHeliWeapon::kSleepCloudShell)
        {
            weapon.attackLine     = kSleepGasAttackLine;
            weapon.attackLineType = SupportHeliWeapon::kPilotRadio;
        }
        else if (weapon.cloudShell == SupportHeliWeapon::kSmokeCloudShell)
        {
            weapon.attackLine     = kSmokeAttackLine;
            weapon.attackLineType = SupportHeliWeapon::kPilotRadio;
        }
    }

    bool StoreWeapon(const char* key, SupportHeliWeapon::Weapon weapon)
    {
        std::int32_t customId = 0;
        if (!V_FrameWorkState::ResolveOrCreateConstantValue(kStateSpace, key, 1, customId, static_cast<std::int32_t>(SupportHeliWeapon::kCustomIdMask)))
        {
            LogWarn("RegisterSupportHeliWeapon: '%s' no free weapon id left (255 max), ignored", key);
            return false;
        }
        weapon.customId = static_cast<std::uint8_t>(customId);

        std::lock_guard<std::mutex> lock(g_Mutex);
        Registered& entry = g_ByCustomId[weapon.customId];
        entry.key         = key;
        entry.weapon      = weapon;
        entry.parentKnown = false;
        ResolveDevelopRow(entry);
        return true;
    }
}

namespace SupportHeliWeapon
{
    int __cdecl Lua_RegisterSupportHeliWeapon(lua_State* L)
    {
        if (!ResolveLuaApi())
            return 0;

        const char* key = (GetLuaTop(L) >= 1 && LuaIsString(L, 1)) ? GetLuaString(L, 1) : nullptr;
        if (!key || !*key || LuaType(L, 2) != LUA_TTABLE)
        {
            LogWarn("RegisterSupportHeliWeapon: expects (key, { type = \"Rocket\" or \"Missile\", ammo = N, ... }), ignored");
            PushLuaBool(L, false);
            return 1;
        }

        const std::string type = SpecString(L, "type");
        if (type != "Rocket" && type != "Missile")
        {
            LogWarn("RegisterSupportHeliWeapon: '%s' type must be \"Rocket\" or \"Missile\", ignored", key);
            PushLuaBool(L, false);
            return 1;
        }

        const float ammo = SpecNumber(L, "ammo", -1.0f);
        if (ammo < 0.0f)
        {
            LogWarn("RegisterSupportHeliWeapon: '%s' needs ammo = 0 or more, ignored", key);
            PushLuaBool(L, false);
            return 1;
        }

        Weapon weapon{};
        weapon.isMissile = (type == "Missile");
        weapon.ammo      = static_cast<std::int32_t>(ammo);

        int podCount = static_cast<int>(SpecNumber(L, "podCount", 2.0f));
        if (podCount != 2 && podCount != 4)
        {
            LogWarn("RegisterSupportHeliWeapon: '%s' podCount must be 2 or 4, using 2", key);
            podCount = 2;
        }
        weapon.fireLevel = FireLevel(weapon.isMissile, podCount);

        const float attackId = SpecNumber(L, "attackId", 0.0f);
        if (attackId < 0.0f || attackId > kAttackIdMask)
            LogWarn("RegisterSupportHeliWeapon: '%s' attackId %.0f does not fit the heli shell (0-1023), vanilla damage kept", key, attackId);
        else
            weapon.attackId = static_cast<std::uint16_t>(attackId);

        const float shellId = SpecNumber(L, "shellId", 0.0f);
        if (shellId == kRocketShell || shellId == kMissileShell)
            weapon.shellId = static_cast<std::uint16_t>(shellId);
        else if (shellId != 0.0f)
            LogWarn("RegisterSupportHeliWeapon: '%s' shellId %.0f is not loaded for the Pequod (use TppEquip.EQP_BL_uth0_ammo0 or EQP_BL_uth0_ammo1), vanilla shell kept", key, shellId);

        if (SpecHasField(L, "blastId"))
        {
            const float blastId = SpecNumber(L, "blastId", -1.0f);
            if (blastId < 0.0f || blastId >= static_cast<float>(EquipParameterTables::kBlastRowCount))
                LogWarn("RegisterSupportHeliWeapon: '%s' blastId must be a blast row id (TppEquip.BLA_*, 0-255), vanilla blast kept", key);
            else
                SetBlastRow(weapon, static_cast<std::int32_t>(blastId));
        }

        if (SpecHasField(L, "voice"))
            ReadAttackLine(L, key, weapon);
        else
            SetCloudAttackLine(weapon);

        const float shotCount = SpecNumber(L, "shotCount", 0.0f);
        if (!weapon.isMissile && shotCount > kMaxRocketShots)
            LogWarn("RegisterSupportHeliWeapon: '%s' a Rocket volley holds at most 8 shots, using 8", key);
        weapon.shotCount = static_cast<std::int32_t>(shotCount);
        if (!weapon.isMissile && weapon.shotCount > kMaxRocketShots)
            weapon.shotCount = kMaxRocketShots;
        if (weapon.shotCount < 0)
            weapon.shotCount = 0;

        if (SpecHasField(L, "shotInterval"))
            weapon.shotInterval = SpecNumber(L, "shotInterval", -1.0f);
        if (SpecHasField(L, "windup"))
            weapon.windup = SpecNumber(L, "windup", -1.0f);
        if (SpecHasField(L, "cooldown"))
            weapon.cooldown = SpecNumber(L, "cooldown", -1.0f);
        weapon.hasPodMask = ReadPods(L, key, weapon.podMask);

        if (!StoreWeapon(key, weapon))
        {
            PushLuaBool(L, false);
            return 1;
        }
        LogInfo("RegisterSupportHeliWeapon: '%s' registered", key);
        PushLuaBool(L, true);
        return 1;
    }

    bool RegisterRocket(const char* key, std::int32_t ammo, int podCount, std::int32_t blastId)
    {
        Weapon weapon{};
        weapon.ammo      = ammo;
        weapon.fireLevel = FireLevel(false, podCount);
        SetBlastRow(weapon, blastId);
        SetCloudAttackLine(weapon);
        return StoreWeapon(key, weapon);
    }

    std::uint16_t ShellAttackId(const Weapon& weapon)
    {
        if (weapon.attackId != 0)
            return weapon.attackId;
        return weapon.fireLevel < std::size(kLauncherAttackByLevel) ? kLauncherAttackByLevel[weapon.fireLevel] : 0;
    }

    bool ReadWeaponBlastRow(const Weapon& weapon, float& radius, std::uint8_t& blastFlag)
    {
        BlastRow row{};
        if (weapon.blastId < 0)
            return false;
        if (!ReadBlastRowSEH(weapon.blastId, row) || row.optimalRange <= 0)
        {
            if (!g_EmptyBlastRowWarned[weapon.customId].exchange(true))
                LogWarn("RegisterSupportHeliWeapon: blastId %d has no blast range, the Pequod shell keeps its vanilla blast", weapon.blastId);
            return false;
        }
        radius    = static_cast<float>(row.optimalRange) * kTenths;   // a non-player shooter's blast reads the second range
        blastFlag = row.blastFlag;
        return true;
    }

    bool SortieAttackLine(std::uint32_t& line, std::uint32_t& lineType)
    {
        Weapon weapon{};
        if (!GetSortieWeapon(weapon))
            return true;
        if (weapon.silentAttack)
            return false;
        if (weapon.attackLine != 0)
        {
            line     = weapon.attackLine;
            lineType = weapon.attackLineType;
        }
        return true;
    }

    bool FindByDevelopId(std::uint32_t developId, Weapon& out)
    {
        if (developId == 0)
            return false;
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (auto& [id, entry] : g_ByCustomId)
        {
            ResolveDevelopRow(entry);
            if (entry.weapon.developId == developId)
            {
                out = entry.weapon;
                return true;
            }
        }
        return false;
    }

    bool FindByCustomId(std::uint8_t customId, Weapon& out)
    {
        if (customId == 0)
            return false;
        std::lock_guard<std::mutex> lock(g_Mutex);
        const auto it = g_ByCustomId.find(customId);
        if (it == g_ByCustomId.end())
            return false;
        ResolveDevelopRow(it->second);
        out = it->second.weapon;
        return true;
    }

    void NoteMenuChoice(std::uint32_t developId)
    {
        Weapon weapon{};
        const std::uint8_t choice = FindByDevelopId(developId, weapon) ? weapon.customId : 0;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            LoadMenuChoice();
            if (g_MenuChoice == choice)
                return;
            g_MenuChoice = choice;
        }
        V_FrameWorkState::SetPersistedConstant(kStateSpace, kStateMenuChoice, choice);
    }

    std::uint8_t MenuChoiceForSortie(std::uint8_t menuLevel)
    {
        std::uint8_t choice = 0;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            LoadMenuChoice();
            choice = g_MenuChoice;
        }
        Weapon weapon{};
        if (!FindByCustomId(choice, weapon) || weapon.developId == 0 || weapon.fallbackLevel != menuLevel)
            return 0;
        if (!EquipDevelop_IsDevelopedByDevelopId(weapon.developId))
            return 0;
        return choice;
    }

    std::uint8_t ReadMenuSubWeaponLevel()
    {
        std::uint8_t* vars = GameVarsSEH();
        if (!vars)
            return 0;
        __try
        {
            return vars[kVarsMenuHeliParts + kSubWeaponSlot];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    void SetSortieWeapon(std::uint8_t customId)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_SortieCustomId = customId;
    }

    bool GetSortieWeapon(Weapon& out)
    {
        if (MissionCodeGuard::ShouldBypassHooks())
            return false;
        std::uint8_t customId = 0;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            customId = g_SortieCustomId;
        }
        return FindByCustomId(customId, out);
    }

    std::uint8_t ReadSortieCustomId()
    {
        std::uint8_t* vars = GameVarsSEH();
        if (!vars)
            return 0;
        __try
        {
            const std::uint32_t packed = *reinterpret_cast<const std::uint32_t*>(vars + kVarsSortieCustomize);
            return static_cast<std::uint8_t>((packed >> kCustomIdShift) & kCustomIdMask);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }
}
