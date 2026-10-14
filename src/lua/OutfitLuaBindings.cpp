#include "pch.h"

#include "OutfitLuaBindings.h"

#include <cstdint>
#include <cstring>
#include <memory>

#include "LuaApi.h"
#include "FoxHashes.h"
#include "log.h"
#include "V_FrameWorkState.h"
#include "../hooks/outfit/OutfitRegistry.h"
#include "../hooks/outfit/UniqueCharacterOwnSuit.h"
#include "../hooks/outfit/CustomHeadRegistry.h"
#include "../hooks/outfit/HeadMotion.h"
#include "../hooks/outfit/PlayerAppearance_LoadPlayerFv2s.h"
#include "../hooks/outfit/BlockControllerImpl_LoadPartsNew.h"
#include "../hooks/outfit/ItemSelectorCallbackImpl_AddListSuit.h"
#include "MissionCodeGuard.h"
#include "../hooks/equip/EquipDevelop_AddToEquipDevelopTable.h"
#include "../hooks/equip/EquipDevelop_SetEquipUndeveloped.h"

static void SetLuaTop(lua_State* L, int idx)
{
    if (!ResolveLuaApi() || !g_lua_settop)
        return;
    g_lua_settop(L, idx);
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

static bool TryReadTableNumberField(lua_State* L, int tableIndex, const char* fieldName, float& outValue)
{
    outValue = 0.0f;
    LuaGetField(L, tableIndex, const_cast<char*>(fieldName));

    const bool ok = (LuaType(L, -1) == 3);
    if (ok)
        outValue = GetLuaNumber(L, -1);

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


namespace
{
    struct PlayerTypeBranchKey
    {
        const char*  key;
        std::uint8_t playerType;
    };
    static constexpr PlayerTypeBranchKey k_PtBranchKeys[] = {
        { "snake",    outfit::kPlayerType_Snake    },
        { "ddMale",   outfit::kPlayerType_DDMale   },
        { "ddFemale", outfit::kPlayerType_DDFemale },
        { "avatar",   outfit::kPlayerType_Avatar   },
        { "ocelot",   outfit::kPlayerType_Ocelot   },
        { "quiet",    outfit::kPlayerType_Quiet    },
    };

    static bool MatchPlayerTypeBranchKey(const char* name,
                                         std::uint8_t& outPlayerType)
    {
        if (!name || !name[0])
            return false;
        for (const auto& bk : k_PtBranchKeys)
        {
            if (_stricmp(name, bk.key) == 0)
            {
                outPlayerType = bk.playerType;
                return true;
            }
        }
        return false;
    }

    template <typename Fn>
    static void ForEachPlayerTypeBranch(lua_State* L, int tableIdx,
                                        const char* site, const char* key,
                                        Fn&& fn)
    {
        std::uint32_t seen = 0;
        LuaPushNil(L);
        while (LuaNext(L, tableIdx) != 0)
        {
            const int valueIdx = GetLuaTop(L);
            std::uint8_t playerType = 0;
            const char* branchName =
                (LuaType(L, -2) == LUA_TSTRING) ? GetLuaString(L, -2) : nullptr;
            if (branchName && LuaType(L, -1) == LUA_TTABLE
                && MatchPlayerTypeBranchKey(branchName, playerType))
            {
                const std::uint32_t bit = 1u << playerType;
                if (seen & bit)
                {
                    LogWarn("%s '%s': branch %s declared twice, first one reached used",
                        site, key ? key : "(unnamed)", branchName);
                }
                else
                {
                    seen |= bit;
                    fn(playerType, branchName, valueIdx);
                }
            }
            SetLuaTop(L, valueIdx - 1);
        }
    }


    const char* RequiredExtForField(const char* fieldName)
    {
        if (std::strcmp(fieldName, "partsPath") == 0) return ".parts";
        if (std::strcmp(fieldName, "fpkPath")   == 0) return ".fpk";
        const std::size_t n = std::strlen(fieldName);
        if (n >= 3 && _stricmp(fieldName + n - 3, "Fpk") == 0) return ".fpk";
        if (n >= 3 && _stricmp(fieldName + n - 3, "Fv2") == 0) return ".fv2";
        return nullptr;
    }

    bool PathHasRequiredExt(const char* path, const char* ext)
    {
        const std::size_t n = std::strlen(path);
        const std::size_t m = std::strlen(ext);
        return n > m && _stricmp(path + n - m, ext) == 0;
    }

    std::uint64_t ReadIconFtexPathField(lua_State* L, int tableIndex)
    {
        LuaGetField(L, tableIndex, "iconFtexPath");
        std::uint64_t result = 0;
        if (LuaType(L, -1) == LUA_TSTRING)
        {
            if (const char* s = GetLuaString(L, -1); s && *s)
            {
                std::string path(s);
                if (!PathHasRequiredExt(path.c_str(), ".ftex"))
                    path += ".ftex";
                result = FoxHashes::PathCode64Ext(path);
            }
        }
        LuaPop(L, 1);
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
            {
                const char* ext = RequiredExtForField(fieldName);
                if (ext && !PathHasRequiredExt(s, ext))
                {
                    LogWarn("OutfitLua: %s '%s' must end in '%s', default used",
                        fieldName, s, ext);
                }
                else
                {
                    result = FoxHashes::PathCode64Ext(s);
                }
            }
        }
        else if (type == 1)
        {
            const bool b = GetLuaBool(L, -1) != 0;
            result = b ? outfit::kSubAssetUseVanilla : outfit::kSubAssetDisabled;
        }


        SetLuaTop(L, -2);
        return result;
    }


    std::uint8_t ReadVoiceFpkByTypeField(
        lua_State* L, int tableIndex, outfit::VoiceFpkByType* out,
        std::size_t maxOut)
    {
        LuaGetField(L, tableIndex, "voiceFpk");
        const int mapIdx = GetLuaTop(L);
        if (LuaType(L, mapIdx) != LUA_TTABLE)
        {
            SetLuaTop(L, -2);
            return 0;
        }

        std::uint8_t count = 0;
        g_lua_pushnil(L);
        while (g_lua_next(L, mapIdx) != 0)
        {
            if (LuaType(L, -2) == LUA_TSTRING && LuaType(L, -1) == LUA_TSTRING)
            {
                const char* name = GetLuaString(L, -2);
                const char* path = GetLuaString(L, -1);
                if (name && name[0] && path && path[0])
                {
                    if (!PathHasRequiredExt(path, ".fpk"))
                    {
                        LogWarn("OutfitLua: voiceFpk[%s] '%s' must end in '.fpk', "
                            "vanilla voice kept", name, path);
                    }
                    else if (count >= maxOut)
                    {
                        LogWarn("OutfitLua: voiceFpk over %zu entries, '%s' dropped",
                            maxOut, name);
                    }
                    else
                    {
                        out[count].voiceType  = outfit::SoundSwitchHash(name);
                        out[count].pathCode64 = FoxHashes::PathCode64Ext(path);
                        ++count;
                    }
                }
            }
            SetLuaTop(L, -2);
        }

        SetLuaTop(L, -2);
        return count;
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
            {
                const char* ext = RequiredExtForField(fieldName);
                if (ext && !PathHasRequiredExt(s, ext))
                {
                    LogWarn("OutfitLua: %s '%s' must end in '%s', path ignored",
                        fieldName, s, ext);
                }
                else
                {
                    result = FoxHashes::PathCode64Ext(s);
                }
            }
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


    constexpr std::uint8_t kQuietSuitAbilities = 0x74;

    static std::uint32_t ReadSoundSwitchField(lua_State* L, int tbl,
                                              const char* key,
                                              const char*& outName)
    {
        outName = nullptr;

        const char* name = nullptr;
        if (TryReadTableStringField(L, tbl, key, name)
            && name && name[0])
        {
            outName = name;
            return outfit::SoundSwitchHash(name);
        }

        LuaGetField(L, tbl, const_cast<char*>(key));
        std::uint32_t raw = outfit::kSoundSwitchUnset;
        if (LuaType(L, -1) == 3)
            raw = static_cast<std::uint32_t>(GetLuaInt64(L, -1));
        SetLuaTop(L, -2);
        return raw;
    }

    static std::uint8_t ClampAbilityLevel(int level)
    {
        if (level < 0) return 0;
        if (level > 9) return 9;
        return static_cast<std::uint8_t>(level);
    }

    static std::uint32_t ReadSprintFlag(lua_State* L, int tbl, const char* field, std::uint32_t flag,
                                        std::uint32_t sprint)
    {
        LuaGetField(L, tbl, const_cast<char*>(field));
        if (LuaType(L, -1) != 0)
            sprint = GetLuaBool(L, -1) != 0 ? (sprint | flag) : (sprint & ~flag);
        LuaPop(L, 1);
        return sprint;
    }

    static std::uint32_t ReadSprintField(lua_State* L, int tbl)
    {
        std::uint32_t sprint = outfit::kSprintNone;
        float speed = 0.0f;
        if (TryReadTableNumberField(L, tbl, "sprintSpeed", speed))
        {
            std::uint32_t percent = 0;
            if (speed > 0.0f)
            {
                const float clamped = speed < 0.1f ? 0.1f : (speed > 10.0f ? 10.0f : speed);
                if (clamped != speed)
                    LogWarn("OutfitLua: abilities.sprintSpeed %.2f outside 0.1-10, using %.2f", speed, clamped);
                percent = static_cast<std::uint32_t>(clamped * 100.0f + 0.5f);
            }
            sprint = (sprint & ~outfit::kSprintSpeedMask) | percent;
        }

        sprint = ReadSprintFlag(L, tbl, "raidenRun", outfit::kSprintRaidenRun, sprint);
        sprint = ReadSprintFlag(L, tbl, "sprintSound", outfit::kSprintDashSound, sprint);

        LuaGetField(L, tbl, "dashAttack");
        if (LuaType(L, -1) == 3)
        {
            const float id = GetLuaNumber(L, -1);
            const auto whole = static_cast<std::uint32_t>(id);
            if (id >= 1.0f && whole <= outfit::kSprintAttackLast && static_cast<float>(whole) == id)
                sprint = (sprint & ~outfit::kSprintAttackMask) | outfit::kSprintDashAttack
                    | (whole << outfit::kSprintAttackShift);
            else
                LogWarn("OutfitLua: abilities.dashAttack %.0f is not a TppDamage.ATK_ id, ignored", id);
        }
        else if (LuaType(L, -1) != 0)
        {
            sprint &= ~outfit::kSprintAttackMask;
            sprint = GetLuaBool(L, -1) != 0 ? (sprint | outfit::kSprintDashAttack)
                                            : (sprint & ~outfit::kSprintDashAttack);
        }
        LuaPop(L, 1);
        return sprint;
    }

    static bool ReadEyeFv2Field(lua_State* L, int tbl, std::uint64_t& out)
    {
        const char* path = nullptr;
        if (!TryReadTableStringField(L, tbl, "eyeFv2", path) || !path)
            return false;
        if (!path[0] || !PathHasRequiredExt(path, ".fv2"))
        {
            LogWarn("OutfitLua: eyeFv2 '%s' must end in '.fv2', ignored", path);
            return false;
        }
        out = FoxHashes::PathCode64Ext(path);
        return true;
    }

    static bool ReadEyeColorField(lua_State* L, int tbl, std::uint8_t& out)
    {
        LuaGetField(L, tbl, "eyeColor");
        const int type = LuaType(L, -1);
        bool present = type != 0;
        if (type == 3)
        {
            const float n = GetLuaNumber(L, -1);
            const auto unit = static_cast<std::uint32_t>(n);
            if (n >= 0.0f && unit < outfit::kEyeColorUnits && static_cast<float>(unit) == n)
                out = static_cast<std::uint8_t>(unit + 1);
            else
            {
                LogWarn("OutfitLua: eyeColor %.0f must be true, false or an iris unit 0-%u, ignored", n,
                    static_cast<unsigned>(outfit::kEyeColorUnits - 1));
                present = false;
            }
        }
        else if (present)
        {
            out = GetLuaBool(L, -1) != 0 ? outfit::kEyeColorStaff : outfit::kEyeColorOff;
        }
        LuaPop(L, 1);
        return present;
    }

    static void WarnTopLevelAbilities(lua_State* L, const char* api, const char* name)
    {
        LuaGetField(L, 1, "abilities");
        if (LuaType(L, -1) == LUA_TTABLE)
            LogWarn("%s '%s': 'abilities' must be inside a character branch "
                "(e.g. ddFemale = { abilities = {...} }), ignored",
                api, name ? name : "(by number)");
        LuaPop(L, 1);
    }

    static std::uint64_t ReadSprintSparkField(lua_State* L, int tbl)
    {
        LuaGetField(L, tbl, "sprintSpark");
        std::uint64_t result = 0;
        if (LuaType(L, -1) == LUA_TSTRING)
        {
            if (const char* s = GetLuaString(L, -1); s && *s)
                result = FoxHashes::PathCode64Ext(s);
        }
        LuaPop(L, 1);
        return result;
    }

    void ReadDeclaredAbilities(lua_State* L, int ownerTblIdx,
                               outfit::DeclaredAbilities& out)
    {
        LuaGetField(L, ownerTblIdx, "abilities");
        if (LuaType(L, -1) != LUA_TTABLE)
        {
            SetLuaTop(L, -2);
            return;
        }

        const int tbl = GetLuaTop(L);
        out.declared = true;

        if (TryReadTableBoolField(L, tbl, "quietMovement", false))
            out.suitParamKind = kQuietSuitAbilities;

        out.silentSteps = TryReadTableBoolField(L, tbl, "silentFootsteps", false);
        out.sprint = ReadSprintField(L, tbl);
        out.sprintSpark = ReadSprintSparkField(L, tbl);

        int level = 0;
        if (TryReadTableIntField(L, tbl, "defense", level))
            out.defense = ClampAbilityLevel(level);
        if (TryReadTableIntField(L, tbl, "lifeRecovery", level))
            out.lifeRecovery = ClampAbilityLevel(level);

        const char* rattleName = nullptr;
        const std::uint32_t rattleSwitch =
            ReadSoundSwitchField(L, tbl, "rattleSuit", rattleName);
        if (rattleSwitch != outfit::kSoundSwitchUnset)
            out.rattleSuit = rattleSwitch;

        const char* damageSeName = nullptr;
        const std::uint32_t damageSeSwitch =
            ReadSoundSwitchField(L, tbl, "damageSe", damageSeName);
        if (damageSeSwitch != outfit::kSoundSwitchUnset)
            out.damageSe = damageSeSwitch;

        SetLuaTop(L, -2);
    }

    void ReadBranchAbilities(
        lua_State* L, int branchTblIdx, outfit::OutfitPlayerTypeData& branch)
    {
        LuaGetField(L, branchTblIdx, "abilities");
        if (LuaType(L, -1) != LUA_TTABLE)
        {
            SetLuaTop(L, -2);
            return;
        }

        const int abilitiesTbl = GetLuaTop(L);

        if (TryReadTableBoolField(L, abilitiesTbl, "quietMovement", false))
            branch.suitParamKind = kQuietSuitAbilities;

        branch.abilitySilentSteps =
            TryReadTableBoolField(L, abilitiesTbl, "silentFootsteps", false);
        branch.abilitySprint = ReadSprintField(L, abilitiesTbl);
        branch.abilitySprintSpark = ReadSprintSparkField(L, abilitiesTbl);

        int level = 0;
        if (TryReadTableIntField(L, abilitiesTbl, "defense", level))
            branch.abilityDefense = ClampAbilityLevel(level);
        if (TryReadTableIntField(L, abilitiesTbl, "lifeRecovery", level))
            branch.abilityLifeRecovery = ClampAbilityLevel(level);

        const char* rattleName = nullptr;
        const std::uint32_t rattleSwitch =
            ReadSoundSwitchField(L, abilitiesTbl, "rattleSuit", rattleName);
        if (rattleSwitch != outfit::kSoundSwitchUnset)
            branch.abilityRattleSuit = rattleSwitch;

        const char* damageSeName = nullptr;
        const std::uint32_t damageSeSwitch =
            ReadSoundSwitchField(L, abilitiesTbl, "damageSe", damageSeName);
        if (damageSeSwitch != outfit::kSoundSwitchUnset)
            branch.abilityDamageSe = damageSeSwitch;

        SetLuaTop(L, -2);
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

    static constexpr const char* k_CamoTypeNames[] = {
        "OLIVEDRAB", "SPLITTER", "SQUARE", "TIGERSTRIPE", "GOLDTIGER", "FOXTROT",       // 0-5
        "WOODLAND", "WETWORK", "ARBANGRAY", "ARBANBLUE", "SANDSTORM", "REALTREE",       // 6-11
        "INVISIBLE", "BLACK", "SNEAKING_SUIT_GZ", "SNEAKING_SUIT_TPP", "BATTLEDRESS",   // 12-16
        "PARASITE", "NAKED", "LEATHER", "SOLIDSNAKE", "NINJA", "RAIDEN", "HOSPITAL",    // 17-23
        "GOLD", "SILVER", "PANTHER", "AVATAR_EDIT_MAN", "MGS3", "MGS3_NAKED",           // 24-29
        "MGS3_SNEAKING", "MGS3_TUXEDO", "EVA_CLOSE", "EVA_OPEN", "BOSS_CLOSE",          // 30-34
        "BOSS_OPEN",                                                                    // 35
        "C23", "C24", "C27", "C29", "C30", "C35", "C38", "C39", "C42", "C46", "C49",    // 36-46
        "C52", "C16", "C17", "C18", "C19", "C20", "C22", "C25", "C26", "C28", "C31",    // 47-57
        "C32", "C33", "C36", "C37", "C40", "C41", "C43", "C44", "C45", "C47", "C48",    // 58-68
        "C50", "C51", "C53", "C54", "C55", "C56", "C57", "C58", "C59", "C60",           // 69-78
        "SWIMWEAR_C00", "SWIMWEAR_C01", "SWIMWEAR_C02", "SWIMWEAR_C03", "SWIMWEAR_C05", // 79-83
        "SWIMWEAR_C06", "SWIMWEAR_C38", "SWIMWEAR_C39", "SWIMWEAR_C44", "SWIMWEAR_C46", // 84-88
        "SWIMWEAR_C48", "SWIMWEAR_C53",                                                 // 89-90
        "SWIMWEAR_G_C00", "SWIMWEAR_G_C01", "SWIMWEAR_G_C02", "SWIMWEAR_G_C03",         // 91-94
        "SWIMWEAR_G_C05", "SWIMWEAR_G_C06", "SWIMWEAR_G_C38", "SWIMWEAR_G_C39",         // 95-98
        "SWIMWEAR_G_C44", "SWIMWEAR_G_C46", "SWIMWEAR_G_C48", "SWIMWEAR_G_C53",         // 99-102
        "SWIMWEAR_H_C00", "SWIMWEAR_H_C01", "SWIMWEAR_H_C02", "SWIMWEAR_H_C03",         // 103-106
        "SWIMWEAR_H_C05", "SWIMWEAR_H_C06", "SWIMWEAR_H_C38", "SWIMWEAR_H_C39",         // 107-110
        "SWIMWEAR_H_C44", "SWIMWEAR_H_C46", "SWIMWEAR_H_C48", "SWIMWEAR_H_C53",         // 111-114
        "OCELOT", "QUIET",                                                              // 115-116
    };
    static_assert(sizeof(k_CamoTypeNames) / sizeof(k_CamoTypeNames[0])
                  == static_cast<std::size_t>(outfit::kVanillaCamoTypeMax) + 1,
                  "k_CamoTypeNames must list all 117 vanilla camo types (0..116)");

    static std::int32_t ResolveCamoTypeNameToIndex(const char* name)
    {
        if (!name || !name[0]) return -1;
        for (std::size_t i = 0;
             i < sizeof(k_CamoTypeNames) / sizeof(k_CamoTypeNames[0]); ++i)
        {
            if (_stricmp(k_CamoTypeNames[i], name) == 0)
                return static_cast<std::int32_t>(i);
        }
        return -1;
    }

    bool ReadHeadOptionsArrayInto(
        lua_State* L, int tableIndex,
        std::uint16_t* outIds, std::uint16_t* outIdOrder, std::uint8_t& outCount,
        std::uint64_t* outPendingHashes, std::uint16_t* outPendingOrder,
        std::uint8_t& outPendingCount)
    {
        outCount = 0;
        outPendingCount = 0;

        auto appendId = [&](std::uint16_t equipId, std::uint16_t declaredOrder)
        {
            outIdOrder[outCount] = declaredOrder;
            outIds[outCount++]   = equipId;
        };

        LuaGetField(L, tableIndex, "headOptions");
        const bool present = (LuaType(L, -1) == LUA_TTABLE);
        if (present)
        {
            const std::size_t n   = LuaObjLen(L, -1);
            const std::size_t cap = outfit::kMaxHeadOptionsPerOutfit;
            const std::size_t lim = (n < cap) ? n : cap;

            for (std::size_t i = 1; i <= lim; ++i)
            {
                const auto declaredOrder = static_cast<std::uint16_t>(i - 1);
                LuaRawGetI(L, -1, static_cast<int>(i));
                if (LuaIsNumber(L, -1))
                {
                    const int v = GetLuaInt(L, -1);
                    if (v > 0 && v <= 0xFFFF)
                    {
                        appendId(static_cast<std::uint16_t>(v), declaredOrder);
                    }
                }
                else if (LuaIsString(L, -1))
                {
                    const char* name = GetLuaString(L, -1);
                    if (const std::uint16_t alias = TryResolveHeadAlias(name);
                        alias != 0)
                    {
                        appendId(alias, declaredOrder);
                    }
                    else if (const auto* head =
                             outfit::TryGetCustomHeadByName(name))
                    {
                        appendId(head->equipId, declaredOrder);
                    }
                    else if (name && name[0]
                             && outPendingCount < outfit::kMaxHeadOptionsPerOutfit)
                    {
                        outPendingOrder[outPendingCount] = declaredOrder;
                        outPendingHashes[outPendingCount++] =
                            FoxHashes::StrCode64(name);
                    }
                    else
                    {
                        LogWarn("OutfitLua: headOptions '%s' skipped, empty or too many pending",
                            name ? name : "(null)");
                    }
                }
                LuaPop(L, 1);
            }
        }
        LuaPop(L, 1);
        return present;
    }


    void ReadMotionMtarsInto(
        lua_State* L, int tableIndex,
        std::uint64_t (&dst)[outfit::kMotionMtarSlotCount]);

    constexpr const char* kColorChannelNames[outfit::kColorChannelCount] =
        { "primary", "secondary" };

    bool FormatColorPattern(const char* pattern, unsigned number, std::string& out)
    {
        out.clear();
        bool numbered = false;
        for (const char* p = pattern; *p; ++p)
        {
            if (*p != '%')
            {
                out += *p;
                continue;
            }
            if (p[1] == '%')
            {
                out += '%';
                ++p;
                continue;
            }
            const char* q = p + 1;
            const bool zero = (*q == '0');
            if (zero) ++q;
            int width = 0;
            if (*q >= '1' && *q <= '9') width = *q++ - '0';
            if (*q != 'd' || numbered)
                return false;
            char digits[16];
            std::snprintf(digits, sizeof(digits), zero ? "%0*u" : "%*u", width, number);
            out += digits;
            numbered = true;
            p = q;
        }
        return numbered;
    }

    bool MakeColor(const char* fv2, const char* fpk, const char* icon, outfit::OutfitColor& out)
    {
        if (!fv2 || !fv2[0])
            return false;
        if (!PathHasRequiredExt(fv2, ".fv2"))
        {
            LogWarn("OutfitLua: colors fv2 '%s' must end in '.fv2', colour skipped", fv2);
            return false;
        }
        if (fpk && fpk[0] && !PathHasRequiredExt(fpk, ".fpk"))
        {
            LogWarn("OutfitLua: colors fpk '%s' must end in '.fpk', colour skipped", fpk);
            return false;
        }
        out.fv2     = fv2;
        out.fv2Code = FoxHashes::PathCode64Ext(fv2);
        out.fpkCode = (fpk && fpk[0]) ? FoxHashes::PathCode64Ext(fpk) : 0;
        out.icon    = icon ? icon : "";
        return true;
    }

    void ReadColorChannel(lua_State* L, int channelTbl, const char* channelName,
                          outfit::OutfitColorList& out)
    {
        out.clear();
        int count = 0;
        if (TryReadTableIntField(L, channelTbl, "count", count))
        {
            const char* fv2Pattern  = nullptr;
            const char* fpkPattern  = nullptr;
            const char* iconPattern = nullptr;
            TryReadTableStringField(L, channelTbl, "fv2", fv2Pattern);
            TryReadTableStringField(L, channelTbl, "fpk", fpkPattern);
            TryReadTableStringField(L, channelTbl, "icon", iconPattern);
            if (count < 1 || !fv2Pattern)
            {
                LogWarn("OutfitLua: colors.%s needs count >= 1 and an fv2 pattern, channel skipped",
                    channelName);
                return;
            }
            if (static_cast<std::size_t>(count) > outfit::kMaxColorsPerChannel)
            {
                LogWarn("OutfitLua: colors.%s count %d over %zu, extra colours dropped",
                    channelName, count, outfit::kMaxColorsPerChannel);
                count = static_cast<int>(outfit::kMaxColorsPerChannel);
            }
            std::string fv2, fpk, icon;
            for (int i = 0; i < count; ++i)
            {
                const auto n = static_cast<unsigned>(i);
                if (!FormatColorPattern(fv2Pattern, n, fv2)
                    || (fpkPattern && !FormatColorPattern(fpkPattern, n, fpk))
                    || (iconPattern && !FormatColorPattern(iconPattern, n, icon)))
                {
                    LogWarn("OutfitLua: colors.%s patterns need exactly one %%d (or %%0Nd), channel skipped",
                        channelName);
                    out.clear();
                    return;
                }
                outfit::OutfitColor color;
                if (MakeColor(fv2.c_str(), fpkPattern ? fpk.c_str() : nullptr,
                              iconPattern ? icon.c_str() : nullptr, color))
                    out.push_back(std::move(color));
            }
            return;
        }

        const std::size_t n = LuaObjLen(L, channelTbl);
        for (std::size_t i = 1; i <= n; ++i)
        {
            if (out.size() >= outfit::kMaxColorsPerChannel)
            {
                LogWarn("OutfitLua: colors.%s over %zu colours, extra colours dropped",
                    channelName, outfit::kMaxColorsPerChannel);
                break;
            }
            LuaRawGetI(L, channelTbl, static_cast<int>(i));
            const int entryTbl = GetLuaTop(L);
            if (LuaType(L, entryTbl) == LUA_TTABLE)
            {
                const char* fv2  = nullptr;
                const char* fpk  = nullptr;
                const char* icon = nullptr;
                TryReadTableStringField(L, entryTbl, "fv2", fv2);
                TryReadTableStringField(L, entryTbl, "fpk", fpk);
                TryReadTableStringField(L, entryTbl, "icon", icon);
                outfit::OutfitColor color;
                if (MakeColor(fv2, fpk, icon, color))
                    out.push_back(std::move(color));
                else if (!fv2)
                    LogWarn("OutfitLua: colors.%s entry %zu has no fv2, skipped",
                        channelName, i);
            }
            LuaPop(L, 1);
        }
    }

    bool ReadColorsInto(lua_State* L, int ownerTbl,
                        outfit::OutfitColorList (&dst)[outfit::kColorChannelCount])
    {
        LuaGetField(L, ownerTbl, "colors");
        const int colorsTbl = GetLuaTop(L);
        if (LuaType(L, colorsTbl) != LUA_TTABLE)
        {
            LuaPop(L, 1);
            return false;
        }
        for (std::size_t ch = 0; ch < outfit::kColorChannelCount; ++ch)
        {
            LuaGetField(L, colorsTbl, kColorChannelNames[ch]);
            if (LuaType(L, -1) == LUA_TTABLE)
                ReadColorChannel(L, GetLuaTop(L), kColorChannelNames[ch], dst[ch]);
            LuaPop(L, 1);
        }
        LuaPop(L, 1);
        return true;
    }

    void ReadVariantsArrayInto(
        lua_State* L, int branchTblIdx, outfit::OutfitPlayerTypeData& branch)
    {
        branch.variantCount = 0;

        LuaGetField(L, branchTblIdx, "variants");
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
                    ReadDeclaredAbilities(L, GetLuaTop(L), v.abilities);
                    v.partsPathCode64 = ReadRequiredPathField(L, -1, "partsPath");
                    v.fpkPathCode64   = ReadRequiredPathField(L, -1, "fpkPath");
                    v.camoFpk         = ReadSubAssetField(L, -1, "camoFpk",
                                            outfit::kSubAssetUseVanilla);
                    v.camoFv2         = ReadSubAssetField(L, -1, "camoFv2",
                                            outfit::kSubAssetDisabled);
                    v.diamondFpk      = ReadSubAssetField(L, -1, "diamondFpk",
                                            outfit::kSubAssetDisabled);
                    v.diamondFv2      = ReadSubAssetField(L, -1, "diamondFv2",
                                            outfit::kSubAssetDisabled);
                    v.voiceFpk        = ReadSubAssetField(L, -1, "voiceFpk",
                                            outfit::kSubAssetUseVanilla);
                    {
                        const char* voiceTypeName = nullptr;
                        v.voiceType = ReadSoundSwitchField(
                            L, GetLuaTop(L), "voiceType", voiceTypeName);
                    }
                    v.voiceFpkByTypeCount = ReadVoiceFpkByTypeField(
                        L, GetLuaTop(L), v.voiceFpkByType,
                        outfit::kMaxVoiceFpkByType);

                    ReadMotionMtarsInto(L, GetLuaTop(L), v.motionMtars);

                    LuaGetField(L, -1, "displayName");
                    if (LuaType(L, -1) == LUA_TSTRING)
                    {
                        if (const char* s = GetLuaString(L, -1); s && *s)
                            v.displayNameHash = FoxHashes::StrCode64(s);
                    }
                    LuaPop(L, 1);

                    v.iconPathHash = ReadIconFtexPathField(L, -1);

                    LuaGetField(L, -1, "enableArm");
                    if (LuaType(L, -1) != 0)
                    {
                        v.hasEnableArm = true;
                        v.enableArm    = GetLuaBool(L, -1) != 0;
                    }
                    LuaPop(L, 1);

                    LuaGetField(L, -1, "enableHead");
                    if (LuaType(L, -1) != 0)
                    {
                        v.hasEnableHead = true;
                        v.enableHead    = GetLuaBool(L, -1) != 0;
                    }
                    LuaPop(L, 1);

                    v.hasEyeFv2 = ReadEyeFv2Field(L, GetLuaTop(L), v.eyeFv2);
                    v.hasEyeColor = ReadEyeColorField(L, GetLuaTop(L), v.eyeColor);

                    {
                        const int variantTbl = GetLuaTop(L);
                        std::uint8_t vHeadCount    = 0;
                        std::uint8_t vPendingCount = 0;
                        const bool declared = ReadHeadOptionsArrayInto(
                            L, variantTbl, v.headOptionEquipIds, v.headOptionOrder,
                            vHeadCount, v.pendingHeadNameHashes, v.pendingHeadOrder,
                            vPendingCount);
                        v.headOptionsDeclared = declared;
                        v.headOptionCount     = vHeadCount;
                        v.pendingHeadCount    = vPendingCount;
                    }

                    v.colorsDeclared = ReadColorsInto(L, GetLuaTop(L), v.colors);


                    if (branch.defaultVariant == 0
                        && TryReadTableBoolField(L, GetLuaTop(L), "default", false))
                        branch.defaultVariant = static_cast<std::uint8_t>(i);

                    branch.EnsureVar(i) = v;
                    maxFilledSlot      = static_cast<std::uint8_t>(i);
                }
                LuaPop(L, 1);
            }

            if (maxFilledSlot > 0)
            {


                branch.variantCount = static_cast<std::uint8_t>(maxFilledSlot + 1);
            }
        }
        LuaPop(L, 1);
    }

    void ReadBranchCamoBonusValues(
        lua_State* L, int tableIndex, outfit::OutfitPlayerTypeData& branch)
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
                branch.camoBonusValues[materialIdx] = GetLuaInt(L, -1);
                ++writeCount;
            }

            SetLuaTop(L, -2);
        }

        if (writeCount > 0)
            branch.hasCamoBonusValues = true;

        SetLuaTop(L, -2);
    }

    void ReadMotionMtarsInto(
        lua_State* L, int tableIndex,
        std::uint64_t (&dst)[outfit::kMotionMtarSlotCount])
    {
        LuaGetField(L, tableIndex, "motionMtars");
        if (LuaType(L, -1) != LUA_TTABLE)
        {
            SetLuaTop(L, -2);
            return;
        }

        const int tbl = GetLuaTop(L);

        LuaPushNil(L);
        while (LuaNext(L, tbl) != 0)
        {
            const char* keyName =
                (LuaType(L, -2) == LUA_TSTRING) ? GetLuaString(L, -2) : nullptr;
            const int slot = outfit::MotionMtarSlotFromName(keyName);

            if (slot < 0)
            {
                LogWarn("OutfitLua: motionMtars '%s' is not a player2 motion archive, ignored",
                    keyName ? keyName : "(non-string key)");
            }
            else if (LuaType(L, -1) == LUA_TBOOLEAN)
            {
                LogWarn("OutfitLua: motionMtars.%s is a boolean, ignored", keyName);
            }
            else if (LuaType(L, -1) == LUA_TSTRING)
            {
                if (const char* s = GetLuaString(L, -1); s && *s)
                {
                    const std::uint64_t hash = FoxHashes::PathCode64Ext(s);
                    dst[slot] = hash;
                    outfit::RegisterMotionMtarOverrideHash(hash, slot);
                }
            }

            SetLuaTop(L, -2);
        }

        SetLuaTop(L, -2);
    }

    bool ReadPlayerTypeBranchTable(
        lua_State* L, int branchTblIdx, outfit::OutfitPlayerTypeData& branch)
    {
        if (LuaType(L, branchTblIdx) != LUA_TTABLE) return false;

        branch                 = outfit::OutfitPlayerTypeData{};
        branch.partsPathCode64 = ReadRequiredPathField(L, branchTblIdx, "partsPath");
        branch.fpkPathCode64   = ReadRequiredPathField(L, branchTblIdx, "fpkPath");

        if (branch.partsPathCode64 == 0 || branch.fpkPathCode64 == 0)
            return false;

        branch.camoFpk    = ReadSubAssetField(L, branchTblIdx, "camoFpk",
                                outfit::kSubAssetDisabled);
        branch.faceFpk    = ReadSubAssetField(L, branchTblIdx, "faceFpk",
                                outfit::kSubAssetUseVanilla);
        branch.skinFv2    = ReadSubAssetField(L, branchTblIdx, "skinFv2",
                                outfit::kSubAssetUseVanilla);
        branch.diamondFpk = ReadSubAssetField(L, branchTblIdx, "diamondFpk",
                                outfit::kSubAssetDisabled);
        branch.voiceFpk   = ReadSubAssetField(L, branchTblIdx, "voiceFpk",
                                outfit::kSubAssetUseVanilla);
        {
            const char* voiceTypeName = nullptr;
            branch.voiceType = ReadSoundSwitchField(
                L, branchTblIdx, "voiceType", voiceTypeName);
        }
        branch.voiceFpkByTypeCount = ReadVoiceFpkByTypeField(
            L, branchTblIdx, branch.voiceFpkByType,
            outfit::kMaxVoiceFpkByType);
        branch.camoFv2    = ReadSubAssetField(L, branchTblIdx, "camoFv2",
                                outfit::kSubAssetDisabled);
        branch.diamondFv2 = ReadSubAssetField(L, branchTblIdx, "diamondFv2",
                                outfit::kSubAssetDisabled);
        ReadMotionMtarsInto(L, branchTblIdx, branch.motionMtars);


        branch.enableArm  = TryReadTableBoolField(L, branchTblIdx, "enableArm",  true);
        branch.enableHead = TryReadTableBoolField(L, branchTblIdx, "enableHead", true);
        ReadEyeFv2Field(L, branchTblIdx, branch.eyeFv2);
        ReadEyeColorField(L, branchTblIdx, branch.eyeColor);

        ReadBranchAbilities(L, branchTblIdx, branch);


        LuaGetField(L, branchTblIdx, "displayName");
        if (LuaType(L, -1) == LUA_TSTRING)
        {
            if (const char* s = GetLuaString(L, -1); s && *s)
                branch.baseDisplayNameHash = FoxHashes::StrCode64(s);
        }
        LuaPop(L, 1);

        branch.baseIconPathHash = ReadIconFtexPathField(L, branchTblIdx);


        ReadVariantsArrayInto(L, branchTblIdx, branch);

        std::uint8_t branchHeadCount    = 0;
        std::uint8_t branchPendingCount = 0;
        ReadHeadOptionsArrayInto(L, branchTblIdx,
            branch.headOptionEquipIds, branch.headOptionOrder, branchHeadCount,
            branch.pendingHeadNameHashes, branch.pendingHeadOrder, branchPendingCount);
        branch.headOptionCount     = branchHeadCount;
        branch.pendingHeadCount    = branchPendingCount;
        branch.supportsHeadOptions = (branchHeadCount > 0);


        {
            LuaGetField(L, branchTblIdx, "camoBonusType");
            const int cbtType = LuaType(L, -1);
            std::int32_t camoIdx = -1;
            if (cbtType == LUA_TNUMBER)
            {
                const int n = GetLuaInt(L, -1);
                if (n >= 0 && n <= outfit::kVanillaCamoTypeMax) camoIdx = n;
            }
            else if (cbtType == LUA_TSTRING)
            {
                const char* nm = GetLuaString(L, -1);
                camoIdx = ResolveCamoTypeNameToIndex(nm);
                if (camoIdx < 0)
                    LogWarn("OutfitLua: camoBonusType '%s' unknown, ignored",
                        nm ? nm : "(null)");
            }
            SetLuaTop(L, -2);
            if (camoIdx >= 0 && camoIdx <= outfit::kVanillaCamoTypeMax)
                branch.camoBonusType = static_cast<std::uint8_t>(camoIdx);
        }
        branch.camoBonusTypeDeclared = branch.camoBonusType;
        ReadBranchCamoBonusValues(L, branchTblIdx, branch);

        ReadColorsInto(L, branchTblIdx, branch.colors);


        branch.used = true;
        return true;
    }
}

