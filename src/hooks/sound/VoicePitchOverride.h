#pragma once

#include <cstdint>

void Set_GlobalVoicePitchBiasCents(float centsBias);
float Get_GlobalVoicePitchBiasCents();

void Set_PitchBiasForAkObjId(std::uint64_t akObjId, float centsBias);
void Clear_PitchBiasForAkObjId(std::uint64_t akObjId);
void Clear_AllPerAkObjIdPitchBiases();

bool Install_VoicePitchOverride_Hook();
bool Uninstall_VoicePitchOverride_Hook();
