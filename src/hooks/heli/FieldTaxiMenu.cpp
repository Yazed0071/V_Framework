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
#include "FieldTaxiMenu.h"

#pragma intrinsic(_ReturnAddress)

namespace
{
    constexpr std::uintptr_t kAddr_CanHeliTaxi     = 0x1408E5DB0ull;
    constexpr std::uintptr_t kAddr_CallRescueHeli  = 0x140F0D8D0ull;
    constexpr std::uintptr_t kAddr_StepWithdraw    = 0x140E0CD90ull;
    constexpr std::uintptr_t kAddr_GetLocationId   = 0x140910890ull;
    constexpr std::uintptr_t kAddr_RequestMapPhase = 0x145C468F0ull;
    constexpr std::uintptr_t kAddr_StepGoToNav     = 0x140E096E0ull;
    constexpr std::uintptr_t kAddr_StepTaxiCurrent = 0x140E0A2F0ull;

    using GetLocationId_t   = unsigned short(__fastcall*)();
    using CanHeliTaxi_t     = char(__fastcall*)(void* self, char param2);
    using CallRescue_t      = void(__fastcall*)(void* utilTable, char call);
    using StepWithdraw_t    = void(__fastcall*)(void* self, unsigned int idx, int substate);
    using RequestMapPhase_t = void(__fastcall*)(void* self, unsigned int phase, char p3, char p4);
    using StepProc_t        = void(__fastcall*)(void* self, unsigned int idx, int proc);

    static CanHeliTaxi_t     g_OrigCanHeliTaxi     = nullptr;
    static CallRescue_t      g_OrigCallRescueHeli  = nullptr;
    static StepWithdraw_t    g_OrigStepWithdraw    = nullptr;
    static RequestMapPhase_t g_OrigRequestMapPhase = nullptr;
    static StepProc_t        g_OrigStepGoToNav     = nullptr;
    static StepProc_t        g_OrigStepTaxiCurrent = nullptr;

    static unsigned short g_taxiMissions[32] = {};
    static volatile int   g_taxiMissionCount = 0;

    static volatile unsigned short g_lastFieldLoc = 0xFFFF;

    static constexpr unsigned int kEmptyRoute = 0xBF169F98u;
    static volatile int          g_carryIdx        = -1;
    static volatile bool         g_carryActive     = false;
    static volatile bool         g_carryWaiting    = false;
    static volatile int          g_carryWaitFrames = 0;

    static volatile bool         g_pendingEmit = false;
    static volatile unsigned int g_emitCur     = 0;
    static volatile unsigned int g_emitDest    = 0;

    static volatile bool         g_taxiMapOpen  = false;

    static unsigned short CurrentLocationId()
    {
        auto fn = reinterpret_cast<GetLocationId_t>(ResolveGameAddress(kAddr_GetLocationId));
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
        void* ret = _ReturnAddress();
        const unsigned short loc = CurrentLocationId();
        static unsigned long long s_last = 0;
        if (Throttle(s_last, 1500))
        {
            const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
            const std::uintptr_t staticRet = reinterpret_cast<std::uintptr_t>(ret) - base + 0x140000000ull;
            Log("[FieldTaxiProbe] CanHeliTaxi called loc=0x%X param2=%d caller(static EN)=0x%llX force=%d\n",
                loc, (int)param2, static_cast<unsigned long long>(staticRet), IsTaxiEnabled() ? 1 : 0);
        }
        if (IsTaxiEnabled()) return 1;
        if (g_OrigCanHeliTaxi) return g_OrigCanHeliTaxi(self, param2);
        return 0;
    }

