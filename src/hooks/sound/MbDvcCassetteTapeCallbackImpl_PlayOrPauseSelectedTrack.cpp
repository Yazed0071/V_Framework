#include <pch.h>

#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <intrin.h>
#include <mutex>

#include "HookUtils.h"
#include "log.h"
#include "MissionCodeGuard.h"
#include "MbDvcCassetteTapeCallbackImpl_PlayOrPauseSelectedTrack.h"
#include "CustomTapeOwnership.h"
#include "SoundSystemImpl_BeginSoundSystem.h"
#include "CassetteWalkmanEvents.h"
#include "AddressSet.h"
#include "CassetteTrackPaging.h"

static int ReadU32FieldSEH(void* base, std::uintptr_t off, std::uint32_t* out)
{
    __try
    {
        *out = *reinterpret_cast<std::uint32_t*>(
            static_cast<std::uint8_t*>(base) + off);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
}
#include <LuaBroadcaster.h>

namespace
{
    using CassetteStart_t = void(__fastcall*)(void* cassetteInner);

    using PlayOrPauseSelectedTrack_t = bool(__fastcall*)(void* cassetteCallbackBase);

    using CassetteSeekStep_t = bool(__fastcall*)(void* cassetteCallbackBase);

    using CassettePlayerGetTrackId_t = std::uint32_t(__fastcall*)(void* player);
    using CassettePlayerGetTrackInfo_t = const std::uint8_t* (__fastcall*)(void* player, std::uint32_t trackId);
    using CassettePlayerGetAlbumInfo_t = const std::uint8_t* (__fastcall*)(void* player, std::uint64_t albumId);
    using CassettePlayerGetPlayingTime_t = std::uint32_t(__fastcall*)(void* player);
    using CassettePlayerSeek_t = std::uint32_t* (__fastcall*)(void* player, std::uint32_t* outResult, std::uint32_t positionMs);

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
    static constexpr std::size_t kCassettePlayerGetPlayingTrackIdVtableOffset = 0xE0ull;
    static constexpr std::size_t kCassettePlayerGetAlbumInfoVtableOffset = 0x170ull;
    static constexpr std::size_t kCassettePlayerGetTrackInfoVtableOffset = 0x180ull;
    static constexpr std::size_t kCassettePlayerSeekVtableOffset = 0x1C0ull;
    static constexpr std::size_t kCassettePlayerGetPlayingTimeVtableOffset = 0x200ull;

    static constexpr std::uintptr_t kCassetteInnerOffset = 0x20ull;

    static constexpr std::size_t kTrackAlbumId = 0x00ull;
    static constexpr std::size_t kTrackDataTime = 0x18ull;
    static constexpr std::size_t kTrackSaveIndex = 0x1Cull;
    static constexpr std::size_t kAlbumType = 0x12ull;
    static constexpr std::size_t kSmpPlayState = 0xD4ull;
    static constexpr std::size_t kSmpSubtitleId = 0x114ull;

    static constexpr std::uint16_t kAlbumTypeBriefing = 3u;
    static constexpr std::uint16_t kAlbumTypeSubtitleSeek = 8u;
    static constexpr std::uint32_t kTapSeekStepMs = 10000u;
    static constexpr int kPlayerStatePlaying = 2;
    static constexpr std::uintptr_t kPauseForSeekHeldReturn = 0x3AFAull;
    static constexpr std::uintptr_t kPauseForSeekButtonReturnBack = 0x37Bull;

    static constexpr std::uint8_t kSeekPrevSubtitlesEntry[] = {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x41, 0x30, 0x48, 0x8B, 0xD9, 0x48, 0x8B, 0x88,
        0xD8, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x01, 0xFF, 0x90, 0xD8, 0x00, 0x00, 0x00, 0x83, 0xC0, 0xFC,
        0x83, 0xF8, 0x01, 0x77, 0x08, 0x32, 0xC0, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3, 0x48, 0x8B, 0x43,
        0x30, 0x48, 0x8B, 0x88, 0xD8, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x01, 0xFF, 0x90, 0xE0, 0x00, 0x00 };

    static constexpr std::uint8_t kSeekNextSubtitlesEntry[] = {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x41, 0x30, 0x48, 0x8B, 0xD9, 0x48, 0x8B, 0x88,
        0xD8, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x01, 0xFF, 0x90, 0xD8, 0x00, 0x00, 0x00, 0x83, 0xC0, 0xFC,
        0x83, 0xF8, 0x01, 0x77, 0x08, 0x32, 0xC0, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3, 0x48, 0x8B, 0x43,
        0x30, 0x48, 0x89, 0x7C, 0x24, 0x38, 0x48, 0x8B, 0x88, 0xD8, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x01 };

    static constexpr std::uint8_t kPauseForSeekEntry[] = {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x41, 0x30, 0x48, 0x8B, 0xD9, 0x48, 0x8B, 0x88,
        0xD8, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x01, 0xFF, 0x90, 0xD8, 0x00, 0x00, 0x00, 0x83, 0xC0, 0xFC,
        0x83, 0xF8, 0x01, 0x77, 0x08, 0x32, 0xC0, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3, 0x48, 0x8B, 0x43,
        0x30, 0x48, 0x8B, 0x88, 0xD8, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x01, 0xFF, 0x90, 0xD8, 0x00, 0x00 };

    static constexpr std::uint32_t kCassetteSpeakerModeDisabled = 0u;
    static constexpr std::uint32_t kCassetteSpeakerModeEnabled = 1u;

    static constexpr std::size_t kTapeIdTableSize = 0x200ull;

    static CassetteStart_t g_OrigCassetteStart = nullptr;
    static PlayOrPauseSelectedTrack_t g_OrigPlayOrPauseSelectedTrack = nullptr;
    static MusicPlayerPlay_t g_OrigMusicPlayerPlay = nullptr;
    static CassetteSeekStep_t g_OrigSeekPrevSubtitles = nullptr;
    static CassetteSeekStep_t g_OrigSeekNextSubtitles = nullptr;
    static CassetteSeekStep_t g_OrigPauseForSeek = nullptr;
    static void* g_SeekPrevSubtitlesTarget = nullptr;
    static void* g_SeekNextSubtitlesTarget = nullptr;
    static void* g_PauseForSeekTarget = nullptr;

    static std::mutex g_CassettePlayHookMutex;

    static void* g_MusicPlayerPlayTarget = nullptr;
    static void* g_LastCassetteCallbackBase = nullptr;
    static void* g_LastMusicPlayer = nullptr;

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


static std::uint32_t ReadTapeIdTableEntry(const void* tapeIdTable, std::uint32_t index)
{
    if (!tapeIdTable || index >= 0x1000u)
        return 0;

    __try
    {
        return reinterpret_cast<const std::uint32_t*>(tapeIdTable)[index];
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
}


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


static bool IsModuleImagePtr(const void* p, bool requireExec)
{
    if (!p)
        return false;

    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(p, &mbi, sizeof(mbi)) != sizeof(mbi))
        return false;
    if (mbi.State != MEM_COMMIT || mbi.Type != MEM_IMAGE)
        return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
        return false;
    if (requireExec)
    {
        const DWORD exec =
            PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        if ((mbi.Protect & exec) == 0)
            return false;
    }
    return true;
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

        if (!IsModuleImagePtr(reinterpret_cast<const void*>(vtable), false))
            return nullptr;

        const std::uintptr_t target =
            *reinterpret_cast<const std::uintptr_t*>(vtable + kMusicPlayerPlayVtableOffset);
        if (!target)
            return nullptr;

        if (!IsModuleImagePtr(reinterpret_cast<const void*>(target), true))
            return nullptr;

        return reinterpret_cast<void*>(target);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
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
        LogWarn("CassettePlay: tape-id table unreadable, track selection may be wrong");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_CassettePlayHookMutex);

        std::memcpy(g_CachedTapeIdTable, tempBuffer, kTapeIdTableSize);
        g_HasCopiedTapeIdTable = true;
    }
}


