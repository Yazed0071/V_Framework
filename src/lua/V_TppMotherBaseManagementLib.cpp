#include "pch.h"

extern "C" {
    #include "lua.h"
    #include "lauxlib.h"
}

#include "log.h"
#include "LuaApi.h"
#include "V_TppMotherBaseManagementLib.h"
#include "ChangeLocationMenu.h"
#include "UniqueStaffRegistry.h"
#include "GetPhotoAdditionalTextLangId.h"
#include "OutfitLuaBindings.h"
#include "V_TppGameObjectConstants.h"
#include "../hooks/equip/EquipDevelop_SetEquipUndeveloped.h"
#include "../core/V_FrameWorkState.h"
#include "../hooks/equip/CustomBluePrint.h"
#include "../hooks/equip/RankLimitedItems.h"
#include "../hooks/equip/DataBaseControllerImpl_AddDataBase.h"
#include "FoxHashes.h"
#include "MissionCodeGuard.h"
#include "FeatureModule.h"
#include "../hooks/deploy/DeployMissionRegistry.h"
#include "../hooks/deploy/DeployMissionHooks.h"
#include "../hooks/deploy/DeployControllerImpl_RegisterDeployMissionParam.h"
#include "../hooks/deploy/PoolRewardControllerImpl_RegisterPoolRewardParamDeploy.h"

#include <cstring>
#include <initializer_list>
#include <string>

namespace
{
    // V_TppMotherBaseManagement.AddToChangeLocationMenu({ locationCode1, locationCode2, ... })
    static int __cdecl l_AddToChangeLocationMenu(lua_State* L)
    {
        if (LuaType(L, -1) != LUA_TTABLE)
        {
            LogWarn("AddToChangeLocationMenu: expected a table, ignored");
            return 0;
        }

        for (g_lua_pushnil(L); g_lua_next(L, -2); LuaPop(L, 1))
        {
            if (LuaType(L, -1) == LUA_TNUMBER)
                AddLocationIdToChangeLocationMenu(static_cast<unsigned short>(GetLuaInt(L, -1)));
        }
        return 0;
    }

    // V_TppMotherBaseManagement.AddPhotoAdditionalText({ {missionCode=, photoId=, photoType=, targetTypeLangId=""}, ... })
    static int __cdecl l_AddPhotoAdditionalText(lua_State* L)
    {
        if (LuaType(L, -1) != LUA_TTABLE)
        {
            LogWarn("AddPhotoAdditionalText: expected a table, ignored");
            return 0;
        }

        for (g_lua_pushnil(L); g_lua_next(L, -2); LuaPop(L, 1))
        {
            if (LuaType(L, -1) != LUA_TTABLE)
                continue;

            unsigned short missionCode = 0xFFFF;
            unsigned char photoId = 0xFF;
            unsigned char photoType = 0xFF;
            const char* targetTypeLangIdStr = "";

            LuaGetField(L, -1, "missionCode");
            if (LuaType(L, -1) == LUA_TNUMBER)
                missionCode = static_cast<unsigned short>(GetLuaInt(L, -1));
            LuaPop(L, 1);

            LuaGetField(L, -1, "photoId");
            if (LuaType(L, -1) == LUA_TNUMBER)
                photoId = static_cast<unsigned char>(GetLuaInt(L, -1));
            LuaPop(L, 1);

            LuaGetField(L, -1, "photoType");
            if (LuaType(L, -1) == LUA_TNUMBER)
                photoType = static_cast<unsigned char>(GetLuaInt(L, -1));
            LuaPop(L, 1);

            LuaGetField(L, -1, "targetTypeLangId");
            if (LuaType(L, -1) == LUA_TSTRING)
                targetTypeLangIdStr = GetLuaString(L, -1);
            LuaPop(L, 1);

            if (missionCode == 0xFFFF || photoId == 0xFF || photoType == 0xFF)
                continue;

            AddPhotoAdditionalText(missionCode, photoId, photoType, targetTypeLangIdStr);
        }
        return 0;
    }

    // V_TppMotherBaseManagement.GetDevelopId(key)
    static int __cdecl l_GetDevelopId(lua_State* L)
    {
        std::int32_t developId = 0;
        if (LuaType(L, 1) == LUA_TSTRING)
        {
            const char* key = GetLuaString(L, 1);
            if (key && key[0])
                developId = V_FrameWorkState::GetDevelopIdByKey(key);
        }

        if (developId > 0)
            PushLuaNumber(L, static_cast<float>(developId));
        else
            g_lua_pushnil(L);
        return 1;
    }


    static const char* BluePrintKeyArg(lua_State* L)
    {
        if (LuaType(L, 1) != LUA_TSTRING)
            return nullptr;
        const char* key = GetLuaString(L, 1);
        return (key && key[0]) ? key : nullptr;
    }

    static std::int32_t BluePrintSlotArg(lua_State* L)
    {
        if (LuaType(L, 1) == LUA_TNUMBER)
            return bluePrint::SlotFromPublicId(GetLuaInt(L, 1));

        const char* key = BluePrintKeyArg(L);
        return key ? bluePrint::Find(key) : 0;
    }

