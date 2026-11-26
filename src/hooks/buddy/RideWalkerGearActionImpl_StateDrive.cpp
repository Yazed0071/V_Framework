#include <pch.h>

#include "RideWalkerGearActionImpl_StateDrive.h"

#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "AddressSet.h"
#include "BattleGearLegs.h"
#include "BattleGearWeapons.h"
#include "BuddyCodeCave.h"
#include "HookUtils.h"
#include "RideWalkerGearActionImpl_SubjectiveCamera.h"
#include "log.h"
#include "../player/Fova2ControllerImpl_ApplyPlayerFova.h"

namespace
{
    using StateDrive_t         = void(__fastcall*)(void* action, std::uint32_t index, std::uint32_t proc, void* arg);
    using GetDeltaTime_t       = float(__fastcall*)(void* action, std::uint32_t index);
    using GetCameraDirection_t = void(__fastcall*)(void* unused, float* direction, const std::uint8_t* comm);
    using CurrentMotion_t      = std::uint64_t*(__fastcall*)(void* motion, std::uint64_t* state, std::uint32_t index, std::uint32_t layer);
    using MotionEnded_t        = bool(__fastcall*)(void* motion, std::uint32_t index, std::uint32_t layer);
    using SetMotion_t          = void(__fastcall*)(void* motion, std::uint32_t index, std::uint32_t layer, std::uint64_t state, float blend,
                                                   std::uint32_t flags);
    using RootTransform_t      = void(__fastcall*)(void* motion, std::uint32_t index, float* matrix);
    using BoneWorldMatrix_t    = void(__fastcall*)(void* motion, std::uint32_t index, std::uint32_t bone, float* matrix);

    constexpr std::uint32_t kProcUpdate    = 0;
    constexpr std::uint32_t kProcStart     = 3;
    constexpr std::uint32_t kProcEnd       = 4;
    constexpr std::uint32_t kProcMotion    = 7;
    constexpr std::size_t   kActionOwner   = 0x08;    // the action plugin's player object
    constexpr std::size_t   kOwnerCore     = 0x138;   // its action core
    constexpr std::size_t   kCoreRideComms = 0x150;   // one ridden-walker comm per player
    constexpr std::size_t   kCommCamera    = 0x2E0;   // camera heading, the way the walker faces in fire posture
    constexpr std::size_t   kCommButtons   = 0x560;
    constexpr std::uint32_t kDashHeld      = 0x800000;
    constexpr std::size_t   kCommPresses   = 0x3E8;   // this frame's ride input, read by the walker's attack and arm-attack polling
    constexpr std::uint32_t kWeaponReady   = 0x4;
    constexpr std::uint32_t kHoldHeld      = 0x80;    // the gun action only starts and stays up while ready and this is set
    constexpr std::uint32_t kCqcInput      = 0x7;     // CQC pressed, held, released: the D-Walker arm melee
    constexpr std::uint32_t kFirePressed   = 0x1;     // the same bits are the fire button's own state
    constexpr std::uint32_t kFireHeld      = 0x2;
    constexpr std::uint32_t kFireReleased  = 0x4;
    constexpr std::uint32_t kHoldPressed   = 0x10;
    constexpr std::size_t   kCommAimPoint  = 0x370;   // the camera's look-at point; the walker aims every gun at it
    constexpr std::uint32_t kAimRayFlags   = 0x4020700;
    constexpr float         kAimRayFrom    = 5.0f;    // metres along the view: past the Battle Gear's own hull
    constexpr float         kAimRange      = 300.0f;
    constexpr float         kHullTurnRate  = 0.7854f; // 45 degrees a second
    constexpr float         kLongestFrame  = 0.1f;
    constexpr float         kPi            = 3.14159265f;

    // the get-on: the player's body object, its motion and ride root per player, and the ride action's work per player
    constexpr std::size_t   kOwnerBody        = 0x78;
    constexpr std::size_t   kBodyFirstPlayer  = 0x08;
    constexpr std::size_t   kBodyMotion       = 0x238;
    constexpr std::size_t   kBodyRideRoots    = 0x270;
    constexpr std::size_t   kBodyActionFlags  = 0x280;
    constexpr std::size_t   kActionFlagsSize  = 0x20;
    constexpr std::size_t   kBodyModels       = 0x240;   // ModelControllerImpl: one 0x80 entry per player from its first index
    constexpr std::size_t   kModelsEntries    = 0x08;
    constexpr std::size_t   kModelsFirst      = 0x14;
    constexpr std::size_t   kModelsEntrySize  = 0x80;
    constexpr std::size_t   kEquipAlpha       = 0x68;    // this frame's alpha request per equip handle, 0 = gone; reset each frame
    constexpr std::size_t   kEquipHandles     = 6;
    constexpr std::size_t   kRideRootMatrix   = 0x10;
    constexpr std::size_t   kActionWorkIndex  = 0x38;   // -> +0x24 the first player's index
    constexpr std::size_t   kWorkIndexBase    = 0x24;
    constexpr std::size_t   kActionWorks      = 0x50;
    constexpr std::size_t   kWorkSize         = 0xB0;
    constexpr std::size_t   kWorkGetOnDone    = 0x58;   // 0 until the get-on motion has played
    constexpr std::size_t   kWorkNextState    = 0x55;   // StateDrive picks 3 (StateGetOff) to get off, 4 (StateEvade) to dive off at speed
    constexpr std::uint8_t  kStateGetOff      = 3;
    constexpr std::uint8_t  kStateEvade       = 4;
    constexpr std::size_t   kWorkFlags        = 0xA0;
    constexpr std::uint32_t kAlreadySeated    = 0x2;     // the ride starts in the seat, without a get-on
    constexpr std::uint32_t kSeated           = 0x2008;
    constexpr std::uint32_t kOwnRideRoot      = 0x40000; // the ride root updates itself instead of following the walker
    constexpr std::uint32_t kGotOn            = 0x80000; // action flag the get-on's end sets
    constexpr std::size_t   kMotionCurrent    = 0x208 / sizeof(void*);
    constexpr std::size_t   kMotionEnded      = 0x228 / sizeof(void*);
    constexpr std::size_t   kMotionSet        = 0x170 / sizeof(void*);
    constexpr std::size_t   kMotionRoot       = 0xB0 / sizeof(void*);
    constexpr std::size_t   kMotionBoneWorld  = 0x130 / sizeof(void*);
    constexpr std::uint32_t kKnobBones[4]     = { 8, 0xC, 4, 0xE };   // what the armored-vehicle ride holds a hatch's knob with
    constexpr float         kLidFullOpen      = 1.9199f; // the animation job's full-open stop, 110 degrees
    constexpr float         kLidInOpenAt      = 0.35f;   // seconds into the climb in: he reaches the roof and swings the lid open
    constexpr float         kLidInPullAt      = 1.75f;   // he reaches up and pulls it shut after him
    constexpr float         kLidPullTime      = 0.3f;
    constexpr float         kLidOutOpenAt     = 0.1f;    // the climb out pushes it open
    constexpr float         kLidOutShutAt     = 1.1f;    // and lets it fall once he is out on the roof
    constexpr float         kLidAllOpen       = 3.1416f; // the animation job stops it at its full-open angle
    constexpr std::uint64_t kWalkerGetOn      = 0x23B9A991BFB;    // snapwkg_s_tak_on
    constexpr std::uint64_t kArmoredGetOn     = 0xBD6437E2E2B5;   // snapwav_s_rid_on_l; its first event starts the RideOnArmoredVehicleWest camera
    constexpr std::uint64_t kWalkerGetOff     = 0x9C9259D9C72B;   // snapwkg_s_get_off
    constexpr std::uint64_t kArmoredGetOff    = 0x49D422B0DE0B;   // snapwav_s_rid_of_vr2, the armored vehicles' own climb-out
    constexpr float         kWalkerGetOnBlend = 8.0f;
    constexpr float         kClimbStartWithin = 0.5f;    // seconds: else the D-Walker get-on / get-off plays instead
    constexpr float         kClimbLongest     = 6.0f;    // the clip runs 130 frames
    constexpr float         kClimbOutLongest  = 4.0f;    // the clip runs 100 frames; the get-off itself ends with it