int __cdecl l_RegisterOutfit(lua_State* L)
{
    V_FrameWorkState::SaveBatch _saveBatch;

    if (LuaType(L, 1) != LUA_TTABLE)
    {
        LogWarn("RegisterOutfit: argument #1 must be a table");
        PushLuaBool(L, false);
        return 1;
    }

    const auto defStore = std::make_unique<outfit::OutfitDefinition>();
    outfit::OutfitDefinition& def = *defStore;


    const char* key = nullptr;
    TryReadTableStringField(L, 1, "key", key);
    def.key = key;

    if (!key || !key[0])
    {
        LogWarn("RegisterOutfit: missing 'key', not registered");
        PushLuaBool(L, false);
        return 1;
    }
    WarnTopLevelAbilities(L, "RegisterOutfit", key);
    std::uint8_t branchCount = 0;
    ForEachPlayerTypeBranch(L, 1, "RegisterOutfit", key,
        [&](std::uint8_t playerType, const char* branchName, int branchIdx)
    {
        const auto branchStore =
            std::make_unique<outfit::OutfitPlayerTypeData>();
        outfit::OutfitPlayerTypeData& branch = *branchStore;
        if (ReadPlayerTypeBranchTable(L, branchIdx, branch))
        {
            def.perPlayerType[playerType] = branch;
            ++branchCount;
        }
        else
        {
            LogWarn("RegisterOutfit '%s': branch %s missing partsPath/fpkPath, skipped",
                key, branchName);
        }
    });

    if (branchCount == 0)
    {
        LogWarn("RegisterOutfit '%s': no valid playerType branch, not registered", key);
        PushLuaBool(L, false);
        return 1;
    }

    {
        std::int32_t newId = 0;
        if (V_FrameWorkState::ResolveOrCreateDevelopId(key, 0, newId)
            && newId > 0 && newId <= 0xFFFF)
        {
            def.developId = static_cast<std::uint16_t>(newId);
        }
        V_FrameWorkState::SetRowKind(key, V_FrameWorkState::kRowKindOutfit);
    }
    if (def.developId == 0)
    {
        LogError("RegisterOutfit '%s': developId allocation failed, not registered", key);
        PushLuaBool(L, false);
        return 1;
    }

    def.flowIndex = 0;


    if (def.partsTypeHint == 0xFF)
    {
        const std::uint8_t pp = V_FrameWorkState::GetPersistedOutfitPartsType(key);
        if (pp != 0) def.partsTypeHint = pp;
    }
    if (def.selectorCodeHint == 0xFF)
    {
        const std::uint8_t ps = V_FrameWorkState::GetPersistedOutfitSelector(key);
        if (ps != 0) def.selectorCodeHint = ps;
    }
    {
        std::uint8_t persisted[V_FrameWorkState::kPersistedVariantSelectorSlots] = {};
        V_FrameWorkState::GetPersistedOutfitVariantSelectors(
            key, persisted, sizeof(persisted));
        for (std::size_t i = 0;
             i < V_FrameWorkState::kPersistedVariantSelectorSlots; ++i)
            def.variantSelectorHints[i + 1] = persisted[i];
    }

    std::uint8_t allocatedPartsType = 0xFF;
    const bool ok = outfit::RegisterOutfit(def, &allocatedPartsType);

    if (!ok)
    {
        PushLuaBool(L, false);
        return 1;
    }

    std::uint8_t finalSelector = 0;
    const outfit::OutfitEntry* e = nullptr;
    if (outfit::TryGetOutfitByDevelopId(def.developId, &e) && e)
        finalSelector = e->selectorCode;

    bool hasColors = false;
    for (const auto& branch : def.perPlayerType)
    {
        for (const auto& list : branch.colors)
            hasColors = hasColors || !list.empty();
        for (const auto& variant : branch.variants)
            hasColors = hasColors || variant.colorsDeclared;
    }
    if (hasColors)
        for (std::uint8_t pt = 0; pt < outfit::kPlayerTypeMax; ++pt)
            for (std::size_t ch = 0; ch < outfit::kColorChannelCount; ++ch)
                outfit::GetSavedColor(key, pt, ch);   // a saved choice nobody reads in a session is dropped after a few launches

    LogInfo("RegisterOutfit %s partsType:0x%02X developId:%u selector:0x%02X",
        key, static_cast<unsigned>(allocatedPartsType),
        static_cast<unsigned>(def.developId),
        static_cast<unsigned>(finalSelector));

    PushLuaNumber(L, static_cast<float>(allocatedPartsType));
    PushLuaNumber(L, static_cast<float>(def.developId));
    PushLuaNumber(L, static_cast<float>(def.flowIndex));
    return 3;
}


