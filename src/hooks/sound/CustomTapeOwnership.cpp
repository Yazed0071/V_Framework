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

extern "C"
{
    #include "lua.h"
}

namespace
{


    using AddCassetteTapeTrack_t = std::uint64_t(__cdecl*)(lua_State* luaState);


    using IsGotCassetteTapeTrack_t = int(__cdecl*)(lua_State* luaState);


    using SetCassetteTapeTrackNewFlag_t = int(__cdecl*)(lua_State* luaState);


    using CollectGotTapes_t = std::uint64_t(__cdecl*)(std::uint32_t albumType, std::intptr_t outAlbumIds, std::uint32_t outCapacity, std::intptr_t cassetteCallbackThis);


    using GetQuarkSystemTable_t = void* (__cdecl*)();


    using GetTrackInfoByName_t = void* (__fastcall*)(void* soundMusicPlayer, std::int32_t trackNameStrCode);


    using lua_isstring_t = int(__fastcall*)(lua_State* luaState, int idx);
    using lua_type_t = int(__fastcall*)(lua_State* luaState, int idx);
    using lua_tolstring_t = const char* (__fastcall*)(lua_State* luaState, int idx, size_t* len);
    using lua_toboolean_t = int(__fastcall*)(lua_State* luaState, int idx);
    using lua_pushboolean_t = void(__fastcall*)(lua_State* luaState, int value);

    static constexpr std::int16_t kCustomSaveIndexMin = 300;
    static constexpr std::int16_t kCustomSaveIndexMax = 1999;
    static constexpr std::uint16_t kVanillaOwnedIndexBias = 0x00B7u;


    struct CustomTapePersistEntry
    {
        std::int16_t saveIndex = -1;
        std::string albumId;
        std::string fileName;
        bool owned = false;
        bool isNew = false;
    };

    static AddCassetteTapeTrack_t g_OrigAddCassetteTapeTrack = nullptr;
    static IsGotCassetteTapeTrack_t g_OrigIsGotCassetteTapeTrack = nullptr;
    static SetCassetteTapeTrackNewFlag_t g_OrigSetCassetteTapeTrackNewFlag = nullptr;
    static CollectGotTapes_t g_OrigCollectGotTapes = nullptr;

    static lua_isstring_t g_lua_isstring = nullptr;
    static lua_type_t g_lua_type = nullptr;
    static lua_tolstring_t g_lua_tolstring = nullptr;
    static lua_toboolean_t g_lua_toboolean = nullptr;
    static lua_pushboolean_t g_lua_pushboolean = nullptr;

    static std::mutex g_CustomTapeStateMutex;
    static std::unordered_set<int> g_CustomOwnedTapeSaveIndices;
    static std::unordered_set<int> g_CustomNewTapeSaveIndices;
    static std::unordered_map<int, CustomTapePersistEntry> g_CustomTapePersistEntries;
    static std::unordered_set<int> g_ActiveSessionCustomTapeSaveIndices;
    static std::unordered_map<std::string, int> g_ActiveSessionTrackKeyToSaveIndex;
    static std::unordered_set<int> g_NewlyCreatedThisSessionSaveIndices;
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
        Log("[CustomTapeState] Save failed path=%s\n", path.c_str());
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

    Log(
        "[CustomTapeState] Saved %zu tape entries to %s\n",
        entries.size(),
        path.c_str());

    return true;
}


