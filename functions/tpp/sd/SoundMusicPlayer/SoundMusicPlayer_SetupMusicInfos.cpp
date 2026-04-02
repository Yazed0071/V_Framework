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

namespace
{
    using SetupMusicInfos_t = void(__fastcall*)(void* thisPtr);
    using KernelAllocAligned_t = void* (__fastcall*)(std::uint64_t size, std::uint64_t alignment, std::uint32_t tag);
    using ArrayBaseFree_t = void(__fastcall*)(void* data, std::uint32_t tag);
    using SubtitleManagerGet_t = void* (__fastcall*)();
    using GetVoiceLanguage_t = int(__fastcall*)(void* subtitleManager);

    static constexpr std::uintptr_t ABS_SetupMusicInfos = 0x140974880ull;
    static constexpr std::uintptr_t ABS_KernelAllocAligned = 0x140015F20ull;
    static constexpr std::uintptr_t ABS_ArrayBaseFree = 0x140015EF0ull;
    static constexpr std::uintptr_t ABS_SubtitleManager_Get = 0x1404D2770ull;
    static constexpr std::uintptr_t ABS_GetVoiceLanguage = 0x1404D2AD0ull;
    static constexpr std::uintptr_t ABS_MusicManager_s_instance = 0x142BFFAC8ull;

    static constexpr std::uint32_t kFoxAllocTag = 0x5006Fu;
    static constexpr std::size_t kFileNameMaxChars = 15u;

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

// Holds one in-memory tape album record.
// Params: none
struct TapeAlbumRecord
{
    std::uint64_t albumId;     // 0x00
    std::uint64_t langId;      // 0x08
    std::uint16_t trackCount;  // 0x10
    std::uint16_t type;        // 0x12
    std::uint32_t pad14;       // 0x14
};

// Holds one in-memory tape track record.
// Params: none
struct TapeTrackRecord
{
    std::uint64_t albumId;           // 0x00
    std::uint64_t langId;            // 0x08
    std::uint32_t fileNameStrCode;   // 0x10
    std::uint32_t directPlayTrackId; // 0x14
    std::uint32_t dataTime;          // 0x18
    std::int16_t  saveIndex;         // 0x1C
    std::int16_t  albumTrackIndex;   // 0x1E
    std::uint16_t sourceType;        // 0x20
    std::uint16_t special;           // 0x22
    std::uint16_t important;         // 0x24
    char          fileName[0x10];    // 0x26
    std::uint8_t  reserved[2];       // 0x36
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
    // Holds one validated custom album ready for injection.
    // Params: none
    struct PreparedCustomAlbum
    {
        std::string albumId;
        std::uint64_t albumIdHash = 0;
        std::uint64_t langIdHash = 0;
        std::uint16_t type = 0;
    };

    // Holds one validated custom track ready for injection.
    // Params: none
    struct PreparedCustomTrack
    {
        std::uint64_t albumIdHash = 0;
        std::uint64_t langIdHash = 0;
        std::uint32_t fileNameStrCode = 0;
        std::uint32_t dataTime = 0;
        std::int16_t saveIndex = -1;
        std::uint16_t special = 0;
        std::uint16_t important = 0;
        std::string fileName;
    };

