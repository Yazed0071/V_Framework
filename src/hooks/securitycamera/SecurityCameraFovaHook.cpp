#include "pch.h"

#include <Windows.h>
#include <array>
#include <atomic>
#include <cstdint>

#include "HookUtils.h"
#include "log.h"
#include "MissionCodeGuard.h"
#include "AddressSet.h"
#include "FoxHashes.h"
#include "SecurityCameraFovaHook.h"

namespace
{

    using SetFova_t = void(__fastcall*)(void* self, std::uint32_t param2, std::int32_t variant);


    using LoadFv2EntryByHash_t = std::int64_t(__fastcall*)(
        void* owner, std::uint32_t partsType, std::uint32_t slotKind, std::uint64_t hash);


    static SetFova_t g_OrigSetFova = nullptr;


    static constexpr std::int32_t kMaxVariants = 2;
    static constexpr std::uint32_t kFv2SlotKind = 0x10u;
    static constexpr std::uintptr_t kFovaArrayBaseOffset = 0x98ull;
    static constexpr std::uintptr_t kPartsTypeOffset = 0x34ull;
    static constexpr std::uintptr_t kOwnerPtrOffset = 0x10ull;
    static constexpr std::uintptr_t kFv2EntryToFv2Offset = 0x28ull;
    static constexpr std::size_t kVtblIndex_LoadFv2EntryByHash = 0x140 / sizeof(void*);


    static std::array<std::atomic<std::uint64_t>, kMaxVariants> g_VariantOverrides{};


    static bool ApplyOverrideForVariant(void* self, std::int32_t variant)
    {
        if (variant < 0 || variant >= kMaxVariants)
            return false;

        const std::uint64_t hash = g_VariantOverrides[variant].load(std::memory_order_relaxed);
        if (hash == 0)
            return false;

        if (!self)
            return false;

        __try
        {
            const auto thisAsBytes = reinterpret_cast<std::uintptr_t>(self);

            const auto owner = *reinterpret_cast<std::uintptr_t*>(thisAsBytes + kOwnerPtrOffset);
            if (!owner)
                return false;

            const auto ownerVtbl = *reinterpret_cast<std::uintptr_t*>(owner);
            if (!ownerVtbl)
                return false;

            const auto loadFn = reinterpret_cast<LoadFv2EntryByHash_t>(
                reinterpret_cast<std::uintptr_t*>(ownerVtbl)[kVtblIndex_LoadFv2EntryByHash]);
            if (!loadFn)
                return false;

            const std::uint32_t partsType =
                *reinterpret_cast<std::uint32_t*>(thisAsBytes + kPartsTypeOffset);

            const std::int64_t entry = loadFn(
                reinterpret_cast<void*>(owner), partsType, kFv2SlotKind, hash);
            if (!entry)
                return false;

            const std::int64_t fv2Ptr =
                *reinterpret_cast<std::int64_t*>(entry + kFv2EntryToFv2Offset);
            if (!fv2Ptr)
                return false;

            const auto slotAddr = thisAsBytes + kFovaArrayBaseOffset
                + static_cast<std::uintptr_t>(variant) * sizeof(void*);
            *reinterpret_cast<std::int64_t*>(slotAddr) = fv2Ptr;

            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("[SecCamFova] ApplyOverride: exception (self=%p, variant=%d)\n",
                self, static_cast<int>(variant));
            return false;
        }
    }


    static void __fastcall hkSetFova(void* self, std::uint32_t param2, std::int32_t variant)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigSetFova, self, param2, variant);

        ApplyOverrideForVariant(self, variant);

        if (g_OrigSetFova)
            g_OrigSetFova(self, param2, variant);
    }
}


bool Install_SecurityCameraFova_Hook()
{
    void* target = ResolveGameAddress(gAddr.RealizedSecurityCamera2Impl_SetFova);
    if (!target)
    {
        Log("[Hook] SecurityCameraFova: target resolve failed (addr=%llX)\n",
            static_cast<unsigned long long>(gAddr.RealizedSecurityCamera2Impl_SetFova));
        return false;
    }

    const bool ok = CreateAndEnableHook(
        target,
        reinterpret_cast<void*>(&hkSetFova),
        reinterpret_cast<void**>(&g_OrigSetFova));

    Log("[Hook] SecurityCameraFova: %s (target=%p)\n", ok ? "OK" : "FAIL", target);
    return ok;
}


bool Uninstall_SecurityCameraFova_Hook()
{
    DisableAndRemoveHook(ResolveGameAddress(gAddr.RealizedSecurityCamera2Impl_SetFova));
    g_OrigSetFova = nullptr;

    Log("[Hook] SecurityCameraFova: removed\n");
    return true;
}


void Set_SecurityCameraFovaHash(std::int32_t variantIndex, std::uint64_t hash)
{
    if (variantIndex < 0 || variantIndex >= kMaxVariants)
    {
        Log("[SecCamFova] Set: variant %d out of range [0..%d)\n",
            static_cast<int>(variantIndex), static_cast<int>(kMaxVariants));
        return;
    }

    g_VariantOverrides[variantIndex].store(hash, std::memory_order_relaxed);
    Log("[SecCamFova] variant %d -> 0x%016llX\n",
        static_cast<int>(variantIndex), static_cast<unsigned long long>(hash));
}


void Set_SecurityCameraFovaPath(std::int32_t variantIndex, const char* path)
{
    if (!path || !*path)
        return;

    const std::uint64_t hash = FoxHashes::PathCode64Ext(path);
    Log("[SecCamFova] hashing path \"%s\" -> 0x%016llX (variant=%d)\n",
        path, static_cast<unsigned long long>(hash), static_cast<int>(variantIndex));
    Set_SecurityCameraFovaHash(variantIndex, hash);
}


void Clear_SecurityCameraFova(std::int32_t variantIndex)
{
    if (variantIndex < 0 || variantIndex >= kMaxVariants)
        return;

    g_VariantOverrides[variantIndex].store(0, std::memory_order_relaxed);
    Log("[SecCamFova] variant %d cleared\n", static_cast<int>(variantIndex));
}


void Clear_AllSecurityCameraFovas()
{
    for (auto& slot : g_VariantOverrides)
        slot.store(0, std::memory_order_relaxed);
    Log("[SecCamFova] all variants cleared\n");
}


std::uint64_t Get_SecurityCameraFovaHash(std::int32_t variantIndex)
{
    if (variantIndex < 0 || variantIndex >= kMaxVariants)
        return 0;

    return g_VariantOverrides[variantIndex].load(std::memory_order_relaxed);
}