    static int __cdecl l_RegisterDataBase(lua_State* L)
    {
        const char* key = BluePrintKeyArg(L);
        const std::int32_t slot = key ? bluePrint::Register(key) : 0;
        if (slot > 0)
        {
            bluePrint::InjectConstantFor(L, key);
            PushLuaNumber(L, static_cast<float>(bluePrint::PublicId(slot)));
        }
        else
        {
            g_lua_pushnil(L);
        }
        return 1;
    }

    static std::int32_t DocumentationTargetId(lua_State* L)
    {
        const int top = GetLuaTop(L);
        std::int32_t publicId = 0;

        LuaGetField(L, 1, "dataBaseId");
        if (LuaIsNumber(L, -1))
        {
            publicId = GetLuaInt(L, -1);
        }
        else
        {
            g_lua_settop(L, top);
            LuaGetField(L, 1, "key");
            if (const char* key = GetLuaString(L, -1))
            {
                const std::int32_t slot = bluePrint::Find(key);
                if (slot > 0)
                    publicId = bluePrint::PublicId(slot);
            }
        }

        g_lua_settop(L, top);
        return (bluePrint::SlotFromPublicId(publicId) > 0) ? publicId : 0;
    }

    static std::uint64_t DocumentationHashField(lua_State* L, const char* field, bool assetPath)
    {
        const int top = GetLuaTop(L);
        std::uint64_t hash = 0;

        LuaGetField(L, 1, field);
        if (LuaIsNumber(L, -1))
            hash = static_cast<std::uint64_t>(GetLuaInt(L, -1));
        else if (const char* text = GetLuaString(L, -1))
            hash = assetPath ? FoxHashes::PathCode64Ext(text) : FoxHashes::StrCode64(text);

        g_lua_settop(L, top);
        return hash;
    }

    static std::uint64_t DocumentationPathField(lua_State* L, const char* field,
                                                const char* legacyField)
    {
        if (const std::uint64_t hash = DocumentationHashField(L, field, true))
            return hash;

        const std::uint64_t legacy = DocumentationHashField(L, legacyField, true);
        if (legacy != 0)
        {
            static bool s_said = false;
            if (!s_said)
            {
                s_said = true;
                LogWarn("SetDataBaseDisplay: '%s' is renamed '%s', old name still accepted for now",
                    legacyField, field);
            }
        }
        return legacy;
    }

    static int __cdecl l_SetDataBaseDisplay(lua_State* L)
    {
        if (LuaType(L, 1) != LUA_TTABLE)
        {
            LogWarn("SetDataBaseDisplay: expected a table, DATABASE row keeps '???'");
            PushLuaBool(L, false);
            return 1;
        }

        const std::int32_t publicId = DocumentationTargetId(L);
        if (publicId <= 0)
        {
            LogWarn("SetDataBaseDisplay: no registered dataBaseId or key, DATABASE row not named");
            PushLuaBool(L, false);
            return 1;
        }

        bluePrintDb::Documentation doc;
        doc.nameHash  = DocumentationHashField(L, "langDocName", false);
        doc.iconHash  = DocumentationPathField(L, "docIcon", "langDocIcon");
        doc.imageHash = DocumentationPathField(L, "docImage", "langDocImage");
        doc.infoHash  = DocumentationHashField(L, "langDocInfo", false);

        const int top = GetLuaTop(L);
        LuaGetField(L, 1, "tab");
        if (LuaIsNumber(L, -1))
        {
            const std::int32_t tab = GetLuaInt(L, -1);
            if (IsListableDataBaseTab(tab))
            {
                doc.tab = static_cast<std::uint8_t>(tab);
            }
            else
            {
                LogWarn("SetDataBaseDisplay %d: tab %d has no list, kept on tab %u, use V_TppDataBase.TAB_*",
                    publicId, tab, static_cast<unsigned>(doc.tab));
            }
        }
        g_lua_settop(L, top);

        if (doc.nameHash == 0 && doc.iconHash == 0 && doc.imageHash == 0 && doc.infoHash == 0)
        {
            LogWarn("SetDataBaseDisplay %d: no langDocName/docIcon/docImage/langDocInfo, row keeps '???'", publicId);
            PushLuaBool(L, false);
            return 1;
        }

        PushLuaBool(L, bluePrintDb::SetDocumentation(publicId, doc));
        return 1;
    }

    static int __cdecl l_SetBluePrint(lua_State* L)
    {
        const std::int32_t slot = BluePrintSlotArg(L);
        if (slot <= 0)
        {
            PushLuaBool(L, false);
            return 1;
        }

        if (LuaType(L, 2) != LUA_TBOOLEAN)
        {
            LogWarn("SetBluePrint: argument #2 must be a boolean, nothing changed");
            PushLuaBool(L, false);
            return 1;
        }

        const bool owned        = GetLuaBool(L, 2);
        const bool alreadyOwned = bluePrint::Has(slot);
        const bool ok           = bluePrint::Set(slot, owned);
        if (ok)
            bluePrint::SetNew(slot, owned);
        if (ok && owned && !alreadyOwned)
            bluePrintDb::AnnounceBluePrintObtained(L, bluePrint::PublicId(slot));
        PushLuaBool(L, ok);
        return 1;
    }

