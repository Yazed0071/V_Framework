#include "pch.h"
#include "EnemyLangIdOverride.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "MissionCodeGuard.h"

namespace
{
    using GetEnemyInformationLangId_t = std::uint64_t* (__fastcall*)(
        void* this_,
        std::uint64_t* outLangId,
        std::uint32_t markerId,
        std::uint32_t param2,
        std::uint32_t typeSelector);

    using GetEnemyUnitName_t = bool(__fastcall*)(void* this_);

    using ResolveLangIdToString_t = void* (__fastcall*)(void* resolver, std::uint64_t hash);

    using BinoUiPushString_t = void(__fastcall*)(
        void* uiObj,
        void* arg1,
        void* arg2,
        void* stringPtr,
        std::uint8_t flag);


    static constexpr std::size_t kBinoResolverField       = 0x48;
    static constexpr std::size_t kBinoResolverSubOffset   = 0x20;
    static constexpr std::size_t kBinoCachedStringField   = 0x9C0;
    static constexpr std::size_t kBinoUiObjField          = 0x70;
    static constexpr std::size_t kBinoUiArg1FieldMain     = 0x350;
    static constexpr std::size_t kBinoUiArg1FieldShadow   = 0x358;
    static constexpr std::size_t kBinoUiArg2Field         = 0x38;
    static constexpr std::size_t kResolverVtableOffset    = 0x750;
    static constexpr std::size_t kBinoUiPushVtableOffset  = 0x708;


    static GetEnemyInformationLangId_t g_OrigGetEnemyInformationLangId = nullptr;
    static GetEnemyUnitName_t          g_OrigGetEnemyUnitName          = nullptr;


    static std::atomic<bool>          g_MapOverrideActive{ false };
    static std::atomic<std::uint64_t> g_MapOverrideHash{ 0 };
    static std::atomic<bool>          g_BinoOverrideActive{ false };
    static std::atomic<std::uint64_t> g_BinoOverrideHash{ 0 };


