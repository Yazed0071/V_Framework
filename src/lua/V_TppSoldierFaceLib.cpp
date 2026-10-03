#include "pch.h"

extern "C" {
    #include "lua.h"
    #include "lauxlib.h"
}

#include <cmath>
#include <cstdint>
#include <string>

#include "LuaApi.h"
#include "V_TppSoldierFaceLib.h"
#include "log.h"
#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "../hooks/outfit/CustomFaceTable.h"
#include "../hooks/outfit/FaceIdRegistry.h"
#include "../hooks/outfit/StaffControllerImpl_AddStaff.h"

namespace
{
    using StaffIdToFaceIdLua_t = int(__fastcall*)(lua_State*);
    StaffIdToFaceIdLua_t g_OriginalStaffIdToFaceId = nullptr;

    int __fastcall hkStaffIdToFaceId(lua_State* L)
    {
        const int results = g_OriginalStaffIdToFaceId(L);
        if (results != 1 || MissionCodeGuard::ShouldBypassHooks()
            || !ResolveLuaApi() || !g_lua_tonumber || LuaType(L, 1) != LUA_TTABLE
            || LuaType(L, -1) != LUA_TNUMBER)
            return results;
        const double nativeFace = g_lua_tonumber(L, -1);
        if (!std::isfinite(nativeFace) || nativeFace < 0 || nativeFace > 899
            || std::floor(nativeFace) != nativeFace)
            return results;
        const int top = GetLuaTop(L);
        LuaGetField(L, 1, "staffId");
        const bool numeric = LuaType(L, -1) == LUA_TNUMBER;
        const double identity = numeric ? g_lua_tonumber(L, -1) : -1;
        LuaPop(L, GetLuaTop(L) - top);
        if (!std::isfinite(identity) || identity < 0 || identity > 9007199254740991.0
            || std::floor(identity) != identity)
            return results;
        std::uint16_t visualFace = static_cast<std::uint16_t>(nativeFace);
        if (outfit::TryResolveStaffVisualFace(static_cast<std::uint64_t>(identity),
                                             visualFace, visualFace))
        {
            LuaPop(L, 1);
            PushLuaNumber(L, static_cast<float>(visualFace));
        }
        return results;
    }

    bool ReadStringField(lua_State* L, int tableIndex, const char* fieldName, const char*& outValue)
    {
        const int top = GetLuaTop(L);
        LuaGetField(L, tableIndex, fieldName);
        const bool ok = LuaType(L, -1) == LUA_TSTRING;
        if (ok)
            outValue = GetLuaString(L, -1);
        LuaPop(L, GetLuaTop(L) - top);
        return ok;
    }

    bool ReadBoundedInteger(lua_State* L, int valueIndex, int maxValue, int& outValue)
    {
        if (LuaType(L, valueIndex) != LUA_TNUMBER || !ResolveLuaApi() || !g_lua_tonumber)
            return false;
        const lua_Number raw = g_lua_tonumber(L, valueIndex);
        if (!std::isfinite(raw) || raw < 0 || raw > maxValue || std::floor(raw) != raw)
            return false;
        outValue = static_cast<int>(raw);
        return true;
    }

    bool ReadBoolField(lua_State* L, int tableIndex, const char* fieldName,
                       bool& outValue, std::string& reason)
    {
        const int top = GetLuaTop(L);
        LuaGetField(L, tableIndex, fieldName);
        const int type = LuaType(L, -1);
        if (type == LUA_TBOOLEAN)
            outValue = GetLuaBool(L, -1);
        LuaPop(L, GetLuaTop(L) - top);
        if (type == LUA_TNIL || type == LUA_TBOOLEAN)
            return true;
        reason = std::string(fieldName) + " must be a boolean";
        return false;
    }