namespace
{
    struct HeadLayerKey
    {
        const char*  name;
        std::uint8_t bit;
        const char*  pathName;
        std::uint64_t outfit::HeadLayerPaths::*path;
        const char*  fpkName;
        std::uint64_t outfit::HeadLayerPaths::*fpk;
    };
    static constexpr HeadLayerKey k_HeadLayerKeys[] = {
        { "enableFaceFova", outfit::kHeadLayerKeepFace,
          "faceFovaPath", &outfit::HeadLayerPaths::faceFova,
          "faceFovaFpkPath", &outfit::HeadLayerPaths::faceFovaFpk },
        { "enableFaceDecoFova", outfit::kHeadLayerKeepFaceDeco,
          "faceDecoFovaPath", &outfit::HeadLayerPaths::faceDecoFova,
          "faceDecoFovaFpkPath", &outfit::HeadLayerPaths::faceDecoFovaFpk },
        { "enableHairFova", outfit::kHeadLayerKeepHair,
          "hairFovaPath", &outfit::HeadLayerPaths::hairFova,
          "hairFovaFpkPath", &outfit::HeadLayerPaths::hairFovaFpk },
        { "enableHairDecoFova", outfit::kHeadLayerKeepHairDeco,
          "hairDecoFovaPath", &outfit::HeadLayerPaths::hairDecoFova,
          "hairDecoFovaFpkPath", &outfit::HeadLayerPaths::hairDecoFovaFpk },
    };

