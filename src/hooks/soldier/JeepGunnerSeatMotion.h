#pragma once

bool JeepGunnerSeatMotion_Install(); // false when the gunner seat's motion hooks are not all in: then nobody gets that seat
void JeepGunnerSeatMotion_Uninstall();
bool JeepGunner_InStandScope();      // true while the gunner's get-on/get-off code runs as seat 1 from his gun stand