    bool ReadRangedField(lua_State* L, int tableIndex, const char* fieldName, int maxValue,
                         int& outValue, std::string& reason)
    {
        const int top = GetLuaTop(L);
        LuaGetField(L, tableIndex, fieldName);
        const bool absent = LuaType(L, -1) == LUA_TNIL;
        const bool ok = absent || ReadBoundedInteger(L, -1, maxValue, outValue);
        LuaPop(L, GetLuaTop(L) - top);
        if (!ok)
        {
            reason = std::string(fieldName) + " must be an integer in 0.." + std::to_string(maxValue);
            return false;
        }
        return true;
    }

    void WarnLegacyFieldOverride(lua_State* L, int tableIndex, const char* fieldName,
                                 const char* legacyName)
    {
        const char* key = nullptr;
        ReadStringField(L, tableIndex, "key", key);
        LogWarn("RegisterFace '%s': %s conflicts with %s, using %s",
                key && key[0] ? key : "(unknown)", legacyName, fieldName, fieldName);
    }

    void WarnRemovedField(lua_State* L, int tableIndex, const char* fieldName, const char* consequence)
    {
        const int top = GetLuaTop(L);
        LuaGetField(L, tableIndex, fieldName);
        const bool present = LuaType(L, -1) != LUA_TNIL;
        LuaPop(L, GetLuaTop(L) - top);
        if (!present)
            return;
        const char* key = nullptr;
        ReadStringField(L, tableIndex, "key", key);
        LogWarn("RegisterFace '%s': %s is no longer supported, ignored; %s",
                key && key[0] ? key : "(unknown)", fieldName, consequence);
    }

    bool ReadRangedFieldAlias(lua_State* L, int tableIndex, const char* fieldName,
                              const char* legacyName, int maxValue,
                              int& outValue, std::string& reason)
    {
        int namedValue = -1;
        int legacyValue = -1;
        if (!ReadRangedField(L, tableIndex, fieldName, maxValue, namedValue, reason)
            || !ReadRangedField(L, tableIndex, legacyName, maxValue, legacyValue, reason))
            return false;
        if (namedValue >= 0 && legacyValue >= 0 && namedValue != legacyValue)
            WarnLegacyFieldOverride(L, tableIndex, fieldName, legacyName);
        if (namedValue >= 0)
            outValue = namedValue;
        else if (legacyValue >= 0)
            outValue = legacyValue;
        return true;
    }

    bool ReadOptionalStringField(lua_State* L, int tableIndex, const char* fieldName,
                                 std::string& outValue, std::string& reason)
    {
        const int top = GetLuaTop(L);
        LuaGetField(L, tableIndex, fieldName);
        const int type = LuaType(L, -1);
        if (type == LUA_TSTRING)
        {
            const char* text = GetLuaString(L, -1);
            outValue = text ? text : "";
        }
        LuaPop(L, GetLuaTop(L) - top);
        if (type == LUA_TNIL || type == LUA_TSTRING)
            return true;
        reason = std::string(fieldName) + " must be a string";
        return false;
    }

    bool ReadOptionalFovaScalar(lua_State* L, int tableIndex, const char* fieldName,
                               std::uint16_t& outValue, std::string& reason)
    {
        int raw = outValue;
        if (!ReadRangedField(L, tableIndex, fieldName, outfit::kFovaNone, raw, reason))
            return false;
        if (raw == outfit::kFovaLegacyNone)
            raw = outfit::kFovaNone;
        if (raw > 31 && raw != outfit::kFovaNone)
        {
            reason = std::string(fieldName) + " must be 0..31, or 32767 or 65535 for none";
            return false;
        }
        outValue = static_cast<std::uint16_t>(raw);
        return true;
    }