    constexpr int kFaceUnitCount = 900;

    std::uint8_t ReadHeadLayerFlags(lua_State* L, int tableIdx, const char* key,
                                    const char* where, bool allowed)
    {
        std::uint8_t flags = 0;
        for (const HeadLayerKey& k : k_HeadLayerKeys)
        {
            LuaGetField(L, tableIdx, const_cast<char*>(k.name));
            const int type = LuaType(L, -1);
            if (type != LUA_TNIL && type != LUA_TNONE)
            {
                if (!allowed)
                {
                    LogWarn("RegisterHeadOption '%s': %s only read in ddMale/ddFemale, "
                        "ignored in %s", key, k.name, where);
                }
                else if (type != LUA_TBOOLEAN)
                {
                    LogWarn("RegisterHeadOption '%s': %s.%s is not a boolean, ignored",
                        key, where, k.name);
                }
                else
                {
                    flags |= outfit::kHeadLayerDeclared;
                    if (GetLuaBool(L, -1) != 0)
                        flags |= k.bit;
                }
            }
            SetLuaTop(L, -2);
        }
        return flags;
    }

    outfit::HeadLayerPaths ReadHeadLayerPaths(lua_State* L, int tableIdx,
        const char* key, const char* where, bool allowed, std::uint8_t flags)
    {
        outfit::HeadLayerPaths paths{};
        for (const HeadLayerKey& k : k_HeadLayerKeys)
        {
            for (int pack = 0; pack < 2; ++pack)
            {
                const char* field = pack ? k.fpkName : k.pathName;
                LuaGetField(L, tableIdx, const_cast<char*>(field));
                const int type = LuaType(L, -1);
                if (type != LUA_TNIL && type != LUA_TNONE)
                {
                    if (!allowed)
                        LogWarn("RegisterHeadOption '%s': %s only read in ddMale/ddFemale, "
                            "ignored in %s", key, field, where);
                    else if (type != LUA_TSTRING)
                        LogWarn("RegisterHeadOption '%s': %s.%s must be a %s path string, "
                            "ignored", key, where, field, pack ? "FPK" : "FV2");
                    else if (const char* value = GetLuaString(L, -1); value && value[0])
                    {
                        const std::uint64_t code = FoxHashes::PathCode64Ext(value);
                        if (!(pack ? outfit::IsFpkPathCode(code) : outfit::IsFv2PathCode(code)))
                            LogWarn("RegisterHeadOption '%s': %s.%s '%s' must end in %s, "
                                "ignored", key, where, field, value,
                                pack ? ".fpk or .fpkd" : ".fv2");
                        else if ((flags & k.bit) == 0)
                            LogWarn("RegisterHeadOption '%s': %s.%s ignored, %s is not true",
                                key, where, field, k.name);
                        else
                            paths.*(pack ? k.fpk : k.path) = code;
                    }
                }
                SetLuaTop(L, -2);
            }
            if (paths.*(k.fpk) && !(paths.*(k.path)))
            {
                LogWarn("RegisterHeadOption '%s': %s.%s needs a valid %s, ignored",
                    key, where, k.fpkName, k.pathName);
                paths.*(k.fpk) = 0;
            }
        }
        return paths;
    }
}

