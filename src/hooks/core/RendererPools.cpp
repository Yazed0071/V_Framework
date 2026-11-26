#include <pch.h>
#include "RendererPools.h"

#include <Windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

// The renderer setup builds each graphics resource manager once, with a fixed slot count. A full manager makes a
// model fail to load (its FMDL ends in error -1), and a decal projected onto that model then waits for it forever,
// so the decal's block never finishes loading. Survive's engine builds the same managers with the sizes below.
namespace
{
    // test rax,rax / jz +0F / mov edx,<slots> / mov rcx,rax / call <manager ctor> / jmp +3 / mov rax,rbx /
    // mov [<manager instance>],rax  (the same shape as VertexStreamPool's site)
    constexpr std::size_t kSiteLen      = 30;
    constexpr std::size_t kSlotsOff     = 6;        // imm32 of the mov edx
    constexpr std::size_t kStoreDispOff = 26;
    constexpr std::size_t kCapacityOff  = 0x10;     // slot count in the built manager
    constexpr std::size_t kUsedOff      = 0x14;     // slots in use; creation fails once it reaches the capacity
    const std::uint8_t kSiteCode[kSiteLen] = {
        0x48, 0x85, 0xC0, 0x74, 0x0F, 0xBA, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xC8, 0xE8, 0x00,
        0x00, 0x00, 0x00, 0xEB, 0x03, 0x48, 0x8B, 0xC3, 0x48, 0x89, 0x05, 0x00, 0x00, 0x00, 0x00 };
    // 0: the slot count, the call target and the store displacement (they differ per manager and build)
    const std::uint8_t kSiteMask[kSiteLen] = {
        1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0,
        0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0 };

    using AddressField = uintptr_t AddressSetRuntime::AddressSet::*;

    struct PoolSpec
    {
        const char*  name;
        std::int32_t vanillaSlots;
        std::int32_t newSlots;
        AddressField createSite;
        AddressField instance;
    };

    constexpr std::size_t kPoolCount = 3;
    const PoolSpec kPools[kPoolCount] = {
        { "ModelManager",        1826, 4096,
          &AddressSetRuntime::AddressSet::ModelManagerPool_CreateSite,
          &AddressSetRuntime::AddressSet::ModelManagerPool_Instance },
        { "VertexBufferManager", 2720, 8192,    // SSD Afghanistan failed 19 models at 2161/2720 after a long warp
          &AddressSetRuntime::AddressSet::VertexBufferPool_CreateSite,
          &AddressSetRuntime::AddressSet::VertexBufferPool_Instance },
        { "IndexBufferManager",  2544, 4096,
          &AddressSetRuntime::AddressSet::IndexBufferPool_CreateSite,
          &AddressSetRuntime::AddressSet::IndexBufferPool_Instance },
    };

    enum class PrePatchState : int
    {
        NotAttempted,
        AddressMissing,
        SiteUnexpected,
        SlotsUnexpected,
        PoolAlreadyBuilt,
        WriteFailed,
        Applied,
        AlreadyApplied,     // another module got there first
    };

    struct PoolState
    {
        std::atomic<PrePatchState> prePatch{ PrePatchState::NotAttempted };
        std::uint8_t**             instance   = nullptr;   // set once the site's store is verified
        std::int32_t               foundSlots = 0;
        DWORD                      writeError = 0;
        char                       foundCode[kSiteLen * 3] = {};
    };

    PoolState             g_Pools[kPoolCount];
    std::atomic<bool>     g_StopCheck{ false };
    std::atomic<bool>     g_CheckThreadStarted{ false };
    std::atomic<unsigned> g_CheckGeneration{ 0 };   // each Install restarts the check's logging

    bool SiteMatches(const std::uint8_t* code)
    {
        for (std::size_t i = 0; i < kSiteLen; ++i)
            if (kSiteMask[i] && code[i] != kSiteCode[i])
                return false;
        return true;
    }

    void NoteFoundCode(PoolState& state, const std::uint8_t* code)
    {
        for (std::size_t i = 0; i < kSiteLen; ++i)
            std::snprintf(state.foundCode + i * 3, sizeof(state.foundCode) - i * 3, i + 1 < kSiteLen ? "%02X " : "%02X", code[i]);
    }

