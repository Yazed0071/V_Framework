#include <pch.h>
#include "V_TppEquipLib.h"

#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>

#include "LuaApi.h"
#include "log.h"
#include "../hooks/equip/TppEquip_ReloadEquipIdTable.h"
#include "../hooks/equip/GunBasicInject.h"
#include "../hooks/equip/EquipPartParams.h"
#include "../hooks/equip/MotionLoaderImpl_GetMtarIds.h"
#include "../hooks/shell/RemoteMissile.h"
#include "../hooks/equip/UiUtility_GetWeaponItemNameLangId.h"
#include "../hooks/equip/SupportWeaponRegistry.h"
#include "../hooks/equip/EquipParameterTablesImpl_ReloadEquipParameterTablesImpl.h"
#include "../hooks/equip/EquipAliasLease.h"
#include "../hooks/equip/EquipControllerImpl_UpdateBattery.h"
#include "../hooks/equip/ItemCount.h"
#include "../hooks/equip/ShieldParams.h"
#include "../hooks/player/ItemParameter.h"
#include "../hooks/player/AttackActionImpl_UpdateShootingByPad.h"
#include "V_FrameWorkState.h"
#include "FoxHashes.h"
#include "AddressSet.h"
#include "HookUtils.h"

namespace
{
    using LuaCFunction_t = int(__fastcall*)(lua_State*);
    static thread_local LuaCFunction_t g_TlsNativeReloadOriginal = nullptr;
    static LuaCFunction_t g_OrigReloadEquipMotionData2 = nullptr;
    static void* g_ReloadEquipMotionData2Addr = nullptr;
    static bool  g_ChimeraWrapOwned = false;

    static int CallReloadOriginal(lua_State* L, int nargs, const char* what)
    {
        if (LuaCFunction_t native = g_TlsNativeReloadOriginal)
        {
            g_TlsNativeReloadOriginal = nullptr;
            return native(L);
        }
        g_lua_pushvalue(L, LUA_UPVALUEINDEX_51_1);
        for (int i = 1; i <= nargs; ++i)
            g_lua_pushvalue(L, i);
        if (g_lua_pcall(L, nargs, 0, 0) != 0)
        {
            const char* err = LuaIsString(L, -1) ? GetLuaString(L, -1) : "?";
            LogWarn("%s: original failed: %s", what, err ? err : "?");
            LuaPop(L, 1);
        }
        return 0;
    }

    static int l_SetWeaponHandling(lua_State* L)
    {
        if (LuaType(L, 1) != LUA_TTABLE)
        {
            LogWarn("SetWeaponHandling: argument #1 must be a table, ignored");
            return 0;
        }
        unsigned int equipId = 0, familyFrom = 0;
        LuaGetField(L, 1, "equipId");
        if (LuaIsNumber(L, -1)) equipId = static_cast<unsigned int>(GetLuaInt(L, -1));
        LuaPop(L, 1);
        LuaGetField(L, 1, "familyFrom");
        if (LuaIsNumber(L, -1)) familyFrom = static_cast<unsigned int>(GetLuaInt(L, -1));
        LuaPop(L, 1);
        if (equipId == 0)
        {
            LogWarn("SetWeaponHandling: missing/invalid equipId, ignored");
            return 0;
        }
        if (familyFrom == 0)
        {
            LogWarn("SetWeaponHandling equipId %u: missing/invalid familyFrom, ignored", equipId);
            return 0;
        }
        EquipParam_SetWeaponHandling(equipId, familyFrom);
        return 0;
    }

    static bool ReadNumberField(lua_State* L, const char* name, float& out)
    {
        LuaGetField(L, 1, name);
        const bool have = LuaIsNumber(L, -1);
        if (have)
            out = GetLuaNumber(L, -1);
        LuaPop(L, 1);
        return have;
    }

    static bool ReadIntegerField(lua_State* L, const char* name, int& out)
    {
        LuaGetField(L, 1, name);
        const bool have = LuaIsNumber(L, -1);
        if (have)
            out = GetLuaInt(L, -1);
        LuaPop(L, 1);
        return have;
    }

    static float ReadBarField(lua_State* L, int swpId, const char* name)
    {
        float bar = EquipParameterTables::kBarAutomatic;
        LuaGetField(L, 1, name);
        const int type = LuaType(L, -1);
        if (type == LUA_TBOOLEAN && !GetLuaBool(L, -1))
            bar = 0.0f;
        else if (type == LUA_TNUMBER)
        {
            bar = GetLuaNumber(L, -1);
            if (bar < 0.0f || bar > 1.0f)
            {
                LogWarn("SetSupportWeaponParameter swpId %d: %s %g is outside 0-1, clamped", swpId, name, bar);
                bar = bar < 0.0f ? 0.0f : 1.0f;
            }
        }
        else if (type != LUA_TNIL)
            LogWarn("SetSupportWeaponParameter swpId %d: %s must be a number 0-1 or false, ignored", swpId, name);
        LuaPop(L, 1);
        return bar;
    }

    static float ReadBarMaxField(lua_State* L, int swpId, const char* name)
    {
        float value = 0.0f;
        if (ReadNumberField(L, name, value) && value <= 0.0f)
        {
            LogWarn("SetSupportWeaponParameter swpId %d: %s must be above 0, ignored", swpId, name);
            value = 0.0f;
        }
        return value;
    }

    static constexpr std::uint32_t kEquipTypeThrowing = 12;
    static constexpr float         kMostGravity       = 100.0f;

    static int ReadPhysicsField(lua_State* L, int equipId, const char* name, float scale, int maxValue)
    {
        float value = 0.0f;
        if (!ReadNumberField(L, name, value))
            return -1;
        const float maxAllowed = static_cast<float>(maxValue) / scale;
        if (value < 0.0f || value > maxAllowed)
        {
            LogWarn("SetSupportWeapon equipId %d: %s %g is outside 0-%g, clamped", equipId, name, value, maxAllowed);
            value = value < 0.0f ? 0.0f : maxAllowed;
        }
        return static_cast<int>(value * scale + 0.5f);
    }

