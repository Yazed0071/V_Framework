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
            0x141A09B10ull, // PathHashCode
            0x140972C70ull, // PauseMusicPlayer
            0x140EF6BD0ull, // PlayOrPauseSelectedTrack
            0x140A69070ull, // RequestCorpse
            0x1409739E0ull, // ResumeMusicPlayer
            0x145F236F0ull, // SetEquipBackgroundTexture
            0x1408D78A0ull, // SetLuaFunctions
            0x141DC78F0ull, // SetTextureName
            0x140989120ull, // SoundSystemCtor
            0x14A2ACC00ull, // StateRadioRequest
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
            0x14BBC3630ull, // Sahelan_ActionCoreImpl_UpdateEyeLampColor
            0x14BBC37A0ull, // Sahelan_ActionCoreImpl_UpdateHeartLight
            0x14BCA4b70ull, // Sahelan_PhaseSneakAi_PushEyeColor
            0x140EF2EE0ull, // MbDvcAnnouncePopupCallbackImpl_UpdateAnnounceNormal
            0x140EF32A0ull, // MbDvcAnnouncePopupCallbackImpl_UpdateAnnounceServer
            0x140EF2CC0ull, // MbDvcAnnouncePopupCallbackImpl_UpdateAnnounceEmergency
            0x145C0A890ull, // HudCommonDataManager_GetInstance
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
            0x14162136Bull, // IconTitleGetLangTextCall
            0x146CC7170ull, // GameObject_SendCommand
            0x1496A8070ull, // UiControllerImpl_InitEquipHudData
            0x1414E6110ull, // NoticeControllerImpl_GetOccasionalChat
            0x140D83480ull, // SoldierConversationService_ConvertSpeechLabelToConversationType
            0x1414E6229ull, // OccasionalChat_FactionTestNop
            0x140932BE0ull, // MbDvcReserveAnnouncePopup
            0x140939EE0ull, // MbDvcPopupGateFn
            0x14151FE30ull, // NoticeNoiseAiImpl_StepAware
            0x141513B00ull, // NoticeIndisAiImpl_StepAware
            0x1414E5830ull, // NoticeControllerImpl_DoCheckSpreadNotice
            0x1414E3070ull, // NoticeControllerImpl_CheckSightNoticeSoldier
            0x140E46630ull, // NoticeNearThreatAiImpl_Wake
            0x140E47000ull, // NoticeNoiseAlertAiImpl_Wake
            0x14A8B9420ull, // NoticeNearGameObjectAiImpl_Wolf_Wake
            0x14A8BD650ull, // NoticeNoiseSneakAiImpl_Wolf_Wake
            0x146D89810ull, // NoticeNearGameObjectAiImpl_Bear_Wake
            0x140C36BE0ull, // NoticeNoiseAiImpl_Bear_Wake
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
            0x1408A2EA0ull, // Hud_TypingLogActUpdate
            0x141DC9ED0ull, // Ui_SoundControlStart
            0x14085F0C0ull, // Ui_UiCommonDataManagerGetInstance
            0x14085F190ull, // Ui_EventNodeBodyGetGraphState
            0x145F804D0ull, // VoiceParam_PlayDialogue
            0x149B603F0ull, // RideHeliActionPluginImpl_ExecPreMotionGraph
            0x14121EB90ull, // RideHeliActionPluginImpl_GetStateFn
            0x1411FB360ull, // MBTerminalActionPluginImpl_IsDisableMBTerminal
            0x14110F160ull, // CallMenuManagerImpl_Update
            0x1411FA660ull, // MBTerminalActionPluginImpl_CheckActionState
            0x142BFF960ull, // SoundDaemon_Instance
            0x146142730ull, // SoundDaemon_PostEventQueue
            0x1420A72B0ull, // Sd_kW_Select
            0x140d7c7d0ull, // UpdateAntiAir
            0x147362370ull, // ClearAntiAir
            0x145F785D0ull, // GetChangeLocationMenuParameterByLocationId
            0x145F78B90ull, // GetMbFreeChangeLocationMenuParameter
            0x140925ef0ull, // GetPhotoAdditionalTextLangId
            0x140fe7db0ull, // UiControllerImpl_HideBinocle
            0x1466A5770ull, // AddCassetteTapeTrack
            0x149309EA0ull, // CollectGotTapes
            0x1466EC350ull, // IsGotCassetteTapeTrack
            0x140AAC670ull, // SetCassetteTapeTrackNewFlag
            0x140EF7A50ull, // SetCurrentAlbum
            0x140974880ull, // SetupMusicInfos
            0x1450487C0ull, // RadioCassette_SearchCasseteInfo
            0x1450475C0ull, // RadioCassette_GetCassetteMusic
            0x145047E90ull, // RadioCassette_IsGotCassette
            0x145047B10ull, // RadioCassette_GetCassetteSaveIndex
            0x1450C73E0ull, // RadioCassette_SdPostEvent
            0x140641CA0ull, // RadioCassette_RadioUpdate
            0x1466A5A50ull, // AddCassetteTapeTrackByIndex
            0x1451FCC50ull, // RadioCassette_ActivateUnit
            0x1405D56D0ull, // RadioCassette_IsSameSaveIndexFromName
            0x14126C060ull, // SearchLightActionPluginImpl_StateDoorStart
            0x14126B560ull, // SearchLightActionPluginImpl_StateDoorEnd
            0x140338B60ull, // SoundControl_PostExternalEvent
            0x140989B20ull, // MusicPlayerPlayWrapper
            0x145FA1510ull, // Barrier_GetItemId
            0x1408B1A0Dull, // Barrier_EquipItemCallRet
            0x141000780ull, // Barrier_Updater
            0x14504E090ull, // Barrier_IsFobMode
            0x1409C3AF5ull, // Barrier_LoadGate0
            0x1409C3B5Full, // Barrier_LoadGate1
            0x1409C3BC5ull, // Barrier_LoadGate2
            0x140FC3CC0ull, // Dm_FireLoop
            0x140A2A480ull, // Dm_Classify
            0x141B18900ull, // Dm_VfxFactory
            0x140C0C0B0ull, // Dm_ComponentFactory
            0x140BFF480ull, // Dm_Alloc
            0x140AF36D0ull, // Dm_BackLinkPool
            0x1404ECF80ull, // Dm_OneShot
            0x1497396C0ull, // AttackActionImpl_IsWeaponNoUseInPlaceAction
            0x1466f3b10ull, // TppMotherBaseManagement_RegCstDev
            0x1466f4600ull, // TppMotherBaseManagement_RegFlwDev
            0x140f6b660ull, // EquipDevCtrl_GetSuitDevelopInfoIndex
            0x140f69f20ull, // EquipDevCtrl_GetFaceEquipDevelopInfoIndex
            0x1416a1aa0ull, // AddListSuit
            0x14691b460ull, // CamoufParamInfo_GetCamoufValue
            0x140fdc5d0ull, // CamouflageController_ExecSuitCorrect
            0x1411b0d10ull, // EquipController_SetHandSlotEnabled
            0x141675600ull, // EquipDevelopCallbackImpl_SetSupplyCBoxInfo
            0x14a56bfa0ull, // IsEnableCurrentSuit
            0x1416a4280ull, // ItemSelectorCallbackImpl_DecideActMissionPreparationSetEquipMode
            0x1416a51c0ull, // ItemSelectorCallbackImpl_DecideActMotherBaseDeviceSupportDropMode
            0x1416a9b80ull, // ItemSelectorCallbackImpl_SetupPrefabListElement
            0x1416af270ull, // ItemSelectorRecordCallFunc_UpdateRecords
            0x14a53c210ull, // ItemSelector_AddListBandana
            0x140ae90f0ull, // LoadPlayerBionicArmFpk
            0x140ae9040ull, // LoadPlayerBionicArmFv2
            0x146864180ull, // LoadPlayerCamoFpk
            0x146863f80ull, // LoadPlayerCamoFv2
            0x146866c80ull, // LoadPlayerPartsFpk
            0x146865f80ull, // LoadPlayerPartsParts
            0x146864e30ull, // LoadPlayerSnakeBlackDiamondFpk
            0x146864c80ull, // LoadPlayerSnakeBlackDiamondFv2
            0x140ae8df0ull, // LoadPlayerSnakeFaceFpk
            0x140ae8ce0ull, // LoadPlayerSnakeFaceFv2
            0x1460b9fa0ull, // MissionPrepSystem_IsEnableHeadOptionSuit
            0x14a56ba20ull, // MissionPrep_IsEnableCurrentHeadOption
            0x146239160ull, // Player2BlockController_LoadPartsNew
            0x1409c5d00ull, // Player2GameObjectImpl_ProcessSignal
            0x1409ca560ull, // Player2Impl_SetUpParts
            0x1462c93f0ull, // Player2UtilityImpl_LoadoutApplyAfterSetSuit
            0x1462c7670ull, // Player2UtilityImpl_SetInitialConditionWithLoadoutInfo
            0x1409defe0ull, // Player2UtilityImpl_SetSuitAndHandConditionWithLoadoutInfo
            0x14622a3b0ull, // Player_ConverFaceIdWithFaceEquipId
            0x140ae84b0ull, // ResourceTable_DoesNeedFaceFova
            0x140ae8500ull, // ResourceTable_DoesNeedFaceFovaForAvatar
            0x1409c45c0ull, // Sys_IsArtificialHandEnabled
            0x141e02d80ull, // Sys_IsArtificialHandEnabledForCurrentPlayerType
            0x1409cc380ull, // UpdatePartsStatus
            0x1416a3670ull, // ItemSelectorCallbackImpl_DecideActMotherBaseCustomize
            0x143232490ull, // Fox_Path_Exists
            0x14319eb70ull, // Fox_Path_Dtor
            0x14d9aa3d0ull, // PluginFacial_ApplyMotion
            0x140f6d8e0ull, // EquipDevCtrl_IsEquipVisile
            0x14950ed50ull, // EquipDevCtrl_GetHeadBadgeCategory
            0x14a544d40ull, // MissionPrep_SetInitialSelectRecord
            0x1464a0e00ull, // TppEquip_RegisterConstant
            0x141e02930ull, // PlayerInfoInterfaceImpl_GetPartsTypeAtCamoType
            0x142c20fb0ull, // EquipIdTable_InfoList
            0x142a70928ull, // EquipIdTable_TypeWords
            0x1464b6740ull, // EquipIdTable_ReloadEquipIdTable
            0x14950d190ull, // EquipDevelopCtrl_GetEquipDevelopIndex
            0x149527750ull, // EquipDevelopCtrl_SetEquipUndeveloped
            0x149519770ull, // EquipDevCtrl_GetSuitCamoType
            0x149519990ull, // EquipDevCtrl_GetSuitLevel
            0x140f6d7a0ull, // EquipDevCtrl_IsEquipSuit
            0x140f6ed90ull, // EquipDevelopCtrl_SetEquipDeveloped
            0x1412a2f80ull, // SupplyCboxActionPluginImpl_StateInBox
            0x140863050ull, // HudCommonDataManager_AnnounceLogView
            0x145e6ab60ull, // Ui_LangIdToKey
            0x1409122d0ull, // Ui_GetLangText
            0x141393B8Eull, // Soldier_ShootOneBullet_GroupMaskCall
            0x140D2DB6Dull, // Soldier_ActivateBulletAtEmptyWork_SameArmyJnz
            0x1408A88E0ull, // TelopStartTitleEvCall_SetBgTexture
            0x141DAEF60ull, // Layout_GetLayout
            0x141DAEFA0ull, // Layout_GetModel
            0x146081DB0ull, // GetCassetteTapeUnreadInfo
            0x1466EE5A0ull, // IsNewCassetteTapeTrack
            0x149308D60ull, // CassetteMenuCheckNewFlag
            0x1493087C0ull, // CassetteAlbumCheckNewFlag
            0x149309C50ull, // CassetteCheckUnreadInfo
            0x144A06660ull, // SubtitlesObjectSendMessage
            0x14951ed70ull, // EquipDevCtrl_IsEquipDevelopable
            0x146860380ull, // LoadAvatarFaceFv2
            0x1468606e0ull, // LoadAvatarFaceFpk
            0x1409B56D0ull, // AvatarFaceEditUpdate
            0x144A4A350ull, // Fox_ModelFromHandle
            0x14685DA20ull, // LoadAvatarHeadOptionFv2
            0x14685DD50ull, // LoadAvatarHeadOptionFpk
            0x1495264F0ull, // EquipDevelopCtrl_SetEnableDevelop
            0x1464C1180ull, // ReloadEquipParameterTables2
            0x142C25C30ull, // GunBasicParameters2Buffer
            0x142A711F0ull, // EquipParameterTablesImpl_Instance
            0x1423495C0ull, // MotionLoaderImpl_ReceiverTypeTable
            0x147413D10ull, // MotionLoaderImpl_GetReceiverType
            0x1423495A8ull, // MotionLoaderImpl_UnderBarrelTypeTable
            0x147414460ull, // MotionLoaderImpl_GetUnderBarrelType
            0x14951AC10ull, // EquipDevelopControllerImpl_GetSuppressorAmount
            0x142BDCFE0ull, // DamageParameterTable_Instance
            0x145000FF0ull, // DamageParameterTable_ReloadDamageParameter
            0x144FFB300ull, // DamageParameterTable_GetDamageParameter
            0x149500A30ull, // EquipDevCtrl_GetBaseDevelopId
            0x141679d50ull, // MenuDevelopGrid_FillGrid
            0x1416796d0ull, // MenuDevelopGrid_CopyGrid
            0x14a4ef340ull, // MenuDevelopGrid_CountBadge
            0x14167a2f0ull, // MenuDevelopGrid_FillFlat
            0x14a4f7130ull, // MenuDevelopGrid_CopyFlat
            0x1412829a0ull, // SightManager_UpdateMissileLockOn
            0x140d2fd20ull, // Bullet3_DoSimulation
            0x147215650ull, // Bullet3_ActivateBulletAtEmptyWork
            0x149c6cd60ull, // SightManager_Update
            0x149c6e5c0ull, // SightManager_UpdateMissileLockOnUi
            0x145debb40ull, // LockOnReticleFactory_CreateWindow
            0x140A3B5E0ull, // EquipParams_GetAttackIdByEquipId
            0x140dc35e0ull, // EquipSystem_SetUpGunInfoFromGunPartsDesc
            0x141daba30ull, // UiWindowFunction_FindWindow
            0x141dabd70ull, // UiWindowFunction_PostShowAndStartMessage
            0x141dabab0ull, // UiWindowFunction_GetLayout
            0x141041950ull, // AttackAction_Fire
            0x141303a40ull, // EquipObject_DoFire
            0x14132bf90ull, // Shell_ActivateShellAtEmptyWork
            0x1411574f0ull, // CorePlugin_UpdateLoadoutRequest
            0x140dc0120ull, // WeaponSystem_DefineWeaponFireSound
            0x142C93480ull, // EquipSystem_ChimeraPartsSetWork
            0x140EFC010ull, // MbDvcUpdateTrackListCallFuncs
            0x140EF7680ull, // MbDvcRefreshTrackListPrefabParameter
            0x140EF6F30ull, // MbDvcTrackListRecordRefresh
            0x140F31E50ull, // MbDvcMissionListCallbackImpl_ActivatePrefabPopupParameter
            0x149459D50ull, // MbDvcMissionListCallbackImpl_SetMenuHelp
            0x1464B6340ull, // Equip_ReloadChimeraPartsInfoTable
            0x142C207A0ull, // Equip_ChimeraPartsPackageInfos
            0x141A75720ull, // Mtar_GetAnimFile
            0x141A762F0ull, // Mtar_GetDataInfo
            0x142A6B400ull, // Equip_MotionEntryTable
            0x1422E2560ull, // SimplePartsControllerImpl_Vtable
            0x146826550ull, // SimplePartsControllerImpl_SetMotionData
            0x146826900ull, // SimplePartsControllerImpl_SetMotionDataByPath
            0x140DBFA70ull, // EquipSystemImpl_HideMagazine
            0x140DBEF70ull, // EquipSystemImpl_GetPartsController
            0x140BFF3F0ull, // Fox_GetQuarkSystemTable
            0x140071030ull, // Fox_ArrayBaseExtend
            0x1420BCAC8ull, // Fox_ArrayOperatorVtbl
            0x141AB7100ull, // Animx_GetControlSize
            0x141AB6B20ull, // Animx_SimpleControlCtorPool
            0x141AB6EA0ull, // Animx_SimpleControlCtorHeap
            0x141AB6D00ull, // Animx_SimpleControlDtorPool
            0x141AB6F10ull, // Animx_SimpleControlDtorHeap
            0x141AB6E90ull, // Animx_SimpleControlAux
            0x145045590ull, // Equip_MotionEntrySlotHookA
            0x1450464F0ull, // Equip_MotionEntrySlotHookB
            0x14649E9C0ull, // EquipIdTable_GetEquipTypeId
            0x1414404D0ull, // Soldier2InterrogateUtil_UpdateInterrogation
            0x141441EB0ull, // Soldier2InterrogateUtil_UpdateInterrogationMarker
            0x1468F0540ull, // SoundControllerImpl_CallVoice
            0x14742D6D0ull, // EquipSystem_GetGunInfoById
            0x1460257C0ull, // Reticle_InitHandGunAsset
            0x149CD4AA0ull, // TimeCigaretteActionPluginImpl_HideTimeCigaretteUi
            0x143173d10ull, // Fox_EntityInfoMapTeardown
            0x142AC6D10ull, // WeaponEnhanceLangIdArray
            0x149581170ull, // WeaponEnhance_GetIndexForRegist
            0x140fa0260ull, // WeaponEnhance_GetMessageIdArray
            0x1495812F0ull, // WeaponEnhance_GetLangIdById
            0x1495814B0ull, // WeaponEnhance_GetLangIdBound
            0x140b3b690ull, // BulletEffectController_CreateEffect
            0x1416A2160ull, // ItemSelectorCallbackImpl_AddRecord
            0x140AE3C40ull, // AdditionalMotionTable_GetMtarPathId
            0x1416A33F0ull, // ItemSelector_AddDevelopWeaponList
            0x1409B6F40ull, // BlockControllerImpl_UpdateAdditionalMotionBlock
            0x14006D440ull, // Fox_Block_GetFileForPathId
            0x14006D230ull, // Fox_BlockGroup_GetBlockAtIndex
            0x1416AD560ull, // ItemSelector_StartEquipPreviewImpl
            0x141682D50ull, // UiEquipPreviewController_ScrollNext
            0x141682BA0ull, // UiEquipPreviewController_ScrollPrev
            0x146255A40ull, // Player2Impl_AddAdditionalMtarAll
            0x146270C80ull, // Player2Impl_RemoveAdditionalMtarAll
            0x1423496B0ull, // MotionLoaderImpl_BarrelTypeTable
            0x147410F10ull, // MotionLoaderImpl_GetBarrelType
            0x142349730ull, // MotionLoaderImpl_MagazineTypeTable
            0x147412460ull, // MotionLoaderImpl_GetMagazineType
            0x1423497F0ull, // MotionLoaderImpl_StockTypeTable
            0x147413F80ull, // MotionLoaderImpl_GetStockType
            0x14120B025ull, // TppPickable_ItemWindowBoundSite
            0x1409147F0ull, // UiUtility_GetWeaponItemNameLangId
            0x140983B25ull, // SoundPlayerAnimEvent_StepNoiseQuietCmp
            0x140A0AEA0ull, // CollectionLocatorArray_SetUpLinearAccessor
            0x140D4FB40ull, // CollectionSystemImpl_PickUp
            0x140D50E30ull, // CollectionSystemImpl_RepopCountOperation
            0x140D4D4E0ull, // CollectionSystemImpl_GetCollectionInfo
            0x140D4E460ull, // Collection_GetModelFilePath
            0x140D4D290ull, // Collection_GetCatchEffectColorAndScale
            0x140D4DFF0ull, // Collection_GetGroundEffectSize
            0x141209CD0ull, // PickUpActionPluginImpl_DoPickUpCollection
            0x14090FF00ull, // GetCollectionIconFtexPath
            0x1409102E0ull, // GetCollectionNameText
            0x14050B2A0ull, // UixUtility_GetUixUtility
            0x140D4E680ull, // Collection_GetModelFilePathRoot
            0x140D4DE90ull, // Collection_GetFovaFilePath
            0x140A0BB30ull, // Collection_IsHerbByType
            0x140A0BBA0ull, // Collection_IsMaterialByType
            0x140A0BA50ull, // Collection_IsDiamondByType
            0x140A95E10ull, // MbmImpl_IsGotDataBase
            0x140A880E0ull, // MbmImpl_AddTempDataBase
            0x141160A64ull, // QuietChokeHold
            0x1411611F9ull, // QuietInterrogate
            0x14111013Cull, // QuietInterrogateBypass
            0x14110E5B7ull, // QuietHoldupInterrogate
            0x1416BC309ull, // CharaSlotSelectionNumGate
            0x1416BC2C0ull, // CharaSlotSelectionNum
            0x14196F300ull, // UiMbmDataBase_RefreshPrefabList
            0x14196E340ull, // UiMbmDataBase_RefreshDataBaseNameText
            0x14196E200ull, // UiMbmDataBase_RefreshDataBaseIcon
            0x14196CE80ull, // UiMbmDataBase_GetDataBaseInfoText
            0x14196E960ull, // UiMbmDataBase_RefreshInfoLayout
            0x14196E780ull, // UiMbmDataBase_RefreshImageRecord
            0x14196E0F0ull, // UiMbmDataBase_RefreshCollectionRate
            0x140FE61C0ull, // SkillAndItemParameterSystem_GetSuitParam
            0x140F1BF40ull, // MbDvcMapCallbackIconImpl_DispOnNewMarkerIcon
            0x140F1B8E0ull, // MbDvcMapCallbackIconImpl_DispOnImportantIcon
            0x140F1AE40ull, // MbDvcMapCallbackIconImpl_DispOnGoalIcon
            0x140F1ABE0ull, // MbDvcMapCallbackIconImpl_DispOnGeneralIcon
            0x140F1BA10ull, // MbDvcMapCallbackIconImpl_DispOnTargetMarkerIcon
            0x140F1A080ull, // MbDvcMapCallbackIconImpl_DispOnAreaIcon
            0x140F22100ull, // MbDvcMapCallbackIconImpl_GetIconDisplayParam
            0x141E02AE0ull, // PlayerInfo_GetLocalPartsType
            0x141E02290ull, // PlayerInfo_CheckStatusBit
            0x1409CCECFull, // UpdatePartsStatus_QuietCamoStrip
            0x140956410ull, // MissionPrepSystem_GetPlayerNameFromPlayerTypeAndStaffId
            0x1462708E0ull, // Player2BlockController_ApplyQuietSuitShaderParams
            0x140FE7C40ull, // UiControllerImpl_GetUiSightTypeBySightId
            0x1408ED460ull, // SightCommonFactory_CreateWindow
            0x1408EDE40ull, // SightCommonFactory_DeleteWindow
            0x14196F250ull, // UiMbmDataBase_RefreshNewIcon
            0x14196FCC0ull, // UiMbmDataBase_SetPutCursorFlag
            0x14196DF30ull, // UiMbmDataBase_OnStop
            0x140F5A8C0ull, // MbmImpl_CalcDocumentationUnreadCount
            0x140F5AA90ull, // MbmImpl_CalcEncyclopediaUnreadCount
            0x141E0AFB0ull, // Net_ServerManagerBufferRelease
            0x1414E20A0ull, // NoticeControllerImpl_CheckSightNoticePlayer
            0x14C88F650ull, // DemoPlayback_OnPlayingAfterUpdateStream
            0x14C886DF0ull, // DemoPlayback_DoFinish
            0x14C887060ull, // DemoPlayback_DoInterrupt
            0x140975710ull, // UpdateMusicPlayer
            0x146C96F50ull, // GameObject_GetGameObjectIdWithName
            0x14124AA10ull, // Vehicle_GetConnectPointWorldMatrix
            0x14127A440ull, // Player_SequentialDemoActionExecute
            0x14127A880ull, // Player_SequentialDemoActionOnSignal
            0x140BF9E80ull, // Fox_GameObjectManager_GetSharedInstance
            0x14229B7A0ull, // GameObjectType_Horse2_Vtable
            0x1422C54C0ull, // GameObjectType_Heli2_Vtable
            0x1422C4AA0ull, // GameObjectType_HeliAlt_Vtable
            0x1422CA6B0ull, // GameObjectType_WalkerGear2_Vtable
            0x1422E5CF0ull, // AnimationControllerImpl_Vtable
            0x1422FB080ull, // HeliAnimationControllerImpl_Vtable
            0x147413A00ull, // MotionLoaderImpl_GetMtarIds
            0x147412E00ull, // MotionLoaderImpl_GetMtarFilePath
            0x147411E30ull, // MotionLoaderImpl_GetBlockPackagePath
            0x141049400ull, // AttackAction_GetGunMotionIdFromTable
            0x141046620ull, // AttackAction_GetEquipMotionIdFromTable
            0x14104C110ull, // AttackAction_GetNormalGripMotionId
            0x14104C2F0ull, // AttackAction_GetNormalMagazineGripMotionId
            0x141043EE0ull, // AttackAction_GetAssaultGripMotionId
            0x1410496B0ull, // AttackAction_GetHandgunGripMotionId
            0x141050DC0ull, // AttackAction_GetSubMachinegunGripMotionId
            0x14104FF80ull, // AttackAction_GetSniperRifleGripMotionId
            0x14104FC70ull, // AttackAction_GetShotgunGripMotionId
            0x14104BAD0ull, // AttackAction_GetMachinegunGripMotionId
            0x141046A70ull, // AttackAction_GetGrenadeLauncherGripMotionId
            0x14104BDF0ull, // AttackAction_GetMissileGripMotionId
            0x1410441E0ull, // AttackAction_GetAssaultUnderBarrelGripMotionId
            0x141049B50ull, // AttackAction_GetHandgunOneHandGripMotionId
            0x141049CA0ull, // AttackAction_GetHandgunOneHandMotionId
            0x141050FD0ull, // AttackAction_GetSubMachinegunOneHandMotionId
            0x141050640ull, // AttackAction_GetStepReloadHandgunOneHandMotionId
            0x1410504D0ull, // AttackAction_GetStepReloadHandgunMotionId
            0x1410567E0ull, // AttackAction_ResetMissionPrepareGripMotion
            0x14104C450ull, // AttackAction_GetNormalMotionIdByCategory
            0x141043F30ull, // AttackAction_GetAssaultMotionIdTable
            0x1410497A0ull, // AttackAction_GetHandgunMotionIdTable
            0x141050E00ull, // AttackAction_GetSubMachinegunMotionIdTable
            0x141050050ull, // AttackAction_GetSniperRifleMotionIdTable
            0x14104FD10ull, // AttackAction_GetShotgunMotionIdTable
            0x141050720ull, // AttackAction_GetShotgunStepReloadMotionIdTable
            0x14104BB10ull, // AttackAction_GetMachinegunMotionIdTable
            0x141046BA0ull, // AttackAction_GetGrenadeLauncherMotionIdTable
            0x14104BE40ull, // AttackAction_GetMissileMotionIdTable
            0x141051C70ull, // AttackAction_GetChaseMotionIdTable
            0x1463B24B0ull, // EquipMotionDataTable_ReloadEquipMotionData2
            0x1416C363Dull, // MissionPreparationCallbackImpl_Start_PlayBgmEvent
            0x1416C6AA9ull, // MissionPreparationCallbackImpl_Update_StopBgmEvent
            0x1416C6DCEull, // MissionPreparationCallbackImpl_Update_StopBgmEvent2
            0x1416C33C0ull, // MissionPreparationCallbackImpl_Start
            0x1416C3F30ull, // MissionPreparationCallbackImpl_Update
            0x142c1d990ull, // Soldier2FaceSystem_Instance
            0x140F812E0ull, // StaffControllerImpl_AddStaffShort
            0x140F81620ull, // StaffControllerImpl_AddStaffFull
            0x146756CA0ull, // tpp::mbm::impl::StaffIdToFaceId
            0x140F97E40ull, // tpp::mbm::impl::StaffControllerImpl::StaffSeedToFaceTexturePathId
            0x14A5BDC40ull, // tpp::ui::menu::mbm::impl::StaffListCallbackImpl::RefreshFaceRecord
            0x141DEF540ull, // fox::ui::WindowFactoryCollector::FindWindowFactory
            0x141DAB8B0ull, // fox::ui::WindowFunction::CreateWindow
            0x140867C00ull, // tpp::ui::hud::CommonDataManager::SetSubjectiveWindowHandle
            0x14D94C970ull, // fox::ui::WindowFactoryCollector::FindWindowFactory (implementation reached through thunk)
            0x145C1F580ull, // tpp::ui::hud::CommonDataManager::SetSubjectiveWindowHandle (implementation reached through thunk)
            0x1409ECF3Dull, // CalcFv2ResourceManagerInfoForFace
            0x1409ED7D1ull, // ConvertExtendFaceFova2PathToFovaFile
            0x1409EDDE5ull, // ConvertFova2PathToFovaFileForProg
            0x14631B577ull, // GetFaceFovaFpkPathIdArrayAtFaceId
            0x14631B899ull, // GetFaceFovaFpkPathIdAtFaceId
            0x14631BCD7ull, // GetFaceFovaPathIdArrayAtFaceId
            0x14631C119ull, // GetFaceFovaPathIdAtFaceId
            0x1409F0405ull, // GetFaceFpkFileCodeListImpl
            0x1409F05F4ull, // GetFaceFpkFileCodeListImpl
            0x14631EFAEull, // GetHairFovaPathIdAtFaceId
            0x1409F3003ull, // PushFaceFovaFpk
            0x1409F4060ull, // SetFaceFovaDefinitionTableImpl
            0x1409F4890ull, // SetFovaFileTable
            0x140A3B330ull, // EquipParameterTablesImpl::GetAmmoParameterBlock
            0x1466966F0ull, // WalkerGearSystemImpl::GetSubWeaponLoadAmmoMax
            0x1471C0350ull, // BuddyQuietEquipControllerImpl::SetupCurrentSlot
            0x149191CC0ull, // HostageEquipControllerImpl::SetupCurrentSlot
            0x14A1170F0ull, // SoldierEquipControllerImpl::SetupCurrentSlot
            0x14B716050ull, // BossQuietEquipControllerImpl::SetupCurrentSlot
            0x140F665E0ull, // EquipDevelopControllerImpl::GetBulletCountMagazine
            0x140F66710ull, // EquipDevelopControllerImpl::GetBulletCountMax
            0x1495021D0ull, // EquipDevelopControllerImpl::GetBulletType
            0x140DC368Full, // EquipSystemImpl::SetUpGunInfoFromGunPartsDesc (receiver)
            0x140DC3884ull, // EquipSystemImpl::SetUpGunInfoFromGunPartsDesc (barrel)
            0x140DC3A5Aull, // EquipSystemImpl::SetUpGunInfoFromGunPartsDesc (magazine)
            0x140DC3A01ull, // EquipSystemImpl::SetUpGunInfoFromGunPartsDesc (stock)
            0x140DC3A9Full, // EquipSystemImpl::SetUpGunInfoFromGunPartsDesc (muzzleOption)
            0x140DC3BFCull, // EquipSystemImpl::SetUpGunInfoFromGunPartsDesc (sight)
            0x140DC3C80ull, // EquipSystemImpl::SetUpGunInfoFromGunPartsDesc (sight2)
            0x140DC3B11ull, // EquipSystemImpl::SetUpGunInfoFromGunPartsDesc (laserFlash1)
            0x140DC3B6Full, // EquipSystemImpl::SetUpGunInfoFromGunPartsDesc (laserFlash2)
            0x140DC3CD0ull, // EquipSystemImpl::SetUpGunInfoFromGunPartsDesc (underBarrel)
            0x140DC3D56ull, // EquipSystemImpl::SetUpGunInfoFromGunPartsDesc (underBarrelCmp)
            0x140EE93C0ull, // InventoryImpl::CalcAmmoAndStockIfPickUp
            0x14649F310ull, // EquipIdTableImpl::GetRootParameterId
            0x1494F3520ull, // DeployControllerImpl::GenerateDeployMission
            0x1494EB510ull, // DeployControllerImpl::AddLogMission
            0x1494F3CC0ull, // DeployControllerImpl::GenerateDeployMissionInfoOffline
            0x1494F13B0ull, // DeployControllerImpl::CreateDeployMissionSeedParam
            0x140F63030ull, // DeployControllerImpl::GetDeployMissionNameMessageId
            0x140F62AB0ull, // DeployControllerImpl::GetDeployMissionCaptionMessageId
            0x1494F5420ull, // DeployControllerImpl::GetDeployMissionNameKeyId
            0x140F64030ull, // DeployControllerImpl::RefreshMissionList
            0x1494F3230ull, // DeployControllerImpl::GetNextRandomMissionId
            0x140F61120ull, // DeployControllerImpl::CompletedDeployMissionOffline
            0x140AB0740ull, // MotherBaseManagementSystemImpl::SetupAfterRestoreFromSVars
            0x140F776A0ull, // PoolRewardControllerImpl::GeneratePoolRewardsInfo
            0x14A4B2000ull, // CombatDeployCallbackImpl::RefreshMissionIconInGlobe
            0x1413dce80ull, // NoticeAntiBuddyWalkerGearAiImpl::ShouldSleep
            0x149fc3600ull, // CombatAntiBuddyWalkerGearAiImpl::UpdatePrimaryTarget
            0x14138ed00ull, // AttackActionImpl::HasOtherSoldierOnVec
            0x140d31280ull, // Bullet3Impl::OnOffenseHit
            0x14145f880ull, // soldier DamageHandlerImpl::ProcessDamages
            0x14136E110ull, // Soldier2Impl::UpdateMarkers
            0x141644A20ull, // CombatDeployMissionListRecordCallFunc::Refresh
            0x14164AC10ull, // SetRevengeIcon
            0x1413e0260ull, // CombatAntiBuddyWalkerGearAiImpl::StepSurprise
            0x143F68B80ull, // AK::MemoryMgr::Malloc
            0x143F68840ull, // AK::MemoryMgr::Malign
            0x143F66C00ull, // AK::MemoryMgr::Free
            0x142B9EC18ull, // AK::MemoryMgr::s_pMemPools
            0x142A0223Cull, // g_DefaultPoolId
            0x142B9EB1Cull, // g_PrepareEventMemoryPoolID
            0x149f7d6b0ull, // Soldier2Util::SearchActiveMember
            0x1413e15f0ull, // CombatAntiBuddyWalkerGearAiImpl::UpdateSnipeTarget
            0x1415a78f0ull, // WalkerGearActionImpl::HasOtherSoldierOnVec
            0x141647310ull, // CombatDeployCallbackImpl::RefreshMissionList
            0x14086E630ull, // MbTopMenuSelectorEv::UpdateMenuNotice
            0x1414df3f0ull, // NoticeControllerImpl::CheckNoiseNoticeSoldier
            0x14a254b20ull, // NoticeDownComradeAiImpl::ShouldSleep
            0x14a379f60ull, // VehicleNoticeDownComradeAiImpl::ShouldSleep
            0x14155C710ull, // RideVehicleActionImpl::HasOtherSoldierOnVec
            0x1414AABE0ull, // LifeControllerImpl::SendStatusMessage
            0x149FBF0E0ull, // CombatAntiBuddyWalkerGearAiImpl::CheckGrenade
            0x1411FC660ull, // IDroidActionPluginImpl::CloseState
            0x1411FBE00ull, // IDroidActionPluginImpl::Deactivate
            0x140DC41C0ull, // EquipSystemImpl::SetItemVisible
            0x1411FA830ull, // IDroidActionPluginImpl::IsPutAwayInterrupted
            0x1411FB600ull, // IDroidActionPluginImpl::IsOtherAvatar
            0x14D178330ull, // MotionGraphBlendNodeTraverser leaf clip-id read
            0x141A6C590ull, // AnimControl::GetAnimFileFromGaniPath
            0x141002B00ull, // player action plugin GetStance (1 stand, 2 squat, 3 crawl, 4 supine)
            0x141181E03ull, // CureActionPluginImpl::StateCureStart GetStance call
            0x141181EBDull, // CureActionPluginImpl::StateCureStart GetStance call
            0x141181719ull, // CureActionPluginImpl::StateCure GetStance call
            0x141181A39ull, // CureActionPluginImpl::StateCureEnd body GetStance call
            0x1410A2050ull, // BasicActionImpl::SelectWakeupState
            0x141180E10ull, // CureActionPluginImpl::FinishCureSprayEffect
            0x1411815A0ull, // CureActionPluginImpl::StateCure
            0x14110FEC0ull, // CallMenuManagerImpl::UpdateCommandSetInterrogation
            0x1413621C0ull, // Soldier2Impl::ProcessSignal
            0x1415135A0ull, // NoticeNoiseAiImpl::SearchSpecificComrade
            0x1414DDFE0ull, // NoticeControllerImpl::CheckForceNotice
            0x149826D90ull, // CallMenuManagerImpl::UpdateInShowing
            0x141110A80ull, // CallMenuCommandSet::UpdateState
            0x141110370ull, // CallMenuManagerImpl::UpdateCommandSetRescueBoy
            0x141369BB0ull, // soldier::ActionController::SetCommand
            0x141520C40ull, // NoticeNoiseAiImpl::StepMovePause
            0x1491730D0ull, // hostage CoreAiImpl::SetSpecialActionParam
            0x140EDB5C0ull, // hostage SpecialActionImpl::State_SpecialIdle
            0x14A903970ull, // ui::hud::CallMenuImpl::UpdateOption
            0x14A903C90ull, // ui::hud::CallMenuImpl::UpdateSelectDoc
            0x1498213D0ull, // CallMenuManagerImpl::ShowCallMenu
            0x142B743B0ull, // fox::gr::dg::MaterialManager::s_Instance (data)
            0x142B743A8ull, // fox::gr::dg::ShaderTechniqueManager::s_Instance (data)
            0x1473CCB80ull, // cp::RadioControllerImpl::StepRadioNoResponse
            0x1473CD460ull, // cp::RadioControllerImpl::StepRadioWaitResponse
            0x140D58370ull, // cp::CommandPost2Impl::Controller_RequestRadioByMember_
            0x14729D4E0ull, // cp::CommandPost2Impl::Controller_SetRadioMemberReady_
            0x147299F30ull, // cp::CommandPost2Impl::Controller_ReleaseRadioByMember_
            0x140D5B230ull, // cp::CommandPost2Impl::FinishRadio
            0x140DA1D30ull, // cp::RadioControllerImpl::StepRadio
            0x140D58BB0ull, // cp::CommandPost2Impl::Controller_UpdateRadio_
            0x140D627A0ull, // cp::CommandPost2Impl::RequestRadioByCp
            0x1473CC750ull, // cp::RadioControllerImpl::StepIdle
            0x140D58D20ull, // tpp::gm::CpRadioService::ConvertSpeechLabelToRadioType
            0x146D1C6C0ull, // tpp::gm::impl::AiControllerImpl::WakeNode
            0x146D1BF60ull, // tpp::gm::impl::AiControllerImpl::SleepNode
            0x149F41220ull, // tpp::gm::soldier::Soldier2InterrogateUtil::IsTranslatable (Arxan-moved body)
            0x140A461D0ull, // tpp::gm::buddy::impl::Buddy2BlockControllerImpl::GetPathAtBuddyType
            0x1464D5DC0ull, // tpp::gm::buddy::impl::Buddy2BlockControllerImpl::GetBuddyQuietWeaponFileFromBuddyBlock
            0x1464D56D0ull, // tpp::gm::buddy::impl::Buddy2BlockControllerImpl::GetBuddyQuietFovaFile
            0x1464D59A9ull, // tpp::gm::buddy::impl::Buddy2BlockControllerImpl::GetBuddyQuietFv2FileFromBuddyBlock (fv2 id load)
            0x140F664B0ull, // EquipDevelopControllerImpl::GetBuddyPartsDevelopInfoIndex
            0x149501260ull, // EquipDevelopControllerImpl::GetBuddyPartsId
            0x149501470ull, // EquipDevelopControllerImpl::GetBuddyPartsKind
            0x142C29010ull, // tpp::gm::buddy::impl::g_buddyService (BuddyServiceImpl instance)
            0x140A4E6D0ull, // tpp::gm::buddy::impl::BuddyServiceImpl::ChangeHorseSettings
            0x1464F8F20ull, // tpp::gm::buddy::impl::BuddyServiceImpl::ChangeDogSettings
            0x14650ABE0ull, // tpp::gm::buddy::impl::BuddyServiceImpl::GetQuietEquipIdByWeaponType
            0x146514E70ull, // tpp::gm::buddy::impl::BuddyServiceImpl::IsUseSuppressorByWeaponType
            0x1464F956Dull, // tpp::gm::buddy::impl::BuddyServiceImpl::ChangeQuietCostume (costume clamp)
            0x140A54847ull, // TppBuddyService.SetVarsQuietCostume (costume clamp)
            0x1464FA151ull, // tpp::gm::buddy::impl::BuddyServiceImpl::ChangeQuietWeaponType (weapon clamp)
            0x140A548C2ull, // TppBuddyService.SetVarsQuietWeaponType (weapon clamp)
            0x140A4E74Cull, // tpp::gm::buddy::impl::BuddyServiceImpl::ChangeHorseSettings (tack check)
            0x1464F8F69ull, // tpp::gm::buddy::impl::BuddyServiceImpl::ChangeDogSettings (gear check)
            0x140CE3918ull, // tpp::gm::buddyquiet::impl::BuddyQuiet2Impl weapon change command (weapon clamp)
            0x140CE45A3ull, // tpp::gm::buddyquiet::impl::BuddyQuiet2Impl::SetBruise (costume check)
            0x1416ABD67ull, // tpp::ui::menu::impl::ItemSelectorCallbackImpl::SetupPrefabListElement (Quiet costume list gate)
            0x1416AB8B7ull, // tpp::ui::menu::impl::ItemSelectorCallbackImpl::SetupPrefabListElement (Quiet weapon list gate)
            0x141459E2Bull, // tpp::gm::soldier::impl::DamageAiImpl::StepBitDamage (dog gear check)
            0x140D04B80ull, // tpp::gm::buddyquiet::impl::(anon)::CombatAiImpl::SendFireSignal
            0x1471BF840ull, // tpp::gm::buddyquiet::impl::EquipControllerImpl::InitCurrentSlot
            0x140D18770ull, // tpp::gm::buddyquiet::impl::EquipControllerImpl::Update
            0x1471BFCD0ull, // tpp::gm::buddyquiet::impl::EquipControllerImpl::SetMotion
            0x1464D6740ull, // tpp::gm::buddy::Buddy2BlockController::GetFileForPathFromBuddyBlock
            0x142C28910ull, // tpp::gm::buddy::Buddy2BlockController instance pointer
            0x140E51490ull, // tpp::gm::horse::impl::ActionCoreImpl::ExecDefenseCallback tack damage switch
            0x1490BC088ull, // tpp::gm::horse::impl::BasicAction2Impl::UpdateGallopMotionSpeed tack switch
            0x1490AF9E2ull, // tpp::gm::horse::impl::BasicAction2Impl::EnterOffenseTarget tack switch
            0x140E7C24Bull, // horse motion sound tack switch (FUN_140E7BE70)
            0x140E58C20ull, // tpp::gm::horse::impl::BasicAction2Impl::Execute
            0x14650BB8Aull, // tpp::gm::buddy::impl::BuddyServiceImpl D-Dog order variant gear check (FUN_140A515B0)
            0x14650BF04ull, // tpp::gm::buddy::impl::BuddyServiceImpl::InitBuddyCommandForStatus gear check
            0x140C89707ull, // tpp::gm::buddydog::impl::DamageHandlerImpl::ProcessDamages gear check
            0x146EE53B0ull, // tpp::gm::buddydog::impl::DamageHandlerImpl::IsNeedReaction gear check
            0x140C886E6ull, // tpp::gm::buddydog::impl::DamageHandlerImpl::CallDamageEffect gear check
            0x0ull,         // tpp::gm::buddydog::impl::DamageHandlerImpl::CallDamageSound gear check (not ported)
            0x140C8F57Eull, // tpp::gm::buddydog::impl::EscapeIdleAiImpl::StepSearch gear check
            0x0ull,         // tpp::gm::buddydog::impl::NoticeDamageAiImpl::Setup gear check (not ported)
            0x140C63DB3ull, // tpp::gm::buddydog::impl::AttackAiImpl::ReTarget gear check
            0x1414CFE15ull, // tpp::gm::soldier::impl::NoticeBuddyDogAiImpl::StepAware gear check
            0x140C56471ull, // tpp::gm::buddydog::impl::BuddyDog2Impl::Initialize gear copy to +0xF4
            0x0ull,         // tpp::gm::impl::SimplePartsEffectControllerImpl::CreateEffectImpl (not ported)
            0x0ull,         // fox::fx::Effect::Create (not ported)
            0x142A82C28ull, // tpp::gm::player Player2System instance
            0x14C6A08E0ull, // fox::anim::AnimFile::GetEventData
            0x149D78060ull, // tpp::gm::impl::equip::RealizedEquipObjectImpl::MotionEventCallbackImpl
            0x141304890ull, // tpp::gm::impl::equip::RealizedEquipObjectImpl::EjectCasing
            0x149744960ull, // tpp::gm::player::impl::attack::AttackActionImpl::SetEquipMotion
            0x149739BA0ull, // tpp::gm::player::impl::attack::AttackActionImpl::MotionEventCallback
            0x140A1C900ull, // tpp::gm::impl::(anon)::GetPackagePaths
            0x1431FCE20ull, // fox::Array<fox::Path>::PushBack
            0x14319E2B0ull, // fox::Path::Path(const char*)
            0x14C6AF100ull, // fox::anim::EventCallback::ExecCallback
            0x1416ADA20ull, // tpp::ui::menu::impl::ItemSelectorCallbackImpl::Update
            0x146231F10ull, // tpp::gm::player::impl::Player2BlockController::GetFileFromPartsBlockNew
            0x142C1B0D8ull, // the Player2BlockController pointer it is called with
            0x144A3BDB0ull, // fox::FormVariationFile2::ApplyFormVariation (ModelBuilder)
            0x0ull,         // tpp::gm::player::impl::Fova2ControllerImpl work: type 0x11 extra after an fv2 apply (not ported)
            0x140A29FE0ull, // tpp::gm::impl::equip::EquipIdTableImpl::GetSupportWeaponTypeId
            0x140A41BA0ull, // tpp::gm::impl::equip::EquipParameterTablesImpl::ReloadEquipParameterTablesImpl
            0x140A41CD5ull, // ReloadEquipParameterTablesImpl: add rax, 0x2D8 (SupportWeaponParameter size pass)
            0x140A41CEEull, // ReloadEquipParameterTablesImpl: lea rax, [rcx+0x2D8] (allocation pass)
            0x1415D6D20ull, // tpp::gm::impl::throwing::ThrowingImpl::GetBlastParamByEquipId
            0x1415DA720ull, // tpp::gm::impl::throwing::ThrowingImpl::UpdateActionGrenade (grenade and stun)
            0x1415DAF50ull, // tpp::gm::impl::throwing::ThrowingImpl::UpdateActionSmoke (smoke, sleeping gas, flares)
            0x1415DAD00ull, // tpp::gm::impl::throwing::ThrowingImpl::UpdateActionMolotovCocktail
            0x14A3CE1A0ull, // tpp::gm::impl::throwing::ThrowingImpl::UpdateActionDecoy
            0x1415DAA50ull, // tpp::gm::impl::throwing::ThrowingImpl::UpdateActionKibidango
            0x14A3CE5E0ull, // tpp::gm::impl::throwing::ThrowingImpl fake sign activation
            0x14A3D0200ull, // tpp::gm::impl::throwing::ThrowingImpl::UpdateStatus
            0x14A3D3F00ull, // tpp::gm::impl::throwing::ThrowingSystemImpl::ExecuteSerially
            0x1415D3450ull, // tpp::gm::impl::throwing::ThrowingImpl::ActivateThrowingAtEmptyWork
            0x14A3CED80ull, // tpp::gm::impl::throwing::ThrowingImpl::UpdateDeactivation
            0x1415D3F50ull, // tpp::gm::impl::throwing::ThrowingImpl::AddReboundNoise
            0x1415D9600ull, // tpp::gm::impl::throwing::ThrowingImpl::PostReboundSound
            0x14A3CBA90ull, // tpp::gm::impl::throwing::ThrowingImpl::IsObstructSight
            0x140F670C0ull, // tpp::mbm::impl::EquipDevelopControllerImpl::GetDamageType
            0x1411AE310ull, // tpp::gm::player::impl::EquipControllerImpl::InitializePlayerAtIndex
            0x1492E3000ull, // tpp::gm::impl::LoadoutManagerImpl::RestoreLoadoutInfosFromVars
            0x1492E3C30ull, // tpp::gm::impl::LoadoutManagerImpl sortie loadout slot restore
            0x1492E4FC0ull, // tpp::gm::impl::LoadoutManagerImpl return-heli loadout slot restore
            0x140FCE680ull, // tpp::gm::impl::placed::PlacedSystemImpl::RestorationPlace
            0x140ACA230ull, // tpp::gm::impl::SupplyCboxGameObjectImpl::RestoreRequestFromSVars
            0x1409D1ED0ull, // tpp::gm::player::impl::Player2UtilityImpl::GetPickableInfosOfAmmoBox
            0x140A3C980ull, // tpp::gm::impl::equip::EquipParameterTablesImpl::GetSupportWeaponParameterBlock
            0x149504D80ull, // tpp::mbm::impl::EquipDevelopControllerImpl::GetDamageAmountRatio
            0x14166C3F0ull, // tpp::ui::menu::mbm::impl::EquipDetailsCallbackImpl::SetupSupportParameterDetails (bar panel caller range)
            0x141484C60ull, // tpp::gm::soldier::impl::IdleActingActionImpl::CallVoice
            0x14A311030ull, // tpp::gm::soldier::impl::Soldier2SoundControllerImpl::__callVoiceWithoutSync
            0x14158D2E0ull, // tpp::gm::soldier::impl::Soldier2SoundControllerImpl::__callVoiceWithSync
            0x1415507A0ull, // tpp::gm::soldier::impl::RadioActionImpl::State_RadioChaffEnd
            0x1460AFD9Cull, // tpp::gm::EquipIdTable::GetAmmoStockIdOfSupportOrItem: lea eax,[rbx-0x50A]
            0x140951BD3ull, // MissionPreparationSystemImpl::CalcEquipWeaponItemCostFromLoadout support loop: lea r9d,[rbx-0x50A]
            0x1492FD877ull, // LoadoutManagerImpl::StoreLoadoutInfoToPlayerVars support cells: add edi,-0x50A
            0x1411AEEF9ull, // EquipControllerImpl::InitializePlayerAtIndex support slots: lea r8d,[rbx-0x50A]
            0x149A99DF2ull, // PickUpActionPluginImpl::CheckLoadableForSupportOrItem: lea r8d,[rbx-0x50A]
            0x14120A24Eull, // PickUpActionPluginImpl::DoPickUpPlaced carried slot: lea eax,[rsi-0x50A]
            0x14120A2F2ull, // PickUpActionPluginImpl::DoPickUpPlaced picked item: lea ecx,[r15-0x50A]
            0x14120ADE4ull, // PickUpActionPluginImpl::DoPickUpWithPickableInfo carried slot: lea ecx,[rsi-0x50A]
            0x14A53EA5Full, // ItemSelectorCallbackImpl::GetCurrentAmmoFromVarsForSupportWeapon: add ebx,-0x50A
            0x140A3B525ull, // EquipParameterTablesImpl::GetAmmoParameterBlock: lea edi,[rbx-0x50A]
            0x140DA2AA0ull, // tpp::gm::impl::cp::RadioSpeechHandlerImpl::CallPart
            0x149D35A50ull, // tpp::gm::impl::RangeAttackSystemImpl::ActivateChaff
            0x140B08540ull, // tpp::gm::impl::SoundControllerImpl::IsPlayingVoice
            0x1414B1A60ull, // tpp::gm::soldier::impl::MortarActionImpl::State_WaitAction
            0x1414B1BF0ull, // tpp::gm::soldier::impl::UnrealMortarActionImpl::State_WaitAction
            0x147478B80ull, // tpp::gm::heli::impl::Heli2Impl::Unrealize
            0x1414B12B0ull, // tpp::gm::soldier::impl::MortarActionImpl::State_MortarAction
            0x14A20D500ull, // tpp::gm::soldier::impl::UnrealMortarActionImpl::State_MortarAction
            0x140DA1FD0ull, // tpp::gm::impl::cp::RadioControllerImpl::StepRadioChaff
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
            0x1431CC520ull, // FoxLuaRegisterLibrary
            0x143227D20ull, // FoxPath_Path
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
            0x147DEAC30ull, // PauseMusicPlayer
            0x140EF6D40ull, // PlayOrPauseSelectedTrack
            0x140A68B60ull, // RequestCorpse
            0x1409734D0ull, // ResumeMusicPlayer
            0x147A8C170ull, // SetEquipBackgroundTexture
            0x1408D72B0ull, // SetLuaFunctions
            0x141DC7930ull, // SetTextureName
            0x140988C00ull, // SoundSystemCtor
            0x14ACA5E60ull, // StateRadioRequest
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
            0x14B7801D0ull, // Sahelan_ActionCoreImpl_UpdateEyeLampColor
            0x14B7807B0ull, // Sahelan_ActionCoreImpl_UpdateHeartLight
            0x14B84DEC0ull, // Sahelan_PhaseSneakAi_PushEyeColor
            0x140EF3050ull, // MbDvcAnnouncePopupCallbackImpl_UpdateAnnounceNormal
            0x140EF3410ull, // MbDvcAnnouncePopupCallbackImpl_UpdateAnnounceServer
            0x140EF2E30ull, // MbDvcAnnouncePopupCallbackImpl_UpdateAnnounceEmergency
            0x147719930ull, // HudCommonDataManager_GetInstance
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
            0x1416214bbull, // IconTitleGetLangTextCall
            0x148b0e440ull, // GameObject_SendCommand
            0x14A10B530ull, // UiControllerImpl_InitEquipHudData
            0x1414E60E0ull, // NoticeControllerImpl_GetOccasionalChat
            0x140D833D0ull, // SoldierConversationService_ConvertSpeechLabelToConversationType
            0x1414e61f9ull, // OccasionalChat_FactionTestNop
            0x1409325E0ull, // MbDvcReserveAnnouncePopup
            0x140915150ull, // MbDvcPopupGateFn
            0x14151FE00ull, // NoticeNoiseAiImpl_StepAware
            0x141513AD0ull, // NoticeIndisAiImpl_StepAware
            0x1414E5800ull, // NoticeControllerImpl_DoCheckSpreadNotice
            0x1414E3040ull, // NoticeControllerImpl_CheckSightNoticeSoldier
            0x140E46700ull, // NoticeNearThreatAiImpl_Wake
            0x140E470D0ull, // NoticeNoiseAlertAiImpl_Wake
            0x14B23EB90ull, // NoticeNearGameObjectAiImpl_Wolf_Wake
            0x14B243340ull, // NoticeNoiseSneakAiImpl_Wolf_Wake
            0x148C72F50ull, // NoticeNearGameObjectAiImpl_Bear_Wake
            0x140C36760ull, // NoticeNoiseAiImpl_Bear_Wake
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
            0x140E0A3A0ull, // HeliTaxi_StepTaxiCurrentCluster
            0x14973F220ull, // HeliTaxi_PassengerUpdate
            0x140E14470ull, // MechaActionImpl_StateOff
            0x140E148B0ull, // MechaActionImpl_StateOn
            0x14973C110ull, // PassengerControllerImpl_IsPassengerClosingDoor
            0x14804A500ull, // PlacedSystemImpl_BindResource
            0x140935170ull, // UiMarkerCommonDataImpl_RegisterLZMarkerInUpdate
            0x140E24390ull, // HeliSoundControllerImpl_Update
            0x14975E670ull, // HeliSoundControllerImpl_CallVoice
            0x140DfD310ull, // HeliFlightControllerImpl_Update
            0x147718790ull, // Hud_GetAnnounceLogSE
            0x1408a29c0ull, // Hud_TypingLogActUpdate
            0x141DC9F10ull, // Ui_SoundControlStart
            0x1476cda70ull, // Ui_UiCommonDataManagerGetInstance
            0x1404b2220ull, // Ui_EventNodeBodyGetGraphState
            0x147b92a30ull, // VoiceParam_PlayDialogue
            0x14a4979a0ull, // RideHeliActionPluginImpl_ExecPreMotionGraph
            0x14121ecb0ull, // RideHeliActionPluginImpl_GetStateFn
            0x1411fb470ull, // MBTerminalActionPluginImpl_IsDisableMBTerminal
            0x14110f1c0ull, // CallMenuManagerImpl_Update
            0x1411fa770ull, // MBTerminalActionPluginImpl_CheckActionState
            0x142bff960ull, // SoundDaemon_Instance
            0x147ddeb40ull, // SoundDaemon_PostEventQueue
            0x1420a72b0ull, // Sd_kW_Select
            0x140d7c720ull, // UpdateAntiAir
            0x1494bfe40ull, // ClearAntiAir
            0x147b88d00ull, // GetChangeLocationMenuParameterByLocationId
            0x147b897d0ull, // GetMbFreeChangeLocationMenuParameter
            0x140925910ull, // GetPhotoAdditionalTextLangId
            0x140fe7e80ull, // UiControllerImpl_HideBinocle
            0x140A87290ull, // AddCassetteTapeTrack
            0x140EF6540ull, // CollectGotTapes
            0x140A95740ull, // IsGotCassetteTapeTrack
            0x140AAC180ull, // SetCassetteTapeTrackNewFlag
            0x149CD4320ull, // SetCurrentAlbum
            0x140974370ull, // SetupMusicInfos
            0x144EBCCC0ull, // RadioCassette_SearchCasseteInfo
            0x144EBBC20ull, // RadioCassette_GetCassetteMusic
            0x144EBC1E0ull, // RadioCassette_IsGotCassette
            0x144EBBE30ull, // RadioCassette_GetCassetteSaveIndex
            0x145032460ull, // RadioCassette_SdPostEvent
            0x140641AC0ull, // RadioCassette_RadioUpdate
            0x1482D7CB0ull, // AddCassetteTapeTrackByIndex
            0x1451E1DF0ull, // RadioCassette_ActivateUnit
            0x1405D5420ull, // RadioCassette_IsSameSaveIndexFromName
            0x14126C190ull, // SearchLightActionPluginImpl_StateDoorStart
            0x14126B690ull, // SearchLightActionPluginImpl_StateDoorEnd
            0x1403385E0ull, // SoundControl_PostExternalEvent
            0x140989600ull, // MusicPlayerPlayWrapper
            0x147BB2DA0ull, // Barrier_GetItemId
            0x1408B152Dull, // Barrier_EquipItemCallRet
            0x1410007D0ull, // Barrier_Updater
            0x144EC3FF0ull, // Barrier_IsFobMode
            0x1409C35B5ull, // Barrier_LoadGate0
            0x1409C361Full, // Barrier_LoadGate1
            0x1409C3685ull, // Barrier_LoadGate2
            0x140FC3D90ull, // Dm_FireLoop
            0x140A29F50ull, // Dm_Classify
            0x141B18A40ull, // Dm_VfxFactory
            0x140C0BC30ull, // Dm_ComponentFactory
            0x140BFF010ull, // Dm_Alloc
            0x140AF3210ull, // Dm_BackLinkPool
            0x1404ECA60ull, // Dm_OneShot
            0x14A1AE850ull, // AttackActionImpl_IsWeaponNoUseInPlaceAction
            0x148332810ull, // TppMotherBaseManagement_RegCstDev
            0x148333ac0ull, // TppMotherBaseManagement_RegFlwDev
            0x140f6b760ull, // EquipDevCtrl_GetSuitDevelopInfoIndex
            0x140f6a020ull, // EquipDevCtrl_GetFaceEquipDevelopInfoIndex
            0x1416a1bf0ull, // AddListSuit
            0x148508540ull, // CamoufParamInfo_GetCamoufValue
            0x140fdc6a0ull, // CamouflageController_ExecSuitCorrect
            0x1411b0e40ull, // EquipController_SetHandSlotEnabled
            0x141675740ull, // EquipDevelopCallbackImpl_SetSupplyCBoxInfo
            0x14b01a880ull, // IsEnableCurrentSuit
            0x1416a43d0ull, // ItemSelectorCallbackImpl_DecideActMissionPreparationSetEquipMode
            0x1416a5320ull, // ItemSelectorCallbackImpl_DecideActMotherBaseDeviceSupportDropMode
            0x1416a9cd0ull, // ItemSelectorCallbackImpl_SetupPrefabListElement
            0x1416af3c0ull, // ItemSelectorRecordCallFunc_UpdateRecords
            0x14afd6ea0ull, // ItemSelector_AddListBandana
            0x140ae8c30ull, // LoadPlayerBionicArmFpk
            0x140ae8b80ull, // LoadPlayerBionicArmFv2
            0x14844b070ull, // LoadPlayerCamoFpk
            0x14844aea0ull, // LoadPlayerCamoFv2
            0x14844de90ull, // LoadPlayerPartsFpk
            0x14844db10ull, // LoadPlayerPartsParts
            0x14844cde0ull, // LoadPlayerSnakeBlackDiamondFpk
            0x14844c6f0ull, // LoadPlayerSnakeBlackDiamondFv2
            0x140ae8930ull, // LoadPlayerSnakeFaceFpk
            0x140ae8820ull, // LoadPlayerSnakeFaceFv2
            0x147cd7780ull, // MissionPrepSystem_IsEnableHeadOptionSuit
            0x14b01a3f0ull, // MissionPrep_IsEnableCurrentHeadOption
            0x147eeb520ull, // Player2BlockController_LoadPartsNew
            0x1409c57c0ull, // Player2GameObjectImpl_ProcessSignal
            0x147f47c60ull, // Player2Impl_SetUpParts
            0x147f89ce0ull, // Player2UtilityImpl_LoadoutApplyAfterSetSuit
            0x147f876f0ull, // Player2UtilityImpl_SetInitialConditionWithLoadoutInfo
            0x1409dead0ull, // Player2UtilityImpl_SetSuitAndHandConditionWithLoadoutInfo
            0x147edbee0ull, // Player_ConverFaceIdWithFaceEquipId
            0x140ae7ff0ull, // ResourceTable_DoesNeedFaceFova
            0x140ae8040ull, // ResourceTable_DoesNeedFaceFovaForAvatar
            0x1409c4080ull, // Sys_IsArtificialHandEnabled
            0x141e02db0ull, // Sys_IsArtificialHandEnabledForCurrentPlayerType
            0x1409cbe40ull, // UpdatePartsStatus
            0x1416a37c0ull, // ItemSelectorCallbackImpl_DecideActMotherBaseCustomize
            0x1432be400ull, // Fox_Path_Exists
            0x143227e40ull, // Fox_Path_Dtor
            0x14e02b500ull, // PluginFacial_ApplyMotion
            0x140f6d9e0ull, // EquipDevCtrl_IsEquipVisile
            0x149f28b70ull, // EquipDevCtrl_GetHeadBadgeCategory
            0x14afe4630ull, // MissionPrep_SetInitialSelectRecord
            0x1480e0f70ull, // TppEquip_RegisterConstant
            0x141e02960ull, // PlayerInfoInterfaceImpl_GetPartsTypeAtCamoType
            0x142c20fb0ull, // EquipIdTable_InfoList
            0x142a70928ull, // EquipIdTable_TypeWords
            0x1480f8110ull, // EquipIdTable_ReloadEquipIdTable
            0x149f26dd0ull, // EquipDevelopCtrl_GetEquipDevelopIndex
            0x149f44b70ull, // EquipDevelopCtrl_SetEquipUndeveloped
            0x149f32490ull, // EquipDevCtrl_GetSuitCamoType
            0x149f326c0ull, // EquipDevCtrl_GetSuitLevel
            0x140f6d8a0ull, // EquipDevCtrl_IsEquipSuit
            0x149f42df0ull, // EquipDevelopCtrl_SetEquipDeveloped
            0x1412a30a0ull, // SupplyCboxActionPluginImpl_StateInBox
            0x140862ce0ull, // HudCommonDataManager_AnnounceLogView
            0x147a74800ull, // Ui_LangIdToKey
            0x140911d00ull, // Ui_GetLangText
            0x141393B6Eull, // Soldier_ShootOneBullet_GroupMaskCall
            0x140D2D7CDull, // Soldier_ActivateBulletAtEmptyWork_SameArmyJnz
            0x1408A8400ull, // TelopStartTitleEvCall_SetBgTexture
            0x141DAEF70ull, // Layout_GetLayout
            0x141DAEFB0ull, // Layout_GetModel
            0x147CB2BB0ull, // GetCassetteTapeUnreadInfo
            0x140A95BA0ull, // IsNewCassetteTapeTrack
            0x149CCDE50ull, // CassetteMenuCheckNewFlag
            0x149CCD510ull, // CassetteAlbumCheckNewFlag
            0x149CCF740ull, // CassetteCheckUnreadInfo
            0x144868DF0ull, // SubtitlesObjectSendMessage
            0x149f38f70ull, // EquipDevCtrl_IsEquipDevelopable
            0x148446230ull, // LoadAvatarFaceFv2
            0x148446630ull, // LoadAvatarFaceFpk
            0x1409B51B0ull, // AvatarFaceEditUpdate
            0x1448ACC50ull, // Fox_ModelFromHandle
            0x148442AF0ull, // LoadAvatarHeadOptionFv2
            0x148442EF0ull, // LoadAvatarHeadOptionFpk
            0x149F42BE0ull, // EquipDevelopCtrl_SetEnableDevelop
            0x140A40EC0ull, // ReloadEquipParameterTables2
            0x142C25C30ull, // GunBasicParameters2Buffer
            0x142A711F0ull, // EquipParameterTablesImpl_Instance
            0x142349520ull, // MotionLoaderImpl_ReceiverTypeTable
            0x14957D790ull, // MotionLoaderImpl_GetReceiverType
            0x142349508ull, // MotionLoaderImpl_UnderBarrelTypeTable
            0x14957DCC0ull, // MotionLoaderImpl_GetUnderBarrelType
            0x140F6BAF0ull, // EquipDevelopControllerImpl_GetSuppressorAmount
            0x142BDCFE0ull, // DamageParameterTable_Instance
            0x144E87260ull, // DamageParameterTable_ReloadDamageParameter
            0x144E80B60ull, // DamageParameterTable_GetDamageParameter
            0x149F1A710ull, // EquipDevCtrl_GetBaseDevelopId
            0x141679e90ull, // MenuDevelopGrid_FillGrid
            0x141679810ull, // MenuDevelopGrid_CopyGrid
            0x14af88160ull, // MenuDevelopGrid_CountBadge
            0x14167a430ull, // MenuDevelopGrid_FillFlat
            0x14af8ea80ull, // MenuDevelopGrid_CopyFlat
            0x141282ad0ull, // SightManager_UpdateMissileLockOn
            0x140d2f980ull, // Bullet3_DoSimulation
            0x14939a090ull, // Bullet3_ActivateBulletAtEmptyWork
            0x14a506720ull, // SightManager_Update
            0x14a507ed0ull, // SightManager_UpdateMissileLockOnUi
            0x147926060ull, // LockOnReticleFactory_CreateWindow
            0x140A3B0B0ull, // EquipParams_GetAttackIdByEquipId
            0x140dc36c0ull, // EquipSystem_SetUpGunInfoFromGunPartsDesc
            0x141dab8e0ull, // UiWindowFunction_FindWindow
            0x141dabc20ull, // UiWindowFunction_PostShowAndStartMessage
            0x141dab960ull, // UiWindowFunction_GetLayout
            0x1410419a0ull, // AttackAction_Fire
            0x141303b80ull, // EquipObject_DoFire
            0x14132c0d0ull, // Shell_ActivateShellAtEmptyWork
            0x141157570ull, // CorePlugin_UpdateLoadoutRequest
            0x140dc0200ull, // WeaponSystem_DefineWeaponFireSound
            0x142C93480ull, // EquipSystem_ChimeraPartsSetWork
            0x140EFC180ull, // MbDvcUpdateTrackListCallFuncs
            0x140EF77F0ull, // MbDvcRefreshTrackListPrefabParameter
            0x140EF70A0ull, // MbDvcTrackListRecordRefresh
            0x140F31F50ull, // MbDvcMissionListCallbackImpl_ActivatePrefabPopupParameter
            0x149D5F450ull, // MbDvcMissionListCallbackImpl_SetMenuHelp
            0x1480F7B50ull, // Equip_ReloadChimeraPartsInfoTable
            0x142C207A0ull, // Equip_ChimeraPartsPackageInfos
            0x141A75850ull, // Mtar_GetAnimFile
            0x141A76420ull, // Mtar_GetDataInfo
            0x142A6B400ull, // Equip_MotionEntryTable
            0x1422E24F0ull, // SimplePartsControllerImpl_Vtable
            0x140ADCE80ull, // SimplePartsControllerImpl_SetMotionData
            0x140ADCF10ull, // SimplePartsControllerImpl_SetMotionDataByPath
            0x140DBFB50ull, // EquipSystemImpl_HideMagazine
            0x140DBF050ull, // EquipSystemImpl_GetPartsController
            0x140BFEF80ull, // Fox_GetQuarkSystemTable
            0x140071160ull, // Fox_ArrayBaseExtend
            0x1420BCA98ull, // Fox_ArrayOperatorVtbl
            0x141AB7260ull, // Animx_GetControlSize
            0x141AB6C80ull, // Animx_SimpleControlCtorPool
            0x141AB7000ull, // Animx_SimpleControlCtorHeap
            0x141AB6E60ull, // Animx_SimpleControlDtorPool
            0x141AB7070ull, // Animx_SimpleControlDtorHeap
            0x141AB6FF0ull, // Animx_SimpleControlAux
            0x144EB9920ull, // Equip_MotionEntrySlotHookA
            0x144EBAC50ull, // Equip_MotionEntrySlotHookB
            0x1480DE9B0ull, // EquipIdTable_GetEquipTypeId
            0x1414404B0ull, // Soldier2InterrogateUtil_UpdateInterrogation
            0x141441E90ull, // Soldier2InterrogateUtil_UpdateInterrogationMarker
            0x1484DA340ull, // SoundControllerImpl_CallVoice
            0x14961D670ull, // EquipSystem_GetGunInfoById
            0x147C34E20ull, // Reticle_InitHandGunAsset
            0x14A630E60ull, // TimeCigaretteActionPluginImpl_HideTimeCigaretteUi
            0x1431f89d0ull, // Fox_EntityInfoMapTeardown
            0x142AC6D10ull, // WeaponEnhanceLangIdArray
            0x14A0267B0ull, // WeaponEnhance_GetIndexForRegist
            0x140fa0350ull, // WeaponEnhance_GetMessageIdArray
            0x14A026A20ull, // WeaponEnhance_GetLangIdById
            0x14A026BE0ull, // WeaponEnhance_GetLangIdBound
            0x140b3b210ull, // BulletEffectController_CreateEffect
            0x1416A22B0ull, // ItemSelectorCallbackImpl_AddRecord
            0x140AE3780ull,           // AdditionalMotionTable_GetMtarPathId
            0x1416A3540ull, // ItemSelector_AddDevelopWeaponList
            0x1409B6A20ull,           // BlockControllerImpl_UpdateAdditionalMotionBlock
            0x14006D570ull,           // Fox_Block_GetFileForPathId
            0x14006D360ull,           // Fox_BlockGroup_GetBlockAtIndex
            0x1416AD6B0ull, // ItemSelector_StartEquipPreviewImpl
            0x141682EA0ull, // UiEquipPreviewController_ScrollNext
            0x141682CF0ull, // UiEquipPreviewController_ScrollPrev
            0x147F20CD0ull, // Player2Impl_AddAdditionalMtarAll
            0x147F3FE80ull, // Player2Impl_RemoveAdditionalMtarAll
            0x142349610ull, // MotionLoaderImpl_BarrelTypeTable
            0x14957B690ull, // MotionLoaderImpl_GetBarrelType
            0x142349690ull, // MotionLoaderImpl_MagazineTypeTable
            0x14957C9A0ull, // MotionLoaderImpl_GetMagazineType
            0x142349750ull, // MotionLoaderImpl_StockTypeTable
            0x14957D970ull, // MotionLoaderImpl_GetStockType
            0x14120B135ull, // TppPickable_ItemWindowBoundSite
            0x140914220ull, // UiUtility_GetWeaponItemNameLangId
            0x140983615ull, // SoundPlayerAnimEvent_StepNoiseQuietCmp
            0x140A0A960ull, // CollectionLocatorArray_SetUpLinearAccessor
            0x140D4F8A0ull, // CollectionSystemImpl_PickUp
            0x140D50BA0ull, // CollectionSystemImpl_RepopCountOperation
            0x140D4D250ull, // CollectionSystemImpl_GetCollectionInfo
            0x140D4E1C0ull, // Collection_GetModelFilePath
            0x140D4D000ull, // Collection_GetCatchEffectColorAndScale
            0x140D4DD50ull, // Collection_GetGroundEffectSize
            0x141209DE0ull, // PickUpActionPluginImpl_DoPickUpCollection
            0x14090F930ull, // GetCollectionIconFtexPath
            0x14090FD10ull, // GetCollectionNameText
            0x14050AE60ull, // UixUtility_GetUixUtility
            0x140D4E3E0ull, // Collection_GetModelFilePathRoot
            0x140D4DBF0ull, // Collection_GetFovaFilePath
            0x140A0B600ull, // Collection_IsHerbByType
            0x140A0B670ull, // Collection_IsMaterialByType
            0x140A0B520ull, // Collection_IsDiamondByType
            0x140A95920ull, // MbmImpl_IsGotDataBase
            0x140A87BF0ull, // MbmImpl_AddTempDataBase
            0x141160AF4ull, // QuietChokeHold
            0x141161289ull, // QuietInterrogate
            0x14111019Cull, // QuietInterrogateBypass
            0x14110E617ull, // QuietHoldupInterrogate
            0x1416BC459ull, // CharaSlotSelectionNumGate
            0x1416BC410ull, // CharaSlotSelectionNum
            0x14196F450ull, // UiMbmDataBase_RefreshPrefabList
            0x14196E490ull, // UiMbmDataBase_RefreshDataBaseNameText
            0x14196E350ull, // UiMbmDataBase_RefreshDataBaseIcon
            0x14196CFD0ull, // UiMbmDataBase_GetDataBaseInfoText
            0x14196EAB0ull, // UiMbmDataBase_RefreshInfoLayout
            0x14196E8D0ull, // UiMbmDataBase_RefreshImageRecord
            0x14196E240ull, // UiMbmDataBase_RefreshCollectionRate
            0x140FE6290ull, // SkillAndItemParameterSystem_GetSuitParam
            0x140F1C0C0ull, // MbDvcMapCallbackIconImpl_DispOnNewMarkerIcon
            0x140F1BA60ull, // MbDvcMapCallbackIconImpl_DispOnImportantIcon
            0x140F1AFC0ull, // MbDvcMapCallbackIconImpl_DispOnGoalIcon
            0x140F1AD60ull, // MbDvcMapCallbackIconImpl_DispOnGeneralIcon
            0x140F1BB90ull, // MbDvcMapCallbackIconImpl_DispOnTargetMarkerIcon
            0x140F1A200ull, // MbDvcMapCallbackIconImpl_DispOnAreaIcon
            0x140F22290ull, // MbDvcMapCallbackIconImpl_GetIconDisplayParam
            0x141E02B10ull, // PlayerInfo_GetLocalPartsType
            0x141E022E0ull, // PlayerInfo_CheckStatusBit
            0x1409CC98Full, // UpdatePartsStatus_QuietCamoStrip
            0x140955E40ull, // MissionPrepSystem_GetPlayerNameFromPlayerTypeAndStaffId
            0x147F3FDA0ull, // Player2BlockController_ApplyQuietSuitShaderParams
            0x140FE7D10ull, // UiControllerImpl_GetUiSightTypeBySightId
            0x1408ECE80ull, // SightCommonFactory_CreateWindow
            0x1408ED860ull, // SightCommonFactory_DeleteWindow
            0x14196F3A0ull, // UiMbmDataBase_RefreshNewIcon
            0x14196FE10ull, // UiMbmDataBase_SetPutCursorFlag
            0x14196E080ull, // UiMbmDataBase_OnStop
            0x140F5A9B0ull, // MbmImpl_CalcDocumentationUnreadCount
            0x140F5AB80ull, // MbmImpl_CalcEncyclopediaUnreadCount
            0x141E0AFB0ull, // Net_ServerManagerBufferRelease
            0x1414E2070ull, // NoticeControllerImpl_CheckSightNoticePlayer
            0x14D003820ull, // DemoPlayback_OnPlayingAfterUpdateStream
            0x14CFFA3A0ull, // DemoPlayback_DoFinish
            0x14CFFA6A0ull, // DemoPlayback_DoInterrupt
            0x140975200ull, // UpdateMusicPlayer
            0x148A57EA0ull, // GameObject_GetGameObjectIdWithName
            0x14124AB50ull, // Vehicle_GetConnectPointWorldMatrix
            0x14127A570ull, // Player_SequentialDemoActionExecute
            0x14127A9B0ull, // Player_SequentialDemoActionOnSignal
            0x140BF9A30ull, // Fox_GameObjectManager_GetSharedInstance
            0x14229B720ull, // GameObjectType_Horse2_Vtable
            0x1422C5440ull, // GameObjectType_Heli2_Vtable
            0x1422C4A20ull, // GameObjectType_HeliAlt_Vtable
            0x1422CA630ull, // GameObjectType_WalkerGear2_Vtable
            0x1422E5C80ull, // AnimationControllerImpl_Vtable
            0x1422FAFE0ull, // HeliAnimationControllerImpl_Vtable
            0x14957CE70ull, // MotionLoaderImpl_GetMtarIds
            0x14957CB60ull, // MotionLoaderImpl_GetMtarFilePath
            0x14957BDB0ull, // MotionLoaderImpl_GetBlockPackagePath
            0x141049450ull, // AttackAction_GetGunMotionIdFromTable
            0x141046670ull, // AttackAction_GetEquipMotionIdFromTable
            0x14104C160ull, // AttackAction_GetNormalGripMotionId
            0x14104C340ull, // AttackAction_GetNormalMagazineGripMotionId
            0x141043F30ull, // AttackAction_GetAssaultGripMotionId
            0x141049700ull, // AttackAction_GetHandgunGripMotionId
            0x141050E10ull, // AttackAction_GetSubMachinegunGripMotionId
            0x14104FFD0ull, // AttackAction_GetSniperRifleGripMotionId
            0x14104FCC0ull, // AttackAction_GetShotgunGripMotionId
            0x14104BB20ull, // AttackAction_GetMachinegunGripMotionId
            0x141046AC0ull, // AttackAction_GetGrenadeLauncherGripMotionId
            0x14104BE40ull, // AttackAction_GetMissileGripMotionId
            0x141044230ull, // AttackAction_GetAssaultUnderBarrelGripMotionId
            0x141049BA0ull, // AttackAction_GetHandgunOneHandGripMotionId
            0x141049CF0ull, // AttackAction_GetHandgunOneHandMotionId
            0x141051020ull, // AttackAction_GetSubMachinegunOneHandMotionId
            0x141050690ull, // AttackAction_GetStepReloadHandgunOneHandMotionId
            0x141050520ull, // AttackAction_GetStepReloadHandgunMotionId
            0x141056830ull, // AttackAction_ResetMissionPrepareGripMotion
            0x14104C4A0ull, // AttackAction_GetNormalMotionIdByCategory
            0x141043F80ull, // AttackAction_GetAssaultMotionIdTable
            0x1410497F0ull, // AttackAction_GetHandgunMotionIdTable
            0x141050E50ull, // AttackAction_GetSubMachinegunMotionIdTable
            0x1410500A0ull, // AttackAction_GetSniperRifleMotionIdTable
            0x14104FD60ull, // AttackAction_GetShotgunMotionIdTable
            0x141050770ull, // AttackAction_GetShotgunStepReloadMotionIdTable
            0x14104BB60ull, // AttackAction_GetMachinegunMotionIdTable
            0x141046BF0ull, // AttackAction_GetGrenadeLauncherMotionIdTable
            0x14104BE90ull, // AttackAction_GetMissileMotionIdTable
            0x141051CC0ull, // AttackAction_GetChaseMotionIdTable
            0x14800B500ull, // EquipMotionDataTable_ReloadEquipMotionData2
            0x1416C378Dull, // MissionPreparationCallbackImpl_Start_PlayBgmEvent
            0x1416C6BF9ull, // MissionPreparationCallbackImpl_Update_StopBgmEvent
            0x1416C6F1Eull, // MissionPreparationCallbackImpl_Update_StopBgmEvent2
            0x1416C3510ull, // MissionPreparationCallbackImpl_Start
            0x1416C4080ull, // MissionPreparationCallbackImpl_Update
            0x142c1d990ull, // Soldier2FaceSystem_Instance
            0x140F813D0ull, // StaffControllerImpl_AddStaffShort
            0x140F81710ull, // StaffControllerImpl_AddStaffFull
            0x14835FB80ull, // tpp::mbm::impl::StaffIdToFaceId
            0x140F97F30ull, // tpp::mbm::impl::StaffControllerImpl::StaffSeedToFaceTexturePathId
            0x14B05BFB0ull, // tpp::ui::menu::mbm::impl::StaffListCallbackImpl::RefreshFaceRecord
            0x141DEF540ull, // fox::ui::WindowFactoryCollector::FindWindowFactory
            0x141DAB760ull, // fox::ui::WindowFunction::CreateWindow
            0x140867880ull, // tpp::ui::hud::CommonDataManager::SetSubjectiveWindowHandle
            0x14DFDD520ull, // fox::ui::WindowFactoryCollector::FindWindowFactory (implementation reached through thunk)
            0x14772F5C0ull, // tpp::ui::hud::CommonDataManager::SetSubjectiveWindowHandle (implementation reached through thunk)
            0x1409ECA0Dull, // CalcFv2ResourceManagerInfoForFace
            0x1409ED2A1ull, // ConvertExtendFaceFova2PathToFovaFile
            0x1409ED8B5ull, // ConvertFova2PathToFovaFileForProg
            0x147FCBA27ull, // GetFaceFovaFpkPathIdArrayAtFaceId
            0x147FCBF99ull, // GetFaceFovaFpkPathIdAtFaceId
            0x147FCC317ull, // GetFaceFovaPathIdArrayAtFaceId
            0x147FCC629ull, // GetFaceFovaPathIdAtFaceId
            0x1409EFED5ull, // GetFaceFpkFileCodeListImpl
            0x1409F00C4ull, // GetFaceFpkFileCodeListImpl
            0x147FCF71Eull, // GetHairFovaPathIdAtFaceId
            0x1409F2AD3ull, // PushFaceFovaFpk
            0x1409F3B30ull, // SetFaceFovaDefinitionTableImpl
            0x1409F4360ull, // SetFovaFileTable
            0x140A3AE00ull, // EquipParameterTablesImpl::GetAmmoParameterBlock
            0x1482C4EC0ull, // WalkerGearSystemImpl::GetSubWeaponLoadAmmoMax
            0x14934AA90ull, // BuddyQuietEquipControllerImpl::SetupCurrentSlot
            0x149AF68D0ull, // HostageEquipControllerImpl::SetupCurrentSlot
            0x14AADB580ull, // SoldierEquipControllerImpl::SetupCurrentSlot
            0x14B34C750ull, // BossQuietEquipControllerImpl::SetupCurrentSlot
            0x140F666D0ull, // EquipDevelopControllerImpl::GetBulletCountMagazine
            0x140F66800ull, // EquipDevelopControllerImpl::GetBulletCountMax
            0x149F1B570ull, // EquipDevelopControllerImpl::GetBulletType
            0x140DC376Full, // EquipSystemImpl::SetUpGunInfoFromGunPartsDesc (receiver)
            0x140DC3964ull, // EquipSystemImpl::SetUpGunInfoFromGunPartsDesc (barrel)
            0x140DC3B3Aull, // EquipSystemImpl::SetUpGunInfoFromGunPartsDesc (magazine)
            0x140DC3AE1ull, // EquipSystemImpl::SetUpGunInfoFromGunPartsDesc (stock)
            0x140DC3B7Full, // EquipSystemImpl::SetUpGunInfoFromGunPartsDesc (muzzleOption)
            0x140DC3CDCull, // EquipSystemImpl::SetUpGunInfoFromGunPartsDesc (sight)
            0x140DC3D60ull, // EquipSystemImpl::SetUpGunInfoFromGunPartsDesc (sight2)
            0x140DC3BF1ull, // EquipSystemImpl::SetUpGunInfoFromGunPartsDesc (laserFlash1)
            0x140DC3C4Full, // EquipSystemImpl::SetUpGunInfoFromGunPartsDesc (laserFlash2)
            0x140DC3DB0ull, // EquipSystemImpl::SetUpGunInfoFromGunPartsDesc (underBarrel)
            0x140DC3E36ull, // EquipSystemImpl::SetUpGunInfoFromGunPartsDesc (underBarrelCmp)
            0x140EE9540ull, // InventoryImpl::CalcAmmoAndStockIfPickUp
            0x1480DEE30ull, // EquipIdTableImpl::GetRootParameterId
            0x149F0FFE0ull, // DeployControllerImpl::GenerateDeployMission
            0x149F07500ull, // DeployControllerImpl::AddLogMission
            0x149F10500ull, // DeployControllerImpl::GenerateDeployMissionInfoOffline
            0x149F0DD60ull, // DeployControllerImpl::CreateDeployMissionSeedParam
            0x140F63110ull, // DeployControllerImpl::GetDeployMissionNameMessageId
            0x140F62B90ull, // DeployControllerImpl::GetDeployMissionCaptionMessageId
            0x149F11420ull, // DeployControllerImpl::GetDeployMissionNameKeyId
            0x140F64120ull, // DeployControllerImpl::RefreshMissionList
            0x149F0FE00ull, // DeployControllerImpl::GetNextRandomMissionId
            0x140F61210ull, // DeployControllerImpl::CompletedDeployMissionOffline
            0x140AB0250ull, // MotherBaseManagementSystemImpl::SetupAfterRestoreFromSVars
            0x140F77790ull, // PoolRewardControllerImpl::GeneratePoolRewardsInfo
            0x14AF22560ull, // CombatDeployCallbackImpl::RefreshMissionIconInGlobe
            0x1413dce50ull, // NoticeAntiBuddyWalkerGearAiImpl::ShouldSleep
            0x14a9f97c0ull, // CombatAntiBuddyWalkerGearAiImpl::UpdatePrimaryTarget
            0x14138ece0ull, // AttackActionImpl::HasOtherSoldierOnVec
            0x140d30ed0ull, // Bullet3Impl::OnOffenseHit
            0x14145f870ull, // soldier DamageHandlerImpl::ProcessDamages
            0x14136E0F0ull, // Soldier2Impl::UpdateMarkers
            0x141644B50ull, // CombatDeployMissionListRecordCallFunc::Refresh
            0x14164AD40ull, // SetRevengeIcon
            0x1413e0230ull, // CombatAntiBuddyWalkerGearAiImpl::StepSurprise
            0x143F8E060ull, // AK::MemoryMgr::Malloc
            0x143F8DDF0ull, // AK::MemoryMgr::Malign
            0x143F8BDA0ull, // AK::MemoryMgr::Free
            0x142B9EC18ull, // AK::MemoryMgr::s_pMemPools
            0x142A0223Cull, // g_DefaultPoolId
            0x142B9EB1Cull, // g_PrepareEventMemoryPoolID
            0x14a984bc0ull, // Soldier2Util::SearchActiveMember
            0x1413e15c0ull, // CombatAntiBuddyWalkerGearAiImpl::UpdateSnipeTarget
            0x1415a78a0ull, // WalkerGearActionImpl::HasOtherSoldierOnVec
            0x141647440ull, // CombatDeployCallbackImpl::RefreshMissionList
            0x14086E150ull, // MbTopMenuSelectorEv::UpdateMenuNotice
            0x1414df3c0ull, // NoticeControllerImpl::CheckNoiseNoticeSoldier
            0x14ab9a240ull, // NoticeDownComradeAiImpl::ShouldSleep
            0x14ad7a540ull, // VehicleNoticeDownComradeAiImpl::ShouldSleep
            0x14155C6D0ull, // RideVehicleActionImpl::HasOtherSoldierOnVec
            0x1414AABB0ull, // LifeControllerImpl::SendStatusMessage
            0x14A9F3F50ull, // CombatAntiBuddyWalkerGearAiImpl::CheckGrenade
            0x1411FC770ull, // IDroidActionPluginImpl::CloseState
            0x1411FBF10ull, // IDroidActionPluginImpl::Deactivate
            0x140DC42A0ull, // EquipSystemImpl::SetItemVisible
            0x1411FA940ull, // IDroidActionPluginImpl::IsPutAwayInterrupted
            0x1411FB710ull, // IDroidActionPluginImpl::IsOtherAvatar
            0x14D6B3B80ull, // MotionGraphBlendNodeTraverser leaf clip-id read
            0x141A6C6B0ull, // AnimControl::GetAnimFileFromGaniPath
            0x141002B50ull, // player action plugin GetStance (1 stand, 2 squat, 3 crawl, 4 supine)
            0x141181E93ull, // CureActionPluginImpl::StateCureStart GetStance call
            0x141181F4Dull, // CureActionPluginImpl::StateCureStart GetStance call
            0x1411817A9ull, // CureActionPluginImpl::StateCure GetStance call
            0x141181AC9ull, // CureActionPluginImpl::StateCureEnd body GetStance call
            0x1410A20A0ull, // BasicActionImpl::SelectWakeupState
            0x141180E90ull, // CureActionPluginImpl::FinishCureSprayEffect
            0x141181630ull, // CureActionPluginImpl::StateCure
            0x14110FF20ull, // CallMenuManagerImpl::UpdateCommandSetInterrogation
            0x141362180ull, // Soldier2Impl::ProcessSignal
            0x141513570ull, // NoticeNoiseAiImpl::SearchSpecificComrade
            0x1414DDFB0ull, // NoticeControllerImpl::CheckForceNotice
            0x14A2461E0ull, // CallMenuManagerImpl::UpdateInShowing
            0x141110AE0ull, // CallMenuCommandSet::UpdateState
            0x1411103D0ull, // CallMenuManagerImpl::UpdateCommandSetRescueBoy
            0x141369B80ull, // soldier::ActionController::SetCommand
            0x141520C10ull, // NoticeNoiseAiImpl::StepMovePause
            0x149AD7830ull, // hostage CoreAiImpl::SetSpecialActionParam
            0x140EDB740ull, // hostage SpecialActionImpl::State_SpecialIdle
            0x14B284AF0ull, // ui::hud::CallMenuImpl::UpdateOption
            0x14B284F50ull, // ui::hud::CallMenuImpl::UpdateSelectDoc
            0x14A241BF0ull, // CallMenuManagerImpl::ShowCallMenu
            0x142B743B0ull, // fox::gr::dg::MaterialManager::s_Instance (data)
            0x142B743A8ull, // fox::gr::dg::ShaderTechniqueManager::s_Instance (data)
            0x14952EF60ull, // cp::RadioControllerImpl::StepRadioNoResponse
            0x14952F810ull, // cp::RadioControllerImpl::StepRadioWaitResponse
            0x140D580D0ull, // cp::CommandPost2Impl::Controller_RequestRadioByMember_
            0x14948EB90ull, // cp::CommandPost2Impl::Controller_SetRadioMemberReady_
            0x14948BCC0ull, // cp::CommandPost2Impl::Controller_ReleaseRadioByMember_
            0x140D5AF90ull, // cp::CommandPost2Impl::FinishRadio
            0x140DA1CA0ull, // cp::RadioControllerImpl::StepRadio
            0x140D58910ull, // cp::CommandPost2Impl::Controller_UpdateRadio_
            0x140D62510ull, // cp::CommandPost2Impl::RequestRadioByCp
            0x14952EC50ull, // cp::RadioControllerImpl::StepIdle
            0x140D58A80ull, // tpp::gm::CpRadioService::ConvertSpeechLabelToRadioType
            0x148B5B7D0ull, // tpp::gm::impl::AiControllerImpl::WakeNode
            0x148B5ACF0ull, // tpp::gm::impl::AiControllerImpl::SleepNode
            0x14A93FAB0ull, // tpp::gm::soldier::Soldier2InterrogateUtil::IsTranslatable (Arxan-moved body)
            0x140A45CA0ull, // tpp::gm::buddy::impl::Buddy2BlockControllerImpl::GetPathAtBuddyType
            0x14811F640ull, // tpp::gm::buddy::impl::Buddy2BlockControllerImpl::GetBuddyQuietWeaponFileFromBuddyBlock
            0x14811F200ull, // tpp::gm::buddy::impl::Buddy2BlockControllerImpl::GetBuddyQuietFovaFile
            0x14811F339ull, // tpp::gm::buddy::impl::Buddy2BlockControllerImpl::GetBuddyQuietFv2FileFromBuddyBlock (fv2 id load)
            0x140F665A0ull, // EquipDevelopControllerImpl::GetBuddyPartsDevelopInfoIndex
            0x149F1AFE0ull, // EquipDevelopControllerImpl::GetBuddyPartsId
            0x149F1B3D0ull, // EquipDevelopControllerImpl::GetBuddyPartsKind
            0x142C29010ull, // tpp::gm::buddy::impl::g_buddyService (BuddyServiceImpl instance)
            0x140A4E1A0ull, // tpp::gm::buddy::impl::BuddyServiceImpl::ChangeHorseSettings
            0x1481441C0ull, // tpp::gm::buddy::impl::BuddyServiceImpl::ChangeDogSettings
            0x1481547D0ull, // tpp::gm::buddy::impl::BuddyServiceImpl::GetQuietEquipIdByWeaponType
            0x148161830ull, // tpp::gm::buddy::impl::BuddyServiceImpl::IsUseSuppressorByWeaponType
            0x148144EADull, // tpp::gm::buddy::impl::BuddyServiceImpl::ChangeQuietCostume (costume clamp)
            0x140A54307ull, // TppBuddyService.SetVarsQuietCostume (costume clamp)
            0x1481453B1ull, // tpp::gm::buddy::impl::BuddyServiceImpl::ChangeQuietWeaponType (weapon clamp)
            0x140A54382ull, // TppBuddyService.SetVarsQuietWeaponType (weapon clamp)
            0x140A4E21Cull, // tpp::gm::buddy::impl::BuddyServiceImpl::ChangeHorseSettings (tack check)
            0x148144209ull, // tpp::gm::buddy::impl::BuddyServiceImpl::ChangeDogSettings (gear check)
            0x140CE3458ull, // tpp::gm::buddyquiet::impl::BuddyQuiet2Impl weapon change command (weapon clamp)
            0x140CE40F3ull, // tpp::gm::buddyquiet::impl::BuddyQuiet2Impl::SetBruise (costume check)
            0x1416ABEB7ull, // tpp::ui::menu::impl::ItemSelectorCallbackImpl::SetupPrefabListElement (Quiet costume list gate)
            0x1416ABA07ull, // tpp::ui::menu::impl::ItemSelectorCallbackImpl::SetupPrefabListElement (Quiet weapon list gate)
            0x141459E0Bull, // tpp::gm::soldier::impl::DamageAiImpl::StepBitDamage (dog gear check)
            0x140D04800ull, // tpp::gm::buddyquiet::impl::(anon)::CombatAiImpl::SendFireSignal
            0x149349CC0ull, // tpp::gm::buddyquiet::impl::EquipControllerImpl::InitCurrentSlot
            0x140D183E0ull, // tpp::gm::buddyquiet::impl::EquipControllerImpl::Update
            0x14934A560ull, // tpp::gm::buddyquiet::impl::EquipControllerImpl::SetMotion
            0x14811FC60ull, // tpp::gm::buddy::Buddy2BlockController::GetFileForPathFromBuddyBlock
            0x142C28910ull, // tpp::gm::buddy::Buddy2BlockController instance pointer
            0x140E51610ull, // tpp::gm::horse::impl::ActionCoreImpl::ExecDefenseCallback tack damage switch
            0x149911448ull, // tpp::gm::horse::impl::BasicAction2Impl::UpdateGallopMotionSpeed tack switch
            0x149905212ull, // tpp::gm::horse::impl::BasicAction2Impl::EnterOffenseTarget tack switch
            0x140E7C3CBull, // horse motion sound tack switch (FUN_140E7BE70)
            0x140E58DA0ull, // tpp::gm::horse::impl::BasicAction2Impl::Execute
            0x14815614Eull, // tpp::gm::buddy::impl::BuddyServiceImpl D-Dog order variant gear check (FUN_140A515B0)
            0x148156594ull, // tpp::gm::buddy::impl::BuddyServiceImpl::InitBuddyCommandForStatus gear check
            0x140C89297ull, // tpp::gm::buddydog::impl::DamageHandlerImpl::ProcessDamages gear check
            0x148E35CC0ull, // tpp::gm::buddydog::impl::DamageHandlerImpl::IsNeedReaction gear check
            0x140C88276ull, // tpp::gm::buddydog::impl::DamageHandlerImpl::CallDamageEffect gear check
            0x0ull,         // tpp::gm::buddydog::impl::DamageHandlerImpl::CallDamageSound gear check (not ported)
            0x140C8F10Eull, // tpp::gm::buddydog::impl::EscapeIdleAiImpl::StepSearch gear check
            0x0ull,         // tpp::gm::buddydog::impl::NoticeDamageAiImpl::Setup gear check (not ported)
            0x140C63933ull, // tpp::gm::buddydog::impl::AttackAiImpl::ReTarget gear check
            0x1414CFDE5ull, // tpp::gm::soldier::impl::NoticeBuddyDogAiImpl::StepAware gear check
            0x140C55FF1ull, // tpp::gm::buddydog::impl::BuddyDog2Impl::Initialize gear copy to +0xF4
            0x0ull,         // tpp::gm::impl::SimplePartsEffectControllerImpl::CreateEffectImpl (not ported)
            0x0ull,         // fox::fx::Effect::Create (not ported)
            0x142A82C28ull, // tpp::gm::player Player2System instance
            0x14CC6E250ull, // fox::anim::AnimFile::GetEventData
            0x14A7FAA50ull, // tpp::gm::impl::equip::RealizedEquipObjectImpl::MotionEventCallbackImpl
            0x1413049D0ull, // tpp::gm::impl::equip::RealizedEquipObjectImpl::EjectCasing
            0x14A1D1D60ull, // tpp::gm::player::impl::attack::AttackActionImpl::SetEquipMotion
            0x0ull,         // tpp::gm::player::impl::attack::AttackActionImpl::MotionEventCallback (not ported)
            0x140A1C3C0ull, // tpp::gm::impl::(anon)::GetPackagePaths
            0x14328DEF0ull, // fox::Array<fox::Path>::PushBack
            0x143227C10ull, // fox::Path::Path(const char*)
            0x14CC806C0ull, // fox::anim::EventCallback::ExecCallback
            0x1416ADB70ull, // tpp::ui::menu::impl::ItemSelectorCallbackImpl::Update
            0x147EE3670ull, // tpp::gm::player::impl::Player2BlockController::GetFileFromPartsBlockNew
            0x142C1B0D8ull, // the Player2BlockController pointer it is called with
            0x14489E650ull, // fox::FormVariationFile2::ApplyFormVariation (ModelBuilder)
            0x0ull,         // tpp::gm::player::impl::Fova2ControllerImpl work: type 0x11 extra after an fv2 apply (not ported)
            0x140A29AB0ull, // tpp::gm::impl::equip::EquipIdTableImpl::GetSupportWeaponTypeId
            0x140A41670ull, // tpp::gm::impl::equip::EquipParameterTablesImpl::ReloadEquipParameterTablesImpl
            0x140A417A5ull, // ReloadEquipParameterTablesImpl: add rax, 0x2D8 (SupportWeaponParameter size pass)
            0x140A417BEull, // ReloadEquipParameterTablesImpl: lea rax, [rcx+0x2D8] (allocation pass)
            0x1415D6E90ull, // tpp::gm::impl::throwing::ThrowingImpl::GetBlastParamByEquipId
            0x1415DA890ull, // tpp::gm::impl::throwing::ThrowingImpl::UpdateActionGrenade (grenade and stun)
            0x1415DB0C0ull, // tpp::gm::impl::throwing::ThrowingImpl::UpdateActionSmoke (smoke, sleeping gas, flares)
            0x14ADCDC30ull, // tpp::gm::impl::throwing::ThrowingImpl::UpdateActionMolotovCocktail
            0x14ADCD260ull, // tpp::gm::impl::throwing::ThrowingImpl::UpdateActionDecoy
            0x1415DABC0ull, // tpp::gm::impl::throwing::ThrowingImpl::UpdateActionKibidango
            0x14ADCD860ull, // tpp::gm::impl::throwing::ThrowingImpl fake sign activation
            0x14ADCFEA0ull, // tpp::gm::impl::throwing::ThrowingImpl::UpdateStatus
            0x14ADD3DA0ull, // tpp::gm::impl::throwing::ThrowingSystemImpl::ExecuteSerially
            0x1415D35C0ull, // tpp::gm::impl::throwing::ThrowingImpl::ActivateThrowingAtEmptyWork
            0x14ADCE120ull, // tpp::gm::impl::throwing::ThrowingImpl::UpdateDeactivation
            0x1415D40C0ull, // tpp::gm::impl::throwing::ThrowingImpl::AddReboundNoise
            0x1415D9760ull, // tpp::gm::impl::throwing::ThrowingImpl::PostReboundSound
            0x14ADCA700ull, // tpp::gm::impl::throwing::ThrowingImpl::IsObstructSight
            0x140F671C0ull, // tpp::mbm::impl::EquipDevelopControllerImpl::GetDamageType
            0x1411AE440ull, // tpp::gm::player::impl::EquipControllerImpl::InitializePlayerAtIndex
            0x149CB9B10ull, // tpp::gm::impl::LoadoutManagerImpl::RestoreLoadoutInfosFromVars
            0x149CBB190ull, // tpp::gm::impl::LoadoutManagerImpl sortie loadout slot restore
            0x149CBB900ull, // tpp::gm::impl::LoadoutManagerImpl return-heli loadout slot restore
            0x140FCE740ull, // tpp::gm::impl::placed::PlacedSystemImpl::RestorationPlace
            0x140AC9D60ull, // tpp::gm::impl::SupplyCboxGameObjectImpl::RestoreRequestFromSVars
            0x1409D1990ull, // tpp::gm::player::impl::Player2UtilityImpl::GetPickableInfosOfAmmoBox
            0x140A3C450ull, // tpp::gm::impl::equip::EquipParameterTablesImpl::GetSupportWeaponParameterBlock
            0x149F1DCE0ull, // tpp::mbm::impl::EquipDevelopControllerImpl::GetDamageAmountRatio
            0x14166C530ull, // tpp::ui::menu::mbm::impl::EquipDetailsCallbackImpl::SetupSupportParameterDetails (bar panel caller range)
            0x141484C30ull, // tpp::gm::soldier::impl::IdleActingActionImpl::CallVoice
            0x14AD11CF0ull, // tpp::gm::soldier::impl::Soldier2SoundControllerImpl::__callVoiceWithoutSync
            0x14158D2B0ull, // tpp::gm::soldier::impl::Soldier2SoundControllerImpl::__callVoiceWithSync
            0x141550770ull, // tpp::gm::soldier::impl::RadioActionImpl::State_RadioChaffEnd
            0x147CCDECCull, // tpp::gm::EquipIdTable::GetAmmoStockIdOfSupportOrItem: lea eax,[rbx-0x50A]
            0x1409515F3ull, // MissionPreparationSystemImpl::CalcEquipWeaponItemCostFromLoadout support loop: lea r9d,[rbx-0x50A]
            0x149CC0597ull, // LoadoutManagerImpl::StoreLoadoutInfoToPlayerVars support cells: add edi,-0x50A
            0x1411AF029ull, // EquipControllerImpl::InitializePlayerAtIndex support slots: lea r8d,[rbx-0x50A]
            0x14A448E62ull, // PickUpActionPluginImpl::CheckLoadableForSupportOrItem: lea r8d,[rbx-0x50A]
            0x14120A35Eull, // PickUpActionPluginImpl::DoPickUpPlaced carried slot: lea eax,[rsi-0x50A]
            0x14120A402ull, // PickUpActionPluginImpl::DoPickUpPlaced picked item: lea ecx,[r15-0x50A]
            0x14120AEF4ull, // PickUpActionPluginImpl::DoPickUpWithPickableInfo carried slot: lea ecx,[rsi-0x50A]
            0x14AFD9F5Full, // ItemSelectorCallbackImpl::GetCurrentAmmoFromVarsForSupportWeapon: add ebx,-0x50A
            0x140A3AFF5ull, // EquipParameterTablesImpl::GetAmmoParameterBlock: lea edi,[rbx-0x50A]
            0x140DA2A10ull, // tpp::gm::impl::cp::RadioSpeechHandlerImpl::CallPart
            0x14A72B430ull, // tpp::gm::impl::RangeAttackSystemImpl::ActivateChaff
            0x140B08080ull, // tpp::gm::impl::SoundControllerImpl::IsPlayingVoice
            0x1414B1A30ull, // tpp::gm::soldier::impl::MortarActionImpl::State_WaitAction
            0x1414B1BC0ull, // tpp::gm::soldier::impl::UnrealMortarActionImpl::State_WaitAction
            0x14966C410ull, // tpp::gm::heli::impl::Heli2Impl::Unrealize
            0x1414B1280ull, // tpp::gm::soldier::impl::MortarActionImpl::State_MortarAction
            0x14AB4D490ull, // tpp::gm::soldier::impl::UnrealMortarActionImpl::State_MortarAction
            0x140DA1F40ull, // tpp::gm::impl::cp::RadioControllerImpl::StepRadioChaff
        };

        return value;
    }
}
