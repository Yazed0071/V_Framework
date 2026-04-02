#include "pch.h"

#include <Windows.h>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>
#include <algorithm>
#include <cstdlib>

#include "HookUtils.h"
#include "log.h"
#include "FoxHashes.h"
#include "CustomTapeOwnership.h"

extern "C"
{
    #include "lua.h"
}

namespace
{
    // Original TppMotherBaseManagement::AddCassetteTapeTrack.
    // Params: luaState
    using AddCassetteTapeTrack_t = std::uint64_t(__cdecl*)(lua_State* luaState);

    // Original TppMotherBaseManagement::IsGotCassetteTapeTrack.
    // Params: luaState
    using IsGotCassetteTapeTrack_t = int(__cdecl*)(lua_State* luaState);

    // Original anonymous CollectGotTapes.
    // Params: albumType, outAlbumIds, outCapacity, cassetteCallbackThis
    using CollectGotTapes_t = std::uint64_t(__cdecl*)(std::uint32_t albumType, std::intptr_t outAlbumIds, std::uint32_t outCapacity, std::intptr_t cassetteCallbackThis);

    // SoundMusicPlayer::GetTrackInfoByName.
    // Params: soundMusicPlayer, trackNameStrCode
    using GetTrackInfoByName_t = void* (__fastcall*)(void* soundMusicPlayer, std::int32_t trackNameStrCode);

    // Game Lua helpers.
    // Params: luaState, idx
    using lua_isstring_t = int(__fastcall*)(lua_State* luaState, int idx);
    using lua_tolstring_t = const char* (__fastcall*)(lua_State* luaState, int idx, size_t* len);
    using lua_pushboolean_t = void(__fastcall*)(lua_State* luaState, int value);

    static constexpr std::uintptr_t ABS_AddCassetteTapeTrack = 0x1466A5770ull;
    static constexpr std::uintptr_t ABS_IsGotCassetteTapeTrack = 0x1466EC350ull;
    static constexpr std::uintptr_t ABS_CollectGotTapes = 0x149309EA0ull;
    static constexpr std::uintptr_t ABS_GetTrackInfoByName = 0x14614C0C0ull;
    static constexpr std::uintptr_t ABS_MusicManager_s_instance = 0x142BFFAC8ull;
    static constexpr std::uintptr_t ABS_lua_isstring = 0x14C1D9250ull;
    static constexpr std::uintptr_t ABS_lua_tolstring = 0x141A123C0ull;
    static constexpr std::uintptr_t ABS_lua_pushboolean = 0x14C1DB230ull;

    static constexpr std::int16_t kCustomSaveIndexMin = 300;
    static constexpr std::int16_t kCustomSaveIndexMax = 999;
    static constexpr std::uint16_t kVanillaOwnedIndexBias = 0x00B7u;

    static AddCassetteTapeTrack_t g_OrigAddCassetteTapeTrack = nullptr;
    static IsGotCassetteTapeTrack_t g_OrigIsGotCassetteTapeTrack = nullptr;
    static CollectGotTapes_t g_OrigCollectGotTapes = nullptr;

    static lua_isstring_t g_lua_isstring = nullptr;
    static lua_tolstring_t g_lua_tolstring = nullptr;
    static lua_pushboolean_t g_lua_pushboolean = nullptr;

    static std::mutex g_CustomTapeOwnershipMutex;
    static std::unordered_set<int> g_CustomOwnedTapeSaveIndices;
}

// Resolves the Lua helpers used by this file.
// Params: none
static bool ResolveLuaHelpers()
{
    if (!g_lua_isstring)
        g_lua_isstring = reinterpret_cast<lua_isstring_t>(ResolveGameAddress(ABS_lua_isstring));

    if (!g_lua_tolstring)
        g_lua_tolstring = reinterpret_cast<lua_tolstring_t>(ResolveGameAddress(ABS_lua_tolstring));

    if (!g_lua_pushboolean)
        g_lua_pushboolean = reinterpret_cast<lua_pushboolean_t>(ResolveGameAddress(ABS_lua_pushboolean));

    return g_lua_isstring && g_lua_tolstring && g_lua_pushboolean;
}