    static int l_SetSupportWeapon(lua_State* L)
    {
        if (LuaType(L, 1) != LUA_TTABLE)
        {
            LogWarn("SetSupportWeapon: argument #1 must be a table, ignored");
            return 0;
        }
        int equipId = 0;
        int throwAs = 0;
        int placeAs = 0;
        int legacy = 0;
        ReadIntegerField(L, "equipId", equipId);
        ReadIntegerField(L, "throwAs", throwAs);
        ReadIntegerField(L, "placeAs", placeAs);
        LuaGetField(L, 1, "nativeAction");
        const bool hasLegacy = ReadIntegerField(L, "familyFrom", legacy) || LuaType(L, -1) != LUA_TNIL;
        LuaPop(L, 1);
        if (hasLegacy)
            LogWarn("SetSupportWeapon equipId %d: familyFrom and nativeAction are gone, use throwAs (how it is held and thrown) and do the effect in OnActivateThrowing", equipId);
        if (equipId <= 0 || equipId >= static_cast<int>(SupportWeapon::kEquipIdCount)
            || !V_FrameWorkState::IsClaimedEquipId(equip::ResolveReal(equipId)))
        {
            LogWarn("SetSupportWeapon: equipId %d is not a custom EQP_SWP_ id from RegisterConstantEquipId, ignored", equipId);
            return 0;
        }
        if ((throwAs != 0) == (placeAs != 0))
        {
            LogWarn("SetSupportWeapon equipId %d: give either throwAs (a thrown item) or placeAs (a placed item), ignored", equipId);
            return 0;
        }
        if (throwAs != 0 && (!SupportWeapon::IsVanillaSupportWeaponEquipId(static_cast<std::uint32_t>(throwAs))
            || EquipParam_GetEquipType(static_cast<std::uint32_t>(throwAs)) != kEquipTypeThrowing))
        {
            LogWarn("SetSupportWeapon equipId %d: throwAs %d is not a vanilla thrown EQP_SWP_ id, ignored", equipId, throwAs);
            return 0;
        }
        if (placeAs != 0 && !SupportWeapon::IsPlaceAsDonor(static_cast<std::uint32_t>(placeAs)))
        {
            LogWarn("SetSupportWeapon equipId %d: placeAs %d is not a vanilla C4, mine, Dung or decoy EQP_SWP_ id, ignored", equipId, placeAs);
            return 0;
        }
        const int donor = throwAs != 0 ? throwAs : placeAs;

        int attackId = 0;
        if (ReadIntegerField(L, "attackId", attackId) && (attackId <= 0 || attackId > 0xFFFF))
        {
            LogWarn("SetSupportWeapon equipId %d: attackId %d out of range, the throwAs attack is used", equipId, attackId);
            attackId = 0;
        }

        int blastId = SupportWeapon::kNoBlastId;
        if (ReadIntegerField(L, "blastId", blastId)
            && (blastId < 0 || blastId >= static_cast<int>(EquipParameterTables::kBlastRowCount)))
        {
            LogWarn("SetSupportWeapon equipId %d: blastId %d out of range 0-255, the throwAs blast is used", equipId, blastId);
            blastId = SupportWeapon::kNoBlastId;
        }

        int noticeAs = 0;
        if (ReadIntegerField(L, "noticeAs", noticeAs))
        {
            if (noticeAs == 0)
                noticeAs = static_cast<int>(SupportWeapon::kNoticeNothing);
            else if (!SupportWeapon::IsVanillaSupportWeaponEquipId(static_cast<std::uint32_t>(noticeAs))
                     || (EquipParam_GetEquipType(static_cast<std::uint32_t>(noticeAs)) != kEquipTypeThrowing
                         && !SupportWeapon::IsDecoyDonor(static_cast<std::uint32_t>(noticeAs))))
            {
                LogWarn("SetSupportWeapon equipId %d: noticeAs %d is not a vanilla thrown EQP_SWP_ id, a decoy or EQP_None, ignored", equipId, noticeAs);
                noticeAs = 0;
            }
        }

        SupportWeapon::Physics physics;
        physics.bounce          = ReadPhysicsField(L, equipId, "bounce", 250.0f, 0xFF);
        physics.friction        = ReadPhysicsField(L, equipId, "friction", 250.0f, 0xFF);
        physics.rollingFriction = ReadPhysicsField(L, equipId, "rollingFriction", 100.0f, 0xFFFF);
        if (ReadNumberField(L, "radius", physics.radius) && !(physics.radius > 0.0f))
        {
            LogWarn("SetSupportWeapon equipId %d: radius must be above 0, the size from the model is used", equipId);
            physics.radius = 0.0f;
        }
        if (ReadNumberField(L, "throwSpeed", physics.throwSpeed) && !(physics.throwSpeed > 0.0f))
        {
            LogWarn("SetSupportWeapon equipId %d: throwSpeed must be above 0, 1 is used", equipId);
            physics.throwSpeed = 1.0f;
        }
        float gravity = -1.0f;
        if (ReadNumberField(L, "gravity", gravity) && !(gravity >= 0.0f && gravity <= kMostGravity))
        {
            LogWarn("SetSupportWeapon equipId %d: gravity %g is outside 0-%g, clamped", equipId, gravity, kMostGravity);
            gravity = gravity > kMostGravity ? kMostGravity : 0.0f;
        }

        int decoyType = 0;
        if (ReadIntegerField(L, "decoyType", decoyType))
        {
            if (!SupportWeapon::IsDecoyDonor(static_cast<std::uint32_t>(placeAs)))
            {
                LogWarn("SetSupportWeapon equipId %d: decoyType needs placeAs = a vanilla decoy, ignored", equipId);
                decoyType = 0;
            }
            else if (decoyType < 0 || decoyType > 7)
            {
                LogWarn("SetSupportWeapon equipId %d: decoyType %d is outside 0-7, ignored", equipId, decoyType);
                decoyType = 0;
            }
        }

        SupportWeapon::SetNoticeAs(static_cast<std::uint32_t>(equipId), static_cast<std::uint32_t>(noticeAs));
        SupportWeapon::SetPhysics(static_cast<std::uint32_t>(equipId), physics);
        SupportWeapon::SetGravity(static_cast<std::uint32_t>(equipId), gravity);
        SupportWeapon::SetDecoyType(static_cast<std::uint32_t>(equipId), static_cast<std::uint32_t>(decoyType));
        SupportWeapon::Set(static_cast<std::uint32_t>(equipId), static_cast<std::uint32_t>(donor),
                           attackId, blastId);
        return 0;
    }

    static int l_SetItemBattery(lua_State* L)
    {
        if (LuaType(L, 1) != LUA_TTABLE)
        {
            LogWarn("SetItemBattery: argument #1 must be a table, ignored");
            return 0;
        }
        int equipId = 0;
        float seconds = 0.0f;
        float rechargeSeconds = ItemBattery::kRechargeStillSeconds;
        float rechargeSecondsMoving = ItemBattery::kRechargeMovingSeconds;
        ReadIntegerField(L, "equipId", equipId);
        ReadNumberField(L, "seconds", seconds);
        ReadNumberField(L, "rechargeSeconds", rechargeSeconds);
        ReadNumberField(L, "rechargeSecondsMoving", rechargeSecondsMoving);
        int gaugeLength = 0;
        ReadIntegerField(L, "gaugeLength", gaugeLength);
        if (equipId <= 0 || equipId > 0xFFFF || !V_FrameWorkState::IsClaimedEquipId(equipId))
        {
            LogWarn("SetItemBattery: equipId %d is not a custom EQP_IT_ id from RegisterConstantEquipId, ignored", equipId);
            return 0;
        }
        if (!(seconds > 0.0f) || !(rechargeSeconds > 0.0f) || !(rechargeSecondsMoving > 0.0f))
        {
            LogWarn("SetItemBattery equipId %d: seconds, rechargeSeconds and rechargeSecondsMoving must be above 0, ignored", equipId);
            return 0;
        }
        if (gaugeLength < 0 || gaugeLength > 3)
        {
            LogWarn("SetItemBattery equipId %d: gaugeLength %d is not 1, 2 or 3, the length follows seconds", equipId, gaugeLength);
            gaugeLength = 0;
        }
        ItemBattery::Set(static_cast<std::uint16_t>(equipId), seconds, rechargeSeconds, rechargeSecondsMoving,
                         static_cast<std::uint8_t>(gaugeLength));
        return 0;
    }

