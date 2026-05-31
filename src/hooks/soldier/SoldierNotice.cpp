#include "pch.h"

#include <Windows.h>
#include <cstdint>
#include <unordered_map>
#include <mutex>

#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "AddressSet.h"
#include "GetGameObjectIdWithIndex.h"
#include "LuaBroadcaster.h"


using StepFn_t = void(__fastcall*)(void* self, std::uint32_t soldierIndex, int stepProc, void* knowledge);
using SpreadFn_t = void(__fastcall*)(void* self, std::uint32_t comradeIndex, std::uint32_t sourceIndex);
using CheckNoticeFn_t = void(__fastcall*)(void* self, std::uint32_t param1, void* param2, void* param3);
using SpeakVfunc20_t = bool(__fastcall*)(void* self, std::uint32_t id32, std::uint32_t a3, int a4, std::uint32_t lineId, float a6);

static constexpr int STEP_ENTER = 0;
static constexpr std::uint32_t kEvn220LineId = 0x8073EA46u; // EVN220
static constexpr std::uint64_t kSpeakCooldownMs = 8000;
static constexpr std::uint64_t kGlobalSpeakCooldownMs = 3000;

static StepFn_t g_OrigNoiseAware = nullptr;
static StepFn_t g_OrigIndisAware = nullptr;
static SpreadFn_t g_OrigSpread = nullptr;
static CheckNoticeFn_t g_OrigCheckSight = nullptr;
static thread_local bool g_spreadFromSight = false;

static std::unordered_map<std::uint64_t, std::uint64_t> g_SpeakCooldown;
static std::uint64_t g_lastSpeakTick = 0;
static std::mutex g_Mutex;


