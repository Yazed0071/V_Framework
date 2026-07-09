#include "pch.h"
#include "EquipDevelop_AddToEquipDevelopTable.h"

#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "V_FrameWorkState.h"
#include "../../lua/LuaApi.h"
#include "../../core/LuaBroadcaster.h"
#include "../outfit/OutfitRegistry.h"
#include "../outfit/CustomHeadRegistry.h"
#include "../outfit/EquipDevelopControllerImpl_GetSuitDevelopInfoIndex.h"
#include "EquipDevelop_SetEquipUndeveloped.h"

namespace
{
    using RegCstDev_t = void(__fastcall*)(lua_State* L);
    using RegFlwDev_t = void(__fastcall*)(lua_State* L);

    struct FieldValue
    {
        enum class Type
        {
            Number,
            String,
            Boolean
        };

        std::string name;
        Type type = Type::Number;
        std::int32_t numberValue = 0;
        std::string stringValue;
        bool boolValue = false;
    };

    struct NamedFieldSpec
    {
        const char* canonicalName;
        const char* aliasName;
    };

    struct DevelopKeyIds
    {
        std::uint16_t developId = 0;
        std::uint16_t flowIndex = 0;
    };

    struct PendingDevelopRequest
    {
        std::string key;
        std::vector<FieldValue> constFields;
        std::vector<FieldValue> flowFields;
        bool hasDynamicGate = false;
    };

    EquipDevelopAdd::Deps g_Deps{};

    RegCstDev_t g_OrigRegCstDev = nullptr;
    RegFlwDev_t g_OrigRegFlwDev = nullptr;

    bool g_RegCstDevHookInstalled = false;
    bool g_RegFlwDevHookInstalled = false;

    struct DevelopNameLangId
    {
        bool hasHash = false;
        std::uint32_t hash = 0;
        std::string str;
    };

    std::recursive_mutex g_StateMutex;
    std::unordered_map<std::string, DevelopKeyIds> g_KeyRegistry;
    std::unordered_map<std::int32_t, DevelopNameLangId> g_DevelopNameByDevelopId;
    std::unordered_map<std::uint32_t, int> g_GradeByDevelopId;
    std::unordered_map<std::uint32_t, int> g_SideByDevelopId;
    std::unordered_map<std::uint32_t, std::uint32_t> g_ManagedBaseByDevelopId;

    constexpr int         kLuaRegistryIndex = -10000;
    constexpr const char* kGateTableKey     = "V_FrameWork_DevGates";
    struct DynamicGate { std::uint16_t flowIndex; std::string key; };
    std::vector<DynamicGate> g_DynamicGates;
    std::atomic<bool>        g_AnyDynamicGate{ false };
    volatile DWORD           g_LastGateRefreshTick = 0;
    constexpr DWORD          kGateRefreshThrottleMs = 300;

    static void EnsureGateTable(lua_State* L)
    {
        g_lua_getfield(L, kLuaRegistryIndex, const_cast<char*>(kGateTableKey));
        const bool exists = g_lua_type(L, -1) == LUA_TTABLE;
        g_lua_settop(L, g_lua_gettop(L) - 1);
        if (exists)
            return;
        g_lua_pushstring(L, const_cast<char*>(kGateTableKey));
        g_lua_createtable(L, 0, 0);
        g_lua_rawset(L, kLuaRegistryIndex);
    }

    static bool StoreGatePredicate(lua_State* L, int constTableIdx,
                                   const std::string& key)
    {
        if (!L || !ResolveLuaApi())
            return false;

        const int top = g_lua_gettop(L);
        bool isFunc = false;
        EnsureGateTable(L);

        const char* names[2] = { "bluePrintId", "p05" };
        for (const char* nm : names)
        {
            g_lua_pushstring(L, const_cast<char*>(nm));
            g_lua_gettable(L, constTableIdx);
            if (g_lua_type(L, -1) == LUA_TFUNCTION)
            {
                g_lua_getfield(L, kLuaRegistryIndex,
                               const_cast<char*>(kGateTableKey));
                g_lua_pushstring(L, const_cast<char*>(key.c_str()));
                g_lua_pushvalue(L, -3);
                g_lua_rawset(L, -3);
                isFunc = true;
                break;
            }
            g_lua_settop(L, g_lua_gettop(L) - 1);
        }

        g_lua_settop(L, top);
        return isFunc;
    }