    static int l_SetSupportWeaponParameter(lua_State* L)
    {
        if (LuaType(L, 1) != LUA_TTABLE)
        {
            LogWarn("SetSupportWeaponParameter: argument #1 must be a table, ignored");
            return 0;
        }
        int swpId = 0;
        ReadIntegerField(L, "swpId", swpId);
        if (swpId < static_cast<int>(EquipParameterTables::kFirstCustomSupportWeaponId)
            || swpId >= static_cast<int>(EquipParameterTables::kSupportWeaponRowCount))
        {
            LogWarn("SetSupportWeaponParameter: swpId %d is not a custom SWP_ id from DeclareSWPs, ignored", swpId);
            return 0;
        }
        if (!EquipParameterTables::SupportWeaponRowsAvailable())
        {
            LogWarn("SetSupportWeaponParameter swpId %d: custom rows are not available on this build, ignored", swpId);
            return 0;
        }
        int   ammo  = 0;
        float p1    = 0.0f;
        float p2    = 0.0f;
        int   grade = 1;
        ReadIntegerField(L, "ammo", ammo);
        ReadIntegerField(L, "grade", grade);

        int p1Fields = 0;
        p1Fields += ReadNumberField(L, "p1", p1) ? 1 : 0;
        p1Fields += ReadNumberField(L, "fuseTime", p1) ? 1 : 0;
        p1Fields += ReadNumberField(L, "sensorRange", p1) ? 1 : 0;
        p1Fields += ReadNumberField(L, "decoyStrength", p1) ? 1 : 0;
        if (p1Fields > 1)
            LogWarn("SetSupportWeaponParameter swpId %d: p1, fuseTime, sensorRange and decoyStrength are the same column, %g used",
                swpId, p1);

        int p2Fields = 0;
        p2Fields += ReadNumberField(L, "p2", p2) ? 1 : 0;
        p2Fields += ReadNumberField(L, "activeTime", p2) ? 1 : 0;
        if (p2Fields > 1)
            LogWarn("SetSupportWeaponParameter swpId %d: p2 and activeTime are the same column, %g used", swpId, p2);
        EquipParameterTables::SetSupportWeaponRow(static_cast<std::uint32_t>(swpId), ammo, p1, p2, grade);

        int damageType = EquipParameterTables::kAutomaticDamageType;
        if (ReadIntegerField(L, "damageType", damageType) && (damageType < 0 || damageType > 6))
        {
            LogWarn("SetSupportWeaponParameter swpId %d: damageType %d out of range 0-6, automatic used", swpId, damageType);
            damageType = EquipParameterTables::kAutomaticDamageType;
        }
        EquipParameterTables::SetSupportWeaponDamageType(static_cast<std::uint32_t>(swpId), damageType);

        EquipParameterTables::SupportWeaponBars bars;
        bars.damage        = ReadBarField(L, swpId, "barDamage");
        bars.durability    = ReadBarField(L, swpId, "barDurability");
        bars.effectTime    = ReadBarField(L, swpId, "barEffectTime");
        bars.durabilityMax = ReadBarMaxField(L, swpId, "durabilityMax");
        bars.effectTimeMax = ReadBarMaxField(L, swpId, "effectTimeMax");
        EquipParameterTables::SetSupportWeaponBars(static_cast<std::uint32_t>(swpId), bars);
        return 0;
    }

    static int l_SetBlastParameter(lua_State* L)
    {
        if (LuaType(L, 1) != LUA_TTABLE)
        {
            LogWarn("SetBlastParameter: argument #1 must be a table, ignored");
            return 0;
        }
        int blastId = 0;
        ReadIntegerField(L, "blastId", blastId);
        if (blastId < static_cast<int>(EquipParameterTables::kFirstCustomBlastId)
            || blastId >= static_cast<int>(EquipParameterTables::kBlastRowCount))
        {
            LogWarn("SetBlastParameter: blastId %d is not a custom BLA_ id from DeclareBLAs, ignored", blastId);
            return 0;
        }
        if (!EquipParameterTables::BlastRowsAvailable())
        {
            LogWarn("SetBlastParameter blastId %d: custom rows are not available on this build, ignored", blastId);
            return 0;
        }
        int   blastFlag    = 2;
        float effectRange  = 0.0f;
        float optimalRange = 0.0f;
        ReadIntegerField(L, "blastFlag", blastFlag);
        ReadNumberField(L, "effectRange", effectRange);
        ReadNumberField(L, "optimalRange", optimalRange);
        if (blastFlag < 0 || blastFlag > 4)
        {
            LogWarn("SetBlastParameter blastId %d: blastFlag %d out of range 0-4, ignored", blastId, blastFlag);
            return 0;
        }
        EquipParameterTables::SetBlastRow(static_cast<std::uint32_t>(blastId), blastFlag, effectRange, optimalRange);
        return 0;
    }

    static bool ReadLevelNumber(lua_State* L, int idx, const char* name, float& out)
    {
        LuaGetField(L, idx, name);
        const bool have = LuaIsNumber(L, -1);
        if (have)
            out = GetLuaNumber(L, -1);
        LuaPop(L, 1);
        return have;
    }

    static bool ReadLevelInteger(lua_State* L, int idx, const char* name, int& out)
    {
        LuaGetField(L, idx, name);
        const bool have = LuaIsNumber(L, -1);
        if (have)
            out = GetLuaInt(L, -1);
        LuaPop(L, 1);
        return have;
    }


    static bool ReadChargeHash(lua_State* L, int idx, const char* key, const char* name, bool effect,
                               std::uint64_t& out)
    {
        LuaGetField(L, idx, name);
        const int type = LuaType(L, -1);
        bool have = true;
        if (type == LUA_TBOOLEAN && !GetLuaBool(L, -1))
            out = 0;
        else if (type == LUA_TNUMBER)
            out = GetLuaInt64(L, -1);
        else if (type == LUA_TSTRING)
            out = effect ? (FoxHashes::StrCode64(GetLuaString(L, -1)) & 0xFFFFFFFFFFFFull)
                         : FoxHashes::FNVHash32(GetLuaString(L, -1));
        else
        {
            if (type != LUA_TNIL)
                LogWarn("SetChargeShot %s: %s must be a name, an id or false, the default used", key, name);
            have = false;
        }
        LuaPop(L, 1);
        return have;
    }

    static void ReadChargeSound(lua_State* L, int idx, const char* key, const char* name, std::uint32_t& out)
    {
        std::uint64_t value = 0;
        if (ReadChargeHash(L, idx, key, name, false, value))
            out = static_cast<std::uint32_t>(value);
    }

    static void ReadChargeVariation(lua_State* L, int idx, int number, const char* name, std::uint32_t& out)
    {
        LuaGetField(L, idx, name);
        const int type = LuaType(L, -1);
        if (type == LUA_TNUMBER)
            out = static_cast<std::uint32_t>(GetLuaInt64(L, -1));
        else if (type != LUA_TNIL)
            LogWarn("SetChargeShot level %d: %s must be a number such as 0x6448D545, the effect's own variation used",
                number, name);
        LuaPop(L, 1);
    }

    static bool ReadChargeLevel(lua_State* L, int idx, int number, float previousTime,
                                equip::ChargeShotLevel& level)
    {
        char key[32] = {};
        sprintf_s(key, "level %d", number);
        if (!ReadLevelNumber(L, idx, "time", level.time) || level.time <= previousTime)
        {
            LogWarn("SetChargeShot level %d: time must be above the previous level's, level and the ones after ignored",
                number);
            return false;
        }
        if (ReadLevelInteger(L, idx, "ammo", level.ammo) && (level.ammo < 0 || level.ammo > 0xFFFF))
        {
            LogWarn("SetChargeShot level %d: ammo %d out of range, 1 used", number, level.ammo);
            level.ammo = 1;
        }
        if (ReadLevelInteger(L, idx, "shots", level.shots) && (level.shots < 1 || level.shots > 0xFF))
        {
            LogWarn("SetChargeShot level %d: shots %d out of range 1-255, 1 used", number, level.shots);
            level.shots = 1;
        }
        if (ReadLevelInteger(L, idx, "bulletId", level.bulletId)
            && (level.bulletId < 0
                || (level.bulletId > 0 && !EquipParam_IsBulletIdTaken(level.bulletId))))
        {
            LogWarn("SetChargeShot level %d: bulletId %d is not a vanilla or declared bullet, the weapon's bullet used",
                number, level.bulletId);
            level.bulletId = 0;
        }
        ReadChargeSound(L, idx, key, "sound", level.sound);
        ReadChargeSound(L, idx, key, "fireSound", level.fireSound);
        ReadChargeHash(L, idx, key, "effect", true, level.effect);
        ReadChargeHash(L, idx, key, "impactEffect", true, level.impactEffect);
        ReadChargeVariation(L, idx, number, "effectVariation", level.effectVariation);
        ReadChargeVariation(L, idx, number, "impactVariation", level.impactVariation);
        return true;
    }