// Returns true when one saveIndex belongs to the custom ownership range.
bool IsCustomTapeSaveIndex(std::int16_t saveIndex)
{
    return saveIndex >= kCustomSaveIndexMin && saveIndex <= kCustomSaveIndexMax;
}

// Returns the persistence file path next to the module DLL.
// Params: none
static std::string GetCustomTapeOwnershipFilePath()
{
    char modulePath[MAX_PATH] = {};
    HMODULE moduleHandle = nullptr;

    if (GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&GetCustomTapeOwnershipFilePath),
        &moduleHandle) && moduleHandle)
    {
        GetModuleFileNameA(moduleHandle, modulePath, MAX_PATH);
    }
    else
    {
        GetModuleFileNameA(nullptr, modulePath, MAX_PATH);
    }

    std::string path(modulePath);
    const std::size_t slashPos = path.find_last_of("\\/");
    if (slashPos != std::string::npos)
        path.resize(slashPos + 1);
    else
        path.clear();

    path += "V_FrameWork_CustomTapeOwnership.txt";
    return path;
}

// Saves the custom owned saveIndex set to disk.
// Params: none
static bool SaveCustomTapeOwnershipToDisk()
{
    std::vector<int> sortedSaveIndices;

    {
        std::lock_guard<std::mutex> lock(g_CustomTapeOwnershipMutex);
        sortedSaveIndices.assign(g_CustomOwnedTapeSaveIndices.begin(), g_CustomOwnedTapeSaveIndices.end());
    }

    std::sort(sortedSaveIndices.begin(), sortedSaveIndices.end());

    const std::string path = GetCustomTapeOwnershipFilePath();
    std::ofstream file(path, std::ios::out | std::ios::trunc);
    if (!file.is_open())
    {
        Log("[CustomTapeOwnership] Save failed path=%s\n", path.c_str());
        return false;
    }

    file << "# V_FrameWork custom cassette ownership\n";
    file << "# One saveIndex per line\n";

    for (int saveIndex : sortedSaveIndices)
    {
        file << saveIndex << '\n';
    }

    Log(
        "[CustomTapeOwnership] Saved %zu custom owned tape indices to %s\n",
        sortedSaveIndices.size(),
        path.c_str());

    return true;
}

// Loads the custom owned saveIndex set from disk.
// Params: none
static bool LoadCustomTapeOwnershipFromDisk()
{
    const std::string path = GetCustomTapeOwnershipFilePath();
    std::ifstream file(path);
    if (!file.is_open())
    {
        Log("[CustomTapeOwnership] No persistence file yet at %s\n", path.c_str());
        return true;
    }

    std::unordered_set<int> loadedSaveIndices;
    std::string line;

    while (std::getline(file, line))
    {
        if (line.empty())
            continue;

        if (line[0] == '#')
            continue;

        char* endPtr = nullptr;
        const long value = std::strtol(line.c_str(), &endPtr, 10);
        if (endPtr == line.c_str())
            continue;

        if (value < kCustomSaveIndexMin || value > kCustomSaveIndexMax)
            continue;

        loadedSaveIndices.insert(static_cast<int>(value));
    }

    const std::size_t loadedCount = loadedSaveIndices.size();

    {
        std::lock_guard<std::mutex> lock(g_CustomTapeOwnershipMutex);
        g_CustomOwnedTapeSaveIndices = std::move(loadedSaveIndices);
    }

    Log(
        "[CustomTapeOwnership] Loaded %zu custom owned tape indices from %s\n",
        loadedCount,
        path.c_str());

    return true;
}

