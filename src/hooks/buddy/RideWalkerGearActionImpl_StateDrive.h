#pragma once

// riding the Battle Gear: Stance toggles run / fire posture (fire posture keeps the guns raised = the walker's
// weapon-ready strafe), and Stance or Evade no longer start the D-Walker roll
bool Install_RideWalkerGearActionImpl_StateDrive_Patches();
void Uninstall_RideWalkerGearActionImpl_StateDrive_Patches();

// fire posture: the walker strafes as the game has it while its hull swings toward the camera at 45 degrees a second; getting in
// climbs into the turret's cupola the armored-vehicle way
bool Install_RideWalkerGearActionImpl_StateDrive_SteeringHook();
void Uninstall_RideWalkerGearActionImpl_StateDrive_SteeringHook();
