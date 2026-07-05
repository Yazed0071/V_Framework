#include "pch.h"

#include <Windows.h>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>

#include "HookUtils.h"
#include "log.h"
#include "FoxHashes.h"
#include "SoundMusicPlayer_SetupMusicInfos.h"
#include "CustomTapeOwnership.h"
#include "AddressSet.h"
#include "Control_PostExternalEvent.h"

namespace
{
    using SetupMusicInfos_t = void(__fastcall*)(void* thisPtr);
    using KernelAllocAligned_t = void* (__fastcall*)(std::uint64_t size, std::uint64_t alignment, std::uint32_t tag);
    using ArrayBaseFree_t = void(__fastcall*)(void* data, std::uint32_t tag);
    using SubtitleManagerGet_t = void* (__fastcall*)();
    using GetVoiceLanguage_t = int(__fastcall*)(void* subtitleManager);

    static constexpr std::uint32_t kFoxAllocTag = 0x5006Fu;
    static constexpr std::size_t kFileNameMaxChars = 15u;
    static constexpr std::size_t kFileNameMaxCharsLong = 27u;

    static SetupMusicInfos_t g_OrigSetupMusicInfos = nullptr;
    static void* g_LastSoundMusicPlayer = nullptr;

    struct CustomTapeRegistry
    {
        std::vector<CustomTapeAlbumDefinition> albums;
        std::vector<CustomTapeTrackDefinition> tracks;
    };

    static std::mutex g_CustomTapeMutex;
    static CustomTapeRegistry g_CustomTapeRegistry;
}

#pragma pack(push, 1)


struct TapeAlbumRecord
{
    std::uint64_t albumId;
    std::uint64_t langId;
    std::uint16_t trackCount;
    std::uint16_t type;
    std::uint32_t pad14;
};


struct TapeTrackRecord
{
    std::uint64_t albumId;
    std::uint64_t langId;
    std::uint32_t fileNameStrCode;
    std::uint32_t directPlayTrackId;
    std::uint32_t dataTime;
    std::int16_t  saveIndex;
    std::int16_t  albumTrackIndex;
    std::uint16_t sourceType;
    std::uint16_t special;
    std::uint16_t important;
    char          fileName[0x10];
    std::uint8_t  reserved[2];
};

#pragma pack(pop)

static_assert(sizeof(TapeAlbumRecord) == 0x18, "TapeAlbumRecord size mismatch");
static_assert(offsetof(TapeTrackRecord, albumId) == 0x00, "TapeTrackRecord::albumId offset mismatch");
static_assert(offsetof(TapeTrackRecord, langId) == 0x08, "TapeTrackRecord::langId offset mismatch");
static_assert(offsetof(TapeTrackRecord, fileNameStrCode) == 0x10, "TapeTrackRecord::fileNameStrCode offset mismatch");
static_assert(offsetof(TapeTrackRecord, directPlayTrackId) == 0x14, "TapeTrackRecord::directPlayTrackId offset mismatch");
static_assert(offsetof(TapeTrackRecord, dataTime) == 0x18, "TapeTrackRecord::dataTime offset mismatch");
static_assert(offsetof(TapeTrackRecord, saveIndex) == 0x1C, "TapeTrackRecord::saveIndex offset mismatch");
static_assert(offsetof(TapeTrackRecord, albumTrackIndex) == 0x1E, "TapeTrackRecord::albumTrackIndex offset mismatch");
static_assert(offsetof(TapeTrackRecord, sourceType) == 0x20, "TapeTrackRecord::sourceType offset mismatch");
static_assert(offsetof(TapeTrackRecord, special) == 0x22, "TapeTrackRecord::special offset mismatch");
static_assert(offsetof(TapeTrackRecord, important) == 0x24, "TapeTrackRecord::important offset mismatch");
static_assert(offsetof(TapeTrackRecord, fileName) == 0x26, "TapeTrackRecord::fileName offset mismatch");
static_assert(sizeof(TapeTrackRecord) == 0x38, "TapeTrackRecord size mismatch");

namespace
{


    struct PreparedCustomAlbum
    {
        std::string albumId;
        std::uint64_t albumIdHash = 0;
        std::uint64_t langIdHash = 0;
        std::uint16_t type = 0;
    };


