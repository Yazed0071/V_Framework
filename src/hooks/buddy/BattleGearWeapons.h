#pragma once

#include <cstdint>

namespace battleGearWeapons
{
    constexpr std::uint8_t kRailGun  = 3;   // D-Walker main weapon type 3 (the stun gun) carries the rail-gun
    constexpr std::uint8_t kHMissile = 2;   // D-Walker main weapon type 2, the missile pod
    constexpr std::uint8_t kHeavyMg  = 5;   // D-Walker sidearm type 5, the walker ZE'EV

    constexpr std::uint8_t kSupportHead   = 1;   // D-Walker head types (0 = none)
    constexpr std::uint8_t kScoutingHead  = 2;
    constexpr std::uint8_t kInterceptHead = 3;

    // the gear getters answer for the Battle Gear; without them its weapon pack must stay the D-Walker's
    void SetLoadoutActive(bool active);

    // when the buddy block sets the Battle Gear up: fixes its main gun and whether it carries the Battle Gear loadout at all
    // (field mission, not the ACC); the gear getters, the weapon pack and the HUD names follow that one decision
    void LatchLoadout();
    bool LoadoutApplies();

    // Lua's pick, carried by the next Battle Gear the buddy block sets up
    bool SetChosenMainWeapon(const char* name);
    const char* ChosenMainWeaponName();
    std::uint8_t MainWeaponType();

    // Lua's head pick; the walker swaps the head as soon as it reads a new type
    bool SetChosenHead(const char* name);
    const char* ChosenHeadName();
    std::uint8_t HeadType();
    std::uint8_t HeadGrade();

    // the Customize > BATTLE GEAR page's picks (0 = none), saved by V Framework. A script's pick replaces them for that mission only:
    // the mission reset drops it, and the next mission takes the page picks again (or the defaults)
    void SetPagePicks(std::uint8_t mainWeapon, std::uint8_t head);
    std::uint8_t PageMainWeapon();
    std::uint8_t PageHead();
    std::uint8_t EffectiveMainWeapon();
    void ClearScriptPicks();

    // the buddy block's last weapon pack was a D-Walker pack loaded under the Battle Gear: its main gun gets hidden at setup
    // (the Battle Gear's own weapon packs hide it in the model already)
    void SetHideMainGunAtSetup(bool hide);
    bool HideMainGunAtSetup();

    // the walker update while the Battle Gear is out, and when it is not
    void OnFieldFrame();
    void OnNotOut();

    // the Battle Gear's comm block (WalkerGear2AttachmentCommunicationInfo), 0 while it is not out
    const void* BattleGearCommSlot();
    const void* FireModeSlot();
    const void* AimingSlot();
    const void* RiddenSlot();   // a byte, 1 while the local player rides the Battle Gear
    bool FireMode();
    bool Aiming();
    bool Ridden();

    // the tail hand's carry. The rider's carry prompt looks for a body in a box round the hull instead of near the seat; the carried
    // body follows the walker's hold point, which the tail hand takes over. The animation job publishes both each frame.
    enum class CarryPhase { None, PickingUp, Holding, PuttingDown };
    CarryPhase Carry();                          // from the walker's interaction bits in the comm block
    const void* CarryZoneSlot();                 // 16-byte aligned: the box centre x y z w, then its half sizes
    void PublishCarryZone(const float* centre, float halfSize);
    void* CarryFindSlot();                       // 16-byte aligned: the body the prompt's search found, x y z w, then a byte set
    bool CarryFound(float* position);            // once one was; false until then
    void PublishHoldPoint(const float* matrix);  // 16 floats as the game stores a matrix: three axis rows, then the position
    bool HoldPoint(float* matrix);

    // getting in climbs into the turret's cupola the armored-vehicle way: the Battle Gear kneels until the climb ends, and the rider's
    // root follows the cupola, which the animation job publishes each frame (a matrix as the game stores one)
    void SetClimbing(bool climbing);
    bool Climbing();
    void PublishHatch(const float* matrix);
    bool Hatch(float* matrix);

    // the cupola's lid follows the rider's hand the armored-vehicle way: the ride update holds it open at an angle while a hand is on
    // it, the animation job turns it and lets it fall shut once let go. The lid's hinge frame is the turret's axes at its pivot, closed.
    void PublishLid(const float* matrix);
    bool Lid(float* matrix);
    void HoldLid(float angle);   // radians open; negative lets go
    float LidHold();

    // the hull's frame as it sits on the ground under its feet (rows: right, up, ahead; position: the body bone) and how high the body
    // bone stands above that ground, published by the animation job each frame; the ride's step keeps the hull's outline off walls
    // with it and climbs what is low enough
    void PublishHullFrame(const float* matrix, float clearance);
    bool HullFrame(float* matrix, float* clearance);

    // the ride-input caves count crouch-button presses and mark each frame the button is down; the ride update then reads them:
    // a tap switches normal <-> upright (fire posture -> normal), a hold goes into fire posture (fire posture -> upright), a new
    // Dash press drops upright into normal. Getting in or out leaves it in normal.
    const void* StancePressSlot();
    const void* StanceHeldSlot();
    void OnRideInput(bool dash, float dt);
    bool Upright();

    bool IsBattleGearComm(const void* comm);

