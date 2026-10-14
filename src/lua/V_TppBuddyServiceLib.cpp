#include "pch.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

#include "FoxHashes.h"
#include "LuaApi.h"
#include "V_FrameWorkState.h"
#include "V_TppBuddyServiceLib.h"
#include "log.h"

#include "../hooks/buddy/BuddyOutfitEquip.h"
#include "../hooks/buddy/BuddyOutfitRegistry.h"
#include "../hooks/buddy/EquipControllerImpl_InitCurrentSlot.h"
#include "../hooks/outfit/HeadMotion.h"

namespace
{
    struct BranchName
    {
        const char*       name;
        buddyoutfit::Kind kind;
    };

    constexpr BranchName kBranches[] =
    {
        { "quiet", buddyoutfit::Kind::QuietCostume },
        { "horse", buddyoutfit::Kind::HorseTack },
        { "dog",   buddyoutfit::Kind::DogGear },
    };

    bool EndsWith(const char* s, const char* ext)
    {
        const std::size_t n = std::strlen(s);
        const std::size_t m = std::strlen(ext);
        return n > m && _stricmp(s + n - m, ext) == 0;
    }

    std::uint64_t ReadPathField(lua_State* L, int tableIdx, const char* site, const char* key,
                                const char* field, const char* ext, bool* present)
    {
        LuaGetField(L, tableIdx, field);
        std::uint64_t code = 0;
        *present = LuaType(L, -1) != LUA_TNIL;
        if (LuaType(L, -1) == LUA_TSTRING)
        {
            const char* s = GetLuaString(L, -1);
            if (s && *s && EndsWith(s, ext))
                code = FoxHashes::PathCode64Ext(s);
            else
                LogWarn("%s '%s': %s must be a '%s' path, ignored", site, key, field, ext);
        }
        else if (*present)
        {
            LogWarn("%s '%s': %s must be a '%s' path, ignored", site, key, field, ext);
        }
        LuaPop(L, 1);
        return code;
    }

    enum class NumberField { Absent, Number, Invalid };

    NumberField ReadNumberField(lua_State* L, int tableIdx, const char* field, float* out)
    {
        LuaGetField(L, tableIdx, field);
        const int type = LuaType(L, -1);
        NumberField result = NumberField::Absent;
        if (type == LUA_TNUMBER)
        {
            *out = GetLuaNumber(L, -1);
            result = NumberField::Number;
        }
        else if (type != LUA_TNIL)
        {
            result = NumberField::Invalid;
        }
        LuaPop(L, 1);
        return result;
    }

    bool IsWhole(float n, float min, float limit)
    {
        return n >= min && n < limit && n == std::floor(n);
    }

    void ReadHorseEffects(lua_State* L, int branchIdx, buddyoutfit::Entry& e)
    {
        const char* key = e.key.c_str();
        buddyoutfit::HorseTackEffects& fx = e.horse;
        constexpr float kTackLimit = static_cast<float>(buddyoutfit::kHorseVanillaTackCount);
        float n = 0.0f;

        NumberField f = ReadNumberField(L, branchIdx, "damageRate", &n);
        if (f == NumberField::Number && n >= 0.0f)
        {
            const long rate = std::lround(n * 200.0f);
            fx.damageRate = rate > buddyoutfit::kHorseMaxDamageRate ? buddyoutfit::kHorseMaxDamageRate : static_cast<std::int32_t>(rate);
            if (rate > buddyoutfit::kHorseMaxDamageRate)
                LogWarn("RegisterBuddyOutfit '%s': damageRate tops out at 1.275, 1.275 used", key);
        }
        else if (f != NumberField::Absent)
        {
            LogWarn("RegisterBuddyOutfit '%s': damageRate must be 0 or more, ignored", key);
        }

        f = ReadNumberField(L, branchIdx, "gallopSpeedRate", &n);
        if (f == NumberField::Number && n > 0.0f)
            fx.gallopSpeedRate = n;
        else if (f != NumberField::Absent)
            LogWarn("RegisterBuddyOutfit '%s': gallopSpeedRate must be above 0, ignored", key);

        f = ReadNumberField(L, branchIdx, "bodyAttackId", &n);
        if (f == NumberField::Number && IsWhole(n, 1.0f, static_cast<float>(buddyoutfit::kAttackIdLimit)))
            fx.bodyAttackId = static_cast<std::int32_t>(n);
        else if (f != NumberField::Absent)
            LogWarn("RegisterBuddyOutfit '%s': bodyAttackId must be a TppDamage.ATK_* id, ignored", key);

        f = ReadNumberField(L, branchIdx, "soundType", &n);
        if (f == NumberField::Number && IsWhole(n, 0.0f, kTackLimit))
            fx.soundType = static_cast<std::int32_t>(n);
        else if (f != NumberField::Absent)
            LogWarn("RegisterBuddyOutfit '%s': soundType must be a vanilla tack 0..6, ignored", key);
    }

