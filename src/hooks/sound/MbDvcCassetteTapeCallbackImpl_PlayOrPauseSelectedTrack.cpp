#include "pch.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <mutex>

#include "HookUtils.h"
#include "log.h"
#include "MissionCodeGuard.h"
#include "MbDvcCassetteTapeCallbackImpl_PlayOrPauseSelectedTrack.h"
#include "SoundSystemImpl_BeginSoundSystem.h"
#include "AddressSet.h"

namespace
{


    using CassetteStart_t = void(__fastcall*)(void* cassetteCallbackBase);


    using PlayOrPauseSelectedTrack_t = bool(__fastcall*)(void* cassetteCallbackBase);


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


    using CassettePlayerGetState_t = int(__fastcall*)(void* player);


    using CassettePlayerSetSpeakerMode_t =
        std::uint32_t* (__fastcall*)(void* player, void* outResult, std::uint32_t targetMode);


    using CassettePlayerGetSpeakerMode_t = int(__fastcall*)(void* player);

    static constexpr std::size_t kMusicPlayerPlayVtableOffset = 0xF0ull;
    static constexpr std::size_t kCassettePlayerGetStateVtableOffset = 0xD8ull;
    static constexpr std::size_t kCassettePlayerSetSpeakerModeVtableOffset = 0x1A0ull;
    static constexpr std::size_t kCassettePlayerGetSpeakerModeVtableOffset = 0x1A8ull;

    static constexpr std::uint32_t kCassetteSpeakerModeDisabled = 0u;
    static constexpr std::uint32_t kCassetteSpeakerModeEnabled = 1u;


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


    static std::uint32_t g_DirectTrackTable[0x80] = {};
}


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

static void __fastcall hkCassetteStart(void* cassetteCallbackBase)
{
    Log(
        "[CassettePlay] hkCassetteStart entered callback=%p orig=%p\n",
        cassetteCallbackBase,
        g_OrigCassetteStart);

    if (!g_OrigCassetteStart)
    {
        Log("[CassettePlay] hkCassetteStart: orig is null\n");
        return;
    }

    g_OrigCassetteStart(cassetteCallbackBase);

    const bool bypass = MissionCodeGuard::ShouldBypassHooks();
    Log(
        "[CassettePlay] hkCassetteStart bypass=%d callback=%p\n",
        bypass ? 1 : 0,
        cassetteCallbackBase);

    if (bypass)
        return;

    {
        std::lock_guard<std::mutex> lock(g_CassettePlayHookMutex);
        g_LastCassetteCallbackBase = cassetteCallbackBase;
    }

    CacheTapeIdTableFromCassetteCallback(cassetteCallbackBase);

    Log("[CassettePlay] Cassette Start cached callback=%p\n", cassetteCallbackBase);
}

static bool __fastcall hkPlayOrPauseSelectedTrack(void* cassetteCallbackBase)
{
    Log(
        "[CassettePlay] hkPlayOrPauseSelectedTrack entered callback=%p orig=%p\n",
        cassetteCallbackBase,
        g_OrigPlayOrPauseSelectedTrack);

    if (!g_OrigPlayOrPauseSelectedTrack)
    {
        Log("[CassettePlay] hkPlayOrPauseSelectedTrack: orig is null\n");
        return false;
    }

    const bool bypass = MissionCodeGuard::ShouldBypassHooks();
    Log(
        "[CassettePlay] hkPlayOrPauseSelectedTrack bypass=%d callback=%p\n",
        bypass ? 1 : 0,
        cassetteCallbackBase);

    if (bypass)
        return g_OrigPlayOrPauseSelectedTrack(cassetteCallbackBase);

    {
        std::lock_guard<std::mutex> lock(g_CassettePlayHookMutex);
        g_LastCassetteCallbackBase = cassetteCallbackBase;
    }

    CacheTapeIdTableFromCassetteCallback(cassetteCallbackBase);

    void* musicPlayer = ResolveMusicPlayerFromCassetteCallback(cassetteCallbackBase);
    if (musicPlayer)
    {
        Log("[CassettePlay] hkPlayOrPauseSelectedTrack resolved musicPlayer=%p\n", musicPlayer);
        TryInstallMusicPlayerPlayHook(musicPlayer);
    }
    else
    {
        Log("[CassettePlay] ResolveMusicPlayerFromCassetteCallback: FAIL callback=%p\n", cassetteCallbackBase);
    }

    return g_OrigPlayOrPauseSelectedTrack(cassetteCallbackBase);
}


