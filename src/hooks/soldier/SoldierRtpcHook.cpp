#include "pch.h"

#include <cstdint>

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
    static ConvertParameterID_t g_ConvertParameterID = nullptr;


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


    static int SetRtpcCore(const char* rtpcNameForLog, std::uint32_t rtpcId,
                           float value, std::uint64_t akGameObj, long timeMs)
    {
        if (!ResolveApis())
            return -2;

        const int result = g_SetRTPCValue(rtpcId, value, akGameObj, timeMs, kCurveLinear);
        const char* nameShown = (rtpcNameForLog && *rtpcNameForLog) ? rtpcNameForLog : "<by-id>";

        if (akGameObj == kAkInvalidGameObject)
        {
            Log("[SoldierRtpc] SetGlobal rtpc='%s' (id=0x%08X) value=%f timeMs=%ld result=%d\n",
                nameShown, rtpcId, value, timeMs, result);
        }
        else
        {
            Log("[SoldierRtpc] SetSoldier goId=%u rtpc='%s' (id=0x%08X) value=%f timeMs=%ld result=%d\n",
                static_cast<unsigned>(akGameObj), nameShown, rtpcId, value, timeMs, result);
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
}

namespace SoldierRtpc
{
    int SetSoldierRtpc(std::uint32_t goId, const char* rtpcName, float value, long timeMs)
    {


        return SetRtpcByName(rtpcName, value, static_cast<std::uint64_t>(goId), timeMs);
    }

    int SetSoldierRtpcById(std::uint32_t goId, std::uint32_t rtpcId, float value, long timeMs)
    {
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
}
