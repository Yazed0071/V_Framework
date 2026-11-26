#include <pch.h>

#include "BattleGearLegs.h"

#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdint>

#include "AddressSet.h"
#include "BattleGearRig.h"
#include "BattleGearWeapons.h"
#include "WalkerGearBasicActionImpl_CorrectVelocity.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using BoneWorld_t           = float*(__fastcall*)(const std::uint8_t* skeleton, float* out, std::uint32_t bone);
    using AccessorCtor_t        = void*(__fastcall*)(void* accessor, const std::uint8_t* skeleton);
    using SetBoneWorld_t        = void(__fastcall*)(void* accessor, std::int32_t bone, const float* matrix);
    using ConstructGeoControl_t = void(__fastcall*)(void* control, std::uint32_t scene);
    using GeoLineCheck_t        = void(__fastcall*)(void* control, std::uint32_t flags, const float* start, const float* end, float radius,
                                                    std::uint64_t scene);
    using GetHitPosition_t      = float*(__fastcall*)(const void* hit, float* position);
    using GetHitNormal_t        = float*(__fastcall*)(const void* hit, float* normal);
    using DeleteExtraResults_t  = void(__fastcall*)(void* results, int free);
    using AkRegister_t          = std::int32_t(__fastcall*)(std::uint64_t object, const char* name, std::uint32_t listeners);
    using AkSetPosition_t       = std::int32_t(__fastcall*)(std::uint64_t object, const float* place);
    using AkPostEvent_t         = std::uint32_t(__fastcall*)(std::uint32_t event, std::uint64_t object, std::uint32_t flags, void* callback,
                                                             void* cookie, std::uint32_t externals, void* external, std::uint32_t playing);

    constexpr int kFirstRigBone = battleGearRig::kFirstBone;
    constexpr int kRigBoneCount = battleGearRig::kBones;
    constexpr int kPodPoints    = battleGearRig::kPodPoints;

    struct Vec3
    {
        float x;
        float y;
        float z;
    };

    struct Frame   // a bone world matrix as the game stores it: three axis rows and a position
    {
        Vec3 row[3];
        Vec3 origin;
    };

    struct Turn    // a part's rigid move in the shell's bind space: x -> m * x + shift
    {
        float m[3][3];
        Vec3  shift;
    };

    struct FrontLeg   // one front leg at this frame's reference pose, shell bind space
    {
        Vec3  hip;
        Vec3  knee;
        Vec3  knee2;
        Vec3  ankle;
        Vec3  axis;
        Vec3  sole;
        float kneeLow;    // the lowest knee turns: they straighten the leg at most, never bend it backward
        float knee2Low;
        Vec3  splayAxis;  // the splay pair's turn axis; turning the pair slides everything below it sideways
        Vec3  splayArm;   // from the pair's first pivot to its second
        float splayRef;   // the reference pose's own splay, radians out from the bind pose
    };

    struct RearLeg    // one rear pod at this frame's reference pose, shell bind space
    {
        Vec3 base;
        Vec3 baseAxis;
        Vec3 hip;
        Vec3 hipAxis;
        Vec3 pod[kPodPoints];
    };

    struct Foot
    {
        Vec3  planted;     // where it stands while it carries weight
        Vec3  liftLocal;   // where it left the ground, in the hull's heading frame
        float liftY;
        float liftPhase;   // where in its cycle it left the ground
        Vec3  now;
        float arc;         // metres the swing raises it this frame
        float swing;       // 0-1 through the swing
        float stood;       // cycles since it touched down
        bool  stance;
        Vec3  landing;     // where the swing will put it down
        float splayLift;   // the front splay turn it left the ground with
        float ground;      // the height its pad carries the hull on: the middle of the ground heights under its footprint
        float landingY;    // the height its swing comes down to
    };

    struct FrontPose   // radians from the reference pose, all about the leg's hinge axis
    {
        float hip;
        float knee;
        float knee2;
    };

    struct PodPose     // radians from the reference pose about btg0_hang0_def's hinge axis: the 050 base and the 054 hip
    {
        float base;
        float hip;
    };

    struct Gait          // one gait at one speed; feet 0 FL, 1 FR, 2 RL, 3 RR
    {
        float speed;
        float duty;          // share of a cycle each front foot stands on the ground
        float lift;          // metres a swinging foot rises
        float bob;           // metres the hull rises and dips with the weight on its feet
        float pitch;         // radians it rocks nose to tail as the front or rear feet carry it
        float roll;          // radians it rocks side to side
        float offsets[4];    // where in the cycle each front foot touches down (the rear pods keep their own)
    };

    struct Stride        // one foot's share of this frame's gait
    {
        float duty;
        float steps;         // its cycles per gait cycle
        float offset;
        float stance;        // metres of ground it carries while planted
    };

    struct Sway
    {
        float bob;
        float pitch;   // radians, nose up
        float roll;    // radians, the left side up
    };

    struct Tilt
    {
        float side;    // radians, the left feet up
        float front;   // radians, the front feet up
        float lift;    // metres
    };

    struct alignas(16) GeoCheckControl   // fox::geo::GeoCheckControlN<1>, two 0x60-byte hit slots
    {
        std::uint8_t bytes[0x130];
    };

    struct alignas(16) BoneMatrixAccessor
    {
        std::uint8_t bytes[0x90];
    };

    struct FrontBones
    {
        int hip;
        int knee;
        int knee2;
        int ankle;
        int splay;      // the splay pair: the second bone turns back as far as the first turns, a parallelogram
        int splayEnd;
    };

    struct RearBones
    {
        int base;
        int hip;
    };

    constexpr std::size_t   kModelSkeleton = 0xA0;
    constexpr std::size_t   kRecordModel   = 0x68;
    constexpr std::size_t   kSkeletonWorld = 0x00;   // the model's world matrix, which the game puts every bone under
    constexpr std::size_t   kSkeletonNames = 0x48;   // one u32 per bone, the low half of its name hash
    constexpr std::size_t   kSkeletonBones = 0x58;
    constexpr std::uint32_t kModelSpaceBones = 0x10000;   // in the u32 at kSkeletonBones: the bones skip the world matrix
    constexpr std::uint32_t kRootBone      = 0;      // SKL_000_ROOT
    constexpr std::uint32_t kBodyBone      = 1;      // SKL_001_BODY
    constexpr std::int16_t  kRigBones      = 99;     // the D-Walker's 34 bones, then btg0_hang0_def's rig
    constexpr std::int16_t  kRoofMgBones   = 101;    // then the roof machine gun's mount and gun
    constexpr std::uint32_t kFlLegName     = 0x75B2EE6C;   // SKL_010_LFLEG
    constexpr std::uint32_t kTurretName    = 0x06396702;   // SKL_092_TURRET
    constexpr std::uint32_t kRoofMgName    = 0x599560C1;   // SKL_614_BTG_ROOFMG

    constexpr FrontBones kFrontBones[2] = { { 38, 39, 40, 41, 35, 36 }, { 48, 49, 50, 51, 45, 46 } };
    constexpr RearBones  kRearBones[2]  = { { 54, 58 }, { 64, 68 } };
    constexpr int        kNeckBones[2]  = { 74, 75 };
    constexpr int        kTurretBone    = 76;
    constexpr int        kHeadBone      = 81;
    constexpr int        kRailBone      = 91;
    constexpr int        kRailBarrelBone = 92;
    constexpr int        kLidBone       = 77;    // the cupola's lid, hinged on its rear edge
    constexpr int        kGunBaseBone   = 79;    // SKL_094_GUNBASE, btg0_hang0_def's empty roof mount
    constexpr int        kRoofMgBone    = 99;    // turns the M2 about the turret's up axis
    constexpr int        kRoofMgGunBone = 100;   // pitches it
    constexpr int        kFirstTailBone = 82;
    constexpr int        kTailBones     = 6;
    constexpr int        kTailHandBone  = 88;    // TAILHAND's finger base: where a carried body hangs
    constexpr int        kCylinders[3][2] = { { 93, 94 }, { 95, 96 }, { 97, 98 } };   // hydraulic rams: each end points at the other
    constexpr int        kLegJoints     = 16;
    constexpr int        kJointBones[kLegJoints] = { 38, 39, 40, 41, 48, 49, 50, 51, 54, 58, 64, 68, 35, 36, 45, 46 };
    constexpr int        kFirstRearJoint = 8;
    constexpr int        kFirstSplayJoint = 12;

    // walk = 4-beat lateral (LH LF RH RF), trot = diagonal pairs, canter = 3-beat right lead, gallop = transverse 4-beat with a moment
    // in the air; values blend by speed between neighbours. A foot stands for the 1.2 m its leg reaches, so the share of a stride
    // it stands (duty) sets the beat: these give 0.8 strides a second walking, 1.15 at the trot, 1.5 at the canter, 1.9 galloping
    // and 2.1 sprinting (a horse's shares, 0.42 / 0.34 / 0.22 / 0.18, ran them at 1.3 / 2.0 / 2.6 / 3.0)
    constexpr Gait kGaits[] = {
        { 0.0f,  0.65f,  0.20f, 0.00f, 0.000f, 0.000f, { 0.25f, 0.75f, 0.00f, 0.50f } },
        { 1.5f,  0.62f,  0.22f, 0.02f, 0.017f, 0.035f, { 0.25f, 0.75f, 0.00f, 0.50f } },
        { 3.7f,  0.37f,  0.30f, 0.05f, 0.035f, 0.035f, { 0.25f, 0.75f, 0.75f, 0.25f } },
        { 7.0f,  0.25f,  0.40f, 0.07f, 0.087f, 0.035f, { 0.25f, 0.45f, 0.00f, 0.25f } },
        { 14.0f, 0.16f,  0.45f, 0.08f, 0.087f, 0.035f, { 0.25f, 0.35f, 0.00f, 0.10f } },
        { 20.0f, 0.125f, 0.45f, 0.08f, 0.087f, 0.035f, { 0.25f, 0.35f, 0.00f, 0.10f } },
    };

    // bind space; the rail-gun barrel from btg\hang0\rail_barrel.py, the MG on the head model at CNP_HEAD, the H-missile hatches
    constexpr Vec3 kBodyBind      = { 0.0f, 0.2032000571f, 0.1136000082f };
    constexpr Vec3 kRailMuzzle    = { -1.1314f, 2.1896f, 5.2044f };
    constexpr Vec3 kRailBarrel    = { 0.0709f, 0.1527f, 0.9857f };
    constexpr Vec3 kHeavyMgMuzzle = { 0.1815f, 1.6571f, 1.7803f };
    constexpr Vec3 kHatches[3]    = { { 0.4857f, 1.8062f, -1.1521f }, { -0.5154f, 1.8190f, -1.2698f }, { 0.0504f, 2.0794f, -0.4043f } };
    constexpr Vec3 kCupolaOpening = { 0.07f, 2.0794f, -0.035f };   // the middle of the round hatch beside the roof MG: the rider climbs through it
    constexpr float kRiderDrop    = 0.2f;   // metres the ride root sits below the lid: the armored-vehicle climbs then stand him on the roof
    // the roof M2 (hw01_main0_def) in its own frame, x / y / z on the shell's hinge / turret up / ahead axes at SKL_094_GUNBASE
    constexpr Vec3 kRoofMgGunPivot = { 0.0f, 0.16f, 0.04f };     // SKL_010_GUN
    constexpr Vec3 kRoofMgMuzzle   = { 0.0f, 0.206f, 1.085f };   // CNP_MUZZLE_FLASH

    constexpr std::uint32_t kGroundFlags      = 0x4000700;           // what the D-Walker's leg IK casts its foot rays against
    constexpr std::uint64_t kMainScene        = 0xD8FD1368C292ull;   // MainScene
    constexpr std::uint64_t kGeoControlMode   = 0x80000004;
    constexpr std::size_t   kGeoControlModeAt = 0x18;
    constexpr std::size_t   kGeoExtraResults  = 0x48;
    constexpr std::size_t   kExtraBorrowed    = 0x68;
    constexpr std::size_t   kGeoHitCount      = 0x60;
    constexpr std::size_t   kGeoNearestHit    = 0x64;
    constexpr std::size_t   kGeoHits          = 0x70;
    constexpr std::size_t   kGeoHitSize       = 0x60;
    constexpr int           kGeoHitSlots      = 2;

    constexpr float kRayAbove       = 2.5f;
    constexpr float kRayBelow       = 2.5f;
    constexpr float kFootStep       = 0.6f;      // metres a foot steps up onto anything
    constexpr float kFootClimb      = 1.4f;      // and onto what is broad enough to stand the hull on: the ledges its walker climbs
    constexpr float kBroadProbe     = 1.2f;      // metres round a raised spot the same top is looked for
    constexpr float kBroadDrop      = 0.35f;     // lower than this under the top, a spot is not on it
    constexpr float kSpinHover      = 0.35f;     // an about-turn swings round on the hover this high: the feet clear the ground
    constexpr float kMaxTilt        = 0.6109f;   // 35 degrees: on steeper side slopes the downhill feet were left short
    constexpr float kMaxLift        = 3.0f;
    constexpr float kHoverHeight    = 0.8f;      // fire posture glides this high
    constexpr float kHoverRate      = 0.85f;     // metres a second, lifting off and landing
    constexpr float kLiftOffHeight  = 0.3f;      // the feet leave the ground over the first part of the lift
    constexpr float kTurretTurnRate = 1.5708f;   // 90 degrees a second
    constexpr float kRailLowest     = 0.1745f;   // 10 degrees; a negative turn about the hinge axis raises the barrel
    constexpr float kRailHighest    = 0.6109f;   // 35 degrees
    constexpr float kRailTurnRate   = 1.0472f;   // 60 degrees a second
    constexpr float kRoofMgTurnRate = 2.0944f;   // 120 degrees a second, the tank cupola's
    constexpr float kRoofMgWidest   = 1.5708f;   // 90 degrees either side of the turret
    constexpr float kRoofMgLowest   = 0.2618f;   // 15 degrees down; a negative turn about the hinge axis raises the barrel
    constexpr float kRoofMgHighest  = 1.0472f;   // 60 degrees up
    constexpr float kRoofMgPitchRate = 1.5708f;  // 90 degrees a second
    constexpr float kRoofMgHeardRate = 0.0873f;  // 5 degrees a second: from here the mount's motor is heard
    constexpr float kHangFoldTime   = 1.2f;      // seconds the legs fold and the hull levels as the Fulton balloon takes the weight
    constexpr float kLidOpenMost    = 1.9199f;   // 110 degrees: up and a little back
    constexpr float kLidHandRate    = 8.0f;      // radians a second the lid follows a hand
    constexpr float kLidGravity     = 9.81f;     // radians a second squared it falls shut, as the armored vehicles' hatches do
    constexpr float kLidBounce      = -0.5f;     // its speed back off the full-open stop
    constexpr float kLidSounded     = 0.05f;     // radians open before the open sound plays
    constexpr std::uint32_t kLidOpenSound   = 0x28D4B39D;   // the armored vehicles' hatch opening
    constexpr std::uint32_t kLidShutSound   = 0xA4F0C7B0;   // and slamming shut
    constexpr std::uint64_t kLidSoundObject = 0x9F5A0002ull;
    constexpr float kHullSpring     = 12.0f;     // per second: the hull settles on its feet in about 0.3 s, critically damped
    constexpr float kMaxTiltRate    = 0.6109f;   // 35 degrees a second
    constexpr float kBellyUnderBody = 0.15f;     // metres under the body bone the hull's belly lies
    constexpr float kBellyClear     = 0.25f;     // metres the belly keeps over what stands under it
    constexpr float kBellyAcross[]  = { -0.85f, 0.0f, 0.85f };   // metres across the belly its way is read at
    constexpr float kBellyNose      = 3.1f;      // metres from the feet's middle to the belly's ends
    constexpr float kBellyTail      = 3.4f;
    constexpr float kBellyHalf      = 1.0f;      // its half width
    constexpr float kBellyLead      = 0.3f;      // seconds of travel past its leading end the ground is read: the hull rises in that time
    constexpr float kBellyEvery     = 0.3f;      // metres of travel between two readings
    constexpr float kBellyKept      = 12.0f;     // metres from the hull a reading is kept
    constexpr float kBellyUp        = 2.0f;      // m/s the rise it asks for grows,
    constexpr float kBellyDown      = 1.0f;      // and goes again
    constexpr int   kBellySpots     = 128;
    constexpr float kLegGive        = 0.2f;      // metres its legs take up under a running hull before it tilts with the ground
    constexpr float kGiveFrom       = 2.0f;      // m/s: slower, the hull lies as the ground does;
    constexpr float kGiveAt         = 7.0f;      // from here its legs give in full
    constexpr float kTiltSettle     = 0.1047f;   // 6 degrees a second: what the legs took up, the hull comes round to
    constexpr float kGiveAhead      = 2.4f;      // metres from the hull's middle to its feet, ahead and
    constexpr float kGiveAcross     = 1.8f;      // across
    constexpr float kHeightGive     = 0.12f;     // metres the ground may rise under a running hull before the hull rises with it
    constexpr float kHeightSettle   = 0.3f;      // m/s it comes up to the rest
    constexpr float kMaxLiftLag     = 0.5f;      // metres its legs give under the hull: lower it is carried
    constexpr float kLegSlack       = 0.3f;      // metres over its fitted height its legs still hold it: higher it falls freely
    constexpr float kHeightSpring   = 10.0f;     // per second: in its legs' range the hull settles on its fitted height in about 0.3 s
    constexpr float kHullFall       = 16.0f;     // m/s^2 it falls, its feet off the ground: off a drop it comes down in an arc
    constexpr float kHullPush       = 45.0f;     // m/s^2 its legs move it at most, up or down
    constexpr float kLieTrust       = 0.25f;     // metres the lie of the ground round it may move its height off what carries it
    constexpr float kFitRateSmooth  = 0.12f;     // seconds the fitted height's own speed is smoothed over
    constexpr float kFastestFit     = 15.0f;     // m/s of it that count (a ledge moves the fit a metre in a frame)
    constexpr float kFarBelow       = 12.0f;     // metres the fit lies under the hull before it is taken at once (a warp)
    constexpr float kPadRadius      = 0.3f;      // metres around a landing foot its pad samples the ground
    constexpr float kSupportReach   = 2.5f;      // a planted foot farther than this from its contact point carries nothing
    constexpr float kStepEarlyMiss  = 0.2f;      // metres a leg may miss its planted foot before that foot steps
    constexpr int   kMinHits        = 3;
    constexpr float kLieAlong[]     = { -2.5f, 0.0f, 2.5f, 5.0f };   // metres along the hull from its feet's middle, and to
    constexpr float kLieAcross      = 1.6f;      // either side: where the ground round the hull is sampled for its lie
    constexpr int   kLieSamples     = 8;
    constexpr float kLieLead        = 0.2f;      // seconds of travel the samples lie ahead: a slope tilts the hull before it is on it
    constexpr float kLieMostLead    = 3.0f;      // metres
    constexpr float kLieOutlier     = 0.8f;      // metres off the plane: a sample on a wall, a wreck or in a pit says nothing of the lie
    constexpr int   kLieFewest      = 6;         // points the plane needs
    constexpr float kFrontHipBack   = -0.6109f;  // -35 degrees from the posture's own pose
    constexpr float kFrontHipFwd    = 0.6981f;   // 40 degrees
    constexpr float kFrontKneeLimit = 1.0472f;   // knee and second knee, 60 degrees either way, never past straight: with 45 a
                                                 // foot at the rear end of its stride lifted 0.05 m over level ground at most
    constexpr float kKneeNudge      = 0.0175f;   // 1 degree: a straight knee starts each solve this far into its bend, a straight leg
                                                 // has no first-order way to fold
    constexpr float kSplayLow       = -0.2793f;  // -16 degrees: the front legs tuck in this far (the hangar cutscene spreads 0..37)
    constexpr float kSplayHigh      = 0.7854f;   // 45 degrees out
    constexpr float kSplayRate      = 20.0f;     // radians a second: only stops a jump after a warp or a reset
    constexpr float kSplayFadeFrom  = 7.0f;      // m/s: the splay works fully up to the canter and is gone at a gallop, where the feet
    constexpr float kSplayFadeTo    = 11.0f;     // stand too briefly to slide and the shifted legs flicker between solutions
    constexpr int   kSplaySearchSteps = 24;
    constexpr float kIkDamping      = 0.05f;
    constexpr int   kIkIterations   = 12;
    constexpr float kIkRest         = 0.15f;     // share of its turn off the reference pose a front leg gives back each pass, with
                                                 // the one turn its three joints have to spare: the ankle stays where it is
    constexpr float kIkRestWithin   = 0.1f;      // metres from its foothold the ankle is before the leg gives any back: reaching
                                                 // comes first
    constexpr float kRetrySeeds     = 0.02f;     // metres a warm-started leg may miss before other start poses are tried
    constexpr float kSeedGain       = 0.01f;     // how much closer another start pose must get to be taken
    constexpr FrontPose kSeeds[]    = { { -0.2618f, 0.5236f, 0.6981f }, { 0.2618f, -0.5236f, -0.6981f }, { -0.3491f, 0.6981f, -0.3491f },
                                        { 0.1745f, 0.3491f, -0.6981f }, { 0.0f, 0.0f, 0.0f } };
    constexpr float kPodBaseLow     = -0.7854f;  // -45 degrees
    constexpr float kPodBaseHigh    = 0.5236f;   // 30 degrees
    constexpr float kPodHipLow      = -0.9599f;  // -55 degrees
    constexpr float kPodHipHigh     = 0.6981f;   // 40 degrees
    constexpr float kMaxPodTurn     = 0.6109f;   // 35 degrees: further the pod tips onto its ends
    constexpr int   kPodIterations  = 8;
    constexpr float kMaxPodStep     = 0.2618f;   // 15 degrees a step
    constexpr int   kPodBacktracks  = 4;
    constexpr int   kPodGrid        = 11;
    constexpr float kPodCloseness   = 0.05f;     // metres of miss one radian away from last frame's pose is worth when picking a start
    constexpr float kPodTurnRate    = 8.0f;      // radians a second the pods turn at most: a solver jump shows as a quick swing
    constexpr float kFrontCloseness = 0.15f;     // metres of miss one radian from last frame's front pose is worth: no knee flips
                                                 // back and forth while a foothold is out of reach
    constexpr float kFrontJointRate = 10.0f;     // radians a second a front joint may turn standing; a stride adds kFrontJointPerSpeed per m/s
    constexpr float kFrontJointPerSpeed = 2.0f;  // of travel (the sim's gallop needs 26 rad/s at 14 m/s, 37 at 20): a solution that flips
                                                 // further is approached over a few frames, not jumped to
    constexpr float kPodSolveShare  = 2.0f;      // times kPodTurnRate: the most a pod's solution may move in one frame
    constexpr float kFeetLost       = 8.0f;      // metres a foot may stand from the hull before the hull counts as relocated
    constexpr float kFeetAdrift     = 2.5f;      // metres over or under a parked hull's own contacts the ground of every foot may
                                                 // lie (more than any step it climbs) before its feet start over under it
    constexpr float kReached       = 0.01f;
    constexpr int   kArcSearchSteps = 6;
    constexpr float kTallFrom       = 2.6f;      // m/s: without a rider normal mode walks crouched and stands up into the trot
    constexpr float kTallAt         = 3.7f;
    constexpr float kUprightShare   = 0.6f;      // how far upright stands from normal mode's legs toward the bind pose: the whole way
                                                 // stood it on straight stilts; here the knees keep 17 / 32 degrees to give, 2.89 m tall
    constexpr float kUprightStride  = 0.8f;      // posture the upright stance walks at: the bind pose's straight front legs reach 0.44 m
                                                 // of ground, here (knees 4 / 15 degrees) 1.34 m; standing still it is the full bind pose
    constexpr float kUprightLean    = -0.1745f;  // -10 degrees: walking upright the front legs lean forward on their bases, so a step
                                                 // lands 0.7 m ahead of the hip instead of 0.24 and covers 1.93 m of ground, not 1.34
    constexpr int   kFrontBaseBones[2] = { 34, 44 };   // 010, the front legs' base pitch
    constexpr float kLeanFrom       = 0.3f;      // m/s: the lean grows with the walk and is gone standing or turning on the spot, where
    constexpr float kLeanAt         = 1.2f;      // the leaned feet would slide sideways
    constexpr float kLeanRate       = 1.0f;      // share of the lean a second
    constexpr float kPostureRate    = 0.7f;      // share of the stand-up a second, and of a switch between the two stand-ups
    constexpr float kNeckRate       = 2.0f;      // share of the neck lift a second: the turret rises in half a second while zooming
    constexpr float kFrontStanceShare = 0.9f;    // of the ground the front legs reach
    constexpr float kFrontReachSpare  = 0.15f;   // metres at the far end of that reach the stride leaves out: a foot set down there
                                                 // reaches barely under the level it stands on, and uneven ground lies lower
    constexpr float kRearStanceShare  = 0.75f;
    constexpr float kRearMargin     = 0.03f;     // a pod's target stays this far inside what it reaches
    constexpr float kRearSteps      = 2.0f;      // the pods step twice for every front stride
    constexpr float kRearOffsets[2] = { 0.0f, 0.5f };
    constexpr float kMinRearDuty    = 0.15f;
    constexpr float kFloatFrom      = 2.6f;      // m/s: from the trot the pods glide on the hover instead of stepping
    constexpr float kLandBelow      = 2.2f;
    constexpr float kFloatTurn      = 0.8f;      // rad/s: turning faster they glide too
    constexpr float kLandTurn       = 0.5f;
    constexpr float kLandPosture    = 0.05f;     // standing tall the pods never step: they land only with the hull crouched again
    constexpr float kFloatHeight    = 0.10f;
    constexpr float kFloatBob       = 0.03f;
    constexpr float kFloatEase      = 0.3f;      // seconds to lift off or land
    constexpr float kArcSkew[4]     = { 1.4f, 1.4f, 0.7f, 0.7f };   // front feet rise late (they cannot lift far back), rear pods early
    constexpr float kTouchMatch     = 0.25f;     // share of the ground's speed a foot matches as it lands
    constexpr float kSwingDrop      = 6.0f;      // m/s a swinging foot comes down faster than the hull falls: down a drop it reaches
                                                 // for the ground instead of being thrown at it
    constexpr float kLandWithin     = 0.05f;     // metres over its landing spot a swing that has run its time still has to come down
    constexpr float kRebaseUntil    = 0.8f;      // share of its way down a swinging foot has come: before it, a new landing height
                                                 // re-plans the rest of the swing from where the foot is; after it, the foot keeps
                                                 // the height it was coming down to
    constexpr float kMinCadence     = 1.0f;      // gait cycles a second while settling: the feet get home briskly
    constexpr float kWalkMinCadence = 0.45f;     // moving slowly it keeps its full stance at a slower beat, not quick short steps
    constexpr float kMaxCadence     = 2.2f;      // past this beat the feet slip along rather than step faster
    constexpr float kFootSpread     = 1.9f;      // metres from the hull's middle to its outer feet: turning sweeps them along the hull
    constexpr float kTurnGaitSpeed  = 3.7f;      // turning on the spot strides as a walk at its feet's speed does, up to the trot's
    constexpr float kSideStanceShare = 0.8f;     // of the sideways ground the front splay pairs reach: a side-step's stance
    constexpr float kSideEase       = 0.3f;      // m/s: the front feet move into the middle of the splay's reach as a side-step gets going
    constexpr float kSpotTurnTo     = 1.5f;      // m/s ahead: turning on the spot sweeps the front feet sideways and their steps follow
                                                 // that sweep; walking, turns step as the walk does
    constexpr float kUprightLift    = 0.40f;     // metres the upright front feet rise: the D-Walker lifts 0.19 m on a 1.44 m leg, these
                                                 // legs are 3.05 m
    // the D-Walker walk loop's swing (wkrg_s_walk_lp, right foot, frames 30-65): its height as a share of the highest point and its
    // way from the spot it left to the spot it lands on, per frame - up at once, forward early, highest late, straight down
    constexpr int   kStepSamples    = 36;
    constexpr float kStepHeight[kStepSamples]  = { 0.000f, 0.093f, 0.265f, 0.358f, 0.354f, 0.346f, 0.335f, 0.348f, 0.362f, 0.415f, 0.475f,
                                                   0.535f, 0.594f, 0.653f, 0.710f, 0.762f, 0.810f, 0.854f, 0.890f, 0.927f, 0.951f, 0.975f,
                                                   0.990f, 0.993f, 0.997f, 1.000f, 0.978f, 0.956f, 0.902f, 0.820f, 0.738f, 0.597f, 0.406f,
                                                   0.212f, 0.061f, 0.000f };
    constexpr float kStepForward[kStepSamples] = { 0.000f, 0.008f, 0.028f, 0.057f, 0.091f, 0.129f, 0.167f, 0.202f, 0.238f, 0.279f, 0.327f,
                                                   0.384f, 0.446f, 0.509f, 0.568f, 0.618f, 0.656f, 0.685f, 0.710f, 0.735f, 0.763f, 0.791f,
                                                   0.812f, 0.828f, 0.845f, 0.861f, 0.879f, 0.898f, 0.915f, 0.931f, 0.947f, 0.962f, 0.976f,
                                                   0.987f, 0.996f, 1.000f };
    constexpr float kMoving         = 0.15f;     // m/s, or rad/s turning, below which it stands
    constexpr float kSettled        = 0.12f;     // a planted foot this close to its home needs no step to stand still
    constexpr float kSlipShare      = 0.55f;     // a planted foot slips once it is this share of the stance away from its home
    constexpr float kPlantSettle    = 6.0f;      // m/s a planted foot that came down at the height its swing kept slides onto
                                                 // the ground its pad stands on
    constexpr float kMinStand       = 0.5f;      // share of its stance a landed foot stands before it may lift again
    constexpr float kMinSwing       = 0.6f;      // a foot lifting late needs this share of a full swing left, else it waits a cycle
    constexpr int   kLoadSamples    = 24;
    constexpr float kSquat          = 0.008f;    // radians nose up per m/s^2 of speeding up; braking dips the nose
    constexpr float kMaxSquat       = 0.0611f;   // 3.5 degrees
    constexpr float kBank           = 0.0105f;   // radians of lean into a turn per m/s^2 of sideways pull
    constexpr float kMaxBank        = 0.1047f;   // 6 degrees
    constexpr float kFireTilt       = 0.6f;      // share of the squat and bank the hovering hull takes: aiming stays usable
    constexpr float kFireNose       = 0.01f;     // radians the hovering hull leans into its glide per m/s, nose down ahead, up backing
    constexpr float kFireSide       = 0.01f;     // and onto the side it glides to
    constexpr float kFireMost       = 0.0873f;   // 5 degrees
    constexpr float kSwayLag        = 0.1f;      // seconds the hull takes to follow the load on its feet: its weight smooths each footfall
    constexpr float kUprightSway    = 0.4f;      // share of the gait's bob and pitch left upright: the near-straight legs cannot follow more
    constexpr float kTailBob        = 1.5f;      // radians the tail rises per metre the hull dips
    constexpr float kTailSurge      = 0.02f;     // radians per m/s^2 of speeding up
    constexpr float kTailTurn       = 0.15f;     // radians it swings out per rad/s of turning
    constexpr float kTailMost       = 0.3f;
    constexpr float kTailStiffness  = 7.0f;      // rad/s
    constexpr float kTailDamping    = 0.4f;
    constexpr float kCarryBehind    = 1.0f;      // metres behind the hull's middle: the box the rider's carry prompt searches, from a
    constexpr float kCarryHalf      = 4.5f;      // body under the nose to one behind the rear pods, and as far to either side
    constexpr float kCarryAbove     = 0.4f;      // the box centre above the ground
    constexpr Vec3  kCarryHold      = { 0.0f, -1.0f, -0.85f };   // under the rear: across, up, ahead of the hull's middle (level axes)
    constexpr float kTailCarryRate  = 2.0f;      // share a second the tail moves between its sway and the carry
    constexpr float kTailJointRate  = 3.0f;      // radians a second each tail joint turns at most
    constexpr float kTailJointLimit = 1.2f;      // radians either way
    constexpr int   kTailIterations = 8;
    constexpr float kCarryLiftAfter = 0.8f;      // seconds of a pick-up the tail spends down at the body before it curls it in
    constexpr float kCarryCurlTime  = 1.0f;
    constexpr float kTeleport       = 1.0f;      // metres the hull may move in one frame before the feet are put back under it
    constexpr float kFastest        = 30.0f;     // m/s
    constexpr float kHeadingJump    = 0.3f;      // radians the heading may turn in one frame before the feet are put back
    constexpr float kMaxYawRate     = 3.0f;
    constexpr float kIdleSpeed      = 0.2f;
    constexpr float kStillTurnRate  = 0.2f;      // radians a second; turning slower counts as standing still
    constexpr float kRestFollow     = 0.05f;     // share of the gap to the standing body's place learnt each still frame
    constexpr float kRootLevel      = 0.0524f;   // 3 degrees: a D-Walker root pitched further is leaning into a move or a turn
    constexpr float kMaxModelGap    = 5.0f;      // a body farther than this from its model: the matrix is not the walker's
    constexpr float kRecoilPitch[3] = { 0.02f, 0.04f, 0.07f };   // nose up after a rail-gun shot of level 1-3
    constexpr float kRecoilSettle   = 8.0f;      // the kick shrinks by e every 1/8 s
    constexpr float kVelocityFollow = 0.3f;
    constexpr float kAccelFollow    = 0.08f;
    constexpr float kFrameFollow    = 0.1f;      // the frame time is smoothed: the animation job's own timing jitters
    constexpr float kLongestFrame   = 0.1f;
    constexpr float kPi             = 3.14159265f;

    const std::uint8_t kBoneWorldEntry[]           = { 0x48, 0x83, 0xEC, 0x58, 0x41, 0x8B, 0xC0, 0x48, 0xC1, 0xE0, 0x06, 0x48, 0x03,
                                                       0x41, 0x40 };
    const std::uint8_t kAccessorCtorEntry[]        = { 0x4C, 0x8B, 0xDC, 0x48, 0x81, 0xEC, 0x98, 0x00, 0x00, 0x00, 0x0F, 0x28, 0x02 };
    const std::uint8_t kSetBoneWorldEntry[]        = { 0x48, 0x83, 0xEC, 0x58, 0x80, 0xB9, 0x8C, 0x00, 0x00, 0x00, 0x00 };
    const std::uint8_t kConstructGeoControlEntry[] = { 0x48, 0x89, 0x4C, 0x24, 0x08, 0x48, 0x83, 0xEC, 0x18, 0x48, 0xC7, 0x04, 0x24,
                                                       0xFE, 0xFF, 0xFF, 0xFF };
    const std::uint8_t kGeoLineCheckEntry[]        = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74,
                                                       0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B, 0xE9, 0x48, 0x8B, 0x4C,
                                                       0x24, 0x68 };
    const std::uint8_t kGetHitPositionEntry[]      = { 0x48, 0x83, 0xEC, 0x48, 0xF3, 0x0F, 0x10, 0x41, 0x08, 0xF3, 0x0F, 0x10, 0x51,
                                                       0x04, 0x48, 0x8B, 0x41, 0x58 };
    const std::uint8_t kGetHitNormalEntry[]        = { 0x48, 0x81, 0xEC, 0x88, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x10, 0x41, 0x28, 0xF3,
                                                       0x0F, 0x10, 0x51, 0x24, 0x48, 0x8B, 0x41, 0x58 };

    BoneWorld_t           g_BoneWorld = nullptr;
    AccessorCtor_t        g_AccessorCtor = nullptr;
    SetBoneWorld_t        g_SetBoneWorld = nullptr;
    ConstructGeoControl_t g_ConstructGeoControl = nullptr;
    GeoLineCheck_t        g_GeoLineCheck = nullptr;
    GetHitPosition_t      g_GetHitPosition = nullptr;
    GetHitNormal_t        g_GetHitNormal = nullptr;   // missing, a hit's face is taken as square to the ray
    AkRegister_t          g_AkRegister = nullptr;
    AkSetPosition_t       g_AkSetPosition = nullptr;
    AkPostEvent_t         g_AkPostEvent = nullptr;
    std::atomic<bool>     g_Ready{ false };
    int                   g_JointOf[kRigBoneCount] = {};   // the leg joint whose IK move carries each rig bone, -1 for none

    std::atomic<std::uint8_t*> g_Record{ nullptr };
    std::atomic<std::uint8_t*> g_Model{ nullptr };
    std::atomic<bool>          g_Reset{ true };
    float                      g_PodsRest = 0.0f;     // the rear pods' contacts above the front soles, crouched

    // only the animation job that poses the field Battle Gear touches these
    Tilt          g_Tilt{};
    Tilt          g_TiltRate{};      // its springs' speeds
    Vec3          g_BellySpots[kBellySpots] = {};   // the ground read along the belly's way, kept while the hull passes over it
    bool          g_BellySpotKept[kBellySpots] = {};
    int           g_BellyNext = 0;
    Vec3          g_BellyReadAt{};
    bool          g_BellyRead = false;
    float         g_BellyRise = 0.0f;
    Tilt          g_TiltGoal{};      // what its springs pull it to: the ground's lie, less what the legs take up
    float         g_GroundHeight = 0.0f; // the ground fitted under its feet (its posture taken out), without its steps
    float         g_GroundClimb = 0.0f;  // metres a second it climbs
    float         g_FitRate = 0.0f;      // metres a second the fit itself climbs, smoothed
    float         g_LastFitted = 0.0f;
    float         g_FitLift = 0.0f;      // the last ground fit's lift over the walker's body
    float         g_FootMiss[4]{};   // last frame's leg miss for each planted foot
    int           g_EarlySteps = 0;
    float         g_LidAngle = 0.0f;     // radians the cupola's lid stands open
    float         g_LidSpeed = 0.0f;
    bool          g_LidSounded = false;  // its open sound played since it last shut
    Foot          g_Feet[4]{};
    FrontPose     g_FrontPose[2]{};
    float         g_Splay[2]{};      // each front splay pair's turn from its reference pose: slides the planted foot's leg sideways
    PodPose       g_PodPose[2]{};    // last stepping solution, the next one's start
    PodPose       g_PodFloat[2]{};   // last gliding solution
    PodPose       g_PodHold[2]{};    // the stepping pose the pods glide away from
    PodPose       g_PodShown[2]{};
    Vec3          g_LastBodyPos{};
    Vec3          g_Velocity{};
    Vec3          g_FireVelocity{};   // the hull's glide while it hovers (g_Velocity stays zero there: the gait and the pods ignore it)
    float         g_FireAccel = 0.0f;
    Vec3          g_LastBasePos{};
    float         g_YawRate = 0.0f;
    float         g_Accel = 0.0f;
    float         g_LastSpeed = 0.0f;
    float         g_SmoothFrame = 0.0f;
    float         g_MeasuredFrame = 0.0f;   // this frame's own length: a hitch carries more motion than the smoothed frame
    float         g_Cycle = 0.0f;       // 0-1 through the gait cycle
    float         g_Cadence = 0.0f;     // cycles a second this frame
    float         g_RearDuty = 0.0f;
    float         g_Posture = 0.0f;     // 0 crouched, 1 stood up
    Frame         g_HangFrom{};         // the hull as the field pose last showed it, and how far that stood from the animated body bone
    Vec3          g_HangOffset{};
    float         g_HangFold = 0.0f;    // 0 as it stood, 1 hanging level under the Fulton balloon
    float         g_Upright = 0.0f;     // what it stands up into: 0 normal mode's legs (cutscene frame 2340), 1 the bind pose
    float         g_Lean = 0.0f;        // share of the upright walk's lean
    float         g_NeckLift = 0.0f;    // 0 the turret seated, 1 raised on its neck (while zooming)
    bool          g_Floating = false;
    bool          g_FeetAdrift = false;   // last frame every foot's ground lay far over or under the parked hull
    float         g_FloatShare = 0.0f;
    float         g_LastHeading = 0.0f;
    float         g_WorstFrontMiss = 0.0f;
    float         g_WorstPlantedMiss = 0.0f;   // TEMP probe: the same, of the front feet that stand