    struct PreparedCustomTrack
    {
        std::string albumId;
        std::string fileName;
        std::uint64_t albumIdHash = 0;
        std::uint64_t langIdHash = 0;
        std::uint32_t fileNameStrCode = 0;
        std::uint32_t dataTime = 0;
        std::int16_t requestedSaveIndex = -1;
        std::int16_t resolvedSaveIndex = -1;
        std::uint16_t special = 0;
        std::uint16_t important = 0;
        bool unlocked = false;
        bool usePersistentSaveIndex = true;
    };


    struct SoundMusicPlayerSnapshot
    {
        TapeAlbumRecord* oldAlbums = nullptr;
        TapeTrackRecord* oldTracks = nullptr;
        std::uint32_t oldLuaAlbumCount = 0;
        std::uint32_t oldTotalAlbumCount = 0;
        std::uint32_t oldLuaTrackCount = 0;
        std::uint32_t oldTotalTrackCount = 0;
    };
}


static std::unordered_map<std::int16_t, bool> g_CustomTapeImportantBySaveIndex;
static std::mutex g_CustomTapeImportantMutex;

bool IsCustomTapeImportantBySaveIndex(std::int16_t saveIndex)
{
    std::lock_guard<std::mutex> lock(g_CustomTapeImportantMutex);
    const auto it = g_CustomTapeImportantBySaveIndex.find(saveIndex);
    return it != g_CustomTapeImportantBySaveIndex.end() && it->second;
}


static void* GameAllocAligned(std::uint64_t sizeBytes, std::uint64_t alignment)
{
    void* fnAddr = ResolveGameAddress(gAddr.KernelAllocAligned);
    if (!fnAddr)
        return nullptr;

    KernelAllocAligned_t kernelAllocAligned =
        reinterpret_cast<KernelAllocAligned_t>(fnAddr);

    return kernelAllocAligned(sizeBytes, alignment, kFoxAllocTag);
}


static void GameFree(void* data)
{
    if (!data)
        return;

    void* fnAddr = ResolveGameAddress(gAddr.ArrayBaseFree);
    if (!fnAddr)
        return;

    ArrayBaseFree_t arrayBaseFree =
        reinterpret_cast<ArrayBaseFree_t>(fnAddr);

    arrayBaseFree(data, kFoxAllocTag);
}