    const std::uint8_t kStateDriveEntry[]   = { 0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x48, 0x8D, 0x6C, 0x24, 0xE8,
                                                0x48, 0x81, 0xEC, 0x18, 0x01, 0x00, 0x00 };
    const std::uint8_t kGetDeltaTimeEntry[] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x41, 0x08, 0x8B, 0xDA, 0x48, 0x8B,
                                                0x88, 0x38, 0x01, 0x00, 0x00, 0x48, 0x8B, 0x49, 0x60 };
    const std::uint8_t kGetCameraDirectionEntry[] = { 0x48, 0x8B, 0xC4, 0x55, 0x48, 0x8D, 0x68, 0xE8, 0x48, 0x81, 0xEC, 0x10, 0x01, 0x00,
                                                      0x00, 0x0F, 0x29, 0x70, 0xE8, 0x0F, 0x29, 0x78, 0xD8 };

    StateDrive_t         g_OrigStateDrive = nullptr;
    StateDrive_t         g_OrigStateGetOff = nullptr;
    GetDeltaTime_t       g_GetDeltaTime = nullptr;
    GetCameraDirection_t g_GetCameraDirection = nullptr;
    void*                g_StateDriveTarget = nullptr;
    void*                g_StateGetOffTarget = nullptr;
    float                g_AimOrigin[3] = {};    // the camera's own look-at point
    float                g_AimWritten[3] = {};   // the aim point last written over it
    bool                 g_AimOriginKnown = false;
    bool                 g_FireDown = false;     // the fire button, from its pressed and released bits
#ifdef _DEBUG
    ULONGLONG            g_NextHullLog = 0;
    ULONGLONG            g_NextControlsLog = 0;
    std::uint32_t        g_CqcDropped = 0;
    std::uint32_t        g_AimOriginKept = 0;
    float                g_AimDistance = 0.0f;
    bool                 g_AimHit = false;
#endif

    BuddyCodeCave g_Ready{ "Battle Gear fire posture" };
    BuddyCodeCave g_StanceEdge{ "Battle Gear Stance toggle" };
    BuddyCodeCave g_StanceHeld{ "Battle Gear Stance hold" };
    BuddyCodeCave g_EvadeEdge{ "Battle Gear Evade" };
    BuddyCodeCave g_ClimbMotion{ "Battle Gear hatch climb" };
    BuddyCodeCave g_ClimbOutMotion{ "Battle Gear cupola climb-out" };

    // movabs r9, 0x23B9A991BFB: the motion state the get-on starts
    constexpr std::uint8_t kGetOnMotionSite[] = { 0x49, 0xB9, 0xFB, 0x1B, 0x99, 0x9A, 0x3B, 0x02, 0x00, 0x00 };
    // movabs r9, 0x9C9259D9C72B: the motion state the get-off starts
    constexpr std::uint8_t kGetOffMotionSite[] = { 0x49, 0xB9, 0x2B, 0xC7, 0xD9, 0x59, 0x92, 0x9C, 0x00, 0x00 };
    const std::uint8_t kStateGetOffEntry[] = { 0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x6C, 0x24, 0x20, 0x56, 0x57, 0x41, 0x57, 0x48,
                                               0x83, 0xEC, 0x30 };

    std::uint8_t  g_ClimbRequest = 0;      // the get-on cave reads it as a byte
    std::uint8_t  g_ClimbOutRequest = 0;   // the get-off cave reads it as a byte
    std::uint32_t g_ClimbPlayer = 0;
    float         g_ClimbTime = 0.0f;
    bool          g_ClimbStarted = false;
    bool          g_ClimbingOut = false;
    float         g_LidHeld = -1.0f;       // radians the climb holds the cupola's lid open; negative lets it fall
#ifdef _DEBUG
    ULONGLONG     g_NextClimbLog = 0;
    ULONGLONG     g_NextLidLog = 0;
