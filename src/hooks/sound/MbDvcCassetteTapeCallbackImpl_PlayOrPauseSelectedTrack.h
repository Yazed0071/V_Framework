#pragma once

#include <cstdint>

// Installs the cassette Start and PlayOrPause hooks.
// Params: none
bool Install_MbDvcCassetteTapeCallbackImpl_PlayOrPauseSelectedTrack_Hook();

// Removes the cassette Start and PlayOrPause hooks.
// Params: none
bool Uninstall_MbDvcCassetteTapeCallbackImpl_PlayOrPauseSelectedTrack_Hook();

// Plays a cassette directly by album and track.
// Params: albumIndex, trackIndex, loopPlay, playAll
// Returns: true on success, false on failure.
bool PlayCassetteByAlbumAndTrack(
    std::uint32_t albumIndex,
    std::uint32_t trackIndex,
    bool loopPlay,
    bool playAll);

// Plays a cassette directly by numeric track id.
// Params: albumIndex, trackId, loopPlay, playAll
// Returns: true on success, false on failure.
bool PlayCassetteByTrackId(
    std::uint32_t albumIndex,
    std::uint32_t trackId,
    bool loopPlay,
    bool playAll);

// Gets the current cassette speaker state from the cached cassette callback.
// Params: outEnabled
// Returns: true on success, false on failure.
bool IsCassetteSpeakerEnabled(bool& outEnabled);

// Sets the cassette speaker state on the cached cassette callback.
// Params: enabled
// Returns: true on success, false on failure.
bool SetCassetteSpeakerEnabled(bool enabled);
