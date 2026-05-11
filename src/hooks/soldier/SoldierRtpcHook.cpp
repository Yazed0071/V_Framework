#include "pch.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <mutex>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "SoldierRtpcHook.h"

namespace
{


    using SetRTPCValue_t = int(__cdecl*)(
        std::uint32_t rtpcId,
        float value,
        std::uint64_t akGameObjId,
        long timeMs,
        int curve);


    using ConvertParameterID_t = std::uint32_t(__cdecl*)(const char* name);

    static SetRTPCValue_t g_SetRTPCValue = nullptr;
    static SetRTPCValue_t g_OrigSetRTPCValue = nullptr;
    static ConvertParameterID_t g_ConvertParameterID = nullptr;

    static void* g_SetRTPCValueHookTarget = nullptr;
    static std::once_flag g_LoggerInstallFlag;
    static std::atomic<bool> g_LoggingEnabled{ false };


    static constexpr int kCurveLinear = 4;


    static constexpr std::uint64_t kAkInvalidGameObject = 0xFFFFFFFFFFFFFFFFULL;


    static bool ResolveApis()
    {
        if (!g_SetRTPCValue && gAddr.AK_SoundEngine_SetRTPCValue != 0)
        {
            g_SetRTPCValue = reinterpret_cast<SetRTPCValue_t>(
                ResolveGameAddress(gAddr.AK_SoundEngine_SetRTPCValue));
        }

        if (!g_ConvertParameterID && gAddr.Fox_Sd_ConvertParameterID != 0)
        {
            g_ConvertParameterID = reinterpret_cast<ConvertParameterID_t>(
                ResolveGameAddress(gAddr.Fox_Sd_ConvertParameterID));
        }

        if (!g_SetRTPCValue || !g_ConvertParameterID)
        {
            static bool s_warned = false;
            if (!s_warned)
            {
                s_warned = true;
                Log("[SoldierRtpc] address resolution failed "
                    "(SetRTPCValue=%p ConvertParameterID=%p) — "
                    "AddressSet may not be initialized yet.\n",
                    reinterpret_cast<void*>(g_SetRTPCValue),
                    reinterpret_cast<void*>(g_ConvertParameterID));
            }
            return false;
        }
        return true;
    }


    // Logging hook on AK::SoundEngine::SetRTPCValue.
    // No SEH and no allocations on the hot path.
    static int __cdecl hk_SetRTPCValue(std::uint32_t rtpcId,
                                       float value,
                                       std::uint64_t akGameObjId,
                                       long timeMs,
                                       int curve)
    {
        const int result = g_OrigSetRTPCValue
            ? g_OrigSetRTPCValue(rtpcId, value, akGameObjId, timeMs, curve)
            : 0;

        if (g_LoggingEnabled.load(std::memory_order_relaxed))
        {
            if (akGameObjId == kAkInvalidGameObject)
            {
                Log("[SoldierRtpc][AK] SetRTPC global rtpc=0x%08X value=%f timeMs=%ld curve=%d -> %d\n",
                    rtpcId, value, timeMs, curve, result);
            }
            else
            {
                Log("[SoldierRtpc][AK] SetRTPC akObj=0x%016llX rtpc=0x%08X value=%f timeMs=%ld curve=%d -> %d\n",
                    static_cast<unsigned long long>(akGameObjId),
                    rtpcId, value, timeMs, curve, result);
            }
        }
        return result;
    }


    // Lazy install — only installs when logging is enabled the first time.
    static void TryInstallLoggerHook()
    {
        std::call_once(g_LoggerInstallFlag, []()
            {
                if (!ResolveApis())
                {
                    Log("[SoldierRtpc] LoggerHook install: SetRTPCValue not resolvable\n");
                    return;
                }

                void* target = reinterpret_cast<void*>(g_SetRTPCValue);
                const bool ok = CreateAndEnableHook(
                    target,
                    reinterpret_cast<void*>(&hk_SetRTPCValue),
                    reinterpret_cast<void**>(&g_OrigSetRTPCValue));
                if (ok)
                {
                    g_SetRTPCValueHookTarget = target;
                    Log("[SoldierRtpc] LoggerHook install: OK (target=%p)\n", target);
                }
                else
                {
                    Log("[SoldierRtpc] LoggerHook install: MinHook FAILED (target=%p)\n", target);
                }
            });
    }


