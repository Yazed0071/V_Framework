#include <pch.h>
#include "LoadWatch.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "../hooks/fox/path/PathHashCode.h"

namespace
{
    constexpr std::uintptr_t kGroupNodeSize     = 0x18;
    constexpr std::uintptr_t kGroupNodeNext     = 0x0C;
    constexpr std::uintptr_t kGroupBlockCount   = 0x1C;
    constexpr std::uintptr_t kGroupBlocks       = 0x48;
    constexpr std::uintptr_t kBlockSize         = 0x178;
    constexpr std::uintptr_t kBlockState        = 0x80;
    constexpr std::uintptr_t kBlockPackages     = 0x90;
    constexpr std::uintptr_t kBlockPackageCount = 0x150;
    constexpr std::uintptr_t kPackagePath       = 0x08;
    constexpr std::uintptr_t kPackageStage      = 0x20;
    constexpr std::uint32_t  kEndOfGroups       = 0xFFFFFFFFu;
    constexpr int            kBlockLoading      = 1;   // 2 loaded, 3 active
    constexpr int            kStageQueued       = 0;
    constexpr int            kStageReading      = 1;   // its file was asked for
    constexpr int            kStageFailed       = 5;   // stage 4 went wrong; the game sets it back to 0 from here
    constexpr int            kStageUnloading    = 6;
    constexpr int            kStageLoaded       = 7;
    constexpr DWORD          kPollMs            = 1000;
    constexpr DWORD          kStuckMs           = 30000;
    constexpr std::uint32_t  kMaxGroups         = 4096;
    constexpr std::uint32_t  kMaxBlocks         = 256;
    constexpr std::uint32_t  kMaxPackages       = 256;

    struct WatchedPackage
    {
        std::uint64_t path   = 0;
        int           stage  = 0;
        DWORD         since  = 0;
        bool          warned = false;
        bool          seen   = false;
    };

    std::unordered_map<std::uintptr_t, WatchedPackage> g_Watched;   // only the watch thread touches it
    std::atomic<bool> g_Started{ false };
    std::atomic<bool> g_Paused{ true };

    template <typename T>
    bool Read(std::uintptr_t at, T& out)
    {
        return at && ReadEntryBytesSEH(reinterpret_cast<const void*>(at), &out, sizeof(T));
    }

    std::uintptr_t GameAddress(std::uintptr_t address)
    {
        return reinterpret_cast<std::uintptr_t>(ResolveGameAddress(address));
    }

    std::string PackName(std::uint64_t path)
    {
        std::string name = PackPathFor(path);
        if (!name.empty())
            return name;
        char code[48] = {};
        sprintf_s(code, "with path code 0x%016llX", static_cast<unsigned long long>(path));
        return code;
    }

    const char* StageText(int stage)
    {
        if (stage == kStageReading)
            return "still waiting for its file";
        if (stage == kStageFailed)
            return "it failed and is being reset";
        if (stage == kStageQueued)
            return "it failed and went back to the queue";
        return "still building its contents";
    }

    void WatchPackage(std::uintptr_t package, DWORD now)
    {
        std::uint64_t path  = 0;
        int           stage = 0;
        if (!Read(package + kPackagePath, path) || !Read(package + kPackageStage, stage))
            return;
        if (stage == kStageLoaded || stage == kStageUnloading)
            return;

        const auto found = g_Watched.find(package);
        if (found == g_Watched.end() || found->second.path != path)
        {
            if (stage != kStageQueued)
                g_Watched[package] = WatchedPackage{ path, stage, now, false, true };
            return;
        }
        WatchedPackage& watched = found->second;
        watched.seen  = true;
        watched.stage = stage;
        if (!watched.warned && now - watched.since >= kStuckMs)
        {
            watched.warned = true;
            LogWarn("LoadWatch: pack %s has been loading for %lu s without finishing, %s. The game cannot find it or a file it needs: "
                    "check the path and that the mod adding it is installed",
                    PackName(path).c_str(), static_cast<unsigned long>((now - watched.since) / 1000), StageText(stage));
        }
    }

    void WatchBlock(std::uintptr_t block, DWORD now)
    {
        int            state    = 0;
        std::uintptr_t packages = 0;
        std::uint32_t  count    = 0;
        if (!Read(block + kBlockState, state) || state != kBlockLoading)
            return;
        if (!Read(block + kBlockPackages, packages) || !Read(block + kBlockPackageCount, count) || !packages)
            return;
        for (std::uint32_t i = 0; i < count && i < kMaxPackages; ++i)
        {
            std::uintptr_t package = 0;
            if (Read(packages + i * sizeof(void*), package) && package)
                WatchPackage(package, now);
        }
    }

    void WatchGroups(DWORD now)
    {
        std::uint32_t  index  = kEndOfGroups;
        std::uintptr_t nodes  = 0;
        std::uintptr_t values = 0;
        if (!Read(GameAddress(gAddr.Uix_BlockGroupMapHead), index) || !Read(GameAddress(gAddr.Uix_BlockGroupMapNodes), nodes)
            || !Read(GameAddress(gAddr.Uix_BlockGroupMapValues), values) || !nodes || !values)
            return;
        for (std::uint32_t guard = 0; index != kEndOfGroups && guard < kMaxGroups; ++guard)
        {
            std::uintptr_t group  = 0;
            std::uint32_t  blocks = 0;
            std::uintptr_t first  = 0;
            if (Read(values + index * sizeof(void*), group) && group && Read(group + kGroupBlockCount, blocks)
                && Read(group + kGroupBlocks, first) && first)
            {
                for (std::uint32_t b = 0; b < blocks && b < kMaxBlocks; ++b)
                    WatchBlock(first + b * kBlockSize, now);
            }
            if (!Read(nodes + index * kGroupNodeSize + kGroupNodeNext, index))
                break;
        }
    }

    void ForgetFinished()
    {
        for (auto it = g_Watched.begin(); it != g_Watched.end();)
        {
            if (it->second.seen)
            {
                it->second.seen = false;
                ++it;
                continue;
            }
            if (it->second.warned)
                LogInfo("LoadWatch: pack %s finished or was dropped after all", PackName(it->second.path).c_str());
            it = g_Watched.erase(it);
        }
    }

    DWORD WINAPI WatchThread(LPVOID)
    {
        for (;;)
        {
            Sleep(kPollMs);
            if (g_Paused.load(std::memory_order_relaxed))
                continue;
            try
            {
                const DWORD now = GetTickCount();
                WatchGroups(now);
                ForgetFinished();
            }
            catch (...)
            {
            }
        }
    }
}

void LoadWatch::Start()
{
    g_Paused.store(false, std::memory_order_relaxed);
    if (g_Started.exchange(true))
        return;
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                            reinterpret_cast<LPCWSTR>(&WatchThread), &pinned))
        LogWarn("LoadWatch: cannot pin the module for the watch thread (err %lu)", GetLastError());
    HANDLE thread = CreateThread(nullptr, 0, &WatchThread, nullptr, 0, nullptr);
    if (thread)
    {
        CloseHandle(thread);
        return;
    }
    g_Started.store(false);
    LogWarn("LoadWatch: watch thread failed to start, stuck packs are not reported");
}

void LoadWatch::Stop()
{
    g_Paused.store(true, std::memory_order_relaxed);
}
