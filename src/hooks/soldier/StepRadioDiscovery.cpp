
#include "pch.h"

#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <mutex>
#include <array>

#include "HookUtils.h"
#include "log.h"
#include "MissionCodeGuard.h"
#include "NoticeControllerImpl_CheckSightNoticePlayer.h"
#include "AddressSet.h"

namespace
{
    using StepRadioDiscovery_t = void(__fastcall*)(void* self, std::uint32_t soldierIndex, int step, void* info);

    static constexpr std::uint16_t LHD_INVALID_TARGET_ID = 0xFFFFu;
    static constexpr std::uint32_t LHD_INVALID_MEMBER = 0xFFFFFFFFu;

    static constexpr std::uint8_t LHD_RADIO_TYPE_HOSTAGE_FOUND_A = 0x12u;
    static constexpr std::uint8_t LHD_RADIO_TYPE_HOSTAGE_FOUND_B = 0x0Bu;
    static constexpr std::uint8_t LHD_RADIO_TYPE_HOSTAGE_FOUND_C = 0x25u;
    static constexpr std::uint8_t LHD_RADIO_TYPE_HOSTAGE_FOUND_D = 0x0Cu;

    static constexpr std::uint32_t LHD_LABEL_MALE = 0x247b3defu;     // CPR0281
    static constexpr std::uint32_t LHD_LABEL_FEMALE = 0xa2c4a6ecu;   // CPR0282
    static constexpr std::uint32_t LHD_LABEL_CHILD = 0x100eb512u;    // CPR0283
    static constexpr std::uint32_t LHD_LABEL_DEAD = 0xc944e305u;     // V_CPR0231 "Found the body of a prisoner." (V_CpRadioSeqCommonGz2)

    static constexpr std::int32_t  LHD_STEP_ENTER = 0;
    static constexpr std::int32_t  LHD_STEP_EXIT = 1;
    static constexpr std::int32_t  LHD_STEP_REENTER = 4;
    static constexpr std::size_t   LHD_KNOWLEDGE_TARGET = 0x26;      // NoticeHostageAiKnowledge: the hostage's gameObjectId
    static constexpr std::size_t   LHD_KNOWLEDGE_STATUS = 0x32;      // CheckHostageStatus: 4 normal, 8 dead, 0x10 flag 0x10000
    static constexpr std::uint8_t  LHD_STATUS_DEAD = 0x08u;
    static constexpr std::size_t   LHD_AI_SOLDIERS = 0x70;           // NoticeHostageAiImpl: shared soldier context
    static constexpr std::size_t   LHD_SOLDIERS_COMMAND_POST = 0xD0;
    static constexpr std::size_t   LHD_COMMAND_POST_CONTROLLER = 0x08;   // the CP controller StepRadioDiscovery requests the call from
    static constexpr std::size_t   LHD_COMMAND_POST_CLIENT = 0x30;
    static constexpr std::size_t   LHD_CONTROLLER_CLIENTS = 0x1F0;   // soldiers, helis... each with its own run of members
    static constexpr std::size_t   LHD_CLIENT_SIZE = 0x48;
    static constexpr std::size_t   LHD_CLIENT_FIRST_MEMBER = 0x40;
    static constexpr std::uint64_t LHD_REPORT_FRESH_MS = 60000;      // a busy CP can keep his call queued a while

    static StepRadioDiscovery_t      g_LHD_OrigStepRadioDiscovery = nullptr;

    struct LostHostageDiscoveryInfo
    {
        std::uint16_t rawGameObjectId = LHD_INVALID_TARGET_ID;
        int           hostageType = -1;
        std::uint32_t speechLabel = 0u;
    };

    struct LostHostageDiscoveryReport   // the call StepRadioDiscovery just requested for this CP member, and the label it gets
    {
        std::uint32_t soldierIndex = 0xFFFFFFFFu;
        std::uint16_t targetId = LHD_INVALID_TARGET_ID;
        bool          dead = false;
        std::uint32_t label = 0u;
        std::uint64_t startedMs = 0;
    };

    static std::unordered_map<std::uint16_t, LostHostageDiscoveryInfo> g_LHD_TrackedHostagesByRawId;
    static std::unordered_map<std::uint32_t, LostHostageDiscoveryReport> g_LHD_ReportByMember;
    static std::mutex g_LHD_Mutex;
}

