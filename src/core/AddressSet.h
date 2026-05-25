#pragma once

#include <Windows.h>
#include <cstdint>

namespace AddressSetRuntime
{
    enum class GameBuild
    {
        Unknown,
        English,
        Japanese
    };

    struct AddressSet
    {
        uintptr_t AddNoise = 0;
        uintptr_t AddNoticeInfo = 0;
        uintptr_t ArrayBaseFree = 0;
        uintptr_t BeginSoundSystem = 0;
        uintptr_t CallImpl = 0;
        uintptr_t CallWithRadioType = 0;
        uintptr_t CassettePlayerVtable = 0;
        uintptr_t CassetteStart = 0;
        uintptr_t CheckSightNoticeHostage = 0;
        uintptr_t ConvertRadioTypeToLabel = 0;
        uintptr_t CopyAndAdjustInfo = 0;
        uintptr_t DecrementPhaseCounter = 0;
        uintptr_t ExecCallback = 0;
        uintptr_t FoxLuaRegisterLibrary = 0;
        uintptr_t FoxPath_Path = 0;
        uintptr_t FoxStrHash32 = 0;
        uintptr_t FoxStrHash64 = 0;
        uintptr_t GameOverSetVisible = 0;
        uintptr_t GetCurrentMissionCode = 0;
        uintptr_t GetNameIdWithGameObjectId = 0;
        uintptr_t GetGameObjectIdWithIndex = 0;
        uintptr_t GetPlayingTime = 0;
        uintptr_t GetPlayingTrackId = 0;
        uintptr_t GetQuarkSystemTable = 0;
        uintptr_t GetTrackInfoByName = 0;
        uintptr_t GetVoiceLanguage = 0;
        uintptr_t GetVoiceParamWithCallSign = 0;
        uintptr_t KernelAllocAligned = 0;
        uintptr_t LoadPlayerVoiceFpk = 0;
        uintptr_t LoadingScreenOrGameOverSplash2 = 0;
        uintptr_t MusicManager_s_instance = 0;
        uintptr_t PathHashCode = 0;
        uintptr_t PauseMusicPlayer = 0;
        uintptr_t PlayOrPauseSelectedTrack = 0;
        uintptr_t RequestCorpse = 0;
        uintptr_t ResumeMusicPlayer = 0;
        uintptr_t SetEquipBackgroundTexture = 0;
        uintptr_t SetLuaFunctions = 0;
        uintptr_t SetTextureName = 0;
        uintptr_t SoundSystemCtor = 0;
        uintptr_t StateRadio = 0;
        uintptr_t StateRadioRequest = 0;
        uintptr_t State_ComradeAction = 0;
        uintptr_t State_EnterDownHoldup = 0;
        uintptr_t State_EnterStandHoldup1 = 0;
        uintptr_t State_EnterStandHoldupUnarmed = 0;
        uintptr_t State_RecoveryKick = 0;
        uintptr_t State_RecoveryTouch = 0;
        uintptr_t State_StandEnterRecoverySleepFaintHoldupComradeBySound = 0;
        uintptr_t State_StandHoldupCancelLookToPlayer = 0;
        uintptr_t State_StandRecoveryHoldup = 0;
        uintptr_t StepRadioDiscovery = 0;
        uintptr_t StopMusicPlayer = 0;
        uintptr_t SubtitleManager_Get = 0;
        uintptr_t UpdateOptCamo = 0;
        uintptr_t g_SoundSystem = 0;
        uintptr_t lua_getfield = 0;
        uintptr_t lua_gettop = 0;
        uintptr_t lua_isnumber = 0;
        uintptr_t lua_isstring = 0;
        uintptr_t lua_objlen = 0;
        uintptr_t lua_pushboolean = 0;
        uintptr_t lua_pushnumber = 0;
        uintptr_t lua_rawgeti = 0;
        uintptr_t lua_settop = 0;
        uintptr_t lua_toboolean = 0;
        uintptr_t lua_tointeger = 0;
        uintptr_t lua_tolstring = 0;
        uintptr_t lua_tonumber = 0;
        uintptr_t lua_type = 0;
        uintptr_t lua_pushstring = 0;
        uintptr_t lua_createtable = 0;
        uintptr_t lua_rawset = 0;
        uintptr_t lua_settable = 0;
        uintptr_t lua_pushnil = 0;
        uintptr_t lua_next = 0;
        uintptr_t lua_gettable = 0;
        uintptr_t lua_pushvalue = 0;
        uintptr_t lua_pcall = 0;
        uintptr_t lua_pushcclosure = 0;
        uintptr_t GetIconFtexPath = 0;
        uintptr_t LoadingTipsEv_UpdateActPhase = 0;
        uintptr_t AK_SoundEngine_SetRTPCValue = 0;
        uintptr_t Fox_Sd_ConvertParameterID = 0;
        uintptr_t Fox_Sd_Ad_AudioSoundEngine_RegisterGameObject = 0;
        uintptr_t Fox_Sd_Object_Activate = 0;
        uintptr_t Fox_Sd_Daemon_GetObject = 0;
        uintptr_t Fox_Sd_Daemon_Singleton = 0;
        uintptr_t SoundControllerImpl_CallInternal = 0;
        uintptr_t TornadoDualPatch                  = 0;
        uintptr_t RealizedSahelan2Impl_Realize      = 0;
        uintptr_t RealizedSahelan2Impl_SetFovaImpl  = 0;
        uintptr_t FormVariationFile2_ApplyOnlyMeshAndTextureVariation = 0;
        uintptr_t Sahelan_ActionCoreImpl_SetEyeLampColor = 0;
        uintptr_t Sahelan_ActionCoreImpl_UpdateEyeLampColor = 0;
        uintptr_t Sahelan_ActionCoreImpl_UpdateHeartLight = 0;
        uintptr_t Sahelan_PhaseSneakAi_PushEyeColor = 0;
        uintptr_t Sahelan_EyeMeshHashTable = 0;
        uintptr_t Sahelan_PhaseSneakAi_ColorTableBase = 0;
        uintptr_t RealizedSecurityCamera2Impl_SetFova = 0;
        uintptr_t MbDvcAnnouncePopupCallbackImpl_UpdateAnnounceNormal    = 0;
        uintptr_t MbDvcAnnouncePopupCallbackImpl_UpdateAnnounceServer    = 0;
        uintptr_t MbDvcAnnouncePopupCallbackImpl_UpdateAnnounceEmergency = 0;
        uintptr_t HudCommonDataManager_GetInstance       = 0;
        uintptr_t HudCommonDataManager_SetPopupType      = 0;
        uintptr_t HudCommonDataManager_SetPopupText      = 0;
        uintptr_t HudCommonDataManager_SetPopupErrorType = 0;
        uintptr_t HudCommonDataManager_StartPopup        = 0;
        uintptr_t Soldier2SoundController_GetVoiceTypeFromSoldierTypeImpl = 0;
        uintptr_t Soldier2SoundController_Activate = 0;
        uintptr_t CAkResampler_SetPitch = 0;
        uintptr_t FNVHash32                         = 0;
        uintptr_t Play_bgm_gameover                 = 0;
        uintptr_t Play_bgm_gameover_paradox         = 0;
        uintptr_t Play_bgm_gameover_perfectstealth  = 0;
        uintptr_t Play_bgm_s10010_gameover          = 0;
        uintptr_t Stop_bgm_gameover                 = 0;
        uintptr_t Stop_bgm_gameover_paradox         = 0;
        uintptr_t Stop_bgm_gameover_perfectstealth  = 0;
        uintptr_t Stop_bgm_s10010_gameover          = 0;
        uintptr_t Play_bgm_gameover_paradox_soundId = 0;
        uintptr_t Stop_bgm_gameover_paradox_soundId = 0;
        uintptr_t DD_vox_SH_voice                   = 0;
        uintptr_t DD_vox_SH_radio                   = 0;
        uintptr_t DD_vox_SH_radio2                  = 0;
        uintptr_t DD_vox_SH_radio3                  = 0;