    std::int32_t ReadFlagField(lua_State* L, int tableIdx, const char* site, const char* key, const char* field)
    {
        LuaGetField(L, tableIdx, field);
        const int type = LuaType(L, -1);
        std::int32_t result = -1;
        if (type == LUA_TBOOLEAN)
            result = GetLuaBool(L, -1) ? 1 : 0;
        else if (type != LUA_TNIL)
            LogWarn("%s '%s': %s must be true or false, ignored", site, key, field);
        LuaPop(L, 1);
        return result;
    }

    void ReadDistance(lua_State* L, int branchIdx, const char* key, const char* field, float* out)
    {
        float n = 0.0f;
        const NumberField f = ReadNumberField(L, branchIdx, field, &n);
        if (f == NumberField::Number && n >= 0.0f)
            *out = n;
        else if (f != NumberField::Absent)
            LogWarn("RegisterBuddyOutfit '%s': %s must be 0 or more metres, ignored", key, field);
    }

    void ReadDogEffects(lua_State* L, int branchIdx, buddyoutfit::Entry& e)
    {
        const char* key = e.key.c_str();
        buddyoutfit::DogGearEffects& fx = e.dog;
        constexpr float kGearLimit = static_cast<float>(buddyoutfit::kDogVanillaGearCount);
        float n = 0.0f;

        NumberField f = ReadNumberField(L, branchIdx, "commandType", &n);
        if (f == NumberField::Number && IsWhole(n, 0.0f, kGearLimit))
            fx.commandType = static_cast<std::int32_t>(n);
        else if (f != NumberField::Absent)
            LogWarn("RegisterBuddyOutfit '%s': commandType must be a V_DogCommandType value, KEEP_BUSY used", key);

        f = ReadNumberField(L, branchIdx, "damageRate", &n);
        if (f == NumberField::Number && n >= 0.0f)
            fx.damageRate = n;
        else if (f != NumberField::Absent)
            LogWarn("RegisterBuddyOutfit '%s': damageRate must be 0 or more, ignored", key);

        fx.bulletFlinch   = ReadFlagField(L, branchIdx, "RegisterBuddyOutfit", key, "bulletFlinch");
        fx.armorHitEffect = ReadFlagField(L, branchIdx, "RegisterBuddyOutfit", key, "armorHitEffect");
        fx.armorHitSound  = ReadFlagField(L, branchIdx, "RegisterBuddyOutfit", key, "armorHitSound");
        fx.soldierThreat  = ReadFlagField(L, branchIdx, "RegisterBuddyOutfit", key, "soldierThreat");

        ReadDistance(L, branchIdx, key, "escapeDistance", &fx.escapeDistance);
        ReadDistance(L, branchIdx, key, "hitRetreatDistance", &fx.hitRetreatDistance);
        ReadDistance(L, branchIdx, key, "retargetRange", &fx.retargetRange);
    }

