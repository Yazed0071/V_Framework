#include "pch.h"

#include <Windows.h>
#include <cstdint>
#include <intrin.h>
#include <unordered_map>
#include <mutex>

#include "HookUtils.h"
#include "log.h"
#include "LuaBroadcaster.h"
#include "MissionCodeGuard.h"
#include "GetGameObjectIdWithIndex.h"
#include "AddressSet.h"
#include "FieldTaxiMenu.h"

#pragma intrinsic(_ReturnAddress)

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

    static unsigned int       g_hiddenLz[64]   = {};
    static volatile int       g_hiddenLzCount  = 0;

    static unsigned short g_taxiMissions[32] = {};
    static volatile int   g_taxiMissionCount = 0;

    static volatile unsigned short g_lastFieldLoc = 0xFFFF;

    static constexpr unsigned int kEmptyRoute = 0xBF169F98u;
    static volatile int          g_carryIdx        = -1;
    static volatile bool         g_carryActive     = false;
    static volatile bool         g_carryWaiting    = false;
    static volatile int          g_carryWaitFrames = 0;
    static volatile bool         g_doorOpenAtDest  = false;
    static volatile bool         g_disembarkReady  = false;
    static volatile int          g_doorOpenFrames  = 0;
    static constexpr int         kDoorSlideFrames  = 60;
    static volatile unsigned long long g_arrivalTick = 0;
    static constexpr unsigned long long kDoorSlideMs = 1200;
    static constexpr unsigned long long kDoorForceMs = 5000;

    static volatile bool         g_pendingEmit = false;
    static volatile unsigned int g_emitCur     = 0;
    static volatile unsigned int g_emitDest    = 0;

    static volatile bool         g_taxiMapOpen  = false;

    static volatile unsigned int g_taxiRideState = 0x08;
    static volatile unsigned int g_taxiPose = 0x03;
    static volatile bool         g_taxiPoseLocked = false;
    static volatile bool         g_taxiRideLog   = false;
    static volatile bool         g_taxiRequested = false;
    static volatile unsigned int g_dbgCur  = 0;
    static volatile unsigned int g_dbgNext = 0;

    static unsigned short CurrentLocationId()
    {
        auto fn = reinterpret_cast<GetLocationId_t>(ResolveGameAddress(gAddr.HeliTaxi_GetLocationId));
        if (!fn) return 0xFFFF;
        __try { return fn(); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0xFFFF; }
    }

    static bool IsTaxiEnabled()
    {
        if (g_taxiMissionCount <= 0) return false;
        const unsigned short mc = MissionCodeGuard::GetCurrentMissionCode();
        for (int i = 0; i < g_taxiMissionCount; ++i)
            if (g_taxiMissions[i] == mc) return true;
        return false;
    }

    static bool Throttle(unsigned long long& last, unsigned long long ms)
    {
        const unsigned long long now = GetTickCount64();
        if (now - last > ms) { last = now; return true; }
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

    static void EmitTaxiRequest(unsigned int heliId, unsigned int curHash, unsigned int destHash)
    {
        V_FrameWork::EmitMessage("GameObject", "RequestedHeliTaxi", heliId, curHash, destHash);
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
            g_disembarkReady = false;
            g_doorOpenFrames = 0;
            g_arrivalTick    = GetTickCount64();
        }

        if (!IsTaxiEnabled())
            return;

        const unsigned short loc = CurrentLocationId();
        g_lastFieldLoc = loc;

        if (g_pendingEmit)
        {
            g_pendingEmit = false;
            const unsigned int heliId = GetGameObjectIdByIndex("TppHeli2", 0);
            EmitTaxiRequest(heliId, g_emitCur, g_emitDest);
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
                else if (++g_carryWaitFrames > 1800)
                {
                    g_carryWaiting = false;
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { g_carryWaiting = false; }
        }

        if (g_taxiMapOpen || g_carryWaiting)
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
            g_dbgCur = cur; g_dbgNext = next;
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
                g_disembarkReady = false;
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
        if ((!g_doorOpenAtDest && !(g_carryActive && g_taxiRideState == 0x03)) || proc != 5 || side > 1)
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
                if (phase == 3 || (g_carryActive && g_taxiRideState == 0x03))
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
        if ((!g_doorOpenAtDest && !(g_carryActive && g_taxiRideState == 0x03)) || proc != 5 || side > 1)
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
        if (g_doorOpenAtDest || (g_carryActive && g_taxiRideState == 0x03))
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
                g_doorOpenAtDest = false;
                g_disembarkReady = false;
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

    static void __fastcall hkExecPreMotion(void* self, unsigned int idx)
    {
        static bool s_prevCarry = false;
        const bool nowCarry = g_carryActive;
        if (s_prevCarry && !nowCarry) g_taxiRequested = false;
        if (!nowCarry) { g_taxiPoseLocked = false; g_taxiRideState = 0x08; }
        __try
        {
            const std::uintptr_t S      = reinterpret_cast<std::uintptr_t>(self);
            const std::uintptr_t a38    = *reinterpret_cast<std::uintptr_t*>(S + 0x38);
            const unsigned int   idxOff = static_cast<unsigned int>(*reinterpret_cast<int*>(a38 + 0x24));
            const std::uintptr_t plv    = *reinterpret_cast<std::uintptr_t*>(S + 0x78) + static_cast<std::uintptr_t>(idx - idxOff) * 0x1d0;
            const unsigned char  st     = *reinterpret_cast<unsigned char*>(plv + 0x10);
            const unsigned int   want   = g_taxiRideState;

            static int s_nullFrames = 0;
            if (st == 0x00) { ++s_nullFrames; } else s_nullFrames = 0;
            if (nowCarry)
            {
                if (g_taxiPoseLocked && want != 0 && st != want && st == 0x08)
                {
                    ForceRideState(S, plv, idx, static_cast<unsigned char>(want), StateFnViaMapper(S, want));
                }
            }
            else if (s_prevCarry && IsTaxiEnabled() && g_doorOpenAtDest && g_taxiRideState != 0x03
                     && (st == 0x0A || st == 0x0B))
            {
                ForceRideState(S, plv, idx, 0x0C, StateFnViaMapper(S, 0x0C));
            }

            if (nowCarry && g_taxiPoseLocked && want != 0 && want != 0x03)
            {
                const std::uintptr_t paxRec = *reinterpret_cast<std::uintptr_t*>(plv + 0x68);
                if (paxRec)
                {
                    unsigned short* doorCtrl = reinterpret_cast<unsigned short*>(paxRec + 0x1bc);
                    if (st == 0x03 && (*doorCtrl & 0x40) == 0)
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
            else if (g_carryActive && g_taxiRideState == 0x03 && st != 0x03 && st != 0x04 && st != 0x05 && s_edgeMotion != 0
                     && *reinterpret_cast<unsigned long long*>(plv + 0x70) != s_edgeMotion)
            {
                const std::uintptr_t sub78     = owner ? *reinterpret_cast<std::uintptr_t*>(owner + 0x78) : 0;
                const std::uintptr_t motionMgr = sub78 ? *reinterpret_cast<std::uintptr_t*>(sub78 + 0x238) : 0;
                if (motionMgr)
                {
                    void** vt = *reinterpret_cast<void***>(motionMgr);
                    auto play = reinterpret_cast<void(__fastcall*)(void*, unsigned int, int, unsigned long long, float)>(vt[50]);
                    play(reinterpret_cast<void*>(motionMgr), idx, 0, s_edgeMotion, 0.5f);
                    *reinterpret_cast<unsigned long long*>(plv + 0x70) = s_edgeMotion;
                    if (nodeSlot && s_edgeNode) *nodeSlot = s_edgeNode;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    static void __fastcall hkCallRescueHeli(void* utilTable, char call)
    {
        if (g_OrigCallRescueHeli) g_OrigCallRescueHeli(utilTable, call);

        if (!IsTaxiEnabled())
        {
            return;
        }
        g_lastFieldLoc = CurrentLocationId();
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
                        g_carryIdx        = reqIdx;
                        g_taxiRequested   = true;

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
        if (g_taxiMapOpen && g_hiddenLzCount > 0)
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
                        for (int k = 0; k < g_hiddenLzCount; ++k)
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
}

void FieldTaxi_SetMissionEnabled(unsigned int missionCode, bool enabled)
{
    const unsigned short mc = static_cast<unsigned short>(missionCode);
    int idx = -1;
    for (int i = 0; i < g_taxiMissionCount; ++i)
        if (g_taxiMissions[i] == mc) { idx = i; break; }

    if (enabled)
    {
        if (idx < 0 && g_taxiMissionCount < 32)
            g_taxiMissions[g_taxiMissionCount++] = mc;
    }
    else if (idx >= 0)
    {
        g_taxiMissions[idx] = g_taxiMissions[g_taxiMissionCount - 1];
        --g_taxiMissionCount;
    }
}

void FieldTaxi_SetTaxiRideState(unsigned int state)
{
    g_taxiPose = state;
}

void FieldTaxi_ResetTaxiState()
{
    g_carryActive    = false;
    g_carryWaiting   = false;
    g_taxiRequested  = false;
    g_taxiPoseLocked = false;
    g_taxiRideState  = 0x08;
    g_doorOpenAtDest = false;
    g_disembarkReady = false;
    g_taxiMapOpen    = false;
}

void FieldTaxi_SetTaxiRideLog(bool enabled)
{
    g_taxiRideLog = enabled;
}

void FieldTaxi_SetTaxiLandingZoneHidden(unsigned int lzNameHash, bool hidden)
{
    int idx = -1;
    for (int i = 0; i < g_hiddenLzCount; ++i)
        if (g_hiddenLz[i] == lzNameHash) { idx = i; break; }

    if (hidden)
    {
        if (idx < 0 && g_hiddenLzCount < 64)
            g_hiddenLz[g_hiddenLzCount++] = lzNameHash;
    }
    else if (idx >= 0)
    {
        g_hiddenLz[idx] = g_hiddenLz[g_hiddenLzCount - 1];
        --g_hiddenLzCount;
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