    static int __cdecl l_GetBluePrintId(lua_State* L)
    {
        const std::int32_t slot = BluePrintSlotArg(L);
        if (slot > 0)
            PushLuaNumber(L, static_cast<float>(bluePrint::PublicId(slot)));
        else
            g_lua_pushnil(L);
        return 1;
    }

    static int __cdecl l_HasBluePrint(lua_State* L)
    {
        const std::int32_t slot = BluePrintSlotArg(L);
        PushLuaBool(L, slot > 0 ? bluePrint::Has(slot) : false);
        return 1;
    }

    struct UniqueStaffField
    {
        const char* name;
        int         requiredType;
    };

    static const UniqueStaffField kUniqueStaffRequiredFields[] =
    {
        { "nameLangMessageId",   LUA_TSTRING },
        { "combatSectionPoint",  LUA_TNUMBER },
        { "developSectionPoint", LUA_TNUMBER },
        { "baseDevSectionPoint", LUA_TNUMBER },
        { "supportSectionPoint", LUA_TNUMBER },
        { "spySectionPoint",     LUA_TNUMBER },
        { "medicalSectionPoint", LUA_TNUMBER },
        { "isEnmity",            LUA_TNONE },
        { "moraleEnmity",        LUA_TNUMBER },
        { "condition",           LUA_TSTRING },
        { "badConditionWeight",  LUA_TNUMBER },
        { "langProficEnglish",   LUA_TNONE },
        { "langProficRussian",   LUA_TNONE },
        { "langProficPashto",    LUA_TNONE },
        { "langProficKikongo",   LUA_TNONE },
        { "langProficAfrikaans", LUA_TNONE },
    };

    static bool UniqueStaffFieldsComplete(lua_State* L, const char* label)
    {
        const int top = GetLuaTop(L);

        for (const UniqueStaffField& field : kUniqueStaffRequiredFields)
        {
            LuaGetField(L, 1, field.name);
            const int type = LuaType(L, -1);
            g_lua_settop(L, top);

            if (type == LUA_TNIL || type == LUA_TNONE)
            {
                LogWarn("RegisterUniqueStaff '%s': missing %s, not registered", label, field.name);
                return false;
            }

            if (field.requiredType != LUA_TNONE && type != field.requiredType)
            {
                LogWarn("RegisterUniqueStaff '%s': %s must be %s, not registered",
                    label, field.name,
                    field.requiredType == LUA_TSTRING ? "a string" : "a number");
                return false;
            }
        }
        return true;
    }

    static int __cdecl l_RegisterUniqueStaff(lua_State* L)
    {
        if (!ResolveLuaApi() || !g_lua_pcall || !g_lua_settop || !g_lua_rawset
            || !g_lua_pushvalue)
        {
            LogError("RegisterUniqueStaff: Lua API unavailable, nothing registered");
            g_lua_pushnil(L);
            return 1;
        }

        if (LuaType(L, 1) != LUA_TTABLE)
        {
            LogWarn("RegisterUniqueStaff: expected a table, nothing registered");
            g_lua_pushnil(L);
            return 1;
        }

        const int top = GetLuaTop(L);

        std::string key;
        LuaGetField(L, 1, "key");
        if (LuaType(L, -1) == LUA_TSTRING)
        {
            if (const char* text = GetLuaString(L, -1))
                key = text;
        }
        g_lua_settop(L, top);

        std::int32_t typeId = 0;

        if (!key.empty())
        {
            if (!uniqueStaff::IsValidKey(key.c_str()))
            {
                LogWarn("RegisterUniqueStaff '%s': invalid key (letter or _ first, [A-Za-z0-9_.-], max 96), not registered",
                    key.c_str());
                g_lua_pushnil(L);
                return 1;
            }

            typeId = uniqueStaff::Register(key.c_str());
            if (typeId <= 0)
            {
                LogError("RegisterUniqueStaff '%s': all %d uniqueTypeIds taken, not registered",
                    key.c_str(), uniqueStaff::PoolSize());
                g_lua_pushnil(L);
                return 1;
            }
        }
        else
        {
            LuaGetField(L, 1, "uniqueTypeId");
            if (LuaIsNumber(L, -1))
                typeId = GetLuaInt(L, -1);
            g_lua_settop(L, top);

            if (typeId <= 0 || typeId > 253)
            {
                LogWarn("RegisterUniqueStaff: no key and no uniqueTypeId in 1..253, not registered");
                g_lua_pushnil(L);
                return 1;
            }

            if (const char* owner = uniqueStaff::KeyHoldingId(typeId))
            {
                LogWarn("RegisterUniqueStaff: uniqueTypeId %d already owned by '%s', not registered, use key=",
                    typeId, owner);
                g_lua_pushnil(L);
                return 1;
            }
        }

        const char* label = key.empty() ? "<uniqueTypeId>" : key.c_str();

        if (!UniqueStaffFieldsComplete(L, label))
        {
            g_lua_pushnil(L);
            return 1;
        }

        PushLuaString(L, "uniqueTypeId");
        PushLuaNumber(L, static_cast<float>(typeId));
        g_lua_rawset(L, 1);

        LuaGetField(L, LUA_GLOBALSINDEX_51, "TppMotherBaseManagement");
        if (LuaType(L, -1) != LUA_TTABLE)
        {
            g_lua_settop(L, top);
            LogWarn("RegisterUniqueStaff '%s': TppMotherBaseManagement not loaded, not registered", label);
            g_lua_pushnil(L);
            return 1;
        }

        LuaGetField(L, -1, "RegisterUniqueStaff");
        if (LuaType(L, -1) != LUA_TFUNCTION)
        {
            g_lua_settop(L, top);
            LogWarn("RegisterUniqueStaff '%s': TppMotherBaseManagement.RegisterUniqueStaff missing, not registered", label);
            g_lua_pushnil(L);
            return 1;
        }

        g_lua_pushvalue(L, 1);
        if (g_lua_pcall(L, 1, 0, 0) != 0)
        {
            const char* err = LuaIsString(L, -1) ? GetLuaString(L, -1) : nullptr;
            LogWarn("RegisterUniqueStaff '%s': game rejected it: %s, id %d stays reserved",
                label, err ? err : "?", typeId);
            g_lua_settop(L, top);
            g_lua_pushnil(L);
            return 1;
        }

        g_lua_settop(L, top);
        LogInfo("RegisterUniqueStaff %s uniqueTypeId:%d", label, typeId);
        PushLuaNumber(L, static_cast<float>(typeId));
        return 1;
    }

