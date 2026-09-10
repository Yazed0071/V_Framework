#include "pch.h"

extern "C" {
    #include "lua.h"
    #include "lauxlib.h"
}

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "LuaApi.h"
#include "V_TppSoldierFaceLib.h"
#include "log.h"
#include "FoxHashes.h"
#include "../hooks/outfit/CustomFaceTable.h"
#include "../hooks/outfit/FaceIdRegistry.h"

namespace
{
    bool ReadStringField(lua_State* L, int tableIndex, const char* fieldName, const char*& outValue)
    {
        const int top = GetLuaTop(L);
        LuaGetField(L, tableIndex, fieldName);
        const bool ok = LuaIsString(L, -1);
        if (ok)
            outValue = GetLuaString(L, -1);
        LuaPop(L, GetLuaTop(L) - top);
        return ok;
    }

    bool ReadIntField(lua_State* L, int tableIndex, const char* fieldName, int& outValue)
    {
        const int top = GetLuaTop(L);
        LuaGetField(L, tableIndex, fieldName);
        const bool ok = LuaIsNumber(L, -1);
        if (ok)
            outValue = GetLuaInt(L, -1);
        LuaPop(L, GetLuaTop(L) - top);
        return ok;
    }

    bool ReadBoolField(lua_State* L, int tableIndex, const char* fieldName, bool defaultValue)
    {
        const int top = GetLuaTop(L);
        LuaGetField(L, tableIndex, fieldName);
        bool value = defaultValue;
        if (LuaType(L, -1) == LUA_TBOOLEAN)
            value = GetLuaBool(L, -1);
        LuaPop(L, GetLuaTop(L) - top);
        return value;
    }

    std::uint16_t ReadFovaField(lua_State* L, int tableIndex, const char* fieldName)
    {
        int raw = 0;
        if (!ReadIntField(L, tableIndex, fieldName, raw))
            return outfit::kFovaNone;
        if (raw < 0)
            return outfit::kFovaNone;
        return static_cast<std::uint16_t>(raw);
    }

    int FailWithReason(lua_State* L, const char* reason)
    {
        PushLuaNil(L);
        PushLuaString(L, reason);
        return 2;
    }

    bool ParseFovaColumnArg(lua_State* L, int idx, int& outColumn)
    {
        if (LuaType(L, idx) == LUA_TNUMBER)
        {
            outColumn = GetLuaInt(L, idx);
            return outColumn >= 0 && outColumn < outfit::kHeadFovaColumnCount;
        }
        if (LuaType(L, idx) != LUA_TSTRING)
            return false;
        outColumn = outfit::ParseFovaColumnName(GetLuaString(L, idx));
        return outColumn >= 0;
    }

    bool ReadColumnField(lua_State* L, int tableIndex, int& outColumn)
    {
        const int top = GetLuaTop(L);
        LuaGetField(L, tableIndex, "column");
        const bool ok = ParseFovaColumnArg(L, GetLuaTop(L), outColumn);
        LuaPop(L, GetLuaTop(L) - top);
        return ok;
    }

    bool ReadFovaPairTable(lua_State* L, int tableIndex, std::string& fv2, std::string& fpk)
    {
        const char* fv2Text = nullptr;
        const char* fpkText = nullptr;
        ReadStringField(L, tableIndex, "fv2", fv2Text);
        ReadStringField(L, tableIndex, "fpk", fpkText);
        if (!fv2Text || !fv2Text[0])
            return false;
        fv2 = fv2Text;
        fpk = (fpkText && fpkText[0]) ? fpkText : "";
        return true;
    }

    bool IhSaysFpkIsMissing(lua_State* L, const std::string& fpk)
    {
        if (!g_lua_pcall || !g_lua_toboolean)
            return false;

        const int top = GetLuaTop(L);
        LuaGetField(L, LUA_GLOBALSINDEX_51, "InfCore");
        if (LuaType(L, -1) != LUA_TTABLE)
        {
            LuaPop(L, GetLuaTop(L) - top);
            return false;
        }
        LuaGetField(L, -1, "FpkExistsInternal");
        if (LuaType(L, -1) != LUA_TFUNCTION)
        {
            LuaPop(L, GetLuaTop(L) - top);
            return false;
        }

        PushLuaString(L, FoxHashes::NormalizeAssetPath(fpk).c_str());
        const bool missing = g_lua_pcall(L, 1, 1, 0) == 0 && g_lua_toboolean(L, -1) == 0;
        LuaPop(L, GetLuaTop(L) - top);
        return missing;
    }

