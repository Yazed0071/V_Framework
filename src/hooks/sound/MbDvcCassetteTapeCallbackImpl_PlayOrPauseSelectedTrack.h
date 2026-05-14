#pragma once

#include <cstdint>

bool Install_MbDvcCassetteTapeCallbackImpl_PlayOrPauseSelectedTrack_Hook();

bool Uninstall_MbDvcCassetteTapeCallbackImpl_PlayOrPauseSelectedTrack_Hook();

bool PlayCassetteByAlbumAndTrack(
    std::uint32_t albumIndex,
    std::uint32_t trackIndex,
    bool loopPlay,
    bool playAll);

bool PlayCassetteByTrackId(
    std::uint32_t albumIndex,
    std::uint32_t trackId,
    bool loopPlay,
    bool playAll);

bool IsCassetteSpeakerEnabled(bool& outEnabled);

bool SetCassetteSpeakerEnabled(bool enabled);
