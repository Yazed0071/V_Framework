#pragma once

#include <Windows.h>
#include <cstdint>

namespace AddressSetRuntime
{
    enum class GameBuild
    {
        Unknown,
        En_1_0_15_3,   // day1820
        Jp_1_0_15_3,   // day1820
        En_1_0_15_4,   // day3800
        Jp_1_0_15_4,   // day3800
        En_1_0_15_4a,    // day3900 - exe version resource still reads 1.0.15.4
        Jp_1_0_15_4a     // day3900
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
        uintptr_t GetUixUtilityToFeedQuarkEnvironment = 0;
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
        uintptr_t StateRadioRequest = 0;
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
        uintptr_t Sahelan_ActionCoreImpl_UpdateEyeLampColor = 0;
        uintptr_t Sahelan_ActionCoreImpl_UpdateHeartLight = 0;
        uintptr_t Sahelan_PhaseSneakAi_PushEyeColor = 0;
        uintptr_t MbDvcAnnouncePopupCallbackImpl_UpdateAnnounceNormal    = 0;
        uintptr_t MbDvcAnnouncePopupCallbackImpl_UpdateAnnounceServer    = 0;
        uintptr_t MbDvcAnnouncePopupCallbackImpl_UpdateAnnounceEmergency = 0;
        uintptr_t HudCommonDataManager_GetInstance       = 0;
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



        uintptr_t IconTitleGetLangTextCall                              = 0;

        uintptr_t GameObject_SendCommand                                = 0;


        uintptr_t UiControllerImpl_InitEquipHudData                     = 0;

        uintptr_t NoticeControllerImpl_GetOccasionalChat                = 0;

        uintptr_t SoldierConversationService_ConvertSpeechLabelToConversationType = 0;

        uintptr_t OccasionalChat_FactionTestNop                         = 0;

        uintptr_t MbDvcReserveAnnouncePopup                             = 0;
        uintptr_t MbDvcPopupGateFn                                      = 0;

        uintptr_t NoticeNoiseAiImpl_StepAware                           = 0;
        uintptr_t NoticeIndisAiImpl_StepAware                           = 0;
        uintptr_t NoticeControllerImpl_DoCheckSpreadNotice              = 0;
        uintptr_t NoticeControllerImpl_CheckSightNoticeSoldier          = 0;
        uintptr_t NoticeNearThreatAiImpl_Wake                           = 0;
        uintptr_t NoticeNoiseAlertAiImpl_Wake                           = 0;
        uintptr_t NoticeNearGameObjectAiImpl_Wolf_Wake                  = 0;
        uintptr_t NoticeNoiseSneakAiImpl_Wolf_Wake                      = 0;
        uintptr_t NoticeNearGameObjectAiImpl_Bear_Wake                  = 0;
        uintptr_t NoticeNoiseAiImpl_Bear_Wake                           = 0;

        uintptr_t RealizedSoldier2Impl_ConvertHeadEquipModelType        = 0;
        uintptr_t RealizedSoldier2Impl_UpdateHeadEquipMesh              = 0;
        uintptr_t FovaController_GetActiveFovaResourceManager           = 0;
        uintptr_t Fv2ResourceManager_GetModel                           = 0;

        uintptr_t TimeCigaretteActionPluginImpl_ShowTimeCigaretteUi      = 0;

        uintptr_t HeliTaxi_CanHeliTaxi      = 0;
        uintptr_t HeliTaxi_CallRescueHeli   = 0;
        uintptr_t HeliTaxi_StepWithdraw     = 0;
        uintptr_t HeliTaxi_GetLocationId    = 0;
        uintptr_t HeliTaxi_RequestMapPhase  = 0;
        uintptr_t HeliTaxi_StepGoToNav      = 0;
        uintptr_t HeliTaxi_StepTaxiCurrentCluster  = 0;
        uintptr_t HeliTaxi_PassengerUpdate  = 0;
        uintptr_t MechaActionImpl_StateOff  = 0;
        uintptr_t MechaActionImpl_StateOn    = 0;
        uintptr_t PassengerControllerImpl_IsPassengerClosingDoor = 0;

        uintptr_t PlacedSystemImpl_BindResource = 0;
        uintptr_t UiMarkerCommonDataImpl_RegisterLZMarkerInUpdate = 0;

        uintptr_t HeliSoundControllerImpl_Update    = 0;
        uintptr_t HeliSoundControllerImpl_CallVoice = 0;
        uintptr_t HeliFlightControllerImpl_Update   = 0;
        uintptr_t Hud_GetAnnounceLogSE              = 0;

        uintptr_t Hud_TypingLogActUpdate            = 0;
        uintptr_t Ui_SoundControlStart              = 0;
        uintptr_t Ui_UiCommonDataManagerGetInstance = 0;
        uintptr_t Ui_EventNodeBodyGetGraphState     = 0;
        uintptr_t VoiceParam_PlayDialogue                 = 0;

        uintptr_t RideHeliActionPluginImpl_ExecPreMotionGraph = 0;
        uintptr_t RideHeliActionPluginImpl_GetStateFn         = 0;
        uintptr_t MBTerminalActionPluginImpl_IsDisableMBTerminal = 0;
        uintptr_t CallMenuManagerImpl_Update = 0;
        uintptr_t MBTerminalActionPluginImpl_CheckActionState = 0;

        uintptr_t SoundDaemon_Instance       = 0;
        uintptr_t SoundDaemon_PostEventQueue = 0;
        uintptr_t Sd_kW_Select               = 0;

        uintptr_t UpdateAntiAir              = 0;
        uintptr_t ClearAntiAir               = 0;

        uintptr_t GetChangeLocationMenuParameterByLocationId = 0;
        uintptr_t GetMbFreeChangeLocationMenuParameter       = 0;
        uintptr_t GetPhotoAdditionalTextLangId               = 0;

        uintptr_t UiControllerImpl_HideBinocle               = 0;

        uintptr_t AddCassetteTapeTrack        = 0;
        uintptr_t CollectGotTapes             = 0;
        uintptr_t IsGotCassetteTapeTrack      = 0;
        uintptr_t SetCassetteTapeTrackNewFlag = 0;
        uintptr_t SetCurrentAlbum             = 0;
        uintptr_t SetupMusicInfos             = 0;

        uintptr_t RadioCassette_SearchCasseteInfo = 0;
        uintptr_t RadioCassette_GetCassetteMusic  = 0;
        uintptr_t RadioCassette_IsGotCassette     = 0;
        uintptr_t RadioCassette_GetCassetteSaveIndex = 0;
        uintptr_t RadioCassette_SdPostEvent          = 0;
        uintptr_t RadioCassette_RadioUpdate          = 0;
        uintptr_t AddCassetteTapeTrackByIndex        = 0;
        uintptr_t RadioCassette_ActivateUnit         = 0;
        uintptr_t RadioCassette_IsSameSaveIndexFromName = 0;

