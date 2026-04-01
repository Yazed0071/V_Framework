#pragma once

#include <Windows.h>
#include <cstdint>

enum class GameBuild
{
    Unknown = 0,
    Steam_MST_EN_DAY1820MGO_PATCH_0212_1307 = 1,
    Steam_MST_JPN_DAY1820MGO_PATCH_0212_1307 = 2
};

struct AddressSet
{
    std::uintptr_t AddNoise = 0;
    std::uintptr_t AddNoticeInfo = 0;
    std::uintptr_t BeginSoundSystem = 0;
    std::uintptr_t CallImpl = 0;
    std::uintptr_t CallWithRadioType = 0;
    std::uintptr_t CassettePlayerVtable = 0;
    std::uintptr_t CassetteStart = 0;
    std::uintptr_t CheckSightNoticeHostage = 0;
    std::uintptr_t ConvertRadioTypeToLabel = 0;
    std::uintptr_t DecrementPhaseCounter = 0;
    std::uintptr_t ExecCallback = 0;
    std::uintptr_t FoxLuaRegisterLibrary = 0;
    std::uintptr_t FoxPath_Path = 0;
    std::uintptr_t FoxStrHash32 = 0;
    std::uintptr_t FoxStrHash64 = 0;
    std::uintptr_t GameOverSetVisible = 0;
    std::uintptr_t GetCurrentMissionCode = 0;
    std::uintptr_t GetNameIdWithGameObjectId = 0;
    std::uintptr_t GetPlayingTime = 0;
    std::uintptr_t GetPlayingTrackId = 0;
    std::uintptr_t GetTrackInfoByName = 0;
    std::uintptr_t GetVoiceParamWithCallSign = 0;
    std::uintptr_t LoadPlayerVoiceFpk = 0;
    std::uintptr_t LoadingScreenOrGameOverSplash2 = 0;
    std::uintptr_t MusicManager_s_instance = 0;
    std::uintptr_t PathHashCode = 0;
    std::uintptr_t PauseMusicPlayer = 0;
    std::uintptr_t PlayOrPauseSelectedTrack = 0;
    std::uintptr_t RequestCorpse = 0;
    std::uintptr_t ResumeMusicPlayer = 0;
    std::uintptr_t SetEquipBackgroundTexture = 0;
    std::uintptr_t SetLuaFunctions = 0;
    std::uintptr_t SetTextureName = 0;
    std::uintptr_t SoundSystemCtor = 0;
    std::uintptr_t StateRadio = 0;
    std::uintptr_t StateRadioRequest = 0;
    std::uintptr_t State_ComradeAction = 0;
    std::uintptr_t State_EnterDownHoldup = 0;
    std::uintptr_t State_EnterStandHoldup1 = 0;
    std::uintptr_t State_EnterStandHoldupUnarmed = 0;
    std::uintptr_t State_RecoveryTouch = 0;
    std::uintptr_t State_StandHoldupCancelLookToPlayer = 0;
    std::uintptr_t State_StandRecoveryHoldup = 0;
    std::uintptr_t StepRadioDiscovery = 0;
    std::uintptr_t StopMusicPlayer = 0;
    std::uintptr_t UpdateOptCamo = 0;
    std::uintptr_t g_SoundSystem = 0;
    std::uintptr_t lua_getfield = 0;
    std::uintptr_t lua_gettop = 0;
    std::uintptr_t lua_isnumber = 0;
    std::uintptr_t lua_isstring = 0;
    std::uintptr_t lua_objlen = 0;
    std::uintptr_t lua_pushboolean = 0;
    std::uintptr_t lua_pushnumber = 0;
    std::uintptr_t lua_rawgeti = 0;
    std::uintptr_t lua_settop = 0;
    std::uintptr_t lua_toboolean = 0;
    std::uintptr_t lua_tointeger = 0;
    std::uintptr_t lua_tolstring = 0;
    std::uintptr_t lua_tonumber = 0;
    std::uintptr_t lua_type = 0;
};

extern GameBuild gGameBuild;
extern AddressSet gAddr;

bool ResolveAddressSet(HMODULE hGame);
const char* GetGameBuildName(GameBuild build);