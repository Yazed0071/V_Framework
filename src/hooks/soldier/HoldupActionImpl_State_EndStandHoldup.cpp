#include <pch.h>
#include "HoldupActionImpl_State_EndStandHoldup.h"

#include <Windows.h>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HoldupAiImpl_StepHoldupStand.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

// The end states pick only _d (Alert) or _c (comrade). For a soldier who freed himself the enter runs vanilla's own
// steps with the unused _b state instead.
// _b and _c turn the soldier on their root (up to 180 degrees), but nothing ever turns him: vanilla turn states rotate the
// CharaControl themselves (BasicAction2Impl::RotationControl) and these states don't, so his legs step a turn on the spot.
// Each frame of those clips his CharaControl facing and turn target are set to where the clip's root has turned him.

namespace
{
    using State_t        = void(__fastcall*)(std::uint8_t* action, std::uint32_t slot, std::uint32_t proc, void* param);
    using RequestState_t = std::uint64_t(__fastcall*)(void* motion, std::uint32_t slot, std::uint32_t layer, std::uint64_t state, float blend, std::uint32_t flags);
    using DropWeapon_t   = void(__fastcall*)(std::uint8_t* action, std::uint32_t soldier, std::uint32_t slot);
    using CharaControl_t = std::uint8_t*(__fastcall*)(void* motion, std::uint32_t slot);
    using ClipTime_t     = float(__fastcall*)(void* motion, std::uint32_t slot, std::uint32_t a, std::uint32_t b, std::uint8_t c);

    constexpr std::uint32_t kProcEnter = 1;
    constexpr std::uint32_t kProcExit  = 2;

    constexpr std::size_t kCharaControlSlot = 0x388 / sizeof(void*);   // AnimationController -> CharaControlInterface
    constexpr std::size_t kClipTimeSlot     = 0x2A0 / sizeof(void*);   // where the body layer's clip is, as the crawl end reads it
    constexpr std::size_t kClipLengthSlot   = 0x2B0 / sizeof(void*);
    constexpr std::size_t kCharaRotation    = 0x60;                    // facing quaternion x y z w, as RotationControl reads it
    constexpr std::size_t kCharaTurnTarget  = 0x80;                    // SetTurnTarget's; while its flag is up ControlActImpl slerps the facing toward it
    constexpr std::size_t kWorkCommand      = 0x3E;                    // 5 / 11 = the comrade rescue (_c), else Alert (_d)
    constexpr std::uint8_t kComradeStand    = 5;
    constexpr std::uint8_t kComradeCrawl    = 11;
    constexpr std::uint32_t kMaxSoldiers    = 1024;