    static int l_SetChargeShot(lua_State* L)
    {
        if (LuaType(L, 1) != LUA_TTABLE)
        {
            LogWarn("SetChargeShot: argument #1 must be a table, ignored");
            return 0;
        }
        const int top = GetLuaTop(L);
        int equipId = 0;
        int receiverId = 0;
        ReadIntegerField(L, "equipId", equipId);
        ReadIntegerField(L, "receiverId", receiverId);
        if ((equipId > 0) == (receiverId > 0))
        {
            LogWarn("SetChargeShot equipId %d receiverId %d: set exactly one, ignored", equipId, receiverId);
            return 0;
        }
        const int key = equipId > 0 ? equipId : receiverId;

        float cooldown = 1.0f;
        if (ReadNumberField(L, "cooldown", cooldown) && cooldown < 0.0f)
        {
            LogWarn("SetChargeShot %d: cooldown %g below 0, 0 used", key, cooldown);
            cooldown = 0.0f;
        }

        char keyText[16] = {};
        sprintf_s(keyText, "%d", key);
        equip::ChargeShotStyle style;
        ReadChargeSound(L, 1, keyText, "chargeSound", style.chargeSound);
        ReadChargeSound(L, 1, keyText, "fullSound", style.fullSound);
        ReadChargeHash(L, 1, keyText, "chargeEffect", true, style.chargeEffect);
        LuaGetField(L, 1, "hud");
        style.hud = !(LuaType(L, -1) == LUA_TBOOLEAN && !GetLuaBool(L, -1));
        LuaPop(L, 1);

        LuaGetField(L, 1, "levels");
        if (LuaType(L, -1) != LUA_TTABLE)
        {
            g_lua_settop(L, top);
            LogWarn("SetChargeShot %d: levels must be a table of levels, ignored", key);
            return 0;
        }
        const int listAbs = GetLuaTop(L);
        int count = static_cast<int>(LuaObjLen(L, listAbs));

        std::vector<equip::ChargeShotLevel> levels;
        float previousTime = 0.0f;
        for (int i = 1; i <= count; ++i)
        {
            LuaRawGetI(L, listAbs, i);
            equip::ChargeShotLevel level;
            const bool isTable = LuaType(L, -1) == LUA_TTABLE;
            if (!isTable)
                LogWarn("SetChargeShot %d: level %d is not a table, it and the ones after ignored", key, i);
            const bool ok = isTable && ReadChargeLevel(L, GetLuaTop(L), i, previousTime, level);
            LuaPop(L, 1);
            if (!ok)
                break;
            levels.push_back(level);
            previousTime = level.time;
        }
        g_lua_settop(L, top);

        if (levels.empty())
        {
            LogWarn("SetChargeShot %d: no valid level, ignored", key);
            return 0;
        }
        equip::ChargeShot_Register(equipId, receiverId, cooldown, style, levels);
        return 0;
    }

    static bool ReadStringField(lua_State* L, int idx, const char* name, std::string& out)
    {
        LuaGetField(L, idx, name);
        bool have = false;
        if (LuaType(L, -1) == LUA_TSTRING)
        {
            const char* s = GetLuaString(L, -1);
            if (s && *s)
            {
                out = s;
                have = true;
            }
        }
        LuaPop(L, 1);
        return have;
    }

    static std::uint64_t ReadPathField(lua_State* L, int idx, const char* name)
    {
        std::string path;
        if (!ReadStringField(L, idx, name, path))
            return 0;
        return FoxHashes::ClipPathId(path);
    }

    static std::uint64_t PathAtTop(lua_State* L)
    {
        if (LuaType(L, -1) != LUA_TSTRING)
            return 0;
        const char* s = GetLuaString(L, -1);
        if (!s || !*s)
            return 0;
        return FoxHashes::ClipPathId(s);
    }

    static void ReadStepPhases(lua_State* L, int idx, const char* name,
                               std::array<std::uint64_t, 4>& phases)
    {
        LuaGetField(L, idx, name);
        if (LuaType(L, -1) == LUA_TTABLE)
        {
            const int listAbs = GetLuaTop(L);
            const std::size_t count = LuaObjLen(L, listAbs);
            if (count > phases.size())
                LogWarn("SetReceiverMotion: %s lists %u phases, only %u used",
                    name, static_cast<unsigned>(count), static_cast<unsigned>(phases.size()));
            for (std::size_t i = 0; i < phases.size(); ++i)
            {
                LuaRawGetI(L, listAbs, static_cast<int>(i + 1));
                phases[i] = PathAtTop(L);
                LuaPop(L, 1);
            }
        }
        else if (const std::uint64_t clip = PathAtTop(L))
        {
            phases.fill(clip);
        }
        LuaPop(L, 1);
    }

    static bool HasField(lua_State* L, int idx, const char* name)
    {
        LuaGetField(L, idx, name);
        const bool present = LuaType(L, -1) != LUA_TNIL;
        LuaPop(L, 1);
        return present;
    }

    static bool ReadBoolField(lua_State* L, int idx, const char* name, bool fallback)
    {
        LuaGetField(L, idx, name);
        const bool value = LuaType(L, -1) == LUA_TNIL ? fallback : GetLuaBool(L, -1);
        LuaPop(L, 1);
        return value;
    }

    static void ReadPartRow(lua_State* L, int rowIdx, ReceiverPartMotionRow& row, bool pose)
    {
        LuaRawGetI(L, rowIdx, 1);
        const char* bone = LuaIsString(L, -1) ? GetLuaString(L, -1) : nullptr;
        if (bone && *bone)
            row.bone = bone;
        LuaPop(L, 1);
        if (row.bone.empty())
            return;
        double numbers[5] = { 0, 0, 0, 0, 0 };
        const int count = pose ? 2 : 5;
        for (int c = 0; c < count; ++c)
        {
            LuaRawGetI(L, rowIdx, c + 2);
            if (LuaIsNumber(L, -1))
                numbers[c] = GetLuaNumber(L, -1);
            LuaPop(L, 1);
        }
        row.axis = static_cast<int>(numbers[0]);
        if (pose)
            row.value = numbers[1];
        else
        {
            row.moveType = static_cast<int>(numbers[1]);
            row.start    = numbers[2];
            row.end      = numbers[3];
            row.value    = numbers[4];
        }
        row.set = true;
    }