// Returns true when one custom saveIndex is currently owned.
bool IsCustomTapeOwnedSaveIndex(std::int16_t saveIndex)
{
    std::lock_guard<std::mutex> lock(g_CustomTapeOwnershipMutex);
    return g_CustomOwnedTapeSaveIndices.find(static_cast<int>(saveIndex)) != g_CustomOwnedTapeSaveIndices.end();
}

// Sets custom ownership for one saveIndex.
// Params: saveIndex, owned
static bool SetCustomTapeOwned(std::int16_t saveIndex, bool owned)
{
    bool changed = false;

    {
        std::lock_guard<std::mutex> lock(g_CustomTapeOwnershipMutex);

        if (owned)
        {
            changed = g_CustomOwnedTapeSaveIndices.insert(static_cast<int>(saveIndex)).second;
        }
        else
        {
            changed = g_CustomOwnedTapeSaveIndices.erase(static_cast<int>(saveIndex)) > 0;
        }
    }

    if (changed)
    {
        SaveCustomTapeOwnershipToDisk();
    }

    return changed;
}

// Resolves the live SoundMusicPlayer instance from MusicManager::s_instance.
// Params: none
static void* ResolveSoundMusicPlayerFromMusicManager()
{
    void* musicManagerGlobalAddr = ResolveGameAddress(ABS_MusicManager_s_instance);
    if (!musicManagerGlobalAddr)
        return nullptr;

    __try
    {
        void* musicManagerInstance = *reinterpret_cast<void**>(musicManagerGlobalAddr);
        if (!musicManagerInstance)
            return nullptr;

        return *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(musicManagerInstance) + 0xA8ull);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
}