#ifdef _DEBUG
    Vec3          g_LastAim[2] = {};           // TEMP probe: where each front foot was sent on the last frame, in the shell's bind space
    float         g_LastLanding[2] = {};
#endif
    float         g_WorstRearMiss = 0.0f;
    std::int64_t  g_LastTick = 0;
    float         g_TurretYaw = 0.0f;
    float         g_RailPitch = 0.0f;
    float         g_RoofMgYaw = 0.0f;     // the M2's turn on its mount, from the turret's front
    float         g_RoofMgPitch = 0.0f;
    Sway          g_Sway{};
    float         g_TailPitch = 0.0f;
    float         g_TailYaw = 0.0f;
    float         g_TailPitchRate = 0.0f;
    float         g_TailYawRate = 0.0f;
    float         g_TailCarry[kTailBones]{};   // each tail joint's pitch while it carries
    float         g_TailCarryShare = 0.0f;
    float         g_CarryClock = 0.0f;         // seconds in this carry phase
#ifdef _DEBUG
    ULONGLONG     g_NextGapLog = 0;            // TEMP probe: the walker's state while its walk frames stay away
#endif
    Vec3          g_CarryFrom{};               // where the body lay that the tail picks up
    battleGearWeapons::CarryPhase g_LastCarry = battleGearWeapons::CarryPhase::None;
    float         g_Hover = 0.0f;
    float         g_PlaneUnder[4]{};           // height of the fitted ground under each contact point, last frame
    bool          g_PlaneKnown[4]{};
    Vec3          g_LiftOffFeet[4]{};
    Vec3          g_RestOffset{};    // the standing body's place from the model: across, up, ahead
    bool          g_RestKnown = false;
    bool          g_ModelWarned = false;
    float         g_LastYaw = 0.0f;
    float         g_RecoilKick = 0.0f;
    std::uint32_t g_SeenRailShots = 0;

    SRWLOCK       g_HoverLock = SRWLOCK_INIT;
    bool          g_Gliding = false;
    Vec3          g_GroundUnderHull{};
#ifdef _DEBUG
    std::atomic<ULONGLONG> g_NextLog{ 0 };
    constexpr float kTwitchTurn  = 0.3f;     // radians a pod solution may jump in one frame before it is logged
    constexpr float kTwitchLeg   = 0.55f;    // the same for the front legs: a gallop stride turns a planted one up to 0.45
    constexpr float kTwitchAim   = 0.4f;     // metres a front foot's spot moves in the hull's frame in one frame (a sprint's swing: 0.33)
    constexpr float kTwitchJump  = 0.08f;    // metres the hull may move in one frame beyond its own travel
    constexpr float kTwitchTilt  = 0.0436f;  // 2.5 degrees the hull may turn in one frame
    constexpr int   kTwitchLines = 8;        // a second
    ULONGLONG     g_TwitchSecond = 0;
    int           g_TwitchCount = 0;
    Vec3          g_LastShownOrigin{};
    Vec3          g_LastBaseOrigin{};
    Vec3          g_LastShownUp{};
    Tilt          g_LastTilt{};
    bool          g_ShownKnown = false;
    float         g_SeenYawRate = 0.0f;   // TEMP probe: the turn read off the model's heading, which the ridden gait no longer uses,
    float         g_TurnedLow = 0.0f;     // and the model's least and most heading change in one frame since the last line
    float         g_TurnedHigh = 0.0f;
#endif

#ifdef _DEBUG
    bool TwitchLogAllowed()
    {
        const ULONGLONG second = GetTickCount64() / 1000;
        if (second != g_TwitchSecond)
        {
            g_TwitchSecond = second;
            g_TwitchCount = 0;
        }
        return g_TwitchCount++ < kTwitchLines;
    }
