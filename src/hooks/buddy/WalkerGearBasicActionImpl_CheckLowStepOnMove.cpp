#include <pch.h>

#include "WalkerGearBasicActionImpl_CheckLowStepOnMove.h"

#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "BattleGearLegs.h"
#include "BattleGearWeapons.h"
#include "HookUtils.h"
#include "WalkerGearBasicActionImpl_CorrectVelocity.h"
#include "log.h"

namespace
{
    using CheckLowStepOnMove_t = bool(__fastcall*)(void* action, std::uint32_t index);
    using GetComm_t            = const std::uint8_t*(__fastcall*)(void* system, std::uint16_t commId);
    using GetTransform_t       = const std::uint8_t*(__fastcall*)(void* animation, std::uint32_t index);

    constexpr std::size_t kSystem            = 0x70;
    constexpr std::size_t kAnimation         = 0x80;
    constexpr std::size_t kWorks             = 0x98;
    constexpr std::size_t kFirstIndex        = 0xA0;
    constexpr std::size_t kWorkSize          = 0x3C0;
    constexpr std::size_t kWorkCommId        = 0xF0;
    constexpr std::size_t kWorkStepHeight    = 0x23C;   // State_StepOn lifts the walker by this, 1.5 m a second
    constexpr std::size_t kGetCommSlot       = 0xF8 / sizeof(void*);
    constexpr std::size_t kGetTransformSlot  = 0x388 / sizeof(void*);
    constexpr std::size_t kTransformPosition = 0x20;
    constexpr std::size_t kTransformTurn     = 0x60;
    constexpr std::size_t kCommStickHeading  = 0x2DC;

    constexpr std::uint32_t kGroundFlags = 0x4000700;   // what the D-Walker's leg IK casts its foot rays against
    constexpr float kLowest       = 0.3f;     // the D-Walker's own lowest step
    constexpr float kFaceHeights[3] = { 0.35f, 0.8f, 1.25f };   // the face is looked for at these heights, ahead of the walker's
    constexpr float kBodySides[3]   = { 0.0f, -0.3f, 0.3f };    // middle and of both sides of its 0.3 m body
    constexpr float kFaceReach    = 0.8f;     // metres from the walker's middle: its body and a little more
    constexpr float kBellyUnder   = 0.15f;    // metres under the body bone the hull's belly lies
    constexpr float kUnderBelly   = 0.02f;    // a thing passes under it with this much to spare
    constexpr int   kTopSamples   = 4;        // the top is sampled from just past the face, a step apart
    constexpr float kTopNear      = 0.05f;
    constexpr float kTopSpacing   = 0.2f;
    constexpr float kStillRising  = 0.12f;    // the farthest sample is the highest and this much above the one before: a slope
    constexpr float kRoomLow      = 0.3f;     // over the top, the walker's body needs these heights clear
    constexpr float kRoomHigh     = 1.2f;
    constexpr float kTopMargin    = 0.25f;    // lifted this much past the top, the walker's round underside clears the edge
    constexpr float kMostOffAxis  = 1.309f;   // ridden, the stick within 75 degrees of the facing: it walks onto the ledge, not along it
    constexpr float kPi           = 3.14159265f;

    const std::uint8_t kCheckLowStepOnMoveEntry[] = { 0x40, 0x55, 0x53, 0x56, 0x41, 0x55, 0x41, 0x56, 0x48, 0x8D, 0x6C, 0x24, 0xD0, 0x48, 0x81, 0xEC };

    struct Walker
    {
        void*          system;
        void*          animation;
        GetComm_t      getComm;
        GetTransform_t getTransform;
        std::uint8_t*  work;
        std::uint16_t  commId;
    };

    struct Place
    {
        float position[3];
        float facing;
        float stick;
    };

    CheckLowStepOnMove_t g_Orig = nullptr;
    void*                g_Target = nullptr;
#ifdef _DEBUG
    ULONGLONG            g_NextLog = 0;
#endif

