#include "pch.h"

#include <Windows.h>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <algorithm>
#include <regex>

#include "HookUtils.h"
#include "log.h"
#include "FoxHashes.h"
#include "CustomTapeOwnership.h"

#include "AddressSet.h"
#include "V_FrameWorkState.h"
#include "SoundMusicPlayer_SetupMusicInfos.h"

extern "C"
{
    #include "lua.h"
}

namespace
{


    using AddCassetteTapeTrack_t = std::uint64_t(__cdecl*)(lua_State* luaState);
    using AddCassetteTapeTrackByIndex_t = std::uint64_t(__cdecl*)(lua_State* luaState);


    using IsGotCassetteTapeTrack_t = int(__cdecl*)(lua_State* luaState);


    using SetCassetteTapeTrackNewFlag_t = int(__cdecl*)(lua_State* luaState);


    using CollectGotTapes_t = std::uint64_t(__cdecl*)(std::uint32_t albumType, std::intptr_t outAlbumIds, std::uint32_t outCapacity, std::intptr_t cassetteCallbackThis);
    using GetCassetteTapeUnreadInfo_t = void(__fastcall*)(void* menuSystem, std::uint32_t* outUnreadCount, std::uint8_t* outHasNew);
    using IsNewCassetteTapeTrack_t = int(__cdecl*)(lua_State* luaState);
    using CassetteMenuCheckNewFlag_t = unsigned char(__fastcall*)(void* trackInfo, void* uiUtilityTable);
    using CassetteAlbumCheckNewFlag_t = unsigned char(__fastcall*)(void* albumInfo, void* uiUtilityTable);
    using CassetteTrackRecordGetter_t = void*(__fastcall*)(void* recordSource, void* albumFirst, unsigned int trackIndex);
    using CassetteCheckUnreadInfo_t = void(__fastcall*)(void* albumInfo, void* uiUtilityTable, unsigned int* outCount, unsigned char* outFlag);


    using GetQuarkSystemTable_t = void* (__cdecl*)();


    using GetTrackInfoByName_t = void* (__fastcall*)(void* soundMusicPlayer, std::int32_t trackNameStrCode);


    using lua_isstring_t = int(__fastcall*)(lua_State* luaState, int idx);
    using lua_type_t = int(__fastcall*)(lua_State* luaState, int idx);
    using lua_tolstring_t = const char* (__fastcall*)(lua_State* luaState, int idx, size_t* len);
    using lua_toboolean_t = int(__fastcall*)(lua_State* luaState, int idx);
    using lua_pushboolean_t = void(__fastcall*)(lua_State* luaState, int value);
    using lua_tointeger_t = std::intptr_t(__fastcall*)(lua_State* luaState, int idx);

    static constexpr std::int16_t kCustomSaveIndexMin = 300;
    static constexpr std::int16_t kCustomSaveIndexMax = 32000;
    static constexpr std::uint16_t kVanillaOwnedIndexBias = 0x00B7u;
    static constexpr std::uint16_t kVanillaCassetteFlagRegionEndExclusive = 0x193u;


    struct CustomTapePersistEntry
    {
        std::int16_t saveIndex = -1;
        std::string albumId;
        std::string fileName;
        bool owned = false;
        bool isNew = false;
    };

    static AddCassetteTapeTrack_t g_OrigAddCassetteTapeTrack = nullptr;
    static AddCassetteTapeTrackByIndex_t g_OrigAddCassetteTapeTrackByIndex = nullptr;
    static IsGotCassetteTapeTrack_t g_OrigIsGotCassetteTapeTrack = nullptr;
    static SetCassetteTapeTrackNewFlag_t g_OrigSetCassetteTapeTrackNewFlag = nullptr;
    static CollectGotTapes_t g_OrigCollectGotTapes = nullptr;
    static GetCassetteTapeUnreadInfo_t g_OrigGetCassetteTapeUnreadInfo = nullptr;
    static IsNewCassetteTapeTrack_t g_OrigIsNewCassetteTapeTrack = nullptr;
    static CassetteMenuCheckNewFlag_t g_OrigCassetteMenuCheckNewFlag = nullptr;
    static CassetteAlbumCheckNewFlag_t g_OrigCassetteAlbumCheckNewFlag = nullptr;
    static CassetteCheckUnreadInfo_t g_OrigCassetteCheckUnreadInfo = nullptr;

    static lua_isstring_t g_lua_isstring = nullptr;
    static lua_type_t g_lua_type = nullptr;
    static lua_tolstring_t g_lua_tolstring = nullptr;
    static lua_toboolean_t g_lua_toboolean = nullptr;
    static lua_pushboolean_t g_lua_pushboolean = nullptr;
    static lua_tointeger_t g_lua_tointeger = nullptr;

    static std::mutex g_CustomTapeStateMutex;
    static std::unordered_set<int> g_CustomOwnedTapeSaveIndices;
    static std::unordered_set<int> g_CustomNewTapeSaveIndices;
    static std::unordered_map<int, CustomTapePersistEntry> g_CustomTapePersistEntries;
    static std::unordered_set<int> g_ActiveSessionCustomTapeSaveIndices;
    static std::unordered_map<std::string, int> g_ActiveSessionTrackKeyToSaveIndex;
    static std::unordered_set<int> g_NewlyCreatedThisSessionSaveIndices;
    static std::unordered_set<int> g_HiddenTapeSaveIndices;
    static std::once_flag g_CustomTapeStateLoadOnce;
}


static std::uint8_t* ResolveLiveCassetteFlagTable();


static void ApplyCustomTapeStateToLiveTable(std::int16_t saveIndex, bool owned, bool isNew);


static bool ResolveLuaHelpers()
{
    if (!g_lua_isstring)
        g_lua_isstring = reinterpret_cast<lua_isstring_t>(ResolveGameAddress(gAddr.lua_isstring));

    if (!g_lua_type)
        g_lua_type = reinterpret_cast<lua_type_t>(ResolveGameAddress(gAddr.lua_type));

    if (!g_lua_tolstring)
        g_lua_tolstring = reinterpret_cast<lua_tolstring_t>(ResolveGameAddress(gAddr.lua_tolstring));

    if (!g_lua_toboolean)
        g_lua_toboolean = reinterpret_cast<lua_toboolean_t>(ResolveGameAddress(gAddr.lua_toboolean));

    if (!g_lua_pushboolean)
        g_lua_pushboolean = reinterpret_cast<lua_pushboolean_t>(ResolveGameAddress(gAddr.lua_pushboolean));

    if (!g_lua_tointeger)
        g_lua_tointeger = reinterpret_cast<lua_tointeger_t>(ResolveGameAddress(gAddr.lua_tointeger));

    return g_lua_isstring && g_lua_type && g_lua_tolstring && g_lua_toboolean && g_lua_pushboolean;
}


bool IsCustomTapeSaveIndex(std::int16_t saveIndex)
{
    return saveIndex >= kCustomSaveIndexMin && saveIndex <= kCustomSaveIndexMax;
}


static std::string GetModuleDirectoryPath()
{
    char modulePath[MAX_PATH] = {};
    HMODULE moduleHandle = nullptr;

    if (GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&GetModuleDirectoryPath),
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
        path.resize(slashPos);
    else
        path.clear();

    return path;
}


static bool EnsureDirectoryExistsRecursive(const std::string& absoluteDirectoryPath)
{
    if (absoluteDirectoryPath.empty())
        return false;

    std::string partial;
    partial.reserve(absoluteDirectoryPath.size());

    for (std::size_t i = 0; i < absoluteDirectoryPath.size(); ++i)
    {
        const char ch = absoluteDirectoryPath[i];
        partial.push_back(ch);

        if (ch != '\\' && ch != '/')
            continue;

        if (partial.size() <= 3 && partial.find(':') != std::string::npos)
            continue;

        CreateDirectoryA(partial.c_str(), nullptr);
    }

    if (CreateDirectoryA(absoluteDirectoryPath.c_str(), nullptr) != 0)
        return true;

    const DWORD error = GetLastError();
    return error == ERROR_ALREADY_EXISTS;
}


