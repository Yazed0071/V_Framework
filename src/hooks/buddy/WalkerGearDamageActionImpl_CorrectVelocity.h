#pragma once

// the walker's damage action moves it through a hit's reaction clip in place of its basic action. The ridden Battle Gear
// walks on through the bullet, big bullet and blow reactions: a hit does not stop it. Knocked down, the game's clip keeps it
bool Install_WalkerGearDamageActionImpl_CorrectVelocity_Hook();
void Uninstall_WalkerGearDamageActionImpl_CorrectVelocity_Hook();
