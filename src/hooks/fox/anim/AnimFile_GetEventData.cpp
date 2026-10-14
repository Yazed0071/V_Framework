#include "pch.h"

#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "AddressSet.h"
#include "AnimFile_GetEventData.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using GetEventData_t = const std::uint8_t*(__fastcall*)(const void* animFile);

    constexpr std::uint32_t kEventTreeTag = 0x0BFE2CF6;   // first word of every clip's event tree (real name unknown)
    constexpr std::uint32_t kNormalGroup = 0xC8F4FCB6;    // Normal
    constexpr std::uint8_t kOneTimeAsInt16 = 0x41;        // one time section, start and end stored as int16
    constexpr int kMaxFrame = 0x7FFF;
    constexpr int kMaxGroups = 32;
    constexpr std::size_t kMaxEventsPerClip = 64;
    constexpr std::size_t kMaxGroupBytes = 0x100000;
    constexpr std::size_t kOurEventBytes = 12;

    struct ClipEvent
    {
        int frame;
        ClipEventLabel label;
    };

    struct GroupBytes
    {
        const std::uint8_t* data;
        std::size_t size;
    };

    struct BuiltTree
    {
        const std::uint8_t* tree = nullptr;
        bool failed = false;
    };

    GetEventData_t g_Orig = nullptr;
    void* g_Target = nullptr;

    std::shared_mutex g_Lock;
    std::unordered_map<std::uint64_t, std::vector<ClipEvent>> g_Events;
    std::map<std::pair<std::uint64_t, const std::uint8_t*>, BuiltTree> g_Built;
    std::deque<std::vector<std::uint8_t>> g_Trees;
    std::unordered_map<const void*, std::vector<ClipEventLabel>> g_AddedGroups;
    std::atomic<bool> g_HaveEvents{false};

    template <typename T>
    T Read(const std::uint8_t* at)
    {
        T value;
        std::memcpy(&value, at, sizeof(T));
        return value;
    }

    template <typename T>
    void Write(std::uint8_t* at, T value)
    {
        std::memcpy(at, &value, sizeof(T));
    }

    std::size_t EventBytes(const std::uint8_t* event)
    {
        const std::uint8_t timeInfo = event[4];   // low 6 bits = how many time sections, top 2 = how each is stored
        const std::size_t sections = timeInfo & 0x3F;
        std::size_t paramsAt = 8;
        switch (timeInfo >> 6)
        {
        case 0: paramsAt = 8 + sections * 8; break;
        case 1: paramsAt = 8 + sections * 4; break;
        case 2: paramsAt = (8 + sections * 2 + 3) & ~std::size_t(3); break;
        default: break;
        }
        const int ints = std::max<int>(static_cast<std::int8_t>(event[5]), 0);
        const int floats = std::max<int>(static_cast<std::int8_t>(event[6]), 0);
        const int strings = std::max<int>(static_cast<std::int8_t>(event[7]), 0);
        return paramsAt + 4 * ints + 4 * floats + 8 * strings;
    }

    std::size_t GroupSize(const std::uint8_t* group)
    {
        const int events = Read<std::int16_t>(group + 4);
        if (events < 0)
            return 0;
        std::size_t end = 8 + 4 * static_cast<std::size_t>(events);
        for (int i = 0; i < events; ++i)
        {
            const std::int32_t at = Read<std::int32_t>(group + 8 + 4 * i);
            if (at <= 0)
                continue;
            const std::size_t eventEnd = static_cast<std::size_t>(at) + EventBytes(group + at);
            if (eventEnd > end)
                end = eventEnd;
        }
        return end;
    }

    int MeasureGroups(const std::uint8_t* tree, GroupBytes* groups)
    {
        __try
        {
            const int count = Read<std::int16_t>(tree + 4);
            if (count < 0 || count > kMaxGroups)
                return -1;
            for (int i = 0; i < count; ++i)
            {
                const std::int32_t at = Read<std::int32_t>(tree + 8 + 4 * i);
                if (at == 0)
                    return -1;
                groups[i].data = tree + at;
                groups[i].size = GroupSize(tree + at);
                if (groups[i].size == 0 || groups[i].size > kMaxGroupBytes)
                    return -1;
            }
            return count;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
    }

    bool CopyBytes(std::uint8_t* to, const std::uint8_t* from, std::size_t size)
    {
        __try
        {
            std::memcpy(to, from, size);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint64_t ClipOf(const void* animFile)
    {
        __try
        {
            return *static_cast<const std::uint64_t*>(animFile);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    std::size_t Align16(std::size_t n)
    {
        return (n + 15) & ~std::size_t(15);
    }

    const std::uint8_t* BuildTree(const std::uint8_t* original, const std::vector<ClipEvent>& events)
    {
        GroupBytes kept[kMaxGroups] = {};
        const int keptCount = original ? MeasureGroups(original, kept) : 0;
        if (keptCount < 0)
            return nullptr;

        const int groupCount = keptCount + 1;
        std::size_t size = Align16(8 + 4 * static_cast<std::size_t>(groupCount));
        std::size_t keptAt[kMaxGroups] = {};
        for (int i = 0; i < keptCount; ++i)
        {
            keptAt[i] = size;
            size = Align16(size + kept[i].size);
        }
        const std::size_t ourGroupAt = size;
        const std::size_t ourEventsAt = 8 + 4 * events.size();
        size += ourEventsAt + kOurEventBytes * events.size();

        std::vector<std::uint8_t> tree(size, 0);
        Write<std::uint32_t>(&tree[0], kEventTreeTag);
        Write<std::int16_t>(&tree[4], static_cast<std::int16_t>(groupCount));
        for (int i = 0; i < keptCount; ++i)
        {
            Write<std::int32_t>(&tree[8 + 4 * i], static_cast<std::int32_t>(keptAt[i]));
            if (!CopyBytes(&tree[keptAt[i]], kept[i].data, kept[i].size))
                return nullptr;
            Write<std::uint16_t>(&tree[keptAt[i] + 6], 0);   // offset to the file's event cache, which the copy can't reach
        }
        Write<std::int32_t>(&tree[8 + 4 * keptCount], static_cast<std::int32_t>(ourGroupAt));

        std::uint8_t* group = &tree[ourGroupAt];
        Write<std::uint32_t>(group, kNormalGroup);
        Write<std::int16_t>(group + 4, static_cast<std::int16_t>(events.size()));
        std::vector<ClipEventLabel> labels;
        for (std::size_t i = 0; i < events.size(); ++i)
        {
            const std::size_t eventAt = ourEventsAt + kOurEventBytes * i;
            Write<std::int32_t>(group + 8 + 4 * i, static_cast<std::int32_t>(eventAt));
            std::uint8_t* event = group + eventAt;
            Write<std::uint32_t>(event, events[i].label.eventHash);
            event[4] = kOneTimeAsInt16;
            Write<std::int16_t>(event + 8, static_cast<std::int16_t>(events[i].frame));
            Write<std::int16_t>(event + 10, -1);   // no end frame, so it fires once
            labels.push_back(events[i].label);
        }

        g_Trees.push_back(std::move(tree));
        std::uint8_t* built = g_Trees.back().data();
        g_AddedGroups[built + ourGroupAt] = std::move(labels);
        return built;
    }

    bool HasEvents(std::uint64_t clip)
    {
        std::shared_lock<std::shared_mutex> read(g_Lock);
        return g_Events.find(clip) != g_Events.end();
    }

    const std::uint8_t* TreeFor(std::uint64_t clip, const std::uint8_t* original)
    {
        {
            std::shared_lock<std::shared_mutex> read(g_Lock);
            const auto built = g_Built.find({clip, original});
            if (built != g_Built.end())
                return built->second.tree ? built->second.tree : original;
        }

        std::unique_lock<std::shared_mutex> write(g_Lock);
        BuiltTree& built = g_Built[{clip, original}];
        if (!built.tree && !built.failed)
        {
            const auto events = g_Events.find(clip);
            if (events != g_Events.end())
                built.tree = BuildTree(original, events->second);
            if (!built.tree)
            {
                built.failed = true;
                LogWarn("ClipEvents: clip %016llX: its own events could not be read, added events skipped",
                    static_cast<unsigned long long>(clip));
            }
        }
        return built.tree ? built.tree : original;
    }

    const std::uint8_t* __fastcall hk_GetEventData(const void* animFile)
    {
        const std::uint8_t* original = g_Orig(animFile);
        if (!g_HaveEvents.load(std::memory_order_acquire))
            return original;

        const std::uint64_t clip = ClipOf(animFile);
        if (!clip || !HasEvents(clip) || MissionCodeGuard::ShouldBypassHooks())
            return original;
        return TreeFor(clip, original);
    }
}

bool AddClipEvent(std::uint64_t clip, int frame, const ClipEventLabel& label)
{
    if (!clip || !label.eventHash || frame < 0 || frame > kMaxFrame)
        return false;

    std::unique_lock<std::shared_mutex> write(g_Lock);
    std::vector<ClipEvent>& events = g_Events[clip];
    const bool alreadyAdded = std::any_of(events.begin(), events.end(),
        [&](const ClipEvent& e) { return e.frame == frame && e.label.eventHash == label.eventHash; });
    if (alreadyAdded)
        return true;
    if (events.size() >= kMaxEventsPerClip)
        return false;

    events.push_back({frame, label});
    for (auto it = g_Built.begin(); it != g_Built.end();)
        it = (it->first.first == clip) ? g_Built.erase(it) : std::next(it);
    g_HaveEvents.store(true, std::memory_order_release);
    return true;
}

bool FindAddedClipEvent(const void* group, int index, ClipEventLabel& label)
{
    if (!g_HaveEvents.load(std::memory_order_acquire) || !group || index < 0)
        return false;

    std::shared_lock<std::shared_mutex> read(g_Lock);
    const auto found = g_AddedGroups.find(group);
    if (found == g_AddedGroups.end() || static_cast<std::size_t>(index) >= found->second.size())
        return false;
    label = found->second[index];
    return true;
}

bool Install_AnimFile_GetEventData_Hook()
{
    if (g_Target)
        return true;
    if (!gAddr.AnimFile_GetEventData)
    {
        LogError("ClipEvents: AnimFile::GetEventData address missing for this build, V_TppMotion.AddClipEvent off");
        return true;
    }
    void* target = ResolveGameAddress(gAddr.AnimFile_GetEventData);
    if (!target || !CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_GetEventData), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("ClipEvents: GetEventData hook failed, V_TppMotion.AddClipEvent off");
        return true;
    }
    g_Target = target;
    return true;
}

bool Uninstall_AnimFile_GetEventData_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig = nullptr;
    return true;
}