static void InstallMusicPlayerPlayHookOnTarget(void* targetPtr)
{
    if (!targetPtr)
        return;

    std::lock_guard<std::mutex> lock(g_CassettePlayHookMutex);

    if (g_MusicPlayerPlayTarget != nullptr)
        return;

    const bool ok = CreateAndEnableHook(
        targetPtr,
        reinterpret_cast<void*>(&hkMusicPlayerPlay),
        reinterpret_cast<void**>(&g_OrigMusicPlayerPlay));

    if (!ok)
    {
        LogError("CassettePlay: MusicPlayer Play hook failed, direct cassette playback off");
        return;
    }

    g_MusicPlayerPlayTarget = targetPtr;
}


static void TryInstallMusicPlayerPlayHook(void* musicPlayer)
{
    if (!musicPlayer)
        return;

    void* targetPtr = ResolveMusicPlayerPlayTarget(musicPlayer);
    if (!targetPtr)
    {
        LogWarn("CassettePlay: MusicPlayer Play not found in vtable, direct playback unavailable");
        return;
    }

    InstallMusicPlayerPlayHookOnTarget(targetPtr);
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
    }

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

    std::uint32_t playResult = 0;
    if (g_OrigMusicPlayerPlay
        && ReadU32FieldSEH(outHandle, 0, &playResult)
        && static_cast<std::int32_t>(playResult) >= 0
        && !MissionCodeGuard::ShouldBypassHooks())
    {
        const std::uint32_t playedTrackId = ReadTapeIdTableEntry(tapeIdTable, selectedTrackIndex);
        if (playedTrackId != 0 && !IsPagerSentinelId(playedTrackId))
            OnCassetteTrackPlayedByTrackId(playedTrackId);

        if (!IsPagerSentinelId(playedTrackId))
            Emit_CassetteWalkmanStart(playedTrackId);
    }
}

