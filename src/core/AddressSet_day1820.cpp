#include "pch.h"

#include "AddressSet.h"

namespace AddressSetRuntime
{
    const AddressSet& Get_mst_en_day1820_AddressSet()   // 1.0.15.3 english
    {
        static const AddressSet value =
        {
            0x14147F240ull, // AddNoise
            0x1414DCB60ull, // AddNoticeInfo
            0x140015EF0ull, // ArrayBaseFree
            0x140989340ull, // BeginSoundSystem
            0x1473CFCD0ull, // CallImpl
            0x1473CFF10ull, // CallWithRadioType
            0x142285780ull, // CassettePlayerVtable
            0x149310440ull, // CassetteStart
            0x1414E1090ull, // CheckSightNoticeHostage
            0x140D685C0ull, // ConvertRadioTypeToLabel
            0x140FB9000ull, // CopyAndAdjustInfo
            0x140D6EAA0ull, // DecrementPhaseCounter
            0x140A19030ull, // ExecCallback
            0x14006B6D0ull, // FoxLuaRegisterLibrary
            0x1400855B0ull, // FoxPath_Path
            0x142ECE7F0ull, // FoxStrHash32
            0x14C1BD310ull, // FoxStrHash64
            0x145CB8890ull, // GameOverSetVisible
            0x145E5EE70ull, // GetCurrentMissionCode
            0x146C98180ull, // GetNameIdWithGameObjectId
            0x146C96520ull, // GetGameObjectIdWithIndex
            0x14614A4E0ull, // GetPlayingTime
            0x14614AA30ull, // GetPlayingTrackId
            0x140BFF3F0ull, // GetQuarkSystemTable
            0x14614C0C0ull, // GetTrackInfoByName
            0x14050B2A0ull, // GetUixUtilityToFeedQuarkEnvironment
            0x1404D2AD0ull, // GetVoiceLanguage
            0x140DA3170ull, // GetVoiceParamWithCallSign
            0x140015F20ull, // KernelAllocAligned
            0x146867240ull, // LoadPlayerVoiceFpk
            0x145CD0630ull, // LoadingScreenOrGameOverSplash2
            0x142BFFAC8ull, // MusicManager_s_instance
            0x14C1BD5D0ull, // PathHashCode
            0x140972C70ull, // PauseMusicPlayer
            0x140EF6BD0ull, // PlayOrPauseSelectedTrack
            0x140A69070ull, // RequestCorpse
            0x1409739E0ull, // ResumeMusicPlayer
            0x145F236F0ull, // SetEquipBackgroundTexture
            0x1408D78A0ull, // SetLuaFunctions
            0x141DC78F0ull, // SetTextureName
            0x140989120ull, // SoundSystemCtor
            0x140D69140ull, // StateRadio
            0x14A2ACC00ull, // StateRadioRequest
            0x1414B8D20ull, // State_ComradeAction
            0x14A140940ull, // State_EnterDownHoldup
            0x14A140C00ull, // State_EnterStandHoldup1
            0x14A141500ull, // State_EnterStandHoldupUnarmed
            0x1414BC600ull, // State_RecoveryKick
            0x1414BCEF0ull, // State_RecoveryTouch
            0x1414BC7B0ull, // State_StandEnterRecoverySleepFaintHoldupComradeBySound
            0x14A141910ull, // State_StandHoldupCancelLookToPlayer
            0x1414BCA10ull, // State_StandRecoveryHoldup
            0x14150F2C0ull, // StepRadioDiscovery
            0x146150970ull, // StopMusicPlayer
            0x1404D2770ull, // SubtitleManager_Get
            0x149F65330ull, // UpdateOptCamo
            0x142C009F0ull, // g_SoundSystem
            0x14C1D7320ull, // lua_getfield
            0x14C1D7D40ull, // lua_gettop
            0x14C1D8C90ull, // lua_isnumber
            0x14C1D9250ull, // lua_isstring
            0x14C1DA960ull, // lua_objlen
            0x14C1DB230ull, // lua_pushboolean
            0x141A11BC0ull, // lua_pushnumber
            0x14C1E9320ull, // lua_rawgeti
            0x14C1EBBE0ull, // lua_settop
            0x141A12330ull, // lua_toboolean
            0x141A12390ull, // lua_tointeger
            0x141A123C0ull, // lua_tolstring
            0x141A12460ull, // lua_tonumber
            0x14C1ED760ull, // lua_type
            0x14C1E7EE0ull, // lua_pushstring
            0x14C1D6320ull, // lua_createtable
            0x14C1E9CF0ull, // lua_rawset
            0x14C1EB2B0ull, // lua_settable
            0x14C1E7CC0ull, // lua_pushnil
            0x14C1DA770ull, // lua_next
            0x14C1D7C10ull, // lua_gettable
            0x14C1E87E0ull, // lua_pushvalue
            0x141A11930ull, // lua_pcall
            0x14C1E67B0ull, // lua_pushcclosure
            0x145E62540ull, // GetIconFtexPath
            0x145CCFCC0ull, // LoadingTipsEv_UpdateActPhase
            0x14033d520ull, // AK_SoundEngine_SetRTPCValue
            0x14032ADF0ull, // Fox_Sd_ConvertParameterID
            0x143F42540ull, // Fox_Sd_Ad_AudioSoundEngine_RegisterGameObject
            0x14032B040ull, // Fox_Sd_Object_Activate
            0x140329C80ull, // Fox_Sd_Daemon_GetObject
            0x142B9E8B0ull, // Fox_Sd_Daemon_Singleton
            0x1468EDD50ull, // SoundControllerImpl_CallInternal
            0x149CFBA54ull, // TornadoDualPatch
            0x146ACC210ull, // RealizedSahelan2Impl_Realize
            0x146ACC650ull, // RealizedSahelan2Impl_SetFovaImpl
            0x144A3CBD0ull, // FormVariationFile2_ApplyOnlyMeshAndTextureVariation
            0x14BBC1B10ull, // Sahelan_ActionCoreImpl_SetEyeLampColor
            0x14BBC3630ull, // Sahelan_ActionCoreImpl_UpdateEyeLampColor
            0x14BBC37A0ull, // Sahelan_ActionCoreImpl_UpdateHeartLight
            0x14BCA4b70ull, // Sahelan_PhaseSneakAi_PushEyeColor
            0x142B40000ull, // Sahelan_EyeMeshHashTable
            0x142C69A50ull, // Sahelan_PhaseSneakAi_ColorTableBase
            0x140EF2EE0ull, // MbDvcAnnouncePopupCallbackImpl_UpdateAnnounceNormal
            0x140EF32A0ull, // MbDvcAnnouncePopupCallbackImpl_UpdateAnnounceServer
            0x140EF2CC0ull, // MbDvcAnnouncePopupCallbackImpl_UpdateAnnounceEmergency
            0x145C0A890ull, // HudCommonDataManager_GetInstance
            0x1408679B0ull, // HudCommonDataManager_SetPopupType
            0x1408678D0ull, // HudCommonDataManager_SetPopupText
            0x140867570ull, // HudCommonDataManager_SetPopupErrorType
            0x147732010ull, // HudCommonDataManager_StartPopup
            0x14158C290ull, // Soldier2SoundController_GetVoiceTypeFromSoldierTypeImpl
            0x14158B520ull, // Soldier2SoundController_Activate
            0x1441DBB80ull, // CAkResampler_SetPitch
            0x143F33A20ull, // FNVHash32
            0x145CB70BDull, // Play_bgm_gameover
            0x145CB70C4ull, // Play_bgm_gameover_paradox
            0x145CB70B6ull, // Play_bgm_gameover_perfectstealth
            0x145CB70CBull, // Play_bgm_s10010_gameover
            0x145CB8EF5ull, // Stop_bgm_gameover
            0x145CB8F06ull, // Stop_bgm_gameover_paradox
            0x145CB8EFAull, // Stop_bgm_gameover_perfectstealth
            0x145CB8F0Dull, // Stop_bgm_s10010_gameover
            0x14226bFC8ull, // Play_bgm_gameover_paradox_soundId
            0x14226BFCCull, // Stop_bgm_gameover_paradox_soundId
            0x140e2470full, // DD_vox_SH_voice
            0x140e24682ull, // DD_vox_SH_radio
            0x140e246ffull, // DD_vox_SH_radio2
            0x140e24707ull, // DD_vox_SH_radio3
            0x140921EC0ull, // MotherBaseMapCommonDataImpl_GetEnemyInformationLangId
            0x1415E4FE0ull, // TppUIBinoSubjectiveImpl_GetEnemyUnitName
            0x1410A9520ull, // BasicActionImpl_StateCrawlSideRoll
            0x1418FF8D0ull, // Sahelan_PhaseSneakAiImpl_PreUpdate
            0x142C69AA0ull, // Sahelan_PhaseSneakAiImpl_StepFuncsTable
            0x142C1fD24ull, // MessageResendCounter
            0x145e642a0ull, // GetMissionCodeCategory
            0x145DADFC1ull, // TppUiCommand_ShowMissionIcon
            0x14162135Cull, // IconTitleHashImm
            0x14162136Bull, // IconTitleGetLangTextCall
            0x146CC7170ull, // GameObject_SendCommand
            0x14D71DB50ull, // ModelNode_UpdateModelNodeParameter
            0x1496A8070ull, // UiControllerImpl_InitEquipHudData
            0x1414E6110ull, // NoticeControllerImpl_GetOccasionalChat
            0x140D83480ull, // SoldierConversationService_ConvertSpeechLabelToConversationType
            0x1414E6229ull, // OccasionalChat_FactionTestNop
            0x140932BE0ull, // MbDvcReserveAnnouncePopup
            0x140939EE0ull, // MbDvcPopupGateFn
            0x1415144E0ull, // NoticeIndisAiImpl_StepCallHelp
            0x141520910ull, // NoticeNoiseAiImpl_StepCallHelp
            0x141522A00ull, // NoticeNoiseAiImpl_StepResponse
            0x14151FE30ull, // NoticeNoiseAiImpl_StepAware
            0x141513B00ull, // NoticeIndisAiImpl_StepAware
            0x1415162D0ull, // NoticeIndisAiImpl_StepResponse
            0x1414E5830ull, // NoticeControllerImpl_DoCheckSpreadNotice
            0x1414E3070ull, // NoticeControllerImpl_CheckSightNoticeSoldier
            0x140B15B00ull, // RealizedSoldier2Impl_ConvertHeadEquipModelType
            0x140B18ED0ull, // RealizedSoldier2Impl_UpdateHeadEquipMesh
            0x140B1AE60ull, // FovaController_GetActiveFovaResourceManager
            0x144A49BF0ull, // Fv2ResourceManager_GetModel
            0x149CD7DD0ull, // TimeCigaretteActionPluginImpl_ShowTimeCigaretteUi
            0x1408E5DB0ull, // HeliTaxi_CanHeliTaxi
            0x140F0D8D0ull, // HeliTaxi_CallRescueHeli
            0x140E0CD90ull, // HeliTaxi_StepWithdraw
            0x140910890ull, // HeliTaxi_GetLocationId
            0x145C468F0ull, // HeliTaxi_RequestMapPhase
            0x140E096E0ull, // HeliTaxi_StepGoToNav
            0x140E0A2F0ull, // HeliTaxi_StepTaxiCurrentCluster
            0x148F8CB80ull, // HeliTaxi_PassengerUpdate
            0x140E143C0ull, // MechaActionImpl_StateOff
            0x140E14800ull, // MechaActionImpl_StateOn
            0x148F8A4A0ull, // PassengerControllerImpl_IsPassengerClosingDoor
            0x1463FC140ull, // PlacedSystemImpl_BindResource
            0x140935760ull, // UiMarkerCommonDataImpl_RegisterLZMarkerInUpdate
            0x140E242C0ull, // HeliSoundControllerImpl_Update
            0x148FABEA0ull, // HeliSoundControllerImpl_CallVoice
            0x140DFD240ull, // HeliFlightControllerImpl_Update
            0x145C096B0ull, // Hud_GetAnnounceLogSE
            0x1409137A0ull, // Ui_GetStringId
            0x1408A2EA0ull, // Hud_TypingLogActUpdate
            0x141DC9ED0ull, // Ui_SoundControlStart
            0x14085F0C0ull, // Ui_UiCommonDataManagerGetInstance
            0x14085F190ull, // Ui_EventNodeBodyGetGraphState
            0x145F804D0ull, // VoiceParam_PlayDialogue
            0x1408CC280ull, // Ui_AnnounceLogViewLangId
            0x149B603F0ull, // RideHeliActionPluginImpl_ExecPreMotionGraph
            0x14121EB90ull, // RideHeliActionPluginImpl_GetStateFn
            0x142BFF960ull, // SoundDaemon_Instance
            0x146142730ull, // SoundDaemon_PostEventQueue
            0x1420A72B0ull, // Sd_kW_Select
            0x1466A5770ull, // AddCassetteTapeTrack
            0x149309EA0ull, // CollectGotTapes
            0x1466EC350ull, // IsGotCassetteTapeTrack
            0x140AAC670ull, // SetCassetteTapeTrackNewFlag
            0x140EF7A50ull, // SetCurrentAlbum
            0x140974880ull, // SetupMusicInfos
        };
        return value;
    }
    const AddressSet& Get_mst_jp_day1820_AddressSet()   // 1.0.15.3 japanese
    {
        static const AddressSet value =
        {
            0x14147F210ull, // AddNoise
            0x1414DCB30ull, // AddNoticeInfo
            0x140015EE0ull, // ArrayBaseFree
            0x140988E20ull, // BeginSoundSystem
            0x149531DB0ull, // CallImpl
            0x149532190ull, // CallWithRadioType
            0x142285700ull, // CassettePlayerVtable
            0x149CD67B0ull, // CassetteStart
            0x1414E1060ull, // CheckSightNoticeHostage
            0x140D68330ull, // ConvertRadioTypeToLabel
            0x140FB90D0ull, // CopyAndAdjustInfo
            0x140D6E810ull, // DecrementPhaseCounter
            0x140A18AF0ull, // ExecCallback
            0x1431CC520ull, // FoxLuaRegisterLibrary
            0x1400856D0ull, // FoxPath_Path
            0x142EB6C10ull, // FoxStrHash32
            0x14C96BF00ull, // FoxStrHash64
            0x1477CFCB0ull, // GameOverSetVisible
            0x147A691E0ull, // GetCurrentMissionCode
            0x148A58CB0ull, // GetNameIdWithGameObjectId
            0x148A57620ull, // GetGameObjectIdWithIndex
            0x147DE8FA0ull, // GetPlayingTime
            0x147DE93E0ull, // GetPlayingTrackId
            0x140BFEF80ull, // GetQuarkSystemTable
            0x147DEA880ull, // GetTrackInfoByName
            0x14050ae60ull, // GetUixUtilityToFeedQuarkEnvironment
            0x1404D25C0ull, // GetVoiceLanguage
            0x140DA30D0ull, // GetVoiceParamWithCallSign
            0x140015F10ull, // KernelAllocAligned
            0x14844E550ull, // LoadPlayerVoiceFpk
            0x1477ED2F0ull, // LoadingScreenOrGameOverSplash2
            0x142BFFAC8ull, // MusicManager_s_instance
            0x14C96C160ull, // PathHashCode
            0x147DF6C00ull, // PauseMusicPlayer
            0x140EF6D40ull, // PlayOrPauseSelectedTrack
            0x140A68B60ull, // RequestCorpse
            0x147DFE3B0ull, // ResumeMusicPlayer
            0x147A8C170ull, // SetEquipBackgroundTexture
            0x1408D72B0ull, // SetLuaFunctions
            0x141DC7930ull, // SetTextureName
            0x140988C00ull, // SoundSystemCtor
            0x140D68EB0ull, // StateRadio
            0x14ACA5E60ull, // StateRadioRequest
            0x1414B8CF0ull, // State_ComradeAction
            0x14AB05B80ull, // State_EnterDownHoldup
            0x14AB05D90ull, // State_EnterStandHoldup1
            0x14AB06770ull, // State_EnterStandHoldupUnarmed
            0x1414BC5D0ull, // State_RecoveryKick
            0x1414BCEC0ull, // State_RecoveryTouch
            0x1414BC780ull, // State_StandEnterRecoverySleepFaintHoldupComradeBySound
            0x14AB06C40ull, // State_StandHoldupCancelLookToPlayer
            0x1414BC9E0ull, // State_StandRecoveryHoldup
            0x14150F290ull, // StepRadioDiscovery
            0x147DEF390ull, // StopMusicPlayer
            0x1404D2260ull, // SubtitleManager_Get
            0x14A96AF20ull, // UpdateOptCamo
            0x142C009F0ull, // g_SoundSystem
            0x14C987300ull, // lua_getfield
            0x14C987CB0ull, // lua_gettop
            0x14C988960ull, // lua_isnumber
            0x14C988CA0ull, // lua_isstring
            0x14C98A230ull, // lua_objlen
            0x14C98B310ull, // lua_pushboolean
            0x14C98D800ull, // lua_pushnumber
            0x14C98Ebc0ull, // lua_rawgeti
            0x14C990ED0ull, // lua_settop
            0x14C991120ull, // lua_toboolean
            0x14C991B80ull, // lua_tointeger
            0x14C992060ull, // lua_tolstring
            0x14C9924D0ull, // lua_tonumber
            0x14C9935F0ull, // lua_type
            0x14C98DCB0ull, // lua_pushstring
            0x14C986520ull, // lua_createtable
            0x14C98ED50ull, // lua_rawset
            0x14C990BD0ull, // lua_settable
            0x14C98D570ull, // lua_pushnil
            0x14C98A010ull, // lua_next
            0x14C987B90ull, // lua_gettable
            0x14C98E1D0ull, // lua_pushvalue
            0x141A11A50ull, // lua_pcall
            0x14C98C080ull, // lua_pushcclosure


            0x147A6BD40ull, // GetIconFtexPath
            0x1477EC6F0ull, // LoadingTipsEv_UpdateActPhase
            0x14033CFC0ull, // AK_SoundEngine_SetRTPCValue
            0x14032A870ull, // Fox_Sd_ConvertParameterID
            0x143F7BCA0ull, // Fox_Sd_Ad_AudioSoundEngine_RegisterGameObject
            0x14032AAC0ull, // Fox_Sd_Object_Activate
            0x140329710ull, // Fox_Sd_Daemon_GetObject
            0x142B9E8B0ull, // Fox_Sd_Daemon_Singleton
            0x1484D84E0ull, // SoundControllerImpl_CallInternal


            0x14A6C34B4ull, // TornadoDualPatch


            0x148655E70ull, // RealizedSahelan2Impl_Realize
            0x148656360ull, // RealizedSahelan2Impl_SetFovaImpl
            0x1448A0190ull, // FormVariationFile2_ApplyOnlyMeshAndTextureVariation
            0x14B77E550ull, // Sahelan_ActionCoreImpl_SetEyeLampColor
            0x14B7801D0ull, // Sahelan_ActionCoreImpl_UpdateEyeLampColor
            0x14B7807B0ull, // Sahelan_ActionCoreImpl_UpdateHeartLight
            0x14B84DEC0ull, // Sahelan_PhaseSneakAi_PushEyeColor
            0x142B40000ull, // Sahelan_EyeMeshHashTable
            0x142C69A50ull, // Sahelan_PhaseSneakAi_ColorTableBase


            0x140EF3050ull, // MbDvcAnnouncePopupCallbackImpl_UpdateAnnounceNormal
            0x140EF3410ull, // MbDvcAnnouncePopupCallbackImpl_UpdateAnnounceServer
            0x140EF2E30ull, // MbDvcAnnouncePopupCallbackImpl_UpdateAnnounceEmergency

            0x147719930ull, // HudCommonDataManager_GetInstance
            0x140867630ull, // HudCommonDataManager_SetPopupType
            0x140867550ull, // HudCommonDataManager_SetPopupText
            0x1408671F0ull, // HudCommonDataManager_SetPopupErrorType
            0x147732010ull, // HudCommonDataManager_StartPopup

            0x14158C270ull, // Soldier2SoundController_GetVoiceTypeFromSoldierTypeImpl
            0x14158B4F0ull, // Soldier2SoundController_Activate
            0x14418D980ull, // CAkResampler_SetPitch


            0x143F6EE50ull, // FNVHash32
            0x1477CD50Cull, // Play_bgm_gameover
            0x1477CD513ull, // Play_bgm_gameover_paradox
            0x1477CD505ull, // Play_bgm_gameover_perfectstealth
            0x1477CD51Aull, // Play_bgm_s10010_gameover
            0x1477D0274ull, // Stop_bgm_gameover
            0x1477D0285ull, // Stop_bgm_gameover_paradox
            0x1477D0279ull, // Stop_bgm_gameover_perfectstealth
            0x1477D028Cull, // Stop_bgm_s10010_gameover
            0x14226BF18ull, // Play_bgm_gameover_paradox_soundId
            0x14226BF1Cull, // Stop_bgm_gameover_paradox_soundId


            0x140E247DFull, // DD_vox_SH_voice
            0x140E24752ull, // DD_vox_SH_radio
            0x140E247CFull, // DD_vox_SH_radio2
            0x140E247D7ull, // DD_vox_SH_radio3

            0x1409218E0ull, // MotherBaseMapCommonDataImpl_GetEnemyInformationLangId
            0x1415E5150ull, // TppUIBinoSubjectiveImpl_GetEnemyUnitName

            0x1410A6CA0ull, // BasicActionImpl_StateCrawlSideRoll

            0x1418FFA10ull, // Sahelan_PhaseSneakAiImpl_PreUpdate
            0x142C69AA0ull, // Sahelan_PhaseSneakAiImpl_StepFuncsTable

            0x142C1FD24ull, // MessageResendCounter

            0x147a6cfe0ull, // GetMissionCodeCategory

            0x1478EB990ull, // TppUiCommand_ShowMissionIcon

            0x1416214aaull, // IconTitleHashImm

            0x1416214bbull, // IconTitleGetLangTextCall

            0x148b0e440ull, // GameObject_SendCommand

            0x14ddb9ac0ull, // ModelNode_UpdateModelNodeParameter

            0x14A10B530ull, // UiControllerImpl_InitEquipHudData (JP address not yet found)

            0x1414E60E0ull, // NoticeControllerImpl_GetOccasionalChat

            0x140D833D0ull, // SoldierConversationService_ConvertSpeechLabelToConversationType

            0x1414e61f9ull, // OccasionalChat_FactionTestNop

            0x1409325E0ull, // MbDvcReserveAnnouncePopup
            0x140915150ull, // MbDvcPopupGateFn

            0x1415144B0ull, // NoticeIndisAiImpl_StepCallHelp
            0x1415208E0ull, // NoticeNoiseAiImpl_StepCallHelp
            0x1415229D0ull, // NoticeNoiseAiImpl_StepResponse
            0x14151FE00ull, // NoticeNoiseAiImpl_StepAware
            0x141513AD0ull, // NoticeIndisAiImpl_StepAware
            0x1415162A0ull, // NoticeIndisAiImpl_StepResponse
            0x1414E5800ull, // NoticeControllerImpl_DoCheckSpreadNotice
            0x1414E3040ull, // NoticeControllerImpl_CheckSightNoticeSoldier

            0x140B15650ull, // RealizedSoldier2Impl_ConvertHeadEquipModelType
            0x140B18A20ull, // RealizedSoldier2Impl_UpdateHeadEquipMesh
            0x140B1A9B0ull, // FovaController_GetActiveFovaResourceManager
            0x1448ACA50ull, // Fv2ResourceManager_GetModel

            0x14A6347F0ull, // TimeCigaretteActionPluginImpl_ShowTimeCigaretteUi

            0x1408E57C0ull, // HeliTaxi_CanHeliTaxi
            0x149D09260ull, // HeliTaxi_CallRescueHeli
            0x140E0CE40ull, // HeliTaxi_StepWithdraw
            0x1409102C0ull, // HeliTaxi_GetLocationId
            0x14779D2F0ull, // HeliTaxi_RequestMapPhase
            0x140E09790ull, // HeliTaxi_StepGoToNav
            0x140E0E3A0ull, // HeliTaxi_StepTaxiCurrentCluster
            0x14973F220ull, // HeliTaxi_PassengerUpdate
            0x140E14470ull, // MechaActionImpl_StateOff
            0x140E148B0ull, // MechaActionImpl_StateOn
            0x14973C110ull, // PassengerControllerImpl_IsPassengerClosingDoor

            0x14804A500ull, // PlacedSystemImpl_BindResource
            0x140935170ull, // UiMarkerCommonDataImpl_RegisterLZMarkerInUpdate

            0x140E24030ull, // HeliSoundControllerImpl_Update
            0x14975E670ull, // HeliSoundControllerImpl_CallVoice
            0x140DfD310ull, // HeliFlightControllerImpl_Update
            0x147718790ull, // Hud_GetAnnounceLogSE
            0x148108C90ull, // Ui_GetStringId
            0x1408a29c0ull, // Hud_TypingLogActUpdate
            0x141DC9F10ull, // Ui_SoundControlStart
            0x1476cda70ull, // Ui_UiCommonDataManagerGetInstance
            0x1404b2220ull, // Ui_EventNodeBodyGetGraphState
            0x147b92a30ull, // VoiceParam_PlayDialogue
            0x1408cbcb0ull, // Ui_AnnounceLogViewLangId
            0x14a4979a0ull, // RideHeliActionPluginImpl_ExecPreMotionGraph
            0x14121ecb0ull, // RideHeliActionPluginImpl_GetStateFn

            0x142bff960ull, // SoundDaemon_Instance
            0x147ddeb40ull, // SoundDaemon_PostEventQueue
            0x1420a72b0ull, // Sd_kW_Select
            0x140A87290ull, // AddCassetteTapeTrack (thunk)
            0x140EF6540ull, // CollectGotTapes (thunk)
            0x140A95740ull, // IsGotCassetteTapeTrack (thunk)
            0x140AAC180ull, // SetCassetteTapeTrackNewFlag
            0x149CD4320ull, // SetCurrentAlbum (body)
            0x140974370ull, // SetupMusicInfos
        };

        return value;
    }
}
