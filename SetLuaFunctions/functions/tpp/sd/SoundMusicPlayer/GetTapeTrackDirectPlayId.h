#pragma once

#include <cstdint>

// Resolves a direct-play tape track id from a C string.
// Params: trackName
// Returns: direct-play track id, or -1 on failure.
std::int32_t ResolveTapeTrackDirectPlayId(const char* trackName);