    const char* MissingFpkReason()
    {
        return "the fpk is not in the game's QAR dats according to snakebite.xml, and a pack the "
               "loader cannot open hangs the mission load forever - install it with SnakeBite, or "
               "pass trustFpk=true if you packed it another way";
    }

    std::string ColumnStateSuffix(int column)
    {
        outfit::FovaColumnInfo info;
        if (!outfit::ReadFovaColumnInfo(column, info))
            return "";
        char text[64];
        std::snprintf(text, sizeof text, " (rows %u of %u used)", info.registered, info.capacity);
        return text;
    }

    bool ResolveHeadFovaValue(lua_State* L, int valueIndex, const char* fieldName, int column,
                              std::uint16_t& outIndex, std::string& reason)
    {
        outIndex = outfit::kFovaNone;
        const int type = LuaType(L, valueIndex);

        if (type == LUA_TNIL)
            return true;

        if (type == LUA_TNUMBER)
        {
            const int raw = GetLuaInt(L, valueIndex);
            if (raw < 0 || raw > outfit::kFovaNone)
            {
                reason = std::string(fieldName) + " must be 0..32767";
                return false;
            }
            outIndex = static_cast<std::uint16_t>(raw);
            return true;
        }

        if (type == LUA_TSTRING)
        {
            const char* fv2 = GetLuaString(L, valueIndex);
            std::uint32_t found = 0;
            if (const char* failure = outfit::FindFovaIndexByFv2(column, fv2, found))
            {
                reason = std::string(fieldName) + " '" + (fv2 ? fv2 : "") + "': " + failure;
                if (std::strstr(failure, "not in the fova file table"))
                    reason += " - pass {fv2=, fpk=} to register it";
                return false;
            }
            outIndex = static_cast<std::uint16_t>(found);
            return true;
        }

        if (type == LUA_TTABLE)
        {
            std::string fv2, fpk;
            if (!ReadFovaPairTable(L, valueIndex, fv2, fpk))
            {
                reason = std::string(fieldName) + ": a pair table needs an fv2 path";
                return false;
            }
            if (fpk.empty())
            {
                std::uint32_t found = 0;
                if (const char* failure = outfit::FindFovaIndexByFv2(column, fv2.c_str(), found))
                {
                    reason = std::string(fieldName) + " '" + fv2 + "': " + failure;
                    if (std::strstr(failure, "not in the fova file table"))
                        reason += " - add fpk= to register it";
                    return false;
                }
                outIndex = static_cast<std::uint16_t>(found);
                return true;
            }
            if (!ReadBoolField(L, valueIndex, "trustFpk", false) && IhSaysFpkIsMissing(L, fpk))
            {
                reason = std::string(fieldName) + " '" + fpk + "': " + MissingFpkReason();
                return false;
            }
            std::uint32_t registered = 0;
            if (const char* failure = outfit::RegisterFovaPair(column, fv2.c_str(), fpk.c_str(), registered))
            {
                reason = std::string(fieldName) + " '" + fv2 + "': " + failure + ColumnStateSuffix(column);
                return false;
            }
            outIndex = static_cast<std::uint16_t>(registered);
            return true;
        }

        reason = std::string(fieldName) + " must be a row index, an fv2 path string or a {fv2=,fpk=} table";
        return false;
    }

    bool ResolveHeadFovaField(lua_State* L, int tableIndex, const char* fieldName, int column,
                              std::uint16_t& outIndex, std::string& reason)
    {
        const int top = GetLuaTop(L);
        LuaGetField(L, tableIndex, fieldName);
        const bool ok = ResolveHeadFovaValue(L, GetLuaTop(L), fieldName, column, outIndex, reason);
        LuaPop(L, GetLuaTop(L) - top);
        return ok;
    }

