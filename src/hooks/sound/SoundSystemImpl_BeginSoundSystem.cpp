#include "pch.h"

#include <Windows.h>
#include <cstdint>
#include <mutex>

#include "HookUtils.h"
#include "log.h"
#include "MissionCodeGuard.h"
#include "SoundSystemImpl_BeginSoundSystem.h"
#include "AddressSet.h"

namespace
{
    using BeginSoundSystem_t = void(__fastcall*)();

    using SoundSystemCtor_t = void* (__fastcall*)(void* thisPtr, std::uint64_t a2, std::uint64_t a3, std::uint64_t a4);

    using GetPlayingTime_t = std::uint32_t(__fastcall*)(void* thisPtr);

    using GetPlayingTrackId_t = std::uint32_t(__fastcall*)(void* thisPtr);

    using PauseMusicPlayer_t = int* (__fastcall*)(void* thisPtr, int* outError, std::uint32_t fadeMs);

    using ResumeMusicPlayer_t = int* (__fastcall*)(void* thisPtr, int* outError, std::uint32_t fadeMs);

    using StopMusicPlayer_t = std::uint32_t* (__fastcall*)(void* thisPtr, std::uint32_t* outError, std::uint32_t fadeMs, std::uint8_t stopByUser);

    static constexpr std::size_t kSoundSystemScanSize = 0x50ull;
    static constexpr std::size_t kSubObjectScanSize = 0x200ull;

    static BeginSoundSystem_t g_OrigBeginSoundSystem = nullptr;
    static SoundSystemCtor_t g_OrigSoundSystemCtor = nullptr;

    static std::mutex g_SoundSystemMutex;

    static void* g_CachedSoundSystem = nullptr;
    static void* g_CachedCassettePlayer = nullptr;
}


static bool TryReadPtr(const void* address, std::uintptr_t& outValue)
{
    outValue = 0;

    if (!address)
        return false;

    __try
    {
        outValue = *reinterpret_cast<const std::uintptr_t*>(address);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        outValue = 0;
        return false;
    }
}


static void* ResolveSoundSystemFromGlobal()
{
    void* slot = ResolveGameAddress(gAddr.g_SoundSystem);
    if (!slot)
        return nullptr;

    __try
    {
        return *reinterpret_cast<void**>(slot);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
}


static void* ResolveSoundMusicPlayerFromMusicManager()
{
    void* musicManagerGlobalAddr = ResolveGameAddress(gAddr.MusicManager_s_instance);
    if (!musicManagerGlobalAddr)
    {
        Log("[SoundMusicPlayer] ERROR: MusicManager::s_instance address unavailable for this build — cassette control functions will not work.\n");
        return nullptr;
    }

    __try
    {
        void* musicManagerInstance = *reinterpret_cast<void**>(musicManagerGlobalAddr);
        if (!musicManagerInstance)
        {
            Log("[SoundMusicPlayer] WARN: MusicManager not initialized yet — cassette control unavailable until the sound system is up.\n");
            return nullptr;
        }

        void* soundMusicPlayer =
            *reinterpret_cast<void**>(
                reinterpret_cast<std::uintptr_t>(musicManagerInstance) + 0xA8ull);

        if (!soundMusicPlayer)
        {
            Log("[SoundMusicPlayer] WARN: SoundMusicPlayer not initialized yet — cassette control unavailable until the sound system is up.\n");
            return nullptr;
        }

        return soundMusicPlayer;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Log("[SoundMusicPlayer] ERROR: exception while resolving SoundMusicPlayer — cassette control functions will not work.\n");
        return nullptr;
    }
}


static bool HasExpectedVtable(void* objectPtr, std::uintptr_t expectedVtable)
{
    if (!objectPtr)
        return false;

    std::uintptr_t actualVtable = 0;
    if (!TryReadPtr(objectPtr, actualVtable))
        return false;

    return actualVtable == expectedVtable;
}


static void* FindCassettePlayerInStruct(void* basePtr, std::size_t scanSize)
{
    if (!basePtr || scanSize < sizeof(std::uintptr_t))
        return nullptr;

    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(basePtr);

    for (std::size_t offset = 0; offset + sizeof(std::uintptr_t) <= scanSize; offset += sizeof(std::uintptr_t))
    {
        std::uintptr_t candidate = 0;
        if (!TryReadPtr(reinterpret_cast<const void*>(base + offset), candidate))
            continue;

        if (candidate == 0)
            continue;

        if (HasExpectedVtable(reinterpret_cast<void*>(candidate), gAddr.CassettePlayerVtable))
        {
            return reinterpret_cast<void*>(candidate);
        }
    }

    return nullptr;
}


static void* FindCassettePlayerFromSoundSystemInternal(void* soundSystem)
{
    if (!soundSystem)
        return nullptr;

    if (HasExpectedVtable(soundSystem, gAddr.CassettePlayerVtable))
    {
        return soundSystem;
    }

    void* direct = FindCassettePlayerInStruct(soundSystem, kSoundSystemScanSize);
    if (direct)
        return direct;

    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(soundSystem);

    for (std::size_t offset = 0; offset + sizeof(std::uintptr_t) <= kSoundSystemScanSize; offset += sizeof(std::uintptr_t))
    {
        std::uintptr_t subObject = 0;
        if (!TryReadPtr(reinterpret_cast<const void*>(base + offset), subObject))
            continue;

        if (subObject == 0)
            continue;

        void* nested = FindCassettePlayerInStruct(reinterpret_cast<void*>(subObject), kSubObjectScanSize);
        if (nested)
        {
            return nested;
        }
    }

    return nullptr;
}


bool RefreshGlobalCassetteMusicPlayerFromSoundSystem()
{
    void* soundSystem = nullptr;

    {
        std::lock_guard<std::mutex> lock(g_SoundSystemMutex);
        soundSystem = g_CachedSoundSystem;
    }

    if (!soundSystem)
    {
        soundSystem = ResolveSoundSystemFromGlobal();
        if (soundSystem)
        {
            std::lock_guard<std::mutex> lock(g_SoundSystemMutex);
            g_CachedSoundSystem = soundSystem;
        }
    }

    if (!soundSystem)
    {
        Log("[SoundSystem] WARN: sound system not initialized yet — cassette player not captured; open the in-game Music Player once it is.\n");
        return false;
    }

    void* cassettePlayer = FindCassettePlayerFromSoundSystemInternal(soundSystem);
    if (!cassettePlayer)
    {
        Log("[SoundSystem] WARN: could not locate the cassette player inside the sound system — direct cassette playback may be unavailable.\n");
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_SoundSystemMutex);
        g_CachedCassettePlayer = cassettePlayer;
    }

    return true;
}