        uintptr_t SearchLightActionPluginImpl_StateDoorStart = 0;
        uintptr_t SearchLightActionPluginImpl_StateDoorEnd   = 0;

        uintptr_t SoundControl_PostExternalEvent = 0;
        uintptr_t MusicPlayerPlayWrapper = 0;

        uintptr_t Barrier_GetItemId        = 0;
        uintptr_t Barrier_EquipItemCallRet = 0;
        uintptr_t Barrier_Updater          = 0;
        uintptr_t Barrier_IsFobMode        = 0;
        uintptr_t Barrier_LoadGate0        = 0; 
        uintptr_t Barrier_LoadGate1        = 0;
        uintptr_t Barrier_LoadGate2        = 0;

        uintptr_t Dm_FireLoop         = 0;
        uintptr_t Dm_Classify         = 0;
        uintptr_t Dm_VfxFactory       = 0;
        uintptr_t Dm_ComponentFactory = 0;
        uintptr_t Dm_Alloc            = 0;
        uintptr_t Dm_BackLinkPool     = 0;
        uintptr_t Dm_OneShot          = 0;

        uintptr_t AttackActionImpl_IsWeaponNoUseInPlaceAction = 0;

        uintptr_t TppMotherBaseManagement_RegCstDev = 0;
        uintptr_t TppMotherBaseManagement_RegFlwDev = 0;
        uintptr_t EquipDevCtrl_GetSuitDevelopInfoIndex = 0;
        uintptr_t EquipDevCtrl_GetFaceEquipDevelopInfoIndex = 0;

        uintptr_t AddListSuit = 0;
        uintptr_t CamoufParamInfo_GetCamoufValue = 0;
        uintptr_t CamouflageController_ExecSuitCorrect = 0;
        uintptr_t EquipController_SetHandSlotEnabled = 0;
        uintptr_t EquipDevelopCallbackImpl_SetSupplyCBoxInfo = 0;
        uintptr_t IsEnableCurrentSuit = 0;
        uintptr_t ItemSelectorCallbackImpl_DecideActMissionPreparationSetEquipMode = 0;
        uintptr_t ItemSelectorCallbackImpl_DecideActMotherBaseDeviceSupportDropMode = 0;
        uintptr_t ItemSelectorCallbackImpl_SetupPrefabListElement = 0;
        uintptr_t ItemSelectorRecordCallFunc_UpdateRecords = 0;
        uintptr_t ItemSelector_AddListBandana = 0;
        uintptr_t LoadPlayerBionicArmFpk = 0;
        uintptr_t LoadPlayerBionicArmFv2 = 0;
        uintptr_t LoadPlayerCamoFpk = 0;
        uintptr_t LoadPlayerCamoFv2 = 0;
        uintptr_t LoadPlayerPartsFpk = 0;
        uintptr_t LoadPlayerPartsParts = 0;
        uintptr_t LoadPlayerSnakeBlackDiamondFpk = 0;
        uintptr_t LoadPlayerSnakeBlackDiamondFv2 = 0;
        uintptr_t LoadPlayerSnakeFaceFpk = 0;
        uintptr_t LoadPlayerSnakeFaceFv2 = 0;
        uintptr_t MissionPrepSystem_IsEnableHeadOptionSuit = 0;
        uintptr_t MissionPrep_IsEnableCurrentHeadOption = 0;
        uintptr_t Player2BlockController_LoadPartsNew = 0;
        uintptr_t Player2GameObjectImpl_ProcessSignal = 0;
        uintptr_t Player2Impl_SetUpParts = 0;
        uintptr_t Player2UtilityImpl_LoadoutApplyAfterSetSuit = 0;
        uintptr_t Player2UtilityImpl_SetInitialConditionWithLoadoutInfo = 0;
        uintptr_t Player2UtilityImpl_SetSuitAndHandConditionWithLoadoutInfo = 0;
        uintptr_t Player_ConverFaceIdWithFaceEquipId = 0;
        uintptr_t ResourceTable_DoesNeedFaceFova = 0;
        uintptr_t ResourceTable_DoesNeedFaceFovaForAvatar = 0;
        uintptr_t Sys_IsArtificialHandEnabled = 0;
        uintptr_t Sys_IsArtificialHandEnabledForCurrentPlayerType = 0;
        uintptr_t UpdatePartsStatus = 0;
        uintptr_t ItemSelectorCallbackImpl_DecideActMotherBaseCustomize = 0;
        uintptr_t Fox_Path_Exists = 0;
        uintptr_t Fox_Path_Dtor   = 0;
        uintptr_t PluginFacial_ApplyMotion = 0;
        uintptr_t EquipDevCtrl_IsEquipVisile = 0;
        uintptr_t EquipDevCtrl_GetHeadBadgeCategory = 0;
        uintptr_t MissionPrep_SetInitialSelectRecord = 0;
        uintptr_t TppEquip_RegisterConstant = 0;
        uintptr_t PlayerInfoInterfaceImpl_GetPartsTypeAtCamoType = 0;
        uintptr_t EquipIdTable_InfoList = 0;
        uintptr_t EquipIdTable_TypeWords = 0;
        uintptr_t EquipIdTable_ReloadEquipIdTable = 0;
        uintptr_t EquipDevelopCtrl_GetEquipDevelopIndex = 0;
        uintptr_t EquipDevelopCtrl_SetEquipUndeveloped = 0;
        uintptr_t EquipDevCtrl_GetSuitCamoType = 0;
        uintptr_t EquipDevCtrl_GetSuitLevel = 0;
        uintptr_t EquipDevCtrl_IsEquipSuit = 0;
        uintptr_t EquipDevelopCtrl_SetEquipDeveloped = 0;
        uintptr_t SupplyCboxActionPluginImpl_StateInBox = 0;
        uintptr_t HudCommonDataManager_AnnounceLogView = 0;
        uintptr_t Ui_LangIdToKey = 0;
        uintptr_t Ui_GetLangText = 0;
        uintptr_t Soldier_ShootOneBullet_GroupMaskCall = 0;
        uintptr_t Soldier_ActivateBulletAtEmptyWork_SameArmyJnz = 0;

        uintptr_t TelopStartTitleEvCall_SetBgTexture = 0;
        uintptr_t Layout_GetLayout = 0;
        uintptr_t Layout_GetModel = 0;