    // Holds a POD snapshot of the live SoundMusicPlayer arrays and counters.
    // Params: none
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

// Allocates game memory with the Fox allocator.
// Params: sizeBytes, alignment
static void* GameAllocAligned(std::uint64_t sizeBytes, std::uint64_t alignment)
{
    void* fnAddr = ResolveGameAddress(ABS_KernelAllocAligned);
    if (!fnAddr)
        return nullptr;

    KernelAllocAligned_t kernelAllocAligned =
        reinterpret_cast<KernelAllocAligned_t>(fnAddr);

    return kernelAllocAligned(sizeBytes, alignment, kFoxAllocTag);
}

// Frees game memory with the Fox allocator.
// Params: data
static void GameFree(void* data)
{
    if (!data)
        return;

    void* fnAddr = ResolveGameAddress(ABS_ArrayBaseFree);
    if (!fnAddr)
        return;

    ArrayBaseFree_t arrayBaseFree =
        reinterpret_cast<ArrayBaseFree_t>(fnAddr);

    arrayBaseFree(data, kFoxAllocTag);
}

// Returns true when the current voice language is Japanese.
// Params: none
static bool IsJapaneseVoiceLanguage()
{
    void* getMgrAddr = ResolveGameAddress(ABS_SubtitleManager_Get);
    void* getLangAddr = ResolveGameAddress(ABS_GetVoiceLanguage);
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

// Reads the live SoundMusicPlayer arrays and counters.
// Params: soundMusicPlayer, outSnapshot
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

// Swaps rebuilt arrays into SoundMusicPlayer and updates counters.
// Params: soundMusicPlayer, oldAlbums, oldTracks, newAlbums, newTracks, newLuaAlbumCount, newTotalAlbumCount, newLuaTrackCount, newTotalTrackCount
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

// Finds one album record by StrCode64 album id.
// Params: albums, count, albumIdHash
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

// Returns the highest direct-play track id currently in the player.
// Params: tracks, count
static std::uint32_t GetMaxDirectPlayTrackId(
    const TapeTrackRecord* tracks,
    std::uint32_t count)
{
    std::uint32_t maxId = 0;

    if (!tracks)
        return maxId;

    for (std::uint32_t i = 0; i < count; ++i)
    {
        maxId = (std::max)(maxId, tracks[i].directPlayTrackId);
    }

    return maxId;
}

// Copies the current registry under lock.
// Params: outRegistry
static void CopyCustomTapeRegistry(CustomTapeRegistry& outRegistry)
{
    std::lock_guard<std::mutex> lock(g_CustomTapeMutex);
    outRegistry = g_CustomTapeRegistry;
}

// Resolves a user-facing album type string to one stock album id exemplar.
// Params: typeName
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

// Validates and prepares custom albums against the current player arrays.
// Params: registry, existingAlbums, existingAlbumCount, outAlbums
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
            Log("[CustomTapes] Skipping album with missing albumId/langId\n");
            continue;
        }

        const std::uint64_t albumIdHash = FoxHashes::StrCode64(def.albumId);
        const std::uint64_t langIdHash = FoxHashes::StrCode64(def.langId);

        if (albumIdHash == 0 || langIdHash == 0)
        {
            Log("[CustomTapes] Skipping album hash failure: %s\n", def.albumId.c_str());
            continue;
        }

        if (FindAlbumRecordByHash(existingAlbums, existingAlbumCount, albumIdHash) != nullptr)
        {
            Log("[CustomTapes] Skipping album duplicate with existing data: %s\n", def.albumId.c_str());
            continue;
        }