    static void ReadPartRows(lua_State* L, int idx, const char* name,
                             ReceiverPartMotionRow rows[2], bool pose)
    {
        LuaGetField(L, idx, name);
        if (LuaType(L, -1) == LUA_TTABLE)
        {
            const int listAbs = GetLuaTop(L);
            for (int i = 0; i < 2; ++i)
            {
                LuaRawGetI(L, listAbs, i + 1);
                if (LuaType(L, -1) == LUA_TTABLE)
                    ReadPartRow(L, GetLuaTop(L), rows[i], pose);
                LuaPop(L, 1);
            }
        }
        LuaPop(L, 1);
    }

    static void ReadClipsTable(lua_State* L, int idx, const char* name, ReceiverMotionClips& clips)
    {
        LuaGetField(L, idx, name);
        if (LuaType(L, -1) == LUA_TTABLE)
        {
            clips.hold             = ReadPathField(L, -1, "hold");
            clips.fire             = ReadPathField(L, -1, "fire");
            clips.reload           = ReadPathField(L, -1, "reload");
            clips.reloadEmpty      = ReadPathField(L, -1, "reloadEmpty");
            clips.cock             = ReadPathField(L, -1, "cock");
            clips.dualReload       = ReadPathField(L, -1, "dualReload");
            clips.grip             = ReadPathField(L, -1, "grip");
            clips.magazineGrip     = ReadPathField(L, -1, "magazineGrip");
            clips.magazineGripDual = ReadPathField(L, -1, "magazineGripDual");
            clips.underBarrelGrip  = ReadPathField(L, -1, "underBarrelGrip");
            ReadStepPhases(L, -1, "stepReload", clips.stepReload);
            ReadStepPhases(L, -1, "stepReloadEmpty", clips.stepReloadEmpty);
        }
        LuaPop(L, 1);
    }

    static void ReadClipsField(lua_State* L, int idx, ReceiverMotionClips& clips)
    {
        ReadClipsTable(L, idx, "clips", clips);
    }

    static void ReadExtraArchives(lua_State* L, int idx, std::vector<ReceiverMotionArchive>& out)
    {
        LuaGetField(L, idx, "extraArchives");
        if (LuaType(L, -1) == LUA_TTABLE)
        {
            const int listAbs = GetLuaTop(L);
            const std::size_t count = LuaObjLen(L, listAbs);
            for (std::size_t i = 1; i <= count; ++i)
            {
                LuaRawGetI(L, listAbs, static_cast<int>(i));
                if (LuaType(L, -1) == LUA_TTABLE)
                {
                    ReceiverMotionArchive archive;
                    archive.mtar = ReadPathField(L, -1, "mtar");
                    archive.pack = ReadPathField(L, -1, "pack");
                    out.push_back(archive);
                }
                LuaPop(L, 1);
            }
        }
        LuaPop(L, 1);
    }

    static int l_SetReceiverMotion(lua_State* L)
    {
        if (!ResolveLuaApi())
            return 0;
        if (LuaType(L, 1) != LUA_TTABLE)
        {
            LogWarn("SetReceiverMotion: argument #1 must be a table, nothing registered");
            return 0;
        }

        ReceiverMotionDesc desc;
        LuaGetField(L, 1, "receiverId");
        if (LuaIsNumber(L, -1)) desc.receiverId = GetLuaInt(L, -1);
        LuaPop(L, 1);

        LuaGetField(L, 1, "playerMotion");
        if (LuaType(L, -1) == LUA_TTABLE)
        {
            desc.playerMtar = ReadPathField(L, -1, "mtar");
            desc.playerPack = ReadPathField(L, -1, "pack");
            ReadExtraArchives(L, -1, desc.extraArchives);
            ReadClipsField(L, -1, desc.playerClips);
            ReadClipsTable(L, -1, "oneHand", desc.oneHandClips);
            ReadClipsTable(L, -1, "oneHandCqc", desc.oneHandCqcClips);
            ReadClipsTable(L, -1, "oneHandCrouch", desc.oneHandCrouchClips);
            ReadClipsTable(L, -1, "horse", desc.horseClips);
            ReadClipsTable(L, -1, "crouch", desc.crouchClips);
        }
        LuaPop(L, 1);

        LuaGetField(L, 1, "weaponMotion");
        if (LuaType(L, -1) == LUA_TTABLE)
        {
            desc.weaponMtar = ReadPathField(L, -1, "mtar");
            ReadClipsField(L, -1, desc.weaponClips);
        }
        LuaPop(L, 1);

        LuaGetField(L, 1, "partMotion");
        if (LuaType(L, -1) == LUA_TTABLE)
        {
            desc.partMotion.present = true;
            ReadPartRows(L, -1, "shoot",      desc.partMotion.shoot,      false);
            ReadPartRows(L, -1, "shootLast",  desc.partMotion.shootLast,  false);
            ReadPartRows(L, -1, "poseLoaded", desc.partMotion.poseLoaded, true);
            ReadPartRows(L, -1, "poseEmpty",  desc.partMotion.poseEmpty,  true);
            LuaGetField(L, -1, "casing");
            if (LuaType(L, -1) == LUA_TTABLE)
            {
                desc.partMotion.casingShoot = ReadBoolField(L, -1, "shoot", true);
                desc.partMotion.casingLast  = ReadBoolField(L, -1, "shootLast", true);
            }
            LuaPop(L, 1);
        }
        LuaPop(L, 1);

        const bool registered = ReceiverMotion_Register(desc);

        LuaGetField(L, 1, "receiverType");
        const int receiverTypeLuaType = LuaType(L, -1);
        const int receiverType = LuaIsNumber(L, -1) ? GetLuaInt(L, -1) : 0;
        LuaPop(L, 1);
        if (registered && receiverTypeLuaType != LUA_TNIL)
        {
            if (receiverTypeLuaType != LUA_TNUMBER)
                LogWarn("SetReceiverMotion receiverId %d: receiverType not an integer, ignored",
                    desc.receiverId);
            else
                EquipParam_SetReceiverMotionType(desc.receiverId, receiverType);
        }
        return 0;
    }

    static int AppendPartRow(lua_State* L, int listAbs, const ReceiverPartMotionRow& row, bool pose)
    {
        const int index = static_cast<int>(LuaObjLen(L, listAbs)) + 1;
        if (index > 65535)
        {
            LogWarn("ReceiverMotion: %s list full at 65535 rows, bone %s row dropped",
                pose ? "poses" : "motions", row.bone.c_str());
            return 0;
        }
        double values[6] = { 0, static_cast<double>(row.axis), static_cast<double>(row.moveType),
                             row.start, row.end, row.value };
        const int count = pose ? 3 : 6;
        if (pose)
            values[2] = row.value;
        g_lua_pushnumber(L, static_cast<lua_Number>(index));
        g_lua_createtable(L, count, 0);
        g_lua_pushnumber(L, 1.0);
        PushLuaString(L, row.bone.c_str());
        g_lua_rawset(L, -3);
        for (int c = 2; c <= count; ++c)
        {
            g_lua_pushnumber(L, static_cast<lua_Number>(c));
            g_lua_pushnumber(L, static_cast<lua_Number>(values[c - 1]));
            g_lua_rawset(L, -3);
        }
        g_lua_rawset(L, listAbs);
        return index;
    }

