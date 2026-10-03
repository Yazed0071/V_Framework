#include "pch.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <intrin.h>

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
    constexpr std::uintptr_t kMusicTickSpan         = 0x1000;

    using StopMusicPlayer_t = std::uint32_t* (__fastcall*)(void*, std::uint32_t*, std::uint32_t, std::uint8_t);
    using PauseResume_t     = int* (__fastcall*)(void*, int*, std::uint32_t);
    using SetSpeakerMode_t  = std::uint32_t* (__fastcall*)(void* player, std::uint32_t* outResult, std::uint32_t targetMode);

    std::atomic<std::uint32_t> g_currentTrackId{ 0 };
    thread_local bool          t_programmatic = false;

    enum class WalkmanPlayState : std::uint32_t { Stopped = 0, Playing = 1, Paused = 2 };
    std::atomic<WalkmanPlayState> g_playState{ WalkmanPlayState::Stopped };

    StopMusicPlayer_t g_OrigStop    = nullptr;
    PauseResume_t     g_OrigPause   = nullptr;
    PauseResume_t     g_OrigResume  = nullptr;
    SetSpeakerMode_t  g_OrigSpeaker = nullptr;

    std::uintptr_t g_MusicTickBase = 0;

    void* g_StopTarget    = nullptr;
    void* g_PauseTarget   = nullptr;
    void* g_ResumeTarget  = nullptr;
    void* g_SpeakerTarget = nullptr;

    std::uint32_t ByUser() { return t_programmatic ? 0u : 1u; }

    template <typename... Args>
    void QueueWalkmanMessage(const char* message, Args... args)
    {
        const V_FrameWork::LuaBroadcastArg values[] = { V_FrameWork::LuaBroadcastArg(args)... };
        V_FrameWork::QueueMessageValues(kClass, message, values, sizeof...(Args));
    }

    bool IsMusicTickReturnAddress(const void* returnAddress)
    {
        if (!g_MusicTickBase || !returnAddress)
            return false;
        const std::uintptr_t ret =
            reinterpret_cast<std::uintptr_t>(returnAddress);
        return ret >= g_MusicTickBase
            && ret < g_MusicTickBase + kMusicTickSpan;
    }

    std::uint32_t* __fastcall hk_Stop(void* player, std::uint32_t* outErr,
                                      std::uint32_t fadeMs, std::uint8_t stopByUser)
    {
        const bool fromTick = IsMusicTickReturnAddress(_ReturnAddress());
        std::uint32_t* result = g_OrigStop ? g_OrigStop(player, outErr, fadeMs, stopByUser) : nullptr;
        if (!result || static_cast<std::int32_t>(*result) < 0)
            return result;

        if (g_playState.exchange(WalkmanPlayState::Stopped, std::memory_order_relaxed)
                != WalkmanPlayState::Stopped)
            QueueWalkmanMessage("StopWalkMan",
                g_currentTrackId.load(std::memory_order_relaxed),
                fromTick ? 0u : ByUser());
        return result;
    }

    int* __fastcall hk_Pause(void* player, int* outErr, std::uint32_t fadeMs)
    {
        int* result = g_OrigPause ? g_OrigPause(player, outErr, fadeMs) : nullptr;
        if (!result || *result < 0)
            return result;

        WalkmanPlayState expected = WalkmanPlayState::Playing;
        if (g_playState.compare_exchange_strong(expected, WalkmanPlayState::Paused,
                                                std::memory_order_relaxed))
            QueueWalkmanMessage("PauseWalkMan",
                g_currentTrackId.load(std::memory_order_relaxed), ByUser());
        return result;
    }

    int* __fastcall hk_Resume(void* player, int* outErr, std::uint32_t fadeMs)
    {
        int* result = g_OrigResume ? g_OrigResume(player, outErr, fadeMs) : nullptr;
        if (!result || *result < 0)
            return result;

        if (g_playState.exchange(WalkmanPlayState::Playing, std::memory_order_relaxed)
                != WalkmanPlayState::Playing)
            QueueWalkmanMessage("StartWalkMan",
                g_currentTrackId.load(std::memory_order_relaxed), ByUser());
        return result;
    }

    std::uint32_t* __fastcall hk_Speaker(void* player, std::uint32_t* outResult, std::uint32_t targetMode)
    {
        std::uint32_t* result = g_OrigSpeaker ? g_OrigSpeaker(player, outResult, targetMode) : nullptr;
        if (!result || static_cast<std::int32_t>(*result) < 0)
            return result;

        if (g_playState.load(std::memory_order_relaxed) != WalkmanPlayState::Stopped)
            QueueWalkmanMessage("SpeakerWalkMan",
                g_currentTrackId.load(std::memory_order_relaxed), targetMode, ByUser());
        return result;
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
    g_playState.store(WalkmanPlayState::Playing, std::memory_order_relaxed);
    QueueWalkmanMessage("StartWalkMan", trackId, ByUser());
}

bool Install_CassetteWalkmanEvents_Hook()
{
    g_MusicTickBase = reinterpret_cast<std::uintptr_t>(
        ResolveGameAddress(gAddr.UpdateMusicPlayer));
    if (!g_MusicTickBase)
        LogWarn("CassetteWalkman: UpdateMusicPlayer address missing, tape end reports StopWalkMan isStopByUser=1");

    void* stop = ResolveGameAddress(gAddr.StopMusicPlayer);
    if (stop && CreateAndEnableHook(stop, reinterpret_cast<void*>(&hk_Stop),
                                    reinterpret_cast<void**>(&g_OrigStop)))
        g_StopTarget = stop;
    else
        LogError("CassetteWalkman: StopMusicPlayer hook failed, StopWalkMan off");

    void* pause = ResolveGameAddress(gAddr.PauseMusicPlayer);
    if (pause && CreateAndEnableHook(pause, reinterpret_cast<void*>(&hk_Pause),
                                     reinterpret_cast<void**>(&g_OrigPause)))
        g_PauseTarget = pause;
    else
        LogError("CassetteWalkman: PauseMusicPlayer hook failed, PauseWalkMan off");

    void* resume = ResolveGameAddress(gAddr.ResumeMusicPlayer);
    if (resume && CreateAndEnableHook(resume, reinterpret_cast<void*>(&hk_Resume),
                                      reinterpret_cast<void**>(&g_OrigResume)))
        g_ResumeTarget = resume;
    else
        LogError("CassetteWalkman: ResumeMusicPlayer hook failed, resume StartWalkMan off");

    void* speaker = ResolveSpeakerModeConcrete();
    if (speaker && CreateAndEnableHook(speaker, reinterpret_cast<void*>(&hk_Speaker),
                                       reinterpret_cast<void**>(&g_OrigSpeaker)))
        g_SpeakerTarget = speaker;
    else
        LogError("CassetteWalkman: SetSpeakerMode hook failed, SpeakerWalkMan off");

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
    return true;
}
