#include <pch.h>

#include "AddressSet.h"
#include "AddressSet_day3900.h"

namespace AddressSetRuntime
{
    const AddressSet& Get_mst_en_day3900_AddressSet()   // 1.0.15.4 english
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
            0x14006b8c0ull, // FoxLuaRegisterLibrary
            0x140085700ull, // FoxPath_Path
            0x1400234c0ull, // FoxStrHash32
            0x141a098b0ull, // FoxStrHash64
            0x140887840ull, // GameOverSetVisible
            0x140911210ull, // GetCurrentMissionCode
            0x140BF43E0ull, // GetNameIdWithGameObjectId
            0x140BF4090ull, // GetGameObjectIdWithIndex
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
            0x14154FD00ull, // StateRadioRequest
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
            0x1404D2450ull, // SubtitleManager_Get
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
            0x1418C51B0ull, // Sahelan_ActionCoreImpl_UpdateEyeLampColor
            0x1418C5410ull, // Sahelan_ActionCoreImpl_UpdateHeartLight
            0x1418FFEB0ull, // Sahelan_PhaseSneakAi_PushEyeColor
            0x140EF28D0ull, // MbDvcAnnouncePopupCallbackImpl_UpdateAnnounceNormal
            0x140EF2C90ull, // MbDvcAnnouncePopupCallbackImpl_UpdateAnnounceServer
            0x140EF26B0ull, // MbDvcAnnouncePopupCallbackImpl_UpdateAnnounceEmergency
            0x140866910ull, // HudCommonDataManager_GetInstance
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
            0x1416201EBull, // IconTitleGetLangTextCall
            0x140BFA380ull, // GameObject_SendCommand
            0x140FE7660ull, // UiControllerImpl_InitEquipHudData
            0x1414E5030ull, // NoticeControllerImpl_GetOccasionalChat
            0x140D83470ull, // SoldierConversationService_ConvertSpeechLabelToConversationType
            0x1414E5149ull, // OccasionalChat_FactionTestNop
            0x140933630ull, // MbDvcReserveAnnouncePopup
            0x14093A940ull, // MbDvcPopupGateFn
            0x14151F510ull, // NoticeNoiseAiImpl_StepAware
            0x141512CD0ull, // NoticeIndisAiImpl_StepAware
            0x1414E4750ull, // NoticeControllerImpl_DoCheckSpreadNotice
            0x1414E1F90ull, // NoticeControllerImpl_CheckSightNoticeSoldier
            0x140E46900ull, // NoticeNearThreatAiImpl_Wake
            0x140E472D0ull, // NoticeNoiseAlertAiImpl_Wake
            0x141788760ull, // NoticeNearGameObjectAiImpl_Wolf_Wake
            0x141789760ull, // NoticeNoiseSneakAiImpl_Wolf_Wake
            0x140C35240ull, // NoticeNearGameObjectAiImpl_Bear_Wake
            0x140C36BA0ull, // NoticeNoiseAiImpl_Bear_Wake
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
            0x1408A35C0ull, // Hud_TypingLogActUpdate
            0x141DCA2B0ull, // Ui_SoundControlStart
            0x14085FD00ull, // Ui_UiCommonDataManagerGetInstance
            0x1404B27B0ull, // Ui_EventNodeBodyGetGraphState
            0x140928950ull, // VoiceParam_PlayDialogue
            0x14121D490ull, // RideHeliActionPluginImpl_ExecPreMotionGraph
            0x14121E180ull, // RideHeliActionPluginImpl_GetStateFn
            0x1411FA890ull, // MBTerminalActionPluginImpl_IsDisableMBTerminal
            0x14110E820ull, // CallMenuManagerImpl_Update
            0x1411F9BF0ull, // MBTerminalActionPluginImpl_CheckActionState
            0x142BFF980ull, // SoundDaemon_Instance
            0x140971410ull, // SoundDaemon_PostEventQueue
            0x1420A72F0ull, // Sd_kW_Select
            0x140d7c770ull, // UpdateAntiAir
            0x140d769b0ull, // ClearAntiAir
            0x140926730ull, // GetChangeLocationMenuParameterByLocationId
            0x140926860ull, // GetMbFreeChangeLocationMenuParameter
            0x140926940ull, // GetPhotoAdditionalTextLangId
            0x140fe75a0ull, // UiControllerImpl_HideBinocle
            0x140A87D10ull, // AddCassetteTapeTrack
            0x140EF5E00ull, // CollectGotTapes
            0x140A961C0ull, // IsGotCassetteTapeTrack
            0x140AACC00ull, // SetCassetteTapeTrackNewFlag
            0x140EF7480ull, // SetCurrentAlbum
            0x140975580ull, // SetupMusicInfos
            0x1405D66F0ull, // RadioCassette_SearchCasseteInfo
            0x1405D6560ull, // RadioCassette_GetCassetteMusic
            0x1405D65B0ull, // RadioCassette_IsGotCassette
            0x1405D6580ull, // RadioCassette_GetCassetteSaveIndex
            0x1405FC750ull, // RadioCassette_SdPostEvent
            0x1406433D0ull, // RadioCassette_RadioUpdate
            0x140A87DB0ull, // AddCassetteTapeTrackByIndex
            0x1406424C0ull, // RadioCassette_ActivateUnit
            0x1405D6640ull, // RadioCassette_IsSameSaveIndexFromName
            0x14126B610ull, // SearchLightActionPluginImpl_StateDoorStart
            0x14126AB10ull, // SearchLightActionPluginImpl_StateDoorEnd
            0x140338820ull, // SoundControl_PostExternalEvent
            0x14098A810ull, // MusicPlayerPlayWrapper
            0x140931C10ull, // Barrier_GetItemId
            0x1408B223Dull, // Barrier_EquipItemCallRet
            0x140FFFFE0ull, // Barrier_Updater
            0x1405D76F0ull, // Barrier_IsFobMode
            0x1409C47A5ull, // Barrier_LoadGate0
            0x1409C480Full, // Barrier_LoadGate1
            0x1409C4875ull, // Barrier_LoadGate2
            0x140FC35D0ull, // Dm_FireLoop
            0x140A2ABB0ull, // Dm_Classify
            0x141B18A10ull, // Dm_VfxFactory
            0x140C0BD10ull, // Dm_ComponentFactory
            0x140BFF0E0ull, // Dm_Alloc
            0x140AF3E80ull, // Dm_BackLinkPool
            0x1404ECD40ull, // Dm_OneShot
            0x141052820ull, // AttackActionImpl_IsWeaponNoUseInPlaceAction
            0x140A97B80ull, // TppMotherBaseManagement_RegCstDev
            0x140A983E0ull, // TppMotherBaseManagement_RegFlwDev
            0x140F6AED0ull, // EquipDevCtrl_GetSuitDevelopInfoIndex
            0x140F69790ull, // EquipDevCtrl_GetFaceEquipDevelopInfoIndex
            0x1416A0AB0ull, // AddListSuit
            0x140B10D40ull, // CamoufParamInfo_GetCamoufValue
            0x140FDBDA0ull, // CamouflageController_ExecSuitCorrect
            0x1411B0430ull, // EquipController_SetHandSlotEnabled
            0x141674530ull, // EquipDevelopCallbackImpl_SetSupplyCBoxInfo
            0x1416BBF10ull, // IsEnableCurrentSuit
            0x1416A3290ull, // ItemSelectorCallbackImpl_DecideActMissionPreparationSetEquipMode
            0x1416A41D0ull, // ItemSelectorCallbackImpl_DecideActMotherBaseDeviceSupportDropMode
            0x1416A8B90ull, // ItemSelectorCallbackImpl_SetupPrefabListElement
            0x1416AE280ull, // ItemSelectorRecordCallFunc_UpdateRecords
            0x1416A09C0ull, // ItemSelector_AddListBandana
            0x140AE98D0ull, // LoadPlayerBionicArmFpk
            0x140AE9820ull, // LoadPlayerBionicArmFv2
            0x140AE9080ull, // LoadPlayerCamoFpk
            0x140AE8FB0ull, // LoadPlayerCamoFv2
            0x140AE9A20ull, // LoadPlayerPartsFpk
            0x140AE9980ull, // LoadPlayerPartsParts
            0x140AE9440ull, // LoadPlayerSnakeBlackDiamondFpk
            0x140AE93C0ull, // LoadPlayerSnakeBlackDiamondFv2
            0x140AE95D0ull, // LoadPlayerSnakeFaceFpk
            0x140AE94C0ull, // LoadPlayerSnakeFaceFv2
            0x140958030ull, // MissionPrepSystem_IsEnableHeadOptionSuit
            0x1416BBE80ull, // MissionPrep_IsEnableCurrentHeadOption
            0x1409B47D0ull, // Player2BlockController_LoadPartsNew
            0x1409C69B0ull, // Player2GameObjectImpl_ProcessSignal
            0x1409CB200ull, // Player2Impl_SetUpParts
            0x1409DF760ull, // Player2UtilityImpl_LoadoutApplyAfterSetSuit
            0x1409DEC40ull, // Player2UtilityImpl_SetInitialConditionWithLoadoutInfo
            0x1409DFD70ull, // Player2UtilityImpl_SetSuitAndHandConditionWithLoadoutInfo
            0x1409B1030ull, // Player_ConverFaceIdWithFaceEquipId
            0x140AE8C90ull, // ResourceTable_DoesNeedFaceFova
            0x140AE8CE0ull, // ResourceTable_DoesNeedFaceFovaForAvatar
            0x1409C5270ull, // Sys_IsArtificialHandEnabled
            0x141E02FF0ull, // Sys_IsArtificialHandEnabledForCurrentPlayerType
            0x1409CD030ull, // UpdatePartsStatus
            0x1416A2680ull, // ItemSelectorCallbackImpl_DecideActMotherBaseCustomize
            0x1400cce00ull, // Fox_Path_Exists
            0x140085760ull, // Fox_Path_Dtor
            0x141E01FA0ull, // PluginFacial_ApplyMotion
            0x140f6d150ull, // EquipDevCtrl_IsEquipVisile
            0x140F697D0ull, // EquipDevCtrl_GetHeadBadgeCategory
            0x1416a6b70ull, // MissionPrep_SetInitialSelectRecord
            0x140A2B3C0ull, // TppEquip_RegisterConstant
            0x141E02BA0ull, // PlayerInfoInterfaceImpl_GetPartsTypeAtCamoType
            0x142C20FD0ull, // EquipIdTable_InfoList
            0x142A70928ull, // EquipIdTable_TypeWords
            0x140A3B760ull, // EquipIdTable_ReloadEquipIdTable
            0x140F68CD0ull, // EquipDevelopCtrl_GetEquipDevelopIndex
            0x140F6E800ull, // EquipDevelopCtrl_SetEquipUndeveloped
            0x140F6AE80ull, // EquipDevCtrl_GetSuitCamoType
            0x140F6AF60ull, // EquipDevCtrl_GetSuitLevel
            0x140F6D010ull, // EquipDevCtrl_IsEquipSuit
            0x140F6E600ull, // EquipDevelopCtrl_SetEquipDeveloped
            0x1412A2670ull, // SupplyCboxActionPluginImpl_StateInBox
            0x140863C60ull, // HudCommonDataManager_AnnounceLogView
            0x1409140E0ull, // Ui_LangIdToKey
            0x140912C10ull, // Ui_GetLangText
            0x141392A2Eull, // Soldier_ShootOneBullet_GroupMaskCall
            0x140D2DCADull, // Soldier_ActivateBulletAtEmptyWork_SameArmyJnz
            0x1408A9020ull, // TelopStartTitleEvCall_SetBgTexture
            0x141DAE4A0ull, // Layout_GetLayout
            0x141DAE4E0ull, // Layout_GetModel
            0x14094E4E0ull, // GetCassetteTapeUnreadInfo
            0x140A96620ull, // IsNewCassetteTapeTrack
            0x140EF5B30ull, // CassetteMenuCheckNewFlag
            0x140EF5A30ull, // CassetteAlbumCheckNewFlag
            0x140EF5CE0ull, // CassetteCheckUnreadInfo
            0x1404D6E90ull, // SubtitlesObjectSendMessage
            0x140f6ccb0ull, // EquipDevCtrl_IsEquipDevelopable
            0x140AE8180ull, // LoadAvatarFaceFv2
            0x140AE81F0ull, // LoadAvatarFaceFpk
            0x1409B63D0ull, // AvatarFaceEditUpdate
            0x1404E6BA0ull, // Fox_ModelFromHandle
            0x140AE73D0ull, // LoadAvatarHeadOptionFv2
            0x140AE7440ull, // LoadAvatarHeadOptionFpk
            0x140F6E5D0ull, // EquipDevelopCtrl_SetEnableDevelop
            0x140A41AE0ull, // ReloadEquipParameterTables2
            0x142C25C50ull, // GunBasicParameters2Buffer
            0x142A711F0ull, // EquipParameterTablesImpl_Instance
            0x142349A90ull, // MotionLoaderImpl_ReceiverTypeTable
            0x140DB6CB0ull, // MotionLoaderImpl_GetReceiverType
            0x142349A78ull, // MotionLoaderImpl_UnderBarrelTypeTable
            0x140DB6CD0ull, // MotionLoaderImpl_GetUnderBarrelType
            0x140F6B260ull, // EquipDevelopControllerImpl_GetSuppressorAmount
            0x142BDCFF0ull, // DamageParameterTable_Instance
            0x1405CE160ull, // DamageParameterTable_ReloadDamageParameter
            0x1405C9E30ull, // DamageParameterTable_GetDamageParameter
            0x140F65C70ull, // EquipDevCtrl_GetBaseDevelopId
            0x141678C80ull, // MenuDevelopGrid_FillGrid
            0x141678600ull, // MenuDevelopGrid_CopyGrid
            0x141672630ull, // MenuDevelopGrid_CountBadge
            0x141679220ull, // MenuDevelopGrid_FillFlat
            0x141678FC0ull, // MenuDevelopGrid_CopyFlat
            0x141281FB0ull, // SightManager_UpdateMissileLockOn
            0x140D2FE60ull, // Bullet3_DoSimulation
            0x140D2D430ull, // Bullet3_ActivateBulletAtEmptyWork
            0x141280000ull, // SightManager_Update
            0x141282C50ull, // SightManager_UpdateMissileLockOnUi
            0x1408EC8B0ull, // LockOnReticleFactory_CreateWindow
            0x140A3BCD0ull, // EquipParams_GetAttackIdByEquipId
            0x140DC3850ull, // EquipSystem_SetUpGunInfoFromGunPartsDesc
            0x141DABED0ull, // UiWindowFunction_FindWindow
            0x141DAC220ull, // UiWindowFunction_PostShowAndStartMessage
            0x141DABF50ull, // UiWindowFunction_GetLayout
            0x141041110ull, // AttackAction_Fire
            0x141302F50ull, // EquipObject_DoFire
            0x14132AF20ull, // Shell_ActivateShellAtEmptyWork
            0x141156E60ull, // CorePlugin_UpdateLoadoutRequest
            0x140DC03A0ull, // WeaponSystem_DefineWeaponFireSound
            0x142C934A0ull, // EquipSystem_ChimeraPartsSetWork
            0x140EFBA40ull, // MbDvcUpdateTrackListCallFuncs
            0x140EF70B0ull, // MbDvcRefreshTrackListPrefabParameter
            0x140EF6960ull, // MbDvcTrackListRecordRefresh
            0x140F31830ull, // MbDvcMissionListCallbackImpl_ActivatePrefabPopupParameter
            0x140F320E0ull, // MbDvcMissionListCallbackImpl_SetMenuHelp
            0x140A3B4C0ull, // Equip_ReloadChimeraPartsInfoTable
            0x142C207C0ull, // Equip_ChimeraPartsPackageInfos
            0x141A754C0ull, // Mtar_GetAnimFile
            0x141A76090ull, // Mtar_GetDataInfo
            0x142A6B400ull, // Equip_MotionEntryTable
            0x1422E2970ull, // SimplePartsControllerImpl_Vtable
            0x140ADDB80ull, // SimplePartsControllerImpl_SetMotionData
            0x140ADDC10ull, // SimplePartsControllerImpl_SetMotionDataByPath
            0x140DBFCF0ull, // EquipSystemImpl_HideMagazine
            0x140DBF1F0ull, // EquipSystemImpl_GetPartsController
            0x140BFF050ull, // Fox_GetQuarkSystemTable
            0x140071510ull, // Fox_ArrayBaseExtend
            0x1420BCAD8ull, // Fox_ArrayOperatorVtbl
            0x141AB7940ull, // Animx_GetControlSize
            0x141AB7360ull, // Animx_SimpleControlCtorPool
            0x141AB76E0ull, // Animx_SimpleControlCtorHeap
            0x141AB7540ull, // Animx_SimpleControlDtorPool
            0x141AB7750ull, // Animx_SimpleControlDtorHeap
            0x141AB76D0ull, // Animx_SimpleControlAux
            0x1405D5D80ull, // Equip_MotionEntrySlotHookA
            0x1405D60E0ull, // Equip_MotionEntrySlotHookB
            0x140A2A3E0ull, // EquipIdTable_GetEquipTypeId
            0x14143F0B0ull, // Soldier2InterrogateUtil_UpdateInterrogation
            0x141440A90ull, // Soldier2InterrogateUtil_UpdateInterrogationMarker
            0x140B08380ull, // SoundControllerImpl_CallVoice
            0x140DBEB20ull, // EquipSystem_GetGunInfoById
            0x140943130ull, // Reticle_ResetLayout
            0x1412B0240ull, // TimeCigaretteActionPluginImpl_HideTimeCigaretteUi
            0x140076DC0ull, // Fox_EntityInfoMapTeardown
            0x142AC6D10ull, // WeaponEnhanceLangIdArray
            0x140F9FA00ull, // WeaponEnhance_GetIndexForRegist
            0x140F9FAB0ull, // WeaponEnhance_GetMessageIdArray
            0x140F9FA60ull, // WeaponEnhance_GetLangIdById
            0x140F9FAA0ull, // WeaponEnhance_GetLangIdBound
            0x140B3B8A0ull, // BulletEffectController_CreateEffect
            0x1416A1170ull, // ItemSelectorCallbackImpl_AddRecord
            0x140AE4490ull, // AdditionalMotionTable_GetMtarPathId
            0x1416A2400ull, // ItemSelector_AddDevelopWeaponList
            0x1409B7C40ull, // BlockControllerImpl_UpdateAdditionalMotionBlock
            0x14006D9A0ull, // Fox_Block_GetFileForPathId
            0x14006D790ull, // Fox_BlockGroup_GetBlockAtIndex
            0x1416AC570ull, // ItemSelector_StartEquipPreviewImpl
            0x141681CC0ull, // UiEquipPreviewController_ScrollNext
            0x141681B10ull, // UiEquipPreviewController_ScrollPrev
            0x1409BB960ull, // Player2Impl_AddAdditionalMtarAll
            0x1409C9440ull, // Player2Impl_RemoveAdditionalMtarAll
            0x142349B80ull, // MotionLoaderImpl_BarrelTypeTable
            0x140DB69D0ull, // MotionLoaderImpl_GetBarrelType
            0x142349C00ull, // MotionLoaderImpl_MagazineTypeTable
            0x140DB6A20ull, // MotionLoaderImpl_GetMagazineType
            0x142349CC0ull, // MotionLoaderImpl_StockTypeTable
            0x140DB6CC0ull, // MotionLoaderImpl_GetStockType
            0x14120A4F5ull, // TppPickable_ItemWindowBoundSite
            0x140915130ull, // UiUtility_GetWeaponItemNameLangId
            0x1409849B5ull, // SoundPlayerAnimEvent_StepNoiseQuietCmp
            0x140A0B5A0ull, // CollectionLocatorArray_SetUpLinearAccessor
            0x140D4FA70ull, // CollectionSystemImpl_PickUp
            0x140D50D60ull, // CollectionSystemImpl_RepopCountOperation
            0x140D4D410ull, // CollectionSystemImpl_GetCollectionInfo
            0x140D4E390ull, // Collection_GetModelFilePath
            0x140D4D1C0ull, // Collection_GetCatchEffectColorAndScale
            0x140D4DF20ull, // Collection_GetGroundEffectSize
            0x1412091A0ull, // PickUpActionPluginImpl_DoPickUpCollection
            0x140910840ull, // GetCollectionIconFtexPath
            0x140910C20ull, // GetCollectionNameText
            0x14050B580ull, // UixUtility_GetUixUtility
            0x140D4E5B0ull, // Collection_GetModelFilePathRoot
            0x140D4DDC0ull, // Collection_GetFovaFilePath
            0x140A0C240ull, // Collection_IsHerbByType
            0x140A0C2B0ull, // Collection_IsMaterialByType
            0x140A0C160ull, // Collection_IsDiamondByType
            0x140A963A0ull, // MbmImpl_IsGotDataBase
            0x140A88670ull, // MbmImpl_AddTempDataBase
            0x1411603E4ull, // QuietChokeHold
            0x141160B79ull, // QuietInterrogate
            0x14110F7FCull, // QuietInterrogateBypass
            0x14110DC77ull, // QuietHoldupInterrogate
            0x1416BB359ull, // CharaSlotSelectionNumGate
            0x1416BB310ull, // CharaSlotSelectionNum
            0x14196F900ull, // UiMbmDataBase_RefreshPrefabList
            0x14196E940ull, // UiMbmDataBase_RefreshDataBaseNameText
            0x14196E800ull, // UiMbmDataBase_RefreshDataBaseIcon
            0x14196D480ull, // UiMbmDataBase_GetDataBaseInfoText
            0x14196EF60ull, // UiMbmDataBase_RefreshInfoLayout
            0x14196ED80ull, // UiMbmDataBase_RefreshImageRecord
            0x14196E6F0ull, // UiMbmDataBase_RefreshCollectionRate
            0x140FE59B0ull, // SkillAndItemParameterSystem_GetSuitParam
            0x140F1BA10ull, // MbDvcMapCallbackIconImpl_DispOnNewMarkerIcon
            0x140F1B3B0ull, // MbDvcMapCallbackIconImpl_DispOnImportantIcon
            0x140F1A910ull, // MbDvcMapCallbackIconImpl_DispOnGoalIcon
            0x140F1A6B0ull, // MbDvcMapCallbackIconImpl_DispOnGeneralIcon
            0x140F1B4E0ull, // MbDvcMapCallbackIconImpl_DispOnTargetMarkerIcon
            0x140F19B50ull, // MbDvcMapCallbackIconImpl_DispOnAreaIcon
            0x140F21B30ull, // MbDvcMapCallbackIconImpl_GetIconDisplayParam
            0x141E02D50ull, // PlayerInfo_GetLocalPartsType
            0x141E02520ull, // PlayerInfo_CheckStatusBit
            0x1409CDB7Full, // UpdatePartsStatus_QuietCamoStrip
            0x140956F00ull, // MissionPrepSystem_GetPlayerNameFromPlayerTypeAndStaffId
            0x1409C93D0ull, // Player2BlockController_ApplyQuietSuitShaderParams
            0x140FE7430ull, // UiControllerImpl_GetUiSightTypeBySightId
            0x1408EDDC0ull, // SightCommonFactory_CreateWindow
            0x1408EE7A0ull, // SightCommonFactory_DeleteWindow
            0x14196F850ull, // UiMbmDataBase_RefreshNewIcon
            0x1419702C0ull, // UiMbmDataBase_SetPutCursorFlag
            0x14196E530ull, // UiMbmDataBase_OnStop
            0x140F5A130ull, // MbmImpl_CalcDocumentationUnreadCount
            0x140F5A300ull, // MbmImpl_CalcEncyclopediaUnreadCount
            0x141E0B230ull, // Net_ServerManagerBufferRelease
            0x1414E0FC0ull, // NoticeControllerImpl_CheckSightNoticePlayer
            0x141AEAA80ull, // DemoPlayback_OnPlayingAfterUpdateStream
            0x141AE81D0ull, // DemoPlayback_DoFinish
            0x141AE82E0ull, // DemoPlayback_DoInterrupt
            0x140976410ull, // UpdateMusicPlayer
            0x140BF41B0ull, // GameObject_GetGameObjectIdWithName
            0x14124A040ull, // Vehicle_GetConnectPointWorldMatrix
            0x1412799F0ull, // Player_SequentialDemoActionExecute
            0x141279E90ull, // Player_SequentialDemoActionOnSignal
            0x140BF9A10ull, // Fox_GameObjectManager_GetSharedInstance
            0x14229B880ull, // GameObjectType_Horse2_Vtable
            0x1422C54D0ull, // GameObjectType_Heli2_Vtable
            0x1422C4AB0ull, // GameObjectType_HeliAlt_Vtable
            0x1422CAA30ull, // GameObjectType_WalkerGear2_Vtable
            0x1422E6070ull, // AnimationControllerImpl_Vtable
            0x1422FB400ull, // HeliAnimationControllerImpl_Vtable
            0x140DB6AC0ull, // MotionLoaderImpl_GetMtarIds
            0x140DB6A30ull, // MotionLoaderImpl_GetMtarFilePath
            0x140DB69E0ull, // MotionLoaderImpl_GetBlockPackagePath
            0x141048BC0ull, // AttackAction_GetGunMotionIdFromTable
            0x141045DE0ull, // AttackAction_GetEquipMotionIdFromTable
            0x14104B8D0ull, // AttackAction_GetNormalGripMotionId
            0x14104BAB0ull, // AttackAction_GetNormalMagazineGripMotionId
            0x1410436A0ull, // AttackAction_GetAssaultGripMotionId
            0x141048E70ull, // AttackAction_GetHandgunGripMotionId
            0x141050580ull, // AttackAction_GetSubMachinegunGripMotionId
            0x14104F740ull, // AttackAction_GetSniperRifleGripMotionId
            0x14104F430ull, // AttackAction_GetShotgunGripMotionId
            0x14104B290ull, // AttackAction_GetMachinegunGripMotionId
            0x141046230ull, // AttackAction_GetGrenadeLauncherGripMotionId
            0x14104B5B0ull, // AttackAction_GetMissileGripMotionId
            0x1410439A0ull, // AttackAction_GetAssaultUnderBarrelGripMotionId
            0x141049310ull, // AttackAction_GetHandgunOneHandGripMotionId
            0x141049460ull, // AttackAction_GetHandgunOneHandMotionId
            0x141050790ull, // AttackAction_GetSubMachinegunOneHandMotionId
            0x14104FE00ull, // AttackAction_GetStepReloadHandgunOneHandMotionId
            0x14104FC90ull, // AttackAction_GetStepReloadHandgunMotionId
            0x141055FA0ull, // AttackAction_ResetMissionPrepareGripMotion
            0x14104BC10ull, // AttackAction_GetNormalMotionIdByCategory
            0x1410436F0ull, // AttackAction_GetAssaultMotionIdTable
            0x141048F60ull, // AttackAction_GetHandgunMotionIdTable
            0x1410505C0ull, // AttackAction_GetSubMachinegunMotionIdTable
            0x14104F810ull, // AttackAction_GetSniperRifleMotionIdTable
            0x14104F4D0ull, // AttackAction_GetShotgunMotionIdTable
            0x14104FEE0ull, // AttackAction_GetShotgunStepReloadMotionIdTable
            0x14104B2D0ull, // AttackAction_GetMachinegunMotionIdTable
            0x141046360ull, // AttackAction_GetGrenadeLauncherMotionIdTable
            0x14104B600ull, // AttackAction_GetMissileMotionIdTable
            0x141051430ull, // AttackAction_GetChaseMotionIdTable
            0x140A00C50ull, // EquipMotionDataTable_ReloadEquipMotionData2
            0x1416C268Dull, // MissionPreparationCallbackImpl_Start_PlayBgmEvent
            0x1416C5AF9ull, // MissionPreparationCallbackImpl_Update_StopBgmEvent
            0x1416C5E1Eull, // MissionPreparationCallbackImpl_Update_StopBgmEvent2
            0x1416C2410ull, // MissionPreparationCallbackImpl_Start
            0x1416C2F80ull, // MissionPreparationCallbackImpl_Update
            0x142c1d9b0ull, // Soldier2FaceSystem_Instance
            0x140F80B70ull, // StaffControllerImpl_AddStaffShort
            0x140F80EB0ull, // StaffControllerImpl_AddStaffFull
            0x140AB0EA0ull, // tpp::mbm::impl::StaffIdToFaceId
            0x140F976D0ull, // tpp::mbm::impl::StaffControllerImpl::StaffSeedToFaceTexturePathId
            0x1416DFB40ull, // tpp::ui::menu::mbm::impl::StaffListCallbackImpl::RefreshFaceRecord
            0x141DEF780ull, // fox::ui::WindowFactoryCollector::FindWindowFactory
            0x141DABD50ull, // fox::ui::WindowFunction::CreateWindow
            0x140868820ull, // tpp::ui::hud::CommonDataManager::SetSubjectiveWindowHandle
            0x141DEF780ull, // fox::ui::WindowFactoryCollector::FindWindowFactory (implementation)
            0x140868820ull, // tpp::ui::hud::CommonDataManager::SetSubjectiveWindowHandle (implementation)
            0x1409ED7ADull, // CalcFv2ResourceManagerInfoForFace
            0x1409EE041ull, // ConvertExtendFaceFova2PathToFovaFile
            0x1409EE655ull, // ConvertFova2PathToFovaFileForProg
            0x1409F0817ull, // GetFaceFovaFpkPathIdArrayAtFaceId
            0x1409F0889ull, // GetFaceFovaFpkPathIdAtFaceId
            0x1409F0987ull, // GetFaceFovaPathIdArrayAtFaceId
            0x1409F09F9ull, // GetFaceFovaPathIdAtFaceId
            0x1409F0C95ull, // GetFaceFpkFileCodeListImpl
            0x1409F0E84ull, // GetFaceFpkFileCodeListImpl
            0x1409F137Eull, // GetHairFovaPathIdAtFaceId
            0x1409F38A3ull, // PushFaceFovaFpk
            0x1409F4900ull, // SetFaceFovaDefinitionTableImpl
            0x1409F5130ull, // SetFovaFileTable
            0x140A3BA20ull, // EquipParameterTablesImpl::GetAmmoParameterBlock
            0x140A84300ull, // WalkerGearSystemImpl::GetSubWeaponLoadAmmoMax
            0x140D18710ull, // BuddyQuietEquipControllerImpl::SetupCurrentSlot
            0x140EBAA10ull, // HostageEquipControllerImpl::SetupCurrentSlot
            0x14146FE40ull, // SoldierEquipControllerImpl::SetupCurrentSlot
            0x1417E01A0ull, // BossQuietEquipControllerImpl::SetupCurrentSlot
            0x140F65E50ull, // EquipDevelopControllerImpl::GetBulletCountMagazine
            0x140F65F80ull, // EquipDevelopControllerImpl::GetBulletCountMax
            0x140F660B0ull, // EquipDevelopControllerImpl::GetBulletType
            0x140DC38FFull, // EquipSystemImpl::SetUpGunInfoFromGunPartsDesc (receiver)
            0x140DC3AF4ull, // EquipSystemImpl::SetUpGunInfoFromGunPartsDesc (barrel)
            0x140DC3CCAull, // EquipSystemImpl::SetUpGunInfoFromGunPartsDesc (magazine)
            0x140DC3C71ull, // EquipSystemImpl::SetUpGunInfoFromGunPartsDesc (stock)
            0x140DC3D0Full, // EquipSystemImpl::SetUpGunInfoFromGunPartsDesc (muzzleOption)
            0x140DC3E6Cull, // EquipSystemImpl::SetUpGunInfoFromGunPartsDesc (sight)
            0x140DC3EF0ull, // EquipSystemImpl::SetUpGunInfoFromGunPartsDesc (sight2)
            0x140DC3D81ull, // EquipSystemImpl::SetUpGunInfoFromGunPartsDesc (laserFlash1)
            0x140DC3DDFull, // EquipSystemImpl::SetUpGunInfoFromGunPartsDesc (laserFlash2)
            0x140DC3F40ull, // EquipSystemImpl::SetUpGunInfoFromGunPartsDesc (underBarrel)
            0x140DC3FC6ull, // EquipSystemImpl::SetUpGunInfoFromGunPartsDesc (underBarrelCmp)
            0x140EE8DF0ull, // InventoryImpl::CalcAmmoAndStockIfPickUp
            0x140A2A520ull, // EquipIdTableImpl::GetRootParameterId
            0x140F61CE0ull, // DeployControllerImpl::GenerateDeployMission
            0x140F5F370ull, // DeployControllerImpl::AddLogMission
            0x140F61D90ull, // DeployControllerImpl::GenerateDeployMissionInfoOffline
            0x140F61460ull, // DeployControllerImpl::CreateDeployMissionSeedParam
            0x140F62890ull, // DeployControllerImpl::GetDeployMissionNameMessageId
            0x140F62310ull, // DeployControllerImpl::GetDeployMissionCaptionMessageId
            0x140F62740ull, // DeployControllerImpl::GetDeployMissionNameKeyId
            0x140F638A0ull, // DeployControllerImpl::RefreshMissionList
            0x140F61C80ull, // DeployControllerImpl::GetNextRandomMissionId
            0x140F60990ull, // DeployControllerImpl::CompletedDeployMissionOffline
            0x140AB0CD0ull, // MotherBaseManagementSystemImpl::SetupAfterRestoreFromSVars
            0x140F76F30ull, // PoolRewardControllerImpl::GeneratePoolRewardsInfo
            0x141645DB0ull, // CombatDeployCallbackImpl::RefreshMissionIconInGlobe
            0x1413DBCA0ull, // NoticeAntiBuddyWalkerGearAiImpl::ShouldSleep
            0x1413E03A0ull, // CombatAntiBuddyWalkerGearAiImpl::UpdatePrimaryTarget
            0x14138DB90ull, // AttackActionImpl::HasOtherSoldierOnVec
            0x140D313B0ull, // Bullet3Impl::OnOffenseHit
            0x14145E460ull, // soldier DamageHandlerImpl::ProcessDamages
            0x14136CEE0ull, // Soldier2Impl::UpdateMarkers
            0x141643740ull, // CombatDeployMissionListRecordCallFunc::Refresh
            0x141649930ull, // SetRevengeIcon
            0x1413DF080ull, // CombatAntiBuddyWalkerGearAiImpl::StepSurprise
            0x140340D40ull, // AK::MemoryMgr::Malloc
            0x140340CB0ull, // AK::MemoryMgr::Malign
            0x140340A70ull, // AK::MemoryMgr::Free
            0x142B9EC18ull, // AK::MemoryMgr::s_pMemPools
            0x142A0223Cull, // g_DefaultPoolId
            0x142B9EB1Cull, // g_PrepareEventMemoryPoolID
            0x141391EE0ull, // Soldier2Util::SearchActiveMember
            0x1413E0410ull, // CombatAntiBuddyWalkerGearAiImpl::UpdateSnipeTarget
            0x1415A66D0ull, // WalkerGearActionImpl::HasOtherSoldierOnVec
            0x141646030ull, // CombatDeployCallbackImpl::RefreshMissionList
            0x14086F160ull, // MbTopMenuSelectorEv::UpdateMenuNotice
            0x1414DE310ull, // NoticeControllerImpl::CheckNoiseNoticeSoldier
            0x1414FCA20ull, // NoticeDownComradeAiImpl::ShouldSleep
            0x1415B4860ull, // VehicleNoticeDownComradeAiImpl::ShouldSleep
            0x14155B580ull, // RideVehicleActionImpl::HasOtherSoldierOnVec
            0x1414A99D0ull, // LifeControllerImpl::SendStatusMessage
            0x1413DB760ull, // CombatAntiBuddyWalkerGearAiImpl::CheckGrenade
            0x1411FBB10ull, // IDroidActionPluginImpl::CloseState
            0x1411FB2B0ull, // IDroidActionPluginImpl::Deactivate
            0x140DC4430ull, // EquipSystemImpl::SetItemVisible
            0x1411F9DC0ull, // IDroidActionPluginImpl::IsPutAwayInterrupted
            0x14121E7D0ull, // IDroidActionPluginImpl::IsOtherAvatar
            0x141C75C3Full, // MotionGraphBlendNodeTraverser leaf clip-id read
            0x141A6C300ull, // AnimControl::GetAnimFileFromGaniPath
            0x141A93C00ull, // AnimLoader::GetMotionData
            0x141002350ull, // player action plugin GetStance (1 stand, 2 squat, 3 crawl, 4 supine)
            0x141181673ull, // CureActionPluginImpl::StateCureStart GetStance call
            0x14118172Dull, // CureActionPluginImpl::StateCureStart GetStance call
            0x141180F89ull, // CureActionPluginImpl::StateCure GetStance call
            0x1411812A9ull, // CureActionPluginImpl::StateCureEnd body GetStance call
            0x1410A1710ull, // BasicActionImpl::SelectWakeupState
            0x141180670ull, // CureActionPluginImpl::FinishCureSprayEffect
            0x141180E10ull, // CureActionPluginImpl::StateCure
            0x14110F580ull, // CallMenuManagerImpl::UpdateCommandSetInterrogation
            0x141360F90ull, // Soldier2Impl::ProcessSignal
            0x14151EFC0ull, // NoticeNoiseAiImpl::SearchSpecificComrade
            0x1414DCF00ull, // NoticeControllerImpl::CheckForceNotice
            0x14110FDF0ull, // CallMenuManagerImpl::UpdateInShowing
            0x141110140ull, // CallMenuCommandSet::UpdateState
            0x14110FA30ull, // CallMenuManagerImpl::UpdateCommandSetRescueBoy
            0x141368980ull, // soldier::ActionController::SetCommand
            0x141520320ull, // NoticeNoiseAiImpl::StepMovePause
            0x140EB07B0ull, // hostage CoreAiImpl::SetSpecialActionParam
            0x140EDB220ull, // hostage SpecialActionImpl::State_SpecialIdle
            0x14179C240ull, // ui::hud::CallMenuImpl::UpdateOption
            0x14179CCD0ull, // ui::hud::CallMenuImpl::UpdateSelectDoc
            0x14110E750ull, // CallMenuManagerImpl::ShowCallMenu
            0x142B743B0ull, // fox::gr::dg::MaterialManager::s_Instance (data)
            0x142B743A8ull, // fox::gr::dg::ShaderTechniqueManager::s_Instance (data)
            0x140DA20D0ull, // cp::RadioControllerImpl::StepRadioNoResponse
            0x140DA2380ull, // cp::RadioControllerImpl::StepRadioWaitResponse
            0x140D58220ull, // cp::CommandPost2Impl::Controller_RequestRadioByMember_
            0x140D58A20ull, // cp::CommandPost2Impl::Controller_SetRadioMemberReady_
            0x140D577A0ull, // cp::CommandPost2Impl::Controller_ReleaseRadioByMember_
            0x140D5B0E0ull, // cp::CommandPost2Impl::FinishRadio
            0x140DA1CE0ull, // cp::RadioControllerImpl::StepRadio
            0x140D58A60ull, // cp::CommandPost2Impl::Controller_UpdateRadio_
            0x140D62660ull, // cp::CommandPost2Impl::RequestRadioByCp
            0x140DA1C70ull, // cp::RadioControllerImpl::StepIdle
            0x140D58BD0ull, // tpp::gm::CpRadioService::ConvertSpeechLabelToRadioType
            0x140C100B0ull, // tpp::gm::impl::AiControllerImpl::WakeNode
            0x140C0F8E0ull, // tpp::gm::impl::AiControllerImpl::SleepNode
            0x1413555F0ull, // tpp::gm::soldier::Soldier2InterrogateUtil::IsTranslatable
            0x140A46870ull, // tpp::gm::buddy::impl::Buddy2BlockControllerImpl::GetPathAtBuddyType
            0x140A465C0ull, // tpp::gm::buddy::impl::Buddy2BlockControllerImpl::GetBuddyQuietWeaponFileFromBuddyBlock
            0x140A464F0ull, // tpp::gm::buddy::impl::Buddy2BlockControllerImpl::GetBuddyQuietFovaFile
            0x140A46599ull, // tpp::gm::buddy::impl::Buddy2BlockControllerImpl::GetBuddyQuietFv2FileFromBuddyBlock (fv2 id load)
            0x140F65D20ull, // EquipDevelopControllerImpl::GetBuddyPartsDevelopInfoIndex
            0x140F65D90ull, // EquipDevelopControllerImpl::GetBuddyPartsId
            0x140F65DF0ull, // EquipDevelopControllerImpl::GetBuddyPartsKind
            0x142C29030ull, // tpp::gm::buddy::impl::g_buddyService (BuddyServiceImpl instance)
            0x140A4ED70ull, // tpp::gm::buddy::impl::BuddyServiceImpl::ChangeHorseSettings
            0x140A4E910ull, // tpp::gm::buddy::impl::BuddyServiceImpl::ChangeDogSettings
            0x140A51300ull, // tpp::gm::buddy::impl::BuddyServiceImpl::GetQuietEquipIdByWeaponType
            0x140A52A10ull, // tpp::gm::buddy::impl::BuddyServiceImpl::IsUseSuppressorByWeaponType
            0x140A4EEDDull, // tpp::gm::buddy::impl::BuddyServiceImpl::ChangeQuietCostume (costume clamp)
            0x140A54EE7ull, // TppBuddyService.SetVarsQuietCostume (costume clamp)
            0x140A4F081ull, // tpp::gm::buddy::impl::BuddyServiceImpl::ChangeQuietWeaponType (weapon clamp)
            0x140A54F62ull, // TppBuddyService.SetVarsQuietWeaponType (weapon clamp)
            0x140A4EDECull, // tpp::gm::buddy::impl::BuddyServiceImpl::ChangeHorseSettings (tack check)
            0x140A4E959ull, // tpp::gm::buddy::impl::BuddyServiceImpl::ChangeDogSettings (gear check)
            0x140CE3C58ull, // tpp::gm::buddyquiet::impl::BuddyQuiet2Impl weapon change command (weapon clamp)
            0x140CE48F3ull, // tpp::gm::buddyquiet::impl::BuddyQuiet2Impl::SetBruise (costume check)
            0x1416AAD77ull, // tpp::ui::menu::impl::ItemSelectorCallbackImpl::SetupPrefabListElement (Quiet costume list gate)
            0x1416AA8C7ull, // tpp::ui::menu::impl::ItemSelectorCallbackImpl::SetupPrefabListElement (Quiet weapon list gate)
            0x141458A0Bull, // tpp::gm::soldier::impl::DamageAiImpl::StepBitDamage (dog gear check)
            0x140D04D50ull, // tpp::gm::buddyquiet::impl::(anon)::CombatAiImpl::SendFireSignal
            0x140D18100ull, // tpp::gm::buddyquiet::impl::EquipControllerImpl::InitCurrentSlot
            0x140D188C0ull, // tpp::gm::buddyquiet::impl::EquipControllerImpl::Update
            0x140D18460ull, // tpp::gm::buddyquiet::impl::EquipControllerImpl::SetMotion
            0x140A466C0ull, // tpp::gm::buddy::Buddy2BlockController::GetFileForPathFromBuddyBlock
            0x142C28930ull, // tpp::gm::buddy::Buddy2BlockController instance pointer
            0x140E511A0ull, // tpp::gm::horse::impl::ActionCoreImpl::ExecDefenseCallback tack damage switch
            0x140E67378ull, // tpp::gm::horse::impl::BasicAction2Impl::UpdateGallopMotionSpeed tack switch
            0x140E58842ull, // tpp::gm::horse::impl::BasicAction2Impl::EnterOffenseTarget tack switch
            0x140E7BF9Bull, // horse motion sound tack switch (FUN_140E7BE70)
            0x140E588D0ull, // tpp::gm::horse::impl::BasicAction2Impl::Execute
            0x140A51643ull, // tpp::gm::buddy::impl::BuddyServiceImpl D-Dog order variant gear check (FUN_140A515B0)
            0x140A51844ull, // tpp::gm::buddy::impl::BuddyServiceImpl::InitBuddyCommandForStatus gear check
            0x140C896E7ull, // tpp::gm::buddydog::impl::DamageHandlerImpl::ProcessDamages gear check
            0x140C894C0ull, // tpp::gm::buddydog::impl::DamageHandlerImpl::IsNeedReaction gear check
            0x140C886D6ull, // tpp::gm::buddydog::impl::DamageHandlerImpl::CallDamageEffect gear check
            0x140C88EE4ull, // tpp::gm::buddydog::impl::DamageHandlerImpl::CallDamageSound gear check
            0x140C8F63Eull, // tpp::gm::buddydog::impl::EscapeIdleAiImpl::StepSearch gear check
            0x140CADCC3ull, // tpp::gm::buddydog::impl::NoticeDamageAiImpl::Setup gear check
            0x140C63FD3ull, // tpp::gm::buddydog::impl::AttackAiImpl::ReTarget gear check
            0x1414CECF5ull, // tpp::gm::soldier::impl::NoticeBuddyDogAiImpl::StepAware gear check
            0x140C56531ull, // tpp::gm::buddydog::impl::BuddyDog2Impl::Initialize gear copy to +0xF4
            0x140A50C20ull, // tpp::gm::buddy::impl::BuddyServiceImpl::GetBuddyCommandForWeapon gear check
            0x140F663B0ull, // tpp::mbm::impl::EquipDevelopControllerImpl::GetDWalkerPartsDevelopInfoIndex
            0x140F66410ull, // tpp::mbm::impl::EquipDevelopControllerImpl::GetDWalkerPartsDevelopedCurrentLevel
            0x140F664B0ull, // tpp::mbm::impl::EquipDevelopControllerImpl::GetDWalkerPartsId
            0x140F665C0ull, // tpp::mbm::impl::EquipDevelopControllerImpl::GetDWalkerPartsLevel
            0x140F66620ull, // tpp::mbm::impl::EquipDevelopControllerImpl::GetDWalkerPartsType
            0x14170AE00ull, // FUN_14170AD80 sub weapon type check
            0x140A4ED30ull, // tpp::gm::buddy::impl::BuddyServiceImpl::ChangeGearSubWeaponSettings type limit
            0x140A03F70ull, // tpp::gm::EquipBlockController2::StartLoadingToEquipMissionBlock
            0x140A057C0ull, // FUN_140A057C0 EquipDataSystemAccessorImpl vt+0x38, block holding an equip's pack
            0x14125CFBDull, // RideWalkerGearActionImpl::ChangeWeapon sub weapon type switch
            0x14125F220ull, // RideWalkerGearActionImpl::GetVehicleEquipId sub weapon type switch
            0x14125FD5Full, // RideWalkerGearActionImpl::SetEquipWeaponId sub weapon type switch
            0x140939550ull, // HUD utility vt+0x428, name text of a tpp::VehicleEquipId (real name unknown)
            0x1409390A0ull, // HUD utility vt+0x420, icon path of a tpp::VehicleEquipId (real name unknown)
            0x14125E766ull, // RideWalkerGearActionImpl rider UI update, sub weapon type -> reticle switch
            0x141965650ull, // CustomizeSelectorCallbackImpl item list for the open category (real name unknown)
            0x140ADF650ull, // tpp::gm::impl::SimplePartsEffectControllerImpl::CreateEffectImpl
            0x141B19340ull, // fox::fx::Effect::Create
            0x142A82C28ull, // tpp::gm::player Player2System instance
            0x141A76410ull, // fox::anim::AnimFile::GetEventData
            0x141306FF0ull, // tpp::gm::impl::equip::RealizedEquipObjectImpl::MotionEventCallbackImpl
            0x141303DA0ull, // tpp::gm::impl::equip::RealizedEquipObjectImpl::EjectCasing
            0x141059620ull, // tpp::gm::player::impl::attack::AttackActionImpl::SetEquipMotion
            0x1410528F0ull, // tpp::gm::player::impl::attack::AttackActionImpl::MotionEventCallback
            0x140A1D000ull, // tpp::gm::impl::(anon)::GetPackagePaths
            0x1400AB5D0ull, // fox::Array<fox::Path>::PushBack
            0x140085670ull, // fox::Path::Path(const char*)
            0x141A7D210ull, // fox::anim::EventCallback::ExecCallback
            0x1416ACA30ull, // tpp::ui::menu::impl::ItemSelectorCallbackImpl::Update
            0x1409B2640ull, // tpp::gm::player::impl::Player2BlockController::GetFileFromPartsBlockNew
            0x142C1B0F8ull, // the Player2BlockController pointer it is called with
            0x1404E1C20ull, // fox::FormVariationFile2::ApplyFormVariation (ModelBuilder)
            0x140AEEE10ull, // tpp::gm::player::impl::Fova2ControllerImpl work: type 0x11 extra after an fv2 apply
            0x140A2A710ull, // tpp::gm::impl::equip::EquipIdTableImpl::GetSupportWeaponTypeId
            0x140A42290ull, // tpp::gm::impl::equip::EquipParameterTablesImpl::ReloadEquipParameterTablesImpl
            0x140A423C5ull, // ReloadEquipParameterTablesImpl: add rax, 0x2D8 (SupportWeaponParameter size pass)
            0x140A423DEull, // ReloadEquipParameterTablesImpl: lea rax, [rcx+0x2D8] (allocation pass)
            0x1415D5AE0ull, // tpp::gm::impl::throwing::ThrowingImpl::GetBlastParamByEquipId
            0x1415D94E0ull, // tpp::gm::impl::throwing::ThrowingImpl::UpdateActionGrenade (grenade and stun)
            0x1415D9D10ull, // tpp::gm::impl::throwing::ThrowingImpl::UpdateActionSmoke (smoke, sleeping gas, flares)
            0x1415D9AC0ull, // tpp::gm::impl::throwing::ThrowingImpl::UpdateActionMolotovCocktail
            0x1415D9230ull, // tpp::gm::impl::throwing::ThrowingImpl::UpdateActionDecoy
            0x1415D9810ull, // tpp::gm::impl::throwing::ThrowingImpl::UpdateActionKibidango
            0x1415D93C0ull, // tpp::gm::impl::throwing::ThrowingImpl fake sign activation
            0x1415DD660ull, // tpp::gm::impl::throwing::ThrowingImpl::UpdateStatus
            0x1415DE4F0ull, // tpp::gm::impl::throwing::ThrowingSystemImpl::ExecuteSerially
            0x1415D2210ull, // tpp::gm::impl::throwing::ThrowingImpl::ActivateThrowingAtEmptyWork
            0x1415DAAE0ull, // tpp::gm::impl::throwing::ThrowingImpl::UpdateDeactivation
            0x1415D2D10ull, // tpp::gm::impl::throwing::ThrowingImpl::AddReboundNoise
            0x1415D83C0ull, // tpp::gm::impl::throwing::ThrowingImpl::PostReboundSound
            0x1415D7400ull, // tpp::gm::impl::throwing::ThrowingImpl::IsObstructSight
            0x140F66930ull, // tpp::mbm::impl::EquipDevelopControllerImpl::GetDamageType
            0x1411ADA20ull, // tpp::gm::player::impl::EquipControllerImpl::InitializePlayerAtIndex
            0x140EEDD40ull, // tpp::gm::impl::LoadoutManagerImpl::RestoreLoadoutInfosFromVars
            0x140EEE360ull, // tpp::gm::impl::LoadoutManagerImpl sortie loadout slot restore
            0x140EEE6D0ull, // tpp::gm::impl::LoadoutManagerImpl return-heli loadout slot restore
            0x140FCDE00ull, // tpp::gm::impl::placed::PlacedSystemImpl::RestorationPlace
            0x140ACA7D0ull, // tpp::gm::impl::SupplyCboxGameObjectImpl::RestoreRequestFromSVars
            0x1409D2B60ull, // tpp::gm::player::impl::Player2UtilityImpl::GetPickableInfosOfAmmoBox
            0x140A3D070ull, // tpp::gm::impl::equip::EquipParameterTablesImpl::GetSupportWeaponParameterBlock
            0x140F667C0ull, // tpp::mbm::impl::EquipDevelopControllerImpl::GetDamageAmountRatio
            0x14166B3E0ull, // tpp::ui::menu::mbm::impl::EquipDetailsCallbackImpl::SetupSupportParameterDetails (bar panel caller range)
            0x141483A60ull, // tpp::gm::soldier::impl::IdleActingActionImpl::CallVoice
            0x14158C140ull, // tpp::gm::soldier::impl::Soldier2SoundControllerImpl::__callVoiceWithoutSync
            0x14158C050ull, // tpp::gm::soldier::impl::Soldier2SoundControllerImpl::__callVoiceWithSync
            0x14154F790ull, // tpp::gm::soldier::impl::RadioActionImpl::State_RadioChaffEnd
            0x140955A0Dull, // tpp::gm::EquipIdTable::GetAmmoStockIdOfSupportOrItem: lea eax,[rbx-0x50A]
            0x140952693ull, // MissionPreparationSystemImpl::CalcEquipWeaponItemCostFromLoadout support loop: lea r9d,[rbx-0x50A]
            0x140EEF5F3ull, // LoadoutManagerImpl::StoreLoadoutInfoToPlayerVars support cells: add edi,-0x50A
            0x1411AE609ull, // EquipControllerImpl::InitializePlayerAtIndex support slots: lea r8d,[rbx-0x50A]
            0x141208642ull, // PickUpActionPluginImpl::CheckLoadableForSupportOrItem: lea r8d,[rbx-0x50A]
            0x14120971Eull, // PickUpActionPluginImpl::DoPickUpPlaced carried slot: lea eax,[rsi-0x50A]
            0x1412097C2ull, // PickUpActionPluginImpl::DoPickUpPlaced picked item: lea ecx,[r15-0x50A]
            0x14120A2B4ull, // PickUpActionPluginImpl::DoPickUpWithPickableInfo carried slot: lea ecx,[rsi-0x50A]
            0x1416A3B4Full, // ItemSelectorCallbackImpl::GetCurrentAmmoFromVarsForSupportWeapon: add ebx,-0x50A
            0x140A3BC15ull, // EquipParameterTablesImpl::GetAmmoParameterBlock: lea edi,[rbx-0x50A]
            0x140DA2A50ull, // tpp::gm::impl::cp::RadioSpeechHandlerImpl::CallPart
            0x1412DCDB0ull, // tpp::gm::impl::RangeAttackSystemImpl::ActivateChaff
            0x140B08860ull, // tpp::gm::impl::SoundControllerImpl::IsPlayingVoice
            0x1414B07C0ull, // tpp::gm::soldier::impl::MortarActionImpl::State_WaitAction
            0x1414B0950ull, // tpp::gm::soldier::impl::UnrealMortarActionImpl::State_WaitAction
            0x140DDAB80ull, // tpp::gm::heli::impl::Heli2Impl::Unrealize
            0x1414B0010ull, // tpp::gm::soldier::impl::MortarActionImpl::State_MortarAction
            0x1414B02F0ull, // tpp::gm::soldier::impl::UnrealMortarActionImpl::State_MortarAction
            0x140DA1F80ull, // tpp::gm::impl::cp::RadioControllerImpl::StepRadioChaff
            0x140AB95B0ull, // tpp::gm::radio::RadioCommand::RegisterPriorityTable
            0x14094FF70ull, // tpp::ui::menu::impl::MissionPreparationSystemImpl::CalcEquipWeaponItemCostFromLoadout
            0x140953AF0ull, // tpp::ui::menu::impl::MissionPreparationSystemImpl::CalcEquipWeaponItemReturnResourcesFromLoadout
            0x140A224B0ull, // tpp::gm::impl::ScriptVars_LoadVarsFromSlot
            0x140A227C0ull, // tpp::gm::impl::ScriptVars_SaveVarsToSlot
            0x140A23050ull, // tpp::gm::impl::ScriptVars_WriteSlotToFile
            0x14105BAF0ull, // AttackActionImpl_SetVisibleLaserSight
            0x1411B0D30ull, // EquipControllerImpl_SetUpPartsStateForGun
            0x1408B4040ull, // EquipCrossEvCall_SetPrimaryOptionPanel
            0x1408B0560ull, // EquipCrossEvCall_PrimaryOptionSelect
            0x1408B1450ull, // EquipCrossEvCall_SecondaryOptionSelect
            0x1408AF040ull, // EquipCrossEvCall_ItemToggleChange
            0x1408AB450ull, // EquipCrossEvCall_CallEquipSE
            0x140916DD0ull, // Ui_PauseInitUiAnimation
            0x14091B910ull, // Ui_StartUiAnimation
            0x14091B8A0ull, // Ui_StartInstantUiAnimation
            0x140917B10ull, // Ui_SetTextForModelNodeText
            0x140913740ull, // Ui_GetPadButtonUvShift
            0x141DACEE0ull, // Fox_Ui_ConnectComponent
            0x141DC7840ull, // ModelNodeMesh_SetShaderParameter
            0x1400252E0ull, // ModelNode_SetVisible
            0x1408B2A50ull, // EquipCrossEvCall_SetPanelInfo
            0x140912610ull, // Ui_GetKeyboardIconUV
            0x141DD62C0ull, // FontManager_SetIconTextureCoord
            0x142C8FAB8ull, // RawDaemon_s_fontManager
            0x140DC1A80ull, // EquipSystemImpl_SetAmmoCount
            0x140BF1E30ull, // QuarkCollector_GetInstanceForCreatorId
            0x140023F90ull, // LwLockSection_Enter
            0x1400240E0ull, // LwLockSection_Leave
            0x14238FA00ull, // MbDvcMapCallbackImpl_QuarkVtable
            0x14227F940ull, // MbDvcMapLocationParameterImpl_Vtable
            0x140AC8D30ull, // SpeechSystemImpl_FindSpeechSequenceIndex
            0x142C2EFA0ull, // SpeechFileTable
            0x142C32FA8ull, // SpeechFileTableLock
            0x142BD3600ull, // TppWeatherManager_s_Instance
            0x1414E3400ull, // NoticeControllerImpl_CheckSpecialSaluteVoiceLabel
            0x140AEEB00ull, // Fova2ControllerImpl_SetChickenVisibility
            0x140E102E0ull,  // HeliLightActionImpl_Execute
            0x140E07090ull,  // HeliGunActionImpl_IsAimTypePosition
            0x140E06C40ull,  // HeliGunActionImpl_GetAimPosition
            0x140DDD6F0ull,  // HeliActionCoreImpl_Execute
            0x140B92550ull,  // HeliEffectControllerImpl_CreateEffect
            0x140E23960ull,  // HeliSoundControllerImpl_CallSound
            0x140DEFC10ull,  // HeliDamageHandlerImpl_ProcessQueuedDamages
            0x140E14F30ull,  // MechaActionImpl_StateOnPassenger
            0x140E14AF0ull,  // MechaActionImpl_StateOffPassenger
            0x141245820ull,  // RideVehicleActionImpl_CheckVehicleIconArea
            0x141556C10ull,  // RideHeliActionImpl_State_GetInAction
            0x141557AD0ull,  // RideHeliActionImpl_State_IdleAction
            0x1415563E0ull,  // RideHeliActionImpl_SetRootAdjustMatrix
            0x140E19C30ull,  // HeliPassengerControllerImpl_GetAttachMatrix
            0x140B17ED0ull,  // RealizedSoldier2Impl_SetModelAlphaAndMeshVisibilityImmediately
            0x141AD8250ull,  // CcRootAdjustAction_Exec
            0x141557CE0ull,  // UnrealRideHeliActionImpl_State_IdleAction
            0x1415571F0ull,  // RideHeliActionImpl_State_GetOutAction
            0x14138B4F0ull,  // SoldierAttackActionImpl_Execute
            0x1413922D0ull,  // SoldierAttackActionImpl_ShootOneBullet
            0x14138D4E0ull,  // SoldierEquipController_GetMuzzleWorldMatrix
            0x141443050ull,  // SoldierDamageActionImpl_Execute
            0x141365510ull,  // Soldier2Impl_RequestDamage
            0x1413517E0ull,  // Soldier2Impl_GetLatestDamageOffenseOwnerId
            0x140DBEB20ull,  // EquipSystemImpl_GetGunInfoById
            0x1416A4E80ull,  // ItemSelector_IsGetLatestItemTool
            0x1407F1840ull,  // FobTarget_RequestSneakCommon
            0x141686BE0ull,  // FobMission2CallbackImpl_OpenErrorPopup
            0x1408684F0ull,  // HudCommonDataManager_SetPopupText
            0x1405FBDE0ull,  // TppPermanentGimmickUnit_CallNoise
            0x141391900ull,  // SoldierAttackActionImpl_RequestFire
            0x141430750ull,  // Soldier2ActionUtil_ResetEquipMotion
            0x141430B30ull,  // Soldier2ActionUtil_StartEquipReloadMotion
            0x140868170ull,  // HudCommonDataManager_SetPopupErrorParam
            0x141950350ull,  // UavLifeControllerImpl_SendStatusMessage
            0x141672EB0ull,  // EquipDevelopCallbackImpl_IsLatestItemTool
            0x1411B2E30ull, // EquipControllerImpl_UpdateBattery
            0x140938500ull, // UiUtilityImpl_GetBatteryLevel
            0x140FF2477ull, // ActionCoreImpl_ExecPostPutMotion_EmpGate
            0x140D683D0ull,  // CpRadioService_CheckRadioTypeBgmPhaseMismatch
            0x1408DBBA0ull,  // TppUiCommand_SetMbTopMenuItemVisible
            0x1408D1F40ull,  // TppUiCommand_InitAllMbTopMenuItemVisible
            0x140968460ull,  // MotherBaseDeviceTopMenuTableImpl_SetVisibleItem
            0x140968140ull,  // MotherBaseDeviceTopMenuTableImpl_IsVisibleItem
            0x141095DF0ull,  // AttackActionImpl_UpdateShootingByPad
            0x1410589E0ull,  // AttackActionImpl_SetAmmoCount
            0x141623E80ull,  // TppUIWeaponNumImpl_SetPanelInfo
            0x141624A30ull,  // TppUIWeaponNumImpl_SetWeaponChargeGauge
            0x141623560ull,  // TppUIWeaponNumImpl_SetBatteryGauge
            0x140DBE5E0ull,  // EquipSystemImpl_GetEffectController
            0x140AE0000ull,  // SimplePartsEffectControllerImpl_GetEffectWorldMatrix
            0x140418280ull,  // FoxFxEffect_SetVisible
            0x140418100ull,  // FoxFxEffect_SetKillNow
            0x141B19460ull,  // FoxFxEffect_SendDestroy
            0x140016090ull,  // FoxSharedPtrBase_Set
            0x1407A9470ull,  // FoxGeoResultI_GetPosition
            0x141B99910ull,  // FoxGeoResultI_GetNormal
            0x14045BEB0ull,  // GeoCheckControlN1_Construct
            0x141B9B260ull,  // FoxGeo_CollisionLineCheck
            0x140D45740ull,  // ChimeraSystemImpl_GetEffectController
            0x140ADB300ull,  // SimplePartsControllerImpl_Create
            0x1413068D0ull,  // RealizedEquipObjectImpl_Initialize
            0x1401CCA90ull,  // FoxGrModel_InvisibleMesh
            0x1401CEB80ull,  // FoxGrModel_VisibleMesh
            0x1401CCD50ull,  // FoxGrModel_IsMeshVisible
            0x141364FF0ull,  // Soldier2Impl_RealizeRealizedSoldier
            0x140AED0D0ull,  // Fova2ControllerImpl_ApplyPlayerFova
            0x140AF2E40ull,  // AnimationControllerImpl_SetGameObjectIdToCharaControl
            0x1417A0C5Aull,  // RespawnActionPluginImpl_PollingImpl_OnlineGate
            0x140A099C0ull, // collection locator array destructor
            0x140835160ull, // CmdCheckServerItemCorrect::Pack
            0x1408519D0ull, // CmdSendBootOption::Pack
            0x14080F7B0ull, // CmdSyncMotherBaseOption::Pack
            0x1408132A0ull, // CmdSyncSoldierBinOption::Pack
            0x140813BF0ull, // CmdSyncSoldierDiffOption::Pack
            0x1407EA9E0ull, // SyncSoldierBinReceive::Success
            0x1411AC2E0ull, // tpp::gm::player::impl::EquipControllerImpl::ChangeEquip
            0x1411AFF30ull, // tpp::gm::player::impl::EquipControllerImpl::SetEquipState
            0x1416740B0ull, // EquipDevelopCallbackImpl::SetEquipDevelopVars
            0x140DC3250ull, // tpp::gm::impl::EquipSystemImpl::SetUpEquip
            0x14103C750ull, // CreateLaserSightController
            0x140EEF130ull, // LoadoutManagerImpl::StoreLoadoutInfoToPlayerVars
            0x1407F5BD0ull, // SynchronizeLoadout
            0x140FC69A0ull, // PlacedSystemImpl::CopyToScriptVarsPlayerPlacedWeapon
            0x140FC71B0ull, // PlacedSystemImpl::CreateByLocators
            0x1412082A0ull, // PickUpActionPluginImpl::CheckLoadableForAmmo
            0x141209EC0ull, // PickUpActionPluginImpl::DoPickUpWithPickableInfo
            0x140FB9810ull, // PickableSystemImpl::CreateInstanceFromVars
            0x1409CE930ull, // tpp::gm::player::impl::Player2Impl::UpdateVoiceType
            0x141390360ull, // tpp::gm::soldier::impl::AttackActionImpl::ProcessFireControl
            0x141396AC0ull, // tpp::gm::soldier::impl::AttackActionImpl::SubState_Idle
            0x1413A3FA0ull, // tpp::gm::soldier::impl::BasicAction2Impl::State_ChangeStandOrSquat
            0x140D5DF30ull, // TppCp2 lua command table
            0x14146E3A0ull, // tpp::gm::soldier::impl::EquipControllerImpl::DropEquip
            0x1414703A0ull, // tpp::gm::soldier::impl::EquipPluginImpl::Execute
            0x1414735B0ull, // tpp::gm::soldier::impl::EquipPluginImpl::NormalMtevCallback
            0x140EEA650ull, // InvestigateSituationOperatorImpl::GetSituationWithRequest
            0x1414BDE70ull, // tpp::gm::soldier::impl::NoticeActionImpl::Wake
            0x1405141F0ull, // RouteGroupImpl::GetEventAimCharacterId
            0x140514B50ull, // RouteGroupImpl::GetEventId
            0x140FE9430ull, // UiControllerImpl::UpdateEquipHud
            0x14115A210ull, // tpp::gm::player::impl::CqcActionPluginImpl::Work::CompleteContinueCqcTarget
            0x14151E850ull, // tpp::gm::soldier::impl::NoticeNoiseAiImpl::CheckTargetPositionUpdate
            0x141523460ull, // tpp::gm::soldier::impl::NoticeNoiseAiImpl::StepWaitReach
            0x140DA3700ull, // RadioSpeechHandlerImpl::SendFinishedMessage
            0x1409BF230ull, // tpp::gm::player::impl::Player2Impl::CallVoice
            0x14006ACD0ull, // lua panic handler
            0x141DBC2D0ull, // IsReadyTexture
            0x1404E1ED0ull, // fv2 apply by group (real name unknown)
            0x1407440E0ull, // GetVfxFileFromParts
            0x140742E02ull, // return after the sprint spark GetVfxFileFromParts call
            0x142BF2698ull, // GameOver open flag
            0x14224A3F0ull, // GameOverEvent vtable
            0x140F50D20ull, // mother base security rank slot
            0x140F55620ull, // mother base security clamp slot
            0x140F6CC70ull, // tpp::mbm::impl::EquipDevelopControllerImpl::IsDisplayableBluePrintIDContion
            0x140F67C80ull, // blueprint condition count
            0x140F670C0ull, // blueprint condition message ids
            0x142438780ull, // develop grid flat tab table A
            0x1424387C0ull, // develop grid flat tab table B
            0x141A6B8F0ull, // additional motion mtar add
            0x141A6CC90ull, // additional motion mtar remove
            0x1411666A0ull, // tpp::gm::player::impl::CqcActionPluginImpl::StateHoldMove
            0x1411644E0ull, // tpp::gm::player::impl::CqcActionPluginImpl::StateGunHoldMove
            0x1409DB170ull, // RequestToSetTargetStance
            0x1411F9072ull, // player LifeControllerImpl::UpdateHost delay load
            0x1411F7FF9ull, // player LifeControllerImpl::UpdateHost doubled damage delay load
            0x14147F590ull, // tpp::gm::soldier::impl::HoldupActionImpl::State_EndStandHoldup
            0x14147F3C0ull, // tpp::gm::soldier::impl::HoldupActionImpl::State_EndCrawlHoldup
            0x14147E210ull, // tpp::gm::soldier::impl::HoldupActionImpl drop weapon
            0x141482100ull, // tpp::gm::soldier::impl::HoldupAiImpl::StepHoldupStand
            0x141481C30ull, // tpp::gm::soldier::impl::HoldupAiImpl::StepHoldupDown
            0x1414825D0ull, // tpp::gm::soldier::impl::HoldupAiImpl unholster
            0x1414DDCD0ull, // NoticeControllerImpl::CheckNoiseNoticePlayer
            0x141573610ull, // tpp::gm::soldier::impl::RouteAiImpl::SetSubActionParamWithEdgeEvent
            0x141572160ull, // tpp::gm::soldier::impl::RouteAiImpl::SetAimingActionParam
            0x141572CBDull, // RouteAiImpl edge event move command return
            0x141572D1Bull, // RouteAiImpl edge event aim clear
            0x141573A10ull, // tpp::gm::soldier::impl::RouteAiImpl::SetSubActionParamWithNodeEvent
            0x1415729D0ull, // tpp::gm::soldier::impl::RouteAiImpl::SetLookActionParam
            0x141576468ull, // RouteAiImpl node event caution swap
            0x141574B90ull, // tpp::gm::soldier::impl::RouteAiImpl::StepEvent
            0x141484890ull, // RouteAiImpl get target position
            0x140611B10ull, // TppPermanentGimmickGatlingGun::SetAimMatrix
            0x140FF04C1ull, // barrier effect alloc gate
            0x140FF1139ull, // barrier effect populate gate
            0x14179BB50ull, // tpp::ui::hud::impl::CallMenuImpl::UpdateCallMenu
            0x14179B340ull, // tpp::ui::hud::impl::CallMenuImpl::SetCommandText
            0x141DA70B0ull, // texture prefetch controller lookup
            0x141DBBEE0ull, // texture prefetch check
            0x14105FDB0ull, // SetupWeaponInfo
            0x141307D70ull, // tpp::gm::impl::equip::RealizedEquipObjectImpl::Realize
            0x141307150ull, // tpp::gm::impl::equip::RealizedEquipObjectImpl post put motion
            0x141307EF2ull, // RealizedEquipObjectImpl motion entry index site
            0x1422A0F18ull, // equip motion entry table vtable
            0x140A02290ull, // AddMtarBlockPackagePaths
            0x140FC8020ull, // PlacedSystemImpl::Insert
            0x1409CEAE0ull, // SetWarpToPosition
            0x14118C1C0ull, // player death family tick
            0x14118E1B0ull, // player damage fall state
            0x140FF28D0ull, // player pre polling
            0x141194A80ull, // GetBasicDamageReaction
            0x140FF94D0ull, // barrier blow callback
            0x140D2DDB7ull, // bullet wall-hit SE site
            0x1417525CAull, // wall-object hit SE site
            0x141751648ull, // ricochet-spawn sound flag site
            0x140FF5BB0ull, // barrier register defense callback
            0x142ACEB58ull, // barrier defense functor
            0x140FFAAA0ull, // barrier predicate
            0x140FF4AE0ull, // barrier player destructor
            0x142BE1280ull, // main viewport global
            0x140437B60ull, // demo camera commit
            0x140421740ull, // demo camera get control root
            0x1404334B0ull, // demo camera anchor capture begin
            0x140433720ull, // demo camera anchor capture end
            0x1414AA180ull, // tpp::gm::soldier::impl::LifeControllerImpl::Update
            0x140EC2990ull, // tpp::gm::hostage::impl::LifeControllerImpl::Update
            0x141348F10ull, // animal LifeControllerImpl::Update
            0x141762410ull, // walker gear LifeControllerImpl::Update
            0x140B33670ull, // tpp::gm::vehicle::impl::GameObjectImpl::ReflectLife
            0x1409B2750ull, // LoadPlayerFv2s
            0x1409B3770ull, // LoadPlayerFv2s subset
            0x1404E39D0ull, // CreateFv2ResourceManager
            0x1409B3670ull, // GetShell
            0x1409B3170ull, // GetShellForBlock
            0x1409F1FE0ull, // GetFaceSystem
            0x1409F09D0ull, // GetFaceFovaPathIdAtFaceId
            0x1409F0860ull, // GetFaceFovaFpkPathIdAtFaceId
            0x14006F320ull, // fox::Block::Reload
            0x140A1E4D3ull, // SimpleMissionBlockController::Update checkpoint call of fox::Block::Reload
            0x1411AEC10ull, // tpp::gm::player::impl::EquipControllerImpl shield hit
            0x1411AEAE0ull, // tpp::gm::player::impl::EquipControllerImpl::IsShieldBreakableDamage
            0x140ED71E0ull, // Hostage2SoundControllerImpl::CallVoiceImpl
            0x14181D110ull, // liquid ActionCoreImpl::UpdateSleepVoice
            0x141830400ull, // liquid CarriedActionImpl::State_Carried
            0x1418377A0ull, // liquid DamageActionImpl::State_SmokeStart
            0x140F837E0ull, // StaffControllerImpl::AssignSectionCheckBoxStaff
            0x1416DBF30ull, // staff list dismiss
            0x1416D3B30ull, // ResourceManagementCallbackImpl::UpdateState
            0x1407E79E0ull, // ServerSynchronize::DestructNuclear
            0x1407F9A50ull, // FOB nuclear disposal request
            0x141280820ull, // SightManagerImpl::UpdateEspionageRadio
            0x140DC8110ull, // EspionageRadioSystemImpl::GetGroupNameFromGameObjectId
            0x14079CE30ull, // atmosphere DirectionalLightController::Update
            0x14127EEB0ull, // binocular picker centre test
            0x142BE8B90ull, // TppStarSystem instance
            0x140739A20ull, // TppStarSystem vtable slot 0
            0x1422A60F0ull, // decoy system vtable
            0x141514D60ull, // NoticeSightAiImpl radio start
            0x1415167D0ull, // NoticeSightAiImpl wait help
            0x141521B00ull, // NoticeNoiseAiImpl radio start
            0x1415232D0ull, // NoticeNoiseAiImpl wait help
            0x14151EA20ull, // NoticeNoiseAiImpl::PreUpdate
            0x142C618A0ull, // NoticeNoiseAiImpl step table
            0x140AEF070ull, // Fova2ControllerImpl put and act motion
            0x141AB82B0ull, // Animx control SetSpeedRate
            0x141AB8260ull, // Animx control SetMotionData
            0x141E19020ull, // Animx control advance motion
            0x141A95AB0ull, // Animx decode tracks
            0x140407110ull, // fox::fx effect SetWorldMatrix
            0x1400DDA00ull, // fox block package GetFile
            0x140216050ull, // fox model bone world matrix
            0x141B8EA30ull, // fox::fx effect instance Update
            0x141B26A00ull, // fox::fx effect sync
            0x141B40FA0ull, // fox::fx effect context acquire
            0x141B40F20ull, // fox::fx effect context release
            0x14033C870ull, // AK::SoundEngine::RegisterGameObj
            0x14033DB20ull, // AK::SoundEngine::UnregisterGameObj
            0x14033D030ull, // AK::SoundEngine::SetPosition
            0x14033C050ull, // AK::SoundEngine::PostEvent
            0x1403917A0ull, // CAkParameterNodeBase::SetInitialRTPC
            0x140391940ull, // CAkParameterNodeBase::SetNodeBaseParams
            0x140391FB0ull, // CAkParameterNodeBase::SetRTPC
            0x14034D6B0ull, // CAkAudioLibIndex::GetNodePtrAndAddRef
            0x14033D090ull, // AK::SoundEngine::SetRTPCValue
            0x142B9EAF0ull, // AK g_pIndex
            0x142B9EB70ull, // AK g_pRTPCMgr
            0x142B9EB78ull, // AK g_pAudioMgr
            0x142B9EB98ull, // AK g_csMain
            0x1408941F0ull, // KeySettingEvCall::SetKeySettingMenu
            0x140895180ull, // KeySettingEvCall::SetListMenu
            0x140890D20ull, // KeySettingEvCall::GetListInfoText
            0x14088EFE0ull, // KeySettingEvCall::DispKeySettingParam
            0x140896150ull, // KeySettingEvCall::SettingParamChanged
            0x140893850ull, // KeySettingEvCall::SetDecideParam
            0x1408933E0ull, // KeySettingEvCall::SetCancelParam
            0x140890BD0ull, // KeySettingEvCall::GetConnectNodeName
            0x140016F40ull, // fox kernel string format
            0x140894675ull, // KeySettingEvCall::SetKeySettingMenu sound page row list
            0x14089469Bull, // KeySettingEvCall::SetKeySettingMenu sound page row list resume
            0x141DFA330ull, // fox::ui ReadUilb
            0x140891170ull, // KeySettingEvCall::GetModelAssetForPause
            0x140896F30ull, // KeySettingEvCall::VisibleKeySetting
            0x140896550ull, // KeySettingEvCall::UpdateKeySetting
            0x141DC2020ull, // fox::ui::Layout::GetAnimation
            0x1409134B0ull, // tpp::ui::utility::GetModel
            0x140913540ull, // tpp::ui::utility::GetModelNodeText
            0x14091B940ull, // tpp::ui::utility::StartLoopUiAnimation
            0x140225B40ull, // FontData::SearchGlyphData
            0x140224726ull, // FontTextureRender::GetStringWidth default char lookup return
            0x1402251B5ull, // FontTextureRender::RegisterString default char lookup return
            0x1402252FCull, // FontTextureRender::RegisterStringW default char lookup return
            0x1407E8FF0ull, // staff combat section points
            0x1407E9170ull, // staff develop section points
            0x1407E8E30ull, // staff base development section points
            0x1407E9710ull, // staff support section points
            0x1407E9520ull, // staff spy section points
            0x1407E9310ull, // staff medical section points
            0x1411F5350ull, // player damage calc
            0x1411F92D0ull, // player life update
            0x140984C30ull, // player rattle sound body
            0x140984010ull, // player foot step sound body
            0x141192840ull, // CallDamageEffectWithReaction
            0x1423997E0ull, // suit param table
            0x142C1C2AAull, // script condition bits
            0x140900000ull, // step noise quiet scan window start
            0x141709D20ull, // walker gear sub weapon slot setup
            0x140A06700ull, // equip block controller load by equipId
            0x140A08140ull, // equip block controller load in block
            0x140A07970ull, // equip block controller find pack
            0x14170AD80ull, // WalkerGear_SetUpSubWeapon
            0x141715AF0ull, // walker gear show sidearm
            0x141724140ull, // walker gear sidearm shot
            0x140FEA420ull, // walker gear reticle update
            0x14195C3A0ull, // customize decide part
            0x140A4E340ull, // BuddyServiceImpl buddy equip settings
            0x140A4EBE0ull, // BuddyServiceImpl::ChangeGearSettings
            0x141E02B80ull, // PlayerInfo GetPartsType
            0x14093027Dull, // return after GetLocalPartsType, run style
            0x14098D41Aull, // return after GetLocalPartsType, dash sound
            0x140743509ull, // return after GetPartsType, sprint spark
            0x140743519ull, // return after GetPartsType, sprint spark
            0x140743558ull, // return after GetPartsType, sprint spark
            0x140743616ull, // return after GetPartsType, sprint spark
            0x140743626ull, // return after GetPartsType, sprint spark
            0x1407448C5ull, // return after GetPartsType, sprint spark
            0x1407448D5ull, // return after GetPartsType, sprint spark
            0x1410B52F0ull, // player dash move
            0x1410A0660ull, // player dash attack
            0x140F658E2ull, // develop row equipId read (GetAvailableEquipIDArray)
            0x140F658EDull, // develop row equipId shr (GetAvailableEquipIDArray)
            0x140F65A73ull, // develop row equipId read (Read02)
            0x140F65B03ull, // develop row equipId read (Read03)
            0x140F65B93ull, // develop row equipId read (Read04)
            0x140F65C23ull, // develop row equipId read (Read05)
            0x140F666B8ull, // develop row equipId read (Read06)
            0x140F667FCull, // develop row equipId read (Read07)
            0x140F68A73ull, // develop row equipId read (Read08)
            0x140F69234ull, // develop row equipId read (GetEquipID)
            0x140F69239ull, // develop row equipId shr (GetEquipID)
            0x140F69254ull, // develop row equipId read (GetEquipIDFromDevelopIndex)
            0x140F69259ull, // develop row equipId shr (GetEquipIDFromDevelopIndex)
            0x140F6926Dull, // develop row equipId read (GetEquipPosition)
            0x140F69873ull, // develop row equipId read (Read12)
            0x140F69953ull, // develop row equipId read (Read13)
            0x140F69EC6ull, // develop row equipId read (GetHighestGradeEuipId_Compare)
            0x140F69ECAull, // develop row equipId shr (GetHighestGradeEuipId_Compare)
            0x140F69F12ull, // develop row equipId read (GetHighestGradeEuipId_Result)
            0x140F69F17ull, // develop row equipId shr (GetHighestGradeEuipId_Result)
            0x140F69E70ull, // EquipDevelopControllerImpl::GetHighestGradeEuipId
            0x140A03587ull, // EquipIdTable InfoList rip-relative site
            0x140A03CA4ull, // EquipIdTable InfoList rip-relative site
            0x140A047A4ull, // EquipIdTable InfoList rip-relative site
            0x140A04832ull, // EquipIdTable InfoList rip-relative site
            0x140A05487ull, // EquipIdTable InfoList rip-relative site
            0x140A05B80ull, // EquipIdTable InfoList rip-relative site
            0x140A05DD2ull, // EquipIdTable InfoList rip-relative site
            0x140A05E15ull, // EquipIdTable InfoList rip-relative site
            0x140A0676Cull, // EquipIdTable InfoList rip-relative site
            0x140A07556ull, // EquipIdTable InfoList rip-relative site
            0x140A079D8ull, // EquipIdTable InfoList rip-relative site
            0x140A081BBull, // EquipIdTable InfoList rip-relative site
            0x140A0588Aull, // EquipIdTable InfoList absolute-displacement site
            0x140A058BEull, // EquipIdTable InfoList absolute-displacement site
            0x141DF5A40ull, // fox::ui::WindowManager::UpdateLayouts
            0x141DEF6D0ull, // fox::ui::WindowDaemon::GetWindowManager
            0x141DABE90ull, // fox::ui::WindowFunction::DestroyWindow
            0x141DBF3E0ull, // fox::ui::Animation::GetTimeLength
            0x141DB6800ull, // fox::ui::ModelNodeText::SetFontGroup
            0x14091B8E0ull, // tpp::ui::utility::StartSound
            0x141DCA2D0ull, // fox::ui::SoundControl::Stop
            0x142C8FAD8ull, // the SoundControl the UI graphs play through
            0x1400241F0ull, // fox::LwRWLockSection::LockShared
            0x1400243F0ull, // fox::LwRWLockSection::Unlock
            0x142B64238ull, // g_blockGroupsLock
            0x1429E3CA0ull, // first entry of the block group map
            0x1429E3CB8ull, // block group map nodes
            0x1429E3CC0ull, // block group map values
            0x141DACF80ull, // fox::ui::LayoutUtility::DisconnectComponent
            0x141DF55F0ull, // fox::ui::WindowManager::DeleteDestroyedWindows
            0x141DA9980ull, // fox::ui::ObjControl::CreateCommon
            0x1400B31F0ull, // fox::impl::EntityPtrBase::Set
            0x1409122B0ull, // tpp::ui::utility::GetGameCameraViewMatrix
            0x140913C00ull, // tpp::ui::utility::GetProjectionMatrix
            0x140912100ull, // tpp::ui::utility::GetFocalLengthZoomRatio
            0x140916A90ull, // tpp::ui::utility::IsOutsideScreen
            0x140914F80ull, // tpp::ui::utility::GetViewport
            0x1420A8068ull, // how far head markers rise per unit of zoom
            0x1421079D4ull, // the screen margin head markers use
            0x14093A480ull, // tpp::ui::utility::GetUiUtility
            0x14006F990ull, // fox::Block::Unload
            0x14006D480ull, // fox::Block::Deactivate
            0x140917730ull, // tpp::ui::utility::SetButtonIconTextureByName
            0x140917AC0ull, // tpp::ui::utility::SetPlatformButtonTexture
            0x140915DE0ull, // tpp::ui::utility::IsGamePauseMenu
            0x140915E40ull, // tpp::ui::utility::IsInDemo
            0x14091BA10ull, // tpp::ui::utility::StopSound
            0x140913FC0ull, // tpp::ui::utility::GetSoundTypeId
            0x141DBF830ull, // fox::ui::Animation::Pause
            0x141DBF390ull, // fox::ui::Animation::ForceLoop
            0x141DBFE80ull, // fox::ui::Animation::UnsetForceLoop
            0x141DABC40ull, // fox::ui::WindowFunction::AddChildWindow
            0x141DAC340ull, // fox::ui::WindowFunction::PostTriggerMessage
            0x14006C910ull, // fox::BlockMemoryDesc ctor
            0x140061AC0ull, // fox::SharedString(const char*)
            0x14001D4C0ull, // fox::SharedString copy
            0x14001D3B0ull, // fox::SharedString dtor
            0x14006D3E0ull, // fox::BlockGroup::Create
            0x14006E040ull, // fox::Block::Load(ids, count)
            0x14006CFB0ull, // fox::Block::Activate
            0x140016490ull, // fox::KernelString(const char*)
            0x1400858D0ull, // fox::Path::CInitWithString
            0x1400041C0ull, // jmp ffstd::StdFree
            0x14003DAD0ull, // fox FindKnownProject
            0x140F64993ull, // develop array record displacement site
            0x140F6499Aull, // develop array record displacement site
            0x140F649A1ull, // develop array record displacement site
            0x140F649C3ull, // develop array record displacement site
            0x140F649CAull, // develop array record displacement site
            0x140F649D1ull, // develop array record displacement site
            0x140F649D8ull, // develop array record displacement site
            0x140F649DFull, // develop array record displacement site
            0x140F649E6ull, // develop array record displacement site
            0x140F649EDull, // develop array record displacement site
            0x140F649F4ull, // develop array record displacement site
            0x140F649FBull, // develop array record displacement site
            0x140F64A02ull, // develop array record displacement site
            0x140F64A09ull, // develop array record displacement site
            0x140F64A10ull, // develop array record displacement site
            0x140F64A17ull, // develop array record displacement site
            0x140F64A1Eull, // develop array record displacement site
            0x140F64A25ull, // develop array record displacement site
            0x140F64A2Cull, // develop array record displacement site
            0x140F64A34ull, // develop array record displacement site
            0x140F64A3Bull, // develop array record displacement site
            0x140F64A4Aull, // develop array record displacement site
            0x140F64A56ull, // develop array record displacement site
            0x140F64A6Cull, // develop array record displacement site
            0x140F64A73ull, // develop array record displacement site
            0x140F64A7Aull, // develop array record displacement site
            0x140F64A81ull, // develop array record displacement site
            0x140F64A88ull, // develop array record displacement site
            0x140F64A8Full, // develop array record displacement site
            0x140F64A96ull, // develop array record displacement site
            0x140F64A9Dull, // develop array record displacement site
            0x140F64AA4ull, // develop array record displacement site
            0x140F64AABull, // develop array record displacement site
            0x140F64AB1ull, // develop array record displacement site
            0x140F64AC2ull, // develop array record displacement site
            0x140F64AC9ull, // develop array record displacement site
            0x140F64AD0ull, // develop array record displacement site
            0x140F64AD7ull, // develop array record displacement site
            0x140F64ADEull, // develop array record displacement site
            0x140F64AE4ull, // develop array record displacement site
            0x140F64AEBull, // develop array record displacement site
            0x140F64B11ull, // develop array record displacement site
            0x140F64B7Bull, // develop array record displacement site
            0x140F64BF2ull, // develop array record displacement site
            0x140F64C92ull, // develop array record displacement site
            0x140F64E10ull, // develop array record displacement site
            0x140F64E20ull, // develop array record displacement site
            0x140F64E6Cull, // develop array record displacement site
            0x140F64E81ull, // develop array record displacement site
            0x140F64EC0ull, // develop array record displacement site
            0x140F64EE0ull, // develop array record displacement site
            0x140F64EF0ull, // develop array record displacement site
            0x140F64EF7ull, // develop array record displacement site
            0x140F64F13ull, // develop array record displacement site
            0x140F64F26ull, // develop array record displacement site
            0x140F64F3Full, // develop array record displacement site
            0x140F64F46ull, // develop array record displacement site
            0x140F64F64ull, // develop array record displacement site
            0x140F6502Cull, // develop array record displacement site
            0x140F6532Eull, // develop array record displacement site
            0x140F653A5ull, // develop array record displacement site
            0x140F65487ull, // develop array record displacement site
            0x140F6551Aull, // develop array record displacement site
            0x140F65675ull, // develop array record displacement site
            0x140F65711ull, // develop array record displacement site
            0x140F65783ull, // develop array record displacement site
            0x140F65835ull, // develop array record displacement site
            0x140F658D5ull, // develop array record displacement site
            0x140F65982ull, // develop array record displacement site
            0x140F659E1ull, // develop array record displacement site
            0x140F661C0ull, // develop array record displacement site
            0x140F66200ull, // develop array record displacement site
            0x140F66298ull, // develop array record displacement site
            0x140F662B3ull, // develop array record displacement site
            0x140F66DD2ull, // develop array record displacement site
            0x140F66E13ull, // develop array record displacement site
            0x140F66E76ull, // develop array record displacement site
            0x140F66F3Full, // develop array record displacement site
            0x140F671B0ull, // develop array record displacement site
            0x140F671E5ull, // develop array record displacement site
            0x140F67213ull, // develop array record displacement site
            0x140F6724Dull, // develop array record displacement site
            0x140F674BAull, // develop array record displacement site
            0x140F674E0ull, // develop array record displacement site
            0x140F6750Cull, // develop array record displacement site
            0x140F67556ull, // develop array record displacement site
            0x140F67566ull, // develop array record displacement site
            0x140F67576ull, // develop array record displacement site
            0x140F67586ull, // develop array record displacement site
            0x140F67596ull, // develop array record displacement site
            0x140F675A6ull, // develop array record displacement site
            0x140F675ECull, // develop array record displacement site
            0x140F675FCull, // develop array record displacement site
            0x140F6760Cull, // develop array record displacement site
            0x140F6761Cull, // develop array record displacement site
            0x140F6762Cull, // develop array record displacement site
            0x140F6763Cull, // develop array record displacement site
            0x140F67675ull, // develop array record displacement site
            0x140F676BAull, // develop array record displacement site
            0x140F676F0ull, // develop array record displacement site
            0x140F67733ull, // develop array record displacement site
            0x140F6778Cull, // develop array record displacement site
            0x140F67D70ull, // develop array record displacement site
            0x140F67D89ull, // develop array record displacement site
            0x140F68040ull, // develop array record displacement site
            0x140F68091ull, // develop array record displacement site
            0x140F680A7ull, // develop array record displacement site
            0x140F680ADull, // develop array record displacement site
            0x140F680B7ull, // develop array record displacement site
            0x140F681E3ull, // develop array record displacement site
            0x140F681F6ull, // develop array record displacement site
            0x140F6823Cull, // develop array record displacement site
            0x140F682C7ull, // develop array record displacement site
            0x140F68327ull, // develop array record displacement site
            0x140F6837Full, // develop array record displacement site
            0x140F683D3ull, // develop array record displacement site
            0x140F68406ull, // develop array record displacement site
            0x140F68450ull, // develop array record displacement site
            0x140F684D0ull, // develop array record displacement site
            0x140F685EBull, // develop array record displacement site
            0x140F686ABull, // develop array record displacement site
            0x140F68838ull, // develop array record displacement site
            0x140F68920ull, // develop array record displacement site
            0x140F6893Dull, // develop array record displacement site
            0x140F6895Aull, // develop array record displacement site
            0x140F68977ull, // develop array record displacement site
            0x140F68994ull, // develop array record displacement site
            0x140F689B1ull, // develop array record displacement site
            0x140F689CEull, // develop array record displacement site
            0x140F689EBull, // develop array record displacement site
            0x140F68E6Cull, // develop array record displacement site
            0x140F68ECCull, // develop array record displacement site
            0x140F68F18ull, // develop array record displacement site
            0x140F68F28ull, // develop array record displacement site
            0x140F68F38ull, // develop array record displacement site
            0x140F68F48ull, // develop array record displacement site
            0x140F68F58ull, // develop array record displacement site
            0x140F68F68ull, // develop array record displacement site
            0x140F68FBAull, // develop array record displacement site
            0x140F68FCAull, // develop array record displacement site
            0x140F68FDAull, // develop array record displacement site
            0x140F68FEAull, // develop array record displacement site
            0x140F68FFAull, // develop array record displacement site
            0x140F6900Aull, // develop array record displacement site
            0x140F6904Full, // develop array record displacement site
            0x140F6909Eull, // develop array record displacement site
            0x140F690DBull, // develop array record displacement site
            0x140F69124ull, // develop array record displacement site
            0x140F69178ull, // develop array record displacement site
            0x140F69340ull, // develop array record displacement site
            0x140F6954Cull, // develop array record displacement site
            0x140F69588ull, // develop array record displacement site
            0x140F695C9ull, // develop array record displacement site
            0x140F695F7ull, // develop array record displacement site
            0x140F696A3ull, // develop array record displacement site
            0x140F696E9ull, // develop array record displacement site
            0x140F69725ull, // develop array record displacement site
            0x140F69759ull, // develop array record displacement site
            0x140F69E22ull, // develop array record displacement site
            0x140F6A569ull, // develop array record displacement site
            0x140F6A5FCull, // develop array record displacement site
            0x140F6A692ull, // develop array record displacement site
            0x140F6A730ull, // develop array record displacement site
            0x140F6A8ADull, // develop array record displacement site
            0x140F6AC4Eull, // develop array record displacement site
            0x140F6AC75ull, // develop array record displacement site
            0x140F6AD2Eull, // develop array record displacement site
            0x140F6ADA0ull, // develop array record displacement site
            0x140F6ADD0ull, // develop array record displacement site
            0x140F6AE30ull, // develop array record displacement site
            0x140F6BDCEull, // develop array record displacement site
            0x140F6BE16ull, // develop array record displacement site
            0x140F6BE49ull, // develop array record displacement site
            0x140F6BEEEull, // develop array record displacement site
            0x140F6BF33ull, // develop array record displacement site
            0x140F6BF67ull, // develop array record displacement site
            0x140F6C209ull, // develop array record displacement site
            0x140F6C26Dull, // develop array record displacement site
            0x140F6C2A7ull, // develop array record displacement site
            0x140F6C2CDull, // develop array record displacement site
            0x140F6C364ull, // develop array record displacement site
            0x140F6C39Bull, // develop array record displacement site
            0x140F6C3C2ull, // develop array record displacement site
            0x140F6C770ull, // develop array record displacement site
            0x140F6C7B5ull, // develop array record displacement site
            0x140F6C869ull, // develop array record displacement site
            0x140F6C876ull, // develop array record displacement site
            0x140F6C8C3ull, // develop array record displacement site
            0x140F6C927ull, // develop array record displacement site
            0x140F6CAB3ull, // develop array record displacement site
            0x140F6CAD9ull, // develop array record displacement site
            0x140F6CB19ull, // develop array record displacement site
            0x140F6CB37ull, // develop array record displacement site
            0x140F6CBC0ull, // develop array record displacement site
            0x140F6CC31ull, // develop array record displacement site
            0x140F6CD65ull, // develop array record displacement site
            0x140F6CD8Bull, // develop array record displacement site
            0x140F6CDB0ull, // develop array record displacement site
            0x140F6CDD0ull, // develop array record displacement site
            0x140F6CDF0ull, // develop array record displacement site
            0x140F6CE20ull, // develop array record displacement site
            0x140F6CEFAull, // develop array record displacement site
            0x140F6CF4Aull, // develop array record displacement site
            0x140F6CFB1ull, // develop array record displacement site
            0x140F6CFF2ull, // develop array record displacement site
            0x140F6D130ull, // develop array record displacement site
            0x140F6D1B1ull, // develop array record displacement site
            0x140F6D21Aull, // develop array record displacement site
            0x140F6D283ull, // develop array record displacement site
            0x140F6D3B1ull, // develop array record displacement site
            0x140F6D452ull, // develop array record displacement site
            0x140F6D4B6ull, // develop array record displacement site
            0x140F6D4E0ull, // develop array record displacement site
            0x140F6D5E3ull, // develop array record displacement site
            0x140F6D838ull, // develop array record displacement site
            0x140F6D9C0ull, // develop array record displacement site
            0x140F6DA80ull, // develop array record displacement site
            0x140F6DAF2ull, // develop array record displacement site
            0x140F6DAFBull, // develop array record displacement site
            0x140F6DB8Dull, // develop array record displacement site
            0x140F6DC8Full, // develop array record displacement site
            0x140F6DEF8ull, // develop array record displacement site
            0x140F6E07Cull, // develop array record displacement site
            0x140F6E0E2ull, // develop array record displacement site
            0x140F6E0EAull, // develop array record displacement site
            0x140F6E160ull, // develop array record displacement site
            0x140F6E170ull, // develop array record displacement site
            0x140F6E540ull, // develop array record displacement site
            0x140F6E60Full, // develop array record displacement site
            0x140F6E65Bull, // develop array record displacement site
            0x140F6E680ull, // develop array record displacement site
            0x140F6E69Cull, // develop array record displacement site
            0x140F6E6BBull, // develop array record displacement site
            0x140F6E6D0ull, // develop array record displacement site
            0x140F6E6F8ull, // develop array record displacement site
            0x140F6E707ull, // develop array record displacement site
            0x140F6E720ull, // develop array record displacement site
            0x140F6E757ull, // develop array record displacement site
            0x140F6E798ull, // develop array record displacement site
            0x140F6E7A8ull, // develop array record displacement site
            0x140F6E7C0ull, // develop array record displacement site
            0x140F6E7C7ull, // develop array record displacement site
            0x140F6E7CEull, // develop array record displacement site
            0x140F6E7D5ull, // develop array record displacement site
            0x140F6E7DCull, // develop array record displacement site
            0x140F6E7E3ull, // develop array record displacement site
            0x140F6E7EBull, // develop array record displacement site
            0x140F6E814ull, // develop array record displacement site
            0x140F6E849ull, // develop array record displacement site
            0x140F6E878ull, // develop array record displacement site
            0x140F6E887ull, // develop array record displacement site
            0x140F6E94Full, // develop array record displacement site
            0x140F6EA93ull, // develop array record displacement site
            0x140F6EAB2ull, // develop array record displacement site
            0x140F6EAE0ull, // develop array record displacement site
            0x140F6EE30ull, // develop array record displacement site
            0x140F6EE3Aull, // develop array record displacement site
            0x140F6EE4Bull, // develop array record displacement site
            0x140F6EE5Aull, // develop array record displacement site
            0x140F6EE62ull, // develop array record displacement site
            0x140F6EE69ull, // develop array record displacement site
            0x140F6EE77ull, // develop array record displacement site
            0x140F6EE86ull, // develop array record displacement site
            0x140F6EE8Eull, // develop array record displacement site
            0x140F6EE95ull, // develop array record displacement site
            0x140F6EEA3ull, // develop array record displacement site
            0x140F6EEB2ull, // develop array record displacement site
            0x140F6EEBAull, // develop array record displacement site
            0x140F6EEC1ull, // develop array record displacement site
            0x140F6EECFull, // develop array record displacement site
            0x140F6EEDEull, // develop array record displacement site
            0x140F6EEE6ull, // develop array record displacement site
            0x140F6EEEDull, // develop array record displacement site
            0x140F6EEFBull, // develop array record displacement site
            0x140F6EF0Aull, // develop array record displacement site
            0x140F6EF12ull, // develop array record displacement site
            0x140F6EF19ull, // develop array record displacement site
            0x140F6EF27ull, // develop array record displacement site
            0x140F6EF36ull, // develop array record displacement site
            0x140F6EF3Eull, // develop array record displacement site
            0x140F6EF45ull, // develop array record displacement site
            0x140F6EF53ull, // develop array record displacement site
            0x140F6EF62ull, // develop array record displacement site
            0x140F6EF6Aull, // develop array record displacement site
            0x140F6EF71ull, // develop array record displacement site
            0x140F6EF7Full, // develop array record displacement site
            0x140F6EF8Eull, // develop array record displacement site
            0x140F6EF96ull, // develop array record displacement site
            0x140F6EF9Dull, // develop array record displacement site
            0x140F6EFB5ull, // develop array record displacement site
            0x140F6F007ull, // develop array record displacement site
            0x140F6F0A6ull, // develop array record displacement site
            0x140F6F122ull, // develop array record displacement site
            0x140F6F1CEull, // develop array record displacement site
            0x140F6F671ull, // develop array record displacement site
            0x140F6F6C0ull, // develop array record displacement site
            0x140F6F6CEull, // develop array record displacement site
            0x140F6F6DCull, // develop array record displacement site
            0x140F6F6EAull, // develop array record displacement site
            0x140F6F6F8ull, // develop array record displacement site
            0x140F6F703ull, // develop array record displacement site
            0x140F6F711ull, // develop array record displacement site
            0x140F6F72Aull, // develop array record displacement site
            0x140F6F739ull, // develop array record displacement site
            0x140F6F749ull, // develop array record displacement site
            0x140F6F758ull, // develop array record displacement site
            0x140F6F770ull, // develop array record displacement site
            0x140F6F777ull, // develop array record displacement site
            0x140F6F78Cull, // develop array record displacement site
            0x140F6F7FEull, // develop array record displacement site
            0x140F6F80Eull, // develop array record displacement site
            0x140F6F824ull, // develop array record displacement site
            0x140F6F858ull, // develop array record displacement site
            0x140F6F87Dull, // develop array record displacement site
            0x140F6F889ull, // develop array record displacement site
            0x140F6F890ull, // develop array record displacement site
            0x140F6F8ACull, // develop array record displacement site
            0x140F6F8C2ull, // develop array record displacement site
            0x140F6F8DBull, // develop array record displacement site
            0x140F6F8E2ull, // develop array record displacement site
            0x140F6F90Bull, // develop array record displacement site
            0x140F6F9BFull, // develop array record displacement site
            0x140F6FA66ull, // develop array record displacement site
            0x140F6FA76ull, // develop array record displacement site
            0x140F6FA9Dull, // develop array record displacement site
            0x140F6FAC0ull, // develop array record displacement site
            0x140F6FAD0ull, // develop array record displacement site
            0x140F6FAD7ull, // develop array record displacement site
            0x140F6FAF3ull, // develop array record displacement site
            0x140F6FB06ull, // develop array record displacement site
            0x140F6FB1Full, // develop array record displacement site
            0x140F6FB26ull, // develop array record displacement site
            0x140F6FB60ull, // develop array record displacement site
            0x140F6FBA9ull, // develop array record displacement site
            0x140F6FBEAull, // develop array record displacement site
            0x140F6FC19ull, // develop array record displacement site
            0x140F6FC2Eull, // develop array record displacement site
            0x140F6FC35ull, // develop array record displacement site
            0x140F6FC3Dull, // develop array record displacement site
            0x140F6FC44ull, // develop array record displacement site
            0x140F6FC4Cull, // develop array record displacement site
            0x140F6FC9Cull, // develop array record displacement site
            0x140F6FD1Aull, // develop array record displacement site
            0x140F6FD4Bull, // develop array record displacement site
            0x140F6FD5Dull, // develop array record displacement site
            0x140F6FD7Dull, // develop array record displacement site
            0x140F6FD8Cull, // develop array record displacement site
            0x140F6FDB3ull, // develop array record displacement site
            0x140F6FDC5ull, // develop array record displacement site
            0x140F6FDE0ull, // develop array record displacement site
            0x140F6FDEFull, // develop array record displacement site
            0x140F6FF36ull, // develop array record displacement site
            0x140F700F5ull, // develop array record displacement site
            0x140F7010Full, // develop array record displacement site
            0x140F70116ull, // develop array record displacement site
            0x140F70135ull, // develop array record displacement site
            0x140F70153ull, // develop array record displacement site
            0x140F7017Bull, // develop array record displacement site
            0x140F7018Eull, // develop array record displacement site
            0x140F70195ull, // develop array record displacement site
            0x147F32D65ull, // develop array record displacement site
            0x147F32D87ull, // develop array record displacement site
            0x147F32DA6ull, // develop array record displacement site
            0x140F64DBDull, // develop array row bound site
            0x140F652EBull, // develop array row bound site
            0x140F655D0ull, // develop array row bound site
            0x140F657ABull, // develop array row bound site
            0x140F65809ull, // develop array row bound site
            0x140F658A9ull, // develop array row bound site
            0x140F6594Dull, // develop array row bound site
            0x140F65C80ull, // develop array row bound site
            0x140F6693Aull, // develop array row bound site
            0x140F66DA1ull, // develop array row bound site
            0x140F66F8Cull, // develop array row bound site
            0x140F6700Aull, // develop array row bound site
            0x140F670D0ull, // develop array row bound site
            0x140F672C5ull, // develop array row bound site
            0x140F6747Eull, // develop array row bound site
            0x140F6780Aull, // develop array row bound site
            0x140F6792Bull, // develop array row bound site
            0x140F67DDAull, // develop array row bound site
            0x140F68050ull, // develop array row bound site
            0x140F68227ull, // develop array row bound site
            0x140F6829Aull, // develop array row bound site
            0x140F68317ull, // develop array row bound site
            0x140F68362ull, // develop array row bound site
            0x140F683BDull, // develop array row bound site
            0x140F684DEull, // develop array row bound site
            0x140F6854Aull, // develop array row bound site
            0x140F68683ull, // develop array row bound site
            0x140F68857ull, // develop array row bound site
            0x140F68E1Eull, // develop array row bound site
            0x140F69202ull, // develop array row bound site
            0x140F6931Full, // develop array row bound site
            0x140F694CDull, // develop array row bound site
            0x140F69CBBull, // develop array row bound site
            0x140F69E94ull, // develop array row bound site
            0x140F6A26Aull, // develop array row bound site
            0x140F6A542ull, // develop array row bound site
            0x140F6A62Aull, // develop array row bound site
            0x140F6A66Aull, // develop array row bound site
            0x140F6A74Aull, // develop array row bound site
            0x140F6BD55ull, // develop array row bound site
            0x140F6BFC5ull, // develop array row bound site
            0x140F6C1E5ull, // develop array row bound site
            0x140F6C415ull, // develop array row bound site
            0x140F6C5C5ull, // develop array row bound site
            0x140F6C79Cull, // develop array row bound site
            0x140F6C893ull, // develop array row bound site
            0x140F6C8F8ull, // develop array row bound site
            0x140F6C993ull, // develop array row bound site
            0x140F6CBF0ull, // develop array row bound site
            0x140F6CE13ull, // develop array row bound site
            0x140F6DD95ull, // develop array row bound site
            0x140F6DE4Full, // develop array row bound site
            0x140F6E501ull, // develop array row bound site
            0x140F6E73Aull, // develop array row bound site
            0x140F6EB19ull, // develop array row bound site
            0x140F6EB67ull, // develop array row bound site
            0x140F6EBCEull, // develop array row bound site
            0x140F6EBFFull, // develop array row bound site
            0x140F6EC74ull, // develop array row bound site
            0x140F6ECBBull, // develop array row bound site
            0x140F6ED1Full, // develop array row bound site
            0x140F6EDA0ull, // develop array row bound site
            0x140F6F652ull, // develop array row bound site
            0x140F71002ull, // develop array row bound site
            0x140FE5295ull, // develop array sweep allowed site
            0x140FFE8C8ull, // develop array sweep allowed site
            0x14127A055ull, // develop array sweep allowed site
            0x14127A37Eull, // develop array sweep allowed site
            0x141311057ull, // develop array sweep allowed site
            0x1413110C5ull, // develop array sweep allowed site
            0x141A582B5ull, // develop array sweep allowed site
            0x142643B6Dull, // develop array sweep allowed site
            0x142687645ull, // develop array sweep allowed site
            0x1426B4C85ull, // develop array sweep allowed site
            0x142747999ull, // develop array sweep allowed site
            0x1427480D5ull, // develop array sweep allowed site
            0x14274839Dull, // develop array sweep allowed site
            0x1430A6147ull, // develop array sweep allowed site
            0x1430B5DBEull, // develop array sweep allowed site
            0x1430BA26Full, // develop array sweep allowed site
            0x1431034EFull, // develop array sweep allowed site
            0x14310566Bull, // develop array sweep allowed site
            0x14310B923ull, // develop array sweep allowed site
            0x1434C8182ull, // develop array sweep allowed site
            0x148C7381Eull, // develop array sweep allowed site
            0x149A7361Cull, // develop array sweep allowed site
            0x149A7CBC8ull, // develop array sweep allowed site
            0x14308066Full, // develop array sweep allowed site
            0x14308C91Bull, // develop array sweep allowed site
            0x14308F67Bull, // develop array sweep allowed site
            0x14309FCCFull, // develop array sweep allowed site
            0x1457E24E7ull, // develop array sweep allowed site
            0x145AAD062ull, // develop array sweep allowed site
            0x149C8B054ull, // develop array sweep allowed site
            0x149CAB88Cull, // develop array sweep allowed site
            0x149CAE708ull, // develop array sweep allowed site
            0x149CE09D8ull, // develop array sweep allowed site
            0x149CF0EF8ull, // develop array sweep allowed site
            0x149CF9170ull, // develop array sweep allowed site
            0x149D7C1C9ull, // develop array sweep allowed site
            0x140F65084ull, // develop array patch: block alloc size
            0x140F64908ull, // develop array patch: ctor loop count
            0x140F6496Full, // develop array patch: ctor parallel base
            0x140F6C82Bull, // develop array patch: reset records size
            0x140F6C83Aull, // develop array patch: reset parallel base
            0x140F6C843ull, // develop array patch: reset parallel size
            0x140F6E105ull, // develop array patch: regist scan bound
            0x140F6E117ull, // develop array patch: regist probe disp
            0x140F6E135ull, // develop array patch: regist store disp+0
            0x140F6E140ull, // develop array patch: regist store disp+4
            0x140F6E14Bull, // develop array patch: regist store disp+8
            0x140F6E156ull, // develop array patch: regist store disp+C
            0x140F69240ull, // develop array patch: equipid-from-idx bound
            0x140F6D15Aull, // develop array patch: is-visible bound
            0x140F68DF0ull, // develop array patch: grade getter bound
            0x140F68CB0ull, // develop array patch: packed getter bound
            0x140F68C75ull, // develop array patch: count-by-type scan bound
            0x140F6AA62ull, // develop array patch: provider count bound
            0x140F6A9BBull, // develop array patch: provider fill bound
            0x140F6CD04ull, // develop array patch: menu list gate bound
            0x140F64AF1ull, // develop array patch: equip-equiped bits alloc words
            0x140F64B1Aull, // develop array patch: equip-equiped bits clear size
            0x140F693DAull, // develop array byte fix: sibling retire loop index init
            0x140F69400ull, // develop array byte fix: sibling retire loop index read
            0x140F6944Cull, // develop array byte fix: sibling retire loop index inc
            0x140F6944Full, // develop array byte fix: sibling retire loop index test
            0x140F6AD5Full, // develop array byte fix: develop-list count miss branch
            0x140F6FF79ull, // develop array byte fix: develop-list apply miss branch
            0x140F6C820ull, // develop array blockReset
            0x140F68D00ull, // develop array findByEquipId
            0x140F65930ull, // develop array listDevCount
            0x140F657E0ull, // develop array listDevFill
            0x140F6B020ull, // develop array equipIdCount
            0x140F6B430ull, // develop array equipIdRow
            0x140F67000ull, // develop array getDerivationDevelopIndex
            0x140F6A660ull, // develop array siblingCount
            0x14239B460ull, // develop array blockVtblA
            0x14239BCD0ull, // develop array blockVtblB
            0x14239B530ull, // develop array base20VtblA
            0x14239BDA0ull, // develop array base20VtblB
            0x140F64BC0ull, // develop array edcDtor
            0x140E6EA00ull, // tpp::gm::horse::impl::DamageActionImpl::State_Whiii
            0x1412803D0ull, // tpp::gm::player::impl::SightManagerImpl::UpdateAutoMarkingToMecha
            0x1409C5750ull, // tpp::gm::player::impl::Player2Impl::IsPlayingVoice
            0x141236940ull, // RideHorseActionPluginImpl_State_Idle
            0x140E61BB0ull, // tpp::gm::horse::impl::BasicAction2Impl::State_RideOnIdle
            0x140D6FD90ull, // tpp::gm::impl::cp::EvasionAiImpl::DecrementPhaseCounter
            0x140D6B7E0ull, // FUN_140d6b7e0
            0x14239A760ull, // SupportHeli_PartsTable
            0x140F69B90ull, // EquipDevelopControllerImpl_GetHeliPartsId
            0x140F69B40ull, // EquipDevelopControllerImpl_FindHeliPartsRow
            0x14195C540ull, // CustomizeSelectorCallbackImpl_DecideActWindowPartHelicopter
            0x142A78060ull, // SupportHeli_CustomizeCodec
            0x1422C5FD0ull, // SupportHeli_CustomizeCodecVtable
            0x140DEEF00ull, // CustomizeControllerImpl_SetSubWeaponAmmo
            0x140DEF0D0ull, // CustomizeControllerImpl_SetSubWeaponPods
            0x140E0EA00ull, // LauncherActionImpl_Execute
            0x140E0FC50ull, // LauncherActionImpl_Wake
            0x140E29110ull, // SupportCombatAiImpl_StepRocketAim
            0x140E286A0ull, // SupportCombatAiImpl_StepMissile
            0x141337760ull, // ShellSystemImpl_Fire
            0x140DD56F0ull, // TppHeli2_LuaCommands
            0x140DC1080ull, // EquipSystemImpl_RegisterEquip
            0x140DC6770ull, // EquipSystemImpl_UnregisterEquip
            0x140E067D0ull, // GunActionImpl_FireBarrel
            0x14195A120ull, // CustomizeSelectorCallbackImpl_HoverHeliPart
            0x1416684D0ull, // EquipDetailsCallbackImpl_SetupHeliCustomizeDetails
            0x140A0FE50ull, // BlastSystemImpl_Blast
            0x141330DF0ull, // ShellImpl_UpdateAction
            0x14132C870ull, // ShellImpl_RequestSmokeCloud
            0x1407E66A0ull, // GetFobStatusCallback_AnnounceDefenseWon
            0x1407E65B0ull, // GetFobStatusCallback_AnnounceDefenseLost
            0x14158A460ull, // Soldier2SoundControllerImpl_CallVoiceImpl
            0x140ECD580ull, // HostageRideVehicleActionImpl_State_GetInAction
            0x140ECDAC0ull, // HostageRideVehicleActionImpl_State_GetOutAction
            0x140F33800ull, // MbDvcMissionListRecordCallFunc_Start
            0x140F0B780ull, // MbDvcMapCallbackBaseImpl_UpdateLocationInfoView
            0x140F40D70ull, // FUN_140F40D70 called by MbDvcSideOpsRecordCallFunc_Start
            0x140F330E0ull, // FUN_140F330E0 called by MbDvcMissionListCallbackImpl prefab setup
            0x140F31A70ull, // MbDvcMissionListCallbackImpl_ActivatePrefabTablistParameter
            0x140F3479Aull, // MbDvcMissionListCallbackImpl_Update tab type read
            0x140F3553Aull, // MbDvcMissionListCallbackImpl_UpdateMissionListParameters tab filter
            0x140915B10ull, // tpp::ui::utility::IsDirButtonPress
            0x140915A80ull, // tpp::ui::utility::IsDirButtonLong
            0x140910110ull, // tpp::ui::utility::CheckIllegalDirButtonInput
            0x14085FC50ull, // tpp::ui::menu::UiCommonDataManager::GetEnableDirButton
            0x142A10750ull, // PrefabSystemImpl layout name per prefab type, 20 x u64
            0x14227EE50ull, // MotherBaseDeviceTopMenuTableImpl vtable, 49 slots
            0x140966320ull, // MotherBaseDeviceTopMenuTableImpl factory
            0x140916060ull, // tpp::ui::utility::IsInFreePlayHeliMission
            0x140995FA0ull, // mov rax,[rcx+0x70]; ret (JP: folded twin)
            0x140876870ull, // tpp::ui::menu::MbDvcManageEv Update
            0x1409172E0ull, // tpp::ui::utility::SendTriggerToParent
            0x14086EEB0ull, // tpp::ui::menu::MbTopMenuSelectorEv::Update
            0x14086D4C0ull, // MbTopMenuSelectorEv_GetItemIcon
            0x140510FA0ull, // fox::uix::impl::PrefabSystemImpl::GetPrefabLayout
            0x140510B30ull, // PrefabSystemImpl vtable +0x100
            0x140510B60ull, // PrefabSystemImpl vtable +0x108
            0x1402051D2ull, // vertex-stream pool creation: mov edx,3496 before its constructor call (real name unknown)
            0x142B77F88ull, // vertex-stream pool instance, read by SetVertexStream
            0x140205083ull, // ModelManager creation: mov edx,1826 before its constructor call
            0x142B743A0ull, // fox::gr::dg::ModelManager::s_Instance
            0x14020514Eull, // VertexBufferManager creation: mov edx,2720 before its constructor call
            0x142B74A80ull, // fox::gr::dg::VertexBufferManager::s_Instance
            0x140205190ull, // IndexBufferManager creation: mov edx,2544 before its constructor call
            0x142B74A88ull, // fox::gr::dg::IndexBufferManager::s_Instance
            0x14146CDC0ull, // tpp::gm::soldier::impl::DownActionImpl::State_ToDyingSufferIdle
            0x14146CB50ull, // tpp::gm::soldier::impl::DownActionImpl::State_DyingSufferToDyingSitup
            0x14144B030ull, // SoldierDamageActionImpl_State_DamageReaction
            0x14121C620ull, // tpp::gm::player::impl::RideHeliActionPluginImpl::ExecActionButtonFunction
            0x14121E4F0ull, // tpp::gm::player::impl::RideHeliActionPluginImpl::IsForcedOffGun
            0x14095C740ull, // tpp::ui::menu::impl::MissionPreparationSystemImpl::StartPopup
            0x14095D2C0ull, // MissionPreparationSystemImpl_GetPopupResult
            0x141A10FA0ull, // lua_error
            0x141A209B0ull, // luaG_errormsg
            0x140C4FE00ull, // tpp::ui::hud::impl::TppBonusPopupImpl::SetBgTexture
            0x141BB6190ull, // FoxGeo_CheckSphere
            0x142C79640ull, // FoxGeo_WorldTable
            0x140928A50ull, // MbDvcSoundControllerImpl_RequestVoice
            0x140958250ull, // MissionPreparationSystemImpl_IsFobSortie
            0x141DBAA80ull, // fox::ui::ButtonMapper::SetDefaultButtonMap
            0x142C90800ull, // ButtonMapper_Map
            0x141DD5C60ull, // FontManager_GetIconTextureCoord
            0x14092CD90ull, // tpp::ui::menu::UiDepend::ActHudChecking
            0x141DA70B0ull, // fox::ui::Daemon::GetDaemonPrefetchController
            0x141DBB760ull, // fox::ui::PrefetchTextureController::CreatePrefetchTexture
            0x141DBBDC0ull, // fox::ui::PrefetchTextureController::GetPrefetchTexture
            0x140917960ull, // Ui_SetFontIconTexture
            0x141694330ull, // FobMission2CallbackImpl_Update
            0x141DBA710ull, // fox::ui::ButtonMapper::GetKeyCodeEqual
            0x1409BF290ull, // Player2Impl_CallVoiceWithDialogueEvent
            0x14003DB30ull, // Fox_PathCodeFromString
            0x140B16D90ull, // RealizedSoldier2Impl_ExecuteSerially
            0x140B18DF0ull, // RealizedSoldier2Impl_StopAllFovaResourceManager
            0x14021DF8Full, // TextureStoragePool_SizeSite
            0x142B79B18ull, // TextureStoragePool_Streamer
            0x1402A6D40ull, // TextureStoragePool_AllocBlock
            0x140994CB0ull, // MessageArgs_GetValueAsDouble
            0x140ADA200ull, // ScriptUpdater_ReceiveMessage
            0x1406EF1C0ull, // TppSubViewport create
            0x1401BD970ull, // fox::gr::Render::GetDefault
            0x1401BD290ull, // fox::gr::Render::AddViewport
            0x1401BD620ull, // fox::gr::Render::DeleteViewport
            0x1401BB560ull, // fox::gr::Viewport::SetScene
            0x1401BB1E0ull, // fox::gr::Viewport::SetExposure
            0x1401B9490ull, // fox::gr::GrViewport::UpdateFromCamera
            0x140276EE0ull, // fox::gr::Material::BindTexture
            0x1408B4AA0ull, // tpp::ui::hud::EquipCrossEvCall::SpecialItemAct
            0x1408ABC80ull, // EquipCrossEvCall cursor to panel index
            0x140A29E60ull, // TppEquip.AddToEquipIdTable (vanilla Lua function)
            0x140FC2CD0ull, // tpp::gm::impl::placed::PlacedImpl::ExecuteAfter
            0x140FC3520ull, // tpp::gm::impl::placed::PlacedImpl::SendOnActivatePlacedMessage
            0x14134DD87ull, // Soldier2InterrogateUtil::CheckMine, placed id load before the 0x19D mine compare
            0x140C7FF7Cull, // buddydog mine sniffing, placed id load before the 0x19D mine compare
            0x140A2A350ull, // tpp::gm::impl::equip::EquipIdTableImpl::GetEquipIdFromStrCode32Impl
            0x1414FF7E0ull, // NoticeDownComradeAiImpl::StepRadio
            0x1413BD95Full, // Soldier2CombatUtil::GrenadeUpdate, throw command equipId store
            0x1413F55D9ull, // CombatInDoorImpl::StepGrenade, throw command equipId store
            0x1413FBB8Dull, // CombatInDoorImpl::ThrowGrenadeInDoor, throw command equipId store
            0x140A46143ull, // GetBuddyGameObjectType
            0x140A48284ull, // ProcSignal
            0x140A46A00ull, // GetPathAtSub
            0x140A499A0ull, // SubBlockSetting
            0x140A515B0ull, // InitBuddyCommand
            0x140A51690ull, // InitBuddyCommandForStatus
            0x140876E73ull, // UpdateCallBuddyPhaseOpen
            0x140B95DB0ull, // AdjustLeg
            0x1415B4C90ull, // VehicleNoticeDownComradeAiImpl::StepRadio
            0x1413BDAE0ull, // tpp::gm::soldier::impl::Soldier2Util::HasOtherSoldierInPoint
            0x14056B790ull, // TppPermanentGimmickGameObject<TppPermanentGimmickDoorUnit>::GetUnit64 (thunk)
            0x142BD80C0ull, // DoorGameObject s_gameObjectHandle (global, door game object at +0x20)
            0x140600E90ull, // tpp::gk::TppPermanentGimmickDoorUnit::GetDoorCenter
            0x140A4E830ull, // CanSortieBuddyType
            0x140A4E870ull, // CanSortieBuddyType
            0x1400D7C60ull, // fox::LuaPushVector3(lua_State*, Vector3 const&)
            0x14163E470ull, // SetupBuddyList
            0x140D73CC0ull, // MemberRoleControllerImpl::AssignSelectAlertLost
            0x140D63550ull, // CommandPost2Impl::SetMemberRole_
            0x141310F10ull, // SecurityCamera2Impl::ProcessLuaCommand
            0x14193A110ull, // UavImpl::ProcessLuaCommand
            0x140B305B0ull, // vehicle::TppVehicle2_LuaCommands
            0x141356170ull, // Soldier2Impl::ProcessLuaCommand
            0x140E85E10ull, // Hostage2Impl::ProcessLuaCommand
            0x1414975C0ull, // InvestigateAiImpl::PreUpdate
            0x141706190ull, // WalkerGear2Impl::ProcessLuaCommand
            0x1418BB820ull, // enemy::sahelan::TppSahelan2_LuaCommands
            0x1415D7440ull, // tpp::gm::impl::throwing::ThrowingImpl::OnOffenseHit
            0x141039020ull, // tpp::gm::player::impl::attack::AttackActionImpl::CalcThrowingVelocity
            0x140600880ull, // tpp::gk::DoorDefenceCallback::Exec
            0x14060EE50ull, // tpp::gk::GatlingGunDefenseCallback::Exec
            0x140616EA0ull, // tpp::gk::ImportantBreakableDefenseCallback::Exec
            0x14061C780ull, // tpp::gk::MachineGunDefenseCallback::Exec
            0x140625160ull, // tpp::gk::MortarDefenseCallback::Exec
            0x14062A510ull, // tpp::gk::SearchLightDefenseCallback::Exec
            0x14062DE60ull, // tpp::gk::WatchTowerDefenseCallback::Exec
            0x140630CF0ull, // tpp::gk::IrSensorDefCallback::Exec
            0x140636940ull, // tpp::gk::EventAnimationDefenseCallback::Exec
            0x140639540ull, // tpp::gk::BridgeDefenseCallback::Exec
            0x14063A5A0ull, // tpp::gk::WaterTowerDefenseCallback::Exec
            0x14063D8C0ull, // tpp::gk::ToiletDefenceCallback::Exec
            0x1406418B0ull, // tpp::gk::RadioCasseteDefenseCallback::Exec
            0x1405D2040ull, // buddy viewer command
            0x140A44740ull, // CallBuddyInit
            0x140956C70ull, // GetParamBuddyPartsFromVars
            0x140A50EE0ull, // GetBuddyService
            0x140FAA740ull, // CreateController
            0x140FAA8D0ull, // NoticeSensorImpl_Init
            0x140FAA8FEull, // notice table sizes, sizing pass
            0x140FAA96Eull, // notice table sizes, build pass
            0x140FAA9EEull, // notice game socket slots
            0x140FAAA31ull, // notice synchronizer records
            0x140A1F970ull, // Mission_GetMissionFlags
            0x140EF7370ull, // MbDvcSeekPrevSubtitles
            0x140EF7260ull, // MbDvcSeekNextSubtitles
            0x140EF6570ull, // MbDvcPauseForSeek
            0x140FC7510ull, // tpp::gm::impl::placed::PlacedSystemImpl::CreateCorrelation
            0x140B5CAE0ull, // RealizedWalkerGear2Impl::Execute1
            0x140D5BA80ull, // CommandPost2Impl::Initialize
            0x140D5B220ull, // CommandPost2Impl free member slots (0x168 - total)
            0x140D59050ull, // CommandPost2Impl::CreateController
            0x141A6B250ull, // fox::anim::AnimControl::ActLastPlugins
            0x140215CD0ull, // fox::Skeleton::BoneMatrixAccessor::BoneMatrixAccessor
            0x140216370ull, // fox::Skeleton::BoneMatrixAccessor::SetBoneWorldMatrix
            0x141A6CAA0ull, // fox::anim::AnimControl pose -> skeleton bone matrices
            0x142C2DC70ull, // tpp::gm::walkergear::impl::SystemImpl instance
            0x140A50DE0ull, // BuddyServiceImpl::GetBuddyGearMainWPType
            0x140A50DC0ull, // BuddyServiceImpl::GetBuddyGearMainWPGrade
            0x140A50E20ull, // BuddyServiceImpl::GetBuddyGearSubWPType
            0x140A50E00ull, // BuddyServiceImpl::GetBuddyGearSubWPGrade
            0x141723140ull, // tpp::gm::walkergear::impl::AttackActionImpl::GetMuzzleMatrix
            0x1417246A0ull, // tpp::gm::walkergear::impl::AttackActionImpl::RegisterStunGunOffenseTarget
            0x14170A2A0ull, // tpp::gm::walkergear::impl::WalkerGear2Impl::SetUpMainWeapon
            0x140A50DA0ull, // BuddyServiceImpl::GetBuddyGearHeadType
            0x140A50D80ull, // BuddyServiceImpl::GetBuddyGearHeadGrade
            0x140A53BF0ull, // BuddyServiceImpl::SetBuddyGearHeadType
            0x140A53BD0ull, // BuddyServiceImpl::SetBuddyGearHeadGrade
            0x141746ED9ull, // walker gear buddy announce_dwalker_find_enemy
            0x141745699ull, // walker gear buddy announce_dwalker_attack_enemy
            0x141744F41ull, // walker gear buddy announce_dwalker_attack_stop
            0x14171686Full, // walker gear buddy mode announce call
            0x14110D0D0ull, // GetBuddyCommandHelpName
            0x14110D320ull, // GetBuddyCommandName
            0x1416606A0ull, // buddy command menu voice_cmd_wg_whistle_call
            0x141260CC0ull, // tpp::gm::player::impl::RideWalkerGearActionImpl::StateDrive
            0x141044E10ull, // tpp::gm::player::ActionPlugin::GetDeltaTime
            0x14173034Bull, // walkergear BasicActionImpl::Execute ridden: mov eax, [comm+0x2E0] / mov [work+0x1D8], eax
            0x14172DB70ull, // tpp::gm::walkergear::impl::BasicActionImpl::CorrectVelocity
            0x14172CD30ull, // tpp::gm::walkergear::impl::BasicActionImpl::CheckLowStepOnMove
            0x141730790ull, // tpp::gm::walkergear::impl::BasicActionImpl::FallCheck
            0x14174C390ull, // tpp::gm::walkergear::impl::DamageActionImpl::CorrectVelocity
            0x14125CF00ull, // tpp::gm::player::impl::RideWalkerGearActionImpl::ChangeWeapon
            0x14125F570ull, // tpp::gm::player::impl::RideWalkerGearActionImpl::PollingForSubjectiveCamera
            0x141265BC0ull, // tpp::gm::player::impl::RideWalkerGearActionImpl::UpdateCameraBasePosition
            0x141261C26ull, // RideWalkerGearActionImpl::StateDrive: the camera preset call's set-up
            0x140AEC8D0ull, // tpp::gm::player::GetPlayer2System
            0x141728350ull, // tpp::gm::walkergear::impl::AttackActionImpl::State_FireStunGun
            0x141729640ull, // tpp::gm::walkergear::impl::AttackActionImpl::State_HoldOnArmWeapon
            0x142BE1270ull, // CameraShake_Instance
            0x141722870ull, // tpp::gm::walkergear::impl::AttackActionImpl camera direction (missile aim)
            0x1412615F8ull, // RideWalkerGearActionImpl::StateDrive weapon ready flag
            0x1412612E7ull, // RideWalkerGearActionImpl::StateDrive stance press
            0x14126112Cull, // RideWalkerGearActionImpl::StateDrive stance held
            0x141261310ull, // RideWalkerGearActionImpl::StateDrive evade press
            0x140968980ull, // tpp::ui::menu::impl::MotherBaseDeviceTopMenuTableImpl::ApplyConditionVisibility
            0x140A53C30ull, // BuddyServiceImpl::SetBuddyGearMainWPType
            0x140A53C10ull, // BuddyServiceImpl::SetBuddyGearMainWPGrade
            0x141742935ull, // walker gear buddy announce_dwalker_lz
            0x141742707ull, // walker gear buddy announce_dwalker_escape
            0x141742783ull, // walker gear buddy announce_dwalker_recovery
            0x14174EB34ull, // walker gear buddy announce_dwalker_dying
            0x140204EF7ull, // TexturePools_ManagerCreateSite
            0x140203330ull, // TexturePools_ManagerCtor
            0x142B73420ull, // TexturePools_ManagerInstance
            0x140217369ull, // TexturePools_StreamerCountSite
            0x140217635ull, // TexturePools_StreamerArraySite
            0x14021FB7Aull, // TexturePools_PlannerSite
            0x14022A458ull, // TexturePools_EnumerateSetupSite
            0x142B5A2E0ull, // TexturePools_JobPool
            0x142B79AE8ull, // TexturePools_ProxyPool
            0x14003C770ull, // FileLocationManager_LocatePath
            0x1416A5030ull, // tpp::ui::menu::impl::ItemSelectorCallbackImpl::IsParentEquip
            0x1415DB191ull, // ThrowingImpl::UpdateNextPositionHopping mulss xmm0, 9.8
            0x1415DB1FBull, // ThrowingImpl::UpdateNextPositionRolling movaps xmm5, (-0,-9.8,-0,-0)
            0x1415D68FBull, // ThrowingImpl::GetPredictedPositionAtTime mulss xmm0, 9.8
            0x1415D2E70ull, // tpp::gm::impl::throwing::ThrowingImpl::CalcInitialVelocityByAngle
            0x1415D55D0ull, // tpp::gm::impl::throwing::ThrowingImpl::DrawLocusLineImpl
            0x140DAC310ull, // tpp::gm::impl::decoy::DecoySystemImpl::Realize
            0x140996220ull, // tpp::gm::impl::decoy::DecoyResourceManagerImpl::InitializePlayerDecoyModel
            0x140996273ull, // tpp::gm::impl::decoy::DecoyResourceManagerImpl::InitializePlayerDecoyModel
            0x140ADFDE0ull, // tpp::gm::impl::SimplePartsEffectControllerImpl::DeleteImmediately
            0x140928C60ull, // tpp::ui::menu::impl::MbDvcSoundControllerImpl::Update
            0x140928810ull, // tpp::ui::menu::impl::MbDvcSoundControllerImpl::IsAnyVoicePlaying
            0x140F52690ull, // tpp::mbm::impl::BaseControllerImpl::CompletePlatformDeck
            0x140866920ull, // hud CommonDataManager announce type by langId
            0x140A1D7D0ull, // tpp::gm::impl::SimpleMissionBlockController::ReceiveMessage
            0x140B799C0ull, // VehicleCommunicationInfo::FetchVehicleSeatLocalMatrix
            0x140C92D10ull, // VehicleCommunicationInfo::GetVehicleTurretCount
            0x141564690ull, // RideVehicleActionImpl::State_IdleAction
            0x141569240ull, // RideVehicleActionImpl::UpdateShootState
            0x14138C6C0ull, // AttackActionImpl::GetAimState
            0x14021AF10ull, // fox::gr::dg::DgTextureStreamer::DoExecPftxsLoad
            0x14021AFCFull, // fox::gr::dg::DgTextureStreamer::DoExecPftxsLoad
            0x14021B0D0ull, // fox::gr::dg::DgTextureStreamer::DoExecPftxsLoad
            0x14148EF50ull, // InVehicleAiImpl::ShouldWake
            0x140912B40ull, // AnnounceLog_GetLangAnnounceType
            0x14125DCB6ull, // RideWalkerGearActionImpl_ExecActionButtonCarry search centre
            0x14125DCF9ull, // RideWalkerGearActionImpl_ExecActionButtonCarry CheckEnableResult call
            0x14171694Eull, // WalkerGear_UpdateCarryAllowedFlag carry bit
            0x141758060ull, // WalkerGear_UpdateAttachmentsAndHoldPoint
            0x141261839ull, // RideWalkerGearActionImpl::StateDrive get-on motion state
            0x140966750ull, // MotherBaseDeviceTopMenuTableImpl::BuildMenuItems
            0x140878460ull, // MbDvcManageEv::ChildWindowController::UpdateReceiveTrigger
            0x14095D8D0ull, // CustomizeSelectorSystemImpl::OpenCustomizeSelector
            0x14095CEF0ull, // CustomizeSelectorSystemImpl::EndCustomizeSelector
            0x14095CD80ull, // CustomizeSelectorSystemImpl::AbortCustomizeSelector
            0x141966020ull, // CustomizeSelectorCallbackImpl::SetupItemList D-Walker main weapon group filter
            0x1409AA930ull, // tpp::gm::GetScriptVars
            0x140968183ull, // lea eax,[rdi-0xA] / test al,0xFD / jne: items 10 and 12 hide while table+0xC3A bit 0 is set
            0x14125DBB6ull, // mov edx,[rcx+rax+0x10] / shr edx,8 / test dl,1: no carry prompt while HOLD is held
            0x1412628A0ull, // tpp::gm::player::impl::RideWalkerGearActionImpl::StateGetOff
            0x1412629E3ull, // movabs r9, 0x9C9259D9C72B: the walker get-off motion state
            0x14096798Full, // lea eax,[rdi-0xA] / test al,0xFD / jne: items 10 and 12 are inactive while table+0xC3A bit 1 is set
            0x1409680A8ull, // MotherBaseDeviceTopMenuTableImpl_IsCutItem_Ids
            0x1415E9AB0ull, // TppUIBinoSubjectiveImpl_Act
            0x1409382E0ull, // UiUtilityImpl_GetAssignedButtonNumber
            0x14043E190ull, // Pad_GetInstance
            0x14044FB80ull, // PadMapping_GetPhysicalPadNum
            0x14001B9B0ull, // PadListener_GetDeviceType
            0x140023C90ull, // DevPad_GetInstance
            0x140023F20ull, // DevPad_UpdateSystem
            0x14002F4F0ull, // DevPad_ReadDirectInput
            0x1402036B0ull, // TextureManager_RequestTextureByPath
            0x14089465Dull, // KeySettingEvCall::SetKeySettingMenu controls page row list
            0x14023F8F0ull, // TextureResourceDx11::GetShaderResourceView
            0x1409360E0ull, // UiMarkerCommonDataImpl::InsertNearestLzMarker
            0x142BD8210ull, // GatlingGunGameObject::s_gameObjectHandle
            0x140D32C60ull, // tpp::gm::impl::bullet::BulletSystem3Impl::CreateBullet
            0x140B14B52ull, // RealizedSoldier2Impl_Construct
            0x140B18780ull, // RealizedSoldier2Impl_PrepareOptCamo
            0x140B16400ull, // RealizedSoldier2Impl_ShowOptCamo
            0x140B16470ull, // RealizedSoldier2Impl_ReleaseOptCamo
            0x140B08230ull, // tpp::gm::impl::SoundControllerImpl::CallSound
            0x141521160ull, // tpp::gm::soldier::impl::NoticeNoiseAiImpl::StepPreAware
            0x14095BB70ull, // MissionPreparationSystemImpl_SetPlayerLoadoutInfoFromStaffId
            0x14112C680ull, // CarryActionPluginImpl_ExecActionButtonCarry
            0x141DC5520ull, // ModelNodeMesh_Destruct
            0x141B19580ull, // fox::fx::Effect: sets a named variable of a live effect (the parts effects' Color)
            0x140C12990ull, // tpp::gm::impl::balloon::BalloonSystemImpl::CreateImpl
            0x14111B5D0ull, // PlayerCameraImpl_SetTpsParams
            0x14120D420ull, // tpp::gm::player::impl::PickUpActionPluginImpl::PickUpAmmoBox
            0x1412852A0ull, // tpp::gm::player::impl::SkillControllerImpl::StartSkill
            0x140FE51E0ull, // tpp::gm::player::impl::SkillAndItemParameterSystemImpl::GetItemParamWithEqpId
            0x140A3CFF0ull, // tpp::gm::impl::equip::EquipParameterTablesImpl::GetShieldLifeMax
            0x1411AD7A0ull, // tpp::gm::player::impl::EquipControllerImpl::GetShieldLevel
            0x14115C210ull, // tpp::gm::player::impl::CqcActionPluginImpl::ExecCqc
            0x140F97A70ull, // tpp::mbm::impl::StaffControllerImpl::StaffSeedToNameLangMessageCode
            0x141624D40ull, // tpp::ui::hud::impl::TppUIWeaponNumImpl::ShowItemPanel
            0x141168530ull, // tpp::gm::player::impl::CqcActionPluginImpl::StateHoldStart
            0x1416BAFC0ull, // tpp::ui::menu::impl::MissionPreparationCallbackImpl::GetCharaSlotIconTexturePathId
            0x1411B6060ull, // tpp::gm::player::impl::EvadeActionPluginImpl::StateEvade
            0x14033CA50ull, // AK::SoundEngine::DynamicDialogue::ResolveDialogueEvent
            0x140D2AF30ull, // tpp::gm::buddyquiet::impl::BuddyQuiet2SoundControllerImpl::CallVoice
            0x140D11040ull, // tpp::gm::buddyquiet::impl::(anon)::DamageHandlerImpl::ProcessDamages
            0x140513E40ull, // fox::gs::impl::RouteGroupImpl deleting destructor
            0x141949350ull, // tpp::gm::uav::impl CombatAiImpl::Work / RouteAiImpl::Work element destructor
            0x141949380ull, // ChunkAllocatorT::NewObjectArray<tpp::gm::uav::impl::CombatAiImpl::Work>
            0x1419543E0ull, // ChunkAllocatorT::NewObjectArray<tpp::gm::uav::impl::RouteAiImpl::Work>
            0x1419546F0ull, // tpp::gm::uav::impl::CombatAiImpl::AreRoutesValid (RouteAiImpl shares it)
            0x140AF3F10ull, // tpp::gm::impl::EffectControllerImpl::CreateEffect
            0x142A66EE8ull, // PlayerPad_ButtonTables
            0x142295C00ull, // PlayerPad_UiPairList
            0x141206A00ull, // PadImpl_Update
            0x1412066D0ull, // PadImpl_SetPadMappings
            0x141DBAB40ull, // ButtonMapper_SetAssignedButton
            0x14001B8A0ull, // Fox_PadListener_GetDefault
            0x14085DB20ull, // GamePauseMenuItemTable_GetLabelLangId
            0x14085D9C0ull, // GamePauseMenuItemTable_GetHelpLangId
            0x14085D9A0ull, // GamePauseMenuItemTable_CreateDecideEvent
            0x14085DAB0ull, // GamePauseMenuItemTable_CopyPageChildren
            0x1408A1E70ull, // KeyMappingAct_SetupSelectors
            0x14095F6D0ull, // KeyMapping_GetCategory
            0x1408A2560ull, // KeyMappingAct_CaptureKey
            0x1408A07B0ull, // KeyMappingAct_IsKeyChanged
            0x1408A0A80ull, // KeyMappingAct_RestoreKeys
            0x1408A0A50ull, // KeyMappingAct_DefaultNode_Exec
            0x1408A1670ull, // KeyMappingAct_SetListTextAndIcon
            0x14095E3F0ull, // tpp::ui::menu::impl::CustomizeUtilityImpl::GetBuddyUsageCost
            0x14195C3A0ull, // tpp::ui::menu::impl::CustomizeSelectorCallbackImpl::DecideActWindowPartBuddyDWalker
            0x140A50CC0ull, // tpp::gm::impl::BuddyServiceImpl::GetBuddyGearArmType
            0x140A50CA0ull, // tpp::gm::impl::BuddyServiceImpl::GetBuddyGearArmLevel
            0x1417027A0ull, // tpp::gm::walkergear::impl::WalkerGear2Impl::CheckHeadChanged
            0x141967140ull, // tpp::ui::menu::impl::CustomizeSelectorRecordCallFunc::SetRecord
            0x141963920ull, // tpp::ui::menu::impl::CustomizeSelectorRecordCallFunc::SetPartsMode
            0x14195C1E0ull, // tpp::ui::menu::impl::CustomizeSelectorCallbackImpl::DecideAct
            0x14195AC10ull, // tpp::ui::menu::impl::CustomizeSelectorCallbackImpl::SendHeliPartsPreview
            0x1405CED30ull, // tpp::gk::impl::ColoringSystemImpl::EvaluateDetail
            0x1405CEDA0ull, // TppColoringSystem::GetAdditionalColoringPackFilePaths
            0x14167B2A0ull, // MenuDevelopGrid_BuildSections
            0x14167AD60ull, // MenuDevelopGrid_SetupTabBar
            0x141676F10ull, // MenuDevelopGrid_ChangeSection
            0x140A44900ull, // tpp::gm::buddy::Buddy2BlockController::CallBuddyWithHalo
            0x140A49310ull, // tpp::gm::buddy::Buddy2BlockController::SetForceChange
            0x140A6FE80ull, // TppHeli2_Allocate
            0x1400E6400ull, // fox::BlockPackageContext::GetCurrentBlockPackage
            0x140079870ull, // fox::StringMap<unsigned __int64,2>::Find
            0x14095E6A0ull, // tpp::ui::menu::impl::CustomizeUtilityImpl vtable +0x58: the D-Walker's current loadout slot (vars+0x1B74)
            0x14095EAA0ull, // tpp::ui::menu::impl::CustomizeUtilityImpl vtable +0x60: sets the D-Walker's current loadout slot
            0x14095E080ull, // tpp::ui::menu::impl::CustomizeUtilityImpl::GetBuddyDWalkerUsageCost
            0x1416A5830ull, // tpp::ui::menu::impl::ItemSelectorCallbackImpl: takes the equip kind and sets the list up (unnamed in the exe)
            0x140A4DE90ull, // tpp::gm::buddy::impl::BuddyServiceImpl vtable +0x1F8: the field equipment change (unnamed in the exe)
            0x141962140ull, // tpp::ui::menu::impl::CustomizeSelectorCallbackImpl::RestoreCustomizedDWalkerPartsFromVars
            0x141963710ull, // tpp::ui::menu::impl::CustomizeSelectorCallbackImpl::SetCustomizedDWalkerPartsToVars
            0x140B39030ull, // tpp::gm::vehicle::BodyData::DataBody::SetupBody
            0x140B38DC0ull, // tpp::gm::vehicle::AttachmentData::DataBody::SetupAttachment
            0x1416F5200ull, // tpp::gm::vehicle::impl::ConstantImpl::GetVehicleTypeSpecs
            0x140A26030ull, // tpp::gm::vehicle::ConvertIntoReliefType
            0x142C36A00ull, // tpp::gm::vehicle::g_reliefPackFilePaths
            0x142C20730ull, // tpp::gm::vehicle::impl::SystemImpl::s_instance
            0x14088CD10ull, // tpp::ui::menu::KeySettingEvCall::ChangeControllerButton
            0x14088E990ull, // tpp::ui::menu::KeySettingEvCall: stops the scrolling of the controller picture's labels
            0x14088ED80ull, // tpp::ui::menu::KeySettingEvCall: reads confirm and cancel on an options page
            0x140892FC0ull, // tpp::ui::menu::KeySettingEvCall::SelectKeySettingMenu
            0x140892B90ull, // tpp::ui::menu::KeySettingEvCall::ProcessDecide
            0x140894651ull, // KeySettingEvCall::SetKeySettingMenu Control Type page row list
            0x140910540ull, // tpp::ui::utility::CreateBoxText
            0x140A29530ull, // tpp::gm::vehicle::impl::SystemImpl::TryToRide
            0x1415615C0ull, // tpp::gm::soldier::impl::RideVehicleActionImpl::State_GetOutAction
            0x140A276B0ull, // tpp::gm::vehicle::impl::SystemImpl::GetRiderOff
            0x140A28600ull, // tpp::gm::vehicle::impl::SystemImpl::RegisterExtendedRider
            0x140A29800ull, // tpp::gm::vehicle::impl::SystemImpl::UnregisterExtendedRider
            0x140FB38E0ull, // tpp::gm::soldier::impl::PassengerInfoSystemImpl::AddPassenger
            0x140FB4680ull, // tpp::gm::soldier::impl::PassengerInfoSystemImpl::UpdateReserveSeatIndex
            0x14156EC20ull, // tpp::gm::soldier::impl::RouteAiImpl::GetVehicleEntryPosition
            0x141564080ull, // tpp::gm::soldier::impl::RideVehicleActionImpl::State_GetOutImmediatelyAction
            0x14155FAA0ull, // tpp::gm::soldier::impl::RideVehicleActionImpl::SetVehicleInfo
            0x141560E70ull, // tpp::gm::soldier::impl::RideVehicleActionImpl::State_GetInAction
            0x140B2E840ull, // tpp::gm::vehicle::impl::GameObjectImpl::Initialize
            0x140BA8110ull, // tpp::gm::VehicleCommunicationInfo::TransmitDamageToRiders
            0x140BA32DAull, // tpp::gm::vehicle::impl::BodyBase::Initialize: call GetBuilderMemberFromTypeIndex before the vanilla model shader list check
            0x140B0F3C0ull, // fox::gk::PartsBuildDirector::GetBuilderMemberFromTypeIndex
            0x140F13130ull, // tpp::ui::menu::impl::MbDvcMapCallbackControlImpl::RendezvousOperation
            0x140BDB6C0ull, // tpp::gm::vehicle::impl::WeaponBulletImpl::FireSpecs
            0x140936450ull, // tpp::ui::hud::impl::UiMarkerCommonDataImpl::RegisterMarker
            0x140F02C10ull, // tpp::ui::menu::impl::MbDvcDependJobFunctionsImpl::RegisterMapIconSelectMode_LandPoint
            0x142BDE6F8ull, // tpp::gk::s_saveStorageController
            0x1416A38A0ull, // tpp::ui::menu::impl::ItemSelectorCallbackImpl::ExhibitSelectingRelief
            0x14095C870ull, // tpp::ui::menu::impl::MissionPreparationSystemImpl::StartVehicleDisplay
            0x140955470ull, // tpp::ui::menu::impl::MissionPreparationSystemImpl::ChangeVehicleDisplay
            0x1416BC4B0ull, // tpp::ui::menu::impl::MissionPreparationCallbackImpl::ReserveSelectionRelief
            0x140F7D6A0ull, // tpp::mbm::impl::ResourceControllerImpl::PayResourceCost
            0x1416BD850ull, // tpp::ui::menu::impl::MissionPreparationCallbackImpl::SetTopMenuVehicleName
            0x140A262F0ull, // tpp::gm::vehicle::impl::SystemImpl::ExhibitRelief
            0x140A29320ull, // tpp::gm::vehicle::impl::SystemImpl::SpawnRelief
            0x140A26140ull, // tpp::gm::vehicle::impl::SystemImpl::DeliverRelief
            0x140B2B710ull, // tpp::gm::vehicle::impl::GameObjectImpl::DispatchLuaCommandToInstance
            0x14166BBF0ull, // tpp::ui::menu::mbm::impl::EquipDetailsCallbackImpl::SetupVehicleDetails
            0x141672830ull, // tpp::ui::menu::impl::MenuDevelopGrid (R&D grid): a cell's EQUIPPED tag
            0x141674530ull, // tpp::ui::menu::impl::MenuDevelopGrid (R&D grid): equip step after the equip dialog's yes
            0x140966270ull, // tpp::ui::menu::impl::MotherBaseDeviceTopMenuTableImpl::OnItemDecided
            0x14196CCC7ull, // tpp::ui::menu::impl::CustomizeSelectorCallbackImpl::UpdateMenuTitleText D-Walker title
            0x14196CD4Cull, // tpp::ui::menu::impl::CustomizeSelectorCallbackImpl::UpdateSettingMenuHelpText D-Walker slot help
            0x141651148ull, // tpp::ui::menu::impl::CustomizeSlotSelectorCallbackImpl::SetupListElementWalkerGear title
            0x141667AE0ull, // tpp::ui::menu::mbm::impl::EquipDetailsCallbackImpl::SetupDetails
            0x141667B23ull, // tpp::ui::menu::mbm::impl::EquipDetailsCallbackImpl::SetupDetails walker type check
            0x14166C892ull, // tpp::ui::menu::mbm::impl::EquipDetailsCallbackImpl::SetupWalkerDetails title
            0x140A2905Bull, // tpp::gm::vehicle::impl::SystemImpl::SaveCarry: call VehicleCommunicationInfo::GetVehicleType on the carried vehicle
            0x140A260D0ull, // tpp::gm::vehicle::ConvertIntoResourceId
            0x1416FA5C0ull, // tpp::gm::vehicle::impl::InstanceImpl::ReflectLifeState
            0x140BCB570ull, // tpp::gm::vehicle::impl::ManeuverControllerImpl::FultonRecovery
            0x140F7ABB0ull, // tpp::mbm::impl::ResourceControllerImpl::CanPayResourceCost
            0x140ACFD40ull, // tpp::gm::impl::SupportRequestSystemImpl::RequestDropVehicle
            0x141673260ull, // tpp::ui::menu::impl::MenuDevelopGrid (R&D grid): decide on a cell -> 0 equip / supply-drop dialog, 2 develop dialog, 3 refused (named IsSupplyDevelop on 8080)
            0x141667A10ull, // tpp::ui::menu::impl::EquipDetailsCallbackImpl: an entry's damage badge (details, entry, damage type 1 lethal / 2 sleep / 3 stun)
            0x14023AA30ull, // fox::gr::dg::TextureResource_Load
            0x140B34D80ull, // tpp::gm::vehicle::impl::GameObjectImpl::Seize
            0x140F79030ull, // tpp::mbm::impl::ResourceControllerImpl::AddResource
            0x140A88B90ull, // tpp::mbm::MotherBaseManagementSystem::AddTempResource
            0x140F791D0ull, // tpp::mbm::impl::ResourceControllerImpl::AddResourcesFromTempResourceBuffer
            0x14196F670ull, // tpp::ui::menu::mbm::impl::DataBaseCallbackImpl::RefreshMapLayout
            0x14196FEA0ull, // tpp::ui::menu::mbm::impl::DataBaseCallbackImpl::RefreshViewMap
            0x140DD9B20ull, // tpp::gm::heli::impl::Heli2Impl::Realize (vtable slot 0 next to Unrealize)
            0x140B578A0ull, // RealizedHeli2Impl creation (TppHeli2_Allocate's only realized-heli builder)
            0x140923090ull, // tpp::ui::menu::impl::MotherBaseMapCommonDataImpl::GetUniqueInformationIconLangId
            0x1416D0B10ull, // tpp::ui::menu::mbm::impl::ResourceManagementCallbackImpl::RefreshPrefabList
            0x1416D3130ull, // tpp::ui::menu::mbm::impl::ResourceListRecordCallFunc::Start
            0x1416D0990ull, // tpp::ui::menu::mbm::impl::ResourceManagementCallbackImpl: info text of the cursor row
            0x1407E82D0ull, // tpp::mbm::ResourceController::GetResourceUsableCount
            0x1407E8320ull, // tpp::mbm::ResourceController: second count the sale check adds (server stock)
            0x141DCF6C0ull, // fox::ui::WindowResourceCreator::CreateLayouts
            0x140F7DA00ull, // tpp::mbm::impl::ResourceControllerImpl::SaleResource
            0x140A70630ull, // TppHeli2 type vtable slot 1: destroys every realized heli, the preloaded bullets and the Heli2Impl quark
            0x140F75550ull, // tpp::mbm::impl::PoolRewardControllerImpl::AddPoolRewards
            0x140F752A0ull, // tpp::mbm::impl::PoolRewardControllerImpl::AddPoolRewardCore
            0x140F74480ull, // tpp::mbm::impl::PoolRewardControllerImpl::AcceptPoolRewardOneCore
            0x1416C9820ull, // tpp::ui::menu::mbm::impl Rewards list row draw (row, entry type branches)
            0x141646A30ull, // tpp::ui::menu::mbm::impl::CombatDeployCallbackImpl::RefreshMissionRewards
            0x141648510ull, // CombatDeployCallbackImpl GMP reward row (page, row, amount, fob)
            0x141648880ull, // CombatDeployCallbackImpl material reward row (page, row, materialIndex, rewardParam)
            0x141648CE0ull, // CombatDeployCallbackImpl staff reward row (page, row, staffDraw, rewardParam)
            0x1416D0830ull, // tpp::ui::menu::mbm::impl::ResourceManagementCallbackImpl::RefreshImageRecord
            0x1416CF210ull, // Resources sale popup: item picture, name, price and counts
            0x140B8CDB0ull, // Heli2 model plugin vt+0x108 (instance, FormVariationFile2): applies a heli paint, then switches 5 hard-coded vanilla materials to painted mode
            0x1401CB6F0ull, // fox::gr::Model material modifier of a mesh by its 32-bit name (the paint switch lookup)
            0x140260AA0ull, // fox::gr::MaterialModifier2::SearchParameterInfo
            0x140260880ull, // fox::gr::MaterialModifier2::GetParameter
            0x140260B10ull, // fox::gr::MaterialModifier2 set vector parameter (material, name, const Vector4*)
            0x140216F60ull, // fox::gr::dg::DgTextureStreamer proxy list node pool setup
            0x1402170F0ull, // fox::gr::dg::DgTextureStreamer base constructor
            0x1402171C0ull, // fox::gr::dg::DgTextureStreamer::DgTextureStreamer
            0x140218680ull, // DgTextureStreamer proxy use counter read (FUN_140218680)
            0x1402188C0ull, // fox::gr::dg::DgTextureStreamer::AddSearchTag
            0x140218CB0ull, // DgTextureStreamer proxy list node alloc
            0x1402192F0ull, // fox::gr::dg::DgTextureStreamer::CancelUpgrade
            0x1402195F0ull, // fox::gr::dg::DgTextureStreamer proxy list node pool reset
            0x140219BB0ull, // DgTextureStreamer proxy bits clear (FUN_140219bb0)
            0x14021A1E0ull, // DgTextureStreamer proxy list node free
            0x14021A320ull, // fox::gr::dg::DgTextureStreamer::DeleteSearchTag
            0x14021AA50ull, // DgTextureStreamer proxy index check (FUN_14021aa50)
            0x14021ACB0ull, // DgTextureStreamer released resource data flush (FUN_14021acb0)
            0x14021B320ull, // DgTextureStreamer upgrade release (FUN_14021b320)
            0x14021B730ull, // fox::gr::dg::DgTextureStreamer::DoUpgrade2
            0x14021B8C0ull, // fox::gr::dg::DgTextureStreamer::DoUpgradeEntry
            0x14021C280ull, // DgTextureStreamer proxy bits reset (FUN_14021c280)
            0x14021CF30ull, // fox::gr::dg::DgTextureStreamer::GetProxy
            0x14021D0C0ull, // DgTextureStreamer proxy bit test (FUN_14021d0c0)
            0x14021D1E0ull, // fox::gr::dg::InitStaticResourceDataDx11
            0x14021D990ull, // DgTextureStreamer proxy index check (FUN_14021d990)
            0x14021E2D0ull, // fox::gr::dg::DgTextureStreamer::PostCreation
            0x14021E6A0ull, // DgTextureStreamer proxy use counter add (FUN_14021e6a0)
            0x14021E720ull, // DgTextureStreamer proxy use counter add by texture slot (FUN_14021e720)
            0x14021EA80ull, // fox::gr::dg::DgTextureStreamer::SearchProxyFromPath
            0x14021F8C0ull, // DgTextureStreamer proxy use counter sub (FUN_14021f8c0)
            0x14021F940ull, // DgTextureStreamer proxy use counter sub by texture slot (FUN_14021f940)
            0x14021FA20ull, // DgTextureStreamer upgrade planner (FUN_14021fa20)
            0x140220B40ull, // fox::gr::dg::DgTextureStreamer::UpdateProxyLoadedStatus
            0x140220EE0ull, // fox::gr::dg::DgTextureStreamer::allocateHandlableProxy
            0x140221370ull, // fox::gr::dg::DgTextureStreamer::doDegrade
            0x140222010ull, // fox::gr::dg::DgTextureStreamer::pftxsWaitForDecompression
            0x140222330ull, // DgTextureStreamer proxy bits clear (FUN_140222330)
            0x1402224F0ull, // fox::gr::dg::DgTextureStreamer::safeFreeResourceDataMembers
            0x140216960ull, // dynamic_sequence_allocator<DgIncrementalTextureResource::Proxy> constructor
            0x1400041A0ull, // std::malloc
            0x0ull,         // develop array sweep allowed site
            0x0ull,         // develop array sweep allowed site
            0x0ull,         // develop array sweep allowed site
            0x0ull,         // develop array sweep allowed site
            0x0ull,         // develop array sweep allowed site
            0x0ull,         // develop array sweep allowed site
            0x14125E550ull, // RideWalkerGearActionImpl_UpdateRiderUi
        };

        return value;
    }

    const AddressSet& Get_mst_jp_day3900_AddressSet()   // 1.0.15.4 japanese
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
            0x14006B920ull, // FoxLuaRegisterLibrary
            0x140085760ull, // FoxPath_Path
            0x1400234E0ull, // FoxStrHash32
            0x141A097F0ull, // FoxStrHash64
            0x140887790ull, // GameOverSetVisible
            0x1409110F0ull, // GetCurrentMissionCode
            0x140BF4390ull, // GetNameIdWithGameObjectId
            0x140BF4040ull, // GetGameObjectIdWithIndex
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
            0x14154FD10ull, // StateRadioRequest
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
            0x1404D2860ull, // SubtitleManager_Get
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
            0x1418C50B0ull, // Sahelan_ActionCoreImpl_UpdateEyeLampColor
            0x1418C5310ull, // Sahelan_ActionCoreImpl_UpdateHeartLight
            0x1418FFDD0ull, // Sahelan_PhaseSneakAi_PushEyeColor
            0x140EF2900ull, // MbDvcAnnouncePopupCallbackImpl_UpdateAnnounceNormal
            0x140EF2CC0ull, // MbDvcAnnouncePopupCallbackImpl_UpdateAnnounceServer
            0x140EF26E0ull, // MbDvcAnnouncePopupCallbackImpl_UpdateAnnounceEmergency
            0x1408669B0ull, // HudCommonDataManager_GetInstance
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
            0x1416201DBull, // IconTitleGetLangTextCall
            0x140BFA330ull, // GameObject_SendCommand
            0x140FE7690ull, // UiControllerImpl_InitEquipHudData
            0x1414E5040ull, // NoticeControllerImpl_GetOccasionalChat
            0x140D83420ull, // SoldierConversationService_ConvertSpeechLabelToConversationType
            0x1414E5159ull, // OccasionalChat_FactionTestNop
            0x140933510ull, // MbDvcReserveAnnouncePopup
            0x14093A820ull, // MbDvcPopupGateFn
            0x14151F520ull, // NoticeNoiseAiImpl_StepAware
            0x141513000ull, // NoticeIndisAiImpl_StepAware
            0x1414E4760ull, // NoticeControllerImpl_DoCheckSpreadNotice
            0x1414E1FA0ull, // NoticeControllerImpl_CheckSightNoticeSoldier
            0x140E46940ull, // NoticeNearThreatAiImpl_Wake
            0x140E47310ull, // NoticeNoiseAlertAiImpl_Wake
            0x1417886F0ull, // NoticeNearGameObjectAiImpl_Wolf_Wake
            0x1417896F0ull, // NoticeNoiseSneakAiImpl_Wolf_Wake
            0x140C351C0ull, // NoticeNearGameObjectAiImpl_Bear_Wake
            0x140C36B20ull, // NoticeNoiseAiImpl_Bear_Wake
            0x140B15E00ull, // RealizedSoldier2Impl_ConvertHeadEquipModelType
            0x140B19130ull, // RealizedSoldier2Impl_UpdateHeadEquipMesh
            0x140B1B050ull, // FovaController_GetActiveFovaResourceManager
            0x1404E6F40ull, // Fv2ResourceManager_GetModel
            0x1412B1410ull, // TimeCigaretteActionPluginImpl_ShowTimeCigaretteUi
            0x1408E6600ull, // HeliTaxi_CanHeliTaxi
            0x140F0D360ull, // HeliTaxi_CallRescueHeli
            0x140E0D3F0ull, // HeliTaxi_StepWithdraw
            0x1409110B0ull, // HeliTaxi_GetLocationId
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
            0x1408A34F0ull, // Hud_TypingLogActUpdate
            0x141DCA310ull, // Ui_SoundControlStart
            0x14085FD80ull, // Ui_UiCommonDataManagerGetInstance
            0x1407D2220ull, // Ui_EventNodeBodyGetGraphState
            0x140928850ull, // VoiceParam_PlayDialogue
            0x14121D4C0ull, // RideHeliActionPluginImpl_ExecPreMotionGraph
            0x14121E1B0ull, // RideHeliActionPluginImpl_GetStateFn
            0x1411FA8D0ull, // MBTerminalActionPluginImpl_IsDisableMBTerminal
            0x14110E8A0ull, // CallMenuManagerImpl_Update
            0x1411F9C20ull, // MBTerminalActionPluginImpl_CheckActionState
            0x142BFF980ull, // SoundDaemon_Instance
            0x140971360ull, // SoundDaemon_PostEventQueue
            0x1420A72F0ull, // Sd_kW_Select
            0x140d7c720ull, // UpdateAntiAir
            0x140d76960ull, // ClearAntiAir
            0x140926640ull, // GetChangeLocationMenuParameterByLocationId
            0x140926770ull, // GetMbFreeChangeLocationMenuParameter
            0x140926840ull, // GetPhotoAdditionalTextLangId
            0x140fe75d0ull, // UiControllerImpl_HideBinocle
            0x140A87CB0ull, // AddCassetteTapeTrack
            0x140EF5E30ull, // CollectGotTapes
            0x140A96160ull, // IsGotCassetteTapeTrack
            0x140AACBA0ull, // SetCassetteTapeTrackNewFlag
            0x140EF74B0ull, // SetCurrentAlbum
            0x1409754D0ull, // SetupMusicInfos
            0x1405D69B0ull, // RadioCassette_SearchCasseteInfo
            0x1405D6820ull, // RadioCassette_GetCassetteMusic
            0x1405D6870ull, // RadioCassette_IsGotCassette
            0x1405D6840ull, // RadioCassette_GetCassetteSaveIndex
            0x1405FC7E0ull, // RadioCassette_SdPostEvent
            0x140643400ull, // RadioCassette_RadioUpdate
            0x140A87D50ull, // AddCassetteTapeTrackByIndex
            0x1406424F0ull, // RadioCassette_ActivateUnit
            0x1405D6900ull, // RadioCassette_IsSameSaveIndexFromName
            0x14126B640ull, // SearchLightActionPluginImpl_StateDoorStart
            0x14126AB40ull, // SearchLightActionPluginImpl_StateDoorEnd
            0x140338E00ull, // SoundControl_PostExternalEvent
            0x14098A750ull, // MusicPlayerPlayWrapper
            0x140931B10ull, // Barrier_GetItemId
            0x1408B216Dull, // Barrier_EquipItemCallRet
            0x141000070ull, // Barrier_Updater
            0x1405D79B0ull, // Barrier_IsFobMode
            0x1409C46B5ull, // Barrier_LoadGate0
            0x1409C471Full, // Barrier_LoadGate1
            0x1409C4785ull, // Barrier_LoadGate2
            0x140FC3620ull, // Dm_FireLoop
            0x140A2A9F0ull, // Dm_Classify
            0x141B187F0ull, // Dm_VfxFactory
            0x140C0BCA0ull, // Dm_ComponentFactory
            0x140BFF060ull, // Dm_Alloc
            0x140AF3DE0ull, // Dm_BackLinkPool
            0x1404ED130ull, // Dm_OneShot
            0x1410528A0ull, // AttackActionImpl_IsWeaponNoUseInPlaceAction
            0x140a97b20ull, // TppMotherBaseManagement_RegCstDev
            0x140a98380ull, // TppMotherBaseManagement_RegFlwDev
            0x140f6af30ull, // EquipDevCtrl_GetSuitDevelopInfoIndex
            0x140f697f0ull, // EquipDevCtrl_GetFaceEquipDevelopInfoIndex
            0x1416a0a80ull, // AddListSuit
            0x140b10cb0ull, // CamoufParamInfo_GetCamoufValue
            0x140fdbdd0ull, // CamouflageController_ExecSuitCorrect
            0x1411b0450ull, // EquipController_SetHandSlotEnabled
            0x141674500ull, // EquipDevelopCallbackImpl_SetSupplyCBoxInfo
            0x1416bbef0ull, // IsEnableCurrentSuit
            0x1416a3260ull, // ItemSelectorCallbackImpl_DecideActMissionPreparationSetEquipMode
            0x1416a41a0ull, // ItemSelectorCallbackImpl_DecideActMotherBaseDeviceSupportDropMode
            0x1416a8b60ull, // ItemSelectorCallbackImpl_SetupPrefabListElement
            0x1416ae250ull, // ItemSelectorRecordCallFunc_UpdateRecords
            0x1416a0990ull, // ItemSelector_AddListBandana
            0x140ae9840ull, // LoadPlayerBionicArmFpk
            0x140ae9790ull, // LoadPlayerBionicArmFv2
            0x140ae8ff0ull, // LoadPlayerCamoFpk
            0x140ae8f20ull, // LoadPlayerCamoFv2
            0x140ae9990ull, // LoadPlayerPartsFpk
            0x140ae98f0ull, // LoadPlayerPartsParts
            0x140ae93b0ull, // LoadPlayerSnakeBlackDiamondFpk
            0x140ae9330ull, // LoadPlayerSnakeBlackDiamondFv2
            0x140ae9540ull, // LoadPlayerSnakeFaceFpk
            0x140ae9430ull, // LoadPlayerSnakeFaceFv2
            0x140957f60ull, // MissionPrepSystem_IsEnableHeadOptionSuit
            0x1416bbe60ull, // MissionPrep_IsEnableCurrentHeadOption
            0x1409b46e0ull, // Player2BlockController_LoadPartsNew
            0x1409c68c0ull, // Player2GameObjectImpl_ProcessSignal
            0x1409cb100ull, // Player2Impl_SetUpParts
            0x1409df5d0ull, // Player2UtilityImpl_LoadoutApplyAfterSetSuit
            0x1409deab0ull, // Player2UtilityImpl_SetInitialConditionWithLoadoutInfo
            0x1409dfbe0ull, // Player2UtilityImpl_SetSuitAndHandConditionWithLoadoutInfo
            0x1409b0f40ull, // Player_ConverFaceIdWithFaceEquipId
            0x140ae8c00ull, // ResourceTable_DoesNeedFaceFova
            0x140ae8c50ull, // ResourceTable_DoesNeedFaceFovaForAvatar
            0x1409c5180ull, // Sys_IsArtificialHandEnabled
            0x141e02fc0ull, // Sys_IsArtificialHandEnabledForCurrentPlayerType
            0x1409ccf30ull, // UpdatePartsStatus
            0x1416a2650ull, // ItemSelectorCallbackImpl_DecideActMotherBaseCustomize
            0x1400cd2f0ull, // Fox_Path_Exists
            0x1400857c0ull, // Fox_Path_Dtor
            0x141e01f70ull, // PluginFacial_ApplyMotion
            0x140f6d1b0ull, // EquipDevCtrl_IsEquipVisile
            0x140f69830ull, // EquipDevCtrl_GetHeadBadgeCategory
            0x1416a6b40ull, // MissionPrep_SetInitialSelectRecord
            0x140a2b200ull, // TppEquip_RegisterConstant
            0x141e02b70ull, // PlayerInfoInterfaceImpl_GetPartsTypeAtCamoType
            0x142c20fd0ull, // EquipIdTable_InfoList
            0x142a70928ull, // EquipIdTable_TypeWords
            0x140a3b5a0ull, // EquipIdTable_ReloadEquipIdTable
            0x140f68d30ull, // EquipDevelopCtrl_GetEquipDevelopIndex
            0x140f6e860ull, // EquipDevelopCtrl_SetEquipUndeveloped
            0x140f6aee0ull, // EquipDevCtrl_GetSuitCamoType
            0x140f6afc0ull, // EquipDevCtrl_GetSuitLevel
            0x140f6d070ull, // EquipDevCtrl_IsEquipSuit
            0x140f6e660ull, // EquipDevelopCtrl_SetEquipDeveloped
            0x1412a26b0ull, // SupplyCboxActionPluginImpl_StateInBox
            0x140863d00ull, // HudCommonDataManager_AnnounceLogView
            0x140913fc0ull, // Ui_LangIdToKey
            0x140912af0ull, // Ui_GetLangText
            0x141392A3Eull, // Soldier_ShootOneBullet_GroupMaskCall
            0x140D2DC5Dull, // Soldier_ActivateBulletAtEmptyWork_SameArmyJnz
            0x1408A8F50ull, // TelopStartTitleEvCall_SetBgTexture
            0x141DAE510ull, // Layout_GetLayout
            0x141DAE550ull, // Layout_GetModel
            0x14094E410ull, // GetCassetteTapeUnreadInfo
            0x140A965C0ull, // IsNewCassetteTapeTrack
            0x140EF5B60ull, // CassetteMenuCheckNewFlag
            0x140EF5A60ull, // CassetteAlbumCheckNewFlag
            0x140EF5D10ull, // CassetteCheckUnreadInfo
            0x1404D72B0ull, // SubtitlesObjectSendMessage
            0x140f6cd10ull, // EquipDevCtrl_IsEquipDevelopable
            0x140AE8100ull, // LoadAvatarFaceFv2
            0x140AE8170ull, // LoadAvatarFaceFpk
            0x1409B62E0ull, // AvatarFaceEditUpdate
            0x1404E6F90ull, // Fox_ModelFromHandle
            0x140AE7350ull, // LoadAvatarHeadOptionFv2
            0x140AE73C0ull, // LoadAvatarHeadOptionFpk
            0x140F6E630ull, // EquipDevelopCtrl_SetEnableDevelop
            0x140A41920ull, // ReloadEquipParameterTables2
            0x142C25C50ull, // GunBasicParameters2Buffer
            0x142A711F0ull, // EquipParameterTablesImpl_Instance
            0x142349B40ull, // MotionLoaderImpl_ReceiverTypeTable
            0x140DB6C70ull, // MotionLoaderImpl_GetReceiverType
            0x142349B28ull, // MotionLoaderImpl_UnderBarrelTypeTable
            0x140DB6C90ull, // MotionLoaderImpl_GetUnderBarrelType
            0x140F6B2C0ull, // EquipDevelopControllerImpl_GetSuppressorAmount
            0x142BDCFF0ull, // DamageParameterTable_Instance
            0x1405CE410ull, // DamageParameterTable_ReloadDamageParameter
            0x1405CA0E0ull, // DamageParameterTable_GetDamageParameter
            0x140F65CD0ull, // EquipDevCtrl_GetBaseDevelopId
            0x141678c50ull, // MenuDevelopGrid_FillGrid
            0x1416785d0ull, // MenuDevelopGrid_CopyGrid
            0x1416725f0ull, // MenuDevelopGrid_CountBadge
            0x1416791f0ull, // MenuDevelopGrid_FillFlat
            0x141678f90ull, // MenuDevelopGrid_CopyFlat
            0x141281fe0ull, // SightManager_UpdateMissileLockOn
            0x140d2fe10ull, // Bullet3_DoSimulation
            0x140d2d3e0ull, // Bullet3_ActivateBulletAtEmptyWork
            0x141280030ull, // SightManager_Update
            0x141282c80ull, // SightManager_UpdateMissileLockOnUi
            0x1408ec7a0ull, // LockOnReticleFactory_CreateWindow
            0x140A3BB10ull, // EquipParams_GetAttackIdByEquipId
            0x140dc3820ull, // EquipSystem_SetUpGunInfoFromGunPartsDesc
            0x141dabdb0ull, // UiWindowFunction_FindWindow
            0x141dac100ull, // UiWindowFunction_PostShowAndStartMessage
            0x141dabe30ull, // UiWindowFunction_GetLayout
            0x141041190ull, // AttackAction_Fire
            0x141302f70ull, // EquipObject_DoFire
            0x14132AF40ull, // Shell_ActivateShellAtEmptyWork
            0x141156ef0ull, // CorePlugin_UpdateLoadoutRequest
            0x140dc0360ull, // WeaponSystem_DefineWeaponFireSound
            0x142C934A0ull, // EquipSystem_ChimeraPartsSetWork
            0x140EFBA70ull, // MbDvcUpdateTrackListCallFuncs
            0x140EF70E0ull, // MbDvcRefreshTrackListPrefabParameter
            0x140EF6990ull, // MbDvcTrackListRecordRefresh
            0x140F31870ull, // MbDvcMissionListCallbackImpl_ActivatePrefabPopupParameter
            0x140F32120ull, // MbDvcMissionListCallbackImpl_SetMenuHelp
            0x140A3B300ull, // Equip_ReloadChimeraPartsInfoTable
            0x142C207C0ull, // Equip_ChimeraPartsPackageInfos
            0x141A75430ull, // Mtar_GetAnimFile
            0x141A76000ull, // Mtar_GetDataInfo
            0x142A6B400ull, // Equip_MotionEntryTable
            0x1422E2980ull, // SimplePartsControllerImpl_Vtable
            0x140ADDB00ull, // SimplePartsControllerImpl_SetMotionData
            0x140ADDB90ull, // SimplePartsControllerImpl_SetMotionDataByPath
            0x140DBFCB0ull, // EquipSystemImpl_HideMagazine
            0x140DBF1B0ull, // EquipSystemImpl_GetPartsController
            0x140BFEFD0ull, // Fox_GetQuarkSystemTable
            0x140071580ull, // Fox_ArrayBaseExtend
            0x1420BCAC8ull, // Fox_ArrayOperatorVtbl
            0x141AB7890ull, // Animx_GetControlSize
            0x141AB72B0ull, // Animx_SimpleControlCtorPool
            0x141AB7630ull, // Animx_SimpleControlCtorHeap
            0x141AB7490ull, // Animx_SimpleControlDtorPool
            0x141AB76A0ull, // Animx_SimpleControlDtorHeap
            0x141AB7620ull, // Animx_SimpleControlAux
            0x1405D6040ull, // Equip_MotionEntrySlotHookA
            0x1405D63A0ull, // Equip_MotionEntrySlotHookB
            0x140A2A210ull, // EquipIdTable_GetEquipTypeId
            0x14143F0D0ull, // Soldier2InterrogateUtil_UpdateInterrogation
            0x141440AB0ull, // Soldier2InterrogateUtil_UpdateInterrogationMarker
            0x140B082E0ull, // SoundControllerImpl_CallVoice
            0x140DBEAE0ull, // EquipSystem_GetGunInfoById
            0x140943010ull, // Reticle_ResetLayout (EN - 0x120 like its neighbours, checked by bytes at install)
            0x1412B0280ull, // TimeCigaretteActionPluginImpl_HideTimeCigaretteUi
            0x140076e30ull, // Fox_EntityInfoMapTeardown
            0x142AC6D10ull, // WeaponEnhanceLangIdArray
            0x140F9FA50ull, // WeaponEnhance_GetIndexForRegist
            0x140F9FB00ull, // WeaponEnhance_GetMessageIdArray
            0x140F9FAB0ull, // WeaponEnhance_GetLangIdById
            0x140F9FAF0ull, // WeaponEnhance_GetLangIdBound
            0x140b3b850ull, // BulletEffectController_CreateEffect
            0x1416A1140ull, // ItemSelectorCallbackImpl_AddRecord
            0x140AE4410ull, // AdditionalMotionTable_GetMtarPathId
            0x1416A23D0ull, // ItemSelector_AddDevelopWeaponList
            0x1409B7B50ull, // BlockControllerImpl_UpdateAdditionalMotionBlock
            0x14006DA00ull, // Fox_Block_GetFileForPathId
            0x14006D7F0ull, // Fox_BlockGroup_GetBlockAtIndex
            0x1416AC540ull, // ItemSelector_StartEquipPreviewImpl
            0x141681C90ull, // UiEquipPreviewController_ScrollNext
            0x141681AE0ull, // UiEquipPreviewController_ScrollPrev
            0x1409BB870ull, // Player2Impl_AddAdditionalMtarAll
            0x1409C9350ull, // Player2Impl_RemoveAdditionalMtarAll
            0x142349C30ull, // MotionLoaderImpl_BarrelTypeTable
            0x140DB6990ull, // MotionLoaderImpl_GetBarrelType
            0x142349CB0ull, // MotionLoaderImpl_MagazineTypeTable
            0x140DB69E0ull, // MotionLoaderImpl_GetMagazineType
            0x142349D70ull, // MotionLoaderImpl_StockTypeTable
            0x140DB6C80ull, // MotionLoaderImpl_GetStockType
            0x14120A535ull, // TppPickable_ItemWindowBoundSite
            0x140915010ull, // UiUtility_GetWeaponItemNameLangId
            0x1409848F5ull, // SoundPlayerAnimEvent_StepNoiseQuietCmp
            0x140A0B3C0ull, // CollectionLocatorArray_SetUpLinearAccessor
            0x140D4FA20ull, // CollectionSystemImpl_PickUp
            0x140D50D10ull, // CollectionSystemImpl_RepopCountOperation
            0x140D4D3C0ull, // CollectionSystemImpl_GetCollectionInfo
            0x140D4E340ull, // Collection_GetModelFilePath
            0x140D4D170ull, // Collection_GetCatchEffectColorAndScale
            0x140D4DED0ull, // Collection_GetGroundEffectSize
            0x1412091E0ull, // PickUpActionPluginImpl_DoPickUpCollection
            0x140910720ull, // GetCollectionIconFtexPath
            0x140910B00ull, // GetCollectionNameText
            0x14050B9D0ull, // UixUtility_GetUixUtility
            0x140D4E560ull, // Collection_GetModelFilePathRoot
            0x140D4DD70ull, // Collection_GetFovaFilePath
            0x140A0C060ull, // Collection_IsHerbByType
            0x140A0C0D0ull, // Collection_IsMaterialByType
            0x140A0BF80ull, // Collection_IsDiamondByType
            0x140A96340ull, // MbmImpl_IsGotDataBase
            0x140A88610ull, // MbmImpl_AddTempDataBase
            0x141160474ull, // QuietChokeHold
            0x141160C09ull, // QuietInterrogate
            0x14110F87Cull, // QuietInterrogateBypass
            0x14110DCF7ull, // QuietHoldupInterrogate
            0x1416BB339ull, // CharaSlotSelectionNumGate
            0x1416BB2F0ull, // CharaSlotSelectionNum
            0x14196F850ull, // UiMbmDataBase_RefreshPrefabList
            0x14196E890ull, // UiMbmDataBase_RefreshDataBaseNameText
            0x14196E750ull, // UiMbmDataBase_RefreshDataBaseIcon
            0x14196D3D0ull, // UiMbmDataBase_GetDataBaseInfoText
            0x14196EEB0ull, // UiMbmDataBase_RefreshInfoLayout
            0x14196ECD0ull, // UiMbmDataBase_RefreshImageRecord
            0x14196E640ull, // UiMbmDataBase_RefreshCollectionRate
            0x140FE59E0ull, // SkillAndItemParameterSystem_GetSuitParam
            0x140F1BA40ull, // MbDvcMapCallbackIconImpl_DispOnNewMarkerIcon
            0x140F1B3E0ull, // MbDvcMapCallbackIconImpl_DispOnImportantIcon
            0x140F1A940ull, // MbDvcMapCallbackIconImpl_DispOnGoalIcon
            0x140F1A6E0ull, // MbDvcMapCallbackIconImpl_DispOnGeneralIcon
            0x140F1B510ull, // MbDvcMapCallbackIconImpl_DispOnTargetMarkerIcon
            0x140F19B80ull, // MbDvcMapCallbackIconImpl_DispOnAreaIcon
            0x140F21B60ull, // MbDvcMapCallbackIconImpl_GetIconDisplayParam
            0x141E02D20ull, // PlayerInfo_GetLocalPartsType
            0x141E024F0ull, // PlayerInfo_CheckStatusBit
            0x1409CDA7Full, // UpdatePartsStatus_QuietCamoStrip
            0x140956E30ull, // MissionPrepSystem_GetPlayerNameFromPlayerTypeAndStaffId
            0x1409C92E0ull, // Player2BlockController_ApplyQuietSuitShaderParams
            0x140FE7460ull, // UiControllerImpl_GetUiSightTypeBySightId
            0x1408EDCB0ull, // SightCommonFactory_CreateWindow
            0x1408EE690ull, // SightCommonFactory_DeleteWindow
            0x14196F7A0ull, // UiMbmDataBase_RefreshNewIcon
            0x141970210ull, // UiMbmDataBase_SetPutCursorFlag
            0x14196E480ull, // UiMbmDataBase_OnStop
            0x140F5A190ull, // MbmImpl_CalcDocumentationUnreadCount
            0x140F5A360ull, // MbmImpl_CalcEncyclopediaUnreadCount
            0x141E0B210ull, // Net_ServerManagerBufferRelease
            0x1414E0FD0ull, // NoticeControllerImpl_CheckSightNoticePlayer
            0x141AEA9B0ull, // DemoPlayback_OnPlayingAfterUpdateStream
            0x141AE8100ull, // DemoPlayback_DoFinish
            0x141AE8210ull, // DemoPlayback_DoInterrupt
            0x140976360ull, // UpdateMusicPlayer
            0x140BF4160ull, // GameObject_GetGameObjectIdWithName
            0x14124A060ull, // Vehicle_GetConnectPointWorldMatrix
            0x141279A20ull, // Player_SequentialDemoActionExecute
            0x141279EC0ull, // Player_SequentialDemoActionOnSignal
            0x140BF99C0ull, // Fox_GameObjectManager_GetSharedInstance
            0x14229B870ull, // GameObjectType_Horse2_Vtable
            0x1422C54C0ull, // GameObjectType_Heli2_Vtable
            0x1422C4AA0ull, // GameObjectType_HeliAlt_Vtable
            0x1422CAA30ull, // GameObjectType_WalkerGear2_Vtable
            0x1422E6080ull, // AnimationControllerImpl_Vtable
            0x1422FB440ull, // HeliAnimationControllerImpl_Vtable
            0x140DB6A80ull, // MotionLoaderImpl_GetMtarIds
            0x140DB69F0ull, // MotionLoaderImpl_GetMtarFilePath
            0x140DB69A0ull, // MotionLoaderImpl_GetBlockPackagePath
            0x141048C40ull, // AttackAction_GetGunMotionIdFromTable
            0x141045E70ull, // AttackAction_GetEquipMotionIdFromTable
            0x14104B950ull, // AttackAction_GetNormalGripMotionId
            0x14104BB30ull, // AttackAction_GetNormalMagazineGripMotionId
            0x141043720ull, // AttackAction_GetAssaultGripMotionId
            0x141048EF0ull, // AttackAction_GetHandgunGripMotionId
            0x141050600ull, // AttackAction_GetSubMachinegunGripMotionId
            0x14104F7C0ull, // AttackAction_GetSniperRifleGripMotionId
            0x14104F4B0ull, // AttackAction_GetShotgunGripMotionId
            0x14104B310ull, // AttackAction_GetMachinegunGripMotionId
            0x1410462B0ull, // AttackAction_GetGrenadeLauncherGripMotionId
            0x14104B630ull, // AttackAction_GetMissileGripMotionId
            0x141043A20ull, // AttackAction_GetAssaultUnderBarrelGripMotionId
            0x141049390ull, // AttackAction_GetHandgunOneHandGripMotionId
            0x1410494E0ull, // AttackAction_GetHandgunOneHandMotionId
            0x141050810ull, // AttackAction_GetSubMachinegunOneHandMotionId
            0x14104FE80ull, // AttackAction_GetStepReloadHandgunOneHandMotionId
            0x14104FD10ull, // AttackAction_GetStepReloadHandgunMotionId
            0x141056010ull, // AttackAction_ResetMissionPrepareGripMotion
            0x14104BC90ull, // AttackAction_GetNormalMotionIdByCategory
            0x141043770ull, // AttackAction_GetAssaultMotionIdTable
            0x141048FE0ull, // AttackAction_GetHandgunMotionIdTable
            0x141050640ull, // AttackAction_GetSubMachinegunMotionIdTable
            0x14104F890ull, // AttackAction_GetSniperRifleMotionIdTable
            0x14104F550ull, // AttackAction_GetShotgunMotionIdTable
            0x14104FF60ull, // AttackAction_GetShotgunStepReloadMotionIdTable
            0x14104B350ull, // AttackAction_GetMachinegunMotionIdTable
            0x1410463E0ull, // AttackAction_GetGrenadeLauncherMotionIdTable
            0x14104B680ull, // AttackAction_GetMissileMotionIdTable
            0x1410514B0ull, // AttackAction_GetChaseMotionIdTable
            0x140A00A60ull, // EquipMotionDataTable_ReloadEquipMotionData2
            0x1416C266Dull, // MissionPreparationCallbackImpl_Start_PlayBgmEvent
            0x1416C5AD9ull, // MissionPreparationCallbackImpl_Update_StopBgmEvent
            0x1416C5DFEull, // MissionPreparationCallbackImpl_Update_StopBgmEvent2
            0x1416C23F0ull, // MissionPreparationCallbackImpl_Start
            0x1416C2F60ull, // MissionPreparationCallbackImpl_Update
            0x142c1d9b0ull, // Soldier2FaceSystem_Instance
            0x140F80BD0ull, // StaffControllerImpl_AddStaffShort
            0x140F80F10ull, // StaffControllerImpl_AddStaffFull
            0x140AB0E40ull, // tpp::mbm::impl::StaffIdToFaceId
            0x140F97730ull, // tpp::mbm::impl::StaffControllerImpl::StaffSeedToFaceTexturePathId
            0x1416DFB20ull, // tpp::ui::menu::mbm::impl::StaffListCallbackImpl::RefreshFaceRecord
            0x141DEF770ull, // fox::ui::WindowFactoryCollector::FindWindowFactory
            0x141DABC30ull, // fox::ui::WindowFunction::CreateWindow
            0x1408688D0ull, // tpp::ui::hud::CommonDataManager::SetSubjectiveWindowHandle
            0x141DEF770ull, // fox::ui::WindowFactoryCollector::FindWindowFactory (implementation)
            0x1408688D0ull, // tpp::ui::hud::CommonDataManager::SetSubjectiveWindowHandle (implementation)
            0x1409ED5DDull, // CalcFv2ResourceManagerInfoForFace
            0x1409EDE71ull, // ConvertExtendFaceFova2PathToFovaFile
            0x1409EE485ull, // ConvertFova2PathToFovaFileForProg
            0x1409F0637ull, // GetFaceFovaFpkPathIdArrayAtFaceId
            0x1409F06A9ull, // GetFaceFovaFpkPathIdAtFaceId
            0x1409F07A7ull, // GetFaceFovaPathIdArrayAtFaceId
            0x1409F0819ull, // GetFaceFovaPathIdAtFaceId
            0x1409F0AB5ull, // GetFaceFpkFileCodeListImpl
            0x1409F0CA4ull, // GetFaceFpkFileCodeListImpl
            0x1409F119Eull, // GetHairFovaPathIdAtFaceId
            0x1409F36B3ull, // PushFaceFovaFpk
            0x1409F4710ull, // SetFaceFovaDefinitionTableImpl
            0x1409F4F40ull, // SetFovaFileTable
            0x140A3B860ull, // EquipParameterTablesImpl::GetAmmoParameterBlock
            0x140A84290ull, // WalkerGearSystemImpl::GetSubWeaponLoadAmmoMax
            0x140D186D0ull, // BuddyQuietEquipControllerImpl::SetupCurrentSlot
            0x140EBAA40ull, // HostageEquipControllerImpl::SetupCurrentSlot
            0x14146FE60ull, // SoldierEquipControllerImpl::SetupCurrentSlot
            0x1417E0090ull, // BossQuietEquipControllerImpl::SetupCurrentSlot
            0x140F65EB0ull, // EquipDevelopControllerImpl::GetBulletCountMagazine
            0x140F65FE0ull, // EquipDevelopControllerImpl::GetBulletCountMax
            0x140F66110ull, // EquipDevelopControllerImpl::GetBulletType
            0x140DC38CFull, // EquipSystemImpl::SetUpGunInfoFromGunPartsDesc (receiver)
            0x140DC3AC4ull, // EquipSystemImpl::SetUpGunInfoFromGunPartsDesc (barrel)
            0x140DC3C9Aull, // EquipSystemImpl::SetUpGunInfoFromGunPartsDesc (magazine)
            0x140DC3C41ull, // EquipSystemImpl::SetUpGunInfoFromGunPartsDesc (stock)
            0x140DC3CDFull, // EquipSystemImpl::SetUpGunInfoFromGunPartsDesc (muzzleOption)
            0x140DC3E3Cull, // EquipSystemImpl::SetUpGunInfoFromGunPartsDesc (sight)
            0x140DC3EC0ull, // EquipSystemImpl::SetUpGunInfoFromGunPartsDesc (sight2)
            0x140DC3D51ull, // EquipSystemImpl::SetUpGunInfoFromGunPartsDesc (laserFlash1)
            0x140DC3DAFull, // EquipSystemImpl::SetUpGunInfoFromGunPartsDesc (laserFlash2)
            0x140DC3F10ull, // EquipSystemImpl::SetUpGunInfoFromGunPartsDesc (underBarrel)
            0x140DC3F96ull, // EquipSystemImpl::SetUpGunInfoFromGunPartsDesc (underBarrelCmp)
            0x140EE8E20ull, // InventoryImpl::CalcAmmoAndStockIfPickUp
            0x140A2A350ull, // EquipIdTableImpl::GetRootParameterId
            0x140F61D40ull, // DeployControllerImpl::GenerateDeployMission
            0x140F5F3D0ull, // DeployControllerImpl::AddLogMission
            0x140F61DF0ull, // DeployControllerImpl::GenerateDeployMissionInfoOffline
            0x140F614C0ull, // DeployControllerImpl::CreateDeployMissionSeedParam
            0x140F62900ull, // DeployControllerImpl::GetDeployMissionNameMessageId
            0x140F62380ull, // DeployControllerImpl::GetDeployMissionCaptionMessageId
            0x140F627B0ull, // DeployControllerImpl::GetDeployMissionNameKeyId
            0x140F63900ull, // DeployControllerImpl::RefreshMissionList
            0x140F61CE0ull, // DeployControllerImpl::GetNextRandomMissionId
            0x140F609F0ull, // DeployControllerImpl::CompletedDeployMissionOffline
            0x140AB0C70ull, // MotherBaseManagementSystemImpl::SetupAfterRestoreFromSVars
            0x140F76F90ull, // PoolRewardControllerImpl::GeneratePoolRewardsInfo
            0x141645D90ull, // CombatDeployCallbackImpl::RefreshMissionIconInGlobe
            0x1413dbcc0ull, // NoticeAntiBuddyWalkerGearAiImpl::ShouldSleep
            0x1413e03c0ull, // CombatAntiBuddyWalkerGearAiImpl::UpdatePrimaryTarget
            0x14138dba0ull, // AttackActionImpl::HasOtherSoldierOnVec
            0x140d31360ull, // Bullet3Impl::OnOffenseHit
            0x14145e480ull, // soldier DamageHandlerImpl::ProcessDamages
            0x14136CEF0ull, // Soldier2Impl::UpdateMarkers
            0x141643720ull, // CombatDeployMissionListRecordCallFunc::Refresh
            0x141649910ull, // SetRevengeIcon
            0x1413df0a0ull, // CombatAntiBuddyWalkerGearAiImpl::StepSurprise
            0x140341330ull, // AK::MemoryMgr::Malloc
            0x1403412A0ull, // AK::MemoryMgr::Malign
            0x140341060ull, // AK::MemoryMgr::Free
            0x142B9EC18ull, // AK::MemoryMgr::s_pMemPools
            0x142A0223Cull, // g_DefaultPoolId
            0x142B9EB1Cull, // g_PrepareEventMemoryPoolID
            0x141391ef0ull, // Soldier2Util::SearchActiveMember
            0x1413e0430ull, // CombatAntiBuddyWalkerGearAiImpl::UpdateSnipeTarget
            0x1415a66e0ull, // WalkerGearActionImpl::HasOtherSoldierOnVec
            0x141646010ull, // CombatDeployCallbackImpl::RefreshMissionList
            0x14086F0B0ull, // MbTopMenuSelectorEv::UpdateMenuNotice
            0x1414de320ull, // NoticeControllerImpl::CheckNoiseNoticeSoldier
            0x1414fca30ull, // NoticeDownComradeAiImpl::ShouldSleep
            0x1415b4870ull, // VehicleNoticeDownComradeAiImpl::ShouldSleep
            0x14155B590ull, // RideVehicleActionImpl::HasOtherSoldierOnVec
            0x1414A99E0ull, // LifeControllerImpl::SendStatusMessage
            0x1413DB780ull, // CombatAntiBuddyWalkerGearAiImpl::CheckGrenade
            0x1411FBB50ull, // IDroidActionPluginImpl::CloseState
            0x1411FB2F0ull, // IDroidActionPluginImpl::Deactivate
            0x140DC4400ull, // EquipSystemImpl::SetItemVisible
            0x1411F9DF0ull, // IDroidActionPluginImpl::IsPutAwayInterrupted
            0x14121E800ull, // IDroidActionPluginImpl::IsOtherAvatar
            0x141C75AFFull, // MotionGraphBlendNodeTraverser leaf clip-id read
            0x141A6C270ull, // AnimControl::GetAnimFileFromGaniPath
            0x141A93B60ull, // AnimLoader::GetMotionData
            0x1410023E0ull, // player action plugin GetStance (1 stand, 2 squat, 3 crawl, 4 supine)
            0x1411816F3ull, // CureActionPluginImpl::StateCureStart GetStance call
            0x1411817ADull, // CureActionPluginImpl::StateCureStart GetStance call
            0x141181009ull, // CureActionPluginImpl::StateCure GetStance call
            0x141181329ull, // CureActionPluginImpl::StateCureEnd body GetStance call
            0x1410A1780ull, // BasicActionImpl::SelectWakeupState
            0x141180700ull, // CureActionPluginImpl::FinishCureSprayEffect
            0x141180E90ull, // CureActionPluginImpl::StateCure
            0x14110F600ull, // CallMenuManagerImpl::UpdateCommandSetInterrogation
            0x141360FA0ull, // Soldier2Impl::ProcessSignal
            0x141512AA0ull, // NoticeNoiseAiImpl::SearchSpecificComrade
            0x1414DCF10ull, // NoticeControllerImpl::CheckForceNotice
            0x14110FE70ull, // CallMenuManagerImpl::UpdateInShowing
            0x1411101C0ull, // CallMenuCommandSet::UpdateState
            0x14110FAB0ull, // CallMenuManagerImpl::UpdateCommandSetRescueBoy
            0x141368990ull, // soldier::ActionController::SetCommand
            0x141520330ull, // NoticeNoiseAiImpl::StepMovePause
            0x140EB07E0ull, // hostage CoreAiImpl::SetSpecialActionParam
            0x140EDB250ull, // hostage SpecialActionImpl::State_SpecialIdle
            0x14179C130ull, // ui::hud::CallMenuImpl::UpdateOption
            0x14179CBC0ull, // ui::hud::CallMenuImpl::UpdateSelectDoc
            0x14110E7D0ull, // CallMenuManagerImpl::ShowCallMenu
            0x142B743B0ull, // fox::gr::dg::MaterialManager::s_Instance (data)
            0x142B743A8ull, // fox::gr::dg::ShaderTechniqueManager::s_Instance (data)
            0x140DA2090ull, // cp::RadioControllerImpl::StepRadioNoResponse
            0x140DA2340ull, // cp::RadioControllerImpl::StepRadioWaitResponse
            0x140D581D0ull, // cp::CommandPost2Impl::Controller_RequestRadioByMember_
            0x140D589D0ull, // cp::CommandPost2Impl::Controller_SetRadioMemberReady_
            0x140D57750ull, // cp::CommandPost2Impl::Controller_ReleaseRadioByMember_
            0x140D5B090ull, // cp::CommandPost2Impl::FinishRadio
            0x140DA1CA0ull, // cp::RadioControllerImpl::StepRadio
            0x140D58A10ull, // cp::CommandPost2Impl::Controller_UpdateRadio_
            0x140D62610ull, // cp::CommandPost2Impl::RequestRadioByCp
            0x140DA1C30ull, // cp::RadioControllerImpl::StepIdle
            0x140D58B80ull, // tpp::gm::CpRadioService::ConvertSpeechLabelToRadioType
            0x140C10030ull, // tpp::gm::impl::AiControllerImpl::WakeNode
            0x140C0F860ull, // tpp::gm::impl::AiControllerImpl::SleepNode
            0x141355600ull, // tpp::gm::soldier::Soldier2InterrogateUtil::IsTranslatable
            0x140A466B0ull, // tpp::gm::buddy::impl::Buddy2BlockControllerImpl::GetPathAtBuddyType
            0x140A46400ull, // tpp::gm::buddy::impl::Buddy2BlockControllerImpl::GetBuddyQuietWeaponFileFromBuddyBlock
            0x140A46330ull, // tpp::gm::buddy::impl::Buddy2BlockControllerImpl::GetBuddyQuietFovaFile
            0x140A463D9ull, // tpp::gm::buddy::impl::Buddy2BlockControllerImpl::GetBuddyQuietFv2FileFromBuddyBlock (fv2 id load)
            0x140F65D80ull, // EquipDevelopControllerImpl::GetBuddyPartsDevelopInfoIndex
            0x140F65DF0ull, // EquipDevelopControllerImpl::GetBuddyPartsId
            0x140F65E50ull, // EquipDevelopControllerImpl::GetBuddyPartsKind
            0x142C29030ull, // tpp::gm::buddy::impl::g_buddyService (BuddyServiceImpl instance)
            0x140A4EBB0ull, // tpp::gm::buddy::impl::BuddyServiceImpl::ChangeHorseSettings
            0x140A4E750ull, // tpp::gm::buddy::impl::BuddyServiceImpl::ChangeDogSettings
            0x140A51140ull, // tpp::gm::buddy::impl::BuddyServiceImpl::GetQuietEquipIdByWeaponType
            0x140A52850ull, // tpp::gm::buddy::impl::BuddyServiceImpl::IsUseSuppressorByWeaponType
            0x140A4ED1Dull, // tpp::gm::buddy::impl::BuddyServiceImpl::ChangeQuietCostume (costume clamp)
            0x140A54D27ull, // TppBuddyService.SetVarsQuietCostume (costume clamp)
            0x140A4EEC1ull, // tpp::gm::buddy::impl::BuddyServiceImpl::ChangeQuietWeaponType (weapon clamp)
            0x140A54DA2ull, // TppBuddyService.SetVarsQuietWeaponType (weapon clamp)
            0x140A4EC2Cull, // tpp::gm::buddy::impl::BuddyServiceImpl::ChangeHorseSettings (tack check)
            0x140A4E799ull, // tpp::gm::buddy::impl::BuddyServiceImpl::ChangeDogSettings (gear check)
            0x140CE3BF8ull, // tpp::gm::buddyquiet::impl::BuddyQuiet2Impl weapon change command (weapon clamp)
            0x140CE4893ull, // tpp::gm::buddyquiet::impl::BuddyQuiet2Impl::SetBruise (costume check)
            0x1416AAD47ull, // tpp::ui::menu::impl::ItemSelectorCallbackImpl::SetupPrefabListElement (Quiet costume list gate)
            0x1416AA897ull, // tpp::ui::menu::impl::ItemSelectorCallbackImpl::SetupPrefabListElement (Quiet weapon list gate)
            0x141458A2Bull, // tpp::gm::soldier::impl::DamageAiImpl::StepBitDamage (dog gear check)
            0x140D04D00ull, // tpp::gm::buddyquiet::impl::(anon)::CombatAiImpl::SendFireSignal
            0x140D180C0ull, // tpp::gm::buddyquiet::impl::EquipControllerImpl::InitCurrentSlot
            0x140D18880ull, // tpp::gm::buddyquiet::impl::EquipControllerImpl::Update
            0x140D18420ull, // tpp::gm::buddyquiet::impl::EquipControllerImpl::SetMotion
            0x140A46500ull, // tpp::gm::buddy::Buddy2BlockController::GetFileForPathFromBuddyBlock
            0x142C28930ull, // tpp::gm::buddy::Buddy2BlockController instance pointer
            0x140E511D0ull, // tpp::gm::horse::impl::ActionCoreImpl::ExecDefenseCallback tack damage switch
            0x140E673A8ull, // tpp::gm::horse::impl::BasicAction2Impl::UpdateGallopMotionSpeed tack switch
            0x140E58872ull, // tpp::gm::horse::impl::BasicAction2Impl::EnterOffenseTarget tack switch
            0x140E7BFABull, // horse motion sound tack switch (FUN_140E7BE70)
            0x140E58900ull, // tpp::gm::horse::impl::BasicAction2Impl::Execute
            0x140A51483ull, // tpp::gm::buddy::impl::BuddyServiceImpl D-Dog order variant gear check (FUN_140A515B0)
            0x140A51684ull, // tpp::gm::buddy::impl::BuddyServiceImpl::InitBuddyCommandForStatus gear check
            0x140C89697ull, // tpp::gm::buddydog::impl::DamageHandlerImpl::ProcessDamages gear check
            0x140C89470ull, // tpp::gm::buddydog::impl::DamageHandlerImpl::IsNeedReaction gear check
            0x140C88686ull, // tpp::gm::buddydog::impl::DamageHandlerImpl::CallDamageEffect gear check
            0x140C88E94ull, // tpp::gm::buddydog::impl::DamageHandlerImpl::CallDamageSound gear check
            0x140C8F5EEull, // tpp::gm::buddydog::impl::EscapeIdleAiImpl::StepSearch gear check
            0x140CADC73ull, // tpp::gm::buddydog::impl::NoticeDamageAiImpl::Setup gear check
            0x140C63F63ull, // tpp::gm::buddydog::impl::AttackAiImpl::ReTarget gear check
            0x1414CED05ull, // tpp::gm::soldier::impl::NoticeBuddyDogAiImpl::StepAware gear check
            0x140C564A1ull, // tpp::gm::buddydog::impl::BuddyDog2Impl::Initialize gear copy to +0xF4
            0x140A50A60ull, // tpp::gm::buddy::impl::BuddyServiceImpl::GetBuddyCommandForWeapon gear check
            0x140F66410ull, // tpp::mbm::impl::EquipDevelopControllerImpl::GetDWalkerPartsDevelopInfoIndex
            0x140F66470ull, // tpp::mbm::impl::EquipDevelopControllerImpl::GetDWalkerPartsDevelopedCurrentLevel
            0x140F66510ull, // tpp::mbm::impl::EquipDevelopControllerImpl::GetDWalkerPartsId
            0x140F66620ull, // tpp::mbm::impl::EquipDevelopControllerImpl::GetDWalkerPartsLevel
            0x140F66680ull, // tpp::mbm::impl::EquipDevelopControllerImpl::GetDWalkerPartsType
            0x14170ADD0ull, // FUN_14170AD80 sub weapon type check
            0x140A4EB70ull, // tpp::gm::buddy::impl::BuddyServiceImpl::ChangeGearSubWeaponSettings type limit
            0x140A03D80ull, // tpp::gm::EquipBlockController2::StartLoadingToEquipMissionBlock
            0x140A055D0ull, // FUN_140A057C0 EquipDataSystemAccessorImpl vt+0x38, block holding an equip's pack
            0x14125CFEDull, // RideWalkerGearActionImpl::ChangeWeapon sub weapon type switch
            0x14125F250ull, // RideWalkerGearActionImpl::GetVehicleEquipId sub weapon type switch
            0x14125FD8Full, // RideWalkerGearActionImpl::SetEquipWeaponId sub weapon type switch
            0x140939430ull, // HUD utility vt+0x428, name text of a tpp::VehicleEquipId (real name unknown)
            0x140938F80ull, // HUD utility vt+0x420, icon path of a tpp::VehicleEquipId (real name unknown)
            0x14125E796ull, // RideWalkerGearActionImpl rider UI update, sub weapon type -> reticle switch
            0x1419655A0ull, // CustomizeSelectorCallbackImpl item list for the open category (real name unknown)
            0x140ADF5D0ull, // tpp::gm::impl::SimplePartsEffectControllerImpl::CreateEffectImpl
            0x141B19120ull, // fox::fx::Effect::Create
            0x142A82C28ull, // tpp::gm::player Player2System instance
            0x141A76380ull, // fox::anim::AnimFile::GetEventData
            0x141307010ull, // tpp::gm::impl::equip::RealizedEquipObjectImpl::MotionEventCallbackImpl
            0x141303DC0ull, // tpp::gm::impl::equip::RealizedEquipObjectImpl::EjectCasing
            0x141059690ull, // tpp::gm::player::impl::attack::AttackActionImpl::SetEquipMotion
            0x141052970ull, // tpp::gm::player::impl::attack::AttackActionImpl::MotionEventCallback
            0x140A1CE30ull, // tpp::gm::impl::(anon)::GetPackagePaths
            0x1400AB8F0ull, // fox::Array<fox::Path>::PushBack
            0x1400856D0ull, // fox::Path::Path(const char*)
            0x141A7D180ull, // fox::anim::EventCallback::ExecCallback
            0x1416ACA00ull, // tpp::ui::menu::impl::ItemSelectorCallbackImpl::Update
            0x1409B2550ull, // tpp::gm::player::impl::Player2BlockController::GetFileFromPartsBlockNew
            0x142C1B0F8ull, // the Player2BlockController pointer it is called with
            0x1404E2080ull, // fox::FormVariationFile2::ApplyFormVariation (ModelBuilder)
            0x140AEED80ull, // tpp::gm::player::impl::Fova2ControllerImpl work: type 0x11 extra after an fv2 apply
            0x140A2A550ull, // tpp::gm::impl::equip::EquipIdTableImpl::GetSupportWeaponTypeId
            0x140A420D0ull, // tpp::gm::impl::equip::EquipParameterTablesImpl::ReloadEquipParameterTablesImpl
            0x140A42205ull, // ReloadEquipParameterTablesImpl: add rax, 0x2D8 (SupportWeaponParameter size pass)
            0x140A4221Eull, // ReloadEquipParameterTablesImpl: lea rax, [rcx+0x2D8] (allocation pass)
            0x1415D5AF0ull, // tpp::gm::impl::throwing::ThrowingImpl::GetBlastParamByEquipId
            0x1415D94F0ull, // tpp::gm::impl::throwing::ThrowingImpl::UpdateActionGrenade (grenade and stun)
            0x1415D9D20ull, // tpp::gm::impl::throwing::ThrowingImpl::UpdateActionSmoke (smoke, sleeping gas, flares)
            0x1415D9AD0ull, // tpp::gm::impl::throwing::ThrowingImpl::UpdateActionMolotovCocktail
            0x1415D9240ull, // tpp::gm::impl::throwing::ThrowingImpl::UpdateActionDecoy
            0x1415D9820ull, // tpp::gm::impl::throwing::ThrowingImpl::UpdateActionKibidango
            0x1415D93D0ull, // tpp::gm::impl::throwing::ThrowingImpl fake sign activation
            0x1415DD670ull, // tpp::gm::impl::throwing::ThrowingImpl::UpdateStatus
            0x1415DE500ull, // tpp::gm::impl::throwing::ThrowingSystemImpl::ExecuteSerially
            0x1415D2220ull, // tpp::gm::impl::throwing::ThrowingImpl::ActivateThrowingAtEmptyWork
            0x1415DAAF0ull, // tpp::gm::impl::throwing::ThrowingImpl::UpdateDeactivation
            0x1415D2D20ull, // tpp::gm::impl::throwing::ThrowingImpl::AddReboundNoise
            0x1415D83D0ull, // tpp::gm::impl::throwing::ThrowingImpl::PostReboundSound
            0x1415D7410ull, // tpp::gm::impl::throwing::ThrowingImpl::IsObstructSight
            0x140F66990ull, // tpp::mbm::impl::EquipDevelopControllerImpl::GetDamageType
            0x1411ADA40ull, // tpp::gm::player::impl::EquipControllerImpl::InitializePlayerAtIndex
            0x140EEDD70ull, // tpp::gm::impl::LoadoutManagerImpl::RestoreLoadoutInfosFromVars
            0x140EEE390ull, // tpp::gm::impl::LoadoutManagerImpl sortie loadout slot restore
            0x140EEE700ull, // tpp::gm::impl::LoadoutManagerImpl return-heli loadout slot restore
            0x140FCDE50ull, // tpp::gm::impl::placed::PlacedSystemImpl::RestorationPlace
            0x140ACA750ull, // tpp::gm::impl::SupplyCboxGameObjectImpl::RestoreRequestFromSVars
            0x1409D2A50ull, // tpp::gm::player::impl::Player2UtilityImpl::GetPickableInfosOfAmmoBox
            0x140A3CEB0ull, // tpp::gm::impl::equip::EquipParameterTablesImpl::GetSupportWeaponParameterBlock
            0x140F66820ull, // tpp::mbm::impl::EquipDevelopControllerImpl::GetDamageAmountRatio
            0x14166B3B0ull, // tpp::ui::menu::mbm::impl::EquipDetailsCallbackImpl::SetupSupportParameterDetails (bar panel caller range)
            0x141483A70ull, // tpp::gm::soldier::impl::IdleActingActionImpl::CallVoice
            0x14158C150ull, // tpp::gm::soldier::impl::Soldier2SoundControllerImpl::__callVoiceWithoutSync
            0x14158C060ull, // tpp::gm::soldier::impl::Soldier2SoundControllerImpl::__callVoiceWithSync
            0x14154F7A0ull, // tpp::gm::soldier::impl::RadioActionImpl::State_RadioChaffEnd
            0x14095593Dull, // tpp::gm::EquipIdTable::GetAmmoStockIdOfSupportOrItem: lea eax,[rbx-0x50A]
            0x1409525C3ull, // MissionPreparationSystemImpl::CalcEquipWeaponItemCostFromLoadout support loop: lea r9d,[rbx-0x50A]
            0x140EEF623ull, // LoadoutManagerImpl::StoreLoadoutInfoToPlayerVars support cells: add edi,-0x50A
            0x1411AE629ull, // EquipControllerImpl::InitializePlayerAtIndex support slots: lea r8d,[rbx-0x50A]
            0x141208682ull, // PickUpActionPluginImpl::CheckLoadableForSupportOrItem: lea r8d,[rbx-0x50A]
            0x14120975Eull, // PickUpActionPluginImpl::DoPickUpPlaced carried slot: lea eax,[rsi-0x50A]
            0x141209802ull, // PickUpActionPluginImpl::DoPickUpPlaced picked item: lea ecx,[r15-0x50A]
            0x14120A2F4ull, // PickUpActionPluginImpl::DoPickUpWithPickableInfo carried slot: lea ecx,[rsi-0x50A]
            0x1416A3B1Full, // ItemSelectorCallbackImpl::GetCurrentAmmoFromVarsForSupportWeapon: add ebx,-0x50A
            0x140A3BA55ull, // EquipParameterTablesImpl::GetAmmoParameterBlock: lea edi,[rbx-0x50A]
            0x140DA2A10ull, // tpp::gm::impl::cp::RadioSpeechHandlerImpl::CallPart
            0x1412DCDD0ull, // tpp::gm::impl::RangeAttackSystemImpl::ActivateChaff
            0x140B087C0ull, // tpp::gm::impl::SoundControllerImpl::IsPlayingVoice
            0x1414B07D0ull, // tpp::gm::soldier::impl::MortarActionImpl::State_WaitAction
            0x1414B0960ull, // tpp::gm::soldier::impl::UnrealMortarActionImpl::State_WaitAction
            0x140DDABD0ull, // tpp::gm::heli::impl::Heli2Impl::Unrealize
            0x1414B0020ull, // tpp::gm::soldier::impl::MortarActionImpl::State_MortarAction
            0x1414B0300ull, // tpp::gm::soldier::impl::UnrealMortarActionImpl::State_MortarAction
            0x140DA1F40ull, // tpp::gm::impl::cp::RadioControllerImpl::StepRadioChaff
            0x140AB9540ull, // tpp::gm::radio::RadioCommand::RegisterPriorityTable
            0x14094FEA0ull, // tpp::ui::menu::impl::MissionPreparationSystemImpl::CalcEquipWeaponItemCostFromLoadout
            0x140953A20ull, // tpp::ui::menu::impl::MissionPreparationSystemImpl::CalcEquipWeaponItemReturnResourcesFromLoadout
            0x140A222F0ull, // tpp::gm::impl::ScriptVars_LoadVarsFromSlot
            0x140A22600ull, // tpp::gm::impl::ScriptVars_SaveVarsToSlot
            0x140A22E90ull, // tpp::gm::impl::ScriptVars_WriteSlotToFile
            0x14105BB60ull, // AttackActionImpl_SetVisibleLaserSight
            0x1411B0D50ull, // EquipControllerImpl_SetUpPartsStateForGun
            0x1408B3F70ull, // EquipCrossEvCall_SetPrimaryOptionPanel
            0x1408B0490ull, // EquipCrossEvCall_PrimaryOptionSelect
            0x1408B1380ull, // EquipCrossEvCall_SecondaryOptionSelect
            0x1408AEF70ull, // EquipCrossEvCall_ItemToggleChange
            0x1408AB380ull, // EquipCrossEvCall_CallEquipSE
            0x140916CB0ull, // Ui_PauseInitUiAnimation
            0x14091B7F0ull, // Ui_StartUiAnimation
            0x14091B780ull, // Ui_StartInstantUiAnimation
            0x1409179F0ull, // Ui_SetTextForModelNodeText
            0x140913620ull, // Ui_GetPadButtonUvShift
            0x141DACF20ull, // Fox_Ui_ConnectComponent
            0x141DC78A0ull, // ModelNodeMesh_SetShaderParameter
            0x140025330ull, // ModelNode_SetVisible
            0x1408B2980ull, // EquipCrossEvCall_SetPanelInfo
            0x1409124F0ull, // Ui_GetKeyboardIconUV
            0x141DD6320ull, // FontManager_SetIconTextureCoord
            0x142C8FAB8ull, // RawDaemon_s_fontManager
            0x140DC1A50ull, // EquipSystemImpl_SetAmmoCount
            0x140BF1DE0ull, // QuarkCollector_GetInstanceForCreatorId
            0x140023FE0ull, // LwLockSection_Enter
            0x140024130ull, // LwLockSection_Leave
            0x14238FD10ull, // MbDvcMapCallbackImpl_QuarkVtable
            0x14227F900ull, // MbDvcMapLocationParameterImpl_Vtable
            0x140AC8CB0ull, // SpeechSystemImpl_FindSpeechSequenceIndex
            0x142C2EFA0ull, // SpeechFileTable
            0x142C32FA8ull, // SpeechFileTableLock
            0x142BD3600ull, // TppWeatherManager_s_Instance
            0x1414E3410ull, // NoticeControllerImpl_CheckSpecialSaluteVoiceLabel
            0x140AEEA70ull, // Fova2ControllerImpl_SetChickenVisibility
            0x140E10330ull,  // HeliLightActionImpl_Execute
            0x140E070D0ull,  // HeliGunActionImpl_IsAimTypePosition
            0x140E06C90ull,  // HeliGunActionImpl_GetAimPosition
            0x140DDD740ull,  // HeliActionCoreImpl_Execute
            0x140B92510ull,  // HeliEffectControllerImpl_CreateEffect
            0x140E23990ull,  // HeliSoundControllerImpl_CallSound
            0x140DEFC50ull,  // HeliDamageHandlerImpl_ProcessQueuedDamages
            0x140E14F80ull,  // MechaActionImpl_StateOnPassenger
            0x140E14B40ull,  // MechaActionImpl_StateOffPassenger
            0x141245860ull,  // RideVehicleActionImpl_CheckVehicleIconArea
            0x141556C20ull,  // RideHeliActionImpl_State_GetInAction
            0x141557AE0ull,  // RideHeliActionImpl_State_IdleAction
            0x1415563F0ull,  // RideHeliActionImpl_SetRootAdjustMatrix
            0x140E19C70ull,  // HeliPassengerControllerImpl_GetAttachMatrix
            0x140B17E40ull,  // RealizedSoldier2Impl_SetModelAlphaAndMeshVisibilityImmediately
            0x141AD8190ull,  // CcRootAdjustAction_Exec
            0x141557CF0ull,  // UnrealRideHeliActionImpl_State_IdleAction
            0x141557200ull,  // RideHeliActionImpl_State_GetOutAction
            0x14138B500ull,  // SoldierAttackActionImpl_Execute
            0x1413922E0ull,  // SoldierAttackActionImpl_ShootOneBullet
            0x14138D4F0ull,  // SoldierEquipController_GetMuzzleWorldMatrix
            0x141443070ull,  // SoldierDamageActionImpl_Execute
            0x141365520ull,  // Soldier2Impl_RequestDamage
            0x1413517F0ull,  // Soldier2Impl_GetLatestDamageOffenseOwnerId
            0x140DBEAE0ull,  // EquipSystemImpl_GetGunInfoById
            0x1416A4E50ull,  // ItemSelector_IsGetLatestItemTool
            0x1407F1980ull,  // FobTarget_RequestSneakCommon
            0x141686BC0ull,  // FobMission2CallbackImpl_OpenErrorPopup
            0x1408685A0ull,  // HudCommonDataManager_SetPopupText
            0x1405FBE70ull,  // TppPermanentGimmickUnit_CallNoise
            0x141391910ull,  // SoldierAttackActionImpl_RequestFire
            0x141430770ull,  // Soldier2ActionUtil_ResetEquipMotion
            0x141430B50ull,  // Soldier2ActionUtil_StartEquipReloadMotion
            0x140868220ull,  // HudCommonDataManager_SetPopupErrorParam
            0x1419502A0ull,  // UavLifeControllerImpl_SendStatusMessage
            0x141672E70ull,  // EquipDevelopCallbackImpl_IsLatestItemTool
            0x1411B2E50ull,  // EquipControllerImpl_UpdateBattery
            0x1409383E0ull,  // UiUtilityImpl_GetBatteryLevel
            0x140FF2497ull,  // ActionCoreImpl_ExecPostPutMotion_EmpGate
            0x140D68380ull,  // CpRadioService_CheckRadioTypeBgmPhaseMismatch
            0x1408DBAD0ull,  // TppUiCommand_SetMbTopMenuItemVisible
            0x1408D1E70ull,  // TppUiCommand_InitAllMbTopMenuItemVisible
            0x1409683A0ull,  // MotherBaseDeviceTopMenuTableImpl_SetVisibleItem
            0x140968080ull,  // MotherBaseDeviceTopMenuTableImpl_IsVisibleItem
            0x141095E60ull,  // AttackActionImpl_UpdateShootingByPad
            0x141058A50ull,  // AttackActionImpl_SetAmmoCount
            0x141623E70ull,  // TppUIWeaponNumImpl_SetPanelInfo
            0x141624A20ull,  // TppUIWeaponNumImpl_SetWeaponChargeGauge
            0x141623550ull,  // TppUIWeaponNumImpl_SetBatteryGauge
            0x140DBE5A0ull,  // EquipSystemImpl_GetEffectController
            0x140ADFF80ull,  // SimplePartsEffectControllerImpl_GetEffectWorldMatrix
            0x140418870ull,  // FoxFxEffect_SetVisible
            0x1404186F0ull,  // FoxFxEffect_SetKillNow
            0x141B19240ull,  // FoxFxEffect_SendDestroy
            0x1400160A0ull,  // FoxSharedPtrBase_Set
            0x1407A9430ull,  // FoxGeoResultI_GetPosition
            0x141B99680ull,  // FoxGeoResultI_GetNormal
            0x14045C450ull,  // GeoCheckControlN1_Construct
            0x141B9AFD0ull,  // FoxGeo_CollisionLineCheck
            0x140D456E0ull,  // ChimeraSystemImpl_GetEffectController
            0x140ADB270ull,  // SimplePartsControllerImpl_Create
            0x1413068F0ull,  // RealizedEquipObjectImpl_Initialize
            0x1401CD020ull,  // FoxGrModel_InvisibleMesh
            0x1401CF110ull,  // FoxGrModel_VisibleMesh
            0x1401CD2E0ull,  // FoxGrModel_IsMeshVisible
            0x141365000ull,  // Soldier2Impl_RealizeRealizedSoldier
            0x140AED040ull,  // Fova2ControllerImpl_ApplyPlayerFova
            0x140AF2DA0ull,  // AnimationControllerImpl_SetGameObjectIdToCharaControl
            0x1417A0B4Aull,  //RespawnActionPluginImpl_PollingImpl_OnlineGate
            0x140A097D0ull, // collection locator array destructor
            0x1408351C0ull, // CmdCheckServerItemCorrect::Pack
            0x140851A20ull, // CmdSendBootOption::Pack
            0x14080F800ull, // CmdSyncMotherBaseOption::Pack
            0x1408132F0ull, // CmdSyncSoldierBinOption::Pack
            0x140813C40ull, // CmdSyncSoldierDiffOption::Pack
            0x1407EAAD0ull, // SyncSoldierBinReceive::Success
            0x1411AC2F0ull, // tpp::gm::player::impl::EquipControllerImpl::ChangeEquip
            0x1411AFF50ull, // tpp::gm::player::impl::EquipControllerImpl::SetEquipState
            0x141674080ull, // EquipDevelopCallbackImpl::SetEquipDevelopVars
            0x140DC3220ull, // tpp::gm::impl::EquipSystemImpl::SetUpEquip
            0x14103C7D0ull, // CreateLaserSightController
            0x140EEF160ull, // LoadoutManagerImpl::StoreLoadoutInfoToPlayerVars
            0x1407F5D10ull, // SynchronizeLoadout
            0x140FC69F0ull, // PlacedSystemImpl::CopyToScriptVarsPlayerPlacedWeapon
            0x140FC7200ull, // PlacedSystemImpl::CreateByLocators
            0x1412082E0ull, // PickUpActionPluginImpl::CheckLoadableForAmmo
            0x141209F00ull, // PickUpActionPluginImpl::DoPickUpWithPickableInfo
            0x140FB9860ull, // PickableSystemImpl::CreateInstanceFromVars
            0x1409CE830ull, // tpp::gm::player::impl::Player2Impl::UpdateVoiceType
            0x141390370ull, // tpp::gm::soldier::impl::AttackActionImpl::ProcessFireControl
            0x141396AD0ull, // tpp::gm::soldier::impl::AttackActionImpl::SubState_Idle
            0x1413A3FB0ull, // tpp::gm::soldier::impl::BasicAction2Impl::State_ChangeStandOrSquat
            0x140D5DEF0ull, // TppCp2 lua command table
            0x14146E3C0ull, // tpp::gm::soldier::impl::EquipControllerImpl::DropEquip
            0x1414703C0ull, // tpp::gm::soldier::impl::EquipPluginImpl::Execute
            0x1414735D0ull, // tpp::gm::soldier::impl::EquipPluginImpl::NormalMtevCallback
            0x140EEA690ull, // InvestigateSituationOperatorImpl::GetSituationWithRequest
            0x1414BDE80ull, // tpp::gm::soldier::impl::NoticeActionImpl::Wake
            0x140514610ull, // RouteGroupImpl::GetEventAimCharacterId
            0x140514F70ull, // RouteGroupImpl::GetEventId
            0x140FE9450ull, // UiControllerImpl::UpdateEquipHud
            0x14115A2A0ull, // tpp::gm::player::impl::CqcActionPluginImpl::Work::CompleteContinueCqcTarget
            0x14151EB80ull, // tpp::gm::soldier::impl::NoticeNoiseAiImpl::CheckTargetPositionUpdate
            0x141523470ull, // tpp::gm::soldier::impl::NoticeNoiseAiImpl::StepWaitReach
            0x140DA36C0ull, // RadioSpeechHandlerImpl::SendFinishedMessage
            0x1409BF140ull, // tpp::gm::player::impl::Player2Impl::CallVoice
            0x14006AD30ull, // lua panic handler
            0x141DBC330ull, // IsReadyTexture
            0x1404E2330ull, // fv2 apply by group (real name unknown)
            0x140744110ull, // GetVfxFileFromParts
            0x140742E32ull, // return after the sprint spark GetVfxFileFromParts call
            0x142BF2698ull, // GameOver open flag
            0x14224A370ull, // GameOverEvent vtable
            0x140F50D70ull, // mother base security rank slot
            0x140F55680ull, // mother base security clamp slot
            0x140F6CCD0ull, // tpp::mbm::impl::EquipDevelopControllerImpl::IsDisplayableBluePrintIDContion
            0x140F67CE0ull, // blueprint condition count
            0x140F67120ull, // blueprint condition message ids
            0x142438A70ull, // develop grid flat tab table A
            0x142438AB0ull, // develop grid flat tab table B
            0x141A6B860ull, // additional motion mtar add
            0x141A6CC00ull, // additional motion mtar remove
            0x141166730ull, // tpp::gm::player::impl::CqcActionPluginImpl::StateHoldMove
            0x141164570ull, // tpp::gm::player::impl::CqcActionPluginImpl::StateGunHoldMove
            0x1409DAFE0ull, // RequestToSetTargetStance
            0x1411F90A2ull, // player LifeControllerImpl::UpdateHost delay load
            0x1411F8029ull, // player LifeControllerImpl::UpdateHost doubled damage delay load
            0x14147F5B0ull, // tpp::gm::soldier::impl::HoldupActionImpl::State_EndStandHoldup
            0x14147F3E0ull, // tpp::gm::soldier::impl::HoldupActionImpl::State_EndCrawlHoldup
            0x14147E230ull, // tpp::gm::soldier::impl::HoldupActionImpl drop weapon
            0x141482120ull, // tpp::gm::soldier::impl::HoldupAiImpl::StepHoldupStand
            0x141481C50ull, // tpp::gm::soldier::impl::HoldupAiImpl::StepHoldupDown
            0x1414825F0ull, // tpp::gm::soldier::impl::HoldupAiImpl unholster
            0x1414DDCE0ull, // NoticeControllerImpl::CheckNoiseNoticePlayer
            0x141573620ull, // tpp::gm::soldier::impl::RouteAiImpl::SetSubActionParamWithEdgeEvent
            0x141572170ull, // tpp::gm::soldier::impl::RouteAiImpl::SetAimingActionParam
            0x141572CCDull, // RouteAiImpl edge event move command return
            0x141572D2Bull, // RouteAiImpl edge event aim clear
            0x141573A20ull, // tpp::gm::soldier::impl::RouteAiImpl::SetSubActionParamWithNodeEvent
            0x1415729E0ull, // tpp::gm::soldier::impl::RouteAiImpl::SetLookActionParam
            0x141576478ull, // RouteAiImpl node event caution swap
            0x141574BA0ull, // tpp::gm::soldier::impl::RouteAiImpl::StepEvent
            0x1414848A0ull, // RouteAiImpl get target position
            0x140611B40ull, // TppPermanentGimmickGatlingGun::SetAimMatrix
            0x140FF04E1ull, // barrier effect alloc gate
            0x140FF1159ull, // barrier effect populate gate
            0x14179BA40ull, // tpp::ui::hud::impl::CallMenuImpl::UpdateCallMenu
            0x14179B230ull, // tpp::ui::hud::impl::CallMenuImpl::SetCommandText
            0x141DA6F90ull, // texture prefetch controller lookup
            0x141DBBF40ull, // texture prefetch check
            0x14105FE20ull, // SetupWeaponInfo
            0x141307D90ull, // tpp::gm::impl::equip::RealizedEquipObjectImpl::Realize
            0x141307170ull, // tpp::gm::impl::equip::RealizedEquipObjectImpl post put motion
            0x141307F12ull, // RealizedEquipObjectImpl motion entry index site
            0x1422A0ED8ull, // equip motion entry table vtable
            0x140A020A0ull, // AddMtarBlockPackagePaths
            0x140FC8070ull, // PlacedSystemImpl::Insert
            0x1409CE9E0ull, // SetWarpToPosition
            0x14118C240ull, // player death family tick
            0x14118E230ull, // player damage fall state
            0x140FF28F0ull, // player pre polling
            0x141194B00ull, // GetBasicDamageReaction
            0x140FF9560ull, // barrier blow callback
            0x140D2DD67ull, // bullet wall-hit SE site
            0x14175258Aull, // wall-object hit SE site
            0x141751608ull, // ricochet-spawn sound flag site
            0x140FF5C40ull, // barrier register defense callback
            0x142ACEB58ull, // barrier defense functor
            0x140FFAB30ull, // barrier predicate
            0x140FF4B70ull, // barrier player destructor
            0x142BE1280ull, // main viewport global
            0x1404380E0ull, // demo camera commit
            0x140421D30ull, // demo camera get control root
            0x140433A90ull, // demo camera anchor capture begin
            0x140433D00ull, // demo camera anchor capture end
            0x1414AA190ull, // tpp::gm::soldier::impl::LifeControllerImpl::Update
            0x140EC29C0ull, // tpp::gm::hostage::impl::LifeControllerImpl::Update
            0x141348F20ull, // animal LifeControllerImpl::Update
            0x1417623D0ull, // walker gear LifeControllerImpl::Update
            0x140B33620ull, // tpp::gm::vehicle::impl::GameObjectImpl::ReflectLife
            0x1409B2660ull, // LoadPlayerFv2s
            0x1409B3680ull, // LoadPlayerFv2s subset
            0x1404E3DC0ull, // CreateFv2ResourceManager
            0x1409B3580ull, // GetShell
            0x1409B3080ull, // GetShellForBlock
            0x1409F1E00ull, // GetFaceSystem
            0x1409F07F0ull, // GetFaceFovaPathIdAtFaceId
            0x1409F0680ull, // GetFaceFovaFpkPathIdAtFaceId
            0x14006F390ull, // fox::Block::Reload
            0x140A1E303ull, // SimpleMissionBlockController::Update checkpoint call of fox::Block::Reload
            0x1411AEC30ull, // tpp::gm::player::impl::EquipControllerImpl shield hit
            0x1411AEB00ull, // tpp::gm::player::impl::EquipControllerImpl::IsShieldBreakableDamage
            0x140ED7210ull, // Hostage2SoundControllerImpl::CallVoiceImpl
            0x14181D010ull, // liquid ActionCoreImpl::UpdateSleepVoice
            0x141830310ull, // liquid CarriedActionImpl::State_Carried
            0x1418376A0ull, // liquid DamageActionImpl::State_SmokeStart
            0x140F83840ull, // StaffControllerImpl::AssignSectionCheckBoxStaff
            0x1416DBF10ull, // staff list dismiss
            0x1416D3B10ull, // ResourceManagementCallbackImpl::UpdateState
            0x1407E7AD0ull, // ServerSynchronize::DestructNuclear
            0x1407F9B40ull, // FOB nuclear disposal request
            0x141280850ull, // SightManagerImpl::UpdateEspionageRadio
            0x140DC80F0ull, // EspionageRadioSystemImpl::GetGroupNameFromGameObjectId
            0x14079CE40ull, // atmosphere DirectionalLightController::Update
            0x14127EEE0ull, // binocular picker centre test
            0x142BE8B90ull, // TppStarSystem instance
            0x140739A50ull, // TppStarSystem vtable slot 0
            0x1422A60B0ull, // decoy system vtable
            0x141515090ull, // NoticeSightAiImpl radio start
            0x141516B00ull, // NoticeSightAiImpl wait help
            0x141521B10ull, // NoticeNoiseAiImpl radio start
            0x1415232E0ull, // NoticeNoiseAiImpl wait help
            0x14151ED50ull, // NoticeNoiseAiImpl::PreUpdate
            0x142C618A0ull, // NoticeNoiseAiImpl step table
            0x140AEEFE0ull, // Fova2ControllerImpl put and act motion
            0x141AB8200ull, // Animx control SetSpeedRate
            0x141AB81B0ull, // Animx control SetMotionData
            0x141E19010ull, // Animx control advance motion
            0x141A95A00ull, // Animx decode tracks
            0x140407700ull, // fox::fx effect SetWorldMatrix
            0x1400DDEE0ull, // fox block package GetFile
            0x1402165D0ull, // fox model bone world matrix
            0x141B8E7B0ull, // fox::fx effect instance Update
            0x141B267E0ull, // fox::fx effect sync
            0x141B40DD0ull, // fox::fx effect context acquire
            0x141B40D50ull, // fox::fx effect context release
            0x14033CE50ull, // AK::SoundEngine::RegisterGameObj
            0x14033E100ull, // AK::SoundEngine::UnregisterGameObj
            0x14033D610ull, // AK::SoundEngine::SetPosition
            0x14033C630ull, // AK::SoundEngine::PostEvent
            0x140391D60ull, // CAkParameterNodeBase::SetInitialRTPC
            0x140391F00ull, // CAkParameterNodeBase::SetNodeBaseParams
            0x140392570ull, // CAkParameterNodeBase::SetRTPC
            0x14034DC90ull, // CAkAudioLibIndex::GetNodePtrAndAddRef
            0x14033D670ull, // AK::SoundEngine::SetRTPCValue
            0x142B9EAF0ull, // AK g_pIndex
            0x142B9EB70ull, // AK g_pRTPCMgr
            0x142B9EB78ull, // AK g_pAudioMgr
            0x142B9EB98ull, // AK g_csMain
            0x140894130ull, // KeySettingEvCall::SetKeySettingMenu
            0x1408950C0ull, // KeySettingEvCall::SetListMenu
            0x140890C60ull, // KeySettingEvCall::GetListInfoText
            0x14088EF20ull, // KeySettingEvCall::DispKeySettingParam
            0x140896090ull, // KeySettingEvCall::SettingParamChanged
            0x140893790ull, // KeySettingEvCall::SetDecideParam
            0x140893320ull, // KeySettingEvCall::SetCancelParam
            0x140890B10ull, // KeySettingEvCall::GetConnectNodeName
            0x140016F50ull, // fox kernel string format
            0x1408945B5ull, // KeySettingEvCall::SetKeySettingMenu sound page row list
            0x1408945DBull, // KeySettingEvCall::SetKeySettingMenu sound page row list resume
            0x141DFA310ull, // fox::ui ReadUilb
            0x1408910B0ull, // KeySettingEvCall::GetModelAssetForPause
            0x140896E60ull, // KeySettingEvCall::VisibleKeySetting
            0x140896480ull, // KeySettingEvCall::UpdateKeySetting
            0x141DC2080ull, // fox::ui::Layout::GetAnimation
            0x140913390ull, // tpp::ui::utility::GetModel
            0x140913420ull, // tpp::ui::utility::GetModelNodeText
            0x14091B820ull, // tpp::ui::utility::StartLoopUiAnimation
            0x1402260B0ull, // FontData::SearchGlyphData
            0x140224C96ull, // FontTextureRender::GetStringWidth default char lookup return
            0x140225725ull, // FontTextureRender::RegisterString default char lookup return
            0x14022586Cull, // FontTextureRender::RegisterStringW default char lookup return
            0x1407E90E0ull, // staff combat section points
            0x1407E9260ull, // staff develop section points
            0x1407E8F20ull, // staff base development section points
            0x1407E9800ull, // staff support section points
            0x1407E9610ull, // staff spy section points
            0x1407E9400ull, // staff medical section points
            0x1411F5380ull, // player damage calc
            0x1411F9300ull, // player life update
            0x140984B70ull, // player rattle sound body
            0x140983F50ull, // player foot step sound body
            0x1411928C0ull, // CallDamageEffectWithReaction
            0x142399AF0ull, // suit param table
            0x142C1C2AAull, // script condition bits
            0x140900000ull, // step noise quiet scan window start
            0x141709CF0ull, // walker gear sub weapon slot setup
            0x140A06510ull, // equip block controller load by equipId
            0x140A07F50ull, // equip block controller load in block
            0x140A07780ull, // equip block controller find pack
            0x14170AD50ull, // WalkerGear_SetUpSubWeapon
            0x141715AD0ull, // walker gear show sidearm
            0x141724120ull, // walker gear sidearm shot
            0x140FEA440ull, // walker gear reticle update
            0x14195C2F0ull, // customize decide part
            0x140A4E180ull, // BuddyServiceImpl buddy equip settings
            0x140A4EA20ull, // BuddyServiceImpl::ChangeGearSettings
            0x141E02B50ull, // PlayerInfo GetPartsType
            0x14093017Dull, // return after GetLocalPartsType, run style
            0x14098D35Aull, // return after GetLocalPartsType, dash sound
            0x140743539ull, // return after GetPartsType, sprint spark
            0x140743549ull, // return after GetPartsType, sprint spark
            0x140743588ull, // return after GetPartsType, sprint spark
            0x140743646ull, // return after GetPartsType, sprint spark
            0x140743656ull, // return after GetPartsType, sprint spark
            0x1407448F5ull, // return after GetPartsType, sprint spark
            0x140744905ull, // return after GetPartsType, sprint spark
            0x1410B5360ull, // player dash move
            0x1410A06D0ull, // player dash attack
            0x140F65942ull, // develop row equipId read (GetAvailableEquipIDArray)
            0x140F6594Dull, // develop row equipId shr (GetAvailableEquipIDArray)
            0x140F65AD3ull, // develop row equipId read (Read02)
            0x140F65B63ull, // develop row equipId read (Read03)
            0x140F65BF3ull, // develop row equipId read (Read04)
            0x140F65C83ull, // develop row equipId read (Read05)
            0x140F66718ull, // develop row equipId read (Read06)
            0x140F6685Cull, // develop row equipId read (Read07)
            0x140F68AD3ull, // develop row equipId read (Read08)
            0x140F69294ull, // develop row equipId read (GetEquipID)
            0x140F69299ull, // develop row equipId shr (GetEquipID)
            0x140F692B4ull, // develop row equipId read (GetEquipIDFromDevelopIndex)
            0x140F692B9ull, // develop row equipId shr (GetEquipIDFromDevelopIndex)
            0x140F692CDull, // develop row equipId read (GetEquipPosition)
            0x140F698D3ull, // develop row equipId read (Read12)
            0x140F699B3ull, // develop row equipId read (Read13)
            0x140F69F26ull, // develop row equipId read (GetHighestGradeEuipId_Compare)
            0x140F69F2Aull, // develop row equipId shr (GetHighestGradeEuipId_Compare)
            0x140F69F72ull, // develop row equipId read (GetHighestGradeEuipId_Result)
            0x140F69F77ull, // develop row equipId shr (GetHighestGradeEuipId_Result)
            0x140F69ED0ull, // EquipDevelopControllerImpl::GetHighestGradeEuipId
            0x140A03397ull, // EquipIdTable InfoList rip-relative site
            0x140A03AB4ull, // EquipIdTable InfoList rip-relative site
            0x140A045B4ull, // EquipIdTable InfoList rip-relative site
            0x140A04642ull, // EquipIdTable InfoList rip-relative site
            0x140A05297ull, // EquipIdTable InfoList rip-relative site
            0x140A05990ull, // EquipIdTable InfoList rip-relative site
            0x140A05BE2ull, // EquipIdTable InfoList rip-relative site
            0x140A05C25ull, // EquipIdTable InfoList rip-relative site
            0x140A0657Cull, // EquipIdTable InfoList rip-relative site
            0x140A07366ull, // EquipIdTable InfoList rip-relative site
            0x140A077E8ull, // EquipIdTable InfoList rip-relative site
            0x140A07FCBull, // EquipIdTable InfoList rip-relative site
            0x140A0569Aull, // EquipIdTable InfoList absolute-displacement site
            0x140A056CEull, // EquipIdTable InfoList absolute-displacement site
            0x141DF5A10ull, // fox::ui::WindowManager::UpdateLayouts
            0x141DEF6C0ull, // fox::ui::WindowDaemon::GetWindowManager
            0x141DABD70ull, // fox::ui::WindowFunction::DestroyWindow
            0x141DBF440ull, // fox::ui::Animation::GetTimeLength
            0x141DB6870ull, // fox::ui::ModelNodeText::SetFontGroup
            0x14091B7C0ull, // tpp::ui::utility::StartSound
            0x141DCA330ull, // fox::ui::SoundControl::Stop
            0x142C8FAD8ull, // the SoundControl the UI graphs play through
            0x140024240ull, // fox::LwRWLockSection::LockShared
            0x140024440ull, // fox::LwRWLockSection::Unlock
            0x142B64238ull, // g_blockGroupsLock
            0x1429E3CA0ull, // first entry of the block group map
            0x1429E3CB8ull, // block group map nodes
            0x1429E3CC0ull, // block group map values
            0x141DACFC0ull, // fox::ui::LayoutUtility::DisconnectComponent
            0x141DF55C0ull, // fox::ui::WindowManager::DeleteDestroyedWindows
            0x141DA9860ull, // fox::ui::ObjControl::CreateCommon
            0x1400B3510ull, // fox::impl::EntityPtrBase::Set
            0x140912190ull, // tpp::ui::utility::GetGameCameraViewMatrix
            0x140913AE0ull, // tpp::ui::utility::GetProjectionMatrix
            0x140911FE0ull, // tpp::ui::utility::GetFocalLengthZoomRatio
            0x140916970ull, // tpp::ui::utility::IsOutsideScreen
            0x140914E60ull, // tpp::ui::utility::GetViewport
            0x1420A8078ull, // how far head markers rise per unit of zoom
            0x1421079A4ull, // the screen margin head markers use
            0x14093A360ull, // tpp::ui::utility::GetUiUtility
            0x14006FA00ull, // fox::Block::Unload
            0x14006D4E0ull, // fox::Block::Deactivate
            0x140917610ull, // tpp::ui::utility::SetButtonIconTextureByName
            0x1409179A0ull, // tpp::ui::utility::SetPlatformButtonTexture
            0x140915CC0ull, // tpp::ui::utility::IsGamePauseMenu
            0x140915D20ull, // tpp::ui::utility::IsInDemo
            0x14091B8F0ull, // tpp::ui::utility::StopSound
            0x140913EA0ull, // tpp::ui::utility::GetSoundTypeId
            0x141DBF890ull, // fox::ui::Animation::Pause
            0x141DBF3F0ull, // fox::ui::Animation::ForceLoop
            0x141DBFEE0ull, // fox::ui::Animation::UnsetForceLoop
            0x141DABB20ull, // fox::ui::WindowFunction::AddChildWindow
            0x141DAC220ull, // fox::ui::WindowFunction::PostTriggerMessage
            0x14006C970ull, // fox::BlockMemoryDesc ctor
            0x140061B20ull, // fox::SharedString(const char*)
            0x14001D4D0ull, // fox::SharedString copy
            0x14001D3C0ull, // fox::SharedString dtor
            0x14006D440ull, // fox::BlockGroup::Create
            0x14006E0B0ull, // fox::Block::Load(ids, count)
            0x14006D010ull, // fox::Block::Activate
            0x1400164A0ull, // fox::KernelString(const char*)
            0x140085930ull, // fox::Path::CInitWithString
            0x1400041C0ull, // jmp ffstd::StdFree
            0x14003DB30ull, // fox FindKnownProject
            0x140F649F3ull, // develop array record displacement site
            0x140F649FAull, // develop array record displacement site
            0x140F64A01ull, // develop array record displacement site
            0x140F64A23ull, // develop array record displacement site
            0x140F64A2Aull, // develop array record displacement site
            0x140F64A31ull, // develop array record displacement site
            0x140F64A38ull, // develop array record displacement site
            0x140F64A3Full, // develop array record displacement site
            0x140F64A46ull, // develop array record displacement site
            0x140F64A4Dull, // develop array record displacement site
            0x140F64A54ull, // develop array record displacement site
            0x140F64A5Bull, // develop array record displacement site
            0x140F64A62ull, // develop array record displacement site
            0x140F64A69ull, // develop array record displacement site
            0x140F64A70ull, // develop array record displacement site
            0x140F64A77ull, // develop array record displacement site
            0x140F64A7Eull, // develop array record displacement site
            0x140F64A85ull, // develop array record displacement site
            0x140F64A8Cull, // develop array record displacement site
            0x140F64A94ull, // develop array record displacement site
            0x140F64A9Bull, // develop array record displacement site
            0x140F64AAAull, // develop array record displacement site
            0x140F64AB6ull, // develop array record displacement site
            0x140F64ACCull, // develop array record displacement site
            0x140F64AD3ull, // develop array record displacement site
            0x140F64ADAull, // develop array record displacement site
            0x140F64AE1ull, // develop array record displacement site
            0x140F64AE8ull, // develop array record displacement site
            0x140F64AEFull, // develop array record displacement site
            0x140F64AF6ull, // develop array record displacement site
            0x140F64AFDull, // develop array record displacement site
            0x140F64B04ull, // develop array record displacement site
            0x140F64B0Bull, // develop array record displacement site
            0x140F64B11ull, // develop array record displacement site
            0x140F64B22ull, // develop array record displacement site
            0x140F64B29ull, // develop array record displacement site
            0x140F64B30ull, // develop array record displacement site
            0x140F64B37ull, // develop array record displacement site
            0x140F64B3Eull, // develop array record displacement site
            0x140F64B44ull, // develop array record displacement site
            0x140F64B4Bull, // develop array record displacement site
            0x140F64B71ull, // develop array record displacement site
            0x140F64BDBull, // develop array record displacement site
            0x140F64C52ull, // develop array record displacement site
            0x140F64CF2ull, // develop array record displacement site
            0x140F64E70ull, // develop array record displacement site
            0x140F64E80ull, // develop array record displacement site
            0x140F64ECCull, // develop array record displacement site
            0x140F64EE1ull, // develop array record displacement site
            0x140F64F20ull, // develop array record displacement site
            0x140F64F40ull, // develop array record displacement site
            0x140F64F50ull, // develop array record displacement site
            0x140F64F57ull, // develop array record displacement site
            0x140F64F73ull, // develop array record displacement site
            0x140F64F86ull, // develop array record displacement site
            0x140F64F9Full, // develop array record displacement site
            0x140F64FA6ull, // develop array record displacement site
            0x140F64FC4ull, // develop array record displacement site
            0x140F6508Cull, // develop array record displacement site
            0x140F6538Eull, // develop array record displacement site
            0x140F65405ull, // develop array record displacement site
            0x140F654E7ull, // develop array record displacement site
            0x140F6557Aull, // develop array record displacement site
            0x140F656D5ull, // develop array record displacement site
            0x140F65771ull, // develop array record displacement site
            0x140F657E3ull, // develop array record displacement site
            0x140F65895ull, // develop array record displacement site
            0x140F65935ull, // develop array record displacement site
            0x140F659E2ull, // develop array record displacement site
            0x140F65A41ull, // develop array record displacement site
            0x140F66220ull, // develop array record displacement site
            0x140F66260ull, // develop array record displacement site
            0x140F662F8ull, // develop array record displacement site
            0x140F66313ull, // develop array record displacement site
            0x140F66E32ull, // develop array record displacement site
            0x140F66E73ull, // develop array record displacement site
            0x140F66ED6ull, // develop array record displacement site
            0x140F66F9Full, // develop array record displacement site
            0x140F67210ull, // develop array record displacement site
            0x140F67245ull, // develop array record displacement site
            0x140F67273ull, // develop array record displacement site
            0x140F672ADull, // develop array record displacement site
            0x140F6751Aull, // develop array record displacement site
            0x140F67540ull, // develop array record displacement site
            0x140F6756Cull, // develop array record displacement site
            0x140F675B6ull, // develop array record displacement site
            0x140F675C6ull, // develop array record displacement site
            0x140F675D6ull, // develop array record displacement site
            0x140F675E6ull, // develop array record displacement site
            0x140F675F6ull, // develop array record displacement site
            0x140F67606ull, // develop array record displacement site
            0x140F6764Cull, // develop array record displacement site
            0x140F6765Cull, // develop array record displacement site
            0x140F6766Cull, // develop array record displacement site
            0x140F6767Cull, // develop array record displacement site
            0x140F6768Cull, // develop array record displacement site
            0x140F6769Cull, // develop array record displacement site
            0x140F676D5ull, // develop array record displacement site
            0x140F6771Aull, // develop array record displacement site
            0x140F67750ull, // develop array record displacement site
            0x140F67793ull, // develop array record displacement site
            0x140F677ECull, // develop array record displacement site
            0x140F67DD0ull, // develop array record displacement site
            0x140F67DE9ull, // develop array record displacement site
            0x140F680A0ull, // develop array record displacement site
            0x140F680F1ull, // develop array record displacement site
            0x140F68107ull, // develop array record displacement site
            0x140F6810Dull, // develop array record displacement site
            0x140F68117ull, // develop array record displacement site
            0x140F68243ull, // develop array record displacement site
            0x140F68256ull, // develop array record displacement site
            0x140F6829Cull, // develop array record displacement site
            0x140F68327ull, // develop array record displacement site
            0x140F68387ull, // develop array record displacement site
            0x140F683DFull, // develop array record displacement site
            0x140F68433ull, // develop array record displacement site
            0x140F68466ull, // develop array record displacement site
            0x140F684B0ull, // develop array record displacement site
            0x140F68530ull, // develop array record displacement site
            0x140F6864Bull, // develop array record displacement site
            0x140F6870Bull, // develop array record displacement site
            0x140F68898ull, // develop array record displacement site
            0x140F68980ull, // develop array record displacement site
            0x140F6899Dull, // develop array record displacement site
            0x140F689BAull, // develop array record displacement site
            0x140F689D7ull, // develop array record displacement site
            0x140F689F4ull, // develop array record displacement site
            0x140F68A11ull, // develop array record displacement site
            0x140F68A2Eull, // develop array record displacement site
            0x140F68A4Bull, // develop array record displacement site
            0x140F68ECCull, // develop array record displacement site
            0x140F68F2Cull, // develop array record displacement site
            0x140F68F78ull, // develop array record displacement site
            0x140F68F88ull, // develop array record displacement site
            0x140F68F98ull, // develop array record displacement site
            0x140F68FA8ull, // develop array record displacement site
            0x140F68FB8ull, // develop array record displacement site
            0x140F68FC8ull, // develop array record displacement site
            0x140F6901Aull, // develop array record displacement site
            0x140F6902Aull, // develop array record displacement site
            0x140F6903Aull, // develop array record displacement site
            0x140F6904Aull, // develop array record displacement site
            0x140F6905Aull, // develop array record displacement site
            0x140F6906Aull, // develop array record displacement site
            0x140F690AFull, // develop array record displacement site
            0x140F690FEull, // develop array record displacement site
            0x140F6913Bull, // develop array record displacement site
            0x140F69184ull, // develop array record displacement site
            0x140F691D8ull, // develop array record displacement site
            0x140F693A0ull, // develop array record displacement site
            0x140F695ACull, // develop array record displacement site
            0x140F695E8ull, // develop array record displacement site
            0x140F69629ull, // develop array record displacement site
            0x140F69657ull, // develop array record displacement site
            0x140F69703ull, // develop array record displacement site
            0x140F69749ull, // develop array record displacement site
            0x140F69785ull, // develop array record displacement site
            0x140F697B9ull, // develop array record displacement site
            0x140F69E82ull, // develop array record displacement site
            0x140F6A5C9ull, // develop array record displacement site
            0x140F6A65Cull, // develop array record displacement site
            0x140F6A6F2ull, // develop array record displacement site
            0x140F6A790ull, // develop array record displacement site
            0x140F6A90Dull, // develop array record displacement site
            0x140F6ACAEull, // develop array record displacement site
            0x140F6ACD5ull, // develop array record displacement site
            0x140F6AD8Eull, // develop array record displacement site
            0x140F6AE00ull, // develop array record displacement site
            0x140F6AE30ull, // develop array record displacement site
            0x140F6AE90ull, // develop array record displacement site
            0x140F6BE2Eull, // develop array record displacement site
            0x140F6BE76ull, // develop array record displacement site
            0x140F6BEA9ull, // develop array record displacement site
            0x140F6BF4Eull, // develop array record displacement site
            0x140F6BF93ull, // develop array record displacement site
            0x140F6BFC7ull, // develop array record displacement site
            0x140F6C269ull, // develop array record displacement site
            0x140F6C2CDull, // develop array record displacement site
            0x140F6C307ull, // develop array record displacement site
            0x140F6C32Dull, // develop array record displacement site
            0x140F6C3C4ull, // develop array record displacement site
            0x140F6C3FBull, // develop array record displacement site
            0x140F6C422ull, // develop array record displacement site
            0x140F6C7D0ull, // develop array record displacement site
            0x140F6C815ull, // develop array record displacement site
            0x140F6C8C9ull, // develop array record displacement site
            0x140F6C8D6ull, // develop array record displacement site
            0x140F6C923ull, // develop array record displacement site
            0x140F6C987ull, // develop array record displacement site
            0x140F6CB13ull, // develop array record displacement site
            0x140F6CB39ull, // develop array record displacement site
            0x140F6CB79ull, // develop array record displacement site
            0x140F6CB97ull, // develop array record displacement site
            0x140F6CC20ull, // develop array record displacement site
            0x140F6CC91ull, // develop array record displacement site
            0x140F6CDC5ull, // develop array record displacement site
            0x140F6CDEBull, // develop array record displacement site
            0x140F6CE10ull, // develop array record displacement site
            0x140F6CE30ull, // develop array record displacement site
            0x140F6CE50ull, // develop array record displacement site
            0x140F6CE80ull, // develop array record displacement site
            0x140F6CF5Aull, // develop array record displacement site
            0x140F6CFAAull, // develop array record displacement site
            0x140F6D011ull, // develop array record displacement site
            0x140F6D052ull, // develop array record displacement site
            0x140F6D190ull, // develop array record displacement site
            0x140F6D211ull, // develop array record displacement site
            0x140F6D27Aull, // develop array record displacement site
            0x140F6D2E3ull, // develop array record displacement site
            0x140F6D411ull, // develop array record displacement site
            0x140F6D4B2ull, // develop array record displacement site
            0x140F6D516ull, // develop array record displacement site
            0x140F6D540ull, // develop array record displacement site
            0x140F6D643ull, // develop array record displacement site
            0x140F6D898ull, // develop array record displacement site
            0x140F6DA20ull, // develop array record displacement site
            0x140F6DAE0ull, // develop array record displacement site
            0x140F6DB52ull, // develop array record displacement site
            0x140F6DB5Bull, // develop array record displacement site
            0x140F6DBEDull, // develop array record displacement site
            0x140F6DCEFull, // develop array record displacement site
            0x140F6DF58ull, // develop array record displacement site
            0x140F6E0DCull, // develop array record displacement site
            0x140F6E142ull, // develop array record displacement site
            0x140F6E14Aull, // develop array record displacement site
            0x140F6E1C0ull, // develop array record displacement site
            0x140F6E1D0ull, // develop array record displacement site
            0x140F6E5A0ull, // develop array record displacement site
            0x140F6E66Full, // develop array record displacement site
            0x140F6E6BBull, // develop array record displacement site
            0x140F6E6E0ull, // develop array record displacement site
            0x140F6E6FCull, // develop array record displacement site
            0x140F6E71Bull, // develop array record displacement site
            0x140F6E730ull, // develop array record displacement site
            0x140F6E758ull, // develop array record displacement site
            0x140F6E767ull, // develop array record displacement site
            0x140F6E780ull, // develop array record displacement site
            0x140F6E7B7ull, // develop array record displacement site
            0x140F6E7F8ull, // develop array record displacement site
            0x140F6E808ull, // develop array record displacement site
            0x140F6E820ull, // develop array record displacement site
            0x140F6E827ull, // develop array record displacement site
            0x140F6E82Eull, // develop array record displacement site
            0x140F6E835ull, // develop array record displacement site
            0x140F6E83Cull, // develop array record displacement site
            0x140F6E843ull, // develop array record displacement site
            0x140F6E84Bull, // develop array record displacement site
            0x140F6E874ull, // develop array record displacement site
            0x140F6E8A9ull, // develop array record displacement site
            0x140F6E8D8ull, // develop array record displacement site
            0x140F6E8E7ull, // develop array record displacement site
            0x140F6E9AFull, // develop array record displacement site
            0x140F6EAF3ull, // develop array record displacement site
            0x140F6EB12ull, // develop array record displacement site
            0x140F6EB40ull, // develop array record displacement site
            0x140F6EE90ull, // develop array record displacement site
            0x140F6EE9Aull, // develop array record displacement site
            0x140F6EEABull, // develop array record displacement site
            0x140F6EEBAull, // develop array record displacement site
            0x140F6EEC2ull, // develop array record displacement site
            0x140F6EEC9ull, // develop array record displacement site
            0x140F6EED7ull, // develop array record displacement site
            0x140F6EEE6ull, // develop array record displacement site
            0x140F6EEEEull, // develop array record displacement site
            0x140F6EEF5ull, // develop array record displacement site
            0x140F6EF03ull, // develop array record displacement site
            0x140F6EF12ull, // develop array record displacement site
            0x140F6EF1Aull, // develop array record displacement site
            0x140F6EF21ull, // develop array record displacement site
            0x140F6EF2Full, // develop array record displacement site
            0x140F6EF3Eull, // develop array record displacement site
            0x140F6EF46ull, // develop array record displacement site
            0x140F6EF4Dull, // develop array record displacement site
            0x140F6EF5Bull, // develop array record displacement site
            0x140F6EF6Aull, // develop array record displacement site
            0x140F6EF72ull, // develop array record displacement site
            0x140F6EF79ull, // develop array record displacement site
            0x140F6EF87ull, // develop array record displacement site
            0x140F6EF96ull, // develop array record displacement site
            0x140F6EF9Eull, // develop array record displacement site
            0x140F6EFA5ull, // develop array record displacement site
            0x140F6EFB3ull, // develop array record displacement site
            0x140F6EFC2ull, // develop array record displacement site
            0x140F6EFCAull, // develop array record displacement site
            0x140F6EFD1ull, // develop array record displacement site
            0x140F6EFDFull, // develop array record displacement site
            0x140F6EFEEull, // develop array record displacement site
            0x140F6EFF6ull, // develop array record displacement site
            0x140F6EFFDull, // develop array record displacement site
            0x140F6F015ull, // develop array record displacement site
            0x140F6F067ull, // develop array record displacement site
            0x140F6F106ull, // develop array record displacement site
            0x140F6F182ull, // develop array record displacement site
            0x140F6F22Eull, // develop array record displacement site
            0x140F6F6D1ull, // develop array record displacement site
            0x140F6F720ull, // develop array record displacement site
            0x140F6F72Eull, // develop array record displacement site
            0x140F6F73Cull, // develop array record displacement site
            0x140F6F74Aull, // develop array record displacement site
            0x140F6F758ull, // develop array record displacement site
            0x140F6F763ull, // develop array record displacement site
            0x140F6F771ull, // develop array record displacement site
            0x140F6F78Aull, // develop array record displacement site
            0x140F6F799ull, // develop array record displacement site
            0x140F6F7A9ull, // develop array record displacement site
            0x140F6F7B8ull, // develop array record displacement site
            0x140F6F7D0ull, // develop array record displacement site
            0x140F6F7D7ull, // develop array record displacement site
            0x140F6F7ECull, // develop array record displacement site
            0x140F6F85Eull, // develop array record displacement site
            0x140F6F86Eull, // develop array record displacement site
            0x140F6F884ull, // develop array record displacement site
            0x140F6F8B8ull, // develop array record displacement site
            0x140F6F8DDull, // develop array record displacement site
            0x140F6F8E9ull, // develop array record displacement site
            0x140F6F8F0ull, // develop array record displacement site
            0x140F6F90Cull, // develop array record displacement site
            0x140F6F922ull, // develop array record displacement site
            0x140F6F93Bull, // develop array record displacement site
            0x140F6F942ull, // develop array record displacement site
            0x140F6F96Bull, // develop array record displacement site
            0x140F6FA1Full, // develop array record displacement site
            0x140F6FAC6ull, // develop array record displacement site
            0x140F6FAD6ull, // develop array record displacement site
            0x140F6FAFDull, // develop array record displacement site
            0x140F6FB20ull, // develop array record displacement site
            0x140F6FB30ull, // develop array record displacement site
            0x140F6FB37ull, // develop array record displacement site
            0x140F6FB53ull, // develop array record displacement site
            0x140F6FB66ull, // develop array record displacement site
            0x140F6FB7Full, // develop array record displacement site
            0x140F6FB86ull, // develop array record displacement site
            0x140F6FBC0ull, // develop array record displacement site
            0x140F6FC09ull, // develop array record displacement site
            0x140F6FC4Aull, // develop array record displacement site
            0x140F6FC79ull, // develop array record displacement site
            0x140F6FC8Eull, // develop array record displacement site
            0x140F6FC95ull, // develop array record displacement site
            0x140F6FC9Dull, // develop array record displacement site
            0x140F6FCA4ull, // develop array record displacement site
            0x140F6FCACull, // develop array record displacement site
            0x140F6FCFCull, // develop array record displacement site
            0x140F6FD7Aull, // develop array record displacement site
            0x140F6FDABull, // develop array record displacement site
            0x140F6FDBDull, // develop array record displacement site
            0x140F6FDDDull, // develop array record displacement site
            0x140F6FDECull, // develop array record displacement site
            0x140F6FE13ull, // develop array record displacement site
            0x140F6FE25ull, // develop array record displacement site
            0x140F6FE40ull, // develop array record displacement site
            0x140F6FE4Full, // develop array record displacement site
            0x140F6FF96ull, // develop array record displacement site
            0x140F70155ull, // develop array record displacement site
            0x140F7016Full, // develop array record displacement site
            0x140F70176ull, // develop array record displacement site
            0x140F70195ull, // develop array record displacement site
            0x140F701B3ull, // develop array record displacement site
            0x140F701DBull, // develop array record displacement site
            0x140F701EEull, // develop array record displacement site
            0x140F701F5ull, // develop array record displacement site
            0x14801E985ull, // develop array record displacement site
            0x14801E9A8ull, // develop array record displacement site
            0x14801E9C7ull, // develop array record displacement site
            0x140F64E1Dull, // develop array row bound site
            0x140F6534Bull, // develop array row bound site
            0x140F65630ull, // develop array row bound site
            0x140F6580Bull, // develop array row bound site
            0x140F65869ull, // develop array row bound site
            0x140F65909ull, // develop array row bound site
            0x140F659ADull, // develop array row bound site
            0x140F65CE0ull, // develop array row bound site
            0x140F6699Aull, // develop array row bound site
            0x140F66E01ull, // develop array row bound site
            0x140F66FECull, // develop array row bound site
            0x140F6706Aull, // develop array row bound site
            0x140F67130ull, // develop array row bound site
            0x140F67325ull, // develop array row bound site
            0x140F674DEull, // develop array row bound site
            0x140F6786Aull, // develop array row bound site
            0x140F6798Bull, // develop array row bound site
            0x140F67E3Aull, // develop array row bound site
            0x140F680B0ull, // develop array row bound site
            0x140F68287ull, // develop array row bound site
            0x140F682FAull, // develop array row bound site
            0x140F68377ull, // develop array row bound site
            0x140F683C2ull, // develop array row bound site
            0x140F6841Dull, // develop array row bound site
            0x140F6853Eull, // develop array row bound site
            0x140F685AAull, // develop array row bound site
            0x140F686E3ull, // develop array row bound site
            0x140F688B7ull, // develop array row bound site
            0x140F68E7Eull, // develop array row bound site
            0x140F69262ull, // develop array row bound site
            0x140F6937Full, // develop array row bound site
            0x140F6952Dull, // develop array row bound site
            0x140F69D1Bull, // develop array row bound site
            0x140F69EF4ull, // develop array row bound site
            0x140F6A2CAull, // develop array row bound site
            0x140F6A5A2ull, // develop array row bound site
            0x140F6A68Aull, // develop array row bound site
            0x140F6A6CAull, // develop array row bound site
            0x140F6A7AAull, // develop array row bound site
            0x140F6BDB5ull, // develop array row bound site
            0x140F6C025ull, // develop array row bound site
            0x140F6C245ull, // develop array row bound site
            0x140F6C475ull, // develop array row bound site
            0x140F6C625ull, // develop array row bound site
            0x140F6C7FCull, // develop array row bound site
            0x140F6C8F3ull, // develop array row bound site
            0x140F6C958ull, // develop array row bound site
            0x140F6C9F3ull, // develop array row bound site
            0x140F6CC50ull, // develop array row bound site
            0x140F6CE73ull, // develop array row bound site
            0x140F6DDF5ull, // develop array row bound site
            0x140F6DEAFull, // develop array row bound site
            0x140F6E561ull, // develop array row bound site
            0x140F6E79Aull, // develop array row bound site
            0x140F6EB79ull, // develop array row bound site
            0x140F6EBC7ull, // develop array row bound site
            0x140F6EC2Eull, // develop array row bound site
            0x140F6EC5Full, // develop array row bound site
            0x140F6ECD4ull, // develop array row bound site
            0x140F6ED1Bull, // develop array row bound site
            0x140F6ED7Full, // develop array row bound site
            0x140F6EE00ull, // develop array row bound site
            0x140F6F6B2ull, // develop array row bound site
            0x140F71062ull, // develop array row bound site
            0x140FE52C5ull, // develop array sweep allowed site
            0x140FFE958ull, // develop array sweep allowed site
            0x14127A085ull, // develop array sweep allowed site
            0x14127A3AEull, // develop array sweep allowed site
            0x141311077ull, // develop array sweep allowed site
            0x1413110E5ull, // develop array sweep allowed site
            0x141A58225ull, // develop array sweep allowed site
            0x1430D9F20ull, // develop array sweep allowed site
            0x1430E52CCull, // develop array sweep allowed site
            0x149765F03ull, // develop array sweep allowed site
            0x149D8C125ull, // develop array sweep allowed site
            0x149D8EB35ull, // develop array sweep allowed site
            0x149D93671ull, // develop array sweep allowed site
            0x149DAC5B5ull, // develop array sweep allowed site
            0x149DB4C35ull, // develop array sweep allowed site
            0x149DBA015ull, // develop array sweep allowed site
            0x149DC7451ull, // develop array sweep allowed site
            0x149DD4BF5ull, // develop array sweep allowed site
            0x149DDE595ull, // develop array sweep allowed site
            0x149DDFCDDull, // develop array sweep allowed site
            0x149DE47B9ull, // develop array sweep allowed site
            0x149DF1899ull, // develop array sweep allowed site
            0x149E03FE1ull, // develop array sweep allowed site
            0x149E0CCF5ull, // develop array sweep allowed site
            0x149E99AB5ull, // develop array sweep allowed site
            0x142687585ull, // develop array sweep allowed site
            0x142687991ull, // develop array sweep allowed site
            0x1426B4BD1ull, // develop array sweep allowed site
            0x142747B41ull, // develop array sweep allowed site
            0x142748535ull, // develop array sweep allowed site
            0x14308C2A6ull, // develop array sweep allowed site
            0x1430FF1BEull, // develop array sweep allowed site
            0x1430FF3BAull, // develop array sweep allowed site
            0x14310E366ull, // develop array sweep allowed site
            0x143111202ull, // develop array sweep allowed site
            0x143117F0Aull, // develop array sweep allowed site
            0x140F650E4ull, // develop array patch: block alloc size
            0x140F64968ull, // develop array patch: ctor loop count
            0x140F649CFull, // develop array patch: ctor parallel base
            0x140F6C88Bull, // develop array patch: reset records size
            0x140F6C89Aull, // develop array patch: reset parallel base
            0x140F6C8A3ull, // develop array patch: reset parallel size
            0x140F6E165ull, // develop array patch: regist scan bound
            0x140F6E177ull, // develop array patch: regist probe disp
            0x140F6E195ull, // develop array patch: regist store disp+0
            0x140F6E1A0ull, // develop array patch: regist store disp+4
            0x140F6E1ABull, // develop array patch: regist store disp+8
            0x140F6E1B6ull, // develop array patch: regist store disp+C
            0x140F692A0ull, // develop array patch: equipid-from-idx bound
            0x140F6D1BAull, // develop array patch: is-visible bound
            0x140F68E50ull, // develop array patch: grade getter bound
            0x140F68D10ull, // develop array patch: packed getter bound
            0x140F68CD5ull, // develop array patch: count-by-type scan bound
            0x140F6AAC2ull, // develop array patch: provider count bound
            0x140F6AA1Bull, // develop array patch: provider fill bound
            0x140F6CD64ull, // develop array patch: menu list gate bound
            0x140F64B51ull, // develop array patch: equip-equiped bits alloc words
            0x140F64B7Aull, // develop array patch: equip-equiped bits clear size
            0x140F6943Aull, // develop array byte fix: sibling retire loop index init
            0x140F69460ull, // develop array byte fix: sibling retire loop index read
            0x140F694ACull, // develop array byte fix: sibling retire loop index inc
            0x140F694AFull, // develop array byte fix: sibling retire loop index test
            0x140F6ADBFull, // develop array byte fix: develop-list count miss branch
            0x140F6FFD9ull, // develop array byte fix: develop-list apply miss branch
            0x140F6C880ull, // develop array blockReset
            0x140F68D60ull, // develop array findByEquipId
            0x140F65990ull, // develop array listDevCount
            0x140F65840ull, // develop array listDevFill
            0x140F6B080ull, // develop array equipIdCount
            0x140F6B490ull, // develop array equipIdRow
            0x140F67060ull, // develop array getDerivationDevelopIndex
            0x140F6A6C0ull, // develop array siblingCount
            0x14239B770ull, // develop array blockVtblA
            0x14239BFE0ull, // develop array blockVtblB
            0x14239B840ull, // develop array base20VtblA
            0x14239C0B0ull, // develop array base20VtblB
            0x140F64C20ull, // develop array edcDtor
            0x140E6EA30ull, // tpp::gm::horse::impl::DamageActionImpl::State_Whiii
            0x141280400ull, // tpp::gm::player::impl::SightManagerImpl::UpdateAutoMarkingToMecha
            0x1409C5660ull, // tpp::gm::player::impl::Player2Impl::IsPlayingVoice
            0x141236980ull, // RideHorseActionPluginImpl_State_Idle
            0x140E61BE0ull, // tpp::gm::horse::impl::BasicAction2Impl::State_RideOnIdle
            0x140D6FD40ull, // tpp::gm::impl::cp::EvasionAiImpl::DecrementPhaseCounter
            0x140D6B790ull, // FUN_140d6b7e0
            0x14239AA70ull, // SupportHeli_PartsTable
            0x140F69BF0ull, // EquipDevelopControllerImpl_GetHeliPartsId
            0x140F69BA0ull, // EquipDevelopControllerImpl_FindHeliPartsRow
            0x14195C490ull, // CustomizeSelectorCallbackImpl_DecideActWindowPartHelicopter
            0x142A78060ull, // SupportHeli_CustomizeCodec
            0x1422C5FC0ull, // SupportHeli_CustomizeCodecVtable
            0x140DEEF50ull, // CustomizeControllerImpl_SetSubWeaponAmmo
            0x140DEF120ull, // CustomizeControllerImpl_SetSubWeaponPods
            0x140E0EA50ull, // LauncherActionImpl_Execute
            0x140E0FCA0ull, // LauncherActionImpl_Wake
            0x140E29140ull, // SupportCombatAiImpl_StepRocketAim
            0x140E286D0ull, // SupportCombatAiImpl_StepMissile
            0x141337780ull, // ShellSystemImpl_Fire
            0x140DD5750ull, // TppHeli2_LuaCommands
            0x140DC1050ull, // EquipSystemImpl_RegisterEquip
            0x140DC6740ull, // EquipSystemImpl_UnregisterEquip
            0x140E06820ull, // GunActionImpl_FireBarrel
            0x14195A070ull, // CustomizeSelectorCallbackImpl_HoverHeliPart
            0x1416684A0ull, // EquipDetailsCallbackImpl_SetupHeliCustomizeDetails
            0x140A0FC70ull, // BlastSystemImpl_Blast
            0x141330E10ull, // ShellImpl_UpdateAction
            0x14132C890ull, // ShellImpl_RequestSmokeCloud
            0x1407E6790ull, // GetFobStatusCallback_AnnounceDefenseWon
            0x1407E66A0ull, // GetFobStatusCallback_AnnounceDefenseLost
            0x14158A470ull, // Soldier2SoundControllerImpl_CallVoiceImpl
            0x140ECD5B0ull, // HostageRideVehicleActionImpl_State_GetInAction
            0x140ECDAF0ull, // HostageRideVehicleActionImpl_State_GetOutAction
            0x140F33840ull, // MbDvcMissionListRecordCallFunc_Start
            0x140F0B7B0ull, // MbDvcMapCallbackBaseImpl_UpdateLocationInfoView
            0x140F40DB0ull, // FUN_140F40D70 called by MbDvcSideOpsRecordCallFunc_Start
            0x140F33120ull, // FUN_140F330E0 called by MbDvcMissionListCallbackImpl prefab setup
            0x140F31AB0ull, // MbDvcMissionListCallbackImpl_ActivatePrefabTablistParameter
            0x140F347DAull, // MbDvcMissionListCallbackImpl_Update tab type read
            0x140F3557Aull, // MbDvcMissionListCallbackImpl_UpdateMissionListParameters tab filter
            0x1409159F0ull, // tpp::ui::utility::IsDirButtonPress
            0x140915960ull, // tpp::ui::utility::IsDirButtonLong
            0x14090FFF0ull, // tpp::ui::utility::CheckIllegalDirButtonInput
            0x14085FCD0ull, // tpp::ui::menu::UiCommonDataManager::GetEnableDirButton
            0x142A10750ull, // PrefabSystemImpl layout name per prefab type, 20 x u64
            0x14227EE10ull, // MotherBaseDeviceTopMenuTableImpl vtable, 49 slots
            0x140966260ull, // MotherBaseDeviceTopMenuTableImpl factory
            0x140915F40ull, // tpp::ui::utility::IsInFreePlayHeliMission
            0x141A20B10ull, // mov rax,[rcx+0x70]; ret (JP: folded twin)
            0x1408767C0ull, // tpp::ui::menu::MbDvcManageEv Update
            0x1409171C0ull, // tpp::ui::utility::SendTriggerToParent
            0x14086EE00ull, // tpp::ui::menu::MbTopMenuSelectorEv::Update
            0x14086D410ull, // MbTopMenuSelectorEv_GetItemIcon
            0x1405113E0ull, // fox::uix::impl::PrefabSystemImpl::GetPrefabLayout
            0x140510F80ull, // PrefabSystemImpl vtable +0x100
            0x140510FB0ull, // PrefabSystemImpl vtable +0x108
            0x140205762ull, // VertexStreamPool_CreateSite
            0x142B77F88ull, // VertexStreamPool_Instance
            0x140205613ull, // ModelManagerPool_CreateSite
            0x142B743A0ull, // ModelManagerPool_Instance
            0x1402056DEull, // VertexBufferPool_CreateSite
            0x142B74A80ull, // VertexBufferPool_Instance
            0x140205720ull, // IndexBufferPool_CreateSite
            0x142B74A88ull, // IndexBufferPool_Instance
            0x14146CDE0ull, // tpp::gm::soldier::impl::DownActionImpl::State_ToDyingSufferIdle
            0x14146CB70ull, // tpp::gm::soldier::impl::DownActionImpl::State_DyingSufferToDyingSitup
            0x14144B050ull, // SoldierDamageActionImpl_State_DamageReaction
            0x14121C650ull, // tpp::gm::player::impl::RideHeliActionPluginImpl::ExecActionButtonFunction
            0x14121E520ull, // tpp::gm::player::impl::RideHeliActionPluginImpl::IsForcedOffGun
            0x14095C670ull, // tpp::ui::menu::impl::MissionPreparationSystemImpl::StartPopup
            0x14095D1F0ull, // MissionPreparationSystemImpl_GetPopupResult
            0x141A10EE0ull, // lua_error
            0x141A20900ull, // luaG_errormsg
            0x140C4FD80ull, // tpp::ui::hud::impl::TppBonusPopupImpl::SetBgTexture
            0x141BB5E90ull, // FoxGeo_CheckSphere
            0x142C79640ull, // FoxGeo_WorldTable
            0x140928950ull, // MbDvcSoundControllerImpl_RequestVoice
            0x140958180ull, // MissionPreparationSystemImpl_IsFobSortie
            0x141DBAAF0ull, // fox::ui::ButtonMapper::SetDefaultButtonMap
            0x142C90800ull, // ButtonMapper_Map
            0x141DD5CC0ull, // FontManager_GetIconTextureCoord
            0x14092CC90ull, // tpp::ui::menu::UiDepend::ActHudChecking
            0x141DA6F90ull, // fox::ui::Daemon::GetDaemonPrefetchController
            0x141DBB7D0ull, // fox::ui::PrefetchTextureController::CreatePrefetchTexture
            0x141DBBE30ull, // fox::ui::PrefetchTextureController::GetPrefetchTexture
            0x140917840ull, // Ui_SetFontIconTexture
            0x141694300ull, // FobMission2CallbackImpl_Update
            0x141DBA780ull, // fox::ui::ButtonMapper::GetKeyCodeEqual
            0x1409BF1A0ull, // Player2Impl_CallVoiceWithDialogueEvent
            0x14003DB90ull, // Fox_PathCodeFromString
            0x140B16D00ull, // RealizedSoldier2Impl_ExecuteSerially
            0x140B18D60ull, // RealizedSoldier2Impl_StopAllFovaResourceManager
            0x14021E50Full, // TextureStoragePool_SizeSite
            0x142B79B18ull, // TextureStoragePool_Streamer
            0x1402A72F0ull, // TextureStoragePool_AllocBlock
            0x140994BF0ull, // MessageArgs_GetValueAsDouble
            0x140ADA170ull, // ScriptUpdater_ReceiveMessage
            0x1406EF1F0ull, // TppSubViewport create
            0x1401BDF10ull, // fox::gr::Render::GetDefault
            0x1401BD830ull, // fox::gr::Render::AddViewport
            0x1401BDBC0ull, // fox::gr::Render::DeleteViewport
            0x1401BBB00ull, // fox::gr::Viewport::SetScene
            0x1401BB780ull, // fox::gr::Viewport::SetExposure
            0x1401B9A20ull, // fox::gr::GrViewport::UpdateFromCamera
            0x140277490ull, // fox::gr::Material::BindTexture
            0x1408B49D0ull, // tpp::ui::hud::EquipCrossEvCall::SpecialItemAct
            0x1408ABBB0ull, // EquipCrossEvCall cursor to panel index
            0x140A29C90ull, // TppEquip.AddToEquipIdTable (vanilla Lua function)
            0x140FC2D20ull, // tpp::gm::impl::placed::PlacedImpl::ExecuteAfter
            0x140FC3570ull, // tpp::gm::impl::placed::PlacedImpl::SendOnActivatePlacedMessage
            0x14134DD97ull, // Soldier2InterrogateUtil::CheckMine, placed id load before the 0x19D mine compare
            0x140C7FF1Cull, // buddydog mine sniffing, placed id load before the 0x19D mine compare
            0x140A2A180ull, // tpp::gm::impl::equip::EquipIdTableImpl::GetEquipIdFromStrCode32Impl
            0x1414FF7F0ull, // NoticeDownComradeAiImpl::StepRadio
            0x1413BD97Full, // Soldier2CombatUtil::GrenadeUpdate, throw command equipId store
            0x1413F55F9ull, // CombatInDoorImpl::StepGrenade, throw command equipId store
            0x1413FBBADull, // CombatInDoorImpl::ThrowGrenadeInDoor, throw command equipId store
            0x140A45F83ull, // GetBuddyGameObjectType
            0x140A480C4ull, // ProcSignal
            0x140A46840ull, // GetPathAtSub
            0x140A497E0ull, // SubBlockSetting
            0x140A513F0ull, // InitBuddyCommand
            0x140A514D0ull, // InitBuddyCommandForStatus
            0x140876DC3ull, // UpdateCallBuddyPhaseOpen
            0x140B95D70ull, // AdjustLeg
            0x1415B4CA0ull, // VehicleNoticeDownComradeAiImpl::StepRadio
            0x1413BDB00ull, // tpp::gm::soldier::impl::Soldier2Util::HasOtherSoldierInPoint
            0x14056BBC0ull, // TppPermanentGimmickGameObject<TppPermanentGimmickDoorUnit>::GetUnit64 (thunk)
            0x142BD80C0ull, // DoorGameObject s_gameObjectHandle (global, door game object at +0x20)
            0x140600F20ull, // tpp::gk::TppPermanentGimmickDoorUnit::GetDoorCenter
            0x140A4E670ull, // CanSortieBuddyType
            0x140A4E6B0ull, // CanSortieBuddyType
            0x1400D8150ull, // fox::LuaPushVector3(lua_State*, Vector3 const&)
            0x14163E450ull, // SetupBuddyList
            0x140D73C70ull, // MemberRoleControllerImpl::AssignSelectAlertLost
            0x140D63500ull, // CommandPost2Impl::SetMemberRole_
            0x141310F30ull, // SecurityCamera2Impl::ProcessLuaCommand
            0x14193A040ull, // UavImpl::ProcessLuaCommand
            0x140B30560ull, // vehicle::TppVehicle2_LuaCommands
            0x141356180ull, // Soldier2Impl::ProcessLuaCommand
            0x140E85E30ull, // Hostage2Impl::ProcessLuaCommand
            0x1414975C0ull, // InvestigateAiImpl::PreUpdate
            0x141706170ull, // WalkerGear2Impl::ProcessLuaCommand
            0x1418BB720ull, // enemy::sahelan::TppSahelan2_LuaCommands
            0x1415D7450ull, // tpp::gm::impl::throwing::ThrowingImpl::OnOffenseHit
            0x1410390A0ull, // tpp::gm::player::impl::attack::AttackActionImpl::CalcThrowingVelocity
            0x140600910ull, // tpp::gk::DoorDefenceCallback::Exec
            0x14060EE80ull, // tpp::gk::GatlingGunDefenseCallback::Exec
            0x140616ED0ull, // tpp::gk::ImportantBreakableDefenseCallback::Exec
            0x14061C7B0ull, // tpp::gk::MachineGunDefenseCallback::Exec
            0x140625190ull, // tpp::gk::MortarDefenseCallback::Exec
            0x14062A540ull, // tpp::gk::SearchLightDefenseCallback::Exec
            0x14062DE90ull, // tpp::gk::WatchTowerDefenseCallback::Exec
            0x140630D20ull, // tpp::gk::IrSensorDefCallback::Exec
            0x140636920ull, // tpp::gk::EventAnimationDefenseCallback::Exec
            0x140639520ull, // tpp::gk::BridgeDefenseCallback::Exec
            0x14063A5D0ull, // tpp::gk::WaterTowerDefenseCallback::Exec
            0x14063D8F0ull, // tpp::gk::ToiletDefenceCallback::Exec
            0x1406418E0ull, // tpp::gk::RadioCasseteDefenseCallback::Exec
            0x1405D2300ull, // buddy viewer command
            0x140A44580ull, // CallBuddyInit
            0x140956BA0ull, // GetParamBuddyPartsFromVars
            0x140A50D20ull, // GetBuddyService
            0x140FAA780ull, // CreateController
            0x140FAA910ull, // NoticeSensorImpl_Init
            0x140FAA93Eull, // notice table sizes, sizing pass
            0x140FAA9AEull, // notice table sizes, build pass
            0x140FAAA2Eull, // notice game socket slots
            0x140FAAA71ull, // notice synchronizer records
            0x140A1F7A0ull, // Mission_GetMissionFlags
            0x140EF73A0ull, // MbDvcSeekPrevSubtitles
            0x140EF7290ull, // MbDvcSeekNextSubtitles
            0x140EF65A0ull, // MbDvcPauseForSeek
            0x140FC7560ull, // tpp::gm::impl::placed::PlacedSystemImpl::CreateCorrelation
            0x140B5CA90ull, // RealizedWalkerGear2Impl::Execute1
            0x140D5BA40ull, // CommandPost2Impl::Initialize
            0x140D5B1D0ull, // CommandPost2Impl free member slots (0x168 - total)
            0x140D59000ull, // CommandPost2Impl::CreateController
            0x141A6B1C0ull, // fox::anim::AnimControl::ActLastPlugins
            0x140216250ull, // fox::Skeleton::BoneMatrixAccessor::BoneMatrixAccessor
            0x1402168F0ull, // fox::Skeleton::BoneMatrixAccessor::SetBoneWorldMatrix
            0x141A6CA10ull, // fox::anim::AnimControl pose -> skeleton bone matrices
            0x142C2DC70ull, // tpp::gm::walkergear::impl::SystemImpl instance
            0x140A50C20ull, // BuddyServiceImpl::GetBuddyGearMainWPType
            0x140A50C00ull, // BuddyServiceImpl::GetBuddyGearMainWPGrade
            0x140A50C60ull, // BuddyServiceImpl::GetBuddyGearSubWPType
            0x140A50C40ull, // BuddyServiceImpl::GetBuddyGearSubWPGrade
            0x141723120ull, // tpp::gm::walkergear::impl::AttackActionImpl::GetMuzzleMatrix
            0x141724670ull, // tpp::gm::walkergear::impl::AttackActionImpl::RegisterStunGunOffenseTarget
            0x14170A270ull, // tpp::gm::walkergear::impl::WalkerGear2Impl::SetUpMainWeapon
            0x140A50BE0ull, // BuddyServiceImpl::GetBuddyGearHeadType
            0x140A50BC0ull, // BuddyServiceImpl::GetBuddyGearHeadGrade
            0x140A53A30ull, // BuddyServiceImpl::SetBuddyGearHeadType
            0x140A53A10ull, // BuddyServiceImpl::SetBuddyGearHeadGrade
            0x141746EA9ull, // walker gear buddy announce_dwalker_find_enemy
            0x141745669ull, // walker gear buddy announce_dwalker_attack_enemy
            0x141744F11ull, // walker gear buddy announce_dwalker_attack_stop
            0x14171684Full, // walker gear buddy mode announce call
            0x14110D150ull, // GetBuddyCommandHelpName
            0x14110D3A0ull, // GetBuddyCommandName
            0x141660670ull, // buddy command menu voice_cmd_wg_whistle_call
            0x141260CE0ull, // tpp::gm::player::impl::RideWalkerGearActionImpl::StateDrive
            0x141044EA0ull, // tpp::gm::player::ActionPlugin::GetDeltaTime
            0x14173031Bull, // walkergear BasicActionImpl::Execute ridden: mov eax, [comm+0x2E0] / mov [work+0x1D8], eax
            0x14172DB40ull, // tpp::gm::walkergear::impl::BasicActionImpl::CorrectVelocity
            0x14172CD00ull, // tpp::gm::walkergear::impl::BasicActionImpl::CheckLowStepOnMove
            0x141730760ull, // tpp::gm::walkergear::impl::BasicActionImpl::FallCheck
            0x14174C360ull, // tpp::gm::walkergear::impl::DamageActionImpl::CorrectVelocity
            0x14125CF30ull, // tpp::gm::player::impl::RideWalkerGearActionImpl::ChangeWeapon
            0x14125F5A0ull, // tpp::gm::player::impl::RideWalkerGearActionImpl::PollingForSubjectiveCamera
            0x141265BE0ull, // tpp::gm::player::impl::RideWalkerGearActionImpl::UpdateCameraBasePosition
            0x141261C46ull, // RideWalkerGearActionImpl::StateDrive: the camera preset call's set-up
            0x140AEC840ull, // tpp::gm::player::GetPlayer2System
            0x141728320ull, // tpp::gm::walkergear::impl::AttackActionImpl::State_FireStunGun
            0x141729610ull, // tpp::gm::walkergear::impl::AttackActionImpl::State_HoldOnArmWeapon
            0x142BE1270ull, // CameraShake_Instance
            0x141722840ull, // tpp::gm::walkergear::impl::AttackActionImpl camera direction (missile aim)
            0x141261618ull, // RideWalkerGearActionImpl::StateDrive weapon ready flag
            0x141261307ull, // RideWalkerGearActionImpl::StateDrive stance press
            0x14126114Cull, // RideWalkerGearActionImpl::StateDrive stance held
            0x141261330ull, // RideWalkerGearActionImpl::StateDrive evade press
            0x1409688C0ull, // tpp::ui::menu::impl::MotherBaseDeviceTopMenuTableImpl::ApplyConditionVisibility
            0x140A53A70ull, // BuddyServiceImpl::SetBuddyGearMainWPType
            0x140A53A50ull, // BuddyServiceImpl::SetBuddyGearMainWPGrade
            0x141742905ull, // walker gear buddy announce_dwalker_lz
            0x1417426D7ull, // walker gear buddy announce_dwalker_escape
            0x141742753ull, // walker gear buddy announce_dwalker_recovery
            0x14174EB04ull, // walker gear buddy announce_dwalker_dying
            0x140205487ull, // TexturePools_ManagerCreateSite
            0x1402038C0ull, // TexturePools_ManagerCtor
            0x142B73420ull, // TexturePools_ManagerInstance
            0x1402178E9ull, // TexturePools_StreamerCountSite
            0x140217BB5ull, // TexturePools_StreamerArraySite
            0x1402200FAull, // TexturePools_PlannerSite
            0x14022A9C8ull, // TexturePools_EnumerateSetupSite
            0x142B5A2E0ull, // TexturePools_JobPool
            0x142B79AE8ull, // TexturePools_ProxyPool
            0x14003C7D0ull, // FileLocationManager_LocatePath
            0x1416A5000ull, // tpp::ui::menu::impl::ItemSelectorCallbackImpl::IsParentEquip
            0x1415DB1A1ull, // ThrowingImpl::UpdateNextPositionHopping mulss xmm0, 9.8
            0x1415DB20Bull, // ThrowingImpl::UpdateNextPositionRolling movaps xmm5, (-0,-9.8,-0,-0)
            0x1415D690Bull, // ThrowingImpl::GetPredictedPositionAtTime mulss xmm0, 9.8
            0x1415D2E80ull, // tpp::gm::impl::throwing::ThrowingImpl::CalcInitialVelocityByAngle
            0x1415D55E0ull, // tpp::gm::impl::throwing::ThrowingImpl::DrawLocusLineImpl
            0x140DAC2D0ull, // tpp::gm::impl::decoy::DecoySystemImpl::Realize
            0x140996140ull, // tpp::gm::impl::decoy::DecoyResourceManagerImpl::InitializePlayerDecoyModel
            0x140996193ull, // tpp::gm::impl::decoy::DecoyResourceManagerImpl::InitializePlayerDecoyModel
            0x140ADFD60ull, // tpp::gm::impl::SimplePartsEffectControllerImpl::DeleteImmediately
            0x140928B60ull, // tpp::ui::menu::impl::MbDvcSoundControllerImpl::Update
            0x140928710ull, // tpp::ui::menu::impl::MbDvcSoundControllerImpl::IsAnyVoicePlaying
            0x140F526E0ull, // tpp::mbm::impl::BaseControllerImpl::CompletePlatformDeck
            0x1408669C0ull, // hud CommonDataManager announce type by langId
            0x140A1D600ull, // tpp::gm::impl::SimpleMissionBlockController::ReceiveMessage
            0x140B79980ull, // VehicleCommunicationInfo::FetchVehicleSeatLocalMatrix
            0x140C92CB0ull, // VehicleCommunicationInfo::GetVehicleTurretCount
            0x1415646A0ull, // RideVehicleActionImpl::State_IdleAction
            0x141569250ull, // RideVehicleActionImpl::UpdateShootState
            0x14138C6D0ull, // AttackActionImpl::GetAimState
            0x14021B490ull, // fox::gr::dg::DgTextureStreamer::DoExecPftxsLoad
            0x14021B54Full, // fox::gr::dg::DgTextureStreamer::DoExecPftxsLoad
            0x14021B650ull, // fox::gr::dg::DgTextureStreamer::DoExecPftxsLoad
            0x14148EF60ull, // InVehicleAiImpl::ShouldWake
            0x140912A20ull, // AnnounceLog_GetLangAnnounceType
            0x14125DCE6ull, // RideWalkerGearActionImpl_ExecActionButtonCarry search centre
            0x14125DD29ull, // RideWalkerGearActionImpl_ExecActionButtonCarry CheckEnableResult call
            0x14171692Eull, // WalkerGear_UpdateCarryAllowedFlag carry bit
            0x141758020ull, // WalkerGear_UpdateAttachmentsAndHoldPoint
            0x141261859ull, // RideWalkerGearActionImpl::StateDrive get-on motion state
            0x140966690ull, // MotherBaseDeviceTopMenuTableImpl::BuildMenuItems
            0x1408783B0ull, // MbDvcManageEv::ChildWindowController::UpdateReceiveTrigger
            0x14095D800ull, // CustomizeSelectorSystemImpl::OpenCustomizeSelector
            0x14095CE20ull, // CustomizeSelectorSystemImpl::EndCustomizeSelector
            0x14095CCB0ull, // CustomizeSelectorSystemImpl::AbortCustomizeSelector
            0x141965F70ull, // CustomizeSelectorCallbackImpl::SetupItemList D-Walker main weapon group filter
            0x1409AA840ull, // tpp::gm::GetScriptVars
            0x1409680C3ull, // lea eax,[rdi-0xA] / test al,0xFD / jne: items 10 and 12 hide while table+0xC3A bit 0 is set
            0x14125DBE6ull, // mov edx,[rcx+rax+0x10] / shr edx,8 / test dl,1: no carry prompt while HOLD is held
            0x1412628C0ull, // tpp::gm::player::impl::RideWalkerGearActionImpl::StateGetOff
            0x141262A03ull, // movabs r9, 0x9C9259D9C72B: the walker get-off motion state
            0x1409678CFull, // lea eax,[rdi-0xA] / test al,0xFD / jne: items 10 and 12 are inactive while table+0xC3A bit 1 is set
            0x140967FE8ull, // MotherBaseDeviceTopMenuTableImpl_IsCutItem_Ids
            0x1415E9AC0ull, // TppUIBinoSubjectiveImpl_Act
            0x1409381C0ull, // UiUtilityImpl_GetAssignedButtonNumber
            0x14043E710ull, // Pad_GetInstance
            0x140450100ull, // PadMapping_GetPhysicalPadNum
            0x14001B9C0ull, // PadListener_GetDeviceType
            0x140023CC0ull, // DevPad_GetInstance
            0x140023F50ull, // DevPad_UpdateSystem
            0x14002F540ull, // DevPad_ReadDirectInput
            0x140203C40ull, // TextureManager_RequestTextureByPath
            0x14089459Dull, // KeySettingEvCall::SetKeySettingMenu controls page row list
            0x14023FE80ull, // TextureResourceDx11::GetShaderResourceView
            0x140935FC0ull, // UiMarkerCommonDataImpl::InsertNearestLzMarker
            0x142BD8210ull, // GatlingGunGameObject::s_gameObjectHandle
            0x140D32C10ull, // tpp::gm::impl::bullet::BulletSystem3Impl::CreateBullet
            0x140B14AC2ull, // RealizedSoldier2Impl_Construct
            0x140B186F0ull, // RealizedSoldier2Impl_PrepareOptCamo
            0x140B16370ull, // RealizedSoldier2Impl_ShowOptCamo
            0x140B163E0ull, // RealizedSoldier2Impl_ReleaseOptCamo
            0x140B08190ull, // tpp::gm::impl::SoundControllerImpl::CallSound
            0x141521170ull, // tpp::gm::soldier::impl::NoticeNoiseAiImpl::StepPreAware
            0x14095BAA0ull, // MissionPreparationSystemImpl_SetPlayerLoadoutInfoFromStaffId
            0x14112C710ull, // CarryActionPluginImpl_ExecActionButtonCarry
            0x141DC5580ull, // ModelNodeMesh_Destruct
            0x141B19360ull, // fox::fx::Effect: sets a named variable of a live effect (the parts effects' Color)
            0x140C12910ull, // tpp::gm::impl::balloon::BalloonSystemImpl::CreateImpl
            0x14111B660ull, // PlayerCameraImpl_SetTpsParams
            0x14120D460ull, // tpp::gm::player::impl::PickUpActionPluginImpl::PickUpAmmoBox
            0x1412852D0ull, // tpp::gm::player::impl::SkillControllerImpl::StartSkill
            0x140FE5210ull, // tpp::gm::player::impl::SkillAndItemParameterSystemImpl::GetItemParamWithEqpId
            0x140A3CE30ull, // tpp::gm::impl::equip::EquipParameterTablesImpl::GetShieldLifeMax
            0x1411AD7C0ull, // tpp::gm::player::impl::EquipControllerImpl::GetShieldLevel
            0x14115C2A0ull, // tpp::gm::player::impl::CqcActionPluginImpl::ExecCqc
            0x140F97AD0ull, // tpp::mbm::impl::StaffControllerImpl::StaffSeedToNameLangMessageCode
            0x141624D30ull, // tpp::ui::hud::impl::TppUIWeaponNumImpl::ShowItemPanel
            0x1411685C0ull, // tpp::gm::player::impl::CqcActionPluginImpl::StateHoldStart
            0x1416BAFA0ull, // tpp::ui::menu::impl::MissionPreparationCallbackImpl::GetCharaSlotIconTexturePathId
            0x1411B6080ull, // tpp::gm::player::impl::EvadeActionPluginImpl::StateEvade
            0x14033D030ull, // AK::SoundEngine::DynamicDialogue::ResolveDialogueEvent
            0x140D2AEF0ull, // tpp::gm::buddyquiet::impl::BuddyQuiet2SoundControllerImpl::CallVoice
            0x140D11000ull, // tpp::gm::buddyquiet::impl::(anon)::DamageHandlerImpl::ProcessDamages
            0x140514260ull, // fox::gs::impl::RouteGroupImpl deleting destructor
            0x141949290ull, // tpp::gm::uav::impl CombatAiImpl::Work / RouteAiImpl::Work element destructor
            0x1419492C0ull, // ChunkAllocatorT::NewObjectArray<tpp::gm::uav::impl::CombatAiImpl::Work>
            0x141954330ull, // ChunkAllocatorT::NewObjectArray<tpp::gm::uav::impl::RouteAiImpl::Work>
            0x141954640ull, // tpp::gm::uav::impl::CombatAiImpl::AreRoutesValid (RouteAiImpl shares it)
            0x140AF3E70ull, // tpp::gm::impl::EffectControllerImpl::CreateEffect
            0x142A66EE8ull, // PlayerPad_ButtonTables
            0x142295BC0ull, // PlayerPad_UiPairList
            0x141206A40ull, // PadImpl_Update
            0x141206710ull, // PadImpl_SetPadMappings
            0x141DBABB0ull, // ButtonMapper_SetAssignedButton
            0x14001B8B0ull, // Fox_PadListener_GetDefault
            0x14085DBB0ull, // GamePauseMenuItemTable_GetLabelLangId
            0x14085DA50ull, // GamePauseMenuItemTable_GetHelpLangId
            0x14085DA30ull, // GamePauseMenuItemTable_CreateDecideEvent
            0x14085DB40ull, // GamePauseMenuItemTable_CopyPageChildren
            0x1408A1DA0ull, // KeyMappingAct_SetupSelectors
            0x14095F610ull, // KeyMapping_GetCategory
            0x1408A2490ull, // KeyMappingAct_CaptureKey
            0x1408A06E0ull, // KeyMappingAct_IsKeyChanged
            0x1408A09B0ull, // KeyMappingAct_RestoreKeys
            0x1408A0980ull, // KeyMappingAct_DefaultNode_Exec
            0x1408A15A0ull, // KeyMappingAct_SetListTextAndIcon
            0x14095E320ull, // tpp::ui::menu::impl::CustomizeUtilityImpl::GetBuddyUsageCost
            0x14195C2F0ull, // tpp::ui::menu::impl::CustomizeSelectorCallbackImpl::DecideActWindowPartBuddyDWalker
            0x140A50B00ull, // tpp::gm::impl::BuddyServiceImpl::GetBuddyGearArmType
            0x140A50AE0ull, // tpp::gm::impl::BuddyServiceImpl::GetBuddyGearArmLevel
            0x141702780ull, // tpp::gm::walkergear::impl::WalkerGear2Impl::CheckHeadChanged
            0x141967090ull, // tpp::ui::menu::impl::CustomizeSelectorRecordCallFunc::SetRecord
            0x141963870ull, // tpp::ui::menu::impl::CustomizeSelectorRecordCallFunc::SetPartsMode
            0x14195C130ull, // tpp::ui::menu::impl::CustomizeSelectorCallbackImpl::DecideAct
            0x14195AB60ull, // tpp::ui::menu::impl::CustomizeSelectorCallbackImpl::SendHeliPartsPreview
            0x1405CEFE0ull, // tpp::gk::impl::ColoringSystemImpl::EvaluateDetail
            0x1405CF050ull, // TppColoringSystem::GetAdditionalColoringPackFilePaths
            0x14167B270ull, // MenuDevelopGrid_BuildSections
            0x14167AD30ull, // MenuDevelopGrid_SetupTabBar
            0x141676EE0ull, // MenuDevelopGrid_ChangeSection
            0x140A44740ull, // tpp::gm::buddy::Buddy2BlockController::CallBuddyWithHalo
            0x140A49150ull, // tpp::gm::buddy::Buddy2BlockController::SetForceChange
            0x140A6FD70ull, // TppHeli2_Allocate
            0x1400E68E0ull, // fox::BlockPackageContext::GetCurrentBlockPackage
            0x1400798E0ull, // fox::StringMap<unsigned __int64,2>::Find
            0x14095E5D0ull, // tpp::ui::menu::impl::CustomizeUtilityImpl vtable +0x58: the D-Walker's current loadout slot (vars+0x1B74)
            0x14095E9D0ull, // tpp::ui::menu::impl::CustomizeUtilityImpl vtable +0x60: sets the D-Walker's current loadout slot
            0x14095DFB0ull, // tpp::ui::menu::impl::CustomizeUtilityImpl::GetBuddyDWalkerUsageCost
            0x1416A5800ull, // tpp::ui::menu::impl::ItemSelectorCallbackImpl: takes the equip kind and sets the list up (unnamed in the exe)
            0x140A4DCD0ull, // tpp::gm::buddy::impl::BuddyServiceImpl vtable +0x1F8: the field equipment change (unnamed in the exe)
            0x141962090ull, // tpp::ui::menu::impl::CustomizeSelectorCallbackImpl::RestoreCustomizedDWalkerPartsFromVars
            0x141963660ull, // tpp::ui::menu::impl::CustomizeSelectorCallbackImpl::SetCustomizedDWalkerPartsToVars
            0x140B38FE0ull, // tpp::gm::vehicle::BodyData::DataBody::SetupBody
            0x140B38D70ull, // tpp::gm::vehicle::AttachmentData::DataBody::SetupAttachment
            0x1416F51E0ull, // tpp::gm::vehicle::impl::ConstantImpl::GetVehicleTypeSpecs
            0x140A25E60ull, // tpp::gm::vehicle::ConvertIntoReliefType
            0x142C36A00ull, // tpp::gm::vehicle::g_reliefPackFilePaths
            0x142C20730ull, // tpp::gm::vehicle::impl::SystemImpl::s_instance
            0x14088CC50ull, // tpp::ui::menu::KeySettingEvCall::ChangeControllerButton
            0x14088E8D0ull, // tpp::ui::menu::KeySettingEvCall: stops the scrolling of the controller picture's labels
            0x14088ECC0ull, // tpp::ui::menu::KeySettingEvCall: reads confirm and cancel on an options page
            0x140892F00ull, // tpp::ui::menu::KeySettingEvCall::SelectKeySettingMenu
            0x140892AD0ull, // tpp::ui::menu::KeySettingEvCall::ProcessDecide
            0x140894591ull, // KeySettingEvCall::SetKeySettingMenu Control Type page row list
            0x140910420ull, // tpp::ui::utility::CreateBoxText
            0x140A29360ull, // tpp::gm::vehicle::impl::SystemImpl::TryToRide
            0x1415615D0ull, // tpp::gm::soldier::impl::RideVehicleActionImpl::State_GetOutAction
            0x140A274E0ull, // tpp::gm::vehicle::impl::SystemImpl::GetRiderOff
            0x140A28430ull, // tpp::gm::vehicle::impl::SystemImpl::RegisterExtendedRider
            0x140A29630ull, // tpp::gm::vehicle::impl::SystemImpl::UnregisterExtendedRider
            0x140FB3930ull, // tpp::gm::soldier::impl::PassengerInfoSystemImpl::AddPassenger
            0x140FB46D0ull, // tpp::gm::soldier::impl::PassengerInfoSystemImpl::UpdateReserveSeatIndex
            0x14156EC30ull, // tpp::gm::soldier::impl::RouteAiImpl::GetVehicleEntryPosition
            0x141564090ull, // tpp::gm::soldier::impl::RideVehicleActionImpl::State_GetOutImmediatelyAction
            0x14155FAB0ull, // tpp::gm::soldier::impl::RideVehicleActionImpl::SetVehicleInfo
            0x141560E80ull, // tpp::gm::soldier::impl::RideVehicleActionImpl::State_GetInAction
            0x140B2E7F0ull, // tpp::gm::vehicle::impl::GameObjectImpl::Initialize
            0x140BA80B0ull, // tpp::gm::VehicleCommunicationInfo::TransmitDamageToRiders
            0x140BA327Aull, // tpp::gm::vehicle::impl::BodyBase::Initialize: call GetBuilderMemberFromTypeIndex before the vanilla model shader list check
            0x140B0F320ull, // fox::gk::PartsBuildDirector::GetBuilderMemberFromTypeIndex
            0x140F13160ull, // tpp::ui::menu::impl::MbDvcMapCallbackControlImpl::RendezvousOperation
            0x140BDB670ull, // tpp::gm::vehicle::impl::WeaponBulletImpl::FireSpecs
            0x140936330ull, // tpp::ui::hud::impl::UiMarkerCommonDataImpl::RegisterMarker
            0x140F02C40ull, // tpp::ui::menu::impl::MbDvcDependJobFunctionsImpl::RegisterMapIconSelectMode_LandPoint
            0x142BDE6F8ull, // tpp::gk::s_saveStorageController
            0x1416A3870ull, // tpp::ui::menu::impl::ItemSelectorCallbackImpl::ExhibitSelectingRelief
            0x14095C7A0ull, // tpp::ui::menu::impl::MissionPreparationSystemImpl::StartVehicleDisplay
            0x1409553A0ull, // tpp::ui::menu::impl::MissionPreparationSystemImpl::ChangeVehicleDisplay
            0x1416BC490ull, // tpp::ui::menu::impl::MissionPreparationCallbackImpl::ReserveSelectionRelief
            0x140F7D700ull, // tpp::mbm::impl::ResourceControllerImpl::PayResourceCost
            0x1416BD830ull, // tpp::ui::menu::impl::MissionPreparationCallbackImpl::SetTopMenuVehicleName
            0x140A26120ull, // tpp::gm::vehicle::impl::SystemImpl::ExhibitRelief
            0x140A29150ull, // tpp::gm::vehicle::impl::SystemImpl::SpawnRelief
            0x140A25F70ull, // tpp::gm::vehicle::impl::SystemImpl::DeliverRelief
            0x140B2B6D0ull, // tpp::gm::vehicle::impl::GameObjectImpl::DispatchLuaCommandToInstance
            0x14166BBC0ull, // tpp::ui::menu::mbm::impl::EquipDetailsCallbackImpl::SetupVehicleDetails
            0x1416727F0ull, // tpp::ui::menu::impl::MenuDevelopGrid (R&D grid): a cell's EQUIPPED tag
            0x141674500ull, // tpp::ui::menu::impl::MenuDevelopGrid (R&D grid): equip step after the equip dialog's yes
            0x1409661B0ull, // tpp::ui::menu::impl::MotherBaseDeviceTopMenuTableImpl::OnItemDecided
            0x14196CC17ull, // tpp::ui::menu::impl::CustomizeSelectorCallbackImpl::UpdateMenuTitleText D-Walker title
            0x14196CC9Cull, // tpp::ui::menu::impl::CustomizeSelectorCallbackImpl::UpdateSettingMenuHelpText D-Walker slot help
            0x141651128ull, // tpp::ui::menu::impl::CustomizeSlotSelectorCallbackImpl::SetupListElementWalkerGear title
            0x141667AB0ull, // tpp::ui::menu::mbm::impl::EquipDetailsCallbackImpl::SetupDetails
            0x141667AF3ull, // tpp::ui::menu::mbm::impl::EquipDetailsCallbackImpl::SetupDetails walker type check
            0x14166C862ull, // tpp::ui::menu::mbm::impl::EquipDetailsCallbackImpl::SetupWalkerDetails title
            0x140A28E8Bull, // tpp::gm::vehicle::impl::SystemImpl::SaveCarry: call VehicleCommunicationInfo::GetVehicleType on the carried vehicle
            0x140A25F00ull, // tpp::gm::vehicle::ConvertIntoResourceId
            0x1416FA5A0ull, // tpp::gm::vehicle::impl::InstanceImpl::ReflectLifeState
            0x140BCB520ull, // tpp::gm::vehicle::impl::ManeuverControllerImpl::FultonRecovery
            0x140F7AC10ull, // tpp::mbm::impl::ResourceControllerImpl::CanPayResourceCost
            0x140ACFCD0ull, // tpp::gm::impl::SupportRequestSystemImpl::RequestDropVehicle
            0x141673220ull, // tpp::ui::menu::impl::MenuDevelopGrid (R&D grid): decide on a cell -> 0 equip / supply-drop dialog, 2 develop dialog, 3 refused (named IsSupplyDevelop on 8080)
            0x1416679E0ull, // tpp::ui::menu::impl::EquipDetailsCallbackImpl: an entry's damage badge (details, entry, damage type 1 lethal / 2 sleep / 3 stun)
            0x14023AFC0ull, // fox::gr::dg::TextureResource_Load
            0x140B34D30ull, // tpp::gm::vehicle::impl::GameObjectImpl::Seize
            0x140F79090ull, // tpp::mbm::impl::ResourceControllerImpl::AddResource
            0x140A88B30ull, // tpp::mbm::MotherBaseManagementSystem::AddTempResource
            0x140F79230ull, // tpp::mbm::impl::ResourceControllerImpl::AddResourcesFromTempResourceBuffer
            0x14196F5C0ull, // tpp::ui::menu::mbm::impl::DataBaseCallbackImpl::RefreshMapLayout
            0x14196FDF0ull, // tpp::ui::menu::mbm::impl::DataBaseCallbackImpl::RefreshViewMap
            0x140DD9B70ull, // tpp::gm::heli::impl::Heli2Impl::Realize (vtable slot 0 next to Unrealize)
            0x140B579C0ull, // RealizedHeli2Impl creation (TppHeli2_Allocate's only realized-heli builder)
            0x140922FA0ull, // tpp::ui::menu::impl::MotherBaseMapCommonDataImpl::GetUniqueInformationIconLangId
            0x1416D0AF0ull, // tpp::ui::menu::mbm::impl::ResourceManagementCallbackImpl::RefreshPrefabList
            0x1416D3110ull, // tpp::ui::menu::mbm::impl::ResourceListRecordCallFunc::Start
            0x1416D0970ull, // tpp::ui::menu::mbm::impl::ResourceManagementCallbackImpl: info text of the cursor row
            0x1407E83C0ull, // tpp::mbm::ResourceController::GetResourceUsableCount
            0x1407E8410ull, // tpp::mbm::ResourceController: second count the sale check adds (server stock)
            0x141DCF720ull, // fox::ui::WindowResourceCreator::CreateLayouts
            0x140F7DA60ull, // tpp::mbm::impl::ResourceControllerImpl::SaleResource
            0x140A70520ull, // TppHeli2 type vtable slot 1: destroys every realized heli, the preloaded bullets and the Heli2Impl quark
            0x140F755B0ull, // tpp::mbm::impl::PoolRewardControllerImpl::AddPoolRewards
            0x140F75300ull, // tpp::mbm::impl::PoolRewardControllerImpl::AddPoolRewardCore
            0x140F744E0ull, // tpp::mbm::impl::PoolRewardControllerImpl::AcceptPoolRewardOneCore
            0x1416C9800ull, // tpp::ui::menu::mbm::impl Rewards list row draw (row, entry type branches)
            0x141646A10ull, // tpp::ui::menu::mbm::impl::CombatDeployCallbackImpl::RefreshMissionRewards
            0x1416484F0ull, // CombatDeployCallbackImpl GMP reward row (page, row, amount, fob)
            0x141648860ull, // CombatDeployCallbackImpl material reward row (page, row, materialIndex, rewardParam)
            0x141648CC0ull, // CombatDeployCallbackImpl staff reward row (page, row, staffDraw, rewardParam)
            0x1416D0810ull, // tpp::ui::menu::mbm::impl::ResourceManagementCallbackImpl::RefreshImageRecord
            0x1416CF1F0ull, // Resources sale popup: item picture, name, price and counts
            0x140B8CD60ull, // Heli2 model plugin vt+0x108 (instance, FormVariationFile2): applies a heli paint, then switches 5 hard-coded vanilla materials to painted mode
            0x1401CBC80ull, // fox::gr::Model material modifier of a mesh by its 32-bit name (the paint switch lookup)
            0x140261050ull, // fox::gr::MaterialModifier2::SearchParameterInfo
            0x140260E30ull, // fox::gr::MaterialModifier2::GetParameter
            0x1402610C0ull, // fox::gr::MaterialModifier2 set vector parameter (material, name, const Vector4*)
            0x1402174E0ull, // fox::gr::dg::DgTextureStreamer proxy list node pool setup
            0x140217670ull, // fox::gr::dg::DgTextureStreamer base constructor
            0x140217740ull, // fox::gr::dg::DgTextureStreamer::DgTextureStreamer
            0x140218C00ull, // DgTextureStreamer proxy use counter read (FUN_140218680)
            0x140218E40ull, // fox::gr::dg::DgTextureStreamer::AddSearchTag
            0x140219230ull, // DgTextureStreamer proxy list node alloc
            0x140219870ull, // fox::gr::dg::DgTextureStreamer::CancelUpgrade
            0x140219B70ull, // fox::gr::dg::DgTextureStreamer proxy list node pool reset
            0x14021A130ull, // DgTextureStreamer proxy bits clear (FUN_140219bb0)
            0x14021A760ull, // DgTextureStreamer proxy list node free
            0x14021A8A0ull, // fox::gr::dg::DgTextureStreamer::DeleteSearchTag
            0x14021AFD0ull, // DgTextureStreamer proxy index check (FUN_14021aa50)
            0x14021B230ull, // DgTextureStreamer released resource data flush (FUN_14021acb0)
            0x14021B8A0ull, // DgTextureStreamer upgrade release (FUN_14021b320)
            0x14021BCB0ull, // fox::gr::dg::DgTextureStreamer::DoUpgrade2
            0x14021BE40ull, // fox::gr::dg::DgTextureStreamer::DoUpgradeEntry
            0x14021C800ull, // DgTextureStreamer proxy bits reset (FUN_14021c280)
            0x14021D4B0ull, // fox::gr::dg::DgTextureStreamer::GetProxy
            0x14021D640ull, // DgTextureStreamer proxy bit test (FUN_14021d0c0)
            0x14021D760ull, // fox::gr::dg::InitStaticResourceDataDx11
            0x14021DF10ull, // DgTextureStreamer proxy index check (FUN_14021d990)
            0x14021E850ull, // fox::gr::dg::DgTextureStreamer::PostCreation
            0x14021EC20ull, // DgTextureStreamer proxy use counter add (FUN_14021e6a0)
            0x14021ECA0ull, // DgTextureStreamer proxy use counter add by texture slot (FUN_14021e720)
            0x14021F000ull, // fox::gr::dg::DgTextureStreamer::SearchProxyFromPath
            0x14021FE40ull, // DgTextureStreamer proxy use counter sub (FUN_14021f8c0)
            0x14021FEC0ull, // DgTextureStreamer proxy use counter sub by texture slot (FUN_14021f940)
            0x14021FFA0ull, // DgTextureStreamer upgrade planner (FUN_14021fa20)
            0x1402210C0ull, // fox::gr::dg::DgTextureStreamer::UpdateProxyLoadedStatus
            0x140221460ull, // fox::gr::dg::DgTextureStreamer::allocateHandlableProxy
            0x1402218F0ull, // fox::gr::dg::DgTextureStreamer::doDegrade
            0x140222590ull, // fox::gr::dg::DgTextureStreamer::pftxsWaitForDecompression
            0x1402228B0ull, // DgTextureStreamer proxy bits clear (FUN_140222330)
            0x140222A70ull, // fox::gr::dg::DgTextureStreamer::safeFreeResourceDataMembers
            0x140216EE0ull, // dynamic_sequence_allocator<DgIncrementalTextureResource::Proxy> constructor
            0x1400041A0ull, // std::malloc
            0x149D12A52ull, // develop array sweep allowed site
            0x149D35732ull, // develop array sweep allowed site
            0x149D3ADEAull, // develop array sweep allowed site
            0x149D522E2ull, // develop array sweep allowed site
            0x149D5D78Eull, // develop array sweep allowed site
            0x149D5F52Aull, // develop array sweep allowed site
            0x14125E580ull, // RideWalkerGearActionImpl_UpdateRiderUi
        };
        return value;
    }
}