#endif

    Vec3 Add(Vec3 a, Vec3 b)
    {
        return { a.x + b.x, a.y + b.y, a.z + b.z };
    }

    Vec3 Sub(Vec3 a, Vec3 b)
    {
        return { a.x - b.x, a.y - b.y, a.z - b.z };
    }

    Vec3 Scale(Vec3 a, float s)
    {
        return { a.x * s, a.y * s, a.z * s };
    }

    float Dot(Vec3 a, Vec3 b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    Vec3 Cross(Vec3 a, Vec3 b)
    {
        return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
    }

    float Length(Vec3 a)
    {
        return std::sqrt(Dot(a, a));
    }

    Vec3 Normalized(Vec3 a)
    {
        const float length = Length(a);
        return length > 1e-6f ? Scale(a, 1.0f / length) : a;
    }

    Vec3 Flat(Vec3 a)
    {
        return { a.x, 0.0f, a.z };
    }

    Vec3 Planar(Vec3 v, Vec3 axis)
    {
        return Sub(v, Scale(axis, Dot(v, axis)));
    }

    Vec3 Rotate(Vec3 v, Vec3 axis, float angle)
    {
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        return Add(Add(Scale(v, c), Scale(Cross(axis, v), s)), Scale(axis, Dot(axis, v) * (1.0f - c)));
    }

    float SignedAngle(Vec3 from, Vec3 to, Vec3 axis)
    {
        return std::atan2(Dot(axis, Cross(from, to)), Dot(from, to));
    }

    Vec3 Lerp(Vec3 a, Vec3 b, float t)
    {
        return Add(a, Scale(Sub(b, a), t));
    }

    float Ease(float t)
    {
        return t * t * (3.0f - 2.0f * t);
    }

    Vec3 FromPoint(const battleGearRig::Point& p)
    {
        return { p.x, p.y, p.z };
    }

    Vec3 Bind(int bone)
    {
        return FromPoint(battleGearRig::kBind[bone - kFirstRigBone]);
    }

    int Parent(int bone)
    {
        return battleGearRig::kParent[bone - kFirstRigBone];
    }

    Turn Identity()
    {
        return { { { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } }, { 0.0f, 0.0f, 0.0f } };
    }

    Turn Translate(Vec3 by)
    {
        Turn t = Identity();
        t.shift = by;
        return t;
    }

    Vec3 Apply(const Turn& t, Vec3 x)
    {
        return { t.m[0][0] * x.x + t.m[0][1] * x.y + t.m[0][2] * x.z + t.shift.x,
                 t.m[1][0] * x.x + t.m[1][1] * x.y + t.m[1][2] * x.z + t.shift.y,
                 t.m[2][0] * x.x + t.m[2][1] * x.y + t.m[2][2] * x.z + t.shift.z };
    }

    Vec3 ApplyDir(const Turn& t, Vec3 x)
    {
        return { t.m[0][0] * x.x + t.m[0][1] * x.y + t.m[0][2] * x.z,
                 t.m[1][0] * x.x + t.m[1][1] * x.y + t.m[1][2] * x.z,
                 t.m[2][0] * x.x + t.m[2][1] * x.y + t.m[2][2] * x.z };
    }

    Turn RotationAbout(const float r[3][3], Vec3 pivot)
    {
        Turn t{};
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                t.m[i][j] = r[i][j];
        t.shift = Sub(pivot, ApplyDir(t, pivot));
        return t;
    }

    Turn TurnAbout(Vec3 axis, float angle, Vec3 pivot)
    {
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        const float k = 1.0f - c;
        const float a[3] = { axis.x, axis.y, axis.z };
        const float skew[3][3] = { { 0.0f, -axis.z, axis.y }, { axis.z, 0.0f, -axis.x }, { -axis.y, axis.x, 0.0f } };
        float r[3][3];
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                r[i][j] = (i == j ? c : 0.0f) + k * a[i] * a[j] + s * skew[i][j];
        return RotationAbout(r, pivot);
    }

    Turn Then(const Turn& first, const Turn& second)
    {
        Turn t{};
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                t.m[i][j] = second.m[i][0] * first.m[0][j] + second.m[i][1] * first.m[1][j] + second.m[i][2] * first.m[2][j];
        t.shift = Apply(second, first.shift);
        return t;
    }

    battleGearRig::Quat Slerp(const battleGearRig::Quat& a, const battleGearRig::Quat& b, float t)
    {
        float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
        float wa = 1.0f - t;
        float wb = d < 0.0f ? -t : t;   // the shorter way round
        d = std::fabs(d);
        if (d < 0.9995f)
        {
            const float angle = std::acos(std::clamp(d, -1.0f, 1.0f));
            const float s = std::sin(angle);
            wa = std::sin(wa * angle) / s;
            wb = std::sin(wb * angle) / s;
        }
        battleGearRig::Quat q{ a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb };
        const float length = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
        q = { q.x / length, q.y / length, q.z / length, q.w / length };
        return q;
    }

    void QuatRotation(const battleGearRig::Quat& q, float r[3][3])
    {
        const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
        const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
        const float xw = q.x * q.w, yw = q.y * q.w, zw = q.z * q.w;
        r[0][0] = 1.0f - 2.0f * (yy + zz);
        r[0][1] = 2.0f * (xy - zw);
        r[0][2] = 2.0f * (xz + yw);
        r[1][0] = 2.0f * (xy + zw);
        r[1][1] = 1.0f - 2.0f * (xx + zz);
        r[1][2] = 2.0f * (yz - xw);
        r[2][0] = 2.0f * (xz - yw);
        r[2][1] = 2.0f * (yz + xw);
        r[2][2] = 1.0f - 2.0f * (xx + yy);
    }

    Frame FromMatrix(const float* m)
    {
        return { { { m[0], m[1], m[2] }, { m[4], m[5], m[6] }, { m[8], m[9], m[10] } }, { m[12], m[13], m[14] } };
    }

    void ToMatrix(const Frame& f, float* m)
    {
        for (int r = 0; r < 3; ++r)
        {
            m[r * 4] = f.row[r].x;
            m[r * 4 + 1] = f.row[r].y;
            m[r * 4 + 2] = f.row[r].z;
            m[r * 4 + 3] = 0.0f;
        }
        m[12] = f.origin.x;
        m[13] = f.origin.y;
        m[14] = f.origin.z;
        m[15] = 1.0f;
    }

    Vec3 ToWorld(const Frame& body, Vec3 bind)
    {
        const Vec3 local = Sub(bind, kBodyBind);
        return Add(Add(Add(Scale(body.row[0], local.x), Scale(body.row[1], local.y)), Scale(body.row[2], local.z)), body.origin);
    }

    Vec3 BodyDir(const Frame& body, Vec3 bind)
    {
        return Add(Add(Scale(body.row[0], bind.x), Scale(body.row[1], bind.y)), Scale(body.row[2], bind.z));
    }

    Vec3 IntoAxes(const Frame& f, Vec3 v)
    {
        return { Dot(v, f.row[0]) / Dot(f.row[0], f.row[0]), Dot(v, f.row[1]) / Dot(f.row[1], f.row[1]),
                 Dot(v, f.row[2]) / Dot(f.row[2], f.row[2]) };
    }

    Vec3 ToBind(const Frame& body, Vec3 world)
    {
        return Add(kBodyBind, IntoAxes(body, Sub(world, body.origin)));
    }

    Vec3 UpInBind(const Frame& body)
    {
        return Normalized(IntoAxes(body, { 0.0f, 1.0f, 0.0f }));
    }

    Frame PartFrame(const Frame& body, const Turn& turn, Vec3 pivot)
    {
        Frame f{};
        for (int j = 0; j < 3; ++j)
            f.row[j] = Add(Add(Scale(body.row[0], turn.m[0][j]), Scale(body.row[1], turn.m[1][j])), Scale(body.row[2], turn.m[2][j]));
        f.origin = ToWorld(body, Apply(turn, pivot));
        return f;
    }

    bool LineHit(Vec3 from, Vec3 to, std::uint32_t flags, Vec3& at, Vec3* face = nullptr)   // face: the hit surface's normal
    {
        alignas(16) float start[4]  = { from.x, from.y, from.z, 0.0f };
        alignas(16) float end[4]    = { to.x, to.y, to.z, 0.0f };
        alignas(16) float hit[4]    = {};
        alignas(16) float normal[4] = {};
        GeoCheckControl control;
        g_ConstructGeoControl(&control, 0);
        *reinterpret_cast<std::uint64_t*>(control.bytes + kGeoControlModeAt) = kGeoControlMode;
        g_GeoLineCheck(&control, flags, start, end, 0.0f, kMainScene);
        const int hits = *reinterpret_cast<const std::int32_t*>(control.bytes + kGeoHitCount);
        const int nearest = *reinterpret_cast<const std::int32_t*>(control.bytes + kGeoNearestHit);
        const bool found = hits > 0 && nearest >= 0 && nearest < kGeoHitSlots;
        const bool faced = found && face && g_GetHitNormal;
        if (found)
            g_GetHitPosition(control.bytes + kGeoHits + nearest * kGeoHitSize, hit);
        if (faced)
            g_GetHitNormal(control.bytes + kGeoHits + nearest * kGeoHitSize, normal);
        auto* extra = *reinterpret_cast<std::uint8_t**>(control.bytes + kGeoExtraResults);
        if (extra && extra[kExtraBorrowed] == 0)
            (*reinterpret_cast<DeleteExtraResults_t**>(extra))[1](extra, 1);
        at = { hit[0], hit[1], hit[2] };
        if (face)
            *face = faced ? Vec3{ normal[0], normal[1], normal[2] } : Normalized(Sub(from, to));
        return found;
    }

    bool GroundBelow(Vec3 point, float& groundY)
    {
        Vec3 hit{};
        const bool found = LineHit({ point.x, point.y + kRayAbove, point.z }, { point.x, point.y - kRayBelow, point.z }, kGroundFlags, hit);
        groundY = hit.y;
        return found;
    }

    // the ground a foot stands on at a spot where the hull's own ground lies at `level`. A low thing is stepped onto, and so is a
    // ledge the hull climbs: its top spreads round the spot. A barrier, a wreck or a wall is no ground: the foot keeps to the ground
    // under or beside it (in the log, pods on such tops tipped the hull 12 degrees and left legs 2 m short of their feet).
    bool StandingGround(Vec3 point, float level, float& groundY)
    {
        if (!GroundBelow(point, groundY))
            return false;
        const float top = groundY;
        if (top <= level + kFootStep)
            return true;
        if (top <= level + kFootClimb)
        {
            const Vec3 round[4] = { { point.x + kBroadProbe, point.y, point.z }, { point.x - kBroadProbe, point.y, point.z },
                                    { point.x, point.y, point.z + kBroadProbe }, { point.x, point.y, point.z - kBroadProbe } };
            bool onTop[4];
            for (int k = 0; k < 4; ++k)
            {
                float y = 0.0f;
                onTop[k] = GroundBelow(round[k], y) && y >= top - kBroadDrop;
            }
            if ((onTop[0] || onTop[1]) && (onTop[2] || onTop[3]))   // its top spreads both ways; a barrier's runs one way only
                return true;
        }
        Vec3 under{};
        groundY = LineHit({ point.x, level + kFootStep, point.z }, { point.x, point.y - kRayBelow, point.z }, kGroundFlags, under) ? under.y : level;
        return true;
    }

    float PadGround(Vec3 at)   // a stone or a crack under one ray does not carry the pad
    {
        const Vec3 spots[5] = { at, { at.x + kPadRadius, at.y, at.z }, { at.x - kPadRadius, at.y, at.z }, { at.x, at.y, at.z + kPadRadius },
                                { at.x, at.y, at.z - kPadRadius } };
        float heights[5];
        int found = 0;
        for (const Vec3& spot : spots)
        {
            float y = 0.0f;
            if (StandingGround(spot, at.y, y))
                heights[found++] = y;
        }
        if (found < 3)
            return at.y;
        std::sort(heights, heights + found);
        return heights[found / 2];
    }

    // a goal that moves only with what lies further than the give from it (low, high), and comes round to the rest at `settle`
    float TakenUp(float goal, float target, float low, float high, float settle, float dt)
    {
        const float gap = target - goal;
        const float held = std::clamp(gap, -low, high);
        return goal + (gap - held) + std::clamp(held, -settle * dt, settle * dt);
    }

    // Metres the hull must stand higher for its belly to clear what stands under it or just ahead. The ground along its way is
    // read a little past the belly's leading end and kept while the hull passes over it: what stands up from the ground the
    // hull lies on higher than its belly rides (a boulder its legs stride over, a bank it runs at) lifts the hull in time.
    // over: metres the belly rides over the ground under the feet.
    float BellyRise(Vec3 pivot, Vec3 across, Vec3 ahead, Vec3 velocity, float over, bool fresh)
    {
        if (fresh)
        {
            for (bool& kept : g_BellySpotKept)
                kept = false;
            g_BellyRead = false;
        }
        const float along = Dot(velocity, ahead);
        const Vec3 reading = Add(pivot, Scale(ahead, (along >= 0.0f ? kBellyNose : -kBellyTail) + kBellyLead * along));
        if (!g_BellyRead || Length(Flat(Sub(reading, g_BellyReadAt))) >= kBellyEvery)
        {
            g_BellyRead = true;
            g_BellyReadAt = reading;
            for (const float out : kBellyAcross)
            {
                const Vec3 at = Add(reading, Scale(across, out));
                float groundY = 0.0f;
                if (!GroundBelow(at, groundY))
                    continue;
                g_BellySpots[g_BellyNext] = { at.x, groundY, at.z };
                g_BellySpotKept[g_BellyNext] = true;
                g_BellyNext = (g_BellyNext + 1) % kBellySpots;
            }
        }
        const float tanSide = std::tan(g_Tilt.side);
        const float tanFront = std::tan(g_Tilt.front);
        const float reach = kBellyLead * std::fabs(along) + kBellyEvery;
        float rise = 0.0f;
        for (int i = 0; i < kBellySpots; ++i)
        {
            if (!g_BellySpotKept[i])
                continue;
            const Vec3 from = Sub(g_BellySpots[i], pivot);
            const float u = Dot(from, across);
            const float v = Dot(from, ahead);
            if (std::fabs(u) > kBellyKept || std::fabs(v) > kBellyKept)
            {
                g_BellySpotKept[i] = false;
                continue;
            }
            if (std::fabs(u) > kBellyHalf || v < -kBellyTail - (along < 0.0f ? reach : 0.0f) || v > kBellyNose + (along >= 0.0f ? reach : 0.0f))
                continue;
            if (g_BellySpots[i].y - pivot.y > kFeetAdrift)   // ground the walker has gone down from
                continue;
            rise = (std::max)(rise, g_BellySpots[i].y + kBellyClear - over - (g_GroundHeight + tanSide * u + tanFront * v));
        }
        return rise;
    }

    void SpringToward(float& value, float& rate, float goal, float maxRate, float dt)   // critically damped, its speed capped
    {
        const float gap = value - goal;
        const float decay = std::exp(-kHullSpring * dt);
        const float lead = rate + kHullSpring * gap;
        const float next = goal + (gap + lead * dt) * decay;
        rate = std::clamp((rate - kHullSpring * lead * dt) * decay, -maxRate, maxRate);
        value += std::clamp(next - value, -maxRate * dt, maxRate * dt);
    }

    battleGearRig::Quat QuatMul(const battleGearRig::Quat& a, const battleGearRig::Quat& b)
    {
        return { a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                 a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
    }

    float UprightLean(float posture, float upright)   // whole at the upright walking posture, gone standing and crouched
    {
        const float walking = std::clamp((1.0f - posture) / (1.0f - kUprightStride), 0.0f, 1.0f)
                            * std::clamp(posture / kUprightStride, 0.0f, 1.0f);
        return kUprightLean * upright * walking * g_Lean;
    }

    // btg0_hang0_def's legs crouched (posture 0) to stood up (1), into normal mode's legs (upright 0) or the bind pose (upright 1),
    // the front bases leaning forward while upright walks
    battleGearRig::Quat ReferenceTurn(int k, float posture, float upright)
    {
        constexpr battleGearRig::Quat bindPose = { 0.0f, 0.0f, 0.0f, 1.0f };
        const battleGearRig::Quat turn = Slerp(battleGearRig::kCrouch[k], Slerp(battleGearRig::kStand[k], bindPose, upright), posture);
        const int bone = kFirstRigBone + k;
        if (bone != kFrontBaseBones[0] && bone != kFrontBaseBones[1])
            return turn;
        const float half = 0.5f * UprightLean(posture, upright);
        const Vec3 hinge = FromPoint(battleGearRig::kHinge);
        return QuatMul(turn, { hinge.x * std::sin(half), hinge.y * std::sin(half), hinge.z * std::sin(half), std::cos(half) });
    }

    // every reference pose bends a front knee the same way about the hinge axis and the bind pose holds it straight: the IK may
    // straighten the leg but never bend it backward
    float KneeLow(int bone, float posture, float upright)
    {
        const battleGearRig::Quat q = ReferenceTurn(bone - kFirstRigBone, posture, upright);
        const Vec3 hinge = FromPoint(battleGearRig::kHinge);
        const float bend = std::remainder(2.0f * std::atan2(q.x * hinge.x + q.y * hinge.y + q.z * hinge.z, q.w), 2.0f * kPi);
        return (std::max)(-kFrontKneeLimit, -bend);
    }

    // every rig bone's place in the shell's bind space: the legs' reference pose, the neck seated (0) to lifting the turret (1), then
    // the turret's yaw, the rail gun's pitch and the tail's sway on top, and each tail joint's own pitch while it carries
    void RigPose(float posture, float upright, float neckLift, float turretYaw, float railPitch, float tailPitch, float tailYaw,
                 Turn* pose, const float* tailCarry = nullptr)
    {
        const Vec3 hinge = FromPoint(battleGearRig::kHinge);
        const Vec3 up = FromPoint(battleGearRig::kTurretUp);
        for (int k = 0; k < kRigBoneCount; ++k)
        {
            const int bone = kFirstRigBone + k;
            const Vec3 pivot = Bind(bone);
            Turn local{};
            if (bone == kNeckBones[0] || bone == kNeckBones[1])   // the second neck bone undoes the first's turn: the turret stays level
                local = TurnAbout(hinge, (bone == kNeckBones[0] ? 1.0f : -1.0f) * battleGearRig::kNeckLift * neckLift, pivot);
            else if (bone == kTurretBone)
                local = TurnAbout(up, turretYaw, pivot);
            else if (bone == kRailBone)
                local = TurnAbout(hinge, railPitch, pivot);
            else if (bone >= kFirstTailBone && bone < kFirstTailBone + kTailBones)
                local = Then(TurnAbout(up, tailYaw / kTailBones, pivot),
                             TurnAbout(hinge, tailPitch / kTailBones + (tailCarry ? tailCarry[bone - kFirstTailBone] : 0.0f), pivot));
            else
            {
                float r[3][3];
                QuatRotation(ReferenceTurn(k, posture, upright), r);
                local = RotationAbout(r, pivot);
            }
            const int parent = battleGearRig::kParent[k];
            pose[k] = parent < kFirstRigBone ? local : Then(local, pose[parent - kFirstRigBone]);
        }
    }

    Vec3 JointAt(const Turn* pose, int bone)
    {
        return Apply(pose[bone - kFirstRigBone], Bind(bone));
    }

    FrontLeg FrontLegAt(const Turn* pose, int leg, float posture, float upright)
    {
        const FrontBones& b = kFrontBones[leg];
        const Turn& ankle = pose[b.ankle - kFirstRigBone];
        const battleGearRig::Quat& crouched = battleGearRig::kCrouch[b.splay - kFirstRigBone];   // the crouch spreads the leg outward
        const Vec3 outward = Normalized({ crouched.x, crouched.y, crouched.z });
        const battleGearRig::Quat spread = ReferenceTurn(b.splay - kFirstRigBone, posture, upright);
        const float splayRef = std::remainder(2.0f * std::atan2(Dot({ spread.x, spread.y, spread.z }, outward), spread.w), 2.0f * kPi);
        return { JointAt(pose, b.hip), JointAt(pose, b.knee), JointAt(pose, b.knee2), JointAt(pose, b.ankle),
                 Normalized(ApplyDir(pose[b.hip - kFirstRigBone], FromPoint(battleGearRig::kHinge))),
                 Apply(ankle, Add(Bind(b.ankle), FromPoint(battleGearRig::kPadSole[leg]))), KneeLow(b.knee, posture, upright),
                 KneeLow(b.knee2, posture, upright), Normalized(ApplyDir(pose[Parent(b.splay) - kFirstRigBone], outward)),
                 Sub(JointAt(pose, b.splayEnd), JointAt(pose, b.splay)), splayRef };
    }

    Vec3 SplayShift(const FrontLeg& leg, float turn)   // the pair's second bone turns back as far: the leg below slides, unturned
    {
        return Sub(Rotate(leg.splayArm, leg.splayAxis, turn), leg.splayArm);
    }

    // the splay turn that carries the foot's target back into the leg's plane (the planar IK cannot reach out of it), within the
    // splay's reach
    float SplayGoal(const FrontLeg& leg, Vec3 soleTarget)
    {
        const float want = Dot(Sub(Add(soleTarget, Sub(leg.ankle, leg.sole)), leg.hip), leg.axis);
        float low = kSplayLow - leg.splayRef;
        float high = kSplayHigh - leg.splayRef;
        const float gapLow = Dot(SplayShift(leg, low), leg.axis) - want;
        const float gapHigh = Dot(SplayShift(leg, high), leg.axis) - want;
        if (gapLow * gapHigh > 0.0f)
            return std::fabs(gapLow) < std::fabs(gapHigh) ? low : high;
        const bool lowBelow = gapLow < 0.0f;
        for (int i = 0; i < kSplaySearchSteps; ++i)
        {
            const float mid = 0.5f * (low + high);
            if ((Dot(SplayShift(leg, mid), leg.axis) - want < 0.0f) == lowBelow)
                low = mid;
            else
                high = mid;
        }
        return 0.5f * (low + high);
    }

    // metres to the hull's right the splay pair carries the leg at its two limits
    void SideBand(const FrontLeg& leg, const Frame& body, Vec3 right, float& low, float& high)
    {
        const float a = Dot(BodyDir(body, SplayShift(leg, kSplayLow - leg.splayRef)), right);
        const float b = Dot(BodyDir(body, SplayShift(leg, kSplayHigh - leg.splayRef)), right);
        low = (std::min)(a, b);
        high = (std::max)(a, b);
    }

    float OvalStance(float aheadShare, float acrossShare, float frontStance, float sideStance)   // the stance along a direction
    {
        return 1.0f / std::sqrt(aheadShare * aheadShare / (frontStance * frontStance) + acrossShare * acrossShare / (sideStance * sideStance));
    }

    float StancesPerSecond(Vec3 travel, float frontStance, float sideStance)
    {
        const float speed = Length(travel);
        return speed > 1e-3f ? speed / OvalStance(std::fabs(travel.z) / speed, std::fabs(travel.x) / speed, frontStance, sideStance) : 0.0f;
    }

    float StepCurve(const float* samples, float s)
    {
        const float x = std::clamp(s, 0.0f, 1.0f) * (kStepSamples - 1);
        const int k = (std::min)(static_cast<int>(x), kStepSamples - 2);
        return samples[k] + (samples[k + 1] - samples[k]) * (x - static_cast<float>(k));
    }

    void SlideLeg(FrontLeg& leg, Vec3 by)
    {
        leg.hip = Add(leg.hip, by);
        leg.knee = Add(leg.knee, by);
        leg.knee2 = Add(leg.knee2, by);
        leg.ankle = Add(leg.ankle, by);
        leg.sole = Add(leg.sole, by);
    }

    RearLeg RearLegAt(const Turn* pose, int leg)
    {
        const RearBones& b = kRearBones[leg];
        const Vec3 hinge = FromPoint(battleGearRig::kHinge);
        RearLeg r{ JointAt(pose, b.base), Normalized(ApplyDir(pose[b.base - kFirstRigBone], hinge)), JointAt(pose, b.hip),
                   Normalized(ApplyDir(pose[b.hip - kFirstRigBone], hinge)), {} };
        const Turn& hip = pose[b.hip - kFirstRigBone];
        for (int k = 0; k < kPodPoints; ++k)
            r.pod[k] = Apply(hip, Add(Bind(b.hip), FromPoint(battleGearRig::kPodUnderside[leg][k])));
        return r;
    }

    void RearTurns(const RearLeg& leg, const PodPose& d, Turn* out)
    {
        out[0] = TurnAbout(leg.baseAxis, d.base, leg.base);
        out[1] = Then(out[0], TurnAbout(ApplyDir(out[0], leg.hipAxis), d.hip, Apply(out[0], leg.hip)));
    }

    Vec3 PodContact(const RearLeg& leg, const PodPose& d, Vec3 up)   // its lowest points, weighted by how low they reach
    {
        Turn turns[2];
        RearTurns(leg, d, turns);
        Vec3 points[kPodPoints];
        float heights[kPodPoints];
        float lowest = FLT_MAX;
        for (int k = 0; k < kPodPoints; ++k)
        {
            points[k] = Apply(turns[1], leg.pod[k]);
            heights[k] = Dot(points[k], up);
            lowest = (std::min)(lowest, heights[k]);
        }
        Vec3 sum{};
        float weight = 0.0f;
        for (int k = 0; k < kPodPoints; ++k)
        {
            const float w = std::exp(-(heights[k] - lowest) / battleGearRig::kContactSoftness);
            sum = Add(sum, Scale(points[k], w));
            weight += w;
        }
        return Scale(sum, 1.0f / weight);
    }

    float PodsAboveFront(const Vec3* contact, Vec3 up)
    {
        return 0.5f * (Dot(Sub(contact[2], contact[0]), up) + Dot(Sub(contact[3], contact[1]), up));
    }

    void MeasureCrouch()
    {
        Turn pose[kRigBoneCount];
        RigPose(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, pose);
        const Vec3 up = Normalized(FromPoint(battleGearRig::kTurretUp));
        const Vec3 contact[4] = { FrontLegAt(pose, 0, 0.0f, 0.0f).sole, FrontLegAt(pose, 1, 0.0f, 0.0f).sole,
                                  PodContact(RearLegAt(pose, 0), {}, up), PodContact(RearLegAt(pose, 1), {}, up) };
        g_PodsRest = PodsAboveFront(contact, up);
    }

    PodPose ClampPod(PodPose d)
    {
        d.base = std::clamp(d.base, kPodBaseLow, kPodBaseHigh);
        d.hip = std::clamp(d.hip, (std::max)(kPodHipLow, -kMaxPodTurn - d.base), (std::min)(kPodHipHigh, kMaxPodTurn - d.base));
        return d;
    }

    float PodMiss(const RearLeg& leg, const PodPose& d, Vec3 target, Vec3 up)
    {
        return Length(Planar(Sub(target, PodContact(leg, d, up)), leg.baseAxis));
    }

    bool SupportUnder(int i, Vec3 contact, bool reset, Vec3& support)   // a planted foot carries the hull from its own ground spot
    {
        const Foot& foot = g_Feet[i];
        if (!reset && foot.stance && !(i >= 2 && g_Floating) && Length(Flat(Sub(foot.planted, contact))) < kSupportReach
            && std::fabs(foot.ground - contact.y) < kFeetAdrift)   // a walker gone down a drop leaves it out of reach
        {
            support = { foot.planted.x, foot.ground, foot.planted.z };
            return true;
        }
        float groundY = 0.0f;
        const bool found = !reset && g_PlaneKnown[i] ? StandingGround(contact, g_PlaneUnder[i], groundY) : GroundBelow(contact, groundY);
        if (!found)
            return false;
        support = { contact.x, groundY, contact.z };
        return true;
    }

    // the ground plane through what carries the hull, read under each contact point
    void SupportRise(const Vec3* contacts, const Vec3* supports, const bool* hit, int hits, float* ground, float* rise)
    {
        Vec3 centre = { 0.0f, 0.0f, 0.0f };
        for (int i = 0; i < 4; ++i)
            if (hit[i])
                centre = Add(centre, supports[i]);
        centre = Scale(centre, 1.0f / static_cast<float>((std::max)(hits, 1)));
        float sxx = 0.0f, szz = 0.0f, sxz = 0.0f, sxy = 0.0f, szy = 0.0f;
        for (int i = 0; i < 4; ++i)
        {
            if (!hit[i])
                continue;
            const Vec3 d = Sub(supports[i], centre);
            sxx += d.x * d.x;
            szz += d.z * d.z;
            sxz += d.x * d.z;
            sxy += d.x * d.y;
            szy += d.z * d.y;
        }
        const float det = sxx * szz - sxz * sxz;
        const bool plane = hits >= kMinHits && det > 1e-4f;
        const float slopeX = plane ? (sxy * szz - szy * sxz) / det : 0.0f;
        const float slopeZ = plane ? (szy * sxx - sxy * sxz) / det : 0.0f;
        for (int i = 0; i < 4; ++i)
        {
            if (!hit[i])
                continue;
            ground[i] = plane ? centre.y + slopeX * (contacts[i].x - centre.x) + slopeZ * (contacts[i].z - centre.z) : supports[i].y;
            rise[i] = ground[i] - contacts[i].y;
        }
    }

    Tilt GroundTilt(const Vec3* feet, const float* rise, const bool* hit, int hits, Vec3 side, Vec3 front)
    {
        Tilt target{};
        if (hits < kMinHits)
            return target;
        Vec3 centre = { 0.0f, 0.0f, 0.0f };
        for (int i = 0; i < 4; ++i)
            if (hit[i])
                centre = Add(centre, feet[i]);
        centre = Scale(centre, 1.0f / static_cast<float>(hits));
        float suu = 0.0f, svv = 0.0f, suv = 0.0f, sur = 0.0f, svr = 0.0f, sr = 0.0f;
        for (int i = 0; i < 4; ++i)
        {
            if (!hit[i])
                continue;
            const Vec3 offset = Sub(feet[i], centre);
            const float u = Dot(offset, side);
            const float v = Dot(offset, front);
            suu += u * u;
            svv += v * v;
            suv += u * v;
            sur += u * rise[i];
            svr += v * rise[i];
            sr += rise[i];
        }
        const float det = suu * svv - suv * suv;
        if (det < 1e-4f)
            return target;
        target.side = std::clamp(std::atan((sur * svv - svr * suv) / det), -kMaxTilt, kMaxTilt);
        target.front = std::clamp(std::atan((svr * suu - sur * suv) / det), -kMaxTilt, kMaxTilt);
        target.lift = std::clamp(sr / static_cast<float>(hits), -kMaxLift, kMaxLift);
        return target;
    }

    // the lie of the ground round the hull: the plane through what carries it and the ground sampled round and ahead of it, as
    // its rise under each contact point. A foot on a rock or in a hole tips the plane through the four feet's spots alone; in
    // this one the legs take that up, and the samples ahead begin a slope's tilt before the feet are on it.
    bool GroundLie(const Vec3* contacts, const Vec3* supports, const bool* hit, const Vec3* around, int aroundCount, float* rise)
    {
        Vec3 points[4 + kLieSamples];
        int count = 0;
        for (int i = 0; i < 4; ++i)
            if (hit[i])
                points[count++] = supports[i];
        const int feet = count;
        for (int i = 0; i < aroundCount; ++i)
            points[count++] = around[i];
        Vec3 centre{};
        float slopeX = 0.0f;
        float slopeZ = 0.0f;
        for (int pass = 0; pass < 2; ++pass)
        {
            if (count < kLieFewest)
                return false;
            centre = {};
            for (int i = 0; i < count; ++i)
                centre = Add(centre, points[i]);
            centre = Scale(centre, 1.0f / static_cast<float>(count));
            float sxx = 0.0f, szz = 0.0f, sxz = 0.0f, sxy = 0.0f, szy = 0.0f;
            for (int i = 0; i < count; ++i)
            {
                const Vec3 d = Sub(points[i], centre);
                sxx += d.x * d.x;
                szz += d.z * d.z;
                sxz += d.x * d.z;
                sxy += d.x * d.y;
                szy += d.z * d.y;
            }
            const float det = sxx * szz - sxz * sxz;
            if (det < 1e-4f)
                return false;
            slopeX = (sxy * szz - szy * sxz) / det;
            slopeZ = (szy * sxx - sxy * sxz) / det;
            if (pass == 1)
                break;
            int kept = feet;   // the feet's own spots stay; a sample far off the plane goes, and the plane is fitted again
            for (int i = feet; i < count; ++i)
            {
                const Vec3 d = Sub(points[i], centre);
                if (std::fabs(d.y - slopeX * d.x - slopeZ * d.z) <= kLieOutlier)
                    points[kept++] = points[i];
            }
            if (kept == count)
                break;
            count = kept;
        }
        for (int i = 0; i < 4; ++i)
            rise[i] = centre.y + slopeX * (contacts[i].x - centre.x) + slopeZ * (contacts[i].z - centre.z) - contacts[i].y;
        return true;
    }

    Vec3 HullAcross(const Frame& f)   // the shell's own level axes (the idle bake's), not the bind axes the bake turned
    {
        return Normalized(BodyDir(f, FromPoint(battleGearRig::kHinge)));
    }

    Vec3 HullAhead(const Frame& f)
    {
        return Normalized(BodyDir(f, FromPoint(battleGearRig::kAhead)));
    }

    Frame Tilted(const Frame& body, Vec3 pivot)
    {
        const Vec3 up = { 0.0f, 1.0f, 0.0f };
        const Vec3 sideAxis = Normalized(Cross(Normalized(Flat(HullAcross(body))), up));   // turning about it raises the left side
        const Vec3 frontAxis = Normalized(Cross(Normalized(Flat(HullAhead(body))), up));   // turning about it raises the front
        Frame f{};
        for (int r = 0; r < 3; ++r)
            f.row[r] = Rotate(Rotate(body.row[r], sideAxis, g_Tilt.side), frontAxis, g_Tilt.front);
        const Vec3 offset = Rotate(Rotate(Sub(body.origin, pivot), sideAxis, g_Tilt.side), frontAxis, g_Tilt.front);
        f.origin = Add(Add(pivot, offset), Scale(up, g_Tilt.lift));
        return f;
    }

    float At(Vec3 v, int i)
    {
        return i == 0 ? v.x : (i == 1 ? v.y : v.z);
    }

    void FrontChain(const FrontLeg& leg, const FrontPose& p, Vec3* joints, Vec3& ankle)
    {
        joints[0] = leg.hip;
        joints[1] = Add(leg.hip, Rotate(Sub(leg.knee, leg.hip), leg.axis, p.hip));
        joints[2] = Add(joints[1], Rotate(Sub(leg.knee2, leg.knee), leg.axis, p.hip + p.knee));
        ankle = Add(joints[2], Rotate(Sub(leg.ankle, leg.knee2), leg.axis, p.hip + p.knee + p.knee2));
    }

    Vec3 Solve3(const float m[3][3], Vec3 b)   // m * x = b
    {
        const float c00 = m[1][1] * m[2][2] - m[1][2] * m[2][1];
        const float c01 = m[1][2] * m[2][0] - m[1][0] * m[2][2];
        const float c02 = m[1][0] * m[2][1] - m[1][1] * m[2][0];
        const float det = m[0][0] * c00 + m[0][1] * c01 + m[0][2] * c02;
        if (std::fabs(det) < 1e-12f)
            return { 0.0f, 0.0f, 0.0f };
        const float adj[3][3] = {
            { c00, m[0][2] * m[2][1] - m[0][1] * m[2][2], m[0][1] * m[1][2] - m[0][2] * m[1][1] },
            { c01, m[0][0] * m[2][2] - m[0][2] * m[2][0], m[0][2] * m[1][0] - m[0][0] * m[1][2] },
            { c02, m[0][1] * m[2][0] - m[0][0] * m[2][1], m[0][0] * m[1][1] - m[0][1] * m[1][0] },
        };
        return { (adj[0][0] * b.x + adj[0][1] * b.y + adj[0][2] * b.z) / det, (adj[1][0] * b.x + adj[1][1] * b.y + adj[1][2] * b.z) / det,
                 (adj[2][0] * b.x + adj[2][1] * b.y + adj[2][2] * b.z) / det };
    }

    void DampedNormal(const Vec3* columns, int count, float m[3][3])   // J * J^T + damping
    {
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
            {
                m[r][c] = r == c ? kIkDamping * kIkDamping : 0.0f;
                for (int j = 0; j < count; ++j)
                    m[r][c] += At(columns[j], r) * At(columns[j], c);
            }
    }

    FrontPose ClampFront(const FrontLeg& leg, const FrontPose& p)
    {
        return { std::clamp(p.hip, kFrontHipBack, kFrontHipFwd), std::clamp(p.knee, leg.kneeLow, kFrontKneeLimit),
                 std::clamp(p.knee2, leg.knee2Low, kFrontKneeLimit) };
    }

    // hip, knee and second knee bend together (damped least squares from the given pose); returns how far the ankle misses. nudge:
    // a knee pinned straight starts a little into its bend (a straight leg has no first-order way to fold)
    float Reach(const FrontLeg& leg, FrontPose& pose, Vec3 soleTarget, bool nudge)
    {
        const Vec3 a = leg.axis;
        const Vec3 target = Add(soleTarget, Sub(leg.ankle, leg.sole));
        Vec3 joints[3];
        Vec3 ankle{};
        pose = ClampFront(leg, pose);
        if (nudge)
        {
            pose.knee = (std::max)(pose.knee, (std::min)(leg.kneeLow + kKneeNudge, kFrontKneeLimit));
            pose.knee2 = (std::max)(pose.knee2, (std::min)(leg.knee2Low + kKneeNudge, kFrontKneeLimit));
        }
        for (int i = 0; i < kIkIterations; ++i)
        {
            FrontChain(leg, pose, joints, ankle);
            const Vec3 err = Planar(Sub(target, ankle), a);
            if (Dot(err, err) < 1e-8f)
                break;
            Vec3 column[3];
            for (int j = 0; j < 3; ++j)
                column[j] = Cross(a, Sub(ankle, joints[j]));
            float m[3][3];
            DampedNormal(column, 3, m);
            const Vec3 y = Solve3(m, err);
            // three joints for a spot in a plane leave one turn free. Left alone it keeps what the foot's path so far gave
            // it and ends at a joint's limit, short of a foothold another bend of the same leg reaches; here it leans back
            // to the reference pose, so the same spot always bends the leg the same way
            const float give = kIkRest * std::clamp(1.0f - Length(err) / kIkRestWithin, 0.0f, 1.0f);
            const float rest[3] = { -give * pose.hip, -give * pose.knee, -give * pose.knee2 };
            const Vec3 moved = Add(Add(Scale(column[0], rest[0]), Scale(column[1], rest[1])), Scale(column[2], rest[2]));
            const Vec3 back = Solve3(m, moved);
            pose = ClampFront(leg, { pose.hip + Dot(column[0], y) + rest[0] - Dot(column[0], back),
                                     pose.knee + Dot(column[1], y) + rest[1] - Dot(column[1], back),
                                     pose.knee2 + Dot(column[2], y) + rest[2] - Dot(column[2], back) });
        }
        FrontChain(leg, pose, joints, ankle);
        return Length(Planar(Sub(target, ankle), a));
    }

    float FrontJump(const FrontPose& a, const FrontPose& b)
    {
        return (std::max)({ std::fabs(b.hip - a.hip), std::fabs(b.knee - a.knee), std::fabs(b.knee2 - a.knee2) });
    }

    // from last frame's pose, or from other start poses when that one gets stuck short of a foothold the leg can reach (first the same
    // pose with a straight knee folded a little); a pose far from last frame's wins only by reaching clearly closer, and the leg then
    // moves toward it at most maxJump radians this frame
    float SolveFront(const FrontLeg& leg, FrontPose& pose, Vec3 soleTarget, float maxJump)
    {
        const FrontPose last = pose;
        float best = Reach(leg, pose, soleTarget, false);
        if (best <= kRetrySeeds)
            return best;
        float score = best + kFrontCloseness * FrontJump(last, pose);
        FrontPose folded = last;
        const float foldedMiss = Reach(leg, folded, soleTarget, true);
        if (foldedMiss + kFrontCloseness * FrontJump(last, folded) < score - kSeedGain)
        {
            score = foldedMiss + kFrontCloseness * FrontJump(last, folded);
            best = foldedMiss;
            pose = folded;
        }
        for (const FrontPose& seed : kSeeds)
        {
            FrontPose tried = seed;
            const float miss = Reach(leg, tried, soleTarget, false);
            const float s = miss + kFrontCloseness * FrontJump(last, tried);
            if (s < score - kSeedGain)
            {
                score = s;
                best = miss;
                pose = tried;
            }
        }
        const float jump = FrontJump(last, pose);
        if (jump > maxJump)
        {
#ifdef _DEBUG
            if (TwitchLogAllowed())
                LogDebug("BattleGear twitch: front leg solution wanted %.0f deg in one frame, held to %.0f deg (it missed %.2f m)",
                         jump * 57.29578f, maxJump * 57.29578f, best);   // TEMP probe
#endif
            const float share = maxJump / jump;
            pose = { last.hip + (pose.hip - last.hip) * share, last.knee + (pose.knee - last.knee) * share,
                     last.knee2 + (pose.knee2 - last.knee2) * share };
            Vec3 joints[3];
            Vec3 ankle{};
            FrontChain(leg, pose, joints, ankle);
            best = Length(Planar(Sub(Add(soleTarget, Sub(leg.ankle, leg.sole)), ankle), leg.axis));
        }
        return best;
    }

    void FrontTurns(const FrontLeg& leg, const FrontPose& pose, Vec3 slide, Turn* out)   // leg: already slid by the splay
    {
        const Vec3 a = leg.axis;
        const Turn slid = Translate(slide);
        const Turn thigh = TurnAbout(a, pose.hip, leg.hip);
        const Turn knee = Then(thigh, TurnAbout(a, pose.knee, Apply(thigh, leg.knee)));
        const Turn knee2 = Then(knee, TurnAbout(a, pose.knee2, Apply(knee, leg.knee2)));
        out[0] = Then(slid, thigh);
        out[1] = Then(slid, knee);
        out[2] = Then(slid, knee2);
        out[3] = Then(slid, Translate(Sub(Apply(knee2, leg.ankle), leg.ankle)));   // the pad keeps its attitude
    }

    // damped Newton on the base and hip turns; a step that does not bring the contact closer is shortened (the pod's flat underside
    // bends the contact's path)
    float PodNewton(const RearLeg& leg, PodPose& d, Vec3 target, Vec3 up)
    {
        const Vec3 a = leg.baseAxis;
        for (int i = 0; i < kPodIterations; ++i)
        {
            const Vec3 contact = PodContact(leg, d, up);
            const Vec3 err = Planar(Sub(target, contact), a);
            const float now = Length(err);
            if (now * now < 1e-8f)
                break;
            Vec3 column[2];
            for (int j = 0; j < 2; ++j)
            {
                PodPose nudged = d;
                (j == 0 ? nudged.base : nudged.hip) += 1e-3f;
                column[j] = Scale(Planar(Sub(PodContact(leg, nudged, up), contact), a), 1000.0f);
            }
            float m[3][3];
            DampedNormal(column, 2, m);
            const Vec3 y = Solve3(m, err);
            float stepBase = Dot(column[0], y);
            float stepHip = Dot(column[1], y);
            const float biggest = (std::max)(std::fabs(stepBase), std::fabs(stepHip));
            if (biggest > kMaxPodStep)
            {
                stepBase *= kMaxPodStep / biggest;
                stepHip *= kMaxPodStep / biggest;
            }
            bool moved = false;
            for (int b = 0; b < kPodBacktracks && !moved; ++b)
            {
                const PodPose tried = ClampPod({ d.base + stepBase, d.hip + stepHip });
                if (PodMiss(leg, tried, target, up) < now)
                {
                    d = tried;
                    moved = true;
                }
                stepBase *= 0.5f;
                stepHip *= 0.5f;
            }
            if (!moved)
                break;
        }
        return PodMiss(leg, d, target, up);
    }

    PodPose ApproachPod(const PodPose& now, const PodPose& goal, float maxStep)
    {
        const float base = goal.base - now.base;
        const float hip = goal.hip - now.hip;
        const float biggest = (std::max)(std::fabs(base), std::fabs(hip));
        const float share = biggest > maxStep ? maxStep / biggest : 1.0f;
        return { now.base + base * share, now.hip + hip * share };
    }

    // from last frame's pose, or from the best of a coarse sweep of the joints when that one gets stuck (the lowest points switch
    // from heel to toe as the pod rocks); the sweep prefers poses near the last one, and the pod moves toward it at most maxJump radians
    float SolvePod(const RearLeg& leg, PodPose& d, Vec3 target, Vec3 up, float maxJump)
    {
        const PodPose warm = d;
        float best = PodNewton(leg, d, target, up);
        if (best <= kRetrySeeds)
            return best;
        PodPose start = warm;
        float score = FLT_MAX;
        for (int i = 0; i < kPodGrid; ++i)
            for (int j = 0; j < kPodGrid; ++j)
            {
                const float u = static_cast<float>(i) / (kPodGrid - 1);
                const float v = static_cast<float>(j) / (kPodGrid - 1);
                const PodPose tried = ClampPod({ kPodBaseLow + (kPodBaseHigh - kPodBaseLow) * u, kPodHipLow + (kPodHipHigh - kPodHipLow) * v });
                const float s = PodMiss(leg, tried, target, up)
                              + kPodCloseness * (std::fabs(tried.base - warm.base) + std::fabs(tried.hip - warm.hip));
                if (s < score)
                {
                    score = s;
                    start = tried;
                }
            }
        const float miss = PodNewton(leg, start, target, up);
        const float away = (std::max)(std::fabs(start.base - warm.base), std::fabs(start.hip - warm.hip));
        const float stay = (std::max)(std::fabs(d.base - warm.base), std::fabs(d.hip - warm.hip));
        if (miss + kPodCloseness * away < best + kPodCloseness * stay - kSeedGain)
        {
            best = miss;
            d = start;
            if (away > maxJump)
            {
                d = ApproachPod(warm, start, maxJump);
                best = PodMiss(leg, d, target, up);
            }
        }
        return best;
    }

    PodPose LerpPod(const PodPose& a, const PodPose& b, float t)
    {
        return { a.base + (b.base - a.base) * t, a.hip + (b.hip - a.hip) * t };
    }

    void AimCylinders(Turn* pose)   // each ram end turns to point at the other
    {
        for (const auto& ram : kCylinders)
        {
            const Vec3 ends[2] = { JointAt(pose, ram[0]), JointAt(pose, ram[1]) };
            for (int e = 0; e < 2; ++e)
            {
                const int bone = ram[e];
                const Vec3 rest = ApplyDir(pose[bone - kFirstRigBone], Sub(Bind(ram[1 - e]), Bind(bone)));
                const Vec3 now = Sub(ends[1 - e], ends[e]);
                const Vec3 axis = Cross(rest, now);
                const float sine = Length(axis);
                if (sine < 1e-6f)
                    continue;
                const float angle = std::atan2(sine, Dot(rest, now));
                pose[bone - kFirstRigBone] = Then(pose[bone - kFirstRigBone], TurnAbout(Scale(axis, 1.0f / sine), angle, ends[e]));
            }
        }
    }

    void WriteRig(void* accessor, const Frame& body, const Turn* pose)
    {
        alignas(16) float m[16];
        for (int k = 0; k < kRigBoneCount; ++k)
        {
            ToMatrix(PartFrame(body, pose[k], Bind(kFirstRigBone + k)), m);
            g_SetBoneWorld(accessor, kFirstRigBone + k, m);
        }
    }

    float Approach(float now, float goal, float maxStep)
    {
        return now + std::clamp(goal - now, -maxStep, maxStep);
    }

    float WrapAngle(float a)
    {
        return std::remainder(a, 2.0f * kPi);
    }

    float ApproachAngle(float now, float goal, float maxStep)
    {
        return WrapAngle(now + std::clamp(WrapAngle(goal - now), -maxStep, maxStep));
    }

    void AimGuns(const Frame& body, const Turn* pose, float dt)   // turret yaw on the neck, rail-gun pitch
    {
        float yawGoal = 0.0f;
        float pitchGoal = 0.0f;
        Vec3 target{};
        if (battleGearWeapons::AimTarget(&target.x) || (battleGearWeapons::AiAttacking() && battleGearWeapons::AimPoint(&target.x)))
        {
            const Turn& neck = pose[kNeckBones[1] - kFirstRigBone];
            const Vec3 turretUp = FromPoint(battleGearRig::kTurretUp);
            const Vec3 hinge = FromPoint(battleGearRig::kHinge);
            const Vec3 up = Normalized(BodyDir(body, ApplyDir(neck, turretUp)));
            const Vec3 front = Normalized(Planar(BodyDir(body, ApplyDir(neck, FromPoint(battleGearRig::kAhead))), up));
            const Vec3 toTarget = Planar(Sub(target, ToWorld(body, Apply(neck, Bind(kTurretBone)))), up);
            if (Length(toTarget) > 1.0f)
                yawGoal = SignedAngle(front, Normalized(toTarget), up);
            const Turn turned = Then(TurnAbout(turretUp, yawGoal, Bind(kTurretBone)), neck);
            const Vec3 axis = Normalized(BodyDir(body, ApplyDir(turned, hinge)));
            const Vec3 barrel = Normalized(Planar(BodyDir(body, ApplyDir(turned, kRailBarrel)), axis));
            const Vec3 toAim = Planar(Sub(target, ToWorld(body, Apply(turned, Bind(kRailBone)))), axis);
            if (Length(toAim) > 1.0f)
                pitchGoal = std::clamp(SignedAngle(barrel, Normalized(toAim), axis), -kRailHighest, kRailLowest);
        }
        g_TurretYaw = ApproachAngle(g_TurretYaw, yawGoal, kTurretTurnRate * dt);
        g_RailPitch = Approach(g_RailPitch, pitchGoal, kRailTurnRate * dt);
    }

    Vec3 RoofMgPoint(Vec3 m2)   // a point of the M2's own frame in the shell's bind space
    {
        return Add(Bind(kGunBaseBone), Add(Add(Scale(FromPoint(battleGearRig::kHinge), m2.x), Scale(FromPoint(battleGearRig::kTurretUp), m2.y)),
                                           Scale(FromPoint(battleGearRig::kAhead), m2.z)));
    }

    // the M2's mount turned on SKL_094_GUNBASE and its gun pitched on the mount, from the given turn and pitch
    void RoofMgTurns(const Turn* pose, float yaw, float pitch, Turn& mount, Turn& gun)
    {
        mount = Then(TurnAbout(FromPoint(battleGearRig::kTurretUp), yaw, Bind(kGunBaseBone)), pose[kGunBaseBone - kFirstRigBone]);
        gun = Then(TurnAbout(FromPoint(battleGearRig::kHinge), pitch, RoofMgPoint(kRoofMgGunPivot)), mount);
    }

    void AimRoofMg(const Frame& body, const Turn* pose, float dt)   // the tank cupola's way: the mount turns, the gun pitches
    {
        float yawGoal = 0.0f;
        float pitchGoal = 0.0f;
        Vec3 target{};
        if (battleGearWeapons::AimTarget(&target.x) || (battleGearWeapons::AiAttacking() && battleGearWeapons::AimPoint(&target.x)))
        {
            const Turn& base = pose[kGunBaseBone - kFirstRigBone];
            const Vec3 up = Normalized(BodyDir(body, ApplyDir(base, FromPoint(battleGearRig::kTurretUp))));
            const Vec3 front = Normalized(Planar(BodyDir(body, ApplyDir(base, FromPoint(battleGearRig::kAhead))), up));
            const Vec3 toTarget = Planar(Sub(target, ToWorld(body, Apply(base, Bind(kGunBaseBone)))), up);
            if (Length(toTarget) > 1.0f)
                yawGoal = std::clamp(SignedAngle(front, Normalized(toTarget), up), -kRoofMgWidest, kRoofMgWidest);
            Turn mount{};
            Turn gun{};
            RoofMgTurns(pose, yawGoal, 0.0f, mount, gun);
            const Vec3 axis = Normalized(BodyDir(body, ApplyDir(mount, FromPoint(battleGearRig::kHinge))));
            const Vec3 barrel = Normalized(Planar(BodyDir(body, ApplyDir(mount, FromPoint(battleGearRig::kAhead))), axis));
            const Vec3 toAim = Planar(Sub(target, ToWorld(body, Apply(mount, RoofMgPoint(kRoofMgGunPivot)))), axis);
            if (Length(toAim) > 1.0f)
                pitchGoal = std::clamp(SignedAngle(barrel, Normalized(toAim), axis), -kRoofMgHighest, kRoofMgLowest);
        }
        const float yawWas = g_RoofMgYaw;
        const float pitchWas = g_RoofMgPitch;
        g_RoofMgYaw = Approach(g_RoofMgYaw, yawGoal, kRoofMgTurnRate * dt);
        g_RoofMgPitch = Approach(g_RoofMgPitch, pitchGoal, kRoofMgPitchRate * dt);
        battleGearWeapons::PublishRoofGunTurning(
            dt > 0.0f && std::fabs(g_RoofMgYaw - yawWas) + std::fabs(g_RoofMgPitch - pitchWas) > kRoofMgHeardRate * dt);
    }

    void PublishWeapons(const Frame& body, const Turn* pose, bool roofMg, float dt)
    {
        static_assert(sizeof(Vec3) == 3 * sizeof(float), "Vec3 passes as three floats");
        const Turn& turret = pose[kTurretBone - kFirstRigBone];
        const Turn& head = pose[kHeadBone - kFirstRigBone];
        const Turn& railGun = pose[kRailBarrelBone - kFirstRigBone];
        Vec3 hatches[3];
        for (int i = 0; i < 3; ++i)
            hatches[i] = ToWorld(body, Apply(turret, kHatches[i]));
        const Vec3 railMuzzle = ToWorld(body, Apply(railGun, kRailMuzzle));
        const Vec3 railAim = Normalized(BodyDir(body, ApplyDir(railGun, kRailBarrel)));
        Vec3 heavyMgMuzzle = ToWorld(body, Apply(head, kHeavyMgMuzzle));
        Vec3 heavyMgAim = Normalized(BodyDir(body, ApplyDir(head, FromPoint(battleGearRig::kAhead))));
        if (roofMg)
        {
            Turn mount{};
            Turn gun{};
            RoofMgTurns(pose, g_RoofMgYaw, g_RoofMgPitch, mount, gun);
            heavyMgMuzzle = ToWorld(body, Apply(gun, RoofMgPoint(kRoofMgMuzzle)));
            heavyMgAim = Normalized(BodyDir(body, ApplyDir(gun, FromPoint(battleGearRig::kAhead))));
            Frame m2{};
            m2.row[0] = Normalized(BodyDir(body, ApplyDir(gun, FromPoint(battleGearRig::kHinge))));
            m2.row[1] = Normalized(BodyDir(body, ApplyDir(gun, FromPoint(battleGearRig::kTurretUp))));
            m2.row[2] = heavyMgAim;
            m2.origin = ToWorld(body, Apply(gun, RoofMgPoint(kRoofMgGunPivot)));
            alignas(16) float gunMatrix[16];
            ToMatrix(m2, gunMatrix);
            battleGearWeapons::PublishRoofGun(gunMatrix, dt);
        }
        else
        {
            battleGearWeapons::DropRoofGun();
        }
        battleGearWeapons::PublishMuzzles(&railMuzzle.x, &heavyMgMuzzle.x, reinterpret_cast<const float(*)[3]>(hatches), &railAim.x,
                                          &heavyMgAim.x);
    }

    void WriteRoofMg(void* accessor, const Frame& body, const Turn* pose, float yaw, float pitch)
    {
        alignas(16) float m[16];
        Turn mount{};
        Turn gun{};
        RoofMgTurns(pose, yaw, pitch, mount, gun);
        ToMatrix(PartFrame(body, mount, Bind(kGunBaseBone)), m);
        g_SetBoneWorld(accessor, kRoofMgBone, m);
        ToMatrix(PartFrame(body, gun, RoofMgPoint(kRoofMgGunPivot)), m);
        g_SetBoneWorld(accessor, kRoofMgGunBone, m);
    }

    // the walker's own clock while it runs: the game's slow motion slows the legs, the guns and the hover with the walker under
    // them. Hanging from a balloon or shown in the ACC its walker runs no frames, and the animation job's own timing counts.
    float FrameSeconds()
    {
        const std::int64_t now = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        const std::int64_t last = g_LastTick;
        g_LastTick = now;
        float walker = 0.0f;
        if (battleGearWeapons::TakeWalkerTime(&walker))
        {
            g_MeasuredFrame = std::clamp(walker, 0.0f, kLongestFrame);
            g_SmoothFrame = g_MeasuredFrame;
            return g_MeasuredFrame;
        }
#ifdef _DEBUG
        if (battleGearWeapons::Ridden() && GetTickCount64() >= g_NextGapLog)   // TEMP probe: what the walker does meanwhile
        {
            g_NextGapLog = GetTickCount64() + 250;
            int state = -1;
            int next = -1;
            unsigned unread = 0;
            unsigned long long callbackVtable = 0;
            BattleGearWalkerStateProbe(&state, &next, &unread, &callbackVtable);
            LogDebug("BattleGear gap: ridden, no walk frame for over 250 ms; the walker's basic action state is %d (next %d), %u "
                     "walk hook calls were not read as its own, its velocity callback's vtable is 0x%llX", state, next, unread,
                     callbackVtable);
        }
#endif
        if (!last)
            return 0.0f;
        const float measured = std::clamp(static_cast<float>(now - last) * 1e-6f, 0.0f, kLongestFrame);
        g_MeasuredFrame = measured;
        g_SmoothFrame = g_SmoothFrame > 0.0f ? g_SmoothFrame + (measured - g_SmoothFrame) * kFrameFollow : measured;
        return g_SmoothFrame;
    }

    float Cycle01(float x)
    {
        return x - std::floor(x);
    }

    Gait GaitAt(float speed)
    {
        constexpr int count = static_cast<int>(sizeof(kGaits) / sizeof(kGaits[0]));
        int lo = 0;
        while (lo + 1 < count && speed >= kGaits[lo + 1].speed)
            ++lo;
        const Gait& a = kGaits[lo];
        const Gait& b = kGaits[(std::min)(lo + 1, count - 1)];
        const float t = b.speed > a.speed ? std::clamp((speed - a.speed) / (b.speed - a.speed), 0.0f, 1.0f) : 0.0f;
        Gait g{ speed, a.duty + (b.duty - a.duty) * t, a.lift + (b.lift - a.lift) * t, a.bob + (b.bob - a.bob) * t,
                a.pitch + (b.pitch - a.pitch) * t, a.roll + (b.roll - a.roll) * t, {} };
        for (int i = 0; i < 4; ++i)
        {
            const float shift = b.offsets[i] - a.offsets[i];
            g.offsets[i] = Cycle01(a.offsets[i] + (shift - std::round(shift)) * t);   // the short way round the cycle
        }
        return g;
    }

#ifdef _DEBUG
    const char* GaitName(float speed)
    {
        return speed < 2.6f ? "walk" : (speed < 5.35f ? "trot" : (speed < 10.5f ? "canter" : (speed < 17.0f ? "gallop" : "sprint")));
    }

    const char* CarryName(battleGearWeapons::CarryPhase phase)
    {
        switch (phase)
        {
        case battleGearWeapons::CarryPhase::PickingUp:   return "picking up";
        case battleGearWeapons::CarryPhase::Holding:     return "holding";
        case battleGearWeapons::CarryPhase::PuttingDown: return "putting down";
        default:                                         return "none";
        }
    }
#endif

    using ReachTable = float[battleGearRig::kUprights][battleGearRig::kPostures][4][2];
    using SoleTable = float[battleGearRig::kUprights][battleGearRig::kPostures];

    void ReachIn(const ReachTable& reach, float posture, float upright, int foot, float& nearest, float& farthest)
    {
        const float x = std::clamp(posture, 0.0f, 1.0f) * (battleGearRig::kPostures - 1);
        const int k = (std::min)(static_cast<int>(x), battleGearRig::kPostures - 2);
        const float t = x - static_cast<float>(k);
        const float y = std::clamp(upright, 0.0f, 1.0f) * (battleGearRig::kUprights - 1);
        const int j = (std::min)(static_cast<int>(y), battleGearRig::kUprights - 2);
        const float u = y - static_cast<float>(j);
        const auto& normalSide = reach[j];
        const auto& uprightSide = reach[j + 1];
        float ends[2];
        for (int e = 0; e < 2; ++e)
        {
            const float low = normalSide[k][foot][e] + (normalSide[k + 1][foot][e] - normalSide[k][foot][e]) * t;
            const float high = uprightSide[k][foot][e] + (uprightSide[k + 1][foot][e] - uprightSide[k][foot][e]) * t;
            ends[e] = low + (high - low) * u;
        }
        nearest = ends[0];
        farthest = ends[1];
    }

    void ReachAt(float posture, float upright, int foot, float& nearest, float& farthest)   // along level ground from its reference contact
    {
        float flatNearest = 0.0f;
        float flatFarthest = 0.0f;
        ReachIn(battleGearRig::kReachFlat, posture, upright, foot, flatNearest, flatFarthest);
        ReachIn(battleGearRig::kReach, posture, upright, foot, nearest, farthest);
        nearest = flatNearest + (nearest - flatNearest) * g_Lean;
        farthest = flatFarthest + (farthest - flatFarthest) * g_Lean;
    }

    float HomeIn(const ReachTable& reach, const SoleTable& soles, float posture, float upright)
    {
        const float x = std::clamp(posture, 0.0f, 1.0f) * (battleGearRig::kPostures - 1);
        const int k = (std::min)(static_cast<int>(x), battleGearRig::kPostures - 2);
        const float t = x - static_cast<float>(k);
        float ends[2];
        for (int e = 0; e < 2; ++e)
        {
            const int j = e == 0 ? 0 : battleGearRig::kUprights - 1;
            const float sole = soles[j][k] + (soles[j][k + 1] - soles[j][k]) * t;
            const float* a = reach[j][k][0];
            const float* b = reach[j][k + 1][0];
            ends[e] = sole + 0.5f * ((a[0] + a[1]) + ((b[0] + b[1]) - (a[0] + a[1])) * t);
        }
        return ends[0] + (ends[1] - ends[0]) * std::clamp(upright, 0.0f, 1.0f);
    }

    // where the front feet's homes stand ahead of the body (the reference sole plus the middle of its reach), blended between the
    // normal and the upright end: the in-between stances' wide reach bands would swing it back and forth
    float HomeAlong(float posture, float upright)
    {
        const float flat = HomeIn(battleGearRig::kReachFlat, battleGearRig::kSoleAlongFlat, posture, upright);
        return flat + (HomeIn(battleGearRig::kReach, battleGearRig::kSoleAlong, posture, upright) - flat) * g_Lean;
    }

    Stride StrideOf(int foot, const Gait& gait, float frontStance, float rearStance)
    {
        if (foot < 2)
            return { gait.duty, 1.0f, gait.offsets[foot], frontStance };
        return { g_RearDuty, kRearSteps, kRearOffsets[foot - 2], rearStance };
    }

    Vec3 IntoHeading(Vec3 world, Vec3 origin, float heading)   // the hull's heading frame: flat, x across, z ahead
    {
        const Vec3 w = Flat(Sub(world, origin));
        const float c = std::cos(heading);
        const float s = std::sin(heading);
        return { w.x * c - w.z * s, 0.0f, w.x * s + w.z * c };
    }

    Vec3 FromHeading(Vec3 local, Vec3 origin, float heading)
    {
        const float c = std::cos(heading);
        const float s = std::sin(heading);
        return { origin.x + local.x * c + local.z * s, origin.y, origin.z - local.x * s + local.z * c };
    }

    Vec3 TurnFlat(Vec3 local, float angle)   // a point of the heading frame as the frame sees it after turning by angle
    {
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        return { local.x * c + local.z * s, 0.0f, -local.x * s + local.z * c };
    }

    Vec3 GroundDrift(Vec3 local, Vec3 velocity, float yawRate)   // how a spot on the ground moves through the heading frame
    {
        return { -yawRate * local.z - velocity.x, 0.0f, yawRate * local.x - velocity.z };
    }

    float OnTheSpot(Vec3 velocity)   // 1 turning on the spot, 0 from a walk ahead
    {
        return 1.0f - std::clamp(std::fabs(velocity.z) / kSpotTurnTo, 0.0f, 1.0f);
    }

    Vec3 FrontTravel(Vec3 local, Vec3 velocity, float spot)   // a front foot's way over the ground: the turn's sweep counts on the spot
    {
        return Add(velocity, Scale({ g_YawRate * local.z, 0.0f, -g_YawRate * local.x }, spot));
    }

    void Rehome(const Vec3* home)
    {
        for (int i = 0; i < 4; ++i)
        {
            g_Feet[i] = { home[i], {}, home[i].y, 0.0f, home[i], 0.0f, 0.0f, 1.0f, true, {}, 0.0f, home[i].y, home[i].y };
            g_FootMiss[i] = 0.0f;
        }
        g_Velocity = {};
        g_YawRate = 0.0f;
        g_Accel = 0.0f;
        g_LastSpeed = 0.0f;
        g_Cadence = 0.0f;
    }

    // a planted foot keeps its ground spot; a swinging one travels with the hull to where its home will be in the middle of its next
    // stance; standing still, feet near home stay down until all four are, and while holdFeet (a stance switch carries the hull over
    // them) no planted foot lifts. The pods step twice per stride on their shorter reach. Side-stepping or turning on the spot, the
    // front feet carry the splay's shorter sideways stance (an oval of the two by the way each moves); upright they swing the
    // D-Walker's way.
    Gait UpdateGait(const Vec3* home, const Frame& body, float heading, float frontStance, float rearStance, float sideStance,
                    const float* sideShare, bool holdFeet, float dt)
    {
        const Vec3 moved = Flat(Sub(body.origin, g_LastBodyPos));
        g_LastBodyPos = body.origin;
        const float frame = (std::max)(dt, g_MeasuredFrame);
        if (Length(moved) > (std::max)(kTeleport, kFastest * frame * 1.5f))   // warped: the feet start again under the hull
        {
#ifdef _DEBUG
            if (TwitchLogAllowed())
                LogDebug("BattleGear twitch: feet put back under the hull, it moved %.2f m in %.3f s", Length(moved), frame);
#endif
            Rehome(home);
            return GaitAt(0.0f);
        }
        if (dt > 0.0f)
        {
            Vec3 sample = Scale(moved, 1.0f / frame);
            const float sampleSpeed = Length(sample);
            if (sampleSpeed > kFastest)
                sample = Scale(sample, kFastest / sampleSpeed);
            g_Velocity = Add(g_Velocity, Scale(Sub(sample, g_Velocity), kVelocityFollow));
        }
        const float speed = Length(g_Velocity);
        if (dt > 0.0f)
        {
            g_Accel += ((speed - g_LastSpeed) / frame - g_Accel) * kAccelFollow;
            g_LastSpeed = speed;
        }
        // turning, the feet travel though the hull does not: the gait of their speed gives long strides round, not quick short ones
        const Gait gait = GaitAt((std::max)(speed, (std::min)(std::fabs(g_YawRate) * kFootSpread, kTurnGaitSpeed)));
        const bool idle = speed < kMoving && std::fabs(g_YawRate) < kMoving;
        const float travel = speed + std::fabs(g_YawRate) * kFootSpread;
        const float minCadence = idle ? kMinCadence : kWalkMinCadence;
        const Vec3 velocity = IntoHeading(Add(body.origin, g_Velocity), body.origin, heading);
        const float hullNeed = StancesPerSecond(velocity, frontStance, sideStance) + std::fabs(g_YawRate) * kFootSpread / frontStance;
        const float spot = OnTheSpot(velocity);
        float feetNeed = 0.0f;   // the front foot that must cover the most stances a second
        for (int i = 0; i < 2; ++i)
            feetNeed = (std::max)(feetNeed, StancesPerSecond(FrontTravel(IntoHeading(home[i], body.origin, heading), velocity, spot),
                                                             frontStance, sideStance));
        float cadence = std::clamp((hullNeed + (feetNeed - hullNeed) * spot) * gait.duty, minCadence, kMaxCadence);
        bool settled = idle;
        for (int i = 0; i < 4 && settled; ++i)
            settled = g_Feet[i].stance && Length(Flat(Sub(g_Feet[i].planted, home[i]))) < kSettled;
        if (settled)
            cadence = 0.0f;
        g_Cadence = cadence;
        g_RearDuty = travel > 0.01f ? std::clamp(rearStance * kRearSteps * cadence / travel, (std::min)(kMinRearDuty, gait.duty), gait.duty)
                                    : gait.duty;
        g_Cycle = Cycle01(g_Cycle + cadence * dt);
        for (int i = 0; i < 4; ++i)
        {
            Foot& foot = g_Feet[i];
            const Stride stride = StrideOf(i, gait, frontStance, rearStance);
            const float advance = cadence * stride.steps * dt;
            const float pace = (std::max)(cadence, minCadence) * stride.steps;
            const float phase = Cycle01(g_Cycle * stride.steps - stride.offset);   // 0 = this foot touches down
            if (foot.stance)
            {
                foot.stood += advance;
                const bool held = holdFeet || (idle && Length(Flat(Sub(foot.planted, home[i]))) < kSettled);
                const bool stretched = g_FootMiss[i] > kStepEarlyMiss;   // its leg no longer reaches it: it steps out of turn
                const bool lifts = !held && cadence > 0.0f && foot.stood >= kMinStand * stride.duty
                                && ((phase >= stride.duty && 1.0f - phase >= kMinSwing * (1.0f - stride.duty)) || stretched);
                if (!lifts)
                {
                    const Vec3 away = Flat(Sub(foot.planted, home[i]));
                    const float distance = Length(away);
                    float most = stride.stance * kSlipShare;
                    if (i < 2 && distance > 1e-6f)
                    {
                        const Vec3 off = IntoHeading(Add(body.origin, away), body.origin, heading);
                        const float oval = OvalStance(std::fabs(off.z) / distance, std::fabs(off.x) / distance, frontStance, sideStance);
                        most = (stride.stance + (oval - stride.stance) * sideShare[i]) * kSlipShare;
                    }
                    if (distance > most)   // left behind by a sudden turn or speed change: it slips
                    {
                        const Vec3 kept = Add(Flat(home[i]), Scale(away, most / distance));
                        foot.planted = { kept.x, foot.planted.y, kept.z };
                    }
                    foot.planted.y = Approach(foot.planted.y, foot.ground, kPlantSettle * dt);
                    foot.now = foot.planted;
                    foot.arc = 0.0f;
                    continue;
                }
                foot.stance = false;
                foot.liftPhase = stretched ? (std::min)(phase, 1.0f - kMinSwing * (1.0f - stride.duty)) : phase;
                if (stretched)
                    ++g_EarlySteps;
                foot.swing = 0.0f;
                foot.liftLocal = IntoHeading(foot.planted, body.origin, heading);
                foot.liftY = foot.planted.y;
                foot.splayLift = i < 2 ? g_Splay[i] : 0.0f;
            }
            const bool justLifted = foot.swing == 0.0f;
            const float wasY = foot.now.y;
            foot.swing += advance / (1.0f - foot.liftPhase);
            const float s = (std::min)(foot.swing, 1.0f);
            const float span = (1.0f - foot.liftPhase) / pace;   // seconds the swing takes
            const float toMid = 0.5f * stride.duty / pace;       // seconds from touchdown to the middle of the stance
            const Vec3 touch = Add(TurnFlat(IntoHeading(home[i], body.origin, heading), g_YawRate * toMid), Scale(velocity, toMid));
            const Vec3 touchPace = Scale(GroundDrift(touch, velocity, g_YawRate), kTouchMatch * span);
            const float s2 = s * s;
            const float s3 = s2 * s;
            const Vec3 local = Add(Add(Scale(foot.liftLocal, 2.0f * s3 - 3.0f * s2 + 1.0f), Scale(touch, 3.0f * s2 - 2.0f * s3)),
                                   Scale(touchPace, s3 - s2));
            Vec3 landing = FromHeading(touch, body.origin, heading);
            landing.y = home[i].y;
            float groundY = landing.y;
            if (StandingGround(landing, landing.y, groundY))
                landing.y = groundY;
            foot.landing = landing;
            Vec3 path = local;
            float arc = gait.lift * std::sin(kPi * std::pow(s, kArcSkew[i]));
            float groundShare = s2 * (3.0f - 2.0f * s);
            const float dWalker = i < 2 ? g_Upright : 0.0f;
            if (dWalker > 0.0f)   // between the ground spot it left and the one it lands on, as that spot will be at touchdown
            {
                const float along = StepCurve(kStepForward, s);
                const Vec3 landsAt = Sub(touch, Scale(GroundDrift(touch, velocity, g_YawRate), (1.0f - s) * span));
                const Vec3 leftAt = IntoHeading(foot.planted, body.origin, heading);
                path = Lerp(local, Add(leftAt, Scale(Sub(landsAt, leftAt), along)), dWalker);
                arc += (kUprightLift * StepCurve(kStepHeight, s) - arc) * dWalker;
                groundShare += (along - groundShare) * dWalker;
            }
            foot.arc = arc;
            foot.now = FromHeading(path, body.origin, heading);
            // the landing spot slides with the hull: over a rock's edge or a ledge the ground read under it changes from one
            // frame to the next, and a foot following it whole jumped that far in a frame
            if (justLifted)
                foot.landingY = landing.y;
            else if (landing.y != foot.landingY && groundShare < kRebaseUntil)
            {
                foot.liftY += groundShare * (foot.landingY - landing.y) / (1.0f - groundShare);
                foot.landingY = landing.y;
            }
            landing.y = foot.landingY;
            foot.now.y = foot.liftY + (landing.y - foot.liftY) * groundShare + foot.arc;
            if (!justLifted)
                foot.now.y = (std::max)(foot.now.y, wasY + ((std::min)(g_GroundClimb, 0.0f) - kSwingDrop) * dt);
            if (foot.swing >= 1.0f && foot.now.y - landing.y < kLandWithin)
            {
                foot.stance = true;
                foot.planted = landing;
                foot.ground = PadGround(landing);
                foot.now = landing;
                foot.stood = 0.0f;
                foot.arc = 0.0f;
            }
        }
        return gait;
    }

    // the pods glide from the trot, in fast turns, while the hull stands tall (their glide sinks onto their own spots as the walk
    // stops) and while it hovers (their stepping spots would be where upright pretends its held-up pods stand); they land only slow,
    // straight and crouched again
    void UpdateFloat(float speed, float dt)
    {
        const float turning = std::fabs(g_YawRate);
        if (g_Hover > 0.0f)
            g_Floating = true;
        else if (g_Floating)
            g_Floating = speed >= kLandBelow || turning >= kLandTurn || g_Posture > kLandPosture;
        else
            g_Floating = speed >= kFloatFrom || turning >= kFloatTurn || g_Posture > kLandPosture;
        g_FloatShare = std::clamp(g_FloatShare + (g_Floating ? dt : -dt) / kFloatEase, 0.0f, 1.0f);
    }

    void PublishGlide(bool gliding, Vec3 groundUnderHull)
    {
        AcquireSRWLockExclusive(&g_HoverLock);
        g_Gliding = gliding;
        g_GroundUnderHull = groundUnderHull;
        ReleaseSRWLockExclusive(&g_HoverLock);
    }

    Frame ModelFrame(const std::uint8_t* skeleton)
    {
        if (*reinterpret_cast<const std::uint32_t*>(skeleton + kSkeletonBones) & kModelSpaceBones)
            return { { { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } }, { 0.0f, 0.0f, 0.0f } };
        return FromMatrix(reinterpret_cast<const float*>(skeleton + kSkeletonWorld));
    }

    // the shell as the idle bake stands it: btg0_hang0_def's hinge across, the turret's up axis vertical, facing the model's way
    Frame LevelHull(Vec3 ahead, Vec3 origin)
    {
        const Vec3 up = { 0.0f, 1.0f, 0.0f };
        const Vec3 side = Cross(up, ahead);
        const Vec3 hinge = FromPoint(battleGearRig::kHinge);
        const Vec3 turretUp = FromPoint(battleGearRig::kTurretUp);
        const Vec3 forward = FromPoint(battleGearRig::kAhead);
        Frame f{};
        for (int j = 0; j < 3; ++j)
            f.row[j] = Add(Add(Scale(side, At(hinge, j)), Scale(up, At(turretUp, j))), Scale(ahead, At(forward, j)));
        f.origin = origin;
        return f;
    }

    float FootLoad(float phase, float duty)   // weight a foot carries through its stance
    {
        return phase < duty ? std::sin(kPi * phase / duty) : 0.0f;
    }

    void Loads(float cycle, const Gait& gait, float* loads)   // the gliding pods carry the front pair's average
    {
        for (int i = 0; i < 2; ++i)
            loads[i] = FootLoad(Cycle01(cycle - gait.offsets[i]), gait.duty);
        const float glide = Ease(g_FloatShare);
        for (int i = 2; i < 4; ++i)
        {
            const float own = FootLoad(Cycle01(cycle * kRearSteps - kRearOffsets[i - 2]), g_RearDuty);
            loads[i] = own + (0.5f * (loads[0] + loads[1]) - own) * glide;
        }
    }

    // the hull dips while its feet carry it: bob with the weight on all four, nose with front against rear, side with left against
    // right; it squats nose up speeding up, dips its nose braking and leans into turns
    Sway HullSway(const Gait& gait, float speed)
    {
        float loads[4];
        float lowest = 4.0f;
        float highest = 0.0f;
        for (int k = 0; k < kLoadSamples; ++k)
        {
            Loads(static_cast<float>(k) / kLoadSamples, gait, loads);
            const float sum = loads[0] + loads[1] + loads[2] + loads[3];
            lowest = (std::min)(lowest, sum);
            highest = (std::max)(highest, sum);
        }
        Loads(g_Cycle, gait, loads);
        Sway s{};
        const float load = loads[0] + loads[1] + loads[2] + loads[3];
        const float stride = 1.0f - g_Posture * g_Upright * (1.0f - kUprightSway);
        if (highest - lowest > 0.05f)
            s.bob = -gait.bob * stride * (2.0f * (load - lowest) / (highest - lowest) - 1.0f);
        s.pitch = gait.pitch * stride * 0.5f * (loads[2] + loads[3] - loads[0] - loads[1])
                + std::clamp(kSquat * g_Accel, -kMaxSquat, kMaxSquat);
        s.roll = gait.roll * 0.5f * (loads[1] + loads[3] - loads[0] - loads[2])
               + std::clamp(-kBank * speed * g_YawRate, -kMaxBank, kMaxBank);
        return s;
    }

    void AddSway(Frame& f, const Sway& s)
    {
        const Vec3 side = HullAcross(f);
        const float noseUp = Cross(side, HullAhead(f)).y >= 0.0f ? s.pitch : -s.pitch;   // whichever turn raises the front
        for (Vec3& row : f.row)
            row = Rotate(row, side, noseUp);
        const Vec3 ahead = HullAhead(f);
        const float leftUp = Cross(ahead, HullAcross(f)).y >= 0.0f ? s.roll : -s.roll;
        for (Vec3& row : f.row)
            row = Rotate(row, ahead, leftUp);
        f.origin.y += s.bob;
    }

    // fire posture has no feet to rock on: the hovering hull squats and banks as the others do, milder, and leans into its glide
    Sway FireLean(Sway s, Vec3 ahead, Vec3 right, float share)
    {
        const float onward = Dot(g_FireVelocity, ahead);
        const float across = Dot(g_FireVelocity, right);
        s.pitch += share * (kFireTilt * std::clamp(kSquat * g_FireAccel, -kMaxSquat, kMaxSquat) - std::clamp(kFireNose * onward, -kFireMost, kFireMost));
        s.roll += share * (kFireTilt * std::clamp(-kBank * Length(g_FireVelocity) * g_YawRate, -kMaxBank, kMaxBank)
                           + std::clamp(kFireSide * across, -kFireMost, kFireMost));
        return s;
    }

    void AddRecoil(Frame& f, float dt)
    {
        const std::uint32_t shots = battleGearWeapons::RailShots();
        if (shots != g_SeenRailShots)
        {
            g_SeenRailShots = shots;
            g_RecoilKick = kRecoilPitch[std::clamp(battleGearWeapons::LastRailShotLevel(), 1, 3) - 1];
        }
        g_RecoilKick *= std::exp(-kRecoilSettle * dt);
        if (g_RecoilKick < 1e-4f)
            return;
        const Vec3 side = HullAcross(f);
        const float noseUp = Cross(side, HullAhead(f)).y >= 0.0f ? g_RecoilKick : -g_RecoilKick;   // whichever turn raises the front
        for (Vec3& row : f.row)
            row = Rotate(row, side, noseUp);
    }

    void SwayTail(const Sway& sway, float dt)   // a damped spring: it rises as the hull dips, lags speeding up, swings out in turns
    {
        const float pitchGoal = std::clamp(-kTailBob * sway.bob + kTailSurge * g_Accel, -kTailMost, kTailMost);
        const float yawGoal = std::clamp(-kTailTurn * g_YawRate, -kTailMost, kTailMost);
        const float k = kTailStiffness * kTailStiffness;
        const float c = 2.0f * kTailDamping * kTailStiffness;
        g_TailPitchRate += (k * (pitchGoal - g_TailPitch) - c * g_TailPitchRate) * dt;
        g_TailYawRate += (k * (yawGoal - g_TailYaw) - c * g_TailYawRate) * dt;
        g_TailPitch += g_TailPitchRate * dt;
        g_TailYaw += g_TailYawRate * dt;
    }

    void TailChain(const float* angles, Vec3* joints)   // the tail joints' pivots, then the hand, with the given pitches
    {
        const Vec3 axis = FromPoint(battleGearRig::kHinge);
        joints[0] = Bind(kFirstTailBone);
        float total = 0.0f;
        for (int k = 0; k < kTailBones; ++k)
        {
            total += angles[k];
            const int next = k + 1 < kTailBones ? kFirstTailBone + k + 1 : kTailHandBone;
            joints[k + 1] = Add(joints[k], Rotate(Sub(Bind(next), Bind(kFirstTailBone + k)), axis, total));
        }
    }

    // every tail joint pitches about the same hinge axis, so the tail stays in its plane: cyclic coordinate descent from the given
    // pitches brings the hand nearest the target (bind space)
    void SolveTail(Vec3 target, float* angles)
    {
        const Vec3 axis = FromPoint(battleGearRig::kHinge);
        Vec3 joints[kTailBones + 1];
        for (int iteration = 0; iteration < kTailIterations; ++iteration)
            for (int j = kTailBones - 1; j >= 0; --j)
            {
                TailChain(angles, joints);
                const float turn = SignedAngle(Planar(Sub(joints[kTailBones], joints[j]), axis), Planar(Sub(target, joints[j]), axis), axis);
                angles[j] = std::clamp(angles[j] + turn, -kTailJointLimit, kTailJointLimit);
            }
    }

    Vec3 LevelPoint(Vec3 level)   // across / up / ahead of the hull's middle, in the shell's bind space
    {
        return Add(kBodyBind, Add(Add(Scale(FromPoint(battleGearRig::kHinge), level.x), Scale(FromPoint(battleGearRig::kTurretUp), level.y)),
                                  Scale(FromPoint(battleGearRig::kAhead), level.z)));
    }

    // the tail follows the walker's carry: down at the body the prompt found while picking up, then curling it in under the rear,
    // holding it there, and down under the rear again to put it down; otherwise it sways. Publishes the box round the hull, on the
    // ground, that the rider's carry prompt searches.
    void UpdateTailCarry(const Frame& body, Vec3 ahead, float dt, bool ridden)
    {
        Vec3 centre = Sub(ToWorld(body, kBodyBind), Scale(ahead, kCarryBehind));
        if (ridden)
        {
            float groundY = centre.y;
            if (GroundBelow(centre, groundY))
                centre.y = groundY + kCarryAbove;
            battleGearWeapons::PublishCarryZone(&centre.x, kCarryHalf);
        }

        const battleGearWeapons::CarryPhase phase = ridden ? battleGearWeapons::Carry() : battleGearWeapons::CarryPhase::None;
        if (phase != g_LastCarry)
        {
            g_LastCarry = phase;
            g_CarryClock = 0.0f;
            float found[4];
            g_CarryFrom = phase == battleGearWeapons::CarryPhase::PickingUp && battleGearWeapons::CarryFound(found)
                        ? Vec3{ found[0], found[1], found[2] } : centre;
        }
        g_CarryClock += dt;
        const bool carrying = phase != battleGearWeapons::CarryPhase::None;
        g_TailCarryShare = Approach(g_TailCarryShare, carrying ? 1.0f : 0.0f, kTailCarryRate * dt);
        float goal[kTailBones] = {};
        if (carrying)
        {
            float curled = 1.0f;
            if (phase == battleGearWeapons::CarryPhase::PickingUp)
                curled = std::clamp((g_CarryClock - kCarryLiftAfter) / kCarryCurlTime, 0.0f, 1.0f);
            else if (phase == battleGearWeapons::CarryPhase::PuttingDown)
                curled = 1.0f - std::clamp(g_CarryClock / kCarryCurlTime, 0.0f, 1.0f);
            for (int k = 0; k < kTailBones; ++k)
                goal[k] = g_TailCarry[k];
            const Vec3 down = phase == battleGearWeapons::CarryPhase::PickingUp ? g_CarryFrom : centre;
            SolveTail(Lerp(ToBind(body, down), LevelPoint(kCarryHold), Ease(curled)), goal);
        }
        for (int k = 0; k < kTailBones; ++k)
            g_TailCarry[k] = Approach(g_TailCarry[k], goal[k], kTailJointRate * dt);
    }

    void PublishTailHand(const Frame& shown, const Turn* pose)   // the hold point: the tail hand, turned like the hull
    {
        Frame hand{};
        hand.row[0] = Normalized(BodyDir(shown, FromPoint(battleGearRig::kHinge)));
        hand.row[1] = Normalized(BodyDir(shown, FromPoint(battleGearRig::kTurretUp)));
        hand.row[2] = Normalized(BodyDir(shown, FromPoint(battleGearRig::kAhead)));
        hand.origin = ToWorld(shown, Apply(pose[kTailHandBone - kFirstRigBone], Bind(kTailHandBone)));
        alignas(16) float m[16];
        ToMatrix(hand, m);
        battleGearWeapons::PublishHoldPoint(m);
    }

    std::uint32_t PostLidSoundSEH(std::uint32_t eventId, const float* place)
    {
        __try
        {
            g_AkRegister(kLidSoundObject, nullptr, 1);
            g_AkSetPosition(kLidSoundObject, place);
            return g_AkPostEvent(eventId, kLidSoundObject, 0, nullptr, nullptr, 0, nullptr, 0);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    void PlayLidSound(std::uint32_t eventId, Vec3 at)
    {
        if (!g_AkRegister || !g_AkSetPosition || !g_AkPostEvent)
            return;
        const float place[6] = { at.x, at.y, -at.z, 0.0f, 0.0f, -1.0f };   // Wwise's axes: z flipped
        const std::uint32_t playing = PostLidSoundSEH(eventId, place);
#ifdef _DEBUG
        LogDebug("BattleGear lid: %s sound %s", eventId == kLidOpenSound ? "open" : "shut", playing ? "posted" : "not posted (its bank not loaded?)");
#else
        (void)playing;
#endif
    }

    void UpdateLid(float dt, Vec3 at)   // held by a hand it follows it; let go it falls shut
    {
        const float hold = battleGearWeapons::LidHold();
        if (hold >= 0.0f)
        {
            const float next = Approach(g_LidAngle, (std::min)(hold, kLidOpenMost), kLidHandRate * dt);
            g_LidSpeed = dt > 0.0f ? (next - g_LidAngle) / dt : 0.0f;
            g_LidAngle = next;
        }
        else if (g_LidAngle > 0.0f)
        {
            g_LidSpeed -= kLidGravity * dt;
            g_LidAngle += g_LidSpeed * dt;
            if (g_LidAngle >= kLidOpenMost && g_LidSpeed > 0.0f)
            {
                g_LidAngle = kLidOpenMost;
                g_LidSpeed *= kLidBounce;
            }
        }
        if (g_LidAngle <= 0.0f)
        {
            g_LidAngle = 0.0f;
            g_LidSpeed = 0.0f;
            if (g_LidSounded)
            {
                g_LidSounded = false;
                PlayLidSound(kLidShutSound, at);
            }
        }
        else if (!g_LidSounded && g_LidAngle > kLidSounded)
        {
            g_LidSounded = true;
            PlayLidSound(kLidOpenSound, at);
        }
    }

    void PublishCupola(const Frame& shown, const Turn* pose)   // the turret's cupola, beside the roof MG, turned like the turret
    {
        const Turn& turret = pose[kTurretBone - kFirstRigBone];
        Frame hatch{};
        hatch.row[0] = Normalized(BodyDir(shown, ApplyDir(turret, FromPoint(battleGearRig::kHinge))));
        hatch.row[1] = Normalized(BodyDir(shown, ApplyDir(turret, FromPoint(battleGearRig::kTurretUp))));
        hatch.row[2] = Normalized(BodyDir(shown, ApplyDir(turret, FromPoint(battleGearRig::kAhead))));
        hatch.origin = Sub(ToWorld(shown, Apply(turret, kCupolaOpening)), Scale(hatch.row[1], kRiderDrop));
        alignas(16) float m[16];
        ToMatrix(hatch, m);
        battleGearWeapons::PublishHatch(m);
        hatch.origin = ToWorld(shown, Apply(turret, Bind(kLidBone)));
        ToMatrix(hatch, m);
        battleGearWeapons::PublishLid(m);
    }

    // the hull rides the model level and faces its way, wherever the D-Walker's own body leans or aims; only the standing body's
    // place is learnt from the D-Walker
    Frame GaitBase(const Frame& model, const Frame& animated, bool rootLevel, bool reset, float dt)
    {
        const Vec3 up = { 0.0f, 1.0f, 0.0f };
        const Vec3 ahead = Normalized(Flat(model.row[2]));
        const Vec3 side = Cross(up, ahead);
        const float yaw = std::atan2(ahead.x, ahead.z);
        const float turnRate = dt > 0.0f ? std::fabs(WrapAngle(yaw - g_LastYaw)) / dt : 0.0f;
        g_LastYaw = yaw;
        if (reset)
        {
            g_RestKnown = false;
            g_RecoilKick = 0.0f;
            g_SeenRailShots = battleGearWeapons::RailShots();
        }
        const Vec3 gap = Sub(animated.origin, model.origin);
        if (Length(gap) > kMaxModelGap)
        {
            if (!g_ModelWarned)
                LogWarn("BattleGear: the walker's body is %.1f m from its model, the hull keeps the D-Walker's motion", Length(gap));
            g_ModelWarned = true;
            g_RestKnown = false;
            return animated;
        }
        const Vec3 place = { Dot(gap, side), gap.y, Dot(gap, ahead) };
        const float speed = Length(g_Velocity);
        if (!g_RestKnown)
            g_RestOffset = place;
        else if (rootLevel && g_Hover <= 0.0f && !battleGearWeapons::FireMode() && speed < kIdleSpeed && turnRate < kStillTurnRate)
            g_RestOffset = Lerp(g_RestOffset, place, kRestFollow);
        g_RestKnown = true;
        const Vec3 origin = Add(model.origin, Add(Add(Scale(side, g_RestOffset.x), Scale(up, g_RestOffset.y)), Scale(ahead, g_RestOffset.z)));
        return LevelHull(ahead, origin);
    }

    bool ArcReached(int foot, const FrontLeg* front, const RearLeg* rear, const Frame& body, Vec3 at, float arc, float share, Vec3 up)
    {
        const Vec3 target = ToBind(body, { at.x, at.y - arc * (1.0f - share), at.z });
        if (foot >= 2)
        {
            PodPose pose = g_PodPose[foot - 2];
            return PodNewton(rear[foot - 2], pose, target, up) < kReached;
        }
        FrontPose pose = g_FrontPose[foot];
        if (Reach(front[foot], pose, target, false) < kReached)
            return true;
        FrontPose folded = g_FrontPose[foot];
        return Reach(front[foot], folded, target, true) < kReached;
    }

    // largest share of a swinging foot's arc the leg reaches: the arc flattens where the leg cannot lift the foot so high
    float ArcShare(int foot, const FrontLeg* front, const RearLeg* rear, const Frame& body, Vec3 at, Vec3 up)
    {
        const Foot& f = g_Feet[foot];
        if (f.stance || f.arc <= 0.0f || ArcReached(foot, front, rear, body, at, f.arc, 1.0f, up))
            return 1.0f;
        float low = 0.0f;
        float high = 1.0f;
        for (int i = 0; i < kArcSearchSteps; ++i)
        {
            const float mid = 0.5f * (low + high);
            if (ArcReached(foot, front, rear, body, at, f.arc, mid, up))
                low = mid;
            else
                high = mid;
        }
        return low;
    }

    void PoseField(std::uint8_t* skeleton, bool guns, bool roofMg)
    {
        alignas(16) float m[16];
        g_BoneWorld(skeleton, m, kBodyBone);
        const Frame animated = FromMatrix(m);
        g_BoneWorld(skeleton, m, kRootBone);
        const bool rootLevel = std::fabs(Normalized(FromMatrix(m).row[2]).y) < std::sin(kRootLevel);
        const bool reset = g_Reset.exchange(false);
        const float dt = FrameSeconds();
        g_HangFold = 0.0f;
        const float step = reset ? 0.0f : dt;
        const Frame base = GaitBase(ModelFrame(skeleton), animated, rootLevel, reset, step);
        const Vec3 baseAhead = Normalized(Flat(HullAhead(base)));
        const float heading = std::atan2(baseAhead.x, baseAhead.z);
        battleGearWeapons::PublishBodyHeading(heading);
        const float turned = WrapAngle(heading - g_LastHeading);
        g_LastHeading = heading;
        const float frame = (std::max)(step, g_MeasuredFrame);
        const bool snapped = !reset && std::fabs(turned) > (std::max)(kHeadingJump, kMaxYawRate * frame * 1.5f);
        if (step > 0.0f && !snapped)
        {
            const float seen = std::clamp(turned / frame, -kMaxYawRate, kMaxYawRate);
            const float turning = battleGearWeapons::Ridden() ? battleGearWeapons::DriveTurn() : seen;   // ridden it turns only as
            g_YawRate += (turning - g_YawRate) * kVelocityFollow;   // steered: the model's heading sways while the turret looks aside
#ifdef _DEBUG
            g_SeenYawRate += (seen - g_SeenYawRate) * kVelocityFollow;
            g_TurnedLow = (std::min)(g_TurnedLow, turned);
            g_TurnedHigh = (std::max)(g_TurnedHigh, turned);
#endif
        }

        // a relocation (the hull warped or turned in one frame, or a foot is left far behind) starts the feet, the hull's ground fit and
        // the leg poses' limits over at once, in this frame
        bool relocated = snapped;
        if (!reset)
        {
            const Vec3 shift = Sub(base.origin, g_LastBasePos);
            relocated = relocated || Length(Flat(shift)) > (std::max)(kTeleport, kFastest * frame * 1.5f);   // not its height:
            // the walker's body is lifted over ledges and low things and drops off them under a hull that stands on its legs
            for (int i = 0; i < 4 && !relocated; ++i)
                relocated = Length(Flat(Sub(g_Feet[i].now, base.origin))) > kFeetLost;
            relocated = relocated || g_FeetAdrift;
        }
        g_FeetAdrift = false;
        g_LastBasePos = base.origin;
        const bool fresh = reset || relocated;

        const bool fire = battleGearWeapons::FireMode();
        const bool upright = battleGearWeapons::Upright();
        const bool ridden = battleGearWeapons::Ridden();
        const float speed = Length(g_Velocity);
        battleGearWeapons::PublishGroundSpeed(speed);
        const float driven = ridden ? battleGearWeapons::DriveSpeed() : speed;   // a stance switch's surge is no walking
        const bool still = driven < kMoving && std::fabs(g_YawRate) < kMoving;
        float postureGoal = 0.0f;
        if (upright)
            postureGoal = still ? 1.0f : kUprightStride;
        else if (ridden && !fire && !battleGearWeapons::Climbing())   // ridden it stays stood up; it kneels parked and for the climb in
            postureGoal = 1.0f;
        else if (!fire)
            postureGoal = std::clamp((speed - kTallFrom) / (kTallAt - kTallFrom), 0.0f, 1.0f);
        const float uprightGoal = upright ? 1.0f : (fire ? g_Upright : 0.0f);   // fire posture crouches out of either stand-up
        const float lastUpright = g_Upright;
        g_Upright = reset ? uprightGoal : Approach(g_Upright, uprightGoal, kPostureRate * step);
        const float rigUpright = g_Upright * kUprightShare;   // the legs' pose and reach tables; g_Upright itself blends the gait
        g_Posture = reset ? postureGoal : Approach(g_Posture, postureGoal, kPostureRate * step);
        const float leanGoal = std::clamp((driven - kLeanFrom) / (kLeanAt - kLeanFrom), 0.0f, 1.0f);
        g_Lean = reset ? leanGoal : Approach(g_Lean, leanGoal, kLeanRate * step);
        // a switch between normal and upright carries the hull over its planted front feet instead of stepping them back
        const bool surging = !reset && g_Hover <= 0.0f && ridden;
        if (surging)
            battleGearWeapons::RequestSurge(HomeAlong(g_Posture, lastUpright * kUprightShare) - HomeAlong(g_Posture, rigUpright));
        const bool holdFeet = surging && std::fabs(uprightGoal - g_Upright) > 1e-3f && still;
        const float neckGoal = battleGearWeapons::Aiming() ? 1.0f : 0.0f;
        g_NeckLift = reset ? neckGoal : Approach(g_NeckLift, neckGoal, kNeckRate * step);

        Turn pose[kRigBoneCount];
        RigPose(g_Posture, rigUpright, g_NeckLift, g_TurretYaw, g_RailPitch, g_TailPitch, g_TailYaw, pose);
        FrontLeg front[2] = { FrontLegAt(pose, 0, g_Posture, rigUpright), FrontLegAt(pose, 1, g_Posture, rigUpright) };
        RearLeg rear[2] = { RearLegAt(pose, 0), RearLegAt(pose, 1) };
        const Vec3 baseUp = UpInBind(base);
        Vec3 contact[4] = { front[0].sole, front[1].sole, PodContact(rear[0], {}, baseUp), PodContact(rear[1], {}, baseUp) };
        // upright holds the pods up off the ground: they count where they would stand; normal mode's stand-up reaches them lower,
        // there they carry it
        const float podsLifted = (std::max)(0.0f, PodsAboveFront(contact, baseUp) - g_PodsRest);
        for (int i = 2; i < 4; ++i)
            contact[i] = Sub(contact[i], Scale(baseUp, podsLifted));

        Vec3 feet[4];
        float rise[4] = {};
        float ground[4] = {};
        bool hit[4] = {};
        int hits = 0;
        Vec3 pivot = { 0.0f, 0.0f, 0.0f };
        float refHeight = 0.0f;
        Vec3 support[4];
        for (int i = 0; i < 4; ++i)
        {
            feet[i] = ToWorld(base, contact[i]);
            pivot = Add(pivot, Scale(feet[i], 0.25f));
            refHeight += 0.25f * Dot(Sub(contact[i], kBodyBind), baseUp);
            hit[i] = SupportUnder(i, feet[i], fresh, support[i]);
            if (hit[i])
                ++hits;
        }
        SupportRise(feet, support, hit, hits, ground, rise);
        // The game puts a parked walker in place under its planted feet (as a mission starts, on its preview spot): they keep
        // the ground they stood on and hold the hull up in the air. A ridden one dropping off a ledge keeps them: it falls.
        if (!fresh && !ridden && driven < kMoving)
        {
            int standing = 0;
            int adrift = 0;
            for (int i = 0; i < 4; ++i)
                if (g_Feet[i].stance)
                {
                    ++standing;
                    if (std::fabs(g_Feet[i].ground - feet[i].y) >= kFeetAdrift)
                        ++adrift;
                }
            g_FeetAdrift = standing > 0 && adrift == standing;
        }
        for (int i = 0; i < 4; ++i)
        {
            g_PlaneKnown[i] = hit[i];
            g_PlaneUnder[i] = ground[i];
        }

        const float hoverGoal = fire ? kHoverHeight : (ridden && battleGearWeapons::Pivoting() ? kSpinHover : 0.0f);
        const Vec3 baseAcross = Normalized(Flat(HullAcross(base)));
        Tilt target = GroundTilt(feet, rise, hit, hits, baseAcross, baseAhead);
        if (hits >= kMinHits)   // its tilt takes the lie of the ground round it, its height stays near what carries it
        {
            Vec3 lead = Scale(Flat(g_Velocity), kLieLead);
            if (Length(lead) > kLieMostLead)
                lead = Scale(lead, kLieMostLead / Length(lead));
            Vec3 around[kLieSamples];
            int aroundCount = 0;
            for (const float along : kLieAlong)
                for (const float out : { -kLieAcross, kLieAcross })
                {
                    const Vec3 at = Add(Add(pivot, lead), Add(Scale(baseAhead, along), Scale(baseAcross, out)));
                    float groundY = 0.0f;
                    if (GroundBelow(at, groundY))
                        around[aroundCount++] = { at.x, groundY, at.z };
                }
            float lie[4];
            const bool everyContact[4] = { true, true, true, true };
            if (GroundLie(feet, support, hit, around, aroundCount, lie))
            {
                const Tilt lay = GroundTilt(feet, lie, everyContact, 4, baseAcross, baseAhead);
                target.side = lay.side;
                target.front = lay.front;
                target.lift = std::clamp(lay.lift, target.lift - kLieTrust, target.lift + kLieTrust);   // a foot's change of
                // support moves the plane through four spots at once; the lie of the ground round the hull does not
            }
        }
        if (hits >= kMinHits || fresh)
            g_FitLift = target.lift;   // with too few feet over ground the last fit rides the walker: a fall takes the hull along
        float fitted = base.origin.y + g_FitLift + refHeight;
        if (fresh)
        {
            g_Tilt = target;
            g_TiltGoal = target;
            g_TiltRate = {};
            g_GroundHeight = fitted;
            g_GroundClimb = 0.0f;
            g_FitRate = 0.0f;
            g_LastFitted = fitted;
        }
        if (reset)
        {
            g_LidAngle = 0.0f;
            g_LidSpeed = 0.0f;
            g_LidSounded = false;
            g_TurretYaw = 0.0f;
            g_RailPitch = 0.0f;
            g_RoofMgYaw = 0.0f;
            g_RoofMgPitch = 0.0f;
            g_TailPitch = g_TailYaw = g_TailPitchRate = g_TailYawRate = 0.0f;
            for (float& joint : g_TailCarry)
                joint = 0.0f;
            g_TailCarryShare = 0.0f;
            g_Hover = 0.0f;
            g_Floating = false;
            g_FloatShare = 0.0f;
        }
        const bool rising = hoverGoal > g_Hover;
        const bool liftingOff = rising && g_Hover <= 0.0f;
        g_Hover = Approach(g_Hover, hoverGoal, kHoverRate * step);
        const float airborne = std::clamp(g_Hover / kLiftOffHeight, 0.0f, 1.0f);
        target.side *= 1.0f - airborne;   // level in the air
        target.front *= 1.0f - airborne;
        // At a run the legs take up what the ground does under them: the hull tilts and rises only with what they cannot,
        // and comes round to the rest slowly. Slow or standing it lies as the ground does. Ground that falls away it
        // follows at once: its feet reach no further down.
        const float give = fresh ? 0.0f : std::clamp((speed - kGiveFrom) / (kGiveAt - kGiveFrom), 0.0f, 1.0f) * (1.0f - airborne);
        const float sideGive = std::atan(kLegGive / kGiveAcross) * give;
        const float frontGive = std::atan(kLegGive / kGiveAhead) * give;
        g_TiltGoal.side = TakenUp(g_TiltGoal.side, target.side, sideGive, sideGive, kTiltSettle, step);
        g_TiltGoal.front = TakenUp(g_TiltGoal.front, target.front, frontGive, frontGive, kTiltSettle, step);
        g_TiltGoal.lift = fresh ? fitted : TakenUp(g_TiltGoal.lift, fitted, 0.0f, kHeightGive * give, kHeightSettle, step);
        fitted = g_TiltGoal.lift;
        SpringToward(g_Tilt.side, g_TiltRate.side, g_TiltGoal.side, kMaxTiltRate, step);
        SpringToward(g_Tilt.front, g_TiltRate.front, g_TiltGoal.front, kMaxTiltRate, step);
        // the hull stands on its legs: its height follows the ground fitted under its feet, and not the walker's own body, which
        // the game sets up onto ledges and the walk sets down off them at once. Within its legs' range a damped spring takes it
        // there, led by the fit's own speed so a slope is climbed without lag. More than kLegSlack over the fit its feet are
        // off the ground and it falls: off a drop it comes down in an arc, its legs reaching for the ground, and lands with a
        // squat. Rising ground carries it once its legs have given kMaxLiftLag. Its posture's own rise lands at once.
        const float bellyNeed = BellyRise(pivot, baseAcross, baseAhead, g_Velocity, -refHeight - kBellyUnderBody, fresh);
        g_BellyRise = fresh ? bellyNeed : Approach(g_BellyRise, bellyNeed, (bellyNeed > g_BellyRise ? kBellyUp : kBellyDown) * step);
        if (g_BellyRise > 0.0f)
            fitted = (std::max)(fitted, g_GroundHeight + g_BellyRise);
        if (fitted < g_GroundHeight - kFarBelow)
        {
            g_GroundHeight = fitted;
            g_GroundClimb = 0.0f;
            g_FitRate = 0.0f;
        }
        else if (step > 0.0f)
        {
            const float fitRate = std::clamp((fitted - g_LastFitted) / step, -kFastestFit, kFastestFit);
            g_FitRate += (fitRate - g_FitRate) * (1.0f - std::exp(-step / kFitRateSmooth));
            if (g_GroundHeight > fitted + kLegSlack)
                g_GroundClimb -= kHullFall * step;
            else
                g_GroundClimb += std::clamp(kHeightSpring * kHeightSpring * (fitted - g_GroundHeight)
                                            + 2.0f * kHeightSpring * (g_FitRate - g_GroundClimb), -kHullPush, kHullPush) * step;
            g_GroundHeight += g_GroundClimb * step;
            if (g_GroundHeight < fitted - kMaxLiftLag)
            {
                g_GroundHeight = fitted - kMaxLiftLag;
                g_GroundClimb = (std::max)(g_GroundClimb, g_FitRate);
            }
        }
        g_LastFitted = fitted;
        g_Tilt.lift = g_GroundHeight - refHeight - base.origin.y;
        Frame body = Tilted(base, pivot);
        body.origin.y += g_Hover;
        {
            const Vec3 fitUp = Normalized(BodyDir(body, FromPoint(battleGearRig::kTurretUp)));
            const Vec3 fitAhead = Normalized(Planar(HullAhead(body), fitUp));
            const Frame fitted{ { Cross(fitUp, fitAhead), fitUp, fitAhead }, body.origin };
            ToMatrix(fitted, m);
            battleGearWeapons::PublishHullFrame(m, g_Hover - refHeight);   // the reference contacts stand refHeight below the body bone
        }

        float nearest[4];
        float farthest[4];
        for (int i = 0; i < 4; ++i)
        {
            ReachAt(g_Posture, rigUpright, i, nearest[i], farthest[i]);
            if (i < 2)
                farthest[i] -= kFrontReachSpare;
        }
        const float frontStance = kFrontStanceShare * (std::min)(farthest[0] - nearest[0], farthest[1] - nearest[1]);
        const float rearStance = kRearStanceShare * (std::min)(farthest[2] - nearest[2], farthest[3] - nearest[3]);
        const Vec3 ahead = Normalized(Flat(HullAhead(body)));
        const Vec3 right = { ahead.z, 0.0f, -ahead.x };
        float bandLow[2];
        float bandHigh[2];
        for (int leg = 0; leg < 2; ++leg)
            SideBand(front[leg], body, right, bandLow[leg], bandHigh[leg]);
        const float sideStance = kSideStanceShare * (std::min)(bandHigh[0] - bandLow[0], bandHigh[1] - bandLow[1]);
        const Vec3 velocity = IntoHeading(Add(body.origin, g_Velocity), body.origin, heading);
        const float spot = OnTheSpot(velocity);
        float sideShare[2] = {};
        Vec3 reference[4];
        Vec3 home[4];
        for (int i = 0; i < 4; ++i)
        {
            reference[i] = ToWorld(body, contact[i]);
            home[i] = Add(reference[i], Scale(ahead, 0.5f * (nearest[i] + farthest[i])));
            if (i < 2)   // moving sideways over the ground, the front feet stand in the middle of what the splay pair reaches
            {
                const Vec3 travel = FrontTravel(IntoHeading(home[i], body.origin, heading), velocity, spot);
                const float t = Length(travel);
                sideShare[i] = t > 1e-3f ? std::fabs(travel.x) / t * (std::min)(t / kSideEase, 1.0f) : 0.0f;
                home[i] = Add(home[i], Scale(right, 0.5f * (bandLow[i] + bandHigh[i]) * sideShare[i]));
            }
            float homeGround = 0.0f;
            if (StandingGround(home[i], home[i].y - g_Hover, homeGround))
                home[i].y = homeGround;
        }
        if (reset)
        {
            Rehome(home);
            g_FrontPose[0] = g_FrontPose[1] = {};
            for (int i = 0; i < 2; ++i)
                g_PodPose[i] = g_PodFloat[i] = g_PodHold[i] = g_PodShown[i] = {};
            g_Cycle = 0.0f;
            g_LastBodyPos = body.origin;
            g_FireVelocity = {};
            g_FireAccel = 0.0f;
        }
        else if (relocated)
        {
#ifdef _DEBUG
            if (TwitchLogAllowed())
                LogDebug("BattleGear twitch: feet put back under the hull, it turned %.0f deg and moved %.2f m (a foot %.1f m away) in %.3f s",
                         turned * 57.29578f, Length(Flat(Sub(body.origin, g_LastBodyPos))), Length(Flat(Sub(g_Feet[0].now, base.origin))), frame);
#endif
            Rehome(home);
            g_LastBodyPos = body.origin;
            g_FireVelocity = {};
            g_FireAccel = 0.0f;
        }
        if (liftingOff)
            for (int i = 0; i < 4; ++i)
                g_LiftOffFeet[i] = g_Feet[i].now;
        Gait gait = GaitAt(0.0f);
        if (g_Hover > 0.0f)
        {
            const Vec3 glided = Flat(Sub(body.origin, g_LastBodyPos));
            if (step > 0.0f && !fresh)   // the glide is read off the hull's own travel, as the gait reads it; Rehome clears the gait's
            {
                const Vec3 before = g_FireVelocity;
                Vec3 sample = Scale(glided, 1.0f / frame);
                if (Length(sample) > kFastest)
                    sample = Scale(sample, kFastest / Length(sample));
                g_FireVelocity = Add(before, Scale(Sub(sample, before), kVelocityFollow));
                g_FireAccel += (Dot(Sub(g_FireVelocity, before), ahead) / frame - g_FireAccel) * kAccelFollow;
            }
            const float turning = g_YawRate;
            Rehome(home);
            g_YawRate = turning;   // the bank and the tail's swing follow the turn
            for (int i = 0; i < 4; ++i)
                g_Feet[i].now = Lerp(rising ? g_LiftOffFeet[i] : home[i], reference[i], airborne);
            g_LastBodyPos = body.origin;
        }
        else
        {
            g_FireVelocity = {};
            g_FireAccel = 0.0f;
            gait = UpdateGait(home, body, heading, frontStance, rearStance, sideStance, sideShare, holdFeet, step);
        }
        UpdateFloat(speed, step);

        Sway swayGoal = HullSway(gait, Length(g_Velocity));
        if (g_Hover > 0.0f)
            swayGoal = FireLean(swayGoal, ahead, right, airborne);
        const float follow = fresh ? 1.0f : 1.0f - std::exp(-step / kSwayLag);
        g_Sway = { g_Sway.bob + (swayGoal.bob - g_Sway.bob) * follow, g_Sway.pitch + (swayGoal.pitch - g_Sway.pitch) * follow,
                   g_Sway.roll + (swayGoal.roll - g_Sway.roll) * follow };
        const Sway sway = g_Sway;
        Frame shown = body;
        AddSway(shown, sway);
        AddRecoil(shown, step);
        SwayTail(sway, step);
        UpdateTailCarry(shown, ahead, step, ridden);
#ifdef _DEBUG
        const Vec3 shownUp = Normalized(BodyDir(shown, FromPoint(battleGearRig::kTurretUp)));
        if (!fresh && g_ShownKnown)
        {
            const float heightChange = (base.origin.y + g_Tilt.lift) - (g_LastBaseOrigin.y + g_LastTilt.lift);
            const Vec3 travel = Add(Flat(Sub(base.origin, g_LastBaseOrigin)), { 0.0f, heightChange, 0.0f });
            const float jump = Length(Sub(Sub(shown.origin, g_LastShownOrigin), travel));
            const float tip = std::acos(std::clamp(Dot(shownUp, g_LastShownUp), -1.0f, 1.0f));
            if ((jump > kTwitchJump || tip > kTwitchTilt) && TwitchLogAllowed())
                LogDebug("BattleGear twitch: hull jumped %.2f m off its own path and tipped %.1f deg in one frame (ground fit moved "
                         "lift %+.2f m, left %+.1f, front %+.1f deg; %d of 4 contacts over ground; %.1f m/s, hover %.2f m)", jump, tip * 57.29578f,
                         g_Tilt.lift - g_LastTilt.lift, (g_Tilt.side - g_LastTilt.side) * 57.29578f,
                         (g_Tilt.front - g_LastTilt.front) * 57.29578f, hits, Length(g_Velocity), g_Hover);
        }
        g_LastShownOrigin = shown.origin;
        g_LastBaseOrigin = base.origin;
        g_LastShownUp = shownUp;
        g_LastTilt = g_Tilt;
        g_ShownKnown = true;
#endif
        if (guns)
            AimGuns(shown, pose, step);
        const float tailSway = 1.0f - g_TailCarryShare;
        RigPose(g_Posture, rigUpright, g_NeckLift, g_TurretYaw, g_RailPitch, g_TailPitch * tailSway, g_TailYaw * tailSway, pose, g_TailCarry);
        if (roofMg)
            AimRoofMg(shown, pose, step);

        const Vec3 up = Normalized(IntoAxes(shown, BodyDir(body, FromPoint(battleGearRig::kTurretUp))));   // the fitted ground's normal
        Turn legTurns[kLegJoints];
        float frontMiss = 0.0f;
        const float travelling = speed + std::fabs(g_YawRate) * kFootSpread;
        const float frontStep = fresh ? FLT_MAX : (kFrontJointRate + kFrontJointPerSpeed * travelling) * frame;
        const float podStep = fresh ? FLT_MAX : kPodSolveShare * kPodTurnRate * frame;
        const float splayFade = std::clamp((kSplayFadeTo - speed) / (kSplayFadeTo - kSplayFadeFrom), 0.0f, 1.0f);
        for (int leg = 0; leg < 2; ++leg)
        {
            const Foot& foot = g_Feet[leg];
            float splayGoal = 0.0f;
            if (foot.stance)
                splayGoal = SplayGoal(front[leg], ToBind(shown, foot.now)) * splayFade;
            else   // a swinging foot carries the splay from where it lifted to what its landing spot needs
                splayGoal = foot.splayLift + (SplayGoal(front[leg], ToBind(shown, foot.landing)) * splayFade - foot.splayLift)
                                                 * Ease((std::min)(foot.swing, 1.0f));
            g_Splay[leg] = fresh ? splayGoal : Approach(g_Splay[leg], splayGoal, kSplayRate * step);
            g_Splay[leg] = std::clamp(g_Splay[leg], kSplayLow - front[leg].splayRef, kSplayHigh - front[leg].splayRef);
            const Vec3 slide = SplayShift(front[leg], g_Splay[leg]);
            SlideLeg(front[leg], slide);
            legTurns[kFirstSplayJoint + leg * 2] = TurnAbout(front[leg].splayAxis, g_Splay[leg], JointAt(pose, kFrontBones[leg].splay));
            legTurns[kFirstSplayJoint + leg * 2 + 1] = Translate(slide);
            const float share = ArcShare(leg, front, rear, shown, foot.now, up);
            const Vec3 aim = ToBind(shown, { foot.now.x, foot.now.y - foot.arc * (1.0f - share), foot.now.z });
#ifdef _DEBUG
            const FrontPose before = g_FrontPose[leg];
#endif
            const float miss = SolveFront(front[leg], g_FrontPose[leg], aim, frontStep);
            frontMiss = (std::max)(frontMiss, miss);
            g_FootMiss[leg] = foot.stance ? miss : 0.0f;
#ifdef _DEBUG
            if (!fresh && Length(Sub(aim, g_LastAim[leg])) > kTwitchAim && TwitchLogAllowed())   // TEMP probe
                LogDebug("BattleGear twitch: front leg %d was sent %.2f m from its last spot in one frame (%+.2f m up; %s, %.0f%% "
                         "through its swing; the ground at its landing spot read %.2f, then %.2f)", leg,
                         Length(Sub(aim, g_LastAim[leg])), Dot(Sub(aim, g_LastAim[leg]), up), foot.stance ? "planted" : "swinging",
                         (std::min)(foot.swing, 1.0f) * 100.0f, g_LastLanding[leg], foot.landing.y);
            g_LastAim[leg] = aim;
            g_LastLanding[leg] = foot.landing.y;
            if (!fresh && FrontJump(before, g_FrontPose[leg]) > kTwitchLeg && TwitchLogAllowed())
                LogDebug("BattleGear twitch: front leg %d solution jumped %.0f deg in one frame (%s, misses %.2f m)", leg,
                         FrontJump(before, g_FrontPose[leg]) * 57.29578f, foot.stance ? "planted" : "swinging", miss);
#endif
            FrontTurns(front[leg], g_FrontPose[leg], slide, legTurns + leg * 4);
        }
        float rearMiss = 0.0f;
        const float glide = Ease(g_FloatShare);
        for (int i = 0; i < 2; ++i)
        {
            const int f = 2 + i;
            const Foot& foot = g_Feet[f];
            Vec3 stepAim{};
            if (!g_Floating)
            {
                Vec3 at = foot.now;
                if (g_Hover <= 0.0f)   // the pod's spot stays inside what it reaches
                {
                    const float along = Dot(Flat(Sub(at, reference[f])), ahead);
                    const float kept = std::clamp(along, nearest[f] + kRearMargin, farthest[f] - kRearMargin);
                    at = Add(at, Scale(ahead, kept - along));
                }
                const float share = ArcShare(f, front, rear, shown, at, up);
                stepAim = ToBind(shown, { at.x, at.y - foot.arc * (1.0f - share), at.z });
#ifdef _DEBUG
                const PodPose before = g_PodPose[i];
#endif
                const float miss = SolvePod(rear[i], g_PodPose[i], stepAim, up, podStep);
                g_FootMiss[f] = foot.stance ? miss : 0.0f;
#ifdef _DEBUG
                const float jump = (std::max)(std::fabs(g_PodPose[i].base - before.base), std::fabs(g_PodPose[i].hip - before.hip));
                if (!fresh && jump > kTwitchTurn && TwitchLogAllowed())
                    LogDebug("BattleGear twitch: rear pod %d solution jumped %.0f deg in one frame (%s, misses %.2f m)", i, jump * 57.29578f,
                             foot.stance ? "planted" : "swinging", miss);
#endif
                g_PodHold[i] = g_PodPose[i];
            }
            else
            {
                g_FootMiss[f] = 0.0f;
            }
            Vec3 floatAim{};
            if (g_FloatShare > 0.0f)
            {
                const float bob = kFloatBob * std::sin(2.0f * kPi * (g_Cycle * kRearSteps - kRearOffsets[i]));
                const float lift = (std::min)(speed / kFloatFrom, 1.0f);   // standing tall they rest on their own spots
                floatAim = Add(PodContact(rear[i], {}, up), Scale(up, (kFloatHeight + bob) * lift));
                if (hit[f])   // gliding over a rock or a kerb the pod rises to stay on top of it, as far as it reaches
                {
                    const float above = ToWorld(shown, floatAim).y - support[f].y;
                    if (above < kFloatHeight * lift)
                        floatAim = Add(floatAim, Scale(up, kFloatHeight * lift - above));
                }
                SolvePod(rear[i], g_PodFloat[i], floatAim, up, podStep);
            }
            const PodPose goal = g_FloatShare > 0.0f ? LerpPod(g_PodHold[i], g_PodFloat[i], glide) : g_PodHold[i];
            g_PodShown[i] = fresh ? goal : ApproachPod(g_PodShown[i], goal, kPodTurnRate * step);
            if (g_FloatShare >= 1.0f)
                rearMiss = (std::max)(rearMiss, PodMiss(rear[i], g_PodShown[i], floatAim, up));
            else if (g_FloatShare <= 0.0f && !g_Floating)
                rearMiss = (std::max)(rearMiss, PodMiss(rear[i], g_PodShown[i], stepAim, up));
            RearTurns(rear[i], g_PodShown[i], legTurns + kFirstRearJoint + i * 2);
        }
        for (int k = 0; k < kRigBoneCount; ++k)
            if (g_JointOf[k] >= 0)
                pose[k] = Then(pose[k], legTurns[g_JointOf[k]]);
        AimCylinders(pose);
        if (guns)
        {
            UpdateLid(step, ToWorld(shown, Apply(pose[kTurretBone - kFirstRigBone], Bind(kLidBone))));
            if (g_LidAngle > 0.0f)   // a negative turn about the hinge axis raises its front edge
                pose[kLidBone - kFirstRigBone] = Then(TurnAbout(FromPoint(battleGearRig::kHinge), -g_LidAngle, Bind(kLidBone)),
                                                      pose[kLidBone - kFirstRigBone]);
        }

        BoneMatrixAccessor accessor;
        g_AccessorCtor(&accessor, skeleton);
        ToMatrix(shown, m);
        g_SetBoneWorld(&accessor, kBodyBone, m);
        WriteRig(&accessor, shown, pose);
        if (roofMg)
            WriteRoofMg(&accessor, shown, pose, g_RoofMgYaw, g_RoofMgPitch);
        if (guns)
            PublishWeapons(shown, pose, roofMg, step);
        g_HangFrom = shown;
        g_HangOffset = Sub(shown.origin, animated.origin);
        if (ridden)
            PublishTailHand(shown, pose);
        PublishCupola(shown, pose);   // the get-on reads it before the ride's first frame: a cupola kept from the last ride put
                                      // the rider, and the map's streaming round him, wherever that ride ended

        float groundY = 0.0f;
        for (int i = 0; i < 4; ++i)
            if (hit[i])
                groundY += ground[i];
        if (hoverGoal > 0.0f && airborne >= 1.0f && hits > 0)
            PublishGlide(true, { pivot.x, groundY / static_cast<float>(hits), pivot.z });
        else if (g_FloatShare >= 0.5f)
            PublishGlide(true, Lerp(home[2], home[3], 0.5f));
        else
            PublishGlide(false, {});
        g_WorstFrontMiss = (std::max)(g_WorstFrontMiss, frontMiss);
        g_WorstPlantedMiss = (std::max)(g_WorstPlantedMiss, (std::max)(g_FootMiss[0], g_FootMiss[1]));
        g_WorstRearMiss = (std::max)(g_WorstRearMiss, rearMiss);

#ifdef _DEBUG
        const ULONGLONG now = GetTickCount64();
        if (now >= g_NextLog.load(std::memory_order_relaxed))
        {
            g_NextLog.store(now + 1000, std::memory_order_relaxed);
            const float moving = Length(g_Velocity);
            g_BoneWorld(skeleton, m, kRootBone);
            const Vec3 rootAhead = Normalized(FromMatrix(m).row[2]);
            const Vec3 walkerAhead = HullAhead(animated);
            LogDebug("BattleGear legs: %d of 4 contacts over ground | rise LF %.2f RF %.2f LB %.2f RB %.2f | tilt left %.1f front %.1f deg, "
                     "lift %.2f m | %s %.1f m/s, turning %.0f deg/s, side-step %.2f, %.2f cycles/s, pod duty %.2f, feet down %d%d%d%d, posture %.2f "
                     "(upright %.2f, lean %.2f), front splay %.0f / %.0f deg, pods glide %.2f | legs miss up to %.2f m, pods %.2f m | sway bob %.2f m, "
                     "pitch %.1f, roll %.1f deg | "
                     "%s stance, hover %.2f m, neck lift %.2f, turret %.1f rail %.1f roof MG %.0f / %.0f deg%s | hull %s; the D-Walker's own "
                     "body pitches %.1f "
                     "deg, its root %.1f deg (nose up) | tail carry %s, share %.2f | %d early steps | model turning %.0f deg/s, its heading "
                     "steps %.2f / %.2f deg | the ground lies %.1f / %.1f deg, its legs take up %.1f / %.1f deg and %.2f m, belly "
                     "asks %.2f m, planted front feet miss up to %.2f m | model-space bones %d, model up %.2f %.2f %.2f, shell up %.2f "
                     "%.2f %.2f, walker body %.2f %.2f %.2f",
                     hits, rise[0], rise[1], rise[2], rise[3], g_Tilt.side * 57.29578f, g_Tilt.front * 57.29578f, g_Tilt.lift,
                     GaitName(moving), moving, g_YawRate * 57.29578f, (std::max)(sideShare[0], sideShare[1]), g_Cadence, g_RearDuty,
                     g_Feet[0].stance, g_Feet[1].stance,
                     g_Feet[2].stance, g_Feet[3].stance, g_Posture, g_Upright, g_Lean, (front[0].splayRef + g_Splay[0]) * 57.29578f,
                     (front[1].splayRef + g_Splay[1]) * 57.29578f, g_FloatShare, g_WorstFrontMiss, g_WorstRearMiss, sway.bob,
                     sway.pitch * 57.29578f, sway.roll * 57.29578f, fire ? "fire" : (upright ? "upright" : "normal"), g_Hover, g_NeckLift,
                     g_TurretYaw * 57.29578f, -g_RailPitch * 57.29578f, g_RoofMgYaw * 57.29578f, -g_RoofMgPitch * 57.29578f,
                     guns ? (roofMg ? "" : " (no roof MG bones)") : " (no turret bone)",
                     g_RestKnown ? "level on its model" : "D-Walker motion",
                     std::asin(std::clamp(walkerAhead.y, -1.0f, 1.0f)) * 57.29578f, std::asin(std::clamp(rootAhead.y, -1.0f, 1.0f)) * 57.29578f,
                     CarryName(g_LastCarry), g_TailCarryShare, g_EarlySteps, g_SeenYawRate * 57.29578f, g_TurnedLow * 57.29578f,
                     g_TurnedHigh * 57.29578f, target.side * 57.29578f, target.front * 57.29578f,
                     (target.side - g_TiltGoal.side) * 57.29578f, (target.front - g_TiltGoal.front) * 57.29578f,
                     g_LastFitted - g_GroundHeight, g_BellyRise, g_WorstPlantedMiss,   // TEMP probe: these and on
                     (*reinterpret_cast<const std::uint32_t*>(skeleton + kSkeletonBones) & kModelSpaceBones) != 0 ? 1 : 0,
                     ModelFrame(skeleton).row[1].x, ModelFrame(skeleton).row[1].y, ModelFrame(skeleton).row[1].z, shownUp.x, shownUp.y,
                     shownUp.z, animated.origin.x, animated.origin.y, animated.origin.z);
            g_WorstPlantedMiss = 0.0f;
            g_EarlySteps = 0;
            g_TurnedLow = 0.0f;
            g_TurnedHigh = 0.0f;
            g_WorstFrontMiss = 0.0f;
            g_WorstRearMiss = 0.0f;
        }
#endif
    }

    void PoseRest(std::uint8_t* skeleton, bool roofMg)   // btg0_hang0_def's crouch on every Battle Gear that is not the one out in the field
    {
        alignas(16) float m[16];
        g_BoneWorld(skeleton, m, kBodyBone);
        const Frame body = FromMatrix(m);
        Turn pose[kRigBoneCount];
        RigPose(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, pose);
        AimCylinders(pose);
        BoneMatrixAccessor accessor;
        g_AccessorCtor(&accessor, skeleton);
        WriteRig(&accessor, body, pose);
        if (roofMg)
            WriteRoofMg(&accessor, body, pose, 0.0f, 0.0f);
    }

    // under its Fulton balloon or dropping in: the hull comes level, as the vehicles hang, and the legs fold into btg0_hang0_def's
    // crouch (the field pose's gait would walk them through the air)
    void PoseHanging(std::uint8_t* skeleton, bool roofMg)
    {
        alignas(16) float m[16];
        g_BoneWorld(skeleton, m, kBodyBone);
        const Frame animated = FromMatrix(m);
        const float dt = FrameSeconds();
        if (g_HangFold <= 0.0f && Length(g_HangFrom.row[2]) < 0.5f)   // lifted before any field frame: nothing to fold from
        {
            g_HangFrom = animated;
            g_HangOffset = {};
        }
        g_HangFold = Approach(g_HangFold, 1.0f, dt / kHangFoldTime);
        const float kept = 1.0f - g_HangFold * g_HangFold * (3.0f - 2.0f * g_HangFold);
        const Vec3 worldUp{ 0.0f, 1.0f, 0.0f };
        Frame level = animated;
        const Vec3 ahead = Planar(animated.row[2], worldUp);
        if (Length(ahead) > 0.05f)
        {
            const Vec3 flatAhead = Normalized(ahead);
            level.row[0] = Scale(Cross(worldUp, flatAhead), Length(animated.row[0]));
            level.row[1] = Scale(worldUp, Length(animated.row[1]));
            level.row[2] = Scale(flatAhead, Length(animated.row[2]));
        }
        Frame body = level;
        for (int r = 0; r < 3; ++r)
            body.row[r] = Add(Scale(g_HangFrom.row[r], kept), Scale(level.row[r], 1.0f - kept));
        body.origin = Add(animated.origin, Scale(g_HangOffset, kept));
        Turn pose[kRigBoneCount];
        RigPose(g_Posture * kept, g_Upright * kUprightShare * kept, g_NeckLift * kept, g_TurretYaw * kept, g_RailPitch * kept, 0.0f, 0.0f, pose);
        AimCylinders(pose);
        BoneMatrixAccessor accessor;
        g_AccessorCtor(&accessor, skeleton);
        ToMatrix(body, m);
        g_SetBoneWorld(&accessor, kBodyBone, m);
        WriteRig(&accessor, body, pose);
        if (roofMg)
            WriteRoofMg(&accessor, body, pose, g_RoofMgYaw * kept, g_RoofMgPitch * kept);
        battleGearWeapons::DropRoofGun();
        battleGearWeapons::PublishGroundSpeed(0.0f);
        g_Reset.store(true, std::memory_order_release);   // back on the ground (the balloon was shot), the field pose starts over
    }

    std::uint32_t BoneName(const std::uint8_t* skeleton, int bone)
    {
        const auto* names = *reinterpret_cast<const std::uint32_t* const*>(skeleton + kSkeletonNames);
        return names ? names[bone] : 0;
    }

    bool FieldRecordHoldsSEH(const std::uint8_t* record, const std::uint8_t* model)
    {
        __try
        {
            return *reinterpret_cast<std::uint8_t* const*>(record + kRecordModel) == model;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ReadModelSEH(const std::uint8_t* record, std::uint8_t*& model)
    {
        __try
        {
            model = *reinterpret_cast<std::uint8_t* const*>(record + kRecordModel);
            return model != nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void* ResolveChecked(std::uintptr_t address, const std::uint8_t* entry, std::size_t size)
    {
        void* fn = address ? ResolveGameAddress(address) : nullptr;
        return fn && CheckHookEntry(fn, entry, size) != HookEntry::Unknown ? fn : nullptr;
    }

    void MapJoints()   // each rig bone follows the nearest leg joint above it
    {
        for (int k = 0; k < kRigBoneCount; ++k)
        {
            g_JointOf[k] = -1;
            for (int bone = kFirstRigBone + k; bone >= kFirstRigBone && g_JointOf[k] < 0; bone = Parent(bone))
                for (int j = 0; j < kLegJoints; ++j)
                    if (kJointBones[j] == bone)
                        g_JointOf[k] = j;
        }
    }
}

namespace battleGearLegs
{
    bool Install()
    {
        g_BoneWorld = reinterpret_cast<BoneWorld_t>(ResolveChecked(gAddr.FoxModel_BoneWorldMatrix, kBoneWorldEntry, sizeof(kBoneWorldEntry)));
        g_AccessorCtor = reinterpret_cast<AccessorCtor_t>(
            ResolveChecked(gAddr.FoxSkeleton_BoneMatrixAccessor_Ctor, kAccessorCtorEntry, sizeof(kAccessorCtorEntry)));
        g_SetBoneWorld = reinterpret_cast<SetBoneWorld_t>(
            ResolveChecked(gAddr.FoxSkeleton_SetBoneWorldMatrix, kSetBoneWorldEntry, sizeof(kSetBoneWorldEntry)));
        g_ConstructGeoControl = reinterpret_cast<ConstructGeoControl_t>(
            ResolveChecked(gAddr.GeoCheckControlN1_Construct, kConstructGeoControlEntry, sizeof(kConstructGeoControlEntry)));
        g_GeoLineCheck = reinterpret_cast<GeoLineCheck_t>(
            ResolveChecked(gAddr.FoxGeo_CollisionLineCheck, kGeoLineCheckEntry, sizeof(kGeoLineCheckEntry)));
        g_GetHitPosition = reinterpret_cast<GetHitPosition_t>(
            ResolveChecked(gAddr.FoxGeoResultI_GetPosition, kGetHitPositionEntry, sizeof(kGetHitPositionEntry)));
        g_GetHitNormal = reinterpret_cast<GetHitNormal_t>(
            ResolveChecked(gAddr.FoxGeoResultI_GetNormal, kGetHitNormalEntry, sizeof(kGetHitNormalEntry)));
        if (!g_GetHitNormal)
            LogWarn("BattleGear: a collision hit's normal reads differently on %s, the hull meets every wall squarely",
                    GetGameBuildName(gGameBuild));
        if (!g_BoneWorld || !g_AccessorCtor || !g_SetBoneWorld || !g_ConstructGeoControl || !g_GeoLineCheck || !g_GetHitPosition)
        {
            LogWarn("BattleGear: leg addresses missing or changed on %s, the legs stay as the D-Walker carries them", GetGameBuildName(gGameBuild));
            return false;
        }
        g_AkRegister = reinterpret_cast<AkRegister_t>(gAddr.AK_RegisterGameObj ? ResolveGameAddress(gAddr.AK_RegisterGameObj) : nullptr);
        g_AkSetPosition = reinterpret_cast<AkSetPosition_t>(gAddr.AK_SetPosition ? ResolveGameAddress(gAddr.AK_SetPosition) : nullptr);
        g_AkPostEvent = reinterpret_cast<AkPostEvent_t>(gAddr.AK_PostEvent ? ResolveGameAddress(gAddr.AK_PostEvent) : nullptr);
        if (!g_AkRegister || !g_AkSetPosition || !g_AkPostEvent)
            LogWarn("BattleGear: sound addresses missing on this build, the cupola's lid moves without its sounds");
        MapJoints();
        MeasureCrouch();
        g_Ready.store(true, std::memory_order_release);
        return true;
    }

    void Uninstall()
    {
        g_Ready.store(false, std::memory_order_release);
        SetFieldRecord(nullptr);
    }

    void SetFieldRecord(std::uint8_t* record)
    {
        std::uint8_t* model = nullptr;
        if (record && !ReadModelSEH(record, model))
            record = nullptr;
        if (!record)
            model = nullptr;
        if (record == g_Record.load(std::memory_order_acquire) && model == g_Model.load(std::memory_order_acquire))
            return;
        PublishGlide(false, {});
        g_Model.store(nullptr, std::memory_order_release);
        g_Record.store(record, std::memory_order_release);
        g_Reset.store(true, std::memory_order_release);
        g_Model.store(model, std::memory_order_release);
    }

    void OnBonesWritten(std::uint8_t* skeleton)
    {
        if (!skeleton || !g_Ready.load(std::memory_order_acquire))
            return;
        const std::int16_t bones = *reinterpret_cast<const std::int16_t*>(skeleton + kSkeletonBones);
        if ((bones != kRigBones && bones != kRoofMgBones) || BoneName(skeleton, battleGearRig::kFirstBone) != kFlLegName)
            return;
        std::uint8_t* model = skeleton - kModelSkeleton;
        const bool guns = BoneName(skeleton, kTurretBone) == kTurretName;
        const bool roofMg = bones == kRoofMgBones && BoneName(skeleton, kRoofMgBone) == kRoofMgName;
        if (model != g_Model.load(std::memory_order_acquire) || !FieldRecordHoldsSEH(g_Record.load(std::memory_order_acquire), model))
            PoseRest(skeleton, roofMg);
        else if (battleGearWeapons::Hanging())
            PoseHanging(skeleton, roofMg);
        else
            PoseField(skeleton, guns, roofMg);
    }

    bool HoverGround(float* point)
    {
        AcquireSRWLockShared(&g_HoverLock);
        const bool gliding = g_Gliding;
        point[0] = g_GroundUnderHull.x;
        point[1] = g_GroundUnderHull.y;
        point[2] = g_GroundUnderHull.z;
        ReleaseSRWLockShared(&g_HoverLock);
        return gliding;
    }

    bool FirstHit(const float* start, const float* end, std::uint32_t flags, float* hit)
    {
        if (!g_Ready.load(std::memory_order_acquire) || !start || !end || !hit)
            return false;
        Vec3 at{};
        if (!LineHit({ start[0], start[1], start[2] }, { end[0], end[1], end[2] }, flags, at))
            return false;
        hit[0] = at.x;
        hit[1] = at.y;
        hit[2] = at.z;
        return true;
    }

    bool FirstHitFace(const float* start, const float* end, std::uint32_t flags, float* hit, float* face)
    {
        if (!g_Ready.load(std::memory_order_acquire) || !start || !end || !hit || !face)
            return false;
        Vec3 at{};
        Vec3 normal{};
        if (!LineHit({ start[0], start[1], start[2] }, { end[0], end[1], end[2] }, flags, at, &normal))
            return false;
        hit[0] = at.x;
        hit[1] = at.y;
        hit[2] = at.z;
        face[0] = normal.x;
        face[1] = normal.y;
        face[2] = normal.z;
        return true;
    }

    bool SpreadsBothWays(const float* spot, float ceiling)
    {
        if (!g_Ready.load(std::memory_order_acquire) || !spot)
            return true;
        Vec3 top{};
        if (!LineHit({ spot[0], ceiling, spot[2] }, { spot[0], spot[1] - kBroadDrop, spot[2] }, kGroundFlags, top))
            return false;
        const float across[4] = { kBroadProbe, -kBroadProbe, 0.0f, 0.0f };
        const float along[4] = { 0.0f, 0.0f, kBroadProbe, -kBroadProbe };
        bool onTop[4];
        for (int k = 0; k < 4; ++k)
        {
            Vec3 at{};
            onTop[k] = LineHit({ spot[0] + across[k], top.y + kBroadProbe + kBroadDrop, spot[2] + along[k] },   // a slope's higher
                               { spot[0] + across[k], top.y - kBroadDrop, spot[2] + along[k] }, kGroundFlags, at);    // side counts
        }
        return (onTop[0] || onTop[1]) && (onTop[2] || onTop[3]);
    }
}