static void __fastcall hkCassetteStart(void* cassetteInner)
{
    if (!g_OrigCassetteStart)
    {
        LogError("CassettePlay: Cassette Start trampoline null, cassette control broken");
        return;
    }

    g_OrigCassetteStart(cassetteInner);

    const bool bypass = MissionCodeGuard::ShouldBypassHooks();

    if (bypass || !cassetteInner)
        return;

    void* cassetteCallbackBase = static_cast<std::uint8_t*>(cassetteInner) - kCassetteInnerOffset;

    {
        std::lock_guard<std::mutex> lock(g_CassettePlayHookMutex);
        g_LastCassetteCallbackBase = cassetteCallbackBase;
    }

    CacheTapeIdTableFromCassetteCallback(cassetteCallbackBase);
}

static bool __fastcall hkPlayOrPauseSelectedTrack(void* cassetteCallbackBase)
{
    if (!g_OrigPlayOrPauseSelectedTrack)
    {
        LogError("CassettePlay: PlayOrPauseSelectedTrack trampoline null, cassette control broken");
        return false;
    }

    const bool bypass = MissionCodeGuard::ShouldBypassHooks();

    if (bypass)
        return g_OrigPlayOrPauseSelectedTrack(cassetteCallbackBase);

    {
        std::uint32_t selIdx = 0;
        if (ReadU32FieldSEH(cassetteCallbackBase, 0xF84ull, &selIdx))
        {
            const int dir = GetPagerActionForSlot(cassetteCallbackBase, selIdx);
            if (dir != 0)
            {
                FlipCassettePage(cassetteCallbackBase, dir);
                return true;
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(g_CassettePlayHookMutex);
        g_LastCassetteCallbackBase = cassetteCallbackBase;
    }

    CacheTapeIdTableFromCassetteCallback(cassetteCallbackBase);

    void* musicPlayer = ResolveMusicPlayerFromCassetteCallback(cassetteCallbackBase);
    if (musicPlayer)
    {
        TryInstallMusicPlayerPlayHook(musicPlayer);
    }
    else
    {
        LogWarn("CassettePlay: music player not found from cassette callback, direct playback may be unavailable");
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
        LogWarn("CassetteSpeaker: player not captured yet, open the Music Player once");
        return false;
    }

    if (!CanSwitchCassetteSpeakerMode(player))
    {
        LogWarn("CassetteSpeaker: player state blocks speaker switching, try while a tape plays");
        return false;
    }

    void* fnAddr = ResolveCassettePlayerVtableFunction(player, kCassettePlayerGetSpeakerModeVtableOffset);
    if (!fnAddr)
    {
        LogError("CassetteSpeaker: speaker-mode getter not found in vtable, speaker toggle off");
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
            LogWarn("CassetteSpeaker: unexpected speaker mode %d, state not reported", currentMode);
            return false;
        }

        outEnabled = currentMode == static_cast<int>(kCassetteSpeakerModeEnabled);

        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        LogWarn("CassetteSpeaker: fault reading speaker mode, state not reported");
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
        LogWarn("CassetteSpeaker: player not captured yet, open the Music Player once");
        return false;
    }

    if (!CanSwitchCassetteSpeakerMode(player))
    {
        LogWarn("CassetteSpeaker: player state blocks speaker switching, try while a tape plays");
        return false;
    }

    bool currentEnabled = false;
    if (!IsCassetteSpeakerEnabled(currentEnabled))
        return false;

    if (currentEnabled == enabled)
    {
        return true;
    }

    void* fnAddr = ResolveCassettePlayerVtableFunction(player, kCassettePlayerSetSpeakerModeVtableOffset);
    if (!fnAddr)
    {
        LogError("CassetteSpeaker: speaker-mode setter not found in vtable, speaker toggle off");
        return false;
    }

    CassettePlayerSetSpeakerMode_t setSpeakerMode =
        reinterpret_cast<CassettePlayerSetSpeakerMode_t>(fnAddr);

    std::uint8_t resultStorage[0x20] = {};

    __try
    {
        const std::uint32_t targetMode = enabled ? kCassetteSpeakerModeEnabled : kCassetteSpeakerModeDisabled;
        Set_CassetteWalkmanProgrammatic(true);
        std::uint32_t* result = setSpeakerMode(player, resultStorage, targetMode);
        Set_CassetteWalkmanProgrammatic(false);
        if (!result)
        {
            LogWarn("CassetteSpeaker: setter returned null (enabled=%d), mode unchanged", enabled ? 1 : 0);
            return false;
        }

        const bool ok = ((*result >> 31) & 1u) == 0;

        return ok;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Set_CassetteWalkmanProgrammatic(false);
        LogWarn("CassetteSpeaker: fault setting speaker mode (enabled=%d), mode unchanged", enabled ? 1 : 0);
        return false;
    }
}


static void* ResolveSoundMusicPlayerFromMusicManager()
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

struct CustomTapeSeekState
{
    void* player = nullptr;
    const std::uint8_t* track = nullptr;
    std::uint32_t trackId = 0;
    std::int16_t saveIndex = -1;
    std::uint16_t albumType = 0;
    int playerState = -1;
    std::uint32_t rawPlayState = 0;
    std::uint32_t playMode = 0;
    std::uint32_t pausedPlayMode = 0;
    std::uint32_t playingId = 0;
    std::uint8_t mediaPath = 0;
    std::uint8_t positional = 0;
    float cooldown = 0.0f;
    std::int32_t subtitleId = -1;
};


static bool ReadCustomTapeSeekState(void* cassetteCallbackBase, CustomTapeSeekState& out)
{
    out = CustomTapeSeekState{};

    void* player = ResolveMusicPlayerFromCassetteCallback(cassetteCallbackBase);
    void* smp = ResolveSoundMusicPlayerFromMusicManager();
    if (!player || !smp)
        return false;

    void* getStateFn = ResolveCassettePlayerVtableFunction(player, kCassettePlayerGetStateVtableOffset);
    void* getTrackIdFn = ResolveCassettePlayerVtableFunction(player, kCassettePlayerGetPlayingTrackIdVtableOffset);
    void* getTrackFn = ResolveCassettePlayerVtableFunction(player, kCassettePlayerGetTrackInfoVtableOffset);
    void* getAlbumFn = ResolveCassettePlayerVtableFunction(player, kCassettePlayerGetAlbumInfoVtableOffset);
    if (!getStateFn || !getTrackIdFn || !getTrackFn || !getAlbumFn)
        return false;

    __try
    {
        const std::uintptr_t smpBase = reinterpret_cast<std::uintptr_t>(smp);

        out.player = player;
        out.playerState = reinterpret_cast<CassettePlayerGetState_t>(getStateFn)(player);
        out.rawPlayState = *reinterpret_cast<const std::uint32_t*>(smpBase + kSmpPlayState);
        out.playMode = *reinterpret_cast<const std::uint32_t*>(smpBase + 0xACull);
        out.pausedPlayMode = *reinterpret_cast<const std::uint32_t*>(smpBase + 0xB0ull);
        out.playingId = *reinterpret_cast<const std::uint32_t*>(smpBase + 0xA0ull);
        out.cooldown = *reinterpret_cast<const float*>(smpBase + 0xC0ull);
        out.mediaPath = *reinterpret_cast<const std::uint8_t*>(smpBase + 0x11Dull);
        out.positional = *reinterpret_cast<const std::uint8_t*>(smpBase + 0x122ull);
        out.subtitleId = *reinterpret_cast<const std::int32_t*>(smpBase + kSmpSubtitleId);

        out.trackId = reinterpret_cast<CassettePlayerGetTrackId_t>(getTrackIdFn)(player);
        out.track = reinterpret_cast<CassettePlayerGetTrackInfo_t>(getTrackFn)(player, out.trackId);
        if (!out.track)
            return false;

        out.saveIndex = *reinterpret_cast<const std::int16_t*>(out.track + kTrackSaveIndex);

        const std::uint8_t* album = reinterpret_cast<CassettePlayerGetAlbumInfo_t>(getAlbumFn)(
            player, *reinterpret_cast<const std::uint64_t*>(out.track + kTrackAlbumId));
        if (!album)
            return false;

        out.albumType = *reinterpret_cast<const std::uint16_t*>(album + kAlbumType);
        return IsCustomTapeSaveIndex(out.saveIndex);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}


static bool TryCustomTapeTapSeek(void* cassetteCallbackBase, bool forward)
{
    CustomTapeSeekState state;
    if (!ReadCustomTapeSeekState(cassetteCallbackBase, state))
        return false;

    if (static_cast<unsigned int>(state.playerState - 4) < 2u)
        return false;

    if (state.albumType != kAlbumTypeBriefing && state.albumType != kAlbumTypeSubtitleSeek)
        return false;

    if (state.subtitleId != -1)
        return false;

    void* getTimeFn = ResolveCassettePlayerVtableFunction(state.player, kCassettePlayerGetPlayingTimeVtableOffset);
    void* seekFn = ResolveCassettePlayerVtableFunction(state.player, kCassettePlayerSeekVtableOffset);
    if (!getTimeFn || !seekFn)
        return false;

    __try
    {
        const std::uint32_t position = reinterpret_cast<CassettePlayerGetPlayingTime_t>(getTimeFn)(state.player);

        std::uint32_t target = 0;
        if (forward)
        {
            target = position + kTapSeekStepMs;
            if (target >= *reinterpret_cast<const std::uint32_t*>(state.track + kTrackDataTime))
                return false;
        }
        else
        {
            if (position <= kTapSeekStepMs)
                return false;
            target = position - kTapSeekStepMs;
        }

        std::uint32_t result = 0xFFFFFFFFu;
        const std::uint32_t* status = reinterpret_cast<CassettePlayerSeek_t>(seekFn)(state.player, &result, target);
        const bool ok = status != nullptr && static_cast<std::int32_t>(*status) >= 0;

        LogDebug("CassetteSeek: tap %s on custom track %u (album type %u, no subtitles) %u -> %u ms, %s",
            forward ? "forward" : "back", state.trackId, static_cast<unsigned int>(state.albumType),
            position, target, ok ? "seeked" : "refused");
        return ok;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}


static bool __fastcall hkSeekPrevSubtitles(void* cassetteCallbackBase)
{
    const bool handled = g_OrigSeekPrevSubtitles ? g_OrigSeekPrevSubtitles(cassetteCallbackBase) : false;
    if (handled || MissionCodeGuard::ShouldBypassHooks())
        return handled;

    return TryCustomTapeTapSeek(cassetteCallbackBase, false);
}


static bool __fastcall hkSeekNextSubtitles(void* cassetteCallbackBase)
{
    const bool handled = g_OrigSeekNextSubtitles ? g_OrigSeekNextSubtitles(cassetteCallbackBase) : false;
    if (handled || MissionCodeGuard::ShouldBypassHooks())
        return handled;

    return TryCustomTapeTapSeek(cassetteCallbackBase, true);
}


static bool __fastcall hkPauseForSeek(void* cassetteCallbackBase)
{
    if (!g_OrigPauseForSeek)
        return false;

    if (MissionCodeGuard::ShouldBypassHooks())
        return g_OrigPauseForSeek(cassetteCallbackBase);

    const std::uintptr_t returnAddress = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const std::uintptr_t entry = reinterpret_cast<std::uintptr_t>(g_PauseForSeekTarget);
    const bool fromHeldSeek = entry != 0 && returnAddress == entry + kPauseForSeekHeldReturn;
    const bool fromPauseButton = entry != 0 && returnAddress == entry - kPauseForSeekButtonReturnBack;

    CustomTapeSeekState before;
    const bool custom = ReadCustomTapeSeekState(cassetteCallbackBase, before);

    const bool ready = g_OrigPauseForSeek(cassetteCallbackBase);

    if (custom)
    {
        LogDebug("CassetteSeek: %s on custom track %u (album type %u): state %d raw %u mode %u/%u playing id %u media %u positional %u cooldown %.2f subtitle %d -> %s",
            fromHeldSeek ? "held seek" : (fromPauseButton ? "pause button" : "pause helper"),
            before.trackId, static_cast<unsigned int>(before.albumType), before.playerState, before.rawPlayState,
            before.playMode, before.pausedPlayMode, before.playingId, static_cast<unsigned int>(before.mediaPath),
            static_cast<unsigned int>(before.positional), static_cast<double>(before.cooldown), before.subtitleId,
            ready ? "ready" : "refused");

        if (fromHeldSeek && !ready && before.playerState == kPlayerStatePlaying)
            LogWarn("CassetteSeek: custom track %u did not pause for a held seek (raw %u mode %u/%u playing id %u media %u positional %u cooldown %.2f)",
                before.trackId, before.rawPlayState, before.playMode, before.pausedPlayMode, before.playingId,
                static_cast<unsigned int>(before.mediaPath), static_cast<unsigned int>(before.positional),
                static_cast<double>(before.cooldown));
    }

    return ready;
}


static void InstallCassetteSeekHook(
    std::uintptr_t address,
    const std::uint8_t* entry,
    std::size_t entrySize,
    void* detour,
    CassetteSeekStep_t* original,
    void*& outTarget,
    const char* what,
    const char* fallback)
{
    void* target = ResolveGameAddress(address);
    if (!target)
    {
        LogWarn("%s: address missing for %s, %s", what, GetGameBuildName(gGameBuild), fallback);
        return;
    }

    const HookEntry state = CheckHookEntry(target, entry, entrySize);
    if (state == HookEntry::Unknown)
    {
        LogError("%s differs on %s, %s", what, GetGameBuildName(gGameBuild), fallback);
        return;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop(what, target);

    if (!CreateAndEnableHook(target, detour, reinterpret_cast<void**>(original)))
    {
        LogError("%s hook failed at %p, %s", what, target, fallback);
        *original = nullptr;
        return;
    }

    outTarget = target;
}


static void* ResolveStaticPlayWrapperFromVtable()
{
    void* vtbl = ResolveGameAddress(gAddr.CassettePlayerVtable);
    if (!vtbl || !IsModuleImagePtr(vtbl, false))
        return nullptr;

    __try
    {
        void* target = *reinterpret_cast<void**>(
            reinterpret_cast<std::uintptr_t>(vtbl) + kMusicPlayerPlayVtableOffset);
        if (target && IsModuleImagePtr(target, true))
            return target;
        return nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
}


static bool ResolveDirectPlayState(MusicPlayerPlay_t& outPlayFn, void*& outPlayer)
{
    outPlayFn = nullptr;
    outPlayer = nullptr;

    void* callbackBase = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_CassettePlayHookMutex);
        outPlayFn = g_OrigMusicPlayerPlay;
        outPlayer = g_LastMusicPlayer;
        callbackBase = g_LastCassetteCallbackBase;
    }

    void* callbackPlayer = callbackBase ? ResolveMusicPlayerFromCassetteCallback(callbackBase) : nullptr;
    void* smp = ResolveSoundMusicPlayerFromMusicManager();

    if (!outPlayer)
        outPlayer = callbackPlayer;
    if (!outPlayer)
        outPlayer = smp;

    if (!outPlayFn && outPlayer)
    {
        void* target = ResolveMusicPlayerPlayTarget(outPlayer);
        if (target)
            outPlayFn = reinterpret_cast<MusicPlayerPlay_t>(target);
    }

    if (!outPlayFn)
    {
        void* staticWrapper = ResolveStaticPlayWrapperFromVtable();
        if (staticWrapper)
        {
            outPlayFn = reinterpret_cast<MusicPlayerPlay_t>(staticWrapper);
            if (!outPlayer)
                outPlayer = smp;
        }
    }

    return (outPlayFn != nullptr && outPlayer != nullptr);
}


bool Install_MbDvcCassetteTapeCallbackImpl_PlayOrPauseSelectedTrack_Hook()
{
    void* startTarget = ResolveGameAddress(gAddr.CassetteStart);
    if (!startTarget)
    {
        LogError("CassettePlay: Cassette Start address missing on this build, cassette control off");
        return false;
    }

    void* playTarget = ResolveGameAddress(gAddr.PlayOrPauseSelectedTrack);
    if (!playTarget)
    {
        LogError("CassettePlay: PlayOrPauseSelectedTrack address missing on this build, cassette control off");
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

    if (!okStart)
        LogError("CassettePlay: Cassette Start hook failed, cassette control off");
    if (!okPlay)
        LogError("CassettePlay: PlayOrPauseSelectedTrack hook failed, cassette control off");

    void* playWrapper = ResolveGameAddress(gAddr.MusicPlayerPlayWrapper);
    if (playWrapper)
        InstallMusicPlayerPlayHookOnTarget(playWrapper);

    InstallCassetteSeekHook(
        gAddr.MbDvcSeekPrevSubtitles, kSeekPrevSubtitlesEntry, sizeof(kSeekPrevSubtitlesEntry),
        reinterpret_cast<void*>(&hkSeekPrevSubtitles), &g_OrigSeekPrevSubtitles, g_SeekPrevSubtitlesTarget,
        "CassetteSeek: SeekPrevSubtitles", "custom briefing tapes without subtitles keep a dead rewind tap");

    InstallCassetteSeekHook(
        gAddr.MbDvcSeekNextSubtitles, kSeekNextSubtitlesEntry, sizeof(kSeekNextSubtitlesEntry),
        reinterpret_cast<void*>(&hkSeekNextSubtitles), &g_OrigSeekNextSubtitles, g_SeekNextSubtitlesTarget,
        "CassetteSeek: SeekNextSubtitles", "custom briefing tapes without subtitles keep a dead fast-forward tap");

    InstallCassetteSeekHook(
        gAddr.MbDvcPauseForSeek, kPauseForSeekEntry, sizeof(kPauseForSeekEntry),
        reinterpret_cast<void*>(&hkPauseForSeek), &g_OrigPauseForSeek, g_PauseForSeekTarget,
        "CassetteSeek: PauseForSeek", "no hold-seek report for custom tapes");

    return okStart && okPlay;
}


bool Uninstall_MbDvcCassetteTapeCallbackImpl_PlayOrPauseSelectedTrack_Hook()
{
    DisableAndRemoveHook(ResolveGameAddress(gAddr.CassetteStart));
    DisableAndRemoveHook(ResolveGameAddress(gAddr.PlayOrPauseSelectedTrack));

    if (g_SeekPrevSubtitlesTarget)
        DisableAndRemoveHook(g_SeekPrevSubtitlesTarget);
    if (g_SeekNextSubtitlesTarget)
        DisableAndRemoveHook(g_SeekNextSubtitlesTarget);
    if (g_PauseForSeekTarget)
        DisableAndRemoveHook(g_PauseForSeekTarget);

    g_SeekPrevSubtitlesTarget = nullptr;
    g_SeekNextSubtitlesTarget = nullptr;
    g_PauseForSeekTarget = nullptr;
    g_OrigSeekPrevSubtitles = nullptr;
    g_OrigSeekNextSubtitles = nullptr;
    g_OrigPauseForSeek = nullptr;

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

        std::memset(g_CachedTapeIdTable, 0, sizeof(g_CachedTapeIdTable));
        g_HasCopiedTapeIdTable = false;
        std::memset(g_DirectTrackTable, 0, sizeof(g_DirectTrackTable));
    }

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
        LogWarn("CassettePlay: no cassette player captured, open the Music Player once");
        return false;
    }

    std::uint64_t outHandle = UINT64_MAX;

    std::memset(g_DirectTrackTable, 0, sizeof(g_DirectTrackTable));
    g_DirectTrackTable[0] = trackId;

    const std::uint32_t playMode = 0;

    const std::uint32_t trackCount = 1;
    const std::uint32_t selectedTrackIndex = 0;
    const std::uint32_t reservedZero = 0;
    const std::uint8_t flag1 = loopPlay ? 1 : 0;
    const std::uint8_t flag2 = playAll ? 1 : 0;

    Set_CassetteWalkmanProgrammatic(true);
    playFn(
        player,
        &outHandle,
        playMode,
        g_DirectTrackTable,
        trackCount,
        selectedTrackIndex,
        reservedZero,
        flag1,
        flag2);
    const std::int32_t playResult =
        static_cast<std::int32_t>(static_cast<std::uint32_t>(outHandle));
    if (playResult >= 0)
        Emit_CassetteWalkmanStart(trackId);
    Set_CassetteWalkmanProgrammatic(false);

    if (playResult < 0)
        LogWarn("CassettePlay: trackId %u rejected (result %d), music system not ready", trackId, playResult);

    return playResult >= 0;
}