    bool ReadNativeFaceFields(lua_State* L, int tableIndex,
                              outfit::CustomFaceRow& row, std::string& reason)
    {
        WarnRemovedField(L, tableIndex, "faceFlags",
            "use race, mbEligible, forceHairFova");
        WarnRemovedField(L, tableIndex, "unk1",
            "use race, mbEligible, forceHairFova");

        int nativeUnique = -1;
        int nativeEyeMesh = -1;
        if (!ReadRangedField(L, tableIndex, "unk2", 1, nativeUnique, reason)
            || !ReadRangedField(L, tableIndex, "unk5", 1, nativeEyeMesh, reason))
            return false;

        if (nativeUnique >= 0)
            row.unique = nativeUnique != 0;
        if (nativeEyeMesh >= 0)
            row.eyeMeshType = static_cast<std::uint8_t>(nativeEyeMesh);

        int gender = row.gender;
        int race = row.race;
        int eyeMeshType = row.eyeMeshType;
        int outsideFaceRef1 = row.outsideFaceRef1;
        int outsideFaceRef2 = row.outsideFaceRef2;
        int outsideFaceRef3 = row.outsideFaceRef3;
        int outsideFaceCount = row.outsideFaceCount;
        if (!ReadOptionalFovaScalar(L, tableIndex, "eyeFova", row.eyeFova, reason)
            || !ReadOptionalFovaScalar(L, tableIndex, "skinFova", row.skinFova, reason)
            || !ReadRangedField(L, tableIndex, "gender", 1, gender, reason)
            || !ReadRangedField(L, tableIndex, "race", 3, race, reason)
            || !ReadRangedField(L, tableIndex, "eyeMeshType", 1, eyeMeshType, reason)
            || !ReadBoolField(L, tableIndex, "forceHairFova", row.forceHairFova, reason)
            || !ReadBoolField(L, tableIndex, "mbEligible", row.mbEligible, reason)
            || !ReadBoolField(L, tableIndex, "unique", row.unique, reason)
            || !ReadRangedFieldAlias(L, tableIndex, "outsideFaceRef1", "unk7", 1023, outsideFaceRef1, reason)
            || !ReadRangedFieldAlias(L, tableIndex, "outsideFaceRef2", "unk8", 1023, outsideFaceRef2, reason)
            || !ReadRangedFieldAlias(L, tableIndex, "outsideFaceRef3", "unk9", 1023, outsideFaceRef3, reason)
            || !ReadRangedFieldAlias(L, tableIndex, "outsideFaceCount", "unk10", 3, outsideFaceCount, reason)
            || !ReadOptionalStringField(L, tableIndex, "uiTextureName", row.uiTextureName, reason)
            || !ReadOptionalStringField(L, tableIndex, "uiTexBasePath", row.uiTexBasePath, reason))
            return false;

        if (nativeUnique >= 0 && (nativeUnique != 0) != row.unique)
            WarnLegacyFieldOverride(L, tableIndex, "unique", "unk2");
        if (nativeEyeMesh >= 0 && nativeEyeMesh != eyeMeshType)
            WarnLegacyFieldOverride(L, tableIndex, "eyeMeshType", "unk5");

        int textureCount = row.uiTextureName.empty() ? 0 : 1;
        if (!ReadRangedField(L, tableIndex, "uiTextureCount", 3, textureCount, reason))
            return false;

        row.gender = static_cast<std::uint8_t>(gender);
        row.race = static_cast<std::uint8_t>(race);
        row.eyeMeshType = static_cast<std::uint8_t>(eyeMeshType);
        row.outsideFaceRef1 = static_cast<std::uint16_t>(outsideFaceRef1);
        row.outsideFaceRef2 = static_cast<std::uint16_t>(outsideFaceRef2);
        row.outsideFaceRef3 = static_cast<std::uint16_t>(outsideFaceRef3);
        row.outsideFaceCount = static_cast<std::uint8_t>(outsideFaceCount);
        row.uiTextureCount = static_cast<std::uint8_t>(textureCount);
        return true;
    }

    int FailWithReason(lua_State* L, const char* reason)
    {
        PushLuaNil(L);
        PushLuaString(L, reason);
        return 2;
    }