static std::string GetCustomTapeStateFilePath()
{
    std::string path = GetModuleDirectoryPath();
    if (!path.empty())
        path += "\\mod\\saves";
    else
        path = "mod\\saves";

    EnsureDirectoryExistsRecursive(path);
    path += "\\V_FrameWork_CustomTapeState.lua";
    return path;
}


static std::string EscapeForLuaLongString(const std::string& value)
{
    std::string escaped;
    escaped.reserve(value.size());

    for (std::size_t i = 0; i < value.size(); ++i)
    {
        if (i + 1 < value.size() && value[i] == ']' && value[i + 1] == ']')
        {
            escaped += "]] .. \"]]\" .. [[";
            ++i;
            continue;
        }

        escaped.push_back(value[i]);
    }

    return escaped;
}


static void* ResolveSoundMusicPlayerFromMusicManager()
{
    void* musicManagerGlobalAddr = ResolveGameAddress(gAddr.MusicManager_s_instance);
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


static void* ResolveTrackInfoByName(const char* trackName)
{
    if (!trackName || !trackName[0])
        return nullptr;

    const std::uint32_t trackNameStrCode = FoxHashes::StrCode32(trackName);
    if (trackNameStrCode == 0)
        return nullptr;

    void* fnAddr = ResolveGameAddress(gAddr.GetTrackInfoByName);
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


static CustomTapePersistEntry& GetOrCreatePersistEntry(std::int16_t saveIndex)
{
    CustomTapePersistEntry& entry = g_CustomTapePersistEntries[static_cast<int>(saveIndex)];
    entry.saveIndex = saveIndex;
    entry.owned = g_CustomOwnedTapeSaveIndices.find(static_cast<int>(saveIndex)) != g_CustomOwnedTapeSaveIndices.end();
    entry.isNew = g_CustomNewTapeSaveIndices.find(static_cast<int>(saveIndex)) != g_CustomNewTapeSaveIndices.end();
    return entry;
}


static bool TryReadLiveCassetteState(std::int16_t saveIndex, bool& outOwned, bool& outNew)
{
    outOwned = false;
    outNew = false;

    if (!IsCustomTapeSaveIndex(saveIndex))
        return false;

    std::uint8_t* liveTable = ResolveLiveCassetteFlagTable();
    if (!liveTable)
        return false;

    const std::uint16_t tableIndex =
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(saveIndex) + kVanillaOwnedIndexBias);

    if (tableIndex == 0xFFFFu)
        return false;

    if (tableIndex < kVanillaOwnedIndexBias || tableIndex >= kVanillaCassetteFlagRegionEndExclusive)
        return false;

    __try
    {
        const std::uint8_t value = liveTable[tableIndex];
        outNew = (value & 0x01u) != 0;
        outOwned = ((value >> 1) & 0x01u) != 0;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        outOwned = false;
        outNew = false;
        return false;
    }
}


static bool SaveCustomTapeStateToDisk()
{


    return true;

    std::vector<CustomTapePersistEntry> entries;

    {
        std::lock_guard<std::mutex> lock(g_CustomTapeStateMutex);

        for (int saveIndex : g_CustomOwnedTapeSaveIndices)
        {
            GetOrCreatePersistEntry(static_cast<std::int16_t>(saveIndex));
        }

        for (int saveIndex : g_CustomNewTapeSaveIndices)
        {
            GetOrCreatePersistEntry(static_cast<std::int16_t>(saveIndex));
        }

        entries.reserve(g_CustomTapePersistEntries.size());
        for (const auto& kv : g_CustomTapePersistEntries)
        {
            entries.push_back(kv.second);
        }
    }

    std::sort(
        entries.begin(),
        entries.end(),
        [](const CustomTapePersistEntry& a, const CustomTapePersistEntry& b)
        {
            return a.saveIndex < b.saveIndex;
        });

    const std::string path = GetCustomTapeStateFilePath();
    std::ofstream file(path, std::ios::out | std::ios::trunc);
    if (!file.is_open())
    {
        Log("[CustomTapeState] ERROR: could not write '%s' - custom-tape owned/new state will not persist across launches.\n", path.c_str());
        return false;
    }

    file << "return {\n";
    file << "    version = 2,\n";
    file << "    tapes = {\n";

    for (const CustomTapePersistEntry& entry : entries)
    {
        file
            << "        [" << static_cast<int>(entry.saveIndex) << "] = "
            << "{ saveIndex = " << static_cast<int>(entry.saveIndex)
            << ", albumId = [[" << EscapeForLuaLongString(entry.albumId) << "]]"
            << ", fileName = [[" << EscapeForLuaLongString(entry.fileName) << "]]"
            << ", owned = " << (entry.owned ? "true" : "false")
            << ", new = " << (entry.isNew ? "true" : "false")
            << " },\n";
    }

    file << "    }\n";
    file << "}\n";

    return true;
}


static bool LoadCustomTapeStateFromDisk()
{
    std::unordered_set<int> loadedOwned;
    std::unordered_set<int> loadedNew;
    std::unordered_map<int, CustomTapePersistEntry> loadedEntries;

    V_FrameWorkState::ForEachTape(
        [&](const std::string& key, std::int16_t saveIndex, bool owned, bool isNew)
        {
            if (saveIndex < kCustomSaveIndexMin || saveIndex > kCustomSaveIndexMax)
                return;

            CustomTapePersistEntry entry;
            entry.saveIndex = saveIndex;

            const auto colon = key.find(':');
            if (colon != std::string::npos)
            {
                entry.albumId = key.substr(0, colon);
                entry.fileName = key.substr(colon + 1);
            }

            entry.owned = owned;
            entry.isNew = isNew;

            if (owned)
                loadedOwned.insert(static_cast<int>(saveIndex));
            if (isNew)
                loadedNew.insert(static_cast<int>(saveIndex));

            loadedEntries[static_cast<int>(saveIndex)] = std::move(entry);
        });

    {
        std::lock_guard<std::mutex> lock(g_CustomTapeStateMutex);
        g_CustomOwnedTapeSaveIndices = std::move(loadedOwned);
        g_CustomNewTapeSaveIndices = std::move(loadedNew);
        g_CustomTapePersistEntries = std::move(loadedEntries);

        g_ActiveSessionCustomTapeSaveIndices.clear();
        g_ActiveSessionTrackKeyToSaveIndex.clear();
        g_NewlyCreatedThisSessionSaveIndices.clear();
    }

    return true;
}


static void EnsureCustomTapeStateLoaded()
{
    std::call_once(g_CustomTapeStateLoadOnce, []()
        {
            ResolveLuaHelpers();
            LoadCustomTapeStateFromDisk();
        });
}


static bool UpdateCustomTapeMetadata_NoLock(std::int16_t saveIndex, const char* albumId, const char* fileName)
{
    if (!IsCustomTapeSaveIndex(saveIndex))
        return false;

    CustomTapePersistEntry& entry = GetOrCreatePersistEntry(saveIndex);
    bool changed = false;

    if (albumId && albumId[0] && entry.albumId != albumId)
    {
        entry.albumId = albumId;
        changed = true;
    }

    if (fileName && fileName[0] && entry.fileName != fileName)
    {
        entry.fileName = fileName;
        changed = true;
    }

    return changed;
}

void Register_CustomTapeStateTrackMetadata(std::int16_t saveIndex, const char* albumId, const char* fileName)
{
    EnsureCustomTapeStateLoaded();

    if (!IsCustomTapeSaveIndex(saveIndex))
        return;

    bool changed = false;

    {
        std::lock_guard<std::mutex> lock(g_CustomTapeStateMutex);
        changed = UpdateCustomTapeMetadata_NoLock(saveIndex, albumId, fileName);
    }

    if (changed)
    {
        SaveCustomTapeStateToDisk();
    }
}


static std::string BuildCustomTapeTrackKey(const char* albumId, const char* fileName)
{
    const char* safeAlbumId = albumId ? albumId : "";
    const char* safeFileName = fileName ? fileName : "";

    std::string key;
    key.reserve(std::strlen(safeAlbumId) + std::strlen(safeFileName) + 1);
    key += safeAlbumId;
    key.push_back('\n');
    key += safeFileName;
    return key;
}


static bool FindPersistEntryByTrackKey_NoLock(const std::string& trackKey, std::int16_t& outSaveIndex)
{
    outSaveIndex = -1;

    for (const auto& kv : g_CustomTapePersistEntries)
    {
        const CustomTapePersistEntry& entry = kv.second;
        const std::string entryKey = BuildCustomTapeTrackKey(entry.albumId.c_str(), entry.fileName.c_str());

        if (entryKey == trackKey)
        {
            outSaveIndex = static_cast<std::int16_t>(entry.saveIndex);
            return true;
        }
    }

    return false;
}


static bool IsSaveIndexReservedThisSession_NoLock(std::int16_t saveIndex)
{
    return g_ActiveSessionCustomTapeSaveIndices.find(static_cast<int>(saveIndex)) !=
        g_ActiveSessionCustomTapeSaveIndices.end();
}


static std::int16_t FindNextAvailableCustomSaveIndex_NoLock()
{
    for (std::int16_t saveIndex = kCustomSaveIndexMin; saveIndex <= kCustomSaveIndexMax; ++saveIndex)
    {
        if (!IsSaveIndexReservedThisSession_NoLock(saveIndex))
            return saveIndex;
    }

    return -1;
}

bool ResolveOrCreateCustomTapeSaveIndex(
    std::int16_t requestedSaveIndex,
    const char* albumId,
    const char* fileName,
    std::int16_t& outResolvedSaveIndex,
    bool& outWasCreated)
{
    EnsureCustomTapeStateLoaded();

    outResolvedSaveIndex = -1;
    outWasCreated = false;

    if (!albumId || !albumId[0] || !fileName || !fileName[0])
        return false;


    const std::string unifiedKey = std::string(albumId) + ":" + std::string(fileName);


    std::int16_t unifiedSaveIndex = -1;
    if (V_FrameWorkState::ResolveOrCreateTapeSaveIndex(
        unifiedKey.c_str(), requestedSaveIndex, unifiedSaveIndex) &&
        unifiedSaveIndex > 0)
    {
        requestedSaveIndex = unifiedSaveIndex;
    }

    const std::string trackKey = BuildCustomTapeTrackKey(albumId, fileName);

    bool saveNeeded = false;

    {
        std::lock_guard<std::mutex> lock(g_CustomTapeStateMutex);

        const auto itActive = g_ActiveSessionTrackKeyToSaveIndex.find(trackKey);
        if (itActive != g_ActiveSessionTrackKeyToSaveIndex.end())
        {
            outResolvedSaveIndex = static_cast<std::int16_t>(itActive->second);
            outWasCreated = false;
            return true;
        }

        std::int16_t persistedSaveIndex = -1;
        if (FindPersistEntryByTrackKey_NoLock(trackKey, persistedSaveIndex) &&
            IsCustomTapeSaveIndex(persistedSaveIndex))
        {
            g_ActiveSessionCustomTapeSaveIndices.insert(static_cast<int>(persistedSaveIndex));
            g_ActiveSessionTrackKeyToSaveIndex[trackKey] = persistedSaveIndex;
            g_NewlyCreatedThisSessionSaveIndices.erase(static_cast<int>(persistedSaveIndex));

            outResolvedSaveIndex = persistedSaveIndex;
            outWasCreated = false;
            return true;
        }

        std::int16_t chosenSaveIndex = -1;

        if (IsCustomTapeSaveIndex(requestedSaveIndex) &&
            !IsSaveIndexReservedThisSession_NoLock(requestedSaveIndex))
        {
            chosenSaveIndex = requestedSaveIndex;
        }

        if (!IsCustomTapeSaveIndex(chosenSaveIndex))
        {
            chosenSaveIndex = FindNextAvailableCustomSaveIndex_NoLock();
        }

        if (!IsCustomTapeSaveIndex(chosenSaveIndex))
            return false;

        CustomTapePersistEntry& entry = g_CustomTapePersistEntries[static_cast<int>(chosenSaveIndex)];

        const bool metadataChanged =
            entry.saveIndex != chosenSaveIndex ||
            entry.albumId != albumId ||
            entry.fileName != fileName;

        const bool replacingOldDifferentEntry =
            !entry.albumId.empty() &&
            !entry.fileName.empty() &&
            (entry.albumId != albumId || entry.fileName != fileName);

        entry.saveIndex = chosenSaveIndex;
        entry.albumId = albumId;
        entry.fileName = fileName;

        if (replacingOldDifferentEntry)
        {
            entry.owned = false;
            entry.isNew = false;
            g_CustomOwnedTapeSaveIndices.erase(static_cast<int>(chosenSaveIndex));
            g_CustomNewTapeSaveIndices.erase(static_cast<int>(chosenSaveIndex));
        }

        g_ActiveSessionCustomTapeSaveIndices.insert(static_cast<int>(chosenSaveIndex));
        g_ActiveSessionTrackKeyToSaveIndex[trackKey] = chosenSaveIndex;
        g_NewlyCreatedThisSessionSaveIndices.insert(static_cast<int>(chosenSaveIndex));

        outResolvedSaveIndex = chosenSaveIndex;
        outWasCreated = true;

        saveNeeded = metadataChanged || replacingOldDifferentEntry;
    }

    if (saveNeeded)
    {
        SaveCustomTapeStateToDisk();
    }

    return true;
}


void InitializeCustomTapeStateIfMissing(
    std::int16_t saveIndex,
    bool owned,
    bool isNew)
{
    EnsureCustomTapeStateLoaded();

    if (!IsCustomTapeSaveIndex(saveIndex))
        return;

    bool shouldInitialize = false;

    {
        std::lock_guard<std::mutex> lock(g_CustomTapeStateMutex);

        auto it = g_NewlyCreatedThisSessionSaveIndices.find(static_cast<int>(saveIndex));
        if (it == g_NewlyCreatedThisSessionSaveIndices.end())
            return;

        g_NewlyCreatedThisSessionSaveIndices.erase(it);
        shouldInitialize = true;

        CustomTapePersistEntry& entry = GetOrCreatePersistEntry(saveIndex);
        entry.owned = owned;
        entry.isNew = isNew;

        if (owned)
            g_CustomOwnedTapeSaveIndices.insert(static_cast<int>(saveIndex));
        else
            g_CustomOwnedTapeSaveIndices.erase(static_cast<int>(saveIndex));

        if (isNew)
            g_CustomNewTapeSaveIndices.insert(static_cast<int>(saveIndex));
        else
            g_CustomNewTapeSaveIndices.erase(static_cast<int>(saveIndex));
    }

    if (!shouldInitialize)
        return;

    ApplyCustomTapeStateToLiveTable(saveIndex, owned, isNew);
    SaveCustomTapeStateToDisk();

    V_FrameWorkState::SetTapeOwnedBySaveIndex(saveIndex, owned);
    V_FrameWorkState::SetTapeNewBySaveIndex(saveIndex, isNew);
}


bool IsCustomTapeOwnedSaveIndex(std::int16_t saveIndex)
{
    EnsureCustomTapeStateLoaded();
    std::lock_guard<std::mutex> lock(g_CustomTapeStateMutex);
    return g_CustomOwnedTapeSaveIndices.find(static_cast<int>(saveIndex)) != g_CustomOwnedTapeSaveIndices.end();
}


bool IsCustomTapeOwnedInLiveTable(std::int16_t saveIndex)
{
    bool owned = false;
    bool isNew = false;
    return TryReadLiveCassetteState(saveIndex, owned, isNew) && owned;
}


bool IsTapeSaveIndexHidden(std::int16_t saveIndex)
{
    if (saveIndex < 0)
        return false;
    std::lock_guard<std::mutex> lock(g_CustomTapeStateMutex);
    return g_HiddenTapeSaveIndices.find(static_cast<int>(saveIndex)) != g_HiddenTapeSaveIndices.end();
}


bool IsCustomTapeNewFlagSaveIndex(std::int16_t saveIndex)
{
    EnsureCustomTapeStateLoaded();
    std::lock_guard<std::mutex> lock(g_CustomTapeStateMutex);
    return g_CustomNewTapeSaveIndices.find(static_cast<int>(saveIndex)) != g_CustomNewTapeSaveIndices.end();
}


static bool SetCustomTapeOwnedSaveIndex(std::int16_t saveIndex, bool owned)
{
    bool changed = false;

    {
        std::lock_guard<std::mutex> lock(g_CustomTapeStateMutex);
        CustomTapePersistEntry& entry = GetOrCreatePersistEntry(saveIndex);

        if (owned)
            changed = g_CustomOwnedTapeSaveIndices.insert(static_cast<int>(saveIndex)).second;
        else
            changed = g_CustomOwnedTapeSaveIndices.erase(static_cast<int>(saveIndex)) > 0;

        entry.owned = owned;
    }

    V_FrameWorkState::SetTapeOwnedBySaveIndex(saveIndex, owned);
    return changed;
}


static bool SetCustomTapeNewFlagSaveIndex(std::int16_t saveIndex, bool isNew)
{
    bool changed = false;

    {
        std::lock_guard<std::mutex> lock(g_CustomTapeStateMutex);
        CustomTapePersistEntry& entry = GetOrCreatePersistEntry(saveIndex);

        if (isNew)
            changed = g_CustomNewTapeSaveIndices.insert(static_cast<int>(saveIndex)).second;
        else
            changed = g_CustomNewTapeSaveIndices.erase(static_cast<int>(saveIndex)) > 0;

        entry.isNew = isNew;
    }

    V_FrameWorkState::SetTapeNewBySaveIndex(saveIndex, isNew);
    return changed;
}


static const char* GetLuaTrackNameArg(lua_State* luaState)
{
    if (!ResolveLuaHelpers() || !luaState)
        return nullptr;

    if (g_lua_isstring(luaState, 1) == 0)
        return nullptr;

    return g_lua_tolstring(luaState, 1, nullptr);
}


static bool TryGetLuaBoolArg2(lua_State* luaState, bool& outValue)
{
    outValue = false;

    if (!ResolveLuaHelpers() || !luaState)
        return false;

    if (g_lua_type(luaState, 2) != 1)
        return false;

    outValue = g_lua_toboolean(luaState, 2) != 0;
    return true;
}


static void PushLuaBool(lua_State* luaState, bool value)
{
    if (!ResolveLuaHelpers() || !luaState)
        return;

    g_lua_pushboolean(luaState, value ? 1 : 0);
}


static std::uint8_t* ResolveLiveCassetteFlagTable()
{
    void* fnAddr = ResolveGameAddress(gAddr.GetQuarkSystemTable);
    if (!fnAddr)
        return nullptr;

    GetQuarkSystemTable_t getQuarkSystemTable = reinterpret_cast<GetQuarkSystemTable_t>(fnAddr);

    __try
    {
        void* quarkSystemTable = getQuarkSystemTable();
        if (!quarkSystemTable)
            return nullptr;

        void* obj98 = *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(quarkSystemTable) + 0x98ull);
        if (!obj98)
            return nullptr;

        void* obj110 = *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(obj98) + 0x110ull);
        if (!obj110)
            return nullptr;

        void* objAE8 = *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(obj110) + 0xAE8ull);
        if (!objAE8)
            return nullptr;

        return *reinterpret_cast<std::uint8_t**>(reinterpret_cast<std::uintptr_t>(objAE8) + 0x740ull);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
}


