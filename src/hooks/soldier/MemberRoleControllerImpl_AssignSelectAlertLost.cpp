#include <pch.h>
#include "MemberRoleControllerImpl_AssignSelectAlertLost.h"

#include <Windows.h>
#include <cstddef>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "SoldierCombatVoice.h"
#include "log.h"

namespace
{
    using AssignSelectAlertLost_t = std::uint64_t(__fastcall*)(void* roleController, std::uint32_t cp, std::uint32_t pointKinds,
                                                              std::uint32_t teamSize);
    using SetMemberRole_t         = void*(__fastcall*)(std::uint8_t* service, std::uint32_t member, std::uint32_t role,
                                                       const void* param, std::uint64_t size);

    constexpr std::uint8_t   kInvestigateRole       = 1;      // what AssignSelectAlertLost gives each searcher, nearest first
    constexpr std::uint32_t  kRoleMask              = 0xFF;
    constexpr std::ptrdiff_t kServiceToCpController = -0x18;
    constexpr std::size_t    kCpControllerMembers   = 0x58;   // 0x18 per member
    constexpr std::size_t    kMemberSize            = 0x18;
    constexpr std::size_t    kMemberGameObjectId    = 0x10;
    constexpr std::uint16_t  kSoldierType           = 0x400;
    constexpr std::uint16_t  kTypeMask              = 0xFE00;
    constexpr std::uint16_t  kIndexMask             = 0x1FF;
    constexpr std::uint32_t  kNoSoldier             = 0xFFFFFFFFu;

    constexpr std::uint8_t kAssignSelectAlertLostEntry[] = {
        0x48, 0x8B, 0xC4, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81,
        0xEC, 0x20, 0x01, 0x00, 0x00, 0x48, 0xC7, 0x80,
    };
    constexpr std::uint8_t kSetMemberRoleEntry[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57,
        0x48, 0x83, 0xEC, 0x20, 0xF6, 0x81, 0x76, 0x01,
    };

    struct SearchTeam
    {
        std::uint8_t* service    = nullptr;
        std::uint32_t lead       = kNoSoldier;   // CP member numbers
        std::uint32_t second     = kNoSoldier;
        std::uint32_t count      = 0;
    };

    AssignSelectAlertLost_t  g_OrigAssign         = nullptr;
    SetMemberRole_t          g_OrigSetMemberRole  = nullptr;
    void*                    g_AssignTarget       = nullptr;
    void*                    g_SetMemberRoleTarget = nullptr;
    thread_local SearchTeam* t_Team               = nullptr;   // while AssignSelectAlertLost runs on this thread

    std::uint32_t SoldierOf(std::uint8_t* service, std::uint32_t member)
    {
        if (!service || member == kNoSoldier)
            return kNoSoldier;
        __try
        {
            const std::uint8_t* entry = *reinterpret_cast<std::uint8_t* const*>(service + kServiceToCpController + kCpControllerMembers)
                + member * kMemberSize;
            const std::uint16_t gameObjectId = *reinterpret_cast<const std::uint16_t*>(entry + kMemberGameObjectId);
            return (gameObjectId & kTypeMask) == kSoldierType ? (gameObjectId & kIndexMask) : kNoSoldier;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return kNoSoldier;
        }
    }

    void* __fastcall hk_SetMemberRole(std::uint8_t* service, std::uint32_t member, std::uint32_t role, const void* param, std::uint64_t size)
    {
        SearchTeam* team = t_Team;
        if (team && (role & kRoleMask) == kInvestigateRole)
        {
            team->service = service;
            if (team->count == 0)
                team->lead = member;
            else if (team->count == 1)
                team->second = member;
            ++team->count;
        }
        return g_OrigSetMemberRole(service, member, role, param, size);
    }