static bool IsJapaneseVoiceLanguage()
{
    void* getMgrAddr = ResolveGameAddress(gAddr.SubtitleManager_Get);
    void* getLangAddr = ResolveGameAddress(gAddr.GetVoiceLanguage);
    if (!getMgrAddr || !getLangAddr)
        return false;

    SubtitleManagerGet_t subtitleManagerGet =
        reinterpret_cast<SubtitleManagerGet_t>(getMgrAddr);
    GetVoiceLanguage_t getVoiceLanguage =
        reinterpret_cast<GetVoiceLanguage_t>(getLangAddr);

    __try
    {
        void* subtitleManager = subtitleManagerGet();
        if (!subtitleManager)
            return false;

        return getVoiceLanguage(subtitleManager) == 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}


static bool ReadSoundMusicPlayerSnapshot(
    void* soundMusicPlayer,
    SoundMusicPlayerSnapshot& outSnapshot)
{
    if (!soundMusicPlayer)
        return false;

    std::memset(&outSnapshot, 0, sizeof(outSnapshot));

    __try
    {
        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(soundMusicPlayer);

        outSnapshot.oldAlbums = *reinterpret_cast<TapeAlbumRecord**>(base + 0x68ull);
        outSnapshot.oldTracks = *reinterpret_cast<TapeTrackRecord**>(base + 0x70ull);

        outSnapshot.oldLuaAlbumCount = *reinterpret_cast<std::uint32_t*>(base + 0xE4ull);
        outSnapshot.oldTotalAlbumCount = *reinterpret_cast<std::uint32_t*>(base + 0xDCull);
        outSnapshot.oldLuaTrackCount = *reinterpret_cast<std::uint32_t*>(base + 0xE8ull);
        outSnapshot.oldTotalTrackCount = *reinterpret_cast<std::uint32_t*>(base + 0xE0ull);

        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}


static bool WriteSoundMusicPlayerSnapshot(
    void* soundMusicPlayer,
    TapeAlbumRecord* oldAlbums,
    TapeTrackRecord* oldTracks,
    TapeAlbumRecord* newAlbums,
    TapeTrackRecord* newTracks,
    std::uint32_t newLuaAlbumCount,
    std::uint32_t newTotalAlbumCount,
    std::uint32_t newLuaTrackCount,
    std::uint32_t newTotalTrackCount)
{
    if (!soundMusicPlayer)
        return false;

    __try
    {
        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(soundMusicPlayer);

        *reinterpret_cast<TapeAlbumRecord**>(base + 0x68ull) = newAlbums;
        *reinterpret_cast<TapeTrackRecord**>(base + 0x70ull) = newTracks;

        *reinterpret_cast<std::uint32_t*>(base + 0xE4ull) = newLuaAlbumCount;
        *reinterpret_cast<std::uint32_t*>(base + 0xDCull) = newTotalAlbumCount;
        *reinterpret_cast<std::uint32_t*>(base + 0xE8ull) = newLuaTrackCount;
        *reinterpret_cast<std::uint32_t*>(base + 0xE0ull) = newTotalTrackCount;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }

    GameFree(oldAlbums);
    GameFree(oldTracks);
    return true;
}


static const TapeAlbumRecord* FindAlbumRecordByHash(
    const TapeAlbumRecord* albums,
    std::uint32_t count,
    std::uint64_t albumIdHash)
{
    if (!albums || count == 0 || albumIdHash == 0)
        return nullptr;

    for (std::uint32_t i = 0; i < count; ++i)
    {
        if (albums[i].albumId == albumIdHash)
            return &albums[i];
    }

    return nullptr;
}


static TapeAlbumRecord* FindMutableAlbumRecordByHash(
    TapeAlbumRecord* albums,
    std::uint32_t count,
    std::uint64_t albumIdHash)
{
    if (!albums || count == 0 || albumIdHash == 0)
        return nullptr;

    for (std::uint32_t i = 0; i < count; ++i)
    {
        if (albums[i].albumId == albumIdHash)
            return &albums[i];
    }

    return nullptr;
}


static const TapeTrackRecord* FindTrackRecordByHash(
    const TapeTrackRecord* tracks,
    std::uint32_t count,
    std::uint64_t albumIdHash,
    std::uint64_t langIdHash)
{
    if (!tracks || count == 0 || albumIdHash == 0 || langIdHash == 0)
        return nullptr;

    for (std::uint32_t i = 0; i < count; ++i)
    {
        if (tracks[i].albumId == albumIdHash && tracks[i].langId == langIdHash)
            return &tracks[i];
    }

    return nullptr;
}


static void CopyCustomTapeRegistry(CustomTapeRegistry& outRegistry)
{
    std::lock_guard<std::mutex> lock(g_CustomTapeMutex);
    outRegistry = g_CustomTapeRegistry;
}


static const char* GetStockAlbumIdForTypeName(const std::string& typeName)
{
    if (typeName == "PREINSTALL_MISSION_INFO")
        return "tp_mission_01";

    if (typeName == "PREINSTALL_MUSIC")
        return "tp_bgm_10";

    if (typeName == "PREINSTALL_BASE")
        return "tp_bgm_11";

    if (typeName == "PREINSTALL_BRIEFING")
        return "tp_etc_0010";

    if (typeName == "PREINSTALL_SPECIAL")
        return "tp_sp_01_01";

    return nullptr;
}


static bool ShouldUseStockMissionInfoSaveIndex(
    const std::string& albumId,
    std::int16_t requestedSaveIndex)
{
    return albumId == "tp_mission_01" && requestedSaveIndex < 0;
}


static void PrepareCustomAlbums(
    const CustomTapeRegistry& registry,
    const TapeAlbumRecord* existingAlbums,
    std::uint32_t existingAlbumCount,
    std::vector<PreparedCustomAlbum>& outAlbums)
{
    outAlbums.clear();

    std::unordered_set<std::uint64_t> seenCustomAlbumIds;

    for (const CustomTapeAlbumDefinition& def : registry.albums)
    {
        if (def.albumId.empty() || def.langId.empty())
        {
            Log("[CustomTapes] WARN: album skipped — albumId or langId is missing; the album will not appear.\n");
            continue;
        }

        const std::uint64_t albumIdHash = FoxHashes::StrCode64(def.albumId);
        const std::uint64_t langIdHash = FoxHashes::StrCode64(def.langId);

        if (albumIdHash == 0 || langIdHash == 0)
        {
            Log("[CustomTapes] WARN: album '%s' skipped — albumId/langId hashed to 0; the album will not appear.\n", def.albumId.c_str());
            continue;
        }

        if (FindAlbumRecordByHash(existingAlbums, existingAlbumCount, albumIdHash) != nullptr)
        {
            continue;
        }

        if (!seenCustomAlbumIds.insert(albumIdHash).second)
        {
            continue;
        }

        std::uint16_t resolvedType = 0;
        bool hasResolvedType = false;

        if (def.typeValue >= 0)
        {
            resolvedType = static_cast<std::uint16_t>(def.typeValue);
            hasResolvedType = true;
        }
        else if (!def.type.empty())
        {
            const char* stockAlbumId = GetStockAlbumIdForTypeName(def.type);
            if (!stockAlbumId)
            {
                Log(
                    "[CustomTapes] WARN: album '%s' skipped — unsupported type '%s'; the album will not appear.\n",
                    def.albumId.c_str(),
                    def.type.c_str());
                continue;
            }

            const std::uint64_t stockAlbumIdHash = FoxHashes::StrCode64(stockAlbumId);
            const TapeAlbumRecord* stockAlbum =
                FindAlbumRecordByHash(existingAlbums, existingAlbumCount, stockAlbumIdHash);

            if (!stockAlbum)
            {
                Log(
                    "[CustomTapes] WARN: album '%s' skipped — stock type exemplar '%s' (%s) not found in game data; the album will not appear.\n",
                    def.albumId.c_str(),
                    def.type.c_str(),
                    stockAlbumId);
                continue;
            }

            resolvedType = stockAlbum->type;
            hasResolvedType = true;
        }

        if (!hasResolvedType)
        {
            Log("[CustomTapes] WARN: album '%s' skipped — no type/typeValue provided; the album will not appear.\n", def.albumId.c_str());
            continue;
        }

        PreparedCustomAlbum prepared;
        prepared.albumId = def.albumId;
        prepared.albumIdHash = albumIdHash;
        prepared.langIdHash = langIdHash;
        prepared.type = resolvedType;

        outAlbums.push_back(prepared);
    }
}


static void PrepareCustomTracks(
    const CustomTapeRegistry& registry,
    bool isJapaneseVoice,
    const TapeAlbumRecord* existingAlbums,
    std::uint32_t existingAlbumCount,
    const TapeTrackRecord* existingTracks,
    std::uint32_t existingTrackCount,
    const std::vector<PreparedCustomAlbum>& validAlbums,
    std::vector<PreparedCustomTrack>& outTracks)
{
    outTracks.clear();

    std::unordered_set<std::uint64_t> validAlbumIds;
    for (const PreparedCustomAlbum& album : validAlbums)
    {
        validAlbumIds.insert(album.albumIdHash);
    }

    for (const CustomTapeTrackDefinition& def : registry.tracks)
    {
        if (def.albumId.empty() || def.langId.empty() || def.fileName.empty())
        {
            Log("[CustomTapes] WARN: tape track skipped — albumId, langId, or fileName is missing; the track will not appear.\n");
            continue;
        }

        const std::size_t fileNameCap =
            IsCustomTapeLongFilenameHookActive() ? kFileNameMaxCharsLong : kFileNameMaxChars;
        if (def.fileName.size() > fileNameCap)
            continue;

        if (def.fileName.size() > kFileNameMaxChars)
            Register_CustomTapeLongFilename(FoxHashes::StrCode32(def.fileName), def.fileName.c_str());

        const std::uint64_t albumIdHash = FoxHashes::StrCode64(def.albumId);
        const std::uint64_t langIdHash = FoxHashes::StrCode64(def.langId);
        const std::uint32_t fileNameStrCode = FoxHashes::StrCode32(def.fileName);

        if (albumIdHash == 0 || langIdHash == 0 || fileNameStrCode == 0)
        {
            Log("[CustomTapes] WARN: tape track '%s' skipped — albumId/langId/fileName hashed to 0; the track will not appear.\n", def.fileName.c_str());
            continue;
        }

        const bool acceptedCustomAlbum =
            validAlbumIds.find(albumIdHash) != validAlbumIds.end();

        const bool acceptedExistingAlbum =
            FindAlbumRecordByHash(existingAlbums, existingAlbumCount, albumIdHash) != nullptr;

        if (!acceptedCustomAlbum && !acceptedExistingAlbum)
        {
            Log(
                "[CustomTapes] WARN: tape track '%s' skipped — its album '%s' does not exist and was not registered; the track will not appear.\n",
                def.fileName.c_str(),
                def.albumId.c_str());
            continue;
        }

        if (FindTrackRecordByHash(existingTracks, existingTrackCount, albumIdHash, langIdHash) != nullptr)
        {
            continue;
        }

        PreparedCustomTrack prepared;
        prepared.albumId = def.albumId;
        prepared.fileName = def.fileName;
        prepared.albumIdHash = albumIdHash;
        prepared.langIdHash = langIdHash;
        prepared.fileNameStrCode = fileNameStrCode;
        prepared.requestedSaveIndex = def.saveIndex;
        prepared.resolvedSaveIndex = def.saveIndex;
        prepared.special = def.special;
        prepared.important = def.important;
        prepared.unlocked = def.unlocked;
        prepared.usePersistentSaveIndex =
            !ShouldUseStockMissionInfoSaveIndex(def.albumId, def.saveIndex);

        prepared.dataTime = isJapaneseVoice ? def.dataTimeJp : def.dataTimeEn;
        if (prepared.dataTime == 0)
        {
            prepared.dataTime = isJapaneseVoice ? def.dataTimeEn : def.dataTimeJp;
        }

        outTracks.push_back(prepared);
    }
}


static bool ResolvePreparedTrackSaveIndices(std::vector<PreparedCustomTrack>& preparedTracks)
{
    for (PreparedCustomTrack& track : preparedTracks)
    {
        if (!track.usePersistentSaveIndex)
        {
            track.resolvedSaveIndex = track.requestedSaveIndex;

            continue;
        }

        std::int16_t resolvedSaveIndex = -1;
        bool wasCreated = false;

        const bool ok = ResolveOrCreateCustomTapeSaveIndex(
            track.requestedSaveIndex,
            track.albumId.c_str(),
            track.fileName.c_str(),
            resolvedSaveIndex,
            wasCreated);

        if (!ok || resolvedSaveIndex < 0)
        {
            Log(
                "[CustomTapes] ERROR: could not assign a save-index for tape albumId=%s fileName=%s requested=%d — custom-tape injection aborted; no custom tapes added this load.\n",
                track.albumId.c_str(),
                track.fileName.c_str(),
                static_cast<int>(track.requestedSaveIndex));
            return false;
        }

        track.resolvedSaveIndex = resolvedSaveIndex;

        Register_CustomTapeStateTrackMetadata(
            track.resolvedSaveIndex,
            track.albumId.c_str(),
            track.fileName.c_str());

        if (wasCreated)
        {
            InitializeCustomTapeStateIfMissing(
                track.resolvedSaveIndex,
                track.unlocked,
                track.unlocked);
        }
    }

    return true;
}


static bool ApplyCustomTapesToPlayer(void* soundMusicPlayer)
{
    if (!soundMusicPlayer)
        return false;

    CustomTapeRegistry registry;
    CopyCustomTapeRegistry(registry);

    if (registry.albums.empty() && registry.tracks.empty())
    {
        return true;
    }

    SoundMusicPlayerSnapshot snapshot;
    if (!ReadSoundMusicPlayerSnapshot(soundMusicPlayer, snapshot))
    {
        Log("[CustomTapes] ERROR: could not read SoundMusicPlayer track/album fields — custom tapes cannot be injected this load.\n");
        return false;
    }

    TapeAlbumRecord* oldAlbums = snapshot.oldAlbums;
    TapeTrackRecord* oldTracks = snapshot.oldTracks;
    const std::uint32_t oldLuaAlbumCount = snapshot.oldLuaAlbumCount;
    const std::uint32_t oldTotalAlbumCount = snapshot.oldTotalAlbumCount;
    const std::uint32_t oldLuaTrackCount = snapshot.oldLuaTrackCount;
    const std::uint32_t oldTotalTrackCount = snapshot.oldTotalTrackCount;

    if (oldTotalAlbumCount < oldLuaAlbumCount || oldTotalTrackCount < oldLuaTrackCount)
    {
        Log(
            "[CustomTapes] ERROR: SoundMusicPlayer counters are inconsistent (luaAlbums=%u totalAlbums=%u luaTracks=%u totalTracks=%u) — custom tapes cannot be injected this load.\n",
            oldLuaAlbumCount,
            oldTotalAlbumCount,
            oldLuaTrackCount,
            oldTotalTrackCount);
        return false;
    }

    const bool isJapaneseVoice = IsJapaneseVoiceLanguage();

    std::vector<PreparedCustomAlbum> preparedAlbums;
    std::vector<PreparedCustomTrack> preparedTracks;

    PrepareCustomAlbums(registry, oldAlbums, oldTotalAlbumCount, preparedAlbums);
    PrepareCustomTracks(
        registry,
        isJapaneseVoice,
        oldAlbums,
        oldTotalAlbumCount,
        oldTracks,
        oldTotalTrackCount,
        preparedAlbums,
        preparedTracks);

    if (!ResolvePreparedTrackSaveIndices(preparedTracks))
    {
        return false;
    }

    if (preparedAlbums.empty() && preparedTracks.empty())
    {
        return true;
    }

    const std::uint32_t customAlbumCount = static_cast<std::uint32_t>(preparedAlbums.size());
    const std::uint32_t customTrackCount = static_cast<std::uint32_t>(preparedTracks.size());

    const std::uint32_t newLuaAlbumCount = oldLuaAlbumCount + customAlbumCount;
    const std::uint32_t newTotalAlbumCount = oldTotalAlbumCount + customAlbumCount;
    const std::uint32_t newLuaTrackCount = oldLuaTrackCount + customTrackCount;
    const std::uint32_t newTotalTrackCount = oldTotalTrackCount + customTrackCount;

    TapeAlbumRecord* newAlbums = nullptr;
    TapeTrackRecord* newTracks = nullptr;

    if (newTotalAlbumCount > 0)
    {
        newAlbums = reinterpret_cast<TapeAlbumRecord*>(
            GameAllocAligned(
                static_cast<std::uint64_t>(newTotalAlbumCount) * sizeof(TapeAlbumRecord),
                8));

        if (!newAlbums)
        {
            Log("[CustomTapes] ERROR: album buffer allocation failed (count=%u) — out of game heap; custom tapes cannot be injected this load.\n", newTotalAlbumCount);
            return false;
        }

        std::memset(newAlbums, 0, static_cast<std::size_t>(newTotalAlbumCount) * sizeof(TapeAlbumRecord));
    }

    if (newTotalTrackCount > 0)
    {
        newTracks = reinterpret_cast<TapeTrackRecord*>(
            GameAllocAligned(
                static_cast<std::uint64_t>(newTotalTrackCount) * sizeof(TapeTrackRecord),
                8));

        if (!newTracks)
        {
            Log("[CustomTapes] ERROR: track buffer allocation failed (count=%u) — out of game heap; custom tapes cannot be injected this load.\n", newTotalTrackCount);
            GameFree(newAlbums);
            return false;
        }

        std::memset(newTracks, 0, static_cast<std::size_t>(newTotalTrackCount) * sizeof(TapeTrackRecord));
    }

    const std::uint32_t oldAlbumTailCount = oldTotalAlbumCount - oldLuaAlbumCount;
    const std::uint32_t oldTrackTailCount = oldTotalTrackCount - oldLuaTrackCount;

    if (oldAlbums && oldLuaAlbumCount > 0)
    {
        std::memcpy(
            newAlbums,
            oldAlbums,
            static_cast<std::size_t>(oldLuaAlbumCount) * sizeof(TapeAlbumRecord));
    }

    if (customAlbumCount > 0 && newAlbums == nullptr)
    {
        Log("[CustomTapes] ERROR: album buffer is null before injection — internal state inconsistent; custom tapes cannot be injected this load.\n");
        GameFree(newTracks);
        return false;
    }

    for (std::uint32_t i = 0; i < customAlbumCount; ++i)
    {
        TapeAlbumRecord& dst = newAlbums[oldLuaAlbumCount + i];
        const PreparedCustomAlbum& src = preparedAlbums[i];

        dst.albumId = src.albumIdHash;
        dst.langId = src.langIdHash;
        dst.trackCount = 0;
        dst.type = src.type;
        dst.pad14 = 0;
    }

    if (oldAlbums && oldAlbumTailCount > 0)
    {
        std::memcpy(
            &newAlbums[newLuaAlbumCount],
            &oldAlbums[oldLuaAlbumCount],
            static_cast<std::size_t>(oldAlbumTailCount) * sizeof(TapeAlbumRecord));
    }

    if (oldTracks && oldLuaTrackCount > 0)
    {
        std::memcpy(
            newTracks,
            oldTracks,
            static_cast<std::size_t>(oldLuaTrackCount) * sizeof(TapeTrackRecord));
    }

    if (customTrackCount > 0 && newTracks == nullptr)
    {
        Log("[CustomTapes] ERROR: track buffer is null before injection — internal state inconsistent; custom tapes cannot be injected this load.\n");
        GameFree(newAlbums);
        return false;
    }

    for (std::uint32_t i = 0; i < customTrackCount; ++i)
    {
        TapeTrackRecord& dst = newTracks[oldLuaTrackCount + i];
        const PreparedCustomTrack& src = preparedTracks[i];

        dst.albumId = src.albumIdHash;
        dst.langId = src.langIdHash;
        dst.fileNameStrCode = src.fileNameStrCode;
        dst.directPlayTrackId = oldLuaTrackCount + i;
        dst.dataTime = src.dataTime;
        dst.saveIndex = src.resolvedSaveIndex;
        dst.sourceType = 2;
        dst.special = src.special;
        dst.important = src.important;
        dst.reserved[0] = 0;
        dst.reserved[1] = 0;

        {
            std::lock_guard<std::mutex> lock(g_CustomTapeImportantMutex);
            g_CustomTapeImportantBySaveIndex[src.resolvedSaveIndex] = (src.important != 0);
        }

        TapeAlbumRecord* targetAlbum =
            FindMutableAlbumRecordByHash(newAlbums, newTotalAlbumCount, src.albumIdHash);

        if (targetAlbum)
        {
            dst.albumTrackIndex = static_cast<std::int16_t>(targetAlbum->trackCount);
            targetAlbum->trackCount = static_cast<std::uint16_t>(targetAlbum->trackCount + 1u);
        }
        else
        {
            dst.albumTrackIndex = 0;
        }

        std::memset(dst.fileName, 0, sizeof(dst.fileName));
        strncpy_s(dst.fileName, sizeof(dst.fileName), src.fileName.c_str(), _TRUNCATE);
    }

    if (oldTracks && oldTrackTailCount > 0)
    {
        std::memcpy(
            &newTracks[newLuaTrackCount],
            &oldTracks[oldLuaTrackCount],
            static_cast<std::size_t>(oldTrackTailCount) * sizeof(TapeTrackRecord));
    }

    if (!WriteSoundMusicPlayerSnapshot(
        soundMusicPlayer,
        oldAlbums,
        oldTracks,
        newAlbums,
        newTracks,
        newLuaAlbumCount,
        newTotalAlbumCount,
        newLuaTrackCount,
        newTotalTrackCount))
    {
        Log("[CustomTapes] ERROR: failed writing rebuilt track/album arrays back to SoundMusicPlayer — custom tapes will not appear this load.\n");
        GameFree(newAlbums);
        GameFree(newTracks);
        return false;
    }

    return true;
}


static void* ResolveSoundMusicPlayerFromMusicManager()
{
    void* musicManagerGlobalAddr = ResolveGameAddress(gAddr.MusicManager_s_instance);
    if (!musicManagerGlobalAddr)
    {
        Log("[CustomTapes] ERROR: MusicManager::s_instance address unavailable for this build — custom tapes cannot be injected.\n");
        return nullptr;
    }

    __try
    {
        void* musicManagerInstance = *reinterpret_cast<void**>(musicManagerGlobalAddr);
        if (!musicManagerInstance)
        {
            Log("[CustomTapes] WARN: MusicManager not initialized yet — custom tapes will be injected once the sound system is up.\n");
            return nullptr;
        }

        void* soundMusicPlayer =
            *reinterpret_cast<void**>(
                reinterpret_cast<std::uintptr_t>(musicManagerInstance) + 0xA8ull);

        if (!soundMusicPlayer)
        {
            Log("[CustomTapes] WARN: SoundMusicPlayer not initialized yet — custom tapes will be injected once the sound system is up.\n");
            return nullptr;
        }

        return soundMusicPlayer;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Log("[CustomTapes] ERROR: exception while resolving SoundMusicPlayer — custom tapes cannot be injected this load.\n");
        return nullptr;
    }
}


static bool ApplyToCachedSoundMusicPlayer()
{
    void* soundMusicPlayer = g_LastSoundMusicPlayer;

    if (!soundMusicPlayer)
    {
        soundMusicPlayer = ResolveSoundMusicPlayerFromMusicManager();
    }

    if (!soundMusicPlayer)
    {
        return true;
    }

    g_LastSoundMusicPlayer = soundMusicPlayer;

    return ApplyCustomTapesToPlayer(soundMusicPlayer);
}


static void __fastcall hkSetupMusicInfos(void* thisPtr)
{
    if (!g_OrigSetupMusicInfos)
        return;

    g_LastSoundMusicPlayer = thisPtr;

    g_OrigSetupMusicInfos(thisPtr);
    ApplyCustomTapesToPlayer(thisPtr);
}


bool Install_SoundMusicPlayer_SetupMusicInfos_Hook()
{
    void* target = ResolveGameAddress(gAddr.SetupMusicInfos);
    if (!target)
    {
        Log("[CustomTapes] ERROR: SetupMusicInfos address unavailable for this build — custom tapes cannot be injected.\n");
        return false;
    }

    const bool ok = CreateAndEnableHook(
        target,
        reinterpret_cast<void*>(&hkSetupMusicInfos),
        reinterpret_cast<void**>(&g_OrigSetupMusicInfos));

    if (!ok)
        Log("[CustomTapes] ERROR: failed to hook SetupMusicInfos (target=%p) — custom tapes cannot be injected.\n", target);

    return ok;
}


bool Uninstall_SoundMusicPlayer_SetupMusicInfos_Hook()
{
    DisableAndRemoveHook(ResolveGameAddress(gAddr.SetupMusicInfos));
    g_OrigSetupMusicInfos = nullptr;
    g_LastSoundMusicPlayer = nullptr;

    {
        std::lock_guard<std::mutex> lock(g_CustomTapeMutex);
        g_CustomTapeRegistry.albums.clear();
        g_CustomTapeRegistry.tracks.clear();
    }

    return true;
}


bool Register_CustomTapes(
    const std::vector<CustomTapeAlbumDefinition>& albums,
    const std::vector<CustomTapeTrackDefinition>& tracks)
{
    if (albums.empty() && tracks.empty())
        return false;

    {
        std::lock_guard<std::mutex> lock(g_CustomTapeMutex);
        g_CustomTapeRegistry.albums.insert(
            g_CustomTapeRegistry.albums.end(),
            albums.begin(),
            albums.end());

        g_CustomTapeRegistry.tracks.insert(
            g_CustomTapeRegistry.tracks.end(),
            tracks.begin(),
            tracks.end());
    }

    return ApplyToCachedSoundMusicPlayer();
}