#include <pch.h>
#include "NoticeControllerImpl_CheckSpecialSaluteVoiceLabel.h"

extern "C" {
    #include "lua.h"
}

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <initializer_list>
#include <mutex>
#include <string>
#include <vector>

#include "AddressSet.h"
#include "FoxHashes.h"
#include "HookUtils.h"
#include "log.h"
#include "MissionCodeGuard.h"
#include "FeatureSwitch.h"
#include "V_FrameWorkState.h"
#include "../../lua/LuaApi.h"
#include "../equip/GoneEquipCleanup.h"
#include "../outfit/BlockControllerImpl_LoadPartsNew.h"
#include "../outfit/CustomHeadRegistry.h"
#include "../outfit/OutfitRegistry.h"

namespace
{
    using CheckSpecialSaluteVoiceLabel_t = bool(__fastcall*)(void* noticeController, std::uint32_t soldierIndex, std::uint32_t label);

    static CheckSpecialSaluteVoiceLabel_t g_OrigCheckSpecialSaluteVoiceLabel = nullptr;

    constexpr std::size_t  kPlayerPlayFlag = 0x13C;                // vars.playerPlayFlag
    constexpr std::uint8_t kWearingChickenOrChickCap = 0x1 | 0x2;  // PlayerPlayFlag.USE_CHICKEN_CAP | USE_CHICK_CAP

    constexpr std::uint32_t kSaluteWhatIsUhNeverMind = 0xD2363F30; // salute0070
    constexpr std::uint32_t kSaluteKnewYoudComeBack  = 0xD5364389; // salute0040
    constexpr std::uint32_t kSaluteLongTimeNoSee     = 0xD4364216; // salute0050
    constexpr std::uint32_t kSaluteWelcomeBack       = 0xD33640A3; // salute0060
    constexpr std::uint32_t kSaluteWelcomeHome       = 0xE13656AD; // salute0080

    constexpr int kSaluteListSlots = 32;

    constexpr std::int32_t kRankS = 1; // TppDefine.MISSION_CLEAR_RANK.S, then A=2 ... E=6
    constexpr std::int32_t kRankB = 3;
    constexpr std::int32_t kRankD = 5;
    constexpr std::int32_t kRankE = 6;

    constexpr const char* kStateSpace         = "MotherBaseSalutes";
    constexpr const char* kVisitedKey         = "visited";
    constexpr const char* kLastMissionRankKey = "lastMissionRank";

    constexpr const char* kOriginalSetBestRankField = "_V_SetBestRank";
    constexpr ULONGLONG   kRankHookCheckIntervalMs  = 1000;

    constexpr const char* kSectionConstants[] = {
        "SECTION_COMBAT", "SECTION_DEVELOP", "SECTION_BASE_DEV", "SECTION_SUPPORT",
        "SECTION_SPY", "SECTION_MEDICAL", "SECTION_SECURITY",
    };
    constexpr int kClusterCount = 7;

    struct MotherBaseTotals
    {
        std::int32_t staffAdded       = -1;
        std::int32_t clusterGrades    = -1;
        std::int32_t developedEquips  = -1;
        std::int32_t fobDefenseLosses = -1;
        std::int32_t nuclearWaste     = -1;
        std::int32_t sectionLevels    = -1;
    };

    struct EventSalute
    {
        const char* stateName;
        std::int32_t MotherBaseTotals::*total;
        const char* label;
    };

    constexpr EventSalute kEventSalutes[] = {
        { "staffAdded",       &MotherBaseTotals::staffAdded,       "salute0110" },
        { "clusterGrades",    &MotherBaseTotals::clusterGrades,    "salute0130" },
        { "developedEquips",  &MotherBaseTotals::developedEquips,  "salute0140" },
        { "fobDefenseLosses", &MotherBaseTotals::fobDefenseLosses, "salute0210" },
        { "nuclearWaste",     &MotherBaseTotals::nuclearWaste,     "salute0390" },
        { "sectionLevels",    &MotherBaseTotals::sectionLevels,    "salute0400" },
    };

    struct VisitSalute
    {
        const char* tier;
        std::string label;
    };

