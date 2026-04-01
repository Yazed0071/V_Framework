#include "pch.h"

#include <Windows.h>
#include <cstdint>
#include <mutex>

#include "HookUtils.h"
#include "log.h"
#include "MissionCodeGuard.h"
#include "SoundSystemImpl_BeginSoundSystem.h"

namespace
{
    // Original BeginSoundSystem.
    // Params: none
    using BeginSoundSystem_t = void(__fastcall*)();

    // Original SoundSystemImpl constructor.
    // Params: thisPtr, a2, a3, a4
    using SoundSystemCtor_t = void* (__fastcall*)(void* thisPtr, std::uint64_t a2, std::uint64_t a3, std::uint64_t a4);

    // Original SoundMusicPlayer::GetPlayingTime.
    // Params: thisPtr
    using GetPlayingTime_t = std::uint32_t(__fastcall*)(void* thisPtr);

    // Original SoundMusicPlayer::GetPlayingTrackId.
    // Params: thisPtr
    using GetPlayingTrackId_t = std::uint32_t(__fastcall*)(void* thisPtr);

    // Original SoundMusicPlayer::Pause.
    // Params: thisPtr, outError, fadeMs
    using PauseMusicPlayer_t = int* (__fastcall*)(void* thisPtr, int* outError, std::uint32_t fadeMs);

    // Original SoundMusicPlayer::Resume.
    // Params: thisPtr, outError, fadeMs
    using ResumeMusicPlayer_t = int* (__fastcall*)(void* thisPtr, int* outError, std::uint32_t fadeMs);

    // Original SoundMusicPlayer::Stop.
    // Params: thisPtr, outError, fadeMs, stopByUser
    using StopMusicPlayer_t = std::uint32_t* (__fastcall*)(void* thisPtr, std::uint32_t* outError, std::uint32_t fadeMs, std::uint8_t stopByUser);

    static constexpr std::uintptr_t ABS_BeginSoundSystem = 0x140989340ull;
    static constexpr std::uintptr_t ABS_SoundSystemCtor = 0x140989120ull;
    static constexpr std::uintptr_t ABS_g_SoundSystem = 0x142C009F0ull;

    static constexpr std::uintptr_t ABS_GetPlayingTime = 0x14614A4E0ull;
    static constexpr std::uintptr_t ABS_GetPlayingTrackId = 0x14614AA30ull;
    static constexpr std::uintptr_t ABS_PauseMusicPlayer = 0x140972C70ull;
    static constexpr std::uintptr_t ABS_ResumeMusicPlayer = 0x1409739E0ull;
    static constexpr std::uintptr_t ABS_StopMusicPlayer = 0x146150970ull;

    // MusicManager::s_instance global.
    static constexpr std::uintptr_t ABS_MusicManager_s_instance = 0x142BFFAC8ull;

    static constexpr std::uintptr_t kCassettePlayerVtable = 0x142285780ull;
    static constexpr std::size_t kSoundSystemScanSize = 0x50ull;
    static constexpr std::size_t kSubObjectScanSize = 0x200ull;

    static BeginSoundSystem_t g_OrigBeginSoundSystem = nullptr;
    static SoundSystemCtor_t g_OrigSoundSystemCtor = nullptr;

    static std::mutex g_SoundSystemMutex;

    static void* g_CachedSoundSystem = nullptr;
    static void* g_CachedCassettePlayer = nullptr;
}

// Safely reads one pointer.
// Params: address, outValue
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

// Resolves g_SoundSystem directly from the global slot.
// Returns: sound-system pointer or null.
static void* ResolveSoundSystemFromGlobal()
{
    void* slot = ResolveGameAddress(ABS_g_SoundSystem);
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

// Resolves the real SoundMusicPlayer from MusicManager::s_instance.
// Returns: SoundMusicPlayer pointer or null.
static void* ResolveSoundMusicPlayerFromMusicManager()
{
    void* musicManagerGlobalAddr = ResolveGameAddress(ABS_MusicManager_s_instance);
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

// Checks whether an object begins with the expected vtable.
// Params: objectPtr, expectedVtable
static bool HasExpectedVtable(void* objectPtr, std::uintptr_t expectedVtable)
{
    if (!objectPtr)
        return false;

    std::uintptr_t actualVtable = 0;
    if (!TryReadPtr(objectPtr, actualVtable))
        return false;

    return actualVtable == expectedVtable;
}

// Scans one struct-sized region for a pointer to the cassette player.
// Params: basePtr, scanSize
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

        if (HasExpectedVtable(reinterpret_cast<void*>(candidate), kCassettePlayerVtable))
        {
            Log("[SoundSystem] Found cassette player at +0x%zX -> %p\n", offset, reinterpret_cast<void*>(candidate));
            return reinterpret_cast<void*>(candidate);
        }
    }

    return nullptr;
}