    void FormatFovaColumn(const outfit::FovaColumnDiagnostics& column, char* out, std::size_t size)
    {
        if (!column.present)
        {
            std::snprintf(out, size, "none(%u/%u)", column.registered, column.capacity);
            return;
        }
        if (column.index >= column.registered)
        {
            std::snprintf(out, size, "%u/%u/%u UNREGISTERED",
                column.index, column.registered, column.capacity);
            return;
        }
        std::string extra;
        if (column.custom)
            extra += " custom fv2=" + column.fv2Path + " fpk=" + column.fpkPath;
        if (column.fv2PathId == 0)
            extra += " zeroFv2";
        if (column.fpkPathId == 0)
            extra += " zeroFpk";
        std::snprintf(out, size, "%u/%u/%u fv2=0x%016llX fpk=0x%016llX%s",
            column.index, column.registered, column.capacity,
            static_cast<unsigned long long>(column.fv2PathId),
            static_cast<unsigned long long>(column.fpkPathId),
            extra.c_str());
    }

    int __cdecl l_RegisterFace(lua_State* L)
    {
        if (LuaType(L, 1) != LUA_TTABLE)
            return FailWithReason(L, "RegisterFace expects a table");

        const char* key = nullptr;
        if (!ReadStringField(L, 1, "key", key) || !key || !key[0])
            return FailWithReason(L, "missing key");

        if (!outfit::IsValidFaceKey(key))
            return FailWithReason(L, "key must start with a letter or underscore and hold only letters, digits, '_', '.' or '-'");

        if (!outfit::IsFaceSystemReady())
            return FailWithReason(L, "the soldier face system is not ready yet");

        int residue = outfit::kFaceResidueAny;
        if (ReadIntField(L, 1, "preferResidue", residue))
        {
            if (residue < 0 || residue > 3)
                return FailWithReason(L, "preferResidue must be 0..3");
        }

        outfit::CustomFaceRow row;
        std::string fovaReason;
        if (!ResolveHeadFovaField(L, 1, "faceFova",     outfit::kFovaColumn_Face,     row.faceFova,     fovaReason)
         || !ResolveHeadFovaField(L, 1, "faceDecoFova", outfit::kFovaColumn_FaceDeco, row.faceDecoFova, fovaReason)
         || !ResolveHeadFovaField(L, 1, "hairFova",     outfit::kFovaColumn_Hair,     row.hairFova,     fovaReason)
         || !ResolveHeadFovaField(L, 1, "hairDecoFova", outfit::kFovaColumn_HairDeco, row.hairDecoFova, fovaReason))
        {
            Log("[CustomFace] '%s' was not created: %s - the face will not exist\n",
                key, fovaReason.c_str());
            return FailWithReason(L, fovaReason.c_str());
        }
        row.eyeFova      = ReadFovaField(L, 1, "eyeFova");
        row.skinFova     = ReadFovaField(L, 1, "skinFova");

        int scratch = 0;
        if (ReadIntField(L, 1, "gender", scratch))
            row.gender = static_cast<std::uint8_t>(scratch < 0 ? 0 : scratch);
        if (ReadIntField(L, 1, "race", scratch))
            row.race = static_cast<std::uint8_t>(scratch < 0 ? 0 : scratch);
        if (ReadIntField(L, 1, "eyeMeshType", scratch))
            row.eyeMeshType = static_cast<std::uint8_t>(scratch < 0 ? 0 : scratch);

        row.forceHairFova = ReadBoolField(L, 1, "forceHairFova", false);

        const std::int32_t faceId = outfit::RegisterFaceId(key, residue);
        if (faceId <= 0)
        {
            Log("[CustomFace] '%s' got no faceId - the %d..%d pool has no free id left%s, so this face was not created\n",
                key, outfit::kFaceIdBandFirst, outfit::kFaceIdBandLast,
                residue >= 0 ? " for the requested preferResidue" : "");
            return FailWithReason(L, residue >= 0 ? "residue_unavailable" : "pool_exhausted");
        }

        if (const char* failure = outfit::WriteCustomFaceRow(faceId, row))
        {
            Log("[CustomFace] '%s' (faceId %d) was not written: %s - the face will not render\n",
                key, static_cast<int>(faceId), failure);
            return FailWithReason(L, failure);
        }

        PushLuaNumber(L, static_cast<float>(faceId));
        return 1;
    }

