#include "pch.h"
#include "AkMemoryMgr_Malloc.h"

#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <malloc.h>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using Malloc_t = void* (__fastcall*)(std::int32_t poolId, std::size_t size);
    using Malign_t = void* (__fastcall*)(std::int32_t poolId, std::size_t size, std::uint32_t alignment);
    using Free_t   = std::int32_t(__fastcall*)(std::int32_t poolId, void* memory);

    constexpr std::int32_t  kAkSuccess            = 1;
    constexpr std::size_t   kMemPoolStride        = 0x70;
    constexpr std::size_t   kMemPoolNumBlocksOff  = 0x00;
    constexpr std::size_t   kMemPoolBlockSizeOff  = 0x04;
    constexpr std::size_t   kMemPoolArenaOff      = 0x08;
    constexpr std::size_t   kMemPoolBlockAlignOff = 0x60;
    constexpr std::uint32_t kTlsfDefaultAlignment = 8;
    constexpr std::uint32_t kMinOverflowAlignment = 16;

    Malloc_t g_OrigMalloc = nullptr;
    Malign_t g_OrigMalign = nullptr;
    Free_t   g_OrigFree   = nullptr;

    void* g_MallocTarget = nullptr;
    void* g_MalignTarget = nullptr;
    void* g_FreeTarget   = nullptr;

    const std::int32_t*          g_DefaultPoolId      = nullptr;
    const std::int32_t*          g_PrepareEventPoolId = nullptr;
    const std::uint8_t* const*   g_MemPools           = nullptr;

    bool IsOverflowPool(std::int32_t poolId)
    {
        if (poolId < 0)
            return false;
        return poolId == *g_PrepareEventPoolId || poolId == *g_DefaultPoolId;
    }

    const std::uint8_t* PoolRecord(std::int32_t poolId)
    {
        const std::uint8_t* pools = *g_MemPools;
        if (!pools)
            return nullptr;
        return pools + static_cast<std::size_t>(poolId) * kMemPoolStride;
    }

    template <typename T>
    T ReadPoolField(const std::uint8_t* pool, std::size_t offset)
    {
        return *reinterpret_cast<const T*>(pool + offset);
    }

    std::size_t PoolArenaBytes(const std::uint8_t* pool)
    {
        const auto numBlocks  = ReadPoolField<std::uint32_t>(pool, kMemPoolNumBlocksOff);
        const auto blockSize  = ReadPoolField<std::uint32_t>(pool, kMemPoolBlockSizeOff);
        const auto blockAlign = ReadPoolField<std::uint32_t>(pool, kMemPoolBlockAlignOff);
        return static_cast<std::size_t>(numBlocks) * blockSize + blockAlign;
    }

    bool IsInsidePoolArena(std::int32_t poolId, const void* memory)
    {
        const std::uint8_t* pool = PoolRecord(poolId);
        if (!pool)
            return true;

        const auto* arena = ReadPoolField<const std::uint8_t*>(pool, kMemPoolArenaOff);
        const auto* address = static_cast<const std::uint8_t*>(memory);
        return address >= arena && address < arena + PoolArenaBytes(pool);
    }

    std::uint32_t MallocAlignment(std::int32_t poolId)
    {
        const std::uint8_t* pool = PoolRecord(poolId);
        if (!pool)
            return kTlsfDefaultAlignment;

        const auto blockAlign = ReadPoolField<std::uint32_t>(pool, kMemPoolBlockAlignOff);
        return blockAlign > 4 ? blockAlign : kTlsfDefaultAlignment;
    }

    std::uint32_t OverflowAlignment(std::uint32_t requested)
    {
        const bool isPowerOfTwo = requested != 0 && (requested & (requested - 1)) == 0;
        if (!isPowerOfTwo || requested < kMinOverflowAlignment)
            return kMinOverflowAlignment;
        return requested;
    }

    void* AllocateOverflow(std::int32_t poolId, std::size_t size, std::uint32_t alignment)
    {
        if (!IsOverflowPool(poolId))
            return nullptr;
        return _aligned_malloc(size, OverflowAlignment(alignment));
    }

    void* __fastcall hkMalloc(std::int32_t poolId, std::size_t size)
    {
        void* memory = g_OrigMalloc(poolId, size);
        if (memory || size == 0)
            return memory;
        return AllocateOverflow(poolId, size, MallocAlignment(poolId));
    }

    void* __fastcall hkMalign(std::int32_t poolId, std::size_t size, std::uint32_t alignment)
    {
        void* memory = g_OrigMalign(poolId, size, alignment);
        if (memory || size == 0)
            return memory;
        return AllocateOverflow(poolId, size, alignment);
    }

    std::int32_t __fastcall hkFree(std::int32_t poolId, void* memory)
    {
        const bool isOverflowMemory = memory
            && IsOverflowPool(poolId)
            && !IsInsidePoolArena(poolId, memory);
        if (!isOverflowMemory)
            return g_OrigFree(poolId, memory);

        _aligned_free(memory);
        return kAkSuccess;
    }

    bool HookOne(void* target, void* detour, void** original, void** installedTarget, const char* name)
    {
        if (!CreateAndEnableHook(target, detour, original))
        {
            LogError("AudioBank: AK::MemoryMgr::%s hook failed, sound bank memory stays capped", name);
            return false;
        }
        *installedTarget = target;
        return true;
    }
}