    static int __cdecl l_GetUniqueStaffTypeId(lua_State* L)
    {
        std::int32_t typeId = 0;
        if (LuaType(L, 1) == LUA_TSTRING)
        {
            if (const char* key = GetLuaString(L, 1))
                typeId = uniqueStaff::Find(key);
        }

        if (typeId > 0)
            PushLuaNumber(L, static_cast<float>(typeId));
        else
            g_lua_pushnil(L);
        return 1;
    }

    constexpr float  kCaptureDeployMissionId = 1.0f;
    constexpr float  kCaptureRewardKey       = 254.0f;
    constexpr int    kLastRewardSection      = 6;
    constexpr int    kInheritanceDepthLimit  = 6;
    constexpr double kRewardByteMax          = 255.0;
    constexpr double kRewardWordMax          = 65535.0;
    constexpr double kRewardGmpMax           = 2147483647.0;

    static std::string StringField(lua_State* L, int table, const char* field)
    {
        const int top = GetLuaTop(L);
        LuaGetField(L, table, field);
        std::string text;
        if (LuaType(L, -1) == LUA_TSTRING)
            if (const char* value = GetLuaString(L, -1))
                text = value;
        g_lua_settop(L, top);
        return text;
    }

    static bool NumberField(lua_State* L, int table, const char* field, int& value)
    {
        const int top = GetLuaTop(L);
        LuaGetField(L, table, field);
        const bool isNumber = LuaType(L, -1) == LUA_TNUMBER;
        if (isNumber)
            value = GetLuaInt(L, -1);
        g_lua_settop(L, top);
        return isNumber;
    }

    static bool KeyIsOneOf(lua_State* L, int keyIndex, std::initializer_list<const char*> names)
    {
        if (LuaType(L, keyIndex) != LUA_TSTRING)
            return false;
        const char* key = GetLuaString(L, keyIndex);
        if (!key)
            return false;
        for (const char* name : names)
            if (std::strcmp(key, name) == 0)
                return true;
        return false;
    }

    static void CopyMissingFields(lua_State* L, int from, int copy, std::initializer_list<const char*> dropped)
    {
        for (g_lua_pushnil(L); g_lua_next(L, from); LuaPop(L, 1))
        {
            if (KeyIsOneOf(L, -2, dropped))
                continue;
            g_lua_pushvalue(L, -2);
            g_lua_gettable(L, copy);
            const bool present = LuaType(L, -1) != LUA_TNIL;
            LuaPop(L, 1);
            if (present)
                continue;
            g_lua_pushvalue(L, -2);
            g_lua_pushvalue(L, -2);
            g_lua_rawset(L, copy);
        }
    }

    enum class InheritanceProblem { None, Unreadable, ProtectedMetatable, FunctionIndex, TooDeep };

    static const char* InheritanceProblemText(InheritanceProblem problem)
    {
        switch (problem)
        {
        case InheritanceProblem::Unreadable:         return "its metatable cannot be read";
        case InheritanceProblem::ProtectedMetatable: return "it has a protected metatable";
        case InheritanceProblem::FunctionIndex:      return "it inherits through a function __index";
        case InheritanceProblem::TooDeep:            return "its __index chain is deeper than 6 tables or loops";
        default:                                     return "";
        }
    }

    static void PushMetatableGetter(lua_State* L)
    {
        LuaGetField(L, LUA_GLOBALSINDEX_51, "debug");
        if (LuaType(L, -1) == LUA_TTABLE)
            LuaGetField(L, -1, "getmetatable");
        else
            LuaGetField(L, LUA_GLOBALSINDEX_51, "getmetatable");
    }