        uintptr_t GetCassetteTapeUnreadInfo = 0;
        uintptr_t IsNewCassetteTapeTrack = 0;
        uintptr_t CassetteMenuCheckNewFlag = 0;
        uintptr_t CassetteAlbumCheckNewFlag = 0;
        uintptr_t CassetteCheckUnreadInfo = 0;
        uintptr_t SubtitlesObjectSendMessage = 0;

        uintptr_t EquipDevCtrl_IsEquipDevelopable = 0;

        uintptr_t LoadAvatarFaceFv2 = 0;
        uintptr_t LoadAvatarFaceFpk = 0;
        uintptr_t AvatarFaceEditUpdate = 0;
        uintptr_t Fox_ModelFromHandle = 0;

        uintptr_t LoadAvatarHeadOptionFv2 = 0;
        uintptr_t LoadAvatarHeadOptionFpk = 0;

        uintptr_t EquipDevelopCtrl_SetEnableDevelop = 0;

        uintptr_t ReloadEquipParameterTables2 = 0;

        uintptr_t GunBasicParameters2Buffer = 0;


        uintptr_t EquipParameterTablesImpl_Instance = 0;

        uintptr_t MotionLoaderImpl_ReceiverTypeTable = 0;
        uintptr_t MotionLoaderImpl_GetReceiverType = 0;
        uintptr_t MotionLoaderImpl_UnderBarrelTypeTable = 0;
        uintptr_t MotionLoaderImpl_GetUnderBarrelType = 0;

        uintptr_t EquipDevelopControllerImpl_GetSuppressorAmount = 0;

        uintptr_t DamageParameterTable_Instance = 0;
        uintptr_t DamageParameterTable_ReloadDamageParameter = 0;
        uintptr_t DamageParameterTable_GetDamageParameter = 0;
        uintptr_t EquipDevCtrl_GetBaseDevelopId = 0;
        uintptr_t MenuDevelopGrid_FillGrid = 0;
        uintptr_t MenuDevelopGrid_CopyGrid = 0;
        uintptr_t MenuDevelopGrid_CountBadge = 0;
        uintptr_t MenuDevelopGrid_FillFlat = 0;
        uintptr_t MenuDevelopGrid_CopyFlat = 0;
        uintptr_t SightManager_UpdateMissileLockOn = 0;
        uintptr_t Bullet3_DoSimulation = 0;
        uintptr_t Bullet3_ActivateBulletAtEmptyWork = 0;

        uintptr_t SightManager_Update = 0;
        uintptr_t SightManager_UpdateMissileLockOnUi = 0;
        uintptr_t LockOnReticleFactory_CreateWindow = 0;
        uintptr_t EquipParams_GetAttackIdByEquipId = 0;
        uintptr_t EquipSystem_SetUpGunInfoFromGunPartsDesc = 0;
        uintptr_t UiWindowFunction_FindWindow = 0;
        uintptr_t UiWindowFunction_PostShowAndStartMessage = 0;
        uintptr_t UiWindowFunction_GetLayout = 0;
        uintptr_t AttackAction_Fire = 0;
        uintptr_t EquipObject_DoFire = 0;
        uintptr_t Shell_ActivateShellAtEmptyWork = 0;
        uintptr_t CorePlugin_UpdateLoadoutRequest = 0;
        uintptr_t WeaponSystem_DefineWeaponFireSound = 0;
        uintptr_t EquipSystem_ChimeraPartsSetWork = 0;
        uintptr_t MbDvcUpdateTrackListCallFuncs = 0;
        uintptr_t MbDvcRefreshTrackListPrefabParameter = 0;
        uintptr_t MbDvcTrackListRecordRefresh = 0;
        uintptr_t MbDvcMissionListCallbackImpl_ActivatePrefabPopupParameter = 0;
        uintptr_t MbDvcMissionListCallbackImpl_SetMenuHelp = 0;
        uintptr_t Equip_ReloadChimeraPartsInfoTable = 0;
        uintptr_t Equip_ChimeraPartsPackageInfos = 0;
        uintptr_t Mtar_GetAnimFile = 0;
        uintptr_t Mtar_GetDataInfo = 0;
        uintptr_t Equip_MotionEntryTable = 0;
        uintptr_t SimplePartsControllerImpl_Vtable = 0;
        uintptr_t SimplePartsControllerImpl_SetMotionData = 0;
        uintptr_t SimplePartsControllerImpl_SetMotionDataByPath = 0;
        uintptr_t EquipSystemImpl_HideMagazine = 0;
        uintptr_t EquipSystemImpl_GetPartsController = 0;
        uintptr_t Fox_GetQuarkSystemTable = 0;
        uintptr_t Fox_ArrayBaseExtend = 0;
        uintptr_t Fox_ArrayOperatorVtbl = 0;
        uintptr_t Animx_GetControlSize = 0;
        uintptr_t Animx_SimpleControlCtorPool = 0;
        uintptr_t Animx_SimpleControlCtorHeap = 0;
        uintptr_t Animx_SimpleControlDtorPool = 0;
        uintptr_t Animx_SimpleControlDtorHeap = 0;
        uintptr_t Animx_SimpleControlAux = 0;
        uintptr_t Equip_MotionEntrySlotHookA = 0;
        uintptr_t Equip_MotionEntrySlotHookB = 0;
        uintptr_t EquipIdTable_GetEquipTypeId = 0;
        uintptr_t Soldier2InterrogateUtil_UpdateInterrogation = 0;
        uintptr_t Soldier2InterrogateUtil_UpdateInterrogationMarker = 0;
        uintptr_t SoundControllerImpl_CallVoice = 0;
        uintptr_t EquipSystem_GetGunInfoById = 0;

        uintptr_t Reticle_InitHandGunAsset = 0;

        uintptr_t TimeCigaretteActionPluginImpl_HideTimeCigaretteUi = 0;

        uintptr_t Fox_EntityInfoMapTeardown = 0;


        uintptr_t WeaponEnhanceLangIdArray = 0;
        uintptr_t WeaponEnhance_GetIndexForRegist = 0;
        uintptr_t WeaponEnhance_GetMessageIdArray = 0;
        uintptr_t WeaponEnhance_GetLangIdById = 0;
        uintptr_t WeaponEnhance_GetLangIdBound = 0;
        uintptr_t BulletEffectController_CreateEffect = 0;
        uintptr_t ItemSelectorCallbackImpl_AddRecord = 0;

