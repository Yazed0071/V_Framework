#pragma once

#include <cstdint>


bool CassettePagingAvailable();

void SetCassetteAlbumTracks(void* cassetteCallback, std::uint64_t albumId, const std::uint32_t* trackIds, std::uint32_t trackCount);

int GetPagerActionForSlot(void* cassetteCallback, std::uint32_t slot);

bool IsPagerSentinelId(std::uint32_t trackId);

void FlipCassettePage(void* cassetteCallback, int direction);


bool Install_CassetteTrackPaging_Hooks();

bool Uninstall_CassetteTrackPaging_Hooks();
