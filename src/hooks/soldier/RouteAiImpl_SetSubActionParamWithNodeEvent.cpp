#include <pch.h>
#include "RouteAiImpl_SetSubActionParamWithNodeEvent.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>

#include "AddressSet.h"
#include "FeatureSwitch.h"
#include "FoxHashes.h"
#include "HookArena.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "NativeCallPatch.h"
#include "log.h"
#include "../buddy/BuddyCodeCave.h"
#include "../equip/EquipPartParams.h"

namespace
{
    using SetSubActionParamWithNodeEvent_t = void(__fastcall*)(void* routeAi, std::uint32_t soldierIndex, std::uint8_t* command, const std::uint8_t* nodeEvent);
    using SetLookActionParam_t             = void(__fastcall*)(void* routeAi, std::uint8_t* lookParam, const std::uint8_t* nodeEvent);
    using RouteEventQuery_t                = std::uint32_t(__fastcall*)(void* routeSet, std::uint16_t route, std::uint16_t node, std::uint16_t event);
    using RouteEventParamQuery_t           = std::uint32_t(__fastcall*)(void* routeSet, std::uint16_t route, std::uint16_t node, std::uint16_t event, std::uint32_t param);

    constexpr std::uint32_t kSquatIdle     = 0x8082EE2E; // SquatIdle
    constexpr std::uint32_t kSquatIdleAct  = 0x413F5645; // SquatIdleAct
    constexpr std::uint32_t kSquatActRifle = 0xA2DB3B2F; // SquatActRifle

    constexpr std::size_t kEventTypeQuery  = 0x28 / 8;
    constexpr std::size_t kEventParamQuery = 0x38 / 8;
    constexpr std::size_t kLookTargetQuery = 0x60 / 8;
    constexpr std::uint32_t kActParam      = 0;

    constexpr std::uint16_t kStanceSquat        = 2;
    constexpr std::size_t   kCommandSubParam    = 0x40;
    constexpr std::size_t   kCommandSubKind     = 0xB5;
    constexpr std::uint8_t  kSubKindLook        = 2;
    constexpr std::size_t   kLookParamSize      = 0x1C;
    constexpr std::size_t   kLookDefaultsOffset = 0x10;
    constexpr float         kLookDefaults[2]    = { 1.0f, 0.6f }; // what the game gives RelaxedStandIdle

    const std::uint8_t kSetSubActionParamPrologue[] = {
        0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x48, 0x8B, 0xEC, 0x48, 0x83, 0xEC, 0x78
    };
    const std::uint8_t kSetLookActionParamPrologue[] = {
        0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x60
    };
    const std::uint8_t kCautionSwapSite[] = {
        0x83, 0xBD, 0x80, 0x02, 0x00, 0x00, 0x01, 0x7C, 0x46, // cmp dword [rbp+0x280],1 / jl: sneak phase goes straight to the event dispatch
        0x3D, 0xDF, 0x50, 0xFE, 0xEA, 0x74, 0x07,             // cmp eax,RelaxedStandIdle / je
        0x3D, 0x47, 0xDD, 0x94, 0xEF, 0x75, 0x38              // cmp eax,RelaxedIdleAct / jne
    };
    constexpr std::size_t kSwapPatchOffset = 16;        // the RelaxedIdleAct compare
    constexpr std::size_t kCautionSwap     = 23;        // where the game turns a relaxed idle into CautionStandIdle
    constexpr std::size_t kEventDispatch   = 23 + 0x38;

    struct RouteSites
    {
        std::uintptr_t setSubActionParam;
        std::uintptr_t setLookActionParam;
        std::uintptr_t cautionSwap;
    };

