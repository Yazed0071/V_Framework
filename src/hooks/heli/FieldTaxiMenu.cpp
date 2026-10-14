#include "pch.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <vector>

#include "HookUtils.h"
#include "log.h"
#include "LuaBroadcaster.h"
#include "MissionCodeGuard.h"
#include "GetGameObjectIdWithIndex.h"
#include "AddressSet.h"
#include "FieldTaxiMenu.h"
#include "AccIDroidPutAway.h"

namespace
{
    using GetLocationId_t   = unsigned short(__fastcall*)();
    using CanHeliTaxi_t     = char(__fastcall*)(void* self, char param2);
    using CallRescue_t      = void(__fastcall*)(void* utilTable, char call);
    using StepWithdraw_t    = void(__fastcall*)(void* self, unsigned int idx, int substate);
    using RequestMapPhase_t = void(__fastcall*)(void* self, unsigned int phase, char p3, char p4);
    using StepProc_t        = void(__fastcall*)(void* self, unsigned int idx, int proc);
    using PassengerUpd_t    = void(__fastcall*)(void* self);
    using MechaState_t      = void(__fastcall*)(void* self, unsigned int idx, unsigned int side, int proc);
    using IsPaxClosing_t    = unsigned long long(__fastcall*)(void* self, unsigned int idx);
    using PlacedBind_t      = void(__fastcall*)(void* thisSub, void* layer, unsigned int flag, unsigned long long hash);
    using RegisterLZMarker_t= void(__fastcall*)(void* self, char param2, float* pos);
    using GetQuarkTable_t   = void*(__fastcall*)();
    using ExecPreMotion_t   = void(__fastcall*)(void* self, unsigned int idx);
    using IsDisableMBTerminal_t = bool(__fastcall*)(void* self, unsigned int idx);
    using CheckActionState_t    = bool(__fastcall*)(void* self, unsigned int idx);
    using CallMenuUpdate_t      = void(__fastcall*)(void* self);
    using PlayerStatusCheck_t   = bool(__fastcall*)(void* player, unsigned int idx, unsigned int status);
    using PadSetMask_t          = std::uint64_t(__fastcall*)(void* pad, unsigned int owner, unsigned int buttons);

    static CanHeliTaxi_t     g_OrigCanHeliTaxi     = nullptr;
    static CallRescue_t      g_OrigCallRescueHeli  = nullptr;
    static StepWithdraw_t    g_OrigStepWithdraw    = nullptr;
    static RequestMapPhase_t g_OrigRequestMapPhase = nullptr;
    static StepProc_t        g_OrigStepGoToNav     = nullptr;
    static StepProc_t        g_OrigStepTaxiCurrent = nullptr;
    static PassengerUpd_t    g_OrigPassengerUpdate = nullptr;
    static MechaState_t      g_OrigMechaDoorClosed = nullptr;
    static MechaState_t      g_OrigMechaDoorOpen   = nullptr;
    static IsPaxClosing_t    g_OrigIsPaxClosingDoor = nullptr;
    static PlacedBind_t       g_OrigPlacedBind       = nullptr;
    static RegisterLZMarker_t g_OrigRegisterLZMarker = nullptr;
    static GetQuarkTable_t    g_GetQuarkSystemTable  = nullptr;
    static ExecPreMotion_t    g_OrigExecPreMotion    = nullptr;
    static IsDisableMBTerminal_t g_OrigIsDisableMBTerminal = nullptr;
    static void*              g_IsDisableMBTerminalTarget = nullptr;

    constexpr std::uint8_t kIsDisableMBTerminalPrologue[] = {
        0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48,
        0x8B, 0x41, 0x08, 0x8B, 0xEA, 0x8B, 0xF2, 0x4C, 0x8B, 0x40, 0x78, 0xBA, 0xC2, 0x00, 0x00, 0x00 }; // its OrCheck(0xC2, 0xC4) on the avatar's flags
    constexpr unsigned int kRideHidesIDroidFlag = 0x4; // flag 0xC2 (dword +0x18 bit 2), raised every frame by the inside and seat ride states
    constexpr unsigned int kRideBlocksIDroidFlag  = 0x80; // dword +8 bit 7: blocks the iDroid unless bit 5 is up too
    constexpr unsigned int kEdgeLetsIDroidOutFlag = 0x20; // dword +8 bit 5: only the edge ride state raises it
    constexpr unsigned int kIDroidActionOkFlag    = 0x800; // dword +0xc bit 11 (flag 0x6B), up all the time at the edge, lets the iDroid's action check pass

    static CheckActionState_t g_OrigCheckActionState   = nullptr;
    static void*              g_CheckActionStateTarget = nullptr;