    static std::mutex               g_VisitMutex;
    static bool                     g_VisitSalutesReady = false;
    static std::vector<VisitSalute> g_VisitSalutes;
    static ULONGLONG                g_NextRankHookCheck = 0;
    static std::atomic<bool>        g_ModuleOn{ false };

    bool WearingChickenOrChickCap()
    {
        const std::uint8_t* vars = GoneEquipCleanup::ScriptVars();
        if (!vars)
            return false;
        __try
        {
            return (vars[kPlayerPlayFlag] & kWearingChickenOrChickCap) != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool IsWelcomeBackSalute(std::uint32_t label)
    {
        return label == kSaluteKnewYoudComeBack || label == kSaluteLongTimeNoSee
            || label == kSaluteWelcomeBack || label == kSaluteWelcomeHome;
    }

    const outfit::CustomHeadEntry* HeadOptionInVars()   // a suit without that head option leaves it off the model
    {
        const std::uint16_t worn = outfit::ReadLiveWornHeadCategory();
        if (worn > 0xFF)
            return nullptr;
        const outfit::CustomHeadEntry* head = outfit::TryGetCustomHeadBySlot(static_cast<std::uint8_t>(worn));
        return head && outfit::IsHeadOptionOfferedByLiveSuit(head->equipId) ? head : nullptr;
    }

    std::uint32_t WornHeadSaluteLabel()
    {
        const outfit::CustomHeadEntry* head = HeadOptionInVars();
        if (!head || outfit::GetRealizedLocalHeadSlot() != head->slotByte)
            return 0;
        const std::string line = outfit::GetCustomHeadSaluteVoice(head->name);
        return line.empty() ? 0 : FoxHashes::FNVHash32(line);
    }

    static bool __fastcall hk_CheckSpecialSaluteVoiceLabel(void* noticeController, std::uint32_t soldierIndex, std::uint32_t label)
    {
        const bool allowed = g_OrigCheckSpecialSaluteVoiceLabel(noticeController, soldierIndex, label);
        if (MissionCodeGuard::ShouldBypassHooks())
            return allowed;

        const std::uint32_t headSalute = WornHeadSaluteLabel();
        if (headSalute != 0 && label == headSalute)
            return allowed;
        if (label == kSaluteWhatIsUhNeverMind)
            return WearingChickenOrChickCap();
        if (IsWelcomeBackSalute(label))
            return allowed && headSalute == 0 && !WearingChickenOrChickCap();
        return allowed;
    }

    const char* MissionResultSalute(std::int32_t rank)
    {
        if (rank >= kRankS && rank < kRankB)
            return "salute0010";
        if (rank >= kRankB && rank < kRankD)
            return "salute0020";
        if (rank >= kRankD && rank <= kRankE)
            return "salute0030";
        return nullptr;
    }

    void PushTableWithText(lua_State* L, const char* key, const char* text)
    {
        g_lua_createtable(L, 0, 1);
        g_lua_pushstring(L, const_cast<char*>(key));
        g_lua_pushstring(L, const_cast<char*>(text));
        g_lua_settable(L, -3);
    }

    void PushTableWithNumber(lua_State* L, const char* key, double number)
    {
        g_lua_createtable(L, 0, 1);
        g_lua_pushstring(L, const_cast<char*>(key));
        g_lua_pushnumber(L, number);
        g_lua_settable(L, -3);
    }

    template <typename PushArguments>
    std::int32_t CallNumber(lua_State* L, const char* library, const char* function, PushArguments pushArguments)
    {
        const int top = g_lua_gettop(L);
        std::int32_t result = -1;
        g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>(library));
        if (g_lua_type(L, -1) == LUA_TTABLE)
        {
            g_lua_getfield(L, -1, const_cast<char*>(function));
            if (g_lua_type(L, -1) == LUA_TFUNCTION)
            {
                const int argumentCount = pushArguments();
                if (g_lua_pcall(L, argumentCount, 1, 0) == 0 && g_lua_type(L, -1) == LUA_TNUMBER)
                    result = static_cast<std::int32_t>(g_lua_tonumber(L, -1));
            }
        }
        g_lua_settop(L, top);
        return result;
    }

    std::int32_t ReadGlobalNumber(lua_State* L, const char* table, const char* field)
    {
        const int top = g_lua_gettop(L);
        std::int32_t result = -1;
        g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>(table));
        if (g_lua_type(L, -1) == LUA_TTABLE || g_lua_type(L, -1) == LUA_TUSERDATA)
        {
            g_lua_getfield(L, -1, const_cast<char*>(field));
            if (g_lua_type(L, -1) == LUA_TNUMBER)
                result = static_cast<std::int32_t>(g_lua_tonumber(L, -1));
        }
        g_lua_settop(L, top);
        return result;
    }

