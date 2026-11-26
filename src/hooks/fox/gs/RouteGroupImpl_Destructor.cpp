#include <pch.h>
#include "RouteGroupImpl_Destructor.h"

#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <mutex>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using DestroyRouteGroup_t = void*(__fastcall*)(void* group, std::uint8_t flags);
    using NewWorkArray_t      = std::uint8_t*(__fastcall*)(void* allocator, std::uint64_t count, std::uint64_t alignment);
    using DestructWork_t      = void(__fastcall*)(std::uint8_t* work);
    using AreRoutesValid_t    = bool(__fastcall*)(std::uint8_t* ai, std::uint32_t uav);

    // tpp::gm::uav::impl CombatAiImpl::Work and RouteAiImpl::Work: one per UAV, a patrol and a combat fox::gs::RouteHandle
    constexpr std::size_t   kWorkSize          = 0x38;
    constexpr std::size_t   kWork_CombatRoute  = 0x18;
    constexpr std::size_t   kHandle_Group      = 0x08;
    constexpr std::size_t   kHandle_RouteIndex = 0x10;
    constexpr std::uint16_t kNoRoute           = 0xFFFF;   // with a null group: what a fresh Work holds
    constexpr std::size_t   kAi_Works          = 0x78;
    constexpr unsigned      kMostArrays        = 8;

    const std::uint8_t kDestroyRouteGroupEntry[] = { 0x48, 0x89, 0x4C, 0x24, 0x08, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x48, 0xC7, 0x44, 0x24, 0x20, 0xFE,
                                                     0xFF, 0xFF, 0xFF, 0x48, 0x8B, 0xD9, 0x48, 0x8D, 0x05, 0x00, 0x00, 0x00, 0x00, 0x48, 0x89, 0x01,
                                                     0xF6, 0xC2, 0x01, 0x74, 0x05 };   // puts the base vtable back (its slot 1 is _purecall), then frees
    const std::uint8_t kDestroyRouteGroupMask[]  = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1,
                                                     1, 1, 1, 1, 1 };
    const std::uint8_t kDestructWorkEntry[] = { 0x48, 0x89, 0x4C, 0x24, 0x08, 0x48, 0x83, 0xEC, 0x18, 0x48, 0xC7, 0x04, 0x24, 0xFE, 0xFF, 0xFF,
                                                0xFF, 0x48, 0x8D, 0x05, 0x00, 0x00, 0x00, 0x00, 0x48, 0x89, 0x41, 0x18, 0x48, 0x89, 0x01 };
    const std::uint8_t kDestructWorkMask[]  = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1 };
    const std::uint8_t kNewWorkArrayEntry[] = { 0x40, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0xC7, 0x44, 0x24, 0x20, 0xFE, 0xFF, 0xFF, 0xFF, 0x48,
                                                0x89, 0x5C, 0x24, 0x48, 0x48, 0x89, 0x74, 0x24, 0x50, 0x4D, 0x8B, 0xD0, 0x48, 0x8B, 0xFA, 0x48,
                                                0x8B, 0xF1, 0xF6, 0x41, 0x22, 0x01, 0x75, 0x55, 0x44, 0x0F, 0xB7, 0x49, 0x20 };
    const std::uint8_t kAllBytes[sizeof(kNewWorkArrayEntry)] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                                                 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
#ifdef _DEBUG
    const std::uint8_t kAreRoutesValidEntry[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0xDA, 0x48, 0x6B, 0xDB, 0x38,
                                                  0x48, 0x03, 0x59, 0x78, 0x48, 0x8B, 0x4B, 0x08, 0x48, 0x85, 0xC9, 0x74, 0x43 };
