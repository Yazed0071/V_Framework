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

    static constexpr std::uintptr_t ABS_BeginSoundSystem = 0x140989340ull;
    static constexpr std::uintptr_t ABS_SoundSystemCtor = 0x140989120ull;
    static constexpr std::uintptr_t ABS_g_SoundSystem = 0x142C009F0ull;

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