    int __cdecl l_GetFaceId(lua_State* L)
    {
        const char* key = GetLuaString(L, 1);
        const std::int32_t faceId = key ? outfit::FindFaceId(key) : 0;
        if (faceId <= 0)
        {
            PushLuaNil(L);
            return 1;
        }
        PushLuaNumber(L, static_cast<float>(faceId));
        return 1;
    }

    int __cdecl l_IsFaceIdClaimed(lua_State* L)
    {
        const int faceId = GetLuaInt(L, 1);
        PushLuaBool(L, outfit::IsFaceRowDefined(faceId));
        return 1;
    }

    int __cdecl l_IsManagedFaceId(lua_State* L)
    {
        const int faceId = GetLuaInt(L, 1);
        PushLuaBool(L, outfit::IsManagedFaceId(faceId));
        return 1;
    }

    int __cdecl l_GetRegisteredFovaCounts(lua_State* L)
    {
        for (int column = 0; column < outfit::kFovaColumnCount; ++column)
            PushLuaNumber(L, static_cast<float>(outfit::GetRegisteredFovaCount(column)));
        return outfit::kFovaColumnCount;
    }

    int __cdecl l_GetFaceIdPoolInfo(lua_State* L)
    {
        int residue = outfit::kFaceResidueAny;
        if (LuaIsNumber(L, 1))
        {
            residue = GetLuaInt(L, 1);
            if (residue < 0 || residue > 3)
                residue = outfit::kFaceResidueAny;
        }

        PushLuaNumber(L, static_cast<float>(outfit::kFaceIdBandFirst));
        PushLuaNumber(L, static_cast<float>(outfit::kFaceIdBandLast));
        PushLuaNumber(L, static_cast<float>(outfit::FaceIdPoolSize()));
        PushLuaNumber(L, static_cast<float>(outfit::FaceIdFreeSlotCount(residue)));
        return 4;
    }

    int __cdecl l_DumpFaceInfo(lua_State* L)
    {
        const int faceId = GetLuaInt(L, 1);

        outfit::FaceDiagnostics info;
        if (!outfit::ReadFaceDiagnostics(faceId, info))
        {
            Log("[CustomFace] faceId %d could not be inspected - the soldier face system is not created yet, so nothing about this face can be reported\n", faceId);
            return FailWithReason(L, "the soldier face system is not ready yet");
        }

        Log("[CustomFace] faceId %d forensics: rowDefined=%d fovaWord=0x%08X flags=0x%08X faceFova=%u"
            " | resident=%d needIndex=%d needCounts=%u,%u,%u needTable=%u/%u blacklisted=%d"
            " | needFiles=%s,%s,%s,%s"
            " | special=%d specialCount=%u specialIds=%u,%u,%u,%u\n",
            faceId,
            info.rowDefined ? 1 : 0,
            info.fovaWord,
            info.flags,
            info.fovaWord & 0xFFu,
            info.resident ? 1 : 0,
            info.needIndex,
            info.needCounts[0], info.needCounts[1], info.needCounts[2],
            info.needCount, info.needCapacity,
            info.needBlacklisted ? 1 : 0,
            info.needFiles[0] ? "set" : "NULL",
            info.needFiles[1] ? "set" : "NULL",
            info.needFiles[2] ? "set" : "NULL",
            info.needFiles[3] ? "set" : "NULL",
            info.special ? 1 : 0,
            info.specialCount,
            info.specialIds[0], info.specialIds[1], info.specialIds[2], info.specialIds[3]);

        char face[400], faceDeco[400], hair[400], hairDeco[400];
        FormatFovaColumn(info.columns[outfit::kFovaColumn_Face],     face,     sizeof face);
        FormatFovaColumn(info.columns[outfit::kFovaColumn_FaceDeco], faceDeco, sizeof faceDeco);
        FormatFovaColumn(info.columns[outfit::kFovaColumn_Hair],     hair,     sizeof hair);
        FormatFovaColumn(info.columns[outfit::kFovaColumn_HairDeco], hairDeco, sizeof hairDeco);
        Log("[CustomFace] faceId %d fova rows (index/registered/capacity) fovaTable=%s: "
            "faceFova=%s | faceDecoFova=%s | hairFova=%s | hairDecoFova=%s\n",
            faceId, info.fovaTableReady ? "ready" : "NOT-READY",
            face, faceDeco, hair, hairDeco);

        PushLuaBool(L, info.rowDefined);
        PushLuaBool(L, info.resident);
        PushLuaBool(L, info.special);
        return 3;
    }