int __cdecl l_RegisterHeadOption(lua_State* L)
{
    if (LuaType(L, 1) != LUA_TTABLE)
    {
        LogWarn("RegisterHeadOption: argument #1 must be a table");
        PushLuaNumber(L, 0);
        return 1;
    }

    const char* key = nullptr;
    TryReadTableStringField(L, 1, "key", key);
    if (!key || !key[0])
    {
        LogWarn("RegisterHeadOption: missing 'key', not registered");
        PushLuaNumber(L, 0);
        return 1;
    }

    std::uint16_t faceIds[outfit::kPlayerTypeMax] = {};
    std::uint64_t faceFv2Codes[outfit::kPlayerTypeMax] = {};
    std::uint64_t faceFpkCodes[outfit::kPlayerTypeMax] = {};
    std::uint8_t  faceLayerFlags[outfit::kPlayerTypeMax] = {};
    outfit::HeadLayerPaths layerPaths[outfit::kPlayerTypeMax] = {};
    std::uint64_t snakeStageFv2[outfit::kSnakeFaceStageCount] = {};
    std::uint64_t snakeStageFpk[outfit::kSnakeFaceStageCount] = {};
    bool          haveSnakeStages = false;
    std::uint8_t  branchMask = 0;
    outfit::HeadColorSet headColors[outfit::kPlayerTypeMax];
    bool          hasHeadColors = false;
    ForEachPlayerTypeBranch(L, 1, "RegisterHeadOption", key,
        [&](std::uint8_t playerType, const char* branchName, int branchIdx)
    {
        const bool isDd = outfit::IsDdPlayerType(playerType);
        branchMask |= static_cast<std::uint8_t>(1u << playerType);

        faceLayerFlags[playerType] =
            ReadHeadLayerFlags(L, branchIdx, key, branchName, isDd);
        layerPaths[playerType] = ReadHeadLayerPaths(L, branchIdx, key, branchName,
            isDd, faceLayerFlags[playerType]);

        if (ReadColorsInto(L, branchIdx, headColors[playerType].colors))
        {
            bool packs = false;
            for (outfit::OutfitColorList& list : headColors[playerType].colors)
                for (outfit::OutfitColor& color : list)
                {
                    packs = packs || color.fpkCode != 0;
                    color.fpkCode = 0;
                    hasHeadColors = true;
                }
            if (packs)
                LogWarn("RegisterHeadOption '%s': %s colours load no fpk, put their fv2s and textures in the head's fpk",
                    key, branchName);
        }

        const char* branchFv2 = nullptr;
        const char* branchFpk = nullptr;
        const bool givesFv2 =
            TryReadTableStringField(L, branchIdx, "fv2", branchFv2) && branchFv2
            && branchFv2[0];
        const bool givesFpk =
            TryReadTableStringField(L, branchIdx, "fpk", branchFpk) && branchFpk
            && branchFpk[0];
        const bool givesModelPair = givesFv2 && givesFpk;

        int rawBranchFace = 0;
        const bool haveBranchFace =
            TryReadTableIntField(L, branchIdx, "TppEnemyFaceId", rawBranchFace);
        const bool validBranchFace =
            haveBranchFace && rawBranchFace > 0 && rawBranchFace < kFaceUnitCount;
        const unsigned fallbackFace = (playerType == outfit::kPlayerType_DDMale)
            ? outfit::kDefaultTppEnemyFaceIdMale : outfit::kDefaultTppEnemyFaceId;
        if (isDd && givesFv2 != givesFpk)
            LogWarn("RegisterHeadOption '%s': %s gives %s but no %s, using face 0x%X",
                key, branchName, givesFv2 ? "fv2" : "fpk", givesFv2 ? "fpk" : "fv2",
                validBranchFace ? static_cast<unsigned>(rawBranchFace) : fallbackFace);
        if (validBranchFace)
        {
            faceIds[playerType] =
                static_cast<std::uint16_t>(rawBranchFace);
        }
        else if (isDd && haveBranchFace)
        {
            LogWarn("RegisterHeadOption '%s': %s.TppEnemyFaceId %d not in 1..%d, %s face 0x%X",
                key, branchName, rawBranchFace, kFaceUnitCount - 1,
                givesModelPair ? "fallback" : "using", fallbackFace);
        }
        else if (isDd && !givesFv2 && !givesFpk)
        {
            LogWarn("RegisterHeadOption '%s': %s has no TppEnemyFaceId, using face 0x%X",
                key, branchName, fallbackFace);
        }

        const char* fv2 = nullptr;
        if (TryReadTableStringField(L, branchIdx, "fv2", fv2) && fv2 && fv2[0])
            faceFv2Codes[playerType] = FoxHashes::PathCode64Ext(fv2);
        const char* fpk = nullptr;
        if (TryReadTableStringField(L, branchIdx, "fpk", fpk) && fpk && fpk[0])
            faceFpkCodes[playerType] = FoxHashes::PathCode64Ext(fpk);

        if (layerPaths[playerType].faceFova
            && !outfit::IsFpkPathCode(layerPaths[playerType].faceFovaFpk
                ? layerPaths[playerType].faceFovaFpk : faceFpkCodes[playerType]))
        {
            LogWarn("RegisterHeadOption '%s': %s.faceFovaPath needs a .fpk/.fpkd "
                "faceFovaFpkPath or fpk, ignored", key, branchName);
            layerPaths[playerType].faceFova = 0;
        }

        if (isDd && givesModelPair
            && (!outfit::IsFv2PathCode(faceFv2Codes[playerType])
                || !outfit::IsFpkPathCode(faceFpkCodes[playerType])))
            LogWarn("RegisterHeadOption '%s': %s fv2 '%s'/fpk '%s' need .fv2 and .fpk/.fpkd, "
                "using face 0x%X", key, branchName, fv2 ? fv2 : "", fpk ? fpk : "",
                validBranchFace ? static_cast<unsigned>(rawBranchFace) : fallbackFace);

        LuaGetField(L, branchIdx, "motion");
        if (LuaType(L, -1) == LUA_TTABLE)
        {
            const int motionIdx = GetLuaTop(L);
            const char* mtarPath = nullptr;
            std::vector<outfit::headmotion::Clip> clips;
            if (TryReadTableStringField(L, motionIdx, "mtar", mtarPath) && mtarPath && mtarPath[0])
            {
                LuaGetField(L, motionIdx, "clips");
                if (LuaType(L, -1) == LUA_TTABLE)
                {
                    const int clipsIdx = GetLuaTop(L);
                    g_lua_pushnil(L);
                    while (g_lua_next(L, clipsIdx) != 0)
                    {
                        if (LuaType(L, -2) == LUA_TSTRING && LuaType(L, -1) == LUA_TSTRING)
                        {
                            outfit::headmotion::Clip clip;
                            clip.name = GetLuaString(L, -2);
                            clip.code = outfit::headmotion::ParseClipId(GetLuaString(L, -1));
                            if (clip.code)
                                clips.push_back(std::move(clip));
                        }
                        LuaPop(L, 1);
                    }
                }
                LuaPop(L, 1);
            }

            std::vector<outfit::headmotion::Effect> effects;
            LuaGetField(L, motionIdx, "effects");
            if (LuaType(L, -1) == LUA_TTABLE)
            {
                const int effectsIdx = GetLuaTop(L);
                g_lua_pushnil(L);
                while (g_lua_next(L, effectsIdx) != 0)
                {
                    if (LuaType(L, -2) == LUA_TSTRING && LuaType(L, -1) == LUA_TTABLE)
                    {
                        const int fxIdx = GetLuaTop(L);
                        outfit::headmotion::Effect fx;
                        fx.name = GetLuaString(L, -2);
                        const char* vfxPath = nullptr;
                        const char* point   = nullptr;
                        const char* clip    = nullptr;
                        TryReadTableStringField(L, fxIdx, "vfx", vfxPath);
                        TryReadTableStringField(L, fxIdx, "point", point);
                        TryReadTableStringField(L, fxIdx, "clip", clip);
                        if (vfxPath && vfxPath[0])
                        {
                            fx.vfx   = FoxHashes::PathCode64Ext(vfxPath);
                            fx.point = outfit::headmotion::ParseEffectPoint(point);
                            if (clip)
                                fx.clip = clip;
                            effects.push_back(std::move(fx));
                        }
                        else
                        {
                            LogWarn("RegisterHeadOption '%s': %s.motion.effects.%s needs vfx, skipped",
                                key, branchName, fx.name.c_str());
                        }
                    }
                    LuaPop(L, 1);
                }
            }
            LuaPop(L, 1);

            const bool hasClips   = mtarPath && mtarPath[0] && !clips.empty();
            const bool hasEffects = !effects.empty();
            if (hasClips)
                outfit::headmotion::SetBranchMotion(key, playerType,
                    FoxHashes::PathCode64Ext(mtarPath), std::move(clips));
            if (hasEffects)
                outfit::headmotion::SetBranchEffects(key, playerType, std::move(effects));
            const char* restClip = nullptr;
            if (TryReadTableStringField(L, motionIdx, "rest", restClip) && restClip && restClip[0])
                outfit::headmotion::SetBranchRest(key, playerType, restClip);
            if (!hasClips && ((mtarPath && mtarPath[0]) || !hasEffects))
                LogWarn("RegisterHeadOption '%s': %s.motion needs mtar and clips, ignored",
                    key, branchName);
        }
        LuaPop(L, 1);

        if (playerType == outfit::kPlayerType_Snake)
        {
            LuaGetField(L, branchIdx, "faceStages");
            if (LuaType(L, -1) == LUA_TTABLE)
            {
                for (std::uint8_t s = 0;
                     s < outfit::kSnakeFaceStageCount; ++s)
                {
                    LuaRawGetI(L, -1, static_cast<int>(s) + 1);
                    if (LuaType(L, -1) == LUA_TTABLE)
                    {
                        const char* sFv2 = nullptr;
                        if (TryReadTableStringField(L, -1, "fv2", sFv2)
                            && sFv2 && sFv2[0])
                        {
                            snakeStageFv2[s] =
                                FoxHashes::PathCode64Ext(sFv2);
                            haveSnakeStages = true;
                        }
                        const char* sFpk = nullptr;
                        if (TryReadTableStringField(L, -1, "fpk", sFpk)
                            && sFpk && sFpk[0])
                        {
                            snakeStageFpk[s] =
                                FoxHashes::PathCode64Ext(sFpk);
                            haveSnakeStages = true;
                        }
                    }
                    LuaPop(L, 1);
                }
            }
            LuaPop(L, 1);
        }
    });

    bool showInDevelopMenu = false;
    {
        LuaGetField(L, 1, "showInDevelopMenu");
        if (LuaType(L, -1) == 1 )
            showInDevelopMenu = (GetLuaBool(L, -1) != 0);
        SetLuaTop(L, -2);
    }

    bool infiniteAmmo = false;
    {
        LuaGetField(L, 1, "abilities");
        if (LuaType(L, -1) == LUA_TTABLE)
            infiniteAmmo = TryReadTableBoolField(L, GetLuaTop(L),
                                                 "infiniteAmmo", false);
        SetLuaTop(L, -2);
    }

    ReadHeadLayerFlags(L, 1, key, "the top level", false);
    ReadHeadLayerPaths(L, 1, key, "the top level", false, 0);

    const std::uint16_t equipId = outfit::RegisterHeadOption(
        key, faceIds, faceFv2Codes, faceFpkCodes, showInDevelopMenu,
        faceLayerFlags, layerPaths);

    outfit::SetCustomHeadInfiniteAmmo(key, infiniteAmmo);
    outfit::SetCustomHeadBranchMask(key, branchMask);
    for (std::uint8_t pt = 0; pt < outfit::kPlayerTypeMax; ++pt)
    {
        bool colored = false;
        for (const outfit::OutfitColorList& list : headColors[pt].colors)
            colored = colored || !list.empty();
        bool model = outfit::IsFv2PathCode(faceFv2Codes[pt]) && outfit::IsFpkPathCode(faceFpkCodes[pt]);
        for (std::uint8_t s = 0; pt == outfit::kPlayerType_Snake && s < outfit::kSnakeFaceStageCount; ++s)
            model = model || outfit::IsFv2PathCode(snakeStageFv2[s]);
        if (!colored || model)
            continue;
        LogWarn("RegisterHeadOption '%s': colours on player type %u need the branch's own fv2 and fpk, ignored",
            key, static_cast<unsigned>(pt));
        headColors[pt] = outfit::HeadColorSet{};
    }
    outfit::SetCustomHeadColors(key, headColors);
    if (hasHeadColors)
        for (std::uint8_t pt = 0; pt < outfit::kPlayerTypeMax; ++pt)
            for (std::size_t ch = 0; ch < outfit::kColorChannelCount; ++ch)
                outfit::GetSavedColor(key, pt, ch);

    if (haveSnakeStages)
        outfit::SetCustomHeadSnakeFaceStages(key, snakeStageFv2, snakeStageFpk);

    PushLuaNumber(L, static_cast<float>(equipId));
    return 1;
}

namespace
{
    void Shim_LuaSetTop(lua_State* L, int idx)            { SetLuaTop(L, idx); }
    void Shim_LuaPushString(lua_State* L, const char* s)
    {
        if (ResolveLuaApi() && g_lua_pushstring)
            g_lua_pushstring(L, const_cast<char*>(s));
    }
    void Shim_LuaCreateTable(lua_State* L, int narr, int nrec)
    {
        if (ResolveLuaApi() && g_lua_createtable)
            g_lua_createtable(L, narr, nrec);
    }
    void Shim_LuaGetTable(lua_State* L, int idx)
    {
        if (ResolveLuaApi() && g_lua_gettable)
            g_lua_gettable(L, idx);
    }
    void Shim_LuaSetTable(lua_State* L, int idx)
    {
        if (ResolveLuaApi() && g_lua_settable)
            g_lua_settable(L, idx);
    }

    bool g_EquipDevelopBound = false;
}

void OutfitLua_EnsureEquipDevelopBound()
{
    if (g_EquipDevelopBound)
        return;

    EquipDevelopAdd::Deps deps{};
    deps.ResolveLuaApi   = &ResolveLuaApi;
    deps.GetLuaTop       = &GetLuaTop;
    deps.LuaType         = &LuaType;
    deps.LuaToBool       = &GetLuaBool;
    deps.LuaSetTop       = &Shim_LuaSetTop;
    deps.GetLuaString    = &GetLuaString;
    deps.GetLuaInt       = &GetLuaInt;
    deps.PushLuaNumber   = &PushLuaNumber;
    deps.LuaPushString   = &Shim_LuaPushString;
    deps.LuaCreateTable  = &Shim_LuaCreateTable;
    deps.LuaGetTable     = &Shim_LuaGetTable;
    deps.LuaSetTable     = &Shim_LuaSetTable;
    EquipDevelopAdd::Bind(deps);

    g_EquipDevelopBound = true;
}