    static void MergePartMotionRows(lua_State* L, int nargs)
    {
        std::vector<ReceiverPartMotionSnapshot> families;
        ReceiverMotion_CollectPartMotions(families);
        if (families.empty())
            return;

        LuaGetField(L, 1, "V_PartMotionRowsMerged");
        const bool tableAlreadyMerged = GetLuaBool(L, -1);
        LuaPop(L, 1);

        LuaGetField(L, 1, "motions");
        const int motionsAbs = GetLuaTop(L);
        LuaGetField(L, 1, "poses");
        const int posesAbs = GetLuaTop(L);
        if (LuaType(L, motionsAbs) != LUA_TTABLE || LuaType(L, posesAbs) != LUA_TTABLE)
        {
            LogWarn("ReceiverMotion: ReloadEquipMotionData2 table lacks motions/poses, "
                "part-motion rows not merged");
            g_lua_settop(L, nargs);
            return;
        }

        for (const ReceiverPartMotionSnapshot& f : families)
        {
            ReceiverPartRowIndices existing;
            if (tableAlreadyMerged && ReceiverMotion_GetPartRowIndices(f.receiverId, existing)
                && existing.fromLua)
                continue;
            ReceiverPartRowIndices idx;
            idx.present  = true;
            idx.fromLua  = true;
            idx.flags[0] = f.motion.casingShoot ? 0 : 1;
            idx.flags[1] = f.motion.casingLast ? 0 : 1;
            const ReceiverPartMotionRow* motionRows[4] =
                { &f.motion.shoot[0], &f.motion.shoot[1],
                  &f.motion.shootLast[0], &f.motion.shootLast[1] };
            const ReceiverPartMotionRow* poseRows[4] =
                { &f.motion.poseLoaded[0], &f.motion.poseLoaded[1],
                  &f.motion.poseEmpty[0], &f.motion.poseEmpty[1] };
            for (int i = 0; i < 4; ++i)
            {
                if (motionRows[i]->set)
                    idx.motion[i] = static_cast<std::uint16_t>(
                        AppendPartRow(L, motionsAbs, *motionRows[i], false));
                if (poseRows[i]->set)
                    idx.pose[i] = static_cast<std::uint16_t>(
                        AppendPartRow(L, posesAbs, *poseRows[i], true));
            }
            ReceiverMotion_SetPartRowIndices(f.receiverId, idx);
        }
        PushLuaString(L, "V_PartMotionRowsMerged");
        PushLuaBool(L, true);
        g_lua_settable(L, 1);
        g_lua_settop(L, nargs);
    }

    struct ChimeraMotionEntry
    {
        int copyFrom = 0;
    };
    static std::mutex g_ChimeraMutex;
    static std::map<int, ChimeraMotionEntry> g_ChimeraEntries;

    constexpr int kMotionTableRows = 233;

    static bool RegisterChimeraDefault(int receiverId, int copyFromRc)
    {
        if (receiverId <= 0 || copyFromRc <= 0 || receiverId == copyFromRc)
            return false;

        std::lock_guard<std::mutex> lock(g_ChimeraMutex);
        auto it = g_ChimeraEntries.find(receiverId);
        if (it != g_ChimeraEntries.end() && it->second.copyFrom == copyFromRc)
            return false;

        ChimeraMotionEntry e;
        e.copyFrom = copyFromRc;
        g_ChimeraEntries[receiverId] = e;
        return true;
    }

    static int FindAssignmentRowByRc(lua_State* L, int asgAbs, int rowCount,
                                     int wantedRc)
    {
        for (int i = 1; i <= rowCount; ++i)
        {
            LuaRawGetI(L, asgAbs, i);
            if (LuaType(L, -1) == LUA_TTABLE)
            {
                LuaRawGetI(L, -1, 1);
                const int rc = LuaIsNumber(L, -1) ? GetLuaInt(L, -1) : 0;
                LuaPop(L, 1);
                if (rc == wantedRc)
                    return 1;
            }
            LuaPop(L, 1);
        }
        return 0;
    }

    static int l_ReloadEquipMotionData2Wrapped(lua_State* L)
    {
        const int nargs = GetLuaTop(L);
        if (nargs < 1 || LuaType(L, 1) != LUA_TTABLE)
        {
            LogWarn("ReloadEquipMotionData2: argument #1 must be a table, call refused");
            return 0;
        }
        {
            LuaGetField(L, 1, "assignments");
            const bool ok = LuaType(L, -1) == LUA_TTABLE;
            LuaPop(L, 1);
            if (!ok)
            {
                LogWarn("ReloadEquipMotionData2: table has no 'assignments', call refused");
                return 0;
            }
        }
        if (!g_ChimeraEntries.empty())
        {
            LuaGetField(L, 1, "assignments");
            const int asgAbs = GetLuaTop(L);
            int n = static_cast<int>(LuaObjLen(L, asgAbs));
            const int vanillaCount = n;

            std::map<int, ChimeraMotionEntry> snapshot;
            {
                std::lock_guard<std::mutex> lock(g_ChimeraMutex);
                snapshot = g_ChimeraEntries;
            }

            for (const auto& kv : snapshot)
            {
                if (kv.first > kMotionTableRows)
                    continue;
                if (FindAssignmentRowByRc(L, asgAbs, vanillaCount, kv.first))
                    continue;
                int srcIdx = 0;
                for (int i = 1; i <= vanillaCount && srcIdx == 0; ++i)
                {
                    LuaRawGetI(L, asgAbs, i);
                    if (LuaType(L, -1) == LUA_TTABLE)
                    {
                        LuaRawGetI(L, -1, 1);
                        const int rc = LuaIsNumber(L, -1) ? GetLuaInt(L, -1) : 0;
                        LuaPop(L, 1);
                        if (rc == kv.second.copyFrom)
                            srcIdx = i;
                    }
                    LuaPop(L, 1);
                }
                if (srcIdx == 0)
                {
                    LogWarn("ChimeraMotion receiverId %d: copyFrom %d not in assignment table, skipped",
                        kv.first, kv.second.copyFrom);
                    continue;
                }
                LuaRawGetI(L, asgAbs, srcIdx);
                const int srcAbs = GetLuaTop(L);
                const int cols = static_cast<int>(LuaObjLen(L, srcAbs));
                g_lua_pushnumber(L, static_cast<lua_Number>(n + 1));
                g_lua_createtable(L, cols, 0);
                g_lua_pushnumber(L, 1.0);
                g_lua_pushnumber(L, static_cast<lua_Number>(kv.first));
                g_lua_rawset(L, -3);
                for (int c = 2; c <= cols; ++c)
                {
                    g_lua_pushnumber(L, static_cast<lua_Number>(c));
                    LuaRawGetI(L, srcAbs, c);
                    g_lua_rawset(L, -3);
                }
                g_lua_rawset(L, asgAbs);
                g_lua_settop(L, asgAbs);
                ++n;
            }
            g_lua_settop(L, nargs);
        }
        MergePartMotionRows(L, nargs);

        return CallReloadOriginal(L, nargs, "ChimeraMotion: ReloadEquipMotionData2");
    }

    static int __fastcall hkReloadEquipMotionData2(lua_State* L)
    {
        if (!ResolveLuaApi())
            return g_OrigReloadEquipMotionData2(L);
        g_TlsNativeReloadOriginal = g_OrigReloadEquipMotionData2;
        const int results = l_ReloadEquipMotionData2Wrapped(L);
        g_TlsNativeReloadOriginal = nullptr;
        return results;
    }