    constexpr std::uint64_t kSquatDownClip = 0xFC52520E96AD0358ull; // /Assets/tpp/motion/SI_game/fani/bodies/enet/enetasr/enetasr_s_pat_idl_2q_a_st.gani
    constexpr std::uint64_t kSquatIdleClip = 0xFC5314AA2D324866ull; // /Assets/tpp/motion/SI_game/fani/bodies/enet/enetasr/enetasr_s_pat_idl_2q_a_lp.gani
    constexpr std::uint64_t kStandUpClip   = 0xFC5115F306086F39ull; // /Assets/tpp/motion/SI_game/fani/bodies/enet/enetasr/enetasr_s_pat_idl_2q_a_ed.gani
    constexpr const char*   kGzSquatDown   = "/Assets/tpp/motion/SI_game/fani/bodies/enem/enemasr/enemasr_s_pat_idl_2q_a_st.gani";
    constexpr const char*   kGzSquatIdle   = "/Assets/tpp/motion/SI_game/fani/bodies/enem/enemasr/enemasr_s_pat_idl_2q_a_lp.gani";
    constexpr const char*   kGzStandUp     = "/Assets/tpp/motion/SI_game/fani/bodies/enem/enemasr/enemasr_s_pat_idl_2q_a_ed.gani";
    constexpr const char*   kQuietSquatIdle = "/Assets/tpp/motion/SI_game/fani/bodies/enet/enetasr/enetasr_s_pat_idl_2q_a_lp_mute.gani"; // TPP's loop without the gun butt hitting the ground at frame 399

    constexpr std::uint64_t kRadioStartClip = 0xFC53FDC8D2E6EBB6ull; // /Assets/tpp/motion/SI_game/fani/bodies/enet/enetasr/enetasr_s_rdo_rdy_st.gani
    constexpr std::uint64_t kRadioLoopClip  = 0xFC513815034C5BB2ull; // /Assets/tpp/motion/SI_game/fani/bodies/enet/enetasr/enetasr_s_rdo_low_rsk_rdy_lp.gani
    constexpr std::uint64_t kRadioEndClip   = 0xFC53003FE961DC7Aull; // /Assets/tpp/motion/SI_game/fani/bodies/enet/enetasr/enetasr_s_rdo_rdy_ed.gani
    constexpr const char*   kGzRadioStart   = "/Assets/tpp/motion/SI_game/fani/bodies/enem/enemasr/enemasr_q_rdo_b_st.gani";
    constexpr const char*   kGzRadioLoop    = "/Assets/tpp/motion/SI_game/fani/bodies/enem/enemasr/enemasr_q_rdo_b_lp.gani";
    constexpr const char*   kGzRadioEnd     = "/Assets/tpp/motion/SI_game/fani/bodies/enem/enemasr/enemasr_q_rdo_b_ed.gani";

    constexpr int           kBarrelLengthLong    = 2; // TppEquip.BARREL_LENGTH_LONG

    constexpr std::size_t   kRouteAiSoldiers      = 0x1B0;
    constexpr std::size_t   kHubSlots             = 0x18;  // realized slots: +0x28 the soldier in each slot, +0x44 how many
    constexpr std::size_t   kSlotSoldiers         = 0x28;
    constexpr std::size_t   kSlotCount            = 0x44;
    constexpr std::size_t   kHubEquipTable        = 0xA0;  // +0x8 each soldier's hand slot at byte +6, +0x10 three weapon ids per soldier
    constexpr std::size_t   kEquipHandSlots       = 0x8;
    constexpr std::size_t   kEquipIds             = 0x10;
    constexpr std::size_t   kHubBlocks            = 0x540; // one RealizedSoldier2Impl per block of slots
    constexpr std::size_t   kHubBlockCount        = 0x71C;
    constexpr std::size_t   kBlockControllers     = 0x88;  // one or two AnimationControllers over the block's slots
    constexpr std::size_t   kBlockControllerCount = 0xB8;
    constexpr std::size_t   kControllerUnitCount  = 0x08;
    constexpr std::size_t   kControllerFirstSlot  = 0x0C;
    constexpr std::size_t   kControllerUnits      = 0x10;
    constexpr std::size_t   kUnitSize             = 0xC0;
    constexpr std::size_t   kUnitAnimControl      = 0x70;
    constexpr std::uint16_t kEmptySlot            = 0x1FF;
    constexpr std::uint32_t kNoSoldier            = 0xFFFFFFFFu;
    constexpr std::uint32_t kMaxBlocks            = 64;
    constexpr std::uint32_t kMaxControllers       = 2;
    constexpr std::uint32_t kMaxUnits             = 512;
    constexpr std::size_t   kMaxGzSquatControls   = 512;   // oldest entry goes first when full: AnimControls of past missions are gone anyway
    constexpr std::uint32_t kMaxSoldiers          = 512;