static std::uint8_t ReadVanillaExtVariants(
    lua_State* L, int branchTblIdx,
    outfit::VanillaSuitVariantAsset* out, std::size_t cap)
{
    std::uint8_t filled = 0;
    LuaGetField(L, branchTblIdx, "variants");
    if (LuaType(L, -1) == LUA_TTABLE)
    {
        const std::size_t n   = LuaObjLen(L, -1);
        const std::size_t lim = (n < cap) ? n : cap;
        for (std::size_t i = 1; i <= lim; ++i)
        {
            LuaRawGetI(L, -1, static_cast<int>(i));
            if (LuaType(L, -1) == LUA_TTABLE)
            {
                outfit::VanillaSuitVariantAsset v{};
                ReadDeclaredAbilities(L, GetLuaTop(L), v.abilities);
                v.partsPathCode64 = ReadRequiredPathField(L, -1, "partsPath");
                v.fpkPathCode64   = ReadRequiredPathField(L, -1, "fpkPath");
                v.camoFpk    = ReadSubAssetField(L, -1, "camoFpk",
                                   outfit::kSubAssetUseVanilla);
                v.camoFv2    = ReadSubAssetField(L, -1, "camoFv2",
                                   outfit::kSubAssetUseVanilla);
                v.diamondFpk = ReadSubAssetField(L, -1, "diamondFpk",
                                   outfit::kSubAssetUseVanilla);
                v.diamondFv2 = ReadSubAssetField(L, -1, "diamondFv2",
                                   outfit::kSubAssetUseVanilla);
                v.voiceFpk   = ReadSubAssetField(L, -1, "voiceFpk",
                                   outfit::kSubAssetUseVanilla);
                const char* variantVoiceTypeName = nullptr;
                v.voiceType = ReadSoundSwitchField(L, GetLuaTop(L), "voiceType",
                                                   variantVoiceTypeName);
                v.voiceFpkByTypeCount = ReadVoiceFpkByTypeField(
                    L, GetLuaTop(L), v.voiceFpkByType,
                    outfit::kMaxVoiceFpkByType);

                LuaGetField(L, -1, "displayName");
                if (LuaType(L, -1) == LUA_TSTRING)
                    if (const char* s = GetLuaString(L, -1); s && *s)
                        v.displayNameHash = FoxHashes::StrCode64(s);
                LuaPop(L, 1);

                if (v.partsPathCode64 != 0 || v.fpkPathCode64 != 0)
                    out[filled++] = v;
            }
            LuaPop(L, 1);
        }
    }
    LuaPop(L, 1);
    return filled;
}

int __cdecl l_ExtendVanillaOutfit(lua_State* L)
{
    if (LuaType(L, 1) != LUA_TTABLE)
    {
        LogWarn("ExtendVanillaOutfit: argument #1 must be a table");
        PushLuaBool(L, false);
        return 1;
    }

    std::uint8_t vanillaPartsType = 0xFF;
    std::int32_t labelCamo        = -1;
    const char*  outfitName       = nullptr;

    if (TryReadTableStringField(L, 1, "outfit", outfitName)
        && outfitName && outfitName[0])
    {
        labelCamo = ResolveCamoTypeNameToIndex(outfitName);
        if (labelCamo < 0)
        {
            LogWarn("ExtendVanillaOutfit: unknown outfit '%s', not applied", outfitName);
            PushLuaBool(L, false);
            return 1;
        }
        vanillaPartsType = outfit::ResolveVanillaPartsTypeForCamo(
            static_cast<std::uint8_t>(labelCamo));
    }
    else
    {
        int rawCamo  = -1;
        int rawParts = -1;
        if (TryReadTableIntField(L, 1, "outfit", rawCamo)
            && rawCamo >= 0
            && rawCamo <= static_cast<int>(outfit::kVanillaCamoTypeMax))
        {
            labelCamo = rawCamo;
            vanillaPartsType = outfit::ResolveVanillaPartsTypeForCamo(
                static_cast<std::uint8_t>(rawCamo));
        }
        else if (TryReadTableIntField(L, 1, "partsType", rawParts)
                 && rawParts >= 0
                 && rawParts < static_cast<int>(outfit::kCustomPartsTypeStart))
        {
            vanillaPartsType = static_cast<std::uint8_t>(rawParts);
        }
    }

    if (vanillaPartsType == 0xFF)
    {
        LogWarn("ExtendVanillaOutfit: no vanilla partsType resolved%s, not applied",
            labelCamo >= 0 ? " (camo->partsType map unavailable)" : "");
        PushLuaBool(L, false);
        return 1;
    }

    WarnTopLevelAbilities(L, "ExtendVanillaOutfit", outfitName);

    bool any = false;
    ForEachPlayerTypeBranch(L, 1, "ExtendVanillaOutfit", outfitName,
        [&](std::uint8_t playerType, const char*, int branchIdx)
    {
        std::uint16_t ids[outfit::kMaxHeadOptionsPerOutfit]       = {};
        std::uint16_t idOrder[outfit::kMaxHeadOptionsPerOutfit]   = {};
        std::uint8_t  idCount = 0;
        std::uint64_t pend[outfit::kMaxHeadOptionsPerOutfit]      = {};
        std::uint16_t pendOrder[outfit::kMaxHeadOptionsPerOutfit] = {};
        std::uint8_t  pendCount = 0;
        ReadHeadOptionsArrayInto(L, branchIdx, ids, idOrder, idCount,
                                 pend, pendOrder, pendCount);
        const std::uint8_t headScopeCamo =
            (labelCamo >= 0) ? static_cast<std::uint8_t>(labelCamo)
                             : outfit::kHeadOptionAnyCamo;
        if ((idCount > 0 || pendCount > 0)
            && outfit::ExtendVanillaSuitHeadOptions(vanillaPartsType,
                   playerType, headScopeCamo, ids, idOrder, idCount,
                   pend, pendOrder, pendCount))
        {
            any = true;
        }

        const std::uint64_t suitVoice = ReadSubAssetField(
            L, branchIdx, "voiceFpk", outfit::kSubAssetUseVanilla);
        const char* suitVoiceTypeName = nullptr;
        const std::uint32_t suitVoiceType =
            ReadSoundSwitchField(L, branchIdx, "voiceType", suitVoiceTypeName);
        outfit::VoiceFpkByType suitVoiceByType[outfit::kMaxVoiceFpkByType] = {};
        const std::uint8_t suitVoiceByTypeCount = ReadVoiceFpkByTypeField(
            L, branchIdx, suitVoiceByType, outfit::kMaxVoiceFpkByType);
        if ((suitVoice > outfit::kSubAssetUseVanilla
             || suitVoiceType != outfit::kSoundSwitchUnset
             || suitVoiceByTypeCount > 0)
            && outfit::ExtendVanillaSuitVoice(vanillaPartsType,
                   playerType,
                   labelCamo >= 0 ? static_cast<std::uint8_t>(labelCamo)
                                  : std::uint8_t{0xFF},
                   suitVoice, suitVoiceType, suitVoiceByType,
                   suitVoiceByTypeCount))
        {
            any = true;
        }

        bool hasArm = false, armVal = true;
        LuaGetField(L, branchIdx, "enableArm");
        if (LuaType(L, -1) != 0) { hasArm = true; armVal = GetLuaBool(L, -1) != 0; }
        LuaPop(L, 1);
        bool hasHead = false, headVal = false;
        LuaGetField(L, branchIdx, "enableHead");
        if (LuaType(L, -1) != 0) { hasHead = true; headVal = GetLuaBool(L, -1) != 0; }
        LuaPop(L, 1);
        if ((hasArm || hasHead)
            && outfit::ExtendVanillaSuitArmHead(vanillaPartsType, playerType,
                   labelCamo >= 0 ? static_cast<std::uint8_t>(labelCamo)
                                  : std::uint8_t{0xFF},
                   hasArm, armVal, hasHead, headVal))
        {
            any = true;
        }

        outfit::OutfitPlayerTypeData abilityBranch;
        ReadBranchAbilities(L, branchIdx, abilityBranch);
        if (abilityBranch.abilitySilentSteps
            || abilityBranch.abilitySprint != outfit::kSprintNone
            || abilityBranch.abilityDefense
            || abilityBranch.abilityLifeRecovery
            || abilityBranch.abilityRattleSuit != outfit::kSoundSwitchUnset
            || abilityBranch.abilityDamageSe != outfit::kSoundSwitchUnset
            || abilityBranch.suitParamKind != 0)
        {
            outfit::VanillaSuitAbilities va{};
            va.silentSteps   = abilityBranch.abilitySilentSteps;
            va.sprint        = abilityBranch.abilitySprint;
            va.sprintSpark   = abilityBranch.abilitySprintSpark;
            va.defense       = abilityBranch.abilityDefense;
            va.lifeRecovery  = abilityBranch.abilityLifeRecovery;
            va.rattleSuit    = abilityBranch.abilityRattleSuit;
            va.damageSe      = abilityBranch.abilityDamageSe;
            va.suitParamKind = abilityBranch.suitParamKind;
            if (outfit::ExtendVanillaSuitAbilities(vanillaPartsType, playerType,
                    labelCamo >= 0 ? static_cast<std::uint8_t>(labelCamo)
                                   : std::uint8_t{0xFF},
                    va))
            {
                any = true;
            }
        }

        outfit::VanillaSuitVariantAsset
            vars[outfit::kMaxVariantsPerOutfit] = {};
        const std::uint8_t vCount = ReadVanillaExtVariants(
            L, branchIdx, vars, outfit::kMaxVariantsPerOutfit - 1);
        if (vCount > 0 && labelCamo < 0)
        {
            LogWarn("ExtendVanillaOutfit partsType 0x%02X: variants need a camo-named outfit, "
                "skipped", static_cast<unsigned>(vanillaPartsType));
        }
        else if (vCount > 0
            && outfit::ExtendVanillaSuitVariants(vanillaPartsType,
                   playerType, static_cast<std::uint8_t>(labelCamo),
                   vars, vCount))
        {
            any = true;
        }
    });

    if (!any)
    {
        LogWarn("ExtendVanillaOutfit '%s': no branch applied anything, nothing registered",
            outfitName ? outfitName : "(by number)");
        PushLuaBool(L, false);
        return 1;
    }

    LogInfo("ExtendVanillaOutfit %s partsType:0x%02X",
        outfitName ? outfitName : "(by number)",
        static_cast<unsigned>(vanillaPartsType));

    PushLuaNumber(L, static_cast<float>(vanillaPartsType));
    return 1;
}

int __cdecl l_GetOutfitInfo(lua_State* L)
{
    const char* key = nullptr;
    if (LuaType(L, 1) == LUA_TSTRING)
        key = GetLuaString(L, 1);
    else if (LuaType(L, 1) == LUA_TTABLE)
        TryReadTableStringField(L, 1, "key", key);

    if (!key || !key[0])
    {
        LogWarn("GetOutfitInfo: missing key");
        PushLuaBool(L, false);
        return 1;
    }

    const std::int32_t developId = V_FrameWorkState::GetDevelopIdByKey(key);
    if (developId <= 0 || developId > 0xFFFF)
    {
        LogWarn("GetOutfitInfo '%s': unknown outfit key", key);
        PushLuaBool(L, false);
        return 1;
    }

    const outfit::OutfitEntry* e = nullptr;
    if (!outfit::TryGetOutfitByDevelopId(
            static_cast<std::uint16_t>(developId), &e) || !e)
    {
        LogWarn("GetOutfitInfo '%s': developId %d has no outfit entry", key, developId);
        PushLuaBool(L, false);
        return 1;
    }

    if (!e->bound)
    {
        outfit::BindOutfit(static_cast<std::uint16_t>(developId), true,
                           "lua-parts-bytes");
        if (!outfit::TryGetOutfitByDevelopId(
                static_cast<std::uint16_t>(developId), &e) || !e || !e->bound)
        {
            LogError("GetOutfitInfo '%s': outfit pools full, not bound", key);
            PushLuaBool(L, false);
            return 1;
        }
    }

    PushLuaNumber(L, static_cast<float>(e->partsType));
    PushLuaNumber(L, static_cast<float>(e->selectorCode));
    PushLuaNumber(L, static_cast<float>(e->developId));
    return 3;
}

int __cdecl l_GetHeadOptionSlot(lua_State* L)
{
    const char* key = nullptr;
    if (LuaType(L, 1) == LUA_TSTRING)
        key = GetLuaString(L, 1);
    else if (LuaType(L, 1) == LUA_TTABLE)
        TryReadTableStringField(L, 1, "key", key);

    if (!key || !key[0])
    {
        LogWarn("GetHeadOptionSlot: missing key");
        PushLuaBool(L, false);
        return 1;
    }

    const outfit::CustomHeadEntry* head = outfit::TryGetCustomHeadByName(key);
    if ((!head || head->slotByte < outfit::kCustomHeadSlotBase)
        && outfit::HasPendingCustomHeads())
    {
        outfit::DrainPendingHeads();
        head = outfit::TryGetCustomHeadByName(key);
    }

    if (!head || head->slotByte < outfit::kCustomHeadSlotBase)
    {
        LogWarn("GetHeadOptionSlot '%s': no slot yet", key);
        PushLuaBool(L, false);
        return 1;
    }

    PushLuaNumber(L, static_cast<float>(head->slotByte));
    PushLuaNumber(L, static_cast<float>(head->equipId));
    return 2;
}