// Scans the sound-system object and one level of pointed subobjects for the cassette player.
// Params: soundSystem
static void* FindCassettePlayerFromSoundSystemInternal(void* soundSystem)
{
    if (!soundSystem)
        return nullptr;

    if (HasExpectedVtable(soundSystem, kCassettePlayerVtable))
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

// Re-scans the cached/global sound-system object for the cassette music player.
// Returns: true on success, false on failure.
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

// Gets the cached sound-system pointer.
// Returns: cached pointer or null.
void* GetCachedSoundSystem()
{
    std::lock_guard<std::mutex> lock(g_SoundSystemMutex);
    return g_CachedSoundSystem;
}

// Gets the cached cassette music player pointer.
// Returns: cached player pointer or null.
void* GetGlobalCassetteMusicPlayerFromSoundSystem()
{
    std::lock_guard<std::mutex> lock(g_SoundSystemMutex);
    return g_CachedCassettePlayer;
}

// Gets the current playing time from SoundMusicPlayer.
// Returns: raw playing-time value, or 0 on failure.
std::uint32_t GetCassettePlayingTime()
{
    void* soundMusicPlayer = ResolveSoundMusicPlayerFromMusicManager();
    if (!soundMusicPlayer)
        return 0;

    void* fnAddr = ResolveGameAddress(ABS_GetPlayingTime);
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

// Gets the current playing track id from SoundMusicPlayer.
// Returns: raw playing-track id, or 0 on failure.
std::uint32_t GetCassettePlayingTrackId()
{
    void* soundMusicPlayer = ResolveSoundMusicPlayerFromMusicManager();
    if (!soundMusicPlayer)
        return 0;

    void* fnAddr = ResolveGameAddress(ABS_GetPlayingTrackId);
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

// Pauses the current cassette through SoundMusicPlayer.
// Params: fadeMs
// Returns: fox::ErrorCode as int. 0 = success, -1 = fail.
std::int32_t PauseCassette(std::uint32_t fadeMs)
{
    void* soundMusicPlayer = ResolveSoundMusicPlayerFromMusicManager();
    if (!soundMusicPlayer)
        return -1;

    void* fnAddr = ResolveGameAddress(ABS_PauseMusicPlayer);
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

// Resumes the current cassette through SoundMusicPlayer.
// Params: fadeMs
// Returns: fox::ErrorCode as int. 0 = success, -1 = fail.
std::int32_t ResumeCassette(std::uint32_t fadeMs)
{
    void* soundMusicPlayer = ResolveSoundMusicPlayerFromMusicManager();
    if (!soundMusicPlayer)
        return -1;

    void* fnAddr = ResolveGameAddress(ABS_ResumeMusicPlayer);
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

// Stops the current cassette through SoundMusicPlayer.
// Params: fadeMs, stopByUser
// Returns: fox::ErrorCode as int. 0 = success, -1 = fail.
std::int32_t StopCassette(std::uint32_t fadeMs, bool stopByUser)
{
    void* soundMusicPlayer = ResolveSoundMusicPlayerFromMusicManager();
    if (!soundMusicPlayer)
        return -1;

    void* fnAddr = ResolveGameAddress(ABS_StopMusicPlayer);
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

// Hooks SoundSystemImpl constructor and caches the created object.
// Params: thisPtr, a2, a3, a4
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

// Hooks BeginSoundSystem and refreshes discovery after startup.
// Params: none
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

// Installs the sound-system hooks.
// Params: none
bool Install_SoundSystem_BeginSoundSystem_Hook()
{
    void* beginTarget = ResolveGameAddress(ABS_BeginSoundSystem);
    if (!beginTarget)
    {
        Log("[Hook] BeginSoundSystem: address resolve failed\n");
        return false;
    }

    void* ctorTarget = ResolveGameAddress(ABS_SoundSystemCtor);
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

// Removes the sound-system hooks.
// Params: none
bool Uninstall_SoundSystem_BeginSoundSystem_Hook()
{
    DisableAndRemoveHook(ResolveGameAddress(ABS_BeginSoundSystem));
    DisableAndRemoveHook(ResolveGameAddress(ABS_SoundSystemCtor));

    g_OrigBeginSoundSystem = nullptr;
    g_OrigSoundSystemCtor = nullptr;

    {
        std::lock_guard<std::mutex> lock(g_SoundSystemMutex);
        g_CachedSoundSystem = nullptr;
        g_CachedCassettePlayer = nullptr;
    }

    return true;
}