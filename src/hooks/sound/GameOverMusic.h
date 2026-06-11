#pragma once
#include <cstdint>

enum GAME_OVER_TYPE : std::uint8_t
{
    GAME_OVER_GENERAL = 0,
    GAME_OVER_PARADOX = 1,
    GAME_OVER_STEALTH = 2,
    GAME_OVER_CYPRUS  = 3,
};

bool SetGameOverMusic(bool isEnable,
                      GAME_OVER_TYPE type,
                      unsigned int playEventHash,
                      unsigned int stopEventHash);
