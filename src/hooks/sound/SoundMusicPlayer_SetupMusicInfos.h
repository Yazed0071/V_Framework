#pragma once

#include <cstdint>
#include <string>
#include <vector>


struct CustomTapeAlbumDefinition
{
    std::string albumId;
    std::string langId;
    std::string type;
    std::int32_t typeValue = -1;
};


struct CustomTapeTrackDefinition
{
    std::string albumId;
    std::int16_t saveIndex = -1;
    std::string langId;
    std::uint32_t dataTimeJp = 0;
    std::uint32_t dataTimeEn = 0;
    std::uint16_t important = 0;
    std::uint16_t special = 0;
    bool unlocked = false;
    std::string fileName;
};


bool Install_SoundMusicPlayer_SetupMusicInfos_Hook();


bool Uninstall_SoundMusicPlayer_SetupMusicInfos_Hook();


bool Register_CustomTapes(
    const std::vector<CustomTapeAlbumDefinition>& albums,
    const std::vector<CustomTapeTrackDefinition>& tracks);


bool IsCustomTapeImportantBySaveIndex(std::int16_t saveIndex);