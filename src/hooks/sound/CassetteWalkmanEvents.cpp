#include "pch.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

#include "AddressSet.h"
#include "HookUtils.h"
#include "LuaBroadcaster.h"
#include "SoundSystemImpl_BeginSoundSystem.h"
#include "CassetteWalkmanEvents.h"
#include "log.h"

namespace
{
    constexpr const char*   kClass                 = "UI";
    constexpr std::uintptr_t kSpeakerModeVtblOffset = 0x1A0;

    using StopMusicPlayer_t = std::uint32_t* (__fastcall*)(void*, std::uint32_t*, std::uint32_t, std::uint8_t);
    using PauseResume_t     = int* (__fastcall*)(void*, int*, std::uint32_t);
    using SetSpeakerMode_t  = std::uint32_t* (__fastcall*)(void* player, std::uint32_t* outResult, std::uint32_t targetMode);

    std::atomic<std::uint32_t> g_currentTrackId{ 0 };
    thread_local bool          t_programmatic = false;

    StopMusicPlayer_t g_OrigStop    = nullptr;
    PauseResume_t     g_OrigPause   = nullptr;
    PauseResume_t     g_OrigResume  = nullptr;
    SetSpeakerMode_t  g_OrigSpeaker = nullptr;

    void* g_StopTarget    = nullptr;
    void* g_PauseTarget   = nullptr;
    void* g_ResumeTarget  = nullptr;
    void* g_SpeakerTarget = nullptr;

    struct Deferred { const char* msg; std::uint32_t a1; std::uint32_t a2; std::uint32_t a3; int argc; };

    std::mutex             g_queueMutex;
    std::vector<Deferred>  g_queue;

    std::uint32_t ByUser() { return t_programmatic ? 0u : 1u; }

    void Enqueue(const char* msg, std::uint32_t a1, std::uint32_t a2, std::uint32_t a3, int argc)
    {
        std::lock_guard<std::mutex> lk(g_queueMutex);
        if (g_queue.size() < 256)
            g_queue.push_back(Deferred{ msg, a1, a2, a3, argc });
    }

    std::uint32_t* __fastcall hk_Stop(void* player, std::uint32_t* outErr,
                                      std::uint32_t fadeMs, std::uint8_t stopByUser)
    {
        Enqueue("StopWalkMan", g_currentTrackId.load(std::memory_order_relaxed), ByUser(), 0, 2);
        return g_OrigStop ? g_OrigStop(player, outErr, fadeMs, stopByUser) : nullptr;
    }

    int* __fastcall hk_Pause(void* player, int* outErr, std::uint32_t fadeMs)
    {
        Enqueue("PauseWalkMan", g_currentTrackId.load(std::memory_order_relaxed), ByUser(), 0, 2);
        return g_OrigPause ? g_OrigPause(player, outErr, fadeMs) : nullptr;
    }

    int* __fastcall hk_Resume(void* player, int* outErr, std::uint32_t fadeMs)
    {
        Enqueue("StartWalkMan", g_currentTrackId.load(std::memory_order_relaxed), ByUser(), 0, 2);
        return g_OrigResume ? g_OrigResume(player, outErr, fadeMs) : nullptr;
    }

    std::uint32_t* __fastcall hk_Speaker(void* player, std::uint32_t* outResult, std::uint32_t targetMode)
    {
        Enqueue("SpeakerWalkMan", g_currentTrackId.load(std::memory_order_relaxed), targetMode, ByUser(), 3);
        return g_OrigSpeaker ? g_OrigSpeaker(player, outResult, targetMode) : nullptr;
    }

    void* ResolveSpeakerModeConcrete()
    {
        void* vtbl = ResolveGameAddress(gAddr.CassettePlayerVtable);
        if (!vtbl)
            return nullptr;
        __try
        {
            return *reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(vtbl) + kSpeakerModeVtblOffset);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }
}

void Set_CassetteWalkmanProgrammatic(bool programmatic)
{
    t_programmatic = programmatic;
}

void Emit_CassetteWalkmanStart(std::uint32_t trackId)
{
    g_currentTrackId.store(trackId, std::memory_order_relaxed);
    Enqueue("StartWalkMan", trackId, ByUser(), 0, 2);
}

void Drain_CassetteWalkmanEvents()
{
    static thread_local bool s_draining = false;
    if (s_draining)
        return;

    std::vector<Deferred> local;
    {
        std::lock_guard<std::mutex> lk(g_queueMutex);
        if (g_queue.empty())
            return;
        local.swap(g_queue);
    }

    s_draining = true;
    for (const Deferred& d : local)
    {
        if (d.argc >= 3)
            V_FrameWork::EmitMessage(kClass, d.msg, d.a1, d.a2, d.a3);
        else
            V_FrameWork::EmitMessage(kClass, d.msg, d.a1, d.a2);
    }
    s_draining = false;
}

bool Install_CassetteWalkmanEvents_Hook()
{
    void* stop = ResolveGameAddress(gAddr.StopMusicPlayer);
    if (stop && CreateAndEnableHook(stop, reinterpret_cast<void*>(&hk_Stop),
                                    reinterpret_cast<void**>(&g_OrigStop)))
        g_StopTarget = stop;
    else
        Log("[CassetteWalkman] WARN: StopMusicPlayer hook failed - StopWalkMan will not fire.\n");

    void* pause = ResolveGameAddress(gAddr.PauseMusicPlayer);
    if (pause && CreateAndEnableHook(pause, reinterpret_cast<void*>(&hk_Pause),
                                     reinterpret_cast<void**>(&g_OrigPause)))
        g_PauseTarget = pause;
    else
        Log("[CassetteWalkman] WARN: PauseMusicPlayer hook failed - PauseWalkMan will not fire.\n");

    void* resume = ResolveGameAddress(gAddr.ResumeMusicPlayer);
    if (resume && CreateAndEnableHook(resume, reinterpret_cast<void*>(&hk_Resume),
                                      reinterpret_cast<void**>(&g_OrigResume)))
        g_ResumeTarget = resume;
    else
        Log("[CassetteWalkman] WARN: ResumeMusicPlayer hook failed - resume StartWalkMan will not fire.\n");

    void* speaker = ResolveSpeakerModeConcrete();
    if (speaker && CreateAndEnableHook(speaker, reinterpret_cast<void*>(&hk_Speaker),
                                       reinterpret_cast<void**>(&g_OrigSpeaker)))
        g_SpeakerTarget = speaker;
    else
        Log("[CassetteWalkman] WARN: SetSpeakerMode hook failed - SpeakerWalkMan will not fire.\n");

    return true;
}

bool Uninstall_CassetteWalkmanEvents_Hook()
{
    if (g_StopTarget)    DisableAndRemoveHook(g_StopTarget);
    if (g_PauseTarget)   DisableAndRemoveHook(g_PauseTarget);
    if (g_ResumeTarget)  DisableAndRemoveHook(g_ResumeTarget);
    if (g_SpeakerTarget) DisableAndRemoveHook(g_SpeakerTarget);

    g_OrigStop = nullptr;
    g_OrigPause = nullptr;
    g_OrigResume = nullptr;
    g_OrigSpeaker = nullptr;
    g_StopTarget = g_PauseTarget = g_ResumeTarget = g_SpeakerTarget = nullptr;

    {
        std::lock_guard<std::mutex> lk(g_queueMutex);
        g_queue.clear();
    }
    return true;
}
