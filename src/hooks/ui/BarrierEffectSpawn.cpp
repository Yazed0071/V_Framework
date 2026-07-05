#include "pch.h"
#include "BarrierEffectSpawn.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <intrin.h>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using UpdateFn    = void* (__fastcall*)(void* r15, std::uint64_t index, void* r8, void* r9);
    using GetItemIdFn = int   (__fastcall*)(void*, unsigned int, std::uint64_t, std::uint64_t);
    using SpawnFn     = int   (__fastcall*)(void*, void*);
    using BoolFn      = int   (__fastcall*)();

    static UpdateFn    g_Orig          = nullptr;
    static GetItemIdFn g_OrigGetItemId = nullptr;
    static SpawnFn     g_OrigSpawn     = nullptr;
    static BoolFn      g_IsFobMode     = nullptr;

    static std::atomic<bool> g_DesiredUp{ false };
    static std::atomic<bool> g_Failed{ false };
    static void* g_SetEquipItemCallRet = nullptr;
    static std::atomic<bool> g_ShieldUpCache{ false };
    static std::atomic<bool> g_ShieldDeployed{ false };
    static std::atomic<bool> g_PrevShieldActive{ false };

    static void* g_GitAddr = nullptr;
    static void* g_UpdAddr = nullptr;
    static void* g_SpnAddr = nullptr;

    constexpr std::uint64_t kPlayerIndex    = 0;
    constexpr int           kBarrierEquipId = 0x1E9;
    constexpr std::uint32_t kBit14          = 0x00004000u;
    constexpr std::uint32_t kBit18          = 0x00040000u;
    constexpr std::size_t   kSlots          = 256;
    constexpr int           kShieldEffectId = 0;

    static std::int32_t g_Arr808[kSlots];
    static std::int32_t g_Arr810[kSlots];
    static std::uint8_t g_Arr818[kSlots];
    static void*        g_Arr828[kSlots];
    static void*        g_Arr830[1024];
    alignas(16) static std::uint8_t g_SlotObj[0x100];
    static void* g_SlotVtbl[64];
    alignas(16) static std::uint8_t g_XformBuf[0x40];

    static void* __fastcall SlotGetXform(void*) { return g_XformBuf; }

    static void InitFakeStorage()
    {
        for (std::size_t i = 0; i < kSlots; ++i)
        {
            g_Arr808[i] = -1;
            g_Arr810[i] = -1;
            g_Arr818[i] = 0;
            g_Arr828[i] = &g_SlotObj;
        }
        std::memset(g_Arr830, 0, sizeof(g_Arr830));
        std::memset(g_SlotObj, 0, sizeof(g_SlotObj));
        std::memset(g_XformBuf, 0, sizeof(g_XformBuf));
        for (std::size_t i = 0; i < 64; ++i)
            g_SlotVtbl[i] = reinterpret_cast<void*>(&SlotGetXform);
        *reinterpret_cast<void**>(g_SlotObj) = g_SlotVtbl;
    }

    static bool InFob()
    {
        if (!g_IsFobMode) return false;
        __try { return g_IsFobMode() != 0; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    static int __fastcall hk_GetItemId(void* lmenu, unsigned int index, std::uint64_t a3, std::uint64_t a4)
    {
        int id = g_OrigGetItemId(lmenu, index, a3, a4);
        if (_ReturnAddress() == g_SetEquipItemCallRet)
            g_DesiredUp.store(id == kBarrierEquipId, std::memory_order_relaxed);
        return id;
    }

    static void* __fastcall hk_Update(void* r15, std::uint64_t index, void* r8, void* r9)
    {
        if (g_Failed.load(std::memory_order_relaxed) || index != kPlayerIndex || !r15)
            return g_Orig ? g_Orig(r15, index, r8, r9) : nullptr;

        if (InFob())
            return g_Orig ? g_Orig(r15, index, r8, r9) : nullptr;

        const bool desired = g_DesiredUp.load(std::memory_order_relaxed);
        std::uint32_t* sub204    = nullptr;
        std::uint32_t  oldSub204 = 0;
        bool           forced    = false;

        __try
        {
            const std::int32_t base = *reinterpret_cast<std::int32_t*>(reinterpret_cast<std::uint8_t*>(r15) + 8);
            const std::int64_t idx  = static_cast<std::int64_t>(index) - base;
            std::uint8_t* recBase = *reinterpret_cast<std::uint8_t**>(reinterpret_cast<std::uint8_t*>(r15) + 0x280);
            std::uint8_t* player  = *reinterpret_cast<std::uint8_t**>(reinterpret_cast<std::uint8_t*>(r15) + 0x408);
            if (idx >= 0 && idx < static_cast<std::int64_t>(kSlots) && recBase && player)
            {
                std::uint8_t* sub = *reinterpret_cast<std::uint8_t**>(player + 0x138);
                if (sub)
                {
                    std::uint32_t* field10 = reinterpret_cast<std::uint32_t*>(recBase + idx * 0x20 + 0x10);
                    sub204 = reinterpret_cast<std::uint32_t*>(sub + 0x204);
                    void** p808 = reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(r15) + 0x808);

                    bool shieldActive = false;
                    std::uint8_t* p60 = *reinterpret_cast<std::uint8_t**>(sub + 0x60);
                    std::uint8_t* ab  = p60 ? *reinterpret_cast<std::uint8_t**>(p60 + 0x8) : nullptr;
                    if (ab)
                        shieldActive = *reinterpret_cast<float*>(ab + static_cast<std::size_t>(kShieldEffectId) * 0x4c + 0x3c) > 0.0f;
                    g_ShieldUpCache.store(desired || shieldActive, std::memory_order_relaxed);
                    g_ShieldDeployed.store(shieldActive, std::memory_order_relaxed);

                    const bool prevShield = g_PrevShieldActive.exchange(shieldActive,
                                                                         std::memory_order_relaxed);
                    if (prevShield && !shieldActive)
                    {
                        __try
                        {
                            *reinterpret_cast<std::uint32_t*>(sub + 0x204) &= ~kBit18;
                            *reinterpret_cast<std::uint8_t*>(sub + 0x3C0)   &= 0xFEu; // clear bit 0
                            Log("[Barrier] shield down - cleared sticky bit18 of [sub+0x204] + bit 0 of [sub+0x3C0]\n");
                        }
                        __except (EXCEPTION_EXECUTE_HANDLER) {}
                    }

                    if (desired || shieldActive)
                    {
                        if (*p808 == nullptr)
                        {
                            *p808 = g_Arr808;
                            *reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(r15) + 0x810) = g_Arr810;
                            *reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(r15) + 0x818) = g_Arr818;
                            *reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(r15) + 0x828) = g_Arr828;
                            *reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(r15) + 0x830) = g_Arr830;
                        }
                        *field10 |= kBit14;
                        oldSub204 = *sub204;
                        *sub204 |= kBit18;
                        forced = true;
                    }
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            g_Failed.store(true, std::memory_order_relaxed);
            Log("[Barrier] ERROR: Energy Wall render faulted reading player state; render disabled to stay safe.\n");
            return g_Orig ? g_Orig(r15, index, r8, r9) : nullptr;
        }

        void* ret = nullptr;
        __try
        {
            ret = g_Orig ? g_Orig(r15, index, r8, r9) : nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            if (forced && sub204) *sub204 = oldSub204;
            g_Failed.store(true, std::memory_order_relaxed);
            Log("[Barrier] ERROR: engine effect update crashed while the Energy Wall was forced; reverted + render disabled.\n");
            return ret;
        }

        if (forced && sub204) *sub204 = oldSub204;
        return ret;
    }

    static int __fastcall hk_Spawn(void* mgr, void* param)
    {
        std::uint32_t slot = 0xFFFFFFFFu;
        __try { slot = *reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uint8_t*>(param) + 0x40); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}

        if (slot == 0x13 && !InFob())
        {
            __try
            {
                auto* m = reinterpret_cast<std::uint8_t*>(mgr);
                int*   ring    = *reinterpret_cast<int**>(m + 0x40);
                int    cap     = *reinterpret_cast<int*>(m + 0x48);
                int    rd      = *reinterpret_cast<int*>(m + 0x4c);
                int    wr      = *reinterpret_cast<int*>(m + 0x50);
                void** instArr = *reinterpret_cast<void***>(m + 0x20);
                if (ring && instArr && cap > 0 && rd == wr && rd >= 0 && rd + 1 < cap)
                {
                    int freeIdx = -1;
                    for (int i = 0; i < cap; ++i) if (instArr[i] == nullptr) { freeIdx = i; break; }
                    if (freeIdx >= 0)
                    {
                        ring[rd] = freeIdx;
                        *reinterpret_cast<int*>(m + 0x50) = rd + 1;
                    }
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        return g_OrigSpawn(mgr, param);
    }

}

bool Install_BarrierEffectSpawn()
{
    InitFakeStorage();

    g_IsFobMode = reinterpret_cast<BoolFn>(gAddr.Barrier_IsFobMode);
    if (!g_IsFobMode)
        Log("[Barrier] WARNING: no IsFobMode address for %s; the dome cannot stand down inside FOB missions (may render there too).\n", GetGameBuildName(gGameBuild));

    g_SetEquipItemCallRet = reinterpret_cast<void*>(gAddr.Barrier_EquipItemCallRet);
    if (!g_SetEquipItemCallRet)
        Log("[Barrier] WARNING: no equip return-address for %s; the dome may not trigger when the Energy Wall is equipped.\n", GetGameBuildName(gGameBuild));

    void* git = reinterpret_cast<void*>(gAddr.Barrier_GetItemId);
    const bool gitOk = git && CreateAndEnableHook(git, reinterpret_cast<void*>(&hk_GetItemId), reinterpret_cast<void**>(&g_OrigGetItemId));
    g_GitAddr = gitOk ? git : nullptr;
    if (!gitOk)
        Log("[Barrier] ERROR: could not hook equip detection (addr 0x%llX) for %s; the Energy Wall dome will never appear on this build.\n",
            static_cast<unsigned long long>(gAddr.Barrier_GetItemId), GetGameBuildName(gGameBuild));

    void* upd = reinterpret_cast<void*>(gAddr.Barrier_Updater);
    const bool updOk = upd && CreateAndEnableHook(upd, reinterpret_cast<void*>(&hk_Update), reinterpret_cast<void**>(&g_Orig));
    g_UpdAddr = updOk ? upd : nullptr;
    if (!updOk)
        Log("[Barrier] ERROR: could not hook the effect updater (addr 0x%llX) for %s; the Energy Wall dome will not render on this build.\n",
            static_cast<unsigned long long>(gAddr.Barrier_Updater), GetGameBuildName(gGameBuild));

    void* spn = reinterpret_cast<void*>(gAddr.Barrier_Pool);
    const bool spnOk = spn && CreateAndEnableHook(spn, reinterpret_cast<void*>(&hk_Spawn), reinterpret_cast<void**>(&g_OrigSpawn));
    g_SpnAddr = spnOk ? spn : nullptr;
    if (!spnOk)
        Log("[Barrier] WARNING: could not hook the effect pool (addr 0x%llX) for %s; the Energy Wall dome may not spawn outside FOB on this build.\n",
            static_cast<unsigned long long>(gAddr.Barrier_Pool), GetGameBuildName(gGameBuild));

    return gitOk && updOk;
}

void Uninstall_BarrierEffectSpawn()
{
    if (g_GitAddr) DisableAndRemoveHook(g_GitAddr);
    if (g_UpdAddr) DisableAndRemoveHook(g_UpdAddr);
    if (g_SpnAddr) DisableAndRemoveHook(g_SpnAddr);
    g_Orig          = nullptr;
    g_OrigGetItemId = nullptr;
    g_OrigSpawn     = nullptr;
    g_GitAddr = g_UpdAddr = g_SpnAddr = nullptr;
}

bool BarrierEffect_IsShieldActive()
{
    return g_ShieldUpCache.load(std::memory_order_relaxed);
}

bool BarrierEffect_IsShieldDeployed()
{
    return g_ShieldDeployed.load(std::memory_order_relaxed);
}
