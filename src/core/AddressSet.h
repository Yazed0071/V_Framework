#pragma once

#include <Windows.h>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>

#include "log.h"

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
        uintptr_t AddCassetteTapeTrack = 0;
        uintptr_t AddNoise = 0;
        uintptr_t AddNoticeInfo = 0;
        uintptr_t ArrayBaseFree = 0;
        uintptr_t BeginSoundSystem = 0;
        uintptr_t CallImpl = 0;
        uintptr_t CallWithRadioType = 0;
        uintptr_t CassettePlayerVtable = 0;
        uintptr_t CassetteStart = 0;
        uintptr_t CheckSightNoticeHostage = 0;
        uintptr_t CollectGotTapes = 0;
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
        uintptr_t GetPlayingTime = 0;
        uintptr_t GetPlayingTrackId = 0;
        uintptr_t GetQuarkSystemTable = 0;
        uintptr_t GetTrackInfoByName = 0;
        uintptr_t GetVoiceLanguage = 0;
        uintptr_t GetVoiceParamWithCallSign = 0;
        uintptr_t IsGotCassetteTapeTrack = 0;
        uintptr_t KernelAllocAligned = 0;
        uintptr_t LoadPlayerVoiceFpk = 0;
        uintptr_t LoadingScreenOrGameOverSplash2 = 0;
        uintptr_t MusicManager_s_instance = 0;
        uintptr_t PathHashCode = 0;
        uintptr_t PauseMusicPlayer = 0;
        uintptr_t PlayOrPauseSelectedTrack = 0;
        uintptr_t RequestCorpse = 0;
        uintptr_t ResumeMusicPlayer = 0;
        uintptr_t SetCassetteTapeTrackNewFlag = 0;
        uintptr_t SetCurrentAlbum = 0;
        uintptr_t SetEquipBackgroundTexture = 0;
        uintptr_t SetLuaFunctions = 0;
        uintptr_t SetTextureName = 0;
        uintptr_t SetupMusicInfos = 0;
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

        uintptr_t RegisterConstantEquipIdHashTable = 0;
        uintptr_t EquipParameterTablesImpl_ReloadEquipParameterTablesImpl2 = 0;
        uintptr_t EquipIdTableImpl_ReloadEquipIdTable = 0;
        uintptr_t TppMotherBaseManagement_RegCstDev = 0;
        uintptr_t TppMotherBaseManagement_RegFlwDev = 0;
        uintptr_t EquipIdTableImpl_GetSupportWeaponTypeId = 0;
        uintptr_t DeclareAMs = 0;
        uintptr_t ReloadEquipMotionData = 0;
        uintptr_t GetIconFtexPath = 0;
        uintptr_t LoadingTipsEv_UpdateActPhase = 0;

        uintptr_t AK_SoundEngine_SetRTPCValue = 0;
        uintptr_t Fox_Sd_ConvertParameterID = 0;


        uintptr_t EquipParameterTablesImpl_Instance = 0;


        uintptr_t EquipIdTableImpl_AddToEquipIdTable = 0;


        uintptr_t EquipIdTableImpl_s_internalInfoList = 0;


        uintptr_t LoadPlayerPartsParts              = 0;
        uintptr_t LoadPlayerPartsFpk                = 0;
        uintptr_t LoadPlayerCamoFpk                 = 0;
        uintptr_t LoadPlayerSnakeBlackDiamondFpk    = 0;
        uintptr_t LoadPlayerBionicArmFpk            = 0;
        uintptr_t LoadPlayerSnakeFaceFpk            = 0;
        uintptr_t Player2BlockController_LoadPartsNew = 0;
        uintptr_t UpdatePartsStatus                 = 0;
        uintptr_t EquipController_SetHandSlotEnabled = 0;
        uintptr_t Sys_IsArtificialHandEnabled       = 0;
        uintptr_t Sys_IsArtificialHandEnabledForCurrentPlayerType = 0;
        uintptr_t Player2GameObjectImpl_ProcessSignal = 0;
        uintptr_t Player2Impl_SetUpParts = 0;


        uintptr_t ResolveSuitToPartsType            = 0;


        uintptr_t MissionPrep_RequestToChangePlayerPartsInMissionPreparationMode = 0;
        uintptr_t Player2UtilityImpl_CommitWrapper  = 0;


        uintptr_t ItemSelectorCallbackImpl_DecideActMissionPreparationSetEquipMode = 0;
        uintptr_t ItemSelectorCallbackImpl_DecideActMotherBaseDeviceSupportDropMode = 0;
        uintptr_t SupplyDropSuitSetup               = 0;


        uintptr_t SupplyCboxGameObjectImpl_RestoreRequestFromSVars = 0;


        uintptr_t Player2UtilityImpl_SetSuitAndHandConditionWithLoadoutInfo = 0;


        uintptr_t Player2UtilityImpl_LoadoutApplyAfterSetSuit = 0;


        uintptr_t Player2UtilityImpl_SetInitialConditionWithLoadoutInfo = 0;


        uintptr_t CharacterSelectorCallbackImpl_ChangeDetailsWindowBuddySelect = 0;


        uintptr_t CharacterSelectorCallbackImpl_OpenBuddySelect = 0;


        uintptr_t SuitList_GetDevelopedCount    = 0;
        uintptr_t SuitList_FillDevelopedFlowIxs = 0;
        uintptr_t SuitList_GetSuitInfoTable     = 0;


        uintptr_t SupplyCboxSystemImpl_Reset = 0;


        uintptr_t SupplyCboxActionPluginImpl_StateHandler1 = 0;


        uintptr_t SupplyCboxSystemImpl_RequestToDropImpl = 0;


        uintptr_t SupplyCboxSystemImpl_OnDropTimerTick = 0;


        uintptr_t SupplyCboxSystemImpl_SettledHandler = 0;
        uintptr_t CharacterSelectorCallbackImpl_StoreCurrentCharacterSuitAndHeadPartsInfo = 0;
        uintptr_t ResourceTable_DoesNeedFaceFova           = 0;
        uintptr_t ResourceTable_DoesNeedFaceFovaForAvatar  = 0;


        uintptr_t CamoSystemObject                  = 0;


        uintptr_t GetSuitVariation                  = 0;
        uintptr_t HeadOptionTableLookup             = 0;
        uintptr_t HasHeadOptions                    = 0;
        uintptr_t HeadOptionIndexGetter             = 0;
        uintptr_t SuitCatalog_FindHeadOptionRow     = 0;
        uintptr_t FetchCurrentHeadOptionKey         = 0;


        uintptr_t MissionPrep_GetSelectionNum       = 0;
        uintptr_t MissionPrep_IsEnableCurrentHeadOption = 0;
        uintptr_t MissionPrep_UpdateLoadMark        = 0;


        uintptr_t MissionPrepSystem_IsEnableHeadOptionSuit = 0;


        uintptr_t Player_ConverFaceIdWithFaceEquipId = 0;


        uintptr_t CamouflageController_ExecSuitCorrect = 0;


        uintptr_t CamoufParamInfo_GetCamoufValue = 0;


        uintptr_t SetupCharacterSlotSelectPrefabListElement = 0;
        uintptr_t AddListSuit                       = 0;
        uintptr_t IsEnableCurrentSuit               = 0;
        uintptr_t SetupEquipPanelParam              = 0;


        uintptr_t ItemSelectorCallbackImpl_SetInDecideOpen = 0;


        uintptr_t ItemSelectorCallbackImpl_SetupPrefabListElement = 0;


        uintptr_t ItemSelector_AddListBandana = 0;


        uintptr_t ItemSelectorRecordCallFunc_UpdateRecords = 0;


        uintptr_t LoadPlayerCamoFv2                = 0;
        uintptr_t LoadPlayerSnakeBlackDiamondFv2   = 0;
        uintptr_t LoadPlayerBionicArmFv2           = 0;
        uintptr_t LoadPlayerSnakeFaceFv2           = 0;


        uintptr_t GetCurrentSuitFlowIndex           = 0;
        uintptr_t GetEquipIdFromLoadoutInfo         = 0;
        uintptr_t IsEquipDeveloped                  = 0;
        uintptr_t SetItemDetail                     = 0;
        uintptr_t SendTrigger                       = 0;

        // Tiny one-line accessor:
        //   byte FUN_14951ed70(longlong tableBase, ushort flowIndex)
        //   { return *(byte*)(tableBase + flowIndex*0x68 + 0x15) >> 4 & 1; }
        // Reads bit 4 of byte +0x15 in a 0x68-stride suit info entry. Used
        // by the develop menu's list-build callback. Crashes when called
        // with flowIndex >> 0x400 (observed param_2 = 0x4000 = 16384). We
        // hook it with a bounds check so the bad iteration becomes a safe
        // no-op (returns 0 for out-of-range indices) instead of crashing.
        // Root cause of the bad index is unidentified — likely a vanilla
        // overflow when total develop entries (vanilla + custom) exceed
        // some internal threshold.
        uintptr_t SuitInfoBitFlag_AccessorPlus15Bit4 = 0;


        uintptr_t IsEquipSuit                       = 0;


        uintptr_t EquipDevelopCallbackImpl_SetSupplyCBoxInfo = 0;


        uintptr_t TornadoDualPatch                  = 0;


        uintptr_t RealizedSahelan2Impl_Realize      = 0;
        uintptr_t RealizedSahelan2Impl_SetFovaImpl  = 0;
        uintptr_t FormVariationFile2_ApplyOnlyMeshAndTextureVariation = 0;


        uintptr_t RealizedSecurityCamera2Impl_SetFova = 0;

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

    inline const AddressSet& GetEnglishAddressSet()
    {
        static const AddressSet value =
        {
            0x1466A5770ull, // AddCassetteTapeTrack
            0x14147F240ull, // AddNoise
            0x1414DCB60ull, // AddNoticeInfo
            0x140015EF0ull, // ArrayBaseFree
            0x140989340ull, // BeginSoundSystem
            0x1473CFCD0ull, // CallImpl
            0x1473CFF10ull, // CallWithRadioType
            0x142285780ull, // CassettePlayerVtable
            0x149310440ull, // CassetteStart
            0x1414E1090ull, // CheckSightNoticeHostage
            0x149309EA0ull, // CollectGotTapes
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
            0x14614A4E0ull, // GetPlayingTime
            0x14614AA30ull, // GetPlayingTrackId
            0x140BFF3F0ull, // GetQuarkSystemTable
            0x14614C0C0ull, // GetTrackInfoByName
            0x1404D2AD0ull, // GetVoiceLanguage
            0x140DA3170ull, // GetVoiceParamWithCallSign
            0x1466EC350ull, // IsGotCassetteTapeTrack
            0x140015F20ull, // KernelAllocAligned
            0x146867240ull, // LoadPlayerVoiceFpk
            0x145CD0630ull, // LoadingScreenOrGameOverSplash2
            0x142BFFAC8ull, // MusicManager_s_instance
            0x14C1BD5D0ull, // PathHashCode
            0x140972C70ull, // PauseMusicPlayer
            0x140EF6BD0ull, // PlayOrPauseSelectedTrack
            0x140A69070ull, // RequestCorpse
            0x1409739E0ull, // ResumeMusicPlayer
            0x140AAC670ull, // SetCassetteTapeTrackNewFlag
            0x140EF7A50ull, // SetCurrentAlbum
            0x145F236F0ull, // SetEquipBackgroundTexture
            0x1408D78A0ull, // SetLuaFunctions
            0x141DC78F0ull, // SetTextureName
            0x140974880ull, // SetupMusicInfos
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

            0x142C24C90ull, // RegisterConstantEquipIdHashTable
            0x140A41410ull, // EquipParameterTablesImpl_ReloadEquipParameterTablesImpl2
            0x1464B6740ull, // EquipIdTableImpl_ReloadEquipIdTable
            0x1466F3B10ull, // TppMotherBaseManagement_RegCstDev
            0x1466F4600ull, // TppMotherBaseManagement_RegFlwDev
            0x140A29FE0ull, // EquipIdTableImpl_GetSupportWeaponTypeId
            0x1464AE4F0ull, // DeclareAMs
            0x1463B2BF0ull, // ReloadEquipMotionData (FUN_1463b2bf0; reads arg.MotionDataTable into +0x142a6b408 buffer)
            0x145E62540ull, // GetIconFtexPath
            0x145ccfcc0ull, // LoadingTipsEv_UpdateActPhase (overrides 0x9d8/0x9e0 w/ DD logo)
            0x14033d520ull, // AK_SoundEngine_SetRTPCValue (thunk → AK::SoundEngine::SetRTPCValue)
            0x14032adf0ull, // Fox_Sd_ConvertParameterID (thunk → fox::sd::ConvertParameterID; RTPC/Switch/State name hash)
            0x142A711F0ull, // EquipParameterTablesImpl_Instance
            0x140A29730ull, // EquipIdTableImpl_AddToEquipIdTable
            0x142C20FB0ull, // EquipIdTableImpl_s_internalInfoList (parts-path array, 0x289 entries × 0x18 bytes)


            0x146865F80ull, // LoadPlayerPartsParts
            0x146866C80ull, // LoadPlayerPartsFpk
            0x146864180ull, // LoadPlayerCamoFpk
            0x146864E30ull, // LoadPlayerSnakeBlackDiamondFpk
            0x140AE90F0ull, // LoadPlayerBionicArmFpk (leaf w/ hardcoded partsType whitelist 0..0x19; custom range 0x40+ rejected, hook substitutes 0x01)
            0x140AE8DF0ull, // LoadPlayerSnakeFaceFpk (leaf w/ hardcoded partsType whitelist; same fix shape as BionicArm)
            0x1409B3B60ull, // Player2BlockController_LoadPartsNew
            0x1409CC380ull, // UpdatePartsStatus
            0x1411B0D10ull, // EquipController_SetHandSlotEnabled (gameplay arm input gate; overridden when custom outfit has enableArm=true)
            0x1409C45C0ull, // Sys_IsArtificialHandEnabled (gates the per-frame arm-render dispatch in FUN_1412a2f80; same partsType whitelist as the leaf loaders, hook overrides to 1 for custom outfits with enableArm=true)
            0x141E02D80ull, // Sys_IsArtificialHandEnabledForCurrentPlayerType (live-state variant; reads QuarkSystemTable+0x98+0x10+0xfb/+0xf8; same whitelist; many callers across the codebase consult this for the live player's arm state)
            0x1409C5D00ull, // Player2GameObjectImpl_ProcessSignal (huge per-player signal dispatcher; we hook to spoof partsType for signal 0x8483a342fa61 which gates InitLoadPlayerFv2s on partsType<0x1C — without this, custom outfits never get the Fv2 attachments wired into the visible scene → arm asset loads but never renders)
            0x1409CA560ull, // Player2Impl_SetUpParts (named EXE: tpp::gm::player::impl::Player2Impl::SetUpParts(this, slot, playerType, partsType, camo, armType, faceId, AvatarInfo*); called from UpdatePartsStatus's cVar13==1 branch with armType read from byte_arrays+0x58+slot. Our UpdatePartsStatus pre-orig zeroing for cascade prevention causes that byte to be 0 at function entry, so SetUpParts→RegisterFilesForArm registers Tier 0 (no arm assets). Hook overrides armType=0 → cached liveTier when slot has custom partsType + enableArm.)
            0x141E02930ull, // ResolveSuitToPartsType
            0x14973DA60ull, // MissionPrep_RequestToChangePlayerPartsInMissionPreparationMode
            0x1462B6590ull, // Player2UtilityImpl_CommitWrapper (3-arg)
            0x1416A3670ull, // ItemSelectorCallbackImpl_DecideActMissionPreparationSetEquipMode
            0x1416A4280ull, // ItemSelectorCallbackImpl_DecideActMotherBaseDeviceSupportDropMode
            0x1416A7610ull, // SupplyDropSuitSetup
            0x140ACA230ull, // SupplyCboxGameObjectImpl_RestoreRequestFromSVars
            0x1409DEFE0ull, // Player2UtilityImpl_SetSuitAndHandConditionWithLoadoutInfo
            0x1462C93F0ull, // Player2UtilityImpl_LoadoutApplyAfterSetSuit (FUN_1462c93f0; calls SetSuit + applies slot loadout via vtable[0x218])
            0x1462C7670ull, // Player2UtilityImpl_SetInitialConditionWithLoadoutInfo (THE actual SetSuit-caller wrapper used by supply-drop; clears weapon slots when info[0xBC] keep-bits clear AND preserve=0)
            0x14163E5F0ull, // CharacterSelectorCallbackImpl_ChangeDetailsWindowBuddySelect (UNIFORMS-row text+icon refresh on buddy select)
            0x14163ECD0ull, // CharacterSelectorCallbackImpl_OpenBuddySelect (companion: initial-open path, same partsType→hash translation)
            0x140F660C0ull, // SuitList_GetDevelopedCount (vtable[0x230] of OutfitListInject's sub50+0xAC8 — runtime-verified stable across runs)
            0x140F65F70ull, // SuitList_FillDevelopedFlowIxs (vtable[0x240] of same object)
            0x14024D330ull, // SuitList_GetSuitInfoTable (vtable[0x718] of OutfitListInject's sub58)
            0x1415C5270ull, // SupplyCboxSystemImpl_Reset
            0x1412A2F80ull, // SupplyCboxActionPluginImpl_StateHandler1 (phase-2 handler)
            0x14A3A9030ull, // SupplyCboxSystemImpl_RequestToDropImpl (mgsvtpp.exe.c:2835510 — auto-burst vs interactive-pickup decision; clear bit 0x20 at this+0x124 to force interactive)
            0x14A3A83F0ull, // SupplyCboxSystemImpl_OnDropTimerTick (disproven hypothesis — this fn is only XREF'd from FUN_14a3b1ca0 collision callback; never fires for dev-menu)
            0x14A3A7B30ull, // SupplyCboxSystemImpl_SettledHandler (FUN_14a3a7b30; mode-7 case in Update FUN_1415c2210 — raycast→Reset path is the actual auto-burst trigger)
            0x14A49DA70ull, // CharacterSelectorCallbackImpl_StoreCurrentCharacterSuitAndHeadPartsInfo
            0x140AE84B0ull, // ResourceTable_DoesNeedFaceFova
            0x140AE8500ull, // ResourceTable_DoesNeedFaceFovaForAvatar
            0x142C1BE48ull, // CamoSystemObject (global pointer)
            0x149519E60ull, // GetSuitVariation
            0x1460AF810ull, // HeadOptionTableLookup
            0x1460B9FA0ull, // HasHeadOptions
            0x1460B4300ull, // HeadOptionIndexGetter
            0x140F665A0ull, // SuitCatalog_FindHeadOptionRow
            0x0ull,         // FetchCurrentHeadOptionKey (disabled; dead-end hook)
            0x1416BC2C0ull, // MissionPrep_GetSelectionNum
            0x14A56BA20ull, // MissionPrep_IsEnableCurrentHeadOption
            0x14A5795C0ull, // MissionPrep_UpdateLoadMark
            0x1460B9FA0ull, // MissionPrepSystem_IsEnableHeadOptionSuit (real body, NOT the JMP thunk at 0x1409575D0). retail vtable[0x460] = 0x1409575D0 which JMPs to 0x1460B9FA0. GetSelectionNum (retail 0x1416BC2C0) calls this in mode 0/2 to decide 2 vs 3 sub-panel rows. We override to 1 when live partsType is a registered custom outfit declaring HasHeadOptions().
            0x14622A3B0ull, // Player_ConverFaceIdWithFaceEquipId (slot byte → vanilla face-fv2 router for Tier-3-A custom heads)
            0x140FDC5D0ull, // CamouflageController_ExecSuitCorrect (per-update bonus-camo pin hook target)
            0x14691B460ull, // CamoufParamInfo_GetCamoufValue (leaf bonus-table lookup; intercepted for virtual camo ids)
            0x1416BF490ull, // SetupCharacterSlotSelectPrefabListElement
            0x1416A1AA0ull, // AddListSuit
            0x14A56BFA0ull, // IsEnableCurrentSuit
            0x1416C0690ull, // SetupEquipPanelParam


            0x1416A7A30ull, // ItemSelectorCallbackImpl_SetInDecideOpen
            0x1416A9B80ull, // ItemSelectorCallbackImpl_SetupPrefabListElement
            0x14A53C210ull, // ItemSelector_AddListBandana
            0x1416AF270ull, // ItemSelectorRecordCallFunc_UpdateRecords (variant cycle-button label resolver)
            0x146863F80ull, // LoadPlayerCamoFv2
            0x146864C80ull, // LoadPlayerSnakeBlackDiamondFv2
            0x140AE9040ull, // LoadPlayerBionicArmFv2 (leaf w/ hardcoded partsType whitelist 0..0x19; custom range 0x40+ rejected, hook substitutes 0x01)
            0x140AE8CE0ull, // LoadPlayerSnakeFaceFv2 (leaf w/ hardcoded partsType whitelist; same fix shape as BionicArm)
            0x140955C70ull, // GetCurrentSuitFlowIndex
            0x1416BB9C0ull, // GetEquipIdFromLoadoutInfo
            0x14951F860ull, // IsEquipDeveloped
            0x14A56E7F0ull, // SetItemDetail
            0x144B05380ull, // SendTrigger
            0x14951ED70ull, // SuitInfoBitFlag_AccessorPlus15Bit4 (FUN_14951ed70: returns *(byte*)(tableBase+flowIndex*0x68+0x15)>>4&1; crashes on flowIndex >= 0x400 in dev menu when many custom outfits registered — we hook to bounds-check)
            0x140F6D7A0ull, // IsEquipSuit (PT/flowIndex match check used by dev-menu request gate)
            0x141675600ull, // EquipDevelopCallbackImpl_SetSupplyCBoxInfo (R&D MotherBase dev-menu "Request Supply Drop" handler — fires per click, takes flowIndex)
            0x149CFBA54ull, // TornadoDualPatch (2-byte JZ inside UnrealUpdaterImpl::PreUpdate; NOP'd to enable tornado dual)


            0x146acc210ull, // RealizedSahelan2Impl_Realize (mission-gated FOVA dispatch; gate is 0x2b8f at +0x8d into the function)
            0x146acc650ull, // RealizedSahelan2Impl_SetFovaImpl (loads vanilla FOVA via hardcoded hash 0x60887fe72aa5c04b at +0x16)
            0x144a3cbd0ull, // FormVariationFile2_ApplyOnlyMeshAndTextureVariation (7-arg FV2 apply used by SetFovaImpl)


            0x146ab80f0ull, // RealizedSecurityCamera2Impl_SetFova (3-arg variant switcher; reads FV2 ptr from this[0x98 + variant*8], no hardcoded hash, no mission gate)
        };

        return value;
    }

    inline const AddressSet& GetJapaneseAddressSet()
    {
        static const AddressSet value =
        {
            0x1482D77E0ull, // AddCassetteTapeTrack
            0x14147F210ull, // AddNoise
            0x1414DCB30ull, // AddNoticeInfo
            0x0ull, // ArrayBaseFree
            0x0ull, // BeginSoundSystem
            0x0ull, // CallImpl
            0x0ull, // CallWithRadioType
            0x0ull, // CassettePlayerVtable
            0x0ull, // CassetteStart
            0x0ull, // CheckSightNoticeHostage
            0x0ull, // CollectGotTapes
            0x0ull, // ConvertRadioTypeToLabel
            0x0ull, // CopyAndAdjustInfo
            0x0ull, // DecrementPhaseCounter
            0x0ull, // ExecCallback
            0x0ull, // FoxLuaRegisterLibrary
            0x0ull, // FoxPath_Path
            0x0ull, // FoxStrHash32
            0x0ull, // FoxStrHash64
            0x0ull, // GameOverSetVisible
            0x0ull, // GetCurrentMissionCode
            0x0ull, // GetNameIdWithGameObjectId
            0x0ull, // GetPlayingTime
            0x147DE93E0ull, // GetPlayingTrackId
            0x0ull, // GetQuarkSystemTable
            0x147DEA880ull, // GetTrackInfoByName
            0x0ull, // GetVoiceLanguage
            0x0ull, // GetVoiceParamWithCallSign
            0x0ull, // IsGotCassetteTapeTrack
            0x0ull, // KernelAllocAligned
            0x14844E550ull, // LoadPlayerVoiceFpk
            0x1477ED2F0ull, // LoadingScreenOrGameOverSplash2
            0x0ull, // MusicManager_s_instance
            0x0ull, // PathHashCode
            0x147DF6C00ull, // PauseMusicPlayer
            0x0ull, // PlayOrPauseSelectedTrack
            0x0ull, // RequestCorpse
            0x147DFE3B0ull, // ResumeMusicPlayer
            0x0ull, // SetCassetteTapeTrackNewFlag
            0x149CD4320ull, // SetCurrentAlbum
            0x147A8C170ull, // SetEquipBackgroundTexture
            0x0ull, // SetLuaFunctions
            0x0ull, // SetTextureName
            0x0ull, // SetupMusicInfos
            0x0ull, // SoundSystemCtor
            0x0ull, // StateRadio
            0x0ull, // StateRadioRequest
            0x0ull, // State_ComradeAction
            0x0ull, // State_EnterDownHoldup
            0x14AB05D90ull, // State_EnterStandHoldup1
            0x0ull, // State_EnterStandHoldupUnarmed
            0x0ull, // State_RecoveryKick
            0x0ull, // State_RecoveryTouch
            0x0ull, // State_StandEnterRecoverySleepFaintHoldupComradeBySound
            0x0ull, // State_StandHoldupCancelLookToPlayer
            0x0ull, // State_StandRecoveryHoldup
            0x0ull, // StepRadioDiscovery
            0x0ull, // StopMusicPlayer
            0x0ull, // SubtitleManager_Get
            0x0ull, // UpdateOptCamo
            0x0ull, // g_SoundSystem
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


			0x0ull, // RegisterConstantEquipIdHashTable
            0x0ull, // EquipParameterTablesImpl_ReloadEquipParameterTablesImpl2
			0x0ull, // EquipIdTableImpl_ReloadEquipIdTable
			0x0ull, // TppMotherBaseManagement_RegCstDev
			0x0ull, // TppMotherBaseManagement_RegFlwDev
			0x0ull, // EquipIdTableImpl_GetSupportWeaponTypeId
            0x1480EE6F0ull, // DeclareAMs
            0x0ull,         // ReloadEquipMotionData (JP TBD; hook silently no-ops if unresolved)
			0x147A6BD40ull, // GetIconFtexPath
            0x0ull, // LoadingTipsEv_UpdateActPhase
            0x0ull, // AK_SoundEngine_SetRTPCValue
            0x0ull, // Fox_Sd_ConvertParameterID
            0x0ull, // EquipParameterTablesImpl_Instance
            0x0ull, // EquipIdTableImpl_AddToEquipIdTable
            0x0ull, // EquipIdTableImpl_s_internalInfoList


            0x14844DB10ull, // LoadPlayerPartsParts
            0x14844DE90ull, // LoadPlayerPartsFpk
            0x14844B070ull, // LoadPlayerCamoFpk
            0x14844CDE0ull, // LoadPlayerSnakeBlackDiamondFpk
            0x0ull, // LoadPlayerBionicArmFpk (JP TBD; hook silently no-ops if unresolved)
            0x0ull, // LoadPlayerSnakeFaceFpk (JP TBD; hook silently no-ops if unresolved)
            0x0ull, // Player2BlockController_LoadPartsNew
            0x0ull, // UpdatePartsStatus
            0x0ull, // EquipController_SetHandSlotEnabled (JP TBD; hook silently no-ops if unresolved)
            0x0ull, // Sys_IsArtificialHandEnabled (JP TBD; hook silently no-ops if unresolved)
            0x0ull, // Sys_IsArtificialHandEnabledForCurrentPlayerType (JP TBD; hook silently no-ops if unresolved)
            0x0ull, // Player2GameObjectImpl_ProcessSignal (JP TBD; hook silently no-ops if unresolved)
            0x0ull, // Player2Impl_SetUpParts (JP TBD; hook silently no-ops if unresolved)
            0x0ull, // ResolveSuitToPartsType
            0x0ull, // MissionPrep_RequestToChangePlayerPartsInMissionPreparationMode
            0x0ull, // Player2UtilityImpl_CommitWrapper
            0x0ull, // ItemSelectorCallbackImpl_DecideActMissionPreparationSetEquipMode
            0x0ull, // ItemSelectorCallbackImpl_DecideActMotherBaseDeviceSupportDropMode
            0x0ull, // SupplyDropSuitSetup
            0x0ull, // SupplyCboxGameObjectImpl_RestoreRequestFromSVars
            0x0ull, // Player2UtilityImpl_SetSuitAndHandConditionWithLoadoutInfo
            0x0ull, // Player2UtilityImpl_LoadoutApplyAfterSetSuit
            0x0ull, // Player2UtilityImpl_SetInitialConditionWithLoadoutInfo
            0x14163E730ull, // CharacterSelectorCallbackImpl_ChangeDetailsWindowBuddySelect (JP retail; verified by matching 0x9C70/0xA0D0/0x9FE0 offsets + 0x8FDA3DFC95ED/0x30A0D543E155 property hashes)
            0x14163EE20ull, // CharacterSelectorCallbackImpl_OpenBuddySelect (JP retail; same constants verified, plus the slot computation `(this+0xA0 + this+0x9C) % this+0x94`)
            0x0ull, // SuitList_GetDevelopedCount
            0x0ull, // SuitList_FillDevelopedFlowIxs
            0x0ull, // SuitList_GetSuitInfoTable
            0x0ull, // SupplyCboxSystemImpl_Reset
            0x0ull, // SupplyCboxActionPluginImpl_StateHandler1
            0x0ull, // SupplyCboxSystemImpl_RequestToDropImpl (JP unknown; hook silently no-ops if unresolved)
            0x0ull, // SupplyCboxSystemImpl_OnDropTimerTick (JP unknown; hook silently no-ops if unresolved)
            0x0ull, // SupplyCboxSystemImpl_SettledHandler (JP unknown; hook silently no-ops if unresolved)
            0x0ull, // CharacterSelectorCallbackImpl_StoreCurrentCharacterSuitAndHeadPartsInfo
            0x0ull, // ResourceTable_DoesNeedFaceFova
            0x0ull, // ResourceTable_DoesNeedFaceFovaForAvatar
            0x0ull, // CamoSystemObject
            0x0ull, // GetSuitVariation
            0x0ull, // HeadOptionTableLookup
            0x0ull, // HasHeadOptions
            0x0ull, // HeadOptionIndexGetter
            0x0ull, // SuitCatalog_FindHeadOptionRow
            0x0ull, // FetchCurrentHeadOptionKey
            0x0ull, // MissionPrep_GetSelectionNum
            0x0ull, // MissionPrep_IsEnableCurrentHeadOption
            0x0ull, // MissionPrep_UpdateLoadMark
            0x0ull, // MissionPrepSystem_IsEnableHeadOptionSuit (JP TBD)
            0x0ull, // Player_ConverFaceIdWithFaceEquipId (JP TBD)
            0x0ull, // CamouflageController_ExecSuitCorrect (JP TBD)
            0x0ull, // CamoufParamInfo_GetCamoufValue (JP TBD)
            0x0ull, // SetupCharacterSlotSelectPrefabListElement
            0x0ull, // AddListSuit
            0x0ull, // IsEnableCurrentSuit
            0x0ull, // SetupEquipPanelParam


            0x0ull, // ItemSelectorCallbackImpl_SetInDecideOpen
            0x0ull, // ItemSelectorCallbackImpl_SetupPrefabListElement
            0x0ull, // ItemSelector_AddListBandana (JP TBD)
            0x0ull, // ItemSelectorRecordCallFunc_UpdateRecords (JP unknown; hook silently no-ops if unresolved)
            0x0ull, // LoadPlayerCamoFv2
            0x0ull, // LoadPlayerSnakeBlackDiamondFv2
            0x0ull, // LoadPlayerBionicArmFv2 (JP TBD; hook silently no-ops if unresolved)
            0x0ull, // LoadPlayerSnakeFaceFv2 (JP TBD; hook silently no-ops if unresolved)
            0x0ull, // GetCurrentSuitFlowIndex
            0x0ull, // GetEquipIdFromLoadoutInfo
            0x0ull, // IsEquipDeveloped
            0x0ull, // SetItemDetail
            0x0ull, // SendTrigger
            0x0ull, // SuitInfoBitFlag_AccessorPlus15Bit4 (JP not yet identified; EN-only fix)
            0x0ull, // IsEquipSuit
            0x0ull, // EquipDevelopCallbackImpl_SetSupplyCBoxInfo
            0x14A6C34B4ull, // TornadoDualPatch (JP 1.0.15.3 — same `74 10` JZ instruction; user-verified in Ghidra at .reloc:14a6c34b4 inside the small function block 14a6c34a5..14a6c34d5, which appears to be the JP-side equivalent of EN's PreUpdate bit-0x12 branch refactored into its own routine. Initial pattern search missed this because the JP build extracted the branch into a separate function instead of inlining it like EN.)


            0x0ull, // RealizedSahelan2Impl_Realize (JP TBD; hook silently no-ops if unresolved)
            0x0ull, // RealizedSahelan2Impl_SetFovaImpl (JP TBD; hook silently no-ops if unresolved)
            0x0ull, // FormVariationFile2_ApplyOnlyMeshAndTextureVariation (JP TBD; hook silently no-ops if unresolved)


            0x0ull, // RealizedSecurityCamera2Impl_SetFova (JP TBD; hook silently no-ops if unresolved)
        };

        return value;
    }

    inline std::wstring GetModuleDirectory(HMODULE hModule)
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

    inline std::string ReadWholeFileUtf8OrAnsi(const std::wstring& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
            return {};

        return std::string(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());
    }

    inline std::string ToLowerAscii(std::string text)
    {
        std::transform(
            text.begin(),
            text.end(),
            text.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        return text;
    }

    inline GameBuild DetectGameBuildFromVersionInfo(HMODULE hGame)
    {
        const std::wstring dir = GetModuleDirectory(hGame ? hGame : GetModuleHandleW(nullptr));
        if (dir.empty())
            return GameBuild::Unknown;

        const std::wstring versionInfoPath = dir + L"\\version_info.txt";
        std::string text = ReadWholeFileUtf8OrAnsi(versionInfoPath);
        if (text.empty())
        {
            Log("[AddressSet] Failed to read version_info.txt, defaulting to English.\n");
            return GameBuild::English;
        }

        text = ToLowerAscii(text);
        Log("[AddressSet] version_info.txt = %s\n", text.c_str());

        if (text.find("mst_en") != std::string::npos)
            return GameBuild::English;

        if (text.find("mst_jp") != std::string::npos)
            return GameBuild::Japanese;

        return GameBuild::English;
    }

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

    inline bool ResolveAddressSet(HMODULE hGame)
    {
        if (!hGame)
            return false;

        GetGameBuild() = DetectGameBuildFromVersionInfo(hGame);

        switch (GetGameBuild())
        {
        case GameBuild::English:
            GetAddressSet() = GetEnglishAddressSet();
            Log("[AddressSet] Selected English address set.\n");
            return true;

        case GameBuild::Japanese:
            GetAddressSet() = GetJapaneseAddressSet();
            Log("[AddressSet] Selected Japanese address set.\n");
            return true;

        default:
            GetAddressSet() = GetEnglishAddressSet();
            Log("[AddressSet] Unknown build, defaulting to English address set.\n");
            return true;
        }
    }
}

#define gGameBuild (::AddressSetRuntime::GetGameBuild())
#define gAddr (::AddressSetRuntime::GetAddressSet())
#define ResolveAddressSet (::AddressSetRuntime::ResolveAddressSet)
#define GetGameBuildName (::AddressSetRuntime::GetGameBuildName)
