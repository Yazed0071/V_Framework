#include "pch.h"

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
    using FoxPathCtor_t   = void* (__fastcall*)(void* path, std::uint64_t code64ext);
    using FoxPathExists_t = std::uint8_t (__fastcall*)(void* path, std::uint32_t flags);
    using FoxPathDtor_t   = void (__fastcall*)(void* path);

    std::atomic<std::uint32_t> g_FpkExtCode{ 0 };
    std::atomic<std::uint32_t> g_FpkdExtCode{ 0 };
    std::atomic<bool>          g_ProbeFailureLogged{ false };

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

        auto ctor = reinterpret_cast<FoxPathCtor_t>(
            ResolveGameAddress(gAddr.FoxPath_Path));
        auto exists = reinterpret_cast<FoxPathExists_t>(
            ResolveGameAddress(gAddr.Fox_Path_Exists));
        auto dtor = reinterpret_cast<FoxPathDtor_t>(
            ResolveGameAddress(gAddr.Fox_Path_Dtor));

        if (!ctor || !exists)
        {
            if (!g_ProbeFailureLogged.exchange(true, std::memory_order_relaxed))
                LogError("FoxPath: fox::Path or fox::File::DoesExist address missing, uninstalled .fpk will hang the load");
            return true;
        }

        std::uint64_t pathBuf[8] = {};
        bool result = false;
        bool faulted = false;

        __try
        {
            ctor(pathBuf, pathCode64Ext);
            result = exists(pathBuf, 0) != 0;
            if (dtor) dtor(pathBuf);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            faulted = true;
        }

        if (faulted)
        {
            if (!g_ProbeFailureLogged.exchange(true, std::memory_order_relaxed))
                LogWarn("FoxPath: .fpk probe faulted, uninstalled .fpk will hang the load");
            return true;
        }

        return result;
    }
}

    bool PathExists(const char* path)
    {
        if (!path || !path[0]) return false;
        return detail::PathExistsByCode(FoxHashes::PathCode64Ext(path));
    }
}
