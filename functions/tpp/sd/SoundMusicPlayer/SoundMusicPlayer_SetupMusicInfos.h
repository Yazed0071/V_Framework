#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Describes one custom cassette album from Lua.
// Params: none
struct CustomTapeAlbumDefinition
{
    std::string albumId;
    std::string langId;
    std::string type;
    std::int32_t typeValue = -1;
};

// Describes one custom cassette track from Lua.
// Params: none
struct CustomTapeTrackDefinition
{
    std::string albumId;
    std::int16_t saveIndex = -1;
    std::string langId;
    std::uint32_t dataTimeJp = 0;
    std::uint32_t dataTimeEn = 0;
    std::uint16_t important = 0;
    std::uint16_t special = 0;
    std::string fileName;
};

// Installs the SetupMusicInfos hook.
// Params: none
bool Install_SoundMusicPlayer_SetupMusicInfos_Hook();

// Removes the SetupMusicInfos hook.
// Params: none
bool Uninstall_SoundMusicPlayer_SetupMusicInfos_Hook();

// Registers custom albums and tracks.
// Params: albums, tracks
bool Register_CustomTapes(
    const std::vector<CustomTapeAlbumDefinition>& albums,
    const std::vector<CustomTapeTrackDefinition>& tracks);

// Clears the pending custom tape registry.
// Note: this does not live-remove already injected tape entries.
// Params: none
void Clear_CustomTapes();

// Returns true if this saveIndex belongs to a custom cassette track.
// Params: saveIndex
bool IsCustomCassetteSaveIndex(short saveIndex);

// Returns true if this custom cassette track is marked owned in the custom store.
// Params: saveIndex
bool IsCustomCassetteTrackOwned(short saveIndex);

// Marks one custom cassette track as owned or not owned in the custom store.
// Params: saveIndex, owned
void SetCustomCassetteTrackOwned(short saveIndex, bool owned);