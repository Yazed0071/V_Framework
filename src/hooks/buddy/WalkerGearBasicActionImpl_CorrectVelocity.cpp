#include <pch.h>

#include "WalkerGearBasicActionImpl_CorrectVelocity.h"

#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

#include "AddressSet.h"
#include "BattleGearLegs.h"
#include "BattleGearRoofGun.h"
#include "BattleGearSound.h"
#include "BattleGearWeapons.h"
#include "HookUtils.h"
#include "WalkerGearBasicActionImpl_CheckLowStepOnMove.h"
#include "WalkerGearBasicActionImpl_FallCheck.h"
#include "log.h"

namespace
{
    using CorrectVelocity_t = void(__fastcall*)(void* action, std::uint32_t index, std::uint8_t* controller, const float* step,
                                                const float* turn);
    using GetComm_t         = const std::uint8_t*(__fastcall*)(void* system, std::uint16_t commId);
    using GetTransform_t    = const std::uint8_t*(__fastcall*)(void* animation, std::uint32_t index);
    using SetBodySize_t     = void(__fastcall*)(void* control, float radius, float height);

    constexpr std::size_t   kSystem           = 0x70;   // walker system: comm blocks and the frame clock
    constexpr std::size_t   kAnimation        = 0x80;   // walker transforms
    constexpr std::size_t   kWorks            = 0x98;
    constexpr std::size_t   kFirstIndex       = 0xA0;
    constexpr std::size_t   kWorkSize         = 0x3C0;
    constexpr std::size_t   kWorkState        = 0xD4;
    constexpr std::size_t   kWorkCommId       = 0xF0;   // the walker's id in its system: its comm block, clock scale and weapon slot
    constexpr std::uint16_t kNoWalkerId       = 0x1FF;
    constexpr std::size_t   kGetCommSlot      = 0xF8 / sizeof(void*);
    constexpr std::size_t   kGetTransformSlot = 0x388 / sizeof(void*);
    constexpr std::size_t   kTransformPlace   = 0x20;   // the walker's position
    constexpr std::size_t   kTransformTurn    = 0x60;   // the walker's rotation quaternion
    constexpr std::size_t   kSystemClock      = 0x18;
    constexpr std::size_t   kClockPerWalker   = 0x38;   // frame time = this walker's scale (by walker id) * the frame delta
    constexpr std::size_t   kClockFrame       = 0x48;
    constexpr std::size_t   kControllerStep   = 0x10;   // this frame's move in walker space
    constexpr std::size_t   kControllerTurn   = 0x20;   // this frame's turn quaternion
    constexpr std::size_t   kCommStick        = 0x2D8;
    constexpr std::size_t   kCommStickHeading = 0x2DC;
    constexpr std::size_t   kCommCamera       = 0x2E0;
    constexpr std::size_t   kCommButtons      = 0x560;
    constexpr std::uint32_t kDashHeld         = 0x800000;
    constexpr std::size_t   kSystemEquip      = 0x88;   // walkergear::EquipController
    constexpr std::size_t   kEquipArmKinds    = 0x30;   // u16 per walker id: the weapon in hand, 2 = sidearm
    constexpr std::uint16_t kSidearm          = 2;
    // StandIdle, StandTurn, StandTurnToMoveStart, MoveTurn, StandIdleToMove, Move, StandMoveToIdle and the fire-posture
    // ParallelStandIdleToMove / ParallelStandMove / ParallelStandMoveToIdle; steps, falls, damage and warps keep the game's motion
    constexpr std::uint32_t kOwnMoveStates    = (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4) | (1u << 6) | (1u << 7) | (1u << 10)
                                              | (1u << 11) | (1u << 12) | (1u << 13);
    constexpr std::uint8_t  kStepOnState      = 8;   // the game's own climb at a slow walk (it does not start while the Battle
                                                     // Gear is ridden): the run takes its pace

    constexpr float kDeadZone     = 0.15f;
    constexpr float kTrotTilt     = 0.5f;
    constexpr float kCanterTilt   = 0.85f;
    constexpr float kWalkSpeed    = 1.5f;      // metres a second
    constexpr float kTrotSpeed    = 3.7f;
    constexpr float kCanterSpeed  = 7.0f;      // a horse's speeds; BattleGearLegs' gait table steps to them
    constexpr float kGallopSpeed  = 14.0f;
    constexpr float kSprintSpeed  = 20.0f;
    constexpr float kSprintWindow = 0.35f;     // seconds from letting Dash go to pressing it again
    constexpr float kSprintFrom   = 0.8f;      // share of the gallop speed the sprint needs
    constexpr float kRunAccel     = 4.0f;      // metres a second, each second
    constexpr float kRunBrake     = 6.0f;
    constexpr float kTurnRate     = 1.5708f;   // 90 degrees a second from a walk on
    constexpr float kStillTurnRate = 0.7854f;  // 45 on the spot: turning faster there, the front feet slide sideways
    constexpr float kUprightTurnRate = 0.7854f;   // 45: the tall stance turns slowly, so its feet keep long steps through turns
    constexpr float kStrafeAhead  = 1.5f;      // zooming, the stick walks ahead or back at most this fast
    constexpr float kStrafeAcross = 1.0f;      // and side-steps this fast
    constexpr float kStrafeAccel  = 2.0f;
    constexpr float kStrafeBrake  = 4.0f;
    constexpr float kGrip         = 9.8f;      // m/s^2 of sideways pull the feet hold: faster runs turn wider
    constexpr float kGlideSpeed   = 8.0f;
    constexpr float kGlideAccel   = 4.0f;
    constexpr float kGlideBrake   = 12.0f;     // 8 m/s stops within 2.7 m
    constexpr float kHullTurnRate = 0.7854f;   // 45 degrees a second
    // a stick pushed behind it turns the hull about: the run stops, the Battle Gear lifts onto its hover and swings round in the
    // air. Stepped round, its feet (3 m from the hull's middle, 0.7 m of sideways reach) ran at their fastest beat, 3.2 steps a
    // second each, through a 1.4 s swing; a swing their steps keep up with takes 5 s
    constexpr float kAboutFrom    = 2.356f;    // 135 degrees: a stick pushed this far round from the heading turns the hull about
    constexpr float kAboutDone    = 0.35f;     // 20 degrees: the stick came back before the swing, the walk carries on
    constexpr float kAboutStick   = 0.35f;
    constexpr float kAboutBrake   = 10.0f;     // the run stops first: 14 m/s in 1.4 s
    constexpr float kPivotFrom    = 0.3f;      // m/s: stopped, the swing starts
    constexpr float kPivotLift    = 0.35f;     // seconds its feet take to leave the ground on the hover before the hull swings
    constexpr float kPivotTime    = 1.6f;      // seconds for 180 degrees, eased in and out
    constexpr float kSteerFollow  = 0.25f;     // share of its gap to the walker's heading the steering closes each frame
    constexpr float kSurgeRate    = 2.5f;      // m/s the hull moves over its planted feet through a stance switch
    constexpr float kRecoil[3]    = { 0.6f, 1.2f, 2.0f };   // m/s pushed back by a rail-gun shot of level 1-3
    constexpr float kLongestFrame = 0.1f;
    constexpr float kNeverPressed = 1000.0f;
    constexpr float kWarpSlack    = 0.4f;      // metres over its step, or up or down, a walker moves in one frame before the probe notes it
    constexpr float kPi           = 3.14159265f;