        uintptr_t AdditionalMotionTable_GetMtarPathId = 0;
        uintptr_t ItemSelector_AddDevelopWeaponList = 0;
        uintptr_t BlockControllerImpl_UpdateAdditionalMotionBlock = 0;
        uintptr_t Fox_Block_GetFileForPathId = 0;
        uintptr_t Fox_BlockGroup_GetBlockAtIndex = 0;
        uintptr_t ItemSelector_StartEquipPreviewImpl = 0;
        uintptr_t UiEquipPreviewController_ScrollNext = 0;
        uintptr_t UiEquipPreviewController_ScrollPrev = 0;
        uintptr_t Player2Impl_AddAdditionalMtarAll = 0;
        uintptr_t Player2Impl_RemoveAdditionalMtarAll = 0;
        uintptr_t MotionLoaderImpl_BarrelTypeTable = 0;
        uintptr_t MotionLoaderImpl_GetBarrelType = 0;
        uintptr_t MotionLoaderImpl_MagazineTypeTable = 0;
        uintptr_t MotionLoaderImpl_GetMagazineType = 0;
        uintptr_t MotionLoaderImpl_StockTypeTable = 0;
        uintptr_t MotionLoaderImpl_GetStockType = 0;
        uintptr_t TppPickable_ItemWindowBoundSite = 0;
        uintptr_t UiUtility_GetWeaponItemNameLangId = 0;
        uintptr_t SoundPlayerAnimEvent_StepNoiseQuietCmp = 0;
        uintptr_t CollectionLocatorArray_SetUpLinearAccessor = 0;
        uintptr_t CollectionSystemImpl_PickUp = 0;
        uintptr_t CollectionSystemImpl_RepopCountOperation = 0;
        uintptr_t CollectionSystemImpl_GetCollectionInfo = 0;
        uintptr_t Collection_GetModelFilePath = 0;
        uintptr_t Collection_GetCatchEffectColorAndScale = 0;
        uintptr_t Collection_GetGroundEffectSize = 0;
        uintptr_t PickUpActionPluginImpl_DoPickUpCollection = 0;
        uintptr_t GetCollectionIconFtexPath = 0;
        uintptr_t GetCollectionNameText = 0;
        uintptr_t UixUtility_GetUixUtility = 0;
        uintptr_t Collection_GetModelFilePathRoot = 0;
        uintptr_t Collection_GetFovaFilePath = 0;
        uintptr_t Collection_IsHerbByType = 0;
        uintptr_t Collection_IsMaterialByType = 0;
        uintptr_t Collection_IsDiamondByType = 0;
        uintptr_t MbmImpl_IsGotDataBase = 0;
        uintptr_t MbmImpl_AddTempDataBase = 0;
        uintptr_t QuietChokeHold = 0;
        uintptr_t QuietInterrogate = 0;
        uintptr_t QuietInterrogateBypass = 0;
        uintptr_t QuietHoldupInterrogate = 0;
        uintptr_t CharaSlotSelectionNumGate = 0;
        uintptr_t CharaSlotSelectionNum = 0;
        uintptr_t UiMbmDataBase_RefreshPrefabList = 0;
        uintptr_t UiMbmDataBase_RefreshDataBaseNameText = 0;
        uintptr_t UiMbmDataBase_RefreshDataBaseIcon = 0;
        uintptr_t UiMbmDataBase_GetDataBaseInfoText = 0;
        uintptr_t UiMbmDataBase_RefreshInfoLayout = 0;
        uintptr_t UiMbmDataBase_RefreshImageRecord = 0;
        uintptr_t UiMbmDataBase_RefreshCollectionRate = 0;
        uintptr_t SkillAndItemParameterSystem_GetSuitParam = 0;
        uintptr_t MbDvcMapCallbackIconImpl_DispOnNewMarkerIcon = 0;
        uintptr_t MbDvcMapCallbackIconImpl_DispOnImportantIcon = 0;
        uintptr_t MbDvcMapCallbackIconImpl_DispOnGoalIcon = 0;
        uintptr_t MbDvcMapCallbackIconImpl_DispOnGeneralIcon = 0;
        uintptr_t MbDvcMapCallbackIconImpl_DispOnTargetMarkerIcon = 0;
        uintptr_t MbDvcMapCallbackIconImpl_DispOnAreaIcon = 0;
        uintptr_t MbDvcMapCallbackIconImpl_GetIconDisplayParam = 0;
        uintptr_t PlayerInfo_GetLocalPartsType = 0;
        uintptr_t PlayerInfo_CheckStatusBit = 0;
        uintptr_t UpdatePartsStatus_QuietCamoStrip = 0;
        uintptr_t MissionPrepSystem_GetPlayerNameFromPlayerTypeAndStaffId = 0;
        uintptr_t Player2BlockController_ApplyQuietSuitShaderParams = 0;
        uintptr_t UiControllerImpl_GetUiSightTypeBySightId = 0;
        uintptr_t SightCommonFactory_CreateWindow = 0;
        uintptr_t SightCommonFactory_DeleteWindow = 0;
        uintptr_t UiMbmDataBase_RefreshNewIcon = 0;
        uintptr_t UiMbmDataBase_SetPutCursorFlag = 0;
        uintptr_t UiMbmDataBase_OnStop = 0;
        uintptr_t MbmImpl_CalcDocumentationUnreadCount = 0;
        uintptr_t MbmImpl_CalcEncyclopediaUnreadCount = 0;
        uintptr_t Net_ServerManagerBufferRelease = 0;
        uintptr_t NoticeControllerImpl_CheckSightNoticePlayer = 0;
        uintptr_t DemoPlayback_OnPlayingAfterUpdateStream = 0;
        uintptr_t DemoPlayback_DoFinish = 0;
        uintptr_t DemoPlayback_DoInterrupt = 0;
        uintptr_t UpdateMusicPlayer = 0;
        uintptr_t GameObject_GetGameObjectIdWithName = 0;
        uintptr_t Vehicle_GetConnectPointWorldMatrix = 0;
        uintptr_t Player_SequentialDemoActionExecute = 0;
        uintptr_t Player_SequentialDemoActionOnSignal = 0;
        uintptr_t Fox_GameObjectManager_GetSharedInstance = 0;
        uintptr_t GameObjectType_Horse2_Vtable = 0;
        uintptr_t GameObjectType_Heli2_Vtable = 0;
        uintptr_t GameObjectType_HeliAlt_Vtable = 0;
        uintptr_t GameObjectType_WalkerGear2_Vtable = 0;
        uintptr_t AnimationControllerImpl_Vtable = 0;
        uintptr_t HeliAnimationControllerImpl_Vtable = 0;
        uintptr_t MotionLoaderImpl_GetMtarIds = 0;
        uintptr_t MotionLoaderImpl_GetMtarFilePath = 0;
        uintptr_t MotionLoaderImpl_GetBlockPackagePath = 0;
        uintptr_t AttackAction_GetGunMotionIdFromTable = 0;
        uintptr_t AttackAction_GetEquipMotionIdFromTable = 0;
        uintptr_t AttackAction_GetNormalGripMotionId = 0;
        uintptr_t AttackAction_GetNormalMagazineGripMotionId = 0;
        uintptr_t AttackAction_GetAssaultGripMotionId = 0;
        uintptr_t AttackAction_GetHandgunGripMotionId = 0;
        uintptr_t AttackAction_GetSubMachinegunGripMotionId = 0;
        uintptr_t AttackAction_GetSniperRifleGripMotionId = 0;
        uintptr_t AttackAction_GetShotgunGripMotionId = 0;
        uintptr_t AttackAction_GetMachinegunGripMotionId = 0;
        uintptr_t AttackAction_GetGrenadeLauncherGripMotionId = 0;
        uintptr_t AttackAction_GetMissileGripMotionId = 0;
        uintptr_t AttackAction_GetAssaultUnderBarrelGripMotionId = 0;
        uintptr_t AttackAction_GetHandgunOneHandGripMotionId = 0;
        uintptr_t AttackAction_GetHandgunOneHandMotionId = 0;
        uintptr_t AttackAction_GetSubMachinegunOneHandMotionId = 0;
        uintptr_t AttackAction_GetStepReloadHandgunOneHandMotionId = 0;
        uintptr_t AttackAction_GetStepReloadHandgunMotionId = 0;
        uintptr_t AttackAction_ResetMissionPrepareGripMotion = 0;
        uintptr_t AttackAction_GetNormalMotionIdByCategory = 0;
        uintptr_t AttackAction_GetAssaultMotionIdTable = 0;
        uintptr_t AttackAction_GetHandgunMotionIdTable = 0;
        uintptr_t AttackAction_GetSubMachinegunMotionIdTable = 0;
        uintptr_t AttackAction_GetSniperRifleMotionIdTable = 0;
        uintptr_t AttackAction_GetShotgunMotionIdTable = 0;
        uintptr_t AttackAction_GetShotgunStepReloadMotionIdTable = 0;
        uintptr_t AttackAction_GetMachinegunMotionIdTable = 0;
        uintptr_t AttackAction_GetGrenadeLauncherMotionIdTable = 0;
        uintptr_t AttackAction_GetMissileMotionIdTable = 0;
        uintptr_t AttackAction_GetChaseMotionIdTable = 0;
        uintptr_t EquipMotionDataTable_ReloadEquipMotionData2 = 0;
        uintptr_t MissionPreparationCallbackImpl_Start_PlayBgmEvent = 0;
        uintptr_t MissionPreparationCallbackImpl_Update_StopBgmEvent = 0;
        uintptr_t MissionPreparationCallbackImpl_Update_StopBgmEvent2 = 0;
        uintptr_t MissionPreparationCallbackImpl_Start = 0;
        uintptr_t MissionPreparationCallbackImpl_Update = 0;