#endif

    struct WorkArray
    {
        std::uint8_t* works        = nullptr;
        std::uint32_t count        = 0;
        void*         handleVtable = nullptr;
        std::uint64_t probeKnown   = 0;
        std::uint64_t probeValid   = 0;
    };

    DestroyRouteGroup_t g_OrigDestroyRouteGroup = nullptr;
    NewWorkArray_t      g_OrigNewCombatWorks    = nullptr;
    NewWorkArray_t      g_OrigNewRouteWorks     = nullptr;
    DestructWork_t      g_OrigDestructWork      = nullptr;
    AreRoutesValid_t    g_OrigAreRoutesValid    = nullptr;
    void*               g_DestroyRouteGroupTarget = nullptr;
    void*               g_NewCombatWorksTarget    = nullptr;
    void*               g_NewRouteWorksTarget     = nullptr;
    void*               g_DestructWorkTarget      = nullptr;
    void*               g_AreRoutesValidTarget    = nullptr;

    std::mutex g_Mutex;
    WorkArray  g_Arrays[kMostArrays] = {};
    bool       g_WarnedFull  = false;
    bool       g_WarnedStale = false;

    template <typename T>
    T& At(std::uint8_t* base, std::size_t offset)
    {
        return *reinterpret_cast<T*>(base + offset);
    }

    void WatchWorkArray(std::uint8_t* works, std::uint64_t count)
    {
        if (!works || !count)
            return;
        bool full = false;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            WorkArray* slot = nullptr;
            for (WorkArray& array : g_Arrays)
            {
                if (array.works == works)
                    slot = &array;
            }
            for (WorkArray& array : g_Arrays)
            {
                if (!slot && !array.works)
                    slot = &array;
            }
            if (slot)
                *slot = WorkArray{ works, static_cast<std::uint32_t>(count), At<void*>(works, 0) };
            full = !slot && !g_WarnedFull;
            g_WarnedFull = g_WarnedFull || full;
        }
        if (full)
            LogWarn("UavRouteHandles: more than %u UAV route handle arrays, the newest one is not watched", kMostArrays);
        LogDebug("UavRouteHandles: watching %llu UAV route handle pairs at %p (thread %lu)", count, works, GetCurrentThreadId());
    }

    // a handle left pointing at a freed group is the pure virtual call abort in AreRoutesValid
    unsigned DropRouteGroup(void* group, bool& stale)
    {
        unsigned dropped = 0;
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (WorkArray& array : g_Arrays)
        {
            for (std::uint32_t i = 0; array.works && i < array.count; ++i)
            {
                std::uint8_t* work = array.works + static_cast<std::size_t>(i) * kWorkSize;
                alignas(16) std::uint8_t seen[kWorkSize] = {};
                if (!ReadEntryBytesSEH(work, seen, sizeof(seen)) || At<void*>(seen, 0) != array.handleVtable
                    || At<void*>(seen, kWork_CombatRoute) != array.handleVtable)
                {
                    array = WorkArray{};   // freed without its destructors: stop touching it
                    stale = true;
                    break;
                }
                for (const std::size_t handle : { std::size_t{ 0 }, kWork_CombatRoute })
                {
                    if (At<void*>(seen, handle + kHandle_Group) != group)
                        continue;
                    At<void*>(work, handle + kHandle_Group) = nullptr;
                    At<std::uint16_t>(work, handle + kHandle_RouteIndex) = kNoRoute;
                    ++dropped;
                }
            }
        }
        return dropped;
    }

    void* __fastcall hkDestroyRouteGroup(void* group, std::uint8_t flags)
    {
        bool stale = false;
        const unsigned dropped = DropRouteGroup(group, stale);
        if (dropped)
            LogDebug("UavRouteHandles: route group %p freed while %u UAV route handle(s) still used it, handles cleared (thread %lu)",
                     group, dropped, GetCurrentThreadId());
        if (stale && !g_WarnedStale)
        {
            g_WarnedStale = true;
            LogWarn("UavRouteHandles: a UAV route handle array was freed without its destructors, it is no longer watched");
        }
        return g_OrigDestroyRouteGroup(group, flags);
    }

    std::uint8_t* __fastcall hkNewCombatWorks(void* allocator, std::uint64_t count, std::uint64_t alignment)
    {
        std::uint8_t* works = g_OrigNewCombatWorks(allocator, count, alignment);   // null on the sizing pass
        WatchWorkArray(works, count);
        return works;
    }

    std::uint8_t* __fastcall hkNewRouteWorks(void* allocator, std::uint64_t count, std::uint64_t alignment)
    {
        std::uint8_t* works = g_OrigNewRouteWorks(allocator, count, alignment);
        WatchWorkArray(works, count);
        return works;
    }

    void __fastcall hkDestructWork(std::uint8_t* work)
    {
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            for (WorkArray& array : g_Arrays)
            {
                if (array.works == work)   // FreeChunk destructs element 0 first
                    array = WorkArray{};
            }
        }
        g_OrigDestructWork(work);
    }

