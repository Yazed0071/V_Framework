#include "pch.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <mutex>

#include "HookUtils.h"
#include "log.h"
#include "MissionCodeGuard.h"
#include "MbDvcCassetteTapeCallbackImpl_PlayOrPauseSelectedTrack.h"
#include "tpp/sd/impl/BeginSoundSystem/SoundSystemImpl_BeginSoundSystem.h"

namespace
{
    // Cassette Start.
    // Params: cassetteCallbackBase
    using CassetteStart_t = void(__fastcall*)(void* cassetteCallbackBase);

    // Cassette PlayOrPause wrapper.
    // Params: cassetteCallbackBase
    using PlayOrPauseSelectedTrack_t = bool(__fastcall*)(void* cassetteCallbackBase);

    // Real lower-level cassette/music play function from player vtable +0xF0.
    // Params: player, outHandle, playMode, tapeIdTable, albumIndex, selectedTrackIndex, reservedZero, loopPlayFlag, allOrOneFlag
    using MusicPlayerPlay_t = void(__fastcall*)(
        void* player,
        void* outHandle,
        std::uint32_t playMode,
        void* tapeIdTable,
        std::uint32_t albumIndex,
        std::uint32_t selectedTrackIndex,
        std::uint32_t reservedZero,
        std::uint8_t loopPlayFlag,
        std::uint8_t allOrOneFlag);

    static constexpr std::uintptr_t ABS_CassetteStart = 0x149310440ull;
    static constexpr std::uintptr_t ABS_PlayOrPauseSelectedTrack = 0x140EF6BD0ull;
    static constexpr std::size_t kMusicPlayerPlayVtableOffset = 0xF0ull;

    // Copy buffer for callbackBase + 0xD80.
    static constexpr std::size_t kTapeIdTableSize = 0x200ull;

    static CassetteStart_t g_OrigCassetteStart = nullptr;
    static PlayOrPauseSelectedTrack_t g_OrigPlayOrPauseSelectedTrack = nullptr;
    static MusicPlayerPlay_t g_OrigMusicPlayerPlay = nullptr;

    static std::mutex g_CassettePlayHookMutex;

    static void* g_MusicPlayerPlayTarget = nullptr;
    static void* g_LastCassetteCallbackBase = nullptr;
    static void* g_LastMusicPlayer = nullptr;
    static void* g_LastTapeIdTable = nullptr;
    static std::uint32_t g_LastAlbumIndex = 0;
    static std::uint32_t g_LastSelectedTrackIndex = 0;
    static std::uint32_t g_LastPlayMode = 0;
    static std::uint8_t g_LastFlag1 = 0;
    static std::uint8_t g_LastFlag2 = 0;

    static std::uint8_t g_CachedTapeIdTable[kTapeIdTableSize] = {};
    static bool g_HasCopiedTapeIdTable = false;

    // Persistent synthetic table for direct play-by-track-id tests.
    static std::uint32_t g_DirectTrackTable[0x80] = {};
}

// Logs the real lower-level cassette play call.
// Params: player, outHandle, playMode, tapeIdTable, albumIndex, selectedTrackIndex, reservedZero, flag1, flag2
static void __fastcall hkMusicPlayerPlay(
    void* player,
    void* outHandle,
    std::uint32_t playMode,
    void* tapeIdTable,
    std::uint32_t albumIndex,
    std::uint32_t selectedTrackIndex,
    std::uint32_t reservedZero,
    std::uint8_t flag1,
    std::uint8_t flag2);

