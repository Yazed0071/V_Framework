#include "pch.h"
#include "V_FrameWorkState.h"
#include "log.h"
#include "AddressSet.h"
#include "../hooks/equip/EquipIdCompression.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace V_FrameWorkState
{
    namespace
    {
        static constexpr const char* kSavePath    = "mod\\V_FrameWork\\V_FrameWork_State.lua";
        static constexpr const char* kLegacyPath  = "mod\\saves\\V_FrameWork_State.lua";

        // Old default 0x609 was the FIRST OOB equipId for the native
        // AddToEquipIdTable (compressed = 0x609 - 0x380 = 0x289 = bound).
        // The new allocator uses EquipIdCompression::FindLowestFreeEquipId
        // which scans compressed slots [floor, 0x289) and avoids slots
        // vanilla MGSV has already populated (verified via SyncFromNativeTable
        // reading the native _s_internalInfoList_ array directly).
        //
        // Floor is 1 (not 0) because EquipIdTable_AddToEquipIdTable.cpp's
        // ReadEquipIdRow treats equipId=0 as "invalid row, drop it" — if
        // we ever picked 0 the row would silently never reach the native
        // table and the weapon would be unusable. Skipping 0 also matches
        // vanilla convention (equipId 0 is reserved/sentinel in TppEquip).
        static constexpr std::int32_t kFirstCustomEquipIdMinimum = 1;
        static constexpr std::int32_t kFirstCustomDevelopId = 0x1000;
        static constexpr std::int32_t kFirstCustomFlowIndex = 922;
        static constexpr std::int16_t kFirstCustomTapeSaveIndex = 300;
        static constexpr std::int16_t kMaxCustomTapeSaveIndex = 1999;

        struct EquipEntry
        {
            // developId and flowIndex are persisted together per key.
            // Equip ids (EQP_* namespace) are session-scoped and live in
            // g_SessionEquipIds below — they never land in the state file.
            std::int32_t developId = 0;
            std::int32_t flowIndex = 0;
        };

        struct TapeEntry
        {
            std::int16_t saveIndex = -1;
            bool owned = false;
            bool isNew = false;
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

        // Session-only cache of equipIds allocated via ResolveOrCreateEquipId.
        // Never persisted: equipIds are reallocated fresh each game session,
        // then memoized here so repeated calls with the same key in one
        // session return the same id.
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

        // One-shot migration: if an older state file still lives under
        // mod\saves but the new mod\V_FrameWork path is missing, move it so
        // previously-assigned equip/develop/tape ids survive the relocation.
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

            if (MoveFileA(kLegacyPath, kSavePath))
                Log("[V_FrameWorkState] Migrated legacy state '%s' -> '%s'\n",
                    kLegacyPath, kSavePath);
            else
                Log("[V_FrameWorkState] Migration failed (err=%lu): '%s' -> '%s'\n",
                    GetLastError(), kLegacyPath, kSavePath);
        }

        // Parses a line like: ["key"] = { developId = 456 },
        // Legacy `equipId = N` fields are ignored — equipIds are now
        // session-scoped and do not participate in the persisted state.
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

        // Parses a line like: ["key"] = { saveIndex = 300 },
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
            return !outKey.empty() && out.saveIndex > 0;
        }

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
                Log("[V_FrameWorkState] No state file at '%s'\n", kSavePath);
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

                // Section closing brace (but not the outer table close)
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
                        g_State.tapes[key] = entry;
                }
            }

            Log("[V_FrameWorkState] Loaded %zu equips, %zu tapes from '%s'\n",
                g_State.equips.size(), g_State.tapes.size(), kSavePath);
        }

        static void SaveToDisk_NoLock()
        {
            EnsureSaveDirectory();

            std::ofstream out(kSavePath, std::ios::binary | std::ios::trunc);
            if (!out)
            {
                Log("[V_FrameWorkState] Save failed: could not open '%s'\n", kSavePath);
                return;
            }

            out << "return {\n";

            // Equips section
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
                    // Persist any entry that has a developId OR a flowIndex.
                    // equipIds are session-scoped and re-allocated each run.
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

            // Tapes section
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
                        << ", new = " << (kv.second.isNew ? "true" : "false")
                        << " },\n";
                }
                out << "    },\n";
            }

            out << "}\n";
            g_State.dirty = false;

            Log("[V_FrameWorkState] Saved %zu equips, %zu tapes to '%s'\n",
                g_State.equips.size(), g_State.tapes.size(), kSavePath);
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

        // Set on first allocation request — triggers a one-time scan of
        // the native EquipIdTable to populate the vanilla-occupancy
        // bitset. Vanilla MGSV's TppEquipParts.lua runs BEFORE our DLL
        // injects, so the live observer hook on AddToEquipIdTable misses
        // vanilla's calls; reading the populated table directly is the
        // only reliable way to know which slots are off-limits.
        static bool g_NativeTableSynced = false;

        static std::int32_t AllocateNextFreeEquipId_NoLock(std::int32_t minimum)
        {
            // First-call sync: scan the native _s_internalInfoList_ array
            // for non-zero parts-path hashes and mark those compressed
            // slots as vanilla-occupied. This MUST happen before the
            // bitset scan below, otherwise we'd allocate slots vanilla
            // already filled and overwrite vanilla equipment data on the
            // next AddToEquipIdTable call.
            if (!g_NativeTableSynced)
            {
                EquipIdCompression::SyncFromNativeTable();
                g_NativeTableSynced = true;
            }

            // Scan compressed slots [floor, 0x289) for one that:
            //   (a) isn't marked vanilla-occupied (from native table sync
            //       AND any live AddToEquipIdTable hook fires), AND
            //   (b) isn't already used by another session-allocated equip.
            //
            // Returns the slot index as the equipId — for slots < 0x400
            // the compressed index IS the equipId (1:1 mapping). We don't
            // try to address a free slot through the 0x400+/0x600+ ranges
            // because those would only be reachable if the slot were
            // *also* unused at compressed = equipId - 0x1D0 / 0x380, and
            // direct addressing keeps the allocation behavior simple.
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
                    "in-bounds compressed slot remains (vanilla + session "
                    "have filled all 0x289 slots above floor=0x%X). Returning "
                    "-1 to fail the allocation cleanly.\n", floor);
            }
            return result;
        }

        static std::int32_t AllocateNextFreeDevelopId_NoLock(std::int32_t minimum)
        {
            std::int32_t id = (minimum > kFirstCustomDevelopId) ? minimum : kFirstCustomDevelopId;
            while (IsDevelopIdInUse_NoLock(id)) ++id;
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
            while (IsFlowIndexInUse_NoLock(idx)) ++idx;
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

        // Session-only: repeated calls with the same key hand back the
        // same id within one game session. Does NOT touch the state file.
        auto it = g_SessionEquipIds.find(key);
        if (it != g_SessionEquipIds.end() && it->second != 0)
        {
            outEquipId = it->second;
            return true;
        }

        const std::int32_t newId = AllocateNextFreeEquipId_NoLock(minimumId);
        if (newId < 0)
        {
            // Allocation failed (every in-bounds compressed slot is taken
            // by vanilla + session). Don't cache the failure — the
            // observer might still be receiving vanilla rows, or the
            // session might free a slot. Caller falls back / errors out.
            outEquipId = 0;
            Log("[V_FrameWorkState] EquipId allocation FAILED for '%s' "
                "(every in-bounds slot occupied)\n", key);
            return false;
        }

        g_SessionEquipIds[key] = newId;
        outEquipId = newId;

        Log("[V_FrameWorkState] Assigned equipId=%d (compressed=0x%X) for '%s' (session-only)\n",
            newId, EquipIdCompression::ComputeCompressed(newId), key);
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

        Log("[V_FrameWorkState] Assigned developId=%d for '%s'\n", newId, key);
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

        Log("[V_FrameWorkState] Assigned flowIndex=%d for '%s'\n", newIdx, key);
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
            outSaveIndex = it->second.saveIndex;
            return true;
        }

        const std::int16_t newIdx = AllocateNextFreeTapeSaveIndex_NoLock(minimumIndex);
        if (newIdx < 0) return false;

        g_State.tapes[key].saveIndex = newIdx;
        g_State.dirty = true;
        outSaveIndex = newIdx;

        SaveToDisk_NoLock();

        Log("[V_FrameWorkState] Assigned saveIndex=%d for '%s'\n",
            static_cast<int>(newIdx), key);
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

    void Reset()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_State.loaded = false;
        g_State.dirty = false;
        g_State.equips.clear();
        g_State.tapes.clear();
    }
}