    int __cdecl l_RegisterFovaPair(lua_State* L)
    {
        if (LuaType(L, 1) != LUA_TTABLE)
            return FailWithReason(L, "RegisterFovaPair expects a table");

        int column = 0;
        if (!ReadColumnField(L, 1, column))
            return FailWithReason(L, "column must be faceFova, faceDecoFova, hairFova or hairDecoFova");

        std::string fv2, fpk;
        if (!ReadFovaPairTable(L, 1, fv2, fpk) || fpk.empty())
            return FailWithReason(L, "RegisterFovaPair needs both fv2 and fpk paths");

        if (!ReadBoolField(L, 1, "trustFpk", false) && IhSaysFpkIsMissing(L, fpk))
        {
            Log("[CustomFace] %s pair '%s' + '%s' was refused: %s - no row was added\n",
                outfit::FovaColumnName(column), fv2.c_str(), fpk.c_str(), MissingFpkReason());
            return FailWithReason(L, MissingFpkReason());
        }

        std::uint32_t index = 0;
        if (const char* failure = outfit::RegisterFovaPair(column, fv2.c_str(), fpk.c_str(), index))
        {
            Log("[CustomFace] %s pair '%s' + '%s' was refused: %s%s - no row was added, so no face can use it\n",
                outfit::FovaColumnName(column), fv2.c_str(), fpk.c_str(), failure,
                ColumnStateSuffix(column).c_str());
            return FailWithReason(L, failure);
        }

        PushLuaNumber(L, static_cast<float>(index));
        return 1;
    }

    int __cdecl l_FindFovaIndex(lua_State* L)
    {
        if (LuaType(L, 1) != LUA_TTABLE)
            return FailWithReason(L, "FindFovaIndex expects a table");

        int column = 0;
        if (!ReadColumnField(L, 1, column))
            return FailWithReason(L, "column must be faceFova, faceDecoFova, hairFova or hairDecoFova");

        const char* fv2 = nullptr;
        if (!ReadStringField(L, 1, "fv2", fv2) || !fv2 || !fv2[0])
            return FailWithReason(L, "FindFovaIndex needs an fv2 path");

        std::uint32_t index = 0;
        if (const char* failure = outfit::FindFovaIndexByFv2(column, fv2, index))
            return FailWithReason(L, failure);

        PushLuaNumber(L, static_cast<float>(index));
        return 1;
    }

    int __cdecl l_GetFovaColumnInfo(lua_State* L)
    {
        int column = 0;
        if (!ParseFovaColumnArg(L, 1, column))
            return FailWithReason(L, "column must be faceFova, faceDecoFova, hairFova or hairDecoFova");

        outfit::FovaColumnInfo info;
        if (!outfit::ReadFovaColumnInfo(column, info))
            return FailWithReason(L, "the fova file table is not filled yet - call this from LoadLibraries or later");

        PushLuaNumber(L, static_cast<float>(info.registered));
        PushLuaNumber(L, static_cast<float>(info.capacity));
        PushLuaNumber(L, static_cast<float>(info.base));
        PushLuaNumber(L, static_cast<float>(info.widthLimit));
        return 4;
    }

    luaL_Reg g_VTppSoldierFaceLib[] =
    {
        { "RegisterFace",             l_RegisterFace },
        { "GetFaceId",                l_GetFaceId },
        { "IsFaceIdClaimed",          l_IsFaceIdClaimed },
        { "IsManagedFaceId",          l_IsManagedFaceId },
        { "GetRegisteredFovaCounts",  l_GetRegisteredFovaCounts },
        { "GetFaceIdPoolInfo",        l_GetFaceIdPoolInfo },
        { "DumpFaceInfo",             l_DumpFaceInfo },
        { "RegisterFovaPair",         l_RegisterFovaPair },
        { "FindFovaIndex",            l_FindFovaIndex },
        { "GetFovaColumnInfo",        l_GetFovaColumnInfo },

        { nullptr, nullptr }
    };
}

bool Register_V_TppSoldierFaceLibrary(lua_State* L)
{
    return RegisterLuaLibrary(L, "V_TppSoldierFace", g_VTppSoldierFaceLib);
}
