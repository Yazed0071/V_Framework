#include "pch.h"
#include "V_FrameWorkState.h"
#include "log.h"
#include "AddressSet.h"

#include <algorithm>
#include <cstdint>
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
        static constexpr std::int16_t kMaxCustomTapeSaveIndex = 1999;
        static constexpr std::int32_t kTapeOrphanGraceLaunches = 2;

        struct EquipEntry
        {


            std::int32_t developId = 0;
            std::int32_t flowIndex = 0;
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
        };

        static State g_State;
        static std::mutex g_Mutex;


        static std::unordered_map<std::string, std::int32_t> g_SessionEquipIds;

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

        static void SaveToDisk_NoLock();

        static void LoadFromDisk_NoLock()
        {
            if (g_State.loaded) return;
            g_State.loaded = true;
            g_State.equips.clear();
            g_State.tapes.clear();

            MigrateLegacyStateFile_NoLock();

            std::ifstream in(kSavePath);
            if (!in)
            {
                return;
            }

            enum Section { None, Equips, Tapes } section = None;

            std::string line;
            while (std::getline(in, line))
            {
                const std::string trimmed = Trim(line);

                if (trimmed.find("equips") != std::string::npos &&
                    trimmed.find('{') != std::string::npos)
                {
                    section = Equips;
                    continue;
                }

                if (trimmed.find("tapes") != std::string::npos &&
                    trimmed.find('{') != std::string::npos)
                {
                    section = Tapes;
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
                        Log("[CustomTapes] tape loaded: '%s' (saveIndex %d)\n", key.c_str(), static_cast<int>(entry.saveIndex));
                    }
                }
            }

            in.close();

            bool gcChanged = false;
            for (auto it = g_State.tapes.begin(); it != g_State.tapes.end(); )
            {
                if (it->second.misses >= kTapeOrphanGraceLaunches)
                {
                    Log("[CustomTapes] tape deleted: '%s' (saveIndex %d) — mod uninstalled; freeing the save slot.\n", it->first.c_str(), static_cast<int>(it->second.saveIndex));
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
            if (gcChanged)
            {
                g_State.dirty = true;
                SaveToDisk_NoLock();
            }
        }

        static void SaveToDisk_NoLock()
        {
            EnsureSaveDirectory();

            std::ofstream out(kSavePath, std::ios::binary | std::ios::trunc);
            if (!out)
            {
                Log("[V_FrameWorkState] ERROR: could not write '%s' — custom-tape ownership/save-index state will not persist.\n", kSavePath);
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


                    if (kv.second.developId == 0 && kv.second.flowIndex == 0)
                        continue;
                    out << "        [\"" << kv.first << "\"] = {";
                    if (kv.second.developId != 0)
                        out << " developId = " << kv.second.developId << ",";
                    if (kv.second.flowIndex != 0)
                        out << " flowIndex = " << kv.second.flowIndex << ",";
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
                    [](const auto& a, const auto& b) { return a.first < b.first; });

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
            for (const auto& kv : g_State.tapes)
                if (kv.second.saveIndex == idx) return true;
            return false;
        }


        static bool g_NativeTableSynced = false;

        static std::int32_t AllocateNextFreeEquipId_NoLock(std::int32_t minimum)
        {


            (void)minimum;
            (void)g_NativeTableSynced;
            return -1;
        }


        static std::int32_t AllocateNextFreeDevelopId_NoLock(std::int32_t minimum)
        {
            std::int32_t id = (minimum > kFirstCustomDevelopId) ? minimum : kFirstCustomDevelopId;
            while (IsDevelopIdInUse_NoLock(id))
                ++id;
            return id;
        }

        static bool IsFlowIndexInUse_NoLock(std::int32_t idx)
        {
            for (const auto& kv : g_State.equips)
                if (kv.second.flowIndex == idx) return true;
            return false;
        }

        static std::int32_t AllocateNextFreeFlowIndex_NoLock(std::int32_t minimum)
        {
            std::int32_t idx = (minimum > kFirstCustomFlowIndex) ? minimum : kFirstCustomFlowIndex;
            while (IsFlowIndexInUse_NoLock(idx))
                ++idx;
            return idx;
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
        std::int32_t& outDevelopId)
    {
        outDevelopId = 0;
        if (!key || !key[0]) return false;

        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();

        auto it = g_State.equips.find(key);
        if (it != g_State.equips.end() && it->second.developId != 0)
        {
            outDevelopId = it->second.developId;
            return true;
        }

        const std::int32_t newId = AllocateNextFreeDevelopId_NoLock(minimumId);
        g_State.equips[key].developId = newId;
        g_State.dirty = true;
        outDevelopId = newId;

        SaveToDisk_NoLock();

        return true;
    }

    bool ResolveOrCreateFlowIndex(
        const char* key,
        std::int32_t minimumIndex,
        std::int32_t& outFlowIndex)
    {
        outFlowIndex = 0;
        if (!key || !key[0]) return false;

        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();

        auto it = g_State.equips.find(key);
        if (it != g_State.equips.end() && it->second.flowIndex != 0)
        {
            outFlowIndex = it->second.flowIndex;
            return true;
        }

        const std::int32_t newIdx = AllocateNextFreeFlowIndex_NoLock(minimumIndex);
        g_State.equips[key].flowIndex = newIdx;
        g_State.dirty = true;
        outFlowIndex = newIdx;

        SaveToDisk_NoLock();

        return true;
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
            Log("[CustomTapes] ERROR: custom-tape save-index pool [300-1999] is full — uninstall unused tape mods; no more custom tapes can be registered.\n");
            return false;
        }

        g_State.tapes[key].saveIndex = newIdx;
        g_State.dirty = true;
        outSaveIndex = newIdx;
        Log("[CustomTapes] tape added: '%s' (saveIndex %d) — first time; saved to V_FrameWork_State.lua.\n", key, static_cast<int>(newIdx));

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
    }
}
