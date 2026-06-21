#include "pch.h"
#include <Windows.h>
#include <cstdint>
#include <cstddef>
#include <unordered_map>
#include <mutex>
#include "MinHook.h"
#include "log.h"
#include "TppPickableRuntime.h"
#include "AddressSet.h"
#include "HookUtils.h"

namespace
{
    static std::unordered_map<std::uint16_t, std::uint16_t> g_PickableCountOverrides;
    static std::mutex g_PickableCountOverridesMutex;

    static bool g_TppPickableHooksInstalled = false;
    static void* g_LastPickableSystem = nullptr;

    using CopyAndAdjustInfo_t =
        void(__fastcall*)(void* thisPtr, std::uint16_t* outInfo, void* statusPtr, std::uint8_t* locatorParam);

    static CopyAndAdjustInfo_t g_OrigCopyAndAdjustInfo = nullptr;
}


static std::uint16_t ClampPickableCount(std::uint32_t value)
{
    if (value > 0x0FFFu)
        return 0x0FFFu;

    return static_cast<std::uint16_t>(value);
}


static bool TryGetLivePickableInfoByIndex(void* thisPtr, std::uint32_t locatorIndex, std::uint16_t*& outInfo)
{
    outInfo = nullptr;

    if (!thisPtr)
        return false;

    std::uint8_t* thisBytes = static_cast<std::uint8_t*>(thisPtr);

    const std::uint32_t locatorCount = *reinterpret_cast<std::uint32_t*>(thisBytes + 0x74);
    if (locatorIndex >= locatorCount)
        return false;

    std::uint8_t* infoBase = *reinterpret_cast<std::uint8_t**>(thisBytes + 0x38);
    if (!infoBase)
        return false;

    outInfo = reinterpret_cast<std::uint16_t*>(infoBase + (static_cast<std::size_t>(locatorIndex) * 0x10));
    return true;
}


static bool TryGetLocatorIndexFromOutInfo(void* thisPtr, std::uint16_t* outInfo, std::uint16_t& outIndex)
{
    outIndex = 0;

    if (!thisPtr || !outInfo)
        return false;

    std::uint8_t* thisBytes = static_cast<std::uint8_t*>(thisPtr);

    std::uint8_t* infoBase = *reinterpret_cast<std::uint8_t**>(thisBytes + 0x38);
    if (!infoBase)
        return false;

    const std::uint32_t locatorCount = *reinterpret_cast<std::uint32_t*>(thisBytes + 0x74);

    std::uint8_t* outBytes = reinterpret_cast<std::uint8_t*>(outInfo);
    const std::ptrdiff_t diff = outBytes - infoBase;

    if (diff < 0)
        return false;

    if ((diff % 0x10) != 0)
        return false;

    const std::uint32_t locatorIndex = static_cast<std::uint32_t>(diff / 0x10);
    if (locatorIndex >= locatorCount)
        return false;

    outIndex = static_cast<std::uint16_t>(locatorIndex);
    return true;
}


static void ApplyCountOverride(std::uint16_t* outInfo, std::uint16_t countRaw)
{
    if (!outInfo)
        return;

    outInfo[2] = countRaw;
}


static bool TryGetStoredOverride(std::uint16_t locatorIndex, std::uint16_t& outCountRaw)
{
    std::lock_guard<std::mutex> lock(g_PickableCountOverridesMutex);

    const auto it = g_PickableCountOverrides.find(locatorIndex);
    if (it == g_PickableCountOverrides.end())
        return false;

    outCountRaw = it->second;
    return true;
}


static void __fastcall hkCopyAndAdjustInfo(void* thisPtr, std::uint16_t* outInfo, void* statusPtr, std::uint8_t* locatorParam)
{
    g_LastPickableSystem = thisPtr;

    g_OrigCopyAndAdjustInfo(thisPtr, outInfo, statusPtr, locatorParam);

    std::uint16_t locatorIndex = 0;
    if (!TryGetLocatorIndexFromOutInfo(thisPtr, outInfo, locatorIndex))
        return;

    std::uint16_t overrideCount = 0;
    if (!TryGetStoredOverride(locatorIndex, overrideCount))
        return;

    ApplyCountOverride(outInfo, overrideCount);
}


bool Set_TppPickableCountRawByIndex(std::uint32_t locatorIndex, std::uint32_t countRaw)
{
    if (locatorIndex > 0xFFFFu)
        return false;

    const std::uint16_t index16 = static_cast<std::uint16_t>(locatorIndex);
    const std::uint16_t value16 = ClampPickableCount(countRaw);

    {
        std::lock_guard<std::mutex> lock(g_PickableCountOverridesMutex);
        g_PickableCountOverrides[index16] = value16;
    }

    std::uint16_t* liveInfo = nullptr;
    if (TryGetLivePickableInfoByIndex(g_LastPickableSystem, locatorIndex, liveInfo))
    {
        ApplyCountOverride(liveInfo, value16);
    }
    else
    {
    }

    return true;
}


bool Get_TppPickableCountRawByIndex(std::uint32_t locatorIndex, std::uint16_t& outCountRaw)
{
    if (locatorIndex > 0xFFFFu)
        return false;

    std::uint16_t* liveInfo = nullptr;
    if (TryGetLivePickableInfoByIndex(g_LastPickableSystem, locatorIndex, liveInfo))
    {
        outCountRaw = liveInfo[2];
        return true;
    }

    const std::uint16_t index16 = static_cast<std::uint16_t>(locatorIndex);
    if (TryGetStoredOverride(index16, outCountRaw))
    {
        return true;
    }

    Log("[TppPickable] GetCountRawByIndex failed index=%u\n",
        static_cast<unsigned>(locatorIndex));
    return false;
}


void Clear_TppPickableCountRawOverrides()
{
    std::lock_guard<std::mutex> lock(g_PickableCountOverridesMutex);

    const std::size_t oldCount = g_PickableCountOverrides.size();
    g_PickableCountOverrides.clear();
}


bool Install_TppPickableHooks()
{
    if (g_TppPickableHooksInstalled)
        return true;

    void* target = reinterpret_cast<void*>(gAddr.CopyAndAdjustInfo);

    if (MH_CreateHook(
        target,
        reinterpret_cast<void*>(&hkCopyAndAdjustInfo),
        reinterpret_cast<void**>(&g_OrigCopyAndAdjustInfo)) != MH_OK)
    {
        Log("[TppPickable] Install hooks: FAIL create hook\n");
        return false;
    }

    if (EnableOrQueueHook(target) != MH_OK)
    {
        MH_RemoveHook(target);
        g_OrigCopyAndAdjustInfo = nullptr;
        Log("[TppPickable] Install hooks: FAIL enable hook\n");
        return false;
    }

    g_TppPickableHooksInstalled = true;
    Log("[TppPickable] Install hooks: OK\n");
    return true;
}


bool Uninstall_TppPickableHooks()
{
    if (!g_TppPickableHooksInstalled)
        return true;

    void* target = reinterpret_cast<void*>(gAddr.CopyAndAdjustInfo);

    MH_DisableHook(target);
    MH_RemoveHook(target);

    g_OrigCopyAndAdjustInfo = nullptr;
    g_TppPickableHooksInstalled = false;
    g_LastPickableSystem = nullptr;

    Clear_TppPickableCountRawOverrides();

    Log("[TppPickable] Uninstall hooks: OK\n");
    return true;
}