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
#include "MissionCodeGuard.h"

namespace
{
    struct PickableOverride
    {
        std::uint16_t mask = 0;
        std::uint16_t values[kTppPickableFieldCount] = {};
    };

    static std::unordered_map<std::uint16_t, PickableOverride> g_PickableOverrides;
    static std::mutex g_PickableOverridesMutex;

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

    __try
    {
        const std::uint32_t locatorCount = *reinterpret_cast<std::uint32_t*>(thisBytes + 0x74);
        if (locatorIndex >= locatorCount)
            return false;

        std::uint8_t* infoBase = *reinterpret_cast<std::uint8_t**>(thisBytes + 0x38);
        if (!infoBase)
            return false;

        outInfo = reinterpret_cast<std::uint16_t*>(infoBase + (static_cast<std::size_t>(locatorIndex) * 0x10));
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        outInfo = nullptr;
        return false;
    }
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


static std::uint16_t ClampPickableFieldValue(std::uint32_t fieldId, std::uint32_t value)
{
    switch (fieldId)
    {
    case kTppPickableFieldEquipId:
        return static_cast<std::uint16_t>(value & 0x7FFu);
    case kTppPickableFieldCountRaw:
    case kTppPickableFieldSecondCountRaw:
    case kTppPickableFieldCountMax:
    case kTppPickableFieldSecondCountMax:
        return ClampPickableCount(value);
    case kTppPickableFieldInfoType:
        return static_cast<std::uint16_t>(value & 0xFFu);
    default:
        return static_cast<std::uint16_t>(value & 0xFFFFu);
    }
}


static void ApplyFieldToInfo(std::uint16_t* info, std::uint32_t fieldId, std::uint16_t value)
{
    if (!info)
        return;

    std::uint8_t* bytes = reinterpret_cast<std::uint8_t*>(info);

    switch (fieldId)
    {
    case kTppPickableFieldEquipId:
        info[0] = static_cast<std::uint16_t>((info[0] & ~0x7FFu) | (value & 0x7FFu));
        break;
    case kTppPickableFieldCountRaw:
        info[2] = value;
        break;
    case kTppPickableFieldSecondCountRaw:
        info[3] = value;
        break;
    case kTppPickableFieldCountMax:
        info[4] = value;
        break;
    case kTppPickableFieldSecondCountMax:
        info[5] = value;
        break;
    case kTppPickableFieldInfoType:
        bytes[12] = static_cast<std::uint8_t>(value);
        info[7] = static_cast<std::uint16_t>((info[7] & ~0x1u) | ((value & 0xFFu) ? 0x1u : 0x0u));
        break;
    case kTppPickableFieldFlags:
        info[7] = static_cast<std::uint16_t>((value & ~0x8u) | (info[7] & 0x8u));
        break;
    default:
        break;
    }
}


static bool GuardedApplyFieldToInfo(std::uint16_t* info, std::uint32_t fieldId, std::uint16_t value)
{
    __try
    {
        ApplyFieldToInfo(info, fieldId, value);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}


static bool GuardedCopyInfoWords(const std::uint16_t* info, std::uint16_t* outWords8)
{
    __try
    {
        for (int i = 0; i < 8; ++i)
            outWords8[i] = info[i];
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}


static bool GuardedReadCountRaw(const std::uint16_t* info, std::uint16_t& outCountRaw)
{
    __try
    {
        outCountRaw = info[2];
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}


static void ApplyOverrideToInfo(std::uint16_t* info, const PickableOverride& override_)
{
    static const std::uint32_t kApplyOrder[kTppPickableFieldCount] =
    {
        kTppPickableFieldEquipId,
        kTppPickableFieldCountRaw,
        kTppPickableFieldSecondCountRaw,
        kTppPickableFieldCountMax,
        kTppPickableFieldSecondCountMax,
        kTppPickableFieldFlags,
        kTppPickableFieldInfoType,
    };

    for (std::uint32_t fieldId : kApplyOrder)
    {
        if (override_.mask & (1u << fieldId))
            ApplyFieldToInfo(info, fieldId, override_.values[fieldId]);
    }
}


static void __fastcall hkCopyAndAdjustInfo(void* thisPtr, std::uint16_t* outInfo, void* statusPtr, std::uint8_t* locatorParam)
{
    g_LastPickableSystem = thisPtr;

    MISSION_GUARD_ORIGINAL_VOID(g_OrigCopyAndAdjustInfo, thisPtr, outInfo, statusPtr, locatorParam);

    g_OrigCopyAndAdjustInfo(thisPtr, outInfo, statusPtr, locatorParam);

    std::uint16_t locatorIndex = 0;
    if (!TryGetLocatorIndexFromOutInfo(thisPtr, outInfo, locatorIndex))
        return;

    std::lock_guard<std::mutex> lock(g_PickableOverridesMutex);
    const auto it = g_PickableOverrides.find(locatorIndex);
    if (it == g_PickableOverrides.end())
        return;

    ApplyOverrideToInfo(outInfo, it->second);
}


bool Set_TppPickableFieldByIndex(std::uint32_t locatorIndex, std::uint32_t fieldId, std::uint32_t value)
{
    if (locatorIndex > 0xFFFFu || fieldId >= kTppPickableFieldCount)
        return false;

    const std::uint16_t index16 = static_cast<std::uint16_t>(locatorIndex);
    const std::uint16_t value16 = ClampPickableFieldValue(fieldId, value);

    {
        std::lock_guard<std::mutex> lock(g_PickableOverridesMutex);
        PickableOverride& override_ = g_PickableOverrides[index16];
        override_.mask |= static_cast<std::uint16_t>(1u << fieldId);
        override_.values[fieldId] = value16;
    }

    if (!MissionCodeGuard::ShouldBypassHooks())
    {
        std::uint16_t* liveInfo = nullptr;
        if (TryGetLivePickableInfoByIndex(g_LastPickableSystem, locatorIndex, liveInfo))
        {
            GuardedApplyFieldToInfo(liveInfo, fieldId, value16);
        }
    }

    return true;
}


bool Get_TppPickableInfoWordsByIndex(std::uint32_t locatorIndex, std::uint16_t* outWords8)
{
    if (locatorIndex > 0xFFFFu || !outWords8)
        return false;

    std::uint16_t* liveInfo = nullptr;
    if (!TryGetLivePickableInfoByIndex(g_LastPickableSystem, locatorIndex, liveInfo))
        return false;

    return GuardedCopyInfoWords(liveInfo, outWords8);
}


bool Set_TppPickableCountRawByIndex(std::uint32_t locatorIndex, std::uint32_t countRaw)
{
    return Set_TppPickableFieldByIndex(locatorIndex, kTppPickableFieldCountRaw, countRaw);
}


bool Get_TppPickableCountRawByIndex(std::uint32_t locatorIndex, std::uint16_t& outCountRaw)
{
    if (locatorIndex > 0xFFFFu)
        return false;

    std::uint16_t* liveInfo = nullptr;
    if (TryGetLivePickableInfoByIndex(g_LastPickableSystem, locatorIndex, liveInfo) &&
        GuardedReadCountRaw(liveInfo, outCountRaw))
    {
        return true;
    }

    const std::uint16_t index16 = static_cast<std::uint16_t>(locatorIndex);
    {
        std::lock_guard<std::mutex> lock(g_PickableOverridesMutex);
        const auto it = g_PickableOverrides.find(index16);
        if (it != g_PickableOverrides.end() &&
            (it->second.mask & (1u << kTppPickableFieldCountRaw)))
        {
            outCountRaw = it->second.values[kTppPickableFieldCountRaw];
            return true;
        }
    }

    Log("[TppPickable] GetCountRawByIndex failed index=%u\n",
        static_cast<unsigned>(locatorIndex));
    return false;
}


void Clear_TppPickableOverrides()
{
    std::lock_guard<std::mutex> lock(g_PickableOverridesMutex);
    g_PickableOverrides.clear();
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
#ifdef _DEBUG
    Log("[TppPickable] Install hooks: OK\n");
#endif
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

    Clear_TppPickableOverrides();

#ifdef _DEBUG
    Log("[TppPickable] Uninstall hooks: OK\n");
#endif
    return true;
}