    bool WriteBytes(PoolState& state, void* target, const void* src, std::size_t size)
    {
        DWORD oldProtect = 0;
        if (!VirtualProtect(target, size, PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            state.writeError = GetLastError();
            return false;
        }
        std::memcpy(target, src, size);
        DWORD restored = 0;
        VirtualProtect(target, size, oldProtect, &restored);
        FlushInstructionCache(GetCurrentProcess(), target, size);
        return true;
    }

    void PreApplyOne(const PoolSpec& spec, PoolState& state)
    {
        PrePatchState expected = PrePatchState::NotAttempted;
        if (!state.prePatch.compare_exchange_strong(expected, PrePatchState::AddressMissing))
            return;

        auto* site     = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.*spec.createSite));
        auto* instance = static_cast<std::uint8_t**>(ResolveGameAddress(gAddr.*spec.instance));
        if (!site || !instance)
            return;

        std::uint8_t code[kSiteLen] = {};
        if (!ReadEntryBytesSEH(site, code, kSiteLen))
        {
            state.prePatch = PrePatchState::SiteUnexpected;
            return;
        }
        NoteFoundCode(state, code);
        std::int32_t disp = 0;
        std::memcpy(&disp, code + kStoreDispOff, sizeof(disp));
        if (!SiteMatches(code) || reinterpret_cast<std::uint8_t**>(site + kSiteLen + disp) != instance)
        {
            state.prePatch = PrePatchState::SiteUnexpected;
            return;
        }
        state.instance = instance;

        std::memcpy(&state.foundSlots, code + kSlotsOff, sizeof(state.foundSlots));
        if (state.foundSlots == spec.newSlots)
        {
            state.prePatch = PrePatchState::AlreadyApplied;
            return;
        }
        if (state.foundSlots != spec.vanillaSlots)
        {
            state.prePatch = PrePatchState::SlotsUnexpected;
            return;
        }
        std::uint8_t* pool = nullptr;
        if (!ReadEntryBytesSEH(instance, &pool, sizeof(pool)) || pool)
        {
            state.prePatch = PrePatchState::PoolAlreadyBuilt;
            return;
        }
        state.prePatch = WriteBytes(state, site + kSlotsOff, &spec.newSlots, sizeof(spec.newSlots))
            ? PrePatchState::Applied : PrePatchState::WriteFailed;
    }

    // Use levels logged once each, so a test run shows how close the managers come to their limits.
    enum UseLevel : int
    {
        AboveVanilla,
        ThreeQuarters,
        NinetyPercent,
        Full,
        LevelCount
    };

    void NoteUse(const PoolSpec& spec, std::int32_t used, std::int32_t capacity, bool (&logged)[LevelCount])
    {
        const std::int64_t u = used;
        const std::int64_t c = capacity;
        if (!logged[AboveVanilla] && c > spec.vanillaSlots && u > spec.vanillaSlots)
        {
            LogInfo("RendererPools: %s in use %d slots, more than vanilla %d", spec.name, used, spec.vanillaSlots);
            logged[AboveVanilla] = true;
        }
        if (!logged[ThreeQuarters] && u * 4 >= c * 3 && u < c)
        {
            LogWarn("RendererPools: %s in use %d of %d slots (75%%)", spec.name, used, capacity);
            logged[ThreeQuarters] = true;
        }
        if (!logged[NinetyPercent] && u * 10 >= c * 9 && u < c)
        {
            LogWarn("RendererPools: %s in use %d of %d slots (90%%)", spec.name, used, capacity);
            logged[NinetyPercent] = true;
        }
        if (!logged[Full] && u >= c)
        {
            LogError("RendererPools: %s full (%d of %d slots), new models fail to load", spec.name, used, capacity);
            logged[Full] = true;
        }
    }

    // Logs each manager's built size, then its use once a second. Started once and never ends (like
    // DevelopArrayGrow's integrity thread); it idles while the module is uninstalled.
    DWORD WINAPI PoolCheckThread(LPVOID)
    {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        std::uint8_t* pools[kPoolCount] = {};
        std::int32_t  capacity[kPoolCount] = {};
        bool          logged[kPoolCount][LevelCount] = {};
        bool          lateNoted = false;
        unsigned      generation = 0;
        int           startTick = 0;
        for (int tick = 1; ; ++tick)
        {
            Sleep(100);
            if (g_StopCheck.load())
                continue;
            const unsigned current = g_CheckGeneration.load();
            if (current != generation)
            {
                generation = current;
                startTick = tick;
                lateNoted = false;
                std::memset(pools, 0, sizeof(pools));
                std::memset(capacity, 0, sizeof(capacity));
                std::memset(logged, 0, sizeof(logged));
            }

            std::size_t unbuilt = 0;
            for (std::size_t i = 0; i < kPoolCount; ++i)
            {
                if (pools[i] || !g_Pools[i].instance)
                    continue;
                std::uint8_t* pool = nullptr;
                std::int32_t  size = 0;
                if (!ReadEntryBytesSEH(g_Pools[i].instance, &pool, sizeof(pool)) || !pool
                    || !ReadEntryBytesSEH(pool + kCapacityOff, &size, sizeof(size)) || size <= 0)
                {
                    ++unbuilt;
                    continue;
                }
                pools[i] = pool;
                capacity[i] = size;
                if (size == kPools[i].newSlots)
                    LogInfo("RendererPools: %s built with %d slots", kPools[i].name, size);
                else
                    LogWarn("RendererPools: %s built with %d slots, not %d", kPools[i].name, size, kPools[i].newSlots);
            }
            if (unbuilt && !lateNoted && tick - startTick >= 1200)   // 120 s
            {
                LogWarn("RendererPools: %u manager(s) not built within 120 s, still watching", static_cast<unsigned>(unbuilt));
                lateNoted = true;
            }

            if ((tick - startTick) % 10 == 0)
            {
                for (std::size_t i = 0; i < kPoolCount; ++i)
                {
                    std::int32_t used = 0;
                    if (pools[i] && ReadEntryBytesSEH(pools[i] + kUsedOff, &used, sizeof(used)))
                        NoteUse(kPools[i], used, capacity[i], logged[i]);
                }
            }
        }
    }

    void StartCheckThread()
    {
        if (g_CheckThreadStarted.exchange(true))
            return;
        HMODULE pinnedModule = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(&PoolCheckThread), &pinnedModule))
            LogWarn("RendererPools: cannot pin the module for the check thread (err %lu)", GetLastError());
        HANDLE thread = CreateThread(nullptr, 0, &PoolCheckThread, nullptr, 0, nullptr);
        if (thread)
            CloseHandle(thread);
        else
        {
            g_CheckThreadStarted = false;
            LogWarn("RendererPools: check thread failed to start, sizes and use are not logged");
        }
    }
}