    // a switch between normal and upright moves the hull by the shift of its front feet's homes so planted feet stay put: the legs
    // ask for metres (forward positive), the walk code takes them at its own pace into the walker's step
    void RequestSurge(float metres);
    float TakeSurge(float most);
    void ClearSurge();

    // the speed the walk code drives the Battle Gear at (not what its hull is seen to move: a surge is no walking)
    void PublishDriveSpeed(float speed);
    float DriveSpeed();

    // the turn the walk code steers the ridden walker by, radians a second; ridden, nothing else turns it
    void PublishDriveTurn(float rate);
    float DriveTurn();

    // the walk code's about-turn: the Battle Gear lifts onto its hover and swings round
    void PublishPivot(bool pivoting);
    bool Pivoting();

    // the rider's first-person view from the cupola, toggled by the tanks' camera button; it ends with the ride
    void SetFirstPerson(bool on);
    bool FirstPerson();
    const void* FirstPersonSlot();   // a byte, read by the ride's aim cave: the first-person camera is no aim by itself

    // the ridden walker holds its arm weapon (the heavy MG) rather than the main gun; the walk code reads it each frame
    void PublishSidearmInHand(bool inHand);
    bool SidearmInHand();

    // the heading the ridden walker faces instead of the camera: in fire posture it swings toward the camera, in run posture it is
    // the walker's own heading (the stick steers). The walker-update cave compares the comm at the slot, then reads the heading 8
    // bytes on.
    const void* HullSteeringSlot();
    void SteerHull(const void* comm, float heading);
    void StopHullSteering();
    bool HullSteering(float* heading);

    // the animation job's latest world heading of the Battle Gear's front
    void PublishBodyHeading(float heading);
    bool BodyHeading(float* heading);

    // true while the Battle Gear is out in the field (its comm block is known)
    bool OnField();

    // the rider's aim point (comm+0x370) as of the last walker update
    bool AimTarget(float* target);

    // the same point without a rider: where the walker's own AI aims its shots
    bool AimPoint(float* target);

    // the AI fired a Battle Gear gun with nobody riding; the guns then turn to its aim point for a few seconds
    void NoteAiAttack();
    bool AiAttacking();

    // the animation job writes the gun muzzles and where the barrels point, in world space, each frame the Battle Gear is posed
    void PublishMuzzles(const float* railGun, const float* heavyMg, const float (*hatches)[3], const float* railGunAim,
                        const float* heavyMgAim);
    bool RailGunMuzzle(float* position);
    bool MuzzleFor(std::int32_t arm, std::int32_t missileShot, float* position);
    bool BarrelFor(std::int32_t arm, float* direction);   // false for the missile hatches, which keep the walker's own aim

    // true for the Battle Gear's walker attack action (type 0x2C object while the Battle Gear is out)
    bool IsBattleGearAttack(const void* attackAction);

    // a rail-gun shot left at charge level 1-3: sets the cooldown that follows and kicks the hull
    void NoteRailShot(int level);
    int LastRailShotLevel();
    std::uint32_t RailShots();   // counts shots so each reader reacts to every one once

    // the heavy MG is locked by overheat (its barrel smokes)
    void PublishMgOverheat(bool overheated);
    bool MgOverheated();

    // the roof M2's barrel heat as the tanks' cupola gun counts it, 0 cold to 1 overheated: a round adds 1/30, a second takes 0.125
    // off, and an overheated gun stays locked until it is cold again
    bool HeatMg(std::uint32_t rounds);   // true while it is locked
    void CoolMg(float dt);
    void ResetMgHeat();
    float MgHeat();

    // how fast the hull moves over the ground, ridden or not
    void PublishGroundSpeed(float speed);
    float GroundSpeed();

    // SKL_010_GUN of the roof M2 out in the world, as hw01_main0_def's own files see it: rows across, up and along the barrel, then
    // its position; and how fast it moves
    void PublishRoofGun(const float* matrix, float dt);
    void DropRoofGun();
    bool RoofGun(float* matrix, float* velocity);
    void PublishRoofGunTurning(bool turning);
    bool RoofGunTurning();

    // the walker hangs under its Fulton balloon, or comes down from the sky on its drop-in
    bool Hanging();

    // where the screen centre points this ride frame, cast by the ride hook; the game's own aim field holds only the camera's
    // look-at point, which sits on the Battle Gear itself
    void PublishRiderAim(const float* point);

    // the roof M2 as the Battle Gear's own gun: the weapon wheel picks it (the walker's sidearm slot) or the main gun for the
    // trigger; 500 rounds a sortie, the tanks' cupola gun's own count
    void SelectMg(bool selected);
    bool MgSelected();
    // the walker's own frame time, on the game's clock (slow motion slows it): the walk code adds each walker frame, the animation
    // job takes what ran since its last pose. False while the walker runs no frames.
    void AddWalkerTime(float seconds);
    bool TakeWalkerTime(float* seconds);
    void PublishMgTrigger(bool held);
    bool MgTrigger();
    bool TakeMgRound();
    std::uint32_t MgAmmo();

    // the rail-gun holds at least a level 1 charge and has not fired yet (its charge glow shows)
    void PublishRailCharging(bool charging);
    bool RailCharging();

    // heavy MG shots so far; each new one gets a muzzle flash at the turret
    void NoteMgShots(std::uint32_t count);
    std::uint32_t MgShots();
}
