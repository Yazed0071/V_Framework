#pragma once

bool Install_FieldTaxiMenu();
bool Uninstall_FieldTaxiMenu();

void FieldTaxi_SetMissionEnabled(unsigned int missionCode, bool enabled);
void FieldTaxi_SetTaxiRideState(unsigned int state);
void FieldTaxi_ResetTaxiState();
void FieldTaxi_SetTaxiRideLog(bool enabled);
void FieldTaxi_SetTaxiLandingZoneHidden(unsigned int lzNameHash, bool hidden);
