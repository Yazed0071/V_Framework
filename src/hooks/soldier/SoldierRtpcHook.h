#pragma once

// Per-soldier (and global) Wwise RTPC control.
//
// Background:
//   MGSV:TPP audio is Wwise-based. Every entity that emits sound has a Wwise
//   AkGameObjectID. The fox::sd::ad::AudioGameObject wrapper stores that ID
//   as the first 4 bytes of the object (see AudioEventCallback::EventCallback
//   in mgsvtpp.exe.c line 4796005: `param_2[1] == (ulonglong)*(uint*)this`).
//   The FOX GameObjectId of a soldier is used directly as the AkGameObjectID
//   (zero-extended to 64 bits) — same uint32_t key used throughout VIPRadio,
//   CorpseManager, and the other per-soldier V_FrameWork hooks.
//
// APIs exposed:
//   - SetSoldierRtpc(goId, rtpcName, value, timeMs)
//       Applies the RTPC only to the soldier with that FOX gameObjectId.
//   - SetGlobalRtpc(rtpcName, value, timeMs)
//       Applies globally (uses AK_INVALID_GAME_OBJECT = 0xFFFFFFFFFFFFFFFF).
//   - ResetSoldierRtpc(goId, rtpcName, timeMs)
//       Resets the per-object RTPC override so the soldier falls back to
//       whatever global / parent value Wwise has.
//
// All three return the Wwise AKRESULT code (1 = AK_Success) or a negative
// number if the game APIs couldn't be resolved.

#include <cstdint>

namespace SoldierRtpc
{
    // Applies an RTPC to one soldier by FOX gameObjectId, resolving the Wwise
    // RTPC id from its authoring name via fox::sd::ConvertParameterID.
    // Params: goId (FOX gameObjectId), rtpcName (Wwise name string),
    //         value (new RTPC value), timeMs (transition time in ms; 0 = instant)
    int SetSoldierRtpc(std::uint32_t goId, const char* rtpcName, float value, long timeMs);

    // Same as SetSoldierRtpc but takes a pre-hashed Wwise RTPC id directly.
    // Use this when you already know the numeric id (e.g. from the Wwise
    // authoring project's generated header or the ConvertParameterIdLogger).
    // Params: goId, rtpcId (uint32 Wwise id), value, timeMs
    int SetSoldierRtpcById(std::uint32_t goId, std::uint32_t rtpcId, float value, long timeMs);

    // Applies an RTPC globally (AK_INVALID_GAME_OBJECT scope).
    // Params: rtpcName, value, timeMs
    int SetGlobalRtpc(const char* rtpcName, float value, long timeMs);

    // Same as SetGlobalRtpc but takes a pre-hashed Wwise RTPC id directly.
    // Params: rtpcId, value, timeMs
    int SetGlobalRtpcById(std::uint32_t rtpcId, float value, long timeMs);

    // Resets a per-soldier RTPC override so the soldier falls back to global.
    // Params: goId (FOX gameObjectId), rtpcName, timeMs
    int ResetSoldierRtpc(std::uint32_t goId, const char* rtpcName, long timeMs);
}