    constexpr int kTurnSteps = 64;
    constexpr float kStandFreedTurn[kTurnSteps + 1] = {   // enetasr_s_hdu_ed_b, 445 frames, root yaw in degrees
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, -0.2f, -0.8f, -2.2f, -3.9f, -6.1f, -8.9f, -12.0f,
        -15.5f, -19.4f, -23.5f, -28.0f, -32.7f, -37.6f, -42.8f, -48.7f,
        -55.8f, -64.0f, -72.7f, -81.8f, -90.9f, -99.7f, -108.2f, -116.2f,
        -123.6f, -131.3f, -139.3f, -147.4f, -155.1f, -162.4f, -168.8f, -173.9f,
        -177.8f, -179.7f, -180.0f, -180.0f, -180.0f, -180.0f, -180.0f, -180.0f,
        -180.0f, -180.0f, -180.0f, -180.0f, -180.0f, -180.0f, -180.0f, -180.0f,
        -180.0f,
    };
    constexpr float kStandComradeTurn[kTurnSteps + 1] = {   // enetasr_s_hdu_ed_c, 458 frames, root yaw in degrees
        0.0f, 0.0f, 0.0f, 0.0f, -0.0f, -0.1f, -0.2f, -0.4f,
        -0.6f, -1.1f, -1.7f, -2.6f, -3.6f, -5.1f, -6.7f, -8.7f,
        -11.7f, -15.8f, -21.0f, -27.0f, -33.5f, -40.6f, -47.8f, -55.1f,
        -62.3f, -69.0f, -75.4f, -81.1f, -85.7f, -89.3f, -91.6f, -92.4f,
        -92.6f, -92.2f, -91.7f, -90.6f, -88.8f, -86.0f, -81.1f, -73.1f,
        -62.6f, -50.9f, -38.9f, -27.0f, -16.2f, -7.7f, -1.9f, 0.2f,
        0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.1f, 0.1f, 0.1f,
        0.1f, 0.1f, 0.1f, 0.1f, 0.1f, 0.0f, 0.0f, 0.0f,
        0.0f,
    };
    constexpr float kCrawlFreedTurn[kTurnSteps + 1] = {   // enetasr_c_hdu_ed_b, 384 frames, root yaw in degrees
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -4.2f, -10.6f,
        -16.9f, -23.3f, -29.6f, -36.0f, -42.3f, -48.7f, -55.0f, -61.4f,
        -67.8f, -74.1f, -80.5f, -86.8f, -93.2f, -99.5f, -105.9f, -112.2f,
        -118.6f, -124.9f, -131.3f, -137.6f, -144.0f, -150.3f, -156.7f, -163.1f,
        -169.4f, -175.8f, -180.0f, -180.0f, -180.0f, -180.0f, -180.0f, -180.0f,
        -180.0f, -180.0f, -180.0f, -180.0f, -180.0f, -180.0f, -180.0f, -180.0f,
        -180.0f, -180.0f, -180.0f, -180.0f, -180.0f, -180.0f, -180.0f, -180.0f,
        -180.0f,
    };
    constexpr float kCrawlComradeTurn[kTurnSteps + 1] = {   // enetasr_c_hdu_ed_c, 550 frames, root yaw in degrees
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.4f, 7.3f, 20.4f, 35.9f,
        50.5f, 61.1f, 69.2f, 76.6f, 83.8f, 91.7f, 101.0f, 111.0f,
        121.0f, 129.7f, 136.2f, 140.3f, 143.7f, 145.9f, 147.7f, 148.7f,
        149.5f, 149.8f, 150.0f, 150.0f, 150.0f, 150.0f, 150.0f, 148.6f,
        139.3f, 126.5f, 115.0f, 103.7f, 90.1f, 71.3f, 47.3f, 25.9f,
        7.6f, -0.0f, -0.0f, -0.0f, -0.0f, -0.0f, -0.0f, -0.0f,
        -0.0f, -0.0f, -0.0f, -0.0f, -0.0f, -0.0f, -0.0f, -0.0f,
        -0.0f,
    };

    struct TurnClip
    {
        const float* curve;
        float        frames;
    };

    constexpr TurnClip kStandFreedClip{ kStandFreedTurn, 445.0f };
    constexpr TurnClip kStandComradeClip{ kStandComradeTurn, 458.0f };
    constexpr TurnClip kCrawlFreedClip{ kCrawlFreedTurn, 384.0f };
    constexpr TurnClip kCrawlComradeClip{ kCrawlComradeTurn, 550.0f };

    struct Turning
    {
        const TurnClip* clip;
        float           start[4];
        float           ratio;
    };

    Turning g_Turning[kMaxSoldiers] = {};

    constexpr std::size_t kWorks       = 0x88;
    constexpr std::size_t kWorkBase    = 0x90;
    constexpr std::size_t kWorkStride  = 0x40;
    constexpr std::size_t kWorkStance  = 0x20;
    constexpr std::size_t kWorkMotion  = 0x30;
    constexpr std::size_t kWorkSoldier = 0x38;
    constexpr std::size_t kWorkFlags   = 0x3F;
    constexpr std::size_t kStanceBits  = 0x34;

    constexpr std::uint32_t kBodyLayer = 0;

    struct EndClip
    {
        std::uint64_t state;
        float         blend;
        std::uint32_t stanceKeep;
        std::uint32_t stanceSet;
    };

    constexpr EndClip kStandFreedHimself{ 0x53A85BE45DEAull, 8.0f, 0xFFFFFFF1u, 1u };  // enetasr_s_hdu_ed_b
    constexpr EndClip kCrawlFreedHimself{ 0x88217F116F61ull, 16.0f, 0xFFFFFFF3u, 3u }; // enetasr_c_hdu_ed_b