bool Install_AkMemoryMgr_Malloc_Hook()
{
    void* mallocTarget = ResolveGameAddress(gAddr.AkMemoryMgr_Malloc);
    void* malignTarget = ResolveGameAddress(gAddr.AkMemoryMgr_Malign);
    void* freeTarget   = ResolveGameAddress(gAddr.AkMemoryMgr_Free);
    g_MemPools           = static_cast<const std::uint8_t* const*>(ResolveGameAddress(gAddr.AkMemoryMgr_s_pMemPools));
    g_DefaultPoolId      = static_cast<const std::int32_t*>(ResolveGameAddress(gAddr.Ak_g_DefaultPoolId));
    g_PrepareEventPoolId = static_cast<const std::int32_t*>(ResolveGameAddress(gAddr.Ak_g_PrepareEventMemoryPoolID));

    if (!mallocTarget || !malignTarget || !freeTarget || !g_MemPools || !g_DefaultPoolId || !g_PrepareEventPoolId)
    {
        LogError("AudioBank: no Wwise memory addresses for this build, sound bank memory stays capped");
        return false;
    }

    const bool ok =
        HookOne(freeTarget, reinterpret_cast<void*>(&hkFree),
                reinterpret_cast<void**>(&g_OrigFree), &g_FreeTarget, "Free")
        && HookOne(mallocTarget, reinterpret_cast<void*>(&hkMalloc),
                   reinterpret_cast<void**>(&g_OrigMalloc), &g_MallocTarget, "Malloc")
        && HookOne(malignTarget, reinterpret_cast<void*>(&hkMalign),
                   reinterpret_cast<void**>(&g_OrigMalign), &g_MalignTarget, "Malign");

    if (!ok)
        Uninstall_AkMemoryMgr_Malloc_Hook();
    return ok;
}

void Uninstall_AkMemoryMgr_Malloc_Hook()
{
    if (g_MalignTarget)
        DisableAndRemoveHook(g_MalignTarget);
    if (g_MallocTarget)
        DisableAndRemoveHook(g_MallocTarget);
    if (g_FreeTarget)
        DisableAndRemoveHook(g_FreeTarget);

    g_MalignTarget = nullptr;
    g_MallocTarget = nullptr;
    g_FreeTarget   = nullptr;
    g_OrigMalign   = nullptr;
    g_OrigMalloc   = nullptr;
    g_OrigFree     = nullptr;
}