    static void RegisterDynamicGate(std::uint16_t flowIndex,
                                    const std::string& key)
    {
        std::lock_guard<std::recursive_mutex> lock(g_StateMutex);
        for (DynamicGate& gt : g_DynamicGates)
        {
            if (gt.key == key)
            {
                gt.flowIndex = flowIndex;
                return;
            }
        }
        g_DynamicGates.push_back({ flowIndex, key });
        g_AnyDynamicGate.store(true, std::memory_order_relaxed);
    }

    // Returns 1 if the row flipped hidden->visible this refresh, else 0.
    static int EvalOneGateSEH(lua_State* L, std::uint16_t flowIndex,
                              const char* key)
    {
        int flippedVisible = 0;
        __try
        {
            const int top = g_lua_gettop(L);
            g_lua_getfield(L, kLuaRegistryIndex,
                           const_cast<char*>(kGateTableKey));
            if (g_lua_type(L, -1) != LUA_TTABLE)
            {
                g_lua_settop(L, top);
                return 0;
            }
            g_lua_getfield(L, -1, const_cast<char*>(key));
            if (g_lua_type(L, -1) != LUA_TFUNCTION)
            {
                g_lua_settop(L, top);
                return 0;
            }
            const int err = g_lua_pcall(L, 0, 1, 0);
            if (err == 0)
            {
                const bool visible   = g_lua_toboolean(L, -1) != 0;
                const bool wasHidden = outfit::IsDevelopHidden(flowIndex);
                outfit::SetDevelopHidden(flowIndex, !visible);
                if (wasHidden != !visible)   // state flipped this refresh
                {
                    Log("[EquipDevelop] dynamic gate key=%s flowIndex=%u -> %s\n",
                        key, static_cast<unsigned>(flowIndex),
                        visible ? "VISIBLE" : "HIDDEN");
                    if (visible)
                        flippedVisible = 1;
                }
            }
            g_lua_settop(L, top);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
        return flippedVisible;
    }

    static void RefreshDynamicGatesImpl()
    {
        std::vector<DynamicGate> gates;
        {
            std::lock_guard<std::recursive_mutex> lock(g_StateMutex);
            if (g_DynamicGates.empty())
                return;
            gates = g_DynamicGates;
        }
        if (!ResolveLuaApi())
            return;
        lua_State* L = V_FrameWork_AnyLuaState();
        if (!L)
            return;

        bool anyRevealed = false;
        for (const DynamicGate& gt : gates)
            if (EvalOneGateSEH(L, gt.flowIndex, gt.key.c_str()) == 1)
                anyRevealed = true;

        if (anyRevealed)
            EquipDevelop_TriggerRequirementsMetAnnounce();
    }

    struct NativeFlowInfo
    {
        std::uint8_t grade     = 0;
        std::uint8_t sideGrade = 0;
        bool         seen      = false;
    };
    constexpr std::size_t kNativeFlowCap = 1024;
    NativeFlowInfo g_NativeFlow[kNativeFlowCap] = {};
    std::unordered_map<std::uint32_t, std::uint32_t> g_NativeBaseByDevelopId;

    using GetDevIndex_t = std::uint16_t (__fastcall*)(void*, std::uint32_t);

    static std::uint16_t SafeGetDevIndexLocal(GetDevIndex_t fn, void* ctrl,
                                              std::uint32_t developId)
    {
        __try { return fn(ctrl, developId); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0x400; }
    }

    static bool TryGetNativeFlowInfo(std::uint32_t developId,
                                     int* outGrade, int* outSideGrade)
    {
        auto getIndex = reinterpret_cast<GetDevIndex_t>(
            ResolveGameAddress(gAddr.EquipDevelopCtrl_GetEquipDevelopIndex));
        void* ctrl = EquipDevelop_ResolveDevelopController();
        if (!getIndex || !ctrl)
            return false;
        const std::uint16_t idx = SafeGetDevIndexLocal(getIndex, ctrl, developId);
        if (idx >= kNativeFlowCap)
            return false;
        std::lock_guard<std::recursive_mutex> lock(g_StateMutex);
        if (!g_NativeFlow[idx].seen)
            return false;
        if (outGrade)     *outGrade     = g_NativeFlow[idx].grade;
        if (outSideGrade) *outSideGrade = g_NativeFlow[idx].sideGrade;
        return true;
    }

    struct FamilyInfo
    {
        std::uint32_t mainGradeMask = 0;
        std::uint32_t allGradeMask  = 0;
        std::uint32_t sideMask      = 0;
        int           parentGrade   = -1;
        int           maxAnyGrade   = 0;
    };

    static FamilyInfo CollectFamilyInfo(std::uint32_t parentDevelopId)
    {
        FamilyInfo out{};

        std::vector<std::pair<std::uint32_t, std::uint32_t>> edges;
        std::unordered_map<std::uint32_t, std::pair<int, int>> managedInfo;
        {
            std::lock_guard<std::recursive_mutex> lock(g_StateMutex);
            edges.reserve(g_NativeBaseByDevelopId.size()
                          + g_ManagedBaseByDevelopId.size());
            for (const auto& kv : g_NativeBaseByDevelopId)
                if (kv.second != 0)
                    edges.push_back({ kv.first, kv.second });
            for (const auto& kv : g_ManagedBaseByDevelopId)
                if (kv.second != 0)
                    edges.push_back({ kv.first, kv.second });
            for (const auto& kv : g_GradeByDevelopId)
            {
                int side = 0;
                const auto itS = g_SideByDevelopId.find(kv.first);
                if (itS != g_SideByDevelopId.end())
                    side = itS->second;
                managedInfo[kv.first] = { kv.second, side };
            }
        }

        std::uint32_t root = parentDevelopId;
        for (int hop = 0; hop < 64; ++hop)
        {
            std::uint32_t base = 0;
            for (const auto& e : edges)
                if (e.first == root) { base = e.second; break; }
            if (base == 0 || base == root)
                break;
            root = base;
        }

        std::vector<std::uint32_t> family{ root };
        std::unordered_set<std::uint32_t> seen{ root };
        for (std::size_t i = 0; i < family.size(); ++i)
            for (const auto& e : edges)
                if (e.second == family[i] && seen.insert(e.first).second)
                    family.push_back(e.first);

        for (std::uint32_t id : family)
        {
            int gr = -1, sg = 0;
            const auto itM = managedInfo.find(id);
            if (itM != managedInfo.end())
            {
                gr = itM->second.first;
                sg = itM->second.second;
            }
            else if (!TryGetNativeFlowInfo(id, &gr, &sg))
            {
                continue;
            }

            if (gr < 0)
                continue;
            if (gr >= 1 && gr <= 15)
            {
                out.allGradeMask |= (1u << gr);
                if (gr > out.maxAnyGrade)
                    out.maxAnyGrade = gr;
                if (sg == 0)
                    out.mainGradeMask |= (1u << gr);
            }
            if (sg >= 1 && sg <= 15)
                out.sideMask |= (1u << sg);
            if (id == parentDevelopId)
                out.parentGrade = gr;
        }

        return out;
    }
    std::vector<PendingDevelopRequest> g_PendingRequests;
    bool g_IsFlushingPending = false;

    std::uint32_t g_NextDevelopId = 0;
    std::uint32_t g_NextFlowIndex = 0;
    bool g_ObservedAnyDevelopId = false;
    bool g_ObservedAnyFlowIndex = false;


    std::unordered_set<std::uint32_t> g_ObservedStockDevelopIds;
    std::unordered_set<std::uint32_t> g_ObservedStockFlowIndices;

    constexpr int LUA_TNUMBER_CONST = 3;
    constexpr int LUA_TSTRING_CONST = 4;
    constexpr int LUA_TTABLE_CONST = 5;


    constexpr std::uint32_t kBootstrapDevelopIdStart = 51006;
    constexpr std::uint32_t kBootstrapFlowIndexStart = 922;
    constexpr std::uint32_t kMaxAllocId = 0xFFFEu;

    constexpr int kMinGrade = 1;
    constexpr int kMaxGrade = 15;

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

    constexpr int LUA_TBOOLEAN_CONST = 1;

    static bool IsLuaBoolean(lua_State* L, int idx)
    {
        return g_Deps.LuaType(L, idx) == LUA_TBOOLEAN_CONST;
    }

    static bool TryReadBoolFieldByName(
        lua_State* L,
        int tableIndex,
        const char* fieldName,
        bool& outValue)
    {
        outValue = false;

        if (!g_Deps.LuaToBool)
            return false;

        const int absIndex = AbsIndex(L, tableIndex);

        PushFieldKey(L, fieldName);
        g_Deps.LuaGetTable(L, absIndex);

        const bool ok = IsLuaBoolean(L, -1);
        if (ok)
            outValue = g_Deps.LuaToBool(L, -1);

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
        std::lock_guard<std::recursive_mutex> lock(g_StateMutex);
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

        std::lock_guard<std::recursive_mutex> lock(g_StateMutex);

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


        std::int32_t persistedFlowIndex = 0;
        if (V_FrameWorkState::ResolveOrCreateFlowIndex(
                key.c_str(),
                static_cast<std::int32_t>(g_NextFlowIndex),
                persistedFlowIndex) &&
            persistedFlowIndex > 0)
        {
            ids.flowIndex = static_cast<std::uint16_t>(persistedFlowIndex);
            if (static_cast<std::uint32_t>(persistedFlowIndex) >= g_NextFlowIndex)
                g_NextFlowIndex = static_cast<std::uint32_t>(persistedFlowIndex) + 1;
        }
        else
        {
            ids.flowIndex = static_cast<std::uint16_t>(g_NextFlowIndex++);
        }

        g_KeyRegistry.emplace(key, ids);

        outDevelopId = ids.developId;
        outFlowIndex = ids.flowIndex;
        outCreated = true;

        return true;
    }

    static void QueueOrUpdatePendingRequest(
        const std::string& key,
        const std::vector<FieldValue>& constFields,
        const std::vector<FieldValue>& flowFields,
        bool hasDynamicGate)
    {
        std::lock_guard<std::recursive_mutex> lock(g_StateMutex);

        if (key.empty())
            return;

        if (PendingDevelopRequest* existing = FindPendingRequest_NoLock(key))
        {
            existing->constFields = constFields;
            existing->flowFields = flowFields;
            existing->hasDynamicGate = hasDynamicGate;
            return;
        }

        PendingDevelopRequest req{};
        req.key = key;
        req.constFields = constFields;
        req.flowFields = flowFields;
        req.hasDynamicGate = hasDynamicGate;
        g_PendingRequests.push_back(std::move(req));
    }

    static void ObserveDevelopId(std::uint32_t developId)
    {
        std::lock_guard<std::recursive_mutex> lock(g_StateMutex);

        g_ObservedAnyDevelopId = true;
        if (developId >= g_NextDevelopId)
            g_NextDevelopId = developId + 1;


        g_ObservedStockDevelopIds.insert(developId);
    }

    static void ObserveFlowIndex(std::uint32_t flowIndex)
    {
        std::lock_guard<std::recursive_mutex> lock(g_StateMutex);

        g_ObservedAnyFlowIndex = true;
        if (flowIndex >= g_NextFlowIndex)
            g_NextFlowIndex = flowIndex + 1;


        g_ObservedStockFlowIndices.insert(flowIndex);
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

            bool boolValue = false;
            if (TryReadBoolFieldByName(L, tableIndex, spec.canonicalName, boolValue) ||
                (spec.aliasName && TryReadBoolFieldByName(L, tableIndex, spec.aliasName, boolValue)))
            {
                FieldValue value{};
                value.name = spec.canonicalName;
                value.type = FieldValue::Type::Boolean;
                value.boolValue = boolValue;
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
        std::vector<FieldValue>& outFlowFields,
        bool& outHasDynamicGate)
    {
        outKey.clear();
        outConstFields.clear();
        outFlowFields.clear();
        outHasDynamicGate = false;

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

        const int constTableIdx = g_Deps.GetLuaTop(L);
        CollectKnownFields(
            L,
            constTableIdx,
            k_ConstFieldSpecs,
            sizeof(k_ConstFieldSpecs) / sizeof(k_ConstFieldSpecs[0]),
            outConstFields);

        outHasDynamicGate = StoreGatePredicate(L, constTableIdx, outKey);

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
            else if (field.type == FieldValue::Type::String)
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
            else if (field.type == FieldValue::Type::String)
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

        for (const FieldValue& f : req.constFields)
        {
            if (f.name != "p06")
                continue;
            DevelopNameLangId nm{};
            if (f.type == FieldValue::Type::Number)
            {
                nm.hasHash = true;
                nm.hash = static_cast<std::uint32_t>(f.numberValue);
            }
            else
            {
                nm.str = f.stringValue;
            }
            std::lock_guard<std::recursive_mutex> lock(g_StateMutex);
            g_DevelopNameByDevelopId[static_cast<std::int32_t>(developId)] = std::move(nm);
            break;
        }

        std::uint32_t baseDevelopId = 0;
        {
            for (const FieldValue& f : req.constFields)
                if (f.type == FieldValue::Type::Number && f.name == "p03")
                    baseDevelopId = static_cast<std::uint32_t>(f.numberValue);
            EquipDevelop_SetDevelopParent(developId, baseDevelopId);
        }

        bool defaultDeveloped = false;
        for (const FieldValue& f : req.flowFields)
            if (f.type == FieldValue::Type::Number && f.name == "p62")
                defaultDeveloped = (f.numberValue != 0);

        const bool developed =
            V_FrameWorkState::ResolveDevelopedFlag(req.key.c_str(), defaultDeveloped);

        g_Deps.LuaSetTop(L, 0);
        BuildFlowRowTable(L, flowIndex, req.flowFields);

        const int flowRowIndex = g_Deps.GetLuaTop(L);
        SetIntField(L, flowRowIndex, "p62", 0);
        SetIntField(L, flowRowIndex, "p72", 0);
        SetIntField(L, flowRowIndex, "p74", 0);

        int grade = kMinGrade;
        int sideGrade = 0;
        for (const FieldValue& f : req.flowFields)
        {
            if (f.type != FieldValue::Type::Number)
                continue;
            if (f.name == "p52")
                grade = f.numberValue;
            else if (f.name == "p51")
                sideGrade = f.numberValue;
        }
        if (grade < kMinGrade)
            grade = kMinGrade;
        else if (grade > kMaxGrade)
            grade = kMaxGrade;

        if (baseDevelopId != 0)
        {
            const FamilyInfo fam = CollectFamilyInfo(baseDevelopId);
            bool parentManaged = false;
            {
                std::lock_guard<std::recursive_mutex> lock(g_StateMutex);
                parentManaged = g_GradeByDevelopId.find(baseDevelopId)
                    != g_GradeByDevelopId.end();
            }

            if (sideGrade == 0)
            {
                if (parentManaged)
                {
                    int target = grade;
                    if (fam.parentGrade >= 0 && target <= fam.parentGrade)
                        target = fam.parentGrade + 1;
                    while (target <= kMaxGrade
                           && (fam.mainGradeMask & (1u << target)) != 0)
                        ++target;

                    if (target > kMaxGrade)
                    {
                        target = kMaxGrade;
                        int side = 1;
                        while (side <= kMaxGrade
                               && (fam.sideMask & (1u << side)) != 0)
                            ++side;
                        if (side <= kMaxGrade)
                        {
                            Log("[EquipDevelop] WARNING key=%s: no free "
                                "mainline grade left in the develop family of "
                                "baseEquipDevelopId=%u (parent grade %d) - "
                                "given grade %d as a side branch (sideGrade "
                                "%d).\n",
                                req.key.c_str(), baseDevelopId,
                                fam.parentGrade, target, side);
                            sideGrade = side;
                            SetIntField(L, flowRowIndex, "p51", side);
                        }
                        else
                        {
                            Log("[EquipDevelop] WARNING key=%s: no free "
                                "mainline grade or sideGrade slot left in the "
                                "develop family of baseEquipDevelopId=%u - "
                                "kept grade %d; the row will collide on the "
                                "grade strip.\n",
                                req.key.c_str(), baseDevelopId, kMaxGrade);
                        }
                    }
                    else if (target != grade)
                    {
                        Log("[EquipDevelop] WARNING key=%s: grade %d collides "
                            "in the develop family of baseEquipDevelopId=%u "
                            "(parent grade %d) - auto-bumped to %d. Chained "
                            "grades must ascend and be unique; set an explicit "
                            "free grade to silence this.\n",
                            req.key.c_str(), grade, baseDevelopId,
                            fam.parentGrade, target);
                    }

                    grade = target;
                }
                else if (grade > fam.maxAnyGrade && grade > fam.parentGrade)
                {
                }
                else
                {
                    int side = 1;
                    while (side <= kMaxGrade
                           && (fam.sideMask & (1u << side)) != 0)
                        ++side;

                    if (side > kMaxGrade)
                    {
                        Log("[EquipDevelop] WARNING key=%s: grade %d under "
                            "vanilla baseEquipDevelopId=%u needs a side branch "
                            "but no sideGrade slot is free - kept mainline at "
                            "grade %d; expect visual overlap.\n",
                            req.key.c_str(), grade, baseDevelopId, grade);
                    }
                    else
                    {
                        Log("[EquipDevelop] WARNING key=%s: grade %d under "
                            "vanilla baseEquipDevelopId=%u does not extend the "
                            "family's top grade (%d) - kept grade %d but "
                            "forced onto a side branch (sideGrade %d). Mod "
                            "children of vanilla items become side branches "
                            "unless their grade is above the family's highest; "
                            "set an explicit sideGrade (or a grade above %d) "
                            "to silence this.\n",
                            req.key.c_str(), grade, baseDevelopId,
                            fam.maxAnyGrade, grade, side, fam.maxAnyGrade);
                        sideGrade = side;
                        SetIntField(L, flowRowIndex, "p51", side);
                    }
                }
            }
            else
            {
                int side = sideGrade;
                if (side < 1)
                    side = 1;
                else if (side > kMaxGrade)
                    side = kMaxGrade;

                int target = side;
                while (target <= kMaxGrade
                       && (fam.sideMask & (1u << target)) != 0)
                    ++target;

                if (target > kMaxGrade)
                {
                    Log("[EquipDevelop] WARNING key=%s: sideGrade %d already "
                        "taken in the develop family of baseEquipDevelopId=%u "
                        "and no free slot left - kept %d; the side branches "
                        "will collide.\n",
                        req.key.c_str(), sideGrade, baseDevelopId, side);
                    target = side;
                }
                else if (target != sideGrade)
                {
                    Log("[EquipDevelop] WARNING key=%s: sideGrade %d already "
                        "taken in the develop family of baseEquipDevelopId=%u "
                        "(vanilla or another mod) - auto-bumped to %d. Side "
                        "branches must have unique sideGrades per family; set "
                        "an explicit free sideGrade to silence this.\n",
                        req.key.c_str(), sideGrade, baseDevelopId, target);
                }

                side = target;
                if (side != sideGrade)
                    SetIntField(L, flowRowIndex, "p51", side);
                sideGrade = side;

                if (fam.parentGrade >= 0 && grade <= fam.parentGrade)
                {
                    const int bumped = (fam.parentGrade + 1 > kMaxGrade)
                        ? kMaxGrade
                        : fam.parentGrade + 1;
                    Log("[EquipDevelop] WARNING key=%s: side-branch grade %d "
                        "is not above its parent's grade %d "
                        "(baseEquipDevelopId=%u) - auto-bumped to %d. Side "
                        "branches must sit above their parent's grade.\n",
                        req.key.c_str(), grade, fam.parentGrade, baseDevelopId,
                        bumped);
                    grade = bumped;
                }
            }
        }

        SetIntField(L, flowRowIndex, "p52", grade);

        {
            std::lock_guard<std::recursive_mutex> lock(g_StateMutex);
            g_GradeByDevelopId[developId]       = grade;
            g_SideByDevelopId[developId]        = sideGrade;
            g_ManagedBaseByDevelopId[developId] = baseDevelopId;
        }

        g_OrigRegFlwDev(L);
        ObserveFlowIndex(flowIndex);

        if (developed)
            outfit::MarkDeveloped(flowIndex);

        if (req.hasDynamicGate)
        {
            RegisterDynamicGate(flowIndex, req.key);
            outfit::SetDevelopHidden(flowIndex, true);
            RefreshDynamicGatesImpl();
#ifdef _DEBUG
            Log("[EquipDevelop] bluePrintId dynamic gate key=%s flowIndex=%u "
                "(predicate re-evaluated each R&D menu build)\n",
                req.key.c_str(), flowIndex);
#endif
        }
        else
        {
            for (const FieldValue& f : req.constFields)
            {
                if (f.type == FieldValue::Type::Boolean && f.name == "p05")
                {
                    outfit::SetDevelopHidden(flowIndex, !f.boolValue);
#ifdef _DEBUG
                    Log("[EquipDevelop] bluePrintId gate key=%s flowIndex=%u -> %s\n",
                        req.key.c_str(), flowIndex,
                        f.boolValue ? "VISIBLE" : "HIDDEN");
#endif
                    break;
                }
            }
        }

        g_Deps.LuaSetTop(L, 0);

        if (outDevelopId)
            *outDevelopId = developId;
        if (outFlowIndex)
            *outFlowIndex = flowIndex;

        return true;
    }

    static void FlushPendingRegistrations(lua_State* L)
    {
        if (!L || !EnsureLuaReady())
            return;

        std::vector<PendingDevelopRequest> work;
        {
            std::lock_guard<std::recursive_mutex> lock(g_StateMutex);

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
            std::lock_guard<std::recursive_mutex> lock(g_StateMutex);

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
            {
                ObserveDevelopId(static_cast<std::uint32_t>(developId));

                std::int32_t base = 0;
                TryReadIntFieldByName(L, 1, "p03", base);
                std::lock_guard<std::recursive_mutex> lock(g_StateMutex);
                g_NativeBaseByDevelopId[static_cast<std::uint32_t>(developId)] =
                    (base > 0) ? static_cast<std::uint32_t>(base) : 0u;
            }
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
            {
                ObserveFlowIndex(static_cast<std::uint32_t>(flowIndex));

                if (flowIndex < static_cast<std::int32_t>(kNativeFlowCap))
                {
                    std::int32_t sg = 0, gr = 0;
                    TryReadIntFieldByName(L, 1, "p51", sg);
                    TryReadIntFieldByName(L, 1, "p52", gr);
                    std::lock_guard<std::recursive_mutex> lock(g_StateMutex);
                    NativeFlowInfo& nf = g_NativeFlow[flowIndex];
                    nf.grade     = static_cast<std::uint8_t>(
                        (gr < 0) ? 0 : (gr > 15 ? 15 : gr));
                    nf.sideGrade = static_cast<std::uint8_t>(
                        (sg < 0) ? 0 : (sg > 15 ? 15 : sg));
                    nf.seen      = true;
                }

                if (EquipDevelopAdd::IsManagedFlowIndex(
                        static_cast<std::uint16_t>(flowIndex)))
                {
                    SetIntField(L, 1, "p74", 0);
#ifdef _DEBUG
                    static int s_fobLog = 0;
                    if (s_fobLog < 8)
                    {
                        ++s_fobLog;
                        Log("[EquipDevelop] hkRegFlwDev: forced isFobAvailable=0 "
                            "(p74) on managed flow row flowIndex=%d\n", flowIndex);
                    }
#endif
                }
            }
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
        bool hasDynamicGate = false;

        if (!ReadKeyAndPayload(L, key, constFields, flowFields, hasDynamicGate))
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


        if (!created)
        {
            bool stillPending = false;
            {
                std::lock_guard<std::recursive_mutex> lock(g_StateMutex);
                stillPending = (FindPendingRequest_NoLock(key) != nullptr);
            }

            if (!stillPending)
            {
                g_Deps.PushLuaNumber(L, static_cast<float>(developId));
                return 1;
            }
        }


        QueueOrUpdatePendingRequest(key, constFields, flowFields, hasDynamicGate);


        FlushPendingRegistrations(L);

        g_Deps.PushLuaNumber(L, static_cast<float>(developId));
        return 1;
    }

    bool TryGetFlowIndexForDevelopId(std::uint16_t developId, std::uint16_t& outFlowIndex)
    {
        std::lock_guard<std::recursive_mutex> lock(g_StateMutex);

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

    bool IsManagedFlowIndex(std::uint16_t flowIndex)
    {
        std::lock_guard<std::recursive_mutex> lock(g_StateMutex);

        for (const auto& kv : g_KeyRegistry)
            if (kv.second.flowIndex == flowIndex)
                return true;

        return false;
    }

    void MaybeRefreshDynamicGates()
    {
        if (!g_AnyDynamicGate.load(std::memory_order_relaxed))
            return;
        const DWORD now = GetTickCount();
        if (now - g_LastGateRefreshTick < kGateRefreshThrottleMs)
            return;
        g_LastGateRefreshTick = now;
        RefreshDynamicGatesImpl();
    }

    bool GetDevelopNameLangId(std::int32_t developId,
                              bool& outHasHash,
                              std::uint32_t& outHash,
                              std::string& outStr)
    {
        std::lock_guard<std::recursive_mutex> lock(g_StateMutex);

        auto it = g_DevelopNameByDevelopId.find(developId);
        if (it == g_DevelopNameByDevelopId.end())
            return false;

        outHasHash = it->second.hasHash;
        outHash = it->second.hash;
        outStr = it->second.str;
        return true;
    }

    bool IsDevelopIdReservedByStock(std::uint32_t developId)
    {
        std::lock_guard<std::recursive_mutex> lock(g_StateMutex);
        return g_ObservedStockDevelopIds.find(developId)
            != g_ObservedStockDevelopIds.end();
    }

    bool IsFlowIndexReservedByStock(std::uint32_t flowIndex)
    {
        std::lock_guard<std::recursive_mutex> lock(g_StateMutex);
        return g_ObservedStockFlowIndices.find(flowIndex)
            != g_ObservedStockFlowIndices.end();
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