    bool ReadWalkerSEH(void* action, std::uint32_t index, Walker& w)
    {
        __try
        {
            auto* a = static_cast<std::uint8_t*>(action);
            auto* works = *reinterpret_cast<std::uint8_t**>(a + kWorks);
            w.system = *reinterpret_cast<void**>(a + kSystem);
            w.animation = *reinterpret_cast<void**>(a + kAnimation);
            if (!works || !w.system || !w.animation)
                return false;
            w.work = works + static_cast<std::uint64_t>(index - *reinterpret_cast<const std::uint32_t*>(a + kFirstIndex)) * kWorkSize;
            w.commId = *reinterpret_cast<const std::uint16_t*>(w.work + kWorkCommId);
            w.getComm = reinterpret_cast<GetComm_t>((*static_cast<void* const* const*>(w.system))[kGetCommSlot]);
            w.getTransform = reinterpret_cast<GetTransform_t>((*static_cast<void* const* const*>(w.animation))[kGetTransformSlot]);
            return w.getComm && w.getTransform;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ReadPlaceSEH(const std::uint8_t* transform, const std::uint8_t* comm, Place& place)
    {
        __try
        {
            const float* p = reinterpret_cast<const float*>(transform + kTransformPosition);
            for (int k = 0; k < 3; ++k)
                place.position[k] = p[k];
            const float* q = reinterpret_cast<const float*>(transform + kTransformTurn);
            place.facing = std::atan2(2.0f * (q[0] * q[2] + q[1] * q[3]), q[3] * q[3] + q[2] * q[2] - q[0] * q[0] - q[1] * q[1]);   // QuatToRotY
            place.stick = *reinterpret_cast<const float*>(comm + kCommStickHeading);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool WriteStepHeightSEH(std::uint8_t* work, float height)
    {
        __try
        {
            *reinterpret_cast<float*>(work + kWorkStepHeight) = height;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool HitAlong(const Place& place, float side, float height, float from, float to, float* hit)   // side: metres to the right
    {
        const float ahead[3] = { std::sin(place.facing), 0.0f, std::cos(place.facing) };
        const float line[3] = { place.position[0] + ahead[2] * side, height, place.position[2] - ahead[0] * side };
        const float start[3] = { line[0] + ahead[0] * from, height, line[2] + ahead[2] * from };
        const float end[3] = { line[0] + ahead[0] * to, height, line[2] + ahead[2] * to };
        return battleGearLegs::FirstHit(start, end, kGroundFlags, hit);
    }

    bool GroundAt(const Place& place, float side, float along, float top, float bottom, float& ground)
    {
        const float ahead[3] = { std::sin(place.facing), 0.0f, std::cos(place.facing) };
        const float start[3] = { place.position[0] + ahead[2] * side + ahead[0] * along, top,
                                 place.position[2] - ahead[0] * side + ahead[2] * along };
        const float end[3] = { start[0], bottom, start[2] };
        float hit[3];
        if (!battleGearLegs::FirstHit(start, end, kGroundFlags, hit))
            return false;
        ground = hit[1];
        return true;
    }

    // a thing right ahead that the Battle Gear walks onto or over: higher than the D-Walker's lowest step, no higher than `most`,
    // not a slope that keeps rising, and room over it for the walker's body. Its height above the ground under the walker, or 0.
    float LedgeAhead(const Place& place, float most, float reach, const char*& refusal)
    {
        float ground = place.position[1];
        GroundAt(place, 0.0f, 0.0f, place.position[1] + 1.0f, place.position[1] - 3.0f, ground);
        const float ahead[2] = { std::sin(place.facing), std::cos(place.facing) };
        float hit[3];
        float face = -1.0f;
        bool upright = false;   // met above the lowest ray as well: a slope the walker walks up stays under those
        for (const float side : kBodySides)
            for (const float height : kFaceHeights)
            {
                if (!HitAlong(place, side, ground + height, 0.0f, reach, hit))
                    continue;
                const float along = (hit[0] - place.position[0]) * ahead[0] + (hit[2] - place.position[2]) * ahead[1];
                face = face < 0.0f ? along : (std::min)(face, along);
                upright = upright || height > kFaceHeights[0];
            }
        refusal = "nothing ahead";
        if (face < 0.0f)
            return 0.0f;
        const float above = ground + most + 0.05f;
        float middle[kTopSamples];   // the slope test reads the line ahead of the walker's middle
        float highest = ground;
        for (const float side : kBodySides)
            for (int i = 0; i < kTopSamples; ++i)
            {
                float height = ground;   // no hit: past a thin wall the ground drops out of the probe
                GroundAt(place, side, face + kTopNear + kTopSpacing * i, above, ground - 0.2f, height);
                if (side == 0.0f)
                    middle[i] = height;
                highest = (std::max)(highest, height);
            }
        const float top = highest - ground;
        const float farthest = middle[kTopSamples - 1];
        refusal = "a slope";
        if (!upright && farthest >= highest && farthest > middle[kTopSamples - 2] + kStillRising)
            return 0.0f;
        refusal = "too low or too high";
        if (top < kLowest || top > most)
            return 0.0f;
        const float beyond = face + kTopNear + kTopSpacing * (kTopSamples - 1);
        refusal = "no room over it";
        for (const float side : kBodySides)
            if (HitAlong(place, side, ground + top + kRoomLow, 0.0f, beyond, hit)
                || HitAlong(place, side, ground + top + kRoomHigh, 0.0f, beyond, hit))
                return 0.0f;
        refusal = nullptr;
        return top;
    }

    bool __fastcall hkCheckLowStepOnMove(void* action, std::uint32_t index)
    {
        Walker w{};
        const std::uint8_t* comm = ReadWalkerSEH(action, index, w) ? w.getComm(w.system, w.commId) : nullptr;
        if (!battleGearWeapons::IsBattleGearComm(comm))
            return g_Orig(action, index);
        // ridden, the walker's body ball rides over what the Battle Gear passes and the game sets it up onto ledges at the run's
        // own pace (the walk hook shapes it); the game's step-on state would walk it up slowly
        if (battleGearWeapons::Ridden())
            return false;
        if (g_Orig(action, index))
            return true;
        const std::uint8_t* transform = w.getTransform(w.animation, index);
        Place place{};
        if (!transform || !ReadPlaceSEH(transform, comm, place))
            return false;
        const bool alongIt = battleGearWeapons::Ridden() && std::fabs(std::remainder(place.stick - place.facing, 2.0f * kPi)) > kMostOffAxis;
        const char* refusal = "walking along it";   // the AI's walker keeps no stick: it walks where it faces
        const float most = BattleGearHighestStep();
        const float top = alongIt ? 0.0f : LedgeAhead(place, most, kFaceReach, refusal);
        const bool steps = top > 0.0f && WriteStepHeightSEH(w.work, top + kTopMargin);
#ifdef _DEBUG
        const ULONGLONG now = GetTickCount64();
        if (now >= g_NextLog && (steps || (refusal && std::strcmp(refusal, "nothing ahead") != 0)))
        {
            g_NextLog = now + 1000;
            if (steps)
                LogDebug("BattleGear step-on: onto or over a %.2f m thing (up to %.2f m passes)", top, most);
            else
                LogDebug("BattleGear step-on: something ahead, not stepped onto (%s)", refusal);
        }
#endif
        return steps;
    }
}

float BattleGearHighestStep()
{
    alignas(16) float hull[16];
    float clearance = 0.0f;
    if (!battleGearWeapons::HullFrame(hull, &clearance))
        return kBattleGearStepHeight;
    return (std::max)(kBattleGearStepHeight, clearance - kBellyUnder - kUnderBelly);
}

bool Install_WalkerGearBasicActionImpl_CheckLowStepOnMove_Hook()
{
    if (g_Target)
        return true;
    void* target = gAddr.WalkerGearBasicActionImpl_CheckLowStepOnMove
                 ? ResolveGameAddress(gAddr.WalkerGearBasicActionImpl_CheckLowStepOnMove) : nullptr;
    if (!target)
    {
        LogError("BattleGear: walker step-on address missing on this build, the Battle Gear steps only onto the D-Walker's ledges");
        return false;
    }
    const HookEntry state = CheckHookEntry(target, kCheckLowStepOnMoveEntry, sizeof(kCheckLowStepOnMoveEntry));
    if (state == HookEntry::Unknown)
    {
        char desc[160] = {};
        DescribeFunctionEntry(target, desc, sizeof(desc));
        LogError("BattleGear: walker step-on code differs on this build (%s), the Battle Gear steps only onto the D-Walker's ledges",
                 desc);
        return false;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("BattleGear: walker CheckLowStepOnMove", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkCheckLowStepOnMove), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("BattleGear: walker step-on hook failed, the Battle Gear steps only onto the D-Walker's ledges");
        return false;
    }
    g_Target = target;
    return true;
}

void Uninstall_WalkerGearBasicActionImpl_CheckLowStepOnMove_Hook()
{
    if (!g_Target)
        return;
    DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig = nullptr;
}
