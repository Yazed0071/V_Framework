#include <pch.h>
#include "RouteGroupImpl_GetEventId.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using GetEventId_t = std::uint32_t(__fastcall*)(void* routeGroup, std::uint16_t route, std::uint16_t node, std::uint16_t event);

    // GZ route edge events and the TPP events that do the same thing
    constexpr std::uint32_t kCautionStandWalkAim = 0x7C8DC089; // CautionStandWalkAim
    constexpr std::uint32_t kCautionStandRunAim  = 0xCE36D8F6; // CautionStandRunAim
    constexpr std::uint32_t kCautionWalkAim      = 0x842FF186; // CautionWalkAim
    constexpr std::uint32_t kRouteMoveRun        = 0xADAA30C2; // RouteMoveRun
    constexpr std::uint32_t kRelaxedRun          = 0xA6F6BF5C; // RelaxedRun
    constexpr std::uint32_t kStandDash           = 0x75687329; // StandDash
    constexpr std::uint32_t kCautionDash         = 0xF4973B94; // CautionDash
    constexpr std::uint32_t kNormalStandWalk     = 0x2A605C07; // NormalStandWalk
    constexpr std::uint32_t kRelaxedWalk         = 0x596C1D5A; // RelaxedWalk

    const std::uint8_t kGetEventIdPrologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x8B, 0x41, 0x50, 0x33, 0xC9, 0x41, 0x0F, 0xB7, 0xD8, 0x44, 0x0F, 0xB6, 0x50, 0x40
    };

    GetEventId_t g_OrigGetEventId = nullptr;
    void*        g_HookTarget     = nullptr;

    std::uintptr_t GetEventIdAddr()
    {
        return gAddr.RouteGroupImpl_GetEventId;
    }

    std::uint32_t __fastcall hk_GetEventId(void* routeGroup, std::uint16_t route, std::uint16_t node, std::uint16_t event)
    {
        MISSION_GUARD_ORIGINAL_RET(g_OrigGetEventId, routeGroup, route, node, event);
        const std::uint32_t id = g_OrigGetEventId(routeGroup, route, node, event);
        switch (id)
        {
        case kCautionStandWalkAim: return kCautionWalkAim;
        case kCautionStandRunAim:  return kCautionWalkAim; // GZ plays it as an aiming walk (StandDynamicAimWalk)
        case kRouteMoveRun:        return kRelaxedRun;
        case kStandDash:           return kCautionDash;
        case kNormalStandWalk:     return kRelaxedWalk;
        default:                   return id;
        }
    }
}

bool Install_RouteGroupImpl_GetEventId_Hook()
{
    if (g_HookTarget)
        return true;
    const std::uintptr_t addr = GetEventIdAddr();
    if (!addr)
        return true;

    auto* target = static_cast<std::uint8_t*>(ResolveGameAddress(addr));
    const HookEntry entry = CheckHookEntry(target, kGetEventIdPrologue, sizeof(kGetEventIdPrologue));
    if (entry == HookEntry::Unknown)
    {
        LogError("RouteWalkEdges: route code does not match %s, GZ walk edge events stay a normal walk",
            GetGameBuildName(gGameBuild));
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("RouteWalkEdges: RouteGroupImpl::GetEventId", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_GetEventId), reinterpret_cast<void**>(&g_OrigGetEventId)))
    {
        LogError("RouteWalkEdges: RouteGroupImpl::GetEventId hook failed, GZ walk edge events stay a normal walk");
        return false;
    }
    g_HookTarget = target;
    return true;
}

bool Uninstall_RouteGroupImpl_GetEventId_Hook()
{
    if (g_HookTarget)
    {
        DisableAndRemoveHook(g_HookTarget);
        g_HookTarget = nullptr;
        g_OrigGetEventId = nullptr;
    }
    return true;
}