    bool ReadBranch(lua_State* L, int branchIdx, buddyoutfit::Entry& e)
    {
        const char* key = e.key.c_str();
        bool present = false;
        e.fpkPath = ReadPathField(L, branchIdx, "RegisterBuddyOutfit", key, "fpkPath", ".fpk", &present);
        if (!e.fpkPath)
            return false;

        if (e.kind == buddyoutfit::Kind::HorseTack)
        {
            ReadHorseEffects(L, branchIdx, e);
            return true;
        }
        if (e.kind == buddyoutfit::Kind::DogGear)
        {
            ReadDogEffects(L, branchIdx, e);
            return true;
        }
        if (e.kind != buddyoutfit::Kind::QuietCostume)
            return true;

        LuaGetField(L, branchIdx, "bruise");
        e.bruise = LuaType(L, -1) != LUA_TNIL && GetLuaBool(L, -1);
        LuaPop(L, 1);

        bool fv2Present = false;
        bool fpkPresent = false;
        e.bruiseFv2 = ReadPathField(L, branchIdx, "RegisterBuddyOutfit", key, "bruiseFv2", ".fv2", &fv2Present);
        e.bruiseFpk = ReadPathField(L, branchIdx, "RegisterBuddyOutfit", key, "bruiseFpk", ".fpk", &fpkPresent);
        if ((e.bruiseFv2 != 0) != (e.bruiseFpk != 0))
        {
            LogWarn("RegisterBuddyOutfit '%s': bruiseFv2 and bruiseFpk go together, own bruise off", key);
            e.bruiseFv2 = 0;
            e.bruiseFpk = 0;
        }
        return true;
    }

    int __cdecl l_RegisterBuddyOutfit(lua_State* L)
    {
        V_FrameWorkState::SaveBatch batch;

        if (LuaType(L, 1) != LUA_TTABLE)
        {
            LogWarn("RegisterBuddyOutfit: argument #1 must be a table");
            PushLuaBool(L, false);
            return 1;
        }

        LuaGetField(L, 1, "key");
        const char* rawKey = LuaType(L, -1) == LUA_TSTRING ? GetLuaString(L, -1) : nullptr;
        const std::string key = rawKey ? rawKey : "";
        LuaPop(L, 1);
        if (key.empty())
        {
            LogWarn("RegisterBuddyOutfit: missing 'key', not registered");
            PushLuaBool(L, false);
            return 1;
        }

        buddyoutfit::Entry e;
        e.key = key;
        int branches = 0;
        for (const BranchName& b : kBranches)
        {
            LuaGetField(L, 1, b.name);
            if (LuaType(L, -1) == LUA_TTABLE)
            {
                if (++branches == 1)
                {
                    e.kind = b.kind;
                    if (!ReadBranch(L, GetLuaTop(L), e))
                    {
                        LuaPop(L, 1);
                        LogWarn("RegisterBuddyOutfit '%s': %s branch needs fpkPath, not registered", key.c_str(), b.name);
                        PushLuaBool(L, false);
                        return 1;
                    }
                }
            }
            LuaPop(L, 1);
        }
        if (branches != 1)
        {
            LogWarn("RegisterBuddyOutfit '%s': give exactly one of quiet, horse or dog, not registered", key.c_str());
            PushLuaBool(L, false);
            return 1;
        }

        std::int32_t developId = 0;
        if (!V_FrameWorkState::ResolveOrCreateDevelopId(key.c_str(), 0, developId)
            || developId <= 0 || developId > 0xFFFF)
        {
            LogError("RegisterBuddyOutfit '%s': developId allocation failed, not registered", key.c_str());
            PushLuaBool(L, false);
            return 1;
        }
        e.developId = static_cast<std::uint16_t>(developId);

        if (!buddyoutfit::Register(e))
        {
            PushLuaBool(L, false);
            return 1;
        }

        LogInfo("RegisterBuddyOutfit %s %s:%u developId:%u", key.c_str(),
            buddyoutfit::KindName(e.kind), static_cast<unsigned>(e.value), static_cast<unsigned>(e.developId));
        PushLuaNumber(L, static_cast<float>(e.value));
        PushLuaNumber(L, static_cast<float>(e.developId));
        return 2;
    }

    int __cdecl l_SetBuddyOutfit(lua_State* L)
    {
        const char* key = LuaType(L, 1) == LUA_TSTRING ? GetLuaString(L, 1) : nullptr;
        buddyoutfit::Entry e;
        if (!key || !buddyoutfit::TryGetByKey(key, &e))
        {
            LogWarn("SetBuddyOutfit: '%s' is not a registered buddy outfit or Quiet weapon", key ? key : "(none)");
            PushLuaBool(L, false);
            return 1;
        }
        PushLuaBool(L, buddyoutfit::EquipNow(e));
        return 1;
    }

    struct MotionField
    {
        const char* name;
        int         slot;
    };

    constexpr MotionField kStandFields[] = { { "hold", 0 }, { "fire", 2 }, { "reload", 4 }, { "aim", 6 } };
    constexpr MotionField kProneFields[] = { { "hold", 1 }, { "fire", 3 }, { "reload", 5 }, { "aim", 7 } };
    constexpr MotionField kPartFields[]  = { { "fire", 0 }, { "reload", 1 } };