    std::int32_t AddUp(std::int32_t total, std::int32_t part)
    {
        return (total < 0 || part < 0) ? -1 : total + part;
    }

    MotherBaseTotals ReadMotherBaseTotals(lua_State* L)
    {
        MotherBaseTotals now;

        now.staffAdded = AddUp(AddUp(ReadGlobalNumber(L, "vars", "mbmPdTotalAddedSoldierStaffCount"),
                                     ReadGlobalNumber(L, "vars", "mbmPdTotalAddedHostageStaffCount")),
                               ReadGlobalNumber(L, "vars", "mbmPdTotalAddedVolunteerStaffCount"));

        now.clusterGrades = 0;
        for (int cluster = 1; cluster <= kClusterCount; ++cluster)
        {
            const std::int32_t grade = CallNumber(L, "TppLocation", "GetMbStageClusterGrade",
                [&] { g_lua_pushnumber(L, cluster); return 1; });
            now.clusterGrades = AddUp(now.clusterGrades, grade);
        }

        now.developedEquips = CallNumber(L, "TppMotherBaseManagement", "GetDevelopedEquipCount",
            [] { return 0; });

        now.fobDefenseLosses = ReadGlobalNumber(L, "vars", "mbmFobDefenseLoseCount");

        now.nuclearWaste = CallNumber(L, "TppMotherBaseManagement", "GetResourceUsableCount",
            [&] { PushTableWithText(L, "resource", "NuclearWaste"); return 1; });

        now.sectionLevels = 0;
        for (const char* sectionConstant : kSectionConstants)
        {
            const std::int32_t section = ReadGlobalNumber(L, "TppMotherBaseManagementConst", sectionConstant);
            const std::int32_t level = section < 0 ? -1 : CallNumber(L, "TppMotherBaseManagement", "GetSectionLv",
                [&] { PushTableWithNumber(L, "section", section); return 1; });
            now.sectionLevels = AddUp(now.sectionLevels, level);
        }
        return now;
    }

    std::vector<VisitSalute> DecideVisitSalutes(lua_State* L)
    {
        std::vector<VisitSalute> salutes;

        const std::int32_t lastRank = V_FrameWorkState::GetPersistedConstant(kStateSpace, kLastMissionRankKey);
        if (const char* resultSalute = MissionResultSalute(lastRank))
        {
            salutes.push_back({ "high", resultSalute });
            V_FrameWorkState::SetPersistedConstant(kStateSpace, kLastMissionRankKey, 0);
        }

        if (const outfit::CustomHeadEntry* head = HeadOptionInVars())
        {
            std::string headSalute = outfit::GetCustomHeadSaluteVoice(head->name);
            if (!headSalute.empty())
                salutes.push_back({ "high", std::move(headSalute) });
        }

        if (g_OrigCheckSpecialSaluteVoiceLabel)
            salutes.push_back({ "high", "salute0070" });

        const bool visitedBefore = V_FrameWorkState::GetPersistedConstant(kStateSpace, kVisitedKey) != 0;
        const MotherBaseTotals now = ReadMotherBaseTotals(L);
        for (const EventSalute& event : kEventSalutes)
        {
            const std::int32_t current = now.*event.total;
            if (current < 0)
                continue;
            const std::int32_t previous = V_FrameWorkState::GetPersistedConstant(kStateSpace, event.stateName);
            if (visitedBefore && current > previous)
                salutes.push_back({ "mid", event.label });
            V_FrameWorkState::SetPersistedConstant(kStateSpace, event.stateName, current);
        }
        V_FrameWorkState::SetPersistedConstant(kStateSpace, kVisitedKey, 1);
        return salutes;
    }

