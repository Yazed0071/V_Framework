#include "pch.h"
#include <Windows.h>
#include <cstdint>
#include <cstddef>
#include <unordered_map>
#include <mutex>
#include "MinHook.h"
#include "log.h"
#include "TppPickableRuntime.h"

namespace
{
    static constexpr uintptr_t kAbsCopyAndAdjustInfo = 0x140FB9000ull;

    static std::unordered_map<std::uint16_t, std::uint16_t> g_PickableCountOverrides;
    static std::mutex g_PickableCountOverridesMutex;

    static bool g_TppPickableHooksInstalled = false;
    static void* g_LastPickableSystem = nullptr;

    using CopyAndAdjustInfo_t =
        void(__fastcall*)(void* thisPtr, std::uint16_t* outInfo, void* statusPtr, std::uint8_t* locatorParam);

    static CopyAndAdjustInfo_t g_OrigCopyAndAdjustInfo = nullptr;
}

// Clamps one count value to the runtime 12-bit range.
// Params: value
static std::uint16_t ClampPickableCount(std::uint32_t value)
{
    if (value > 0x0FFFu)
        return 0x0FFFu;

    return static_cast<std::uint16_t>(value);
}

// Returns one live PickableInfo pointer for one locator index.
// Params: thisPtr, locatorIndex, outInfo
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

// Returns true when one outInfo pointer belongs to one valid locator index.
// Params: thisPtr, outInfo, outIndex
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

// Applies one count override to one runtime PickableInfo block.
// Params: outInfo, countRaw
static void ApplyCountOverride(std::uint16_t* outInfo, std::uint16_t countRaw)
{
    if (!outInfo)
        return;

    outInfo[2] = countRaw;
}

// Returns one stored override by locator index.
// Params: locatorIndex, outCountRaw
static bool TryGetStoredOverride(std::uint16_t locatorIndex, std::uint16_t& outCountRaw)
{
    std::lock_guard<std::mutex> lock(g_PickableCountOverridesMutex);

    const auto it = g_PickableCountOverrides.find(locatorIndex);
    if (it == g_PickableCountOverrides.end())
        return false;

    outCountRaw = it->second;
    return true;
}

// Hooked CopyAndAdjustInfo.
// Params: thisPtr, outInfo, statusPtr, locatorParam
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

    Log("[TppPickable] Applied CountRaw override index=%u value=%u\n",
        static_cast<unsigned>(locatorIndex),
        static_cast<unsigned>(overrideCount));
}

// Sets one pickable countRaw override by locator index.
// Params: locatorIndex, countRaw
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

        Log("[TppPickable] SetCountRawByIndex index=%u value=%u live=patched\n",
            static_cast<unsigned>(index16),
            static_cast<unsigned>(value16));
    }
    else
    {
        Log("[TppPickable] SetCountRawByIndex index=%u value=%u live=pending\n",
            static_cast<unsigned>(index16),
            static_cast<unsigned>(value16));
    }

    return true;
}

// Gets one live pickable countRaw by locator index.
// Params: locatorIndex, outCountRaw
bool Get_TppPickableCountRawByIndex(std::uint32_t locatorIndex, std::uint16_t& outCountRaw)
{
    if (locatorIndex > 0xFFFFu)
        return false;

    std::uint16_t* liveInfo = nullptr;
    if (TryGetLivePickableInfoByIndex(g_LastPickableSystem, locatorIndex, liveInfo))
    {
        outCountRaw = liveInfo[2];

        Log("[TppPickable] GetCountRawByIndex index=%u source=live value=%u\n",
            static_cast<unsigned>(locatorIndex),
            static_cast<unsigned>(outCountRaw));
        return true;
    }

    const std::uint16_t index16 = static_cast<std::uint16_t>(locatorIndex);
    if (TryGetStoredOverride(index16, outCountRaw))
    {
        Log("[TppPickable] GetCountRawByIndex index=%u source=override value=%u\n",
            static_cast<unsigned>(locatorIndex),
            static_cast<unsigned>(outCountRaw));
        return true;
    }

    Log("[TppPickable] GetCountRawByIndex failed index=%u\n",
        static_cast<unsigned>(locatorIndex));
    return false;
}

// Clears all pickable countRaw overrides.
// Params: none
void Clear_TppPickableCountRawOverrides()
{
    std::lock_guard<std::mutex> lock(g_PickableCountOverridesMutex);

    const std::size_t oldCount = g_PickableCountOverrides.size();
    g_PickableCountOverrides.clear();

    Log("[TppPickable] Cleared count overrides count=%zu\n", oldCount);
}

// Installs the pickable runtime hooks.
// Params: none
bool Install_TppPickableHooks()
{
    if (g_TppPickableHooksInstalled)
        return true;

    void* target = reinterpret_cast<void*>(kAbsCopyAndAdjustInfo);

    if (MH_CreateHook(
        target,
        reinterpret_cast<void*>(&hkCopyAndAdjustInfo),
        reinterpret_cast<void**>(&g_OrigCopyAndAdjustInfo)) != MH_OK)
    {
        Log("[TppPickable] Install hooks: FAIL create hook\n");
        return false;
    }

    if (MH_EnableHook(target) != MH_OK)
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

// Removes the pickable runtime hooks.
// Params: none
bool Uninstall_TppPickableHooks()
{
    if (!g_TppPickableHooksInstalled)
        return true;

    void* target = reinterpret_cast<void*>(kAbsCopyAndAdjustInfo);

    MH_DisableHook(target);
    MH_RemoveHook(target);

    g_OrigCopyAndAdjustInfo = nullptr;
    g_TppPickableHooksInstalled = false;
    g_LastPickableSystem = nullptr;

    Clear_TppPickableCountRawOverrides();

    Log("[TppPickable] Uninstall hooks: OK\n");
    return true;
}