    static int SetRtpcCore(const char* rtpcNameForLog, std::uint32_t rtpcId,
                           float value, std::uint64_t akGameObj, long timeMs)
    {
        if (!ResolveApis())
            return -2;

        // Use original through trampoline if logger is installed,
        // otherwise call the resolved direct address.
        const int result = g_OrigSetRTPCValue
            ? g_OrigSetRTPCValue(rtpcId, value, akGameObj, timeMs, kCurveLinear)
            : g_SetRTPCValue(rtpcId, value, akGameObj, timeMs, kCurveLinear);

        const char* nameShown = (rtpcNameForLog && *rtpcNameForLog) ? rtpcNameForLog : "<by-id>";

        if (akGameObj == kAkInvalidGameObject)
        {
            Log("[SoldierRtpc] SetGlobal rtpc='%s' (id=0x%08X) value=%f timeMs=%ld result=%d\n",
                nameShown, rtpcId, value, timeMs, result);
        }
        else
        {
            Log("[SoldierRtpc] Set akObj=0x%016llX rtpc='%s' (id=0x%08X) value=%f timeMs=%ld result=%d\n",
                static_cast<unsigned long long>(akGameObj),
                nameShown, rtpcId, value, timeMs, result);
        }
        return result;
    }


    static int SetRtpcByName(const char* rtpcName, float value,
                             std::uint64_t akGameObj, long timeMs)
    {
        if (!rtpcName || !*rtpcName)
        {
            Log("[SoldierRtpc] empty rtpcName — ignoring\n");
            return -1;
        }
        if (!ResolveApis())
            return -2;

        const std::uint32_t rtpcId = g_ConvertParameterID(rtpcName);
        return SetRtpcCore(rtpcName, rtpcId, value, akGameObj, timeMs);
    }


    // One-shot warning — engine gameObjectId is NOT a Wwise AkGameObjectID.
    static void WarnEngineGoIdMisuse()
    {
        static bool s_warned = false;
        if (!s_warned)
        {
            s_warned = true;
            Log("[SoldierRtpc][NOTE] SetSoldierRtpc passes engine gameObjectId as the Wwise "
                "AkGameObjectID. These are different ID spaces — Wwise registers each audio "
                "object with an internal incrementing counter (see fox::sd::ad::AudioSoundEngine"
                "::RegisterGameObject). Calls below will reach AK::SoundEngine::SetRTPCValue "
                "and return AKRESULT=1, but Wwise has no registered object at that ID and the "
                "RTPC change is silently dropped.\n"
                "[SoldierRtpc][NOTE] Reliable workflow:\n"
                "  1. SetRtpcLoggingEnabled(true) to log every game-side SetRTPCValue call.\n"
                "  2. Trigger a sound on the target soldier (voice line, footstep, etc).\n"
                "  3. Read the [AK] log lines to find the soldier's AkObjectID.\n"
                "  4. Call SetRtpcByAkObjId(akObjId, rtpc, value, time) with that ID.\n"
                "  5. Maintain a soldierName -> akObjId map in your Lua scripts.\n");
        }
    }
}

namespace SoldierRtpc
{
    int SetSoldierRtpc(std::uint32_t goId, const char* rtpcName, float value, long timeMs)
    {
        WarnEngineGoIdMisuse();
        return SetRtpcByName(rtpcName, value, static_cast<std::uint64_t>(goId), timeMs);
    }

    int SetSoldierRtpcById(std::uint32_t goId, std::uint32_t rtpcId, float value, long timeMs)
    {
        WarnEngineGoIdMisuse();
        return SetRtpcCore(nullptr, rtpcId, value, static_cast<std::uint64_t>(goId), timeMs);
    }

    int SetGlobalRtpc(const char* rtpcName, float value, long timeMs)
    {
        return SetRtpcByName(rtpcName, value, kAkInvalidGameObject, timeMs);
    }

    int SetGlobalRtpcById(std::uint32_t rtpcId, float value, long timeMs)
    {
        return SetRtpcCore(nullptr, rtpcId, value, kAkInvalidGameObject, timeMs);
    }

    int ResetSoldierRtpc(std::uint32_t goId, const char* rtpcName, long timeMs)
    {
        (void)goId; (void)rtpcName; (void)timeMs;
        Log("[SoldierRtpc] ResetSoldierRtpc not implemented — "
            "use SetSoldierRtpc with the RTPC's default value instead.\n");
        return -3;
    }


    int SetRtpcByAkObjId(std::uint64_t akObjId, const char* rtpcName, float value, long timeMs)
    {
        return SetRtpcByName(rtpcName, value, akObjId, timeMs);
    }


    int SetRtpcByAkObjIdById(std::uint64_t akObjId, std::uint32_t rtpcId, float value, long timeMs)
    {
        return SetRtpcCore(nullptr, rtpcId, value, akObjId, timeMs);
    }


    bool SetRtpcLoggingEnabled(bool enabled)
    {
        if (enabled)
            TryInstallLoggerHook();
        const bool prev = g_LoggingEnabled.exchange(enabled, std::memory_order_relaxed);
        Log("[SoldierRtpc] logging %s (was %s)\n",
            enabled ? "ENABLED" : "DISABLED",
            prev    ? "enabled" : "disabled");
        return prev;
    }


    bool IsRtpcLoggingEnabled()
    {
        return g_LoggingEnabled.load(std::memory_order_relaxed);
    }
}
