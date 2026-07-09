#include "pch.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

#include "HookUtils.h"
#include "log.h"
#include "MissionCodeGuard.h"
#include "AddressSet.h"
#include "Control_PostExternalEvent.h"

namespace
{
    using PostExternalEvent_t = void* (__fastcall*)(
        void* control,
        void* errOut,
        std::uint32_t playingId,
        std::uint32_t busHash,
        void* externalSource,
        std::uint32_t count,
        std::uint32_t* outHandle);

    static PostExternalEvent_t g_OrigPostExternalEvent = nullptr;
    static bool g_HookActive = false;

    static std::mutex g_Mutex;
    static std::unordered_map<std::uint32_t, std::string> g_LongNames;
    static std::atomic<bool> g_HasAnyLongName{ false };

    static constexpr std::size_t kPathFieldSize = 0x40ull;
    static constexpr std::size_t kMaxPathChars  = kPathFieldSize - 1ull;

    static constexpr std::size_t kSmpTrackArray = 0x70ull;
    static constexpr std::size_t kSmpPlaylist   = 0x78ull;
    static constexpr std::size_t kSmpPlayState  = 0xD4ull;
    static constexpr std::size_t kSmpPlayIndex  = 0xD0ull;
    static constexpr std::size_t kSmpPlayCount  = 0x88ull;
    static constexpr std::size_t kTrackStride   = 0x38ull;
    static constexpr std::size_t kTrackHash     = 0x10ull;
    static constexpr std::size_t kTrackFileName = 0x26ull;

    struct TapeReadContext
    {
        char          path[0x40];
        char          trunc[16];
        std::uint32_t hash;
    };
}


static void* ResolveSoundMusicPlayer()
{
    void* mmGlobal = ResolveGameAddress(gAddr.MusicManager_s_instance);
    if (!mmGlobal)
        return nullptr;

    __try
    {
        void* mm = *reinterpret_cast<void**>(mmGlobal);
        if (!mm)
            return nullptr;
        return *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(mm) + 0xA8ull);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
}


