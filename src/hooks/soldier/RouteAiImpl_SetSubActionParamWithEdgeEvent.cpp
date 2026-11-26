#include <pch.h>
#include "RouteAiImpl_SetSubActionParamWithEdgeEvent.h"

#include <Windows.h>
#include <intrin.h>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookArena.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "NativeCallPatch.h"
#include "log.h"
#include "../buddy/BuddyCodeCave.h"

namespace
{
    using SetSubActionParamWithEdgeEvent_t = void(__fastcall*)(void* routeAi, std::uint32_t soldierIndex, std::uint8_t* command, const std::uint8_t* edgeEvent);
    using SetAimingActionParam_t           = void(__fastcall*)(void* routeAi, std::uint32_t soldierIndex, std::uint8_t* aimingParam, const std::uint8_t* routeEvent, bool keepAiming, bool aimAtCombatTarget);
    using RouteEventQuery_t                = std::uint32_t(__fastcall*)(void* routeSet, std::uint16_t route, std::uint16_t node, std::uint16_t event);

    constexpr std::uint32_t kCautionSquatWalk      = 0xCE145152; // CautionSquatWalk
    constexpr std::uint32_t kCautionStandWalkReady = 0xDFC40BEF; // CautionStandWalkReady

    constexpr std::size_t kEventTypeQuery = 0x28 / 8;
    constexpr std::size_t kAimTypeQuery   = 0x60 / 8;

    constexpr std::size_t   kCommandFlags      = 0xB0;
    constexpr std::uint32_t kSquatMove         = 0x4000; // the move states pick SID_CAUTION_SQUAT_WALK
    constexpr std::uint32_t kCautionAimMode    = 0x10;
    constexpr std::uint32_t kOtherAimMode      = 0x20;
    constexpr std::uint32_t kReadyNotAim       = 0x2000;
    constexpr std::size_t   kCommandSubParam   = 0x40;
    constexpr std::size_t   kCommandSubKind    = 0xB5;
    constexpr std::uint8_t  kSubKindAim        = 1;
    constexpr std::size_t   kAimingParamSize   = 0x28;
    constexpr std::size_t   kAimingLimitsAt    = 0x14;
    constexpr std::uint32_t kAimingLimits      = 0x013A001E; // what the game gives CautionWalkAim
    constexpr std::size_t   kAimingLimit2At    = 0x18;
    constexpr std::uint16_t kAimingLimit2      = 0x013A;

    const std::uint8_t kSetSubActionParamPrologue[] = {
        0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x55, 0x48, 0x8D, 0x6C, 0x24, 0xC9, 0x48, 0x81, 0xEC, 0x90, 0x00, 0x00, 0x00
    };
    const std::uint8_t kSetAimingActionParamPrologue[] = {
        0x40, 0x53, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x60
    };
    const std::uint8_t kMoveCommandReturnSite[] = {
        0x83, 0x7C, 0x24, 0x40, 0x01, 0x7D, 0x23 // cmp dword [rsp+0x40],1 / jge: SetMoveActionCommand right after asking for the sub action
    };
    const std::uint8_t kAimClearSite[] = {
        0x81, 0xFB, 0xB0, 0xDE, 0xCF, 0xB2, // cmp ebx,CautionRunAim
        0x75, 0x04,                         // jne: the other events keep "ready, not aiming"
        0x0F, 0xBA, 0xF7, 0x0D              // btr edi,13
    };
    constexpr std::size_t kAimClearPatchSize = 6;
    constexpr std::size_t kAimClearJne       = 6;

    struct EdgeSites
    {
        std::uintptr_t setSubActionParam;
        std::uintptr_t setAimingActionParam;
        std::uintptr_t moveCommandReturn;
        std::uintptr_t aimClear;
    };

    SetSubActionParamWithEdgeEvent_t g_OrigSetSubActionParam = nullptr;
    SetAimingActionParam_t           g_SetAimingActionParam  = nullptr;
    void*                            g_MoveCommandReturn     = nullptr;
    void*                            g_HookTarget            = nullptr;
    NativeCodePatch                  g_AimClearPatch{};

    EdgeSites EdgeSitesForBuild()
    {
        return { gAddr.RouteAiImpl_SetSubActionParamWithEdgeEvent, gAddr.RouteAiImpl_SetAimingActionParam, gAddr.RouteAiImpl_EdgeEvent_MoveCommandReturn, gAddr.RouteAiImpl_EdgeEvent_AimClear };
    }