    static void InstallChimeraMotionWrap(lua_State* L)
    {
        g_lua_getfield(L, LUA_GLOBALSINDEX_51,
                       const_cast<char*>("TppEquip"));
        if (LuaType(L, -1) != LUA_TTABLE)
        {
            LuaPop(L, 1);
            return;
        }
        LuaGetField(L, -1, "V_ChimeraMotionWrapped");
        const bool already = GetLuaBool(L, -1);
        LuaPop(L, 1);
        if (already)
        {
            LuaPop(L, 1);
            static bool said = false;
            if (!said && !g_ChimeraWrapOwned)
            {
                said = true;
                LogWarn("ChimeraMotion: ReloadEquipMotionData2 wrapped by another V_FrameWork, "
                        "its merge runs instead");
            }
            return;
        }
        LuaGetField(L, -1, "ReloadEquipMotionData2");
        if (LuaType(L, -1) != LUA_TFUNCTION)
        {
            LuaPop(L, 2);
            return;
        }
        g_lua_pushcclosure(L, l_ReloadEquipMotionData2Wrapped, 1);
        PushLuaString(L, "ReloadEquipMotionData2");
        g_lua_pushvalue(L, -2);
        g_lua_settable(L, -4);
        LuaPop(L, 1);
        PushLuaString(L, "V_ChimeraMotionWrapped");
        PushLuaBool(L, true);
        g_lua_settable(L, -3);
        LuaPop(L, 1);
        g_ChimeraWrapOwned = true;
    }

    constexpr int kAmmoInfoResults = 6;   // bullet, capacity, carry, then the same three for the under barrel

    static int l_GetAmmoInfoWithDeclaredBullets(lua_State* L)
    {
        const int nargs = g_lua_gettop(L);
        g_lua_pushvalue(L, LUA_UPVALUEINDEX_51_1);
        for (int i = 1; i <= nargs; ++i)
            g_lua_pushvalue(L, i);
        if (g_lua_pcall(L, nargs, kAmmoInfoResults, 0) != 0)
        {
            const char* err = LuaIsString(L, -1) ? GetLuaString(L, -1) : "?";
            LogWarn("TppEquip.GetAmmoInfo: %s", err ? err : "?");
            LuaPop(L, 1);
            return 0;
        }
        lua_Number values[kAmmoInfoResults];
        for (int i = 0; i < kAmmoInfoResults; ++i)
            values[i] = g_lua_tonumber(L, i - kAmmoInfoResults);
        LuaPop(L, kAmmoInfoResults);
        values[0] = EquipParam_LogicalBulletForLane(static_cast<int>(values[0]));
        values[3] = EquipParam_LogicalBulletForLane(static_cast<int>(values[3]));
        for (const lua_Number value : values)
            g_lua_pushnumber(L, value);
        return kAmmoInfoResults;
    }

