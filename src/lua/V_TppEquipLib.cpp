#include "pch.h"
#include "V_TppEquipLib.h"

#include <map>
#include <mutex>
#include <set>
#include <string>

#include "LuaApi.h"
#include "log.h"
#include "../hooks/equip/TppEquip_ReloadEquipIdTable.h"
#include "../hooks/equip/GunBasicInject.h"
#include "../hooks/equip/EquipPartParams.h"

namespace
{
    struct AssembleMotionEntry
    {
        std::string mtar;
        int         copyFrom = 0;
    };
    static std::mutex g_AsmMutex;
    static std::map<int, AssembleMotionEntry> g_AsmEntries;

    static int l_SetAssembleMotion(lua_State* L)
    {
        if (!ResolveLuaApi())
            return 0;
        ChimeraMotion_EnsureWrapInstalled(L);
        if (LuaType(L, 1) != LUA_TTABLE)
        {
            Log("[AssembleMotion] SetAssembleMotion: argument #1 must be a table\n");
            return 0;
        }
        int equipId = 0;
        LuaGetField(L, 1, "equipId");
        if (LuaIsNumber(L, -1)) equipId = GetLuaInt(L, -1);
        LuaPop(L, 1);
        if (equipId <= 0)
        {
            Log("[AssembleMotion] SetAssembleMotion: missing/invalid equipId\n");
            return 0;
        }

        AssembleMotionEntry e;
        LuaGetField(L, 1, "mtar");
        if (LuaIsString(L, -1))
        {
            const char* s = GetLuaString(L, -1);
            if (s) e.mtar = s;
        }
        LuaPop(L, 1);

        LuaGetField(L, 1, "copyFrom");
        if (LuaIsNumber(L, -1)) e.copyFrom = GetLuaInt(L, -1);
        LuaPop(L, 1);

        if (e.mtar.empty() && e.copyFrom <= 0)
        {
            Log("[AssembleMotion] SetAssembleMotion equipId=%d: needs mtar "
                "(path string) or copyFrom (vanilla equipId)\n", equipId);
            return 0;
        }

        const bool viaPath = !e.mtar.empty();
        {
            std::lock_guard<std::mutex> lock(g_AsmMutex);
            g_AsmEntries[equipId] = std::move(e);
        }
        Log("[AssembleMotion] equipId=%d assemble motion registered (%s)\n",
            equipId, viaPath ? "mtar path" : "copyFrom");
        return 0;
    }

    static int l_SetWeaponHandling(lua_State* L)
    {
        if (LuaType(L, 1) != LUA_TTABLE)
        {
            Log("[WeaponKey] SetWeaponHandling: argument #1 must be a table "
                "{ equipId=, familyFrom= }\n");
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
            Log("[WeaponKey] SetWeaponHandling: missing/invalid equipId (use your "
                "TppEquip.EQP_WP_* constant)\n");
            return 0;
        }
        if (familyFrom == 0)
        {
            Log("[WeaponKey] SetWeaponHandling: missing/invalid familyFrom (a VANILLA "
                "weapon equipId whose animation family this weapon should use)\n");
            return 0;
        }
        EquipParam_SetWeaponHandling(equipId, familyFrom);
        return 0;
    }

    static int FindRowPathByEquipId(lua_State* L, int mdtAbs, int rowCount,
                                    int wantedId, std::string& outPath)
    {
        for (int i = 1; i <= rowCount; ++i)
        {
            LuaRawGetI(L, mdtAbs, i);
            if (LuaType(L, -1) == LUA_TTABLE)
            {
                LuaRawGetI(L, -1, 1);
                const int id = LuaIsNumber(L, -1) ? GetLuaInt(L, -1) : 0;
                LuaPop(L, 1);
                if (id == wantedId)
                {
                    LuaRawGetI(L, -1, 2);
                    const char* s = LuaIsString(L, -1) ? GetLuaString(L, -1) : nullptr;
                    if (s) outPath = s;
                    LuaPop(L, 2);
                    return s != nullptr;
                }
            }
            LuaPop(L, 1);
        }
        return 0;
    }