        uintptr_t MotherBaseMapCommonDataImpl_GetEnemyInformationLangId = 0;
        uintptr_t TppUIBinoSubjectiveImpl_GetEnemyUnitName              = 0;

        uintptr_t BasicActionImpl_StateCrawlSideRoll                    = 0;

        uintptr_t Sahelan_PhaseSneakAiImpl_PreUpdate                    = 0;
        uintptr_t Sahelan_PhaseSneakAiImpl_StepFuncsTable               = 0;

        uintptr_t MessageResendCounter                                  = 0;

        uintptr_t GetMissionCodeCategory                                = 0;

        uintptr_t TppUiCommand_ShowMissionIcon                          = 0;

        uintptr_t IconTitleHashImm                                      = 0;

        uintptr_t IconTitleGetLangTextCall                              = 0;

    };

    inline GameBuild& GetGameBuild()
    {
        static GameBuild value = GameBuild::Unknown;
        return value;
    }

    inline AddressSet& GetAddressSet()
    {
        static AddressSet value{};
        return value;
    }

    const AddressSet& GetEnglishAddressSet();
    const AddressSet& GetJapaneseAddressSet();
    GameBuild DetectGameBuildFromVersionInfo(HMODULE hGame);
    bool ResolveAddressSet(HMODULE hGame);

    inline const char* GetGameBuildName(GameBuild build)
    {
        switch (build)
        {
        case GameBuild::English:
            return "English";
        case GameBuild::Japanese:
            return "Japanese";
        default:
            return "Unknown";
        }
    }
}

#define gGameBuild (::AddressSetRuntime::GetGameBuild())
#define gAddr (::AddressSetRuntime::GetAddressSet())
#define ResolveAddressSet (::AddressSetRuntime::ResolveAddressSet)
#define GetGameBuildName (::AddressSetRuntime::GetGameBuildName)