    static int PushIndexTableOf(lua_State* L, int getter, int table, InheritanceProblem& problem)
    {
        const int top = GetLuaTop(L);
        g_lua_pushvalue(L, getter);
        g_lua_pushvalue(L, table);
        if (g_lua_pcall(L, 1, 1, 0) != 0)
        {
            problem = InheritanceProblem::Unreadable;
            g_lua_settop(L, top);
            return 0;
        }
        const int metatableType = LuaType(L, -1);
        if (metatableType != LUA_TTABLE)
        {
            if (metatableType != LUA_TNIL)
                problem = InheritanceProblem::ProtectedMetatable;
            g_lua_settop(L, top);
            return 0;
        }

        const int metatable = GetLuaTop(L);
        LuaGetField(L, LUA_GLOBALSINDEX_51, "rawget");
        g_lua_pushvalue(L, metatable);
        PushLuaString(L, "__index");
        if (LuaType(L, -3) != LUA_TFUNCTION || g_lua_pcall(L, 2, 1, 0) != 0)
        {
            problem = InheritanceProblem::Unreadable;
            g_lua_settop(L, top);
            return 0;
        }
        const int indexType = LuaType(L, -1);
        if (indexType == LUA_TTABLE)
            return GetLuaTop(L);
        if (indexType != LUA_TNIL)
            problem = InheritanceProblem::FunctionIndex;
        g_lua_settop(L, top);
        return 0;
    }

    static int CopyTableForVanilla(lua_State* L, int source, const char* idField, float idValue,
                                   std::initializer_list<const char*> dropped, InheritanceProblem& problem)
    {
        problem = InheritanceProblem::None;
        g_lua_createtable(L, 0, 32);
        const int copy = GetLuaTop(L);
        CopyMissingFields(L, source, copy, dropped);

        PushMetatableGetter(L);
        const int getter = GetLuaTop(L);
        if (LuaType(L, getter) != LUA_TFUNCTION)
            problem = InheritanceProblem::Unreadable;

        int level = source;
        for (int depth = 0; problem == InheritanceProblem::None; ++depth)
        {
            const int inherited = PushIndexTableOf(L, getter, level, problem);
            if (inherited == 0)
                break;
            if (depth == kInheritanceDepthLimit)
                problem = InheritanceProblem::TooDeep;
            else
                CopyMissingFields(L, inherited, copy, dropped);
            level = inherited;
        }
        if (problem != InheritanceProblem::None)
        {
            g_lua_settop(L, copy - 1);
            return 0;
        }
        g_lua_settop(L, copy);

        PushLuaString(L, idField);
        PushLuaNumber(L, idValue);
        g_lua_rawset(L, copy);
        return copy;
    }

    static double RewardFieldMax(const char* field)
    {
        if (std::strcmp(field, "gmp") == 0)
            return kRewardGmpMax;
        if (std::strcmp(field, "resourceDrawCount") == 0)
            return kRewardWordMax;
        return kRewardByteMax;
    }

    static bool SaturateRewardNumbers(lua_State* L, int copy)
    {
        bool saturated = false;
        for (g_lua_pushnil(L); g_lua_next(L, copy); LuaPop(L, 1))
        {
            if (!LuaIsNumber(L, -1) || LuaType(L, -2) != LUA_TSTRING
                || KeyIsOneOf(L, -2, { "keyValue1", "keyValue2", "keyItemDataBaseId" }))
                continue;

            const double value = g_lua_tonumber(L, -1);
            const double limit = RewardFieldMax(GetLuaString(L, -2));
            const double bounded = !(value >= 0.0) ? 0.0 : (value > limit ? limit : value);
            if (bounded == value && LuaType(L, -1) == LUA_TNUMBER)
                continue;

            saturated = true;
            g_lua_pushvalue(L, -2);
            g_lua_pushnumber(L, bounded);
            g_lua_rawset(L, copy);
        }
        return saturated;
    }

    static bool ForwardToVanilla(lua_State* L, const char* function, int table, std::string& error,
                                 void (*beforeCall)() = nullptr)
    {
        const int top = GetLuaTop(L);
        LuaGetField(L, LUA_GLOBALSINDEX_51, "TppMotherBaseManagement");
        if (LuaType(L, -1) != LUA_TTABLE)
        {
            g_lua_settop(L, top);
            error = "TppMotherBaseManagement is not loaded";
            return false;
        }
        LuaGetField(L, -1, function);
        if (LuaType(L, -1) != LUA_TFUNCTION)
        {
            g_lua_settop(L, top);
            error = std::string("TppMotherBaseManagement.") + function + " is missing";
            return false;
        }
        g_lua_pushvalue(L, table);
        if (beforeCall)
            beforeCall();
        if (g_lua_pcall(L, 1, 0, 0) != 0)
        {
            const char* message = LuaIsString(L, -1) ? GetLuaString(L, -1) : nullptr;
            error = message ? message : "?";
            g_lua_settop(L, top);
            return false;
        }
        g_lua_settop(L, top);
        return true;
    }