int __cdecl l_GetHeadOptions(lua_State* L)
{
    if (!ResolveLuaApi() || !g_lua_createtable || !g_lua_settable)
    {
        PushLuaNil(L);
        return 1;
    }
    g_lua_createtable(L, 0, 0);
    const int list = GetLuaTop(L);
    int index = 0;
    for (std::size_t i = 0; i < outfit::kMaxCustomHeads; ++i)
    {
        const auto slot = static_cast<std::uint8_t>(outfit::kCustomHeadSlotBase + i);
        const outfit::CustomHeadEntry* head = outfit::TryGetCustomHeadBySlot(slot);
        if (!head || !head->name[0])
            continue;

        PushLuaNumber(L, static_cast<float>(++index));
        g_lua_createtable(L, 0, 5);
        const int entry = GetLuaTop(L);

        PushLuaString(L, "key");
        PushLuaString(L, head->name);
        g_lua_settable(L, entry);

        PushLuaString(L, "slot");
        PushLuaNumber(L, static_cast<float>(head->slotByte));
        g_lua_settable(L, entry);

        PushLuaString(L, "equipId");
        PushLuaNumber(L, static_cast<float>(head->equipId));
        g_lua_settable(L, entry);

        const std::string display = head->developId
            ? EquipDevelop_DisplayName(static_cast<std::int32_t>(head->developId)) : std::string();
        PushLuaString(L, "name");
        PushLuaString(L, display.empty() ? head->name : display.c_str());
        g_lua_settable(L, entry);

        PushLuaString(L, "playerTypes");
        g_lua_createtable(L, 0, 0);
        const int types = GetLuaTop(L);
        const std::uint8_t mask = outfit::GetCustomHeadBranchMask(head->name);
        for (std::uint8_t pt = 0; pt < outfit::kPlayerTypeMax; ++pt)
        {
            if ((mask & (1u << pt)) == 0)
                continue;
            PushLuaNumber(L, static_cast<float>(pt));
            PushLuaBool(L, true);
            g_lua_settable(L, types);
        }
        g_lua_settable(L, entry);

        g_lua_settable(L, list);
    }
    return 1;
}

int __cdecl l_PlayHeadMotion(lua_State* L)
{
    const char* name = (LuaType(L, 1) == LUA_TSTRING) ? GetLuaString(L, 1) : nullptr;
    PushLuaBool(L, outfit::headmotion::Play(name));
    return 1;
}

int __cdecl l_PlayHeadEffect(lua_State* L)
{
    const char* name = (LuaType(L, 1) == LUA_TSTRING) ? GetLuaString(L, 1) : nullptr;
    PushLuaBool(L, outfit::headmotion::PlayEffect(name));
    return 1;
}

int __cdecl l_StopHeadEffect(lua_State* L)
{
    const char* name = (LuaType(L, 1) == LUA_TSTRING) ? GetLuaString(L, 1) : nullptr;
    PushLuaBool(L, outfit::headmotion::StopEffect(name));
    return 1;
}

int __cdecl l_PlayHeadSound(lua_State* L)
{
    const char* name = (LuaType(L, 1) == LUA_TSTRING) ? GetLuaString(L, 1) : nullptr;
    const char* point = (LuaType(L, 2) == LUA_TSTRING) ? GetLuaString(L, 2) : nullptr;
    PushLuaBool(L, outfit::headmotion::PlaySound(name, point));
    return 1;
}

namespace
{
    int ColorChannelArg(lua_State* L, int idx)
    {
        if (LuaType(L, idx) == LUA_TSTRING)
        {
            const char* name = GetLuaString(L, idx);
            for (std::size_t ch = 0; name && ch < outfit::kColorChannelCount; ++ch)
                if (_stricmp(name, kColorChannelNames[ch]) == 0)
                    return static_cast<int>(ch);
        }
        else if (LuaType(L, idx) == LUA_TNUMBER)
        {
            const int n = GetLuaInt(L, idx);
            if (n >= 1 && n <= static_cast<int>(outfit::kColorChannelCount))
                return n - 1;
        }
        return -1;
    }

    struct ColorTarget
    {
        const outfit::OutfitEntry* entry = nullptr;
        std::uint8_t playerType = 0;
        std::uint8_t variant    = 0;
        bool         worn       = false;
    };

    bool ResolveColorTarget(lua_State* L, int keyIdx, ColorTarget& out)
    {
        const std::uint8_t liveParts  = outfit::ReadLivePartsType();
        const std::uint8_t livePlayer = outfit::ReadLivePlayerType();
        const outfit::OutfitEntry* entry = nullptr;
        if (keyIdx > 0 && LuaType(L, keyIdx) == LUA_TSTRING)
            outfit::TryGetOutfitByKey(GetLuaString(L, keyIdx), &entry);
        else if (liveParts >= outfit::kCustomPartsTypeStart
                 && liveParts <= outfit::kCustomPartsTypeEnd)
            outfit::TryGetOutfitByPartsType(liveParts, &entry);
        if (!entry)
            return false;

        out.entry      = entry;
        out.playerType = livePlayer < outfit::kPlayerTypeMax
            ? livePlayer : entry->FirstSupportedPlayerType();
        out.worn       = entry->bound && entry->partsType == liveParts;
        out.variant    = !entry->HasVariants() ? std::uint8_t{ 0 }
            : entry->bound ? outfit::GetActiveVariant(entry->partsType)
                           : entry->defaultVariant;
        return true;
    }

    enum class ColorApply { Applied, NotReady, NotLoaded, Failed };

    ColorApply ApplyColorFv2(lua_State* L, const outfit::OutfitColor& color)
    {
        if (MissionCodeGuard::ShouldBypassHooks())
            return ColorApply::Failed;
        if (!outfit::IsLocalFovaModelReady())   // the engine apply would write into a model that is being reloaded
            return ColorApply::NotReady;
        if (!outfit::ddheadlayers::FindLocalHeadFile(color.fv2Code))   // the engine apply has no null check, so a missing fv2 would crash
            return ColorApply::NotLoaded;

        const int top = GetLuaTop(L);
        LuaGetField(L, LUA_GLOBALSINDEX_51, "Player");
        bool ok = false;
        if (LuaType(L, -1) == LUA_TTABLE)
        {
            LuaGetField(L, -1, "ApplyFormVariationWithFile");
            if (LuaType(L, -1) == LUA_TFUNCTION)
            {
                PushLuaString(L, color.fv2.c_str());
                ok = g_lua_pcall(L, 1, 0, 0) == 0;
            }
        }
        SetLuaTop(L, top);
        return ok ? ColorApply::Applied : ColorApply::Failed;
    }

    ColorApply ApplyColorFv2OnTop(const outfit::OutfitColor& color)
    {
        if (MissionCodeGuard::ShouldBypassHooks())
            return ColorApply::Failed;
        if (!outfit::IsLocalFovaModelReady())
            return ColorApply::NotReady;
        if (!outfit::ddheadlayers::FindLocalHeadFile(color.fv2Code))
            return ColorApply::NotLoaded;
        return outfit::ApplyFv2OnTop(color.fv2.c_str()) ? ColorApply::Applied : ColorApply::Failed;
    }

    int ColorChannelsWithColors(const ColorTarget& target)
    {
        int n = 0;
        for (std::size_t ch = 0; ch < outfit::kColorChannelCount; ++ch)
            if (const auto* list = target.entry->GetColors(target.playerType, target.variant, ch); list && !list->empty())
                ++n;
        return n;
    }

    bool HasIris(const ColorTarget& target)
    {
        std::uint64_t fv2 = 0;
        std::uint8_t color = outfit::kEyeColorOff;
        return target.entry->GetEyeColor(target.playerType, fv2, color);
    }

    void WarnColorApplyFailed(const outfit::OutfitColor& color, bool onTop)
    {
        static std::uint64_t s_lastWarned = 0;
        if (s_lastWarned == color.fv2Code)
            return;
        s_lastWarned = color.fv2Code;
        LogWarn("OutfitColor: '%s' could not be applied%s", color.fv2.c_str(), onTop ? " on top of the first channel" : "");
    }

    void WarnColorNotLoaded(const outfit::OutfitColor& color, const char* owner = "outfit")
    {
        static std::uint64_t s_lastWarned = 0;
        if (s_lastWarned == color.fv2Code)
            return;
        s_lastWarned = color.fv2Code;
        LogWarn("OutfitColor: '%s' is not in a loaded pack of the worn %s, colour not applied",
            color.fv2.c_str(), owner);
    }

    struct ColorPass
    {
        bool warnNotLoaded = false;
        bool notReady      = false;
        bool notLoaded     = false;
    };

    void ApplyOutfitColorPass(lua_State* L, ColorPass& pass)
    {
        ColorTarget target;
        if (!ResolveColorTarget(L, 0, target) || !target.worn)
            return;
        bool resetDone = false;
        for (std::size_t ch = 0; ch < outfit::kColorChannelCount; ++ch)
        {
            const outfit::OutfitColorList* list =
                target.entry->GetColors(target.playerType, target.variant, ch);
            if (!list || list->empty())
                continue;
            std::uint32_t selected =
                outfit::GetEffectiveColor(target.entry->key, target.playerType, ch);
            if (selected >= list->size())
                selected = 0;
            const outfit::OutfitColor& color = (*list)[selected];
            const bool onTop = resetDone && outfit::CanApplyFv2OnTop();
            const ColorApply result = onTop ? ApplyColorFv2OnTop(color) : ApplyColorFv2(L, color);
            if (result == ColorApply::NotReady)
            {
                pass.notReady = true;
                return;
            }
            if (result == ColorApply::Applied)
            {
                LogInfo("OutfitColor: '%s' %s colour %u applied%s", target.entry->key,
                    kColorChannelNames[ch], selected + 1, onTop ? " on top" : "");
                resetDone = true;
            }
            if (result == ColorApply::Failed)
                WarnColorApplyFailed(color, onTop);
            if (result != ColorApply::NotLoaded)
                continue;
            if (pass.warnNotLoaded)
                WarnColorNotLoaded(color);
            pass.notLoaded = true;
        }
    }

    void WarnIris(const char* key, const char* what)
    {
        static std::string s_last;
        const std::string line = std::string(key) + "|" + what;
        if (s_last == line)
            return;
        s_last = line;
        LogWarn("OutfitIris: '%s' %s", key, what);
    }

    void ApplyIrisPass(ColorPass& pass)
    {
        const std::uint8_t liveParts = outfit::ReadLivePartsType();
        const std::uint8_t pt = outfit::ReadLivePlayerType();
        const outfit::OutfitEntry* entry = nullptr;
        if (liveParts < outfit::kCustomPartsTypeStart || liveParts > outfit::kCustomPartsTypeEnd
            || pt >= outfit::kPlayerTypeMax || !outfit::TryGetOutfitByPartsType(liveParts, &entry)
            || !entry || !entry->bound)
            return;
        std::uint64_t fv2 = 0;
        std::uint8_t color = outfit::kEyeColorOff;
        if (!entry->GetEyeColor(pt, fv2, color))
            return;
        switch (outfit::ApplyIrisFv2(fv2, color))
        {
        case outfit::IrisFv2::Applied:
            if (color == outfit::kEyeColorStaff)
                LogInfo("OutfitIris: '%s' staff iris applied", entry->key);
            else
                LogInfo("OutfitIris: '%s' iris unit %u applied", entry->key, static_cast<unsigned>(color - 1));
            break;
        case outfit::IrisFv2::NotReady:
            pass.notReady = true;
            break;
        case outfit::IrisFv2::NotLoaded:
            if (pass.warnNotLoaded)
                WarnIris(entry->key, "eyeFv2 is not in a loaded pack of the outfit, iris not applied");
            pass.notLoaded = true;
            break;
        case outfit::IrisFv2::NoIrisGroup:
            WarnIris(entry->key, "eyeFv2 has no iris group (110), iris not applied");
            break;
        case outfit::IrisFv2::OtherGroups:
            WarnIris(entry->key, "eyeFv2 may only hold the iris group (110), plus the skin group (100) on DD "
                "soldiers, iris not applied");
            break;
        case outfit::IrisFv2::NoStaffIris:
            WarnIris(entry->key, "no iris unit to apply (a staff iris needs a DD soldier with a head, "
                "and the unit must exist in eyeFv2)");
            break;
        case outfit::IrisFv2::Failed:
            WarnIris(entry->key, "eyeFv2 could not be applied");
            break;
        }
    }

    void WarnHeadNotWorn(const char* head)
    {
        static const char* s_lastWarned = nullptr;
        if (s_lastWarned == head)
            return;
        s_lastWarned = head;
        LogWarn("OutfitColor: head '%s' has colours but its model is not on the player, colour not applied", head);
    }

