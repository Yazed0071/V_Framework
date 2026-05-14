#include "pch.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <unordered_map>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "VoicePitchOverride.h"


namespace
{
    using SetPitch_t = void(__fastcall*)(void* self, float pitchCents);

    constexpr std::uintptr_t kResampler_BackToPitchNode  = 0x10;
    constexpr std::uintptr_t kPitchNode_PBI              = 0xD8;
    constexpr std::uintptr_t kPBI_RegisteredObj          = 0xA8;
    constexpr std::uintptr_t kRegisteredObj_AkObjId      = 0x70;

    static SetPitch_t        g_OrigSetPitch       = nullptr;
    static void*             g_HookTarget         = nullptr;
    static std::atomic<float> g_PitchBiasCents   { 0.0f };

    static std::unordered_map<std::uint64_t, float> g_BiasByAkObjId;
    static std::mutex g_BiasMapMutex;
    static std::atomic<bool> g_HavePerAkObjIdBias{ false };

    static void* SafeReadPtr(const void* base, std::uintptr_t off)
    {
        if (!base) return nullptr;
        __try
        {
            return *reinterpret_cast<void* const*>(
                reinterpret_cast<std::uintptr_t>(base) + off);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    static std::uint64_t SafeReadU64(const void* base, std::uintptr_t off)
    {
        if (!base) return 0;
        __try
        {
            return *reinterpret_cast<const std::uint64_t*>(
                reinterpret_cast<std::uintptr_t>(base) + off);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }


    static std::uint64_t ResolveAkObjIdFromResampler(void* resampler)
    {
        if (!resampler) return 0;

        const auto pitchNode = reinterpret_cast<const void*>(
            reinterpret_cast<std::uintptr_t>(resampler) - kResampler_BackToPitchNode);

        const void* pbi = SafeReadPtr(pitchNode, kPitchNode_PBI);
        if (!pbi) return 0;

        const void* regObj = SafeReadPtr(pbi, kPBI_RegisteredObj);
        if (!regObj) return 0;

        return SafeReadU64(regObj, kRegisteredObj_AkObjId);
    }


    static float LookupBiasForAkObjId(std::uint64_t akObjId)
    {
        if (!akObjId) return 0.0f;
        if (!g_HavePerAkObjIdBias.load(std::memory_order_relaxed)) return 0.0f;

        std::lock_guard<std::mutex> lock(g_BiasMapMutex);
        const auto it = g_BiasByAkObjId.find(akObjId);
        if (it == g_BiasByAkObjId.end()) return 0.0f;
        return it->second;
    }


    static void __fastcall hk_SetPitch(void* self, float pitchCents)
    {
        const float globalBias = g_PitchBiasCents.load(std::memory_order_relaxed);
        const bool  haveAnyPerObj =
            g_HavePerAkObjIdBias.load(std::memory_order_relaxed);

        std::uint64_t akObjId = 0;
        if (haveAnyPerObj && self)
            akObjId = ResolveAkObjIdFromResampler(self);

        float bias = globalBias;
        if (haveAnyPerObj && akObjId)
        {
            const float perObjBias = LookupBiasForAkObjId(akObjId);
            if (perObjBias != 0.0f)
                bias = perObjBias;
        }

        if (g_OrigSetPitch)
            g_OrigSetPitch(self, pitchCents + bias);
    }
}


void Set_GlobalVoicePitchBiasCents(float centsBias)
{
    g_PitchBiasCents.store(centsBias, std::memory_order_relaxed);
}


float Get_GlobalVoicePitchBiasCents()
{
    return g_PitchBiasCents.load(std::memory_order_relaxed);
}


void Set_PitchBiasForAkObjId(std::uint64_t akObjId, float centsBias)
{
    if (!akObjId) return;

    std::lock_guard<std::mutex> lock(g_BiasMapMutex);
    if (centsBias == 0.0f)
        g_BiasByAkObjId.erase(akObjId);
    else
        g_BiasByAkObjId[akObjId] = centsBias;
    g_HavePerAkObjIdBias.store(!g_BiasByAkObjId.empty(), std::memory_order_relaxed);
}


void Clear_PitchBiasForAkObjId(std::uint64_t akObjId)
{
    std::lock_guard<std::mutex> lock(g_BiasMapMutex);
    g_BiasByAkObjId.erase(akObjId);
    g_HavePerAkObjIdBias.store(!g_BiasByAkObjId.empty(), std::memory_order_relaxed);
}


void Clear_AllPerAkObjIdPitchBiases()
{
    std::lock_guard<std::mutex> lock(g_BiasMapMutex);
    g_BiasByAkObjId.clear();
    g_HavePerAkObjIdBias.store(false, std::memory_order_relaxed);
}


bool Install_VoicePitchOverride_Hook()
{
    const auto addr = gAddr.CAkResampler_SetPitch;
    if (!addr) return false;

    void* target = ResolveGameAddress(addr);
    if (!target) return false;

    const bool ok = CreateAndEnableHook(
        target,
        reinterpret_cast<void*>(&hk_SetPitch),
        reinterpret_cast<void**>(&g_OrigSetPitch));
    if (ok)
        g_HookTarget = target;
    return ok;
}


bool Uninstall_VoicePitchOverride_Hook()
{
    if (g_HookTarget)
    {
        DisableAndRemoveHook(g_HookTarget);
        g_HookTarget = nullptr;
        g_OrigSetPitch = nullptr;
    }
    g_PitchBiasCents.store(0.0f, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(g_BiasMapMutex);
        g_BiasByAkObjId.clear();
    }
    g_HavePerAkObjIdBias.store(false, std::memory_order_relaxed);
    return true;
}