static void* ResolveCurrentTrackRecord(void* smp)
{
    if (!smp)
        return nullptr;

    __try
    {
        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(smp);

        if (*reinterpret_cast<std::uint32_t*>(base + kSmpPlayState) != 4u)
            return nullptr;

        void* trackArray = *reinterpret_cast<void**>(base + kSmpTrackArray);
        void* playlist   = *reinterpret_cast<void**>(base + kSmpPlaylist);
        if (!trackArray || !playlist)
            return nullptr;

        const std::uint32_t index = *reinterpret_cast<std::uint32_t*>(base + kSmpPlayIndex);
        const std::uint32_t count = *reinterpret_cast<std::uint32_t*>(base + kSmpPlayCount);
        if (count == 0u || index >= count)
            return nullptr;

        const std::uint16_t trackIndex = *reinterpret_cast<std::uint16_t*>(
            reinterpret_cast<std::uintptr_t>(playlist) + static_cast<std::uintptr_t>(index) * 2ull);

        return reinterpret_cast<void*>(
            reinterpret_cast<std::uintptr_t>(trackArray) + static_cast<std::uintptr_t>(trackIndex) * kTrackStride);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
}


static bool LookupFullName(std::uint32_t hash, std::string& outFullName)
{
    std::lock_guard<std::mutex> lock(g_Mutex);
    auto it = g_LongNames.find(hash);
    if (it == g_LongNames.end())
        return false;
    outFullName = it->second;
    return true;
}


static bool ReadTapeContext(void* externalSource, TapeReadContext& out)
{
    __try
    {
        const char* livePath = reinterpret_cast<const char*>(externalSource);
        const std::size_t pathLen = ::strnlen(livePath, kPathFieldSize);
        if (pathLen == 0u || pathLen >= kPathFieldSize)
            return false;
        std::memcpy(out.path, livePath, pathLen + 1u);

        void* track = ResolveCurrentTrackRecord(ResolveSoundMusicPlayer());
        if (!track)
            return false;

        out.hash = *reinterpret_cast<std::uint32_t*>(
            reinterpret_cast<std::uintptr_t>(track) + kTrackHash);

        ::strncpy_s(
            out.trunc, sizeof(out.trunc),
            reinterpret_cast<const char*>(reinterpret_cast<std::uintptr_t>(track) + kTrackFileName),
            _TRUNCATE);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}


static bool WriteTapePath(void* externalSource, const char* newPath, std::size_t newLen)
{
    __try
    {
        std::memcpy(externalSource, newPath, newLen + 1u);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}


static void TrySubstituteCustomTapePath(void* externalSource)
{
    if (!externalSource || !g_HasAnyLongName.load(std::memory_order_relaxed))
        return;

    TapeReadContext ctx{};
    if (!ReadTapeContext(externalSource, ctx))
        return;

    const std::size_t truncLen = ::strnlen(ctx.trunc, sizeof(ctx.trunc) - 1u);
    if (truncLen == 0u)
        return;

    std::string fullName;
    if (!LookupFullName(ctx.hash, fullName))
        return;
    if (fullName.size() <= truncLen)
        return;

    char* namePos = nullptr;
    for (char* p = std::strstr(ctx.path, ctx.trunc); p != nullptr; p = std::strstr(p + 1, ctx.trunc))
        namePos = p;
    if (!namePos)
        return;

    const std::size_t prefixLen = static_cast<std::size_t>(namePos - ctx.path);
    const char* tail = namePos + truncLen;
    const std::size_t tailLen = ::strnlen(tail, kPathFieldSize);

    const std::size_t newLen = prefixLen + fullName.size() + tailLen;
    if (newLen > kMaxPathChars)
        return;

    char rebuilt[kPathFieldSize];
    std::memcpy(rebuilt, ctx.path, prefixLen);
    std::memcpy(rebuilt + prefixLen, fullName.c_str(), fullName.size());
    std::memcpy(rebuilt + prefixLen + fullName.size(), tail, tailLen);
    rebuilt[newLen] = '\0';

    WriteTapePath(externalSource, rebuilt, newLen);
}


static void* __fastcall hkPostExternalEvent(
    void* control,
    void* errOut,
    std::uint32_t playingId,
    std::uint32_t busHash,
    void* externalSource,
    std::uint32_t count,
    std::uint32_t* outHandle)
{
    if (!MissionCodeGuard::ShouldBypassHooks())
        TrySubstituteCustomTapePath(externalSource);

    return g_OrigPostExternalEvent(control, errOut, playingId, busHash, externalSource, count, outHandle);
}


void Register_CustomTapeLongFilename(std::uint32_t fullNameHash, const char* fullName)
{
    if (!fullName || !fullName[0])
        return;

    std::lock_guard<std::mutex> lock(g_Mutex);
    g_LongNames[fullNameHash] = fullName;
    g_HasAnyLongName.store(true, std::memory_order_relaxed);
}


bool IsCustomTapeLongFilenameHookActive()
{
    return g_HookActive;
}


bool Install_Control_PostExternalEvent_Hook()
{
    void* target = ResolveGameAddress(gAddr.SoundControl_PostExternalEvent);
    if (!target)
    {
        Log("[CustomTapeLongName] ERROR: PostExternalEvent address unavailable for this build - custom-tape long filenames will be truncated.\n");
        return false;
    }

    const bool ok = CreateAndEnableHook(
        target,
        reinterpret_cast<void*>(&hkPostExternalEvent),
        reinterpret_cast<void**>(&g_OrigPostExternalEvent));

    g_HookActive = ok;
    if (!ok)
        Log("[CustomTapeLongName] ERROR: failed to hook PostExternalEvent - custom-tape long filenames will be truncated.\n");
    return ok;
}


bool Uninstall_Control_PostExternalEvent_Hook()
{
    DisableAndRemoveHook(ResolveGameAddress(gAddr.SoundControl_PostExternalEvent));
    g_OrigPostExternalEvent = nullptr;
    g_HookActive = false;

    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_LongNames.clear();
        g_HasAnyLongName.store(false, std::memory_order_relaxed);
    }
    return true;
}