    static void __fastcall hkStepWithdraw(void* self, unsigned int idx, int substate)
    {
        if (g_OrigStepWithdraw) g_OrigStepWithdraw(self, idx, substate);

        const unsigned short loc = CurrentLocationId();
        if (!IsTaxiEnabled())
            return;
        g_lastFieldLoc = loc;

        if (g_pendingEmit)
        {
            g_pendingEmit = false;
            const unsigned int heliId = GetGameObjectIdByIndex("TppHeli2", 0);
            Log("[FieldTaxiProbe] emit RequestedHeliTaxi (deferred) heli=0x%08X cur=0x%08X dest=0x%08X\n", heliId, g_emitCur, g_emitDest);
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
                    const unsigned char prevStep = *nextStep;
                    *nextStep = 4;
                    g_carryWaiting = false;
                    Log("[FieldTaxiProbe] CARRY route=0x%08X nextStep %u->4 (Lua SetTaxiRoute)\n", cur, prevStep);
                }
                else if (++g_carryWaitFrames > 1800)
                {
                    g_carryWaiting = false;
                    Log("[FieldTaxiProbe] CARRY timeout (no SetTaxiRoute) -> releasing hold\n");
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { g_carryWaiting = false; Log("[FieldTaxiProbe] CARRY detect exception\n"); }
        }

        if (g_taxiMapOpen || g_carryWaiting)
        {
            __try
            {
                const std::uintptr_t S       = reinterpret_cast<std::uintptr_t>(self);
                const std::uintptr_t stepArr = *reinterpret_cast<std::uintptr_t*>(S + 0x88);
                *reinterpret_cast<float*>(stepArr + static_cast<std::uintptr_t>(idx) * 0x14) = 5.0f;
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
            const unsigned long long before = *entry;
            *entry |= 0x2001000000000ull;
            Log("[FieldTaxiProbe] step9 init loc=0x%X idx=%u LZflags before=0x%016llX after=0x%016llX (taxi bits forced)\n",
                loc, idx, before, *entry);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("[FieldTaxiProbe] step9 force-bits exception\n");
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
                Log("[FieldTaxiProbe] CARRY arrived idx=%u nextStep 0xC->8 (land+disembark)\n", idx);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    static void __fastcall hkStepTaxiCurrentCluster(void* self, unsigned int idx, int proc)
    {
        if (g_OrigStepTaxiCurrent) g_OrigStepTaxiCurrent(self, idx, proc);
        if (proc != 3 || !IsTaxiEnabled())
            return;
        __try
        {
            const std::uintptr_t S        = reinterpret_cast<std::uintptr_t>(self);
            const std::uintptr_t Ctrl     = *reinterpret_cast<std::uintptr_t*>(S + 0x70);
            const std::uintptr_t descBase = *reinterpret_cast<std::uintptr_t*>(*reinterpret_cast<std::uintptr_t*>(Ctrl + 0xd8) + 8);
            const unsigned int relay = *reinterpret_cast<unsigned int*>(descBase + static_cast<std::uintptr_t>(idx) * 0x28 + 0x18);
            const unsigned int next  = *reinterpret_cast<unsigned int*>(descBase + static_cast<std::uintptr_t>(idx) * 0x28 + 0x1c);
            const std::uintptr_t stepArr = *reinterpret_cast<std::uintptr_t*>(S + 0x88);
            auto nextStep = reinterpret_cast<unsigned char*>(stepArr + static_cast<std::uintptr_t>(idx) * 0x14 + 0x12);
            if ((relay == kEmptyRoute || relay == 0) && next != kEmptyRoute && next != 0 && *nextStep == 8)
            {
                *nextStep = 6;
                Log("[FieldTaxiProbe] no relay -> direct to nextClusterRoute (step 4->6) next=0x%08X\n", next);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    static void __fastcall hkRequestMapPhase(void* self, unsigned int phase, char p3, char p4)
    {
        unsigned int usePhase = phase;
        if (phase == 0x16)
        {
            const unsigned short loc = CurrentLocationId();
            if (IsTaxiEnabled())
            {
                usePhase = 5;
                g_taxiMapOpen = true;
                Log("[FieldTaxiProbe] RequestMapPhase SWAP 0x16->5 (TAXI) curLoc=0x%X p3=%d p4=%d\n", loc, (int)p3, (int)p4);
            }
            else
            {
                Log("[FieldTaxiProbe] RequestMapPhase TAXI(0x16) curLoc=0x%X (no swap)\n", loc);
            }
        }
        else if (phase == 5)
        {
            g_taxiMapOpen = false;
        }
        if (g_OrigRequestMapPhase) g_OrigRequestMapPhase(self, usePhase, p3, p4);
    }

    static void __fastcall hkCallRescueHeli(void* utilTable, char call)
    {
        if (g_OrigCallRescueHeli) g_OrigCallRescueHeli(utilTable, call);

        const unsigned short loc = CurrentLocationId();
        if (IsTaxiEnabled())
        {
            g_lastFieldLoc = loc;
            float x = 0, y = 0, z = 0; int reqIdx = -1, count = -1; unsigned int route = 0;
            bool emitPick = false;
            __try
            {
                const std::uintptr_t ut = reinterpret_cast<std::uintptr_t>(utilTable);
                const std::uintptr_t posObj = *reinterpret_cast<std::uintptr_t*>(ut + 0x90);
                if (posObj) { x = *reinterpret_cast<float*>(posObj + 0x20); y = *reinterpret_cast<float*>(posObj + 0x24); z = *reinterpret_cast<float*>(posObj + 0x28); }
                const std::uintptr_t ct = ClusterTableFromUtil(ut);
                if (ct)
                {
                    reqIdx = *reinterpret_cast<int*>(ct + 0xC24);
                    count  = *reinterpret_cast<unsigned char*>(ct + 0xC2C);
                    if (reqIdx >= 0 && reqIdx < 0x40)
                    {
                        const std::uintptr_t entry = ct + 0x10 + static_cast<std::uintptr_t>(reqIdx) * 0x30;
                        route = *reinterpret_cast<unsigned int*>(entry + 0x20);
                        const float cx = *reinterpret_cast<float*>(entry + 0);
                        const float cy = *reinterpret_cast<float*>(entry + 4);
                        const float cz = *reinterpret_cast<float*>(entry + 8);
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
                            *reinterpret_cast<int*>(ct + 0xC28)           = reqIdx;
                            *reinterpret_cast<unsigned char*>(ct + 0xC2D) = 0;
                            emitPick = true; g_emitCur = curHash; g_emitDest = destHash; g_pendingEmit = true;
                            Log("[FieldTaxiProbe] taxi pick LZ=%d cur=0x%08X dest=0x%08X pad=(%.1f,%.1f,%.1f) apprRoute=0x%08X -> emit RequestedHeliTaxi, await SetTaxiRoute\n",
                                reqIdx, curHash, destHash, cx, cy, cz, route);
                        }
                        g_taxiMapOpen = false;
                    }
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}

            Log("[FieldTaxiProbe] CallRescueHeli off-base loc=0x%X call=%d pickedPos=(%.1f,%.1f,%.1f) reqIdx=%d clusterCount=%d approachRoute=0x%08X taxiPick=%d\n",
                loc, (int)call, x, y, z, reqIdx, count, route, emitPick ? 1 : 0);
        }
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
    Log("[FieldTaxiProbe] mission %u enabled=%d (count=%d)\n", missionCode, enabled ? 1 : 0, g_taxiMissionCount);
}

bool Install_FieldTaxiMenu()
{
    void* canTaxi      = ResolveGameAddress(kAddr_CanHeliTaxi);
    void* callRescue   = ResolveGameAddress(kAddr_CallRescueHeli);
    void* stepWithdraw = ResolveGameAddress(kAddr_StepWithdraw);
    void* reqMapPhase  = ResolveGameAddress(kAddr_RequestMapPhase);
    void* stepGoToNav  = ResolveGameAddress(kAddr_StepGoToNav);
    void* stepTaxiCur  = ResolveGameAddress(kAddr_StepTaxiCurrent);
    if (!canTaxi || !callRescue || !stepWithdraw || !reqMapPhase || !stepGoToNav || !stepTaxiCur)
    {
        Log("[FieldTaxiProbe] resolve failed (canTaxi=%p callRescue=%p stepWithdraw=%p reqMapPhase=%p stepGoToNav=%p stepTaxiCur=%p)\n", canTaxi, callRescue, stepWithdraw, reqMapPhase, stepGoToNav, stepTaxiCur);
        return false;
    }

    const bool ok1 = CreateAndEnableHook(canTaxi,      reinterpret_cast<void*>(&hkCanHeliTaxi),    reinterpret_cast<void**>(&g_OrigCanHeliTaxi));
    const bool ok2 = CreateAndEnableHook(callRescue,   reinterpret_cast<void*>(&hkCallRescueHeli), reinterpret_cast<void**>(&g_OrigCallRescueHeli));
    const bool ok3 = CreateAndEnableHook(stepWithdraw, reinterpret_cast<void*>(&hkStepWithdraw),   reinterpret_cast<void**>(&g_OrigStepWithdraw));
    const bool ok4 = CreateAndEnableHook(reqMapPhase,  reinterpret_cast<void*>(&hkRequestMapPhase),reinterpret_cast<void**>(&g_OrigRequestMapPhase));
    const bool ok5 = CreateAndEnableHook(stepGoToNav,  reinterpret_cast<void*>(&hkStepGoToNav),    reinterpret_cast<void**>(&g_OrigStepGoToNav));
    const bool ok6 = CreateAndEnableHook(stepTaxiCur,  reinterpret_cast<void*>(&hkStepTaxiCurrentCluster), reinterpret_cast<void**>(&g_OrigStepTaxiCurrent));

    Log("[FieldTaxiProbe] CanHeliTaxi=%s CallRescue=%s StepWithdraw=%s RequestMapPhase=%s StepGoToNav=%s StepTaxiCurrent=%s\n",
        ok1 ? "OK" : "FAIL", ok2 ? "OK" : "FAIL", ok3 ? "OK" : "FAIL", ok4 ? "OK" : "FAIL", ok5 ? "OK" : "FAIL", ok6 ? "OK" : "FAIL");
    return ok1 && ok2 && ok3 && ok4 && ok5 && ok6;
}

bool Uninstall_FieldTaxiMenu()
{
    DisableAndRemoveHook(ResolveGameAddress(kAddr_CanHeliTaxi));
    DisableAndRemoveHook(ResolveGameAddress(kAddr_CallRescueHeli));
    DisableAndRemoveHook(ResolveGameAddress(kAddr_StepWithdraw));
    DisableAndRemoveHook(ResolveGameAddress(kAddr_RequestMapPhase));
    DisableAndRemoveHook(ResolveGameAddress(kAddr_StepGoToNav));
    DisableAndRemoveHook(ResolveGameAddress(kAddr_StepTaxiCurrent));
    g_OrigCanHeliTaxi = nullptr;
    g_OrigCallRescueHeli = nullptr;
    g_OrigStepWithdraw = nullptr;
    g_OrigRequestMapPhase = nullptr;
    g_OrigStepGoToNav = nullptr;
    g_OrigStepTaxiCurrent = nullptr;
    Log("[FieldTaxiProbe] removed\n");
    return true;
}