#endif

    enum class SiteNote
    {
        None,
        CountPress,   // inc byte [slot]: one more crouch-button press
        MarkHeld,     // mov byte [slot], 1: the crouch button is down this frame
    };

    constexpr std::size_t kPatchSize = 10;
    constexpr std::size_t kSetReady  = 0x1A;   // or dword [rbx+0x560], 4
    constexpr std::size_t kNotReady  = 0x0A;   // the HOLD check that follows the aim-camera check
    constexpr std::size_t kSkipOr    = 0x0A;   // jmp past the clearing and
    constexpr std::size_t kClearBit  = 0x0C;   // and dword [rbx+0x560], ~bit

    // cmp dword [r15+0x340], 1 / je set / cmp byte [rsp+0x40], 0 / jne set / and [rbx+0x560], ~4 / jmp / or [rbx+0x560], 4
    constexpr std::uint8_t kReadySite[] = { 0x41, 0x83, 0xBF, 0x40, 0x03, 0x00, 0x00, 0x01, 0x74, 0x10, 0x80, 0x7C, 0x24, 0x40, 0x00,
                                            0x75, 0x09, 0x83, 0xA3, 0x60, 0x05, 0x00, 0x00, 0xFB, 0xEB, 0x07, 0x83, 0x8B, 0x60, 0x05,
                                            0x00, 0x00, 0x04 };
    // or dword [rbx+0x560], bit / jmp +0xA / and dword [rbx+0x560], ~bit
    constexpr std::uint8_t kStanceEdgeSite[] = { 0x81, 0x8B, 0x60, 0x05, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0xEB, 0x0A, 0x81, 0xA3,
                                                 0x60, 0x05, 0x00, 0x00, 0xFF, 0xFF, 0xDF, 0xFF };
    constexpr std::uint8_t kStanceHeldSite[] = { 0x81, 0x8B, 0x60, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0xEB, 0x0A, 0x81, 0xA3,
                                                 0x60, 0x05, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xBF };
    constexpr std::uint8_t kEvadeEdgeSite[] = { 0x81, 0x8B, 0x60, 0x05, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0xEB, 0x0A, 0x81, 0xA3,
                                                0x60, 0x05, 0x00, 0x00, 0xFF, 0xFF, 0xBF, 0xFF };

    bool SiteMatchesSEH(const std::uint8_t* site, const std::uint8_t* expected, std::size_t size)
    {
        __try
        {
            return std::memcmp(site, expected, size) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool SiteMatches(std::uintptr_t siteAbs, const std::uint8_t* expected, std::size_t size, const char* what)
    {
        const auto* site = static_cast<const std::uint8_t*>(siteAbs ? ResolveGameAddress(siteAbs) : nullptr);
        if (site && SiteMatchesSEH(site, expected, size))
            return true;
        LogError("BattleGear: %s site differs on this build (another mod patched it?), Stance on the Battle Gear stays the D-Walker roll", what);
        return false;
    }

    void EmitIsBattleGear(CaveCode& c, std::size_t& notIt)   // rbx = the ridden walker's comm block; rax saved
    {
        c.Emit({ 0x50 });                                                    // push rax
        c.Emit({ 0x48, 0xB8 });                                              // mov rax, &comm slot
        c.Imm64(reinterpret_cast<std::uint64_t>(battleGearWeapons::BattleGearCommSlot()));
        c.Emit({ 0x48, 0x3B, 0x18 });                                        // cmp rbx, [rax]
        notIt = c.Jcc8(0x75);                                                // jne
    }

    bool InstallReady()
    {
        const std::uintptr_t siteAbs = gAddr.RideWalkerGearActionImpl_StateDrive_ReadyFlag;
        if (!SiteMatches(siteAbs, kReadySite, sizeof(kReadySite), g_Ready.name))
            return false;
        const auto base = reinterpret_cast<std::uintptr_t>(ResolveGameAddress(siteAbs));
        CaveCode c;
        std::size_t notIt = 0;
        EmitIsBattleGear(c, notIt);
        c.Emit({ 0x48, 0xB8 });                                              // mov rax, &aiming
        c.Imm64(reinterpret_cast<std::uint64_t>(battleGearWeapons::AimingSlot()));
        c.Emit({ 0xC6, 0x00, 0x00 });                                        // mov byte [rax], 0
        c.Emit({ 0x48, 0xB8 });                                              // mov rax, &first person
        c.Imm64(reinterpret_cast<std::uint64_t>(battleGearWeapons::FirstPersonSlot()));
        c.Emit({ 0x80, 0x38, 0x00 });                                        // cmp byte [rax], 0
        c.Emit({ 0x48, 0xB8 });                                              // mov rax, &aiming (flags kept)
        c.Imm64(reinterpret_cast<std::uint64_t>(battleGearWeapons::AimingSlot()));
        const std::size_t firstPerson = c.Jcc8(0x75);                        // jne: the first-person camera reads as the aim camera
        c.Emit({ 0x41, 0x83, 0xBF, 0x40, 0x03, 0x00, 0x00, 0x01 });          // cmp dword [r15+0x340], 1 (aim camera)
        const std::size_t aimCamera = c.Jcc8(0x74);                          // je
        c.Bind8(firstPerson);
        c.Emit({ 0x80, 0x7C, 0x24, 0x48, 0x00 });                            // cmp byte [rsp+0x48], 0 (HOLD, past our push)
        const std::size_t notHeld = c.Jcc8(0x74);                            // je
        c.Bind8(aimCamera);
        c.Emit({ 0xC6, 0x00, 0x01 });                                        // mov byte [rax], 1
        c.Bind8(notHeld);
        c.Emit({ 0x48, 0xB8 });                                              // mov rax, &fire mode
        c.Imm64(reinterpret_cast<std::uint64_t>(battleGearWeapons::FireModeSlot()));
        c.Emit({ 0x80, 0x38, 0x00 });                                        // cmp byte [rax], 0
        const std::size_t runPosture = c.Jcc8(0x74);                         // je
        c.Emit({ 0x58 });                                                    // pop rax
        c.JmpAbs(base + kSetReady);
        c.Bind8(notIt);
        c.Bind8(runPosture);
        c.Emit({ 0x58 });                                                    // pop rax
        c.Emit({ 0x41, 0x83, 0xBF, 0x40, 0x03, 0x00, 0x00, 0x01 });          // cmp dword [r15+0x340], 1
        const std::size_t notAiming = c.Jcc8(0x75);                          // jne
        c.JmpAbs(base + kSetReady);
        c.Bind8(notAiming);
        c.JmpAbs(base + kNotReady);
        return InstallBuddyCodeCave(g_Ready, siteAbs, kReadySite, kPatchSize, c);
    }

    bool InstallBitSite(BuddyCodeCave& cave, std::uintptr_t siteAbs, const std::uint8_t* site, SiteNote note)
    {
        if (!SiteMatches(siteAbs, site, sizeof(kStanceEdgeSite), cave.name))
            return false;
        const auto base = reinterpret_cast<std::uintptr_t>(ResolveGameAddress(siteAbs));
        CaveCode c;
        std::size_t notIt = 0;
        EmitIsBattleGear(c, notIt);
        if (note == SiteNote::CountPress)
        {
            c.Emit({ 0x48, 0xB8 });                                          // mov rax, &stance presses
            c.Imm64(reinterpret_cast<std::uint64_t>(battleGearWeapons::StancePressSlot()));
            c.Emit({ 0xFE, 0x00 });                                          // inc byte [rax]
        }
        else if (note == SiteNote::MarkHeld)
        {
            c.Emit({ 0x48, 0xB8 });                                          // mov rax, &stance held
            c.Imm64(reinterpret_cast<std::uint64_t>(battleGearWeapons::StanceHeldSlot()));
            c.Emit({ 0xC6, 0x00, 0x01 });                                    // mov byte [rax], 1
        }
        c.Emit({ 0x58 });                                                    // pop rax
        c.JmpAbs(base + kClearBit);
        c.Bind8(notIt);
        c.Emit({ 0x58 });                                                    // pop rax
        c.bytes.insert(c.bytes.end(), site, site + kPatchSize);              // the original or
        c.JmpAbs(base + kSkipOr);
        return InstallBuddyCodeCave(cave, siteAbs, site, kPatchSize, c);
    }

    bool ReadCameraHeadingSEH(void* action, std::uint32_t index, std::uint8_t*& comm, float& camera)
    {
        __try
        {
            const auto* owner = *reinterpret_cast<const std::uint8_t* const*>(static_cast<const std::uint8_t*>(action) + kActionOwner);
            const auto* core = *reinterpret_cast<const std::uint8_t* const*>(owner + kOwnerCore);
            comm = (*reinterpret_cast<std::uint8_t* const* const*>(core + kCoreRideComms))[index];
            if (!comm)
                return false;
            camera = *reinterpret_cast<const float*>(comm + kCommCamera);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // tank controls: the trigger is the Battle Gear's own, the weapon wheel picks its gun. Main gun: the walker's gun stays up in
    // every stance, so FIRE shoots without HOLD. Roof gun (the wheel's sidearm): the walker's own sidearm stays down, so only the
    // roof gun answers the trigger. CQC never reaches the arm melee.
    bool WriteTankControlsSEH(std::uint8_t* comm, bool roofGun, std::uint32_t& input, bool& dash)
    {
        __try
        {
            auto* buttons = reinterpret_cast<std::uint32_t*>(comm + kCommButtons);
            auto* presses = reinterpret_cast<std::uint32_t*>(comm + kCommPresses);
            input = *presses;
            dash = (*buttons & kDashHeld) != 0;
            if (roofGun)
            {
                *buttons &= ~kWeaponReady;
                *presses = input & ~(kCqcInput | kHoldHeld | kHoldPressed);
            }
            else
            {
                *buttons |= kWeaponReady;
                *presses = (input | kHoldHeld) & ~kCqcInput;
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ReadAimOriginSEH(const std::uint8_t* comm, float* origin)
    {
        __try
        {
            std::memcpy(origin, comm + kCommAimPoint, 3 * sizeof(float));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool CameraDirectionSEH(const std::uint8_t* comm, float* direction)
    {
        __try
        {
            g_GetCameraDirection(nullptr, direction, comm);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool WriteAimPointSEH(std::uint8_t* comm, const float* point)
    {
        __try
        {
            std::memcpy(comm + kCommAimPoint, point, 3 * sizeof(float));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // tank aim: every gun aims where the camera looks; without HOLD the walker's aim point is only the camera's look-at point,
    // which sits on the Battle Gear itself, so cast along the view (the look-at point lies on it) for what the screen centre shows
    void AimAlongCamera(std::uint8_t* comm)
    {
        alignas(16) float direction[4] = {};
        float origin[3];
        if (!g_GetCameraDirection || !ReadAimOriginSEH(comm, origin) || !CameraDirectionSEH(comm, direction))
            return;
        if (BattleGearFirstPersonEye(origin))   // first person looks from the eye behind the sights, not from the look-at point
            g_AimOriginKnown = false;
        else if (g_AimOriginKnown && std::memcmp(origin, g_AimWritten, sizeof(origin)) == 0)   // the game left our aim point there this frame:
        {                                                                                // casting from it walks the aim 5 m on each frame
            std::memcpy(origin, g_AimOrigin, sizeof(origin));
#ifdef _DEBUG
            ++g_AimOriginKept;
#endif
        }
        else
        {
            std::memcpy(g_AimOrigin, origin, sizeof(origin));
            g_AimOriginKnown = true;
        }
        const float length = std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
        if (length < 0.5f)
            return;
        float from[3];
        float to[3];
        for (int i = 0; i < 3; ++i)
        {
            direction[i] /= length;
            from[i] = origin[i] + direction[i] * kAimRayFrom;
            to[i] = origin[i] + direction[i] * kAimRange;
        }
        float aim[3];
        const bool hit = battleGearLegs::FirstHit(from, to, kAimRayFlags, aim);
        if (!hit)
            std::memcpy(aim, to, sizeof(aim));
        if (WriteAimPointSEH(comm, aim))
            std::memcpy(g_AimWritten, aim, sizeof(aim));
        battleGearWeapons::PublishRiderAim(aim);
#ifdef _DEBUG
        g_AimHit = hit;
        g_AimDistance = std::sqrt((aim[0] - origin[0]) * (aim[0] - origin[0]) + (aim[1] - origin[1]) * (aim[1] - origin[1])
                                  + (aim[2] - origin[2]) * (aim[2] - origin[2]));
#endif
    }

    float WrapAngle(float a)
    {
        return std::remainder(a, 2.0f * kPi);
    }

    float ApproachAngle(float now, float goal, float maxStep)
    {
        return WrapAngle(now + std::clamp(WrapAngle(goal - now), -maxStep, maxStep));
    }

    // what the armored vehicles' ride (RideVehicleActionImpl::UpdateModelVisible) asks each frame: the rider's guns fade out
    void HideRiderEquipmentSEH(void* action, std::uint32_t index)
    {
        __try
        {
            const auto* owner = *reinterpret_cast<const std::uint8_t* const*>(static_cast<const std::uint8_t*>(action) + kActionOwner);
            const auto* body = *reinterpret_cast<const std::uint8_t* const*>(owner + kOwnerBody);
            const auto* models = *reinterpret_cast<const std::uint8_t* const*>(body + kBodyModels);
            const std::uint32_t slot = index - *reinterpret_cast<const std::uint32_t*>(models + kModelsFirst);
            auto* entry = *reinterpret_cast<std::uint8_t* const*>(models + kModelsEntries) + static_cast<std::size_t>(slot) * kModelsEntrySize;
            std::memset(entry + kEquipAlpha, 0, kEquipHandles);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    // the crouch button and Dash set the stance; guns up in every stance and aimed along the camera; fire posture: the hull swings
    // toward the camera (normal and upright: the walk code publishes the heading it steers to, WalkerGearBasicActionImpl_CorrectVelocity)
    void DriveBattleGear(void* action, std::uint32_t index)
    {
        std::uint8_t* comm = nullptr;
        float camera = 0.0f;
        if (!battleGearWeapons::Ridden() || !ReadCameraHeadingSEH(action, index, comm, camera) || !battleGearWeapons::IsBattleGearComm(comm))
        {
            battleGearWeapons::StopHullSteering();
            g_FireDown = false;
            return;
        }
        std::uint32_t input = 0;
        bool dash = false;
        const bool controls = WriteTankControlsSEH(comm, battleGearWeapons::MgSelected(), input, dash);
        const std::uint32_t cqc = input & kCqcInput;
        const float dt = std::clamp(g_GetDeltaTime(action, index), 0.0f, kLongestFrame);
        if (controls)
        {
            battleGearWeapons::OnRideInput(dash, dt);
            if (input & (kFirePressed | kFireHeld))
                g_FireDown = true;
            if (input & kFireReleased)
                g_FireDown = false;
            battleGearWeapons::PublishMgTrigger(g_FireDown && battleGearWeapons::MgSelected());
        }
        AimAlongCamera(comm);
        if (!battleGearWeapons::Climbing())   // inside the turret he is out of sight, as in an armored vehicle
        {
            if (!PlayerMesh::AllHidden())
                PlayerMesh::SetAllHidden(true);
            HideRiderEquipmentSEH(action, index);
        }
        float heading = 0.0f;
        if (battleGearWeapons::FireMode())
        {
            if (battleGearWeapons::HullSteering(&heading))
                heading = ApproachAngle(heading, camera, kHullTurnRate * dt);
            else if (!battleGearWeapons::BodyHeading(&heading))
                heading = camera;
            battleGearWeapons::SteerHull(comm, heading);
        }
#ifdef _DEBUG
        g_CqcDropped += cqc != 0 ? 1 : 0;
        const ULONGLONG now = GetTickCount64();
        if (battleGearWeapons::FireMode() && now >= g_NextHullLog)
        {
            g_NextHullLog = now + 1000;
            LogDebug("BattleGear hull: %.0f deg, camera %.0f deg", heading * 57.29578f, camera * 57.29578f);
        }
        if (now >= g_NextControlsLog)
        {
            g_NextControlsLog = now + 1000;
            LogDebug("BattleGear controls: %s%s, %u CQC inputs dropped since the last line, %s stance, hold %s, aim %.0f m along "
                     "the view (%s), look-at kept on %u frames", battleGearWeapons::MgSelected() ? "roof gun" : "main gun up",
                     controls ? "" : " FAILED (comm unreadable)", g_CqcDropped,
                     battleGearWeapons::FireMode() ? "fire" : (battleGearWeapons::Upright() ? "upright" : "normal"),
                     battleGearWeapons::Aiming() ? "zooming" : "off", g_AimDistance, g_AimHit ? "hit" : "nothing hit, far point",
                     g_AimOriginKept);   // TEMP probe: frames the game left our aim point in the look-at field
            g_CqcDropped = 0;
            g_AimOriginKept = 0;
        }
#else
        (void)controls;
        (void)cqc;
#endif
    }

    struct RideParts
    {
        std::uint8_t*  work;
        void*          motion;
        std::uint8_t*  rideRoot;
        std::uint32_t* actionFlags;
        std::uint8_t*  comm;
    };

    bool ReadRidePartsSEH(void* action, std::uint32_t index, RideParts& parts)
    {
        __try
        {
            const auto* act = static_cast<const std::uint8_t*>(action);
            const auto* owner = *reinterpret_cast<const std::uint8_t* const*>(act + kActionOwner);
            const auto* body = *reinterpret_cast<const std::uint8_t* const*>(owner + kOwnerBody);
            const auto* core = *reinterpret_cast<const std::uint8_t* const*>(owner + kOwnerCore);
            const auto* works = *reinterpret_cast<const std::uint8_t* const*>(act + kActionWorkIndex);
            const std::uint32_t slot = index - *reinterpret_cast<const std::uint32_t*>(body + kBodyFirstPlayer);
            const std::uint32_t workSlot = index - *reinterpret_cast<const std::uint32_t*>(works + kWorkIndexBase);
            parts.work = *reinterpret_cast<std::uint8_t* const*>(act + kActionWorks) + static_cast<std::size_t>(workSlot) * kWorkSize;
            parts.motion = *reinterpret_cast<void* const*>(body + kBodyMotion);
            parts.rideRoot = (*reinterpret_cast<std::uint8_t* const* const*>(body + kBodyRideRoots))[slot];
            parts.actionFlags = reinterpret_cast<std::uint32_t*>(*reinterpret_cast<std::uint8_t* const*>(body + kBodyActionFlags)
                                                                 + static_cast<std::size_t>(slot) * kActionFlagsSize);
            parts.comm = (*reinterpret_cast<std::uint8_t* const* const*>(core + kCoreRideComms))[index];
            return parts.work && parts.motion && parts.rideRoot && parts.actionFlags;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ReadWorkSEH(const std::uint8_t* work, std::uint32_t& getOnDone, std::uint32_t& flags)
    {
        __try
        {
            getOnDone = *reinterpret_cast<const std::uint32_t*>(work + kWorkGetOnDone);
            flags = *reinterpret_cast<const std::uint32_t*>(work + kWorkFlags);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void WriteRideRootSEH(std::uint8_t* rideRoot, const float* matrix)
    {
        __try
        {
            std::memcpy(rideRoot + kRideRootMatrix, matrix, 16 * sizeof(float));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void CompleteGetOnSEH(const RideParts& parts)   // what the get-on's end writes once the D-Walker's own clip has played
    {
        __try
        {
            *parts.actionFlags |= kGotOn;
            *reinterpret_cast<std::uint32_t*>(parts.work + kWorkGetOnDone) += 1;
            *reinterpret_cast<std::uint32_t*>(parts.work + kWorkFlags) |= kSeated;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    bool WantsClimb(void* action, std::uint32_t index)
    {
        RideParts parts{};
        std::uint32_t getOnDone = 0;
        std::uint32_t flags = 0;
        return ReadRidePartsSEH(action, index, parts) && battleGearWeapons::IsBattleGearComm(parts.comm)
            && ReadWorkSEH(parts.work, getOnDone, flags) && !(flags & kAlreadySeated);
    }

    void LetGoLid()
    {
        g_LidHeld = -1.0f;
        battleGearWeapons::HoldLid(-1.0f);
    }

    bool BoneWorldSEH(void** motion, void* motionObject, std::uint32_t index, std::uint32_t bone, float* matrix)
    {
        constexpr float kUnwritten = 1e30f;
        matrix[12] = kUnwritten;
        __try
        {
            reinterpret_cast<BoneWorldMatrix_t>(motion[kMotionBoneWorld])(motionObject, index, bone, matrix);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        return matrix[12] != kUnwritten;
    }

    // the clips' hands never reach the lid on the way through: it swings open as he gets onto the roof (in) or pushes up from inside
    // (out); climbing in, he pulls it shut after him; climbing out, it falls shut once he is out on the roof
    void UpdateLidHand(void** motion, void* motionObject, std::uint32_t index)
    {
        float hold = -1.0f;
        if (g_ClimbingOut)
            hold = g_ClimbTime >= kLidOutOpenAt && g_ClimbTime < kLidOutShutAt ? kLidAllOpen : -1.0f;
        else if (g_ClimbTime >= kLidInPullAt)
        {
            const float share = std::clamp((g_ClimbTime - kLidInPullAt) / kLidPullTime, 0.0f, 1.0f);
            hold = kLidFullOpen * (1.0f - share * share * (3.0f - 2.0f * share));
        }
        else if (g_ClimbTime >= kLidInOpenAt)
            hold = kLidAllOpen;
        g_LidHeld = hold;
        battleGearWeapons::HoldLid(hold);
#ifdef _DEBUG
        alignas(16) float lid[16];
        const ULONGLONG now = GetTickCount64();
        if (now < g_NextLidLog || !battleGearWeapons::Lid(lid))
            return;
        g_NextLidLog = now + 250;
        char bones[4][48] = {};
        for (int h = 0; h < 4; ++h)
        {
            alignas(16) float m[16];
            if (!BoneWorldSEH(motion, motionObject, index, kKnobBones[h], m))
            {
                std::snprintf(bones[h], sizeof(bones[h]), "unread");
                continue;
            }
            const float d[3] = { m[12] - lid[12], m[13] - lid[13], m[14] - lid[14] };
            float local[3];
            for (int r = 0; r < 3; ++r)
                local[r] = d[0] * lid[4 * r] + d[1] * lid[4 * r + 1] + d[2] * lid[4 * r + 2];   // across, up, ahead of the hinge
            std::snprintf(bones[h], sizeof(bones[h]), "%.2f %.2f %.2f", local[0], local[1], local[2]);
        }
        LogDebug("BattleGear lid: %.2f s, bones 8 [%s] 12 [%s] 4 [%s] 14 [%s] (across up ahead of the hinge) | %s", g_ClimbTime, bones[0],
                 bones[1], bones[2], bones[3], hold < 0.0f ? "let go" : (hold >= kLidAllOpen ? "held open" : "pulled"));
#else
        (void)motion;
        (void)motionObject;
        (void)index;
#endif
    }

    void StopClimb()
    {
        LetGoLid();
        battleGearWeapons::SetClimbing(false);
        g_ClimbStarted = false;
        g_ClimbingOut = false;
        g_ClimbTime = 0.0f;
    }

#ifdef _DEBUG
    const char* ClimbStateName(std::uint64_t state)
    {
        switch (state)
        {
        case kArmoredGetOn:  return "hatch climb";
        case kWalkerGetOn:   return "D-Walker get-on";
        case kArmoredGetOff: return "cupola climb-out";
        case kWalkerGetOff:  return "D-Walker get-off";
        default:             return "another motion";
        }
    }

    void LogClimb(void** motion, void* motionObject, std::uint32_t index, std::uint64_t state)
    {
        const ULONGLONG now = GetTickCount64();
        alignas(16) float hatch[16];
        alignas(16) float root[16] = {};
        if (now < g_NextClimbLog || !battleGearWeapons::Hatch(hatch))
            return;
        g_NextClimbLog = now + 500;
        reinterpret_cast<RootTransform_t>(motion[kMotionRoot])(motionObject, index, root);
        float local[3];
        for (int r = 0; r < 3; ++r)
            local[r] = (root[12] - hatch[12]) * hatch[4 * r] + (root[13] - hatch[13]) * hatch[4 * r + 1] + (root[14] - hatch[14]) * hatch[4 * r + 2];
        LogDebug("BattleGear climb: %.2f s, %s, the rider's root %.2f left %.2f up %.2f ahead of the cupola", g_ClimbTime,
                 ClimbStateName(state), local[0], local[1], local[2]);
    }
#endif

    // the hatch climb: the ride root is the turret's cupola, and the get-on ends with the clip, the way the D-Walker's ends with its
    // own
    void UpdateClimb(void* action, std::uint32_t index)
    {
        if (!battleGearWeapons::Climbing() || g_ClimbingOut || index != g_ClimbPlayer)
            return;
        RideParts parts{};
        std::uint32_t getOnDone = 0;
        std::uint32_t flags = 0;
        if (!ReadRidePartsSEH(action, index, parts) || !ReadWorkSEH(parts.work, getOnDone, flags) || getOnDone != 0)
        {
            StopClimb();
            return;
        }
        g_ClimbTime += std::clamp(g_GetDeltaTime(action, index), 0.0f, kLongestFrame);
        auto** motion = *static_cast<void***>(parts.motion);
        std::uint64_t state = 0;
        reinterpret_cast<CurrentMotion_t>(motion[kMotionCurrent])(parts.motion, &state, index, 0);
        g_ClimbStarted = g_ClimbStarted || state == kArmoredGetOn;
        if (!g_ClimbStarted && g_ClimbTime > kClimbStartWithin)
        {
            LogWarn("BattleGear: the hatch climb did not start, the D-Walker get-on plays instead");
            reinterpret_cast<SetMotion_t>(motion[kMotionSet])(parts.motion, index, 0, kWalkerGetOn, kWalkerGetOnBlend, 0);
            StopClimb();
            return;
        }
        alignas(16) float hatch[16];
        if (!(flags & kOwnRideRoot) && battleGearWeapons::Hatch(hatch))
            WriteRideRootSEH(parts.rideRoot, hatch);   // the clip already lands its hatch drop on the ride root
        UpdateLidHand(motion, parts.motion, index);
#ifdef _DEBUG
        LogClimb(motion, parts.motion, index, state);
#endif
        const bool ended = g_ClimbStarted && state == kArmoredGetOn
                        && reinterpret_cast<MotionEnded_t>(motion[kMotionEnded])(parts.motion, index, 0);
        if (ended || g_ClimbTime > kClimbLongest)
        {
#ifdef _DEBUG
            LogDebug("BattleGear climb: in after %.2f s (%s)", g_ClimbTime, ended ? "the clip ended" : "time limit");
#endif
            CompleteGetOnSEH(parts);
            StopClimb();
        }
    }

    bool RidesBattleGear(void* action, std::uint32_t index)
    {
        RideParts parts{};
        return ReadRidePartsSEH(action, index, parts) && battleGearWeapons::IsBattleGearComm(parts.comm);
    }

    void GetOffInsteadOfEvadeSEH(std::uint8_t* work)   // StateEvade has its own D-Walker dive; StateGetOff starts the climb-out
    {
        __try
        {
            if (work[kWorkNextState] == kStateEvade)
                work[kWorkNextState] = kStateGetOff;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void StartClimbOut(std::uint32_t index)   // he shows again for the climb out, and the Battle Gear kneels for it
    {
        g_ClimbPlayer = index;
        g_ClimbTime = 0.0f;
        g_ClimbStarted = false;
        g_ClimbingOut = true;
        battleGearWeapons::SetClimbing(true);
        if (PlayerMesh::AllHidden())
            PlayerMesh::SetAllHidden(false);
    }

    // the climb out of the cupola: the ride root stays the cupola while the clip plays; the get-off ends with the clip on its own
    void UpdateClimbOut(void* action, std::uint32_t index)
    {
        if (!g_ClimbingOut || index != g_ClimbPlayer)
            return;
        RideParts parts{};
        std::uint32_t getOnDone = 0;
        std::uint32_t flags = 0;
        if (!ReadRidePartsSEH(action, index, parts) || !ReadWorkSEH(parts.work, getOnDone, flags))
        {
            StopClimb();
            return;
        }
        g_ClimbTime += std::clamp(g_GetDeltaTime(action, index), 0.0f, kLongestFrame);
        auto** motion = *static_cast<void***>(parts.motion);
        std::uint64_t state = 0;
        reinterpret_cast<CurrentMotion_t>(motion[kMotionCurrent])(parts.motion, &state, index, 0);
        g_ClimbStarted = g_ClimbStarted || state == kArmoredGetOff;
        if (!g_ClimbStarted && g_ClimbTime > kClimbStartWithin)
        {
            LogWarn("BattleGear: the cupola climb-out did not start, the D-Walker get-off plays instead");
            reinterpret_cast<SetMotion_t>(motion[kMotionSet])(parts.motion, index, 0, kWalkerGetOff, kWalkerGetOnBlend, 0);
            StopClimb();
            return;
        }
        alignas(16) float hatch[16];
        if (!(flags & kOwnRideRoot) && battleGearWeapons::Hatch(hatch))
            WriteRideRootSEH(parts.rideRoot, hatch);
        UpdateLidHand(motion, parts.motion, index);
#ifdef _DEBUG
        LogClimb(motion, parts.motion, index, state);
#endif
        if (g_ClimbTime > kClimbOutLongest)
            StopClimb();
    }

    bool InstallClimbMotion()   // the Battle Gear's get-on starts the armored-vehicle climb instead of the D-Walker's
    {
        const std::uintptr_t siteAbs = gAddr.RideWalkerGearActionImpl_StateDrive_GetOnMotion;
        const auto base = reinterpret_cast<std::uintptr_t>(siteAbs ? ResolveGameAddress(siteAbs) : nullptr);
        if (!base)
        {
            LogError("BattleGear: get-on motion address missing on this build, getting in stays the D-Walker get-on");
            return false;
        }
        CaveCode c;
        c.Emit({ 0x49, 0xB9 });                                              // mov r9, &climb request
        c.Imm64(reinterpret_cast<std::uint64_t>(&g_ClimbRequest));
        c.Emit({ 0x41, 0x80, 0x39, 0x00 });                                  // cmp byte [r9], 0
        c.Emit({ 0x49, 0xB9 });                                              // mov r9, the D-Walker get-on
        c.Imm64(kWalkerGetOn);
        const std::size_t walker = c.Jcc8(0x74);                             // je
        c.Emit({ 0x49, 0xB9 });                                              // mov r9, the armored-vehicle get-on
        c.Imm64(kArmoredGetOn);
        c.Bind8(walker);
        c.JmpAbs(base + sizeof(kGetOnMotionSite));
        return InstallBuddyCodeCave(g_ClimbMotion, siteAbs, kGetOnMotionSite, sizeof(kGetOnMotionSite), c);
    }

    bool InstallClimbOutMotion()   // the Battle Gear's get-off starts the armored-vehicle climb-out instead of the D-Walker's
    {
        const std::uintptr_t siteAbs = gAddr.RideWalkerGearActionImpl_StateGetOff_GetOffMotion;
        const auto base = reinterpret_cast<std::uintptr_t>(siteAbs ? ResolveGameAddress(siteAbs) : nullptr);
        if (!base)
        {
            LogError("BattleGear: get-off motion address missing on this build, getting off stays the D-Walker get-off");
            return false;
        }
        CaveCode c;
        c.Emit({ 0x49, 0xB9 });                                              // mov r9, &climb-out request
        c.Imm64(reinterpret_cast<std::uint64_t>(&g_ClimbOutRequest));
        c.Emit({ 0x41, 0x80, 0x39, 0x00 });                                  // cmp byte [r9], 0
        c.Emit({ 0x49, 0xB9 });                                              // mov r9, the D-Walker get-off
        c.Imm64(kWalkerGetOff);
        const std::size_t walker = c.Jcc8(0x74);                             // je
        c.Emit({ 0x49, 0xB9 });                                              // mov r9, the armored-vehicle climb-out
        c.Imm64(kArmoredGetOff);
        c.Bind8(walker);
        c.JmpAbs(base + sizeof(kGetOffMotionSite));
        return InstallBuddyCodeCave(g_ClimbOutMotion, siteAbs, kGetOffMotionSite, sizeof(kGetOffMotionSite), c);
    }

    void __fastcall hkStateDrive(void* action, std::uint32_t index, std::uint32_t proc, void* arg)
    {
        g_ClimbRequest = proc == kProcStart && g_ClimbMotion.installed && WantsClimb(action, index) ? 1 : 0;
        g_OrigStateDrive(action, index, proc, arg);
        if (g_ClimbRequest)
        {
            g_ClimbRequest = 0;
            g_ClimbPlayer = index;
            g_ClimbTime = 0.0f;
            g_ClimbStarted = false;
            battleGearWeapons::SetClimbing(true);
        }
#ifdef _DEBUG
        if (proc == kProcStart && battleGearWeapons::OnField())
            LogDebug("BattleGear get-on: %s", battleGearWeapons::Climbing() ? "hatch climb"
                                                                          : "D-Walker get-on (another walker's comm, or already seated)");
#endif
        if (proc == kProcUpdate)
        {
            RideParts parts{};
            if (g_StateGetOffTarget && g_ClimbOutMotion.installed && ReadRidePartsSEH(action, index, parts)
                && battleGearWeapons::IsBattleGearComm(parts.comm))
                GetOffInsteadOfEvadeSEH(parts.work);   // getting off at speed climbs out of the cupola as well
            DriveBattleGear(action, index);
        }
        else if (proc == kProcMotion)
            UpdateClimb(action, index);
        else if (proc == kProcEnd)
        {
            if (index == g_ClimbPlayer)
                StopClimb();
            if (PlayerMesh::AllHidden())
                PlayerMesh::SetAllHidden(false);
        }
    }

    void __fastcall hkStateGetOff(void* action, std::uint32_t index, std::uint32_t proc, void* arg)
    {
        g_ClimbOutRequest = proc == kProcStart && g_ClimbOutMotion.installed && RidesBattleGear(action, index) ? 1 : 0;
        g_OrigStateGetOff(action, index, proc, arg);
        if (g_ClimbOutRequest)
        {
            g_ClimbOutRequest = 0;
            StartClimbOut(index);
        }
        if (proc == kProcMotion)
            UpdateClimbOut(action, index);
        else if (proc == kProcEnd && g_ClimbingOut && index == g_ClimbPlayer)
            StopClimb();
    }

    void InstallClimbOut()   // hook the get-off state, then swap its clip; without either, getting off stays the D-Walker's
    {
        void* target = gAddr.RideWalkerGearActionImpl_StateGetOff ? ResolveGameAddress(gAddr.RideWalkerGearActionImpl_StateGetOff) : nullptr;
        if (!target)
        {
            LogError("BattleGear: walker get-off address missing on this build, getting off stays the D-Walker get-off");
            return;
        }
        const HookEntry state = CheckHookEntry(target, kStateGetOffEntry, sizeof(kStateGetOffEntry));
        if (state == HookEntry::Unknown)
        {
            char desc[160] = {};
            DescribeFunctionEntry(target, desc, sizeof(desc));
            LogError("BattleGear: walker get-off code differs on this build (%s), getting off stays the D-Walker get-off", desc);
            return;
        }
        if (state == HookEntry::ForeignHook)
            LogHookedOnTop("BattleGear: RideWalkerGearActionImpl StateGetOff", target);
        if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkStateGetOff), reinterpret_cast<void**>(&g_OrigStateGetOff)))
        {
            LogError("BattleGear: walker get-off hook failed, getting off stays the D-Walker get-off");
            return;
        }
        g_StateGetOffTarget = target;
        InstallClimbOutMotion();
    }

    void UninstallClimbOut()
    {
        UninstallBuddyCodeCave(g_ClimbOutMotion);
        if (g_StateGetOffTarget)
        {
            DisableAndRemoveHook(g_StateGetOffTarget);
            g_StateGetOffTarget = nullptr;
            g_OrigStateGetOff = nullptr;
        }
    }
}

bool Install_RideWalkerGearActionImpl_StateDrive_Patches()
{
    static_assert(sizeof(kStanceEdgeSite) == sizeof(kStanceHeldSite) && sizeof(kStanceEdgeSite) == sizeof(kEvadeEdgeSite),
                  "the three bit sites share one layout");
    const bool ok = InstallReady()
                 && InstallBitSite(g_StanceEdge, gAddr.RideWalkerGearActionImpl_StateDrive_StanceEdge, kStanceEdgeSite, SiteNote::CountPress)
                 && InstallBitSite(g_StanceHeld, gAddr.RideWalkerGearActionImpl_StateDrive_StanceHeld, kStanceHeldSite, SiteNote::MarkHeld)
                 && InstallBitSite(g_EvadeEdge, gAddr.RideWalkerGearActionImpl_StateDrive_EvadeEdge, kEvadeEdgeSite, SiteNote::None);
    if (!ok)
        Uninstall_RideWalkerGearActionImpl_StateDrive_Patches();
    return ok;
}

void Uninstall_RideWalkerGearActionImpl_StateDrive_Patches()
{
    UninstallBuddyCodeCave(g_EvadeEdge);
    UninstallBuddyCodeCave(g_StanceHeld);
    UninstallBuddyCodeCave(g_StanceEdge);
    UninstallBuddyCodeCave(g_Ready);
}

bool Install_RideWalkerGearActionImpl_StateDrive_SteeringHook()
{
    if (g_StateDriveTarget)
        return true;
    void* target = gAddr.RideWalkerGearActionImpl_StateDrive ? ResolveGameAddress(gAddr.RideWalkerGearActionImpl_StateDrive) : nullptr;
    void* deltaTime = gAddr.PlayerActionPlugin_GetDeltaTime ? ResolveGameAddress(gAddr.PlayerActionPlugin_GetDeltaTime) : nullptr;
    if (!target || !deltaTime)
    {
        LogError("BattleGear: ride input addresses missing on this build, in fire posture the hull turns with the camera at once");
        return false;
    }
    if (CheckHookEntry(deltaTime, kGetDeltaTimeEntry, sizeof(kGetDeltaTimeEntry)) == HookEntry::Unknown)
    {
        LogError("BattleGear: ride frame-time code differs on this build, in fire posture the hull turns with the camera at once");
        return false;
    }
    const HookEntry state = CheckHookEntry(target, kStateDriveEntry, sizeof(kStateDriveEntry));
    if (state == HookEntry::Unknown)
    {
        char desc[160] = {};
        DescribeFunctionEntry(target, desc, sizeof(desc));
        LogError("BattleGear: walker ride code differs on this build (%s), in fire posture the hull turns with the camera at once", desc);
        return false;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("BattleGear: RideWalkerGearActionImpl StateDrive", target);
    void* cameraDirection = gAddr.WalkerGearAttackActionImpl_GetCameraDirection
                                ? ResolveGameAddress(gAddr.WalkerGearAttackActionImpl_GetCameraDirection) : nullptr;
    if (!cameraDirection || CheckHookEntry(cameraDirection, kGetCameraDirectionEntry, sizeof(kGetCameraDirectionEntry)) == HookEntry::Unknown)
        LogWarn("BattleGear: walker camera-direction code missing or changed on this build, the guns aim at the camera's look-at point");
    else
        g_GetCameraDirection = reinterpret_cast<GetCameraDirection_t>(cameraDirection);
    g_GetDeltaTime = reinterpret_cast<GetDeltaTime_t>(deltaTime);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkStateDrive), reinterpret_cast<void**>(&g_OrigStateDrive)))
    {
        LogError("BattleGear: walker ride hook failed, in fire posture the hull turns with the camera at once");
        return false;
    }
    g_StateDriveTarget = target;
    InstallClimbMotion();
    InstallClimbOut();
    return true;
}

void Uninstall_RideWalkerGearActionImpl_StateDrive_SteeringHook()
{
    if (!g_StateDriveTarget)
        return;
    UninstallClimbOut();
    UninstallBuddyCodeCave(g_ClimbMotion);
    StopClimb();
    if (PlayerMesh::AllHidden())
        PlayerMesh::SetAllHidden(false);
    DisableAndRemoveHook(g_StateDriveTarget);
    g_StateDriveTarget = nullptr;
    g_OrigStateDrive = nullptr;
    g_GetCameraDirection = nullptr;
    battleGearWeapons::StopHullSteering();
}