    bool BytesAtSEH(const std::uint8_t* at, const std::uint8_t* bytes, std::size_t size)
    {
        __try
        {
            return at && std::memcmp(at, bytes, size) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint32_t QueryEdgeEvent(const std::uint8_t* edgeEvent, std::size_t query)
    {
        __try
        {
            void* routeSet = *reinterpret_cast<void* const*>(edgeEvent + 0x8);
            const auto* ids = reinterpret_cast<const std::uint16_t*>(edgeEvent + 0x10); // route, node, event
            const RouteEventQuery_t ask = (*reinterpret_cast<RouteEventQuery_t* const*>(routeSet))[query];
            return ask(routeSet, ids[0], ids[1], ids[2]);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    bool AimAtEdgeTargetSEH(void* routeAi, std::uint32_t soldierIndex, std::uint8_t* command, const std::uint8_t* edgeEvent)
    {
        __try
        {
            if (static_cast<std::uint8_t>(QueryEdgeEvent(edgeEvent, kAimTypeQuery)) == 0)
                return false;
            std::uint8_t aiming[kAimingParamSize] = {};
            std::memcpy(aiming + kAimingLimitsAt, &kAimingLimits, sizeof(kAimingLimits));
            std::memcpy(aiming + kAimingLimit2At, &kAimingLimit2, sizeof(kAimingLimit2));
            g_SetAimingActionParam(routeAi, soldierIndex, aiming, edgeEvent, false, false);
            command[kCommandSubKind] = kSubKindAim;
            std::memcpy(command + kCommandSubParam, aiming, sizeof(aiming));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void __fastcall hk_SetSubActionParamWithEdgeEvent(void* routeAi, std::uint32_t soldierIndex, std::uint8_t* command, const std::uint8_t* edgeEvent)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigSetSubActionParam, routeAi, soldierIndex, command, edgeEvent);
        g_OrigSetSubActionParam(routeAi, soldierIndex, command, edgeEvent);
        if (_ReturnAddress() != g_MoveCommandReturn)
            return; // the vehicle command asks too
        auto& flags = *reinterpret_cast<std::uint32_t*>(command + kCommandFlags);
        switch (QueryEdgeEvent(edgeEvent, kEventTypeQuery))
        {
        case kCautionSquatWalk:
            flags |= kSquatMove | kCautionAimMode;
            if (!AimAtEdgeTargetSEH(routeAi, soldierIndex, command, edgeEvent))
                flags |= kReadyNotAim; // no aim target: weapon ready, like the game's own squat moves indoors
            break;
        case kCautionStandWalkReady:
            flags = (flags & ~kOtherAimMode) | kCautionAimMode | kReadyNotAim; // what TPP gives every walk in caution, here in any phase
            break;
        default:
            break;
        }
    }

    CaveCode BuildAimClearThunk(std::uintptr_t jne)
    {
        CaveCode code;
        code.Emit({ 0x81, 0xFB, 0xB0, 0xDE, 0xCF, 0xB2 }); // cmp ebx,CautionRunAim
        const std::size_t isCautionRunAim = code.Jcc8(0x74);
        code.Emit({ 0x81, 0xFB, 0x52, 0x51, 0x14, 0xCE }); // cmp ebx,CautionSquatWalk
        const std::size_t notSquatWalk = code.Jcc8(0x75);
        code.Emit({ 0x80, 0x7D, 0x65, 0x01 });             // cmp byte [rbp+0x65],1: command +0xB5, aiming only when the edge has a target
        code.Bind8(isCautionRunAim);
        code.Bind8(notSquatWalk);
        code.JmpAbs(jne);                                  // the game's jne reads our flags
        return code;
    }

    bool InstallAimClear(std::uint8_t* site)
    {
        const std::uintptr_t at = reinterpret_cast<std::uintptr_t>(site);
        const CaveCode code = BuildAimClearThunk(at + kAimClearJne);
        auto* thunk = static_cast<std::uint8_t*>(HookArena::AllocateNear(at, code.bytes.size()));
        const std::int64_t rel = thunk
            ? static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(thunk)) - static_cast<std::int64_t>(at + 5)
            : 0;
        if (!thunk || rel < INT32_MIN || rel > INT32_MAX)
        {
            LogError("RouteWalkEdges: no code page near RouteAiImpl::SetMoveActionCommand, CautionSquatWalk only readies the weapon in caution");
            return false;
        }
        std::memcpy(thunk, code.bytes.data(), code.bytes.size());
        FlushInstructionCache(GetCurrentProcess(), thunk, code.bytes.size());

        g_AimClearPatch = NativeCodePatch{};
        g_AimClearPatch.target = site;
        g_AimClearPatch.size = kAimClearPatchSize;
        std::memcpy(g_AimClearPatch.expected, kAimClearSite, kAimClearPatchSize);
        g_AimClearPatch.replacement[0] = 0xE9;
        const std::int32_t rel32 = static_cast<std::int32_t>(rel);
        std::memcpy(g_AimClearPatch.replacement + 1, &rel32, sizeof(rel32));
        g_AimClearPatch.replacement[5] = 0x90;

        for (int attempt = 0; attempt < 50; ++attempt)
        {
            if (InstallNativeCodePatches(&g_AimClearPatch, 1))
                return true;
            if (!NativePatchErrorIsTransient(GetLastError()))
                break;
            Sleep(4);
        }
        LogError("RouteWalkEdges: RouteAiImpl::SetMoveActionCommand patch refused (%s), CautionSquatWalk only readies the weapon in caution",
            NativePatchErrorName(GetLastError()));
        return false;
    }
}

bool Install_RouteAiImpl_SetSubActionParamWithEdgeEvent_Hook()
{
    if (g_HookTarget)
        return true;
    const EdgeSites sites = EdgeSitesForBuild();
    if (!sites.setSubActionParam)
        return true;

    auto* setSubActionParam    = static_cast<std::uint8_t*>(ResolveGameAddress(sites.setSubActionParam));
    auto* setAimingActionParam = static_cast<std::uint8_t*>(ResolveGameAddress(sites.setAimingActionParam));
    auto* moveCommandReturn    = static_cast<std::uint8_t*>(ResolveGameAddress(sites.moveCommandReturn));
    auto* aimClear             = static_cast<std::uint8_t*>(ResolveGameAddress(sites.aimClear));
    const HookEntry setSubActionParamEntry    = CheckHookEntry(setSubActionParam, kSetSubActionParamPrologue, sizeof(kSetSubActionParamPrologue));
    const HookEntry setAimingActionParamEntry = CheckHookEntry(setAimingActionParam, kSetAimingActionParamPrologue, sizeof(kSetAimingActionParamPrologue));
    if (setSubActionParamEntry == HookEntry::Unknown
        || setAimingActionParamEntry == HookEntry::Unknown
        || !BytesAtSEH(moveCommandReturn, kMoveCommandReturnSite, sizeof(kMoveCommandReturnSite))
        || !BytesAtSEH(aimClear, kAimClearSite, sizeof(kAimClearSite)))
    {
        LogError("RouteWalkEdges: soldier route code does not match %s, CautionSquatWalk/CautionStandWalkReady edges stay a normal walk",
            GetGameBuildName(gGameBuild));
        return false;
    }
    if (setSubActionParamEntry == HookEntry::ForeignHook)
        LogHookedOnTop("RouteWalkEdges: RouteAiImpl::SetSubActionParamWithEdgeEvent", setSubActionParam);
    if (setAimingActionParamEntry == HookEntry::ForeignHook)
        LogHookedOnTop("RouteWalkEdges: RouteAiImpl::SetAimingActionParam", setAimingActionParam);

    g_SetAimingActionParam = reinterpret_cast<SetAimingActionParam_t>(setAimingActionParam);
    g_MoveCommandReturn = moveCommandReturn;
    if (!CreateAndEnableHook(setSubActionParam, reinterpret_cast<void*>(&hk_SetSubActionParamWithEdgeEvent),
                             reinterpret_cast<void**>(&g_OrigSetSubActionParam)))
    {
        LogError("RouteWalkEdges: RouteAiImpl::SetSubActionParamWithEdgeEvent hook failed, CautionSquatWalk/CautionStandWalkReady edges stay a normal walk");
        return false;
    }
    g_HookTarget = setSubActionParam;

    InstallAimClear(aimClear);
    return true;
}

bool Uninstall_RouteAiImpl_SetSubActionParamWithEdgeEvent_Hook()
{
    if (g_AimClearPatch.installed)
        RemoveNativeCodePatches(&g_AimClearPatch, 1);
    if (g_HookTarget)
    {
        DisableAndRemoveHook(g_HookTarget);
        g_HookTarget = nullptr;
        g_OrigSetSubActionParam = nullptr;
    }
    return true;
}
