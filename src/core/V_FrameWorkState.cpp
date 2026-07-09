#include "pch.h"
#include "V_FrameWorkState.h"
#include "log.h"
#include "AddressSet.h"
#include "../hooks/equip/EquipIdCompression.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace V_FrameWorkState
{
    namespace
    {
        static constexpr const char* kSavePath    = "mod\\V_FrameWork\\V_FrameWork_State.lua";
        static constexpr const char* kLegacyPath  = "mod\\saves\\V_FrameWork_State.lua";


        static constexpr std::int32_t kFirstCustomEquipIdMinimum = 1;
        static constexpr std::int32_t kFirstCustomDevelopId = 0x1000;
        static constexpr std::int32_t kFirstCustomFlowIndex = 922;
        static constexpr std::int16_t kFirstCustomTapeSaveIndex = 300;
        static constexpr std::int16_t kMaxCustomTapeSaveIndex = 32000;
        static constexpr std::int32_t kTapeOrphanGraceLaunches = 2;
        static constexpr std::int32_t kEquipOrphanGraceLaunches = 2;
        static constexpr std::int32_t kMaxCustomConstantValue = 0xFFF0;

        struct EquipEntry
        {


            std::int32_t developId = 0;
            std::int32_t flowIndex = 0;

            std::int32_t partsType = 0;
            std::int32_t selector  = 0;
            std::uint8_t variantSelectors[14] = {};

            std::int32_t misses = 0;

            std::int8_t developed = -1;

            bool isNew = false;
        };

        struct TapeEntry
        {
            std::int16_t saveIndex = -1;
            bool owned = false;
            bool isNew = false;
            std::int32_t misses = 0;
        };

        struct State
        {
            bool loaded = false;
            bool dirty = false;
            std::unordered_map<std::string, EquipEntry> equips;
            std::unordered_map<std::string, TapeEntry> tapes;
            std::unordered_map<std::string, std::int32_t> constants;
        };

        static State g_State;
        static std::mutex g_Mutex;
        static std::vector<std::int32_t> g_PendingDevelopedResets;
        static int g_BatchDepth = 0;
        static std::unordered_set<std::int16_t> g_TapeSaveIndexInUse;


        static std::unordered_map<std::string, std::int32_t> g_SessionEquipIds;

        static std::unordered_map<std::string, std::int32_t> g_SessionFlowIndices;
        static std::int32_t g_SessionNextFlowIndex = kFirstCustomFlowIndex;

        static std::string Trim(const std::string& s)
        {
            const auto begin = s.find_first_not_of(" \t\r\n");
            if (begin == std::string::npos)
                return {};
            const auto end = s.find_last_not_of(" \t\r\n");
            return s.substr(begin, end - begin + 1);
        }

        static void EnsureSaveDirectory()
        {
            CreateDirectoryA("mod", nullptr);
            CreateDirectoryA("mod\\V_FrameWork", nullptr);
        }


        static void MigrateLegacyStateFile_NoLock()
        {
            const DWORD newAttr = GetFileAttributesA(kSavePath);
            if (newAttr != INVALID_FILE_ATTRIBUTES)
                return;

            const DWORD oldAttr = GetFileAttributesA(kLegacyPath);
            if (oldAttr == INVALID_FILE_ATTRIBUTES)
                return;

            CreateDirectoryA("mod", nullptr);
            CreateDirectoryA("mod\\V_FrameWork", nullptr);

            MoveFileA(kLegacyPath, kSavePath);
        }


        static bool ParseEquipLine(const std::string& line, std::string& outKey, EquipEntry& out)
        {
            const auto lb = line.find("[\"");
            if (lb == std::string::npos) return false;
            const auto rb = line.find("\"]", lb + 2);
            if (rb == std::string::npos) return false;

            outKey = line.substr(lb + 2, rb - (lb + 2));
            out = {};

            auto findField = [&](const char* name) -> std::int32_t
            {
                const std::string field = std::string(name) + " = ";
                const auto pos = line.find(field);
                if (pos == std::string::npos) return 0;
                const auto start = pos + field.size();
                std::string numStr;
                for (auto i = start; i < line.size(); ++i)
                {
                    const char c = line[i];
                    if (c == ',' || c == '}' || c == ' ') break;
                    numStr.push_back(c);
                }
                try { return static_cast<std::int32_t>(std::stol(numStr, nullptr, 0)); }
                catch (...) { return 0; }
            };

            out.developId = findField("developId");
            out.flowIndex = findField("flowIndex");
            out.partsType = findField("partsType");
            out.selector  = findField("selector");
            out.misses    = findField("misses");

            {
                const char* tag = "variantSelectors = \"";
                const auto pos = line.find(tag);
                if (pos != std::string::npos)
                {
                    std::size_t i = pos + std::strlen(tag);
                    std::size_t vi = 0;
                    std::string numStr;
                    for (; i < line.size() && vi < 14; ++i)
                    {
                        const char c = line[i];
                        if (c >= '0' && c <= '9') { numStr.push_back(c); continue; }
                        if (!numStr.empty())
                        {
                            try
                            {
                                const long v = std::stol(numStr);
                                if (v > 0 && v <= 0xFF)
                                    out.variantSelectors[vi++] =
                                        static_cast<std::uint8_t>(v);
                            }
                            catch (...) {}
                            numStr.clear();
                        }
                        if (c == '"') break;
                    }
                }
            }
            if (line.find("developed = true") != std::string::npos)
                out.developed = 1;
            else if (line.find("developed = false") != std::string::npos)
                out.developed = 0;

            out.isNew = line.find("new = true") != std::string::npos;

            return !outKey.empty();
        }


        static bool ParseTapeLine(const std::string& line, std::string& outKey, TapeEntry& out)
        {
            const auto lb = line.find("[\"");
            if (lb == std::string::npos) return false;
            const auto rb = line.find("\"]", lb + 2);
            if (rb == std::string::npos) return false;

            outKey = line.substr(lb + 2, rb - (lb + 2));
            out = {};

            auto findIntField = [&](const char* name) -> std::int32_t
            {
                const std::string field = std::string(name) + " = ";
                const auto pos = line.find(field);
                if (pos == std::string::npos) return 0;
                const auto start = pos + field.size();
                std::string numStr;
                for (auto i = start; i < line.size(); ++i)
                {
                    const char c = line[i];
                    if (c == ',' || c == '}' || c == ' ') break;
                    numStr.push_back(c);
                }
                try { return static_cast<std::int32_t>(std::stol(numStr, nullptr, 0)); }
                catch (...) { return 0; }
            };

            out.saveIndex = static_cast<std::int16_t>(findIntField("saveIndex"));
            out.owned = line.find("owned = true") != std::string::npos;
            out.isNew = line.find("new = true") != std::string::npos;
            out.misses = findIntField("misses");
            return !outKey.empty() && out.saveIndex > 0;
        }

        static bool ParseConstantLine(const std::string& line, std::string& outKey, std::int32_t& outValue)
        {
            const auto lb = line.find("[\"");
            if (lb == std::string::npos) return false;
            const auto rb = line.find("\"]", lb + 2);
            if (rb == std::string::npos) return false;

            outKey = line.substr(lb + 2, rb - (lb + 2));
            outValue = 0;

            const auto eq = line.find('=', rb);
            if (eq == std::string::npos) return false;

            std::string numStr;
            for (auto i = eq + 1; i < line.size(); ++i)
            {
                const char c = line[i];
                if (c == ' ' && numStr.empty()) continue;
                if (c == ',' || c == '}' || c == ' ') break;
                numStr.push_back(c);
            }
            try { outValue = static_cast<std::int32_t>(std::stol(numStr, nullptr, 0)); }
            catch (...) { return false; }
            return !outKey.empty() && outValue != 0;
        }

        static void SaveToDisk_NoLock();

        static void LoadFromDisk_NoLock()
        {
            if (g_State.loaded) return;
            g_State.loaded = true;
            g_State.equips.clear();
            g_State.tapes.clear();
            g_TapeSaveIndexInUse.clear();

            MigrateLegacyStateFile_NoLock();

            std::ifstream in(kSavePath);
            if (!in)
            {
                return;
            }

            enum Section { None, Equips, Tapes, Constants } section = None;

            std::string line;
            while (std::getline(in, line))
            {
                const std::string trimmed = Trim(line);

                if (trimmed.rfind("equips", 0) == 0 &&
                    trimmed.find('{') != std::string::npos)
                {
                    section = Equips;
                    continue;
                }

                if (trimmed.rfind("tapes", 0) == 0 &&
                    trimmed.find('{') != std::string::npos)
                {
                    section = Tapes;
                    continue;
                }

                if (trimmed.rfind("constants", 0) == 0 &&
                    trimmed.find('{') != std::string::npos)
                {
                    section = Constants;
                    continue;
                }


                if (trimmed == "}," || (trimmed == "}" && section != None))
                {
                    section = None;
                    continue;
                }

                if (section == Equips)
                {
                    std::string key;
                    EquipEntry entry;
                    if (ParseEquipLine(trimmed, key, entry))
                        g_State.equips[key] = entry;
                }
                else if (section == Tapes)
                {
                    std::string key;
                    TapeEntry entry;
                    if (ParseTapeLine(trimmed, key, entry))
                    {
                        g_State.tapes[key] = entry;
                        g_TapeSaveIndexInUse.insert(entry.saveIndex);
                        Log("[CustomTapes] tape loaded: '%s' (saveIndex %d)\n", key.c_str(), static_cast<int>(entry.saveIndex));
                    }
                }
                else if (section == Constants)
                {
                    std::string key;
                    std::int32_t value = 0;
                    if (ParseConstantLine(trimmed, key, value))
                        g_State.constants[key] = value;
                }
            }

            in.close();

            bool gcChanged = false;
            for (auto it = g_State.tapes.begin(); it != g_State.tapes.end(); )
            {
                if (it->second.misses >= kTapeOrphanGraceLaunches)
                {
                    Log("[CustomTapes] tape deleted: '%s' (saveIndex %d) - mod uninstalled; freeing the save slot.\n", it->first.c_str(), static_cast<int>(it->second.saveIndex));
                    it = g_State.tapes.erase(it);
                    gcChanged = true;
                }
                else
                {
                    ++it->second.misses;
                    gcChanged = true;
                    ++it;
                }
            }

            for (auto it = g_State.equips.begin(); it != g_State.equips.end(); )
            {
                if (it->second.misses >= kEquipOrphanGraceLaunches)
                {
                    if (it->second.partsType != 0)
                        Log("[Outfit] Removed \"%s\" (developId %d, partsType 0x%02X) - not registered for %d launches; freeing the id.\n",
                            it->first.c_str(), it->second.developId, it->second.partsType, kEquipOrphanGraceLaunches);
                    else
                        Log("[Equip] Removed \"%s\" (developId %d) - not registered for %d launches; freeing the id.\n",
                            it->first.c_str(), it->second.developId, kEquipOrphanGraceLaunches);

                    if (it->second.developId != 0)
                        g_PendingDevelopedResets.push_back(it->second.developId);
                    it = g_State.equips.erase(it);
                    gcChanged = true;
                }
                else
                {
                    ++it->second.misses;
                    gcChanged = true;
                    ++it;
                }
            }

            if (gcChanged)
            {
                g_State.dirty = true;
                SaveToDisk_NoLock();
            }
        }

        static void SaveToDisk_NoLock()
        {
            if (g_BatchDepth > 0)
                return;

            EnsureSaveDirectory();

            std::ofstream out(kSavePath, std::ios::binary | std::ios::trunc);
            if (!out)
            {
                Log("[V_FrameWorkState] ERROR: could not write '%s' - custom-tape ownership/save-index state will not persist.\n", kSavePath);
                return;
            }

            out << "return {\n";


            {
                std::vector<std::pair<std::string, EquipEntry>> sorted;
                sorted.reserve(g_State.equips.size());
                for (const auto& kv : g_State.equips)
                    sorted.emplace_back(kv.first, kv.second);
                std::sort(sorted.begin(), sorted.end(),
                    [](const auto& a, const auto& b) { return a.first < b.first; });

                out << "    equips = {\n";
                for (const auto& kv : sorted)
                {


                    if (kv.second.developId == 0 && kv.second.flowIndex == 0 &&
                        kv.second.partsType == 0 && kv.second.selector == 0 &&
                        kv.second.misses == 0 && kv.second.developed < 0 &&
                        !kv.second.isNew)
                        continue;
                    out << "        [\"" << kv.first << "\"] = {";
                    if (kv.second.developId != 0)
                        out << " developId = " << kv.second.developId << ",";
                    if (kv.second.flowIndex != 0)
                        out << " flowIndex = " << kv.second.flowIndex << ",";
                    if (kv.second.partsType != 0)
                        out << " partsType = " << kv.second.partsType << ",";
                    if (kv.second.selector != 0)
                        out << " selector = " << kv.second.selector << ",";
                    {
                        std::size_t last = 0;
                        for (std::size_t vi = 0; vi < 14; ++vi)
                            if (kv.second.variantSelectors[vi] != 0) last = vi + 1;
                        if (last != 0)
                        {
                            out << " variantSelectors = \"";
                            for (std::size_t vi = 0; vi < last; ++vi)
                            {
                                if (vi) out << ",";
                                out << static_cast<int>(kv.second.variantSelectors[vi]);
                            }
                            out << "\",";
                        }
                    }
                    if (kv.second.misses != 0)
                        out << " misses = " << kv.second.misses << ",";
                    if (kv.second.developed >= 0)
                        out << " developed = " << (kv.second.developed == 1 ? "true" : "false") << ",";
                    if (kv.second.isNew)
                        out << " new = true,";
                    out << " },\n";
                }
                out << "    },\n";
            }


            {
                std::vector<std::pair<std::string, TapeEntry>> sorted;
                sorted.reserve(g_State.tapes.size());
                for (const auto& kv : g_State.tapes)
                    sorted.emplace_back(kv.first, kv.second);
                std::sort(sorted.begin(), sorted.end(),
                    [](const auto& a, const auto& b) { return a.second.saveIndex < b.second.saveIndex; });

                out << "    tapes = {\n";
                for (const auto& kv : sorted)
                {
                    out << "        [\"" << kv.first << "\"] = {"
                        << " saveIndex = " << static_cast<int>(kv.second.saveIndex)
                        << ", owned = " << (kv.second.owned ? "true" : "false")
                        << ", new = " << (kv.second.isNew ? "true" : "false");
                    if (kv.second.misses != 0)
                        out << ", misses = " << kv.second.misses;
                    out << " },\n";
                }
                out << "    },\n";
            }


            {
                std::vector<std::pair<std::string, std::int32_t>> sorted;
                sorted.reserve(g_State.constants.size());
                for (const auto& kv : g_State.constants)
                    sorted.emplace_back(kv.first, kv.second);
                std::sort(sorted.begin(), sorted.end(),
                    [](const auto& a, const auto& b) { return a.first < b.first; });

                out << "    constants = {\n";
                for (const auto& kv : sorted)
                {
                    if (kv.second == 0)
                        continue;
                    out << "        [\"" << kv.first << "\"] = " << kv.second << ",\n";
                }
                out << "    },\n";
            }

            out << "}\n";
            g_State.dirty = false;
        }

        static bool IsEquipIdInUse_NoLock(std::int32_t id)
        {
            for (const auto& kv : g_SessionEquipIds)
                if (kv.second == id) return true;
            return false;
        }

        static bool IsDevelopIdInUse_NoLock(std::int32_t id)
        {
            for (const auto& kv : g_State.equips)
                if (kv.second.developId == id) return true;
            return false;
        }

        static bool IsTapeSaveIndexInUse_NoLock(std::int16_t idx)
        {
            return g_TapeSaveIndexInUse.find(idx) != g_TapeSaveIndexInUse.end();
        }

        static bool IsConstantValueInUse_NoLock(const std::string& spacePrefix, std::int32_t value)
        {
            for (const auto& kv : g_State.constants)
                if (kv.second == value &&
                    kv.first.compare(0, spacePrefix.size(), spacePrefix) == 0)
                    return true;
            return false;
        }

        static bool IsSafeConstantNamePart(const char* s)
        {
            for (const char* p = s; *p; ++p)
            {
                const char c = *p;
                if (c == '"' || c == '[' || c == ']' || c == '\n' || c == '\r')
                    return false;
            }
            return true;
        }


        static bool g_NativeTableSynced = false;

        static std::int32_t AllocateNextFreeEquipId_NoLock(std::int32_t minimum)
        {
            if (!g_NativeTableSynced)
            {
                EquipIdCompression::SyncFromNativeTable();
                g_NativeTableSynced = true;
            }

            const std::int32_t floor =
                (minimum > kFirstCustomEquipIdMinimum)
                    ? minimum
                    : kFirstCustomEquipIdMinimum;

            const std::int32_t result =
                EquipIdCompression::FindLowestFreeEquipId(
                    [](std::int32_t equipId) {
                        return IsEquipIdInUse_NoLock(equipId);
                    },
                    floor);

            if (result < 0)
            {
                Log("[V_FrameWorkState] AllocateNextFreeEquipId: NO FREE "
                    "in-bounds slot remains (vanilla + session filled all "
                    "0x289 slots above floor=0x%X); allocation fails.\n", floor);
            }
            return result;
        }


        static std::int32_t AllocateNextFreeDevelopId_NoLock(std::int32_t minimum)
        {
            std::int32_t id = (minimum > kFirstCustomDevelopId) ? minimum : kFirstCustomDevelopId;
            while (IsDevelopIdInUse_NoLock(id))
                ++id;
            return id;
        }

        static std::int16_t AllocateNextFreeTapeSaveIndex_NoLock(std::int16_t minimum)
        {
            std::int16_t idx = (minimum > kFirstCustomTapeSaveIndex) ? minimum : kFirstCustomTapeSaveIndex;
            while (idx <= kMaxCustomTapeSaveIndex && IsTapeSaveIndexInUse_NoLock(idx)) ++idx;
            return (idx <= kMaxCustomTapeSaveIndex) ? idx : static_cast<std::int16_t>(-1);
        }
    }

    void Load()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
    }

    void Save()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        if (g_State.dirty)
            SaveToDisk_NoLock();
    }

    void BeginBatch()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        ++g_BatchDepth;
    }

    void EndBatch()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        if (g_BatchDepth > 0)
            --g_BatchDepth;
        if (g_BatchDepth == 0 && g_State.dirty)
            SaveToDisk_NoLock();
    }

    bool ResolveOrCreateEquipId(
        const char* key,
        std::int32_t minimumId,
        std::int32_t& outEquipId)
    {
        outEquipId = 0;
        if (!key || !key[0]) return false;

        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();


        auto it = g_SessionEquipIds.find(key);
        if (it != g_SessionEquipIds.end() && it->second != 0)
        {
            outEquipId = it->second;
            return true;
        }

        const std::int32_t newId = AllocateNextFreeEquipId_NoLock(minimumId);
        if (newId < 0)
        {


            outEquipId = 0;
            return false;
        }

        g_SessionEquipIds[key] = newId;
        outEquipId = newId;

        return true;
    }

    bool ResolveOrCreateDevelopId(
        const char* key,
        std::int32_t minimumId,
        std::int32_t& outDevelopId,
        bool* outCreated)
    {
        outDevelopId = 0;
        if (outCreated) *outCreated = false;
        if (!key || !key[0]) return false;

        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();

        auto it = g_State.equips.find(key);
        if (it != g_State.equips.end() && it->second.developId != 0)
        {
            it->second.misses = 0;
            g_State.dirty = true;
            outDevelopId = it->second.developId;
            if (outCreated) *outCreated = false;   // found existing persisted id -> loaded
            return true;
        }

        const std::int32_t newId = AllocateNextFreeDevelopId_NoLock(minimumId);
        g_State.equips[key].developId = newId;
        g_State.equips[key].misses = 0;
        g_State.dirty = true;
        outDevelopId = newId;
        if (outCreated) *outCreated = true;        // minted a new id -> added first time

        SaveToDisk_NoLock();

        return true;
    }

    std::int32_t GetDevelopIdByKey(const char* key)
    {
        if (!key || !key[0]) return 0;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        auto it = g_State.equips.find(key);
        return (it != g_State.equips.end()) ? it->second.developId : 0;
    }

    std::vector<std::int32_t> TakePendingDevelopedResets()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        std::vector<std::int32_t> out;
        out.swap(g_PendingDevelopedResets);
        return out;
    }

    bool ResolveDevelopedFlag(const char* key, bool defaultDeveloped)
    {
        if (!key || !key[0]) return defaultDeveloped;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        auto it = g_State.equips.find(key);
        if (it == g_State.equips.end())
            return defaultDeveloped;
        if (it->second.developed < 0)
        {
            it->second.developed = defaultDeveloped ? 1 : 0;
            it->second.isNew = true;
            g_State.dirty = true;
            SaveToDisk_NoLock();
        }
        return it->second.developed == 1;
    }

    bool IsManagedDevelopId(std::int32_t developId)
    {
        if (developId == 0) return false;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        for (const auto& kv : g_State.equips)
            if (kv.second.developId == developId)
                return true;
        return false;
    }

    void SetDevelopedByDevelopId(std::int32_t developId, bool developed)
    {
        if (developId == 0) return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        const std::int8_t v = developed ? 1 : 0;
        for (auto& kv : g_State.equips)
        {
            if (kv.second.developId == developId)
            {
                if (kv.second.developed != v)
                {
                    kv.second.developed = v;
                    g_State.dirty = true;
                    SaveToDisk_NoLock();
                }
                return;
            }
        }
    }

    bool GetDevelopedByDevelopId(std::int32_t developId)
    {
        if (developId == 0) return false;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        for (const auto& kv : g_State.equips)
            if (kv.second.developId == developId)
                return kv.second.developed == 1;
        return false;
    }

    void ForEachManagedDevelop(
        const std::function<void(std::int32_t developId, bool developed, bool isNew)>& callback)
    {
        if (!callback) return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        for (const auto& kv : g_State.equips)
            if (kv.second.developId != 0)
                callback(kv.second.developId, kv.second.developed == 1, kv.second.isNew);
    }

    void SetNewByDevelopId(std::int32_t developId, bool isNew)
    {
        if (developId == 0) return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        for (auto& kv : g_State.equips)
        {
            if (kv.second.developId == developId)
            {
                if (kv.second.isNew != isNew)
                {
                    kv.second.isNew = isNew;
                    g_State.dirty = true;
                    SaveToDisk_NoLock();
                }
                return;
            }
        }
    }

    bool GetNewByDevelopId(std::int32_t developId)
    {
        if (developId == 0) return false;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        for (const auto& kv : g_State.equips)
            if (kv.second.developId == developId)
                return kv.second.isNew;
        return false;
    }

    bool ResolveOrCreateFlowIndex(
        const char* key,
        std::int32_t minimumIndex,
        std::int32_t& outFlowIndex)
    {
        (void)minimumIndex;
        outFlowIndex = 0;
        if (!key || !key[0]) return false;

        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();

        auto sit = g_SessionFlowIndices.find(key);
        if (sit != g_SessionFlowIndices.end())
        {
            outFlowIndex = sit->second;
            return true;
        }

        const std::int32_t newIdx = g_SessionNextFlowIndex++;
        g_SessionFlowIndices[key] = newIdx;

        g_State.equips[key].flowIndex = newIdx;
        g_State.dirty = true;
        outFlowIndex = newIdx;

        SaveToDisk_NoLock();

        return true;
    }

    bool ResolveOrCreateConstantValue(
        const char* spaceTag,
        const char* name,
        std::int32_t minimumValue,
        std::int32_t& outValue)
    {
        outValue = 0;
        if (!spaceTag || !spaceTag[0] || !name || !name[0]) return false;
        if (!IsSafeConstantNamePart(spaceTag) || !IsSafeConstantNamePart(name))
        {
            Log("[Constants] ERROR: unsafe characters in constant name '%s' (space '%s'); rejected.\n", name, spaceTag);
            return false;
        }

        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();

        const std::string prefix = std::string(spaceTag) + ":";
        const std::string key = prefix + name;

        auto it = g_State.constants.find(key);
        if (it != g_State.constants.end() && it->second != 0)
        {
            outValue = it->second;
            return true;
        }

        std::int32_t value = (minimumValue > 0) ? minimumValue : 1;
        while (value <= kMaxCustomConstantValue && IsConstantValueInUse_NoLock(prefix, value))
            ++value;
        if (value > kMaxCustomConstantValue)
        {
            Log("[Constants] ERROR: no free value for '%s' - space '%s' pool [%d..%d] is full.\n",
                name, spaceTag, minimumValue, kMaxCustomConstantValue);
            return false;
        }

        g_State.constants[key] = value;
        g_State.dirty = true;
        outValue = value;

        SaveToDisk_NoLock();

#ifdef _DEBUG
        Log("[Constants] allocated %s = %d\n", key.c_str(), value);
#endif
        return true;
    }

    std::uint8_t GetPersistedOutfitPartsType(const char* key)
    {
        if (!key || !key[0]) return 0;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        auto it = g_State.equips.find(key);
        if (it == g_State.equips.end()) return 0;
        const std::int32_t v = it->second.partsType;
        return (v > 0 && v <= 0xFF) ? static_cast<std::uint8_t>(v) : 0;
    }

    std::uint8_t GetPersistedOutfitSelector(const char* key)
    {
        if (!key || !key[0]) return 0;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        auto it = g_State.equips.find(key);
        if (it == g_State.equips.end()) return 0;
        const std::int32_t v = it->second.selector;
        return (v > 0 && v <= 0xFF) ? static_cast<std::uint8_t>(v) : 0;
    }

    void SetPersistedOutfitIds(const char* key,
                               std::uint8_t partsType,
                               std::uint8_t selector)
    {
        if (!key || !key[0]) return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();

        auto& e = g_State.equips[key];
        bool changed = false;
        if (partsType != 0 && e.partsType != static_cast<std::int32_t>(partsType))
        {
            e.partsType = static_cast<std::int32_t>(partsType);
            changed = true;
        }
        if (selector != 0 && e.selector != static_cast<std::int32_t>(selector))
        {
            e.selector = static_cast<std::int32_t>(selector);
            changed = true;
        }
        if (changed)
        {
            g_State.dirty = true;
            SaveToDisk_NoLock();
        }
    }

    std::size_t GetPersistedOutfitVariantSelectors(const char* key,std::uint8_t* out,std::size_t cap)
    {
        if (!key || !key[0] || !out || cap == 0) return 0;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        auto it = g_State.equips.find(key);
        if (it == g_State.equips.end()) return 0;

        if (it->second.misses != 0)
        {
            it->second.misses = 0;
            g_State.dirty = true;
        }

        std::size_t nonZero = 0;
        const std::size_t n = (cap < 14) ? cap : std::size_t{14};
        for (std::size_t i = 0; i < n; ++i)
        {
            out[i] = it->second.variantSelectors[i];
            if (out[i] != 0) ++nonZero;
        }
        for (std::size_t i = n; i < cap; ++i) out[i] = 0;
        return nonZero;
    }

    void SetPersistedOutfitVariantSelectors(const char* key, const std::uint8_t* selectors, std::size_t count)
    {
        if (!key || !key[0]) return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();

        auto& e = g_State.equips[key];
        bool changed = false;

        if (e.misses != 0)
        {
            e.misses = 0;
            changed = true;
        }
        for (std::size_t i = 0; i < 14; ++i)
        {
            const std::uint8_t v =
                (selectors && i < count) ? selectors[i] : std::uint8_t{0};
            if (e.variantSelectors[i] != v)
            {
                e.variantSelectors[i] = v;
                changed = true;
            }
        }
        if (changed)
        {
            g_State.dirty = true;
            SaveToDisk_NoLock();
        }
    }

    void ForEachPersistedOutfit(
        const std::function<void(const std::string& key, std::uint8_t partsType, std::uint8_t selector, const std::uint8_t* variants)>& callback)
    {
        if (!callback) return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        for (const auto& kv : g_State.equips)
        {
            const auto& e = kv.second;
            const std::uint8_t pt =
                (e.partsType > 0 && e.partsType <= 0xFF)
                    ? static_cast<std::uint8_t>(e.partsType) : std::uint8_t{0};
            const std::uint8_t sel =
                (e.selector > 0 && e.selector <= 0xFF)
                    ? static_cast<std::uint8_t>(e.selector) : std::uint8_t{0};
            bool anyVariant = false;
            for (std::size_t i = 0; i < 14; ++i)
                if (e.variantSelectors[i] != 0) { anyVariant = true; break; }
            if (pt == 0 && sel == 0 && !anyVariant) continue;
            callback(kv.first, pt, sel, e.variantSelectors);
        }
    }

    bool ResolveOrCreateTapeSaveIndex(
        const char* key,
        std::int16_t minimumIndex,
        std::int16_t& outSaveIndex)
    {
        outSaveIndex = -1;
        if (!key || !key[0]) return false;

        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();

        auto it = g_State.tapes.find(key);
        if (it != g_State.tapes.end() && it->second.saveIndex > 0)
        {
            if (it->second.misses != 0)
            {
                it->second.misses = 0;
                g_State.dirty = true;
                SaveToDisk_NoLock();
            }
            outSaveIndex = it->second.saveIndex;
            return true;
        }

        const std::int16_t newIdx = AllocateNextFreeTapeSaveIndex_NoLock(minimumIndex);
        if (newIdx < 0)
        {
            Log("[CustomTapes] ERROR: custom-tape save-index pool [300-32000] is full - uninstall unused tape mods; no more custom tapes can be registered.\n");
            return false;
        }

        g_State.tapes[key].saveIndex = newIdx;
        g_TapeSaveIndexInUse.insert(newIdx);
        g_State.dirty = true;
        outSaveIndex = newIdx;
#ifdef _DEBUG
        Log("[CustomTapes] tape added: '%s' (saveIndex %d) - first time; saved to V_FrameWork_State.lua.\n", key, static_cast<int>(newIdx));
#endif

        SaveToDisk_NoLock();

        return true;
    }

    void SetTapeOwned(const char* key, bool owned)
    {
        if (!key || !key[0]) return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        auto it = g_State.tapes.find(key);
        if (it != g_State.tapes.end() && it->second.owned != owned)
        {
            it->second.owned = owned;
            g_State.dirty = true;
            SaveToDisk_NoLock();
        }
    }

    void SetTapeNew(const char* key, bool isNew)
    {
        if (!key || !key[0]) return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        auto it = g_State.tapes.find(key);
        if (it != g_State.tapes.end() && it->second.isNew != isNew)
        {
            it->second.isNew = isNew;
            g_State.dirty = true;
            SaveToDisk_NoLock();
        }
    }

    void SetTapeOwnedBySaveIndex(std::int16_t saveIndex, bool owned)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (auto& kv : g_State.tapes)
        {
            if (kv.second.saveIndex == saveIndex && kv.second.owned != owned)
            {
                kv.second.owned = owned;
                g_State.dirty = true;
                SaveToDisk_NoLock();
                return;
            }
        }
    }

    void SetTapeNewBySaveIndex(std::int16_t saveIndex, bool isNew)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (auto& kv : g_State.tapes)
        {
            if (kv.second.saveIndex == saveIndex && kv.second.isNew != isNew)
            {
                kv.second.isNew = isNew;
                g_State.dirty = true;
                SaveToDisk_NoLock();
                return;
            }
        }
    }

    bool GetTapeOwned(const char* key)
    {
        if (!key || !key[0]) return false;
        std::lock_guard<std::mutex> lock(g_Mutex);
        auto it = g_State.tapes.find(key);
        return (it != g_State.tapes.end()) ? it->second.owned : false;
    }

    bool GetTapeNew(const char* key)
    {
        if (!key || !key[0]) return false;
        std::lock_guard<std::mutex> lock(g_Mutex);
        auto it = g_State.tapes.find(key);
        return (it != g_State.tapes.end()) ? it->second.isNew : false;
    }

    void ForEachTape(
        const std::function<void(const std::string&, std::int16_t, bool, bool)>& callback)
    {
        if (!callback) return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        for (const auto& kv : g_State.tapes)
            callback(kv.first, kv.second.saveIndex, kv.second.owned, kv.second.isNew);
    }

    void Reset()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_State.loaded = false;
        g_State.dirty = false;
        g_State.equips.clear();
        g_State.tapes.clear();
        g_SessionEquipIds.clear();
        g_SessionFlowIndices.clear();
        g_SessionNextFlowIndex = kFirstCustomFlowIndex;
        g_PendingDevelopedResets.clear();
    }
}
