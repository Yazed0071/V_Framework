#include <pch.h>

#include "FoxPath.h"
#include "FoxPathInternal.h"

#include <atomic>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "FoxHashes.h"
#include "log.h"

namespace
{
    struct alignas(16) FoxFsPath
    {
        std::uint64_t pathCode;
        void*         impl;
    };

    using LocatePath_t = bool (__fastcall*)(FoxFsPath* path, std::uint32_t flags);

    std::atomic<std::uint32_t> g_FpkExtCode{ 0 };
    std::atomic<std::uint32_t> g_FpkdExtCode{ 0 };
    std::atomic<bool>          g_MissingAddressLogged{ false };

    std::uint32_t ExtCodeOf(std::uint64_t pathCode64Ext)
    {
        return static_cast<std::uint32_t>(pathCode64Ext >> 51);
    }

    std::uint32_t CachedExtCode(std::atomic<std::uint32_t>& cache, const char* samplePath)
    {
        std::uint32_t code = cache.load(std::memory_order_relaxed);
        if (code == 0)
        {
            code = ExtCodeOf(FoxHashes::PathCode64Ext(samplePath));
            cache.store(code, std::memory_order_relaxed);
        }
        return code;
    }

    bool IsArchiveLevelPackage(std::uint64_t pathCode64Ext)
    {
        const std::uint32_t ext = ExtCodeOf(pathCode64Ext);
        return ext != 0
            && (ext == CachedExtCode(g_FpkExtCode, "/Assets/tpp/pack/probe.fpk")
             || ext == CachedExtCode(g_FpkdExtCode, "/Assets/tpp/pack/probe.fpkd"));
    }

    bool LocateByPathCode(LocatePath_t locatePath, std::uint64_t pathCode64Ext)
    {
        FoxFsPath path{ pathCode64Ext, nullptr };
        return locatePath(&path, 0);
    }

#ifdef _DEBUG
    using FoxPathCtor_t   = void* (__fastcall*)(void* path, std::uint64_t code64ext);
    using FoxPathExists_t = std::uint8_t (__fastcall*)(void* path, std::uint32_t flags);
    using FoxPathDtor_t   = void (__fastcall*)(void* path);

    void LogProbe(LocatePath_t locatePath, const char* packPath)
    {
        auto ctor   = reinterpret_cast<FoxPathCtor_t>(ResolveGameAddress(gAddr.FoxPath_Path));
        auto exists = reinterpret_cast<FoxPathExists_t>(ResolveGameAddress(gAddr.Fox_Path_Exists));
        auto dtor   = reinterpret_cast<FoxPathDtor_t>(ResolveGameAddress(gAddr.Fox_Path_Dtor));
        if (!ctor || !exists || !dtor) return;

        const std::uint64_t code = FoxHashes::PathCode64Ext(packPath);
        alignas(16) std::uint64_t foxPath = 0;
        ctor(&foxPath, code);
        const bool oldResult = exists(&foxPath, 0) != 0;
        dtor(&foxPath);

        LogDebug("FoxPathProbe: %s locate=%d old=%d", packPath,
            LocateByPathCode(locatePath, code) ? 1 : 0, oldResult ? 1 : 0);
    }

    void LogProbeOnce(LocatePath_t locatePath)
    {
        static std::atomic<bool> s_done{ false };
        if (s_done.exchange(true, std::memory_order_relaxed)) return;

        LogProbe(locatePath, "/Assets/tpp/pack/buddy/walkergear/bw_wp_01.fpk");
        LogProbe(locatePath, "/Assets/tpp/pack/buddy/battlegear/buddy_btg_00.fpk");
        LogProbe(locatePath, "/Assets/tpp/pack/zoz/Zoz_Cameras/afgh/zoz_Field_Cameras_custom.fpk");
        LogProbe(locatePath, "/Assets/tpp/pack/buddy/battlegear/buddy_btg_common.fpk");
    }
#endif
}

namespace fox
{
namespace detail
{
    bool PathExistsByCode(std::uint64_t pathCode64Ext)
    {
        if (pathCode64Ext == 0) return false;

        if (!IsArchiveLevelPackage(pathCode64Ext))
            return true;

        auto locatePath = reinterpret_cast<LocatePath_t>(
            ResolveGameAddress(gAddr.FileLocationManager_LocatePath));
        if (!locatePath)
        {
            if (!g_MissingAddressLogged.exchange(true, std::memory_order_relaxed))
                LogError("FoxPath: LocatePath address missing, custom .fpk packs fall back to vanilla");
            return false;
        }

#ifdef _DEBUG
        LogProbeOnce(locatePath);
#endif
        return LocateByPathCode(locatePath, pathCode64Ext);
    }
}

    bool PathExists(const char* path)
    {
        if (!path || !path[0]) return false;
        return detail::PathExistsByCode(FoxHashes::PathCode64Ext(path));
    }
}
