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
        Log("[SoundMusicPlayer] MusicManager::s_instance address resolve failed\n");
        return nullptr;
    }

    __try
    {
        void* musicManagerInstance = *reinterpret_cast<void**>(musicManagerGlobalAddr);
        if (!musicManagerInstance)
        {
            Log("[SoundMusicPlayer] MusicManager::s_instance is null\n");
            return nullptr;
        }

        void* soundMusicPlayer =
            *reinterpret_cast<void**>(
                reinterpret_cast<std::uintptr_t>(musicManagerInstance) + 0xA8ull);

        if (!soundMusicPlayer)
        {
            Log("[SoundMusicPlayer] soundMusicPlayer is null\n");
            return nullptr;
        }

        return soundMusicPlayer;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Log("[SoundMusicPlayer] Exception while resolving MusicManager::s_instance\n");
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
            Log("[SoundSystem] Found cassette player at +0x%zX -> %p\n", offset, reinterpret_cast<void*>(candidate));
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
        Log("[SoundSystem] Sound system object itself matched cassette player vtable: %p\n", soundSystem);
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
            Log(
                "[SoundSystem] Found cassette player through SoundSystem +0x%zX -> %p -> %p\n",
                offset,
                reinterpret_cast<void*>(subObject),
                nested);
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
            Log("[SoundSystem] Resolved g_SoundSystem from global = %p\n", soundSystem);
        }
    }

    if (!soundSystem)
    {
        Log("[SoundSystem] RefreshGlobalCassetteMusicPlayerFromSoundSystem: no cached sound system\n");
        return false;
    }

    void* cassettePlayer = FindCassettePlayerFromSoundSystemInternal(soundSystem);
    if (!cassettePlayer)
    {
        Log("[SoundSystem] RefreshGlobalCassetteMusicPlayerFromSoundSystem: cassette player not found\n");
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_SoundSystemMutex);
        g_CachedCassettePlayer = cassettePlayer;
    }

    Log("[SoundSystem] Cached cassette player = %p\n", cassettePlayer);
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
        Log("[CassettePlayTime] GetPlayingTime address resolve failed\n");
        return 0;
    }

    GetPlayingTime_t GetPlayingTime =
        reinterpret_cast<GetPlayingTime_t>(fnAddr);

    __try
    {
        const std::uint32_t value = GetPlayingTime(soundMusicPlayer);

        Log(
            "[CassettePlayTime] soundMusicPlayer=%p value=%u\n",
            soundMusicPlayer,
            static_cast<unsigned int>(value));

        return value;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Log("[CassettePlayTime] Exception while calling GetPlayingTime\n");
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
        Log("[CassetteTrackId] GetPlayingTrackId address resolve failed\n");
        return 0;
    }

    GetPlayingTrackId_t GetPlayingTrackId =
        reinterpret_cast<GetPlayingTrackId_t>(fnAddr);

    __try
    {
        const std::uint32_t value = GetPlayingTrackId(soundMusicPlayer);

        Log(
            "[CassetteTrackId] soundMusicPlayer=%p value=%u (0x%X)\n",
            soundMusicPlayer,
            static_cast<unsigned int>(value),
            static_cast<unsigned int>(value));

        return value;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Log("[CassetteTrackId] Exception while calling GetPlayingTrackId\n");
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
        Log("[CassettePause] Pause address resolve failed\n");
        return -1;
    }

    PauseMusicPlayer_t PauseMusicPlayer =
        reinterpret_cast<PauseMusicPlayer_t>(fnAddr);

    __try
    {
        int errorCode = -1;
        PauseMusicPlayer(soundMusicPlayer, &errorCode, fadeMs);

        Log(
            "[CassettePause] soundMusicPlayer=%p fadeMs=%u errorCode=%d\n",
            soundMusicPlayer,
            static_cast<unsigned int>(fadeMs),
            errorCode);

        return static_cast<std::int32_t>(errorCode);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Log("[CassettePause] Exception while calling Pause\n");
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
        Log("[CassetteResume] Resume address resolve failed\n");
        return -1;
    }

    ResumeMusicPlayer_t ResumeMusicPlayer =
        reinterpret_cast<ResumeMusicPlayer_t>(fnAddr);

    __try
    {
        int errorCode = -1;
        ResumeMusicPlayer(soundMusicPlayer, &errorCode, fadeMs);

        Log(
            "[CassetteResume] soundMusicPlayer=%p fadeMs=%u errorCode=%d\n",
            soundMusicPlayer,
            static_cast<unsigned int>(fadeMs),
            errorCode);

        return static_cast<std::int32_t>(errorCode);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Log("[CassetteResume] Exception while calling Resume\n");
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
        Log("[CassetteStop] Stop address resolve failed\n");
        return -1;
    }

    StopMusicPlayer_t StopMusicPlayer =
        reinterpret_cast<StopMusicPlayer_t>(fnAddr);

    __try
    {
        std::uint32_t errorCode = static_cast<std::uint32_t>(-1);
        StopMusicPlayer(soundMusicPlayer, &errorCode, fadeMs, stopByUser ? 1 : 0);

        Log(
            "[CassetteStop] soundMusicPlayer=%p fadeMs=%u stopByUser=%u errorCode=%d\n",
            soundMusicPlayer,
            static_cast<unsigned int>(fadeMs),
            stopByUser ? 1u : 0u,
            static_cast<int>(errorCode));

        return static_cast<std::int32_t>(errorCode);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Log("[CassetteStop] Exception while calling Stop\n");
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

    Log("[SoundSystem] SoundSystemImpl ctor cached = %p\n", result ? result : thisPtr);

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

    Log("[SoundSystem] BeginSoundSystem finished\n");
    RefreshGlobalCassetteMusicPlayerFromSoundSystem();
}


bool Install_SoundSystem_BeginSoundSystem_Hook()
{
    void* beginTarget = ResolveGameAddress(gAddr.BeginSoundSystem);
    if (!beginTarget)
    {
        Log("[Hook] BeginSoundSystem: address resolve failed\n");
        return false;
    }

    void* ctorTarget = ResolveGameAddress(gAddr.SoundSystemCtor);
    if (!ctorTarget)
    {
        Log("[Hook] SoundSystemImpl ctor: address resolve failed\n");
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

    Log("[Hook] BeginSoundSystem: %s\n", okBegin ? "OK" : "FAIL");
    Log("[Hook] SoundSystemImpl ctor: %s\n", okCtor ? "OK" : "FAIL");

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