void* GetCachedSoundSystem()
{
    std::lock_guard<std::mutex> lock(g_SoundSystemMutex);
    return g_CachedSoundSystem;
}


void* GetGlobalCassetteMusicPlayerFromSoundSystem()
{
    std::lock_guard<std::mutex> lock(g_SoundSystemMutex);
    return g_CachedCassettePlayer;
}


std::uint32_t GetCassettePlayingTime()
{
    void* soundMusicPlayer = ResolveSoundMusicPlayerFromMusicManager();
    if (!soundMusicPlayer)
        return 0;

    void* fnAddr = ResolveGameAddress(gAddr.GetPlayingTime);
    if (!fnAddr)
    {
        Log("[CassettePlayTime] ERROR: GetPlayingTime address unavailable for this build — cannot read cassette playback time.\n");
        return 0;
    }

    GetPlayingTime_t GetPlayingTime =
        reinterpret_cast<GetPlayingTime_t>(fnAddr);

    __try
    {
        const std::uint32_t value = GetPlayingTime(soundMusicPlayer);

        return value;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Log("[CassettePlayTime] ERROR: exception while reading cassette playback time.\n");
        return 0;
    }
}


std::uint32_t GetCassettePlayingTrackId()
{
    void* soundMusicPlayer = ResolveSoundMusicPlayerFromMusicManager();
    if (!soundMusicPlayer)
        return 0;

    void* fnAddr = ResolveGameAddress(gAddr.GetPlayingTrackId);
    if (!fnAddr)
    {
        Log("[CassetteTrackId] ERROR: GetPlayingTrackId address unavailable for this build — cannot read the playing cassette track.\n");
        return 0;
    }

    GetPlayingTrackId_t GetPlayingTrackId =
        reinterpret_cast<GetPlayingTrackId_t>(fnAddr);

    __try
    {
        const std::uint32_t value = GetPlayingTrackId(soundMusicPlayer);

        return value;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Log("[CassetteTrackId] ERROR: exception while reading the playing cassette track.\n");
        return 0;
    }
}


std::int32_t PauseCassette(std::uint32_t fadeMs)
{
    void* soundMusicPlayer = ResolveSoundMusicPlayerFromMusicManager();
    if (!soundMusicPlayer)
        return -1;

    void* fnAddr = ResolveGameAddress(gAddr.PauseMusicPlayer);
    if (!fnAddr)
    {
        Log("[CassettePause] ERROR: Pause address unavailable for this build — cannot pause cassette playback.\n");
        return -1;
    }

    PauseMusicPlayer_t PauseMusicPlayer =
        reinterpret_cast<PauseMusicPlayer_t>(fnAddr);

    __try
    {
        int errorCode = -1;
        PauseMusicPlayer(soundMusicPlayer, &errorCode, fadeMs);

        return static_cast<std::int32_t>(errorCode);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Log("[CassettePause] ERROR: exception while pausing cassette playback.\n");
        return -1;
    }
}