    std::uint64_t __fastcall hk_AssignSelectAlertLost(void* roleController, std::uint32_t cp, std::uint32_t pointKinds, std::uint32_t teamSize)
    {
        if (MissionCodeGuard::ShouldBypassHooks())
            return g_OrigAssign(roleController, cp, pointKinds, teamSize);

        SearchTeam team;
        SearchTeam* outer = t_Team;
        t_Team = &team;
        const std::uint64_t result = g_OrigAssign(roleController, cp, pointKinds, teamSize);
        t_Team = outer;
        if (team.count)
            SoldierCombatVoice_OnSearchTeamSent(SoldierOf(team.service, team.lead), SoldierOf(team.service, team.second));
        return result;
    }
}

bool Install_MemberRoleControllerImpl_AssignSelectAlertLost_Hook()
{
    if (g_AssignTarget)
        return true;
    if (!gAddr.MemberRoleControllerImpl_AssignSelectAlertLost || !gAddr.CommandPost2Impl_SetMemberRole)
    {
        LogWarn("SoldierCombatVoice: AssignSelectAlertLost unknown for %s, search teams set off without EVE020", GetGameBuildName(gGameBuild));
        return true;
    }

    void* assign        = ResolveGameAddress(gAddr.MemberRoleControllerImpl_AssignSelectAlertLost);
    void* setMemberRole = ResolveGameAddress(gAddr.CommandPost2Impl_SetMemberRole);
    const HookEntry assignState = CheckHookEntry(assign, kAssignSelectAlertLostEntry, sizeof(kAssignSelectAlertLostEntry));
    const HookEntry roleState   = CheckHookEntry(setMemberRole, kSetMemberRoleEntry, sizeof(kSetMemberRoleEntry));
    if (assignState == HookEntry::Unknown || roleState == HookEntry::Unknown)
    {
        const bool assignDiffers = assignState == HookEntry::Unknown;
        char entry[160] = {};
        DescribeFunctionEntry(assignDiffers ? assign : setMemberRole, entry, sizeof(entry));
        LogError("SoldierCombatVoice: %s differs on %s (%s), search teams set off without EVE020",
                 assignDiffers ? "AssignSelectAlertLost" : "SetMemberRole_", GetGameBuildName(gGameBuild), entry);
        return false;
    }
    if (assignState == HookEntry::ForeignHook)
        LogHookedOnTop("SoldierCombatVoice: MemberRoleControllerImpl::AssignSelectAlertLost", assign);
    if (roleState == HookEntry::ForeignHook)
        LogHookedOnTop("SoldierCombatVoice: CommandPost2Impl::SetMemberRole_", setMemberRole);

    if (!CreateAndEnableHook(setMemberRole, reinterpret_cast<void*>(&hk_SetMemberRole), reinterpret_cast<void**>(&g_OrigSetMemberRole)))
    {
        LogError("SoldierCombatVoice: SetMemberRole_ hook failed at %p, search teams set off without EVE020", setMemberRole);
        g_OrigSetMemberRole = nullptr;
        return false;
    }
    g_SetMemberRoleTarget = setMemberRole;

    if (!CreateAndEnableHook(assign, reinterpret_cast<void*>(&hk_AssignSelectAlertLost), reinterpret_cast<void**>(&g_OrigAssign)))
    {
        LogError("SoldierCombatVoice: AssignSelectAlertLost hook failed at %p, search teams set off without EVE020", assign);
        g_OrigAssign = nullptr;
        DisableAndRemoveHook(g_SetMemberRoleTarget);
        g_SetMemberRoleTarget = nullptr;
        g_OrigSetMemberRole   = nullptr;
        return false;
    }
    g_AssignTarget = assign;
    return true;
}

void Uninstall_MemberRoleControllerImpl_AssignSelectAlertLost_Hook()
{
    if (g_AssignTarget)
        DisableAndRemoveHook(g_AssignTarget);
    if (g_SetMemberRoleTarget)
        DisableAndRemoveHook(g_SetMemberRoleTarget);
    g_AssignTarget        = nullptr;
    g_SetMemberRoleTarget = nullptr;
    g_OrigAssign          = nullptr;
    g_OrigSetMemberRole   = nullptr;
}
