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

        uintptr_t GetIconFtexPath = 0;
        uintptr_t LoadingTipsEv_UpdateActPhase = 0;

        uintptr_t AK_SoundEngine_SetRTPCValue = 0;
        uintptr_t Fox_Sd_ConvertParameterID = 0;

        uintptr_t Fox_Sd_Ad_AudioSoundEngine_RegisterGameObject = 0;
        uintptr_t Fox_Sd_Object_Activate = 0;
        uintptr_t Fox_Sd_Daemon_GetObject = 0;
        uintptr_t Fox_Sd_Daemon_Singleton = 0;
        // tpp::gm::impl::SoundControllerImpl::CallInternal
        // Signature: void(this, voiceRequest*, uint soundSlot).
        // soundSlot is the controller's per-controller slot index — not a goId.
        uintptr_t SoundControllerImpl_CallInternal = 0;


        uintptr_t TornadoDualPatch                  = 0;


        uintptr_t RealizedSahelan2Impl_Realize      = 0;
        uintptr_t RealizedSahelan2Impl_SetFovaImpl  = 0;
        uintptr_t FormVariationFile2_ApplyOnlyMeshAndTextureVariation = 0;

        // tpp::gm::sahelan::impl::ActionCoreImpl::SetEyeLampColor
        // Signature: void(this, ulonglong idx, float color[4]).
        // idx is normalized via (idx - *(this+0x20)) into a per-lamp slot
        // at *(this+0x10) + slot*0x60 + 0x20.
        uintptr_t Sahelan_ActionCoreImpl_SetEyeLampColor = 0;

        // tpp::gm::sahelan::impl::ActionCoreImpl::UpdateEyeLampColor
        // Signature: void(this, int slot).
        // Per-frame updater — reads the lamp color from a *separate* storage
        // at *(this+0x48) + (slot - *(this+0x58)) * 0x60 + 0x20 and pushes
        // it to the renderer via vt+0xb0 on *(this+0xa0). Hooking this and
        // overwriting the storage before orig is the rail that actually
        // paints the visible lamp.
        uintptr_t Sahelan_ActionCoreImpl_UpdateEyeLampColor = 0;

        // tpp::gm::sahelan::impl::ActionCoreImpl::UpdateHeartLight
        // Signature: void(this, uint slot).
        // Per-frame updater for the chest "heart" light. Computes the color
        // from a per-actor HP ratio (R=1.0, G=0.09 + ratio*0.91, B=ratio,
        // A=1.0), writes it to *(this+0x48) + slot*0x60 + 0x10, then pushes
        // via vt+0xb0 on *(*(this+0x98)+0x18) with slot index 0x14 and the
        // standard EffectBuilder color hash 0xcff50b635575.
        // Note: storage offset is +0x10 (NOT +0x20 like the eye lamp).
        uintptr_t Sahelan_ActionCoreImpl_UpdateHeartLight = 0;

        // tpp::gm::sahelan::impl::PhaseSneakAiImpl color-preset selector
        // (the function that calls vtable+0x38 on the eye-effect material
        // plugin with the chosen RGBA from the table). Signature:
        // void(longlong this, uint slot, int mode).
        uintptr_t Sahelan_PhaseSneakAi_PushEyeColor = 0;

        // Address of 2 consecutive 64-bit FNV hashes naming the eye-lamp
        // mesh materials. FUN_14b84dec0 calls
        // QuarkSystemTable->plVar2->vt[+0x30](buf, hash, flag) twice with
        // these — once per eye. Filtering by these in our vt[+0x30] hook
        // lets us substitute the RGBA without affecting any other mesh.
        //   [0] = DAT_142b40000 (e.g. MESH_EYES_IV)
        //   [1] = DAT_142b40008 (e.g. MESH_REYES_IV)
        uintptr_t Sahelan_EyeMeshHashTable = 0;

        // Base of 5 consecutive RGBA float[4] presets read by
        // PhaseSneakAiImpl::SetEyeFlare and friends to drive the visible
        // eye-lamp emissive color through the renderer-side plugin
        // (vtable+0x38 on the eye-effect material plugin).
        //   [0]: A50 — primary color (sent with hash 0xCFF50B635575)
        //   [1]: A60 — flare default preset (mode != 2 && mode != 3 fallback)
        //   [2]: A70 — secondary color (sent with hash 0xAB6E796FF844)
        //   [3]: A80 — flare "else" preset
        //   [4]: A90 — flare mode=2/3 preset
        // Overwriting all 5 with a single RGBA forces that color on every
        // state transition.
        uintptr_t Sahelan_PhaseSneakAi_ColorTableBase = 0;


        uintptr_t RealizedSecurityCamera2Impl_SetFova = 0;


        // UpdateAnnounceNormal — slot 0/1 popups.
        uintptr_t MbDvcAnnouncePopupCallbackImpl_UpdateAnnounceNormal = 0;
        // UpdateAnnounceServer — slot 2/3/4/7/8 popups.
        uintptr_t MbDvcAnnouncePopupCallbackImpl_UpdateAnnounceServer = 0;

        // tpp::ui::hud::CommonDataManager — HUD popup (ShowPopup / ShowErrorPopup style).
        uintptr_t HudCommonDataManager_GetInstance       = 0;
        uintptr_t HudCommonDataManager_SetPopupType      = 0;
        uintptr_t HudCommonDataManager_SetPopupText      = 0;
        uintptr_t HudCommonDataManager_SetPopupErrorType = 0;
        uintptr_t HudCommonDataManager_StartPopup        = 0;

        // Soldier2SoundControllerImpl::GetVoiceTypeFromSoldierTypeImpl
        // (this_adjusted, slot, _, currentVoiceType[, isFlag]) — returns the
        // faction-aware ene_a/b/c/d FNV-1 (0x570C8E21..24) variant.
        uintptr_t Soldier2SoundController_GetVoiceTypeFromSoldierTypeImpl = 0;

        // Soldier2SoundControllerImpl::Activate(this, uint slot, int soundIndex).
        // Called per-soldier when their audio component spawns (mission load).
        // Provides a (slot, controller) pair without needing the soldier to
        // make audible noise.
        uintptr_t Soldier2SoundController_Activate = 0;

        // CAkResampler::SetPitch (this, float cents) — Wwise's runtime
        // pitch/SR setter. Clamped to ±2400 cents internally. Called per
        // active voice-pipeline node every audio buffer.
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
            0x14614A4E0ull, // GetPlayingTime
            0x14614AA30ull, // GetPlayingTrackId
            0x140BFF3F0ull, // GetQuarkSystemTable
            0x14614C0C0ull, // GetTrackInfoByName
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

            0x145E62540ull, // GetIconFtexPath
            0x145CCFCC0ull, // LoadingTipsEv_UpdateActPhase (overrides 0x9d8/0x9e0 w/ DD logo)
            0x14033d520ull, // AK_SoundEngine_SetRTPCValue (thunk → AK::SoundEngine::SetRTPCValue)
            0x14032ADF0ull, // Fox_Sd_ConvertParameterID (thunk → fox::sd::ConvertParameterID; RTPC/Switch/State name hash)
            0x143F42540ull, // Fox_Sd_Ad_AudioSoundEngine_RegisterGameObject
            0x14032B040ull,         // Fox_Sd_Object_Activate 
            0x140329C80ull,         // Fox_Sd_Daemon_GetObject 
            0x142B9E8B0ull,         // Fox_Sd_Daemon_Singleton
            0x1468EDD50ull,         // SoundControllerImpl_CallInternal 


            0x149CFBA54ull, // TornadoDualPatch (2-byte JZ inside UnrealUpdaterImpl::PreUpdate; NOP'd to enable tornado dual)


            0x146ACC210ull, // RealizedSahelan2Impl_Realize (mission-gated FOVA dispatch; gate is 0x2b8f at +0x8d into the function)
            0x146ACC650ull, // RealizedSahelan2Impl_SetFovaImpl (loads vanilla FOVA via hardcoded hash 0x60887fe72aa5c04b at +0x16)
            0x144A3CBD0ull, // FormVariationFile2_ApplyOnlyMeshAndTextureVariation (7-arg FV2 apply used by SetFovaImpl)
            0x14BBC1B10ull,         // Sahelan_ActionCoreImpl_SetEyeLampColor (EN TODO)
            0x14BBC3630ull,         // Sahelan_ActionCoreImpl_UpdateEyeLampColor (EN TODO)
            0x14BBC37A0ull,         // Sahelan_ActionCoreImpl_UpdateHeartLight (EN TODO)
            0x14BCA4b70ull,         // Sahelan_PhaseSneakAi_PushEyeColor (EN TODO)
            0x142B40000ull,         // Sahelan_EyeMeshHashTable (EN TODO)
            0x142C69A50ull,         // Sahelan_PhaseSneakAi_ColorTableBase (EN TODO)


            0x146AB80F0ull, // RealizedSecurityCamera2Impl_SetFova (3-arg variant switcher; reads FV2 ptr from this[0x98 + variant*8], no hardcoded hash, no mission gate. FovaType: 0=normal camera, 1=gun camera)


            0x140EF2EE0ull, // MbDvcAnnouncePopupCallbackImpl_UpdateAnnounceNormal (state machine for iDroid slot-0/slot-1 popups; hooked to override default lang-text with V_FrameWork custom title/body when ReserveParam.commonValue1 == 0x56465043 magic)
            0x140EF32A0ull,         // MbDvcAnnouncePopupCallbackImpl_UpdateAnnounceServer

            0x145C0A890ull,         // HudCommonDataManager_GetInstance
            0x1408679B0ull,         // HudCommonDataManager_SetPopupType
            0x1408678D0ull,         // HudCommonDataManager_SetPopupText
            0x140867570ull,         // HudCommonDataManager_SetPopupErrorType
            0x147732010ull,         // HudCommonDataManager_StartPopup

            0x14158C290ull,                 // Soldier2SoundController_GetVoiceTypeFromSoldierTypeImpl
            0x14158B4f0ull,                         // Soldier2SoundController_Activate
            0x1441DBB80ull,                         // CAkResampler_SetPitch


            0x143F33A20ull, // FNVHash32 (FNV-1 32-bit hash function used for sound event name hashes)
            0x145CB70BDull, // Play_bgm_gameover (embedded FNV32 of "PlayBgm_bgm_gameover")
            0x145CB70C4ull, // Play_bgm_gameover_paradox
            0x145CB70B6ull, // Play_bgm_gameover_perfectstealth
            0x145CB70CBull, // Play_bgm_s10010_gameover (Cyprus prologue)
            0x145CB8EF5ull, // Stop_bgm_gameover
            0x145CB8F06ull, // Stop_bgm_gameover_paradox
            0x145CB8EFAull, // Stop_bgm_gameover_perfectstealth
            0x145CB8F0dull, // Stop_bgm_s10010_gameover
            0x14226bFC8ull, // Play_bgm_gameover_paradox_soundId (secondary patch site)
            0x14226bFCCull, // Stop_bgm_gameover_paradox_soundId


            0x140e2470full, // DD_vox_SH_voice (heli pilot voice event hash)
            0x140e24682ull, // DD_vox_SH_radio (heli radio event hash, primary)
            0x140e246ffull, // DD_vox_SH_radio2 (heli radio event hash, secondary)
            0x140e24707ull, // DD_vox_SH_radio3 (heli radio event hash, tertiary)
        };

        return value;
    }

    inline const AddressSet& GetJapaneseAddressSet()
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
            0x147DE8FA0ull, // GetPlayingTime
            0x147DE93E0ull, // GetPlayingTrackId
            0x140BFEF80ull, // GetQuarkSystemTable
            0x147DEA880ull, // GetTrackInfoByName
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


            0x147A6BD40ull, // GetIconFtexPath
            0x1477EC6F0ull, // LoadingTipsEv_UpdateActPhase
            0x14033CFC0ull, // AK_SoundEngine_SetRTPCValue
            0x14032A870ull, // Fox_Sd_ConvertParameterID
            0x143F7BCA0ull, // Fox_Sd_Ad_AudioSoundEngine_RegisterGameObject (JP)
            0x14032AAC0ull, // Fox_Sd_Object_Activate (JP)
            0x140329710ull, // Fox_Sd_Daemon_GetObject (JP)
            0x142B9E8B0ull, // Fox_Sd_Daemon_Singleton (JP)
            0x1484D84E0ull, // SoundControllerImpl_CallInternal (JP)


            0x14A6C34B4ull, // TornadoDualPatch


            0x148655E70ull, // RealizedSahelan2Impl_Realize 
            0x148656360ull, // RealizedSahelan2Impl_SetFovaImpl 
            0x1448A0190ull, // FormVariationFile2_ApplyOnlyMeshAndTextureVariation
            0x14B77E550ull, // Sahelan_ActionCoreImpl_SetEyeLampColor (JP)
            0x14B7801D0ull, // Sahelan_ActionCoreImpl_UpdateEyeLampColor (JP: per-frame updater — reads storage at *(this+0x48)+slot*0x60+0x20)
            0x14B7807B0ull, // Sahelan_ActionCoreImpl_UpdateHeartLight (JP: per-frame HP-driven heart-light updater)
            0x14B84DEC0ull, // Sahelan_PhaseSneakAi_PushEyeColor (JP: FUN_14b84dec0 — picks color preset, calls vtable+0x38 on eye-effect material plugin)
            0x142B40000ull, // Sahelan_EyeMeshHashTable (JP: 2 × uint64 FNV hashes naming the eye-lamp meshes)
            0x142C69A50ull, // Sahelan_PhaseSneakAi_ColorTableBase (JP: 5 consecutive RGBA float[4] presets — visible-emissive rail)


            0x148644FE0ull, // RealizedSecurityCamera2Impl_SetFova


            0x140EF3050ull, // MbDvcAnnouncePopupCallbackImpl_UpdateAnnounceNormal (verified against JP build mgsvtpp_1_0_15_3_jp; case-8 of Update dispatches FUN_140ef3050 which contains the slot-0/slot-1 lang-text hashes 0xdf1b1fd6f40a / 0x2e2fb8df282b)
            0x140EF3410ull, // MbDvcAnnouncePopupCallbackImpl_UpdateAnnounceServer (verified JP; case-7 of Update dispatches FUN_140ef3410, handles slot 2/3/4/7/8)

            0x147719930ull, // HudCommonDataManager_GetInstance
            0x140867630ull, // HudCommonDataManager_SetPopupType
            0x140867550ull, // HudCommonDataManager_SetPopupText
            0x1408671F0ull, // HudCommonDataManager_SetPopupErrorType
            0x147732010ull, // HudCommonDataManager_StartPopup

            0x14158C270ull, // Soldier2SoundController_GetVoiceTypeFromSoldierTypeImpl (FUN_14158c270; case 0x570c8e21..24 dispatch verified at JP line 2393122)
            0x14158B4F0ull, // Soldier2SoundController_Activate (FUN_14158b4f0; per-soldier audio component activation, fires at mission load)
            0x14418D980ull, // CAkResampler_SetPitch (Wwise runtime pitch/SR setter; clamps ±2400 cents)


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
