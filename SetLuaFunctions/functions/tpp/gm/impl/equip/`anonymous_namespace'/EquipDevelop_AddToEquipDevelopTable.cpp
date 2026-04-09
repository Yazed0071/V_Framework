#include "pch.h"
#include "EquipDevelop_AddToEquipDevelopTable.h"

#include <Windows.h>
#include <algorithm>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using RegCstDev_t = void(__fastcall*)(lua_State* L);
    using RegFlwDev_t = void(__fastcall*)(lua_State* L);

    struct FieldValue
    {
        enum class Type
        {
            Number,
            String
        };

        std::string name;
        Type type = Type::Number;
        std::int32_t numberValue = 0;
        std::string stringValue;
    };

    struct NamedFieldSpec
    {
        const char* canonicalName;
        const char* aliasName;
    };

    struct DevelopKeyIds
    {
        std::uint16_t developId = 0; // p00
        std::uint16_t flowIndex = 0; // p50
    };

    EquipDevelopAdd::Deps g_Deps{};

    RegCstDev_t g_OrigRegCstDev = nullptr;
    RegFlwDev_t g_OrigRegFlwDev = nullptr;

    bool g_RegCstDevHookInstalled = false;
    bool g_RegFlwDevHookInstalled = false;

    std::mutex g_StateMutex;
    std::unordered_map<std::string, DevelopKeyIds> g_KeyRegistry;

    std::uint32_t g_NextDevelopId = 0;
    std::uint32_t g_NextFlowIndex = 0;
    bool g_ObservedAnyDevelopId = false;
    bool g_ObservedAnyFlowIndex = false;

    constexpr int LUA_TNUMBER_CONST = 3;
    constexpr int LUA_TSTRING_CONST = 4;
    constexpr int LUA_TTABLE_CONST = 5;

    static const NamedFieldSpec k_ConstFieldSpecs[] =
    {
        { "p01", nullptr },
        { "p02", nullptr },
        { "p03", nullptr },
        { "p04", nullptr },
        { "p05", nullptr },
        { "p06", nullptr },
        { "p07", nullptr },
        { "p08", nullptr },
        { "p09", nullptr },

        { "p10", "langPowerUpInfo0"  },
        { "p11", "langPowerUpInfo1"  },
        { "p12", "langPowerUpInfo2"  },
        { "p13", "langPowerUpInfo3"  },
        { "p14", "langPowerUpInfo4"  },
        { "p15", "langPowerUpInfo5"  },
        { "p16", "langPowerUpInfo6"  },
        { "p17", "langPowerUpInfo7"  },
        { "p18", "langPowerUpInfo8"  },
        { "p19", "langPowerUpInfo9"  },
        { "p20", "langPowerUpInfo10" },
        { "p21", "langPowerUpInfo11" },

        { "p30", nullptr },
        { "p31", nullptr },
        { "p32", nullptr },
        { "p33", nullptr },
        { "p34", nullptr },
        { "p35", nullptr },
        { "p36", nullptr }
    };

    static const char* k_FlowFieldNames[] =
    {
        "p51","p52","p53","p54","p55","p56","p57","p58","p59","p60","p61",
        "p62","p63","p64","p65","p66","p67","p68","p69","p70","p71","p72","p73","p74"
    };
}

namespace
{
    static bool ValidateDeps()
    {
        return
            g_Deps.ResolveLuaApi &&
            g_Deps.GetLuaTop &&
            g_Deps.LuaType &&
            g_Deps.LuaSetTop &&
            g_Deps.GetLuaString &&
            g_Deps.GetLuaInt &&
            g_Deps.PushLuaNumber &&
            g_Deps.LuaPushString &&
            g_Deps.LuaCreateTable &&
            g_Deps.LuaGetTable &&
            g_Deps.LuaSetTable;
    }

    static bool EnsureLuaReady()
    {
        return ValidateDeps() && g_Deps.ResolveLuaApi && g_Deps.ResolveLuaApi();
    }

    static int AbsIndex(lua_State* L, int idx)
    {
        if (idx > 0)
            return idx;

        return g_Deps.GetLuaTop(L) + idx + 1;
    }

    static bool IsLuaTable(lua_State* L, int idx)
    {
        return g_Deps.LuaType(L, idx) == LUA_TTABLE_CONST;
    }

    static bool IsLuaNumber(lua_State* L, int idx)
    {
        return g_Deps.LuaType(L, idx) == LUA_TNUMBER_CONST;
    }

    static bool IsLuaString(lua_State* L, int idx)
    {
        return g_Deps.LuaType(L, idx) == LUA_TSTRING_CONST;
    }

    static void PushFieldKey(lua_State* L, const char* key)
    {
        g_Deps.LuaPushString(L, key);
    }

    static bool TryReadIntFieldByName(
        lua_State* L,
        int tableIndex,
        const char* fieldName,
        std::int32_t& outValue)
    {
        outValue = 0;

        const int absIndex = AbsIndex(L, tableIndex);

        PushFieldKey(L, fieldName);
        g_Deps.LuaGetTable(L, absIndex);

        const bool ok = IsLuaNumber(L, -1);
        if (ok)
            outValue = g_Deps.GetLuaInt(L, -1);

        g_Deps.LuaSetTop(L, -2);
        return ok;
    }

    static bool TryReadStringFieldByName(
        lua_State* L,
        int tableIndex,
        const char* fieldName,
        std::string& outValue)
    {
        outValue.clear();

        const int absIndex = AbsIndex(L, tableIndex);

        PushFieldKey(L, fieldName);
        g_Deps.LuaGetTable(L, absIndex);

        const bool ok = IsLuaString(L, -1);
        if (ok)
        {
            const char* value = g_Deps.GetLuaString(L, -1);
            outValue = value ? value : "";
        }

        g_Deps.LuaSetTop(L, -2);
        return ok;
    }

    static void SetIntField(lua_State* L, int tableIndex, const char* key, std::int32_t value)
    {
        const int absIndex = AbsIndex(L, tableIndex);

        PushFieldKey(L, key);
        g_Deps.PushLuaNumber(L, static_cast<float>(value));
        g_Deps.LuaSetTable(L, absIndex);
    }

    static void SetStringField(lua_State* L, int tableIndex, const char* key, const std::string& value)
    {
        const int absIndex = AbsIndex(L, tableIndex);

        PushFieldKey(L, key);
        g_Deps.LuaPushString(L, value.c_str());
        g_Deps.LuaSetTable(L, absIndex);
    }

    static void ObserveDevelopId(std::uint32_t developId)
    {
        std::lock_guard<std::mutex> lock(g_StateMutex);

        g_ObservedAnyDevelopId = true;
        if (developId >= g_NextDevelopId)
            g_NextDevelopId = developId + 1;
    }

    static void ObserveFlowIndex(std::uint32_t flowIndex)
    {
        std::lock_guard<std::mutex> lock(g_StateMutex);

        g_ObservedAnyFlowIndex = true;
        if (flowIndex >= g_NextFlowIndex)
            g_NextFlowIndex = flowIndex + 1;
    }

    static bool ReserveIdsForKey(
        const std::string& key,
        std::uint16_t& outDevelopId,
        std::uint16_t& outFlowIndex)
    {
        std::lock_guard<std::mutex> lock(g_StateMutex);

        if (key.empty())
            return false;

        if (!g_ObservedAnyDevelopId || !g_ObservedAnyFlowIndex)
        {
            Log("[EquipDevelop] Refusing allocation before stock develop tables were observed\n");
            return false;
        }

        const auto found = g_KeyRegistry.find(key);
        if (found != g_KeyRegistry.end())
        {
            Log("[EquipDevelop] Key already registered, skipping: %s\n", key.c_str());
            return false;
        }

        if (g_NextDevelopId > 0xFFFFu || g_NextFlowIndex > 0xFFFFu)
        {
            Log("[EquipDevelop] ID allocator overflow\n");
            return false;
        }

        DevelopKeyIds ids{};
        ids.developId = static_cast<std::uint16_t>(g_NextDevelopId++);
        ids.flowIndex = static_cast<std::uint16_t>(g_NextFlowIndex++);

        g_KeyRegistry.emplace(key, ids);

        outDevelopId = ids.developId;
        outFlowIndex = ids.flowIndex;
        return true;
    }

    static void CollectKnownFields(
        lua_State* L,
        int tableIndex,
        const NamedFieldSpec* specs,
        std::size_t specCount,
        std::vector<FieldValue>& outFields)
    {
        outFields.clear();

        for (std::size_t i = 0; i < specCount; ++i)
        {
            const NamedFieldSpec& spec = specs[i];

            std::int32_t numericValue = 0;
            if (TryReadIntFieldByName(L, tableIndex, spec.canonicalName, numericValue) ||
                (spec.aliasName && TryReadIntFieldByName(L, tableIndex, spec.aliasName, numericValue)))
            {
                FieldValue value{};
                value.name = spec.canonicalName;
                value.type = FieldValue::Type::Number;
                value.numberValue = numericValue;
                outFields.push_back(value);
                continue;
            }

            std::string stringValue;
            if (TryReadStringFieldByName(L, tableIndex, spec.canonicalName, stringValue) ||
                (spec.aliasName && TryReadStringFieldByName(L, tableIndex, spec.aliasName, stringValue)))
            {
                FieldValue value{};
                value.name = spec.canonicalName;
                value.type = FieldValue::Type::String;
                value.stringValue = std::move(stringValue);
                outFields.push_back(value);
                continue;
            }
        }
    }

    static void CollectKnownFields(
        lua_State* L,
        int tableIndex,
        const char* const* names,
        std::size_t nameCount,
        std::vector<FieldValue>& outFields)
    {
        outFields.clear();

        for (std::size_t i = 0; i < nameCount; ++i)
        {
            const char* fieldName = names[i];

            std::int32_t numericValue = 0;
            if (TryReadIntFieldByName(L, tableIndex, fieldName, numericValue))
            {
                FieldValue value{};
                value.name = fieldName;
                value.type = FieldValue::Type::Number;
                value.numberValue = numericValue;
                outFields.push_back(value);
                continue;
            }

            std::string stringValue;
            if (TryReadStringFieldByName(L, tableIndex, fieldName, stringValue))
            {
                FieldValue value{};
                value.name = fieldName;
                value.type = FieldValue::Type::String;
                value.stringValue = std::move(stringValue);
                outFields.push_back(value);
                continue;
            }
        }
    }

    static bool ReadKeyAndPayload(
        lua_State* L,
        std::string& outKey,
        std::vector<FieldValue>& outConstFields,
        std::vector<FieldValue>& outFlowFields)
    {
        outKey.clear();
        outConstFields.clear();
        outFlowFields.clear();

        if (!EnsureLuaReady())
            return false;

        if (!IsLuaString(L, 1))
            return false;
        if (!IsLuaTable(L, 2))
            return false;

        {
            const char* keyValue = g_Deps.GetLuaString(L, 1);
            outKey = keyValue ? keyValue : "";
            if (outKey.empty())
                return false;
        }

        const int payloadIndex = 2;

        PushFieldKey(L, "const");
        g_Deps.LuaGetTable(L, payloadIndex);
        if (!IsLuaTable(L, -1))
        {
            g_Deps.LuaSetTop(L, -2);
            return false;
        }

        CollectKnownFields(
            L,
            g_Deps.GetLuaTop(L),
            k_ConstFieldSpecs,
            sizeof(k_ConstFieldSpecs) / sizeof(k_ConstFieldSpecs[0]),
            outConstFields);

        g_Deps.LuaSetTop(L, -2);

        PushFieldKey(L, "flow");
        g_Deps.LuaGetTable(L, payloadIndex);
        if (!IsLuaTable(L, -1))
        {
            g_Deps.LuaSetTop(L, -2);
            return false;
        }

        CollectKnownFields(
            L,
            g_Deps.GetLuaTop(L),
            k_FlowFieldNames,
            sizeof(k_FlowFieldNames) / sizeof(k_FlowFieldNames[0]),
            outFlowFields);

        g_Deps.LuaSetTop(L, -2);
        return true;
    }

    static void BuildConstRowTable(
        lua_State* L,
        std::uint16_t developId,
        const std::vector<FieldValue>& fields)
    {
        g_Deps.LuaCreateTable(L, 0, static_cast<int>(fields.size() + 1));
        const int tableIndex = g_Deps.GetLuaTop(L);

        SetIntField(L, tableIndex, "p00", developId);

        for (const FieldValue& field : fields)
        {
            if (field.type == FieldValue::Type::Number)
                SetIntField(L, tableIndex, field.name.c_str(), field.numberValue);
            else
                SetStringField(L, tableIndex, field.name.c_str(), field.stringValue);
        }
    }

    static void BuildFlowRowTable(
        lua_State* L,
        std::uint16_t flowIndex,
        const std::vector<FieldValue>& fields)
    {
        g_Deps.LuaCreateTable(L, 0, static_cast<int>(fields.size() + 1));
        const int tableIndex = g_Deps.GetLuaTop(L);

        SetIntField(L, tableIndex, "p50", flowIndex);

        for (const FieldValue& field : fields)
        {
            if (field.type == FieldValue::Type::Number)
                SetIntField(L, tableIndex, field.name.c_str(), field.numberValue);
            else
                SetStringField(L, tableIndex, field.name.c_str(), field.stringValue);
        }
    }

    static bool RegisterDevelopPairImmediate(
        lua_State* L,
        const std::string& key,
        const std::vector<FieldValue>& constFields,
        const std::vector<FieldValue>& flowFields,
        std::uint16_t* outDevelopId = nullptr,
        std::uint16_t* outFlowIndex = nullptr)
    {
        if (!L || !g_OrigRegCstDev || !g_OrigRegFlwDev)
            return false;

        std::uint16_t developId = 0;
        std::uint16_t flowIndex = 0;
        if (!ReserveIdsForKey(key, developId, flowIndex))
            return false;

        g_Deps.LuaSetTop(L, 0);
        BuildConstRowTable(L, developId, constFields);
        g_OrigRegCstDev(L);
        ObserveDevelopId(developId);

        g_Deps.LuaSetTop(L, 0);
        BuildFlowRowTable(L, flowIndex, flowFields);
        g_OrigRegFlwDev(L);
        ObserveFlowIndex(flowIndex);

        g_Deps.LuaSetTop(L, 0);

        if (outDevelopId)
            *outDevelopId = developId;
        if (outFlowIndex)
            *outFlowIndex = flowIndex;

        Log(
            "[EquipDevelop] Registered key=%s p00=%u p50=%u\n",
            key.c_str(),
            static_cast<unsigned>(developId),
            static_cast<unsigned>(flowIndex));

        return true;
    }

    static void __fastcall hkRegCstDev(lua_State* L)
    {
        if (L && EnsureLuaReady() && IsLuaTable(L, 1))
        {
            std::int32_t developId = 0;
            if (TryReadIntFieldByName(L, 1, "p00", developId) && developId >= 0)
                ObserveDevelopId(static_cast<std::uint32_t>(developId));
        }

        if (g_OrigRegCstDev)
            g_OrigRegCstDev(L);
    }

    static void __fastcall hkRegFlwDev(lua_State* L)
    {
        if (L && EnsureLuaReady() && IsLuaTable(L, 1))
        {
            std::int32_t flowIndex = 0;
            if (TryReadIntFieldByName(L, 1, "p50", flowIndex) && flowIndex >= 0)
                ObserveFlowIndex(static_cast<std::uint32_t>(flowIndex));
        }

        if (g_OrigRegFlwDev)
            g_OrigRegFlwDev(L);
    }
}

namespace EquipDevelopAdd
{
    void Bind(const Deps& deps)
    {
        g_Deps = deps;
    }

    int __cdecl Lua_AddToEquipDevelopTable(lua_State* L)
    {
        std::string key;
        std::vector<FieldValue> constFields;
        std::vector<FieldValue> flowFields;

        if (!ReadKeyAndPayload(L, key, constFields, flowFields))
        {
            Log("[EquipDevelop] Invalid AddToEquipDevelopTable call\n");
            return 0;
        }

        std::uint16_t developId = 0;
        std::uint16_t flowIndex = 0;
        if (!RegisterDevelopPairImmediate(L, key, constFields, flowFields, &developId, &flowIndex))
            return 0;

        g_Deps.PushLuaNumber(L, static_cast<float>(developId));
        return 1;
    }

    bool Install_TppMotherBaseManagement_EquipDevelopHooks()
    {
        if (g_RegCstDevHookInstalled && g_RegFlwDevHookInstalled)
            return true;

        void* regCstTarget = ResolveGameAddress(gAddr.TppMotherBaseManagement_RegCstDev);
        void* regFlwTarget = ResolveGameAddress(gAddr.TppMotherBaseManagement_RegFlwDev);

        if (!regCstTarget || !regFlwTarget)
        {
            Log("[EquipDevelop] Failed to resolve RegCstDev / RegFlwDev\n");
            return false;
        }

        if (!g_RegCstDevHookInstalled)
        {
            if (!CreateAndEnableHook(
                regCstTarget,
                reinterpret_cast<void*>(&hkRegCstDev),
                reinterpret_cast<void**>(&g_OrigRegCstDev)))
            {
                Log("[EquipDevelop] Failed to hook RegCstDev\n");
                return false;
            }

            g_RegCstDevHookInstalled = true;
        }

        if (!g_RegFlwDevHookInstalled)
        {
            if (!CreateAndEnableHook(
                regFlwTarget,
                reinterpret_cast<void*>(&hkRegFlwDev),
                reinterpret_cast<void**>(&g_OrigRegFlwDev)))
            {
                Log("[EquipDevelop] Failed to hook RegFlwDev\n");
                return false;
            }

            g_RegFlwDevHookInstalled = true;
        }

        Log("[EquipDevelop] MBM develop hooks installed\n");
        return true;
    }

    bool Uninstall_TppMotherBaseManagement_EquipDevelopHooks()
    {
        if (g_RegCstDevHookInstalled)
        {
            if (void* target = ResolveGameAddress(gAddr.TppMotherBaseManagement_RegCstDev))
                DisableAndRemoveHook(target);

            g_RegCstDevHookInstalled = false;
            g_OrigRegCstDev = nullptr;
        }

        if (g_RegFlwDevHookInstalled)
        {
            if (void* target = ResolveGameAddress(gAddr.TppMotherBaseManagement_RegFlwDev))
                DisableAndRemoveHook(target);

            g_RegFlwDevHookInstalled = false;
            g_OrigRegFlwDev = nullptr;
        }

        return true;
    }
}