        uintptr_t Soldier2FaceSystem_Instance = 0;

        uintptr_t StaffControllerImpl_AddStaffShort = 0;
        uintptr_t StaffControllerImpl_AddStaffFull = 0;
        uintptr_t StaffIdToFaceIdLua = 0;
        uintptr_t StaffControllerImpl_StaffSeedToFaceTexturePathId = 0;
        uintptr_t StaffListCallbackImpl_RefreshFaceRecord = 0;
        uintptr_t WindowFactoryCollector_FindWindowFactory = 0;
        uintptr_t WindowFunction_CreateWindow = 0;
        uintptr_t CommonDataManager_SetSubjectiveWindowHandle = 0;
        uintptr_t WindowFactoryCollector_FindWindowFactory_Implementation = 0;
        uintptr_t CommonDataManager_SetSubjectiveWindowHandle_Implementation = 0;
        uintptr_t FaceFovaRead_Calc = 0;
        uintptr_t FaceFovaRead_ConvertExtend = 0;
        uintptr_t FaceFovaRead_Convert = 0;
        uintptr_t FaceFovaRead_GetFpkArray = 0;
        uintptr_t FaceFovaRead_GetFpk = 0;
        uintptr_t FaceFovaRead_GetPathArray = 0;
        uintptr_t FaceFovaRead_GetPath = 0;
        uintptr_t FaceFovaRead_GetFpkList = 0;
        uintptr_t FaceFovaRead_GetSpecialFpkList = 0;
        uintptr_t FaceFovaRead_GetHairPath = 0;
        uintptr_t FaceFovaRead_PushFpk = 0;
        uintptr_t FaceFova_SetDefinitionTableImpl = 0;
        uintptr_t FaceFova_SetFileTableImpl = 0;
        uintptr_t EquipParameterTablesImpl_GetAmmoParameterBlock = 0;
        uintptr_t WalkerGearSystemImpl_GetSubWeaponLoadAmmoMax = 0;
        uintptr_t BuddyQuietEquipControllerImpl_SetupCurrentSlot = 0;
        uintptr_t HostageEquipControllerImpl_SetupCurrentSlot = 0;
        uintptr_t SoldierEquipControllerImpl_SetupCurrentSlot = 0;
        uintptr_t BossQuietEquipControllerImpl_SetupCurrentSlot = 0;
        uintptr_t EquipDevelopControllerImpl_GetBulletCountMagazine = 0;
        uintptr_t EquipDevelopControllerImpl_GetBulletCountMax = 0;
        uintptr_t EquipDevelopControllerImpl_GetBulletType = 0;
        uintptr_t GunPartsDescWide_receiver = 0;
        uintptr_t GunPartsDescWide_barrel = 0;
        uintptr_t GunPartsDescWide_magazine = 0;
        uintptr_t GunPartsDescWide_stock = 0;
        uintptr_t GunPartsDescWide_muzzleOption = 0;
        uintptr_t GunPartsDescWide_sight = 0;
        uintptr_t GunPartsDescWide_sight2 = 0;
        uintptr_t GunPartsDescWide_laserFlash1 = 0;
        uintptr_t GunPartsDescWide_laserFlash2 = 0;
        uintptr_t GunPartsDescWide_underBarrel = 0;
        uintptr_t GunPartsDescWide_underBarrelCmp = 0;
        uintptr_t InventoryImpl_CalcAmmoAndStockIfPickUp = 0;
        uintptr_t EquipIdTable_GetRootParameterId = 0;
        uintptr_t DeployControllerImpl_GenerateDeployMission = 0;
        uintptr_t DeployControllerImpl_AddLogMission = 0;
        uintptr_t DeployControllerImpl_GenerateDeployMissionInfoOffline = 0;
        uintptr_t DeployControllerImpl_CreateDeployMissionSeedParam = 0;
        uintptr_t DeployControllerImpl_GetDeployMissionNameMessageId = 0;
        uintptr_t DeployControllerImpl_GetDeployMissionCaptionMessageId = 0;
        uintptr_t DeployControllerImpl_GetDeployMissionNameKeyId = 0;
        uintptr_t DeployControllerImpl_RefreshMissionList = 0;
        uintptr_t DeployControllerImpl_GetNextRandomMissionId = 0;
        uintptr_t DeployControllerImpl_CompletedDeployMissionOffline = 0;
        uintptr_t MotherBaseManagementSystemImpl_SetupAfterRestoreFromSVars = 0;
        uintptr_t PoolRewardControllerImpl_GeneratePoolRewardsInfo = 0;
        uintptr_t CombatDeployCallbackImpl_RefreshMissionIconInGlobe = 0;
        uintptr_t NoticeAntiBuddyWalkerGearAiImpl_ShouldSleep = 0;
        uintptr_t CombatAntiBuddyWalkerGearAiImpl_UpdatePrimaryTarget = 0;
        uintptr_t AttackActionImpl_HasOtherSoldierOnVec = 0;
        uintptr_t Bullet3Impl_OnOffenseHit = 0;
        uintptr_t DamageHandlerImpl_ProcessDamages = 0;
        uintptr_t Soldier2Impl_UpdateMarkers = 0;
        uintptr_t CombatDeployMissionListRecordCallFunc_Refresh = 0;
        uintptr_t SetRevengeIcon = 0;
        uintptr_t CombatAntiBuddyWalkerGearAiImpl_StepSurprise = 0;
        uintptr_t AkMemoryMgr_Malloc = 0;
        uintptr_t AkMemoryMgr_Malign = 0;
        uintptr_t AkMemoryMgr_Free = 0;
        uintptr_t AkMemoryMgr_s_pMemPools = 0;
        uintptr_t Ak_g_DefaultPoolId = 0;
        uintptr_t Ak_g_PrepareEventMemoryPoolID = 0;
        uintptr_t Soldier2Util_SearchActiveMember = 0;
        uintptr_t CombatAntiBuddyWalkerGearAiImpl_UpdateSnipeTarget = 0;
        uintptr_t WalkerGearActionImpl_HasOtherSoldierOnVec = 0;
        uintptr_t CombatDeployCallbackImpl_RefreshMissionList = 0;
        uintptr_t MbTopMenuSelectorEv_UpdateMenuNotice = 0;
        uintptr_t NoticeControllerImpl_CheckNoiseNoticeSoldier = 0;
        uintptr_t NoticeDownComradeAiImpl_ShouldSleep = 0;
        uintptr_t VehicleNoticeDownComradeAiImpl_ShouldSleep = 0;
        uintptr_t RideVehicleActionImpl_HasOtherSoldierOnVec = 0;
        uintptr_t LifeControllerImpl_SendStatusMessage = 0;
        uintptr_t CombatAntiBuddyWalkerGearAiImpl_CheckGrenade = 0;
        uintptr_t IDroidActionPluginImpl_CloseState = 0;
        uintptr_t IDroidActionPluginImpl_Deactivate = 0;
        uintptr_t EquipSystemImpl_SetItemVisible = 0;
        uintptr_t IDroidActionPluginImpl_IsPutAwayInterrupted = 0;
        uintptr_t IDroidActionPluginImpl_IsOtherAvatar = 0;
        uintptr_t MotionGraphBlendNodeTraverser_LeafClipRead = 0;
        uintptr_t AnimControl_GetAnimFileFromGaniPath = 0;
        uintptr_t ActionPluginImpl_GetStance = 0;
        uintptr_t CureActionPluginImpl_StateCureStart_StanceCallBody = 0;
        uintptr_t CureActionPluginImpl_StateCureStart_StanceCallArm = 0;
        uintptr_t CureActionPluginImpl_StateCure_StanceCall = 0;
        uintptr_t CureActionPluginImpl_StateCureEnd_StanceCall = 0;
        uintptr_t BasicActionImpl_SelectWakeupState = 0;
        uintptr_t CureActionPluginImpl_FinishCureSprayEffect = 0;
        uintptr_t CureActionPluginImpl_StateCure = 0;
        uintptr_t CallMenuManagerImpl_UpdateCommandSetInterrogation = 0;
        uintptr_t Soldier2Impl_ProcessSignal = 0;
        uintptr_t NoticeNoiseAiImpl_SearchSpecificComrade = 0;
        uintptr_t NoticeControllerImpl_CheckForceNotice = 0;
        uintptr_t CallMenuManagerImpl_UpdateInShowing = 0;
        uintptr_t CallMenuCommandSet_UpdateState = 0;
        uintptr_t CallMenuManagerImpl_UpdateCommandSetRescueBoy = 0;
        uintptr_t ActionController_SetCommand = 0;
        uintptr_t NoticeNoiseAiImpl_StepMovePause = 0;
        uintptr_t HostageCoreAiImpl_SetSpecialActionParam = 0;
        uintptr_t HostageSpecialActionImpl_State_SpecialIdle = 0;
        uintptr_t CallMenuImpl_UpdateOption = 0;
        uintptr_t CallMenuImpl_UpdateSelectDoc = 0;
        uintptr_t CallMenuManagerImpl_ShowCallMenu = 0;
        uintptr_t GrMaterialManagerInstance = 0;
        uintptr_t GrShaderTechniqueManagerInstance = 0;
        uintptr_t RadioControllerImpl_StepRadioNoResponse = 0;
        uintptr_t RadioControllerImpl_StepRadioWaitResponse = 0;
        uintptr_t CommandPost2Impl_Controller_RequestRadioByMember = 0;
        uintptr_t CommandPost2Impl_Controller_SetRadioMemberReady = 0;
        uintptr_t CommandPost2Impl_Controller_ReleaseRadioByMember = 0;
        uintptr_t CommandPost2Impl_FinishRadio = 0;
        uintptr_t RadioControllerImpl_StepRadio = 0;
        uintptr_t CommandPost2Impl_Controller_UpdateRadio = 0;
        uintptr_t CommandPost2Impl_RequestRadioByCp = 0;
        uintptr_t RadioControllerImpl_StepIdle = 0;
        uintptr_t CpRadioService_ConvertSpeechLabelToRadioType = 0;
        uintptr_t AiControllerImpl_WakeNode = 0;
        uintptr_t AiControllerImpl_SleepNode = 0;
        uintptr_t Soldier2InterrogateUtil_IsTranslatable = 0;
        uintptr_t Buddy2BlockControllerImpl_GetPathAtBuddyType = 0;
        uintptr_t Buddy2BlockControllerImpl_GetBuddyQuietWeaponFile = 0;
        uintptr_t Buddy2BlockControllerImpl_GetBuddyQuietFovaFile = 0;
        uintptr_t Buddy2BlockControllerImpl_QuietFv2IdLoad = 0;
        uintptr_t EquipDevCtrl_GetBuddyPartsDevelopInfoIndex = 0;
        uintptr_t EquipDevCtrl_GetBuddyPartsId = 0;
        uintptr_t EquipDevCtrl_GetBuddyPartsKind = 0;
        uintptr_t BuddyServiceImpl_Instance = 0;
        uintptr_t BuddyServiceImpl_ChangeHorseSettings = 0;
        uintptr_t BuddyServiceImpl_ChangeDogSettings = 0;
        uintptr_t BuddyServiceImpl_GetQuietEquipIdByWeaponType = 0;
        uintptr_t BuddyServiceImpl_IsUseSuppressorByWeaponType = 0;
        uintptr_t BuddyServiceImpl_ChangeQuietCostume_Clamp = 0;
        uintptr_t BuddyServiceImpl_SetVarsQuietCostume_Clamp = 0;
        uintptr_t BuddyServiceImpl_ChangeQuietWeaponType_Clamp = 0;
        uintptr_t BuddyServiceImpl_SetVarsQuietWeaponType_Clamp = 0;
        uintptr_t BuddyServiceImpl_ChangeHorseSettings_TypeCheck = 0;
        uintptr_t BuddyServiceImpl_ChangeDogSettings_GearCheck = 0;
        uintptr_t BuddyQuiet2Impl_WeaponCommand_Clamp = 0;
        uintptr_t BuddyQuiet2Impl_SetBruise_CostumeCheck = 0;
        uintptr_t ItemSelector_QuietCostumeListGate = 0;
        uintptr_t ItemSelector_QuietWeaponListGate = 0;
        uintptr_t DamageAiImpl_StepBitDamage_DogGearCheck = 0;
        uintptr_t BuddyQuietCombatAiImpl_SendFireSignal = 0;
        uintptr_t BuddyQuietEquipControllerImpl_InitCurrentSlot = 0;
        uintptr_t BuddyQuietEquipControllerImpl_Update = 0;
        uintptr_t BuddyQuietEquipControllerImpl_SetMotion = 0;
        uintptr_t Buddy2BlockControllerImpl_GetFileForPathFromBuddyBlock = 0;
        uintptr_t Buddy2BlockController_Instance = 0;
        uintptr_t HorseActionCoreImpl_ExecDefenseCallback_TackCheck = 0;
        uintptr_t HorseBasicAction2Impl_UpdateGallopMotionSpeed_TackCheck = 0;
        uintptr_t HorseBasicAction2Impl_EnterOffenseTarget_TackCheck = 0;
        uintptr_t Horse2_MotionSoundTackSwitch = 0;
        uintptr_t HorseBasicAction2Impl_Execute = 0;
        uintptr_t BuddyServiceImpl_DogCommandVariant_GearCheck = 0;
        uintptr_t BuddyServiceImpl_InitBuddyCommandForStatus_GearCheck = 0;
        uintptr_t BuddyDogDamageHandlerImpl_ProcessDamages_GearCheck = 0;
        uintptr_t BuddyDogDamageHandlerImpl_IsNeedReaction_GearCheck = 0;
        uintptr_t BuddyDogDamageHandlerImpl_CallDamageEffect_GearCheck = 0;
        uintptr_t BuddyDogDamageHandlerImpl_CallDamageSound_GearCheck = 0;
        uintptr_t BuddyDogEscapeIdleAiImpl_StepSearch_GearCheck = 0;
        uintptr_t BuddyDogNoticeDamageAiImpl_Setup_GearCheck = 0;
        uintptr_t BuddyDogAttackAiImpl_ReTarget_GearCheck = 0;
        uintptr_t SoldierNoticeBuddyDogAiImpl_StepAware_GearCheck = 0;
        uintptr_t BuddyDog2Impl_Initialize_GearCopy = 0;
        uintptr_t SimplePartsEffectControllerImpl_CreateEffectImpl = 0;
        uintptr_t FoxFxEffect_Create = 0;
        uintptr_t Player2System = 0;
        uintptr_t AnimFile_GetEventData = 0;
        uintptr_t RealizedEquipObjectImpl_MotionEventCallbackImpl = 0;
        uintptr_t RealizedEquipObjectImpl_EjectCasing = 0;
        uintptr_t AttackActionImpl_SetEquipMotion = 0;
        uintptr_t AttackActionImpl_MotionEventCallback = 0;
        uintptr_t Mission_GetPackagePaths = 0;   // tpp::gm::impl::(anon)::GetPackagePaths(fox::Array<fox::Path>*, u16 missionCode)
        uintptr_t FoxPathArray_PushBack = 0;     // fox::Array<fox::Path>::PushBack(const fox::Path&)
        uintptr_t FoxPath_FromCString = 0;       // fox::Path::Path(const char*)
        uintptr_t EventCallback_ExecCallback = 0;
        uintptr_t ItemSelectorCallbackImpl_Update = 0;   // tpp::ui::menu ItemSelectorCallbackImpl::Update(this = list + 0x20)
        uintptr_t Fv2_GetFileFromPartsBlock = 0;   // Player2BlockController::GetFileFromPartsBlockNew(ctrl, index, path)
        uintptr_t Fv2_PartsBlockController = 0;    // the global that holds that block controller
        uintptr_t Fv2_ApplyFormVariation = 0;      // FormVariationFile2::ApplyFormVariation(file, builder, director, ..., reset, ...)
        uintptr_t Fv2_WorkApplyExtra = 0;          // Fova2ControllerImpl work: what ApplyFormVariationWithFile adds for type 0x11
        uintptr_t EquipIdTableImpl_GetSupportWeaponTypeId = 0;
        uintptr_t EquipParameterTablesImpl_ReloadEquipParameterTablesImpl = 0;
        uintptr_t EquipParameterTablesImpl_SupportWeaponSizePass = 0;    // add rax, 0x2D8 (SupportWeaponParameter block size)
        uintptr_t EquipParameterTablesImpl_SupportWeaponAllocPass = 0;   // lea rax, [rcx+0x2D8] (same block, allocation pass)
        uintptr_t ThrowingImpl_GetBlastParamByEquipId = 0;
        uintptr_t ThrowingImpl_UpdateActionGrenade = 0;
        uintptr_t ThrowingImpl_UpdateActionSmoke = 0;
        uintptr_t ThrowingImpl_UpdateActionMolotovCocktail = 0;
        uintptr_t ThrowingImpl_UpdateActionDecoy = 0;
        uintptr_t ThrowingImpl_UpdateActionKibidango = 0;
        uintptr_t ThrowingImpl_UpdateActionFakeSign = 0;
        uintptr_t ThrowingImpl_UpdateStatus = 0;
        uintptr_t ThrowingSystemImpl_ExecuteSerially = 0;
        uintptr_t ThrowingImpl_ActivateThrowingAtEmptyWork = 0;
        uintptr_t ThrowingImpl_UpdateDeactivation = 0;
        uintptr_t ThrowingImpl_AddReboundNoise = 0;
        uintptr_t ThrowingImpl_PostReboundSound = 0;
        uintptr_t ThrowingImpl_IsObstructSight = 0;
        uintptr_t EquipDevelopControllerImpl_GetDamageType = 0;
        uintptr_t EquipControllerImpl_InitializePlayerAtIndex = 0;
        uintptr_t LoadoutManagerImpl_RestoreLoadoutInfosFromVars = 0;
        uintptr_t LoadoutManagerImpl_RestoreSortieLoadoutInfo = 0;       // restores one slot from the sortieLoadoutInfo buffers
        uintptr_t LoadoutManagerImpl_RestoreReturnHeliLoadoutInfo = 0;   // restores one slot from the returnHeliLoadoutInfo buffers
        uintptr_t PlacedSystemImpl_RestorationPlace = 0;
        uintptr_t SupplyCboxGameObjectImpl_RestoreRequestFromSVars = 0;
        uintptr_t Player2UtilityImpl_GetPickableInfosOfAmmoBox = 0;
        uintptr_t EquipParameterTablesImpl_GetSupportWeaponParameterBlock = 0;
        uintptr_t EquipDevelopControllerImpl_GetDamageAmountRatio = 0;
        uintptr_t EquipDetailsCallbackImpl_SetupSupportParameterDetails = 0;
        uintptr_t IdleActingActionImpl_CallVoice = 0;
        uintptr_t Soldier2SoundControllerImpl_CallVoiceWithoutSync = 0;
        uintptr_t Soldier2SoundControllerImpl_CallVoiceWithSync = 0;
        uintptr_t RadioActionImpl_State_RadioChaffEnd = 0;
        uintptr_t EquipIdTable_GetAmmoStockIdOfSupportOrItem_StockSite = 0;
        uintptr_t MissionPreparationSystemImpl_CalcEquipWeaponItemCost_StockSite = 0;
        uintptr_t LoadoutManagerImpl_StoreLoadoutInfoToPlayerVars_StockSite = 0;
        uintptr_t EquipControllerImpl_InitializePlayerAtIndex_StockSite = 0;
        uintptr_t PickUpActionPluginImpl_CheckLoadableForSupportOrItem_StockSite = 0;
        uintptr_t PickUpActionPluginImpl_DoPickUpPlaced_SlotStockSite = 0;
        uintptr_t PickUpActionPluginImpl_DoPickUpPlaced_PickedStockSite = 0;
        uintptr_t PickUpActionPluginImpl_DoPickUpWithPickableInfo_StockSite = 0;
        uintptr_t ItemSelectorCallbackImpl_GetCurrentAmmoFromVarsForSupportWeapon_StockSite = 0;
        uintptr_t EquipParameterTablesImpl_GetAmmoParameterBlock_StockSite = 0;
        uintptr_t RadioSpeechHandlerImpl_CallPart = 0;
        uintptr_t RangeAttackSystemImpl_ActivateChaff = 0;
        uintptr_t SoundControllerImpl_IsPlayingVoice = 0;
        uintptr_t MortarActionImpl_State_WaitAction = 0;
        uintptr_t UnrealMortarActionImpl_State_WaitAction = 0;
        uintptr_t Heli2Impl_Unrealize = 0;
        uintptr_t MortarActionImpl_State_MortarAction = 0;
        uintptr_t UnrealMortarActionImpl_State_MortarAction = 0;
        uintptr_t RadioControllerImpl_StepRadioChaff = 0;
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

