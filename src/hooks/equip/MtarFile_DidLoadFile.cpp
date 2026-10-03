#include "pch.h"
#include "MtarFile_DidLoadFile.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <unordered_map>

#include "HookUtils.h"
#include "log.h"

namespace
{
    constexpr std::size_t kMtarPathOff   = 0x30;
    constexpr std::size_t kMtarHeaderOff = 0x98;

    constexpr std::size_t kVtblDtor        = 0x08;
    constexpr std::size_t kVtblDidLoad     = 0x48;
    constexpr std::size_t kVtblWillUnload  = 0x50;

    using MtarLoadFn_t = std::int32_t*(__fastcall*)(void*, std::int32_t*);
    using MtarDtorFn_t = void*(__fastcall*)(void*, unsigned int);

    MtarLoadFn_t g_OrigDidLoad    = nullptr;
    MtarLoadFn_t g_OrigWillUnload = nullptr;
    MtarDtorFn_t g_OrigDtor       = nullptr;

    std::mutex g_Mutex;
    std::mutex g_ArmMutex;
    std::unordered_map<std::uint64_t, void*> g_ByPath;
    std::unordered_map<void*, std::uint64_t> g_ByArchive;

    void (*g_UnloadHook)(void*) = nullptr;

    std::atomic<bool> g_Registering{ false };
    bool  g_ArmGaveUp  = false;
    void* g_DidLoadAddr    = nullptr;
    void* g_WillUnloadAddr = nullptr;
    void* g_DtorAddr       = nullptr;

    void* ReadPtrSEH(void* base, std::size_t off)
    {
        __try
        {
            return *reinterpret_cast<void**>(static_cast<char*>(base) + off);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    std::uint64_t ReadU64SEH(void* base, std::size_t off)
    {
        __try
        {
            return *reinterpret_cast<std::uint64_t*>(static_cast<char*>(base) + off);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    void Register(void* mtar)
    {
        if (!mtar || !g_Registering.load(std::memory_order_acquire))
            return;
        const std::uint64_t pathId = ReadU64SEH(mtar, kMtarPathOff);
        if (!pathId || !ReadPtrSEH(mtar, kMtarHeaderOff))
            return;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            g_ByPath[pathId]  = mtar;
            g_ByArchive[mtar] = pathId;
        }
    }

    void Unregister(void* mtar)
    {
        if (!mtar)
            return;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            auto it = g_ByArchive.find(mtar);
            if (it != g_ByArchive.end())
            {
                auto p = g_ByPath.find(it->second);
                if (p != g_ByPath.end() && p->second == mtar)
                    g_ByPath.erase(p);
                g_ByArchive.erase(it);
            }
        }
        if (g_UnloadHook)
            g_UnloadHook(mtar);
    }

    std::int32_t* __fastcall hkDidLoadFile(void* mtar, std::int32_t* errOut)
    {
        std::int32_t* r = g_OrigDidLoad(mtar, errOut);
        Register(mtar);
        return r;
    }

    std::int32_t* __fastcall hkWillUnloadFile(void* mtar, std::int32_t* errOut)
    {
        Unregister(mtar);
        return g_OrigWillUnload(mtar, errOut);
    }

    void* __fastcall hkDtor(void* mtar, unsigned int flags)
    {
        Unregister(mtar);
        return g_OrigDtor(mtar, flags);
    }
}

void MtarRegistry_SetUnloadHook(void (*fn)(void*))
{
    g_UnloadHook = fn;
}

void MtarRegistry_ArmFromLiveArchive(void* mtar)
{
    if (!mtar || g_Registering.load(std::memory_order_acquire))
        return;

    std::lock_guard<std::mutex> armLock(g_ArmMutex);
    if (g_Registering.load(std::memory_order_acquire))
        return;
    if (g_ArmGaveUp)
        return;   // a hook that failed once fails again, and this runs on every animation fetch

    void* vtbl = ReadPtrSEH(mtar, 0);
    void* didLoad    = vtbl ? ReadPtrSEH(vtbl, kVtblDidLoad) : nullptr;
    void* willUnload = vtbl ? ReadPtrSEH(vtbl, kVtblWillUnload) : nullptr;
    void* dtor       = vtbl ? ReadPtrSEH(vtbl, kVtblDtor) : nullptr;
    if (!didLoad || !willUnload || !dtor)
        return;

    const bool okUnload = CreateAndEnableHook(
        willUnload, &hkWillUnloadFile, reinterpret_cast<void**>(&g_OrigWillUnload));
    const bool okDtor = CreateAndEnableHook(
        dtor, &hkDtor, reinterpret_cast<void**>(&g_OrigDtor));
    if (!okUnload || !okDtor)
    {
        if (okDtor || g_OrigDtor)
            DisableAndRemoveHook(dtor);
        if (okUnload || g_OrigWillUnload)
            DisableAndRemoveHook(willUnload);
        g_OrigWillUnload = nullptr;
        g_OrigDtor = nullptr;
        if (!g_ArmGaveUp)
        {
            g_ArmGaveUp = true;
            LogError("WeaponKey: motion-archive registry off, WillUnloadFile@%p=%s ~MtarFile@%p=%s",
                willUnload, okUnload ? "OK" : "FAIL", dtor, okDtor ? "OK" : "FAIL");
        }
        return;
    }

    if (!CreateAndEnableHook(didLoad, &hkDidLoadFile,
                             reinterpret_cast<void**>(&g_OrigDidLoad)))
    {
        if (g_OrigDidLoad)
            DisableAndRemoveHook(didLoad);
        DisableAndRemoveHook(dtor);
        DisableAndRemoveHook(willUnload);
        g_OrigDidLoad = nullptr;
        g_OrigWillUnload = nullptr;
        g_OrigDtor = nullptr;
        if (!g_ArmGaveUp)
        {
            g_ArmGaveUp = true;
            LogError("WeaponKey: motion-archive registry off, DidLoadFile hook failed @ %p",
                didLoad);
        }
        return;
    }

    g_DidLoadAddr = didLoad;
    g_WillUnloadAddr = willUnload;
    g_DtorAddr = dtor;
    g_Registering.store(true, std::memory_order_release);
}

void MtarRegistry_Uninstall()
{
    std::lock_guard<std::mutex> armLock(g_ArmMutex);
    g_Registering.store(false, std::memory_order_release);
    if (g_DidLoadAddr)
        DisableAndRemoveHook(g_DidLoadAddr);
    if (g_DtorAddr)
        DisableAndRemoveHook(g_DtorAddr);
    if (g_WillUnloadAddr)
        DisableAndRemoveHook(g_WillUnloadAddr);
    g_DidLoadAddr = nullptr;
    g_WillUnloadAddr = nullptr;
    g_DtorAddr = nullptr;
    g_OrigDidLoad = nullptr;
    g_OrigWillUnload = nullptr;
    g_OrigDtor = nullptr;
    g_UnloadHook = nullptr;
    std::lock_guard<std::mutex> registryLock(g_Mutex);
    g_ByPath.clear();
    g_ByArchive.clear();
}

void* MtarRegistry_Lookup(std::uint64_t pathId)
{
    if (!pathId || !g_Registering.load(std::memory_order_acquire))
        return nullptr;
    std::lock_guard<std::mutex> lock(g_Mutex);
    auto it = g_ByPath.find(pathId);
    return it == g_ByPath.end() ? nullptr : it->second;
}
