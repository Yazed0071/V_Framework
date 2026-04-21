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
#include "V_FrameWorkState.h"

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

    struct PendingDevelopRequest
    {
        std::string key;
        std::vector<FieldValue> constFields;
        std::vector<FieldValue> flowFields;
    };

    EquipDevelopAdd::Deps g_Deps{};

    RegCstDev_t g_OrigRegCstDev = nullptr;
    RegFlwDev_t g_OrigRegFlwDev = nullptr;

    bool g_RegCstDevHookInstalled = false;
    bool g_RegFlwDevHookInstalled = false;

    std::mutex g_StateMutex;
    std::unordered_map<std::string, DevelopKeyIds> g_KeyRegistry;
    std::vector<PendingDevelopRequest> g_PendingRequests;
    bool g_IsFlushingPending = false;

    std::uint32_t g_NextDevelopId = 0;
    std::uint32_t g_NextFlowIndex = 0;
    bool g_ObservedAnyDevelopId = false;
    bool g_ObservedAnyFlowIndex = false;

    constexpr int LUA_TNUMBER_CONST = 3;
    constexpr int LUA_TSTRING_CONST = 4;
    constexpr int LUA_TTABLE_CONST = 5;

    // Bootstrap values for the current English build you are using.
    // These are the same working custom ranges your logs already showed.
    constexpr std::uint32_t kBootstrapDevelopIdStart = 51006;
    constexpr std::uint32_t kBootstrapFlowIndexStart = 922;
    constexpr std::uint32_t kMaxAllocId = 0xFFFEu;

    static const NamedFieldSpec k_ConstFieldSpecs[] =
    {
        { "p01", "equipID" },
        { "p02", "equipDevelopTypeID" },
        { "p03", "baseEquipDevelopId" },
        { "p04", "skill" },
        { "p05", "bluePrintId" },
        { "p06", "langEquipName" },
        { "p07", "langEquipInfo" },
        { "p08", "iconFtexPath" },
        { "p09", "equipDevelopGroupID" },

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

        { "p30", "langEquipRealName" },
        { "p31", "isResultRankLimited" },
        { "p32", "isCustomEnable" },
        { "p33", "isColorChangeEnable" },
        { "p34", "unk34" },
        { "p35", "isSecurityStaffEquip" },
        { "p36", "unk36" }
    };

    static const NamedFieldSpec k_FlowFieldSpecs[] =
    {
        { "p51", "sideGrade" },
        { "p52", "grade" },
        { "p53", "developGmpCost" },
        { "p54", "usageGmpCost" },
        { "p55", "sectionLvForDevelop" },
        { "p56", "sectionID2ForDevelop" },
        { "p57", "sectionLv2ForDevelop" },
        { "p58", "resourceType1" },
        { "p59", "resourceType1Count" },
        { "p60", "resourceType2" },
        { "p61", "resourceType2Count" },
        { "p62", "initialAvailable" },
        { "p63", "sectionIDForDevelop" },
        { "p64", "developSectionLv" },
        { "p65", "resourceUsageType1" },
        { "p66", "resourceUsageType1Count" },
        { "p67", "resourceUsageType2" },
        { "p68", "resourceUsageType2Count" },
        { "p69", "displayInfo" },
        { "p70", "unk70" },
        { "p71", "developTimeMinute" },
        { "p72", "isValidMbCoin" },
        { "p73", "intimacyPoint" },
        { "p74", "isFobAvailable" },
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

    static bool AreStockDevelopTablesReady_NoLock()
    {
        return g_ObservedAnyDevelopId && g_ObservedAnyFlowIndex;
    }

    static bool CanInjectRowsNow_NoLock()
    {
        // Original function pointers must be available (hooks installed).
        // The observation flags are a bonus — if either is set, the game's
        // develop tables definitely exist. But even without observation,
        // if both hooks are installed the tables are live and injection is safe.
        // This prevents a timing race where our Lua registers suits before
        // the game's stock RegCstDev/RegFlwDev calls set the observation flags.
        return g_OrigRegCstDev && g_OrigRegFlwDev;
    }

    static void EnsureAllocatorSeeded_NoLock()
    {
        if (g_NextDevelopId < kBootstrapDevelopIdStart)
            g_NextDevelopId = kBootstrapDevelopIdStart;

        if (g_NextFlowIndex < kBootstrapFlowIndexStart)
            g_NextFlowIndex = kBootstrapFlowIndexStart;
    }

    static bool TryGetIdsForKey_NoLock(
        const std::string& key,
        std::uint16_t& outDevelopId,
        std::uint16_t& outFlowIndex)
    {
        const auto found = g_KeyRegistry.find(key);
        if (found == g_KeyRegistry.end())
            return false;

        outDevelopId = found->second.developId;
        outFlowIndex = found->second.flowIndex;
        return true;
    }

    static bool TryGetIdsForKey(
        const std::string& key,
        std::uint16_t& outDevelopId,
        std::uint16_t& outFlowIndex)
    {
        std::lock_guard<std::mutex> lock(g_StateMutex);
        return TryGetIdsForKey_NoLock(key, outDevelopId, outFlowIndex);
    }

    static PendingDevelopRequest* FindPendingRequest_NoLock(const std::string& key)
    {
        for (auto& req : g_PendingRequests)
        {
            if (req.key == key)
                return &req;
        }
        return nullptr;
    }

    static bool GetOrCreateIdsForKey(
        const std::string& key,
        std::uint16_t& outDevelopId,
        std::uint16_t& outFlowIndex,
        bool& outCreated)
    {
        outDevelopId = 0;
        outFlowIndex = 0;
        outCreated = false;

        std::lock_guard<std::mutex> lock(g_StateMutex);

        if (key.empty())
            return false;

        const auto found = g_KeyRegistry.find(key);
        if (found != g_KeyRegistry.end())
        {
            outDevelopId = found->second.developId;
            outFlowIndex = found->second.flowIndex;
            return true;
        }

        EnsureAllocatorSeeded_NoLock();

        if (g_NextDevelopId > kMaxAllocId || g_NextFlowIndex > kMaxAllocId)
        {
            Log("[EquipDevelop] ID allocator overflow\n");
            return false;
        }

        DevelopKeyIds ids{};

        // Try to reuse a persistent developId from the unified state
        std::int32_t persistedDevelopId = 0;
        if (V_FrameWorkState::ResolveOrCreateDevelopId(
                key.c_str(),
                static_cast<std::int32_t>(g_NextDevelopId),
                persistedDevelopId) &&
            persistedDevelopId > 0)
        {
            ids.developId = static_cast<std::uint16_t>(persistedDevelopId);
            if (static_cast<std::uint32_t>(persistedDevelopId) >= g_NextDevelopId)
                g_NextDevelopId = static_cast<std::uint32_t>(persistedDevelopId) + 1;
        }
        else
        {
            ids.developId = static_cast<std::uint16_t>(g_NextDevelopId++);
        }

        ids.flowIndex = static_cast<std::uint16_t>(g_NextFlowIndex++);

        g_KeyRegistry.emplace(key, ids);

        outDevelopId = ids.developId;
        outFlowIndex = ids.flowIndex;
        outCreated = true;

        Log(
            "[EquipDevelop] Reserved key=%s p00=%u p50=%u\n",
            key.c_str(),
            static_cast<unsigned>(outDevelopId),
            static_cast<unsigned>(outFlowIndex)
        );

        return true;
    }

    static void QueueOrUpdatePendingRequest(
        const std::string& key,
        const std::vector<FieldValue>& constFields,
        const std::vector<FieldValue>& flowFields)
    {
        std::lock_guard<std::mutex> lock(g_StateMutex);

        if (key.empty())
            return;

        if (PendingDevelopRequest* existing = FindPendingRequest_NoLock(key))
        {
            existing->constFields = constFields;
            existing->flowFields = flowFields;
            return;
        }

        PendingDevelopRequest req{};
        req.key = key;
        req.constFields = constFields;
        req.flowFields = flowFields;
        g_PendingRequests.push_back(std::move(req));

        Log("[EquipDevelop] Queued key=%s until stock develop tables were observed\n", key.c_str());
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
            k_FlowFieldSpecs,
            sizeof(k_FlowFieldSpecs) / sizeof(k_FlowFieldSpecs[0]),
            outFlowFields);

        g_Deps.LuaSetTop(L, -2);

        // Safety clamp: flow grade (p52 / "grade") must be within [0, 15].
        // Out-of-range values silently break R&D UI and unlock gating.
        for (FieldValue& fv : outFlowFields)
        {
            if (fv.type != FieldValue::Type::Number || fv.name != "p52")
                continue;

            if (fv.numberValue < 0)
            {
                Log("[EquipDevelop] Clamped flow grade %d -> 0 for key='%s'\n",
                    fv.numberValue, outKey.c_str());
                fv.numberValue = 0;
            }
            else if (fv.numberValue > 15)
            {
                Log("[EquipDevelop] Clamped flow grade %d -> 15 for key='%s'\n",
                    fv.numberValue, outKey.c_str());
                fv.numberValue = 15;
            }
            break;
        }

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

    static bool InjectReservedDevelopPair(
        lua_State* L,
        const PendingDevelopRequest& req,
        std::uint16_t* outDevelopId = nullptr,
        std::uint16_t* outFlowIndex = nullptr)
    {
        if (!L || !EnsureLuaReady())
            return false;

        std::uint16_t developId = 0;
        std::uint16_t flowIndex = 0;
        if (!TryGetIdsForKey(req.key, developId, flowIndex))
            return false;

        g_Deps.LuaSetTop(L, 0);
        BuildConstRowTable(L, developId, req.constFields);
        g_OrigRegCstDev(L);
        ObserveDevelopId(developId);

        g_Deps.LuaSetTop(L, 0);
        BuildFlowRowTable(L, flowIndex, req.flowFields);
        g_OrigRegFlwDev(L);
        ObserveFlowIndex(flowIndex);

        g_Deps.LuaSetTop(L, 0);

        if (outDevelopId)
            *outDevelopId = developId;
        if (outFlowIndex)
            *outFlowIndex = flowIndex;

        Log(
            "[EquipDevelop] Registered key=%s p00=%u p50=%u\n",
            req.key.c_str(),
            static_cast<unsigned>(developId),
            static_cast<unsigned>(flowIndex)
        );

        return true;
    }

    static void FlushPendingRegistrations(lua_State* L)
    {
        if (!L || !EnsureLuaReady())
            return;

        std::vector<PendingDevelopRequest> work;
        {
            std::lock_guard<std::mutex> lock(g_StateMutex);

            if (g_IsFlushingPending)
                return;

            if (!CanInjectRowsNow_NoLock())
                return;

            if (g_PendingRequests.empty())
                return;

            g_IsFlushingPending = true;
            work = g_PendingRequests;
        }

        std::vector<std::string> completedKeys;

        for (const PendingDevelopRequest& req : work)
        {
            std::uint16_t developId = 0;
            std::uint16_t flowIndex = 0;

            if (InjectReservedDevelopPair(L, req, &developId, &flowIndex))
                completedKeys.push_back(req.key);
        }

        {
            std::lock_guard<std::mutex> lock(g_StateMutex);

            if (!completedKeys.empty())
            {
                g_PendingRequests.erase(
                    std::remove_if(
                        g_PendingRequests.begin(),
                        g_PendingRequests.end(),
                        [&](const PendingDevelopRequest& req)
                        {
                            return std::find(
                                completedKeys.begin(),
                                completedKeys.end(),
                                req.key) != completedKeys.end();
                        }),
                    g_PendingRequests.end());
            }

            g_IsFlushingPending = false;
        }
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

        FlushPendingRegistrations(L);
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

        FlushPendingRegistrations(L);
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
        bool created = false;

        if (!GetOrCreateIdsForKey(key, developId, flowIndex, created))
        {
            Log("[EquipDevelop] Failed to reserve ids for key=%s\n", key.c_str());
            return 0;
        }

        // Already fully known: keep returning the same stable develop id.
        if (!created)
        {
            bool stillPending = false;
            {
                std::lock_guard<std::mutex> lock(g_StateMutex);
                stillPending = (FindPendingRequest_NoLock(key) != nullptr);
            }

            if (!stillPending)
            {
                Log(
                    "[EquipDevelop] Reusing key=%s p00=%u p50=%u\n",
                    key.c_str(),
                    static_cast<unsigned>(developId),
                    static_cast<unsigned>(flowIndex)
                );

                g_Deps.PushLuaNumber(L, static_cast<float>(developId));
                return 1;
            }
        }

        // New key, or existing key still pending injection.
        QueueOrUpdatePendingRequest(key, constFields, flowFields);

        // If vanilla develop tables are already live, inject immediately.
        FlushPendingRegistrations(L);

        g_Deps.PushLuaNumber(L, static_cast<float>(developId));
        return 1;
    }

    bool TryGetFlowIndexForDevelopId(std::uint16_t developId, std::uint16_t& outFlowIndex)
    {
        std::lock_guard<std::mutex> lock(g_StateMutex);

        for (const auto& kv : g_KeyRegistry)
        {
            if (kv.second.developId == developId)
            {
                outFlowIndex = kv.second.flowIndex;
                return true;
            }
        }

        outFlowIndex = 0xFFFF;
        return false;
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