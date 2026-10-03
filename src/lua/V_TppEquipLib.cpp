#include "pch.h"
#include "V_TppEquipLib.h"

#include <array>
#include <cctype>
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

    constexpr std::uint64_t kGaniExtCode = 0x1F8Aull << 51;

    static bool ParseRawPathId(const std::string& text, std::uint64_t& out)
    {
        std::size_t start = 0;
        if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
            start = 2;
        const std::size_t digits = text.size() - start;
        if (digits != 16 && (digits < 8 || digits > 13))
            return false;
        for (std::size_t i = start; i < text.size(); ++i)
            if (!std::isxdigit(static_cast<unsigned char>(text[i])))
                return false;
        const std::uint64_t value = std::strtoull(text.c_str() + start, nullptr, 16);
        out = (digits == 16) ? value : (kGaniExtCode | value);
        return true;
    }

    static std::uint64_t ReadPathField(lua_State* L, int idx, const char* name)
    {
        std::string path;
        if (!ReadStringField(L, idx, name, path))
            return 0;
        std::uint64_t raw = 0;
        return ParseRawPathId(path, raw) ? raw : FoxHashes::PathCode64Ext(path);
    }

    static std::uint64_t PathAtTop(lua_State* L)
    {
        if (LuaType(L, -1) != LUA_TSTRING)
            return 0;
        const char* s = GetLuaString(L, -1);
        if (!s || !*s)
            return 0;
        const std::string path = s;
        std::uint64_t raw = 0;
        return ParseRawPathId(path, raw) ? raw : FoxHashes::PathCode64Ext(path);
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
            outId = static_cast<std::uint64_t>(
                static_cast<std::int64_t>(GetLuaNumber(L, -1)));
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
        { "SetReceiverMotion",      l_SetReceiverMotion },

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
    return ok;
}

int __cdecl l_SetEquipLangInfo(lua_State* L)
{
    return l_SetEquipLangInfoImpl(L);
}