static bool LostHostageDiscovery_SafeReadU16(std::uintptr_t addr, std::uint16_t& outValue)
{
    if (!addr) return false;
    __try
    {
        outValue = *reinterpret_cast<const std::uint16_t*>(addr);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static bool LostHostageDiscovery_SafeReadU8(std::uintptr_t addr, std::uint8_t& outValue)
{
    if (!addr) return false;
    __try
    {
        outValue = *reinterpret_cast<const std::uint8_t*>(addr);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static std::uint32_t LostHostageDiscovery_MemberOf(void* self, std::uint32_t soldierIndex)   // as RequestRadioByMember(client, soldier) numbers him
{
    __try
    {
        const std::uint8_t* soldiers = *reinterpret_cast<std::uint8_t* const*>(static_cast<std::uint8_t*>(self) + LHD_AI_SOLDIERS);
        const std::uint8_t* commandPost = *reinterpret_cast<std::uint8_t* const*>(soldiers + LHD_SOLDIERS_COMMAND_POST);
        const std::uint8_t* controller = *reinterpret_cast<std::uint8_t* const*>(commandPost + LHD_COMMAND_POST_CONTROLLER);
        const std::uint32_t client = *reinterpret_cast<const std::uint32_t*>(commandPost + LHD_COMMAND_POST_CLIENT);
        const std::uint8_t* clients = *reinterpret_cast<std::uint8_t* const*>(controller + LHD_CONTROLLER_CLIENTS);
        return *reinterpret_cast<const std::uint16_t*>(clients + client * LHD_CLIENT_SIZE + LHD_CLIENT_FIRST_MEMBER) + soldierIndex;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return LHD_INVALID_MEMBER;
    }
}

static std::uint32_t LostHostageDiscovery_GetLabelForHostageType(int hostageType)
{
    switch (hostageType)
    {
    case 0:  return LHD_LABEL_MALE;
    case 1:  return LHD_LABEL_FEMALE;
    case 2:  return LHD_LABEL_CHILD;
    default: return 0u;
    }
}

static bool LostHostageDiscovery_IsHostageDiscoveryRadioType(std::uint8_t radioType)
{
    return radioType == LHD_RADIO_TYPE_HOSTAGE_FOUND_A ||
        radioType == LHD_RADIO_TYPE_HOSTAGE_FOUND_B ||
        radioType == LHD_RADIO_TYPE_HOSTAGE_FOUND_C ||
        radioType == LHD_RADIO_TYPE_HOSTAGE_FOUND_D;
}

static void LostHostageDiscovery_ResetRuntimeState_NoLock()
{
    g_LHD_ReportByMember.clear();
}

static void LostHostageDiscovery_NoteReport(void* self, std::uint32_t soldierIndex, void* info)
{
    LostHostageDiscoveryReport report{};
    std::uint8_t status = 0u;
    LostHostageDiscovery_SafeReadU16(reinterpret_cast<std::uintptr_t>(info) + LHD_KNOWLEDGE_TARGET, report.targetId);
    LostHostageDiscovery_SafeReadU8(reinterpret_cast<std::uintptr_t>(info) + LHD_KNOWLEDGE_STATUS, status);
    if (report.targetId == LHD_INVALID_TARGET_ID)
        return;

    const std::uint32_t member = LostHostageDiscovery_MemberOf(self, soldierIndex);
    if (member == LHD_INVALID_MEMBER)
        return;

    report.soldierIndex = soldierIndex;
    report.dead = (status & LHD_STATUS_DEAD) != 0u;   // the step itself picks the caution report (0x0B) for a body
    report.startedMs = GetTickCount64();

    std::lock_guard<std::mutex> lock(g_LHD_Mutex);
    if (report.dead)
        report.label = LHD_LABEL_DEAD;
    else
    {
        const auto tracked = g_LHD_TrackedHostagesByRawId.find(report.targetId);
        report.label = tracked != g_LHD_TrackedHostagesByRawId.end() ? tracked->second.speechLabel : 0u;
    }
    if (report.label != 0u)
        g_LHD_ReportByMember[member] = report;
    else
        g_LHD_ReportByMember.erase(member);
}

static void LostHostageDiscovery_ForgetReport(void* self, std::uint32_t soldierIndex)   // the step ended: his call aired or was dropped
{
    const std::uint32_t member = LostHostageDiscovery_MemberOf(self, soldierIndex);
    if (member == LHD_INVALID_MEMBER)
        return;
    std::lock_guard<std::mutex> lock(g_LHD_Mutex);
    const auto it = g_LHD_ReportByMember.find(member);
    if (it != g_LHD_ReportByMember.end() && it->second.soldierIndex == soldierIndex)
        g_LHD_ReportByMember.erase(it);
}

static void __fastcall hkLostHostageDiscovery_StepRadioDiscovery(
    void* self,
    std::uint32_t soldierIndex,
    int step,
    void* info)
{
    if (!g_LHD_OrigStepRadioDiscovery)
        return;

    if (MissionCodeGuard::ShouldBypassHooks())
    {
        g_LHD_OrigStepRadioDiscovery(self, soldierIndex, step, info);
        return;
    }

    if ((step == LHD_STEP_ENTER || step == LHD_STEP_REENTER) && info != nullptr)
        LostHostageDiscovery_NoteReport(self, soldierIndex, info);   // before the step asks the CP for his call

    g_LHD_OrigStepRadioDiscovery(self, soldierIndex, step, info);

    if (step == LHD_STEP_EXIT)
        LostHostageDiscovery_ForgetReport(self, soldierIndex);
}

bool LostHostageDiscovery_TryOverrideForCallWithRadioType(
    std::uint32_t ownerIndex,
    std::uint8_t radioType,
    std::uint16_t member,
    std::uint32_t& outOverrideLabel)
{
    outOverrideLabel = 0u;

    if (!LostHostageDiscovery_IsHostageDiscoveryRadioType(radioType))
        return false;

    LostHostageDiscoveryReport report{};
    {
        std::lock_guard<std::mutex> lock(g_LHD_Mutex);

        const auto it = g_LHD_ReportByMember.find(member);
        if (it == g_LHD_ReportByMember.end())
            return false;

        report = it->second;
        g_LHD_ReportByMember.erase(it);
    }

    if (report.label == 0u || GetTickCount64() - report.startedMs > LHD_REPORT_FRESH_MS)
        return false;

    outOverrideLabel = report.label;
    if (report.dead)
        LogInfo("LostHostageDiscovery: soldier %u found prisoner 0x%04X dead, CP %u hears V_CPR0231 (radio 0x%02X)",
            report.soldierIndex, static_cast<unsigned>(report.targetId), ownerIndex, static_cast<unsigned>(radioType));
    return true;
}

void Add_LostHostageDiscovery(std::uint32_t gameObjectId, int hostageType)
{
    if (hostageType < 0 || hostageType > 2)
    {
        LogWarn("LostHostageDiscovery: invalid hostageType %d for gameObjectId 0x%08X, "
                "ignored", hostageType, gameObjectId);
        return;
    }

    const std::uint16_t rawGameObjectId = static_cast<std::uint16_t>(gameObjectId);
    const std::uint32_t speechLabel = LostHostageDiscovery_GetLabelForHostageType(hostageType);

    if (rawGameObjectId == LHD_INVALID_TARGET_ID || speechLabel == 0u)
    {
        LogWarn("LostHostageDiscovery: gameObjectId 0x%04X invalid, ignored",
                static_cast<unsigned>(rawGameObjectId));
        return;
    }

    LostHostageDiscoveryInfo info{};
    info.rawGameObjectId = rawGameObjectId;
    info.hostageType = hostageType;
    info.speechLabel = speechLabel;

    {
        std::lock_guard<std::mutex> lock(g_LHD_Mutex);
        g_LHD_TrackedHostagesByRawId[rawGameObjectId] = info;
    }
}

void Remove_LostHostageDiscovery(std::uint32_t gameObjectId)
{
    const std::uint16_t rawGameObjectId = static_cast<std::uint16_t>(gameObjectId);

    {
        std::lock_guard<std::mutex> lock(g_LHD_Mutex);
        g_LHD_TrackedHostagesByRawId.erase(rawGameObjectId);

        for (auto it = g_LHD_ReportByMember.begin();
            it != g_LHD_ReportByMember.end();)
        {
            if (it->second.targetId == rawGameObjectId && !it->second.dead)
                it = g_LHD_ReportByMember.erase(it);
            else
                ++it;
        }
    }
}

void Clear_LostHostageDiscovery()
{
    {
        std::lock_guard<std::mutex> lock(g_LHD_Mutex);
        g_LHD_TrackedHostagesByRawId.clear();
        LostHostageDiscovery_ResetRuntimeState_NoLock();
    }
}

void Dump_LostHostageDiscovery()
{
    std::lock_guard<std::mutex> lock(g_LHD_Mutex);

    for (const auto& kv : g_LHD_TrackedHostagesByRawId)
    {
        const LostHostageDiscoveryInfo& info = kv.second;
    }
}

bool Install_LostHostageDiscovery_Hooks()
{
    void* stepTarget = ResolveGameAddress(gAddr.StepRadioDiscovery);

    if (!stepTarget)
    {
        LogError("LostHostageDiscovery: hook address missing (step=%p), "
                 "hostage-type radio off", stepTarget);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_LHD_Mutex);
        LostHostageDiscovery_ResetRuntimeState_NoLock();
    }

    const bool okStep = CreateAndEnableHook(
        stepTarget,
        reinterpret_cast<void*>(&hkLostHostageDiscovery_StepRadioDiscovery),
        reinterpret_cast<void**>(&g_LHD_OrigStepRadioDiscovery));

    if (!okStep)
        LogError("LostHostageDiscovery: StepRadioDiscovery hook failed, "
                 "hostage-type radio off");

    return okStep;
}

bool Uninstall_LostHostageDiscovery_Hooks()
{
    DisableAndRemoveHook(ResolveGameAddress(gAddr.StepRadioDiscovery));

    g_LHD_OrigStepRadioDiscovery = nullptr;

    {
        std::lock_guard<std::mutex> lock(g_LHD_Mutex);
        g_LHD_TrackedHostagesByRawId.clear();
        LostHostageDiscovery_ResetRuntimeState_NoLock();
    }

    return true;
}