    const AddressSet& Get_mst_en_day3800_AddressSet();   // 1.0.15.4 english
    const AddressSet& Get_mst_jp_day3800_AddressSet();   // 1.0.15.4 japanese
    const AddressSet& Get_mst_en_day1820_AddressSet();   // 1.0.15.3 english
    const AddressSet& Get_mst_jp_day1820_AddressSet();   // 1.0.15.3 japanese
    const AddressSet& Get_mst_en_day3900_AddressSet();   // day3900 english
    const AddressSet& Get_mst_jp_day3900_AddressSet();   // day3900 japanese
    GameBuild DetectGameBuildFromVersionInfo(HMODULE hGame);
    bool ResolveAddressSet(HMODULE hGame);
    void InstallCrashHandler();

    inline const char* GetGameBuildName(GameBuild build)
    {
        switch (build)
        {
        case GameBuild::En_1_0_15_3: return "EN 1.0.15.3";
        case GameBuild::Jp_1_0_15_3: return "JP 1.0.15.3";
        case GameBuild::En_1_0_15_4: return "EN 1.0.15.4";
        case GameBuild::Jp_1_0_15_4: return "JP 1.0.15.4";
        case GameBuild::En_1_0_15_4a:  return "EN 1.0.15.4a (day3900)";
        case GameBuild::Jp_1_0_15_4a:  return "JP 1.0.15.4a (day3900)";
        default:                     return "Unknown";
        }
    }

    inline bool IsEnglishBuild(GameBuild b)  { return b == GameBuild::En_1_0_15_3 || b == GameBuild::En_1_0_15_4 || b == GameBuild::En_1_0_15_4a; }
    inline bool IsJapaneseBuild(GameBuild b) { return b == GameBuild::Jp_1_0_15_3 || b == GameBuild::Jp_1_0_15_4 || b == GameBuild::Jp_1_0_15_4a; }

    inline bool IsEn154Family(GameBuild b) { return b == GameBuild::En_1_0_15_4 || b == GameBuild::En_1_0_15_4a; }
    inline bool IsJp154Family(GameBuild b) { return b == GameBuild::Jp_1_0_15_4 || b == GameBuild::Jp_1_0_15_4a; }
}

#define gGameBuild (::AddressSetRuntime::GetGameBuild())
#define gAddr (::AddressSetRuntime::GetAddressSet())
#define ResolveAddressSet (::AddressSetRuntime::ResolveAddressSet)
#define InstallCrashHandler (::AddressSetRuntime::InstallCrashHandler)
#define GetGameBuildName (::AddressSetRuntime::GetGameBuildName)