    struct Work
    {
        std::uint8_t*  stance;
        void*          motion;
        RequestState_t request;
        std::uint32_t  soldier;
        bool           noBlend;
        std::uint8_t   command;
    };

    struct Addresses
    {
        std::uintptr_t endStand;
        std::uintptr_t endCrawl;
        std::uintptr_t dropWeapon;
    };

    const std::uint8_t kEndStandPrologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x30,
        0x8B, 0xDA, 0x8B, 0xFA, 0x48, 0x8B, 0xF1, 0x2B, 0x99, 0x90, 0x00, 0x00, 0x00, 0x48, 0xC1, 0xE3, 0x06
    };
    const std::uint8_t kEndCrawlPrologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x40,
        0x8B, 0xFA, 0x8B, 0xF2, 0x48, 0x8B, 0xD9, 0x2B, 0xB9, 0x90, 0x00, 0x00, 0x00, 0x48, 0xC1, 0xE7, 0x06
    };
    const std::uint8_t kDropWeaponPrologue[] = { 0x48, 0x83, 0xEC, 0x48, 0x4C, 0x8B, 0x49, 0x70, 0x8B, 0xCA, 0x49, 0x8B, 0x41, 0x18 };

    State_t      g_OrigEndStand   = nullptr;
    State_t      g_OrigEndCrawl   = nullptr;
    DropWeapon_t g_DropWeapon     = nullptr;
    void*        g_EndStandTarget = nullptr;
    void*        g_EndCrawlTarget = nullptr;

    Addresses AddressesForBuild()
    {
        return { gAddr.HoldupActionImpl_State_EndStandHoldup, gAddr.HoldupActionImpl_State_EndCrawlHoldup, gAddr.HoldupActionImpl_DropWeapon };
    }

    bool ReadWorkSEH(std::uint8_t* action, std::uint32_t slot, Work& out)
    {
        __try
        {
            std::uint8_t* work = *reinterpret_cast<std::uint8_t**>(action + kWorks)
                + static_cast<std::size_t>(slot - *reinterpret_cast<std::uint32_t*>(action + kWorkBase)) * kWorkStride;
            out.stance  = *reinterpret_cast<std::uint8_t**>(work + kWorkStance);
            out.motion  = *reinterpret_cast<void**>(work + kWorkMotion);
            out.soldier = *reinterpret_cast<std::uint32_t*>(work + kWorkSoldier);
            out.noBlend = (work[kWorkFlags] & 1) != 0;
            out.command = work[kWorkCommand];
            out.request = out.motion ? reinterpret_cast<RequestState_t>((*reinterpret_cast<void***>(out.motion))[0x170 / sizeof(void*)]) : nullptr;
            return out.stance && out.request;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool SetStanceSEH(std::uint8_t* stance, const EndClip& clip)
    {
        __try
        {
            auto* bits = reinterpret_cast<std::uint32_t*>(stance + kStanceBits);
            *bits = (*bits & clip.stanceKeep) | clip.stanceSet;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool EnterFreedHimself(std::uint8_t* action, std::uint32_t slot, const EndClip& clip)
    {
        Work work{};
        if (!ReadWorkSEH(action, slot, work) || !HoldupSelfRelease_TakeFreed(work.soldier) || !SetStanceSEH(work.stance, clip))
            return false;
        work.request(work.motion, slot, kBodyLayer, clip.state, work.noBlend ? 0.0f : clip.blend, 0);
        g_DropWeapon(action, work.soldier, slot);
        return true;
    }

    void* VirtualSEH(void* object, std::size_t slot)
    {
        __try
        {
            return object ? (*reinterpret_cast<void***>(object))[slot] : nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    bool CopyQuatSEH(float* to, const float* from)
    {
        __try
        {
            std::memcpy(to, from, sizeof(float) * 4);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint8_t* CharaControlOf(void* motion, std::uint32_t slot)
    {
        auto chara = reinterpret_cast<CharaControl_t>(VirtualSEH(motion, kCharaControlSlot));
        return chara ? chara(motion, slot) : nullptr;
    }

    float TurnAt(const TurnClip& clip, float ratio)   // degrees
    {
        if (ratio <= 0.0f)
            return clip.curve[0];
        if (ratio >= 1.0f)
            return clip.curve[kTurnSteps];
        const float at = ratio * kTurnSteps;
        const int step = static_cast<int>(at);
        return clip.curve[step] + (clip.curve[step + 1] - clip.curve[step]) * (at - step);
    }

    bool SameLength(float length, float frames)   // the layer reports the new clip, in frames or in 60ths of a second
    {
        return std::fabs(length - frames) < frames * 0.05f || std::fabs(length * 60.0f - frames) < frames * 0.05f;
    }

    void StartTurning(std::uint8_t* action, std::uint32_t slot, const TurnClip* clip)
    {
        Work work{};
        if (!ReadWorkSEH(action, slot, work) || work.soldier >= kMaxSoldiers)
            return;
        Turning& turning = g_Turning[work.soldier];
        turning = {};
        const std::uint8_t* chara = clip ? CharaControlOf(work.motion, slot) : nullptr;
        if (chara && CopyQuatSEH(turning.start, reinterpret_cast<const float*>(chara + kCharaRotation)))
            turning.clip = clip;
    }

    void KeepTurning(std::uint8_t* action, std::uint32_t slot, std::uint32_t proc)
    {
        Work work{};
        if (!ReadWorkSEH(action, slot, work) || work.soldier >= kMaxSoldiers || !g_Turning[work.soldier].clip)
            return;
        Turning& turning = g_Turning[work.soldier];
        auto clipTime   = reinterpret_cast<ClipTime_t>(VirtualSEH(work.motion, kClipTimeSlot));
        auto clipLength = reinterpret_cast<ClipTime_t>(VirtualSEH(work.motion, kClipLengthSlot));
        std::uint8_t* chara = CharaControlOf(work.motion, slot);
        if (!clipTime || !clipLength || !chara)
            return;
        const float time = clipTime(work.motion, slot, 0, 0, 1);
        const float length = clipLength(work.motion, slot, 0, 0, 1);
        const bool ours = length > 0.0f && SameLength(length, turning.clip->frames);
        if (!ours)   // before the end clip shows up, or once the next one has: his facing is left as it is
            return;
        turning.ratio = std::fmax(turning.ratio, time / length);
        const float yaw = TurnAt(*turning.clip, turning.ratio) * 0.017453292f;

        const float s = std::sin(yaw * 0.5f);
        const float c = std::cos(yaw * 0.5f);
        const float* q = turning.start;
        const float turned[4] = { c * q[0] + s * q[2], c * q[1] + s * q[3], c * q[2] - s * q[0], c * q[3] - s * q[1] };   // turn about Y, then his start facing
        CopyQuatSEH(reinterpret_cast<float*>(chara + kCharaRotation), turned);
        CopyQuatSEH(reinterpret_cast<float*>(chara + kCharaTurnTarget), turned);   // the held-up look at Snake is still its target: it pulled him back
    }

    void StopTurning(std::uint8_t* action, std::uint32_t slot)
    {
        Work work{};
        if (ReadWorkSEH(action, slot, work) && work.soldier < kMaxSoldiers)
            g_Turning[work.soldier].clip = nullptr;
    }

    void __fastcall hk_State_EndStandHoldup(std::uint8_t* action, std::uint32_t slot, std::uint32_t proc, void* param)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigEndStand, action, slot, proc, param);
        if (proc == kProcEnter)
        {
            Work work{};
            const bool comrade = ReadWorkSEH(action, slot, work) && work.command == kComradeStand;
            if (EnterFreedHimself(action, slot, kStandFreedHimself))
                StartTurning(action, slot, &kStandFreedClip);
            else
            {
                g_OrigEndStand(action, slot, proc, param);
                StartTurning(action, slot, comrade ? &kStandComradeClip : nullptr);
            }
            return;
        }
        g_OrigEndStand(action, slot, proc, param);
        if (proc == kProcExit)
            StopTurning(action, slot);
        else
            KeepTurning(action, slot, proc);
    }

    void __fastcall hk_State_EndCrawlHoldup(std::uint8_t* action, std::uint32_t slot, std::uint32_t proc, void* param)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigEndCrawl, action, slot, proc, param);
        if (proc == kProcEnter)
        {
            Work work{};
            const bool comrade = ReadWorkSEH(action, slot, work) && work.command == kComradeCrawl;
            if (EnterFreedHimself(action, slot, kCrawlFreedHimself))
                StartTurning(action, slot, &kCrawlFreedClip);
            else
            {
                g_OrigEndCrawl(action, slot, proc, param);
                StartTurning(action, slot, comrade ? &kCrawlComradeClip : nullptr);
            }
            return;
        }
        g_OrigEndCrawl(action, slot, proc, param);
        if (proc == kProcExit)
            StopTurning(action, slot);
        else
            KeepTurning(action, slot, proc);
    }
}

bool Install_HoldupActionImpl_State_EndStandHoldup_Hooks()
{
    if (g_EndStandTarget)
        return true;
    const Addresses addresses = AddressesForBuild();
    if (!addresses.endStand)
        return true;

    auto* endStand   = static_cast<std::uint8_t*>(ResolveGameAddress(addresses.endStand));
    auto* endCrawl   = static_cast<std::uint8_t*>(ResolveGameAddress(addresses.endCrawl));
    auto* dropWeapon = static_cast<std::uint8_t*>(ResolveGameAddress(addresses.dropWeapon));
    const HookEntry endStandEntry   = CheckHookEntry(endStand, kEndStandPrologue, sizeof(kEndStandPrologue));
    const HookEntry endCrawlEntry   = CheckHookEntry(endCrawl, kEndCrawlPrologue, sizeof(kEndCrawlPrologue));
    const HookEntry dropWeaponEntry = CheckHookEntry(dropWeapon, kDropWeaponPrologue, sizeof(kDropWeaponPrologue));
    if (endStandEntry == HookEntry::Unknown || endCrawlEntry == HookEntry::Unknown || dropWeaponEntry == HookEntry::Unknown)
    {
        LogError("HoldupSelfRelease: hold-up end code does not match %s, a soldier who frees himself plays the comrade rescue clip", GetGameBuildName(gGameBuild));
        return false;
    }
    if (endStandEntry == HookEntry::ForeignHook)
        LogHookedOnTop("HoldupSelfRelease: HoldupActionImpl::State_EndStandHoldup", endStand);
    if (endCrawlEntry == HookEntry::ForeignHook)
        LogHookedOnTop("HoldupSelfRelease: HoldupActionImpl::State_EndCrawlHoldup", endCrawl);
    if (dropWeaponEntry == HookEntry::ForeignHook)
        LogHookedOnTop("HoldupSelfRelease: HoldupActionImpl::DropWeapon", dropWeapon);
    g_DropWeapon = reinterpret_cast<DropWeapon_t>(dropWeapon);

    if (!CreateAndEnableHook(endStand, reinterpret_cast<void*>(&hk_State_EndStandHoldup), reinterpret_cast<void**>(&g_OrigEndStand)))
    {
        LogError("HoldupSelfRelease: HoldupActionImpl::State_EndStandHoldup hook failed, a soldier who frees himself plays the comrade rescue clip");
        return false;
    }
    g_EndStandTarget = endStand;
    if (!CreateAndEnableHook(endCrawl, reinterpret_cast<void*>(&hk_State_EndCrawlHoldup), reinterpret_cast<void**>(&g_OrigEndCrawl)))
    {
        LogError("HoldupSelfRelease: HoldupActionImpl::State_EndCrawlHoldup hook failed, a soldier who frees himself on the ground plays the comrade rescue clip");
        Uninstall_HoldupActionImpl_State_EndStandHoldup_Hooks();
        return false;
    }
    g_EndCrawlTarget = endCrawl;
    return true;
}

bool Uninstall_HoldupActionImpl_State_EndStandHoldup_Hooks()
{
    if (g_EndCrawlTarget)
    {
        DisableAndRemoveHook(g_EndCrawlTarget);
        g_EndCrawlTarget = nullptr;
        g_OrigEndCrawl = nullptr;
    }
    if (g_EndStandTarget)
    {
        DisableAndRemoveHook(g_EndStandTarget);
        g_EndStandTarget = nullptr;
        g_OrigEndStand = nullptr;
    }
    return true;
}
