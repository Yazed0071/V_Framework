#include <pch.h>
#include "ActionCoreImpl_ExecPostPutMotion.h"

#include <Windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "LuaBroadcaster.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using ApplyUavEmp_t = std::uint8_t(__fastcall*)(void* actionCore, std::uint32_t playerIndex);

    const std::uint8_t kGateCode[] = { 0xC1, 0xE8, 0x12, 0xA8, 0x01, 0x74, 0x0E, 0x8B, 0xD6, 0x48, 0x8B, 0xCF, 0xE8 }; // shr eax,0x12 / test al,1 / je +0xE / mov edx,esi / mov rcx,rdi / call EMP: only players flagged for an FOB mission get the EMP
    constexpr std::size_t  kGateLead       = 5;
    constexpr std::size_t  kGateDisp       = 1;
    constexpr std::size_t  kGateCallRel    = 8;
    constexpr std::uint8_t kGateSkipDisp   = 0x0E;
    constexpr std::uint8_t kGateOpenDisp   = 0x00; // je to the next instruction, the call always runs

    const std::uint8_t kApplyPrologue[] = {
        0x4C, 0x8B, 0xDC, 0x55, 0x53, 0x57, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
        0x48, 0x8D, 0x6C, 0x24, 0x88, 0x48, 0x81, 0xEC, 0x78, 0x01, 0x00, 0x00 }; // mov r11,rsp / push rbp,rbx,rdi,r13,r14,r15 / lea rbp,[rsp-0x78] / sub rsp,0x178

    constexpr std::size_t   kCoreFirstIndex   = 0x8;
    constexpr std::size_t   kCoreContext      = 0x408;
    constexpr std::size_t   kCoreEmpRecords   = 0x7C8;
    constexpr std::size_t   kContextPlayer    = 0x138;
    constexpr std::size_t   kPlayerLocalIndex = 0x218;
    constexpr std::size_t   kEmpRecordStride  = 0x20;
    constexpr std::size_t   kEmpRecordUav     = 0x10; // u16 game object id of the UAV hitting this player, 0xFFFF = none this frame
    constexpr std::size_t   kEmpRecordLevel   = 0x12;
    constexpr std::uint16_t kNoUav            = 0xFFFF;

    std::uint8_t*              g_Gate        = nullptr;
    bool                       g_GateOpen    = false;
    bool                       g_GateWarned  = false;
    void*                      g_ApplyTarget = nullptr;
    ApplyUavEmp_t              g_OrigApply   = nullptr;
    std::atomic<std::uint16_t> g_EmpUav{ kNoUav };
    std::atomic<std::uint32_t> g_MissionCode{ 0xFFFFFFFFu };

    bool WriteByte(std::uint8_t* target, std::uint8_t value)
    {
        DWORD oldProtect = 0;
        if (!VirtualProtect(target, 1, PAGE_EXECUTE_READWRITE, &oldProtect))
            return false;
        *target = value;
        DWORD restored = 0;
        VirtualProtect(target, 1, oldProtect, &restored);
        FlushInstructionCache(GetCurrentProcess(), target, 1);
        return true;
    }

    void SetGate(bool open)
    {
        if (!g_Gate || g_GateOpen == open)
            return;
        std::uint8_t* disp = g_Gate + kGateDisp;
        if (g_Gate[0] != 0x74 || *disp != (open ? kGateSkipDisp : kGateOpenDisp))
        {
            if (!g_GateWarned)
                LogWarn("UavEmp: EMP gate at %p was changed by another module, left alone", static_cast<void*>(g_Gate));
            g_GateWarned = true;
            return;
        }
        if (WriteByte(disp, open ? kGateOpenDisp : kGateSkipDisp))
            g_GateOpen = open;
    }

    bool ReadEmpSEH(std::uint8_t* core, std::uint32_t playerIndex, bool& local, std::uint16_t& uav, std::uint8_t& level)
    {
        __try
        {
            auto* context = *reinterpret_cast<std::uint8_t**>(core + kCoreContext);
            auto* player  = *reinterpret_cast<std::uint8_t**>(context + kContextPlayer);
            local = playerIndex == *reinterpret_cast<std::uint32_t*>(player + kPlayerLocalIndex);
            if (!local)
                return true;
            const std::uint32_t slot = playerIndex - *reinterpret_cast<std::uint32_t*>(core + kCoreFirstIndex);
            auto* record = *reinterpret_cast<std::uint8_t**>(core + kCoreEmpRecords) + static_cast<std::size_t>(slot) * kEmpRecordStride;
            uav   = *reinterpret_cast<std::uint16_t*>(record + kEmpRecordUav);
            level = record[kEmpRecordLevel];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void ReportEmp(std::uint16_t uav, std::uint8_t level)
    {
        const std::uint16_t previous = g_EmpUav.exchange(uav);
        if (previous == uav)
            return;
        if (previous != kNoUav)
            V_FrameWork::EmitMessage("Player", "UavEmpEnd", static_cast<int>(previous));
        if (uav != kNoUav)
            V_FrameWork::EmitMessage("Player", "UavEmpStart", static_cast<int>(uav), static_cast<int>(level));
    }

    std::uint8_t __fastcall hkApplyUavEmp(void* actionCore, std::uint32_t playerIndex)
    {
        if (MissionCodeGuard::ShouldBypassHooks())
            return g_OrigApply(actionCore, playerIndex);

        bool          local = false;
        std::uint16_t uav   = kNoUav;
        std::uint8_t  level = 0;
        const bool read = ReadEmpSEH(static_cast<std::uint8_t*>(actionCore), playerIndex, local, uav, level);

        const std::uint8_t active = g_OrigApply(actionCore, playerIndex);
        if (read && local)
            ReportEmp(active != 0 ? uav : kNoUav, level);
        return active;
    }

    void HookApplyRoutine()
    {
        const std::int32_t rel = *reinterpret_cast<const std::int32_t*>(g_Gate + kGateCallRel);
        void* apply = g_Gate + kGateCallRel + sizeof(rel) + rel;

        const HookEntry entry = CheckHookEntry(apply, kApplyPrologue, sizeof(kApplyPrologue));
        if (entry == HookEntry::Unknown)
        {
            LogError("UavEmp: EMP routine prologue mismatch on %s, no UavEmpStart/UavEmpEnd messages",
                GetGameBuildName(gGameBuild));
            return;
        }
        if (entry == HookEntry::ForeignHook)
            LogHookedOnTop("UavEmp: EMP routine", apply);

        if (!CreateAndEnableHook(apply, reinterpret_cast<void*>(&hkApplyUavEmp),
                                 reinterpret_cast<void**>(&g_OrigApply)))
        {
            LogError("UavEmp: EMP routine hook failed on %s, no UavEmpStart/UavEmpEnd messages",
                GetGameBuildName(gGameBuild));
            return;
        }
        g_ApplyTarget = apply;
    }
}

void UavEmp_EnforceMissionGuard()
{
    if (!g_Gate)
        return;
    const std::uint32_t code = MissionCodeGuard::GetCurrentMissionCode();
    if (g_MissionCode.exchange(code) != code)
        g_EmpUav.store(kNoUav);
    SetGate(!MissionCodeGuard::ShouldBypassHooks());
}

bool Install_ActionCoreImpl_ExecPostPutMotion_Hook()
{
    if (g_Gate)
        return true;
    if (!gAddr.ActionCoreImpl_ExecPostPutMotion_EmpGate)
    {
        LogError("UavEmp: EMP gate address missing on %s, UAV EMP stays FOB-only", GetGameBuildName(gGameBuild));
        return false;
    }

    auto* gate = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.ActionCoreImpl_ExecPostPutMotion_EmpGate));
    std::uint8_t live[sizeof(kGateCode)] = {};
    if (!gate || !ReadEntryBytesSEH(gate - kGateLead, live, sizeof(live))
        || std::memcmp(live, kGateCode, sizeof(live)) != 0)
    {
        LogError("UavEmp: EMP gate code does not match %s, UAV EMP stays FOB-only", GetGameBuildName(gGameBuild));
        return false;
    }
    g_Gate = gate;

    HookApplyRoutine();
    SetGate(!MissionCodeGuard::ShouldBypassHooks());
    return true;
}

void Uninstall_ActionCoreImpl_ExecPostPutMotion_Hook()
{
    SetGate(false);
    if (g_ApplyTarget)
        DisableAndRemoveHook(g_ApplyTarget);
    g_ApplyTarget = nullptr;
    g_OrigApply   = nullptr;
    g_Gate        = nullptr;
    g_GateOpen    = false;
    g_EmpUav.store(kNoUav);
}