    constexpr std::uint32_t kWallFlags  = 0x4000700;   // what the D-Walker's leg IK casts its foot rays against
    constexpr float         kWallMargin = 0.25f;       // metres the hull's outline keeps from a wall, square to it
    constexpr float         kTurnMargin = 0.1f;        // a turn's sweep keeps less, along its own way: at a wall the hull still turns
    constexpr float         kMostKeptOff = 3.0f;       // metres along a slanting step the margin may hold a point off a wall
    constexpr float         kGrazing    = 1e-3f;       // share of a path going into a face: under it the path runs along the face,
                                                       // or out of a thing the point stands in, and the face is no wall to it
    constexpr float         kFlatFace   = 0.05f;       // share of a face's normal lying within the hull's plane
    constexpr int           kSlides     = 3;
    constexpr float         kBellyUnder = 0.15f;       // metres under the body bone the hull's belly lies: what stands lower passes
                                                       // under it
    constexpr float         kLegHeight  = 1.45f;       // metres over the feet's ground the legs and pods are tested at, just over
                                                       // what the feet climb: there only a thing whose top spreads stops them
    constexpr float         kLegBelow   = 0.1f;        // the legs' test runs where it lies this far under the belly
    constexpr float         kInsideThing = 0.2f;       // metres past a hit the thing's top is looked for
    constexpr float         kTrailing   = -0.1f;       // metres along a step from the hull's middle: points further back trail it
    constexpr float         kObstacleBrake = 12.0f;    // m/s^2 the run brakes for what stands in its way: a gallop stops within 8 m
    constexpr float         kBrakeFrom  = 1.0f;        // m/s: slower, the last step's cut at the wall is no jolt
    constexpr float         kSlopeProbe = 0.4f;        // metres under a hit the outline looks again: on a slope the ground lies
                                                       // nearer there
    constexpr float         kSteepestClimb = 0.839f;   // tan 40 degrees: rising no steeper, what the outline meets is ground
    constexpr float         kWalkableUp = 0.766f;      // cos 40 degrees: the same slope, as its face's normal points up
    constexpr float         kSlantFrom  = 0.8f;        // share of the run going into a wall ahead: under the first the wall
    constexpr float         kSlantTo    = 0.95f;       // only turns the run along itself, over the second it stops it
    constexpr float         kAtTheWall  = 0.3f;        // metres of free way: with less the hull is at the wall
    constexpr float         kFootBack   = 0.3f;        // metres before a far hit the ground at the thing's foot is read
    constexpr float         kFootBelow  = 3.0f;        // metres under the hull's own ground it is looked for
    constexpr float         kSameGround = 0.2f;        // metres: the ground there lies as under the hull
    constexpr float         kInsideFar  = 0.3f;        // metres past the hit the look from the thing's own ground reaches

    // fox::cc::CharaControl, the body the game moves a walker with (what AnimationController vt+0x388 hands out): a ball whose
    // middle rides a set height over the ground. What is lower than the ball's underside passes under it, and the control sets
    // the walker up onto any ground it then finds under its middle; it never sets it down, a walker off the ground only falls
    constexpr std::size_t   kControlHeight      = 0x160;
    constexpr std::size_t   kControlRadius      = 0x168;
    constexpr std::size_t   kControlState       = 0x1A0;
    constexpr std::uint32_t kControlOnGround    = 2;
    constexpr std::size_t   kControlSetSizeSlot = 0xB0 / sizeof(void*);   // asks for (radius, height): State_StepOn's own call
    constexpr float         kWalkerRadius  = 0.4f;     // the D-Walker's ball: it passes what is under 0.4 m
    constexpr float         kRiddenRadius  = 0.15f;    // the Battle Gear's, riding as high as what passes under its hull
    constexpr float         kHighestBody   = 2.4f;     // metres: the ball's middle rides no higher
    constexpr float         kBodyStep      = 0.1f;     // its height follows the hull's clearance in steps of this
    constexpr float         kSameSize      = 0.005f;
    constexpr float         kSetUnder      = 0.02f;    // metres under its standing height a step sets the walker: the control
                                                       // then sets it up onto the ground and counts it as standing

    struct HullPoint
    {
        float right;
        float up;
        float ahead;
    };

    // the Battle Gear's outline in metres from its body bone, in the hull's frame: nose, front legs at the knee, sides, rear pods,
    // tail, and the belly under the nose (what the nose passes over still meets it)
    constexpr HullPoint kHullOutline[] = {
        { 0.0f, 0.3f, 2.7f },    { -0.85f, 0.0f, 2.5f }, { 0.85f, 0.0f, 2.5f },
        { 0.0f, -0.15f, 2.5f },
        { -1.6f, -0.5f, 1.4f },  { 1.6f, -0.5f, 1.4f },
        { -1.9f, -0.6f, 0.0f },  { 1.9f, -0.6f, 0.0f },
        { -1.9f, -0.7f, -2.6f }, { 1.9f, -0.7f, -2.6f }, { -1.2f, -0.7f, -3.5f }, { 1.2f, -0.7f, -3.5f },
        { 0.0f, -0.4f, -3.2f },
    };

    const std::uint8_t kCorrectVelocityEntry[] = { 0x48, 0x89, 0x5C, 0x24, 0x20, 0x55, 0x56, 0x57, 0x48, 0x81, 0xEC, 0xF0, 0x00, 0x00, 0x00 };

    struct Walker
    {
        void*          system;
        void*          animation;
        GetComm_t      getComm;
        GetTransform_t getTransform;
        std::uint16_t  commId;
        std::uint8_t   state;
        float          frameTime;
        const std::uint8_t* work;
    };

    struct Input
    {
        float stick;
        float stickHeading;
        float camera;
        bool  dash;
    };

    struct Motion
    {
        bool  known;
        bool  fire;
        bool  upright;
        bool  dashWas;
        bool  sprint;
        bool  strafe;
        bool  about;       // stopping to turn about to a stick pushed behind it
        bool  pivoting;    // and swinging round, to the heading the stick had when it stopped
        float pivotFrom;
        float pivotBy;
        float pivotTime;
        bool  steered;     // steer holds the walk's own steering heading
        float steer;
        float sinceDashUp;
        float speed;    // run posture, along the heading
        float across;   // side-stepping, to the right
        float glideX;   // fire posture, world velocity
        float glideZ;
        bool  capped;      // a wall took part of a step: the hull gathers speed again from what it kept
        float wallSpeed;
        std::uint32_t railShots;
    };

    struct Move
    {
        float localX;
        float localZ;
        float turn;
        float speed;
        float goal;
    };

    struct Body   // the walker's body control as the walk reads it each frame
    {
        float place[3];   // the ball's middle
        float height;     // metres it rides over the ground
        float radius;
        bool  onGround;
    };

    CorrectVelocity_t g_Orig = nullptr;
    void*             g_Target = nullptr;
    SRWLOCK           g_MotionLock = SRWLOCK_INIT;
    Motion            g_Motion{};
    bool              g_BodyShaped = false;   // the walker's ball is the Battle Gear's; only the walker's frame touches it
    std::atomic<const std::uint8_t*> g_BasicAction{ nullptr };   // the walkers' basic action, as the hook was last called
#ifdef _DEBUG
    ULONGLONG         g_NextLog = 0;
    std::uint32_t     g_Calls = 0;
    std::uint32_t     g_WallStops = 0;
    std::uint32_t     g_TurnStops = 0;
    std::uint32_t     g_Braked = 0;
    std::uint32_t     g_SetDown = 0;         // TEMP probe: frames the step set the walker down to the ground, and the deepest
    float             g_DeepestSet = 0.0f;
    std::uint32_t     g_OffGround = 0;       // TEMP probe: frames the control did not count the walker as standing
    std::uint32_t     g_Slid = 0;            // TEMP probe: steps that slid along what the outline met
    std::uint32_t     g_Pivoted = 0;         // TEMP probe: turns taken about a held corner
    std::atomic<const std::uint8_t*> g_WalkerWork{ nullptr };   // TEMP probe: the Battle Gear walker's action work
    std::atomic<std::uint32_t>       g_UnreadCalls{ 0 };        // TEMP probe: hook calls not read as the Battle Gear's
    std::atomic<const std::uint8_t*> g_VelocityContext{ nullptr };   // TEMP probe: the controller the hook was last given
    std::uint32_t     g_SlopesPassed = 0;    // TEMP probe: outline hits taken for a slope it walks up
    std::uint32_t     g_PassedOver = 0;      // TEMP probe: hits far ahead the hull will ride over, no braking for them
    std::uint32_t     g_NotInto = 0;         // TEMP probe: faces a path ran along or left from inside
    ULONGLONG         g_NextBrakeLog = 0;    // TEMP probe: what the run brakes for
    float             g_LastPlace[3] = {};   // TEMP probe: the walker moved further in one frame than the step it was given
    float             g_LastAsked = 0.0f;
    bool              g_LastPlaceKnown = false;
    std::uint8_t      g_LastState = 0;
    ULONGLONG         g_NextWarpLog = 0;
    ULONGLONG         g_NextWallLog = 0;     // TEMP probe: which outline point a wall stops, and why it counts as a wall
    std::uint32_t     g_StatesSeen = 0;      // TEMP probe: the walker states since the last line, and how far its facing strayed
    float             g_FacingFrom = 0.0f;   // from the heading at that line (its body's heading sways while the turret looks aside)
    float             g_FacingLow = 0.0f;
    float             g_FacingHigh = 0.0f;
#endif