    static bool DeployApiReady(lua_State* L, const char* function, int expectedType)
    {
        if (!ResolveLuaApi() || !g_lua_pcall || !g_lua_settop || !g_lua_rawset || !g_lua_pushvalue
            || !g_lua_createtable || !g_lua_next || !g_lua_gettable || !g_lua_tonumber || !g_lua_pushnumber)
        {
            LogError("%s: Lua API unavailable, nothing done", function);
            return false;
        }
        if (MissionCodeGuard::ShouldBypassHooks())
        {
            LogWarn("%s: refused during FOB missions", function);
            return false;
        }
        if (FeatureIsDisabled("DispatchMission"))
        {
            LogWarn("%s: DispatchMission is listed in disabled_modules.txt, nothing done", function);
            return false;
        }
        if (DeployMissionHooksFailed())
        {
            LogWarn("%s: custom dispatch missions are off on this game build, see the Dispatch error above", function);
            return false;
        }
        if (LuaType(L, 1) != expectedType)
        {
            LogWarn("%s: expected a %s, nothing done", function,
                expectedType == LUA_TTABLE ? "table" : "string");
            return false;
        }
        return true;
    }

    static int __cdecl PassThroughDeployMission(lua_State* L)
    {
        int id = 0;
        if (!NumberField(L, 1, "deployMissionId", id))
        {
            LogWarn("RegisterDeployMissionParam: no key and no deployMissionId, nothing registered");
            PushLuaBool(L, false);
            return 1;
        }

        if (id < 1 || id >= deployMission::kVanillaIdBound)
        {
            LogWarn("RegisterDeployMissionParam: deployMissionId %d is not a vanilla id (1-%d), use key=\"<key>\" for custom missions - not registered",
                id, deployMission::kVanillaIdBound - 1);
            PushLuaBool(L, false);
            return 1;
        }
        if (id >= deployMission::kFirstPoolId && id <= deployMission::kLastPoolId)
        {
            LogWarn("RegisterDeployMissionParam: deployMissionId %d is in the random pool (21-119), which the game fills by registration order - not registered",
                id);
            PushLuaBool(L, false);
            return 1;
        }
        if (!deployMission::DeployIface())
        {
            LogWarn("RegisterDeployMissionParam: deployMissionId %d - the dispatch controller is not ready, not registered", id);
            PushLuaBool(L, false);
            return 1;
        }

        std::string error;
        const bool forwarded = ForwardToVanilla(L, "RegisterDeployMissionParam", 1, error);
        if (!forwarded)
            LogWarn("RegisterDeployMissionParam: deployMissionId %d rejected by the game: %s", id, error.c_str());
        PushLuaBool(L, forwarded);
        return 1;
    }

    static int __cdecl l_RegisterDeployMissionParam(lua_State* L)
    {
        if (!DeployApiReady(L, "RegisterDeployMissionParam", LUA_TTABLE))
        {
            g_lua_pushnil(L);
            return 1;
        }

        const std::string key = StringField(L, 1, "key");
        if (key.empty())
            return PassThroughDeployMission(L);

        if (!deployMission::IsValidKey(key.c_str()))
        {
            LogWarn("RegisterDeployMissionParam '%s': invalid key (letter or _ first, [A-Za-z0-9_.-], max 96), not registered",
                key.c_str());
            g_lua_pushnil(L);
            return 1;
        }

        const std::string nameLangId = StringField(L, 1, "nameLangId");
        const std::string infoLangId = StringField(L, 1, "infoLangId");
        if (!deployMission::IsSafeLangId(nameLangId) || !deployMission::IsSafeLangId(infoLangId))
        {
            LogWarn("RegisterDeployMissionParam '%s': nameLangId/infoLangId contains a quote, backslash or line break, not registered",
                key.c_str());
            g_lua_pushnil(L);
            return 1;
        }

        bool important = false;
        {
            const int top = GetLuaTop(L);
            LuaGetField(L, 1, "important");
            important = LuaType(L, -1) == LUA_TBOOLEAN && GetLuaBool(L, -1);
            g_lua_settop(L, top);
        }
        int revengeMissionId = 0;
        if (NumberField(L, 1, "revengeMissionId", revengeMissionId)
            && (revengeMissionId < deployMission::kFirstRevengeId || revengeMissionId > deployMission::kLastRevengeId))
        {
            LogWarn("RegisterDeployMissionParam '%s': revengeMissionId %d is not a DEPLOY_MISSION_ID_REVENGE_* id, ignored",
                key.c_str(), revengeMissionId);
            revengeMissionId = 0;
        }

        std::uint8_t* iface = deployMission::DeployIface();
        const std::uint8_t* firstRow = deployMission::VanillaRecord(iface, 1);
        std::uint8_t firstRowBackup[deployMission::kRecordSize] = {};
        if (!iface || !EnsureDeployMissionCaptureInstalled()
            || !deployMission::CopyEngineMemory(firstRowBackup, firstRow, deployMission::kRecordSize))
        {
            LogWarn("RegisterDeployMissionParam '%s': the dispatch controller is not ready, not registered", key.c_str());
            g_lua_pushnil(L);
            return 1;
        }

        const int top = GetLuaTop(L);
        InheritanceProblem problem = InheritanceProblem::None;
        const int copy = CopyTableForVanilla(L, 1, "deployMissionId", kCaptureDeployMissionId,
                                             { "key", "nameLangId", "infoLangId", "deployMissionId",
                                               "important", "revengeMissionId" }, problem);
        if (copy == 0)
        {
            g_lua_settop(L, top);
            LogWarn("RegisterDeployMissionParam '%s': the table cannot be copied because %s, pass a plain table - not registered",
                key.c_str(), InheritanceProblemText(problem));
            g_lua_pushnil(L);
            return 1;
        }

        std::string error;
        ForwardToVanilla(L, "RegisterDeployMissionParam", copy, error, &BeginDeployMissionCapture);
        std::uint8_t record[deployMission::kRecordSize] = {};
        const bool captured = EndDeployMissionCapture(record);
        g_lua_settop(L, top);

        if (!captured)
        {
            deployMission::CopyEngineMemory(const_cast<std::uint8_t*>(firstRow), firstRowBackup,
                                            deployMission::kRecordSize);
            LogWarn("RegisterDeployMissionParam '%s': the game did not accept the table (%s), not registered",
                key.c_str(), error.empty() ? "no record produced" : error.c_str());
            g_lua_pushnil(L);
            return 1;
        }

        const std::uint16_t id = deployMission::RegisterMission(key.c_str(), record, nameLangId, infoLangId,
                                                                important, static_cast<std::uint16_t>(revengeMissionId));
        if (id == 0)
        {
            g_lua_pushnil(L);
            return 1;
        }
        PushLuaNumber(L, static_cast<float>(id));
        return 1;
    }

