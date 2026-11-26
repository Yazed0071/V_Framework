#include <pch.h>
#include "HeliSoundController.h"

#include <Windows.h>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "LuaBroadcaster.h"
#include "log.h"
#include "MissionCodeGuard.h"
#include "FeatureSwitch.h"
#include "PlayerAlertPhase.h"
#include "HeliKey.h"
#include "SupportHeliWeapon.h"
#include "../soldier/SoldierFaction.h"
#include "../ui/MotherBaseMapCommonData_IsDangerLandingZone.h"
#include "../../lua/LuaApi.h"

namespace
{
    using Update_t       = void(__fastcall*)(void* this_);
    using CallVoice_t    = void(__fastcall*)(void* this_, std::uint32_t slot, std::uint32_t voiceId, std::uint32_t voiceType, std::uint32_t param4);
    using FlightUpdate_t = void(__fastcall*)(void* this_);

    static Update_t           g_OrigUpdate       = nullptr;
    static CallVoice_t        g_CallVoice        = nullptr;
    static CallVoice_t        g_OrigCallVoice    = nullptr;
    static FlightUpdate_t     g_OrigFlightUpdate = nullptr;
    static bool               g_Installed     = false;
    static std::atomic<void*> g_Controller{ nullptr };

    static const int kMaxSlots = 16;

    struct VoiceSnap   { std::uint32_t voiceId; std::uint32_t voiceType; std::uint32_t param4; };
    struct VoiceFinish { std::uint32_t voiceId; std::uint32_t slot; std::uint32_t voiceType; std::uint32_t param4; };

    static const int kHeliTypeCount = HeliKey::kLastType - HeliKey::kFirstType + 1;

    static VoiceSnap g_Snap[kHeliTypeCount][kMaxSlots] = {};   // one controller per heli type

    static VoiceFinish g_PendingFinish[kMaxSlots] = {};
    static int         g_PendingFinishCount = 0;

    constexpr float         kKmhToMetresPerSec = 0.2777778f;

    struct TargetSpeed
    {
        std::atomic<bool> pending;
        float             metresPerSec;
        float             changeTime;
    };

    static TargetSpeed              g_TargetSpeed[HeliKey::kCount];
    static std::atomic<int>         g_TargetSpeedPendingCount{ 0 };
    static std::atomic<bool>        g_PilotAiVoiceOff[HeliKey::kCount];

    using GetQuarkSystemTable_t    = std::uint8_t*(__fastcall*)();
    using GetAttackParam_t         = const std::uint8_t*(__fastcall*)(void* attackParams, std::uint32_t attackId);
    using GetPlayerPosition_t      = const float*(__fastcall*)(void* players, void* scratch, std::uint32_t playerIndex);
    using HasLandingZone_t         = bool(__fastcall*)(void* route);
    using GetLandingZonePosition_t = void(__fastcall*)(void* route, float* position);
    using GetCommonDataManager_t   = const std::uint8_t*(__fastcall*)();

    constexpr std::uint32_t kPilotVoice = 0;   // the cabin voice event, SHV lines
    constexpr std::uint32_t kPilotRadio = 1;   // the radio event, SHR lines

    constexpr std::uint32_t kGrazedLine        = 0x5845A905u;  // SHV0010
    constexpr std::uint32_t kGunfireLine       = 0x5545A44Cu;  // SHV0020 "Dammit!"
    constexpr std::uint32_t kHeavyGunfireLine  = 0x5645A63Fu;  // SHV0030 "Not good."
    constexpr std::uint32_t kBlastLine         = 0x5345A166u;  // SHV0040
    constexpr std::uint32_t kHeavyBlastLine    = 0x5445A2D9u;  // SHV0050 "Son of a-!"
    constexpr std::uint32_t kLosingControlLine = 0x51459E00u;  // SHV0060 "Holy-! I'm losing control!"
    constexpr std::uint32_t kClimbingLine      = 0x5D47EF9Bu;  // SHV0100 "Climbing!" / "Pulling her up! Try not to black out!"
    constexpr std::uint32_t kClosingDoorLine   = 0x5C47EE28u;  // SHV0110
    constexpr std::uint32_t kOpeningDoorLine   = 0x5F47F2E1u;  // SHV0120
    constexpr std::uint32_t kForcedLandingLine = 0x1079B0FCu;  // SHR0060 "Making forced landing. Stand by.", his door line in Alert
    constexpr std::uint32_t kDeadAheadLine     = 0x4B4117A0u;  // SHV0200 "Contact dead ahead!"
    constexpr std::uint32_t kClockLines[12]    = { 0x4C411911u, 0x4B4117A1u, 0x4B4117A2u, 0x4B4117A3u,    // SHV0212, then SHV0201..SHV0211:
                                                   0x4B4117A4u, 0x4B4117A5u, 0x4B4117A6u, 0x4B4117A7u,    // twelve o'clock, then one to eleven
                                                   0x4B4117A8u, 0x4B4117A9u, 0x4C411913u, 0x4C411912u };
    constexpr std::uint32_t kCoverFireLine     = 0x947CBF1Fu;  // SHR0100 "Cover fire! In hot!"
    constexpr std::uint32_t kKillZoneLine      = 0x9B7CCA04u;  // SHR0190 "In hot! Stay out of the kill zone!"
    constexpr std::uint32_t kMoveLine          = 0x907CB8D3u;  // SHR0140 "Unable to land. I need you to move."
    constexpr std::uint32_t kObstructionLine   = 0x8F7CB760u;  // SHR0150
    constexpr std::uint32_t kOtherLzLine       = 0x927CBC39u;  // SHR0160 "Obstruction at the LZ is preventing me from landing. Moving to a different LZ."
    constexpr std::uint32_t kTakenFireLine     = 0x1B7544DFu;  // SHR0290 "I have taken fire!"
    constexpr std::uint32_t kHeavyDamageLine   = 0x287797CDu;  // SHR0300 "She's taken heavy damage!"
    constexpr std::uint32_t kCoverMeLine       = 0x2777965Au;  // SHR0310 "Gimme some cover fire!"
    constexpr std::uint32_t kAbortLine         = 0x50435E36u;  // SHV0310 "Aborting the mission, Boss? We're leaving the mission area."
    constexpr std::uint32_t kAbortCombatLine   = 0x50435E37u;  // SHV0311 "Boss, is it mission abort?! Departing the mission area now!"
    constexpr std::uint32_t kDepartingAoLine   = 0x4F435C43u;  // SHV0320 "Departing AO."