// Resolves one trackInfo from a track name.
// Params: trackName
static void* ResolveTrackInfoByName(const char* trackName)
{
    if (!trackName || !trackName[0])
        return nullptr;

    const std::uint32_t trackNameStrCode = FoxHashes::StrCode32(trackName);
    if (trackNameStrCode == 0)
        return nullptr;

    void* fnAddr = ResolveGameAddress(ABS_GetTrackInfoByName);
    if (!fnAddr)
        return nullptr;

    void* soundMusicPlayer = ResolveSoundMusicPlayerFromMusicManager();
    if (!soundMusicPlayer)
        return nullptr;

    GetTrackInfoByName_t getTrackInfoByName = reinterpret_cast<GetTrackInfoByName_t>(fnAddr);

    __try
    {
        return getTrackInfoByName(soundMusicPlayer, static_cast<std::int32_t>(trackNameStrCode));
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
}

// Reads saveIndex from one trackInfo record.
// Params: trackInfo, outSaveIndex
static bool TryGetTrackSaveIndex(void* trackInfo, std::int16_t& outSaveIndex)
{
    outSaveIndex = -1;

    if (!trackInfo)
        return false;

    __try
    {
        outSaveIndex = *reinterpret_cast<const std::int16_t*>(reinterpret_cast<const std::uintptr_t>(trackInfo) + 0x1Cull);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        outSaveIndex = -1;
        return false;
    }
}

// Returns the first Lua string argument, or null on failure.
// Params: luaState
static const char* GetLuaTrackNameArg(lua_State* luaState)
{
    if (!ResolveLuaHelpers() || !luaState)
        return nullptr;

    if (g_lua_isstring(luaState, 1) == 0)
        return nullptr;

    return g_lua_tolstring(luaState, 1, nullptr);
}

// Pushes one bool result back to Lua.
// Params: luaState, value
static void PushLuaBool(lua_State* luaState, bool value)
{
    if (!ResolveLuaHelpers() || !luaState)
        return;

    g_lua_pushboolean(luaState, value ? 1 : 0);
}

// Reads the vanilla ownership bit from the cassette menu callback path.
// Params: cassetteCallbackThis, ownedBitIndex
static bool ReadVanillaTapeOwnedBitFromCallback(std::intptr_t cassetteCallbackThis, std::uint16_t ownedBitIndex)
{
    __try
    {
        void* objectB0 = *reinterpret_cast<void**>(static_cast<std::uintptr_t>(cassetteCallbackThis) + 0xB0ull);
        if (!objectB0)
            return false;

        void* objectAE8 = *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(objectB0) + 0xAE8ull);
        if (!objectAE8)
            return false;

        std::uint8_t* ownedTable = *reinterpret_cast<std::uint8_t**>(reinterpret_cast<std::uintptr_t>(objectAE8) + 0x740ull);
        if (!ownedTable)
            return false;

        return ((ownedTable[ownedBitIndex] >> 1) & 1u) != 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// Returns true when one track should count as owned inside the cassette menu.
// Params: trackInfo, cassetteCallbackThis
static bool IsTrackOwnedForCassetteMenu(void* trackInfo, std::intptr_t cassetteCallbackThis)
{
    std::int16_t saveIndex = -1;
    if (!TryGetTrackSaveIndex(trackInfo, saveIndex))
        return false;

    if (saveIndex < 0)
        return false;

    if (IsCustomTapeSaveIndex(saveIndex))
        return IsCustomTapeOwnedSaveIndex(saveIndex);

    const std::uint16_t ownedBitIndex = static_cast<std::uint16_t>(static_cast<std::uint16_t>(saveIndex) + kVanillaOwnedIndexBias);
    if (ownedBitIndex == 0xFFFFu)
        return false;

    return ReadVanillaTapeOwnedBitFromCallback(cassetteCallbackThis, ownedBitIndex);
}

// Calls SoundMusicPlayer virtual +0x160 and returns the album array base.
// Params: soundMusicPlayer
static std::uintptr_t CallGetAlbumArray(void* soundMusicPlayer)
{
    __try
    {
        std::uintptr_t* vtbl = *reinterpret_cast<std::uintptr_t**>(soundMusicPlayer);
        using Fn = std::uintptr_t(__fastcall*)(void* thisPtr);
        Fn fn = reinterpret_cast<Fn>(vtbl[0x160ull / 8ull]);
        return fn(soundMusicPlayer);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
}

// Calls SoundMusicPlayer virtual +0x150 and returns the album count.
// Params: soundMusicPlayer
static std::uint32_t CallGetAlbumCount(void* soundMusicPlayer)
{
    __try
    {
        std::uintptr_t* vtbl = *reinterpret_cast<std::uintptr_t**>(soundMusicPlayer);
        using Fn = std::uint32_t(__fastcall*)(void* thisPtr);
        Fn fn = reinterpret_cast<Fn>(vtbl[0x150ull / 8ull]);
        return fn(soundMusicPlayer);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
}

// Calls SoundMusicPlayer virtual +0x188 and returns trackInfo for albumId + trackIndex.
// Params: soundMusicPlayer, albumId, trackIndex
static void* CallGetTrackInfoByAlbumAndIndex(void* soundMusicPlayer, std::uint64_t albumId, std::uint32_t trackIndex)
{
    __try
    {
        std::uintptr_t* vtbl = *reinterpret_cast<std::uintptr_t**>(soundMusicPlayer);
        using Fn = void* (__fastcall*)(void* thisPtr, std::uint64_t albumId, std::uint32_t trackIndex);
        Fn fn = reinterpret_cast<Fn>(vtbl[0x188ull / 8ull]);
        return fn(soundMusicPlayer, albumId, trackIndex);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
}

// Hooked AddCassetteTapeTrack.
// Params: luaState
static std::uint64_t __cdecl hkAddCassetteTapeTrack(lua_State* luaState)
{
    const char* trackName = GetLuaTrackNameArg(luaState);
    if (!trackName)
    {
        return g_OrigAddCassetteTapeTrack ? g_OrigAddCassetteTapeTrack(luaState) : 0ull;
    }

    void* trackInfo = ResolveTrackInfoByName(trackName);
    std::int16_t saveIndex = -1;

    if (!TryGetTrackSaveIndex(trackInfo, saveIndex) || !IsCustomTapeSaveIndex(saveIndex))
    {
        return g_OrigAddCassetteTapeTrack ? g_OrigAddCassetteTapeTrack(luaState) : 0ull;
    }

    const bool changed = SetCustomTapeOwned(saveIndex, true);

    Log(
        "[CustomTapeOwnership] AddCassetteTapeTrack custom track=%s saveIndex=%d owned=%d changed=%d\n",
        trackName,
        static_cast<int>(saveIndex),
        1,
        changed ? 1 : 0);

    return 0ull;
}

// Hooked IsGotCassetteTapeTrack.
// Params: luaState
static int __cdecl hkIsGotCassetteTapeTrack(lua_State* luaState)
{
    const char* trackName = GetLuaTrackNameArg(luaState);
    if (!trackName)
    {
        return g_OrigIsGotCassetteTapeTrack ? g_OrigIsGotCassetteTapeTrack(luaState) : 1;
    }

    void* trackInfo = ResolveTrackInfoByName(trackName);
    std::int16_t saveIndex = -1;

    if (!TryGetTrackSaveIndex(trackInfo, saveIndex) || !IsCustomTapeSaveIndex(saveIndex))
    {
        return g_OrigIsGotCassetteTapeTrack ? g_OrigIsGotCassetteTapeTrack(luaState) : 1;
    }

    const bool owned = IsCustomTapeOwnedSaveIndex(saveIndex);
    PushLuaBool(luaState, owned);

    Log(
        "[CustomTapeOwnership] IsGotCassetteTapeTrack custom track=%s saveIndex=%d owned=%d\n",
        trackName,
        static_cast<int>(saveIndex),
        owned ? 1 : 0);

    return 1;
}

// Hooked CollectGotTapes.
// Params: albumType, outAlbumIds, outCapacity, cassetteCallbackThis
static std::uint64_t __cdecl hkCollectGotTapes(
    std::uint32_t albumType,
    std::intptr_t outAlbumIds,
    std::uint32_t outCapacity,
    std::intptr_t cassetteCallbackThis)
{
    if (cassetteCallbackThis == 0)
    {
        return g_OrigCollectGotTapes
            ? g_OrigCollectGotTapes(albumType, outAlbumIds, outCapacity, cassetteCallbackThis)
            : 0ull;
    }

    __try
    {
        void* soundMusicPlayer = *reinterpret_cast<void**>(static_cast<std::uintptr_t>(cassetteCallbackThis) + 0xD8ull);
        if (!soundMusicPlayer)
        {
            return g_OrigCollectGotTapes
                ? g_OrigCollectGotTapes(albumType, outAlbumIds, outCapacity, cassetteCallbackThis)
                : 0ull;
        }

        const std::uintptr_t albumArrayBase = CallGetAlbumArray(soundMusicPlayer);
        const std::uint32_t albumCount = CallGetAlbumCount(soundMusicPlayer);
        if (albumArrayBase == 0 || albumCount == 0)
            return 0ull;

        const std::uint8_t* albumCursor = reinterpret_cast<const std::uint8_t*>(albumArrayBase + 0x10ull);
        std::uint64_t writtenCount = 0;

        for (std::uint32_t albumIndex = 0; albumIndex < albumCount; ++albumIndex)
        {
            const std::uint16_t trackCount = *reinterpret_cast<const std::uint16_t*>(albumCursor + 0x00ull);
            const std::uint16_t currentAlbumType = *reinterpret_cast<const std::uint16_t*>(albumCursor + 0x02ull);
            const std::uint64_t albumId = *reinterpret_cast<const std::uint64_t*>(albumCursor - 0x10ull);

            if (currentAlbumType == albumType)
            {
                bool includeAlbum = false;

                if (currentAlbumType == 10)
                {
                    includeAlbum = true;
                }
                else if (trackCount != 0)
                {
                    for (std::uint32_t trackIndex = 0; trackIndex < trackCount; ++trackIndex)
                    {
                        void* trackInfo = CallGetTrackInfoByAlbumAndIndex(soundMusicPlayer, albumId, trackIndex);
                        if (trackInfo && IsTrackOwnedForCassetteMenu(trackInfo, cassetteCallbackThis))
                        {
                            includeAlbum = true;
                            break;
                        }
                    }
                }

                if (includeAlbum)
                {
                    if (writtenCount >= outCapacity)
                        return writtenCount;

                    *reinterpret_cast<std::uint64_t*>(outAlbumIds + static_cast<std::intptr_t>(writtenCount * 8ull)) = albumId;
                    ++writtenCount;
                }
            }

            albumCursor += 0x18ull;
        }

        return writtenCount;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Log("[CustomTapeOwnership] CollectGotTapes exception, falling back to original\n");

        return g_OrigCollectGotTapes
            ? g_OrigCollectGotTapes(albumType, outAlbumIds, outCapacity, cassetteCallbackThis)
            : 0ull;
    }
}

// Installs the custom tape ownership hooks and loads persisted ownership.
// Params: none
bool Install_CustomTapeOwnership_Hooks()
{
    if (!ResolveLuaHelpers())
    {
        Log("[CustomTapeOwnership] Lua helper resolve failed\n");
        return false;
    }

    LoadCustomTapeOwnershipFromDisk();

    void* addTarget = ResolveGameAddress(ABS_AddCassetteTapeTrack);
    void* isGotTarget = ResolveGameAddress(ABS_IsGotCassetteTapeTrack);
    void* collectTarget = ResolveGameAddress(ABS_CollectGotTapes);

    if (!addTarget || !isGotTarget || !collectTarget)
    {
        Log("[CustomTapeOwnership] address resolve failed add=%p isGot=%p collect=%p\n", addTarget, isGotTarget, collectTarget);
        return false;
    }

    const bool okAdd = CreateAndEnableHook(
        addTarget,
        reinterpret_cast<void*>(&hkAddCassetteTapeTrack),
        reinterpret_cast<void**>(&g_OrigAddCassetteTapeTrack));

    const bool okIsGot = CreateAndEnableHook(
        isGotTarget,
        reinterpret_cast<void*>(&hkIsGotCassetteTapeTrack),
        reinterpret_cast<void**>(&g_OrigIsGotCassetteTapeTrack));

    const bool okCollect = CreateAndEnableHook(
        collectTarget,
        reinterpret_cast<void*>(&hkCollectGotTapes),
        reinterpret_cast<void**>(&g_OrigCollectGotTapes));

    Log("[Hook] CustomTapeOwnership AddCassetteTapeTrack: %s\n", okAdd ? "OK" : "FAIL");
    Log("[Hook] CustomTapeOwnership IsGotCassetteTapeTrack: %s\n", okIsGot ? "OK" : "FAIL");
    Log("[Hook] CustomTapeOwnership CollectGotTapes: %s\n", okCollect ? "OK" : "FAIL");

    return okAdd && okIsGot && okCollect;
}

// Removes the custom tape ownership hooks and saves persisted ownership.
// Params: none
bool Uninstall_CustomTapeOwnership_Hooks()
{
    SaveCustomTapeOwnershipToDisk();

    DisableAndRemoveHook(ResolveGameAddress(ABS_AddCassetteTapeTrack));
    DisableAndRemoveHook(ResolveGameAddress(ABS_IsGotCassetteTapeTrack));
    DisableAndRemoveHook(ResolveGameAddress(ABS_CollectGotTapes));

    g_OrigAddCassetteTapeTrack = nullptr;
    g_OrigIsGotCassetteTapeTrack = nullptr;
    g_OrigCollectGotTapes = nullptr;

    return true;
}