    constexpr std::uint8_t kCheckActionStatePrologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48,
        0x8B, 0x41, 0x08, 0x8B, 0xFA, 0x8B, 0xDA, 0x4C, 0x8B, 0x40, 0x78, 0xBA, 0xC4, 0x00, 0x00, 0x00 }; // its OrCheck(0xC4, 0xC2) on the avatar's flags

    static CallMenuUpdate_t    g_OrigCallMenuUpdate    = nullptr;
    static void*               g_CallMenuUpdateTarget  = nullptr;
    static PlayerStatusCheck_t g_OrigPlayerStatusCheck = nullptr;
    static void**              g_PlayerStatusSlot      = nullptr;
    static PadSetMask_t        g_OrigPadSetMask        = nullptr;
    static void**              g_PadSetMaskSlot        = nullptr;
    static bool                g_CallMenuSlotsTried    = false;
    static thread_local bool   t_inCallMenuUpdate      = false;

    constexpr std::uint8_t kCallMenuUpdatePrologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
        0xD9, 0x48, 0x8B, 0x49, 0x18, 0xBA, 0xFF, 0xFF, 0x00, 0x00, 0x8B, 0xA9, 0x14, 0x02, 0x00, 0x00 }; // reads the local player index at +0x214
    constexpr unsigned int kRideHeliPadMaskOwner = 0xD6C2691Au; // hash of the pad mask the heli ride states set (real name unknown)
    constexpr unsigned int kCallButton           = 0x200;       // PlayerPad.CALL
    constexpr unsigned int kCallMenuStatusQuery  = 0x800;       // what the call menu asks the player before it opens (real name unknown)
    constexpr unsigned int kPlayerCanCallFlag    = 0x400;       // player +0x204 bit 10, the call menu needs it up
    constexpr unsigned int kCallInputAllowed     = 0x2;         // call input +8 bit 1
    constexpr unsigned int kCallInputBlocked     = 0x4;         // call input +8 bit 2

    static std::vector<unsigned int>   g_hiddenLz;
    static std::vector<unsigned short> g_taxiMissions;

    static constexpr unsigned int kEmptyRoute = 0xBF169F98u;
    static volatile bool         g_carryActive     = false;
    static volatile bool         g_carryWaiting    = false;
    static volatile int          g_carryWaitFrames = 0;
    static volatile bool         g_doorOpenAtDest  = false;
    static volatile unsigned long long g_arrivalTick = 0;
    static constexpr unsigned long long kDoorForceMs = 5000;
    static constexpr int         kRouteWaitFrames  = 120;   // the Lua answer comes through a deferred message, so give it frames and real time
    static constexpr unsigned long long kRouteWaitMs = 3000;
    static volatile unsigned long long g_pickTick  = 0;
    static constexpr float       kGlideBlendFrames = 20.0f; // for pose switches the game has no animation for
    static volatile bool         g_taxiStayAtLz    = false;
    static std::uintptr_t        g_pickClusterTable = 0;
    static int                   g_pickPrevCurIdx  = -1;

    static volatile bool         g_pendingEmit = false;
    static volatile unsigned int g_emitCur     = 0;
    static volatile unsigned int g_emitDest    = 0;

    static volatile bool         g_taxiMapOpen  = false;

    static volatile unsigned int g_taxiRideState = 0x08;
    static volatile unsigned int g_taxiPose = 0x03;
    static volatile bool         g_taxiPoseLocked = false;
    static volatile bool         g_taxiPoseChanged = false;
    static volatile bool         g_taxiRequested = false;

    static volatile unsigned char g_rideState = 0; // the player's ride state, refreshed every frame by hkExecPreMotion

    static bool HoldsDoorOpen(unsigned int pose)
    {
        return pose == 0x03 || pose == 0x06;
    }

    static bool HandOpensDoor(unsigned char rideState)
    {
        return rideState == 0x09 || rideState == 0x0C; // InsideDoorOpenToAtk, seat get-up: their clips slide the door open by hand
    }

    static unsigned short CurrentLocationId()
    {
        auto fn = reinterpret_cast<GetLocationId_t>(ResolveGameAddress(gAddr.HeliTaxi_GetLocationId));
        if (!fn) return 0xFFFF;
        __try { return fn(); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0xFFFF; }
    }

    static bool IsTaxiEnabled()
    {
        if (g_taxiMissions.empty() || MissionCodeGuard::ShouldBypassHooks()) return false;
        const unsigned short mc = MissionCodeGuard::GetCurrentMissionCode();
        for (std::size_t i = 0; i < g_taxiMissions.size(); ++i)
            if (g_taxiMissions[i] == mc) return true;
        return false;
    }

    static std::uintptr_t ClusterTableFromUtil(std::uintptr_t utilTable)
    {
        __try
        {
            const std::uintptr_t obj = *reinterpret_cast<std::uintptr_t*>(utilTable + 0x10);
            if (!obj) return 0;
            return *reinterpret_cast<std::uintptr_t*>(obj + 0xC8);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }

    static std::uint32_t SupportHeliId()
    {
        std::uint32_t heliId = 0;
        if (!GetGameObjectIdByName("TppHeli2", "SupportHeli", heliId))
            heliId = 0;
        return heliId;
    }

    static void EmitTaxiRequest(unsigned int heliId, unsigned int curHash, unsigned int destHash)
    {
        V_FrameWork::EmitMessage("GameObject", "RequestedHeliTaxi", heliId, curHash, destHash);
    }

    using HudGetInstance_t   = void* (__fastcall*)();
    using LangIdToKey_t      = void* (__fastcall*)(std::uint64_t* out, const char* langId);
    using GetLangText_t      = const char* (__fastcall*)(std::uint64_t key);
    using GetAnnounceLogSE_t = std::uint32_t (__fastcall*)(void* cdm, std::uint32_t keyLow);
    using AnnounceLogView_t  = std::uint64_t (__fastcall*)(void* cdm, const char* text, std::uint8_t type, std::uint8_t se, bool important);

    constexpr const char* kNoRouteLangId = "V_taxi_no_route";
    static volatile bool  g_noRouteNotice = false;

    // the same native call TppUiCommand.AnnounceLogViewLangId makes; type 0 is the category of a Color="1" lang entry
    static void ShowNoRouteNotice()
    {
        auto getHud   = reinterpret_cast<HudGetInstance_t>(ResolveGameAddress(gAddr.HudCommonDataManager_GetInstance));
        auto toKey    = reinterpret_cast<LangIdToKey_t>(ResolveGameAddress(gAddr.Ui_LangIdToKey));
        auto langText = reinterpret_cast<GetLangText_t>(ResolveGameAddress(gAddr.Ui_GetLangText));
        auto getSe    = reinterpret_cast<GetAnnounceLogSE_t>(ResolveGameAddress(gAddr.Hud_GetAnnounceLogSE));
        auto announce = reinterpret_cast<AnnounceLogView_t>(ResolveGameAddress(gAddr.HudCommonDataManager_AnnounceLogView));
        if (!getHud || !toKey || !langText || !getSe || !announce)
            return;
        __try
        {
            void* cdm = getHud();
            if (!cdm)
                return;
            std::uint64_t key = 0;
            toKey(&key, kNoRouteLangId);
            const char* text = key ? langText(key) : nullptr;
            if (!text || !*text)
                return;
            announce(cdm, text, 0, static_cast<std::uint8_t>(getSe(cdm, static_cast<std::uint32_t>(key))), true);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    static char __fastcall hkCanHeliTaxi(void* self, char param2)
    {
        if (IsTaxiEnabled())
        {
            return 1;
        }
        if (g_OrigCanHeliTaxi) return g_OrigCanHeliTaxi(self, param2);
        return 0;
    }

    static void __fastcall hkStepWithdraw(void* self, unsigned int idx, int substate)
    {
        if (g_OrigStepWithdraw) g_OrigStepWithdraw(self, idx, substate);

        if (g_carryActive)
        {
            g_carryActive    = false;
            g_doorOpenAtDest = (g_taxiRideState != 0x03);
            g_arrivalTick    = GetTickCount64();
        }

        if (!IsTaxiEnabled())
            return;

        if (g_pendingEmit)
        {
            g_pendingEmit = false;
            EmitTaxiRequest(SupportHeliId(), g_emitCur, g_emitDest);
        }

        if (g_carryWaiting)
        {
            __try
            {
                const std::uintptr_t S        = reinterpret_cast<std::uintptr_t>(self);
                const std::uintptr_t Ctrl     = *reinterpret_cast<std::uintptr_t*>(S + 0x70);
                const std::uintptr_t descBase = *reinterpret_cast<std::uintptr_t*>(*reinterpret_cast<std::uintptr_t*>(Ctrl + 0xd8) + 8);
                const unsigned int cur        = *reinterpret_cast<unsigned int*>(descBase + static_cast<std::uintptr_t>(idx) * 0x28 + 0x14);
                if (cur != kEmptyRoute && cur != 0)
                {
                    const std::uintptr_t stepArr = *reinterpret_cast<std::uintptr_t*>(S + 0x88);
                    auto nextStep                = reinterpret_cast<unsigned char*>(stepArr + static_cast<std::uintptr_t>(idx) * 0x14 + 0x12);
                    *nextStep = 4;
                    g_carryWaiting = false;
                }
                else if (++g_carryWaitFrames > kRouteWaitFrames && GetTickCount64() - g_pickTick > kRouteWaitMs)
                {
                    g_carryWaiting  = false;
                    g_taxiRequested = false;
                    g_taxiStayAtLz  = true;
                    if (g_pickClusterTable && g_pickPrevCurIdx >= 0)
                        *reinterpret_cast<int*>(g_pickClusterTable + 0xC28) = g_pickPrevCurIdx; // the LZ the heli is really at
                    LogWarn("FieldTaxiMenu: no route came for the taxi pick (LZ 0x%08X to 0x%08X), the heli waits at the LZ", g_emitCur, g_emitDest);
                    g_noRouteNotice = true;
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { g_carryWaiting = false; }
        }

        if (g_taxiMapOpen || g_carryWaiting || g_taxiStayAtLz)
        {
            __try
            {
                const std::uintptr_t S       = reinterpret_cast<std::uintptr_t>(self);
                const std::uintptr_t stepArr = *reinterpret_cast<std::uintptr_t*>(S + 0x88);
                *reinterpret_cast<float*>(stepArr + static_cast<std::uintptr_t>(idx) * 0x14) = g_carryWaiting ? 5.0f : 0.0f;
                auto nextStep = reinterpret_cast<unsigned char*>(stepArr + static_cast<std::uintptr_t>(idx) * 0x14 + 0x12);
                if (*nextStep == 0x0a || *nextStep == 0x0b || *nextStep == 0x0c)
                    *nextStep = 0x0e;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }

        if (substate != 0)
            return;

        __try
        {
            const std::uintptr_t s   = reinterpret_cast<std::uintptr_t>(self);
            const std::uintptr_t sys = *reinterpret_cast<std::uintptr_t*>(s + 0x70);
            const std::uintptr_t y   = *reinterpret_cast<std::uintptr_t*>(sys + 0x60);
            const std::uintptr_t arr = *reinterpret_cast<std::uintptr_t*>(y + 8);
            auto entry = reinterpret_cast<unsigned long long*>(arr + static_cast<std::uintptr_t>(idx) * 0x18);
            *entry |= 0x2001000000000ull;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    static void __fastcall hkStepGoToNav(void* self, unsigned int idx, int proc)
    {
        if (g_OrigStepGoToNav) g_OrigStepGoToNav(self, idx, proc);
        if (!g_carryActive)
            return;
        __try
        {
            const std::uintptr_t S       = reinterpret_cast<std::uintptr_t>(self);
            const std::uintptr_t stepArr = *reinterpret_cast<std::uintptr_t*>(S + 0x88);
            auto nextStep                = reinterpret_cast<unsigned char*>(stepArr + static_cast<std::uintptr_t>(idx) * 0x14 + 0x12);
            if (*nextStep == 0x0c)
            {
                *nextStep = 8;
                g_carryActive = false;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    static void __fastcall hkStepTaxiCurrentCluster(void* self, unsigned int idx, int proc)
    {
        if (g_OrigStepTaxiCurrent) g_OrigStepTaxiCurrent(self, idx, proc);
        if (proc != 3)
            return;
        if (!g_taxiRequested)
        {
            return;
        }
        __try
        {
            const std::uintptr_t S        = reinterpret_cast<std::uintptr_t>(self);
            const std::uintptr_t Ctrl     = *reinterpret_cast<std::uintptr_t*>(S + 0x70);
            const std::uintptr_t descBase = *reinterpret_cast<std::uintptr_t*>(*reinterpret_cast<std::uintptr_t*>(Ctrl + 0xd8) + 8);
            const unsigned int cur   = *reinterpret_cast<unsigned int*>(descBase + static_cast<std::uintptr_t>(idx) * 0x28 + 0x14);
            const unsigned int relay = *reinterpret_cast<unsigned int*>(descBase + static_cast<std::uintptr_t>(idx) * 0x28 + 0x18);
            const unsigned int next  = *reinterpret_cast<unsigned int*>(descBase + static_cast<std::uintptr_t>(idx) * 0x28 + 0x1c);
            g_carryActive = true;
            const bool routeReady = (cur != kEmptyRoute && cur != 0) || (relay != kEmptyRoute && relay != 0) || (next != kEmptyRoute && next != 0);
            if (!g_taxiPoseLocked && routeReady)
            {
                g_taxiRideState = g_taxiPose;
                g_taxiPoseLocked = true;
            }
            const std::uintptr_t stepArr = *reinterpret_cast<std::uintptr_t*>(S + 0x88);
            auto nextStep = reinterpret_cast<unsigned char*>(stepArr + static_cast<std::uintptr_t>(idx) * 0x14 + 0x12);
            if ((relay == kEmptyRoute || relay == 0) && next != kEmptyRoute && next != 0 && *nextStep == 8)
            {
                if (IsTaxiEnabled()) *nextStep = 3;
                g_carryActive = true;
                g_doorOpenAtDest = false;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    static void __fastcall hkPassengerUpdate(void* self)
    {
        if (g_OrigPassengerUpdate) g_OrigPassengerUpdate(self);
        if (g_doorOpenAtDest && GetTickCount64() - g_arrivalTick > kDoorForceMs)
            g_doorOpenAtDest = false;
        if (!g_doorOpenAtDest)
            return;
        __try
        {
            const std::uintptr_t arr = *reinterpret_cast<std::uintptr_t*>(reinterpret_cast<std::uintptr_t>(self) + 0x40);
            if (!arr)
                return;
            auto ctrl = reinterpret_cast<unsigned short*>(arr + 0x1bc);
            *ctrl = static_cast<unsigned short>((*ctrl & ~static_cast<unsigned short>(0x0040 | 0x0100)) | static_cast<unsigned short>(0x0080));
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    static void __fastcall hkMechaDoorClosed(void* self, unsigned int idx, unsigned int side, int proc)
    {
        if (g_OrigMechaDoorClosed) g_OrigMechaDoorClosed(self, idx, side, proc);
        if ((!g_doorOpenAtDest && !(g_carryActive && HoldsDoorOpen(g_taxiRideState))) || proc != 5 || side > 1)
            return;
        __try
        {
            const std::uintptr_t p   = reinterpret_cast<std::uintptr_t>(self);
            const std::uintptr_t pc  = *reinterpret_cast<std::uintptr_t*>(*reinterpret_cast<std::uintptr_t*>(p + 0x68) + 0x58);
            const std::uintptr_t rec = *reinterpret_cast<std::uintptr_t*>(pc + 0x40) + static_cast<std::uintptr_t>(idx) * 0x1c0;
            const unsigned char phase = static_cast<unsigned char>(*reinterpret_cast<unsigned char*>(rec + 0x1be) & 3);
            const std::uintptr_t workEntry = *reinterpret_cast<std::uintptr_t*>(p + 0x80)
                + static_cast<std::uintptr_t>(idx - static_cast<unsigned int>(*reinterpret_cast<int*>(p + 0x88))) * 0x18;
            const std::uintptr_t layer = *reinterpret_cast<std::uintptr_t*>(workEntry + 8);
            auto e = reinterpret_cast<unsigned char*>(layer + static_cast<std::uintptr_t>(side) * 4);
            if (e[0] == 5)
            {
                if (phase == 3 || (g_carryActive && HoldsDoorOpen(g_taxiRideState) && !HandOpensDoor(g_rideState)))
                {
                    e[3] = static_cast<unsigned char>(e[3] & ~1);
                    e[2] = 2;
                    e[1] = 1;
                }
                else
                {
                    e[1] = 6;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    static void __fastcall hkMechaDoorOpen(void* self, unsigned int idx, unsigned int side, int proc)
    {
        if (proc == 5 && side <= 1 && g_carryActive && (g_taxiRideState == 0x08 || g_taxiRideState == 0x0A))
        {
            __try
            {
                const std::uintptr_t pp  = reinterpret_cast<std::uintptr_t>(self);
                const std::uintptr_t we  = *reinterpret_cast<std::uintptr_t*>(pp + 0x80)
                    + static_cast<std::uintptr_t>(idx - static_cast<unsigned int>(*reinterpret_cast<int*>(pp + 0x88))) * 0x18;
                const std::uintptr_t lyr = *reinterpret_cast<std::uintptr_t*>(we + 8);
                auto ee = reinterpret_cast<unsigned char*>(lyr + static_cast<std::uintptr_t>(side) * 4);
                ee[3] = static_cast<unsigned char>(ee[3] & ~1);
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        if (g_OrigMechaDoorOpen) g_OrigMechaDoorOpen(self, idx, side, proc);
        if ((!g_doorOpenAtDest && !(g_carryActive && HoldsDoorOpen(g_taxiRideState))) || proc != 5 || side > 1)
            return;
        __try
        {
            const std::uintptr_t p         = reinterpret_cast<std::uintptr_t>(self);
            const std::uintptr_t workEntry = *reinterpret_cast<std::uintptr_t*>(p + 0x80)
                + static_cast<std::uintptr_t>(idx - static_cast<unsigned int>(*reinterpret_cast<int*>(p + 0x88))) * 0x18;
            const std::uintptr_t layer = *reinterpret_cast<std::uintptr_t*>(workEntry + 8);
            auto e = reinterpret_cast<unsigned char*>(layer + static_cast<std::uintptr_t>(side) * 4);
            if (e[0] == 2)
            {
                e[2] = 2;
                e[1] = 6;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    static unsigned long long __fastcall hkIsPassengerClosingDoor(void* self, unsigned int idx)
    {
        if (g_doorOpenAtDest || (g_carryActive && HoldsDoorOpen(g_taxiRideState)))
            return 0;
        if (g_OrigIsPaxClosingDoor) return g_OrigIsPaxClosingDoor(self, idx);
        return 0;
    }

    static void __fastcall hkRequestMapPhase(void* self, unsigned int phase, char p3, char p4)
    {
        unsigned int usePhase = phase;
        if (phase == 0x16)
        {
            if (IsTaxiEnabled())
            {
                usePhase = 5;
                g_taxiMapOpen = true;
                g_taxiStayAtLz = false;
                g_doorOpenAtDest = false;
            }
        }
        else if (phase == 5 || phase == 1)
        {
            g_taxiMapOpen = false;
        }
        if (g_OrigRequestMapPhase) g_OrigRequestMapPhase(self, usePhase, p3, p4);
    }

    static void ForceRideState(std::uintptr_t self, std::uintptr_t plv, unsigned int idx, unsigned char newState, std::uintptr_t newFn)
    {
        if (!newFn) return;
        using StateFn = void(__fastcall*)(std::uintptr_t, unsigned int, int, int);
        auto fnSlot = reinterpret_cast<std::uintptr_t*>(plv);
        const std::uintptr_t thisOld = self + static_cast<std::uintptr_t>(static_cast<long long>(static_cast<int>(*reinterpret_cast<long long*>(plv + 8))));
        if (*fnSlot) reinterpret_cast<StateFn>(*fnSlot)(thisOld, idx, 3, 0);
        *fnSlot = newFn;
        *reinterpret_cast<unsigned int*>(plv + 0x1c8) &= 0xfffffffeu;
        *reinterpret_cast<long long*>(plv + 8) = 0;
        *reinterpret_cast<unsigned char*>(plv + 0x10) = newState;
        *reinterpret_cast<unsigned char*>(plv + 0x11) = 0;
        if (*fnSlot) reinterpret_cast<StateFn>(*fnSlot)(self, idx, 2, 0);
    }

    static std::uintptr_t StateFnViaMapper(std::uintptr_t self, unsigned int st)
    {
        using Fn = void*(__fastcall*)(void*, void*, unsigned int);
        static Fn fn = reinterpret_cast<Fn>(ResolveGameAddress(gAddr.RideHeliActionPluginImpl_GetStateFn));
        if (!fn) return 0;
        std::uintptr_t desc[2] = { 0, 0 };
        fn(desc, reinterpret_cast<void*>(self), st);
        return desc[0];
    }

    static void PlayRideMotion(std::uintptr_t owner, std::uintptr_t plv, unsigned int idx, unsigned long long motion, unsigned short* nodeSlot, unsigned short node, float blend)
    {
        const std::uintptr_t sub78     = owner ? *reinterpret_cast<std::uintptr_t*>(owner + 0x78) : 0;
        const std::uintptr_t motionMgr = sub78 ? *reinterpret_cast<std::uintptr_t*>(sub78 + 0x238) : 0;
        if (!motionMgr)
            return;
        void** vt = *reinterpret_cast<void***>(motionMgr);
        auto play = reinterpret_cast<void(__fastcall*)(void*, unsigned int, int, unsigned long long, float)>(vt[50]);
        play(reinterpret_cast<void*>(motionMgr), idx, 0, motion, blend);
        *reinterpret_cast<unsigned long long*>(plv + 0x70) = motion;
        if (nodeSlot && node) *nodeSlot = node;
    }

    static void GlideToRideState(std::uintptr_t self, std::uintptr_t plv, unsigned int idx, unsigned char newState)
    {
        ForceRideState(self, plv, idx, newState, StateFnViaMapper(self, newState));
        const unsigned long long motion = *reinterpret_cast<unsigned long long*>(plv + 0x70); // the idle the new state just asked for
        if (motion)
            PlayRideMotion(*reinterpret_cast<std::uintptr_t*>(self + 8), plv, idx, motion, nullptr, 0, kGlideBlendFrames);
    }

    static unsigned char RideStateOf(void* self, unsigned int idx)
    {
        __try
        {
            const std::uintptr_t S      = reinterpret_cast<std::uintptr_t>(self);
            const std::uintptr_t a38    = *reinterpret_cast<std::uintptr_t*>(S + 0x38);
            const unsigned int   idxOff = static_cast<unsigned int>(*reinterpret_cast<int*>(a38 + 0x24));
            const std::uintptr_t plv    = *reinterpret_cast<std::uintptr_t*>(S + 0x78) + static_cast<std::uintptr_t>(idx - idxOff) * 0x1d0;
            return *reinterpret_cast<unsigned char*>(plv + 0x10);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }

    static bool RideEndedEarly(unsigned char rideState)
    {
        return rideState == 0x0D || rideState == 0x0E || rideState == 0x0F; // GetOff, Dead, HeliBreakDead
    }

    static void __fastcall hkExecPreMotion(void* self, unsigned int idx)
    {
        AccIDroidPutAway_OnRideHeliPreMotion(self, idx);
        if ((g_carryActive || g_taxiStayAtLz) && RideEndedEarly(RideStateOf(self, idx)))
            FieldTaxi_ResetTaxiState();
        static bool s_prevCarry = false;
        const bool nowCarry = g_carryActive;
        if (s_prevCarry && !nowCarry) g_taxiRequested = false;
        if (!nowCarry) { g_taxiPoseLocked = false; g_taxiPoseChanged = false; g_taxiRideState = 0x08; }
        if (nowCarry && g_taxiPoseLocked && g_taxiRideState != g_taxiPose)
        {
            g_taxiRideState   = g_taxiPose;
            g_taxiPoseChanged = true;
        }
        __try
        {
            const std::uintptr_t S      = reinterpret_cast<std::uintptr_t>(self);
            const std::uintptr_t a38    = *reinterpret_cast<std::uintptr_t*>(S + 0x38);
            const unsigned int   idxOff = static_cast<unsigned int>(*reinterpret_cast<int*>(a38 + 0x24));
            const std::uintptr_t plv    = *reinterpret_cast<std::uintptr_t*>(S + 0x78) + static_cast<std::uintptr_t>(idx - idxOff) * 0x1d0;
            const unsigned char  st     = *reinterpret_cast<unsigned char*>(plv + 0x10);
            const unsigned int   want   = g_taxiRideState;
            g_rideState = st;

            static unsigned char s_prevState  = 0;
            static unsigned int  s_insidePose = 0x08;
            if (!nowCarry)
            {
                s_insidePose = 0x08;
            }
            else if (st == 0x08 && s_prevState == 0x06)
            {
                s_insidePose = 0x06;
            }
            else if (st == 0x08 && s_prevState == 0x07)
            {
                s_insidePose = 0x08;
                if (want == 0x06 && !g_taxiPoseChanged)
                    s_insidePose = 0x06;
            }
            s_prevState = st;

            if (nowCarry)
            {
                if (g_taxiPoseLocked && want != 0 && st == 0x08)
                {
                    const bool doorOpen = s_insidePose == 0x06;
                    if ((want == 0x03 && g_taxiPoseChanged && doorOpen) || ((want == 0x08 || want == 0x0A) && doorOpen))
                    {
                        GlideToRideState(S, plv, idx, 0x03);
                    }
                    else
                    {
                        unsigned char nextState = 0;
                        if (want == 0x03 && g_taxiPoseChanged)
                            nextState = 0x09;
                        else if (want == 0x06 && !doorOpen)
                            nextState = 0x09;
                        else if (want != 0x06 && want != 0x08)
                            nextState = static_cast<unsigned char>(want);
                        if (nextState)
                            ForceRideState(S, plv, idx, nextState, StateFnViaMapper(S, nextState));
                    }
                }
                else if (g_taxiPoseLocked && want == 0x08 && (st == 0x0A || st == 0x0B))
                {
                    GlideToRideState(S, plv, idx, 0x08);
                    s_insidePose = 0x08;
                }
                else if (g_taxiPoseLocked && want != 0 && want != 0x0A && (st == 0x0A || st == 0x0B))
                {
                    ForceRideState(S, plv, idx, 0x0C, StateFnViaMapper(S, 0x0C));
                }
                else if (g_taxiPoseLocked && want == 0x06 && g_taxiPoseChanged && st == 0x03)
                {
                    ForceRideState(S, plv, idx, 0x06, StateFnViaMapper(S, 0x06));
                }
            }
            else if (s_prevCarry && IsTaxiEnabled() && g_doorOpenAtDest && (st == 0x0A || st == 0x0B))
            {
                ForceRideState(S, plv, idx, 0x0C, StateFnViaMapper(S, 0x0C));
            }

            if (nowCarry && g_taxiPoseLocked && want != 0 && want != 0x03)
            {
                const std::uintptr_t paxRec = *reinterpret_cast<std::uintptr_t*>(plv + 0x68);
                if (paxRec)
                {
                    unsigned short* doorCtrl = reinterpret_cast<unsigned short*>(paxRec + 0x1bc);
                    if (st == 0x03 && (*doorCtrl & 0x40) == 0 && !(want == 0x06 && g_taxiPoseChanged))
                    {
                        *doorCtrl |= 0x40;
                    }
                    *doorCtrl = static_cast<unsigned short>(*doorCtrl & ~0x80);
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        s_prevCarry = nowCarry;
        if (g_OrigExecPreMotion) g_OrigExecPreMotion(self, idx);

        __try
        {
            const std::uintptr_t S      = reinterpret_cast<std::uintptr_t>(self);
            const std::uintptr_t a38    = *reinterpret_cast<std::uintptr_t*>(S + 0x38);
            const unsigned int   idxOff = static_cast<unsigned int>(*reinterpret_cast<int*>(a38 + 0x24));
            const std::uintptr_t plv    = *reinterpret_cast<std::uintptr_t*>(S + 0x78) + static_cast<std::uintptr_t>(idx - idxOff) * 0x1d0;
            const unsigned char  st     = *reinterpret_cast<unsigned char*>(plv + 0x10);
            const std::uintptr_t owner  = *reinterpret_cast<std::uintptr_t*>(S + 8);
            const std::uintptr_t pm     = owner ? *reinterpret_cast<std::uintptr_t*>(owner + 0x138) : 0;
            const std::uintptr_t ms     = pm ? *reinterpret_cast<std::uintptr_t*>(pm + 0x60) : 0;
            const std::uintptr_t nsb    = ms ? *reinterpret_cast<std::uintptr_t*>(ms + 0x18) : 0;
            auto nodeSlot = nsb ? reinterpret_cast<unsigned short*>(nsb + static_cast<std::uintptr_t>(idx) * 2) : nullptr;

            static unsigned long long s_edgeMotion = 0;
            static unsigned short     s_edgeNode   = 0;
            if (st == 0x03)
            {
                s_edgeMotion = *reinterpret_cast<unsigned long long*>(plv + 0x70);
                if (nodeSlot) s_edgeNode = *nodeSlot;
            }
            else if (g_carryActive && g_taxiRideState == 0x03 && !g_taxiPoseChanged && st != 0x03 && st != 0x04 && st != 0x05 && s_edgeMotion != 0
                     && *reinterpret_cast<unsigned long long*>(plv + 0x70) != s_edgeMotion)
            {
                PlayRideMotion(owner, plv, idx, s_edgeMotion, nodeSlot, s_edgeNode, 0.5f);
            }
            else if (g_carryActive && g_taxiRideState == 0x06 && !g_taxiPoseChanged && st == 0x07)
            {
                unsigned long long insideStart = 0xE0BCFDBE6D2Eull; // hash of the state that plays snaputh_q_pos_to_in_r (real name unknown)
                if (*reinterpret_cast<unsigned char*>(plv + 0x80) & 1)
                    insideStart = 0xA24480C6F3B7ull; // hash of the state that plays snaputh_q_pos_to_in (real name unknown)
                if (*reinterpret_cast<unsigned long long*>(plv + 0x70) != insideStart)
                    PlayRideMotion(owner, plv, idx, insideStart, nodeSlot, 0x63, 0.5f);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    static void __fastcall hkCallRescueHeli(void* utilTable, char call)
    {
        if (g_OrigCallRescueHeli) g_OrigCallRescueHeli(utilTable, call);

        if (!IsTaxiEnabled())
        {
            if (CurrentLocationId() == 0x32 && !MissionCodeGuard::ShouldBypassHooks())
            {
                __try
                {
                    const std::uintptr_t ct = ClusterTableFromUtil(reinterpret_cast<std::uintptr_t>(utilTable));
                    if (ct)
                    {
                        const int reqIdx = *reinterpret_cast<int*>(ct + 0xC24);
                        if (reqIdx >= 0 && reqIdx < 0x40)
                            g_taxiRequested = true;
                    }
                }
                __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
            return;
        }
        __try
        {
            const std::uintptr_t ct = ClusterTableFromUtil(reinterpret_cast<std::uintptr_t>(utilTable));
            if (ct)
            {
                const int reqIdx = *reinterpret_cast<int*>(ct + 0xC24);
                if (reqIdx >= 0 && reqIdx < 0x40)
                {
                    const std::uintptr_t entry = ct + 0x10 + static_cast<std::uintptr_t>(reqIdx) * 0x30;
                    if (g_taxiMapOpen)
                    {
                        const unsigned int destHash = *reinterpret_cast<unsigned int*>(entry + 0x18);
                        unsigned int curHash = 0;
                        const int curIdx = *reinterpret_cast<int*>(ct + 0xC28);
                        if (curIdx >= 0 && curIdx < 0x40)
                            curHash = *reinterpret_cast<unsigned int*>(ct + 0x10 + static_cast<std::uintptr_t>(curIdx) * 0x30 + 0x18);
                        g_carryWaiting    = true;
                        g_carryWaitFrames = 0;
                        g_pickTick        = GetTickCount64();
                        g_taxiRequested   = true;
                        g_taxiStayAtLz    = false;
                        g_pickClusterTable = ct;
                        g_pickPrevCurIdx  = curIdx;

                        *reinterpret_cast<int*>(ct + 0xC28)           = reqIdx;
                        *reinterpret_cast<unsigned char*>(ct + 0xC2D) = 0;
                        g_emitCur = curHash; g_emitDest = destHash; g_pendingEmit = true;
                    }
                    g_taxiMapOpen = false;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    static void __fastcall hkPlacedBind(void* thisSub, void* layer, unsigned int flag, unsigned long long hash)
    {
        if (!layer)
            return;
        __try
        {
            if (g_OrigPlacedBind) g_OrigPlacedBind(thisSub, layer, flag, hash);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    static void __fastcall hkRegisterLZMarker(void* self, char param2, float* pos)
    {
        static char s_lastP2 = -2;
        if (param2 != s_lastP2)
        {
            if (s_lastP2 == 1 && param2 == 0)
                g_taxiMapOpen = false;
            s_lastP2 = param2;
        }
        unsigned int restoreIdx[64];
        int restoreCount = 0;
        std::uintptr_t ct = 0;
        if (g_taxiMapOpen && !g_hiddenLz.empty())
        {
            __try
            {
                void* qt = g_GetQuarkSystemTable ? g_GetQuarkSystemTable() : nullptr;
                if (qt)
                    ct = *reinterpret_cast<std::uintptr_t*>(*reinterpret_cast<std::uintptr_t*>(reinterpret_cast<std::uintptr_t>(qt) + 0x98) + 0xc8);
                if (ct)
                {
                    const unsigned int n = *reinterpret_cast<unsigned char*>(ct + 0xc2c);
                    for (unsigned int i = 0; i < n && i < 64; ++i)
                    {
                        auto flag = reinterpret_cast<unsigned char*>(ct + 0x39 + static_cast<std::uintptr_t>(i) * 0x30);
                        if ((*flag & 1) == 0)
                            continue;
                        const unsigned int hash = *reinterpret_cast<unsigned int*>(ct + 0x28 + static_cast<std::uintptr_t>(i) * 0x30);
                        for (std::size_t k = 0; k < g_hiddenLz.size(); ++k)
                            if (g_hiddenLz[k] == hash)
                            {
                                *flag = static_cast<unsigned char>(*flag & ~1u);
                                restoreIdx[restoreCount++] = i;
                                break;
                            }
                    }
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { restoreCount = 0; }
        }

        if (g_OrigRegisterLZMarker) g_OrigRegisterLZMarker(self, param2, pos);

        __try
        {
            for (int k = 0; k < restoreCount; ++k)
                *reinterpret_cast<unsigned char*>(ct + 0x39 + static_cast<std::uintptr_t>(restoreIdx[k]) * 0x30) |= 1u;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    static unsigned int* ActionFlagsOf(void* terminalPlugin, unsigned int idx)
    {
        __try
        {
            const std::uintptr_t owner  = *reinterpret_cast<std::uintptr_t*>(reinterpret_cast<std::uintptr_t>(terminalPlugin) + 8);
            const std::uintptr_t holder = *reinterpret_cast<std::uintptr_t*>(owner + 0x78);
            const std::uintptr_t table  = *reinterpret_cast<std::uintptr_t*>(holder + 0x280); // every avatar gets 0x20 bytes of action flags
            const unsigned int   first  = *reinterpret_cast<unsigned int*>(holder + 8);
            auto flags = reinterpret_cast<unsigned int*>(table + static_cast<std::uintptr_t>(idx - first) * 0x20);
            volatile unsigned int touch = flags[7];
            (void)touch;
            return flags;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    struct EdgeLikeIDroidFlags
    {
        unsigned int* flags;
        unsigned int  word2;
        unsigned int  word3;
        unsigned int  word6;
    };

    static bool LiftIDroidFlags(void* terminalPlugin, unsigned int idx, EdgeLikeIDroidFlags& saved)
    {
        saved.flags = ActionFlagsOf(terminalPlugin, idx);
        if (!saved.flags)
            return false;
        unsigned int* flags = saved.flags;
        saved.word2 = flags[2];
        saved.word3 = flags[3];
        saved.word6 = flags[6];
        flags[6] &= ~kRideHidesIDroidFlag;
        flags[3] |= kIDroidActionOkFlag;
        if (flags[2] & kRideBlocksIDroidFlag)
            flags[2] |= kEdgeLetsIDroidOutFlag;
        return true;
    }

    static void PutBackIDroidFlags(const EdgeLikeIDroidFlags& saved)
    {
        unsigned int* flags = saved.flags;
        flags[2] = (flags[2] & ~kEdgeLetsIDroidOutFlag) | (saved.word2 & kEdgeLetsIDroidOutFlag);
        flags[3] = (flags[3] & ~kIDroidActionOkFlag) | (saved.word3 & kIDroidActionOkFlag);
        flags[6] = (flags[6] & ~kRideHidesIDroidFlag) | (saved.word6 & kRideHidesIDroidFlag);
    }

    static bool __fastcall hkIsDisableMBTerminal(void* self, unsigned int idx)
    {
        EdgeLikeIDroidFlags saved = {};
        if (!g_carryActive || MissionCodeGuard::ShouldBypassHooks() || !LiftIDroidFlags(self, idx, saved))
            return g_OrigIsDisableMBTerminal(self, idx);

        const bool disabled = g_OrigIsDisableMBTerminal(self, idx);
        PutBackIDroidFlags(saved);
        return disabled;
    }

    static bool __fastcall hkCheckActionState(void* self, unsigned int idx)
    {
        EdgeLikeIDroidFlags saved = {};
        if (!g_carryActive || MissionCodeGuard::ShouldBypassHooks() || !LiftIDroidFlags(self, idx, saved))
            return g_OrigCheckActionState(self, idx);

        const bool ok = g_OrigCheckActionState(self, idx);
        PutBackIDroidFlags(saved);
        return ok;
    }

    static bool __fastcall hkPlayerStatusCheck(void* player, unsigned int idx, unsigned int status)
    {
        if (t_inCallMenuUpdate && status == kCallMenuStatusQuery)
            return false;
        return g_OrigPlayerStatusCheck(player, idx, status);
    }

    static std::uint64_t __fastcall hkPadSetMask(void* pad, unsigned int owner, unsigned int buttons)
    {
        if (owner == kRideHeliPadMaskOwner && g_carryActive && !MissionCodeGuard::ShouldBypassHooks())
            buttons &= ~kCallButton;
        return g_OrigPadSetMask(pad, owner, buttons);
    }

    static std::uintptr_t CallMenuPlayerOf(void* callMenu)
    {
        __try { return *reinterpret_cast<std::uintptr_t*>(reinterpret_cast<std::uintptr_t>(callMenu) + 0x18); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }

    static bool FindCallMenuSlots(std::uintptr_t player, void**& statusSlot, void**& maskSlot)
    {
        __try
        {
            void** playerVtable = *reinterpret_cast<void***>(player);
            const std::uintptr_t pad = *reinterpret_cast<std::uintptr_t*>(player + 0xb0);
            void** padVtable = *reinterpret_cast<void***>(pad);
            statusSlot = playerVtable + 0x120 / 8;
            maskSlot   = padVtable + 0xb0 / 8;
            return *statusSlot && *maskSlot;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    static void InstallCallMenuSlots(std::uintptr_t player)
    {
        if (g_CallMenuSlotsTried)
            return;
        g_CallMenuSlotsTried = true;
        void** statusSlot = nullptr;
        void** maskSlot   = nullptr;
        if (!FindCallMenuSlots(player, statusSlot, maskSlot))
        {
            LogError("FieldTaxiMenu: could not read the player and pad tables, the call menu stays closed on taxi rides");
            return;
        }
        if (SwapVtableSlot(statusSlot, reinterpret_cast<void*>(&hkPlayerStatusCheck), reinterpret_cast<void**>(&g_OrigPlayerStatusCheck)))
            g_PlayerStatusSlot = statusSlot;
        if (SwapVtableSlot(maskSlot, reinterpret_cast<void*>(&hkPadSetMask), reinterpret_cast<void**>(&g_OrigPadSetMask)))
            g_PadSetMaskSlot = maskSlot;
        if (!g_PlayerStatusSlot || !g_PadSetMaskSlot)
            LogError("FieldTaxiMenu: hooking the player or pad table failed, the call menu stays closed on taxi rides");
    }

    static bool CallMenuGates(std::uintptr_t player, unsigned int*& playerFlags, unsigned int*& inputFlags)
    {
        __try
        {
            playerFlags = reinterpret_cast<unsigned int*>(player + 0x204);
            const std::uintptr_t input = *reinterpret_cast<std::uintptr_t*>(player + 0xd8);
            inputFlags = reinterpret_cast<unsigned int*>(input + 8);
            volatile unsigned int touch = *playerFlags + *inputFlags;
            (void)touch;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    static void __fastcall hkCallMenuUpdate(void* self)
    {
        const std::uintptr_t player = CallMenuPlayerOf(self);
        if (player)
            InstallCallMenuSlots(player);

        unsigned int* playerFlags = nullptr;
        unsigned int* inputFlags  = nullptr;
        if (!g_carryActive || MissionCodeGuard::ShouldBypassHooks() || !player || !g_PlayerStatusSlot
            || !CallMenuGates(player, playerFlags, inputFlags))
        {
            g_OrigCallMenuUpdate(self);
            return;
        }

        const unsigned int savedPlayer = *playerFlags;
        const unsigned int savedInput  = *inputFlags;
        *playerFlags |= kPlayerCanCallFlag;
        *inputFlags   = (*inputFlags | kCallInputAllowed) & ~kCallInputBlocked;

        t_inCallMenuUpdate = true;
        g_OrigCallMenuUpdate(self);
        t_inCallMenuUpdate = false;

        *playerFlags = (*playerFlags & ~kPlayerCanCallFlag) | (savedPlayer & kPlayerCanCallFlag);
        *inputFlags  = (*inputFlags & ~(kCallInputAllowed | kCallInputBlocked)) | (savedInput & (kCallInputAllowed | kCallInputBlocked));
    }
}

void FieldTaxi_SetMissionEnabled(unsigned int missionCode, bool enabled)
{
    const unsigned short mc = static_cast<unsigned short>(missionCode);
    std::size_t idx = g_taxiMissions.size();
    for (std::size_t i = 0; i < g_taxiMissions.size(); ++i)
        if (g_taxiMissions[i] == mc) { idx = i; break; }

    if (enabled)
    {
        if (idx == g_taxiMissions.size())
            g_taxiMissions.push_back(mc);
    }
    else if (idx < g_taxiMissions.size())
    {
        g_taxiMissions[idx] = g_taxiMissions.back();
        g_taxiMissions.pop_back();
    }
}

void FieldTaxi_SetTaxiRideState(unsigned int state)
{
    g_taxiPose = state;
}

void FieldTaxi_ResetTaxiState()
{
    g_pendingEmit     = false;
    g_carryActive     = false;
    g_carryWaiting    = false;
    g_carryWaitFrames = 0;
    g_taxiRequested   = false;
    g_taxiPoseLocked  = false;
    g_taxiPoseChanged = false;
    g_taxiRideState   = 0x08;
    g_doorOpenAtDest  = false;
    g_taxiMapOpen     = false;
    g_taxiStayAtLz    = false;
    g_pickClusterTable = 0;
    g_pickPrevCurIdx  = -1;
    g_noRouteNotice   = false;
}

void FieldTaxi_PumpUi()
{
    if (!g_noRouteNotice)
        return;
    g_noRouteNotice = false;
    if (!MissionCodeGuard::ShouldBypassHooks())
        ShowNoRouteNotice();
}

void FieldTaxi_SetTaxiLandingZoneHidden(unsigned int lzNameHash, bool hidden)
{
    std::size_t idx = g_hiddenLz.size();
    for (std::size_t i = 0; i < g_hiddenLz.size(); ++i)
        if (g_hiddenLz[i] == lzNameHash) { idx = i; break; }

    if (hidden)
    {
        if (idx == g_hiddenLz.size())
            g_hiddenLz.push_back(lzNameHash);
    }
    else if (idx < g_hiddenLz.size())
    {
        g_hiddenLz[idx] = g_hiddenLz.back();
        g_hiddenLz.pop_back();
    }
}

bool Install_FieldTaxiMenu()
{
    void* canTaxi      = ResolveGameAddress(gAddr.HeliTaxi_CanHeliTaxi);
    void* callRescue   = ResolveGameAddress(gAddr.HeliTaxi_CallRescueHeli);
    void* stepWithdraw = ResolveGameAddress(gAddr.HeliTaxi_StepWithdraw);
    void* reqMapPhase  = ResolveGameAddress(gAddr.HeliTaxi_RequestMapPhase);
    void* stepGoToNav  = ResolveGameAddress(gAddr.HeliTaxi_StepGoToNav);
    void* stepTaxiCur  = ResolveGameAddress(gAddr.HeliTaxi_StepTaxiCurrentCluster);
    void* paxUpdate    = ResolveGameAddress(gAddr.HeliTaxi_PassengerUpdate);
    void* mechaClosed  = ResolveGameAddress(gAddr.MechaActionImpl_StateOff);
    void* mechaOpen    = ResolveGameAddress(gAddr.MechaActionImpl_StateOn);
    void* isPaxClosing = ResolveGameAddress(gAddr.PassengerControllerImpl_IsPassengerClosingDoor);
    void* placedBind   = ResolveGameAddress(gAddr.PlacedSystemImpl_BindResource);
    void* regLzMarker  = ResolveGameAddress(gAddr.UiMarkerCommonDataImpl_RegisterLZMarkerInUpdate);
    void* execPreMotion= ResolveGameAddress(gAddr.RideHeliActionPluginImpl_ExecPreMotionGraph);
    g_GetQuarkSystemTable = reinterpret_cast<GetQuarkTable_t>(ResolveGameAddress(gAddr.GetQuarkSystemTable));
    if (!canTaxi || !callRescue || !stepWithdraw || !reqMapPhase || !stepGoToNav || !stepTaxiCur || !paxUpdate || !mechaClosed || !mechaOpen || !isPaxClosing)
    {
        return false;
    }

    const bool ok1 = CreateAndEnableHook(canTaxi,      reinterpret_cast<void*>(&hkCanHeliTaxi),    reinterpret_cast<void**>(&g_OrigCanHeliTaxi));
    const bool ok2 = CreateAndEnableHook(callRescue,   reinterpret_cast<void*>(&hkCallRescueHeli), reinterpret_cast<void**>(&g_OrigCallRescueHeli));
    const bool ok3 = CreateAndEnableHook(stepWithdraw, reinterpret_cast<void*>(&hkStepWithdraw),   reinterpret_cast<void**>(&g_OrigStepWithdraw));
    const bool ok4 = CreateAndEnableHook(reqMapPhase,  reinterpret_cast<void*>(&hkRequestMapPhase),reinterpret_cast<void**>(&g_OrigRequestMapPhase));
    const bool ok5 = CreateAndEnableHook(stepGoToNav,  reinterpret_cast<void*>(&hkStepGoToNav),    reinterpret_cast<void**>(&g_OrigStepGoToNav));
    const bool ok6 = CreateAndEnableHook(stepTaxiCur,  reinterpret_cast<void*>(&hkStepTaxiCurrentCluster), reinterpret_cast<void**>(&g_OrigStepTaxiCurrent));
    const bool ok7 = CreateAndEnableHook(paxUpdate,    reinterpret_cast<void*>(&hkPassengerUpdate), reinterpret_cast<void**>(&g_OrigPassengerUpdate));
    const bool ok8 = CreateAndEnableHook(mechaClosed,  reinterpret_cast<void*>(&hkMechaDoorClosed), reinterpret_cast<void**>(&g_OrigMechaDoorClosed));
    const bool ok9 = CreateAndEnableHook(mechaOpen,    reinterpret_cast<void*>(&hkMechaDoorOpen),   reinterpret_cast<void**>(&g_OrigMechaDoorOpen));
    const bool ok10= CreateAndEnableHook(isPaxClosing, reinterpret_cast<void*>(&hkIsPassengerClosingDoor), reinterpret_cast<void**>(&g_OrigIsPaxClosingDoor));
    if (placedBind)  CreateAndEnableHook(placedBind,  reinterpret_cast<void*>(&hkPlacedBind),       reinterpret_cast<void**>(&g_OrigPlacedBind));
    if (regLzMarker) CreateAndEnableHook(regLzMarker, reinterpret_cast<void*>(&hkRegisterLZMarker), reinterpret_cast<void**>(&g_OrigRegisterLZMarker));
    if (execPreMotion) CreateAndEnableHook(execPreMotion, reinterpret_cast<void*>(&hkExecPreMotion), reinterpret_cast<void**>(&g_OrigExecPreMotion));
    void* isDisableTerminal = ResolveGameAddress(gAddr.MBTerminalActionPluginImpl_IsDisableMBTerminal);
    if (isDisableTerminal)
    {
        if (std::memcmp(isDisableTerminal, kIsDisableMBTerminalPrologue, sizeof(kIsDisableMBTerminalPrologue)) != 0)
            LogError("FieldTaxiMenu: iDroid check at %p is not the expected code, the iDroid stays blocked inside the heli on taxi rides", isDisableTerminal);
        else if (CreateAndEnableHook(isDisableTerminal, reinterpret_cast<void*>(&hkIsDisableMBTerminal), reinterpret_cast<void**>(&g_OrigIsDisableMBTerminal)))
            g_IsDisableMBTerminalTarget = isDisableTerminal;
    }
    void* checkActionState = ResolveGameAddress(gAddr.MBTerminalActionPluginImpl_CheckActionState);
    if (checkActionState)
    {
        if (std::memcmp(checkActionState, kCheckActionStatePrologue, sizeof(kCheckActionStatePrologue)) != 0)
            LogError("FieldTaxiMenu: iDroid action check at %p is not the expected code, the iDroid stays blocked inside the heli on taxi rides", checkActionState);
        else if (CreateAndEnableHook(checkActionState, reinterpret_cast<void*>(&hkCheckActionState), reinterpret_cast<void**>(&g_OrigCheckActionState)))
            g_CheckActionStateTarget = checkActionState;
    }
    void* callMenuUpdate = ResolveGameAddress(gAddr.CallMenuManagerImpl_Update);
    if (callMenuUpdate)
    {
        if (std::memcmp(callMenuUpdate, kCallMenuUpdatePrologue, sizeof(kCallMenuUpdatePrologue)) != 0)
            LogError("FieldTaxiMenu: call menu update at %p is not the expected code, the call menu stays closed on taxi rides", callMenuUpdate);
        else if (CreateAndEnableHook(callMenuUpdate, reinterpret_cast<void*>(&hkCallMenuUpdate), reinterpret_cast<void**>(&g_OrigCallMenuUpdate)))
            g_CallMenuUpdateTarget = callMenuUpdate;
    }
    AccIDroidPutAway_Install();

    return ok1 && ok2 && ok3 && ok4 && ok5 && ok6 && ok7 && ok8 && ok9 && ok10;
}

bool Uninstall_FieldTaxiMenu()
{
    DisableAndRemoveHook(ResolveGameAddress(gAddr.HeliTaxi_CanHeliTaxi));
    DisableAndRemoveHook(ResolveGameAddress(gAddr.HeliTaxi_CallRescueHeli));
    DisableAndRemoveHook(ResolveGameAddress(gAddr.HeliTaxi_StepWithdraw));
    DisableAndRemoveHook(ResolveGameAddress(gAddr.HeliTaxi_RequestMapPhase));
    DisableAndRemoveHook(ResolveGameAddress(gAddr.HeliTaxi_StepGoToNav));
    DisableAndRemoveHook(ResolveGameAddress(gAddr.HeliTaxi_StepTaxiCurrentCluster));
    DisableAndRemoveHook(ResolveGameAddress(gAddr.HeliTaxi_PassengerUpdate));
    DisableAndRemoveHook(ResolveGameAddress(gAddr.MechaActionImpl_StateOff));
    DisableAndRemoveHook(ResolveGameAddress(gAddr.MechaActionImpl_StateOn));
    DisableAndRemoveHook(ResolveGameAddress(gAddr.PassengerControllerImpl_IsPassengerClosingDoor));
    DisableAndRemoveHook(ResolveGameAddress(gAddr.PlacedSystemImpl_BindResource));
    DisableAndRemoveHook(ResolveGameAddress(gAddr.UiMarkerCommonDataImpl_RegisterLZMarkerInUpdate));
    DisableAndRemoveHook(ResolveGameAddress(gAddr.RideHeliActionPluginImpl_ExecPreMotionGraph));
    if (g_IsDisableMBTerminalTarget)
        DisableAndRemoveHook(g_IsDisableMBTerminalTarget);
    g_IsDisableMBTerminalTarget = nullptr;
    g_OrigIsDisableMBTerminal = nullptr;
    if (g_CheckActionStateTarget)
        DisableAndRemoveHook(g_CheckActionStateTarget);
    g_CheckActionStateTarget = nullptr;
    g_OrigCheckActionState = nullptr;
    if (g_CallMenuUpdateTarget)
        DisableAndRemoveHook(g_CallMenuUpdateTarget);
    g_CallMenuUpdateTarget = nullptr;
    g_OrigCallMenuUpdate = nullptr;
    RestoreVtableSlot(g_PlayerStatusSlot, reinterpret_cast<void*>(g_OrigPlayerStatusCheck));
    RestoreVtableSlot(g_PadSetMaskSlot, reinterpret_cast<void*>(g_OrigPadSetMask));
    g_PlayerStatusSlot = nullptr;
    g_PadSetMaskSlot = nullptr;
    g_OrigPlayerStatusCheck = nullptr;
    g_OrigPadSetMask = nullptr;
    g_CallMenuSlotsTried = false;
    g_OrigExecPreMotion = nullptr;
    AccIDroidPutAway_Uninstall();
    g_OrigCanHeliTaxi = nullptr;
    g_OrigCallRescueHeli = nullptr;
    g_OrigStepWithdraw = nullptr;
    g_OrigRequestMapPhase = nullptr;
    g_OrigStepGoToNav = nullptr;
    g_OrigStepTaxiCurrent = nullptr;
    g_OrigPassengerUpdate = nullptr;
    g_OrigMechaDoorClosed = nullptr;
    g_OrigMechaDoorOpen = nullptr;
    g_OrigIsPaxClosingDoor = nullptr;
    g_OrigPlacedBind = nullptr;
    g_OrigRegisterLZMarker = nullptr;
    return true;
}