    static int l_ReloadEquipMotionDataWrapped(lua_State* L)
    {
        const int nargs = GetLuaTop(L);
        if (nargs < 1 || LuaType(L, 1) != LUA_TTABLE)
        {
            Log("[AssembleMotion] ReloadEquipMotionData: argument #1 must be a "
                "table - call refused (the native parser would crash on it)\n");
            return 0;
        }
        {
            LuaGetField(L, 1, "MotionDataTable");
            const bool hasMdt = LuaType(L, -1) == LUA_TTABLE;
            LuaPop(L, 1);
            if (!hasMdt)
            {
                Log("[AssembleMotion] ReloadEquipMotionData: the table has no "
                    "'MotionDataTable' field (typo in the mod?) - call refused "
                    "(the native parser would crash on it)\n");
                return 0;
            }
        }
        if (!g_AsmEntries.empty())
        {
            LuaGetField(L, 1, "MotionDataTable");
            const int mdtAbs = GetLuaTop(L);
            if (LuaType(L, mdtAbs) == LUA_TTABLE)
            {
                int n = static_cast<int>(LuaObjLen(L, mdtAbs));
                const int vanillaCount = n;

                std::set<int> present;
                for (int i = 1; i <= n; ++i)
                {
                    LuaRawGetI(L, mdtAbs, i);
                    if (LuaType(L, -1) == LUA_TTABLE)
                    {
                        LuaRawGetI(L, -1, 1);
                        if (LuaIsNumber(L, -1))
                            present.insert(GetLuaInt(L, -1));
                        LuaPop(L, 1);
                    }
                    LuaPop(L, 1);
                }

                std::map<int, AssembleMotionEntry> snapshot;
                {
                    std::lock_guard<std::mutex> lock(g_AsmMutex);
                    snapshot = g_AsmEntries;
                }

                int added = 0;
                for (const auto& kv : snapshot)
                {
                    if (present.count(kv.first))
                        continue;
                    std::string path = kv.second.mtar;
                    if (path.empty()
                        && !FindRowPathByEquipId(L, mdtAbs, vanillaCount,
                                                 kv.second.copyFrom, path))
                    {
                        Log("[AssembleMotion] equipId=%d: copyFrom=%d not found "
                            "in the game's motion table - entry skipped\n",
                            kv.first, kv.second.copyFrom);
                        continue;
                    }
                    g_lua_pushnumber(L, static_cast<lua_Number>(n + 1));
                    g_lua_createtable(L, 2, 0);
                    g_lua_pushnumber(L, 1.0);
                    g_lua_pushnumber(L, static_cast<lua_Number>(kv.first));
                    g_lua_rawset(L, -3);
                    g_lua_pushnumber(L, 2.0);
                    PushLuaString(L, path.c_str());
                    g_lua_rawset(L, -3);
                    g_lua_rawset(L, mdtAbs);
                    ++n;
                    ++added;
                }
#ifdef _DEBUG
                if (added > 0)
                    Log("[AssembleMotion] appended %d custom assemble-motion "
                        "entries (table now %d rows)\n", added, n);
#endif
            }
            g_lua_settop(L, nargs);
        }

        g_lua_pushvalue(L, LUA_UPVALUEINDEX_51_1);
        for (int i = 1; i <= nargs; ++i)
            g_lua_pushvalue(L, i);
        if (g_lua_pcall(L, nargs, 0, 0) != 0)
        {
            const char* err = LuaIsString(L, -1) ? GetLuaString(L, -1) : "?";
            Log("[AssembleMotion] ReloadEquipMotionData original failed: %s\n",
                err ? err : "?");
            LuaPop(L, 1);
        }
        return 0;
    }

    struct ChimeraMotionEntry
    {
        int    copyFrom    = 0;
        double row[11]     = {};
        int    rowN        = 0;
        bool   explicitSet = false;
    };
    static std::mutex g_ChimeraMutex;
    static std::map<int, ChimeraMotionEntry> g_ChimeraEntries;

    constexpr int kMotionTableRows = 233;

    static int l_SetReceiverPartMotion(lua_State* L)
    {
        if (!ResolveLuaApi())
            return 0;
        ChimeraMotion_EnsureWrapInstalled(L);
        if (LuaType(L, 1) != LUA_TTABLE)
        {
            Log("[ChimeraMotion] SetReceiverPartMotion: argument #1 must be a table\n");
            return 0;
        }
        int receiverId = 0;
        LuaGetField(L, 1, "receiverId");
        if (LuaIsNumber(L, -1)) receiverId = GetLuaInt(L, -1);
        LuaPop(L, 1);
        if (receiverId <= 0)
        {
            Log("[ChimeraMotion] SetReceiverPartMotion: missing/invalid receiverId\n");
            return 0;
        }

        ChimeraMotionEntry e;
        LuaGetField(L, 1, "copyFrom");
        if (LuaIsNumber(L, -1)) e.copyFrom = GetLuaInt(L, -1);
        LuaPop(L, 1);

        LuaGetField(L, 1, "row");
        if (LuaType(L, -1) == LUA_TTABLE)
        {
            int n = static_cast<int>(LuaObjLen(L, -1));
            if (n > 11) n = 11;
            for (int i = 1; i <= n; ++i)
            {
                LuaRawGetI(L, -1, i);
                e.row[i - 1] = LuaIsNumber(L, -1)
                    ? static_cast<double>(GetLuaNumber(L, -1)) : 0.0;
                LuaPop(L, 1);
            }
            e.rowN = n;
        }
        LuaPop(L, 1);

        if (e.copyFrom <= 0 && e.rowN == 0)
        {
            Log("[ChimeraMotion] SetReceiverPartMotion receiverId=%d: needs "
                "copyFrom (vanilla RC_) or row ({11 numbers})\n", receiverId);
            return 0;
        }

        e.explicitSet = true;
        {
            std::lock_guard<std::mutex> lock(g_ChimeraMutex);
            g_ChimeraEntries[receiverId] = e;
        }
        Log("[ChimeraMotion] receiverId=%d part-motion assignment registered "
            "(%s)\n", receiverId, e.rowN > 0 ? "raw row" : "copyFrom");
        return 0;
    }

