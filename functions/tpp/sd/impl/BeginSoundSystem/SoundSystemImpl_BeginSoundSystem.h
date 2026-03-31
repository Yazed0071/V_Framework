#pragma once

#include <cstdint>

// Installs the sound-system hooks.
// Params: none
bool Install_SoundSystem_BeginSoundSystem_Hook();

// Removes the sound-system hooks.
// Params: none
bool Uninstall_SoundSystem_BeginSoundSystem_Hook();

// Gets the cached sound-system pointer.
// Returns: cached pointer or null.
void* GetCachedSoundSystem();

// Gets the cached cassette music player pointer.
// Returns: cached player pointer or null.
void* GetGlobalCassetteMusicPlayerFromSoundSystem();

// Re-scans the cached/global sound-system object for the cassette music player.
// Returns: true on success, false on failure.
bool RefreshGlobalCassetteMusicPlayerFromSoundSystem();

// Gets the current playing time from the cached cassette music player.
// Returns: raw playing-time value, or 0 on failure.
std::uint32_t GetCassettePlayingTime();

// Gets the current playing track id from the cached cassette music player.
// Returns: raw playing-track id, or 0 on failure.
std::uint32_t GetCassettePlayingTrackId();