    std::vector<VisitSalute> SalutesForThisVisit(lua_State* L)
    {
        {
            std::lock_guard<std::mutex> lock(g_VisitMutex);
            if (g_VisitSalutesReady)
                return g_VisitSalutes;
        }
        std::vector<VisitSalute> salutes = DecideVisitSalutes(L);
        std::lock_guard<std::mutex> lock(g_VisitMutex);
        g_VisitSalutes = salutes;
        g_VisitSalutesReady = true;
        return salutes;
    }

    int CountListedSalutes(lua_State* L, int list)
    {
        int count = 0;
        for (const char* tier : { "high", "mid", "low" })
        {
            g_lua_getfield(L, list, const_cast<char*>(tier));
            if (g_lua_type(L, -1) == LUA_TTABLE)
            {
                const int tierTable = g_lua_gettop(L);
                for (const char* kind : { "normal", "once" })
                {
                    g_lua_getfield(L, tierTable, const_cast<char*>(kind));
                    if (g_lua_type(L, -1) == LUA_TTABLE)
                        count += static_cast<int>(g_lua_objlen(L, -1));
                    g_lua_settop(L, -2);
                }
            }
            g_lua_settop(L, -2);
        }
        return count;
    }

    int GetOrCreateField(lua_State* L, int table, const char* field)
    {
        g_lua_getfield(L, table, const_cast<char*>(field));
        if (g_lua_type(L, -1) == LUA_TTABLE)
            return g_lua_gettop(L);
        g_lua_settop(L, -2);
        g_lua_pushstring(L, const_cast<char*>(field));
        g_lua_createtable(L, 0, 0);
        g_lua_rawset(L, table);
        g_lua_getfield(L, table, const_cast<char*>(field));
        return g_lua_gettop(L);
    }

    void AppendOnceSalute(lua_State* L, int list, const VisitSalute& salute)
    {
        const int top = g_lua_gettop(L);
        const int tierTable = GetOrCreateField(L, list, salute.tier);
        const int onceTable = GetOrCreateField(L, tierTable, "once");
        g_lua_pushnumber(L, static_cast<lua_Number>(g_lua_objlen(L, onceTable) + 1));
        g_lua_pushstring(L, const_cast<char*>(salute.label.c_str()));
        g_lua_rawset(L, onceTable);
        g_lua_settop(L, top);
    }

    int __cdecl l_SetBestRank(lua_State* L)
    {
        const int top = g_lua_gettop(L);
        const std::int32_t rank = (top >= 2 && g_lua_type(L, 2) == LUA_TNUMBER)
            ? static_cast<std::int32_t>(g_lua_tonumber(L, 2)) : 0;

        g_lua_pushvalue(L, LUA_UPVALUEINDEX_51_1);
        for (int i = 1; i <= top; ++i)
            g_lua_pushvalue(L, i);
        if (g_lua_pcall(L, top, LUA_MULTRET, 0) != 0)
        {
            const char* err = GetLuaString(L, -1);
            LogError("TppResult.SetBestRank: %s", err ? err : "unknown error");
            g_lua_settop(L, top);
            return 0;
        }

        if (rank >= kRankS && rank <= kRankE && !MissionCodeGuard::ShouldBypassHooks())
            V_FrameWorkState::SetPersistedConstant(kStateSpace, kLastMissionRankKey, rank);
        return g_lua_gettop(L) - top;
    }

    void TouchPersistedSnapshot()
    {
        V_FrameWorkState::GetPersistedConstant(kStateSpace, kVisitedKey);
        V_FrameWorkState::GetPersistedConstant(kStateSpace, kLastMissionRankKey);
        for (const EventSalute& event : kEventSalutes)
            V_FrameWorkState::GetPersistedConstant(kStateSpace, event.stateName);
    }
}