    static bool RegisterChimeraDefault(int receiverId, int copyFromRc)
    {
        if (receiverId <= 0 || copyFromRc <= 0 || receiverId == copyFromRc)
            return false;

        std::lock_guard<std::mutex> lock(g_ChimeraMutex);
        auto it = g_ChimeraEntries.find(receiverId);
        if (it != g_ChimeraEntries.end())
        {
            if (it->second.explicitSet)
                return false;
            if (it->second.copyFrom == copyFromRc)
                return false;
        }

        ChimeraMotionEntry e;
        e.copyFrom = copyFromRc;
        e.explicitSet = false;
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
            Log("[ChimeraMotion] ReloadEquipMotionData2: argument #1 must be a "
                "table - call refused (the native parser would crash on it)\n");
            return 0;
        }
        {
            LuaGetField(L, 1, "assignments");
            const bool ok = LuaType(L, -1) == LUA_TTABLE;
            LuaPop(L, 1);
            if (!ok)
            {
                Log("[ChimeraMotion] ReloadEquipMotionData2: the table has no "
                    "'assignments' field (typo in the mod?) - call refused\n");
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
                {
                    static std::set<int> s_oobLogged;
                    if (s_oobLogged.insert(kv.first).second)
                        Log("[ChimeraMotion] receiverId=%d REFUSED: the engine's part-motion "
                            "table holds only %d rows (EquipMotionDataTableImpl arrays at "
                            "+0x688/+0xa2c/+0xdd0/+0xeb9, each memset to %d entries) and its "
                            "parser indexes them as row-1 with NO bounds check. Writing this "
                            "row would land inside the neighbouring array and corrupt vanilla "
                            "receiver data. This weapon's bolt/slide stays static until the "
                            "table is grown.\n",
                            kv.first, (int)kMotionTableRows, (int)kMotionTableRows);
                    continue;
                }
                if (FindAssignmentRowByRc(L, asgAbs, vanillaCount, kv.first))
                    continue;
                if (kv.second.rowN > 0)
                {
                    g_lua_pushnumber(L, static_cast<lua_Number>(n + 1));
                    g_lua_createtable(L, 12, 0);
                    g_lua_pushnumber(L, 1.0);
                    g_lua_pushnumber(L, static_cast<lua_Number>(kv.first));
                    g_lua_rawset(L, -3);
                    for (int c = 0; c < kv.second.rowN; ++c)
                    {
                        g_lua_pushnumber(L, static_cast<lua_Number>(c + 2));
                        g_lua_pushnumber(L, kv.second.row[c]);
                        g_lua_rawset(L, -3);
                    }
                    g_lua_rawset(L, asgAbs);
                    ++n;
                    continue;
                }
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
                    Log("[ChimeraMotion] receiverId=%d: copyFrom=%d not found "
                        "in the game's assignment table - entry skipped\n",
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

        g_lua_pushvalue(L, LUA_UPVALUEINDEX_51_1);
        for (int i = 1; i <= nargs; ++i)
            g_lua_pushvalue(L, i);
        if (g_lua_pcall(L, nargs, 0, 0) != 0)
        {
            const char* err = LuaIsString(L, -1) ? GetLuaString(L, -1) : "?";
            Log("[ChimeraMotion] ReloadEquipMotionData2 original failed: %s\n",
                err ? err : "?");
            LuaPop(L, 1);
        }
        return 0;
    }

    static void InstallAssembleMotionWrap(lua_State* L, bool quiet)
    {
        g_lua_getfield(L, LUA_GLOBALSINDEX_51,
                       const_cast<char*>("TppEquip"));
        if (LuaType(L, -1) != LUA_TTABLE)
        {
            LuaPop(L, 1);
            if (!quiet)
            {
                static bool logged = false;
                if (!logged)
                {
                    logged = true;
                    Log("[AssembleMotion] the global TppEquip table does not exist "
                        "yet - ReloadEquipMotionData NOT wrapped on this attempt. "
                        "Custom assemble motions cannot merge until it is.\n");
                }
            }
            return;
        }
        LuaGetField(L, -1, "V_AssembleMotionWrapped");
        const bool already = GetLuaBool(L, -1);
        LuaPop(L, 1);
        if (already)
        {
            LuaPop(L, 1);
            return;
        }
        LuaGetField(L, -1, "ReloadEquipMotionData");
        if (LuaType(L, -1) != LUA_TFUNCTION)
        {
            LuaPop(L, 2);
            if (!quiet)
            {
                static bool logged = false;
                if (!logged)
                {
                    logged = true;
                    Log("[AssembleMotion] TppEquip exists but ReloadEquipMotionData "
                        "is not a function - NOT wrapped. Custom assemble motions "
                        "will not merge.\n");
                }
            }
            return;
        }
        g_lua_pushcclosure(L, l_ReloadEquipMotionDataWrapped, 1);
        PushLuaString(L, "ReloadEquipMotionData");
        g_lua_pushvalue(L, -2);
        g_lua_settable(L, -4);
        LuaPop(L, 1);
        PushLuaString(L, "V_AssembleMotionWrapped");
        PushLuaBool(L, true);
        g_lua_settable(L, -3);
        LuaPop(L, 1);
        Log("[AssembleMotion] TppEquip.ReloadEquipMotionData wrapped - custom "
            "assemble motions merge into every reload\n");
    }

    static void InstallChimeraMotionWrap(lua_State* L, bool quiet)
    {
        g_lua_getfield(L, LUA_GLOBALSINDEX_51,
                       const_cast<char*>("TppEquip"));
        if (LuaType(L, -1) != LUA_TTABLE)
        {
            LuaPop(L, 1);
            if (!quiet)
            {
                static bool logged = false;
                if (!logged)
                {
                    logged = true;
                    Log("[ChimeraMotion] the global TppEquip table does not exist "
                        "yet - ReloadEquipMotionData2 NOT wrapped on this attempt. "
                        "Until it is, custom receiver part-motion rows cannot reach "
                        "the engine and bolts/slides will not move.\n");
                }
            }
            return;
        }
        LuaGetField(L, -1, "V_ChimeraMotionWrapped");
        const bool already = GetLuaBool(L, -1);
        LuaPop(L, 1);
        if (already)
        {
            LuaPop(L, 1);
            return;
        }
        LuaGetField(L, -1, "ReloadEquipMotionData2");
        if (LuaType(L, -1) != LUA_TFUNCTION)
        {
            LuaPop(L, 2);
            if (!quiet)
            {
                static bool logged = false;
                if (!logged)
                {
                    logged = true;
                    Log("[ChimeraMotion] TppEquip exists but ReloadEquipMotionData2 "
                        "is not a function - NOT wrapped. Custom receiver part-motion "
                        "rows cannot reach the engine.\n");
                }
            }
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
        Log("[ChimeraMotion] TppEquip.ReloadEquipMotionData2 wrapped - custom "
            "receiver part-motion assignments merge into every reload\n");
    }

    static luaL_Reg g_VTppEquipLib[] =
    {
        { "AddToEquipIdTable",      l_AddToEquipIdTable },
        { "RegisterConstantEquipId", l_RegisterConstantEquipId },
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
        { "SetAssembleMotion",      l_SetAssembleMotion },
        { "SetReceiverPartMotion",  l_SetReceiverPartMotion },
        { "SetWeaponHandling",      l_SetWeaponHandling },

        { nullptr, nullptr }
    };

}

void ChimeraMotion_InheritFromMotionFrom(int receiverId, int motionFromRc)
{
    if (!RegisterChimeraDefault(receiverId, motionFromRc))
        return;

    Log("[ChimeraMotion] receiverId=%d part-motion inherited from motionFrom=%d "
        "- the donor's bolt/slide animation now drives this weapon. Call "
        "SetReceiverPartMotion explicitly to override.\n",
        receiverId, motionFromRc);
}

void ChimeraMotion_EnsureWrapInstalled(lua_State* L)
{
    if (!L || !ResolveLuaApi())
        return;
    InstallAssembleMotionWrap(L, false);
    InstallChimeraMotionWrap(L, false);
}

bool Register_V_TppEquipLibrary(lua_State* L)
{
    const bool ok = RegisterLuaLibrary(L, "V_TppEquip", g_VTppEquipLib);
    InstallAssembleMotionWrap(L, true);
    InstallChimeraMotionWrap(L, true);
    return ok;
}