    constexpr std::size_t   kQuarkApplication          = 0x98;
    constexpr std::size_t   kApplicationScriptVars     = 0x10;
    constexpr std::size_t   kApplicationPlayers        = 0x1A0;
    constexpr std::size_t   kScriptVarsPlayerVehicle   = 0x110;  // vars.playerVehicleGameObjectId
    constexpr std::size_t   kGetPlayerPositionSlot     = 0x10 / sizeof(void*);
    constexpr std::size_t   kControllerHeli            = 0x48;
    constexpr std::size_t   kControllerWorks           = 0x58;
    constexpr std::size_t   kWorkSize                  = 0x260;
    constexpr std::size_t   kWorkLine                  = 0x250;  // the line being said or queued, what the busy check (vtable +0x90) reads
    constexpr std::size_t   kHeliTransforms            = 0x28;
    constexpr std::size_t   kTransformsPositions       = 0x20;   // x, y, z, yaw per heli
    constexpr std::size_t   kTransformsCount           = 0x40;
    constexpr std::size_t   kHeliStates                = 0x60;   // +8 -> 0x18 per heli: flags, then the landing phase at +0x14
    constexpr std::size_t   kStateRecords              = 0x08;
    constexpr std::size_t   kStateSize                 = 0x18;
    constexpr std::size_t   kStateLandingPhase         = 0x14;
    constexpr std::uint64_t kStateInAssault            = 1ull << 41;   // SupportCombatAiImpl::Wake sets it, Sleep clears it
    constexpr std::uint8_t  kPhaseWaitingAtLandingZone = 3;      // StepWaitForPlayerToApproach sets it
    constexpr std::uint8_t  kPhaseLanded               = 5;      // StepWaitForPlayerToGetOn sets it
    constexpr std::uint8_t  kPhaseWithdrawing          = 7;      // both StepWithdrawAt* set it as he lifts off
    constexpr std::size_t   kHandlerHeli               = 0x40;
    constexpr std::size_t   kHandlerAttackParams       = 0x50;
    constexpr std::size_t   kAttackParamFlags          = 0x14;
    constexpr std::uint8_t  kAttackParamBlast          = 0x20;   // ProcessQueuedDamages gives these the blast hit sound
    constexpr std::uint16_t kBlastWithBulletSoundA     = 0x12D;  // ...except these two
    constexpr std::uint16_t kBlastWithBulletSoundB     = 0x12E;
    constexpr std::uint16_t kTypeShift                 = 9;
    constexpr std::uint16_t kIndexMask                 = 0x1FF;
    constexpr std::size_t   kHeliRoute                 = 0x90;   // vt+0x40 has an LZ, vt+0x98 where it is
    constexpr std::size_t   kHasLandingZoneVtblIndex   = 0x40 / 8;
    constexpr std::size_t   kLandingZoneAtVtblIndex    = 0x98 / 8;
    constexpr float         kLandingZoneHalfWidth      = 7.5f;   // LandingZoneAiImpl::IsObstructionAtLandingZone's box: 15 x 15 m, 4 m up from the LZ
    constexpr float         kLandingZoneHeight         = 4.0f;
    constexpr float         kLandingZoneBelow          = 2.0f;   // the game tests his whole body; his feet can sit a little under the LZ point
    constexpr std::size_t   kRouteLzTable              = 0x18;   // the LZ table the iDroid call writes to
    constexpr std::size_t   kLzEntries                 = 0x10;   // 0x30 per LZ, position first
    constexpr std::size_t   kLzEntrySize               = 0x30;
    constexpr std::size_t   kLzLoaded                  = 0x10;
    constexpr std::size_t   kLzName                    = 0x18;
    constexpr std::size_t   kLzFlags                   = 0x29;
    constexpr std::uint8_t  kLzEnabled                 = 0x01;   // EnableLandingZone / DisableLandingZone, what the iDroid map shows
    constexpr std::size_t   kLzRequested               = 0xC24;
    constexpr std::size_t   kLzCurrent                 = 0xC28;
    constexpr std::size_t   kLzCount                   = 0xC2C;
    constexpr std::size_t   kLzRequest                 = 0xC2D;
    constexpr std::uint8_t  kRequestFlyToLz            = 1;      // StepWaitForPlayerToApproach takes it the next frame and flies to the requested LZ
    constexpr int           kMaxLzs                    = 0x40;
    constexpr std::size_t   kCommonDataMapData         = 0x78;   // UiCommonDataManager -> MotherBaseMapCommonData
    constexpr std::size_t   kMapDataAssaultLzCount     = 0x1298; // byte count of RegisterDangerLandingZoneTable's LZs, the ones inside bases
    constexpr std::size_t   kMapDataAssaultLzNames     = 0x129C;
    constexpr int           kMaxAssaultLzs             = 0x40;
    constexpr float         kOtherLzReach              = 500.0f;
    constexpr int           kWarningsBeforeMoving      = 2;      // SHR0150 comes every 15 s, the third one turns into SHR0160
    constexpr float         kCoverMeReach              = 150.0f;
    constexpr float         kHitOffCentre              = 1.0f;
    constexpr float         kFullTurn                  = 6.28318531f;
    constexpr std::uint64_t kDamageLineGapMs           = 6000;
    constexpr std::uint64_t kClockLineGapMs            = 15000;
    constexpr std::uint64_t kCoverMeGapMs              = 30000;
    constexpr std::uint64_t kStandbyRecentMs           = 10000;
    constexpr std::uint64_t kClimbWindowMs             = 6000;   // after his own takeoff line
    constexpr std::uint64_t kDepartingAoWindowMs       = 3000;
    constexpr std::uint64_t kJustBoardedMs             = 3000;   // vars already count Snake aboard while he climbs in
    constexpr float         kAbortLineSeconds          = 3.76f;  // length of the SHV0310 take
    constexpr float         kAbortCombatLineSeconds    = 4.01f;  // length of the SHV0311 take
    constexpr float         kRadioAfterPilotGap        = 0.5f;   // vanilla's "short" delay before a common radio
    constexpr std::uint16_t kFreeRoamMissionSeries     = 3;      // mission codes 30000-39999
    constexpr ULONGLONG     kRadioHookCheckIntervalMs  = 1000;
    constexpr const char*   kOriginalPlayCommonRadio   = "_V_PlayCommonRadio";
    constexpr const char*   kRogerRadio                = "f1000_rtrg0060";
    constexpr const char*   kDispatchingChopperRadio   = "f1000_rtrg1810";
    constexpr const char*   kChopperShotDownRadio      = "f1000_rtrg1940";
    constexpr const char*   kBackupChopperRadio        = "f1000_rtrg1910";
    constexpr const char*   kLostAnotherChopperRadio   = "f1000_rtrg1920";
    constexpr const char*   kAnotherChopperDownRadio   = "f1000_rtrg1930";
    constexpr const char*   kModerateLzRadio           = "f1000_rtrg1820";   // the hot-zone call reply without "that area is a hot zone!"

    struct BlockedLz
    {
        std::uint32_t trip;          // g_LandingTrips when this run of blocked LZs started
        int           warnedLz;
        int           warnings;
        int           movedToLz;
        std::uint64_t triedLzBits;   // one bit per LZ index found blocked on this run
    };

    static std::atomic<std::uint64_t> g_StandbyMs[kMaxSlots];
    static std::atomic<std::uint64_t> g_TakeoffMs[kMaxSlots];
    static std::atomic<std::uint64_t> g_LeavingAoMs[kMaxSlots];
    static std::atomic<std::uint8_t>  g_LandingPhase[kMaxSlots];
    static std::atomic<std::uint64_t> g_SnakeOutsideMs[kMaxSlots];
    static std::atomic<std::uint32_t> g_LandingTrips[kMaxSlots];   // +1 every time he lands or lifts off
    static BlockedLz                  g_BlockedLz[kMaxSlots] = {};
    static std::atomic<std::uint64_t> g_DamageLineMs{ 0 };
    static std::atomic<std::uint64_t> g_ClockLineMs{ 0 };
    static std::atomic<std::uint64_t> g_CoverMeMs{ 0 };
    static std::atomic<std::uint32_t> g_ChatterLine[kMaxSlots];    // the damage line we last started on each heli
    static std::atomic<std::uint32_t> g_LastCabinLine[kMaxSlots];
    static std::atomic<ULONGLONG>     g_NextRadioHookCheck{ 0 };
    static std::atomic<int>           g_HelisLostThisMission{ 0 };

