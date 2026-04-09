#define NOMINMAX

#include "pch.h"
#include "CustomEquipIdState.h"
#include "log.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>

namespace CustomEquipIdState
{
    namespace
    {
        constexpr std::int32_t kFirstCustomEquipId = 0x609;
        constexpr const char* kPersistPath = "mod\\saves\\V_FrameWork_CustomEquipIds.lua";

        struct State
        {
            bool loaded = false;
            bool dirty = false;
            std::unordered_map<std::string, std::int32_t> nameToId;
        };

        State g_State;

        bool IsSafeCustomEquipId(const std::int32_t id)
        {
            return id >= kFirstCustomEquipId;
        }

        static std::string Trim(const std::string& s)
        {
            const auto begin = s.find_first_not_of(" \t\r\n");
            if (begin == std::string::npos)
                return {};

            const auto end = s.find_last_not_of(" \t\r\n");
            return s.substr(begin, end - begin + 1);
        }

        static bool ParseLuaLine(const std::string& line, std::string& outName, std::int32_t& outId)
        {
            // expected shape:
            // ["EQP_WP_AK12_MAIN_00"] = 1545,
            const auto lb = line.find("[\"");
            if (lb == std::string::npos)
                return false;

            const auto rb = line.find("\"]", lb + 2);
            if (rb == std::string::npos)
                return false;

            const auto eq = line.find('=', rb + 2);
            if (eq == std::string::npos)
                return false;

            outName = line.substr(lb + 2, rb - (lb + 2));

            std::string rhs = Trim(line.substr(eq + 1));
            if (!rhs.empty() && rhs.back() == ',')
                rhs.pop_back();
            rhs = Trim(rhs);

            try
            {
                outId = static_cast<std::int32_t>(std::stol(rhs, nullptr, 0));
            }
            catch (...)
            {
                return false;
            }

            return !outName.empty();
        }

        void SaveState()
        {
            if (!g_State.dirty)
                return;

            std::ofstream out(kPersistPath, std::ios::binary | std::ios::trunc);
            if (!out)
            {
                Log("[CustomEquipIdState] Save failed: could not open '%s'\n", kPersistPath);
                return;
            }

            out << "return {\n";

            std::vector<std::pair<std::string, std::int32_t>> entries;
            entries.reserve(g_State.nameToId.size());

            for (const auto& kv : g_State.nameToId)
            {
                if (!IsSafeCustomEquipId(kv.second))
                {
                    Log("[CustomEquipIdState] Skipping unsafe persisted mapping '%s' => 0x%X during save\n",
                        kv.first.c_str(),
                        kv.second);
                    continue;
                }

                entries.emplace_back(kv.first, kv.second);
            }

            std::sort(entries.begin(), entries.end(),
                [](const auto& a, const auto& b)
                {
                    return a.first < b.first;
                });

            for (const auto& kv : entries)
            {
                out << "    [\"" << kv.first << "\"] = " << kv.second << ",\n";
            }

            out << "}\n";

            g_State.dirty = false;

            Log("[CustomEquipIdState] Saved %zu custom equip ids to '%s'\n",
                entries.size(),
                kPersistPath);
        }

        void LoadStateIfNeeded()
        {
            if (g_State.loaded)
                return;

            g_State.loaded = true;
            g_State.nameToId.clear();
            g_State.dirty = false;

            std::ifstream in(kPersistPath, std::ios::binary);
            if (!in)
            {
                Log("[CustomEquipIdState] No existing state file at '%s'\n", kPersistPath);
                return;
            }

            std::string line;
            while (std::getline(in, line))
            {
                std::string name;
                std::int32_t id = 0;
                if (!ParseLuaLine(line, name, id))
                    continue;

                if (!IsSafeCustomEquipId(id))
                {
                    Log("[CustomEquipIdState] Ignoring unsafe persisted mapping '%s' => 0x%X (below 0x%X)\n",
                        name.c_str(),
                        id,
                        kFirstCustomEquipId);
                    continue;
                }

                auto [it, inserted] = g_State.nameToId.emplace(name, id);
                if (!inserted)
                {
                    Log("[CustomEquipIdState] Duplicate persisted name '%s', keeping first value 0x%X\n",
                        name.c_str(),
                        it->second);
                }
            }

            Log("[CustomEquipIdState] Loaded %zu safe custom equip ids from '%s'\n",
                g_State.nameToId.size(),
                kPersistPath);
        }

        std::int32_t ComputeNextFreeId(const std::int32_t minimumEquipId)
        {
            std::int32_t nextId = std::max(minimumEquipId, kFirstCustomEquipId);

            for (;;)
            {
                bool inUse = false;

                for (const auto& kv : g_State.nameToId)
                {
                    if (kv.second == nextId)
                    {
                        inUse = true;
                        ++nextId;
                        break;
                    }
                }

                if (!inUse)
                    return nextId;
            }
        }
    }

    bool ResolveOrCreatePersistentEquipId(
        const char* eqpName,
        const std::int32_t minimumEquipId,
        std::int32_t& outEquipId)
    {
        outEquipId = 0;

        if (!eqpName || !eqpName[0])
        {
            Log("[CustomEquipIdState] Resolve failed: empty equip name\n");
            return false;
        }

        LoadStateIfNeeded();

        const auto it = g_State.nameToId.find(eqpName);
        if (it != g_State.nameToId.end())
        {
            if (!IsSafeCustomEquipId(it->second))
            {
                Log("[CustomEquipIdState] Resolve failed: persisted mapping '%s' => 0x%X is unsafe\n",
                    eqpName,
                    it->second);
                return false;
            }

            outEquipId = it->second;
            return true;
        }

        const std::int32_t nextId = ComputeNextFreeId(minimumEquipId);
        if (!IsSafeCustomEquipId(nextId))
        {
            Log("[CustomEquipIdState] Resolve failed: allocator produced unsafe id 0x%X for '%s'\n",
                nextId,
                eqpName);
            return false;
        }

        g_State.nameToId[eqpName] = nextId;
        g_State.dirty = true;
        outEquipId = nextId;

        SaveState();

        Log("[CustomEquipIdState] Created persistent mapping '%s' => 0x%X\n",
            eqpName,
            outEquipId);

        return true;
    }

    void ResetForNewSession()
    {
        g_State.loaded = false;
        g_State.dirty = false;
        g_State.nameToId.clear();
    }
}