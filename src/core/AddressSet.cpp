#include "pch.h"

#include <Windows.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <string>

#include "AddressSet.h"
#include "log.h"

namespace AddressSetRuntime
{
    namespace
    {
        std::wstring GetModuleDirectory(HMODULE hModule)
        {
            wchar_t path[MAX_PATH] = {};
            if (!GetModuleFileNameW(hModule, path, MAX_PATH))
                return L"";

            std::wstring fullPath(path);
            const size_t slash = fullPath.find_last_of(L"\\/");
            if (slash == std::wstring::npos)
                return L"";

            return fullPath.substr(0, slash);
        }

        std::string ReadWholeFileUtf8OrAnsi(const std::wstring& path)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file)
                return {};

            return std::string(
                (std::istreambuf_iterator<char>(file)),
                std::istreambuf_iterator<char>());
        }

        std::string ToLowerAscii(std::string text)
        {
            std::transform(
                text.begin(),
                text.end(),
                text.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            return text;
        }
    }

    const AddressSet& Get_mst_en_day3800_AddressSet()   // 1.0.15.4 english
    {
        static const AddressSet value =
        {
            0x14147df70ull, // AddNoise
            0x1414dba80ull, // AddNoticeInfo
            0x140015f90ull, // ArrayBaseFree
            0x141cd7720ull, // BeginSoundSystem
            0x140da2980ull, // CallImpl
            0x140da2f70ull, // CallWithRadioType
            0x142285960ull, // CassettePlayerVtable
            0x140ef8870ull, // CassetteStart
            0x1414dffb0ull, // CheckSightNoticeHostage
            0x140d68480ull, // ConvertRadioTypeToLabel
            0x140fb8910ull, // CopyAndAdjustInfo
            0x140d6e960ull, // DecrementPhaseCounter
            0x140a19730ull, // ExecCallback
            0x14006b8c0ull, // FoxLuaRegisterLibrary
            0x140085700ull, // FoxPath_Path
            0x1400234c0ull, // FoxStrHash32
            0x141a098b0ull, // FoxStrHash64
            0x140887840ull, // GameOverSetVisible
            0x140911210ull, // GetCurrentMissionCode
            0x147909c90ull, // GetNameIdWithGameObjectId
            0x147922a40ull, // GetGameObjectIdWithIndex
            0x140973280ull, // GetPlayingTime
            0x140973360ull, // GetPlayingTrackId
            0x140bff050ull, // GetQuarkSystemTable
            0x1409738c0ull, // GetTrackInfoByName
            0x14050b580ull, // GetUixUtilityToFeedQuarkEnvironment
            0x1404d27b0ull, // GetVoiceLanguage
            0x140da3110ull, // GetVoiceParamWithCallSign
            0x140015fc0ull, // KernelAllocAligned
            0x140ae9ac0ull, // LoadPlayerVoiceFpk
            0x14089aaa0ull, // LoadingScreenOrGameOverSplash2
            0x142bffae8ull, // MusicManager_s_instance
            0x141a098e0ull, // PathHashCode
            0x140973970ull, // PauseMusicPlayer
            0x140ef6600ull, // PlayOrPauseSelectedTrack
            0x140a69760ull, // RequestCorpse
            0x1409746e0ull, // ResumeMusicPlayer
            0x140918030ull, // SetEquipBackgroundTexture
            0x1408D81F0ull, // SetLuaFunctions
            0x141DC7CE0ull, // SetTextureName
            0x140989E10ull, // SoundSystemCtor
            0x140D69000ull, // StateRadio
            0x14154FD00ull, // StateRadioRequest
            0x1414B7AC0ull, // State_ComradeAction
            0x14147F6C0ull, // State_EnterDownHoldup
            0x14147F830ull, // State_EnterStandHoldup1
            0x14147FB90ull, // State_EnterStandHoldupUnarmed
            0x1414BB4D0ull, // State_RecoveryKick
            0x1414BBDC0ull, // State_RecoveryTouch
            0x1414BB680ull, // State_StandEnterRecoverySleepFaintHoldupComradeBySound
            0x14147FD30ull, // State_StandHoldupCancelLookToPlayer
            0x1414BB8E0ull, // State_StandRecoveryHoldup
            0x14150EA20ull, // StepRadioDiscovery
            0x140976100ull, // StopMusicPlayer
            0x1404D2470ull, // SubtitleManager_Get
            0x14137FD70ull, // UpdateOptCamo
            0x142C00A10ull, // g_SoundSystem
            0x141A111E0ull, // lua_getfield
            0x141A112E0ull, // lua_gettop
            0x141A11410ull, // lua_isnumber
            0x141A11440ull, // lua_isstring
            0x141A11640ull, // lua_objlen
            0x141A11750ull, // lua_pushboolean
            0x141A11950ull, // lua_pushnumber
            0x141A11AE0ull, // lua_rawgeti
            0x141A11F70ull, // lua_settop
            0x141A120C0ull, // lua_toboolean
            0x141A12120ull, // lua_tointeger
            0x141A12150ull, // lua_tolstring
            0x141A121F0ull, // lua_tonumber
            0x141A12300ull, // lua_type
            0x141A11970ull, // lua_pushstring
            0x141A10E80ull, // lua_createtable
            0x141A11B20ull, // lua_rawset
            0x141A11F40ull, // lua_settable
            0x141A11930ull, // lua_pushnil
            0x141A11600ull, // lua_next
            0x141A112B0ull, // lua_gettable
            0x141A119D0ull, // lua_pushvalue
            0x141A116C0ull, // lua_pcall
            0x141A11770ull, // lua_pushcclosure
            0x140912330ull, // GetIconFtexPath
            0x14089A700ull, // LoadingTipsEv_UpdateActPhase
            0x14033D090ull, // AK_SoundEngine_SetRTPCValue
            0x14032A7A0ull, // Fox_Sd_ConvertParameterID
            0x14033E280ull, // Fox_Sd_Ad_AudioSoundEngine_RegisterGameObject
            0x14032A9F0ull, // Fox_Sd_Object_Activate
            0x140329640ull, // Fox_Sd_Daemon_GetObject
            0x142B9E8B0ull, // Fox_Sd_Daemon_Singleton
            0x140B07D40ull, // SoundControllerImpl_CallInternal
            0x1412CC76Eull, // TornadoDualPatch
            0x140B4DFE0ull, // RealizedSahelan2Impl_Realize
            0x140B4E0A0ull, // RealizedSahelan2Impl_SetFovaImpl
            0x1404E2190ull, // FormVariationFile2_ApplyOnlyMeshAndTextureVariation
            0x1418C3CF0ull, // Sahelan_ActionCoreImpl_SetEyeLampColor
            0x1418C51B0ull, // Sahelan_ActionCoreImpl_UpdateEyeLampColor
            0x1418C5410ull, // Sahelan_ActionCoreImpl_UpdateHeartLight
            0x1418FFEB0ull, // Sahelan_PhaseSneakAi_PushEyeColor
            0x142B40000ull, // Sahelan_EyeMeshHashTable
            0x142C69A60ull, // Sahelan_PhaseSneakAi_ColorTableBase
            0x140EF28D0ull, // MbDvcAnnouncePopupCallbackImpl_UpdateAnnounceNormal
            0x140EF2C90ull, // MbDvcAnnouncePopupCallbackImpl_UpdateAnnounceServer
            0x140EF26B0ull, // MbDvcAnnouncePopupCallbackImpl_UpdateAnnounceEmergency
            0x140866910ull, // HudCommonDataManager_GetInstance
            0x1408685D0ull, // HudCommonDataManager_SetPopupType
            0x1408684F0ull, // HudCommonDataManager_SetPopupText
            0x140868190ull, // HudCommonDataManager_SetPopupErrorType
            0x140869180ull, // HudCommonDataManager_StartPopup
            0x14158B000ull, // Soldier2SoundController_GetVoiceTypeFromSoldierTypeImpl
            0x14158A290ull, // Soldier2SoundController_Activate
            0x1403B5370ull, // CAkResampler_SetPitch
            0x14033B9D0ull, // FNVHash32
            0x1408873BEull, // Play_bgm_gameover
            0x1408873C5ull, // Play_bgm_gameover_paradox
            0x1408873B7ull, // Play_bgm_gameover_perfectstealth
            0x1408873CCull, // Play_bgm_s10010_gameover
            0x140887F06ull, // Stop_bgm_gameover
            0x140887F17ull, // Stop_bgm_gameover_paradox
            0x140887F0Bull, // Stop_bgm_gameover_perfectstealth
            0x140887F1Eull, // Stop_bgm_s10010_gameover
            0x14226BF58ull, // Play_bgm_gameover_paradox_soundId
            0x14226BF5Cull, // Stop_bgm_gameover_paradox_soundId
            0x140E24D7Full, // DD_vox_SH_voice
            0x140E24CF2ull, // DD_vox_SH_radio
            0x140E24D6Full, // DD_vox_SH_radio2
            0x140E24D77ull, // DD_vox_SH_radio3
            0x140922910ull, // MotherBaseMapCommonDataImpl_GetEnemyInformationLangId
            0x1415E3E60ull, // TppUIBinoSubjectiveImpl_GetEnemyUnitName
            0x1410A8BE0ull, // BasicActionImpl_StateCrawlSideRoll
            0x1418FF890ull, // Sahelan_PhaseSneakAiImpl_PreUpdate
            0x142C69AB0ull, // Sahelan_PhaseSneakAiImpl_StepFuncsTable
            0x142C1FD44ull, // MessageResendCounter
            0x140912F20ull, // GetMissionCodeCategory
            0x1408DF990ull, // TppUiCommand_ShowMissionIcon
            0x1416201DCull, // IconTitleHashImm
            0x1416201EBull, // IconTitleGetLangTextCall
            0x140BFA380ull, // GameObject_SendCommand
            0x141DAD5F0ull, // ModelNode_UpdateModelNodeParameter
            0x140FE7660ull, // UiControllerImpl_InitEquipHudData
            0x1414E5030ull, // NoticeControllerImpl_GetOccasionalChat
            0x140D83470ull, // SoldierConversationService_ConvertSpeechLabelToConversationType
            0x1414E5149ull, // OccasionalChat_FactionTestNop
            0x140933630ull, // MbDvcReserveAnnouncePopup
            0x14093A940ull, // MbDvcPopupGateFn
            0x1415136B0ull, // NoticeIndisAiImpl_StepCallHelp
            0x14151FFF0ull, // NoticeNoiseAiImpl_StepCallHelp
            0x1415220E0ull, // NoticeNoiseAiImpl_StepResponse
            0x14151F510ull, // NoticeNoiseAiImpl_StepAware
            0x141512CD0ull, // NoticeIndisAiImpl_StepAware
            0x1415154A0ull, // NoticeIndisAiImpl_StepResponse
            0x1414E4750ull, // NoticeControllerImpl_DoCheckSpreadNotice
            0x1414E1F90ull, // NoticeControllerImpl_CheckSightNoticeSoldier
            0x140B15E90ull, // RealizedSoldier2Impl_ConvertHeadEquipModelType
            0x140B191C0ull, // RealizedSoldier2Impl_UpdateHeadEquipMesh
            0x140B1B0E0ull, // FovaController_GetActiveFovaResourceManager
            0x1404E6B50ull, // Fv2ResourceManager_GetModel
            0x1412B13D0ull, // TimeCigaretteActionPluginImpl_ShowTimeCigaretteUi
            0x1408E66D0ull, // HeliTaxi_CanHeliTaxi
            0x140F0D330ull, // HeliTaxi_CallRescueHeli
            0x140E0D3A0ull, // HeliTaxi_StepWithdraw
            0x1409111D0ull, // HeliTaxi_GetLocationId
            0x140874F20ull, // HeliTaxi_RequestMapPhase
            0x140E09CF0ull, // HeliTaxi_StepGoToNav
            0x140E0A900ull, // HeliTaxi_StepTaxiCurrentCluster
            0x140E1B010ull, // HeliTaxi_PassengerUpdate
            0x140E14990ull, // MechaActionImpl_StateOff
            0x140E14DD0ull, // MechaActionImpl_StateOn
            0x140E1AB40ull, // PassengerControllerImpl_IsPassengerClosingDoor
            0x140A0E300ull, // PlacedSystemImpl_BindResource
            0x1409361C0ull, // UiMarkerCommonDataImpl_RegisterLZMarkerInUpdate
            0x140E24930ull, // HeliSoundControllerImpl_Update
            0x140E23B80ull, // HeliSoundControllerImpl_CallVoice
            0x140DFD830ull, // HeliFlightControllerImpl_Update
            0x140865A70ull, // Hud_GetAnnounceLogSE
            0x140AB7170ull, // Ui_GetStringId
            0x1408A35C0ull, // Hud_TypingLogActUpdate
            0x141DCA2B0ull, // Ui_SoundControlStart
            0x14085FD00ull, // Ui_UiCommonDataManagerGetInstance
            0x1404B27B0ull, // Ui_EventNodeBodyGetGraphState
            0x140928950ull, // VoiceParam_PlayDialogue
            0x1408CCBF0ull, // Ui_AnnounceLogViewLangId
            0x14121D490ull, // RideHeliActionPluginImpl_ExecPreMotionGraph
            0x14121E180ull, // RideHeliActionPluginImpl_GetStateFn
            0x142BFF980ull, // SoundDaemon_Instance
            0x140971410ull, // SoundDaemon_PostEventQueue
            0x1420A72F0ull, // Sd_kW_Select
            0x140d7c770ull, // UpdateAntiAir
            0x140d769b0ull, // ClearAntiAir
            0x140926730ull, // GetChangeLocationMenuParameterByLocationId
            0x140926860ull, // GetMbFreeChangeLocationMenuParameter
            0x140926940ull, // GetPhotoAdditionalTextLangId
            0x140fe75a0ull, // UiControllerImpl_HideBinocle
        };

        return value;
    }

    const AddressSet& Get_mst_jp_day3800_AddressSet()   // 1.0.15.4 japanese
    {
        static const AddressSet value =
        {
            0x14147DF90ull, // AddNoise
            0x1414DBA90ull, // AddNoticeInfo
            0x140015FA0ull, // ArrayBaseFree
            0x141CD7610ull, // BeginSoundSystem
            0x140DA2940ull, // CallImpl
            0x140DA2F30ull, // CallWithRadioType
            0x142285920ull, // CassettePlayerVtable
            0x140EF88A0ull, // CassetteStart
            0x1414DFFC0ull, // CheckSightNoticeHostage
            0x140D68430ull, // ConvertRadioTypeToLabel
            0x140FB8960ull, // CopyAndAdjustInfo
            0x140D6E910ull, // DecrementPhaseCounter
            0x140A19550ull, // ExecCallback
            0x14006B920ull, // FoxLuaRegisterLibrary
            0x140085760ull, // FoxPath_Path
            0x1400234E0ull, // FoxStrHash32
            0x141A097F0ull, // FoxStrHash64
            0x140887790ull, // GameOverSetVisible
            0x1409110F0ull, // GetCurrentMissionCode
            0x140BF4390ull, // GetNameIdWithGameObjectId
            0x1479CBD40ull, // GetGameObjectIdWithIndex
            0x1409731D0ull, // GetPlayingTime
            0x1409732B0ull, // GetPlayingTrackId
            0x140BFEFD0ull, // GetQuarkSystemTable
            0x140973810ull, // GetTrackInfoByName
            0x14050B9D0ull, // GetUixUtilityToFeedQuarkEnvironment
            0x1404D2BD0ull, // GetVoiceLanguage
            0x140DA30D0ull, // GetVoiceParamWithCallSign
            0x140015FD0ull, // KernelAllocAligned
            0x140AE9A30ull, // LoadPlayerVoiceFpk
            0x14089A9C0ull, // LoadingScreenOrGameOverSplash2
            0x142BFFAE8ull, // MusicManager_s_instance
            0x141A09820ull, // PathHashCode
            0x1409738C0ull, // PauseMusicPlayer
            0x140EF6630ull, // PlayOrPauseSelectedTrack
            0x140A69640ull, // RequestCorpse
            0x140974630ull, // ResumeMusicPlayer
            0x140917F10ull, // SetEquipBackgroundTexture
            0x1408D8120ull, // SetLuaFunctions
            0x141DC7D40ull, // SetTextureName
            0x140989D50ull, // SoundSystemCtor
            0x140D68FB0ull, // StateRadio
            0x14154FD10ull, // StateRadioRequest
            0x1414B7AD0ull, // State_ComradeAction
            0x14147F6E0ull, // State_EnterDownHoldup
            0x14147F850ull, // State_EnterStandHoldup1
            0x14147FBB0ull, // State_EnterStandHoldupUnarmed
            0x1414BB4E0ull, // State_RecoveryKick
            0x1414BBDD0ull, // State_RecoveryTouch
            0x1414BB690ull, // State_StandEnterRecoverySleepFaintHoldupComradeBySound
            0x14147FD50ull, // State_StandHoldupCancelLookToPlayer
            0x1414BB8F0ull, // State_StandRecoveryHoldup
            0x14150EA30ull, // StepRadioDiscovery
            0x140976050ull, // StopMusicPlayer
            0x1404D2880ull, // SubtitleManager_Get
            0x14137FD80ull, // UpdateOptCamo
            0x142C00A10ull, // g_SoundSystem
            0x141A11120ull, // lua_getfield
            0x141A11220ull, // lua_gettop
            0x141A11350ull, // lua_isnumber
            0x141A11380ull, // lua_isstring
            0x141A11580ull, // lua_objlen
            0x141A11690ull, // lua_pushboolean
            0x141A11890ull, // lua_pushnumber
            0x141A11A20ull, // lua_rawgeti
            0x141A11EB0ull, // lua_settop
            0x141A12010ull, // lua_toboolean
            0x141A12070ull, // lua_tointeger
            0x141A120A0ull, // lua_tolstring
            0x141A12140ull, // lua_tonumber
            0x141A12250ull, // lua_type
            0x141A118B0ull, // lua_pushstring
            0x141A10DC0ull, // lua_createtable
            0x141A11A60ull, // lua_rawset
            0x141A11E80ull, // lua_settable
            0x141A11870ull, // lua_pushnil
            0x141A11540ull, // lua_next
            0x141A111F0ull, // lua_gettable
            0x141A11910ull, // lua_pushvalue
            0x141A11600ull, // lua_pcall
            0x141A116B0ull, // lua_pushcclosure
            0x140912210ull, // GetIconFtexPath
            0x14089A620ull, // LoadingTipsEv_UpdateActPhase
            0x14033D670ull, // AK_SoundEngine_SetRTPCValue
            0x14032AD50ull, // Fox_Sd_ConvertParameterID
            0x14033E860ull, // Fox_Sd_Ad_AudioSoundEngine_RegisterGameObject
            0x14032AFA0ull, // Fox_Sd_Object_Activate
            0x140329BF0ull, // Fox_Sd_Daemon_GetObject
            0x142B9E8B0ull, // Fox_Sd_Daemon_Singleton
            0x140B07CA0ull, // SoundControllerImpl_CallInternal
            0x1412CC7AEull, // TornadoDualPatch
            0x140B4E110ull, // RealizedSahelan2Impl_Realize
            0x140B4E1D0ull, // RealizedSahelan2Impl_SetFovaImpl
            0x1404E25F0ull, // FormVariationFile2_ApplyOnlyMeshAndTextureVariation
            0x1418C3BF0ull, // Sahelan_ActionCoreImpl_SetEyeLampColor
            0x1418C50B0ull, // Sahelan_ActionCoreImpl_UpdateEyeLampColor
            0x1418C5310ull, // Sahelan_ActionCoreImpl_UpdateHeartLight
            0x1418FFDD0ull, // Sahelan_PhaseSneakAi_PushEyeColor
            0x142B40000ull, // Sahelan_EyeMeshHashTable
            0x142C69A60ull, // Sahelan_PhaseSneakAi_ColorTableBase
            0x140EF2900ull, // MbDvcAnnouncePopupCallbackImpl_UpdateAnnounceNormal
            0x140EF2CC0ull, // MbDvcAnnouncePopupCallbackImpl_UpdateAnnounceServer
            0x140EF26E0ull, // MbDvcAnnouncePopupCallbackImpl_UpdateAnnounceEmergency
            0x1408669B0ull, // HudCommonDataManager_GetInstance
            0x140868680ull, // HudCommonDataManager_SetPopupType
            0x1408685A0ull, // HudCommonDataManager_SetPopupText
            0x140868240ull, // HudCommonDataManager_SetPopupErrorType
            0x140869230ull, // HudCommonDataManager_StartPopup
            0x14158B010ull, // Soldier2SoundController_GetVoiceTypeFromSoldierTypeImpl
            0x14158A2A0ull, // Soldier2SoundController_Activate
            0x1403B5970ull, // CAkResampler_SetPitch
            0x14033BFB0ull, // FNVHash32
            0x14088730Eull, // Play_bgm_gameover
            0x140887315ull, // Play_bgm_gameover_paradox
            0x140887307ull, // Play_bgm_gameover_perfectstealth
            0x14088731Cull, // Play_bgm_s10010_gameover
            0x140887E56ull, // Stop_bgm_gameover
            0x140887E67ull, // Stop_bgm_gameover_paradox
            0x140887E5Bull, // Stop_bgm_gameover_perfectstealth
            0x140887E6Eull, // Stop_bgm_s10010_gameover
            0x14226BED8ull, // Play_bgm_gameover_paradox_soundId
            0x14226BEDCull, // Stop_bgm_gameover_paradox_soundId
            0x140E24DAFull, // DD_vox_SH_voice
            0x140E24D22ull, // DD_vox_SH_radio
            0x140E24D9Full, // DD_vox_SH_radio2
            0x140E24DA7ull, // DD_vox_SH_radio3
            0x140922820ull, // MotherBaseMapCommonDataImpl_GetEnemyInformationLangId
            0x1415E3E70ull, // TppUIBinoSubjectiveImpl_GetEnemyUnitName
            0x1410A8C50ull, // BasicActionImpl_StateCrawlSideRoll
            0x1418FF7B0ull, // Sahelan_PhaseSneakAiImpl_PreUpdate
            0x142C69AB0ull, // Sahelan_PhaseSneakAiImpl_StepFuncsTable
            0x142C1FD44ull, // MessageResendCounter
            0x140912E00ull, // GetMissionCodeCategory
            0x1408DF8C0ull, // TppUiCommand_ShowMissionIcon
            0x1416201CAull, // IconTitleHashImm
            0x1416201DBull, // IconTitleGetLangTextCall
            0x140BFA330ull, // GameObject_SendCommand
            0x141DAD650ull, // ModelNode_UpdateModelNodeParameter
            0x140FE7690ull, // UiControllerImpl_InitEquipHudData
            0x1414E5040ull, // NoticeControllerImpl_GetOccasionalChat
            0x140D83420ull, // SoldierConversationService_ConvertSpeechLabelToConversationType
            0x1414E5159ull, // OccasionalChat_FactionTestNop
            0x140933510ull, // MbDvcReserveAnnouncePopup
            0x14093A820ull, // MbDvcPopupGateFn
            0x1415139E0ull, // NoticeIndisAiImpl_StepCallHelp
            0x141520000ull, // NoticeNoiseAiImpl_StepCallHelp
            0x1415220F0ull, // NoticeNoiseAiImpl_StepResponse
            0x14151F520ull, // NoticeNoiseAiImpl_StepAware
            0x141513000ull, // NoticeIndisAiImpl_StepAware
            0x1415157D0ull, // NoticeIndisAiImpl_StepResponse
            0x1414E4760ull, // NoticeControllerImpl_DoCheckSpreadNotice
            0x1414E1FA0ull, // NoticeControllerImpl_CheckSightNoticeSoldier
            0x140B15E00ull, // RealizedSoldier2Impl_ConvertHeadEquipModelType
            0x140B19130ull, // RealizedSoldier2Impl_UpdateHeadEquipMesh
            0x140B1B050ull, // FovaController_GetActiveFovaResourceManager
            0x1404E6F40ull, // Fv2ResourceManager_GetModel
            0x1412B1410ull, // TimeCigaretteActionPluginImpl_ShowTimeCigaretteUi
            0x1408E6600ull, // HeliTaxi_CanHeliTaxi
            0x140F0D360ull, // HeliTaxi_CallRescueHeli
            0x140E0D3F0ull, // HeliTaxi_StepWithdraw
            0x140911110ull, // HeliTaxi_GetLocationId
            0x140874E70ull, // HeliTaxi_RequestMapPhase
            0x140E09D40ull, // HeliTaxi_StepGoToNav
            0x140E0A950ull, // HeliTaxi_StepTaxiCurrentCluster
            0x140E1B050ull, // HeliTaxi_PassengerUpdate
            0x140E149E0ull, // MechaActionImpl_StateOff
            0x140E14E20ull, // MechaActionImpl_StateOn
            0x140E1AB80ull, // PassengerControllerImpl_IsPassengerClosingDoor
            0x140A0E120ull, // PlacedSystemImpl_BindResource
            0x1409360A0ull, // UiMarkerCommonDataImpl_RegisterLZMarkerInUpdate
            0x140E24960ull, // HeliSoundControllerImpl_Update
            0x140E23BB0ull, // HeliSoundControllerImpl_CallVoice
            0x140DFD880ull, // HeliFlightControllerImpl_Update
            0x140865B10ull, // Hud_GetAnnounceLogSE
            0x140AB7110ull, // Ui_GetStringId
            0x1408A34F0ull, // Hud_TypingLogActUpdate
            0x141DCA310ull, // Ui_SoundControlStart
            0x14085FD80ull, // Ui_UiCommonDataManagerGetInstance
            0x1407D2220ull, // Ui_EventNodeBodyGetGraphState
            0x140928850ull, // VoiceParam_PlayDialogue
            0x1408CCB20ull, // Ui_AnnounceLogViewLangId
            0x14121D4C0ull, // RideHeliActionPluginImpl_ExecPreMotionGraph
            0x14121E1B0ull, // RideHeliActionPluginImpl_GetStateFn
            0x142BFF980ull, // SoundDaemon_Instance
            0x140971360ull, // SoundDaemon_PostEventQueue
            0x1420A72F0ull, // Sd_kW_Select
            0x140d7c720ull, // UpdateAntiAir
            0x140d76960ull, // ClearAntiAir
            0x140926640ull, // GetChangeLocationMenuParameterByLocationId
            0x140926770ull, // GetMbFreeChangeLocationMenuParameter
            0x140926840ull, // GetPhotoAdditionalTextLangId
            0x140fe75d0ull, // UiControllerImpl_HideBinocle
        };
        return value;
    }

    GameBuild DetectGameBuildFromVersionInfo(HMODULE hGame)
    {
        const std::wstring dir = GetModuleDirectory(hGame ? hGame : GetModuleHandleW(nullptr));
        if (dir.empty())
            return GameBuild::Unknown;

        const std::wstring versionInfoPath = dir + L"\\version_info.txt";
        std::string text = ReadWholeFileUtf8OrAnsi(versionInfoPath);
        if (text.empty())
        {
            Log("[AddressSet] Failed to read version_info.txt, defaulting to EN 1.0.15.4.\n");
            return GameBuild::En_1_0_15_4;
        }

        text = ToLowerAscii(text);
        Log("[AddressSet] version_info.txt = %s\n", text.c_str());

        const bool jp   = text.find("mst_jp") != std::string::npos;
        const bool prev = text.find("day1820") != std::string::npos;   // 1.0.15.3
        if (prev)
            return jp ? GameBuild::Jp_1_0_15_3 : GameBuild::En_1_0_15_3;
        return jp ? GameBuild::Jp_1_0_15_4 : GameBuild::En_1_0_15_4;    // day3800 / default = current
    }

    bool ResolveAddressSet(HMODULE hGame)
    {
        if (!hGame)
            return false;

        GetGameBuild() = DetectGameBuildFromVersionInfo(hGame);

        switch (GetGameBuild())
        {
        case GameBuild::En_1_0_15_3: GetAddressSet() = Get_mst_en_day1820_AddressSet(); break;
        case GameBuild::Jp_1_0_15_3: GetAddressSet() = Get_mst_jp_day1820_AddressSet();     break;
        case GameBuild::En_1_0_15_4: GetAddressSet() = Get_mst_en_day3800_AddressSet();       break;
        case GameBuild::Jp_1_0_15_4: GetAddressSet() = Get_mst_jp_day3800_AddressSet();  break;
        default:                     GetAddressSet() = Get_mst_en_day3800_AddressSet();        break;
        }
        Log("[AddressSet] Selected %s address set.\n", GetGameBuildName(GetGameBuild()));
        return true;
    }
}