    SetSubActionParamWithNodeEvent_t g_OrigSetSubActionParam = nullptr;
    SetLookActionParam_t             g_SetLookActionParam    = nullptr;
    void*                            g_HookTarget            = nullptr;
    NativeCodePatch                  g_CautionSwapPatch{};
    std::atomic<std::uint8_t*>       g_CautionSwapFlag{ nullptr }; // inside the swap thunk: 1 = swap SquatIdle/SquatIdleAct, 0 = vanilla (FOB)
    std::atomic<std::uint8_t*>       g_SoldierHub{ nullptr };  // every soldier shares it; noted from the route AI
    std::atomic<const void*>         g_GzSquatControls[kMaxGzSquatControls] = {};
    std::size_t                      g_GzSquatNext = 0;
    std::mutex                       g_GzSquatLock;
    std::atomic<bool>                g_RifleSquat[kMaxSoldiers] = {}; // the soldier's last node event was SquatIdleAct/SquatIdle with SquatActRifle

    RouteSites RouteSitesForBuild()
    {
        return { gAddr.RouteAiImpl_SetSubActionParamWithNodeEvent, gAddr.RouteAiImpl_SetLookActionParam, gAddr.RouteAiImpl_NodeEvent_CautionSwap };
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

    std::uint32_t QueryNodeEvent(const std::uint8_t* nodeEvent, std::size_t query)
    {
        __try
        {
            void* routeSet = *reinterpret_cast<void* const*>(nodeEvent + 0x8);
            const auto* ids = reinterpret_cast<const std::uint16_t*>(nodeEvent + 0x10); // route, node, event
            const RouteEventQuery_t ask = (*reinterpret_cast<RouteEventQuery_t* const*>(routeSet))[query];
            return ask(routeSet, ids[0], ids[1], ids[2]);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    std::uint32_t QueryNodeEventParam(const std::uint8_t* nodeEvent, std::uint32_t param)
    {
        __try
        {
            void* routeSet = *reinterpret_cast<void* const*>(nodeEvent + 0x8);
            const auto* ids = reinterpret_cast<const std::uint16_t*>(nodeEvent + 0x10);
            const auto ask = reinterpret_cast<RouteEventParamQuery_t>((*reinterpret_cast<void* const* const*>(routeSet))[kEventParamQuery]);
            return ask(routeSet, ids[0], ids[1], ids[2], param);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    void LookAtNodeTarget(void* routeAi, std::uint8_t* command, const std::uint8_t* nodeEvent)
    {
        __try
        {
            std::uint8_t look[kLookParamSize] = {};
            std::memcpy(look + kLookDefaultsOffset, kLookDefaults, sizeof(kLookDefaults));
            g_SetLookActionParam(routeAi, look, nodeEvent);
            command[kCommandSubKind] = kSubKindLook;
            std::memcpy(command + kCommandSubParam, look, sizeof(look));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    std::uint32_t SoldierOfAnimControlSEH(const std::uint8_t* hub, const void* animControl)
    {
        __try
        {
            const std::uint8_t* slots = *reinterpret_cast<std::uint8_t* const*>(hub + kHubSlots);
            const auto* soldierOfSlot = *reinterpret_cast<const std::uint16_t* const*>(slots + kSlotSoldiers);
            const std::uint32_t slotCount = *reinterpret_cast<const std::uint32_t*>(slots + kSlotCount);
            const auto* blocks = *reinterpret_cast<const std::uint8_t* const* const*>(hub + kHubBlocks);
            const std::uint32_t blockCount = *reinterpret_cast<const std::uint32_t*>(hub + kHubBlockCount);
            for (std::uint32_t b = 0; b < blockCount && b < kMaxBlocks; ++b)
            {
                const std::uint8_t* block = blocks[b];
                if (!block)
                    continue;
                const std::uint32_t controllerCount = *reinterpret_cast<const std::uint32_t*>(block + kBlockControllerCount);
                for (std::uint32_t c = 0; c < controllerCount && c < kMaxControllers; ++c)
                {
                    const std::uint8_t* controller = *reinterpret_cast<const std::uint8_t* const*>(block + kBlockControllers + c * sizeof(void*));
                    if (!controller)
                        continue;
                    const std::uint32_t unitCount = *reinterpret_cast<const std::uint32_t*>(controller + kControllerUnitCount);
                    const std::uint32_t firstSlot = *reinterpret_cast<const std::uint32_t*>(controller + kControllerFirstSlot);
                    const std::uint8_t* units = *reinterpret_cast<const std::uint8_t* const*>(controller + kControllerUnits);
                    for (std::uint32_t i = 0; i < unitCount && i < kMaxUnits; ++i)
                    {
                        if (*reinterpret_cast<const void* const*>(units + i * kUnitSize + kUnitAnimControl) != animControl)
                            continue;
                        const std::uint32_t slot = firstSlot + i;
                        if (slot >= slotCount || soldierOfSlot[slot] == kEmptySlot)
                            return kNoSoldier;
                        return soldierOfSlot[slot];
                    }
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
        return kNoSoldier;
    }

    bool WeaponInHandSEH(const std::uint8_t* hub, std::uint32_t soldier, std::uint16_t& equipId)
    {
        __try
        {
            const std::uint8_t* table = *reinterpret_cast<std::uint8_t* const*>(hub + kHubEquipTable);
            const std::uint8_t handSlot = (*reinterpret_cast<const std::uint8_t* const*>(table + kEquipHandSlots))[soldier * 8 + 6];
            equipId = (*reinterpret_cast<const std::uint16_t* const*>(table + kEquipIds))[soldier * 3 + handSlot];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint64_t GzClipFor(std::uint64_t clip)
    {
        switch (clip)
        {
        case kSquatDownClip:  { static const std::uint64_t id = FoxHashes::ClipPathId(kGzSquatDown);  return id; }
        case kSquatIdleClip:  { static const std::uint64_t id = FoxHashes::ClipPathId(kGzSquatIdle);  return id; }
        case kStandUpClip:    { static const std::uint64_t id = FoxHashes::ClipPathId(kGzStandUp);    return id; }
        case kRadioStartClip: { static const std::uint64_t id = FoxHashes::ClipPathId(kGzRadioStart); return id; }
        case kRadioLoopClip:  { static const std::uint64_t id = FoxHashes::ClipPathId(kGzRadioLoop);  return id; }
        case kRadioEndClip:   { static const std::uint64_t id = FoxHashes::ClipPathId(kGzRadioEnd);   return id; }
        default:              return 0;
        }
    }

    void __fastcall hk_SetSubActionParamWithNodeEvent(void* routeAi, std::uint32_t soldierIndex, std::uint8_t* command, const std::uint8_t* nodeEvent)
    {
        const bool fob = MissionCodeGuard::ShouldBypassHooks();
        volatile std::uint8_t* swapFlag = g_CautionSwapFlag.load(std::memory_order_acquire);
        if (swapFlag && *swapFlag != (fob ? 0 : 1))
            *swapFlag = fob ? 0 : 1;
        if (fob)
        {
            g_OrigSetSubActionParam(routeAi, soldierIndex, command, nodeEvent);
            return;
        }
        g_OrigSetSubActionParam(routeAi, soldierIndex, command, nodeEvent);
        g_SoldierHub.store(*reinterpret_cast<std::uint8_t**>(static_cast<std::uint8_t*>(routeAi) + kRouteAiSoldiers), std::memory_order_release);
        const std::uint32_t event = QueryNodeEvent(nodeEvent, kEventTypeQuery);
        const bool squat = event == kSquatIdleAct || event == kSquatIdle;
        if (soldierIndex < kMaxSoldiers)
            g_RifleSquat[soldierIndex].store(squat && QueryNodeEventParam(nodeEvent, kActParam) == kSquatActRifle, std::memory_order_release);
        if (!squat)
            return;
        std::memcpy(command, &kStanceSquat, sizeof(kStanceSquat)); // an idle command starts with the stance
        if (static_cast<std::uint8_t>(QueryNodeEvent(nodeEvent, kLookTargetQuery)) != 0)
            LookAtNodeTarget(routeAi, command, nodeEvent);
    }

    CaveCode BuildCautionSwapThunk(std::uintptr_t cautionSwap, std::uintptr_t eventDispatch)
    {
        CaveCode code;
        code.Emit({ 0x3D, 0x47, 0xDD, 0x94, 0xEF });       // cmp eax,RelaxedIdleAct
        const std::size_t notRelaxedIdleAct = code.Jcc8(0x75);
        code.JmpAbs(cautionSwap);
        code.Bind8(notRelaxedIdleAct);
        code.Emit({ 0x80, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00 }); // cmp byte [rip+flag],0: the flag byte sits at the end of the thunk, off in FOB
        const std::size_t flagDisp = code.Pos() - 5;
        const std::size_t swapOff = code.Jcc8(0x74);
        code.Emit({ 0x3D, 0x2E, 0xEE, 0x82, 0x80 });       // cmp eax,SquatIdle
        const std::size_t isSquatIdle = code.Jcc8(0x74);
        code.Emit({ 0x3D, 0x45, 0x56, 0x3F, 0x41 });       // cmp eax,SquatIdleAct
        const std::size_t notSquat = code.Jcc8(0x75);
        code.Bind8(isSquatIdle);
        code.Emit({ 0x41, 0xBE, 0xA5, 0xAB, 0xE6, 0xAF }); // mov r14d,CautionSquatIdle: r14d is the id the dispatch switches on
        code.Bind8(notSquat);
        code.Bind8(swapOff);
        code.JmpAbs(eventDispatch);
        code.bytes.insert(code.bytes.end(), 64, 0x90);     // keep the flag byte off the cache line the code runs from
        const std::int32_t flagRel = static_cast<std::int32_t>(code.Pos() - (flagDisp + 5));
        std::memcpy(code.bytes.data() + flagDisp, &flagRel, sizeof(flagRel));
        code.Emit({ 0x00 });
        return code;
    }

    bool InstallCautionSwap(std::uint8_t* swapSite)
    {
        const std::uintptr_t site = reinterpret_cast<std::uintptr_t>(swapSite) + kSwapPatchOffset;
        const CaveCode code = BuildCautionSwapThunk(reinterpret_cast<std::uintptr_t>(swapSite) + kCautionSwap,
                                                    reinterpret_cast<std::uintptr_t>(swapSite) + kEventDispatch);
        auto* thunk = static_cast<std::uint8_t*>(HookArena::AllocateNear(site, code.bytes.size()));
        const std::int64_t rel = thunk
            ? static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(thunk)) - static_cast<std::int64_t>(site + 5)
            : 0;
        if (!thunk || rel < INT32_MIN || rel > INT32_MAX)
        {
            LogError("SquatIdleRoute: no code page near RouteAiImpl::StepEvent, SquatIdle/SquatIdleAct nodes stay relaxed in caution");
            return false;
        }
        std::memcpy(thunk, code.bytes.data(), code.bytes.size());
        FlushInstructionCache(GetCurrentProcess(), thunk, code.bytes.size());
        g_CautionSwapFlag.store(thunk + code.bytes.size() - 1, std::memory_order_release);

        g_CautionSwapPatch = NativeCodePatch{};
        g_CautionSwapPatch.target = reinterpret_cast<void*>(site);
        g_CautionSwapPatch.size = 5;
        std::memcpy(g_CautionSwapPatch.expected, kCautionSwapSite + kSwapPatchOffset, 5);
        g_CautionSwapPatch.replacement[0] = 0xE9;
        const std::int32_t rel32 = static_cast<std::int32_t>(rel);
        std::memcpy(g_CautionSwapPatch.replacement + 1, &rel32, sizeof(rel32));

        for (int attempt = 0; attempt < 50; ++attempt)
        {
            if (InstallNativeCodePatches(&g_CautionSwapPatch, 1))
                return true;
            if (!NativePatchErrorIsTransient(GetLastError()))
                break;
            Sleep(4);
        }
        LogError("SquatIdleRoute: RouteAiImpl::StepEvent patch refused (%s), SquatIdle/SquatIdleAct nodes stay relaxed in caution",
            NativePatchErrorName(GetLastError()));
        return false;
    }
}

bool Install_RouteAiImpl_SetSubActionParamWithNodeEvent_Hook()
{
    if (g_HookTarget)
        return true;
    const RouteSites sites = RouteSitesForBuild();
    if (!sites.setSubActionParam)
        return true;

    auto* setSubActionParam  = static_cast<std::uint8_t*>(ResolveGameAddress(sites.setSubActionParam));
    auto* setLookActionParam = static_cast<std::uint8_t*>(ResolveGameAddress(sites.setLookActionParam));
    auto* swapSite           = static_cast<std::uint8_t*>(ResolveGameAddress(sites.cautionSwap));
    const HookEntry setSubActionParamEntry  = CheckHookEntry(setSubActionParam, kSetSubActionParamPrologue, sizeof(kSetSubActionParamPrologue));
    const HookEntry setLookActionParamEntry = CheckHookEntry(setLookActionParam, kSetLookActionParamPrologue, sizeof(kSetLookActionParamPrologue));
    if (setSubActionParamEntry == HookEntry::Unknown
        || setLookActionParamEntry == HookEntry::Unknown
        || !BytesAtSEH(swapSite, kCautionSwapSite, sizeof(kCautionSwapSite)))
    {
        LogError("SquatIdleRoute: soldier route code does not match %s, SquatIdle/SquatIdleAct route nodes stay a standing idle",
            GetGameBuildName(gGameBuild));
        return false;
    }
    if (setSubActionParamEntry == HookEntry::ForeignHook)
        LogHookedOnTop("SquatIdleRoute: RouteAiImpl::SetSubActionParamWithNodeEvent", setSubActionParam);
    if (setLookActionParamEntry == HookEntry::ForeignHook)
        LogHookedOnTop("SquatIdleRoute: RouteAiImpl::SetLookActionParam", setLookActionParam);

    g_SetLookActionParam = reinterpret_cast<SetLookActionParam_t>(setLookActionParam);
    if (!CreateAndEnableHook(setSubActionParam, reinterpret_cast<void*>(&hk_SetSubActionParamWithNodeEvent),
                             reinterpret_cast<void**>(&g_OrigSetSubActionParam)))
    {
        LogError("SquatIdleRoute: RouteAiImpl::SetSubActionParamWithNodeEvent hook failed, SquatIdle/SquatIdleAct route nodes stay a standing idle");
        return false;
    }
    g_HookTarget = setSubActionParam;

    InstallCautionSwap(swapSite);
    return true;
}

std::uint64_t SquatIdleRoute_GzClip(std::uint64_t clip, void* animControl)
{
    const std::uint64_t gzClip = GzClipFor(clip);
    if (!gzClip || MissionCodeGuard::ShouldBypassHooks())
        return 0;
    const bool radioClip = clip == kRadioStartClip || clip == kRadioLoopClip || clip == kRadioEndClip;
    if (radioClip && !FeatureSwitch::IsOn(FeatureSwitch::Feature::LongBarrelRadio))
        return 0;
    static const std::uint64_t quietSquatIdle = FoxHashes::ClipPathId(kQuietSquatIdle);
    const std::uint64_t tppClip = clip == kSquatIdleClip ? quietSquatIdle : 0;
    const std::uint8_t* hub = g_SoldierHub.load(std::memory_order_acquire);
    if (!hub || !animControl)
        return tppClip;
    const std::uint32_t soldier = SoldierOfAnimControlSEH(hub, animControl);
    std::uint16_t equipId = 0;
    if (soldier >= kMaxSoldiers)
        return tppClip;
    if (clip == kStandUpClip)
        return SquatIdleRoute_PlaysGzSquat(animControl) ? gzClip : 0; // a next event on the same node clears the flag before he stands up
    if ((clip == kSquatDownClip || clip == kSquatIdleClip) && !g_RifleSquat[soldier].load(std::memory_order_acquire))
        return tppClip;
    if (!WeaponInHandSEH(hub, soldier, equipId)) // SquatActRifle and the radio both want a long barrel, otherwise TPP's own clips play
        return tppClip;
    return EquipParam_GetBarrelLengthForEquipId(equipId) == kBarrelLengthLong ? gzClip : tppClip;
}

void SquatIdleRoute_NoteLeafClip(void* animControl, std::uint64_t clip, std::uint64_t picked)
{
    if ((clip != kSquatDownClip && clip != kSquatIdleClip && clip != kStandUpClip) || !animControl)
        return; // the game picks each state's clip when that state first plays, so the squat-down comes before the loop
    const bool gz = picked == GzClipFor(clip);
    std::lock_guard<std::mutex> lock(g_GzSquatLock);
    for (auto& held : g_GzSquatControls)
    {
        if (held.load(std::memory_order_relaxed) != animControl)
            continue;
        if (!gz)
            held.store(nullptr, std::memory_order_release);
        return;
    }
    if (!gz)
        return;
    std::size_t slot = g_GzSquatNext;
    for (std::size_t i = 0; i < kMaxGzSquatControls; ++i)
    {
        if (!g_GzSquatControls[i].load(std::memory_order_relaxed))
        {
            slot = i;
            break;
        }
    }
    g_GzSquatControls[slot].store(animControl, std::memory_order_release);
    g_GzSquatNext = (slot + 1) % kMaxGzSquatControls;
}

bool SquatIdleRoute_PlaysGzSquat(const void* animControl)
{
    for (const auto& held : g_GzSquatControls)
    {
        if (held.load(std::memory_order_acquire) == animControl)
            return true;
    }
    return false;
}

std::uint32_t SquatIdleRoute_SoldierOfAnimControl(const void* animControl)
{
    const std::uint8_t* hub = g_SoldierHub.load(std::memory_order_acquire);
    return hub && animControl ? SoldierOfAnimControlSEH(hub, animControl) : kNoSoldier;
}

void SquatIdleRoute_Clear()
{
    g_SoldierHub.store(nullptr, std::memory_order_release);
    for (auto& rifle : g_RifleSquat)
        rifle.store(false, std::memory_order_relaxed);
}

bool Uninstall_RouteAiImpl_SetSubActionParamWithNodeEvent_Hook()
{
    SquatIdleRoute_Clear();
    if (g_CautionSwapPatch.installed)
        RemoveNativeCodePatches(&g_CautionSwapPatch, 1);
    g_CautionSwapFlag.store(nullptr, std::memory_order_release);
    if (g_HookTarget)
    {
        DisableAndRemoveHook(g_HookTarget);
        g_HookTarget = nullptr;
        g_OrigSetSubActionParam = nullptr;
    }
    return true;
}
