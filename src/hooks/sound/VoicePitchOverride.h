#pragma once

#include <cstdint>


// Phase 0 — global pitch bias applied to every active sound's
// CAkResampler::SetPitch call. Affects ALL audio (voice, sfx, bgm).
//   centsBias : added to whatever pitch the engine asks for. 0 = pass through.
//               Range typically -2400..+2400 (engine clamps internally).
void Set_GlobalVoicePitchBiasCents(float centsBias);
float Get_GlobalVoicePitchBiasCents();


// Phase 1 — per-AkGameObjectID pitch bias.
// When the SetPitch hook fires, it walks the chain:
//   resampler - 0x10 = CAkVPLPitchNode
//   pitchNode + 0xD8 = CAkPBI*
//   pbi + 0xA8       = CAkRegisteredObj*
//   regObj + 0x70    = akObjId (uint64)
// If the akObjId is in this map, that bias is used INSTEAD of the global.
// Pair with the existing SetRtpcLoggingEnabled to discover akObjIds.
void Set_PitchBiasForAkObjId(std::uint64_t akObjId, float centsBias);
void Clear_PitchBiasForAkObjId(std::uint64_t akObjId);
void Clear_AllPerAkObjIdPitchBiases();


// Toggle a logging hook on every CAkResampler::SetPitch call.
// HOT PATH — leave disabled unless debugging.
void Set_PitchHookLoggingEnabled(bool enabled);
bool Is_PitchHookLoggingEnabled();


// When ON, the hook also logs the resolved akObjId for each call (still
// throttled). Lets you correlate a SetPitch this= with a soldier's akObjId
// before applying a per-akObjId bias.
void Set_PitchChainLoggingEnabled(bool enabled);
bool Is_PitchChainLoggingEnabled();


bool Install_VoicePitchOverride_Hook();
bool Uninstall_VoicePitchOverride_Hook();