        if (!seenCustomAlbumIds.insert(albumIdHash).second)
        {
            Log("[CustomTapes] Skipping duplicate custom album in request: %s\n", def.albumId.c_str());
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
                    "[CustomTapes] Skipping album %s because type is unsupported: %s\n",
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
                    "[CustomTapes] Skipping album %s because stock type exemplar was not found: %s -> %s\n",
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
            Log("[CustomTapes] Skipping album %s because no type/typeValue was provided\n", def.albumId.c_str());
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

// Validates and prepares custom tracks against the validated custom albums.
// Params: registry, isJapaneseVoice, validAlbums, outTracks
static void PrepareCustomTracks(
    const CustomTapeRegistry& registry,
    bool isJapaneseVoice,
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
            Log("[CustomTapes] Skipping track with missing field\n");
            continue;
        }

        if (def.fileName.size() > kFileNameMaxChars)
        {
            Log(
                "[CustomTapes] Skipping track %s because fileName is longer than %zu chars\n",
                def.fileName.c_str(),
                kFileNameMaxChars);
            continue;
        }

        const std::uint64_t albumIdHash = FoxHashes::StrCode64(def.albumId);
        const std::uint64_t langIdHash = FoxHashes::StrCode64(def.langId);
        const std::uint32_t fileNameStrCode = FoxHashes::StrCode32(def.fileName);

        if (albumIdHash == 0 || langIdHash == 0 || fileNameStrCode == 0)
        {
            Log("[CustomTapes] Skipping track hash failure: %s\n", def.fileName.c_str());
            continue;
        }

        if (validAlbumIds.find(albumIdHash) == validAlbumIds.end())
        {
            Log(
                "[CustomTapes] Skipping track %s because its album was not accepted: %s\n",
                def.fileName.c_str(),
                def.albumId.c_str());
            continue;
        }

        PreparedCustomTrack prepared;
        prepared.albumIdHash = albumIdHash;
        prepared.langIdHash = langIdHash;
        prepared.fileNameStrCode = fileNameStrCode;
        prepared.saveIndex = def.saveIndex;
        prepared.special = def.special;
        prepared.important = def.important;
        prepared.fileName = def.fileName;

        prepared.dataTime = isJapaneseVoice ? def.dataTimeJp : def.dataTimeEn;
        if (prepared.dataTime == 0)
        {
            prepared.dataTime = isJapaneseVoice ? def.dataTimeEn : def.dataTimeJp;
        }

        outTracks.push_back(prepared);
    }
}

// Injects validated custom albums and tracks into the built player arrays.
// Params: soundMusicPlayer
static bool ApplyCustomTapesToPlayer(void* soundMusicPlayer)
{
    if (!soundMusicPlayer)
        return false;

    Log("[CustomTapes] ApplyCustomTapesToPlayer begin this=%p\n", soundMusicPlayer);

    CustomTapeRegistry registry;
    CopyCustomTapeRegistry(registry);

    if (registry.albums.empty() && registry.tracks.empty())
    {
        Log("[CustomTapes] Registry is empty, nothing to apply\n");
        return true;
    }

    SoundMusicPlayerSnapshot snapshot;
    if (!ReadSoundMusicPlayerSnapshot(soundMusicPlayer, snapshot))
    {
        Log("[CustomTapes] Failed reading SoundMusicPlayer fields\n");
        return false;
    }

    TapeAlbumRecord* oldAlbums = snapshot.oldAlbums;
    TapeTrackRecord* oldTracks = snapshot.oldTracks;
    const std::uint32_t oldLuaAlbumCount = snapshot.oldLuaAlbumCount;
    const std::uint32_t oldTotalAlbumCount = snapshot.oldTotalAlbumCount;
    const std::uint32_t oldLuaTrackCount = snapshot.oldLuaTrackCount;
    const std::uint32_t oldTotalTrackCount = snapshot.oldTotalTrackCount;

    Log(
        "[CustomTapes] snapshot oldLuaAlbumCount=%u oldTotalAlbumCount=%u oldLuaTrackCount=%u oldTotalTrackCount=%u\n",
        oldLuaAlbumCount,
        oldTotalAlbumCount,
        oldLuaTrackCount,
        oldTotalTrackCount);

    if (oldTotalAlbumCount < oldLuaAlbumCount || oldTotalTrackCount < oldLuaTrackCount)
    {
        Log(
            "[CustomTapes] Invalid counters oldLuaAlbumCount=%u oldTotalAlbumCount=%u oldLuaTrackCount=%u oldTotalTrackCount=%u\n",
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
    PrepareCustomTracks(registry, isJapaneseVoice, preparedAlbums, preparedTracks);

    Log("[CustomTapes] prepared albums=%zu tracks=%zu\n", preparedAlbums.size(), preparedTracks.size());

    if (preparedAlbums.empty() && preparedTracks.empty())
    {
        Log("[CustomTapes] Nothing valid to inject\n");
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
            Log("[CustomTapes] Album allocation failed\n");
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
            Log("[CustomTapes] Track allocation failed\n");
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
        Log("[CustomTapes] newAlbums is null before custom album injection\n");
        GameFree(newTracks);
        return false;
    }

    std::unordered_map<std::uint64_t, TapeAlbumRecord*> newCustomAlbumMap;

    for (std::uint32_t i = 0; i < customAlbumCount; ++i)
    {
        TapeAlbumRecord& dst = newAlbums[oldLuaAlbumCount + i];
        const PreparedCustomAlbum& src = preparedAlbums[i];

        dst.albumId = src.albumIdHash;
        dst.langId = src.langIdHash;
        dst.trackCount = 0;
        dst.type = src.type;
        dst.pad14 = 0;

        newCustomAlbumMap[src.albumIdHash] = &dst;
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
        Log("[CustomTapes] newTracks is null before custom track injection\n");
        GameFree(newAlbums);
        return false;
    }

    std::uint32_t nextDirectPlayTrackId =
        GetMaxDirectPlayTrackId(oldTracks, oldTotalTrackCount) + 1u;

    for (std::uint32_t i = 0; i < customTrackCount; ++i)
    {
        TapeTrackRecord& dst = newTracks[oldLuaTrackCount + i];
        const PreparedCustomTrack& src = preparedTracks[i];

        dst.albumId = src.albumIdHash;
        dst.langId = src.langIdHash;
        dst.fileNameStrCode = src.fileNameStrCode;

        // Match stock PreinstallTape behavior:
        // directPlayTrackId continues the Lua track sequence.
        dst.directPlayTrackId = oldLuaTrackCount + i;

        dst.dataTime = src.dataTime;
        dst.saveIndex = src.saveIndex;
        dst.sourceType = 2;
        dst.special = src.special;
        dst.important = src.important;
        dst.reserved[0] = 0;
        dst.reserved[1] = 0;

        auto itAlbum = newCustomAlbumMap.find(src.albumIdHash);
        if (itAlbum != newCustomAlbumMap.end() && itAlbum->second)
        {
            dst.albumTrackIndex = static_cast<std::int16_t>(itAlbum->second->trackCount);
            itAlbum->second->trackCount = static_cast<std::uint16_t>(itAlbum->second->trackCount + 1u);
        }
        else
        {
            dst.albumTrackIndex = 0;
        }

        std::memset(dst.fileName, 0, sizeof(dst.fileName));
        strncpy_s(dst.fileName, sizeof(dst.fileName), src.fileName.c_str(), _TRUNCATE);

        Log(
            "[CustomTapes] Injected track fileName=%s directPlayTrackId=%u albumTrackIndex=%d\n",
            dst.fileName,
            dst.directPlayTrackId,
            static_cast<int>(dst.albumTrackIndex));
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
        Log("[CustomTapes] Failed writing rebuilt SoundMusicPlayer arrays\n");
        GameFree(newAlbums);
        GameFree(newTracks);
        return false;
    }

    Log(
        "[CustomTapes] Injected albums=%u tracks=%u newLuaAlbumCount=%u newLuaTrackCount=%u\n",
        customAlbumCount,
        customTrackCount,
        newLuaAlbumCount,
        newLuaTrackCount);

    return true;
}



// Resolves the real SoundMusicPlayer from MusicManager::s_instance.
// Returns: SoundMusicPlayer pointer or null.
static void* ResolveSoundMusicPlayerFromMusicManager()
{
    void* musicManagerGlobalAddr = ResolveGameAddress(ABS_MusicManager_s_instance);
    if (!musicManagerGlobalAddr)
    {
        Log("[CustomTapes] MusicManager::s_instance address resolve failed\n");
        return nullptr;
    }

    __try
    {
        void* musicManagerInstance = *reinterpret_cast<void**>(musicManagerGlobalAddr);
        if (!musicManagerInstance)
        {
            Log("[CustomTapes] MusicManager::s_instance is null\n");
            return nullptr;
        }

        void* soundMusicPlayer =
            *reinterpret_cast<void**>(
                reinterpret_cast<std::uintptr_t>(musicManagerInstance) + 0xA8ull);

        if (!soundMusicPlayer)
        {
            Log("[CustomTapes] SoundMusicPlayer is null\n");
            return nullptr;
        }

        return soundMusicPlayer;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Log("[CustomTapes] Exception while resolving MusicManager::s_instance\n");
        return nullptr;
    }
}

static bool ApplyToCachedSoundMusicPlayer()
{
    void* soundMusicPlayer = g_LastSoundMusicPlayer;

    if (!soundMusicPlayer)
    {
        Log("[CustomTapes] ApplyToCachedSoundMusicPlayer: no cached player, trying direct resolve\n");
        soundMusicPlayer = ResolveSoundMusicPlayerFromMusicManager();
    }

    if (!soundMusicPlayer)
    {
        Log("[CustomTapes] ApplyToCachedSoundMusicPlayer: direct resolve failed\n");
        return true;
    }

    g_LastSoundMusicPlayer = soundMusicPlayer;
    Log("[CustomTapes] ApplyToCachedSoundMusicPlayer using %p\n", soundMusicPlayer);

    return ApplyCustomTapesToPlayer(soundMusicPlayer);
}

// Hooked SetupMusicInfos.
// Params: thisPtr
static void __fastcall hkSetupMusicInfos(void* thisPtr)
{
    if (!g_OrigSetupMusicInfos)
        return;

    g_LastSoundMusicPlayer = thisPtr;

    Log("[CustomTapes] hkSetupMusicInfos thisPtr=%p\n", thisPtr);

    g_OrigSetupMusicInfos(thisPtr);
    ApplyCustomTapesToPlayer(thisPtr);

    Log("[CustomTapes] hkSetupMusicInfos apply finished\n");
}

// Installs the SetupMusicInfos hook.
// Params: none
bool Install_SoundMusicPlayer_SetupMusicInfos_Hook()
{
    Log("[CustomTapes] Install_SoundMusicPlayer_SetupMusicInfos_Hook begin\n");

    void* target = ResolveGameAddress(ABS_SetupMusicInfos);
    if (!target)
    {
        Log("[CustomTapes] SetupMusicInfos address resolve failed\n");
        return false;
    }

    const bool ok = CreateAndEnableHook(
        target,
        reinterpret_cast<void*>(&hkSetupMusicInfos),
        reinterpret_cast<void**>(&g_OrigSetupMusicInfos));

    Log(
        "[CustomTapes] SetupMusicInfos hook result: %s target=%p orig=%p\n",
        ok ? "OK" : "FAIL",
        target,
        g_OrigSetupMusicInfos);

    return ok;
}

// Removes the SetupMusicInfos hook.
// Params: none
bool Uninstall_SoundMusicPlayer_SetupMusicInfos_Hook()
{
    DisableAndRemoveHook(ResolveGameAddress(ABS_SetupMusicInfos));
    g_OrigSetupMusicInfos = nullptr;
    g_LastSoundMusicPlayer = nullptr;

    std::lock_guard<std::mutex> lock(g_CustomTapeMutex);
    g_CustomTapeRegistry.albums.clear();
    g_CustomTapeRegistry.tracks.clear();
    return true;
}

// Registers custom albums and tracks.
// If the player was already naturally built once, applies immediately.
// Otherwise waits for the next natural SetupMusicInfos call.
// Params: albums, tracks
bool Register_CustomTapes(
    const std::vector<CustomTapeAlbumDefinition>& albums,
    const std::vector<CustomTapeTrackDefinition>& tracks)
{
    Log("[CustomTapes] Register_CustomTapes called albums=%zu tracks=%zu\n", albums.size(), tracks.size());

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

// Clears all custom albums and tracks.
// Note: this only clears the pending registry. It does not live-remove already injected entries.
// Params: none
void Clear_CustomTapes()
{
    {
        std::lock_guard<std::mutex> lock(g_CustomTapeMutex);
        g_CustomTapeRegistry.albums.clear();
        g_CustomTapeRegistry.tracks.clear();
    }

    Log("[CustomTapes] Clear_CustomTapes: registry cleared (live removal disabled)\n");
}