#ifdef _DEBUG
    bool __fastcall hkAreRoutesValid(std::uint8_t* ai, std::uint32_t uav)
    {
        const bool valid = g_OrigAreRoutesValid(ai, uav);
        std::uint8_t* works = At<std::uint8_t*>(ai, kAi_Works);
        bool changed = false;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            for (WorkArray& array : g_Arrays)
            {
                if (array.works != works || uav >= 64 || uav >= array.count)
                    continue;
                const std::uint64_t bit = 1ull << uav;
                changed = !(array.probeKnown & bit) || ((array.probeValid & bit) != 0) != valid;
                array.probeKnown |= bit;
                array.probeValid = valid ? (array.probeValid | bit) : (array.probeValid & ~bit);
            }
        }
        if (changed)
        {
            std::uint8_t* work = works + static_cast<std::size_t>(uav) * kWorkSize;
            LogDebug("UavRouteHandles: UAV %u in array %p %s: patrol group %p route %u, combat group %p route %u", uav, works,
                     valid ? "has valid routes" : "has NO valid routes", At<void*>(work, kHandle_Group),
                     At<std::uint16_t>(work, kHandle_RouteIndex), At<void*>(work, kWork_CombatRoute + kHandle_Group),
                     At<std::uint16_t>(work, kWork_CombatRoute + kHandle_RouteIndex));
        }
        return valid;
    }
#endif

    bool Hook(std::uintptr_t address, const std::uint8_t* entry, const std::uint8_t* mask, std::size_t size, void* detour,
              void** original, void*& installed, const char* what)
    {
        void* target = ResolveGameAddress(address);
        const HookEntry state = address ? CheckHookEntryMasked(target, entry, mask, size) : HookEntry::Unknown;
        if (state == HookEntry::Unknown)
        {
            char found[160] = {};
            if (address)
                DescribeFunctionEntry(target, found, sizeof(found));
            LogError("UavRouteHandles: %s is not the code we know (%s), a pack unloading routes a UAV still uses can crash the game",
                     what, address ? found : "no address for this build");
            return false;
        }
        if (state == HookEntry::ForeignHook)
            LogHookedOnTop(what, target);
        if (!CreateAndEnableHook(target, detour, original))
        {
            LogError("UavRouteHandles: %s hook failed at %p, a pack unloading routes a UAV still uses can crash the game", what, target);
            return false;
        }
        installed = target;
        return true;
    }
}

bool Install_RouteGroupImpl_Destructor_Hooks()
{
    if (g_DestroyRouteGroupTarget)
        return true;

    const bool tracking =
        Hook(gAddr.UavRouteWork_Destructor, kDestructWorkEntry, kDestructWorkMask, sizeof(kDestructWorkEntry),
             reinterpret_cast<void*>(&hkDestructWork), reinterpret_cast<void**>(&g_OrigDestructWork), g_DestructWorkTarget,
             "UAV route Work destructor")
        && Hook(gAddr.UavCombatAiImpl_NewWorkArray, kNewWorkArrayEntry, kAllBytes, sizeof(kNewWorkArrayEntry),
                reinterpret_cast<void*>(&hkNewCombatWorks), reinterpret_cast<void**>(&g_OrigNewCombatWorks), g_NewCombatWorksTarget,
                "NewObjectArray<uav CombatAiImpl::Work>")
        && Hook(gAddr.UavRouteAiImpl_NewWorkArray, kNewWorkArrayEntry, kAllBytes, sizeof(kNewWorkArrayEntry),
                reinterpret_cast<void*>(&hkNewRouteWorks), reinterpret_cast<void**>(&g_OrigNewRouteWorks), g_NewRouteWorksTarget,
                "NewObjectArray<uav RouteAiImpl::Work>")
        && Hook(gAddr.RouteGroupImpl_DeletingDestructor, kDestroyRouteGroupEntry, kDestroyRouteGroupMask, sizeof(kDestroyRouteGroupEntry),
                reinterpret_cast<void*>(&hkDestroyRouteGroup), reinterpret_cast<void**>(&g_OrigDestroyRouteGroup),
                g_DestroyRouteGroupTarget, "RouteGroupImpl destructor");
    if (!tracking)
    {
        Uninstall_RouteGroupImpl_Destructor_Hooks();
        return false;
    }

#ifdef _DEBUG
    Hook(gAddr.UavCombatAiImpl_AreRoutesValid, kAreRoutesValidEntry, kAllBytes, sizeof(kAreRoutesValidEntry),
         reinterpret_cast<void*>(&hkAreRoutesValid), reinterpret_cast<void**>(&g_OrigAreRoutesValid), g_AreRoutesValidTarget,
         "uav AreRoutesValid (Debug probe)");
#endif
    return true;
}

void Uninstall_RouteGroupImpl_Destructor_Hooks()
{
    for (void** target : { &g_AreRoutesValidTarget, &g_DestroyRouteGroupTarget, &g_NewRouteWorksTarget, &g_NewCombatWorksTarget,
                           &g_DestructWorkTarget })
    {
        if (*target)
            DisableAndRemoveHook(*target);
        *target = nullptr;
    }
    std::lock_guard<std::mutex> lock(g_Mutex);
    for (WorkArray& array : g_Arrays)
        array = WorkArray{};
}