static bool LoadCustomTapeStateFromDisk()
{
    const std::string path = GetCustomTapeStateFilePath();
    std::ifstream file(path);
    if (!file.is_open())
    {
        Log("[CustomTapeState] No persistence file yet at %s\n", path.c_str());
        return true;
    }

    std::unordered_set<int> loadedOwned;
    std::unordered_set<int> loadedNew;
    std::unordered_map<int, CustomTapePersistEntry> loadedEntries;

    static const std::regex entryPattern(
        R"(\[(\d+)\]\s*=\s*\{\s*saveIndex\s*=\s*(\d+)\s*,\s*albumId\s*=\s*\[\[(.*?)\]\]\s*,\s*fileName\s*=\s*\[\[(.*?)\]\]\s*,\s*owned\s*=\s*(true|false)\s*,\s*new\s*=\s*(true|false)\s*\})");

    std::string line;
    while (std::getline(file, line))
    {
        std::smatch match;
        if (!std::regex_search(line, match, entryPattern))
            continue;

        const int keySaveIndex = std::stoi(match[1].str());
        const int valueSaveIndex = std::stoi(match[2].str());
        if (keySaveIndex != valueSaveIndex)
            continue;

        if (keySaveIndex < kCustomSaveIndexMin || keySaveIndex > kCustomSaveIndexMax)
            continue;

        CustomTapePersistEntry entry;
        entry.saveIndex = static_cast<std::int16_t>(keySaveIndex);
        entry.albumId = match[3].str();
        entry.fileName = match[4].str();
        entry.owned = match[5].str() == "true";
        entry.isNew = match[6].str() == "true";

        if (entry.owned)
            loadedOwned.insert(keySaveIndex);

        if (entry.isNew)
            loadedNew.insert(keySaveIndex);

        loadedEntries[keySaveIndex] = std::move(entry);
    }

    {
        std::lock_guard<std::mutex> lock(g_CustomTapeStateMutex);
        g_CustomOwnedTapeSaveIndices = std::move(loadedOwned);
        g_CustomNewTapeSaveIndices = std::move(loadedNew);
        g_CustomTapePersistEntries = std::move(loadedEntries);

        g_ActiveSessionCustomTapeSaveIndices.clear();
        g_ActiveSessionTrackKeyToSaveIndex.clear();
        g_NewlyCreatedThisSessionSaveIndices.clear();
    }

    Log(
        "[CustomTapeState] Loaded owned=%zu new=%zu metadata=%zu from %s\n",
        g_CustomOwnedTapeSaveIndices.size(),
        g_CustomNewTapeSaveIndices.size(),
        g_CustomTapePersistEntries.size(),
        path.c_str());


    for (const auto& kv : g_CustomTapePersistEntries)
    {
        const auto& entry = kv.second;
        V_FrameWorkState::SetTapeOwnedBySaveIndex(entry.saveIndex, entry.owned);
        V_FrameWorkState::SetTapeNewBySaveIndex(entry.saveIndex, entry.isNew);
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
        Log(
            "[CustomTapeState] Metadata saveIndex=%d albumId=%s fileName=%s\n",
            static_cast<int>(saveIndex),
            albumId ? albumId : "",
            fileName ? fileName : "");
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

    Log(
        "[CustomTapeState] ResolveOrCreateCustomTapeSaveIndex albumId=%s fileName=%s requested=%d resolved=%d created=%d\n",
        albumId,
        fileName,
        static_cast<int>(requestedSaveIndex),
        static_cast<int>(outResolvedSaveIndex),
        outWasCreated ? 1 : 0);

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

    Log(
        "[CustomTapeState] InitializeCustomTapeStateIfMissing saveIndex=%d owned=%d new=%d\n",
        static_cast<int>(saveIndex),
        owned ? 1 : 0,
        isNew ? 1 : 0);
}


bool IsCustomTapeOwnedSaveIndex(std::int16_t saveIndex)
{
    EnsureCustomTapeStateLoaded();
    std::lock_guard<std::mutex> lock(g_CustomTapeStateMutex);
    return g_CustomOwnedTapeSaveIndices.find(static_cast<int>(saveIndex)) != g_CustomOwnedTapeSaveIndices.end();
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
        Log("[CustomTapeState] Adopted live new-flag changes from cassette table and saved.\n");
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

    Log(
        "[CustomTapeState] AddCassetteTapeTrack custom track=%s saveIndex=%d owned=1 new=1 ownedChanged=%d newChanged=%d metadataChanged=%d\n",
        trackName,
        static_cast<int>(saveIndex),
        ownedChanged ? 1 : 0,
        newChanged ? 1 : 0,
        metadataChanged ? 1 : 0);

    return 0ull;
}


static int __cdecl hkIsGotCassetteTapeTrack(lua_State* luaState)
{
    const char* trackName = GetLuaTrackNameArg(luaState);
    if (!trackName)
        return g_OrigIsGotCassetteTapeTrack ? g_OrigIsGotCassetteTapeTrack(luaState) : 1;

    void* trackInfo = ResolveTrackInfoByName(trackName);
    std::int16_t saveIndex = -1;

    if (!TryGetTrackSaveIndex(trackInfo, saveIndex) || !IsCustomTapeSaveIndex(saveIndex))
        return g_OrigIsGotCassetteTapeTrack ? g_OrigIsGotCassetteTapeTrack(luaState) : 1;

    const bool owned = IsCustomTapeOwnedSaveIndex(saveIndex);
    PushLuaBool(luaState, owned);

    Log(
        "[CustomTapeState] IsGotCassetteTapeTrack custom track=%s saveIndex=%d owned=%d\n",
        trackName,
        static_cast<int>(saveIndex),
        owned ? 1 : 0);

    return 1;
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

    Log(
        "[CustomTapeState] SetCassetteTapeTrackNewFlag custom track=%s saveIndex=%d isNew=%d changed=%d metadataChanged=%d\n",
        trackName,
        static_cast<int>(saveIndex),
        isNew ? 1 : 0,
        changed ? 1 : 0,
        metadataChanged ? 1 : 0);

    return 0;
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
        return g_OrigCollectGotTapes
            ? g_OrigCollectGotTapes(albumType, outAlbumIds, outCapacity, cassetteCallbackThis)
            : 0ull;
    }
}


bool Install_CustomTapeOwnership_Hooks()
{
    EnsureCustomTapeStateLoaded();
    ResolveLuaHelpers();
    LoadCustomTapeStateFromDisk();
    Sync_CustomTapeStateToLiveTable();

    bool okAdd = false;
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

    Log("[Hook] CustomTapeState AddCassetteTapeTrack: %s\n", okAdd ? "OK" : "FAIL");
    Log("[Hook] CustomTapeState IsGotCassetteTapeTrack: %s\n", okIsGot ? "OK" : "FAIL");
    Log("[Hook] CustomTapeState SetCassetteTapeTrackNewFlag: %s\n", okSetNew ? "OK" : "FAIL");
    Log("[Hook] CustomTapeState CollectGotTapes: %s\n", okCollect ? "OK" : "FAIL");

    return okAdd && okIsGot && okSetNew && okCollect;
}


bool Uninstall_CustomTapeOwnership_Hooks()
{
    EnsureCustomTapeStateLoaded();
    DisableAndRemoveHook(ResolveGameAddress(gAddr.AddCassetteTapeTrack));
    DisableAndRemoveHook(ResolveGameAddress(gAddr.IsGotCassetteTapeTrack));
    DisableAndRemoveHook(ResolveGameAddress(gAddr.SetCassetteTapeTrackNewFlag));
    DisableAndRemoveHook(ResolveGameAddress(gAddr.CollectGotTapes));

    g_OrigAddCassetteTapeTrack = nullptr;
    g_OrigIsGotCassetteTapeTrack = nullptr;
    g_OrigSetCassetteTapeTrackNewFlag = nullptr;
    g_OrigCollectGotTapes = nullptr;
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