    template <std::size_t N>
    int ReadClipTable(lua_State* L, int parentIdx, const char* tableName, const MotionField (&fields)[N],
                      std::uint64_t* out, unsigned equipId)
    {
        LuaGetField(L, parentIdx, tableName);
        int read = 0;
        if (LuaType(L, -1) == LUA_TTABLE)
        {
            const int tbl = GetLuaTop(L);
            for (const MotionField& f : fields)
            {
                LuaGetField(L, tbl, f.name);
                if (LuaType(L, -1) == LUA_TSTRING)
                {
                    const std::uint64_t id = outfit::headmotion::ParseClipId(GetLuaString(L, -1));
                    if (id)
                    {
                        out[f.slot] = id;
                        ++read;
                    }
                    else
                        LogWarn("SetQuietWeaponMotion equipId:%u: %s.%s is not a clip path or id, ignored",
                            equipId, tableName, f.name);
                }
                LuaPop(L, 1);
            }
        }
        LuaPop(L, 1);
        return read;
    }

    int __cdecl l_SetQuietWeaponMotion(lua_State* L)
    {
        if (LuaType(L, 1) != LUA_TTABLE)
        {
            LogWarn("SetQuietWeaponMotion: argument #1 must be a table");
            PushLuaBool(L, false);
            return 1;
        }

        LuaGetField(L, 1, "equipId");
        const int equipId = LuaType(L, -1) == LUA_TNUMBER ? GetLuaInt(L, -1) : 0;
        LuaPop(L, 1);
        if (equipId <= 0 || equipId > 0xFFFF)
        {
            LogWarn("SetQuietWeaponMotion: missing or invalid equipId, ignored");
            PushLuaBool(L, false);
            return 1;
        }

        buddyoutfit::QuietWeaponMotion m;
        m.equipId = static_cast<std::uint16_t>(equipId);
        int bodyClips = 0;

        LuaGetField(L, 1, "quietMotion");
        if (LuaType(L, -1) == LUA_TTABLE)
        {
            const int qm = GetLuaTop(L);
            bool present = false;
            const std::string id = std::to_string(equipId);
            m.mtar = ReadPathField(L, qm, "SetQuietWeaponMotion", id.c_str(), "mtar", ".mtar", &present);
            bodyClips += ReadClipTable(L, qm, "clips", kStandFields, m.clips, static_cast<unsigned>(equipId));
            bodyClips += ReadClipTable(L, qm, "prone", kProneFields, m.clips, static_cast<unsigned>(equipId));
            if (!m.mtar && bodyClips)
            {
                LogWarn("SetQuietWeaponMotion equipId:%d: quietMotion needs mtar, body clips ignored", equipId);
                for (std::uint64_t& c : m.clips)
                    c = 0;
                bodyClips = 0;
            }
        }
        LuaPop(L, 1);

        LuaGetField(L, 1, "weaponMotion");
        int partClips = 0;
        if (LuaType(L, -1) == LUA_TTABLE)
            partClips = ReadClipTable(L, GetLuaTop(L), "clips", kPartFields, m.partClips, static_cast<unsigned>(equipId));
        LuaPop(L, 1);

        if (!bodyClips && !partClips)
        {
            LogWarn("SetQuietWeaponMotion equipId:%d: no clips declared, ignored", equipId);
            PushLuaBool(L, false);
            return 1;
        }

        buddyoutfit::RegisterQuietWeaponMotion(m);
        LogInfo("SetQuietWeaponMotion equipId:%d body clips:%d weapon clips:%d", equipId, bodyClips, partClips);
        PushLuaBool(L, true);
        return 1;
    }

    luaL_Reg g_VTppBuddyServiceLib[] =
    {
        { "RegisterBuddyOutfit",   l_RegisterBuddyOutfit },
        { "SetBuddyOutfit",        l_SetBuddyOutfit },
        { "SetQuietWeaponMotion",  l_SetQuietWeaponMotion },
        { nullptr, nullptr }
    };
}

bool Register_V_TppBuddyServiceLibrary(lua_State* L)
{
    return RegisterLuaLibrary(L, "V_TppBuddyService", g_VTppBuddyServiceLib);
}