std::int32_t ResumeCassette(std::uint32_t fadeMs)
{
    void* soundMusicPlayer = ResolveSoundMusicPlayerFromMusicManager();
    if (!soundMusicPlayer)
        return -1;

    void* fnAddr = ResolveGameAddress(gAddr.ResumeMusicPlayer);
    if (!fnAddr)
    {
        Log("[CassetteResume] ERROR: Resume address unavailable for this build — cannot resume cassette playback.\n");
        return -1;
    }

    ResumeMusicPlayer_t ResumeMusicPlayer =
        reinterpret_cast<ResumeMusicPlayer_t>(fnAddr);

    __try
    {
        int errorCode = -1;
        ResumeMusicPlayer(soundMusicPlayer, &errorCode, fadeMs);

        return static_cast<std::int32_t>(errorCode);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Log("[CassetteResume] ERROR: exception while resuming cassette playback.\n");
        return -1;
    }
}


std::int32_t StopCassette(std::uint32_t fadeMs, bool stopByUser)
{
    void* soundMusicPlayer = ResolveSoundMusicPlayerFromMusicManager();
    if (!soundMusicPlayer)
        return -1;

    void* fnAddr = ResolveGameAddress(gAddr.StopMusicPlayer);
    if (!fnAddr)
    {
        Log("[CassetteStop] ERROR: Stop address unavailable for this build — cannot stop cassette playback.\n");
        return -1;
    }

    StopMusicPlayer_t StopMusicPlayer =
        reinterpret_cast<StopMusicPlayer_t>(fnAddr);

    __try
    {
        std::uint32_t errorCode = static_cast<std::uint32_t>(-1);
        StopMusicPlayer(soundMusicPlayer, &errorCode, fadeMs, stopByUser ? 1 : 0);

        return static_cast<std::int32_t>(errorCode);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Log("[CassetteStop] ERROR: exception while stopping cassette playback.\n");
        return -1;
    }
}


static void* __fastcall hkSoundSystemCtor(void* thisPtr, std::uint64_t a2, std::uint64_t a3, std::uint64_t a4)
{
    if (!g_OrigSoundSystemCtor)
        return thisPtr;

    void* result = g_OrigSoundSystemCtor(thisPtr, a2, a3, a4);

    {
        std::lock_guard<std::mutex> lock(g_SoundSystemMutex);
        g_CachedSoundSystem = result ? result : thisPtr;
    }

    RefreshGlobalCassetteMusicPlayerFromSoundSystem();
    return result;
}


static void __fastcall hkBeginSoundSystem()
{
    if (!g_OrigBeginSoundSystem)
        return;

    g_OrigBeginSoundSystem();

    if (MissionCodeGuard::ShouldBypassHooks())
        return;

    RefreshGlobalCassetteMusicPlayerFromSoundSystem();
}


bool Install_SoundSystem_BeginSoundSystem_Hook()
{
    void* beginTarget = ResolveGameAddress(gAddr.BeginSoundSystem);
    if (!beginTarget)
    {
        Log("[SoundSystem] ERROR: BeginSoundSystem address unavailable for this build — the cassette player cannot be captured; direct cassette playback disabled.\n");
        return false;
    }

    void* ctorTarget = ResolveGameAddress(gAddr.SoundSystemCtor);
    if (!ctorTarget)
    {
        Log("[SoundSystem] ERROR: SoundSystemImpl ctor address unavailable for this build — the cassette player cannot be captured; direct cassette playback disabled.\n");
        return false;
    }

    const bool okBegin = CreateAndEnableHook(
        beginTarget,
        reinterpret_cast<void*>(&hkBeginSoundSystem),
        reinterpret_cast<void**>(&g_OrigBeginSoundSystem));

    const bool okCtor = CreateAndEnableHook(
        ctorTarget,
        reinterpret_cast<void*>(&hkSoundSystemCtor),
        reinterpret_cast<void**>(&g_OrigSoundSystemCtor));

    if (!okBegin)
        Log("[SoundSystem] ERROR: failed to hook BeginSoundSystem — the cassette player cannot be captured; direct cassette playback disabled.\n");
    if (!okCtor)
        Log("[SoundSystem] ERROR: failed to hook SoundSystemImpl ctor — the cassette player cannot be captured; direct cassette playback disabled.\n");

    return okBegin && okCtor;
}


bool Uninstall_SoundSystem_BeginSoundSystem_Hook()
{
    DisableAndRemoveHook(ResolveGameAddress(gAddr.BeginSoundSystem));
    DisableAndRemoveHook(ResolveGameAddress(gAddr.SoundSystemCtor));

    g_OrigBeginSoundSystem = nullptr;
    g_OrigSoundSystemCtor = nullptr;

    {
        std::lock_guard<std::mutex> lock(g_SoundSystemMutex);
        g_CachedSoundSystem = nullptr;
        g_CachedCassettePlayer = nullptr;
    }

    return true;
}