    static int PassThroughPoolReward(lua_State* L, int rewardKey)
    {
        if (rewardKey >= deployMission::kFirstPoolId && rewardKey <= deployMission::kLastPoolId)
        {
            LogWarn("RegisterPoolRewardParam: keyValue1 %d is in the random pool (21-119), which the game fills by registration order - reward not registered",
                rewardKey);
            PushLuaBool(L, false);
            return 1;
        }
        if (rewardKey < 1 || rewardKey >= deployMission::kVanillaIdBound)
        {
            LogWarn("RegisterPoolRewardParam: keyValue1 %d is not a vanilla deployMissionId (1-%d), use keyValue1=\"<key>\" for custom missions - reward not registered",
                rewardKey, deployMission::kVanillaIdBound - 1);
            PushLuaBool(L, false);
            return 1;
        }
        if (!deployMission::RewardIface())
        {
            LogWarn("RegisterPoolRewardParam: the reward controller is not ready, reward not registered");
            PushLuaBool(L, false);
            return 1;
        }

        std::string error;
        const bool forwarded = ForwardToVanilla(L, "RegisterPoolRewardParam", 1, error);
        if (!forwarded)
            LogWarn("RegisterPoolRewardParam: keyValue1 %d rejected by the game: %s", rewardKey, error.c_str());
        PushLuaBool(L, forwarded);
        return 1;
    }

    static int __cdecl l_RegisterPoolRewardParam(lua_State* L)
    {
        if (!DeployApiReady(L, "RegisterPoolRewardParam", LUA_TTABLE))
        {
            PushLuaBool(L, false);
            return 1;
        }

        deployMission::MissionView mission;
        std::string key = StringField(L, 1, "keyValue1");
        if (key.empty())
        {
            int rewardKey = 0;
            if (!NumberField(L, 1, "keyValue1", rewardKey))
            {
                LogWarn("RegisterPoolRewardParam: keyValue1 is missing, reward not registered");
                PushLuaBool(L, false);
                return 1;
            }
            if (!deployMission::FindMission(static_cast<std::uint16_t>(rewardKey), mission)
                || rewardKey != mission.id)
                return PassThroughPoolReward(L, rewardKey);
            key = mission.key;
        }

        if (!deployMission::FindMissionByKey(key.c_str(), mission) || !mission.registeredThisLaunch)
        {
            LogWarn("RegisterPoolRewardParam '%s': no dispatch mission registered under that key, call RegisterDeployMissionParam first",
                key.c_str());
            PushLuaBool(L, false);
            return 1;
        }

        if (!deployMission::RewardIface() || !EnsureDeployRewardCaptureInstalled())
        {
            LogWarn("RegisterPoolRewardParam '%s': the reward controller is not ready, reward not registered", key.c_str());
            PushLuaBool(L, false);
            return 1;
        }

        const int top = GetLuaTop(L);
        InheritanceProblem problem = InheritanceProblem::None;
        const int copy = CopyTableForVanilla(L, 1, "keyValue1", kCaptureRewardKey, { "keyValue1", "source" }, problem);
        if (copy == 0)
        {
            g_lua_settop(L, top);
            LogWarn("RegisterPoolRewardParam '%s': the table cannot be copied because %s, pass a plain table - reward not registered",
                key.c_str(), InheritanceProblemText(problem));
            PushLuaBool(L, false);
            return 1;
        }

        int section = 0;
        if (!NumberField(L, copy, "keyValue2", section) || section < 0 || section > kLastRewardSection)
        {
            g_lua_settop(L, top);
            LogWarn("RegisterPoolRewardParam '%s': keyValue2 must be a number from 0 to %d, reward not registered",
                key.c_str(), kLastRewardSection);
            PushLuaBool(L, false);
            return 1;
        }

        if (SaturateRewardNumbers(L, copy))
            LogWarn("RegisterPoolRewardParam '%s': a reward value was outside its field's range and was clamped", key.c_str());

        std::string error;
        ForwardToVanilla(L, "RegisterPoolRewardParam", copy, error, &BeginDeployRewardCapture);
        std::uint8_t reward[deployMission::kRewardSize] = {};
        const bool captured = EndDeployRewardCapture(reward);
        g_lua_settop(L, top);

        if (!captured)
        {
            LogWarn("RegisterPoolRewardParam '%s': the game did not accept the table (%s), reward not registered",
                key.c_str(), error.empty() ? "no record produced" : error.c_str());
            PushLuaBool(L, false);
            return 1;
        }

        PushLuaBool(L, deployMission::RegisterReward(key.c_str(), reward));
        return 1;
    }

