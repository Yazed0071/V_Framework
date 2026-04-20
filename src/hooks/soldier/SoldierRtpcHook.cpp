#include "pch.h"

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "SoldierRtpcHook.h"

namespace
{
    // AK::SoundEngine::SetRTPCValue signature (from mgsvtpp.exe.c line 4786525):
    //   AKRESULT __cdecl AK::SoundEngine::SetRTPCValue(
    //       ulong rtpcId, float value, ulonglong akGameObjId, long timeMs,
    //       AkCurveInterpolation curve);
    using SetRTPCValue_t = int(__cdecl*)(
        std::uint32_t rtpcId,
        float value,
        std::uint64_t akGameObjId,
        long timeMs,
        int curve);

    // fox::sd::ConvertParameterID (RTPC/Switch/State name hasher).
    // Called from fox::sdx::SourceBody::SetRTPC as `sd::_fnv132HashString(*puVar2)`
    // — takes a C string, returns a 32-bit parameter id.
    using ConvertParameterID_t = std::uint32_t(__cdecl*)(const char* name);

    static SetRTPCValue_t g_SetRTPCValue = nullptr;
    static ConvertParameterID_t g_ConvertParameterID = nullptr;

    // Wwise curve interpolation enum. 4 = AkCurveInterpolation_Linear — the same
    // value the game passes when Lua SourceBody:SetRTPC invokes it. Good default.
    static constexpr int kCurveLinear = 4;

    // AkGameObjectID sentinel used by Wwise to mean "apply globally to every
    // registered GameObject". AK_INVALID_GAME_OBJECT in the Wwise SDK.
    static constexpr std::uint64_t kAkInvalidGameObject = 0xFFFFFFFFFFFFFFFFULL;

    // Lazily resolves the two game functions we forward to. Returns true when
    // both pointers are valid. Logs once per failure so Lua errors are visible.
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

    // Shared core — calls the Wwise API with an already-resolved RTPC id and
    // logs the outcome. rtpcNameForLog may be nullptr when the caller started
    // from a numeric id (we log "<by-id>" in that case).
    // Returns the AKRESULT code (1 = AK_Success).
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

    // Name-based entrypoint — hashes the name via the game's ConvertParameterID
    // so the resulting id matches Wwise authoring exactly.
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
        // FOX gameObjectId (uint32) is reused directly as AkGameObjectID
        // (uint64), matching the pattern observed in fox::sd::ad::AudioGameObject
        // (see header for the decomp reference).
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
        // Wwise's "reset RTPC" on a specific game object is normally done by
        // calling ResetRTPCValue. Since we don't have that symbol bound yet,
        // the safest and most portable fallback is to re-set to the RTPC's
        // default using SetRTPCValue with a value read from the Lua caller.
        // Callers that truly want a reset can pass the RTPC's default. For now
        // this returns -3 to signal "not implemented" without crashing.
        (void)goId; (void)rtpcName; (void)timeMs;
        Log("[SoldierRtpc] ResetSoldierRtpc not implemented — "
            "use SetSoldierRtpc with the RTPC's default value instead.\n");
        return -3;
    }
}