static __forceinline std::uintptr_t ReadQwordNoThrow(std::uintptr_t addr)
{
    __try { return *reinterpret_cast<std::uintptr_t*>(addr); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

static void MakeSoldierSpeakViaOwner(std::uintptr_t owner, std::uint32_t soldierId, std::uint32_t lineId)
{
    if (!owner) return;
    const std::uintptr_t obj = ReadQwordNoThrow(owner + 0xA8);
    if (!obj) return;
    const std::uintptr_t vtbl = ReadQwordNoThrow(obj);
    if (!vtbl) return;
    const std::uintptr_t fnp = ReadQwordNoThrow(vtbl + 0x20);
    if (!fnp) return;

    auto fn = reinterpret_cast<SpeakVfunc20_t>(fnp);
    __try { fn(reinterpret_cast<void*>(obj), soldierId, 0x95EA16B0u, 4, lineId, 0.0f); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static void EmitNotice(const char* msg, std::uint32_t soldierIndex)
{
    std::uint32_t objId = 0;
    if (GetSoldierGameObjectIdWithIndex(soldierIndex, objId))
        V_FrameWork::EmitMessage("GameObject", msg, objId);
}


static void __fastcall hk_CheckSightNoticeSoldier(void* self, std::uint32_t param1, void* param2, void* param3)
{
    if (!g_OrigCheckSight) return;
    if (MissionCodeGuard::ShouldBypassHooks()) { g_OrigCheckSight(self, param1, param2, param3); return; }

    g_spreadFromSight = true;
    g_OrigCheckSight(self, param1, param2, param3);
    g_spreadFromSight = false;
}

static void __fastcall hk_SpreadNotice(void* self, std::uint32_t comradeIndex, std::uint32_t sourceIndex)
{
    if (!g_OrigSpread) return;
    if (MissionCodeGuard::ShouldBypassHooks()) { g_OrigSpread(self, comradeIndex, sourceIndex); return; }

    g_OrigSpread(self, comradeIndex, sourceIndex);

    if (!g_spreadFromSight)
        return;

    const std::uint64_t now = GetTickCount64();
    const std::uint32_t pairLo = (sourceIndex < comradeIndex) ? sourceIndex : comradeIndex;
    const std::uint32_t pairHi = (sourceIndex < comradeIndex) ? comradeIndex : sourceIndex;
    const std::uint64_t pairKey = (static_cast<std::uint64_t>(pairLo) << 32) | pairHi;

    bool canSpeak = false;
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        const bool globalCooled = (g_lastSpeakTick != 0) && (now - g_lastSpeakTick) < kGlobalSpeakCooldownMs;
        auto it = g_SpeakCooldown.find(pairKey);
        const bool pairCooled = (it != g_SpeakCooldown.end()) && (now - it->second) < kSpeakCooldownMs;
        if (!globalCooled && !pairCooled)
        {
            canSpeak = true;
            g_SpeakCooldown[pairKey] = now;
            g_lastSpeakTick = now;
        }
    }

    if (canSpeak)
    {
        const std::uintptr_t owner = ReadQwordNoThrow(reinterpret_cast<std::uintptr_t>(self) + 0x58);
        MakeSoldierSpeakViaOwner(owner, comradeIndex, kEvn220LineId);
    }
}

static void __fastcall hk_NoiseAware(void* self, std::uint32_t soldierIndex, int stepProc, void* knowledge)
{
    if (!g_OrigNoiseAware) return;
    if (MissionCodeGuard::ShouldBypassHooks()) { g_OrigNoiseAware(self, soldierIndex, stepProc, knowledge); return; }

    g_OrigNoiseAware(self, soldierIndex, stepProc, knowledge);
    if (stepProc == STEP_ENTER)
        EmitNotice("NoticeNoise", soldierIndex);
}

static void __fastcall hk_IndisAware(void* self, std::uint32_t soldierIndex, int stepProc, void* knowledge)
{
    if (!g_OrigIndisAware) return;
    if (MissionCodeGuard::ShouldBypassHooks()) { g_OrigIndisAware(self, soldierIndex, stepProc, knowledge); return; }

    g_OrigIndisAware(self, soldierIndex, stepProc, knowledge);
    if (stepProc == STEP_ENTER)
        EmitNotice("NoticeIndis", soldierIndex);
}


static bool HookOne(uintptr_t addr, void* detour, void** orig)
{
    if (!addr) return true;
    void* target = ResolveGameAddress(addr);
    return target && CreateAndEnableHook(target, detour, orig);
}

bool Install_SoldierNotice_Hooks()
{
    Install_GetGameObjectIdWithIndex();

    bool ok = true;
    ok &= HookOne(gAddr.NoticeControllerImpl_CheckSightNoticeSoldier, reinterpret_cast<void*>(&hk_CheckSightNoticeSoldier), reinterpret_cast<void**>(&g_OrigCheckSight));
    ok &= HookOne(gAddr.NoticeControllerImpl_DoCheckSpreadNotice, reinterpret_cast<void*>(&hk_SpreadNotice), reinterpret_cast<void**>(&g_OrigSpread));
    ok &= HookOne(gAddr.NoticeNoiseAiImpl_StepAware, reinterpret_cast<void*>(&hk_NoiseAware), reinterpret_cast<void**>(&g_OrigNoiseAware));
    ok &= HookOne(gAddr.NoticeIndisAiImpl_StepAware, reinterpret_cast<void*>(&hk_IndisAware), reinterpret_cast<void**>(&g_OrigIndisAware));
    return ok;
}

bool Uninstall_SoldierNotice_Hooks()
{
    if (gAddr.NoticeControllerImpl_CheckSightNoticeSoldier) DisableAndRemoveHook(ResolveGameAddress(gAddr.NoticeControllerImpl_CheckSightNoticeSoldier));
    if (gAddr.NoticeControllerImpl_DoCheckSpreadNotice) DisableAndRemoveHook(ResolveGameAddress(gAddr.NoticeControllerImpl_DoCheckSpreadNotice));
    if (gAddr.NoticeNoiseAiImpl_StepAware) DisableAndRemoveHook(ResolveGameAddress(gAddr.NoticeNoiseAiImpl_StepAware));
    if (gAddr.NoticeIndisAiImpl_StepAware) DisableAndRemoveHook(ResolveGameAddress(gAddr.NoticeIndisAiImpl_StepAware));

    g_OrigCheckSight = nullptr;
    g_OrigSpread = nullptr;
    g_OrigNoiseAware = nullptr;
    g_OrigIndisAware = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_SpeakCooldown.clear();
        g_lastSpeakTick = 0;
    }
    return true;
}