namespace MotherBaseSalutes
{
    void AddRestoredSalutes(lua_State* L, int commandIndex)
    {
        if (!g_ModuleOn.load() || !L || !ResolveLuaApi() || MissionCodeGuard::ShouldBypassHooks()
            || !FeatureSwitch::IsOn(FeatureSwitch::Feature::StaffSaluteNews))
            return;

        const int top = g_lua_gettop(L);
        g_lua_getfield(L, commandIndex, const_cast<char*>("list"));
        if (g_lua_type(L, -1) != LUA_TTABLE)
        {
            g_lua_settop(L, top);
            return;
        }
        const int list = g_lua_gettop(L);

        const std::vector<VisitSalute> salutes = SalutesForThisVisit(L);
        int freeSlots = kSaluteListSlots - CountListedSalutes(L, list);
        for (const VisitSalute& salute : salutes)
        {
            if (freeSlots <= 0)
                break;
            AppendOnceSalute(L, list, salute);
            --freeSlots;
        }
        g_lua_settop(L, top);
    }

    void EnsureMissionRankHookInstalled(lua_State* L)
    {
        if (!g_ModuleOn.load() || !L)
            return;
        const ULONGLONG now = GetTickCount64();
        if (now < g_NextRankHookCheck)
            return;
        g_NextRankHookCheck = now + kRankHookCheckIntervalMs;
        if (!ResolveLuaApi())
            return;

        const int top = g_lua_gettop(L);
        g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("TppResult"));
        if (g_lua_type(L, -1) != LUA_TTABLE)
        {
            g_lua_settop(L, top);
            return;
        }
        const int tppResult = g_lua_gettop(L);
        g_lua_getfield(L, tppResult, const_cast<char*>(kOriginalSetBestRankField));
        const bool installed = g_lua_type(L, -1) != LUA_TNIL;
        g_lua_settop(L, tppResult);
        if (installed)
        {
            g_lua_settop(L, top);
            return;
        }

        g_lua_getfield(L, tppResult, const_cast<char*>("SetBestRank"));
        if (g_lua_type(L, -1) != LUA_TFUNCTION)
        {
            g_lua_settop(L, top);
            return;
        }
        const int original = g_lua_gettop(L);

        g_lua_pushstring(L, const_cast<char*>(kOriginalSetBestRankField));
        g_lua_pushvalue(L, original);
        g_lua_rawset(L, tppResult);

        g_lua_pushstring(L, const_cast<char*>("SetBestRank"));
        g_lua_pushvalue(L, original);
        g_lua_pushcclosure(L, &l_SetBestRank, 1);
        g_lua_rawset(L, tppResult);

        g_lua_settop(L, top);
    }

    void ClearForNewMission()
    {
        std::lock_guard<std::mutex> lock(g_VisitMutex);
        g_VisitSalutesReady = false;
        g_VisitSalutes.clear();
    }
}

bool Install_MotherBaseSalutes_Hook()
{
    g_ModuleOn.store(true);
    TouchPersistedSnapshot();

    if (!gAddr.NoticeControllerImpl_CheckSpecialSaluteVoiceLabel)
    {
        LogError("MotherBaseSalutes: CheckSpecialSaluteVoiceLabel address missing for this build, the chicken hat salute is off");
        return false;
    }

    void* target = ResolveGameAddress(gAddr.NoticeControllerImpl_CheckSpecialSaluteVoiceLabel);
    if (!target)
    {
        LogError("MotherBaseSalutes: CheckSpecialSaluteVoiceLabel unresolved, the chicken hat salute is off");
        return false;
    }

    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_CheckSpecialSaluteVoiceLabel),
                             reinterpret_cast<void**>(&g_OrigCheckSpecialSaluteVoiceLabel)))
    {
        g_OrigCheckSpecialSaluteVoiceLabel = nullptr;
        LogError("MotherBaseSalutes: CheckSpecialSaluteVoiceLabel hook failed, the chicken hat salute is off");
        return false;
    }
    return true;
}

bool Uninstall_MotherBaseSalutes_Hook()
{
    g_ModuleOn.store(false);
    if (g_OrigCheckSpecialSaluteVoiceLabel)
        DisableAndRemoveHook(ResolveGameAddress(gAddr.NoticeControllerImpl_CheckSpecialSaluteVoiceLabel));
    g_OrigCheckSpecialSaluteVoiceLabel = nullptr;
    return true;
}