// Resolves the lower-level music player object from the cassette callback.
// Params: cassetteCallbackBase
static void* ResolveMusicPlayerFromCassetteCallback(void* cassetteCallbackBase)
{
    if (!cassetteCallbackBase)
        return nullptr;

    __try
    {
        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(cassetteCallbackBase);
        const std::uintptr_t context = *reinterpret_cast<const std::uintptr_t*>(base + 0x30ull);
        if (!context)
            return nullptr;

        const std::uintptr_t player = *reinterpret_cast<const std::uintptr_t*>(context + 0xD8ull);
        if (!player)
            return nullptr;

        return reinterpret_cast<void*>(player);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
}

// Resolves the real lower-level play target from the music player vtable.
// Params: musicPlayer
static void* ResolveMusicPlayerPlayTarget(void* musicPlayer)
{
    if (!musicPlayer)
        return nullptr;

    __try
    {
        const std::uintptr_t vtable = *reinterpret_cast<const std::uintptr_t*>(musicPlayer);
        if (!vtable)
            return nullptr;

        const std::uintptr_t target =
            *reinterpret_cast<const std::uintptr_t*>(vtable + kMusicPlayerPlayVtableOffset);
        if (!target)
            return nullptr;

        return reinterpret_cast<void*>(target);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
}

// Safely copies raw bytes from game memory.
// Params: source, dest, size
// Returns: true on success, false on failure.
static bool TryCopyBytes(const void* source, void* dest, std::size_t size)
{
    if (!source || !dest || size == 0)
        return false;

    __try
    {
        std::memcpy(dest, source, size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// Copies the tape-id table bytes from the cassette callback into a temporary buffer.
// Params: cassetteCallbackBase, outBuffer
// Returns: true on success, false on failure.
static bool TryCopyTapeIdTableFromCassetteCallback(void* cassetteCallbackBase, std::uint8_t* outBuffer)
{
    if (!cassetteCallbackBase || !outBuffer)
        return false;

    __try
    {
        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(cassetteCallbackBase);
        const void* source = reinterpret_cast<const void*>(base + 0xD80ull);

        std::memcpy(outBuffer, source, kTapeIdTableSize);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// Copies callbackBase + 0xD80 into a persistent local buffer.
// Params: cassetteCallbackBase
static void CacheTapeIdTableFromCassetteCallback(void* cassetteCallbackBase)
{
    if (!cassetteCallbackBase)
        return;

    std::uint8_t tempBuffer[kTapeIdTableSize] = {};
    const bool ok = TryCopyTapeIdTableFromCassetteCallback(cassetteCallbackBase, tempBuffer);

    if (!ok)
    {
        Log("[CassettePlay] CacheTapeIdTableFromCassetteCallback: exception\n");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_CassettePlayHookMutex);

        std::memcpy(g_CachedTapeIdTable, tempBuffer, kTapeIdTableSize);
        g_LastTapeIdTable = g_CachedTapeIdTable;
        g_HasCopiedTapeIdTable = true;
    }

    Log(
        "[CassettePlay] Cached tapeIdTable from callback=%p localCopy=%p size=0x%zX\n",
        cassetteCallbackBase,
        g_CachedTapeIdTable,
        kTapeIdTableSize);
}

// Logs one 16-byte row in hex.
// Params: prefix, baseOffset, rowBytes
static void LogHexRow(const char* prefix, std::size_t baseOffset, const std::uint8_t* rowBytes)
{
    Log(
        "%s +0x%04zX : "
        "%02X %02X %02X %02X %02X %02X %02X %02X "
        "%02X %02X %02X %02X %02X %02X %02X %02X\n",
        prefix,
        baseOffset,
        static_cast<unsigned int>(rowBytes[0]),
        static_cast<unsigned int>(rowBytes[1]),
        static_cast<unsigned int>(rowBytes[2]),
        static_cast<unsigned int>(rowBytes[3]),
        static_cast<unsigned int>(rowBytes[4]),
        static_cast<unsigned int>(rowBytes[5]),
        static_cast<unsigned int>(rowBytes[6]),
        static_cast<unsigned int>(rowBytes[7]),
        static_cast<unsigned int>(rowBytes[8]),
        static_cast<unsigned int>(rowBytes[9]),
        static_cast<unsigned int>(rowBytes[10]),
        static_cast<unsigned int>(rowBytes[11]),
        static_cast<unsigned int>(rowBytes[12]),
        static_cast<unsigned int>(rowBytes[13]),
        static_cast<unsigned int>(rowBytes[14]),
        static_cast<unsigned int>(rowBytes[15]));
}

// Logs important cassette callback fields built by Start.
// Params: cassetteCallbackBase
static void LogCassetteCallbackState(void* cassetteCallbackBase)
{
    if (!cassetteCallbackBase)
    {
        Log("[CassetteState] callback is null\n");
        return;
    }

    __try
    {
        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(cassetteCallbackBase);

        const std::uintptr_t pD20 = *reinterpret_cast<const std::uintptr_t*>(base + 0xD20ull);
        const std::uint32_t  cD28 = *reinterpret_cast<const std::uint32_t*>(base + 0xD28ull);

        const std::uintptr_t pD30 = *reinterpret_cast<const std::uintptr_t*>(base + 0xD30ull);
        const std::uint32_t  cD38 = *reinterpret_cast<const std::uint32_t*>(base + 0xD38ull);

        const std::uintptr_t pD40 = *reinterpret_cast<const std::uintptr_t*>(base + 0xD40ull);
        const std::uint32_t  cD48 = *reinterpret_cast<const std::uint32_t*>(base + 0xD48ull);

        const std::uint32_t  curD50 = *reinterpret_cast<const std::uint32_t*>(base + 0xD50ull);
        const std::uint32_t  f64 = *reinterpret_cast<const std::uint32_t*>(base + 0xF64ull);
        const std::uint32_t  f80 = *reinterpret_cast<const std::uint32_t*>(base + 0xF80ull);
        const std::uint32_t  f84 = *reinterpret_cast<const std::uint32_t*>(base + 0xF84ull);

        Log(
            "[CassetteState] callback=%p"
            " D20=%p D28=%u"
            " D30=%p D38=%u"
            " D40=%p D48=%u"
            " D50=%u F64=%u F80=%u F84=%u\n",
            cassetteCallbackBase,
            reinterpret_cast<void*>(pD20),
            static_cast<unsigned int>(cD28),
            reinterpret_cast<void*>(pD30),
            static_cast<unsigned int>(cD38),
            reinterpret_cast<void*>(pD40),
            static_cast<unsigned int>(cD48),
            static_cast<unsigned int>(curD50),
            static_cast<unsigned int>(f64),
            static_cast<unsigned int>(f80),
            static_cast<unsigned int>(f84));

        if (pD20 && cD28)
        {
            const std::size_t elementCount = (cD28 > 8u) ? 8u : static_cast<std::size_t>(cD28);
            const std::size_t bytes = elementCount * sizeof(std::uint64_t);

            if (bytes > 0)
            {
                std::uint8_t temp[8 * sizeof(std::uint64_t)] = {};
                if (TryCopyBytes(reinterpret_cast<const void*>(pD20), temp, bytes))
                {
                    for (std::size_t i = 0; i < bytes; i += 0x10)
                    {
                        LogHexRow("[CassetteState][D20]", i, &temp[i]);
                    }
                }
            }
        }

        if (pD30 && cD38)
        {
            const std::size_t elementCount = (cD38 > 8u) ? 8u : static_cast<std::size_t>(cD38);
            const std::size_t bytes = elementCount * sizeof(std::uint64_t);

            if (bytes > 0)
            {
                std::uint8_t temp[8 * sizeof(std::uint64_t)] = {};
                if (TryCopyBytes(reinterpret_cast<const void*>(pD30), temp, bytes))
                {
                    for (std::size_t i = 0; i < bytes; i += 0x10)
                    {
                        LogHexRow("[CassetteState][D30]", i, &temp[i]);
                    }
                }
            }
        }

        if (pD40 && cD48)
        {
            const std::size_t elementCount = (cD48 > 8u) ? 8u : static_cast<std::size_t>(cD48);
            const std::size_t bytes = elementCount * sizeof(std::uint64_t);

            if (bytes > 0)
            {
                std::uint8_t temp[8 * sizeof(std::uint64_t)] = {};
                if (TryCopyBytes(reinterpret_cast<const void*>(pD40), temp, bytes))
                {
                    for (std::size_t i = 0; i < bytes; i += 0x10)
                    {
                        LogHexRow("[CassetteState][D40]", i, &temp[i]);
                    }
                }
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Log("[CassetteState] exception while reading callback state\n");
    }
}

// Logs raw cassette-table bytes for the live selection.
// Params: tapeIdTable, albumIndex, selectedTrackIndex
static void LogCassetteSelectionTrace(void* tapeIdTable, std::uint32_t albumIndex, std::uint32_t selectedTrackIndex)
{
    if (!tapeIdTable)
    {
        Log("[CassetteTrace] tapeIdTable is null\n");
        return;
    }

    Log(
        "[CassetteTrace] table=%p albumIndex=%u selectedTrackIndex=%u\n",
        tapeIdTable,
        static_cast<unsigned int>(albumIndex),
        static_cast<unsigned int>(selectedTrackIndex));

    {
        std::uint8_t block[0x100] = {};
        if (TryCopyBytes(tapeIdTable, block, sizeof(block)))
        {
            for (std::size_t i = 0; i < sizeof(block); i += 0x10)
            {
                LogHexRow("[CassetteTrace][Head]", i, &block[i]);
            }
        }
        else
        {
            Log("[CassetteTrace] failed to copy table head\n");
        }
    }

    {
        const std::size_t centerOffset = static_cast<std::size_t>(selectedTrackIndex) * sizeof(std::uint32_t);
        const std::size_t startOffset = (centerOffset >= 0x40) ? (centerOffset - 0x40) : 0;

        std::uint8_t block[0x80] = {};
        const void* startPtr = reinterpret_cast<const void*>(
            reinterpret_cast<std::uintptr_t>(tapeIdTable) + startOffset);

        if (TryCopyBytes(startPtr, block, sizeof(block)))
        {
            Log(
                "[CassetteTrace] selectedTrackIndex*4 center=0x%zX start=0x%zX\n",
                centerOffset,
                startOffset);

            for (std::size_t i = 0; i < sizeof(block); i += 0x10)
            {
                LogHexRow("[CassetteTrace][TrackWindow]", startOffset + i, &block[i]);
            }
        }
        else
        {
            Log("[CassetteTrace] failed to copy track window\n");
        }
    }

    {
        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(tapeIdTable);

        std::uint32_t selectedTrackId = 0;
        std::uint32_t selectedAlbumValue = 0;

        const void* selectedTrackPtr = reinterpret_cast<const void*>(
            base + static_cast<std::size_t>(selectedTrackIndex) * sizeof(std::uint32_t));
        const void* selectedAlbumPtr = reinterpret_cast<const void*>(
            base + static_cast<std::size_t>(albumIndex) * sizeof(std::uint32_t));

        if (TryCopyBytes(selectedTrackPtr, &selectedTrackId, sizeof(selectedTrackId)))
        {
            Log(
                "[CassetteTrace] dword@trackIndex*4 addr=%p value=%08X (%u)\n",
                selectedTrackPtr,
                static_cast<unsigned int>(selectedTrackId),
                static_cast<unsigned int>(selectedTrackId));
        }

        if (TryCopyBytes(selectedAlbumPtr, &selectedAlbumValue, sizeof(selectedAlbumValue)))
        {
            Log(
                "[CassetteTrace] dword@albumIndex*4 addr=%p value=%08X (%u)\n",
                selectedAlbumPtr,
                static_cast<unsigned int>(selectedAlbumValue),
                static_cast<unsigned int>(selectedAlbumValue));
        }
    }
}

// Installs the real lower-level play hook from the player vtable.
// Params: musicPlayer
static void TryInstallMusicPlayerPlayHook(void* musicPlayer)
{
    if (!musicPlayer)
        return;

    void* targetPtr = ResolveMusicPlayerPlayTarget(musicPlayer);
    if (!targetPtr)
    {
        Log("[CassettePlay] MusicPlayer vf+0xF0 hook: failed to resolve target\n");
        return;
    }

    std::lock_guard<std::mutex> lock(g_CassettePlayHookMutex);

    if (g_MusicPlayerPlayTarget != nullptr)
        return;

    const bool ok = CreateAndEnableHook(
        targetPtr,
        reinterpret_cast<void*>(&hkMusicPlayerPlay),
        reinterpret_cast<void**>(&g_OrigMusicPlayerPlay));

    if (!ok)
    {
        Log("[CassettePlay] MusicPlayer vf+0xF0 hook: FAIL target=%p\n", targetPtr);
        return;
    }

    g_MusicPlayerPlayTarget = targetPtr;

    Log(
        "[CassettePlay] MusicPlayer vf+0xF0 hook: OK player=%p target=%p\n",
        musicPlayer,
        targetPtr);
}

// Logs the real lower-level cassette play call.
// Params: player, outHandle, playMode, tapeIdTable, albumIndex, selectedTrackIndex, reservedZero, flag1, flag2
static void __fastcall hkMusicPlayerPlay(
    void* player,
    void* outHandle,
    std::uint32_t playMode,
    void* tapeIdTable,
    std::uint32_t albumIndex,
    std::uint32_t selectedTrackIndex,
    std::uint32_t reservedZero,
    std::uint8_t flag1,
    std::uint8_t flag2)
{
    {
        std::lock_guard<std::mutex> lock(g_CassettePlayHookMutex);
        g_LastMusicPlayer = player;
        g_LastTapeIdTable = tapeIdTable;
        g_LastAlbumIndex = albumIndex;
        g_LastSelectedTrackIndex = selectedTrackIndex;
        g_LastPlayMode = playMode;
        g_LastFlag1 = flag1;
        g_LastFlag2 = flag2;
    }

    LogCassetteCallbackState(g_LastCassetteCallbackBase);
    LogCassetteSelectionTrace(tapeIdTable, albumIndex, selectedTrackIndex);

    Log(
        "[CassettePlay] RealPlay"
        " player=%p"
        " outHandle=%p"
        " playMode=%u"
        " tapeIdTable=%p"
        " albumIndex=%u"
        " selectedTrackIndex=%u"
        " reservedZero=%u"
        " flag1=%u"
        " flag2=%u\n",
        player,
        outHandle,
        static_cast<unsigned int>(playMode),
        tapeIdTable,
        static_cast<unsigned int>(albumIndex),
        static_cast<unsigned int>(selectedTrackIndex),
        static_cast<unsigned int>(reservedZero),
        static_cast<unsigned int>(flag1),
        static_cast<unsigned int>(flag2));

    if (g_OrigMusicPlayerPlay)
    {
        g_OrigMusicPlayerPlay(
            player,
            outHandle,
            playMode,
            tapeIdTable,
            albumIndex,
            selectedTrackIndex,
            reservedZero,
            flag1,
            flag2);
    }
}

// Hooks cassette Start and caches the callback-owned tape-id table.
// Params: cassetteCallbackBase
static void __fastcall hkCassetteStart(void* cassetteCallbackBase)
{
    if (!g_OrigCassetteStart)
        return;

    g_OrigCassetteStart(cassetteCallbackBase);

    if (MissionCodeGuard::ShouldBypassHooks())
        return;

    g_LastCassetteCallbackBase = cassetteCallbackBase;
    CacheTapeIdTableFromCassetteCallback(cassetteCallbackBase);

    Log("[CassettePlay] Cassette Start cached callback=%p\n", cassetteCallbackBase);
}

// Hooks the menu wrapper and lazily installs the real play hook.
// Params: cassetteCallbackBase
static bool __fastcall hkPlayOrPauseSelectedTrack(void* cassetteCallbackBase)
{
    if (!g_OrigPlayOrPauseSelectedTrack)
        return false;

    if (MissionCodeGuard::ShouldBypassHooks())
        return g_OrigPlayOrPauseSelectedTrack(cassetteCallbackBase);

    g_LastCassetteCallbackBase = cassetteCallbackBase;
    CacheTapeIdTableFromCassetteCallback(cassetteCallbackBase);

    void* musicPlayer = ResolveMusicPlayerFromCassetteCallback(cassetteCallbackBase);
    if (musicPlayer)
    {
        TryInstallMusicPlayerPlayHook(musicPlayer);
    }
    else
    {
        Log("[CassettePlay] ResolveMusicPlayerFromCassetteCallback: FAIL callback=%p\n", cassetteCallbackBase);
    }

    return g_OrigPlayOrPauseSelectedTrack(cassetteCallbackBase);
}

// Resolves the real lower-level play function and player.
// Params: outPlayFn, outPlayer
// Returns: true on success, false on failure.
static bool ResolveDirectPlayState(MusicPlayerPlay_t& outPlayFn, void*& outPlayer)
{
    outPlayFn = nullptr;
    outPlayer = nullptr;

    {
        std::lock_guard<std::mutex> lock(g_CassettePlayHookMutex);
        outPlayFn = g_OrigMusicPlayerPlay;
        outPlayer = g_LastMusicPlayer;
    }

    if (!outPlayer)
    {
        RefreshGlobalCassetteMusicPlayerFromSoundSystem();
        outPlayer = GetGlobalCassetteMusicPlayerFromSoundSystem();
    }

    if (!outPlayFn && outPlayer)
    {
        void* target = ResolveMusicPlayerPlayTarget(outPlayer);
        if (target)
        {
            outPlayFn = reinterpret_cast<MusicPlayerPlay_t>(target);
        }
    }

    return (outPlayFn != nullptr && outPlayer != nullptr);
}

// Installs the cassette Start hook and the PlayOrPause hook.
// Params: none
bool Install_MbDvcCassetteTapeCallbackImpl_PlayOrPauseSelectedTrack_Hook()
{
    void* startTarget = ResolveGameAddress(ABS_CassetteStart);
    if (!startTarget)
    {
        Log("[Hook] Cassette Start: address resolve failed\n");
        return false;
    }

    void* playTarget = ResolveGameAddress(ABS_PlayOrPauseSelectedTrack);
    if (!playTarget)
    {
        Log("[Hook] Cassette PlayOrPauseSelectedTrack: address resolve failed\n");
        return false;
    }

    const bool okStart = CreateAndEnableHook(
        startTarget,
        reinterpret_cast<void*>(&hkCassetteStart),
        reinterpret_cast<void**>(&g_OrigCassetteStart));

    const bool okPlay = CreateAndEnableHook(
        playTarget,
        reinterpret_cast<void*>(&hkPlayOrPauseSelectedTrack),
        reinterpret_cast<void**>(&g_OrigPlayOrPauseSelectedTrack));

    Log("[Hook] Cassette Start: %s\n", okStart ? "OK" : "FAIL");
    Log("[Hook] Cassette PlayOrPauseSelectedTrack: %s\n", okPlay ? "OK" : "FAIL");

    return okStart && okPlay;
}

// Removes the Start hook, the PlayOrPause hook, and the real player-play hook.
// Params: none
bool Uninstall_MbDvcCassetteTapeCallbackImpl_PlayOrPauseSelectedTrack_Hook()
{
    DisableAndRemoveHook(ResolveGameAddress(ABS_CassetteStart));
    DisableAndRemoveHook(ResolveGameAddress(ABS_PlayOrPauseSelectedTrack));

    g_OrigCassetteStart = nullptr;
    g_OrigPlayOrPauseSelectedTrack = nullptr;

    {
        std::lock_guard<std::mutex> lock(g_CassettePlayHookMutex);

        if (g_MusicPlayerPlayTarget != nullptr)
        {
            DisableAndRemoveHook(g_MusicPlayerPlayTarget);
            g_MusicPlayerPlayTarget = nullptr;
        }

        g_OrigMusicPlayerPlay = nullptr;
        g_LastCassetteCallbackBase = nullptr;
        g_LastMusicPlayer = nullptr;
        g_LastTapeIdTable = nullptr;
        g_LastAlbumIndex = 0;
        g_LastSelectedTrackIndex = 0;
        g_LastPlayMode = 0;
        g_LastFlag1 = 0;
        g_LastFlag2 = 0;

        std::memset(g_CachedTapeIdTable, 0, sizeof(g_CachedTapeIdTable));
        g_HasCopiedTapeIdTable = false;
        std::memset(g_DirectTrackTable, 0, sizeof(g_DirectTrackTable));
    }

    return true;
}

// Plays a cassette directly using the cached lower-level player and cached menu table.
// Params: albumIndex, trackIndex, loopPlay, playAll
// Returns: true on success, false on failure.
bool PlayCassetteByAlbumAndTrack(
    std::uint32_t albumIndex,
    std::uint32_t trackIndex,
    bool loopPlay,
    bool playAll)
{
    if (MissionCodeGuard::ShouldBypassHooks())
        return false;

    MusicPlayerPlay_t playFn = nullptr;
    void* player = nullptr;
    void* tapeIdTable = nullptr;

    {
        std::lock_guard<std::mutex> lock(g_CassettePlayHookMutex);
        tapeIdTable = g_LastTapeIdTable;
    }

    if (!ResolveDirectPlayState(playFn, player))
    {
        Log("[CassettePlay] PlayCassetteByAlbumAndTrack: playFn/player not ready\n");
        return false;
    }

    if (!tapeIdTable)
    {
        Log("[CassettePlay] PlayCassetteByAlbumAndTrack: tapeIdTable not cached\n");
        return false;
    }

    std::uint64_t outHandle = 0;

    const std::uint32_t playMode = 0;
    const std::uint32_t reservedZero = 0;
    const std::uint8_t flag1 = loopPlay ? 1 : 0;
    const std::uint8_t flag2 = playAll ? 1 : 0;

    Log(
        "[CassettePlay] DirectPlay"
        " player=%p"
        " tapeIdTable=%p"
        " albumIndex=%u"
        " trackIndex=%u"
        " loop=%u"
        " playAll=%u\n",
        player,
        tapeIdTable,
        static_cast<unsigned int>(albumIndex),
        static_cast<unsigned int>(trackIndex),
        static_cast<unsigned int>(flag1),
        static_cast<unsigned int>(flag2));

    playFn(
        player,
        &outHandle,
        playMode,
        tapeIdTable,
        albumIndex,
        trackIndex,
        reservedZero,
        flag1,
        flag2);

    return true;
}

// Plays a cassette directly by numeric track id using a persistent synthetic table.
// Params: albumIndex, trackId, loopPlay, playAll
// Returns: true on success, false on failure.
bool PlayCassetteByTrackId(
    std::uint32_t albumIndex,
    std::uint32_t trackId,
    bool loopPlay,
    bool playAll)
{
    if (MissionCodeGuard::ShouldBypassHooks())
        return false;

    MusicPlayerPlay_t playFn = nullptr;
    void* player = nullptr;

    if (!ResolveDirectPlayState(playFn, player))
    {
        Log("[CassettePlay] PlayCassetteByTrackId: playFn/player not ready\n");
        return false;
    }

    std::uint64_t outHandle = 0;

    std::memset(g_DirectTrackTable, 0, sizeof(g_DirectTrackTable));
    g_DirectTrackTable[0] = trackId;

    const std::uint32_t playMode = 0;
    const std::uint32_t selectedTrackIndex = 0;
    const std::uint32_t reservedZero = 0;
    const std::uint8_t flag1 = loopPlay ? 1 : 0;
    const std::uint8_t flag2 = playAll ? 1 : 0;

    Log(
        "[CassettePlay] DirectPlayByTrackId"
        " player=%p"
        " albumIndex=%u"
        " trackId=%u (0x%X)"
        " loop=%u"
        " playAll=%u"
        " table=%p\n",
        player,
        static_cast<unsigned int>(albumIndex),
        static_cast<unsigned int>(trackId),
        static_cast<unsigned int>(trackId),
        static_cast<unsigned int>(flag1),
        static_cast<unsigned int>(flag2),
        g_DirectTrackTable);

    playFn(
        player,
        &outHandle,
        playMode,
        g_DirectTrackTable,
        albumIndex,
        selectedTrackIndex,
        reservedZero,
        flag1,
        flag2);

    return true;
}