    void ApplyHeadColorPass(ColorPass& pass)
    {
        const outfit::CustomHeadEntry* head = outfit::TryGetCustomHeadBySlot(outfit::GetRealizedLocalHeadSlot());
        const std::uint8_t pt = outfit::ReadLivePlayerType();
        if (!head || pt >= outfit::kPlayerTypeMax)
            return;
        const std::shared_ptr<const outfit::HeadColorSet> colors = outfit::GetCustomHeadColors(head->name, pt);
        if (!colors)
            return;
        std::uint64_t models[1 + outfit::kSnakeFaceStageCount] = { head->faceFv2Code[pt] };
        if (pt == outfit::kPlayerType_Snake)
            for (std::uint32_t s = 0; s < outfit::kSnakeFaceStageCount; ++s)
                models[1 + s] = outfit::GetCustomHeadSnakeStageFv2(head->name, s);

        bool resetHead = true;
        for (std::size_t ch = 0; ch < outfit::kColorChannelCount; ++ch)
        {
            const outfit::OutfitColorList& list = colors->colors[ch];
            if (list.empty())
                continue;
            std::uint32_t selected = outfit::GetEffectiveColor(head->name, pt, ch);
            if (selected >= list.size())
                selected = 0;
            const outfit::OutfitColor& color = list[selected];
            switch (outfit::ApplyFv2OnHead(color.fv2Code, models, std::size(models), resetHead))
            {
            case outfit::HeadFv2::Applied:
                LogInfo("OutfitColor: head '%s' %s colour %u applied", head->name, kColorChannelNames[ch], selected + 1);
                resetHead = false;
                break;
            case outfit::HeadFv2::NotReady:
                pass.notReady = true;
                return;
            case outfit::HeadFv2::NotLoaded:
                if (pass.warnNotLoaded)
                    WarnColorNotLoaded(color, "head");
                pass.notLoaded = true;
                break;
            case outfit::HeadFv2::NoHeadModel:
                WarnHeadNotWorn(head->name);
                return;
            case outfit::HeadFv2::Failed:
                WarnColorApplyFailed(color, false);
                break;
            }
        }
    }

    void PushColorChannel(lua_State* L, const outfit::OutfitColorList& list,
                          std::uint32_t selected)
    {
        g_lua_createtable(L, 0, 2);
        const int channelTbl = GetLuaTop(L);

        PushLuaString(L, "selected");
        PushLuaNumber(L, static_cast<float>(selected + 1));
        g_lua_settable(L, channelTbl);

        PushLuaString(L, "colors");
        g_lua_createtable(L, static_cast<int>(list.size()), 0);
        const int colorsTbl = GetLuaTop(L);
        for (std::size_t i = 0; i < list.size(); ++i)
        {
            PushLuaNumber(L, static_cast<float>(i + 1));
            g_lua_createtable(L, 0, 2);
            const int colorTbl = GetLuaTop(L);
            PushLuaString(L, "icon");
            PushLuaString(L, list[i].icon.c_str());
            g_lua_settable(L, colorTbl);
            PushLuaString(L, "pack");
            PushLuaBool(L, list[i].fpkCode != 0);
            g_lua_settable(L, colorTbl);
            g_lua_settable(L, colorsTbl);
        }
        g_lua_settable(L, channelTbl);
    }
}

int __cdecl l_GetOutfitColors(lua_State* L)
{
    ColorTarget target;
    if (!ResolveColorTarget(L, 1, target)
        || !target.entry->HasColors(target.playerType, target.variant))
    {
        PushLuaNil(L);
        return 1;
    }

    g_lua_createtable(L, 0, static_cast<int>(outfit::kColorChannelCount) + 2);
    const int result = GetLuaTop(L);
    PushLuaString(L, "key");
    PushLuaString(L, target.entry->key);
    g_lua_settable(L, result);
    PushLuaString(L, "worn");
    PushLuaBool(L, target.worn);
    g_lua_settable(L, result);
    PushLuaString(L, "variant");   // 0 is the outfit itself, n is its variants[n]
    PushLuaNumber(L, static_cast<float>(target.variant));
    g_lua_settable(L, result);
    for (std::size_t ch = 0; ch < outfit::kColorChannelCount; ++ch)
    {
        const outfit::OutfitColorList* list =
            target.entry->GetColors(target.playerType, target.variant, ch);
        if (!list || list->empty())
            continue;
        std::uint32_t selected =
            outfit::GetSavedColor(target.entry->key, target.playerType, ch);
        if (selected >= list->size())
            selected = 0;
        PushLuaString(L, kColorChannelNames[ch]);
        PushColorChannel(L, *list, selected);
        g_lua_settable(L, result);
    }
    return 1;
}

int __cdecl l_GetOutfitColor(lua_State* L)
{
    ColorTarget target;
    const int channel = ColorChannelArg(L, 2);
    if (channel < 0 || !ResolveColorTarget(L, 1, target))
    {
        PushLuaNil(L);
        return 1;
    }
    const outfit::OutfitColorList* list =
        target.entry->GetColors(target.playerType, target.variant, channel);
    if (!list || list->empty())
    {
        PushLuaNil(L);
        return 1;
    }
    std::uint32_t selected =
        outfit::GetSavedColor(target.entry->key, target.playerType, channel);
    if (selected >= list->size())
        selected = 0;
    PushLuaNumber(L, static_cast<float>(selected + 1));
    return 1;
}

int __cdecl l_SetOutfitColor(lua_State* L)
{
    ColorTarget target;
    const int channel = ColorChannelArg(L, 2);
    if (channel < 0)
    {
        LogWarn("SetOutfitColor: channel must be \"primary\" or \"secondary\"");
        PushLuaBool(L, false);
        return 1;
    }
    if (!ResolveColorTarget(L, 1, target))
    {
        LogWarn("SetOutfitColor: no outfit for that key, and no custom outfit worn");
        PushLuaBool(L, false);
        return 1;
    }
    const outfit::OutfitColorList* list =
        target.entry->GetColors(target.playerType, target.variant, channel);
    const int index = LuaType(L, 3) == LUA_TNUMBER ? GetLuaInt(L, 3) : 0;
    if (!list || index < 1 || static_cast<std::size_t>(index) > list->size())
    {
        LogWarn("SetOutfitColor '%s': %s colour %d does not exist", target.entry->key,
            kColorChannelNames[channel], index);
        PushLuaBool(L, false);
        return 1;
    }

    const outfit::OutfitColor& color = (*list)[static_cast<std::size_t>(index) - 1];
    const bool save = LuaType(L, 4) == LUA_TBOOLEAN ? GetLuaBool(L, 4) : true;
    const auto ch = static_cast<std::size_t>(channel);
    const auto chosen = static_cast<std::uint32_t>(index - 1);
    if (save)
        outfit::SetSavedColor(target.entry->key, target.playerType, ch, chosen);
    else
        outfit::SetPreviewColor(target.entry->key, target.playerType, ch, chosen);

    bool applied = false;
    if (target.worn)
    {
        const outfit::OutfitColor* pack =
            outfit::GetServablePackColor(*target.entry, target.playerType, target.variant);
        if (color.fpkCode != 0 && (!pack || pack->fpkCode != color.fpkCode)
            && outfit::GetPackColor(*target.entry, target.playerType, target.variant) == &color)
        {
            WarnColorNotLoaded(color);   // its package is not installed: no reload that could not show it
            PushLuaBool(L, false);
            return 1;
        }
        const std::uint64_t wantPack = pack ? pack->fpkCode : 0;
        if (wantPack != outfit::GetServedColorFpk())
        {
            outfit::RequestPartsRestream();
            outfit::RequestColorReapply(true);
            applied = true;
            LogInfo("OutfitColor: '%s' %s colour %d %s: its package needs a parts reload", target.entry->key,
                kColorChannelNames[channel], index, save ? "saved" : "previewed");
        }
        else if ((ColorChannelsWithColors(target) > 1 && outfit::CanApplyFv2OnTop()) || HasIris(target))
        {
            outfit::RequestColorReapply(false);
            applied = true;
            LogInfo("OutfitColor: '%s' %s colour %d %s: every channel re-applied", target.entry->key,
                kColorChannelNames[channel], index, save ? "saved" : "previewed");
        }
        else
        {
            const ColorApply result = ApplyColorFv2(L, color);
            if (result == ColorApply::NotLoaded)
                WarnColorNotLoaded(color);
            if (result == ColorApply::NotReady)
                outfit::RequestColorReapply(false);   // the model is reloading: the shown colour goes on once it is back
            applied = result == ColorApply::Applied || result == ColorApply::NotReady;
            LogInfo("OutfitColor: '%s' %s colour %d %s: %s", target.entry->key, kColorChannelNames[channel], index,
                save ? "saved" : "previewed",
                result == ColorApply::Applied ? "applied" : result == ColorApply::NotReady ? "waits for the model"
                    : result == ColorApply::NotLoaded ? "fv2 not loaded" : "apply failed");
        }
    }
    else
    {
        LogInfo("OutfitColor: '%s' %s colour %d %s (not worn, nothing applied)", target.entry->key,
            kColorChannelNames[channel], index, save ? "saved" : "previewed");
    }
    PushLuaBool(L, applied);
    return 1;
}

void OutfitLua_ApplyPendingColors(lua_State* L)
{
    if (!L || !outfit::IsColorReapplyReady())
        return;
    const int top = GetLuaTop(L);
    LuaGetField(L, LUA_GLOBALSINDEX_51, "Player");
    const bool hasPlayerLib = LuaType(L, -1) == LUA_TTABLE;
    SetLuaTop(L, top);
    if (!hasPlayerLib || !outfit::ConsumeColorReapply())   // other Lua states pcall too; wait for the game's own
        return;

    static int s_notLoadedRetries = 0;
    static int s_notReadyRetries = 0;
    static std::uint32_t s_generation = 0;
    static bool s_outfitDone = false;
    static bool s_irisDone = false;
    static bool s_headDone = false;
    constexpr int kMaxNotLoadedRetries = 120;
    constexpr int kMaxNotReadyRetries = 600;
    const std::uint32_t generation = outfit::GetColorReapplyGeneration();
    if (generation != s_generation)
    {
        s_generation = generation;
        s_outfitDone = false;
        s_irisDone = false;
        s_headDone = false;
        s_notLoadedRetries = 0;
        s_notReadyRetries = 0;
    }

    ColorPass outfitPass;
    outfitPass.warnNotLoaded = s_notLoadedRetries >= kMaxNotLoadedRetries;
    ColorPass irisPass = outfitPass;
    ColorPass headPass = outfitPass;
    if (!s_outfitDone)
    {
        ApplyOutfitColorPass(L, outfitPass);
        s_outfitDone = !outfitPass.notReady && !outfitPass.notLoaded;
        s_irisDone = false;
    }
    if (!s_irisDone && !outfitPass.notReady)
    {
        ApplyIrisPass(irisPass);
        s_irisDone = !irisPass.notReady && !irisPass.notLoaded;
    }
    if (!s_headDone && !outfitPass.notReady)
    {
        ApplyHeadColorPass(headPass);
        s_headDone = !headPass.notReady && !headPass.notLoaded;
    }

    if (outfitPass.notReady || irisPass.notReady || headPass.notReady)
    {
        if (++s_notReadyRetries < kMaxNotReadyRetries)
            outfit::RequestColorRetry();
        else
            s_notReadyRetries = 0;
        return;
    }
    s_notReadyRetries = 0;

    if ((outfitPass.notLoaded || irisPass.notLoaded || headPass.notLoaded) && s_notLoadedRetries < kMaxNotLoadedRetries)
    {
        ++s_notLoadedRetries;
        outfit::RequestColorRetry();
    }
    else
    {
        s_notLoadedRetries = 0;
    }
}

int __cdecl l_GetSortieOutfitCursor(lua_State* L)
{
    outfit::SortieListCursor cursor;
    if (!outfit::GetSortieListCursor(cursor) || !cursor.entry)
    {
        PushLuaNil(L);
        return 1;
    }
    const std::uint8_t livePlayer = outfit::ReadLivePlayerType();
    const std::uint8_t playerType = livePlayer < outfit::kPlayerTypeMax
        ? livePlayer : cursor.entry->FirstSupportedPlayerType();
    g_lua_createtable(L, 0, 4);
    const int result = GetLuaTop(L);
    PushLuaString(L, "key");
    PushLuaString(L, cursor.entry->key);
    g_lua_settable(L, result);
    PushLuaString(L, "variant");
    PushLuaNumber(L, static_cast<float>(cursor.variant));
    g_lua_settable(L, result);
    PushLuaString(L, "worn");
    PushLuaBool(L, cursor.worn);
    g_lua_settable(L, result);
    PushLuaString(L, "colors");
    PushLuaBool(L, cursor.entry->HasColors(playerType, cursor.variant));
    g_lua_settable(L, result);
    return 1;
}

int __cdecl l_EquipSortieOutfitCursor(lua_State* L)
{
    PushLuaBool(L, outfit::DecideSortieListCursor());
    return 1;
}

int __cdecl l_AddToEquipDevelopTable(lua_State* L)
{
    OutfitLua_EnsureEquipDevelopBound();
    return EquipDevelopAdd::Lua_AddToEquipDevelopTable(L);
}