static void* ResolveCassettePlayerVtableFunction(void* player, std::size_t vtableOffset)
{
    if (!player)
        return nullptr;

    __try
    {
        const std::uintptr_t vtable = *reinterpret_cast<const std::uintptr_t*>(player);
        if (!vtable)
            return nullptr;

        const std::uintptr_t target =
            *reinterpret_cast<const std::uintptr_t*>(vtable + vtableOffset);
        if (!target)
            return nullptr;

        return reinterpret_cast<void*>(target);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
}


static bool ResolveCachedCassetteCallbackAndPlayer(void*& outCallbackBase, void*& outPlayer)
{
    outCallbackBase = nullptr;
    outPlayer = nullptr;

    {
        std::lock_guard<std::mutex> lock(g_CassettePlayHookMutex);
        outCallbackBase = g_LastCassetteCallbackBase;
    }

    if (!outCallbackBase)
        return false;

    outPlayer = ResolveMusicPlayerFromCassetteCallback(outCallbackBase);
    return outPlayer != nullptr;
}


static bool CanSwitchCassetteSpeakerMode(void* player)
{
    if (!player)
        return false;

    void* fnAddr = ResolveCassettePlayerVtableFunction(player, kCassettePlayerGetStateVtableOffset);
    if (!fnAddr)
        return false;

    CassettePlayerGetState_t getState =
        reinterpret_cast<CassettePlayerGetState_t>(fnAddr);

    __try
    {
        const int state = getState(player);
        return static_cast<unsigned int>(state - 4) >= 2u;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}


bool IsCassetteSpeakerEnabled(bool& outEnabled)
{
    outEnabled = false;

    if (MissionCodeGuard::ShouldBypassHooks())
        return false;

    void* callbackBase = nullptr;
    void* player = nullptr;
    if (!ResolveCachedCassetteCallbackAndPlayer(callbackBase, player))
    {
        Log("[CassetteSpeaker] Get: cached callback/player not ready\n");
        return false;
    }

    if (!CanSwitchCassetteSpeakerMode(player))
    {
        Log("[CassetteSpeaker] Get: player state does not allow switching callback=%p player=%p\n", callbackBase, player);
        return false;
    }

    void* fnAddr = ResolveCassettePlayerVtableFunction(player, kCassettePlayerGetSpeakerModeVtableOffset);
    if (!fnAddr)
    {
        Log("[CassetteSpeaker] Get: mode getter not resolved\n");
        return false;
    }

    CassettePlayerGetSpeakerMode_t getSpeakerMode =
        reinterpret_cast<CassettePlayerGetSpeakerMode_t>(fnAddr);

    __try
    {
        const int currentMode = getSpeakerMode(player);
        if (currentMode != static_cast<int>(kCassetteSpeakerModeDisabled) &&
            currentMode != static_cast<int>(kCassetteSpeakerModeEnabled))
        {
            Log("[CassetteSpeaker] Get: unexpected mode=%d callback=%p player=%p\n", currentMode, callbackBase, player);
            return false;
        }

        outEnabled = currentMode == static_cast<int>(kCassetteSpeakerModeEnabled);

        Log("[CassetteSpeaker] Get: callback=%p player=%p mode=%d enabled=%d\n", callbackBase, player, currentMode, outEnabled ? 1 : 0);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Log("[CassetteSpeaker] Get: exception while reading mode\n");
        return false;
    }
}


bool SetCassetteSpeakerEnabled(bool enabled)
{
    if (MissionCodeGuard::ShouldBypassHooks())
        return false;

    void* callbackBase = nullptr;
    void* player = nullptr;
    if (!ResolveCachedCassetteCallbackAndPlayer(callbackBase, player))
    {
        Log("[CassetteSpeaker] Set: cached callback/player not ready enabled=%d\n", enabled ? 1 : 0);
        return false;
    }

    if (!CanSwitchCassetteSpeakerMode(player))
    {
        Log("[CassetteSpeaker] Set: player state does not allow switching callback=%p player=%p enabled=%d\n", callbackBase, player, enabled ? 1 : 0);
        return false;
    }

    bool currentEnabled = false;
    if (!IsCassetteSpeakerEnabled(currentEnabled))
        return false;

    if (currentEnabled == enabled)
    {
        Log("[CassetteSpeaker] Set: already in requested state enabled=%d\n", enabled ? 1 : 0);
        return true;
    }

    void* fnAddr = ResolveCassettePlayerVtableFunction(player, kCassettePlayerSetSpeakerModeVtableOffset);
    if (!fnAddr)
    {
        Log("[CassetteSpeaker] Set: setter not resolved enabled=%d\n", enabled ? 1 : 0);
        return false;
    }

    CassettePlayerSetSpeakerMode_t setSpeakerMode =
        reinterpret_cast<CassettePlayerSetSpeakerMode_t>(fnAddr);

    std::uint8_t resultStorage[0x20] = {};

    __try
    {
        const std::uint32_t targetMode = enabled ? kCassetteSpeakerModeEnabled : kCassetteSpeakerModeDisabled;
        std::uint32_t* result = setSpeakerMode(player, resultStorage, targetMode);
        if (!result)
        {
            Log("[CassetteSpeaker] Set: setter returned null enabled=%d\n", enabled ? 1 : 0);
            return false;
        }

        const bool ok = ((*result >> 31) & 1u) == 0;

        Log(
            "[CassetteSpeaker] Set: callback=%p player=%p targetMode=%u enabled=%d ok=%d raw=%08X\n",
            callbackBase,
            player,
            static_cast<unsigned int>(targetMode),
            enabled ? 1 : 0,
            ok ? 1 : 0,
            static_cast<unsigned int>(*result));

        return ok;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Log("[CassetteSpeaker] Set: exception while calling setter enabled=%d\n", enabled ? 1 : 0);
        return false;
    }
}


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


bool Install_MbDvcCassetteTapeCallbackImpl_PlayOrPauseSelectedTrack_Hook()
{
    void* startTarget = ResolveGameAddress(gAddr.CassetteStart);
    if (!startTarget)
    {
        Log("[Hook] Cassette Start: address resolve failed\n");
        return false;
    }

    void* playTarget = ResolveGameAddress(gAddr.PlayOrPauseSelectedTrack);
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


bool Uninstall_MbDvcCassetteTapeCallbackImpl_PlayOrPauseSelectedTrack_Hook()
{
    DisableAndRemoveHook(ResolveGameAddress(gAddr.CassetteStart));
    DisableAndRemoveHook(ResolveGameAddress(gAddr.PlayOrPauseSelectedTrack));

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