    bool FieldIsNilOrString(lua_State* L, int tableIndex, const char* fieldName)
    {
        const int top = GetLuaTop(L);
        LuaGetField(L, tableIndex, fieldName);
        const int type = LuaType(L, -1);
        LuaPop(L, GetLuaTop(L) - top);
        return type == LUA_TNIL || type == LUA_TSTRING;
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

    void ReportOverflowedFovaColumns()
    {
        static bool reported = false;
        if (reported)
            return;

        for (int column = 0; column < outfit::kHeadFovaColumnCount; ++column)
        {
            outfit::FovaColumnInfo info;
            if (!outfit::ReadFovaColumnInfo(column, info))
                continue;
            reported = true;
            if (info.registered <= info.capacity)
                continue;
            LogError("CustomFace: %s list holds %u rows but the fova table fits %u, rows %u+ "
                     "unusable; see ih_log.txt",
                     outfit::FovaColumnName(column), info.registered, info.capacity,
                     info.capacity);
        }
    }

    bool ResolveByFv2(int column, const char* fieldName, const std::string& fv2, const char* fpk,
                      std::uint16_t& outIndex, std::string& reason)
    {
        std::uint32_t found = 0;
        if (const char* failure = outfit::FindFovaIndexByFv2(column, fv2.c_str(), fpk, found))
        {
            reason = std::string(fieldName) + " '" + fv2 + "': " + failure;
            return false;
        }
        outIndex = static_cast<std::uint16_t>(found);
        return true;
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
            int raw = 0;
            if (!ReadBoundedInteger(L, valueIndex, outfit::kFovaNone, raw))
            {
                reason = std::string(fieldName) + " must be an integer in 0..65535";
                return false;
            }
            outIndex = raw == outfit::kFovaLegacyNone ? outfit::kFovaNone : static_cast<std::uint16_t>(raw);
            return true;
        }

        if (type == LUA_TSTRING)
        {
            const char* fv2 = GetLuaString(L, valueIndex);
            return ResolveByFv2(column, fieldName, fv2 ? fv2 : "", nullptr, outIndex, reason);
        }

        if (type == LUA_TTABLE)
        {
            std::string fv2, fpk;
            if (!ReadFovaPairTable(L, valueIndex, fv2, fpk))
            {
                reason = std::string(fieldName) + ": a {fv2=, fpk=} table needs an fv2 path";
                return false;
            }
            if (!FieldIsNilOrString(L, valueIndex, "fpk"))
            {
                reason = std::string(fieldName) + ": fpk must be a path string when it is given";
                return false;
            }
            return ResolveByFv2(column, fieldName, fv2, fpk.empty() ? nullptr : fpk.c_str(),
                                outIndex, reason);
        }

        reason = std::string(fieldName) + " must be a row index, an fv2 path string or a {fv2=, fpk=} table";
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

    int __cdecl l_RegisterFace(lua_State* L)
    {
        if (LuaType(L, 1) != LUA_TTABLE)
            return FailWithReason(L, "RegisterFace expects a table");

        const char* key = nullptr;
        if (!ReadStringField(L, 1, "key", key) || !key || !key[0])
            return FailWithReason(L, "missing key");

        if (!outfit::IsValidFaceKey(key))
            return FailWithReason(L, "key must start with a letter or underscore and hold only letters, digits, '_', '.' or '-'");

        outfit::NoteFaceKeyRequested(key);

        if (!outfit::IsFaceSystemReady())
            return FailWithReason(L, "the soldier face system is not ready yet");

        ReportOverflowedFovaColumns();

        WarnRemovedField(L, 1, "preferResidue", "faceIds have no residue filter");

        const int top = GetLuaTop(L);
        LuaGetField(L, 1, "faceId");
        const bool hasFaceId = LuaType(L, -1) != LUA_TNIL;
        LuaPop(L, GetLuaTop(L) - top);
        if (hasFaceId)
            return FailWithReason(L, "faceId is not supported by RegisterFace; IDs are allocated automatically from key");

        outfit::CustomFaceRow row;
        std::string fovaReason;
        if (!ResolveHeadFovaField(L, 1, "faceFova",     outfit::kFovaColumn_Face,     row.faceFova,     fovaReason)
         || !ResolveHeadFovaField(L, 1, "faceDecoFova", outfit::kFovaColumn_FaceDeco, row.faceDecoFova, fovaReason)
         || !ResolveHeadFovaField(L, 1, "hairFova",     outfit::kFovaColumn_Hair,     row.hairFova,     fovaReason)
         || !ResolveHeadFovaField(L, 1, "hairDecoFova", outfit::kFovaColumn_HairDeco, row.hairDecoFova, fovaReason))
        {
            LogWarn("RegisterFace '%s': %s, face not created", key, fovaReason.c_str());
            return FailWithReason(L, fovaReason.c_str());
        }
        std::string fieldReason;
        if (!ReadNativeFaceFields(L, 1, row, fieldReason))
        {
            LogWarn("RegisterFace '%s': %s, face not created", key, fieldReason.c_str());
            return FailWithReason(L, fieldReason.c_str());
        }
        if (row.mbEligible && !outfit::EnsureFobFaceGuard())
        {
            row.mbEligible = false;
            LogWarn("RegisterFace '%s': FOB face guard unavailable, mbEligible ignored", key);
        }

        std::string refused;
        if (!outfit::ValidateCustomFaceRow(row, refused))
        {
            LogWarn("RegisterFace '%s': %s, face not created", key, refused.c_str());
            return FailWithReason(L, refused.c_str());
        }

        outfit::FaceIdRegistrationFailure registrationFailure = outfit::FaceIdRegistrationFailure::None;
        const std::int32_t faceId = outfit::RegisterFaceId(key, &registrationFailure);
        if (faceId <= 0)
        {
            if (registrationFailure == outfit::FaceIdRegistrationFailure::InvalidKey)
                return FailWithReason(L, "invalid_key");
            if (registrationFailure == outfit::FaceIdRegistrationFailure::PersistedFaceIdConflict)
                return FailWithReason(L, "persisted_face_id_conflict: resolve the occupied ID or duplicate key; saved staff IDs are never silently moved");
            LogError("RegisterFace '%s': all %d custom faceIds taken, face not created",
                     key, static_cast<int>(outfit::FaceIdPoolSize()));
            return FailWithReason(L, "pool_exhausted");
        }

        std::string failure;
        if (!outfit::WriteCustomFaceRow(faceId, row, failure))
        {
            LogWarn("RegisterFace '%s' faceId:%d: %s, face not written",
                    key, static_cast<int>(faceId), failure.c_str());
            return FailWithReason(L, failure.c_str());
        }

        outfit::RememberFaceMetadata(faceId, row.gender, row.race);
        LogInfo("RegisterFace %s faceId:%d", key, static_cast<int>(faceId));
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

    luaL_Reg g_VTppSoldierFaceLib[] =
    {
        { "RegisterFace", l_RegisterFace },
        { "GetFaceId",    l_GetFaceId },

        { nullptr, nullptr }
    };
}

bool Register_V_TppSoldierFaceLibrary(lua_State* L)
{
    return RegisterLuaLibrary(L, "V_TppSoldierFace", g_VTppSoldierFaceLib);
}

bool Install_StaffFaceLuaHook()
{
    void* target = ResolveGameAddress(gAddr.StaffIdToFaceIdLua);
    if (!target || !ResolveLuaApi())
    {
        LogError("StaffFace: StaffIdToFaceId address missing, custom staff faces show "
                 "the vanilla stand-in");
        return false;
    }
    if (CreateAndEnableHook(target, &hkStaffIdToFaceId,
                            reinterpret_cast<void**>(&g_OriginalStaffIdToFaceId)))
        return true;
    LogError("StaffFace: StaffIdToFaceId hook failed, custom staff faces show the "
             "vanilla stand-in");
    return false;
}

void Uninstall_StaffFaceLuaHook()
{
    if (g_OriginalStaffIdToFaceId)
    {
        DisableAndRemoveHook(ResolveGameAddress(gAddr.StaffIdToFaceIdLua));
        g_OriginalStaffIdToFaceId = nullptr;
    }
}