    bool ReadWalkerSEH(const std::uint8_t* action, std::uint32_t index, Walker& w)
    {
        __try
        {
            const auto* works = *reinterpret_cast<const std::uint8_t* const*>(action + kWorks);
            const auto first = *reinterpret_cast<const std::uint32_t*>(action + kFirstIndex);
            w.system = *reinterpret_cast<void* const*>(action + kSystem);
            w.animation = *reinterpret_cast<void* const*>(action + kAnimation);
            if (!works || !w.system || !w.animation)
                return false;
            const std::uint8_t* work = works + static_cast<std::uint64_t>(index - first) * kWorkSize;
            w.commId = *reinterpret_cast<const std::uint16_t*>(work + kWorkCommId);
            if (w.commId == kNoWalkerId)
                return false;
            w.state = work[kWorkState];
            w.work = work;
            w.getComm = reinterpret_cast<GetComm_t>((*static_cast<void* const* const*>(w.system))[kGetCommSlot]);
            w.getTransform = reinterpret_cast<GetTransform_t>((*static_cast<void* const* const*>(w.animation))[kGetTransformSlot]);
            const auto* clock = *reinterpret_cast<const std::uint8_t* const*>(static_cast<const std::uint8_t*>(w.system) + kSystemClock);
            const auto* perWalker = *reinterpret_cast<const float* const*>(clock + kClockPerWalker);
            w.frameTime = perWalker[w.commId] * *reinterpret_cast<const float*>(clock + kClockFrame);
            return w.getComm && w.getTransform;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ReadInputSEH(const std::uint8_t* comm, Input& in)
    {
        __try
        {
            in.stick = *reinterpret_cast<const float*>(comm + kCommStick);
            in.stickHeading = *reinterpret_cast<const float*>(comm + kCommStickHeading);
            in.camera = *reinterpret_cast<const float*>(comm + kCommCamera);
            in.dash = (*reinterpret_cast<const std::uint32_t*>(comm + kCommButtons) & kDashHeld) != 0;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool SidearmInHandSEH(const void* system, std::uint32_t walker)
    {
        __try
        {
            const auto* equip = *reinterpret_cast<const std::uint8_t* const*>(static_cast<const std::uint8_t*>(system) + kSystemEquip);
            return (*reinterpret_cast<const std::uint16_t* const*>(equip + kEquipArmKinds))[walker] == kSidearm;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ReadHeadingSEH(const std::uint8_t* transform, float& heading)
    {
        __try
        {
            const float* q = reinterpret_cast<const float*>(transform + kTransformTurn);
            heading = std::atan2(2.0f * (q[0] * q[2] + q[1] * q[3]), q[3] * q[3] + q[2] * q[2] - q[0] * q[0] - q[1] * q[1]);   // QuatToRotY
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ReadBodySEH(const std::uint8_t* control, Body& body)
    {
        __try
        {
            const float* p = reinterpret_cast<const float*>(control + kTransformPlace);
            for (int k = 0; k < 3; ++k)
                body.place[k] = p[k];
            body.height = *reinterpret_cast<const float*>(control + kControlHeight);
            body.radius = *reinterpret_cast<const float*>(control + kControlRadius);
            body.onGround = (*reinterpret_cast<const std::uint32_t*>(control + kControlState) & kControlOnGround) != 0;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // asks the control for a body size the way the game's step-on state does (it takes a new height only where the ball
    // fits, so the asking repeats); true once the body has that size
    bool ShapeBodySEH(const std::uint8_t* control, const Body& body, float radius, float height)
    {
        if (std::fabs(body.radius - radius) < kSameSize && std::fabs(body.height - height) < kSameSize)
            return true;
        __try
        {
            auto* self = const_cast<std::uint8_t*>(control);
            reinterpret_cast<SetBodySize_t>((*reinterpret_cast<void* const* const*>(self))[kControlSetSizeSlot])(self, radius, height);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
        return false;
    }

    float StepLengthSEH(const std::uint8_t* controller)   // the frame's move over the ground as the game set it
    {
        __try
        {
            const float* step = reinterpret_cast<const float*>(controller + kControllerStep);
            return std::sqrt(step[0] * step[0] + step[2] * step[2]);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0.0f;
        }
    }

    bool WriteMoveSEH(std::uint8_t* controller, const Move& move, float rise, float* gameStep)
    {
        __try
        {
            auto* step = reinterpret_cast<float*>(controller + kControllerStep);
            for (int i = 0; i < 4; ++i)
                gameStep[i] = step[i];
            step[0] = move.localX;   // w stays as the game set it, and y (the clip's rise and fall) with the walk's own rise or
            step[1] += rise;         // fall on top: what the game's step-on state adds to climb, 1.5 m a second
            step[2] = move.localZ;
            auto* turn = reinterpret_cast<float*>(controller + kControllerTurn);
            turn[0] = 0.0f;
            turn[1] = std::sin(move.turn * 0.5f);
            turn[2] = 0.0f;
            turn[3] = std::cos(move.turn * 0.5f);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    float WrapAngle(float a)
    {
        return std::remainder(a, 2.0f * kPi);
    }

    float Approach(float now, float goal, float maxStep)
    {
        return now + std::clamp(goal - now, -maxStep, maxStep);
    }

    void TrackDash(Motion& m, const Input& in, float dt)
    {
        if (in.dash && !m.dashWas)
            m.sprint = !m.fire && !m.upright && in.stick > kDeadZone && m.sinceDashUp <= kSprintWindow
                    && m.speed >= kSprintFrom * kGallopSpeed;
        if (!in.dash)
        {
            m.sprint = false;
            m.sinceDashUp = m.dashWas ? 0.0f : (std::min)(m.sinceDashUp + dt, kNeverPressed);
        }
        m.dashWas = in.dash;
    }

    Move Run(Motion& m, const Input& in, float heading, float dt)
    {
        float goal = 0.0f;
        const float gap = WrapAngle(in.stickHeading - heading);
        if (m.pivoting)
            m.about = true;   // the swing plays to its end, as the clip does
        else if (in.stick <= kDeadZone || std::fabs(gap) < kAboutDone)
            m.about = false;
        else if (!m.about && in.stick > kAboutStick && std::fabs(gap) > kAboutFrom)
            m.about = true;
        const float pace = m.capped ? (std::min)(m.speed, m.wallSpeed) : m.speed;   // held at a wall it stands, though the run pushes on
        if (m.about && !m.pivoting && pace <= kPivotFrom)
        {
            m.pivoting = true;
            m.speed = pace;
            m.pivotFrom = heading;
            m.pivotBy = gap;
            m.pivotTime = -kPivotLift;
        }
        if (in.stick <= kDeadZone)
            m.sprint = false;
        else if (in.dash && !m.upright)
            goal = m.sprint ? kSprintSpeed : kGallopSpeed;
        else if (in.stick < kTrotTilt)
            goal = kWalkSpeed;
        else
            goal = m.upright || in.stick < kCanterTilt ? kTrotSpeed : kCanterSpeed;   // upright only walks and trots
        if (m.about)
            goal = 0.0f;
        m.speed = Approach(m.speed, goal, (goal > m.speed ? kRunAccel : (m.about ? kAboutBrake : kRunBrake)) * dt);
        m.across = Approach(m.across, 0.0f, kStrafeBrake * dt);
        float turn = 0.0f;
        if (m.pivoting)
        {
            m.pivotTime += dt;
            const float whole = kPivotTime * std::fabs(m.pivotBy) / kPi;
            const float done = whole > 0.0f ? std::clamp(m.pivotTime / whole, 0.0f, 1.0f) : 1.0f;
            const float swung = done * done * (3.0f - 2.0f * done);
            const float most = whole > 0.0f ? 2.0f * std::fabs(m.pivotBy) / whole * dt : kPi;   // a wall held it back: no leap after
            turn = std::clamp(WrapAngle(m.pivotFrom + m.pivotBy * swung - heading), -most, most);
            if (done >= 1.0f)
            {
                m.pivoting = false;
                m.about = false;
            }
        }
        else if (in.stick > kDeadZone && !m.about)
        {
            const float most = m.upright ? kUprightTurnRate
                                         : kStillTurnRate + (kTurnRate - kStillTurnRate) * std::clamp(m.speed / kWalkSpeed, 0.0f, 1.0f);
            const float rate = (std::min)(most, kGrip / (std::max)(m.speed, 1.0f));
            turn = std::clamp(gap, -rate * dt, rate * dt);
        }
        return { m.across * dt, m.speed * dt, turn, std::sqrt(m.speed * m.speed + m.across * m.across), goal };
    }

    float Toward(float now, float goal, float accel, float brake, float dt)   // brakes while slowing or reversing
    {
        const bool gaining = std::fabs(goal) > std::fabs(now) && goal * now >= 0.0f;
        return Approach(now, goal, (gaining ? accel : brake) * dt);
    }

    // zooming: the hull swings toward the camera and the stick walks it ahead or back and side-steps it, camera-relative
    Move Strafe(Motion& m, const Input& in, float heading, float dt)
    {
        const float push = std::clamp((in.stick - kDeadZone) / (1.0f - kDeadZone), 0.0f, 1.0f);
        const float way = in.stickHeading - heading;
        m.speed = Toward(m.speed, std::cos(way) * push * kStrafeAhead, kRunAccel, kRunBrake, dt);
        m.across = Toward(m.across, std::sin(way) * push * kStrafeAcross, kStrafeAccel, kStrafeBrake, dt);
        m.sprint = false;
        const float turn = std::clamp(WrapAngle(in.camera - heading), -kHullTurnRate * dt, kHullTurnRate * dt);
        return { m.across * dt, m.speed * dt, turn, std::sqrt(m.speed * m.speed + m.across * m.across), push * kStrafeAhead };
    }

    Move Glide(Motion& m, const Input& in, float heading, float dt)
    {
        const float push = std::clamp((in.stick - kDeadZone) / (1.0f - kDeadZone), 0.0f, 1.0f);
        const float goalX = std::sin(in.stickHeading) * kGlideSpeed * push;
        const float goalZ = std::cos(in.stickHeading) * kGlideSpeed * push;
        const float gapX = goalX - m.glideX;
        const float gapZ = goalZ - m.glideZ;
        const float gap = std::sqrt(gapX * gapX + gapZ * gapZ);
        const bool slowing = push <= 0.0f || m.glideX * goalX + m.glideZ * goalZ < 0.0f
                          || m.glideX * m.glideX + m.glideZ * m.glideZ > goalX * goalX + goalZ * goalZ;
        const float most = (slowing ? kGlideBrake : kGlideAccel) * dt;
        const float share = gap > most ? most / gap : 1.0f;
        m.glideX += gapX * share;
        m.glideZ += gapZ * share;
        float hull = in.camera;
        battleGearWeapons::HullSteering(&hull);
        const float turn = std::clamp(WrapAngle(hull - heading), -kHullTurnRate * dt, kHullTurnRate * dt);
        const float c = std::cos(heading);
        const float s = std::sin(heading);
        const float worldX = m.glideX * dt;
        const float worldZ = m.glideZ * dt;
        return { worldX * c - worldZ * s, worldX * s + worldZ * c, turn, std::sqrt(m.glideX * m.glideX + m.glideZ * m.glideZ),
                 kGlideSpeed * push };
    }

    Move Advance(const Input& in, float heading, float dt)
    {
        Motion& m = g_Motion;
        const bool fire = battleGearWeapons::FireMode();
        const std::uint32_t railShots = battleGearWeapons::RailShots();
        if (!m.known)
        {
            m = {};
            m.known = true;
            m.fire = fire;
            m.dashWas = in.dash;
            m.sinceDashUp = kNeverPressed;
            m.railShots = railShots;
        }
        else if (fire != m.fire)
        {
            if (fire)
            {
                m.glideX = std::sin(heading) * m.speed + std::cos(heading) * m.across;
                m.glideZ = std::cos(heading) * m.speed - std::sin(heading) * m.across;
            }
            else
            {
                m.speed = (std::max)(0.0f, m.glideX * std::sin(heading) + m.glideZ * std::cos(heading));
                m.across = m.glideX * std::cos(heading) - m.glideZ * std::sin(heading);
            }
            m.sprint = false;
            m.fire = fire;
        }
        if (railShots != m.railShots)
        {
            m.railShots = railShots;
            const float push = kRecoil[std::clamp(battleGearWeapons::LastRailShotLevel(), 1, 3) - 1];
            m.speed -= push;
            m.glideX -= std::sin(heading) * push;
            m.glideZ -= std::cos(heading) * push;
        }
        m.upright = battleGearWeapons::Upright();
        if (m.upright)
            m.sprint = false;
        TrackDash(m, in, dt);
        m.strafe = !fire && battleGearWeapons::Aiming();
        if (fire || m.strafe)
        {
            m.about = false;
            m.pivoting = false;
        }
        if (fire)
        {
            m.steered = false;   // hovering, the ride hook steers the hull itself
            return Glide(m, in, heading, dt);
        }
        // StandIdle and the weapon-ready states set the walker's heading from the steering, so the heading read here is an earlier
        // frame's steering: taken whole as the new one, a difference between two frames stayed as a turn back and forth on every
        // frame after. The walk keeps its own steering heading and the walker's only pulls it along.
        m.steer = m.steered ? WrapAngle(m.steer + kSteerFollow * WrapAngle(heading - m.steer)) : heading;
        m.steered = true;
        return m.strafe ? Strafe(m, in, m.steer, dt) : Run(m, in, m.steer, dt);
    }

    // The outline's rays run level with the hull, which lies along the ground under its feet: where the ground ahead rises they
    // run into it. What they meet is ground the Battle Gear walks up where its face lies no steeper than that, or where the
    // ground lies nearer under the hit by what such a slope recedes over that height (stairs, a rough slope: the faces stand
    // steep, the slope they make does not). A wall, a rock or a bank stands as near there as above.
    bool GroundAhead(const float* at, const float* way, float reach, float gap, const float* face)
    {
        if (face[1] >= kWalkableUp)
            return true;
        const float lower[3] = { at[0], at[1] - kSlopeProbe, at[2] };
        const float end[3] = { lower[0] + way[0] * reach, lower[1] + way[1] * reach, lower[2] + way[2] * reach };
        float hit[3];
        if (!battleGearLegs::FirstHit(lower, end, kWallFlags, hit))
            return false;
        const float lowerGap = std::sqrt((hit[0] - lower[0]) * (hit[0] - lower[0]) + (hit[1] - lower[1]) * (hit[1] - lower[1])
                                         + (hit[2] - lower[2]) * (hit[2] - lower[2]));
        return lowerGap <= gap - kSlopeProbe / kSteepestClimb;
    }

    // Further ahead the ground rises and falls away from the hull's plane, and the hull will ride it: what a ray met there
    // stands in the hull's way only if it also stands at the body's height over the ground at its own foot. `over`: metres
    // the ray ran over the ground under the hull's feet.
    bool StandsInTheWay(const float* hit, const float* way, float over)
    {
        const float foot[3] = { hit[0] - way[0] * kFootBack, hit[1], hit[2] - way[2] * kFootBack };
        float ground = 0.0f;
        if (!BattleGearGroundUnder(foot, over + kFootBelow, &ground))
            return true;
        const float lower = over - (hit[1] - ground);   // metres under the body's height there the ray met it
        if (std::fabs(lower) <= kSameGround)
            return true;
        const float reach = kFootBack + kInsideFar + (std::max)(lower, 0.0f) / kSteepestClimb;   // a face no steeper recedes this far
        const float from[3] = { foot[0], ground + over, foot[2] };
        const float to[3] = { from[0] + way[0] * reach, from[1], from[2] + way[2] * reach };
        float met[3];
        float face[3];
        return battleGearLegs::FirstHitFace(from, to, kWallFlags, met, face) && face[1] < kWalkableUp;
    }

    struct HullPose
    {
        float frame[16];   // rows: right, up, ahead and the body bone's place
        float clearance;   // metres the body bone stands over the ground under the feet
    };

    struct Sweep   // how the outline's points are cast along their paths
    {
        float margin;   // metres kept from a wall
        bool  square;   // kept square to the wall (a step: slanting, it stops further off along its own way), or along the path
        bool  braking;  // the way the run brakes over: only what still stands in the hull's way from its own ground counts
    };

    constexpr Sweep kStepSweep  = { kWallMargin, true, false };
    constexpr Sweep kTurnSweep  = { kTurnMargin, false, false };
    constexpr Sweep kBrakeSweep = { kWallMargin, true, true };

    struct Met   // what one point of the outline meets on its path
    {
        float free;     // metres it goes before it comes within the margin
        float across;   // the thing's face within the hull's plane, toward the hull
        float ahead;
        float gap;      // metres to the face itself
        float up;       // the face's normal, world up
        bool  legs;     // met under the belly, at the legs' height
    };

    struct Wall   // what stops a step first
    {
        float            share;   // of the step it lets through
        Met              met;
        const HullPoint* point;   // nullptr: nothing in the way
    };

    // What one point of the outline meets on its path (metres across and ahead within the hull's plane, with the hull's middle
    // `gone` along from where it stands); false: nothing it moves into. A point is the hull's own body, no lower than its
    // belly. Under the belly stand the legs and pods: they step over what is lower than it (a hedgehog, a barrier, a fence)
    // and stop only at a thing whose top spreads, too high to climb and nothing to step over.
    bool FreeWay(const HullPose& hull, const HullPoint& p, const float* gone, float across, float ahead, const Sweep& sweep, Met& met)
    {
        const float* m = hull.frame;
        float way[3];
        for (int k = 0; k < 3; ++k)
            way[k] = m[k] * across + m[8 + k] * ahead;
        const float length = std::sqrt(way[0] * way[0] + way[1] * way[1] + way[2] * way[2]);
        const float within = std::sqrt(across * across + ahead * ahead);
        if (length < 1e-5f || within < 1e-5f)
            return false;
        for (float& w : way)
            w /= length;
        const float reach = length + (sweep.square ? kMostKeptOff : sweep.margin);
        const float body = (std::max)(p.up, -kBellyUnder);
        const float legs = (std::max)(p.up, kLegHeight - hull.clearance);
        const float heights[2] = { body, legs };
        const int levels = legs < body - kLegBelow ? 2 : 1;
        float bellyY = 0.0f;
        bool found = false;
        for (int level = 0; level < levels; ++level)
        {
            float at[3];
            for (int k = 0; k < 3; ++k)
                at[k] = m[12 + k] + m[k] * (p.right + gone[0]) + m[4 + k] * heights[level] + m[8 + k] * (p.ahead + gone[1]);
            if (level == 0)
                bellyY = at[1];
            const float end[3] = { at[0] + way[0] * reach, at[1] + way[1] * reach, at[2] + way[2] * reach };
            float hit[3];
            float face[3];
            if (!battleGearLegs::FirstHitFace(at, end, kWallFlags, hit, face))
                continue;
            const float gap = std::sqrt((hit[0] - at[0]) * (hit[0] - at[0]) + (hit[1] - at[1]) * (hit[1] - at[1])
                                        + (hit[2] - at[2]) * (hit[2] - at[2]));
            if (GroundAhead(at, way, reach, gap, face))
            {
#ifdef _DEBUG
                ++g_SlopesPassed;
#endif
                continue;
            }
            if (level == 1)
            {
                const float inside[3] = { hit[0] + way[0] * kInsideThing, hit[1], hit[2] + way[2] * kInsideThing };
                if (!battleGearLegs::SpreadsBothWays(inside, bellyY))
                    continue;
            }
            if (sweep.braking && !StandsInTheWay(hit, way, hull.clearance + heights[level]))
            {
#ifdef _DEBUG
                ++g_PassedOver;
#endif
                continue;
            }
            float faceAcross = face[0] * m[0] + face[1] * m[1] + face[2] * m[2];
            float faceAhead = face[0] * m[8] + face[1] * m[9] + face[2] * m[10];
            const float inPlane = std::sqrt(faceAcross * faceAcross + faceAhead * faceAhead);
            if (inPlane < kFlatFace)   // the face lies along the hull's plane (a ledge's underside): met squarely
            {
                faceAcross = -across / within;
                faceAhead = -ahead / within;
            }
            else
            {
                faceAcross /= inPlane;
                faceAhead /= inPlane;
            }
            const float into = -(across * faceAcross + ahead * faceAhead) / within;
            if (into <= kGrazing)
            {
#ifdef _DEBUG
                ++g_NotInto;
#endif
                continue;
            }
            const float free = gap - (sweep.square ? (std::min)(sweep.margin / into, kMostKeptOff) : sweep.margin);
            if (found && free >= met.free)
                continue;
            met = { free, faceAcross, faceAhead, gap, face[1], level == 1 };
            found = true;
        }
        return found;
    }

    // the first wall a step meets: it is led by the outline's points on its own side of the hull, the rest follow through
    // where those went
    Wall FirstWall(const HullPose& hull, const float* gone, float across, float ahead, const Sweep& sweep)
    {
        Wall wall{ 1.0f, {}, nullptr };
        const float length = std::sqrt(across * across + ahead * ahead);
        if (length < 1e-5f)
            return wall;
        for (const HullPoint& p : kHullOutline)
        {
            if ((p.right * across + p.ahead * ahead) / length < kTrailing)
                continue;
            Met met{};
            if (!FreeWay(hull, p, gone, across, ahead, sweep, met))
                continue;
            const float share = (std::max)(0.0f, met.free / length);
            if (share < wall.share)
                wall = { share, met, &p };
        }
        return wall;
    }

    // share of a turn about the hull's middle its outline sweeps freely; held: the point that meets something first
    float TurnShare(const HullPose& hull, float turn, const HullPoint** held)
    {
        const float tc = std::cos(turn);
        const float ts = std::sin(turn);
        const float still[2] = {};
        float share = 1.0f;
        for (const HullPoint& p : kHullOutline)
        {
            const float across = p.right * tc + p.ahead * ts - p.right;
            const float ahead = -p.right * ts + p.ahead * tc - p.ahead;
            Met met{};
            if (!FreeWay(hull, p, still, across, ahead, kTurnSweep, met))
                continue;
            const float cut = (std::max)(0.0f, met.free / std::sqrt(across * across + ahead * ahead));
            if (cut >= share)
                continue;
            share = cut;
            *held = &p;
        }
        return share;
    }

    // most of its speed a step may keep: what still stops, braking kObstacleBrake, short of the first wall in its way. Only a
    // wall met squarely stops the run; one met at a slant turns it along itself (KeepHullClear) and needs no braking, and at
    // a wall already there is nothing left to brake for.
    float SpeedShareBeforeWall(const Move& move, float dt)
    {
        const float length = std::sqrt(move.localX * move.localX + move.localZ * move.localZ);
        const float speed = length / dt;
        HullPose hull{};
        if (speed < kBrakeFrom || !battleGearWeapons::HullFrame(hull.frame, &hull.clearance))
            return 1.0f;
        const float reach = speed * speed / (2.0f * kObstacleBrake) + length;
        const float still[2] = {};
        const Wall wall = FirstWall(hull, still, move.localX * reach / length, move.localZ * reach / length, kBrakeSweep);
        const float free = wall.share * reach;
        if (!wall.point || free < kAtTheWall)
            return 1.0f;
        const float into = -(move.localX * wall.met.across + move.localZ * wall.met.ahead) / length;
        const float square = std::clamp((into - kSlantFrom) / (kSlantTo - kSlantFrom), 0.0f, 1.0f);
        const float keep = 1.0f - square * (1.0f - (std::min)(1.0f, std::sqrt(2.0f * kObstacleBrake * free) / speed));
#ifdef _DEBUG
        const ULONGLONG now = GetTickCount64();
        if (keep < 1.0f && now >= g_NextBrakeLog)
        {
            g_NextBrakeLog = now + 1000;
            LogDebug("BattleGear brake: outline point (right %.1f ahead %.1f)%s meets a wall %.2f m ahead (its face points %.2f up), "
                     "%.0f%% of the run goes into it: %.1f m/s keeps %.0f%% of its speed", wall.point->right, wall.point->ahead,
                     wall.met.legs ? " at its legs' height" : "", wall.met.gap, wall.met.up, into * 100.0f, speed, keep * 100.0f);
        }
#endif
        return keep;
    }

    // The D-Walker's ball passes what is under 0.4 m, and the game's step-on state shrinks it to pass 0.7 m at a slow walk.
    // Ridden, the Battle Gear's is small and rides as high as what passes under its hull (no lower than what its legs climb),
    // following the stride's bob only in steps: its walker runs over what the hull passes, and the control sets it up onto
    // ledges at the run's own pace.
    float RiddenBodyHeight(float now)
    {
        if (!BattleGearFallCheckHooked())   // the game's fall test looks 1.5 m under the ball's middle: raised, it would always fall
            return kWalkerOverGround;
        const float wanted = (std::min)(BattleGearHighestStep() + kRiddenRadius, kHighestBody);
        return std::fabs(wanted - now) > kBodyStep ? std::floor(wanted / kBodyStep) * kBodyStep : now;
    }

    // the control only sets a walker up onto the ground. Where the ground falls away under a run (a downslope, a crest, a drop)
    // the walker left it and fell after, and 0.7 m up the game's fall clip took it: over a second at a standstill. The step
    // takes it straight down to where it stands after this frame's move, as deep as the legs reach; the hull's own height
    // rides the ground under its feet either way. Metres this frame's step falls.
    float DropToTheGround(const Body& body, float heading, const Move& move)
    {
        const float c = std::cos(heading);
        const float s = std::sin(heading);
        const float after[3] = { body.place[0] + move.localX * c + move.localZ * s, body.place[1],
                                 body.place[2] - move.localX * s + move.localZ * c };
        float ground = 0.0f;
        if (!BattleGearGroundUnder(after, body.height + kBattleGearDeepestDrop, &ground))
            return 0.0f;
        return (std::min)(0.0f, ground + body.height - kSetUnder - body.place[1]);
    }

    // The walker's own body is the D-Walker's: the Battle Gear's hull stops at walls itself. A step goes as far as the first
    // wall its outline meets lets it, and what is left of it goes on along that wall (kSlides walls a frame; in a corner it
    // stops). The turn is cleared apart from the step: one that swings a point of the outline into something turns the hull
    // about that point where it can, its middle giving way by `give` (walker-space metres, across and ahead; no part of the
    // run's speed), and is cut short where it cannot. slid: a wall took part of the step.
    Move KeepHullClear(const Move& move, float* give, bool* slid)
    {
        HullPose hull{};
        if (!battleGearWeapons::HullFrame(hull.frame, &hull.clearance))
            return move;
        Move clear = move;
        const float still[2] = {};
        const HullPoint* held = nullptr;
        const float turned = TurnShare(hull, move.turn, &held);
        if (turned < 1.0f && held)
        {
            const float tc = std::cos(move.turn);
            const float ts = std::sin(move.turn);
            const float x = held->right - (held->right * tc + held->ahead * ts);
            const float z = held->ahead - (held->ahead * tc - held->right * ts);
            if (!FirstWall(hull, still, x, z, kStepSweep).point)
            {
                give[0] = x;
                give[1] = z;
#ifdef _DEBUG
                ++g_Pivoted;
#endif
            }
            else
                clear.turn = move.turn * turned;
        }
        float gone[2] = {};
        float left[2] = { move.localX, move.localZ };
        for (int pass = 0; pass < kSlides; ++pass)
        {
            const Wall wall = FirstWall(hull, gone, left[0], left[1], kStepSweep);
            gone[0] += left[0] * wall.share;
            gone[1] += left[1] * wall.share;
            if (!wall.point)
                break;
            *slid = true;
#ifdef _DEBUG
            const ULONGLONG now = GetTickCount64();
            if (now >= g_NextWallLog)
            {
                g_NextWallLog = now + 1000;
                LogDebug("BattleGear wall: outline point (right %.1f ahead %.1f)%s meets a wall %.2f m along its step and goes %.0f%% "
                         "of it; the wall faces %.2f across, %.2f ahead, %.2f up; the rest goes on along it (wall %d of the frame)",
                         wall.point->right, wall.point->ahead, wall.met.legs ? " at its legs' height" : "", wall.met.gap,
                         wall.share * 100.0f, wall.met.across, wall.met.ahead, wall.met.up, pass + 1);
            }
#endif
            const float rest[2] = { left[0] * (1.0f - wall.share), left[1] * (1.0f - wall.share) };
            const float into = rest[0] * wall.met.across + rest[1] * wall.met.ahead;
            left[0] = rest[0] - wall.met.across * into;
            left[1] = rest[1] - wall.met.ahead * into;
        }
        clear.localX = gone[0];
        clear.localZ = gone[1];
        return clear;
    }

    void SlowToStep(const Move& step, float heading, float dt)   // a wall stops the run as well: no lurch on past the wall's end
    {
        Motion& m = g_Motion;
        if (m.fire)
        {
            const float c = std::cos(heading);
            const float s = std::sin(heading);
            m.glideX = (step.localX * c + step.localZ * s) / dt;
            m.glideZ = (-step.localX * s + step.localZ * c) / dt;
        }
        else
        {
            m.speed = step.localZ / dt;
            m.across = step.localX / dt;
        }
    }

    void ForgetMotion()
    {
        AcquireSRWLockExclusive(&g_MotionLock);
        g_Motion.known = false;
        ReleaseSRWLockExclusive(&g_MotionLock);
    }

    void StopMotion()
    {
        AcquireSRWLockExclusive(&g_MotionLock);
        g_Motion.speed = 0.0f;
        g_Motion.across = 0.0f;
        g_Motion.glideX = 0.0f;
        g_Motion.glideZ = 0.0f;
        g_Motion.capped = false;
        g_Motion.sprint = false;
        g_Motion.about = false;
        g_Motion.pivoting = false;
        ReleaseSRWLockExclusive(&g_MotionLock);
    }

    void SlowToPace(float pace)   // the game walks the walker itself (onto a ledge): the run carries on from that pace afterwards
    {
        AcquireSRWLockExclusive(&g_MotionLock);
        Motion& m = g_Motion;
        m.speed = std::clamp(m.speed, -pace, pace);
        m.across = std::clamp(m.across, -pace, pace);
        const float glide = std::sqrt(m.glideX * m.glideX + m.glideZ * m.glideZ);
        if (glide > pace)
        {
            m.glideX *= pace / glide;
            m.glideZ *= pace / glide;
        }
        m.capped = false;
        m.sprint = false;
        m.about = false;
        m.pivoting = false;
        ReleaseSRWLockExclusive(&g_MotionLock);
    }

    // the game turns the walker itself (a step-on, a fall, a damage clip): the steering takes its heading whole, and the walk's own
    // starts from it again afterwards
    void FollowWalkerHeading(const std::uint8_t* comm, bool headingKnown, float heading)
    {
        AcquireSRWLockExclusive(&g_MotionLock);
        g_Motion.steered = false;
        ReleaseSRWLockExclusive(&g_MotionLock);
        if (headingKnown && !battleGearWeapons::FireMode())
            battleGearWeapons::SteerHull(comm, heading);
    }

    // one walker frame of the Battle Gear's own movement, written over the step and turn the game set in `controller`.
    // `through`: another of the walker's actions holds the velocity callback (a hit's flinch clip) and the walk goes on
    // through it, whatever state the basic action was left in
    void WalkFrame(const std::uint8_t* action, std::uint32_t index, std::uint8_t* controller, bool through)
    {
        const bool ridden = battleGearWeapons::Ridden();
        if (!ridden)
        {
            ForgetMotion();
#ifdef _DEBUG
            g_LastPlaceKnown = false;
#endif
            if (!battleGearWeapons::OnField())
                return;
        }
        Walker w{};
        if (!controller || !ReadWalkerSEH(action, index, w))
        {
#ifdef _DEBUG
            g_UnreadCalls.fetch_add(1, std::memory_order_relaxed);
#endif
            return;
        }
        const std::uint8_t* comm = w.getComm(w.system, w.commId);
        if (!battleGearWeapons::IsBattleGearComm(comm))
            return;
#ifdef _DEBUG
        g_WalkerWork.store(w.work, std::memory_order_relaxed);
        g_VelocityContext.store(controller, std::memory_order_relaxed);
#endif
        const float walkerFrame = std::clamp(w.frameTime, 0.0f, kLongestFrame);
        battleGearWeapons::AddWalkerTime(walkerFrame);
        battleGearWeapons::CoolMg(walkerFrame);
        battleGearSound::OnWalkerFrame(w.system, index, walkerFrame);
        battleGearRoofGun::OnWalkerFrame(w.system, index, comm, w.commId, walkerFrame);
        const std::uint8_t* transform = w.getTransform(w.animation, index);
        Body body{};
        const bool bodyKnown = transform && ReadBodySEH(transform, body);
        if (!ridden)
        {
            if (bodyKnown && g_BodyShaped)   // the rider got off: the D-Walker's own ball again
                g_BodyShaped = !ShapeBodySEH(transform, body, kWalkerRadius, kWalkerOverGround);
            return;
        }
        if (bodyKnown)
        {
            ShapeBodySEH(transform, body, kRiddenRadius, RiddenBodyHeight(body.height));
            g_BodyShaped = true;
        }
        battleGearWeapons::PublishSidearmInHand(SidearmInHandSEH(w.system, w.commId));
        float heading = 0.0f;
        const bool headingKnown = transform && ReadHeadingSEH(transform, heading);
#ifdef _DEBUG
        const float* place = body.place;
        const bool placeKnown = bodyKnown;
#endif
#ifdef _DEBUG
        if (placeKnown)
        {
            const float moved = std::sqrt((place[0] - g_LastPlace[0]) * (place[0] - g_LastPlace[0])
                                          + (place[2] - g_LastPlace[2]) * (place[2] - g_LastPlace[2]));
            const float rose = place[1] - g_LastPlace[1];
            const ULONGLONG warpNow = GetTickCount64();
            if (g_LastPlaceKnown && (moved > g_LastAsked + kWarpSlack || std::fabs(rose) > kWarpSlack) && warpNow >= g_NextWarpLog)
            {
                g_NextWarpLog = warpNow + 250;
                LogDebug("BattleGear warp: the walker moved %.2f m (its step asked for %.2f) and %+.2f m up in one frame, state %u "
                         "after %u", moved, g_LastAsked, rose, w.state, g_LastState);
            }
            for (int k = 0; k < 3; ++k)
                g_LastPlace[k] = place[k];
            g_LastState = w.state;
        }
        g_LastPlaceKnown = placeKnown;
        g_LastAsked = StepLengthSEH(controller);   // the game's own step; the walk's replaces it below
        if (w.state < 32)
            g_StatesSeen |= 1u << w.state;
        if (headingKnown)
        {
            const float strayed = WrapAngle(heading - g_FacingFrom);
            g_FacingLow = (std::min)(g_FacingLow, strayed);
            g_FacingHigh = (std::max)(g_FacingHigh, strayed);
        }
#endif
        if (!through && w.state == kStepOnState)
        {
            const float pace = walkerFrame > 0.0f ? StepLengthSEH(controller) / walkerFrame : 0.0f;
            SlowToPace(pace);
            battleGearWeapons::PublishDriveSpeed(pace);
            battleGearWeapons::PublishDriveTurn(0.0f);
            battleGearWeapons::PublishPivot(false);
            FollowWalkerHeading(comm, headingKnown, heading);
            return;
        }
        if (!through && (w.state >= 32 || (kOwnMoveStates & (1u << w.state)) == 0))
        {
            StopMotion();
            battleGearWeapons::ClearSurge();
            battleGearWeapons::PublishDriveSpeed(0.0f);
            battleGearWeapons::PublishDriveTurn(0.0f);
            battleGearWeapons::PublishPivot(false);
            FollowWalkerHeading(comm, headingKnown, heading);
            return;
        }
        Input in{};
        if (!headingKnown || !ReadInputSEH(comm, in))
            return;
        const float dt = std::clamp(w.frameTime, 0.0f, kLongestFrame);
        AcquireSRWLockExclusive(&g_MotionLock);
        Move move = Advance(in, heading, dt);
        battleGearWeapons::PublishPivot(g_Motion.pivoting);
        move.localZ += battleGearWeapons::TakeSurge(kSurgeRate * dt);
        if (dt > 0.0f)   // a wall met squarely slows the run in time, the cut below only ends it
        {
            const float keep = SpeedShareBeforeWall(move, dt);
            if (keep < 1.0f)
            {
                move.localX *= keep;
                move.localZ *= keep;
                SlowToStep(move, heading, dt);
#ifdef _DEBUG
                ++g_Braked;
#endif
            }
        }
        float give[2] = {};
        bool slid = false;
        Move clear = KeepHullClear(move, give, &slid);
        float made = dt > 0.0f ? std::sqrt(clear.localX * clear.localX + clear.localZ * clear.localZ) / dt : 0.0f;
        // A wall takes the part of a step that goes into it, and the run pushes on as the stick asks: turned along the wall
        // it has its speed again. Past the wall's end the hull gathers speed from what the wall left it.
        if (g_Motion.capped)
        {
            g_Motion.wallSpeed += kRunAccel * dt;
            if (made > g_Motion.wallSpeed)
            {
                clear.localX *= g_Motion.wallSpeed / made;
                clear.localZ *= g_Motion.wallSpeed / made;
                made = g_Motion.wallSpeed;
            }
            else if (!slid)
                g_Motion.capped = false;
        }
        if (slid)
        {
            g_Motion.capped = true;
            g_Motion.wallSpeed = made;
        }
#ifdef _DEBUG
        g_WallStops += slid ? 1 : 0;
        g_Slid += slid && made > kBrakeFrom ? 1 : 0;
        if (clear.turn != move.turn)
            ++g_TurnStops;
#endif
        move = { clear.localX + give[0], clear.localZ + give[1], clear.turn, made, clear.goal };
        battleGearWeapons::PublishDriveSpeed(move.speed);
        battleGearWeapons::PublishDriveTurn(dt > 0.0f ? move.turn / dt : 0.0f);
        if (!g_Motion.fire)   // weapon-ready walkers face the steering heading directly (ParallelStandMove): it must be ours
        {
            g_Motion.steer = WrapAngle(g_Motion.steer + move.turn);
            battleGearWeapons::SteerHull(comm, g_Motion.steer);
        }
        const float rise = bodyKnown ? DropToTheGround(body, heading, move) : 0.0f;
        float gameStep[4] = {};
        const bool written = WriteMoveSEH(controller, move, rise, gameStep);
#ifdef _DEBUG
        g_LastAsked = std::sqrt(move.localX * move.localX + move.localZ * move.localZ);
        g_SetDown += rise < -2.0f * kSetUnder ? 1 : 0;
        g_DeepestSet = (std::min)(g_DeepestSet, rise);
        g_OffGround += bodyKnown && !body.onGround ? 1 : 0;
        ++g_Calls;
        const ULONGLONG now = GetTickCount64();
        if (now >= g_NextLog)
        {
            g_NextLog = now + 1000;
            const float gameSpeed = dt > 0.0f ? std::sqrt(gameStep[0] * gameStep[0] + gameStep[1] * gameStep[1] + gameStep[2] * gameStep[2]) / dt : 0.0f;
            LogDebug("BattleGear move: state %u, %s %.1f m/s toward %.1f%s | stick %.2f at %.0f deg, facing %.0f deg, turning %.0f deg/s | "
                     "D-Walker step %.2f m/s, frame %.4f s, %u calls since the last line, %u steps braked for something ahead, %u steps "
                     "and %u turns cut by a wall%s | states seen 0x%X, facing strayed %.2f / %.2f deg | body ball %.2f m at %.2f m, "
                     "set down on %u frames (deepest %.2f m), off the ground on %u, %u steps slid, %u turns about a corner, %u "
                     "outline hits were slopes, %u far hits it rides over, %u faces not gone into, the run pushes at %.1f m/s",
                     w.state, g_Motion.fire ? "glide" : (g_Motion.strafe ? "side-step" : (g_Motion.upright ? "upright" : "run")), move.speed,
                     move.goal,
                     g_Motion.sprint ? " (sprint)" : "", in.stick,
                     in.stickHeading * 57.29578f, heading * 57.29578f, dt > 0.0f ? move.turn / dt * 57.29578f : 0.0f, gameSpeed,
                     w.frameTime, g_Calls, g_Braked, g_WallStops, g_TurnStops, written ? "" : ", controller write failed", g_StatesSeen,
                     g_FacingLow * 57.29578f, g_FacingHigh * 57.29578f, body.radius, body.height, g_SetDown, -g_DeepestSet,
                     g_OffGround, g_Slid, g_Pivoted, g_SlopesPassed, g_PassedOver, g_NotInto,
                     g_Motion.fire ? std::sqrt(g_Motion.glideX * g_Motion.glideX + g_Motion.glideZ * g_Motion.glideZ)
                                   : std::sqrt(g_Motion.speed * g_Motion.speed + g_Motion.across * g_Motion.across));
            g_PassedOver = 0;
            g_NotInto = 0;
            g_Pivoted = 0;
            g_SetDown = 0;
            g_DeepestSet = 0.0f;
            g_OffGround = 0;
            g_Slid = 0;
            g_SlopesPassed = 0;
            g_Calls = 0;
            g_Braked = 0;
            g_WallStops = 0;
            g_TurnStops = 0;
            g_StatesSeen = 0;
            g_FacingFrom = heading;
            g_FacingLow = 0.0f;
            g_FacingHigh = 0.0f;
        }
#endif
        ReleaseSRWLockExclusive(&g_MotionLock);
        (void)written;
    }

    void __fastcall hkCorrectVelocity(void* action, std::uint32_t index, std::uint8_t* controller, const float* step, const float* turn)
    {
        g_Orig(action, index, controller, step, turn);
        g_BasicAction.store(static_cast<const std::uint8_t*>(action), std::memory_order_relaxed);
        WalkFrame(static_cast<const std::uint8_t*>(action), index, controller, false);
    }
}

bool Install_WalkerGearBasicActionImpl_CorrectVelocity_Hook()
{
    if (g_Target)
        return true;
    void* target = gAddr.WalkerGearBasicActionImpl_CorrectVelocity ? ResolveGameAddress(gAddr.WalkerGearBasicActionImpl_CorrectVelocity) : nullptr;
    if (!target)
    {
        LogError("BattleGear: walker movement address missing on this build, the Battle Gear moves like the D-Walker");
        return false;
    }
    const HookEntry state = CheckHookEntry(target, kCorrectVelocityEntry, sizeof(kCorrectVelocityEntry));
    if (state == HookEntry::Unknown)
    {
        char desc[160] = {};
        DescribeFunctionEntry(target, desc, sizeof(desc));
        LogError("BattleGear: walker movement code differs on this build (%s), the Battle Gear moves like the D-Walker", desc);
        return false;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("BattleGear: walker CorrectVelocity", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkCorrectVelocity), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("BattleGear: walker movement hook failed, the Battle Gear moves like the D-Walker");
        return false;
    }
    g_Target = target;
    return true;
}

void Uninstall_WalkerGearBasicActionImpl_CorrectVelocity_Hook()
{
    if (!g_Target)
        return;
    DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig = nullptr;
    g_BasicAction.store(nullptr, std::memory_order_relaxed);
    ForgetMotion();
}

void BattleGearWalkThrough(std::uint32_t index, std::uint8_t* controller)
{
    const std::uint8_t* action = g_BasicAction.load(std::memory_order_relaxed);
    if (action && g_Target)
        WalkFrame(action, index, controller, true);
}

#ifdef _DEBUG
void BattleGearWalkerStateProbe(int* state, int* next, unsigned* unreadCalls, unsigned long long* callbackVtable)   // TEMP probe
{
    *state = -1;
    *next = -1;
    *callbackVtable = 0;
    *unreadCalls = g_UnreadCalls.exchange(0, std::memory_order_relaxed);
    const std::uint8_t* work = g_WalkerWork.load(std::memory_order_relaxed);
    if (!work)
        return;
    __try
    {
        *state = work[kWorkState];
        *next = work[kWorkState + 1];
        // the hook's controller is the velocity action's context (+0x80); the action keeps its callback object at +0xB8
        const std::uint8_t* context = g_VelocityContext.load(std::memory_order_relaxed);
        const auto* callback = context ? *reinterpret_cast<const std::uint64_t* const*>(context - 0x80 + 0xB8) : nullptr;
        if (callback)
            *callbackVtable = *callback;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}
#endif