    static std::uint32_t HeliTypeOf(void* heli)   // 0 when it isn't a heli
    {
        __try
        {
            const std::uint32_t type = heli ? *reinterpret_cast<std::uint16_t*>(static_cast<std::uint8_t*>(heli) + 0x130) : 0;   // game object type of the heli
            return HeliKey::IsHeliType(type) ? type : 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static void ApplyPendingTargetSpeeds(void* flightController)
    {
        if (g_TargetSpeedPendingCount.load(std::memory_order_relaxed) <= 0)
            return;
        __try
        {
            auto* self = static_cast<std::uint8_t*>(flightController);
            auto* heli = *reinterpret_cast<std::uint8_t**>(self + 0x48);
            const std::uint32_t type = HeliTypeOf(heli);
            if (!type)
                return;
            const std::uint32_t count = *reinterpret_cast<std::uint32_t*>(*reinterpret_cast<std::uint8_t**>(heli + 0x28) + 0x40);
            auto* basicFlight = *reinterpret_cast<std::uint8_t**>(self + 0x50);
            auto* works = basicFlight ? *reinterpret_cast<std::uint8_t**>(basicFlight + 0x40) : nullptr;
            if (!works)
                return;
            for (std::uint32_t i = 0; i < count && i <= 0x1FFu; ++i)
            {
                TargetSpeed& wanted = g_TargetSpeed[HeliKey::Of(type, i)];
                if (!wanted.pending.exchange(false, std::memory_order_acquire))
                    continue;
                g_TargetSpeedPendingCount.fetch_sub(1, std::memory_order_relaxed);
                auto* work = works + static_cast<std::size_t>(i) * 0x24;   // 0x24 per heli: speed now, ramp from, ramp to, ramp time done, ramp time, ... flags +0x21
                *reinterpret_cast<float*>(work + 0x04) = *reinterpret_cast<float*>(work);
                *reinterpret_cast<float*>(work + 0x08) = wanted.metresPerSec;
                *reinterpret_cast<float*>(work + 0x0C) = 0.0f;
                *reinterpret_cast<float*>(work + 0x10) = wanted.changeTime;
                work[0x21] = static_cast<std::uint8_t>(work[0x21] & ~1);   // ramp not finished
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    static bool PilotAiVoiceAllowed(void* soundController, std::uint32_t heliIndex)
    {
        if (heliIndex > 0x1FFu)
            return true;
        void* heli = nullptr;
        __try
        {
            heli = *reinterpret_cast<void**>(static_cast<std::uint8_t*>(soundController) + 0x48);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return true;
        }
        const std::uint32_t type = HeliTypeOf(heli);
        return !type || !g_PilotAiVoiceOff[HeliKey::Of(type, heliIndex)].load(std::memory_order_relaxed);
    }

    static bool IsSupportHeliController(void* soundController)
    {
        void* heli = nullptr;
        __try
        {
            heli = *reinterpret_cast<void**>(static_cast<std::uint8_t*>(soundController) + 0x48);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        return HeliTypeOf(heli) == HeliKey::kFirstType;
    }

    static std::uint8_t* Application()
    {
        const auto quarkTable = reinterpret_cast<GetQuarkSystemTable_t>(ResolveGameAddress(gAddr.Fox_GetQuarkSystemTable));
        std::uint8_t* table = quarkTable ? quarkTable() : nullptr;
        return table ? *reinterpret_cast<std::uint8_t**>(table + kQuarkApplication) : nullptr;
    }

    static bool SnakeAboard(std::uint32_t heliType, std::uint32_t heliIndex)
    {
        __try
        {
            const std::uint8_t* application = Application();
            const std::uint8_t* scriptVars = application ? *reinterpret_cast<std::uint8_t* const*>(application + kApplicationScriptVars) : nullptr;
            if (!scriptVars)
                return false;
            const std::uint16_t vehicle = *reinterpret_cast<const std::uint16_t*>(scriptVars + kScriptVarsPlayerVehicle);
            return (vehicle >> kTypeShift) == heliType && (vehicle & kIndexMask) == heliIndex;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static bool SnakePosition(float* out)
    {
        alignas(16) float scratch[4] = {};
        __try
        {
            std::uint8_t* application = Application();
            void* players = application ? *reinterpret_cast<void**>(application + kApplicationPlayers) : nullptr;
            if (!players)
                return false;
            const auto getPosition = (*reinterpret_cast<GetPlayerPosition_t* const*>(players))[kGetPlayerPositionSlot];
            const float* at = getPosition(players, scratch, 0);
            if (!at)
                return false;
            out[0] = at[0];
            out[1] = at[1];
            out[2] = at[2];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static bool HeliTransform(const void* heli, std::uint32_t heliIndex, float* out)   // x, y, z, yaw
    {
        __try
        {
            const std::uint8_t* transforms = *reinterpret_cast<const std::uint8_t* const*>(static_cast<const std::uint8_t*>(heli) + kHeliTransforms);
            const float* at = reinterpret_cast<const float*>(*reinterpret_cast<const std::uint8_t* const*>(transforms + kTransformsPositions)
                + static_cast<std::size_t>(heliIndex) * 0x10);
            out[0] = at[0];
            out[1] = at[1];
            out[2] = at[2];
            out[3] = at[3];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static bool PilotBusy(void* controller, std::uint32_t slot)
    {
        __try
        {
            const std::uint8_t* works = *reinterpret_cast<const std::uint8_t* const*>(static_cast<const std::uint8_t*>(controller) + kControllerWorks);
            return *reinterpret_cast<const std::uint32_t*>(works + static_cast<std::size_t>(slot) * kWorkSize + kWorkLine) != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return true;
        }
    }

    static bool PilotBusyBeyondChatter(void* controller, std::uint32_t slot)   // his damage lines rank lowest: our other lines cut one off
    {
        __try
        {
            const std::uint8_t* works = *reinterpret_cast<const std::uint8_t* const*>(static_cast<const std::uint8_t*>(controller) + kControllerWorks);
            const std::uint32_t line = *reinterpret_cast<const std::uint32_t*>(works + static_cast<std::size_t>(slot) * kWorkSize + kWorkLine);
            return line != 0 && line != g_ChatterLine[slot].load(std::memory_order_relaxed);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return true;
        }
    }

    static bool LiftsOffForTheAcc(std::uint32_t slot)   // taxi rides never reach the lift-off phase, only leaving does
    {
        const std::uint16_t mission = MissionCodeGuard::GetCurrentMissionCode();
        return mission / 10000 == kFreeRoamMissionSeries && SnakeAboard(HeliKey::kFirstType, slot);
    }

    static void NoteLandingPhase(void* controller)   // waiting at an LZ makes the next "Cover fire!" a takeoff from it; withdrawing is a takeoff
    {
        const std::uint64_t now = GetTickCount64();
        __try
        {
            const std::uint8_t* heli = *reinterpret_cast<const std::uint8_t* const*>(static_cast<const std::uint8_t*>(controller) + kControllerHeli);
            const std::uint8_t* transforms = *reinterpret_cast<const std::uint8_t* const*>(heli + kHeliTransforms);
            const std::uint8_t* states = *reinterpret_cast<const std::uint8_t* const*>(
                *reinterpret_cast<const std::uint8_t* const*>(heli + kHeliStates) + kStateRecords);
            std::uint32_t count = *reinterpret_cast<const std::uint32_t*>(transforms + kTransformsCount);
            if (count > static_cast<std::uint32_t>(kMaxSlots))
                count = kMaxSlots;
            for (std::uint32_t slot = 0; slot < count; ++slot)
            {
                const std::uint8_t phase = states[slot * kStateSize + kStateLandingPhase];
                const std::uint8_t lastPhase = g_LandingPhase[slot].load(std::memory_order_relaxed);
                if (phase == kPhaseWaitingAtLandingZone)
                    g_StandbyMs[slot].store(now, std::memory_order_relaxed);
                if (phase == kPhaseWithdrawing && lastPhase != kPhaseWithdrawing)
                    g_TakeoffMs[slot].store(now, std::memory_order_relaxed);
                if (phase == kPhaseWithdrawing && lastPhase != kPhaseWithdrawing && LiftsOffForTheAcc(slot))
                    g_LeavingAoMs[slot].store(now, std::memory_order_relaxed);
                if ((phase == kPhaseLanded || phase == kPhaseWithdrawing) && lastPhase != phase)
                    g_LandingTrips[slot].fetch_add(1, std::memory_order_relaxed);
                g_LandingPhase[slot].store(phase, std::memory_order_relaxed);
                if (!SnakeAboard(HeliKey::kFirstType, slot))
                    g_SnakeOutsideMs[slot].store(now, std::memory_order_relaxed);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    static void SayDepartingAo(void* controller)   // runs before SayClimbing, so in Alert "Climbing!" waits for this line
    {
        const std::uint64_t now = GetTickCount64();
        for (std::uint32_t slot = 0; slot < static_cast<std::uint32_t>(kMaxSlots); ++slot)
        {
            const std::uint64_t liftOff = g_LeavingAoMs[slot].load(std::memory_order_relaxed);
            if (liftOff == 0)
                continue;
            if (now - liftOff > kDepartingAoWindowMs || !SnakeAboard(HeliKey::kFirstType, slot))
            {
                g_LeavingAoMs[slot].store(0, std::memory_order_relaxed);
                continue;
            }
            if (PilotBusyBeyondChatter(controller, slot))
                continue;
            g_LeavingAoMs[slot].store(0, std::memory_order_relaxed);
            if (g_PilotAiVoiceOff[HeliKey::Of(HeliKey::kFirstType, slot)].load(std::memory_order_relaxed))
                continue;
            const bool said = Play_PilotCallVoice(kDepartingAoLine, slot, kPilotVoice, 0);
        }
    }

    static void SayClimbing(void* controller)   // lifting off with Snake aboard in Alert
    {
        const std::uint64_t now = GetTickCount64();
        for (std::uint32_t slot = 0; slot < static_cast<std::uint32_t>(kMaxSlots); ++slot)
        {
            const std::uint64_t takeoff = g_TakeoffMs[slot].load(std::memory_order_relaxed);
            if (takeoff == 0)
                continue;
            if (now - takeoff > kClimbWindowMs || !SnakeAboard(HeliKey::kFirstType, slot)
                || PlayerAlertPhase::Read() != PlayerAlertPhase::kAlert)
            {
                g_TakeoffMs[slot].store(0, std::memory_order_relaxed);
                continue;
            }
            if (PilotBusyBeyondChatter(controller, slot))
                continue;
            g_TakeoffMs[slot].store(0, std::memory_order_relaxed);
            const bool said = Play_PilotCallVoice(kClimbingLine, slot, kPilotVoice, 0);
        }
    }

    static bool HeliInAssault(const void* heli, std::uint32_t heliIndex)
    {
        __try
        {
            const std::uint8_t* states = *reinterpret_cast<const std::uint8_t* const*>(
                *reinterpret_cast<const std::uint8_t* const*>(static_cast<const std::uint8_t*>(heli) + kHeliStates) + kStateRecords);
            return (*reinterpret_cast<const std::uint64_t*>(states + static_cast<std::size_t>(heliIndex) * kStateSize) & kStateInAssault) != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static bool LandingZoneOf(const void* heli, float* out)
    {
        alignas(16) float at[4] = {};
        __try
        {
            void* route = *reinterpret_cast<void* const*>(static_cast<const std::uint8_t*>(heli) + kHeliRoute);
            if (!route)
                return false;
            void** vtbl = *reinterpret_cast<void***>(route);
            if (!reinterpret_cast<HasLandingZone_t>(vtbl[kHasLandingZoneVtblIndex])(route))
                return false;
            reinterpret_cast<GetLandingZonePosition_t>(vtbl[kLandingZoneAtVtblIndex])(route, at);
            out[0] = at[0];
            out[1] = at[1];
            out[2] = at[2];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static bool SnakeInLandingZone(void* controller, std::uint32_t slot)   // inside the box the obstruction check tests, so the obstruction includes him
    {
        void* heli = nullptr;
        __try
        {
            heli = *reinterpret_cast<void**>(static_cast<std::uint8_t*>(controller) + kControllerHeli);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        float zone[3] = {};
        float snake[3] = {};
        if (!heli || SnakeAboard(HeliKey::kFirstType, slot) || !LandingZoneOf(heli, zone) || !SnakePosition(snake))
            return false;
        const float up = snake[1] - zone[1];
        return std::fabs(snake[0] - zone[0]) <= kLandingZoneHalfWidth && std::fabs(snake[2] - zone[2]) <= kLandingZoneHalfWidth
            && up >= -kLandingZoneBelow && up <= kLandingZoneHeight;
    }

    static bool ScriptCallsPequodToItsOwnLz()
    {
        std::uint32_t mission = MissionCodeGuard::GetCurrentMissionCode();
        if (mission / 1000 % 10 == 1)   // Extreme and Subsistence run the base mission's script
            mission -= 1000;
        switch (mission)
        {
        case 10020: case 10040: case 10050: case 10070: case 10081:
        case 10100: case 10110: case 10120: case 10140: case 10260:
            return true;
        default:
            return false;
        }
    }

    static std::uint8_t* LzTableOf(void* controller)
    {
        __try
        {
            const std::uint8_t* heli = *reinterpret_cast<const std::uint8_t* const*>(static_cast<const std::uint8_t*>(controller) + kControllerHeli);
            const std::uint8_t* route = heli ? *reinterpret_cast<const std::uint8_t* const*>(heli + kHeliRoute) : nullptr;
            return route ? *reinterpret_cast<std::uint8_t* const*>(route + kRouteLzTable) : nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    static bool ReadLzIndices(const std::uint8_t* lzTable, int& current, int& count)
    {
        __try
        {
            current = *reinterpret_cast<const int*>(lzTable + kLzCurrent);
            count = lzTable[kLzCount];
            if (count > kMaxLzs)
                count = kMaxLzs;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static int ReadAssaultLzNames(std::uint32_t* names)
    {
        const auto getCommonDataManager = reinterpret_cast<GetCommonDataManager_t>(ResolveGameAddress(gAddr.Ui_UiCommonDataManagerGetInstance));
        if (!getCommonDataManager)
            return 0;
        __try
        {
            const std::uint8_t* manager = getCommonDataManager();
            const std::uint8_t* mapData = manager ? *reinterpret_cast<const std::uint8_t* const*>(manager + kCommonDataMapData) : nullptr;
            if (!mapData)
                return 0;
            int count = mapData[kMapDataAssaultLzCount];
            if (count > kMaxAssaultLzs)
                count = kMaxAssaultLzs;
            std::memcpy(names, mapData + kMapDataAssaultLzNames, static_cast<std::size_t>(count) * sizeof(std::uint32_t));
            return count;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static bool IsAssaultLz(std::uint32_t name, const std::uint32_t* assaultNames, int assaultCount)
    {
        for (int i = 0; i < assaultCount; ++i)
        {
            if (assaultNames[i] == name)
                return true;
        }
        return false;
    }

    static int NearestFreeLz(const std::uint8_t* lzTable, int blocked, int count, std::uint64_t tried, float& distance)
    {
        std::uint32_t assaultNames[kMaxAssaultLzs];
        const int assaultCount = ReadAssaultLzNames(assaultNames);
        int nearest = -1;
        float nearestSquared = kOtherLzReach * kOtherLzReach;
        __try
        {
            const float* from = reinterpret_cast<const float*>(lzTable + kLzEntries + static_cast<std::size_t>(blocked) * kLzEntrySize);
            for (int i = 0; i < count; ++i)
            {
                if ((tried >> i) & 1)
                    continue;
                const std::uint8_t* lz = lzTable + kLzEntries + static_cast<std::size_t>(i) * kLzEntrySize;
                if (*reinterpret_cast<const std::uint64_t*>(lz + kLzLoaded) == 0 || (lz[kLzFlags] & kLzEnabled) == 0)
                    continue;
                if (IsAssaultLz(*reinterpret_cast<const std::uint32_t*>(lz + kLzName), assaultNames, assaultCount))
                    continue;
                const float* at = reinterpret_cast<const float*>(lz);
                const float dx = at[0] - from[0];
                const float dy = at[1] - from[1];
                const float dz = at[2] - from[2];
                const float squared = dx * dx + dy * dy + dz * dz;
                if (squared < nearestSquared)
                {
                    nearestSquared = squared;
                    nearest = i;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
        distance = std::sqrt(nearestSquared);
        return nearest;
    }

    static bool RequestLz(std::uint8_t* lzTable, int lz)   // the same two writes the iDroid call makes
    {
        __try
        {
            *reinterpret_cast<int*>(lzTable + kLzRequested) = lz;
            lzTable[kLzRequest] = kRequestFlyToLz;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static bool MoveToOtherLz(void* controller, std::uint32_t slot)
    {
        if (slot >= static_cast<std::uint32_t>(kMaxSlots) || ScriptCallsPequodToItsOwnLz())
            return false;
        std::uint8_t* lzTable = LzTableOf(controller);
        int current = -1;
        int count = 0;
        if (!lzTable || !ReadLzIndices(lzTable, current, count) || current < 0 || current >= count)
            return false;

        BlockedLz& blocked = g_BlockedLz[slot];
        const std::uint32_t trip = g_LandingTrips[slot].load(std::memory_order_relaxed);
        if (blocked.trip != trip || (current != blocked.warnedLz && current != blocked.movedToLz))
            blocked = { trip, current, 0, -1, 0 };
        else if (current != blocked.warnedLz)
        {
            blocked.warnedLz = current;
            blocked.warnings = 0;
        }
        if (blocked.warnings < kWarningsBeforeMoving)
        {
            ++blocked.warnings;
            return false;
        }

        blocked.triedLzBits |= 1ull << current;
        float distance = 0.0f;
        const int next = NearestFreeLz(lzTable, current, count, blocked.triedLzBits, distance);
        if (next < 0 || !RequestLz(lzTable, next))
        {
            return false;
        }
        blocked.movedToLz = next;
        blocked.warnedLz = -1;   // calling him back here is a new call: two warnings again
        blocked.warnings = 0;
        return true;
    }

    static std::uint32_t SwapPilotLine(void* controller, std::uint32_t slot, std::uint32_t voiceId)
    {
        const bool extraLines = FeatureSwitch::IsOn(FeatureSwitch::Feature::PequodExtraLines);
        if (extraLines && voiceId == kCoverFireLine && slot < static_cast<std::uint32_t>(kMaxSlots)
            && GetTickCount64() - g_StandbyMs[slot].load(std::memory_order_relaxed) < kStandbyRecentMs && (GetTickCount64() & 1))
            return kKillZoneLine;   // user: mixed with the vanilla line, not in place of it
        if (voiceId == kObstructionLine && SnakeInLandingZone(controller, slot))
            return extraLines ? kMoveLine : voiceId;   // Snake blocking it never moves Pequod, lines or not
        if (voiceId == kObstructionLine && FeatureSwitch::IsOn(FeatureSwitch::Feature::PequodLzMove) && MoveToOtherLz(controller, slot))
            return extraLines ? kOtherLzLine : voiceId;
        return voiceId;
    }

    static std::uint32_t PickCabinLine(std::uint32_t heliIndex, bool blast, float life, std::uint64_t now)
    {
        const std::uint32_t mild   = blast ? kBlastLine : kGrazedLine;
        const std::uint32_t medium = blast ? kHeavyBlastLine : kGunfireLine;
        const std::uint32_t heavy  = blast ? kLosingControlLine : kHeavyGunfireLine;
        const bool low = life <= 1.0f / 3.0f;
        const std::uint32_t first  = low ? medium : mild;
        const std::uint32_t second = low ? heavy : medium;
        const std::uint32_t last = g_LastCabinLine[heliIndex].load(std::memory_order_relaxed);
        if (last == first)
            return second;
        if (last == second)
            return first;
        return (now & 1) ? second : first;
    }

    static int ClockOfHit(const void* heli, std::uint32_t heliIndex, const float* hit)   // 0 for twelve o'clock, -1 when the hit tells nothing
    {
        float at[4] = {};
        if (!HeliTransform(heli, heliIndex, at))
            return -1;
        const float dx = hit[0] - at[0];
        const float dz = hit[2] - at[2];
        const float length = std::sqrt(dx * dx + dz * dz);
        if (length < kHitOffCentre)
            return -1;
        const float forwardX = std::sin(at[3]);
        const float forwardZ = std::cos(at[3]);
        const float toX = dx / length;
        const float toZ = dz / length;
        float dot = forwardX * toX + forwardZ * toZ;
        dot = dot > 1.0f ? 1.0f : (dot < -1.0f ? -1.0f : dot);
        float angle = std::acos(dot);
        if (toZ * forwardX - toX * forwardZ < 0.0f)   // the sign NoticeRediscoveryAi uses for its clock callouts
            angle = kFullTurn - angle;
        return static_cast<int>(std::floor(angle / kFullTurn * 12.0f + 0.5f)) % 12;
    }

    static int CollectVoiceFinishes(void* this_, VoiceFinish* out, int maxOut)
    {
        int n = 0;
        __try
        {
            char* self = reinterpret_cast<char*>(this_);
            char* base = *reinterpret_cast<char**>(self + 0x58);
            char* p48  = *reinterpret_cast<char**>(self + 0x48);
            if (!base || !p48)
                return 0;

            const std::uint32_t heliType = HeliTypeOf(p48);
            if (!heliType)
                return 0;
            VoiceSnap* snap = g_Snap[heliType - HeliKey::kFirstType];

            char* cfg = *reinterpret_cast<char**>(p48 + 0x28);
            if (!cfg)
                return 0;

            std::uint32_t count = *reinterpret_cast<std::uint32_t*>(cfg + 0x40);
            if (count > static_cast<std::uint32_t>(kMaxSlots))
                count = kMaxSlots;

            for (std::uint32_t s = 0; s < count; ++s)
            {
                char* work = base + static_cast<size_t>(s) * 0x260;
                std::uint32_t cur = *reinterpret_cast<std::uint32_t*>(work + 0x250);

                if (cur != 0)
                {
                    if (snap[s].voiceId != 0 && snap[s].voiceId != cur && n < maxOut)   // a newer line cut it off
                    {
                        out[n].voiceId   = snap[s].voiceId;
                        out[n].slot      = s;
                        out[n].voiceType = snap[s].voiceType;
                        out[n].param4    = snap[s].param4;
                        ++n;
                        snap[s].voiceId = 0;
                    }
                    if (snap[s].voiceId == 0)
                    {
                        snap[s].voiceId   = cur;
                        snap[s].voiceType = (static_cast<std::uint8_t>(work[0x258]) >> 2) & 3;
                        snap[s].param4    = *reinterpret_cast<std::uint32_t*>(work + 0x254);
                    }
                }
                else if (snap[s].voiceId != 0)
                {
                    if (n < maxOut)
                    {
                        out[n].voiceId   = snap[s].voiceId;
                        out[n].slot      = s;
                        out[n].voiceType = snap[s].voiceType;
                        out[n].param4    = snap[s].param4;
                        ++n;
                    }
                    snap[s].voiceId = 0;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
        return n;
    }

    static void FlushPendingFinishes()
    {
        const int n = g_PendingFinishCount;
        g_PendingFinishCount = 0;
        for (int i = 0; i < n; ++i)
            V_FrameWork::EmitMessage("Radio", "HeliFinish", g_PendingFinish[i].voiceId, g_PendingFinish[i].param4, g_PendingFinish[i].voiceType);
    }

    static void __fastcall hkUpdate(void* this_)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigUpdate, this_);

        const bool supportHeli = this_ && IsSupportHeliController(this_);
        if (supportHeli)
        {
            g_Controller.store(this_, std::memory_order_relaxed);
            NoteLandingPhase(this_);
        }

        if (g_OrigUpdate)
            g_OrigUpdate(this_);

        if (supportHeli && !MissionCodeGuard::ShouldBypassHooks() && FeatureSwitch::IsOn(FeatureSwitch::Feature::PequodExtraLines))
        {
            SayDepartingAo(this_);
            SayClimbing(this_);
        }

        if (this_)
        {
            VoiceFinish finishes[kMaxSlots];
            const int n = CollectVoiceFinishes(this_, finishes, kMaxSlots);
            for (int i = 0; i < n && g_PendingFinishCount < kMaxSlots; ++i)
                g_PendingFinish[g_PendingFinishCount++] = finishes[i];
        }
    }

    static void __fastcall hkFlightUpdate(void* this_)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigFlightUpdate, this_);

        if (g_OrigFlightUpdate)
            g_OrigFlightUpdate(this_);

        ApplyPendingTargetSpeeds(this_);
        FlushPendingFinishes();
        SoldierFaction_RefreshHelis(this_);
    }

    static void __fastcall hkCallVoice(void* this_, std::uint32_t slot, std::uint32_t voiceId, std::uint32_t voiceType, std::uint32_t param4)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigCallVoice, this_, slot, voiceId, voiceType, param4);

        if (this_ && IsSupportHeliController(this_))
        {
            g_Controller.store(this_, std::memory_order_relaxed);
            if (voiceType == kPilotRadio
                && (voiceId == SupportHeliWeapon::kRocketAttackLine || voiceId == SupportHeliWeapon::kMissileAttackLine))
            {
                if (!SupportHeliWeapon::SortieAttackLine(voiceId, voiceType))
                {
                    return;
                }
            }
            else
            {
                const std::uint32_t swapped = SwapPilotLine(this_, slot, voiceId);
                voiceId = swapped;
            }
        }

        if (!PilotAiVoiceAllowed(this_, slot))
            return;

        FlushPendingFinishes();
        V_FrameWork::EmitMessage("Radio", "HeliStart", voiceId, param4, voiceType);

        if (g_OrigCallVoice)
            g_OrigCallVoice(this_, slot, voiceId, voiceType, param4);
    }

    static bool TryCallVoice(CallVoice_t fn, void* controller, std::uint32_t slot, std::uint32_t voiceId, std::uint32_t voiceType, std::uint32_t param4)
    {
        __try
        {
            fn(controller, slot, voiceId, voiceType, param4);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static bool IsCommonRadio(lua_State* L, int radio, const char* name)
    {
        if (g_lua_type(L, radio) != LUA_TNUMBER)
            return false;
        const int top = g_lua_gettop(L);
        bool isNamedRadio = false;
        g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("TppDefine"));
        if (g_lua_type(L, -1) == LUA_TTABLE)
        {
            g_lua_getfield(L, -1, const_cast<char*>("COMMON_RADIO"));
            if (g_lua_type(L, -1) == LUA_TTABLE)
            {
                g_lua_getfield(L, -1, const_cast<char*>(name));
                isNamedRadio = g_lua_type(L, -1) == LUA_TNUMBER && g_lua_tonumber(L, -1) == g_lua_tonumber(L, radio);
            }
        }
        g_lua_settop(L, top);
        return isNamedRadio;
    }

    static bool CommonRadioTargetIs(lua_State* L, int radio, const char* group)
    {
        const int top = g_lua_gettop(L);
        bool same = false;
        g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("TppRadio"));
        if (g_lua_type(L, -1) == LUA_TTABLE)
        {
            g_lua_getfield(L, -1, const_cast<char*>("GetPlayCommonTargetRadio"));
            if (g_lua_type(L, -1) == LUA_TFUNCTION)
            {
                g_lua_pushvalue(L, radio);
                if (g_lua_pcall(L, 1, 1, 0) == 0 && g_lua_type(L, -1) == LUA_TSTRING)
                {
                    const char* target = GetLuaString(L, -1);
                    same = target && std::strcmp(target, group) == 0;
                }
            }
        }
        g_lua_settop(L, top);
        return same;
    }

    static const char* MillerChopperRadio(lua_State* L)
    {
        if (IsCommonRadio(L, 1, "CALL_HELI_SECOND_TIME"))
        {
            if (GetTickCount64() % 3 != 0 && CommonRadioTargetIs(L, 1, kRogerRadio))   // the group picks one of its two lines, so all three come up equally
                return kDispatchingChopperRadio;
            return nullptr;
        }
        if (IsCommonRadio(L, 1, "HELI_LOST_CONTROL_END"))
        {
            g_HelisLostThisMission.fetch_add(1, std::memory_order_relaxed);
            return nullptr;
        }
        if (IsCommonRadio(L, 1, "HELI_LOST_CONTROL_END_ENEMY_ATTACK"))
        {
            const int lostBefore = g_HelisLostThisMission.fetch_add(1, std::memory_order_relaxed);
            if (!CommonRadioTargetIs(L, 1, kChopperShotDownRadio))
                return nullptr;
            if (lostBefore == 0)
                return (GetTickCount64() & 1) ? kBackupChopperRadio : nullptr;
            if (lostBefore == 1)
                return kLostAnotherChopperRadio;
            return kAnotherChopperDownRadio;
        }
        return nullptr;
    }

    static bool ModerateLzCall(lua_State* L)   // half the calls to a Danger Moderate LZ, the rest keep vanilla's reply
    {
        return FeatureSwitch::IsOn(FeatureSwitch::Feature::DangerLandingZones) && FeatureSwitch::IsOn(FeatureSwitch::Feature::ModerateLandingZones)
            && IsCommonRadio(L, 1, "CALL_HELI_SECOND_TIME") && (GetTickCount64() & 1) && LandingZoneDanger_SelectedIsModerate()
            && CommonRadioTargetIs(L, 1, kRogerRadio);
    }

    static bool MissionWarnsOfAbort(lua_State* L, int radio)   // the target radio is nil where the mission turns the warning off: free roam, s10020, s10030, s10052, s10130
    {
        const int top = g_lua_gettop(L);
        bool warns = false;
        g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("TppRadio"));
        if (g_lua_type(L, -1) == LUA_TTABLE)
        {
            g_lua_getfield(L, -1, const_cast<char*>("GetPlayCommonTargetRadio"));
            if (g_lua_type(L, -1) == LUA_TFUNCTION)
            {
                g_lua_pushvalue(L, radio);
                if (g_lua_pcall(L, 1, 1, 0) == 0)
                    warns = g_lua_type(L, -1) == LUA_TSTRING || g_lua_type(L, -1) == LUA_TTABLE;
            }
        }
        g_lua_settop(L, top);
        return warns;
    }

    static std::uint32_t BoardingLine(lua_State* L, int argumentCount)
    {
        if (argumentCount < 1 || !IsCommonRadio(L, 1, "ABORT_BY_HELI") || !SnakeAboard(HeliKey::kFirstType, 0) || !MissionWarnsOfAbort(L, 1))
            return 0;
        return PlayerAlertPhase::Read() == PlayerAlertPhase::kAlert ? kAbortCombatLine : kAbortLine;
    }

    static bool SayBoardingLine(std::uint32_t line)
    {
        if (g_PilotAiVoiceOff[HeliKey::Of(HeliKey::kFirstType, 0)].load(std::memory_order_relaxed))
            return false;
        const bool said = Play_PilotCallVoice(line, 0, kPilotVoice, 0);
        return said;
    }

    static void CallOriginalPlayCommonRadio(lua_State* L, int argumentCount)
    {
        g_lua_pushvalue(L, LUA_UPVALUEINDEX_51_1);
        for (int i = 1; i <= argumentCount; ++i)
            g_lua_pushvalue(L, i);
        if (g_lua_pcall(L, argumentCount, 0, 0) == 0)
            return;
        const char* err = GetLuaString(L, -1);
        LogError("TppRadio.PlayCommonRadio: %s", err ? err : "unknown error");
        g_lua_settop(L, -2);
    }

    static void PlayRadioAfterPilotLine(lua_State* L, int argumentCount, float lineSeconds)
    {
        const int top = g_lua_gettop(L);
        g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("mvars"));
        if (g_lua_type(L, -1) == LUA_TTABLE)
            g_lua_getfield(L, -1, const_cast<char*>("rad_commonRadioDelayTable"));
        if (g_lua_type(L, -1) != LUA_TTABLE)
        {
            g_lua_settop(L, top);
            CallOriginalPlayCommonRadio(L, argumentCount);
            return;
        }
        const int delays = g_lua_gettop(L);
        g_lua_pushvalue(L, 1);
        g_lua_gettable(L, delays);
        const int vanillaDelay = g_lua_gettop(L);

        g_lua_pushvalue(L, 1);
        g_lua_pushnumber(L, static_cast<lua_Number>(lineSeconds + kRadioAfterPilotGap));
        g_lua_settable(L, delays);
        CallOriginalPlayCommonRadio(L, argumentCount);
        g_lua_pushvalue(L, 1);
        g_lua_pushvalue(L, vanillaDelay);
        g_lua_settable(L, delays);
        g_lua_settop(L, top);
    }

    static void PlayCommonRadioAs(lua_State* L, int argumentCount, const char* group)
    {
        const int top = g_lua_gettop(L);
        g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("mvars"));
        if (g_lua_type(L, -1) == LUA_TTABLE)
            g_lua_getfield(L, -1, const_cast<char*>("rad_commonRadioTable"));
        if (g_lua_type(L, -1) != LUA_TTABLE)
        {
            g_lua_settop(L, top);
            CallOriginalPlayCommonRadio(L, argumentCount);
            return;
        }
        const int radios = g_lua_gettop(L);
        g_lua_pushvalue(L, 1);
        g_lua_gettable(L, radios);
        const int vanillaRadio = g_lua_gettop(L);

        g_lua_pushvalue(L, 1);
        g_lua_pushstring(L, const_cast<char*>(group));
        g_lua_settable(L, radios);
        CallOriginalPlayCommonRadio(L, argumentCount);
        g_lua_pushvalue(L, 1);
        g_lua_pushvalue(L, vanillaRadio);
        g_lua_settable(L, radios);
        g_lua_settop(L, top);
    }

    static int __cdecl l_PlayCommonRadio(lua_State* L)
    {
        const int argumentCount = g_lua_gettop(L);
        const bool active = g_Installed && !MissionCodeGuard::ShouldBypassHooks();
        const bool extraLines = active && FeatureSwitch::IsOn(FeatureSwitch::Feature::PequodExtraLines);
        const std::uint32_t line = extraLines ? BoardingLine(L, argumentCount) : 0;
        if (line != 0 && SayBoardingLine(line))
        {
            PlayRadioAfterPilotLine(L, argumentCount, line == kAbortCombatLine ? kAbortCombatLineSeconds : kAbortLineSeconds);
            return 0;
        }
        if (active && argumentCount >= 1 && ModerateLzCall(L))
        {
            PlayCommonRadioAs(L, argumentCount, kModerateLzRadio);
            return 0;
        }
        const char* millerRadio = active && argumentCount >= 1 ? MillerChopperRadio(L) : nullptr;
        if (millerRadio && FeatureSwitch::IsOn(FeatureSwitch::Feature::MillerChopperReports))
        {
            PlayCommonRadioAs(L, argumentCount, millerRadio);
            return 0;
        }
        CallOriginalPlayCommonRadio(L, argumentCount);
        return 0;
    }
}

bool Play_PilotCallVoice(std::uint32_t voiceId, std::uint32_t slot, std::uint32_t voiceType, std::uint32_t param4)
{
    MISSION_GUARD_RETURN_FALSE();

    CallVoice_t fn = g_OrigCallVoice ? g_OrigCallVoice : g_CallVoice;
    if (!fn)
        return false;

    void* controller = g_Controller.load(std::memory_order_relaxed);
    if (!controller)
        return false;

    FlushPendingFinishes();
    V_FrameWork::EmitMessage("Radio", "HeliStart", voiceId, param4, voiceType);
    return TryCallVoice(fn, controller, slot, voiceId, voiceType, param4);
}

void PilotVoice_OnHeliDamaged(void* handler, std::uint32_t heliIndex, std::uint16_t attackId, const float hit[3], float lifeBefore, float lifeAfter)
{
    if (!handler || lifeAfter >= lifeBefore || lifeAfter <= 0.0f || MissionCodeGuard::ShouldBypassHooks()
        || !FeatureSwitch::IsOn(FeatureSwitch::Feature::PequodExtraLines))
        return;
    void* controller = g_Controller.load(std::memory_order_relaxed);
    const std::uint64_t now = GetTickCount64();
    if (!controller || heliIndex >= static_cast<std::uint32_t>(kMaxSlots) || now - g_DamageLineMs.load(std::memory_order_relaxed) < kDamageLineGapMs)
        return;

    void* heli = nullptr;
    bool blast = false;
    __try
    {
        heli = *reinterpret_cast<void**>(static_cast<std::uint8_t*>(handler) + kHandlerHeli);
        void* attackParams = *reinterpret_cast<void**>(static_cast<std::uint8_t*>(handler) + kHandlerAttackParams);
        const auto getAttackParam = (*reinterpret_cast<GetAttackParam_t* const*>(attackParams))[0];
        const std::uint8_t* param = getAttackParam(attackParams, attackId);
        blast = param && (param[kAttackParamFlags] & kAttackParamBlast) != 0
             && attackId != kBlastWithBulletSoundA && attackId != kBlastWithBulletSoundB;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return;
    }
    if (HeliTypeOf(heli) != HeliKey::kFirstType || PilotBusy(controller, heliIndex))
        return;

    const std::uint32_t cabinLine = PickCabinLine(heliIndex, blast, lifeAfter, now);
    std::uint32_t line = 0;
    std::uint32_t kind = kPilotVoice;
    const bool aboard = SnakeAboard(HeliKey::kFirstType, heliIndex);
    const int hour = blast || !aboard || now - g_ClockLineMs.load(std::memory_order_relaxed) < kClockLineGapMs || !HeliInAssault(heli, heliIndex)
        ? -1 : ClockOfHit(heli, heliIndex, hit);
    if (hour >= 0)
    {
        line = hour == 0 && (now & 1) ? kDeadAheadLine : kClockLines[hour];
        g_ClockLineMs.store(now, std::memory_order_relaxed);
    }
    else if (aboard)
        line = cabinLine;
    else
    {
        kind = kPilotRadio;
        float at[4] = {};
        float snake[3] = {};
        if (lifeBefore > 2.0f / 3.0f && lifeAfter <= 2.0f / 3.0f)
            line = kTakenFireLine;
        else if (lifeBefore > 1.0f / 3.0f && lifeAfter <= 1.0f / 3.0f)
            line = kHeavyDamageLine;
        else if (now - g_CoverMeMs.load(std::memory_order_relaxed) >= kCoverMeGapMs && HeliTransform(heli, heliIndex, at) && SnakePosition(snake)
                 && (snake[0] - at[0]) * (snake[0] - at[0]) + (snake[2] - at[2]) * (snake[2] - at[2]) <= kCoverMeReach * kCoverMeReach)
        {
            line = kCoverMeLine;
            g_CoverMeMs.store(now, std::memory_order_relaxed);
        }
        if (line == 0)
        {
            kind = kPilotVoice;   // from the heli itself, so Snake hears it only near it
            line = cabinLine;
        }
    }
    if (line == 0)
        return;

    g_DamageLineMs.store(now, std::memory_order_relaxed);
    g_ChatterLine[heliIndex].store(line, std::memory_order_relaxed);
    if (line == cabinLine)
        g_LastCabinLine[heliIndex].store(line, std::memory_order_relaxed);
    const bool said = Play_PilotCallVoice(line, heliIndex, kind, 0);
}

void PilotVoice_OnDoorMoving(std::uint32_t heliType, std::uint32_t heliIndex, bool opening)
{
    if (heliType != HeliKey::kFirstType || heliIndex >= static_cast<std::uint32_t>(kMaxSlots) || MissionCodeGuard::ShouldBypassHooks()
        || !FeatureSwitch::IsOn(FeatureSwitch::Feature::PequodExtraLines))
        return;
    const bool aboard = SnakeAboard(heliType, heliIndex);
    const bool cameFromOutside = GetTickCount64() - g_SnakeOutsideMs[heliIndex].load(std::memory_order_relaxed) < kJustBoardedMs;
    const bool forcedLanding = opening && PlayerAlertPhase::Read() == PlayerAlertPhase::kAlert;   // Snake aboard or not
    const bool snakeOutside = opening ? cameFromOutside : !aboard;
    void* controller = g_Controller.load(std::memory_order_relaxed);
    const bool busy = controller && PilotBusyBeyondChatter(controller, heliIndex);
    if ((!forcedLanding && !snakeOutside) || !controller || busy)
    {
        return;
    }
    const std::uint32_t line = forcedLanding ? kForcedLandingLine : (opening ? kOpeningDoorLine : kClosingDoorLine);
    const bool said = Play_PilotCallVoice(line, heliIndex, forcedLanding ? kPilotRadio : kPilotVoice, 0);
}

void Set_HeliTargetSpeed(std::uint32_t heliKey, float targetSpeedKmh, float changeTime)
{
    if (heliKey >= HeliKey::kCount)
        return;
    g_TargetSpeed[heliKey].metresPerSec = (targetSpeedKmh > 0.0f ? targetSpeedKmh : 0.0f) * kKmhToMetresPerSec;
    g_TargetSpeed[heliKey].changeTime   = changeTime > 0.0f ? changeTime : 0.0f;
    if (!g_TargetSpeed[heliKey].pending.exchange(true, std::memory_order_release))
        g_TargetSpeedPendingCount.fetch_add(1, std::memory_order_relaxed);
}

void Set_HeliPilotAiVoiceEnabled(std::uint32_t heliKey, bool enabled)
{
    if (heliKey < HeliKey::kCount)
        g_PilotAiVoiceOff[heliKey].store(!enabled, std::memory_order_relaxed);
}

void PilotVoice_EnsureBoardingRadioHookInstalled(lua_State* L)
{
    if (!g_Installed || !L)
        return;
    const ULONGLONG now = GetTickCount64();
    if (now < g_NextRadioHookCheck.load(std::memory_order_relaxed))
        return;
    g_NextRadioHookCheck.store(now + kRadioHookCheckIntervalMs, std::memory_order_relaxed);
    if (!ResolveLuaApi())
        return;

    const int top = g_lua_gettop(L);
    g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("TppRadio"));
    if (g_lua_type(L, -1) != LUA_TTABLE)
    {
        g_lua_settop(L, top);
        return;
    }
    const int tppRadio = g_lua_gettop(L);
    g_lua_getfield(L, tppRadio, const_cast<char*>(kOriginalPlayCommonRadio));
    const bool installed = g_lua_type(L, -1) != LUA_TNIL;
    g_lua_settop(L, tppRadio);
    if (installed)
    {
        g_lua_settop(L, top);
        return;
    }

    g_lua_getfield(L, tppRadio, const_cast<char*>("PlayCommonRadio"));
    if (g_lua_type(L, -1) != LUA_TFUNCTION)
    {
        g_lua_settop(L, top);
        return;
    }
    const int original = g_lua_gettop(L);

    g_lua_pushstring(L, const_cast<char*>(kOriginalPlayCommonRadio));
    g_lua_pushvalue(L, original);
    g_lua_rawset(L, tppRadio);

    g_lua_pushstring(L, const_cast<char*>("PlayCommonRadio"));
    g_lua_pushvalue(L, original);
    g_lua_pushcclosure(L, &l_PlayCommonRadio, 1);
    g_lua_rawset(L, tppRadio);

    g_lua_settop(L, top);
}

void HeliSoundController_ClearForReload()
{
    for (std::uint32_t i = 0; i < HeliKey::kCount; ++i)
    {
        if (g_TargetSpeed[i].pending.exchange(false, std::memory_order_relaxed))
            g_TargetSpeedPendingCount.fetch_sub(1, std::memory_order_relaxed);
    }
    g_Controller.store(nullptr, std::memory_order_relaxed);   // the old mission's controller is gone
    for (int t = 0; t < kHeliTypeCount; ++t)
        for (int i = 0; i < kMaxSlots; ++i)
            g_Snap[t][i].voiceId = 0;
    g_PendingFinishCount = 0;
    for (int i = 0; i < kMaxSlots; ++i)
    {
        g_StandbyMs[i].store(0, std::memory_order_relaxed);
        g_TakeoffMs[i].store(0, std::memory_order_relaxed);
        g_LandingPhase[i].store(0, std::memory_order_relaxed);
        g_SnakeOutsideMs[i].store(0, std::memory_order_relaxed);
        g_LandingTrips[i].store(0, std::memory_order_relaxed);
        g_BlockedLz[i] = {};
        g_LeavingAoMs[i].store(0, std::memory_order_relaxed);
        g_ChatterLine[i].store(0, std::memory_order_relaxed);
        g_LastCabinLine[i].store(0, std::memory_order_relaxed);
    }
    g_DamageLineMs.store(0, std::memory_order_relaxed);
    g_ClockLineMs.store(0, std::memory_order_relaxed);
    g_CoverMeMs.store(0, std::memory_order_relaxed);
}

void Clear_HeliSoundControllerOverrides()
{
    for (std::uint32_t i = 0; i < HeliKey::kCount; ++i)
        g_PilotAiVoiceOff[i].store(false, std::memory_order_relaxed);
    g_HelisLostThisMission.store(0, std::memory_order_relaxed);
    HeliSoundController_ClearForReload();
}

bool Install_HeliSoundController_Hook()
{
    if (g_Installed)
        return true;

    if (!gAddr.HeliSoundControllerImpl_Update)
    {
        LogError("HeliSoundController: Update address missing, heli radio messages off");
        return false;
    }

    if (gAddr.HeliSoundControllerImpl_CallVoice)
    {
        g_CallVoice = reinterpret_cast<CallVoice_t>(ResolveGameAddress(gAddr.HeliSoundControllerImpl_CallVoice));
        if (g_CallVoice)
        {
            if (!CreateAndEnableHook(reinterpret_cast<void*>(g_CallVoice),
                                     reinterpret_cast<void*>(&hkCallVoice),
                                     reinterpret_cast<void**>(&g_OrigCallVoice)))
            {
                g_OrigCallVoice = nullptr;
                LogError("HeliSoundController: CallVoice hook failed, HeliStart message off");
            }
        }
    }

    if (gAddr.HeliFlightControllerImpl_Update)
    {
        void* flightTarget = ResolveGameAddress(gAddr.HeliFlightControllerImpl_Update);
        if (flightTarget)
        {
            if (!CreateAndEnableHook(flightTarget,
                                     reinterpret_cast<void*>(&hkFlightUpdate),
                                     reinterpret_cast<void**>(&g_OrigFlightUpdate)))
            {
                g_OrigFlightUpdate = nullptr;
                LogError("HeliSoundController: FlightController Update hook failed, "
                         "HeliFinish message off");
            }
        }
    }

    void* target = ResolveGameAddress(gAddr.HeliSoundControllerImpl_Update);
    if (!target)
    {
        LogError("HeliSoundController: Update address missing, heli radio messages off");
        return false;
    }

    const bool ok = CreateAndEnableHook(
        target,
        reinterpret_cast<void*>(&hkUpdate),
        reinterpret_cast<void**>(&g_OrigUpdate));

    if (ok)
        g_Installed = true;

    if (!ok)
        LogError("HeliSoundController: Update hook failed, heli radio messages off");
    return ok;
}

bool Uninstall_HeliSoundController_Hook()
{
    if (!g_Installed)
        return true;

    if (gAddr.HeliSoundControllerImpl_Update)
        DisableAndRemoveHook(ResolveGameAddress(gAddr.HeliSoundControllerImpl_Update));

    if (g_CallVoice)
        DisableAndRemoveHook(reinterpret_cast<void*>(g_CallVoice));

    if (gAddr.HeliFlightControllerImpl_Update && g_OrigFlightUpdate)
        DisableAndRemoveHook(ResolveGameAddress(gAddr.HeliFlightControllerImpl_Update));

    g_OrigUpdate       = nullptr;
    g_CallVoice        = nullptr;
    g_OrigCallVoice    = nullptr;
    g_OrigFlightUpdate = nullptr;
    g_Installed     = false;
    g_Controller.store(nullptr, std::memory_order_relaxed);
    for (int t = 0; t < kHeliTypeCount; ++t)
        for (int i = 0; i < kMaxSlots; ++i)
            g_Snap[t][i].voiceId = 0;
    g_PendingFinishCount = 0;
    return true;
}
