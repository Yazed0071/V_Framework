#include "pch.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>

#include "AddressSet.h"
#include "AnimControl_GetAnimFileFromGaniPath.h"
#include "FoxHashes.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"
#include "../equip/MtarFile_DidLoadFile.h"

namespace
{
    using GetAnimFileFromGaniPath_t = void*(__fastcall*)(void* animControl, std::uint64_t clip);
    using MtarGetAnimFile_t = void*(__fastcall*)(void* mtar, std::uint64_t clip);

    constexpr const char* kFrameWorkMotionArchive = "/Assets/tpp/motion/mtar/V_FrameWork/V_FrameWork_motion.mtar";

    GetAnimFileFromGaniPath_t g_Orig = nullptr;
    MtarGetAnimFile_t g_MtarGetAnimFile = nullptr;
    void* g_Target = nullptr;
    std::atomic<std::uint64_t> g_ArchivePath{0};
    std::atomic<bool> g_ArchiveOn{false};                 // the FrameWorkMotionArchive module wants V_FrameWork_motion.mtar
    std::atomic<AnimControlWatch_t> g_Watch{nullptr};     // WeaponKey's anim-control list shares this one hook

    void* ClipFromArchive(void* mtar, std::uint64_t clip)
    {
        __try
        {
            return g_MtarGetAnimFile(mtar, clip);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    void* FromFrameWorkArchive(std::uint64_t clip)
    {
        std::uint64_t path = g_ArchivePath.load(std::memory_order_relaxed);
        if (!path)
        {
            path = FoxHashes::PathCode64Ext(kFrameWorkMotionArchive);
            g_ArchivePath.store(path, std::memory_order_relaxed);
        }
        void* mtar = MtarRegistry_Lookup(path);
        return mtar ? ClipFromArchive(mtar, clip) : nullptr;
    }

    void* __fastcall hk_GetAnimFileFromGaniPath(void* animControl, std::uint64_t clip)
    {
        void* file = g_Orig(animControl, clip);
        if (const AnimControlWatch_t watch = g_Watch.load(std::memory_order_acquire))
            watch(animControl);
        if (file || !g_ArchiveOn.load(std::memory_order_relaxed) || MissionCodeGuard::ShouldBypassHooks())
            return file;
        return FromFrameWorkArchive(clip);
    }

    bool HookOnce()   // one MinHook per target: both users ride on it
    {
        if (g_Target)
            return true;
        void* target = ResolveGameAddress(gAddr.AnimControl_GetAnimFileFromGaniPath);
        if (!target || !CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_GetAnimFileFromGaniPath), reinterpret_cast<void**>(&g_Orig)))
            return false;
        g_Target = target;
        return true;
    }

    void UnhookWhenUnused()
    {
        if (!g_Target || g_ArchiveOn.load(std::memory_order_relaxed) || g_Watch.load(std::memory_order_acquire))
            return;
        DisableAndRemoveHook(g_Target);
        g_Target = nullptr;
        g_Orig = nullptr;
    }
}

bool Install_AnimControl_GetAnimFileFromGaniPath_Hook()
{
    if (g_ArchiveOn.load(std::memory_order_relaxed))
        return true;
    if (!gAddr.AnimControl_GetAnimFileFromGaniPath || !gAddr.Mtar_GetAnimFile)
    {
        LogError("FrameWorkMotion: GetAnimFileFromGaniPath / Mtar GetAnimFile address missing, V_FrameWork_motion.mtar off");
        return true;
    }
    g_MtarGetAnimFile = reinterpret_cast<MtarGetAnimFile_t>(ResolveGameAddress(gAddr.Mtar_GetAnimFile));
    if (!g_MtarGetAnimFile || !HookOnce())
    {
        LogError("FrameWorkMotion: GetAnimFileFromGaniPath hook failed, V_FrameWork_motion.mtar off");
        return true;
    }
    g_ArchiveOn.store(true, std::memory_order_relaxed);
    return true;
}

bool Uninstall_AnimControl_GetAnimFileFromGaniPath_Hook()
{
    g_ArchiveOn.store(false, std::memory_order_relaxed);
    UnhookWhenUnused();
    return true;
}

bool AnimControl_GetAnimFileFromGaniPath_Watch(AnimControlWatch_t watch)
{
    g_Watch.store(watch, std::memory_order_release);
    if (!watch)
    {
        UnhookWhenUnused();
        return true;
    }
    return gAddr.AnimControl_GetAnimFileFromGaniPath && HookOnce();
}