static void ApplyCustomTapeStateToLiveTable(std::int16_t saveIndex, bool owned, bool isNew)
{
    if (!IsCustomTapeSaveIndex(saveIndex))
        return;

    std::uint8_t* liveTable = ResolveLiveCassetteFlagTable();
    if (!liveTable)
        return;

    const std::uint16_t tableIndex = static_cast<std::uint16_t>(static_cast<std::uint16_t>(saveIndex) + kVanillaOwnedIndexBias);
    if (tableIndex == 0xFFFFu)
        return;

    if (tableIndex < kVanillaOwnedIndexBias || tableIndex >= kVanillaCassetteFlagRegionEndExclusive)
        return;

    __try
    {
        std::uint8_t& value = liveTable[tableIndex];

        if (owned)
            value |= 0x02u;
        else
            value &= static_cast<std::uint8_t>(~0x02u);

        if (isNew)
            value |= 0x01u;
        else
            value &= static_cast<std::uint8_t>(~0x01u);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}


static void SyncAllCustomTapeStateToLiveTable()
{
    std::vector<CustomTapePersistEntry> entries;
    bool saveNeeded = false;

    {
        std::lock_guard<std::mutex> lock(g_CustomTapeStateMutex);
        entries.reserve(g_CustomTapePersistEntries.size());

        for (const auto& kv : g_CustomTapePersistEntries)
            entries.push_back(kv.second);

        for (int saveIndex : g_CustomOwnedTapeSaveIndices)
        {
            if (g_CustomTapePersistEntries.find(saveIndex) == g_CustomTapePersistEntries.end())
            {
                CustomTapePersistEntry entry;
                entry.saveIndex = static_cast<std::int16_t>(saveIndex);
                entry.owned = true;
                entry.isNew = g_CustomNewTapeSaveIndices.find(saveIndex) != g_CustomNewTapeSaveIndices.end();
                entries.push_back(entry);
            }
        }

        for (int saveIndex : g_CustomNewTapeSaveIndices)
        {
            bool alreadyPresent = false;
            for (const CustomTapePersistEntry& entry : entries)
            {
                if (entry.saveIndex == saveIndex)
                {
                    alreadyPresent = true;
                    break;
                }
            }

            if (!alreadyPresent)
            {
                CustomTapePersistEntry entry;
                entry.saveIndex = static_cast<std::int16_t>(saveIndex);
                entry.owned = g_CustomOwnedTapeSaveIndices.find(saveIndex) != g_CustomOwnedTapeSaveIndices.end();
                entry.isNew = true;
                entries.push_back(entry);
            }
        }
    }

    for (CustomTapePersistEntry& entry : entries)
    {
        bool persistedOwned = false;
        bool persistedNew = false;

        {
            std::lock_guard<std::mutex> lock(g_CustomTapeStateMutex);
            persistedOwned =
                g_CustomOwnedTapeSaveIndices.find(static_cast<int>(entry.saveIndex)) != g_CustomOwnedTapeSaveIndices.end();
            persistedNew =
                g_CustomNewTapeSaveIndices.find(static_cast<int>(entry.saveIndex)) != g_CustomNewTapeSaveIndices.end();
        }

        bool liveOwned = false;
        bool liveNew = false;
        const bool hasLiveState = TryReadLiveCassetteState(entry.saveIndex, liveOwned, liveNew);


        if (hasLiveState && persistedOwned && liveOwned && persistedNew != liveNew)
        {
            if (SetCustomTapeNewFlagSaveIndex(entry.saveIndex, liveNew))
            {
                saveNeeded = true;
                persistedNew = liveNew;
            }
        }

        ApplyCustomTapeStateToLiveTable(entry.saveIndex, persistedOwned, persistedNew);
    }

    if (saveNeeded)
    {
        SaveCustomTapeStateToDisk();
    }
}

void Sync_CustomTapeStateToLiveTable()
{
    EnsureCustomTapeStateLoaded();
    SyncAllCustomTapeStateToLiveTable();
}


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


static bool IsTrackOwnedForCassetteMenu(void* trackInfo, std::intptr_t cassetteCallbackThis)
{
    std::int16_t saveIndex = -1;
    if (!TryGetTrackSaveIndex(trackInfo, saveIndex))
        return false;

    if (saveIndex < 0)
        return false;

    if (IsTapeSaveIndexHidden(saveIndex))
        return false;

    if (IsCustomTapeSaveIndex(saveIndex))
        return IsCustomTapeOwnedSaveIndex(saveIndex);

    const std::uint16_t ownedBitIndex = static_cast<std::uint16_t>(static_cast<std::uint16_t>(saveIndex) + kVanillaOwnedIndexBias);
    if (ownedBitIndex == 0xFFFFu)
        return false;

    return ReadVanillaTapeOwnedBitFromCallback(cassetteCallbackThis, ownedBitIndex);
}


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


static void FlushCustomTapeStateIfChanged(bool ownedChanged, bool newChanged, bool metadataChanged)
{
    if (ownedChanged || newChanged || metadataChanged)
        SaveCustomTapeStateToDisk();
}


static std::uint64_t __cdecl hkAddCassetteTapeTrack(lua_State* luaState)
{
    const char* trackName = GetLuaTrackNameArg(luaState);
    if (!trackName)
        return g_OrigAddCassetteTapeTrack ? g_OrigAddCassetteTapeTrack(luaState) : 0ull;

    void* trackInfo = ResolveTrackInfoByName(trackName);
    std::int16_t saveIndex = -1;

    if (!TryGetTrackSaveIndex(trackInfo, saveIndex) || !IsCustomTapeSaveIndex(saveIndex))
        return g_OrigAddCassetteTapeTrack ? g_OrigAddCassetteTapeTrack(luaState) : 0ull;

    bool metadataChanged = false;

    {
        std::lock_guard<std::mutex> lock(g_CustomTapeStateMutex);
        metadataChanged = UpdateCustomTapeMetadata_NoLock(saveIndex, nullptr, trackName);
    }

    const bool ownedChanged = SetCustomTapeOwnedSaveIndex(saveIndex, true);
    const bool newChanged = SetCustomTapeNewFlagSaveIndex(saveIndex, true);
    ApplyCustomTapeStateToLiveTable(saveIndex, true, true);
    FlushCustomTapeStateIfChanged(ownedChanged, newChanged, metadataChanged);

    return 0ull;
}

static std::uint64_t __cdecl hkAddCassetteTapeTrackByIndex(lua_State* luaState)
{
    if (g_lua_tointeger)
    {
        const std::int16_t saveIndex = static_cast<std::int16_t>(g_lua_tointeger(luaState, 1));
        if (IsCustomTapeSaveIndex(saveIndex))
        {
            const bool ownedChanged = SetCustomTapeOwnedSaveIndex(saveIndex, true);
            const bool newChanged   = SetCustomTapeNewFlagSaveIndex(saveIndex, true);
            ApplyCustomTapeStateToLiveTable(saveIndex, true, true);
            FlushCustomTapeStateIfChanged(ownedChanged, newChanged, false);
        }
    }

    return g_OrigAddCassetteTapeTrackByIndex ? g_OrigAddCassetteTapeTrackByIndex(luaState) : 0ull;
}


static int __cdecl hkIsGotCassetteTapeTrack(lua_State* luaState)
{
    const char* trackName = GetLuaTrackNameArg(luaState);
    if (!trackName)
        return g_OrigIsGotCassetteTapeTrack ? g_OrigIsGotCassetteTapeTrack(luaState) : 1;

    void* trackInfo = ResolveTrackInfoByName(trackName);
    std::int16_t saveIndex = -1;

    if (!TryGetTrackSaveIndex(trackInfo, saveIndex))
        return g_OrigIsGotCassetteTapeTrack ? g_OrigIsGotCassetteTapeTrack(luaState) : 1;

    if (IsTapeSaveIndexHidden(saveIndex))
    {
        PushLuaBool(luaState, false);
        return 1;
    }

    if (!IsCustomTapeSaveIndex(saveIndex))
        return g_OrigIsGotCassetteTapeTrack ? g_OrigIsGotCassetteTapeTrack(luaState) : 1;

    const bool owned = IsCustomTapeOwnedSaveIndex(saveIndex);
    PushLuaBool(luaState, owned);

    return 1;
}


static int __cdecl hkIsNewCassetteTapeTrack(lua_State* luaState)
{
    const char* trackName = GetLuaTrackNameArg(luaState);
    if (!trackName)
        return g_OrigIsNewCassetteTapeTrack ? g_OrigIsNewCassetteTapeTrack(luaState) : 1;

    void* trackInfo = ResolveTrackInfoByName(trackName);
    std::int16_t saveIndex = -1;

    if (!TryGetTrackSaveIndex(trackInfo, saveIndex) || !IsCustomTapeSaveIndex(saveIndex))
        return g_OrigIsNewCassetteTapeTrack ? g_OrigIsNewCassetteTapeTrack(luaState) : 1;

    const bool isNew = IsCustomTapeNewFlagSaveIndex(saveIndex);
    PushLuaBool(luaState, isNew);

    return 1;
}


static void NudgeCassetteBadgeRefresh()
{
    if (!g_OrigGetCassetteTapeUnreadInfo)
        return;

    std::uint8_t* liveTable = ResolveLiveCassetteFlagTable();
    if (!liveTable)
        return;

    __try
    {
        liveTable[kVanillaOwnedIndexBias] ^= 0x80u;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}


static int __cdecl hkSetCassetteTapeTrackNewFlag(lua_State* luaState)
{
    const char* trackName = GetLuaTrackNameArg(luaState);
    bool isNew = false;

    if (!trackName || !TryGetLuaBoolArg2(luaState, isNew))
        return g_OrigSetCassetteTapeTrackNewFlag ? g_OrigSetCassetteTapeTrackNewFlag(luaState) : 0;

    void* trackInfo = ResolveTrackInfoByName(trackName);
    std::int16_t saveIndex = -1;

    if (!TryGetTrackSaveIndex(trackInfo, saveIndex) || !IsCustomTapeSaveIndex(saveIndex))
        return g_OrigSetCassetteTapeTrackNewFlag ? g_OrigSetCassetteTapeTrackNewFlag(luaState) : 0;

    bool metadataChanged = false;

    {
        std::lock_guard<std::mutex> lock(g_CustomTapeStateMutex);
        metadataChanged = UpdateCustomTapeMetadata_NoLock(saveIndex, nullptr, trackName);
    }

    const bool changed = SetCustomTapeNewFlagSaveIndex(saveIndex, isNew);
    ApplyCustomTapeStateToLiveTable(saveIndex, IsCustomTapeOwnedSaveIndex(saveIndex), isNew);
    FlushCustomTapeStateIfChanged(false, changed, metadataChanged);

    if (changed)
        NudgeCassetteBadgeRefresh();

    return 0;
}


static void WriteVanillaLiveTapeFlag(std::int16_t saveIndex, std::uint8_t bitMask, bool set)
{
    std::uint8_t* liveTable = ResolveLiveCassetteFlagTable();
    if (!liveTable)
        return;

    const std::uint16_t tableIndex =
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(saveIndex) + kVanillaOwnedIndexBias);

    if (tableIndex == 0xFFFFu)
        return;

    if (tableIndex < kVanillaOwnedIndexBias || tableIndex >= kVanillaCassetteFlagRegionEndExclusive)
        return;

    __try
    {
        if (set)
            liveTable[tableIndex] |= bitMask;
        else
            liveTable[tableIndex] &= static_cast<std::uint8_t>(~bitMask);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}


std::int16_t ResolveCassetteSaveIndexByTrackName(const char* trackName)
{
    void* trackInfo = ResolveTrackInfoByName(trackName);
    std::int16_t saveIndex = -1;
    if (TryGetTrackSaveIndex(trackInfo, saveIndex))
        return saveIndex;
    return -1;
}


void Set_CassetteTapeOwned(std::int16_t saveIndex, bool owned)
{
    if (saveIndex < 0)
        return;

    if (!IsCustomTapeSaveIndex(saveIndex))
        return;

    const bool changed = SetCustomTapeOwnedSaveIndex(saveIndex, owned);
    ApplyCustomTapeStateToLiveTable(saveIndex, owned, IsCustomTapeNewFlagSaveIndex(saveIndex));
    if (changed)
        SaveCustomTapeStateToDisk();

    NudgeCassetteBadgeRefresh();
}


void Set_CassetteTapeNewFlag(std::int16_t saveIndex, bool isNew)
{
    if (saveIndex < 0)
        return;

    if (IsCustomTapeSaveIndex(saveIndex))
    {
        const bool changed = SetCustomTapeNewFlagSaveIndex(saveIndex, isNew);
        ApplyCustomTapeStateToLiveTable(saveIndex, IsCustomTapeOwnedSaveIndex(saveIndex), isNew);
        if (changed)
            SaveCustomTapeStateToDisk();
    }
    else
    {
        WriteVanillaLiveTapeFlag(saveIndex, 0x01u, isNew);
    }

    NudgeCassetteBadgeRefresh();
}


void Hide_CassetteTape(std::int16_t saveIndex)
{
    if (saveIndex < 0)
        return;

    {
        std::lock_guard<std::mutex> lock(g_CustomTapeStateMutex);
        g_HiddenTapeSaveIndices.insert(static_cast<int>(saveIndex));
    }

    NudgeCassetteBadgeRefresh();
}


void Show_CassetteTape(std::int16_t saveIndex)
{
    if (saveIndex < 0)
        return;

    {
        std::lock_guard<std::mutex> lock(g_CustomTapeStateMutex);
        g_HiddenTapeSaveIndices.erase(static_cast<int>(saveIndex));
    }

    NudgeCassetteBadgeRefresh();
}


static bool TryGetTrackId(void* trackInfo, std::uint32_t& outTrackId)
{
    outTrackId = 0;
    if (!trackInfo)
        return false;

    __try
    {
        outTrackId = *reinterpret_cast<const std::uint32_t*>(reinterpret_cast<const std::uintptr_t>(trackInfo) + 0x14ull);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        outTrackId = 0;
        return false;
    }
}


void OnCassetteTrackPlayedByTrackId(std::uint32_t playedTrackId)
{
    if (playedTrackId == 0)
        return;

    std::int16_t playedSaveIndex = -1;
    {
        std::lock_guard<std::mutex> lock(g_CustomTapeStateMutex);
        for (const auto& kv : g_CustomTapePersistEntries)
        {
            const CustomTapePersistEntry& entry = kv.second;
            if (!entry.isNew || !IsCustomTapeSaveIndex(entry.saveIndex) || entry.fileName.empty())
                continue;

            void* trackInfo = ResolveTrackInfoByName(entry.fileName.c_str());
            if (!trackInfo)
                continue;

            std::uint32_t trackId = 0;
            if (TryGetTrackId(trackInfo, trackId) && trackId == playedTrackId)
            {
                playedSaveIndex = entry.saveIndex;
                break;
            }
        }
    }

    if (playedSaveIndex < 0)
        return;

    SetCustomTapeNewFlagSaveIndex(playedSaveIndex, false);
    NudgeCassetteBadgeRefresh();
}


static bool IsListedTopAlbumId(std::uint64_t albumId)
{
    static const std::unordered_set<std::uint64_t> ids = [] {
        static const char* const kNames[] = {
            "tp_chico_01", "tp_chico_02", "tp_chico_03", "tp_chico_04",
            "tp_chico_05", "tp_chico_06", "tp_chico_07", "tp_chico_08",
            "tp_bf20010_01", "tp_pw_01", "tp_it20030_01", "tp_it20030_02",
            "tp_bgm_01", "tp_bgm_02", "tp_bgm_03", "tp_bgm_04", "tp_bgm_05",
            "tp_archCD_01", "tp_archDiary_01",
        };
        std::unordered_set<std::uint64_t> s;
        for (const char* n : kNames)
            s.insert(FoxHashes::StrCode64(n));
        return s;
    }();
    return ids.find(albumId) != ids.end();
}


static std::uint64_t __cdecl hkCollectGotTapes(
    std::uint32_t albumType,
    std::intptr_t outAlbumIds,
    std::uint32_t outCapacity,
    std::intptr_t cassetteCallbackThis)
{
    Sync_CustomTapeStateToLiveTable();

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

        std::uint64_t writtenCount = 0;

        for (int pass = 0; pass < 2; ++pass)
        {
            const bool wantListed = (pass == 0);
            const std::uint8_t* albumCursor = reinterpret_cast<const std::uint8_t*>(albumArrayBase + 0x10ull);

            for (std::uint32_t albumIndex = 0; albumIndex < albumCount; ++albumIndex)
            {
                const std::uint16_t trackCount = *reinterpret_cast<const std::uint16_t*>(albumCursor + 0x00ull);
                const std::uint16_t currentAlbumType = *reinterpret_cast<const std::uint16_t*>(albumCursor + 0x02ull);
                const std::uint64_t albumId = *reinterpret_cast<const std::uint64_t*>(albumCursor - 0x10ull);

                if (currentAlbumType == albumType && IsListedTopAlbumId(albumId) == wantListed)
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
        }

        return writtenCount;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return g_OrigCollectGotTapes
            ? g_OrigCollectGotTapes(albumType, outAlbumIds, outCapacity, cassetteCallbackThis)
            : 0ull;
    }
}


static bool CustomTapeFlagByteOwnedAndNew(const std::uint8_t* liveTable, std::uint16_t tableIndex)
{
    __try
    {
        const std::uint8_t flagByte = liveTable[tableIndex];
        return (flagByte & 0x03u) == 0x03u;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static void __fastcall hkGetCassetteTapeUnreadInfo(void* menuSystem, std::uint32_t* outUnreadCount, std::uint8_t* outHasNew)
{
    if (g_OrigGetCassetteTapeUnreadInfo)
        g_OrigGetCassetteTapeUnreadInfo(menuSystem, outUnreadCount, outHasNew);

    if (!outUnreadCount)
        return;

    std::uint8_t* liveTable = ResolveLiveCassetteFlagTable();
    if (!liveTable)
        return;

    int delta = 0;
    bool customImportantUnread = false;
    {
        std::lock_guard<std::mutex> lock(g_CustomTapeStateMutex);
        for (const auto& kv : g_CustomTapePersistEntries)
        {
            const std::int16_t saveIndex = kv.second.saveIndex;
            if (!IsCustomTapeSaveIndex(saveIndex))
                continue;

            const std::uint16_t tableIndex =
                static_cast<std::uint16_t>(static_cast<std::uint16_t>(saveIndex) + kVanillaOwnedIndexBias);

            const bool strayCounted = CustomTapeFlagByteOwnedAndNew(liveTable, tableIndex);
            const bool realCounted = kv.second.owned && kv.second.isNew;
            delta += (realCounted ? 1 : 0) - (strayCounted ? 1 : 0);

            if (realCounted && IsCustomTapeImportantBySaveIndex(saveIndex))
                customImportantUnread = true;
        }
    }

    if (customImportantUnread && outHasNew)
        *outHasNew = 1;

    if (delta == 0)
        return;

    long corrected = static_cast<long>(*outUnreadCount) + delta;
    if (corrected < 0)
        corrected = 0;
    *outUnreadCount = static_cast<std::uint32_t>(corrected);
}


static bool ReadTrackIconSelector(void* trackInfo, std::uint16_t& outSelector)
{
    outSelector = 0;
    if (!trackInfo)
        return false;

    __try
    {
        outSelector = *reinterpret_cast<const std::uint16_t*>(reinterpret_cast<const std::uintptr_t>(trackInfo) + 0x24ull);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        outSelector = 0;
        return false;
    }
}


static unsigned char __fastcall hkCassetteMenuCheckNewFlag(void* trackInfo, void* uiUtilityTable)
{
    std::int16_t saveIndex = -1;
    if (TryGetTrackSaveIndex(trackInfo, saveIndex) && IsCustomTapeSaveIndex(saveIndex))
    {
        if (!IsCustomTapeOwnedSaveIndex(saveIndex) || !IsCustomTapeNewFlagSaveIndex(saveIndex))
            return 0;

        std::uint16_t selector = 0;
        ReadTrackIconSelector(trackInfo, selector);
        return (selector != 0) ? 0x08u : 0x80u;
    }

    return g_OrigCassetteMenuCheckNewFlag ? g_OrigCassetteMenuCheckNewFlag(trackInfo, uiUtilityTable) : 0;
}


static bool TryComputeAlbumNewMark(void* albumInfo, void* uiUtilityTable, unsigned char& outMark)
{
    outMark = 0;
    if (!albumInfo || !uiUtilityTable)
        return false;

    __try
    {
        unsigned int trackCount = *reinterpret_cast<const std::uint16_t*>(reinterpret_cast<const std::uintptr_t>(albumInfo) + 0x10ull);
        if (trackCount == 0)
            return true;

        void* recordSource = *reinterpret_cast<void* const*>(reinterpret_cast<const std::uintptr_t>(uiUtilityTable) + 0xd8ull);
        if (!recordSource)
            return false;

        void** recordSourceVtable = *reinterpret_cast<void** const*>(recordSource);
        CassetteTrackRecordGetter_t getRecord =
            reinterpret_cast<CassetteTrackRecordGetter_t>(recordSourceVtable[0x188 / sizeof(void*)]);
        void* albumFirst = *reinterpret_cast<void* const*>(albumInfo);
        const std::uint8_t* flagTable = ResolveLiveCassetteFlagTable();

        unsigned char mark = 0;
        for (unsigned int i = 0; i < trackCount; ++i)
        {
            void* record = getRecord(recordSource, albumFirst, i);
            if (!record)
                continue;

            std::int16_t saveIndex =
                *reinterpret_cast<const std::int16_t*>(reinterpret_cast<const std::uintptr_t>(record) + 0x1cull);
            if (saveIndex < 0)
                continue;

            bool owned = false;
            bool isNew = false;
            if (IsCustomTapeSaveIndex(saveIndex))
            {
                owned = IsCustomTapeOwnedSaveIndex(saveIndex);
                isNew = IsCustomTapeNewFlagSaveIndex(saveIndex);
            }
            else if (flagTable)
            {
                std::uint16_t tableIndex =
                    static_cast<std::uint16_t>(static_cast<std::uint16_t>(saveIndex) + kVanillaOwnedIndexBias);
                if (tableIndex >= kVanillaOwnedIndexBias && tableIndex < kVanillaCassetteFlagRegionEndExclusive)
                {
                    std::uint8_t flagByte = flagTable[tableIndex];
                    owned = ((flagByte >> 1) & 1u) != 0;
                    isNew = (flagByte & 1u) != 0;
                }
            }

            if (owned && isNew)
            {
                std::int16_t selector =
                    *reinterpret_cast<const std::int16_t*>(reinterpret_cast<const std::uintptr_t>(record) + 0x24ull);
                mark |= (selector != 0) ? 0x08u : 0x80u;
            }
        }

        outMark = mark;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        outMark = 0;
        return false;
    }
}


static unsigned char __fastcall hkCassetteAlbumCheckNewFlag(void* albumInfo, void* uiUtilityTable)
{
    unsigned char mark = 0;
    if (TryComputeAlbumNewMark(albumInfo, uiUtilityTable, mark))
        return mark;

    return g_OrigCassetteAlbumCheckNewFlag ? g_OrigCassetteAlbumCheckNewFlag(albumInfo, uiUtilityTable) : 0;
}


static bool TryComputeAlbumUnreadInfo(void* albumInfo, void* uiUtilityTable, unsigned int* outCount, unsigned char* outFlag)
{
    if (!albumInfo || !uiUtilityTable || !outCount || !outFlag)
        return false;

    __try
    {
        *outCount = 0;
        *outFlag = 0;

        unsigned int trackCount = *reinterpret_cast<const std::uint16_t*>(reinterpret_cast<const std::uintptr_t>(albumInfo) + 0x10ull);
        if (trackCount == 0)
            return true;

        void* recordSource = *reinterpret_cast<void* const*>(reinterpret_cast<const std::uintptr_t>(uiUtilityTable) + 0xd8ull);
        if (!recordSource)
            return false;

        void** recordSourceVtable = *reinterpret_cast<void** const*>(recordSource);
        CassetteTrackRecordGetter_t getRecord =
            reinterpret_cast<CassetteTrackRecordGetter_t>(recordSourceVtable[0x188 / sizeof(void*)]);
        void* albumFirst = *reinterpret_cast<void* const*>(albumInfo);
        const std::uint8_t* flagTable = ResolveLiveCassetteFlagTable();

        unsigned int newCount = 0;
        unsigned char mark = 0;
        for (unsigned int i = 0; i < trackCount; ++i)
        {
            void* record = getRecord(recordSource, albumFirst, i);
            if (!record)
                continue;

            std::int16_t saveIndex =
                *reinterpret_cast<const std::int16_t*>(reinterpret_cast<const std::uintptr_t>(record) + 0x1cull);
            if (saveIndex < 0)
                continue;

            bool owned = false;
            bool isNew = false;
            if (IsCustomTapeSaveIndex(saveIndex))
            {
                owned = IsCustomTapeOwnedSaveIndex(saveIndex);
                isNew = IsCustomTapeNewFlagSaveIndex(saveIndex);
            }
            else if (flagTable)
            {
                std::uint16_t tableIndex =
                    static_cast<std::uint16_t>(static_cast<std::uint16_t>(saveIndex) + kVanillaOwnedIndexBias);
                if (tableIndex >= kVanillaOwnedIndexBias && tableIndex < kVanillaCassetteFlagRegionEndExclusive)
                {
                    std::uint8_t flagByte = flagTable[tableIndex];
                    owned = ((flagByte >> 1) & 1u) != 0;
                    isNew = (flagByte & 1u) != 0;
                }
            }

            if (owned && isNew)
            {
                std::int16_t selector =
                    *reinterpret_cast<const std::int16_t*>(reinterpret_cast<const std::uintptr_t>(record) + 0x24ull);
                mark |= (selector != 0) ? 0x04u : 0x40u;
                ++newCount;
            }
        }

        *outCount = newCount;
        *outFlag = mark;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}


static void __fastcall hkCassetteCheckUnreadInfo(void* albumInfo, void* uiUtilityTable, unsigned int* outCount, unsigned char* outFlag)
{
    if (TryComputeAlbumUnreadInfo(albumInfo, uiUtilityTable, outCount, outFlag))
        return;

    if (g_OrigCassetteCheckUnreadInfo)
    {
        g_OrigCassetteCheckUnreadInfo(albumInfo, uiUtilityTable, outCount, outFlag);
        return;
    }

    if (outCount)
        *outCount = 0;
    if (outFlag)
        *outFlag = 0;
}


bool Install_CustomTapeOwnership_Hooks()
{
    EnsureCustomTapeStateLoaded();
    ResolveLuaHelpers();
    LoadCustomTapeStateFromDisk();
    Sync_CustomTapeStateToLiveTable();

    bool okAdd = false;
    bool okAddByIndex = false;
    bool okIsGot = false;
    bool okSetNew = false;
    bool okCollect = false;

    if (void* target = ResolveGameAddress(gAddr.AddCassetteTapeTrack))
    {
        okAdd = CreateAndEnableHook(
            target,
            reinterpret_cast<void*>(&hkAddCassetteTapeTrack),
            reinterpret_cast<void**>(&g_OrigAddCassetteTapeTrack));
    }

    if (void* target = ResolveGameAddress(gAddr.AddCassetteTapeTrackByIndex))
    {
        okAddByIndex = CreateAndEnableHook(
            target,
            reinterpret_cast<void*>(&hkAddCassetteTapeTrackByIndex),
            reinterpret_cast<void**>(&g_OrigAddCassetteTapeTrackByIndex));
    }

    if (void* target = ResolveGameAddress(gAddr.IsGotCassetteTapeTrack))
    {
        okIsGot = CreateAndEnableHook(
            target,
            reinterpret_cast<void*>(&hkIsGotCassetteTapeTrack),
            reinterpret_cast<void**>(&g_OrigIsGotCassetteTapeTrack));
    }

    if (void* target = ResolveGameAddress(gAddr.SetCassetteTapeTrackNewFlag))
    {
        okSetNew = CreateAndEnableHook(
            target,
            reinterpret_cast<void*>(&hkSetCassetteTapeTrackNewFlag),
            reinterpret_cast<void**>(&g_OrigSetCassetteTapeTrackNewFlag));
    }

    if (void* target = ResolveGameAddress(gAddr.CollectGotTapes))
    {
        okCollect = CreateAndEnableHook(
            target,
            reinterpret_cast<void*>(&hkCollectGotTapes),
            reinterpret_cast<void**>(&g_OrigCollectGotTapes));
    }

    if (void* target = ResolveGameAddress(gAddr.IsNewCassetteTapeTrack))
    {
        if (!CreateAndEnableHook(
                target,
                reinterpret_cast<void*>(&hkIsNewCassetteTapeTrack),
                reinterpret_cast<void**>(&g_OrigIsNewCassetteTapeTrack)))
        {
            Log("[CustomTapes] ERROR: failed to hook IsNewCassetteTapeTrack - custom tapes' per-track 'new' tags and in-menu new-count will be wrong.\n");
        }
    }

    if (void* target = ResolveGameAddress(gAddr.CassetteMenuCheckNewFlag))
    {
        if (!CreateAndEnableHook(
                target,
                reinterpret_cast<void*>(&hkCassetteMenuCheckNewFlag),
                reinterpret_cast<void**>(&g_OrigCassetteMenuCheckNewFlag)))
        {
            Log("[CustomTapes] ERROR: failed to hook CheckNewFlag - custom tapes will not show the per-track NEW tag in the menu.\n");
        }
    }

    if (void* target = ResolveGameAddress(gAddr.CassetteAlbumCheckNewFlag))
    {
        if (!CreateAndEnableHook(
                target,
                reinterpret_cast<void*>(&hkCassetteAlbumCheckNewFlag),
                reinterpret_cast<void**>(&g_OrigCassetteAlbumCheckNewFlag)))
        {
            Log("[CustomTapes] ERROR: failed to hook album CheckNewFlag - custom albums will not show the category NEW mark.\n");
        }
    }

    if (void* target = ResolveGameAddress(gAddr.CassetteCheckUnreadInfo))
    {
        if (!CreateAndEnableHook(
                target,
                reinterpret_cast<void*>(&hkCassetteCheckUnreadInfo),
                reinterpret_cast<void**>(&g_OrigCassetteCheckUnreadInfo)))
        {
            Log("[CustomTapes] ERROR: failed to hook CheckUnreadInfo - custom tapes will not show the category tab unread count.\n");
        }
    }

    if (void* target = ResolveGameAddress(gAddr.GetCassetteTapeUnreadInfo))
    {
        if (!CreateAndEnableHook(
                target,
                reinterpret_cast<void*>(&hkGetCassetteTapeUnreadInfo),
                reinterpret_cast<void**>(&g_OrigGetCassetteTapeUnreadInfo)))
        {
            Log("[CustomTapes] ERROR: failed to hook GetCassetteTapeUnreadInfo - unowned custom tapes may show a phantom 'new' badge in the cassette menu.\n");
        }
    }

    if (!okAdd)
        Log("[CustomTapes] ERROR: failed to hook AddCassetteTapeTrack - custom tapes cannot be marked owned when picked up.\n");
    if (!okAddByIndex)
        Log("[CustomTapes] ERROR: failed to hook AddCassetteTapeTrackByIndex - custom tapes cannot be granted by save-index.\n");
    if (!okIsGot)
        Log("[CustomTapes] ERROR: failed to hook IsGotCassetteTapeTrack - custom tapes will report wrong owned state.\n");
    if (!okSetNew)
        Log("[CustomTapes] ERROR: failed to hook SetCassetteTapeTrackNewFlag - custom tapes' new-flag will not update.\n");
    if (!okCollect)
        Log("[CustomTapes] ERROR: failed to hook CollectGotTapes - owned custom tapes will not show in the cassette menu.\n");

    return okAdd && okIsGot && okSetNew && okCollect;
}


bool Uninstall_CustomTapeOwnership_Hooks()
{
    EnsureCustomTapeStateLoaded();
    DisableAndRemoveHook(ResolveGameAddress(gAddr.AddCassetteTapeTrack));
    DisableAndRemoveHook(ResolveGameAddress(gAddr.AddCassetteTapeTrackByIndex));
    DisableAndRemoveHook(ResolveGameAddress(gAddr.IsGotCassetteTapeTrack));
    DisableAndRemoveHook(ResolveGameAddress(gAddr.SetCassetteTapeTrackNewFlag));
    DisableAndRemoveHook(ResolveGameAddress(gAddr.CollectGotTapes));
    DisableAndRemoveHook(ResolveGameAddress(gAddr.GetCassetteTapeUnreadInfo));
    DisableAndRemoveHook(ResolveGameAddress(gAddr.IsNewCassetteTapeTrack));
    DisableAndRemoveHook(ResolveGameAddress(gAddr.CassetteMenuCheckNewFlag));
    DisableAndRemoveHook(ResolveGameAddress(gAddr.CassetteAlbumCheckNewFlag));
    DisableAndRemoveHook(ResolveGameAddress(gAddr.CassetteCheckUnreadInfo));

    g_OrigAddCassetteTapeTrack = nullptr;
    g_OrigAddCassetteTapeTrackByIndex = nullptr;
    g_OrigIsGotCassetteTapeTrack = nullptr;
    g_OrigSetCassetteTapeTrackNewFlag = nullptr;
    g_OrigCollectGotTapes = nullptr;
    g_OrigGetCassetteTapeUnreadInfo = nullptr;
    g_OrigIsNewCassetteTapeTrack = nullptr;
    g_OrigCassetteMenuCheckNewFlag = nullptr;
    g_OrigCassetteAlbumCheckNewFlag = nullptr;
    g_OrigCassetteCheckUnreadInfo = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_CustomTapeStateMutex);
        g_CustomOwnedTapeSaveIndices.clear();
        g_CustomNewTapeSaveIndices.clear();
        g_CustomTapePersistEntries.clear();
        g_ActiveSessionCustomTapeSaveIndices.clear();
        g_ActiveSessionTrackKeyToSaveIndex.clear();
        g_NewlyCreatedThisSessionSaveIndices.clear();
    }
    return true;
}