    static void InstallAmmoInfoWrap(lua_State* L)
    {
        g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("TppEquip"));
        if (LuaType(L, -1) != LUA_TTABLE)
        {
            LuaPop(L, 1);
            return;
        }
        LuaGetField(L, -1, "V_AmmoInfoWrapped");
        const bool already = GetLuaBool(L, -1);
        LuaPop(L, 1);
        LuaGetField(L, -1, "GetAmmoInfo");
        if (already || LuaType(L, -1) != LUA_TFUNCTION)
        {
            LuaPop(L, 2);
            return;
        }
        g_lua_pushcclosure(L, l_GetAmmoInfoWithDeclaredBullets, 1);
        PushLuaString(L, "GetAmmoInfo");
        g_lua_pushvalue(L, -2);
        g_lua_settable(L, -4);
        LuaPop(L, 1);
        PushLuaString(L, "V_AmmoInfoWrapped");
        PushLuaBool(L, true);
        g_lua_settable(L, -3);
        LuaPop(L, 1);
    }

    static bool ReadLangField(lua_State* L, const char* field, std::uint64_t& outId)
    {
        LuaGetField(L, 1, field);
        bool have = false;
        if (LuaIsString(L, -1))
        {
            const char* s = GetLuaString(L, -1);
            if (s && *s)
            {
                outId = FoxHashes::StrCode64(s);
                have = true;
            }
        }
        else if (LuaIsNumber(L, -1))
        {
            outId = GetLuaInt64(L, -1);
            have = true;
        }
        LuaPop(L, 1);
        return have;
    }

    static int l_SetEquipLangInfoImpl(lua_State* L)
    {
        if (LuaType(L, 1) != LUA_TTABLE)
        {
            LogWarn("SetEquipLangInfo: argument #1 must be a table, ignored");
            return 0;
        }

        int equipId = 0;
        LuaGetField(L, 1, "equipId");
        if (LuaIsNumber(L, -1)) equipId = GetLuaInt(L, -1);
        LuaPop(L, 1);
        if (equipId <= 0)
        {
            LogWarn("SetEquipLangInfo: missing/invalid equipId, nothing registered");
            return 0;
        }

        std::uint64_t nameId = 0, infoId = 0, realNameId = 0;
        const bool hasName     = ReadLangField(L, "langEquipName",     nameId);
        const bool hasInfo     = ReadLangField(L, "langEquipInfo",     infoId);
        const bool hasRealName = ReadLangField(L, "langEquipRealName", realNameId);

        if (!hasName && !hasInfo && !hasRealName)
        {
            LogWarn("SetEquipLangInfo equipId %d: no langEquipName/Info/RealName, nothing registered",
                equipId);
            return 0;
        }

        EquipLangInfo_Set(equipId, hasName, nameId, hasInfo, infoId,
                          hasRealName, realNameId);
        return 0;
    }

    static int l_SetItemCount(lua_State* L)
    {
        if (LuaType(L, 1) != LUA_TTABLE)
        {
            LogWarn("SetItemCount: argument #1 must be a table, ignored");
            return 0;
        }
        int equipId = 0;
        int count = 0;
        ReadIntegerField(L, "equipId", equipId);
        ReadIntegerField(L, "count", count);
        if (equipId <= 0 || equipId > 0xFFFF || !V_FrameWorkState::IsClaimedEquipId(equipId))
        {
            LogWarn("SetItemCount: equipId %d is not a custom EQP_IT_ id from RegisterConstantEquipId, ignored", equipId);
            return 0;
        }
        if (count < 1)
        {
            LogWarn("SetItemCount equipId %d: count %d is below 1, ignored", equipId, count);
            return 0;
        }
        ItemCount::Set(static_cast<std::uint16_t>(equipId), count,
                       ReadBoolField(L, 1, "consumeOnUse", true));
        return 0;
    }

    static int l_SetItemParameter(lua_State* L)
    {
        if (LuaType(L, 1) != LUA_TTABLE)
        {
            LogWarn("SetItemParameter: argument #1 must be a table, ignored");
            return 0;
        }
        int equipId = 0;
        int grade = 0;
        int count = 0;
        float seconds = 0.0f;
        ReadIntegerField(L, "equipId", equipId);
        const bool haveGrade   = ReadIntegerField(L, "grade", grade);
        const bool haveCount   = ReadIntegerField(L, "count", count);
        const bool haveSeconds = ReadNumberField(L, "seconds", seconds);
        const int grades = equipId > 0 ? ItemParameter::GradeCount(static_cast<std::uint32_t>(equipId)) : 0;
        if (grades == 0)
        {
            LogWarn("SetItemParameter: equipId %d is not EQP_IT_InstantStealth, TimeCigarette, Pentazemin, Clairvoyance, ReflexMedicine, "
                    "ParasiteMist, ParasiteCamouf or ParasiteHard, ignored", equipId);
            return 0;
        }
        if (haveGrade && (grade < 1 || grade > grades))
        {
            LogWarn("SetItemParameter equipId %d: grade %d is outside 1-%d, ignored", equipId, grade, grades);
            return 0;
        }
        if (!haveCount && !haveSeconds)
        {
            LogWarn("SetItemParameter equipId %d: neither count nor seconds given, ignored", equipId);
            return 0;
        }
        if (haveCount && (count < 1 || count > 999))
        {
            LogWarn("SetItemParameter equipId %d: count %d is outside 1-999, ignored", equipId, count);
            return 0;
        }
        if (haveSeconds && !(seconds > 0.0f))
        {
            LogWarn("SetItemParameter equipId %d: seconds must be above 0, ignored", equipId);
            return 0;
        }
        if (haveSeconds && !ItemParameter::HasField(static_cast<std::uint32_t>(equipId), ItemParameter::Field::Seconds))
            LogWarn("SetItemParameter equipId %d: this item keeps no effect length in the item table, seconds ignored", equipId);
        const int first = haveGrade ? grade : 1;
        const int last  = haveGrade ? grade : grades;
        for (int g = first; g <= last; ++g)
        {
            if (haveCount)
                ItemParameter::Set(static_cast<std::uint32_t>(equipId), g, ItemParameter::Field::Count, static_cast<float>(count));
            if (haveSeconds)
                ItemParameter::Set(static_cast<std::uint32_t>(equipId), g, ItemParameter::Field::Seconds, seconds);
        }
        return 0;
    }

    static int l_SetShield(lua_State* L)
    {
        if (LuaType(L, 1) != LUA_TTABLE)
        {
            LogWarn("SetShield: argument #1 must be a table, ignored");
            return 0;
        }
        int equipId = 0;
        int durability = 0;
        int level = 0;
        ReadIntegerField(L, "equipId", equipId);
        ReadIntegerField(L, "durability", durability);
        ReadIntegerField(L, "level", level);
        if (equipId <= 0 || !V_FrameWorkState::IsClaimedEquipId(equipId))
        {
            LogWarn("SetShield: equipId %d is not a custom id from RegisterConstantEquipId, ignored", equipId);
            return 0;
        }
        if (durability < 1 || durability > 255)
        {
            LogWarn("SetShield equipId %d: durability %d is outside 1-255, ignored", equipId, durability);
            return 0;
        }
        if (level < 0 || level > 2)
        {
            LogWarn("SetShield equipId %d: level %d is outside 0-2, 0 used", equipId, level);
            level = 0;
        }
        ShieldParams::Set(static_cast<std::uint32_t>(equipId), static_cast<std::uint32_t>(durability),
                          static_cast<std::uint32_t>(level));
        return 0;
    }

    static luaL_Reg g_VTppEquipLib[] =
    {
        { "AddToEquipIdTable",      l_AddToEquipIdTable },
        { "RegisterConstantEquipId", l_RegisterConstantEquipId },
        { "SetRemoteMissile",       l_SetRemoteMissile },
        { "AbortRemoteMissile",     l_AbortRemoteMissile },
        { "IsRemoteMissileFlying",  l_IsRemoteMissileFlying },
        { "DeclareEQPTypes",        l_DeclareEQPTypes },
        { "DeclareSWPTypes",        l_DeclareSWPTypes },
        { "DeclareEQPBlocks",       l_DeclareEQPBlocks },
        { "DeclareSWPs",            l_DeclareSWPs },
        { "DeclareBLs",             l_DeclareBLs },
        { "DeclareBLAs",            l_DeclareBLAs },
        { "DeclareCasings",         l_DeclareCasings },
        { "DeclareMZs",             l_DeclareMZs },
        { "DeclareLTLS",            l_DeclareLTLS },
        { "DeclareWPs",             l_DeclareWPs },
        { "DeclareMOs",             l_DeclareMOs },
        { "DeclareUBs",             l_DeclareUBs },
        { "DeclareAMs",             l_DeclareAMs },
        { "DeclareSTs",             l_DeclareSTs },
        { "DeclareRCs",             l_DeclareRCs },
        { "DeclareBAs",             l_DeclareBAs },
        { "DeclareSKs",             l_DeclareSKs },
        { "DeclareReticleUIs",      l_DeclareReticleUIs },
        { "DeclareScopeUIs",        l_DeclareScopeUIs },
        { "DeclareBarrelLengths",   l_DeclareBarrelLengths },
        { "DeclareRicochetSizes",   l_DeclareRicochetSizes },
        { "DeclareBulletTypes",     l_DeclareBulletTypes },
        { "DeclarePenetrateLevels", l_DeclarePenetrateLevels },
        { "DeclareTriggers",        l_DeclareTriggers },
        { "DeclareWeaponPaints",    l_DeclareWeaponPaints },
        { "SetGunBasic",            l_SetGunBasic },
        { "SetMagazine",            l_SetMagazine },
        { "SetStock",               l_SetStock },
        { "SetMuzzle",              l_SetMuzzle },
        { "SetReceiver",            l_SetReceiver },
        { "SetSight",               l_SetSight },
        { "SetBarrel",              l_SetBarrel },
        { "SetUnderBarrel",         l_SetUnderBarrel },
        { "SetOption",              l_SetOption },
        { "SetBullet",              l_SetBullet },
        { "SetDamage",              l_SetDamage },
        { "DeclareDamages",         l_DeclareDamages },
        { "SetWeaponHandling",      l_SetWeaponHandling },
        { "SetSupportWeapon",       l_SetSupportWeapon },
        { "SetSupportWeaponParameter", l_SetSupportWeaponParameter },
        { "SetItemBattery",         l_SetItemBattery },
        { "SetItemCount",           l_SetItemCount },
        { "SetItemParameter",       l_SetItemParameter },
        { "SetShield",              l_SetShield },
        { "SetBlastParameter",      l_SetBlastParameter },
        { "SetReceiverMotion",      l_SetReceiverMotion },
        { "SetChargeShot",          l_SetChargeShot },

        { nullptr, nullptr }
    };

}

void ChimeraMotion_InheritFromMotionFrom(int receiverId, int motionFromRc)
{
    RegisterChimeraDefault(receiverId, motionFromRc);
}

void ChimeraMotion_EnsureWrapInstalled(lua_State* L)
{
    if (!L || !ResolveLuaApi())
        return;
    InstallChimeraMotionWrap(L);
    InstallAmmoInfoWrap(L);
}

bool ChimeraMotion_InstallNativeHooks()
{
    bool ok = true;
    if (gAddr.EquipMotionDataTable_ReloadEquipMotionData2)
    {
        void* target = ResolveGameAddress(gAddr.EquipMotionDataTable_ReloadEquipMotionData2);
        if (CreateAndEnableHook(target, reinterpret_cast<void*>(&hkReloadEquipMotionData2),
                                reinterpret_cast<void**>(&g_OrigReloadEquipMotionData2)))
            g_ReloadEquipMotionData2Addr = target;
        else
        {
            LogError("ChimeraMotion: ReloadEquipMotionData2 hook failed @ %p, only Lua wrap merges rows",
                target);
            ok = false;
        }
    }
    return ok;
}

void ChimeraMotion_UninstallNativeHooks()
{
    if (g_ReloadEquipMotionData2Addr)
        DisableAndRemoveHook(g_ReloadEquipMotionData2Addr);
    g_ReloadEquipMotionData2Addr = nullptr;
    g_OrigReloadEquipMotionData2 = nullptr;
}

bool Register_V_TppEquipLibrary(lua_State* L)
{
    const bool ok = RegisterLuaLibrary(L, "V_TppEquip", g_VTppEquipLib);
    InstallChimeraMotionWrap(L);
    InstallAmmoInfoWrap(L);
    return ok;
}

int __cdecl l_SetEquipLangInfo(lua_State* L)
{
    return l_SetEquipLangInfoImpl(L);
}