    static void* TryReadPtr(std::uintptr_t addr)
    {
        if (!addr)
            return nullptr;

        __try
        {
            return *reinterpret_cast<void**>(addr);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }


    static bool OverrideBinoEnemyUnitName(void* this_, std::uint64_t hash)
    {
        if (!this_ || !hash)
            return false;

        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(this_);

        void* resolverHolder = TryReadPtr(base + kBinoResolverField);
        if (!resolverHolder)
            return false;

        void* resolver = TryReadPtr(reinterpret_cast<std::uintptr_t>(resolverHolder) + kBinoResolverSubOffset);
        if (!resolver)
            return false;

        void** resolverVt = static_cast<void**>(TryReadPtr(reinterpret_cast<std::uintptr_t>(resolver)));
        if (!resolverVt)
            return false;

        void* resolveSlot = TryReadPtr(reinterpret_cast<std::uintptr_t>(resolverVt) + kResolverVtableOffset);
        if (!resolveSlot)
            return false;

        void* resolvedString = nullptr;
        __try
        {
            const auto resolve = reinterpret_cast<ResolveLangIdToString_t>(resolveSlot);
            resolvedString = resolve(resolver, hash);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }

        if (!resolvedString)
            return false;

        __try
        {
            *reinterpret_cast<void**>(base + kBinoCachedStringField) = resolvedString;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }

        void* uiObj = TryReadPtr(base + kBinoUiObjField);
        if (!uiObj)
            return true;

        void** uiVt = static_cast<void**>(TryReadPtr(reinterpret_cast<std::uintptr_t>(uiObj)));
        if (!uiVt)
            return true;

        void* uiPushSlot = TryReadPtr(reinterpret_cast<std::uintptr_t>(uiVt) + kBinoUiPushVtableOffset);
        if (!uiPushSlot)
            return true;

        void* argMain   = TryReadPtr(base + kBinoUiArg1FieldMain);
        void* argShadow = TryReadPtr(base + kBinoUiArg1FieldShadow);
        void* arg2      = TryReadPtr(base + kBinoUiArg2Field);

        __try
        {
            const auto uiPush = reinterpret_cast<BinoUiPushString_t>(uiPushSlot);
            if (argMain)
                uiPush(uiObj, argMain, arg2, resolvedString, 1);
            if (argShadow)
                uiPush(uiObj, argShadow, arg2, resolvedString, 1);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }

        return true;
    }
}


static std::uint64_t* __fastcall hkGetEnemyInformationLangId(
    void* this_,
    std::uint64_t* outLangId,
    std::uint32_t markerId,
    std::uint32_t param2,
    std::uint32_t typeSelector)
{
    const bool bypass = MissionCodeGuard::ShouldBypassHooks();
    const bool active = g_MapOverrideActive.load(std::memory_order_acquire);

    if (bypass || !active || !outLangId)
    {
        return g_OrigGetEnemyInformationLangId
            ? g_OrigGetEnemyInformationLangId(this_, outLangId, markerId, param2, typeSelector)
            : outLangId;
    }

    const std::uint64_t hash = g_MapOverrideHash.load(std::memory_order_acquire);

    __try
    {
        *outLangId = hash;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return g_OrigGetEnemyInformationLangId
            ? g_OrigGetEnemyInformationLangId(this_, outLangId, markerId, param2, typeSelector)
            : outLangId;
    }

    return outLangId;
}


static bool __fastcall hkGetEnemyUnitName(void* this_)
{
    if (!g_OrigGetEnemyUnitName)
        return false;

    const bool ok = g_OrigGetEnemyUnitName(this_);

    if (!ok || MissionCodeGuard::ShouldBypassHooks())
        return ok;

    if (!g_BinoOverrideActive.load(std::memory_order_acquire))
        return ok;

    const std::uint64_t hash = g_BinoOverrideHash.load(std::memory_order_acquire);
    if (!hash)
        return ok;

    OverrideBinoEnemyUnitName(this_, hash);
    return ok;
}


void EnemyLangId_SetMapOverride(std::uint64_t langIdHash)
{
    g_MapOverrideHash.store(langIdHash, std::memory_order_release);
    g_MapOverrideActive.store(langIdHash != 0, std::memory_order_release);
    Log("[EnemyLangId] Map override = 0x%llX\n",
        static_cast<unsigned long long>(langIdHash));
}


void EnemyLangId_ClearMapOverride()
{
    g_MapOverrideActive.store(false, std::memory_order_release);
    g_MapOverrideHash.store(0, std::memory_order_release);
    Log("[EnemyLangId] Map override cleared\n");
}


void EnemyLangId_SetBinoOverride(std::uint64_t langIdHash)
{
    g_BinoOverrideHash.store(langIdHash, std::memory_order_release);
    g_BinoOverrideActive.store(langIdHash != 0, std::memory_order_release);
    Log("[EnemyLangId] Bino override = 0x%llX\n",
        static_cast<unsigned long long>(langIdHash));
}


void EnemyLangId_ClearBinoOverride()
{
    g_BinoOverrideActive.store(false, std::memory_order_release);
    g_BinoOverrideHash.store(0, std::memory_order_release);
    Log("[EnemyLangId] Bino override cleared\n");
}


bool Install_EnemyLangIdOverride_Hooks()
{
    void* mapTarget  = ResolveGameAddress(gAddr.MotherBaseMapCommonDataImpl_GetEnemyInformationLangId);
    void* binoTarget = ResolveGameAddress(gAddr.TppUIBinoSubjectiveImpl_GetEnemyUnitName);

    if (!mapTarget)
        Log("[Hook] EnemyLangId Map: address resolve failed\n");

    if (!binoTarget)
        Log("[Hook] EnemyLangId Bino: address resolve failed\n");

    bool okMap = false;
    bool okBino = false;

    if (mapTarget)
    {
        okMap = CreateAndEnableHook(
            mapTarget,
            reinterpret_cast<void*>(&hkGetEnemyInformationLangId),
            reinterpret_cast<void**>(&g_OrigGetEnemyInformationLangId));
        Log("[Hook] EnemyLangId Map: %s\n", okMap ? "OK" : "FAIL");
    }

    if (binoTarget)
    {
        okBino = CreateAndEnableHook(
            binoTarget,
            reinterpret_cast<void*>(&hkGetEnemyUnitName),
            reinterpret_cast<void**>(&g_OrigGetEnemyUnitName));
        Log("[Hook] EnemyLangId Bino: %s\n", okBino ? "OK" : "FAIL");
    }

    return okMap || okBino;
}


bool Uninstall_EnemyLangIdOverride_Hooks()
{
    DisableAndRemoveHook(ResolveGameAddress(gAddr.MotherBaseMapCommonDataImpl_GetEnemyInformationLangId));
    DisableAndRemoveHook(ResolveGameAddress(gAddr.TppUIBinoSubjectiveImpl_GetEnemyUnitName));

    g_OrigGetEnemyInformationLangId = nullptr;
    g_OrigGetEnemyUnitName = nullptr;

    return true;
}