    static int __cdecl l_AddDeployMission(lua_State* L)
    {
        if (!DeployApiReady(L, "AddDeployMission", LUA_TSTRING))
        {
            PushLuaBool(L, false);
            return 1;
        }
        const deployMission::ScriptAddResult result = deployMission::AddScriptMission(GetLuaString(L, 1));
        PushLuaBool(L, result == deployMission::ScriptAddResult::Added
                    || result == deployMission::ScriptAddResult::Queued
                    || result == deployMission::ScriptAddResult::AlreadyListed);
        return 1;
    }

    static int __cdecl l_GetDeployMissionCleared(lua_State* L)
    {
        PushLuaBool(L, LuaType(L, 1) == LUA_TSTRING && deployMission::IsCleared(GetLuaString(L, 1)));
        return 1;
    }

    static int __cdecl l_UnsetDeployMissionCleared(lua_State* L)
    {
        if (!DeployApiReady(L, "UnsetDeployMissionCleared", LUA_TSTRING))
        {
            PushLuaBool(L, false);
            return 1;
        }
        PushLuaBool(L, deployMission::UnsetCleared(GetLuaString(L, 1)));
        return 1;
    }

    static int __cdecl l_GetDeployMissionId(lua_State* L)
    {
        const std::uint16_t id = (LuaType(L, 1) == LUA_TSTRING)
            ? deployMission::FindId(GetLuaString(L, 1)) : 0;
        if (id != 0)
            PushLuaNumber(L, static_cast<float>(id));
        else
            g_lua_pushnil(L);
        return 1;
    }

    static luaL_Reg g_VTppMotherBaseManagementLib[] =
    {
        { "AddToChangeLocationMenu", l_AddToChangeLocationMenu },
        { "AddPhotoAdditionalText",  l_AddPhotoAdditionalText },

        { "GetDevelopId",            l_GetDevelopId },
        { "SetEquipDeveloped",       l_SetEquipDeveloped },
        { "SetEquipUndeveloped",     l_SetEquipUndeveloped },
        { "IsEquipDevelopable",      l_IsEquipDevelopable },
        { "IsEquipDeveloped",        l_IsEquipDeveloped },
        { "SetEquipNew",             l_SetEquipNew },
        { "IsEquipNew",              l_IsEquipNew },
        { "SetEquipDevelopVisible",  l_SetEquipDevelopVisible },

        { "AddToEquipDevelopTable",  l_AddToEquipDevelopTable },
        { "GetUsedRankLimitedItemLangIds", RankLimitedItems::Lua_GetUsedRankLimitedItemLangIds },

        { "RegisterUniqueStaff",     l_RegisterUniqueStaff },
        { "GetUniqueStaffTypeId",    l_GetUniqueStaffTypeId },

        { "RegisterDataBase",       l_RegisterDataBase },
        { "SetDataBaseDisplay",        l_SetDataBaseDisplay },
        { "SetBluePrint",            l_SetBluePrint },
        { "HasBluePrint",            l_HasBluePrint },
        { "GetBluePrintId",          l_GetBluePrintId },

        { "RegisterDeployMissionParam", l_RegisterDeployMissionParam },
        { "RegisterPoolRewardParam",    l_RegisterPoolRewardParam },
        { "AddDeployMission",           l_AddDeployMission },
        { "GetDeployMissionId",         l_GetDeployMissionId },
        { "GetDeployMissionCleared",    l_GetDeployMissionCleared },
        { "UnsetDeployMissionCleared",  l_UnsetDeployMissionCleared },


        { nullptr, nullptr }
    };
}

bool Register_V_TppMotherBaseManagementLibrary(lua_State* L)
{
    return RegisterLuaLibrary(L, "V_TppMotherBaseManagement", g_VTppMotherBaseManagementLib);
}
