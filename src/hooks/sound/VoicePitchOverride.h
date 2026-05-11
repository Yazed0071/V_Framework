#pragma once

#include <cstdint>


// Global pitch bias applied to every active sound's CAkResampler::SetPitch.
// Affects ALL audio (voice, sfx, bgm).
//   centsBias : added to whatever pitch the engine asks for. 0 = pass through.
//               Range typically -2400..+2400 (engine clamps internally).
void Set_GlobalVoicePitchBiasCents(float centsBias);
float Get_GlobalVoicePitchBiasCents();


// Per-AkGameObjectID pitch bias.
// When the SetPitch hook fires, it walks:
//   resampler - 0x10 = CAkVPLPitchNode
//   pitchNode + 0xD8 = CAkPBI*
//   pbi + 0xA8       = CAkRegisteredObj*
//   regObj + 0x70    = akObjId (uint64)
// If the akObjId is in this map, that bias is used INSTEAD of the global.
void Set_PitchBiasForAkObjId(std::uint64_t akObjId, float centsBias);
void Clear_PitchBiasForAkObjId(std::uint64_t akObjId);
void Clear_AllPerAkObjIdPitchBiases();


bool Install_VoicePitchOverride_Hook();
bool Uninstall_VoicePitchOverride_Hook();