// mgsvtpp builds these managers once, before V_FrameWork's InitThread runs, so this is called from DllMain.
void PreApplyRendererPoolsPatch()
{
    for (std::size_t i = 0; i < kPoolCount; ++i)
        PreApplyOne(kPools[i], g_Pools[i]);
}

bool Install_RendererPools()
{
    bool ok = true;
    for (std::size_t i = 0; i < kPoolCount; ++i)
    {
        const PoolSpec& spec  = kPools[i];
        PoolState&      state = g_Pools[i];
        if (state.prePatch.load() == PrePatchState::NotAttempted)
            PreApplyOne(spec, state);   // DllMain skipped it; the same checks apply

        switch (state.prePatch.load())
        {
        case PrePatchState::Applied:
            LogInfo("RendererPools: %s size %d -> %d slots", spec.name, spec.vanillaSlots, spec.newSlots);
            break;
        case PrePatchState::AlreadyApplied:
            LogInfo("RendererPools: %s size already %d slots (another module set it)", spec.name, spec.newSlots);
            break;
        case PrePatchState::PoolAlreadyBuilt:
            LogWarn("RendererPools: %s was built before V_FrameWork loaded, it stays %d slots", spec.name, state.foundSlots);
            break;
        case PrePatchState::SlotsUnexpected:
            LogWarn("RendererPools: %s size is %d, not vanilla %d (another module changed it), left as it is",
                    spec.name, state.foundSlots, spec.vanillaSlots);
            break;
        case PrePatchState::AddressMissing:
        case PrePatchState::NotAttempted:
            LogWarn("RendererPools: %s addresses missing on %s, it stays vanilla", spec.name, GetGameBuildName(gGameBuild));
            break;
        case PrePatchState::SiteUnexpected:
            LogError("RendererPools: %s creation code unexpected (%s), it stays vanilla",
                     spec.name, state.foundCode[0] ? state.foundCode : "unreadable");
            state.instance = nullptr;   // the store did not match, so this may not be the manager
            break;
        case PrePatchState::WriteFailed:
            LogError("RendererPools: %s VirtualProtect failed (err %lu), it stays vanilla", spec.name, state.writeError);
            ok = false;
            break;
        }
    }

    bool check = false;
    for (std::size_t i = 0; i < kPoolCount; ++i)
        check = check || g_Pools[i].instance != nullptr;
    if (check)
    {
        g_CheckGeneration.fetch_add(1);
        g_StopCheck = false;
        StartCheckThread();
    }
    return ok;
}

void Uninstall_RendererPools()
{
    // the managers are built once, so the new sizes stay; the check thread idles
    g_StopCheck = true;
}
