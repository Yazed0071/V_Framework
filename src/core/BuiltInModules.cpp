#include <pch.h>

#include <Windows.h>
#include <TlHelp32.h>
#include <cstdint>
#include <cstring>
#include <mutex>

#include "AddressSet.h"
#include "BuiltInModules.h"
#include "../hooks/equip/EquipPartParams.h"
#include "../hooks/player/LifeControllerImpl_UpdateHost.h"
#include "../hooks/player/Player2Impl_CallVoice.h"
#include "../hooks/player/CqcActionPluginImpl_CompleteContinueCqcTarget.h"
#include "../hooks/player/IdroidVoiceLines.h"
#include "../hooks/ui/MbDvcSoundControllerImpl_RequestVoice.h"
#include "../hooks/ui/MbDvcSoundControllerImpl_Update.h"
#include "../hooks/ui/BaseControllerImpl_CompletePlatformDeck.h"
#include "../hooks/ui/MbDvcMapCallbackControlImpl_RendezvousOperation.h"
#include "../hooks/fox/gm/SimpleMissionBlockController_ReceiveMessage.h"
#include "../hooks/fox/gs/RouteGroupImpl_Destructor.h"
#include "../hooks/ui/FobMission2CallbackImpl_Update.h"
#include "../hooks/ui/MotherBaseMapCommonData_IsDangerLandingZone.h"
#include "../hooks/fox/path/PathHashCode.h"
#include "LoadWatch.h"
#include "../hooks/player/EquipControllerImpl_OnShieldHit.h"
#include "../hooks/buddy/HorseDamageActionImpl_State_Whiii.h"
#include "../hooks/buddy/BuddyQuiet2SoundControllerImpl_CallVoice.h"
#include "../hooks/buddy/BuddyQuietDamageHandlerImpl_ProcessDamages.h"
#include "../hooks/player/RideHorseActionPluginImpl_State_Idle.h"
#include "../hooks/core/GetFobStatusCallback_AnnounceDefense.h"
#include "../hooks/soldier/Soldier2SoundControllerImpl_CallVoiceImpl.h"
#include "../hooks/soldier/MemberRoleControllerImpl_AssignSelectAlertLost.h"
#include "../hooks/soldier/HostageRideVehicleActionImpl_State_GetInAction.h"
#include "../hooks/soldier/DownActionImpl_State_ToDyingSufferIdle.h"
#include "../hooks/soldier/SoldierDamageActionImpl_State_DamageReaction.h"
#include "../hooks/sound/AkSoundEngine_PostEvent.h"
#include "../hooks/player/SightManagerImpl_UpdateEspionageRadio.h"
#include "../hooks/soldier/HostageVoiceLines.h"
#include "../hooks/equip/MotionLoaderImpl_GetMtarIds.h"
#include "../hooks/equip/GetGunMotionIdFromTable.h"
#include "../hooks/core/ServerManager_BufferRelease.h"
#include "../hooks/core/VertexStreamPool.h"
#include "../hooks/core/RendererPools.h"
#include "../hooks/core/TextureStoragePool.h"
#include "../hooks/core/TexturePools.h"
#include "../hooks/core/TexturePackAbort.h"
#include "../hooks/core/TextureProxyFallback.h"
#include "../hooks/core/NoticeSensorImpl_Init.h"
#include "../hooks/core/NoticeSensorImpl_CreateController.h"
#include "../hooks/soldier/CommandPost2Impl_Initialize.h"
#include "../hooks/soldier/CommandPost2Impl_GetFreeMemberSlots.h"
#include "../hooks/soldier/CommandPost2Impl_CreateController.h"
#include "../hooks/equip/PartIdWiden.h"
#include "../hooks/collection/TppCollectionRuntime.h"
#include "../hooks/deploy/DeployMissionHooks.h"
#include "../hooks/buddy/Buddy2BlockControllerImpl_GetPathAtBuddyType.h"
#include "../hooks/buddy/BattleGearBuddy.h"
#include "../hooks/buddy/Buddy2BlockControllerImpl_GetBuddyQuietWeaponFileFromBuddyBlock.h"
#include "../hooks/buddy/Buddy2BlockControllerImpl_GetBuddyQuietFovaFile.h"
#include "../hooks/buddy/EquipDevelopControllerImpl_GetBuddyPartsDevelopInfoIndex.h"
#include "../hooks/buddy/BuddyServiceImpl_ChangeQuietCostume.h"
#include "../hooks/buddy/BuddyServiceImpl_ChangeQuietWeaponType.h"
#include "../hooks/buddy/BuddyServiceImpl_ChangeHorseSettings.h"
#include "../hooks/buddy/BuddyServiceImpl_ChangeDogSettings.h"
#include "../hooks/buddy/BuddyServiceImpl_GetQuietEquipIdByWeaponType.h"
#include "../hooks/buddy/BuddyQuiet2Impl_SetBruise.h"
#include "../hooks/buddy/DamageAiImpl_StepBitDamage.h"
#include "../hooks/buddy/ItemSelectorCallbackImpl_SetupPrefabListElement.h"
#include "../hooks/buddy/CombatAiImpl_SendFireSignal.h"
#include "../hooks/buddy/EquipControllerImpl_InitCurrentSlot.h"
#include "../hooks/buddy/ActionCoreImpl_ExecDefenseCallback.h"
#include "../hooks/buddy/BasicAction2Impl_Execute.h"
#include "../hooks/buddy/BuddyDogDamageHandlerImpl_ProcessDamages.h"
#include "../hooks/buddy/BuddyServiceImpl_InitBuddyCommandForStatus.h"
#include "../hooks/buddy/BuddyDog2Impl_Initialize.h"
#include "../hooks/buddy/EquipDevelopControllerImpl_GetDWalkerParts.h"
#include "../hooks/buddy/WalkerGear_SubWeaponEquipId.h"
#include "../hooks/buddy/BuddyServiceImpl_ChangeGearSubWeaponSettings.h"
#include "../hooks/buddy/EquipBlockController2_StartLoadingToEquipMissionBlock.h"
#include "../hooks/buddy/EquipDataSystemAccessorImpl_GetBlock.h"
#include "../hooks/buddy/RideWalkerGearActionImpl_GetVehicleEquipId.h"
#include "../hooks/buddy/UiUtility_GetVehicleEquipNameText.h"
#include "../hooks/heli/ActionCoreImpl_Execute.h"
#include "../hooks/heli/CpControllerImpl_GetTargetPosition.h"
#include "../hooks/heli/DamageHandlerImpl_ProcessQueuedDamages.h"
#include "../hooks/heli/EnemyCombatAiImpl_Execute.h"
#include "../hooks/heli/GunActionImpl_Execute.h"
#include "../hooks/heli/HeliInterest_SetLookAt.h"
#include "../hooks/heli/LightActionImpl_Execute.h"
#include "../hooks/heli/EquipDevelopControllerImpl_GetHeliPartsId.h"
#include "../hooks/heli/CustomizeSelectorCallbackImpl_DecideActWindowPartHelicopter.h"
#include "../hooks/heli/CustomizeSelectorCallbackImpl_HoverHeliPart.h"
#include "../hooks/heli/EquipDetailsCallbackImpl_SetupHeliCustomizeDetails.h"
#include "../hooks/heli/SupportHeliCustomizeCodec.h"
#include "../hooks/heli/CustomizeControllerImpl_SetSubWeaponAmmo.h"
#include "../hooks/heli/LauncherActionImpl_Execute.h"
#include "../hooks/heli/SupportCombatAiImpl_StepAttack.h"
#include "../hooks/heli/ShellSystemImpl_Fire.h"
#include "../hooks/heli/BlastSystemImpl_Blast.h"
#include "../hooks/heli/ShellImpl_UpdateAction.h"
#include "../hooks/heli/ShellImpl_RequestSmokeCloud.h"
#include "../hooks/heli/SupportHeliCustomizeMenu.h"
#include "../hooks/heli/SupportHeliOutfit.h"
#include "../hooks/heli/SupportHeliDevelopTab.h"
#include "../hooks/heli/GunActionImpl_FireBarrel.h"
#include "../hooks/heli/TppHeli2_Allocate.h"
#include "../hooks/heli/TppHeli2_LuaCommands.h"
#include "../hooks/player/RideHeliActionPluginImpl_ExecActionButtonFunction.h"
#include "../hooks/soldier/AiControllerImpl_WakeNode.h"
#include "../hooks/soldier/NoticeTurretAiImpl_ShouldSleep.h"
#include "../hooks/heli/MechaActionImpl_StateOnPassenger.h"
#include "../hooks/vehicle/RideVehicleActionImpl_CheckVehicleIconArea.h"
#include "../hooks/soldier/RideHeliActionImpl_State_GetInAction.h"
#include "../hooks/soldier/RideHeliActionImpl_State_IdleAction.h"
#include "../hooks/soldier/CcRootAdjustAction_Exec.h"
#include "../hooks/soldier/UnrealRideHeliActionImpl_State_IdleAction.h"
#include "../hooks/soldier/RideHeliActionImpl_State_GetOutAction.h"
#include "../hooks/soldier/SoldierAttackActionImpl_Execute.h"
#include "../hooks/soldier/SoldierAttackActionImpl_RequestFire.h"
#include "../hooks/soldier/SoldierDamageActionImpl_Execute.h"
#include "../hooks/equip/EquipIdTableImpl_GetSupportWeaponTypeId.h"
#include "../hooks/equip/EquipParameterTablesImpl_ReloadEquipParameterTablesImpl.h"
#include "../hooks/equip/SimplePartsControllerImpl_Create.h"
#include "../hooks/equip/ThrowingImpl_GetBlastParamByEquipId.h"
#include "../hooks/equip/ThrowingImpl_UpdateAction.h"
#include "../hooks/equip/ThrowingImpl_UpdateStatus.h"
#include "../hooks/equip/ThrowingImpl_AddReboundNoise.h"
#include "../hooks/equip/ThrowingImpl_ActivateThrowingAtEmptyWork.h"
#include "../hooks/equip/ThrowingImpl_UpdateDeactivation.h"
#include "../hooks/equip/ThrowingSystemImpl_ExecuteSerially.h"
#include "../hooks/equip/ThrowingImpl_OnOffenseHit.h"
#include "../hooks/equip/AttackActionImpl_CalcThrowingVelocity.h"
#include "../hooks/equip/ThrowingImpl_UpdateNextPosition.h"
#include "../hooks/equip/ThrowingImpl_DrawLocusLineImpl.h"
#include "../hooks/equip/ItemSelectorCallbackImpl_IsParentEquip.h"
#include "../hooks/equip/DecoyResourceManagerImpl_InitializePlayerDecoyModel.h"
#include "../hooks/equip/DecoySystemImpl_Realize.h"
#include "../hooks/core/AnimationControllerImpl_SetGameObjectIdToCharaControl.h"
#include "../hooks/player/Fova2ControllerImpl_ApplyPlayerFova.h"
#include "../hooks/soldier/Soldier2Impl_RealizeRealizedSoldier.h"
#include "../hooks/soldier/RealizedSoldier2Impl_ExecuteSerially.h"
#include "../hooks/equip/EquipDevelopControllerImpl_GetDamageType.h"
#include "../hooks/equip/EquipControllerImpl_InitializePlayerAtIndex.h"
#include "../hooks/equip/LoadoutManagerImpl_RestoreLoadoutInfosFromVars.h"
#include "../hooks/equip/PlacedSystemImpl_CopyToScriptVarsPlayerPlacedWeapon.h"
#include "../hooks/equip/PlacedSystemImpl_CreateByLocators.h"
#include "../hooks/equip/PlacedImpl_ExecuteAfter.h"
#include "../hooks/equip/PlacedSystemImpl_CreateCorrelation.h"
#include "../hooks/equip/EquipIdTableImpl_GetEquipIdFromStrCode32Impl.h"
#include "../hooks/soldier/Soldier2CombatUtil_GrenadeUpdate.h"
#include "../hooks/gimmick/PermanentGimmickDefenseCallback_Exec.h"
#include "../hooks/equip/PlacedSystemImpl_RestorationPlace.h"
#include "../hooks/equip/SupplyCboxGameObjectImpl_RestoreRequestFromSVars.h"
#include "../hooks/equip/Player2UtilityImpl_GetPickableInfosOfAmmoBox.h"
#include "../hooks/equip/MissionPreparationSystemImpl_CalcEquipWeaponItemCostFromLoadout.h"
#include "../hooks/equip/EquipSystemImpl_SetAmmoCount.h"
#include "../hooks/ui/ItemSelectorCallbackImpl_IsGetLatestItemTool.h"
#include "../hooks/ui/EquipDevelopCallbackImpl_IsLatestItemTool.h"
#include "../hooks/equip/EquipControllerImpl_UpdateBattery.h"
#include "../hooks/ui/EquipCrossEvCall_SpecialItemAct.h"
#include "../hooks/pickable/PickUpActionPluginImpl_PickUpAmmoBox.h"
#include "../hooks/player/ItemParameter.h"
#include "../hooks/equip/ShieldParams.h"
#include "../hooks/ui/TppUIWeaponNumImpl_ShowItemPanel.h"
#include "../hooks/ui/UiUtilityImpl_GetBatteryLevel.h"
#include "../hooks/player/ActionCoreImpl_ExecPostPutMotion.h"
#include "../hooks/player/RespawnActionPluginImpl_PollingImpl.h"
#include "../hooks/core/FobTarget_RequestSneakCommon.h"
#include "../hooks/soldier/EquipPluginImpl_Execute.h"
#include "../hooks/soldier/EquipControllerImpl_DropEquip.h"
#include "../hooks/equip/EquipParameterTablesImpl_GetSupportWeaponParameterBlock.h"
#include "../hooks/equip/EquipDevelopControllerImpl_GetDamageAmountRatio.h"
#include "../hooks/equip/SupportStockLease.h"
#include "FeatureModule.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using EntityInfoMapTeardown_t = void(__fastcall*)(void*);

    static EntityInfoMapTeardown_t g_OrigEntityInfoMapTeardown = nullptr;
    static void*                   g_EntityInfoMapTeardownAddr = nullptr;
    static std::uintptr_t          g_ExeImageBegin             = 0;
    static std::uintptr_t          g_ExeImageEnd               = 0;



    static bool TeardownEntryLooksSane(void* obj)
    {
        __try
        {
            const std::uintptr_t vt = *reinterpret_cast<std::uintptr_t*>(obj);
            if (vt < g_ExeImageBegin || vt >= g_ExeImageEnd)
                return false;
            const std::uintptr_t f0 = *reinterpret_cast<std::uintptr_t*>(vt);
            if (f0 < g_ExeImageBegin || f0 >= g_ExeImageEnd)
                return false;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static void ScrubTeardownMap(std::uint8_t* base, std::size_t firstOff,
                                 std::size_t nodesOff, std::size_t valuesOff)
    {
        __try
        {
            std::int32_t  idx    = *reinterpret_cast<std::int32_t*>(base + firstOff);
            std::uint8_t* nodes  = *reinterpret_cast<std::uint8_t**>(base + nodesOff);
            void**        values = *reinterpret_cast<void***>(base + valuesOff);
            if (!nodes || !values)
                return;
            for (int guard = 0; idx >= 0 && idx < 0x100000 && guard < 0x10000;
                 ++guard)
            {
                void* obj = values[idx];
                if (obj && !TeardownEntryLooksSane(obj))
                {
                    LogWarn("ExitGuard: EntityInfo entry %d holds dead object %p, skipped at quit",
                        idx, obj);
                    values[idx] = nullptr;
                }
                idx = *reinterpret_cast<std::int32_t*>(
                    nodes + static_cast<std::size_t>(idx) * 0x18 + 0xC);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    static void __fastcall hkEntityInfoMapTeardown(void* self)
    {
        if (self && g_ExeImageEnd != 0)
        {
            auto* base = reinterpret_cast<std::uint8_t*>(self);
            ScrubTeardownMap(base, 0x08, 0x20, 0x28);
            ScrubTeardownMap(base, 0x68, 0x80, 0x88);
        }
        if (g_OrigEntityInfoMapTeardown)
            g_OrigEntityInfoMapTeardown(self);
    }

    static bool Install_ExitTeardownGuard()
    {
        void* target = ResolveGameAddress(gAddr.Fox_EntityInfoMapTeardown);
        if (!target)
        {
            LogError("ExitGuard: no EntityInfo teardown address on %s, exit-crash guard off",
                GetGameBuildName(gGameBuild));
            return true;
        }

        const std::uintptr_t exeBase = GetExeBase();
        __try
        {
            auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(exeBase);
            auto* nt  = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
                exeBase + dos->e_lfanew);
            g_ExeImageBegin = exeBase + 0x1000;
            g_ExeImageEnd   = exeBase + nt->OptionalHeader.SizeOfImage;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            g_ExeImageBegin = 0;
            g_ExeImageEnd   = 0;
        }

        const bool ok = CreateAndEnableHook(
            target, reinterpret_cast<void*>(&hkEntityInfoMapTeardown),
            reinterpret_cast<void**>(&g_OrigEntityInfoMapTeardown));
        g_EntityInfoMapTeardownAddr = ok ? target : nullptr;
        if (!ok)
            LogError("ExitGuard: EntityInfo teardown hook failed, exit-crash guard off");
        return ok;
    }

    static void Uninstall_ExitTeardownGuard()
    {
        if (g_EntityInfoMapTeardownAddr)
            DisableAndRemoveHook(g_EntityInfoMapTeardownAddr);
        g_EntityInfoMapTeardownAddr = nullptr;
        g_OrigEntityInfoMapTeardown = nullptr;
    }
}

bool Install_SetLuaFunctions_Hook();
bool Uninstall_SetLuaFunctions_Hook();

bool Install_EquipBgTexture_Hook();
bool Uninstall_EquipBgTexture_Hook();

bool Install_UixUtilityControl_Hook();
bool Uninstall_UixUtilityControl_Hook();
bool Install_FontData_SearchGlyphData_Hook();
void Uninstall_FontData_SearchGlyphData_Hook();
bool Install_TppUIBinoSubjectiveImpl_Act_Hook();
void Uninstall_TppUIBinoSubjectiveImpl_Act_Hook();
bool Install_MainPadSlot_Hook();
void Uninstall_MainPadSlot_Hook();
bool Install_ButtonIconSheet_Hook();
void Uninstall_ButtonIconSheet_Hook();
bool Install_ModelNodeMesh_Destruct_Hook();
void Uninstall_ModelNodeMesh_Destruct_Hook();
bool Install_PadAssign_Hooks();
void Uninstall_PadAssign_Hooks();
bool Install_ControllerAssignMenu_Hooks();
void Uninstall_ControllerAssignMenu_Hooks();
bool Install_ControlTypePageAssign_Hooks();
void Uninstall_ControlTypePageAssign_Hooks();
bool Install_SonyPadLayout_Hook();
void Uninstall_SonyPadLayout_Hook();
bool Install_UiDepend_ActHudChecking_Hook();
void Uninstall_UiDepend_ActHudChecking_Hook();
bool Install_ButtonMapper_SetDefaultButtonMap_Hook();
void Uninstall_ButtonMapper_SetDefaultButtonMap_Hook();
bool Install_GameObjectLife_Hooks();
bool Uninstall_GameObjectLife_Hooks();

bool Install_MissionTelopBgTexture_Hook();
bool Uninstall_MissionTelopBgTexture_Hook();

bool Install_LoadingSplash_Hook();
bool Uninstall_LoadingSplash_Hook();

bool Install_GameOverSplash_Hook();
bool Uninstall_GameOverSplash_Hook();
bool Install_TppBonusPopupImpl_SetBgTexture_Hook();
bool Uninstall_TppBonusPopupImpl_SetBgTexture_Hook();
bool Install_MissionPreparationCallbackImpl_Start_Hook();
bool Uninstall_MissionPreparationCallbackImpl_Start_Hook();

bool Install_RewardPopupBgTexture_Hook();
bool Uninstall_RewardPopupBgTexture_Hook();

bool Install_State_StandHoldupCancelLookToPlayer_Hook(HMODULE hGame);
bool Uninstall_State_StandHoldupCancelLookToPlayer_Hook();

bool Install_CautionStepNormalTimerHook();
bool Uninstall_CautionStepNormalTimerHook();

bool Install_CpAntiAir_Hook();
bool Uninstall_CpAntiAir_Hook();

bool Install_ChangeLocationMenu_Hook();
bool Uninstall_ChangeLocationMenu_Hook();

bool Install_MbDvcMissionListRecordCallFunc_Start_Hook();
bool Uninstall_MbDvcMissionListRecordCallFunc_Start_Hook();
bool Install_MbDvcMapCallbackBaseImpl_UpdateLocationInfoView_Hook();
bool Uninstall_MbDvcMapCallbackBaseImpl_UpdateLocationInfoView_Hook();
bool Install_MbDvcSideOpsRecordCallFunc_StartBody_Hook();
bool Uninstall_MbDvcSideOpsRecordCallFunc_StartBody_Hook();
bool Install_MbDvcMissionListCallbackImpl_ActivatePrefabTablistParameter_Hook();
bool Uninstall_MbDvcMissionListCallbackImpl_ActivatePrefabTablistParameter_Hook();

bool Install_PhotoAdditionalText_Hook();
bool Uninstall_PhotoAdditionalText_Hook();

bool Install_RealizedSahelanFova_Hook();
bool Uninstall_RealizedSahelanFova_Hook();

bool Install_SoldierHairFova_Hook();
bool Uninstall_SoldierHairFova_Hook();

bool Install_PlayerVoiceFpk_Hook();
bool Uninstall_PlayerVoiceFpk_Hook();

bool Install_PlayerVoiceType_Hook();
bool Uninstall_PlayerVoiceType_Hook();

bool Install_Fova2ControllerImpl_PutAndActMotion_Hook();
void Uninstall_Fova2ControllerImpl_PutAndActMotion_Hook();

bool Install_EffectInstance_Update_Hook();
void Uninstall_EffectInstance_Update_Hook();

bool Install_State_EnterDownHoldupForceVoice_Hook();
bool Uninstall_State_EnterDownHoldupForceVoice_Hook();

bool Install_VIPSleepFaint_Hook();
bool Uninstall_VIPSleepFaint_Hook();

bool Install_NoticeActionImpl_Wake_Hook();
bool Uninstall_NoticeActionImpl_Wake_Hook();

bool Install_VIPHoldup_Hook();
bool Uninstall_VIPHoldup_Hook();

bool Install_VIPSoundRecovery_Hook();
bool Uninstall_VIPSoundRecovery_Hook();

bool Install_VIPRadio_Hook();
bool Uninstall_VIPRadio_Hook();

bool Install_HoldUpReactionCowardlyReactions_Hook();
bool Uninstall_HoldUpReactionCowardlyReactions_Hook();

bool Install_SoldierCallSign_Hook();
bool Uninstall_SoldierCallSign_Hook();

bool Install_LostHostage_Hooks();
bool Uninstall_LostHostage_Hooks();

bool Install_LostHostageDiscovery_Hooks();
bool Uninstall_LostHostageDiscovery_Hooks();

bool Install_UpdateOptCamo_Hook();
bool Uninstall_UpdateOptCamo_Hook();

bool Install_MbDvcCassetteTapeCallbackImpl_PlayOrPauseSelectedTrack_Hook();
bool Uninstall_MbDvcCassetteTapeCallbackImpl_PlayOrPauseSelectedTrack_Hook();

bool Install_SoundMusicPlayer_SetupMusicInfos_Hook();
bool Uninstall_SoundMusicPlayer_SetupMusicInfos_Hook();
bool Install_CustomTapeOwnership_Hooks();
bool Uninstall_CustomTapeOwnership_Hooks();
bool Install_CassetteTapeSetCurrentAlbum_Hook();
bool Uninstall_CassetteTapeSetCurrentAlbum_Hook();
bool Install_CassetteTrackPaging_Hooks();
bool Uninstall_CassetteTrackPaging_Hooks();

bool Install_CustomRadioCassette_Hooks();
bool Uninstall_CustomRadioCassette_Hooks();

bool Install_Control_PostExternalEvent_Hook();
bool Uninstall_Control_PostExternalEvent_Hook();

bool Install_SoundSystem_BeginSoundSystem_Hook();
bool Install_CassetteWalkmanEvents_Hook();
bool Uninstall_CassetteWalkmanEvents_Hook();
bool Uninstall_SoundSystem_BeginSoundSystem_Hook();

bool Install_TppPickableHooks();
bool Uninstall_TppPickableHooks();

bool Install_EquipIconFtexPath_Hook();
bool Uninstall_EquipIconFtexPath_Hook();

bool Install_MbDvcCustomPopup_Hook();
bool Install_MissionDeployWarning_Hook();
bool Uninstall_MissionDeployWarning_Hook();
bool Install_MissionMenuHelp_Hook();
bool Uninstall_MissionMenuHelp_Hook();
bool Uninstall_MbDvcCustomPopup_Hook();

bool Install_SoldierVoiceTypeQuery_Hook();
bool Uninstall_SoldierVoiceTypeQuery_Hook();

bool Install_VoicePitchOverride_Hook();
bool Uninstall_VoicePitchOverride_Hook();

bool Install_CAkParameterNodeBase_SetInitialRTPC_Hook();
bool Uninstall_CAkParameterNodeBase_SetInitialRTPC_Hook();
bool Install_KeySettingEvCall_SetKeySettingMenu_Hooks();
bool Uninstall_KeySettingEvCall_SetKeySettingMenu_Hooks();

bool Install_SetEyeLampColor_Hook();
bool Uninstall_SetEyeLampColor_Hook();

bool Install_GetGameObjectIdWithIndex();
bool Uninstall_GetGameObjectIdWithIndex();

bool Install_EnemyLangIdOverride_Hooks();
bool Uninstall_EnemyLangIdOverride_Hooks();

bool Install_BasicActionImpl_StateCrawlSideRoll_Hook();
bool Uninstall_BasicActionImpl_StateCrawlSideRoll_Hook();

bool Install_FobPlayerCharacters_Patches();
void Uninstall_FobPlayerCharacters_Patches();

bool Install_SearchLightActionPluginImpl_StateDoor_Hook();
bool Uninstall_SearchLightActionPluginImpl_StateDoor_Hook();

bool Install_PlacedSystemImpl_Insert_Hook();
void Uninstall_PlacedSystemImpl_Insert_Hook();

bool Install_SubtitlesEventMessage_Hook();
bool Uninstall_SubtitlesEventMessage_Hook();

bool Install_PhaseSneakAiImpl_PreUpdate_Hook();
bool Uninstall_PhaseSneakAiImpl_PreUpdate_Hook();

bool Install_MissionEmergency_Hook();
bool Uninstall_MissionEmergency_Hook();

bool Install_ShowMissionIcon_Hook();
bool Uninstall_ShowMissionIcon_Hook();

bool Install_GameObjectSendCommand_Hook();
bool Uninstall_GameObjectSendCommand_Hook();

bool Install_SecurityCamera2Impl_ProcessLuaCommand_Hook();
void Uninstall_SecurityCamera2Impl_ProcessLuaCommand_Hook();
bool Install_UavImpl_ProcessLuaCommand_Hook();
void Uninstall_UavImpl_ProcessLuaCommand_Hook();
bool Install_TppVehicle2_LuaCommands_Hook();
void Uninstall_TppVehicle2_LuaCommands_Hook();
bool Install_Soldier2Impl_ProcessLuaCommand_Hook();
void Uninstall_Soldier2Impl_ProcessLuaCommand_Hook();
bool Install_Hostage2Impl_ProcessLuaCommand_Hook();
void Uninstall_Hostage2Impl_ProcessLuaCommand_Hook();
bool Install_WalkerGear2Impl_ProcessLuaCommand_Hook();
void Uninstall_WalkerGear2Impl_ProcessLuaCommand_Hook();
bool Install_TppSahelan2_LuaCommands_Hook();
void Uninstall_TppSahelan2_LuaCommands_Hook();

bool Install_InitEquipHudData();
bool Uninstall_InitEquipHudData();


bool Install_OccasionalChatList_Hook();
bool Uninstall_OccasionalChatList_Hook();

bool Install_SoldierConversations_Hook();
bool Uninstall_SoldierConversations_Hook();

bool Install_MotherBaseSalutes_Hook();
bool Uninstall_MotherBaseSalutes_Hook();

bool Install_TppVarInit_InitializeForNewMission_Hook();
bool Uninstall_TppVarInit_InitializeForNewMission_Hook();

bool Install_SoldierNotice_Hooks();
bool Uninstall_SoldierNotice_Hooks();
bool Install_IdleActingActionImpl_CallVoice_Hook();
bool Uninstall_IdleActingActionImpl_CallVoice_Hook();
bool Install_Soldier2SoundControllerImpl_CallVoiceWithoutSync_Hook();
bool Uninstall_Soldier2SoundControllerImpl_CallVoiceWithoutSync_Hook();
bool Install_Soldier2SoundControllerImpl_CallVoiceWithSync_Hook();
bool Uninstall_Soldier2SoundControllerImpl_CallVoiceWithSync_Hook();
bool Install_RadioActionImpl_State_RadioChaffEnd_Hook();
bool Uninstall_RadioActionImpl_State_RadioChaffEnd_Hook();
bool Install_RouteAiImpl_SetSubActionParamWithNodeEvent_Hook();
bool Uninstall_RouteAiImpl_SetSubActionParamWithNodeEvent_Hook();
bool Install_AttackActionImpl_SubState_Idle_Hook();
bool Uninstall_AttackActionImpl_SubState_Idle_Hook();
bool Install_BasicAction2Impl_State_ChangeStandOrSquat_Hook();
bool Uninstall_BasicAction2Impl_State_ChangeStandOrSquat_Hook();
bool Install_HoldupAiImpl_StepHoldupStand_Hooks();
bool Uninstall_HoldupAiImpl_StepHoldupStand_Hooks();
bool Install_HoldupActionImpl_State_EndStandHoldup_Hooks();
bool Uninstall_HoldupActionImpl_State_EndStandHoldup_Hooks();
bool Install_InvestigateSituationOperatorImpl_GetSituationWithRequest_Hook();
bool Uninstall_InvestigateSituationOperatorImpl_GetSituationWithRequest_Hook();
bool Install_RouteAiImpl_StepEvent_Hook();
bool Uninstall_RouteAiImpl_StepEvent_Hook();
bool Install_AttackActionImpl_ProcessFireControl_Hook();
bool Uninstall_AttackActionImpl_ProcessFireControl_Hook();
bool Install_TppPermanentGimmickGatlingGun_SetAimMatrix_Hook();
bool Uninstall_TppPermanentGimmickGatlingGun_SetAimMatrix_Hook();
bool Install_RouteGroupImpl_GetEventAimCharacterId_Hook();
bool Uninstall_RouteGroupImpl_GetEventAimCharacterId_Hook();
bool Install_RouteAiImpl_SetSubActionParamWithEdgeEvent_Hook();
bool Uninstall_RouteAiImpl_SetSubActionParamWithEdgeEvent_Hook();
bool Install_RouteGroupImpl_GetEventId_Hook();
bool Uninstall_RouteGroupImpl_GetEventId_Hook();
bool Install_CpRadioVariants_Hooks();
bool Uninstall_CpRadioVariants_Hooks();
bool Install_AnimalNotice_Hooks();
bool Uninstall_AnimalNotice_Hooks();

bool Install_FieldTaxiMenu();
bool AccSeatIdleMotion_Install();
void AccSeatIdleMotion_Uninstall();
bool Install_CureActionPluginImpl_StateCure_Hook();
void Uninstall_CureActionPluginImpl_StateCure_Hook();
bool Install_AnimControl_GetAnimFileFromGaniPath_Hook();
bool Uninstall_AnimControl_GetAnimFileFromGaniPath_Hook();
bool Install_AnimLoader_GetMotionData_Hook();
bool Uninstall_AnimLoader_GetMotionData_Hook();
bool Install_AnimFile_GetEventData_Hook();
bool Uninstall_AnimFile_GetEventData_Hook();
bool Install_EventCallback_ExecCallback_Hook();
bool Uninstall_EventCallback_ExecCallback_Hook();
bool Install_EquipPluginImpl_NormalMtevCallback_Hook();
bool Uninstall_EquipPluginImpl_NormalMtevCallback_Hook();
bool Install_AttackActionImpl_SetEquipMotion_Hook();
bool Uninstall_AttackActionImpl_SetEquipMotion_Hook();
bool Install_RealizedEquipObjectImpl_MotionEventCallbackImpl_Hook();
bool Uninstall_RealizedEquipObjectImpl_MotionEventCallbackImpl_Hook();
bool Install_AttackActionImpl_MotionEventCallback_Hook();
bool Uninstall_AttackActionImpl_MotionEventCallback_Hook();
bool Install_CoreAiImpl_SetSpecialActionParam_Hook();
bool Uninstall_CoreAiImpl_SetSpecialActionParam_Hook();
bool Install_SpecialActionImpl_State_SpecialIdle_Hook();
bool Uninstall_SpecialActionImpl_State_SpecialIdle_Hook();
bool Uninstall_FieldTaxiMenu();

bool Install_TimeCigaretteUi_Hook();
bool Uninstall_TimeCigaretteUi_Hook();

bool Install_HeadMarkMarkerEvCall_SetIconSubType_Patch();
void Uninstall_HeadMarkMarkerEvCall_SetIconSubType_Patch();
#include "DataBaseNewFlag.h"
#include "MbTopMenuStickyHide.h"
#include "MotherBaseDeviceTopMenuTableImpl_CustomItems.h"
#include "MbDvcManageEv_CustomPage.h"
#include "PrefabListControl.h"

bool Install_DataBaseBluePrintList_Hook();
void Uninstall_DataBaseBluePrintList_Hook();
bool Install_HideBinocle_Hook();
bool Uninstall_HideBinocle_Hook();

bool Install_HeliSoundController_Hook();
bool Uninstall_HeliSoundController_Hook();
bool Install_AnnounceLogHook();
bool Uninstall_AnnounceLogHook();

bool Install_TornadoDualPatch();
void Uninstall_TornadoDualPatch();

bool Install_IsWeaponNoUseInPlaceActionPatch();
void Uninstall_IsWeaponNoUseInPlaceActionPatch();

bool Install_AttackActionImpl_UpdateShootingByPad_Hook();
void Uninstall_AttackActionImpl_UpdateShootingByPad_Hook();

bool Install_TppUIWeaponNumImpl_SetPanelInfo_Hook();
void Uninstall_TppUIWeaponNumImpl_SetPanelInfo_Hook();

bool Install_CallMenuCommandSet_UpdateState_Hook();
void Uninstall_CallMenuCommandSet_UpdateState_Hook();
bool Install_Soldier2Impl_ProcessSignal_Hook();
void Uninstall_Soldier2Impl_ProcessSignal_Hook();
bool Install_NoticeControllerImpl_CheckForceNotice_Hook();
void Uninstall_NoticeControllerImpl_CheckForceNotice_Hook();
bool Install_CallMenuManagerImpl_UpdateInShowing_Hook();
void Uninstall_CallMenuManagerImpl_UpdateInShowing_Hook();
bool Install_ActionController_SetCommand_Hook();
void Uninstall_ActionController_SetCommand_Hook();
bool Install_NoticeNoiseAiImpl_StepMovePause_Hook();
void Uninstall_NoticeNoiseAiImpl_StepMovePause_Hook();
bool Install_NoticeNoiseAiImpl_CheckTargetPositionUpdate_Hook();
void Uninstall_NoticeNoiseAiImpl_CheckTargetPositionUpdate_Hook();
bool Install_NoticeNoiseAiImpl_StepWaitReach_Hook();
void Uninstall_NoticeNoiseAiImpl_StepWaitReach_Hook();
bool Install_NoticeNoiseAiImpl_StepPreAware_Hook();
void Uninstall_NoticeNoiseAiImpl_StepPreAware_Hook();
bool Install_SoldierBackupCall_Hooks();
void Uninstall_SoldierBackupCall_Hooks();
bool Install_CallMenuImpl_UpdateOption_Hook();
void Uninstall_CallMenuImpl_UpdateOption_Hook();
bool Install_CallMenuImpl_UpdateSelectDoc_Hook();
void Uninstall_CallMenuImpl_UpdateSelectDoc_Hook();
bool Install_CallMenuImpl_UpdateCallMenu_Hook();
void Uninstall_CallMenuImpl_UpdateCallMenu_Hook();
bool Install_CallMenuManagerImpl_ShowCallMenu_Hook();
void Uninstall_CallMenuManagerImpl_ShowCallMenu_Hook();
bool Install_RadioControllerImpl_StepRadioWaitResponse_Hooks();
void Uninstall_RadioControllerImpl_StepRadioWaitResponse_Hooks();

bool Install_BarrierEffectLoadPatch();
void Uninstall_BarrierEffectLoadPatch();

bool Install_BarrierEffectSpawn();
void Uninstall_BarrierEffectSpawn();

bool Install_SupportAttackCrashGuard();
void Uninstall_SupportAttackCrashGuard();

bool Install_LuaErrorThrow();
void Uninstall_LuaErrorThrow();

bool Install_Block_Reload_Hook();
void Uninstall_Block_Reload_Hook();
bool Install_luaG_errormsg_Hook();
void Uninstall_luaG_errormsg_Hook();
bool Install_FoxGeo_CheckSphere_Hook();
void Uninstall_FoxGeo_CheckSphere_Hook();
bool Install_MessageArgs_GetValueAsDouble_Hook();
void Uninstall_MessageArgs_GetValueAsDouble_Hook();
bool Install_ScriptUpdater_ReceiveMessage_Hook();
void Uninstall_ScriptUpdater_ReceiveMessage_Hook();

bool Install_Reticle_ResetLayout_Hook();
void Uninstall_Reticle_ResetLayout_Hook();
bool Install_EnhanceLangIdUnlimited();
bool Uninstall_EnhanceLangIdUnlimited();

namespace SoldierAkObjIdMap { bool Install(); bool Uninstall(); }
bool Install_AkMemoryMgr_Malloc_Hook();
void Uninstall_AkMemoryMgr_Malloc_Hook();
bool LaserSight_UpdatePrim_Install();
void LaserSight_UpdatePrim_Uninstall();
bool Install_AttackActionImpl_SetVisibleLaserSight_Hook();
void Uninstall_AttackActionImpl_SetVisibleLaserSight_Hook();
bool Install_EquipControllerImpl_SetUpPartsStateForGun_Hook();
void Uninstall_EquipControllerImpl_SetUpPartsStateForGun_Hook();
bool Install_EquipCrossEvCall_SetPrimaryOptionPanel_Hook();
void Uninstall_EquipCrossEvCall_SetPrimaryOptionPanel_Hook();
bool Install_EquipCrossEvCall_PrimaryOptionSelect_Hook();
void Uninstall_EquipCrossEvCall_PrimaryOptionSelect_Hook();
bool Install_EquipCrossEvCall_SecondaryOptionSelect_Hook();
void Uninstall_EquipCrossEvCall_SecondaryOptionSelect_Hook();
bool Install_EquipCrossEvCall_ItemToggleChange_Hook();
void Uninstall_EquipCrossEvCall_ItemToggleChange_Hook();
namespace LaserSightSwitch { void LoadSaved(); }
bool Install_Loadout_SynchronizeLoadout();
void Uninstall_Loadout_SynchronizeLoadout();
bool Install_EquipSystemImpl_SetUpEquip();
void Uninstall_EquipSystemImpl_SetUpEquip();
bool Install_EquipControllerImpl_SetEquipState();
void Uninstall_EquipControllerImpl_SetEquipState();
bool Install_EquipControllerImpl_ChangeEquip();
void Uninstall_EquipControllerImpl_ChangeEquip();
bool Install_PickableSystemImpl_CreateInstanceFromVars();
void Uninstall_PickableSystemImpl_CreateInstanceFromVars();
bool Install_PickUpActionPluginImpl_DoPickUpWithPickableInfo();
void Uninstall_PickUpActionPluginImpl_DoPickUpWithPickableInfo();
bool Install_PickUpActionPluginImpl_CheckLoadableForAmmo();
void Uninstall_PickUpActionPluginImpl_CheckLoadableForAmmo();
bool Install_EquipDevelopControllerImpl_GetEquipIDFromDevelopIndex();
void Uninstall_EquipDevelopControllerImpl_GetEquipIDFromDevelopIndex();
bool Install_CmdSyncMotherBaseOption_Pack();
void Uninstall_CmdSyncMotherBaseOption_Pack();
bool Install_CmdSyncSoldierBinOption_Pack();
void Uninstall_CmdSyncSoldierBinOption_Pack();
bool Install_CmdSyncSoldierDiffOption_Pack();
void Uninstall_CmdSyncSoldierDiffOption_Pack();
bool Install_CmdCheckServerItemCorrect_Pack();
void Uninstall_CmdCheckServerItemCorrect_Pack();
bool Install_CmdSendBootOption_Pack();
void Uninstall_CmdSendBootOption_Pack();
bool Install_SyncSoldierBinReceive_Success();
void Uninstall_SyncSoldierBinReceive_Success();
bool Install_LoadoutManagerImpl_StoreLoadoutInfoToPlayerVars();
void Uninstall_LoadoutManagerImpl_StoreLoadoutInfoToPlayerVars();
bool PartMotionRows_Install();
bool ShellActivateGuard_Install();
void ShellActivateGuard_Uninstall();
namespace outfit
{
    bool Install_StaffFaceContainment();
    void Uninstall_StaffFaceContainment();
}
bool Install_StaffFaceLuaHook();
void Uninstall_StaffFaceLuaHook();
void PartMotionRows_Uninstall();
bool Install_TargetCqcStance_Hook();
bool Uninstall_TargetCqcStance_Hook();
bool Install_QuietCqcPatches();
void Uninstall_QuietCqcPatches();
bool Install_UniqueCharacterSuitSlot();
void Uninstall_UniqueCharacterSuitSlot();
bool Install_PlayerAttachInDemo_Hook();
bool Uninstall_PlayerAttachInDemo_Hook();
bool Install_VehicleDemoStopExempt();
void Uninstall_VehicleDemoStopExempt();
bool Install_CheckSightNoticePlayer_Hook();
bool Uninstall_CheckSightNoticePlayer_Hook();
bool Install_SoldierVehicleAvoid_Hook();
bool Uninstall_SoldierVehicleAvoid_Hook();
bool Install_Soldier2Impl_UpdateMarkers_Hook();
bool Uninstall_Soldier2Impl_UpdateMarkers_Hook();
bool Install_CombatAntiBuddyWalkerGearAiImpl_UpdatePrimaryTarget_Hook();
bool Uninstall_CombatAntiBuddyWalkerGearAiImpl_UpdatePrimaryTarget_Hook();
bool Install_NoticeAntiBuddyWalkerGearAiImpl_ShouldSleep_Hook();
bool Uninstall_NoticeAntiBuddyWalkerGearAiImpl_ShouldSleep_Hook();
bool Install_AttackActionImpl_HasOtherSoldierOnVec_Hook();
bool Uninstall_AttackActionImpl_HasOtherSoldierOnVec_Hook();
bool Install_Bullet3Impl_OnOffenseHit_Hook();
bool Uninstall_Bullet3Impl_OnOffenseHit_Hook();
bool Install_DamageHandlerImpl_ProcessDamages_Hook();
bool Uninstall_DamageHandlerImpl_ProcessDamages_Hook();
bool Install_CombatAntiBuddyWalkerGearAiImpl_StepSurprise_Hook();
bool Uninstall_CombatAntiBuddyWalkerGearAiImpl_StepSurprise_Hook();
bool Install_Soldier2Util_SearchActiveMember_Hook();
bool Uninstall_Soldier2Util_SearchActiveMember_Hook();
bool Install_CombatAntiBuddyWalkerGearAiImpl_UpdateSnipeTarget_Hook();
bool Uninstall_CombatAntiBuddyWalkerGearAiImpl_UpdateSnipeTarget_Hook();
bool Install_WalkerGearActionImpl_HasOtherSoldierOnVec_Hook();
bool Uninstall_WalkerGearActionImpl_HasOtherSoldierOnVec_Hook();
bool Install_NoticeControllerImpl_CheckNoiseNoticeSoldier_Hook();
bool Uninstall_NoticeControllerImpl_CheckNoiseNoticeSoldier_Hook();
bool Install_NoticeDownComradeAiImpl_ShouldSleep_Hook();
bool Uninstall_NoticeDownComradeAiImpl_ShouldSleep_Hook();
bool Install_VehicleNoticeDownComradeAiImpl_ShouldSleep_Hook();
bool Uninstall_VehicleNoticeDownComradeAiImpl_ShouldSleep_Hook();
bool Install_RideVehicleActionImpl_HasOtherSoldierOnVec_Hook();
bool Uninstall_RideVehicleActionImpl_HasOtherSoldierOnVec_Hook();
bool Install_RideVehicleActionImpl_State_IdleAction_Hook();
bool Uninstall_RideVehicleActionImpl_State_IdleAction_Hook();
bool Install_RideVehicleActionImpl_UpdateShootState_Hook();
bool Uninstall_RideVehicleActionImpl_UpdateShootState_Hook();
bool Install_VehicleCommunicationInfo_FetchVehicleSeatLocalMatrix_Hook();
bool Uninstall_VehicleCommunicationInfo_FetchVehicleSeatLocalMatrix_Hook();
bool Install_AttackActionImpl_GetAimState_Hook();
bool Uninstall_AttackActionImpl_GetAimState_Hook();
bool Install_InVehicleAiImpl_ShouldWake_Hook();
bool Uninstall_InVehicleAiImpl_ShouldWake_Hook();
bool Install_EffectControllerImpl_CreateEffect_Hook();
bool Uninstall_EffectControllerImpl_CreateEffect_Hook();
bool JeepGunnerSeatMotion_Install();
void JeepGunnerSeatMotion_Uninstall();
bool JeepGunnerSeat_Install(bool seatReadersHooked);
void JeepGunnerSeat_Uninstall();
bool JeepGunEffects_Install();
void JeepGunEffects_Uninstall();
bool Install_BodyData_SetupBody_Hook();
void Uninstall_BodyData_SetupBody_Hook();
bool Install_AttachmentData_SetupAttachment_Hook();
void Uninstall_AttachmentData_SetupAttachment_Hook();
bool Install_ConstantImpl_GetVehicleTypeSpecs_Hook();
void Uninstall_ConstantImpl_GetVehicleTypeSpecs_Hook();
bool Install_VehicleGameObjectImpl_Initialize_Hook();
void Uninstall_VehicleGameObjectImpl_Initialize_Hook();
namespace CustomVehicles { void SetHooksInstalled(bool installed); }
namespace VehicleSortieRows { bool Install(); void Uninstall(); }
bool Install_LifeControllerImpl_SendStatusMessage_Hook();
bool Uninstall_LifeControllerImpl_SendStatusMessage_Hook();
bool Install_CombatAntiBuddyWalkerGearAiImpl_CheckGrenade_Hook();
bool Uninstall_CombatAntiBuddyWalkerGearAiImpl_CheckGrenade_Hook();
void SoldierFaction_SetModuleActive(bool active);
bool Install_InterrogationVoiceEvent_Hook();
bool Uninstall_InterrogationVoiceEvent_Hook();

namespace outfit
{
    bool Install_OutfitEquippedState_Hooks();
    void Uninstall_OutfitEquippedState_Hooks();

    bool Install_OutfitRuntimeParts_Hooks();
    void Uninstall_OutfitRuntimeParts_Hooks();
    bool Install_OutfitSuitConditionApply_Hook();
    void Uninstall_OutfitSuitConditionApply_Hook();
    bool Install_OutfitItemSelector_Hook();
    void Uninstall_OutfitItemSelector_Hook();
    bool Install_OutfitListInject_Hook();
    void Uninstall_OutfitListInject_Hook();
    bool Install_DevelopWeaponList_Hook();
    void Uninstall_DevelopWeaponList_Hook();
    bool Install_OutfitPalette();
    void Uninstall_OutfitPalette();
    bool Install_OutfitHeadOption_Hook();
    void Uninstall_OutfitHeadOption_Hook();
    bool Install_OutfitCamoBonus_Hook();
    void Uninstall_OutfitCamoBonus_Hook();
    bool Install_OutfitGetCamoufValue_Hook();
    void Uninstall_OutfitGetCamoufValue_Hook();
    bool Install_OutfitMotionMtar_Hook();
    void Uninstall_OutfitMotionMtar_Hook();
    bool Install_OutfitAbilities_Hooks();
    void Uninstall_OutfitAbilities_Hooks();
    bool Install_PlayerInfoGetPartsType_Hooks();
    void Uninstall_PlayerInfoGetPartsType_Hooks();
    bool Install_GetVfxFileFromParts_Hook();
    void Uninstall_GetVfxFileFromParts_Hook();
    bool Install_SuitParamUniqueCharacter_Hook();
    void Uninstall_SuitParamUniqueCharacter_Hook();
    bool Install_UniqueCharacterName_Hook();
    void Uninstall_UniqueCharacterName_Hook();
    bool Install_OwnSuitRowLabel_Hooks();
    void Uninstall_OwnSuitRowLabel_Hooks();
}
namespace EquipDevelopAdd
{
    bool Install_TppMotherBaseManagement_EquipDevelopHooks();
    bool Uninstall_TppMotherBaseManagement_EquipDevelopHooks();
}
void EquipDevelop_InstallDevelopSyncHooks();
void EquipDevelop_ReleaseDevelopTimerHook();
void EquipDevelop_ReleaseSetEquipNewGuard();
namespace equip
{
    bool Install_BulletLockOn_Hooks();
    void Uninstall_BulletLockOn_Hooks();
}
namespace shell
{
    bool Install_RemoteMissile_Hooks();
    void Uninstall_RemoteMissile_Hooks();
}
namespace equip
{
    bool Install_BulletMultiShot_Hooks();
    void Uninstall_BulletMultiShot_Hooks();
    bool Install_MenuDevelopGridExpand();
    bool Install_CustomizeSelectorCallbackImpl_SetupItemList_Hook();
    void Uninstall_CustomizeSelectorCallbackImpl_SetupItemList_Hook();
    bool Install_DevelopArrayGrow();
    void Uninstall_DevelopArrayGrow();
    bool DevelopArrayGrowPending();
    bool Install_UiEquipPreviewControllerScroll();
    void Uninstall_UiEquipPreviewControllerScroll();
    bool Install_EquipDevelopControllerImpl_IsDisplayableBluePrintIDContion();
    void Uninstall_EquipDevelopControllerImpl_IsDisplayableBluePrintIDContion();
}
bool Install_TppEquip_RegisterConstant_Hook();
bool Uninstall_TppEquip_RegisterConstant_Hook();
bool Install_TppEquip_ReloadEquipIdTable_Hook();
bool Uninstall_TppEquip_ReloadEquipIdTable_Hook();
bool Install_TppEquip_ReloadEquipParameterTables2_Hook();
bool Uninstall_TppEquip_ReloadEquipParameterTables2_Hook();
bool Install_MotionLoader_ReceiverTypeHook();
void Uninstall_MotionLoader_ReceiverTypeHook();
bool Install_MotionLoader_UnderBarrelTypeHook();
void Uninstall_MotionLoader_UnderBarrelTypeHook();
bool Install_MotionLoader_BarrelTypeHook();
void Uninstall_MotionLoader_BarrelTypeHook();
bool Install_MotionLoader_MagazineTypeHook();
void Uninstall_MotionLoader_MagazineTypeHook();
bool Install_MotionLoader_StockTypeHook();
bool Install_UiUtility_GetWeaponItemNameLangIdHook();
void Uninstall_MotionLoader_StockTypeHook();
bool Install_UiController_UiSightTypeHook();
void Uninstall_UiController_UiSightTypeHook();
void Uninstall_UiUtility_GetWeaponItemNameLangIdHook();
bool Install_GetAttackIdGuard();
void Uninstall_GetAttackIdGuard();
bool Install_GunInfoGuard();
void Uninstall_GunInfoGuard();
bool Install_BulletEffectGuard();
void Uninstall_BulletEffectGuard();
bool Install_WeaponKeyLog();
void Uninstall_WeaponKeyLog();
bool Install_FireSoundOverride_Hook();
void Uninstall_FireSoundOverride_Hook();
bool Install_LoadoutRequestGuard();
void Uninstall_LoadoutRequestGuard();
bool Install_LoadoutGunInfoGetOrBuild();
void Uninstall_LoadoutGunInfoGetOrBuild();
bool Install_SuppressorGauge_Hook();
void Uninstall_SuppressorGauge_Hook();
bool Install_DamageParameter_Hook();
void Uninstall_DamageParameter_Hook();


namespace
{
    class AudioBankMemoryModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "AudioBankMemory";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_AkMemoryMgr_Malloc_Hook();
        }

        void Uninstall() override
        {
            Uninstall_AkMemoryMgr_Malloc_Hook();
        }
    };

    class FobLoadoutSyncGuardModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "FobLoadoutSyncGuard";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_Loadout_SynchronizeLoadout();
        }

        void Uninstall() override
        {
            Uninstall_Loadout_SynchronizeLoadout();
        }
    };

    class WideEquipAliasGatesModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "WideEquipAliasGates";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            const bool setUpEquip    = Install_EquipSystemImpl_SetUpEquip();
            const bool equipState    = Install_EquipControllerImpl_SetEquipState();
            const bool droppedFromVars = Install_PickableSystemImpl_CreateInstanceFromVars();
            const bool pickUp        = Install_PickUpActionPluginImpl_DoPickUpWithPickableInfo();
            const bool developRows   = Install_EquipDevelopControllerImpl_GetEquipIDFromDevelopIndex();
            const bool changeEquip   = Install_EquipControllerImpl_ChangeEquip();
            Install_PickUpActionPluginImpl_CheckLoadableForAmmo();
            return setUpEquip && equipState && droppedFromVars && pickUp && developRows && changeEquip;
        }

        void Uninstall() override
        {
            Uninstall_PickUpActionPluginImpl_CheckLoadableForAmmo();
            Uninstall_EquipControllerImpl_ChangeEquip();
            Uninstall_EquipDevelopControllerImpl_GetEquipIDFromDevelopIndex();
            Uninstall_PickUpActionPluginImpl_DoPickUpWithPickableInfo();
            Uninstall_PickableSystemImpl_CreateInstanceFromVars();
            Uninstall_EquipControllerImpl_SetEquipState();
            Uninstall_EquipSystemImpl_SetUpEquip();
        }
    };

    class MotherBaseSyncGuardModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "MotherBaseSyncGuard";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_CmdSyncMotherBaseOption_Pack();
        }

        void Uninstall() override
        {
            Uninstall_CmdSyncMotherBaseOption_Pack();
        }
    };

    class StaffSyncGuardModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "StaffSyncGuard";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            const bool echo = Install_SyncSoldierBinReceive_Success();
            const bool bin = echo && Install_CmdSyncSoldierBinOption_Pack();
            const bool diff = Install_CmdSyncSoldierDiffOption_Pack();
            return echo && bin && diff;
        }

        void Uninstall() override
        {
            Uninstall_CmdSyncSoldierBinOption_Pack();
            Uninstall_CmdSyncSoldierDiffOption_Pack();
            Uninstall_SyncSoldierBinReceive_Success();
        }
    };

    class ItemCheckGuardModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "ItemCheckGuard";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_CmdCheckServerItemCorrect_Pack();
        }

        void Uninstall() override
        {
            Uninstall_CmdCheckServerItemCorrect_Pack();
        }
    };

    class BootRecordGuardModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "BootRecordGuard";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_CmdSendBootOption_Pack();
        }

        void Uninstall() override
        {
            Uninstall_CmdSendBootOption_Pack();
        }
    };

    class LoadoutItemLevelGuardModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "LoadoutItemLevelGuard";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_LoadoutManagerImpl_StoreLoadoutInfoToPlayerVars();
        }

        void Uninstall() override
        {
            Uninstall_LoadoutManagerImpl_StoreLoadoutInfoToPlayerVars();
        }
    };

    class LaserSightColorModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "LaserSightColor";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return LaserSight_UpdatePrim_Install();
        }

        void Uninstall() override
        {
            LaserSight_UpdatePrim_Uninstall();
        }
    };

    class LaserSightSwitchModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "LaserSightSwitch";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            LaserSightSwitch::LoadSaved();
            if (!Install_AttackActionImpl_SetVisibleLaserSight_Hook())
                return true;
            Install_EquipControllerImpl_SetUpPartsStateForGun_Hook();
            Install_EquipCrossEvCall_SetPrimaryOptionPanel_Hook();
            Install_EquipCrossEvCall_PrimaryOptionSelect_Hook();
            Install_EquipCrossEvCall_SecondaryOptionSelect_Hook();
            Install_EquipCrossEvCall_ItemToggleChange_Hook();
            return true;
        }

        void Uninstall() override
        {
            Uninstall_EquipCrossEvCall_ItemToggleChange_Hook();
            Uninstall_EquipCrossEvCall_SecondaryOptionSelect_Hook();
            Uninstall_EquipCrossEvCall_PrimaryOptionSelect_Hook();
            Uninstall_EquipCrossEvCall_SetPrimaryOptionPanel_Hook();
            Uninstall_EquipControllerImpl_SetUpPartsStateForGun_Hook();
            Uninstall_AttackActionImpl_SetVisibleLaserSight_Hook();
        }
    };

    class LuaErrorUnwindModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "LuaErrorUnwind"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_luaG_errormsg_Hook(); }
        void Uninstall() override { Uninstall_luaG_errormsg_Hook(); }
    };

    class GeoWorldGuardModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "GeoWorldGuard"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_FoxGeo_CheckSphere_Hook(); }
        void Uninstall() override { Uninstall_FoxGeo_CheckSphere_Hook(); }
    };

    class WideEquipMessagesModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "WideEquipMessages"; }
        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            const bool missionScripts = Install_MessageArgs_GetValueAsDouble_Hook();
            const bool scriptUpdaters = Install_ScriptUpdater_ReceiveMessage_Hook();
            return missionScripts || scriptUpdaters;
        }
        void Uninstall() override
        {
            Uninstall_ScriptUpdater_ReceiveMessage_Hook();
            Uninstall_MessageArgs_GetValueAsDouble_Hook();
        }
    };

    class EquipOwnPackModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "EquipOwnPack"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_EquipOwnPack_Hook(); }
        void Uninstall() override { Uninstall_EquipOwnPack_Hook(); }
    };

    class EquipCustomPlacedModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "EquipCustomPlaced"; }
        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            bool ok = Install_PlacedImpl_ExecuteAfter_Hook();
            ok = Install_PlacedSystemImpl_CreateCorrelation_Hook() && ok;
            return ok;
        }
        void Uninstall() override
        {
            Uninstall_PlacedSystemImpl_CreateCorrelation_Hook();
            Uninstall_PlacedImpl_ExecuteAfter_Hook();
        }
    };

    class Fox2EquipNamesModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "Fox2EquipNames"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_EquipIdTableImpl_GetEquipIdFromStrCode32Impl_Hook(); }
        void Uninstall() override { Uninstall_EquipIdTableImpl_GetEquipIdFromStrCode32Impl_Hook(); }
    };

    class SoldierGrenadeIdsModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "SoldierGrenadeIds"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_Soldier2CombatUtil_GrenadeUpdate_Hook(); }
        void Uninstall() override { Uninstall_Soldier2CombatUtil_GrenadeUpdate_Hook(); }
    };

    class DamageGimmickModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "DamageGimmick"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_PermanentGimmickDefenseCallback_Exec_Hooks(); }
        void Uninstall() override { Uninstall_PermanentGimmickDefenseCallback_Exec_Hooks(); }
    };

    class PartMotionRowsModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "PartMotionRows";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return PartMotionRows_Install();
        }

        void Uninstall() override
        {
            PartMotionRows_Uninstall();
        }
    };

    class SupportWeaponModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "SupportWeapon";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            bool ok = Install_EquipParameterTablesImpl_ReloadEquipParameterTablesImpl_Hook();
            ok = Install_EquipIdTableImpl_GetSupportWeaponTypeId_Hook() && ok;
            ok = Install_ThrowingImpl_GetBlastParamByEquipId_Hook() && ok;
            ok = Install_ThrowingImpl_UpdateAction_Hooks() && ok;
            ok = Install_ThrowingImpl_UpdateStatus_Hook() && ok;
            ok = Install_ThrowingImpl_AddReboundNoise_Hook() && ok;
            ok = Install_ThrowingImpl_ActivateThrowingAtEmptyWork_Hook() && ok;
            ok = Install_ThrowingImpl_UpdateDeactivation_Hook() && ok;
            ok = Install_ThrowingSystemImpl_ExecuteSerially_Hook() && ok;
            ok = Install_ThrowingImpl_OnOffenseHit_Hook() && ok;
            ok = Install_AttackActionImpl_CalcThrowingVelocity_Hook() && ok;
            ok = Install_EquipDevelopControllerImpl_GetDamageType_Hook() && ok;
            ok = Install_EquipControllerImpl_InitializePlayerAtIndex_Hook() && ok;
            ok = Install_LoadoutManagerImpl_RestoreLoadoutInfosFromVars_Hooks() && ok;
            ok = Install_PlacedSystemImpl_RestorationPlace_Hook() && ok;
            ok = Install_PlacedSystemImpl_CopyToScriptVarsPlayerPlacedWeapon_Hook() && ok;
            ok = Install_PlacedSystemImpl_CreateByLocators_Hook() && ok;
            ok = Install_SupplyCboxGameObjectImpl_RestoreRequestFromSVars_Hook() && ok;
            ok = Install_Player2UtilityImpl_GetPickableInfosOfAmmoBox_Hook() && ok;
            ok = Install_EquipParameterTablesImpl_GetSupportWeaponParameterBlock_Hook() && ok;
            ok = Install_EquipDevelopControllerImpl_GetDamageAmountRatio_Hook() && ok;
            ok = SupportStockLease::Install() && ok;
            return ok;
        }

        void Uninstall() override
        {
            SupportStockLease::Uninstall();
            Uninstall_EquipDevelopControllerImpl_GetDamageAmountRatio_Hook();
            Uninstall_EquipParameterTablesImpl_GetSupportWeaponParameterBlock_Hook();
            Uninstall_Player2UtilityImpl_GetPickableInfosOfAmmoBox_Hook();
            Uninstall_SupplyCboxGameObjectImpl_RestoreRequestFromSVars_Hook();
            Uninstall_PlacedSystemImpl_CreateByLocators_Hook();
            Uninstall_PlacedSystemImpl_CopyToScriptVarsPlayerPlacedWeapon_Hook();
            Uninstall_PlacedSystemImpl_RestorationPlace_Hook();
            Uninstall_LoadoutManagerImpl_RestoreLoadoutInfosFromVars_Hooks();
            Uninstall_EquipControllerImpl_InitializePlayerAtIndex_Hook();
            Uninstall_EquipDevelopControllerImpl_GetDamageType_Hook();
            Uninstall_AttackActionImpl_CalcThrowingVelocity_Hook();
            Uninstall_ThrowingImpl_OnOffenseHit_Hook();
            Uninstall_ThrowingSystemImpl_ExecuteSerially_Hook();
            Uninstall_ThrowingImpl_UpdateDeactivation_Hook();
            Uninstall_ThrowingImpl_ActivateThrowingAtEmptyWork_Hook();
            Uninstall_ThrowingImpl_AddReboundNoise_Hook();
            Uninstall_ThrowingImpl_UpdateStatus_Hook();
            Uninstall_ThrowingImpl_UpdateAction_Hooks();
            Uninstall_ThrowingImpl_GetBlastParamByEquipId_Hook();
            Uninstall_EquipIdTableImpl_GetSupportWeaponTypeId_Hook();
            Uninstall_EquipParameterTablesImpl_ReloadEquipParameterTablesImpl_Hook();
        }
    };

    class SupportWeaponGravityModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "SupportWeaponGravity"; }
        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            bool ok = Install_ThrowingImpl_UpdateNextPosition_Caves();
            ok = Install_ThrowingImpl_DrawLocusLineImpl_Hook() && ok;
            return ok;
        }
        void Uninstall() override
        {
            Uninstall_ThrowingImpl_DrawLocusLineImpl_Hook();
            Uninstall_ThrowingImpl_UpdateNextPosition_Caves();
        }
    };

    class GradeFamilyModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "GradeFamily"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_ItemSelectorCallbackImpl_IsParentEquip_Hook(); }
        void Uninstall() override { Uninstall_ItemSelectorCallbackImpl_IsParentEquip_Hook(); }
    };

    class DecoyModelModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "DecoyModel"; }
        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            if (!Install_DecoyResourceManagerImpl_InitializePlayerDecoyModel_Hook())
                return false;
            if (Install_DecoySystemImpl_Realize_Hook())
                return true;
            Uninstall_DecoyResourceManagerImpl_InitializePlayerDecoyModel_Hook();
            return false;
        }

        void Uninstall() override
        {
            Uninstall_DecoySystemImpl_Realize_Hook();
            Uninstall_DecoyResourceManagerImpl_InitializePlayerDecoyModel_Hook();
        }
    };

    class ShellActivateGuardModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "ShellActivateGuard";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return ShellActivateGuard_Install();
        }

        void Uninstall() override
        {
            ShellActivateGuard_Uninstall();
        }
    };

    class LuaBridgeModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "LuaBridge";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_SetLuaFunctions_Hook();
        }

        void Uninstall() override
        {
            Uninstall_SetLuaFunctions_Hook();
        }
    };

    class EquipBgTextureModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "EquipBgTexture";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_EquipBgTexture_Hook();
        }

        void Uninstall() override
        {
            Uninstall_EquipBgTexture_Hook();
        }
    };

    class UixUtilityControlModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "UixUtilityControl";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_UixUtilityControl_Hook();
        }

        void Uninstall() override
        {
            Uninstall_UixUtilityControl_Hook();
        }
    };

    class FontGlyphGuardModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "FontGlyphGuard";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_FontData_SearchGlyphData_Hook();
        }

        void Uninstall() override
        {
            Uninstall_FontData_SearchGlyphData_Hook();
        }
    };

    class BinoRadioIconModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "BinoRadioIcon";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_TppUIBinoSubjectiveImpl_Act_Hook();
        }

        void Uninstall() override
        {
            Uninstall_TppUIBinoSubjectiveImpl_Act_Hook();
        }
    };

    class SonyPadLayoutModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "SonyPadLayout";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_SonyPadLayout_Hook();
        }

        void Uninstall() override
        {
            Uninstall_SonyPadLayout_Hook();
        }
    };

    class ButtonMeshNodeGuardModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "ButtonMeshNodeGuard";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_ModelNodeMesh_Destruct_Hook();
        }

        void Uninstall() override
        {
            Uninstall_ModelNodeMesh_Destruct_Hook();
        }
    };

    class PadAssignModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "PadAssign";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_PadAssign_Hooks();
        }

        void Uninstall() override
        {
            Uninstall_PadAssign_Hooks();
        }
    };

    class ControllerAssignMenuModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "ControllerAssignMenu";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_ControllerAssignMenu_Hooks();
        }

        void Uninstall() override
        {
            Uninstall_ControllerAssignMenu_Hooks();
        }
    };

    class ControlTypePageAssignModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "ControlTypePageAssign";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_ControlTypePageAssign_Hooks();
        }

        void Uninstall() override
        {
            Uninstall_ControlTypePageAssign_Hooks();
        }
    };

    class ButtonIconSheetModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "ButtonIconSheet";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_ButtonIconSheet_Hook();
        }

        void Uninstall() override
        {
            Uninstall_ButtonIconSheet_Hook();
        }
    };

    class MainPadSlotModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "MainPadSlot";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_MainPadSlot_Hook();
        }

        void Uninstall() override
        {
            Uninstall_MainPadSlot_Hook();
        }
    };

    class TextIconsModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "TextIcons";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            if (!Install_UiDepend_ActHudChecking_Hook())
                return false;
            Install_ButtonMapper_SetDefaultButtonMap_Hook();
            return true;
        }

        void Uninstall() override
        {
            Uninstall_ButtonMapper_SetDefaultButtonMap_Hook();
            Uninstall_UiDepend_ActHudChecking_Hook();
        }
    };

    class FallDeathVoiceModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "FallDeathVoice";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            bool ok = Install_Player2Impl_CallVoice_Hook();
            ok &= Install_CqcActionPluginImpl_CompleteContinueCqcTarget_Hook();
            ok &= Install_IdroidVoiceLines_Hooks();
            ok &= Install_MbDvcSoundControllerImpl_RequestVoice_Hook();
            ok &= Install_FobMission2CallbackImpl_Update_Hook();
            ok &= Install_HostageVoiceLines_Hooks();
            ok &= Install_SightManagerImpl_UpdateEspionageRadio_Hooks();
            ok &= Install_EquipControllerImpl_OnShieldHit_Hook();
            ok &= Install_HorseDamageActionImpl_State_Whiii_Hook();
            ok &= Install_AkSoundEngine_PostEvent_Hook();
            ok &= Install_RideHorseActionPluginImpl_State_Idle_Hook();
            ok &= Install_GetFobStatusCallback_AnnounceDefense_Hooks();
            return ok;
        }

        void Uninstall() override
        {
            Uninstall_GetFobStatusCallback_AnnounceDefense_Hooks();
            Uninstall_RideHorseActionPluginImpl_State_Idle_Hook();
            Uninstall_AkSoundEngine_PostEvent_Hook();
            Uninstall_HorseDamageActionImpl_State_Whiii_Hook();
            Uninstall_EquipControllerImpl_OnShieldHit_Hook();
            Uninstall_SightManagerImpl_UpdateEspionageRadio_Hooks();
            Uninstall_HostageVoiceLines_Hooks();
            Uninstall_FobMission2CallbackImpl_Update_Hook();
            Uninstall_MbDvcSoundControllerImpl_RequestVoice_Hook();
            Uninstall_IdroidVoiceLines_Hooks();
            Uninstall_CqcActionPluginImpl_CompleteContinueCqcTarget_Hook();
            Uninstall_Player2Impl_CallVoice_Hook();
        }
    };

    class QuietVoiceLinesModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "QuietVoiceLines";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            bool ok = Install_BuddyQuiet2SoundControllerImpl_CallVoice_Hook();
            ok &= Install_BuddyQuietDamageHandlerImpl_ProcessDamages_Hook();
            return ok;
        }

        void Uninstall() override
        {
            Uninstall_BuddyQuietDamageHandlerImpl_ProcessDamages_Hook();
            Uninstall_BuddyQuiet2SoundControllerImpl_CallVoice_Hook();
        }
    };

    class GameObjectLifeModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "GameObjectLife";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_GameObjectLife_Hooks();
        }

        void Uninstall() override
        {
            Uninstall_GameObjectLife_Hooks();
        }
    };

    class MissionTelopBgTextureModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "MissionTelopBgTexture";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_MissionTelopBgTexture_Hook();
        }

        void Uninstall() override
        {
            Uninstall_MissionTelopBgTexture_Hook();
        }
    };

    class LoadingSplashModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "LoadingSplash";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_LoadingSplash_Hook();
        }

        void Uninstall() override
        {
            Uninstall_LoadingSplash_Hook();
        }
    };

    class GameOverSplashModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "GameOverSplash";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_GameOverSplash_Hook();
        }

        void Uninstall() override
        {
            Uninstall_GameOverSplash_Hook();
        }
    };

    class BonusPopupBgTextureModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "BonusPopupBgTexture";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_TppBonusPopupImpl_SetBgTexture_Hook();
        }

        void Uninstall() override
        {
            Uninstall_TppBonusPopupImpl_SetBgTexture_Hook();
        }
    };

    class MissionPreparationBgmModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "MissionPreparationBgm";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_MissionPreparationCallbackImpl_Start_Hook();
        }

        void Uninstall() override
        {
            Uninstall_MissionPreparationCallbackImpl_Start_Hook();
        }
    };

    class RewardPopupBgTextureModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "RewardPopupBgTexture";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_RewardPopupBgTexture_Hook();
        }

        void Uninstall() override
        {
            Uninstall_RewardPopupBgTexture_Hook();
        }
    };

    class HoldupCancelLookToPlayerModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "HoldupCancelLookToPlayer";
        }

        bool Install(HMODULE hGame) override
        {
            return Install_State_StandHoldupCancelLookToPlayer_Hook(hGame);
        }

        void Uninstall() override
        {
            Uninstall_State_StandHoldupCancelLookToPlayer_Hook();
        }
    };

    class CautionTimerModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "CautionTimer";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_CautionStepNormalTimerHook();
        }

        void Uninstall() override
        {
            Uninstall_CautionStepNormalTimerHook();
        }
    };

    class CpAntiAirModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "CpAntiAir";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_CpAntiAir_Hook();
        }

        void Uninstall() override
        {
            Uninstall_CpAntiAir_Hook();
        }
    };

    class AccSeatIdleMotionModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "AccSeatIdleMotion";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return AccSeatIdleMotion_Install();
        }

        void Uninstall() override
        {
            AccSeatIdleMotion_Uninstall();
        }
    };

    class SupineCureMotionModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "SupineCureMotion";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_CureActionPluginImpl_StateCure_Hook();
        }

        void Uninstall() override
        {
            Uninstall_CureActionPluginImpl_StateCure_Hook();
        }
    };

    class LifeRecoveryCooldownModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "LifeRecoveryCooldown";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_LifeControllerImpl_UpdateHost();
        }

        void Uninstall() override
        {
            Uninstall_LifeControllerImpl_UpdateHost();
        }
    };

    class CustomWeaponLoadoutCostModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "CustomWeaponLoadoutCost";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_MissionPreparationSystemImpl_CalcEquipWeaponItemCostFromLoadout();
        }

        void Uninstall() override
        {
            Uninstall_MissionPreparationSystemImpl_CalcEquipWeaponItemCostFromLoadout();
        }
    };

    class UnderBarrelAmmoMeshModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "UnderBarrelAmmoMesh";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_EquipSystemImpl_SetAmmoCount();
        }

        void Uninstall() override
        {
            Uninstall_EquipSystemImpl_SetAmmoCount();
        }
    };

    class SupplyDropItemGuardModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "SupplyDropItemGuard";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            const bool supplyDrop = Install_ItemSelectorCallbackImpl_IsGetLatestItemTool();
            const bool developMenu = Install_EquipDevelopCallbackImpl_IsLatestItemTool();
            return supplyDrop && developMenu;
        }

        void Uninstall() override
        {
            Uninstall_EquipDevelopCallbackImpl_IsLatestItemTool();
            Uninstall_ItemSelectorCallbackImpl_IsGetLatestItemTool();
        }
    };

    class ItemBatteryModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "ItemBattery";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            const bool tick = Install_EquipControllerImpl_UpdateBattery_Hook();
            const bool gauge = Install_UiUtilityImpl_GetBatteryLevel_Hook();
            return tick && gauge;
        }

        void Uninstall() override
        {
            Uninstall_UiUtilityImpl_GetBatteryLevel_Hook();
            Uninstall_EquipControllerImpl_UpdateBattery_Hook();
        }
    };

    class ItemUseModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "ItemUse";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_EquipCrossEvCall_SpecialItemAct_Hook();
        }

        void Uninstall() override
        {
            Uninstall_EquipCrossEvCall_SpecialItemAct_Hook();
        }
    };

    class ItemCountModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "ItemCount";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_PickUpActionPluginImpl_PickUpAmmoBox_Hook();
        }

        void Uninstall() override
        {
            Uninstall_PickUpActionPluginImpl_PickUpAmmoBox_Hook();
        }
    };

    class ItemParameterModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "ItemParameter";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_SkillAndItemParameterSystemImpl_GetItemParamWithEqpId_Hook();
        }

        void Uninstall() override
        {
            Uninstall_SkillAndItemParameterSystemImpl_GetItemParamWithEqpId_Hook();
        }
    };

    class ShieldParamsModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "ShieldParams";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_ShieldParams_Hooks();
        }

        void Uninstall() override
        {
            Uninstall_ShieldParams_Hooks();
        }
    };

    class ItemEffectGaugeModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "ItemEffectGauge";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_TppUIWeaponNumImpl_ShowItemPanel_Hook();
        }

        void Uninstall() override
        {
            Uninstall_TppUIWeaponNumImpl_ShowItemPanel_Hook();
        }
    };

    class UavEmpModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "UavEmp";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_ActionCoreImpl_ExecPostPutMotion_Hook();
        }

        void Uninstall() override
        {
            Uninstall_ActionCoreImpl_ExecPostPutMotion_Hook();
        }
    };

    class SpRespawnModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "SpRespawn";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_RespawnActionPluginImpl_PollingImpl_Patch();
        }

        void Uninstall() override
        {
            Uninstall_RespawnActionPluginImpl_PollingImpl_Patch();
        }
    };

    class LoadWatchModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "LoadWatch";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            const bool ok = Install_PathHashCode_Hook();
            LoadWatch::Start();
            return ok;
        }

        void Uninstall() override
        {
            LoadWatch::Stop();
            Uninstall_PathHashCode_Hook();
        }
    };

    class FobVisitGuardModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "FobVisitGuard";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_FobTarget_RequestSneakCommon();
        }

        void Uninstall() override
        {
            Uninstall_FobTarget_RequestSneakCommon();
        }
    };

    class SoldierWideWeaponModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "SoldierWideWeapon";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            const bool execute = Install_EquipPluginImpl_Execute();
            const bool drop = Install_EquipControllerImpl_DropEquip();
            return execute && drop;
        }

        void Uninstall() override
        {
            Uninstall_EquipControllerImpl_DropEquip();
            Uninstall_EquipPluginImpl_Execute();
        }
    };

    class FrameWorkMotionArchiveModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "FrameWorkMotionArchive";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            Install_AnimLoader_GetMotionData_Hook();
            return Install_AnimControl_GetAnimFileFromGaniPath_Hook();
        }

        void Uninstall() override
        {
            Uninstall_AnimControl_GetAnimFileFromGaniPath_Hook();
            Uninstall_AnimLoader_GetMotionData_Hook();
        }
    };

    class ClipEventsModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "ClipEvents";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            Install_AnimFile_GetEventData_Hook();
            Install_EventCallback_ExecCallback_Hook();
            Install_AttackActionImpl_SetEquipMotion_Hook();
            Install_RealizedEquipObjectImpl_MotionEventCallbackImpl_Hook();
            Install_AttackActionImpl_MotionEventCallback_Hook();
            Install_EquipPluginImpl_NormalMtevCallback_Hook(); // V_FrameWork_ItemRightHand
            return true;
        }

        void Uninstall() override
        {
            Uninstall_EquipPluginImpl_NormalMtevCallback_Hook();
            Uninstall_AttackActionImpl_MotionEventCallback_Hook();
            Uninstall_RealizedEquipObjectImpl_MotionEventCallbackImpl_Hook();
            Uninstall_AttackActionImpl_SetEquipMotion_Hook();
            Uninstall_EventCallback_ExecCallback_Hook();
            Uninstall_AnimFile_GetEventData_Hook();
        }
    };

    class HostageStruggleModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "HostageStruggle";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            Install_SpecialActionImpl_State_SpecialIdle_Hook();
            return Install_CoreAiImpl_SetSpecialActionParam_Hook();
        }

        void Uninstall() override
        {
            Uninstall_CoreAiImpl_SetSpecialActionParam_Hook();
            Uninstall_SpecialActionImpl_State_SpecialIdle_Hook();
        }
    };

    class ChangeLocationMenuModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "ChangeLocationMenu";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_ChangeLocationMenu_Hook();
        }

        void Uninstall() override
        {
            Uninstall_ChangeLocationMenu_Hook();
        }
    };

    class LocationIconModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "LocationIcon";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            const bool rows = Install_MbDvcMissionListRecordCallFunc_Start_Hook();
            const bool header = Install_MbDvcMapCallbackBaseImpl_UpdateLocationInfoView_Hook();
            const bool sideOps = Install_MbDvcSideOpsRecordCallFunc_StartBody_Hook();
            return rows && header && sideOps;
        }

        void Uninstall() override
        {
            Uninstall_MbDvcMissionListRecordCallFunc_Start_Hook();
            Uninstall_MbDvcMapCallbackBaseImpl_UpdateLocationInfoView_Hook();
            Uninstall_MbDvcSideOpsRecordCallFunc_StartBody_Hook();
        }
    };

    class MissionListLocationTabsModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "MissionListLocationTabs";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_MbDvcMissionListCallbackImpl_ActivatePrefabTablistParameter_Hook();
        }

        void Uninstall() override
        {
            Uninstall_MbDvcMissionListCallbackImpl_ActivatePrefabTablistParameter_Hook();
        }
    };

    class PhotoAdditionalTextModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "PhotoAdditionalText";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_PhotoAdditionalText_Hook();
        }

        void Uninstall() override
        {
            Uninstall_PhotoAdditionalText_Hook();
        }
    };

    class RealizedSahelanFovaModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "RealizedSahelanFova";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_RealizedSahelanFova_Hook();
        }

        void Uninstall() override
        {
            Uninstall_RealizedSahelanFova_Hook();
        }
    };

    class MeshVisibilityModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "MeshVisibility";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            const bool soldierOk = Install_Soldier2Impl_RealizeRealizedSoldier_Hook();
            const bool playerOk = Install_Fova2ControllerImpl_ApplyPlayerFova_Hook();
            const bool othersOk = Install_AnimationControllerImpl_SetGameObjectIdToCharaControl_Hook();
            return soldierOk && playerOk && othersOk;
        }

        void Uninstall() override
        {
            Uninstall_AnimationControllerImpl_SetGameObjectIdToCharaControl_Hook();
            Uninstall_Fova2ControllerImpl_ApplyPlayerFova_Hook();
            Uninstall_Soldier2Impl_RealizeRealizedSoldier_Hook();
        }
    };

    class SoldierHairFovaModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "SoldierHairFova";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_SoldierHairFova_Hook();
        }

        void Uninstall() override
        {
            Uninstall_SoldierHairFova_Hook();
        }
    };

    class ChangeFovaOffScreenModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "ChangeFovaOffScreen";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_RealizedSoldier2Impl_ExecuteSerially_Hook();
        }

        void Uninstall() override
        {
            Uninstall_RealizedSoldier2Impl_ExecuteSerially_Hook();
        }
    };

    class PlayerVoiceFpkModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "PlayerVoiceFpk";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_PlayerVoiceFpk_Hook();
        }

        void Uninstall() override
        {
            Uninstall_PlayerVoiceFpk_Hook();
        }
    };

    class PlayerVoiceTypeModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "PlayerVoiceType";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_PlayerVoiceType_Hook();
        }

        void Uninstall() override
        {
            Uninstall_PlayerVoiceType_Hook();
        }
    };

    class HeadMotionModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "HeadMotion";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            if (!Install_Fova2ControllerImpl_PutAndActMotion_Hook())
                return false;
            Install_EffectInstance_Update_Hook();
            return true;
        }

        void Uninstall() override
        {
            Uninstall_EffectInstance_Update_Hook();
            Uninstall_Fova2ControllerImpl_PutAndActMotion_Hook();
        }
    };

    class EnterDownHoldupForceVoiceModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "EnterDownHoldupForceVoice";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_State_EnterDownHoldupForceVoice_Hook();
        }

        void Uninstall() override
        {
            Uninstall_State_EnterDownHoldupForceVoice_Hook();
        }
    };

    class VIPSleepFaintModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "VIPSleepFaint";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_VIPSleepFaint_Hook();
        }

        void Uninstall() override
        {
            Uninstall_VIPSleepFaint_Hook();
        }
    };

    class OfficerWakeModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "OfficerWake";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_NoticeActionImpl_Wake_Hook();
        }

        void Uninstall() override
        {
            Uninstall_NoticeActionImpl_Wake_Hook();
        }
    };

    class VIPHoldupModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "VIPHoldup";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_VIPHoldup_Hook();
        }

        void Uninstall() override
        {
            Uninstall_VIPHoldup_Hook();
        }
    };

    class VIPSoundRecoveryModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "VIPSoundRecovery";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_VIPSoundRecovery_Hook();
        }

        void Uninstall() override
        {
            Uninstall_VIPSoundRecovery_Hook();
        }
    };

    class VIPRadioModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "VIPRadio";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_VIPRadio_Hook();
        }

        void Uninstall() override
        {
            Uninstall_VIPRadio_Hook();
        }
    };

    class HoldUpReactionCowardlyReactionsModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "HoldUpReactionCowardlyReactions";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_HoldUpReactionCowardlyReactions_Hook();
        }

        void Uninstall() override
        {
            Uninstall_HoldUpReactionCowardlyReactions_Hook();
        }
    };

    class PerSoldierCallSignOverrideModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "PerSoldierCallSignOverride";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_SoldierCallSign_Hook();
        }

        void Uninstall() override
        {
            Uninstall_SoldierCallSign_Hook();
        }
    };

    class LostHostageModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "LostHostage";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_LostHostage_Hooks() && Install_LostHostageDiscovery_Hooks();
        }

        void Uninstall() override
        {
            Uninstall_LostHostage_Hooks();
            Uninstall_LostHostageDiscovery_Hooks();
        }
    };

    class UpdateOptCamoModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "UpdateOptCamo";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_UpdateOptCamo_Hook();
        }

        void Uninstall() override
        {
            Uninstall_UpdateOptCamo_Hook();
        }
    };

    class CassetteTapePlayHookModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "CassetteTapePlayHook";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_MbDvcCassetteTapeCallbackImpl_PlayOrPauseSelectedTrack_Hook();
        }

        void Uninstall() override
        {
            Uninstall_MbDvcCassetteTapeCallbackImpl_PlayOrPauseSelectedTrack_Hook();
        }
    };

    class CustomTapesModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "CustomTapes"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_SoundMusicPlayer_SetupMusicInfos_Hook(); }
        void Uninstall() override { Uninstall_SoundMusicPlayer_SetupMusicInfos_Hook(); }
    };

    class CustomTapeOwnershipModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "CustomTapeOwnership"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_CustomTapeOwnership_Hooks(); }
        void Uninstall() override { Uninstall_CustomTapeOwnership_Hooks(); }
    };

    class CassetteTrackPagingModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "CassetteTrackPaging"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_CassetteTrackPaging_Hooks(); }
        void Uninstall() override { Uninstall_CassetteTrackPaging_Hooks(); }
    };

    class CassetteTapeSetCurrentAlbumModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "CassetteTapeSetCurrentAlbum"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_CassetteTapeSetCurrentAlbum_Hook(); }
        void Uninstall() override { Uninstall_CassetteTapeSetCurrentAlbum_Hook(); }
    };

    class CustomRadioCassetteModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "CustomRadioCassette"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_CustomRadioCassette_Hooks(); }
        void Uninstall() override { Uninstall_CustomRadioCassette_Hooks(); }
    };

    class CustomTapeLongFilenameModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "CustomTapeLongFilename"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_Control_PostExternalEvent_Hook(); }
        void Uninstall() override { Uninstall_Control_PostExternalEvent_Hook(); }
    };

    class SoundSystemBeginModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "SoundSystemBegin";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_SoundSystem_BeginSoundSystem_Hook();
        }

        void Uninstall() override
        {
            Uninstall_SoundSystem_BeginSoundSystem_Hook();
        }
    };

    class CassetteWalkmanEventsModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "CassetteWalkmanEvents";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_CassetteWalkmanEvents_Hook();
        }

        void Uninstall() override
        {
            Uninstall_CassetteWalkmanEvents_Hook();
        }
    };

    class TppPickableModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "TppPickable";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_TppPickableHooks();
        }

        void Uninstall() override
        {
            Uninstall_TppPickableHooks();
        }
    };

    class TppCollectionModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "TppCollection";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_TppCollectionHooks();
        }

        void Uninstall() override
        {
            Uninstall_TppCollectionHooks();
        }
    };

    class DispatchMissionModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "DispatchMission";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_DeployMission_Hooks();
        }

        void Uninstall() override
        {
            Uninstall_DeployMission_Hooks();
        }
    };
    class EquipIconFtexPathModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "EquipIconFtexPath";
        }
        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_EquipIconFtexPath_Hook();
        }
        void Uninstall() override
        {
            Uninstall_EquipIconFtexPath_Hook();
        }
    };

    class MbDvcCustomPopupModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "MbDvcCustomPopup";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_MbDvcCustomPopup_Hook();
        }

        void Uninstall() override
        {
            Uninstall_MbDvcCustomPopup_Hook();
        }
    };

    class MissionDeployWarningModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "MissionDeployWarning";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_MissionDeployWarning_Hook();
        }

        void Uninstall() override
        {
            Uninstall_MissionDeployWarning_Hook();
        }
    };

    class MissionMenuHelpModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "MissionMenuHelp";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_MissionMenuHelp_Hook();
        }

        void Uninstall() override
        {
            Uninstall_MissionMenuHelp_Hook();
        }
    };

    class SoldierVoiceTypeQueryModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "SoldierVoiceTypeQuery";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_SoldierVoiceTypeQuery_Hook();
        }

        void Uninstall() override
        {
            Uninstall_SoldierVoiceTypeQuery_Hook();
        }
    };

    class VoicePitchOverrideModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "VoicePitchOverride";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_VoicePitchOverride_Hook();
        }

        void Uninstall() override
        {
            Uninstall_VoicePitchOverride_Hook();
        }
    };

    class AudioVolumeOptionsModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "AudioVolumeOptions";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            if (!Install_CAkParameterNodeBase_SetInitialRTPC_Hook())
                return false;
            if (Install_KeySettingEvCall_SetKeySettingMenu_Hooks())
                return true;
            Uninstall_CAkParameterNodeBase_SetInitialRTPC_Hook();
            return false;
        }

        void Uninstall() override
        {
            Uninstall_KeySettingEvCall_SetKeySettingMenu_Hooks();
            Uninstall_CAkParameterNodeBase_SetInitialRTPC_Hook();
        }
    };

    class SoldierAkObjIdMapModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "SoldierAkObjIdMap";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return ::SoldierAkObjIdMap::Install();
        }

        void Uninstall() override
        {
            ::SoldierAkObjIdMap::Uninstall();
        }
    };

    class TargetCqcStanceModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "TargetCqcStance";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_TargetCqcStance_Hook();
        }

        void Uninstall() override
        {
            Uninstall_TargetCqcStance_Hook();
        }
    };

    class QuietCqcPatchesModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "QuietCqcPatches";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_QuietCqcPatches();
        }

        void Uninstall() override
        {
            Uninstall_QuietCqcPatches();
        }
    };


    class UniqueCharacterSuitSlotModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "UniqueCharacterSuitSlot";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_UniqueCharacterSuitSlot();
        }

        void Uninstall() override
        {
            Uninstall_UniqueCharacterSuitSlot();
        }
    };

    class PlayerAttachInDemoModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "PlayerAttachInDemo";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_PlayerAttachInDemo_Hook();
        }

        void Uninstall() override
        {
            Uninstall_PlayerAttachInDemo_Hook();
        }
    };

    class VehicleDemoStopExemptModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "VehicleDemoStopExempt";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_VehicleDemoStopExempt();
        }

        void Uninstall() override
        {
            Uninstall_VehicleDemoStopExempt();
        }
    };

    class SoldierIgnorePlayerModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "SoldierIgnorePlayer";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_CheckSightNoticePlayer_Hook();
        }

        void Uninstall() override
        {
            Uninstall_CheckSightNoticePlayer_Hook();
        }
    };

    class SoldierIgnoreVehicleModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "SoldierIgnoreVehicle";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_SoldierVehicleAvoid_Hook();
        }

        void Uninstall() override
        {
            Uninstall_SoldierVehicleAvoid_Hook();
        }
    };

    class HeadMarkStaffNameModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "HeadMarkStaffName";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_Soldier2Impl_UpdateMarkers_Hook();
        }

        void Uninstall() override
        {
            Uninstall_Soldier2Impl_UpdateMarkers_Hook();
        }
    };

    bool s_SoldierEventHooksInstalled = false;

    class SoldierEventHooksModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "SoldierEventHooks";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            s_SoldierEventHooksInstalled = Install_DamageHandlerImpl_ProcessDamages_Hook()
                && Install_LifeControllerImpl_SendStatusMessage_Hook()
                && Install_NoticeControllerImpl_CheckNoiseNoticeSoldier_Hook();
            if (!s_SoldierEventHooksInstalled)
                Uninstall();
            return s_SoldierEventHooksInstalled;
        }

        void Uninstall() override
        {
            s_SoldierEventHooksInstalled = false;
            Uninstall_NoticeControllerImpl_CheckNoiseNoticeSoldier_Hook();
            Uninstall_LifeControllerImpl_SendStatusMessage_Hook();
            Uninstall_DamageHandlerImpl_ProcessDamages_Hook();
        }
    };

    class SoldierFactionModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "SoldierFaction";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            const bool installed = s_SoldierEventHooksInstalled   // its damage, life and noise callbacks ride on SoldierEventHooks
                && Install_CombatAntiBuddyWalkerGearAiImpl_UpdatePrimaryTarget_Hook()
                && Install_NoticeAntiBuddyWalkerGearAiImpl_ShouldSleep_Hook()
                && Install_AttackActionImpl_HasOtherSoldierOnVec_Hook()
                && Install_Bullet3Impl_OnOffenseHit_Hook()
                && Install_CombatAntiBuddyWalkerGearAiImpl_StepSurprise_Hook()
                && Install_Soldier2Util_SearchActiveMember_Hook()
                && Install_CombatAntiBuddyWalkerGearAiImpl_UpdateSnipeTarget_Hook()
                && Install_WalkerGearActionImpl_HasOtherSoldierOnVec_Hook()
                && Install_NoticeDownComradeAiImpl_ShouldSleep_Hook()
                && Install_VehicleNoticeDownComradeAiImpl_ShouldSleep_Hook()
                && Install_RideVehicleActionImpl_HasOtherSoldierOnVec_Hook()
                && Install_CombatAntiBuddyWalkerGearAiImpl_CheckGrenade_Hook();
            if (!installed)
            {
                Uninstall();
            }
            else
            {
                Install_GunActionImpl_Execute_Hook();          // optional: without it faction helis fire without a line of sight check
                Install_AiControllerImpl_WakeNode_Hooks();     // shared: finds the heli combat AI and the anti-air gunner nodes
                Install_ActionController_SetCommand_Hook();    // shared: points anti-air gunners at the faction heli
            }
            SoldierFaction_SetModuleActive(installed);
            return installed;
        }

        void Uninstall() override
        {
            SoldierFaction_SetModuleActive(false);
            Uninstall_NoticeTurretAiImpl_ShouldSleep_Hook();
            Uninstall_HeliInterest_SetLookAt_Hook();
            Uninstall_EnemyCombatAiImpl_Execute_Hook();
            Uninstall_CpControllerImpl_GetTargetPosition_Hook();
            Uninstall_GunActionImpl_Execute_Hook();
            Uninstall_CombatAntiBuddyWalkerGearAiImpl_CheckGrenade_Hook();
            Uninstall_RideVehicleActionImpl_HasOtherSoldierOnVec_Hook();
            Uninstall_VehicleNoticeDownComradeAiImpl_ShouldSleep_Hook();
            Uninstall_NoticeDownComradeAiImpl_ShouldSleep_Hook();
            Uninstall_WalkerGearActionImpl_HasOtherSoldierOnVec_Hook();
            Uninstall_CombatAntiBuddyWalkerGearAiImpl_UpdateSnipeTarget_Hook();
            Uninstall_Soldier2Util_SearchActiveMember_Hook();
            Uninstall_CombatAntiBuddyWalkerGearAiImpl_StepSurprise_Hook();
            Uninstall_Bullet3Impl_OnOffenseHit_Hook();
            Uninstall_AttackActionImpl_HasOtherSoldierOnVec_Hook();
            Uninstall_NoticeAntiBuddyWalkerGearAiImpl_ShouldSleep_Hook();
            Uninstall_CombatAntiBuddyWalkerGearAiImpl_UpdatePrimaryTarget_Hook();
        }
    };

    class InterrogationVoiceEventModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "InterrogationVoiceEvent";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_InterrogationVoiceEvent_Hook();
        }

        void Uninstall() override
        {
            Uninstall_InterrogationVoiceEvent_Hook();
        }
    };

    class SetEyeLampColorModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "SetEyeLampColor";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_SetEyeLampColor_Hook();
        }

        void Uninstall() override
        {
            Uninstall_SetEyeLampColor_Hook();
        }
    };
    class GetGameObjectIdWithIndex final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "GetGameObjectIdWithIndex";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_GetGameObjectIdWithIndex();
        }

        void Uninstall() override
        {
            Uninstall_GetGameObjectIdWithIndex();
        }
    };

    class EnemyLangIdOverrideModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "EnemyLangIdOverride";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_EnemyLangIdOverride_Hooks();
        }

        void Uninstall() override
        {
            Uninstall_EnemyLangIdOverride_Hooks();
        }
    };

    class FobPlayerCharactersModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "FobPlayerCharacters";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_FobPlayerCharacters_Patches();
        }

        void Uninstall() override
        {
            Uninstall_FobPlayerCharacters_Patches();
        }
    };

    class CrawlSideRollModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "CrawlSideRoll";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_BasicActionImpl_StateCrawlSideRoll_Hook();
        }

        void Uninstall() override
        {
            Uninstall_BasicActionImpl_StateCrawlSideRoll_Hook();
        }
    };

    class WormholeNearDeathWarpModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "WormholeNearDeathWarp";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_PlacedSystemImpl_Insert_Hook();
        }

        void Uninstall() override
        {
            Uninstall_PlacedSystemImpl_Insert_Hook();
        }
    };

    class PlayerLockPickModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "PlayerLockPick";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_SearchLightActionPluginImpl_StateDoor_Hook();
        }

        void Uninstall() override
        {
            Uninstall_SearchLightActionPluginImpl_StateDoor_Hook();
        }
    };

    class SubtitlesEventMessageModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "SubtitlesEventMessage"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_SubtitlesEventMessage_Hook(); }
        void Uninstall() override { Uninstall_SubtitlesEventMessage_Hook(); }
    };

    class PhaseSneakAiImpl_PreUpdateModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "PhaseSneakAiImpl_PreUpdate"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_PhaseSneakAiImpl_PreUpdate_Hook(); }
        void Uninstall() override { Uninstall_PhaseSneakAiImpl_PreUpdate_Hook(); }
    };

    class MissionEmergencyModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "MissionEmergency"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_MissionEmergency_Hook(); }
        void Uninstall() override { Uninstall_MissionEmergency_Hook(); }
    };

    class ShowMissionIconModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "ShowMissionIcon"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_ShowMissionIcon_Hook(); }
        void Uninstall() override { Uninstall_ShowMissionIcon_Hook(); }
    };

    class GameObjectSendCommandModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "GameObjectSendCommand"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_GameObjectSendCommand_Hook(); }
        void Uninstall() override { Uninstall_GameObjectSendCommand_Hook(); }
    };

    class ReinitializeModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "Reinitialize"; }
        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            bool ok = Install_SecurityCamera2Impl_ProcessLuaCommand_Hook();
            ok &= Install_UavImpl_ProcessLuaCommand_Hook();
            ok &= Install_TppVehicle2_LuaCommands_Hook();
            ok &= Install_Soldier2Impl_ProcessLuaCommand_Hook();
            ok &= Install_Hostage2Impl_ProcessLuaCommand_Hook();
            ok &= Install_WalkerGear2Impl_ProcessLuaCommand_Hook();
            ok &= Install_TppSahelan2_LuaCommands_Hook();
            return ok;
        }
        void Uninstall() override
        {
            Uninstall_TppSahelan2_LuaCommands_Hook();
            Uninstall_WalkerGear2Impl_ProcessLuaCommand_Hook();
            Uninstall_Hostage2Impl_ProcessLuaCommand_Hook();
            Uninstall_Soldier2Impl_ProcessLuaCommand_Hook();
            Uninstall_TppVehicle2_LuaCommands_Hook();
            Uninstall_UavImpl_ProcessLuaCommand_Hook();
            Uninstall_SecurityCamera2Impl_ProcessLuaCommand_Hook();
        }
    };

    class InitEquipHudDataModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "InitEquipHudData"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_InitEquipHudData(); }
        void Uninstall() override { Uninstall_InitEquipHudData(); }
    };

    class OccasionalChatListModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "OccasionalChatList"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_OccasionalChatList_Hook(); }
        void Uninstall() override { Uninstall_OccasionalChatList_Hook(); }
    };

    class SoldierConversationsModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "SoldierConversations"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_SoldierConversations_Hook(); }
        void Uninstall() override { Uninstall_SoldierConversations_Hook(); }
    };

    class MotherBaseSalutesModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "MotherBaseSalutes"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_MotherBaseSalutes_Hook(); }
        void Uninstall() override { Uninstall_MotherBaseSalutes_Hook(); }
    };

    class HeadOptionMissionAbilitiesModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "HeadOptionMissionAbilities"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_TppVarInit_InitializeForNewMission_Hook(); }
        void Uninstall() override { Uninstall_TppVarInit_InitializeForNewMission_Hook(); }
    };

    class SoldierNoticeModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "SoldierNotice"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_SoldierNotice_Hooks(); }
        void Uninstall() override { Uninstall_SoldierNotice_Hooks(); }
    };

    class IdleActingVoiceModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "IdleActingVoice"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_IdleActingActionImpl_CallVoice_Hook(); }
        void Uninstall() override { Uninstall_IdleActingActionImpl_CallVoice_Hook(); }
    };

    class SoldierVoiceSwapModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "SoldierVoiceSwap"; }
        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            bool ok = Install_Soldier2SoundControllerImpl_CallVoiceWithoutSync_Hook();
            ok &= Install_Soldier2SoundControllerImpl_CallVoiceImpl_Hook();
            ok &= Install_HostageRideVehicleActionImpl_State_GetInAction_Hooks();
            ok &= Install_DownActionImpl_State_ToDyingSufferIdle_Hooks();
            ok &= Install_SoldierDamageActionImpl_State_DamageReaction_Hook();
            return ok;
        }
        void Uninstall() override
        {
            Uninstall_SoldierDamageActionImpl_State_DamageReaction_Hook();
            Uninstall_DownActionImpl_State_ToDyingSufferIdle_Hooks();
            Uninstall_HostageRideVehicleActionImpl_State_GetInAction_Hooks();
            Uninstall_Soldier2SoundControllerImpl_CallVoiceImpl_Hook();
            Uninstall_Soldier2SoundControllerImpl_CallVoiceWithoutSync_Hook();
        }
    };

    class SoldierCombatVoiceModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "SoldierCombatVoice"; }
        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            bool ok = Install_Soldier2SoundControllerImpl_CallVoiceWithSync_Hook();
            ok &= Install_MemberRoleControllerImpl_AssignSelectAlertLost_Hook();   // the lead searcher's EVE020
            return ok;
        }
        void Uninstall() override
        {
            Uninstall_MemberRoleControllerImpl_AssignSelectAlertLost_Hook();
            Uninstall_Soldier2SoundControllerImpl_CallVoiceWithSync_Hook();
        }
    };

    class ChaffRadioVoiceModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "ChaffRadioVoice"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_RadioActionImpl_State_RadioChaffEnd_Hook(); }
        void Uninstall() override { Uninstall_RadioActionImpl_State_RadioChaffEnd_Hook(); }
    };

    class SquatIdleRouteModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "SquatIdleRoute"; }
        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            Install_AttackActionImpl_SubState_Idle_Hook(); // the relaxed arm pose on layer 1 would hold the squat clip's left arm
            Install_BasicAction2Impl_State_ChangeStandOrSquat_Hook(); // and while the soldier squats down or stands up
            Install_RouteAiImpl_StepEvent_Hook(); // binocular acts are sent from the route step
            return Install_RouteAiImpl_SetSubActionParamWithNodeEvent_Hook();
        }
        void Uninstall() override
        {
            Uninstall_AttackActionImpl_SubState_Idle_Hook();
            Uninstall_BasicAction2Impl_State_ChangeStandOrSquat_Hook();
            Uninstall_RouteAiImpl_SetSubActionParamWithNodeEvent_Hook();
        }
    };

    class HoldupSelfReleaseModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "HoldupSelfRelease"; }
        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            if (!Install_HoldupActionImpl_State_EndStandHoldup_Hooks())
                return false;
            return Install_HoldupAiImpl_StepHoldupStand_Hooks();
        }
        void Uninstall() override
        {
            Uninstall_HoldupAiImpl_StepHoldupStand_Hooks();
            Uninstall_HoldupActionImpl_State_EndStandHoldup_Hooks();
        }
    };

    class InvestigationSquadsModule final : public IFeatureModule   // RequestInvestigation's memberCount past four
    {
    public:
        const char* GetName() const override { return "InvestigationSquads"; }
        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_InvestigateSituationOperatorImpl_GetSituationWithRequest_Hook();
        }
        void Uninstall() override
        {
            Uninstall_InvestigateSituationOperatorImpl_GetSituationWithRequest_Hook();
        }
    };

    class AntiAirGunRouteModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "AntiAirGunRoute"; }
        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            Install_AttackActionImpl_ProcessFireControl_Hook(); // only param0 (fire rate) needs it
            Install_TppPermanentGimmickGatlingGun_SetAimMatrix_Hook(); // only the gun's turn speed (param3) needs it
            return Install_RouteAiImpl_StepEvent_Hook();
        }
        void Uninstall() override
        {
            Uninstall_AttackActionImpl_ProcessFireControl_Hook();
            Uninstall_TppPermanentGimmickGatlingGun_SetAimMatrix_Hook();
            Uninstall_RouteAiImpl_StepEvent_Hook();
        }
    };

    class JeepGunnerModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "JeepGunner"; }
        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            if (!Install_RideVehicleActionImpl_State_IdleAction_Hook())
                return false;
            Install_RideVehicleActionImpl_UpdateShootState_Hook();
            const bool seatMatrix = Install_VehicleCommunicationInfo_FetchVehicleSeatLocalMatrix_Hook();
            Install_AttackActionImpl_GetAimState_Hook();
            Install_InVehicleAiImpl_ShouldWake_Hook();
            if (Install_EffectControllerImpl_CreateEffect_Hook())
                JeepGunEffects_Install();
            const bool seatMotion = JeepGunnerSeatMotion_Install();
            JeepGunnerSeat_Install(seatMatrix && seatMotion);
            return true;
        }
        void Uninstall() override
        {
            JeepGunnerSeat_Uninstall();
            JeepGunEffects_Uninstall();
            JeepGunnerSeatMotion_Uninstall();
            Uninstall_EffectControllerImpl_CreateEffect_Hook();
            Uninstall_InVehicleAiImpl_ShouldWake_Hook();
            Uninstall_AttackActionImpl_GetAimState_Hook();
            Uninstall_VehicleCommunicationInfo_FetchVehicleSeatLocalMatrix_Hook();
            Uninstall_RideVehicleActionImpl_UpdateShootState_Hook();
            Uninstall_RideVehicleActionImpl_State_IdleAction_Hook();
        }
    };

    class CustomVehiclesModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "CustomVehicles"; }
        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            const bool installed = Install_BodyData_SetupBody_Hook()
                && Install_AttachmentData_SetupAttachment_Hook()
                && Install_ConstantImpl_GetVehicleTypeSpecs_Hook()
                && Install_VehicleGameObjectImpl_Initialize_Hook();
            CustomVehicles::SetHooksInstalled(installed);
            if (installed)
                VehicleSortieRows::Install();
            return installed;
        }
        void Uninstall() override
        {
            VehicleSortieRows::Uninstall();
            CustomVehicles::SetHooksInstalled(false);
            Uninstall_VehicleGameObjectImpl_Initialize_Hook();
            Uninstall_ConstantImpl_GetVehicleTypeSpecs_Hook();
            Uninstall_AttachmentData_SetupAttachment_Hook();
            Uninstall_BodyData_SetupBody_Hook();
        }
    };

    class RouteCharacterTargetModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "RouteCharacterTarget"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_RouteGroupImpl_GetEventAimCharacterId_Hook(); }
        void Uninstall() override { Uninstall_RouteGroupImpl_GetEventAimCharacterId_Hook(); }
    };

    class RouteWalkEdgesModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "RouteWalkEdges"; }
        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            Install_RouteGroupImpl_GetEventId_Hook(); // GZ edge events read as their TPP twins
            return Install_RouteAiImpl_SetSubActionParamWithEdgeEvent_Hook();
        }
        void Uninstall() override
        {
            Uninstall_RouteGroupImpl_GetEventId_Hook();
            Uninstall_RouteAiImpl_SetSubActionParamWithEdgeEvent_Hook();
        }
    };

    class CpRadioVariantsModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "CpRadioVariants"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_CpRadioVariants_Hooks(); }
        void Uninstall() override { Uninstall_CpRadioVariants_Hooks(); }
    };

    class AnimalNoticeModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "AnimalNotice"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_AnimalNotice_Hooks(); }
        void Uninstall() override { Uninstall_AnimalNotice_Hooks(); }
    };

    class BuddyOutfitModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "BuddyOutfit"; }
        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            bool ok = Install_EquipDevelopControllerImpl_GetBuddyPartsDevelopInfoIndex_Hooks();
            ok &= Install_Buddy2BlockControllerImpl_GetPathAtBuddyType_Hook();
            ok &= Install_Buddy2BlockControllerImpl_GetBuddyQuietWeaponFileFromBuddyBlock_Hook();
            ok &= Install_Buddy2BlockControllerImpl_GetBuddyQuietFovaFile_Hooks();
            ok &= Install_BuddyServiceImpl_GetQuietEquipIdByWeaponType_Hooks();
            ok &= Install_BuddyServiceImpl_ChangeQuietCostume_Patch();
            ok &= Install_BuddyServiceImpl_ChangeQuietWeaponType_Patch();
            ok &= Install_BuddyServiceImpl_ChangeHorseSettings_Patch();
            ok &= Install_BuddyServiceImpl_ChangeDogSettings_Patch();
            ok &= Install_BuddyQuiet2Impl_SetBruise_Patch();
            ok &= Install_DamageAiImpl_StepBitDamage_Patch();
            ok &= Install_ItemSelectorCallbackImpl_SetupPrefabListElement_BuddyPatches();
            ok &= Install_CombatAiImpl_SendFireSignal_Hook();
            ok &= Install_EquipControllerImpl_InitCurrentSlot_Hooks();
            ok &= Install_ActionCoreImpl_ExecDefenseCallback_Patches();
            ok &= Install_BasicAction2Impl_Execute_Hook();
            ok &= Install_BuddyDogDamageHandlerImpl_ProcessDamages_Patches();
            ok &= Install_BuddyServiceImpl_InitBuddyCommandForStatus_Patches();
            ok &= Install_BuddyDog2Impl_Initialize_Patch();
            ok &= Install_EquipDevelopControllerImpl_GetDWalkerParts_Hooks();
            ok &= Install_WalkerGear_SubWeaponEquipId_Patch();
            ok &= Install_BuddyServiceImpl_ChangeGearSubWeaponSettings_Patch();
            ok &= Install_EquipBlockController2_StartLoadingToEquipMissionBlock_Hook();
            ok &= Install_EquipDataSystemAccessorImpl_GetBlock_Hook();
            ok &= Install_RideWalkerGearActionImpl_GetVehicleEquipId_Patches();
            ok &= Install_UiUtility_GetVehicleEquipNameText_Hooks();
            return ok;
        }
        void Uninstall() override
        {
            Uninstall_UiUtility_GetVehicleEquipNameText_Hooks();
            Uninstall_RideWalkerGearActionImpl_GetVehicleEquipId_Patches();
            Uninstall_EquipDataSystemAccessorImpl_GetBlock_Hook();
            Uninstall_EquipBlockController2_StartLoadingToEquipMissionBlock_Hook();
            Uninstall_BuddyServiceImpl_ChangeGearSubWeaponSettings_Patch();
            Uninstall_WalkerGear_SubWeaponEquipId_Patch();
            Uninstall_EquipDevelopControllerImpl_GetDWalkerParts_Hooks();
            Uninstall_BuddyDog2Impl_Initialize_Patch();
            Uninstall_BuddyServiceImpl_InitBuddyCommandForStatus_Patches();
            Uninstall_BuddyDogDamageHandlerImpl_ProcessDamages_Patches();
            Uninstall_BasicAction2Impl_Execute_Hook();
            Uninstall_ActionCoreImpl_ExecDefenseCallback_Patches();
            Uninstall_EquipControllerImpl_InitCurrentSlot_Hooks();
            Uninstall_CombatAiImpl_SendFireSignal_Hook();
            Uninstall_ItemSelectorCallbackImpl_SetupPrefabListElement_BuddyPatches();
            Uninstall_DamageAiImpl_StepBitDamage_Patch();
            Uninstall_BuddyQuiet2Impl_SetBruise_Patch();
            Uninstall_BuddyServiceImpl_ChangeDogSettings_Patch();
            Uninstall_BuddyServiceImpl_ChangeHorseSettings_Patch();
            Uninstall_BuddyServiceImpl_ChangeQuietWeaponType_Patch();
            Uninstall_BuddyServiceImpl_ChangeQuietCostume_Patch();
            Uninstall_BuddyServiceImpl_GetQuietEquipIdByWeaponType_Hooks();
            Uninstall_Buddy2BlockControllerImpl_GetBuddyQuietFovaFile_Hooks();
            Uninstall_Buddy2BlockControllerImpl_GetBuddyQuietWeaponFileFromBuddyBlock_Hook();
            Uninstall_Buddy2BlockControllerImpl_GetPathAtBuddyType_Hook();
            Uninstall_EquipDevelopControllerImpl_GetBuddyPartsDevelopInfoIndex_Hooks();
        }
    };

    class BattleGearModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "BattleGear"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return battleGear::Install(); }
        void Uninstall() override { battleGear::Uninstall(); }
    };

    class FieldTaxiMenuModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "FieldTaxiMenu"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_FieldTaxiMenu(); }
        void Uninstall() override { Uninstall_FieldTaxiMenu(); }
    };

    class TimeCigaretteUiModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "TimeCigaretteUi"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_TimeCigaretteUi_Hook(); }
        void Uninstall() override { Uninstall_TimeCigaretteUi_Hook(); }
    };

    class HeadMarkDyingColourModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "HeadMarkDyingColour";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_HeadMarkMarkerEvCall_SetIconSubType_Patch();
        }

        void Uninstall() override
        {
            Uninstall_HeadMarkMarkerEvCall_SetIconSubType_Patch();
        }
    };

    class DataBaseNewFlagModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "DataBaseNewFlag";
        }

        bool Install(HMODULE hGame) override
        {
            return dataBaseNewFlag::Install(hGame);
        }

        void Uninstall() override
        {
        }
    };

    class MbTopMenuStickyHideModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "MbTopMenuStickyHide";
        }

        bool Install(HMODULE hGame) override
        {
            return mbTopMenuStickyHide::Install(hGame);
        }

        void Uninstall() override
        {
        }
    };

    class IDroidCustomItemsModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "IDroidCustomItems";
        }

        bool Install(HMODULE hGame) override
        {
            const bool table = mbTopMenuCustomItems::Install(hGame);
            const bool page = mbDvcCustomPage::Install(hGame);
            return table && page;
        }

        void Uninstall() override
        {
            mbDvcCustomPage::Uninstall();
            mbTopMenuCustomItems::Uninstall();
        }
    };

    class PrefabListControlModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "PrefabListControl";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_PrefabListControl();
        }

        void Uninstall() override
        {
            Uninstall_PrefabListControl();
        }
    };

    class DataBaseBluePrintListModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "DataBaseBluePrintList";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_DataBaseBluePrintList_Hook();
        }

        void Uninstall() override
        {
            Uninstall_DataBaseBluePrintList_Hook();
        }
    };

    class HideBinocleModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "HideBinocle"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_HideBinocle_Hook(); }
        void Uninstall() override { Uninstall_HideBinocle_Hook(); }
    };

    class HeliSoundControllerModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "HeliSoundController"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_HeliSoundController_Hook(); }
        void Uninstall() override { Uninstall_HeliSoundController_Hook(); }
    };

    class LandingZoneDangerModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "LandingZoneDanger"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_MotherBaseMapCommonData_IsDangerLandingZone(); }
        void Uninstall() override { Uninstall_MotherBaseMapCommonData_IsDangerLandingZone(); }
    };

    class HeliSearchLightModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "HeliSearchLight"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_LightActionImpl_Execute_Hook(); }
        void Uninstall() override { Uninstall_LightActionImpl_Execute_Hook(); }
    };

    class HeliDamageEffectsModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "HeliDamageEffects"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_ActionCoreImpl_Execute_Hook(); }
        void Uninstall() override { Uninstall_ActionCoreImpl_Execute_Hook(); }
    };

    class HeliLifeRateModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "HeliLifeRate"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_DamageHandlerImpl_ProcessQueuedDamages_Hook(); }
        void Uninstall() override { Uninstall_DamageHandlerImpl_ProcessQueuedDamages_Hook(); }
    };

    class HeliDoorModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "HeliDoor"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_MechaActionImpl_StateOnPassenger_Hook(); }
        void Uninstall() override { Uninstall_MechaActionImpl_StateOnPassenger_Hook(); }
    };

    class HeliPlayerMachineGunModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "HeliPlayerMachineGun"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_RideHeliActionPluginImpl_ExecActionButtonFunction_Hook(); }
        void Uninstall() override { Uninstall_RideHeliActionPluginImpl_ExecActionButtonFunction_Hook(); }
    };

    class VehicleCannotRideModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "VehicleCannotRide"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_RideVehicleActionImpl_CheckVehicleIconArea_Hook(); }
        void Uninstall() override { Uninstall_RideVehicleActionImpl_CheckVehicleIconArea_Hook(); }
    };

    class SoldierRideHeliModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "SoldierRideHeli"; }
        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            bool ok = Install_RideHeliActionImpl_State_GetInAction_Hook();
            ok = Install_RideHeliActionImpl_State_IdleAction_Hook() && ok;
            ok = Install_CcRootAdjustAction_Exec_Hook() && ok;
            ok = Install_UnrealRideHeliActionImpl_State_IdleAction_Hook() && ok;
            ok = Install_RideHeliActionImpl_State_GetOutAction_Hook() && ok;
            ok = Install_ActionController_SetCommand_Hook() && ok;   // shared with CallComradeCommand, a hit on a rider ends his ride here
            ok = Install_DamageHandlerImpl_ProcessQueuedDamages_Hook() && ok;   // shared with HeliLifeRate, tells riders their heli is destroyed
            ok = Install_SoldierAttackActionImpl_Execute_Hook() && ok;   // RequestToFireOnHelicopter, riders' bursts
            ok = Install_SoldierAttackActionImpl_RequestFire_Hook() && ok;   // riders' rockets fly through their own heli
            ok = Install_SoldierDamageActionImpl_Execute_Hook() && ok;   // a rider who fell out lands
            return ok;
        }
        void Uninstall() override
        {
            Uninstall_SoldierDamageActionImpl_Execute_Hook();
            Uninstall_SoldierAttackActionImpl_RequestFire_Hook();
            Uninstall_SoldierAttackActionImpl_Execute_Hook();
            Uninstall_RideHeliActionImpl_State_GetOutAction_Hook();
            Uninstall_UnrealRideHeliActionImpl_State_IdleAction_Hook();
            Uninstall_CcRootAdjustAction_Exec_Hook();
            Uninstall_RideHeliActionImpl_State_IdleAction_Hook();
            Uninstall_RideHeliActionImpl_State_GetInAction_Hook();
        }
    };

    class AnnounceLogModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "AnnounceLog"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_AnnounceLogHook(); }
        void Uninstall() override { Uninstall_AnnounceLogHook(); }
    };

    class UavRouteHandlesModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "UavRouteHandles"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_RouteGroupImpl_Destructor_Hooks(); }
        void Uninstall() override { Uninstall_RouteGroupImpl_Destructor_Hooks(); }
    };

    class IdroidLinesModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "IdroidLines"; }
        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            const bool voices = Install_MbDvcSoundControllerImpl_Update_Hooks();
            const bool platforms = Install_BaseControllerImpl_CompletePlatformDeck_Hook();
            const bool messages = Install_SimpleMissionBlockController_ReceiveMessage_Hook();
            const bool pickupMap = voices && Install_MbDvcMapCallbackControlImpl_RendezvousOperation_Hook();
            return voices || platforms || messages || pickupMap;
        }
        void Uninstall() override
        {
            Uninstall_MbDvcMapCallbackControlImpl_RendezvousOperation_Hook();
            Uninstall_SimpleMissionBlockController_ReceiveMessage_Hook();
            Uninstall_BaseControllerImpl_CompletePlatformDeck_Hook();
            Uninstall_MbDvcSoundControllerImpl_Update_Hooks();
        }
    };

    class TornadoDualPatchModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "TornadoDualPatch"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_TornadoDualPatch(); }
        void Uninstall() override { Uninstall_TornadoDualPatch(); }
    };

    class IsWeaponNoUseInPlaceActionModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "IsWeaponNoUseInPlaceAction"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_IsWeaponNoUseInPlaceActionPatch(); }
        void Uninstall() override { Uninstall_IsWeaponNoUseInPlaceActionPatch(); }
    };

    class ChargeShotModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "ChargeShot"; }
        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            const bool charge = Install_AttackActionImpl_UpdateShootingByPad_Hook();
            if (charge)
            {
                Install_TppUIWeaponNumImpl_SetPanelInfo_Hook(); // the ring is optional, the charge works without it
                Install_SimplePartsControllerImpl_Create_Hook(); // so are the extra effect slots of a gun's .parts
            }
            return charge;
        }

        void Uninstall() override
        {
            Uninstall_SimplePartsControllerImpl_Create_Hook();
            Uninstall_TppUIWeaponNumImpl_SetPanelInfo_Hook();
            Uninstall_AttackActionImpl_UpdateShootingByPad_Hook();
        }
    };

    class CallComradeCommandModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "CallComradeCommand"; }
        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            const bool menuReady    = Install_CallMenuCommandSet_UpdateState_Hook();
            const bool soldierReady = Install_Soldier2Impl_ProcessSignal_Hook();
            const bool noticeReady  = Install_NoticeControllerImpl_CheckForceNotice_Hook();
            const bool pagingReady  = Install_CallMenuManagerImpl_UpdateInShowing_Hook();
            const bool turnReady    = Install_ActionController_SetCommand_Hook();
            const bool waveReady    = Install_NoticeNoiseAiImpl_StepMovePause_Hook();
            const bool guideReady   = Install_CallMenuImpl_UpdateOption_Hook();
            const bool panelReady   = Install_CallMenuImpl_UpdateSelectDoc_Hook();
            const bool numberReady  = Install_CallMenuImpl_UpdateCallMenu_Hook();
            const bool openReady    = Install_CallMenuManagerImpl_ShowCallMenu_Hook();
            const bool radioReady   = Install_RadioControllerImpl_StepRadioWaitResponse_Hooks();
            const bool spotReady    = Install_NoticeNoiseAiImpl_CheckTargetPositionUpdate_Hook();
            const bool arriveReady  = Install_NoticeNoiseAiImpl_StepWaitReach_Hook();
            const bool calmReady    = Install_NoticeNoiseAiImpl_StepPreAware_Hook();
            const bool backupReady  = Install_SoldierBackupCall_Hooks();
            return menuReady && soldierReady && noticeReady && pagingReady && turnReady && waveReady && guideReady && panelReady && openReady
                && radioReady && numberReady && spotReady && arriveReady && calmReady && backupReady;
        }
        void Uninstall() override
        {
            Uninstall_SoldierBackupCall_Hooks();
            Uninstall_NoticeNoiseAiImpl_StepPreAware_Hook();
            Uninstall_NoticeNoiseAiImpl_StepWaitReach_Hook();
            Uninstall_NoticeNoiseAiImpl_CheckTargetPositionUpdate_Hook();
            Uninstall_RadioControllerImpl_StepRadioWaitResponse_Hooks();
            Uninstall_CallMenuManagerImpl_ShowCallMenu_Hook();
            Uninstall_CallMenuImpl_UpdateCallMenu_Hook();
            Uninstall_CallMenuImpl_UpdateSelectDoc_Hook();
            Uninstall_CallMenuImpl_UpdateOption_Hook();
            Uninstall_NoticeNoiseAiImpl_StepMovePause_Hook();
            Uninstall_ActionController_SetCommand_Hook();
            Uninstall_CallMenuManagerImpl_UpdateInShowing_Hook();
            Uninstall_NoticeControllerImpl_CheckForceNotice_Hook();
            Uninstall_Soldier2Impl_ProcessSignal_Hook();
            Uninstall_CallMenuCommandSet_UpdateState_Hook();
        }
    };

    class BarrierEffectLoadModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "BarrierEffectLoad"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_BarrierEffectLoadPatch(); }
        void Uninstall() override { Uninstall_BarrierEffectLoadPatch(); }
    };

    class BarrierEffectSpawnModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "BarrierEffectSpawn"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_BarrierEffectSpawn(); }
        void Uninstall() override { Uninstall_BarrierEffectSpawn(); }
    };

    class LuaErrorThrowModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "LuaErrorReport"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_LuaErrorThrow(); }
        void Uninstall() override { Uninstall_LuaErrorThrow(); }
    };

    class CheckpointReloadModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "CheckpointReload"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_Block_Reload_Hook(); }
        void Uninstall() override { Uninstall_Block_Reload_Hook(); }
    };

    class SupportAttackCrashGuardModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "SupportAttackCrashGuard"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_SupportAttackCrashGuard(); }
        void Uninstall() override { Uninstall_SupportAttackCrashGuard(); }
    };

    class ExitTeardownGuardModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "ExitTeardownGuard"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_ExitTeardownGuard(); }
        void Uninstall() override { Uninstall_ExitTeardownGuard(); }
    };

    class StaffFaceContainmentModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "StaffFacePersistence"; }
        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            if (!outfit::Install_StaffFaceContainment())
                return false;
            Install_StaffFaceLuaHook();
            return true;
        }
        void Uninstall() override
        {
            outfit::Uninstall_StaffFaceContainment();
            Uninstall_StaffFaceLuaHook();
        }
    };

    class ServerManagerBufferReleaseModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "ServerManagerBufferRelease"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_ServerManagerBufferRelease(); }
        void Uninstall() override { Uninstall_ServerManagerBufferRelease(); }
    };

    class VertexStreamPoolModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "VertexStreamPool"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_VertexStreamPool(); }
        void Uninstall() override { Uninstall_VertexStreamPool(); }
    };

    class RendererPoolsModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "RendererPools"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_RendererPools(); }
        void Uninstall() override { Uninstall_RendererPools(); }
    };

    class TextureStoragePoolModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "TextureStoragePool"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_TextureStoragePool(); }
        void Uninstall() override { Uninstall_TextureStoragePool(); }
    };

    class TexturePoolsModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "TexturePools"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_TexturePools(); }
        void Uninstall() override { Uninstall_TexturePools(); }
    };

    class TexturePackAbortModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "TexturePackAbort"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_TexturePackAbort(); }
        void Uninstall() override { Uninstall_TexturePackAbort(); }
    };

    class TextureProxyFallbackModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "TextureProxyFallback"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_TextureProxyFallback(); }
        void Uninstall() override { Uninstall_TextureProxyFallback(); }
    };

    class NoticeSensorModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "NoticeSensor"; }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            if (!Install_NoticeSensorImpl_Init_Hook())
                return false;
            Install_NoticeSensorImpl_CreateController_Hook();
            return true;
        }

        void Uninstall() override
        {
            Uninstall_NoticeSensorImpl_CreateController_Hook();
            Uninstall_NoticeSensorImpl_Init_Hook();
        }
    };

    class CommandPostMembersModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "CommandPostMembers"; }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            Install_CommandPost2Impl_GetFreeMemberSlots_Hook();
            if (!Install_CommandPost2Impl_Initialize_Hook())
            {
                Uninstall_CommandPost2Impl_GetFreeMemberSlots_Hook();
                return false;
            }
            Install_CommandPost2Impl_CreateController_Hook();
            return true;
        }

        void Uninstall() override
        {
            Uninstall_CommandPost2Impl_CreateController_Hook();
            Uninstall_CommandPost2Impl_Initialize_Hook();
            Uninstall_CommandPost2Impl_GetFreeMemberSlots_Hook();
        }
    };

    class ReticleLayoutResetModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "ReticleLayoutReset"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_Reticle_ResetLayout_Hook(); }
        void Uninstall() override { Uninstall_Reticle_ResetLayout_Hook(); }
    };

    class EnhanceLangIdUnlimitedModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "EnhanceLangIdUnlimited"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_EnhanceLangIdUnlimited(); }
        void Uninstall() override { Uninstall_EnhanceLangIdUnlimited(); }
    };

    class DevelopArrayGrowModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "DevelopArrayGrow"; }
        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            equip::Install_DevelopArrayGrow();
            equip::Install_UiEquipPreviewControllerScroll();
            equip::Install_EquipDevelopControllerImpl_IsDisplayableBluePrintIDContion();
            return true;
        }
        void Uninstall() override
        {
            equip::Uninstall_EquipDevelopControllerImpl_IsDisplayableBluePrintIDContion();
            equip::Uninstall_UiEquipPreviewControllerScroll();
            equip::Uninstall_DevelopArrayGrow();
        }
    };

    class PlayerOutfitCoreModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "PlayerOutfitCore"; }
        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            const bool a = outfit::Install_OutfitEquippedState_Hooks();
            const bool b = EquipDevelopAdd::Install_TppMotherBaseManagement_EquipDevelopHooks();

            equip::Install_MenuDevelopGridExpand();
            equip::Install_CustomizeSelectorCallbackImpl_SetupItemList_Hook();
            if (!equip::DevelopArrayGrowPending())   // the grow retry thread installs them itself when it finishes
                EquipDevelop_InstallDevelopSyncHooks();

            const bool runtime = outfit::Install_OutfitRuntimeParts_Hooks();
            (void)runtime;
            return a && b;
        }
        void Uninstall() override
        {
            equip::Uninstall_CustomizeSelectorCallbackImpl_SetupItemList_Hook();
            outfit::Uninstall_OutfitRuntimeParts_Hooks();
            EquipDevelop_ReleaseDevelopTimerHook();
            EquipDevelop_ReleaseSetEquipNewGuard();
            EquipDevelopAdd::Uninstall_TppMotherBaseManagement_EquipDevelopHooks();
            outfit::Uninstall_OutfitEquippedState_Hooks();
        }
    };

    class PlayerOutfitEquipModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "PlayerOutfitEquip"; }
        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            const bool apply  = outfit::Install_OutfitSuitConditionApply_Hook();
            const bool select = outfit::Install_OutfitItemSelector_Hook();
            const bool list   = outfit::Install_OutfitListInject_Hook();
            const bool roots  = outfit::Install_DevelopWeaponList_Hook();
            const bool colors = outfit::Install_OutfitPalette();
            (void)apply; (void)select; (void)list; (void)roots; (void)colors;
            return true;
        }
        void Uninstall() override
        {
            outfit::Uninstall_OutfitPalette();
            outfit::Uninstall_DevelopWeaponList_Hook();
            outfit::Uninstall_OutfitListInject_Hook();
            outfit::Uninstall_OutfitItemSelector_Hook();
            outfit::Uninstall_OutfitSuitConditionApply_Hook();
        }
    };

    class PlayerOutfitExtrasModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "PlayerOutfitExtras"; }
        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            const bool head  = outfit::Install_OutfitHeadOption_Hook();
            const bool camo  = outfit::Install_OutfitCamoBonus_Hook();
            const bool value = outfit::Install_OutfitGetCamoufValue_Hook();
            const bool mtar  = outfit::Install_OutfitMotionMtar_Hook();
            const bool abil  = outfit::Install_OutfitAbilities_Hooks();
            const bool rsprt = outfit::Install_PlayerInfoGetPartsType_Hooks();
            const bool spark = outfit::Install_GetVfxFileFromParts_Hook();
            const bool sparm = outfit::Install_SuitParamUniqueCharacter_Hook();
            const bool cname = outfit::Install_UniqueCharacterName_Hook();
            const bool olbl = outfit::Install_OwnSuitRowLabel_Hooks();
            (void)head; (void)camo; (void)value; (void)mtar; (void)abil;
            (void)rsprt; (void)spark; (void)sparm; (void)cname; (void)olbl;
            return true;
        }
        void Uninstall() override
        {
            outfit::Uninstall_OwnSuitRowLabel_Hooks();
            outfit::Uninstall_UniqueCharacterName_Hook();
            outfit::Uninstall_SuitParamUniqueCharacter_Hook();
            outfit::Uninstall_GetVfxFileFromParts_Hook();
            outfit::Uninstall_PlayerInfoGetPartsType_Hooks();
            outfit::Uninstall_OutfitAbilities_Hooks();
            outfit::Uninstall_OutfitMotionMtar_Hook();
            outfit::Uninstall_OutfitGetCamoufValue_Hook();
            outfit::Uninstall_OutfitCamoBonus_Hook();
            outfit::Uninstall_OutfitHeadOption_Hook();
        }
    };
    class TppEquipConstInjectModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "TppEquipConstInject"; }
        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_TppEquip_RegisterConstant_Hook();
        }
        void Uninstall() override
        {
            Uninstall_TppEquip_RegisterConstant_Hook();
        }
    };

    class EquipIdTableOverflowModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "EquipIdTableReload"; }
        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_TppEquip_ReloadEquipIdTable_Hook();
        }
        void Uninstall() override
        {
            Uninstall_TppEquip_ReloadEquipIdTable_Hook();
        }
    };

    class GunBasicInjectModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "GunBasicInject"; }
        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);

            bool ok = Install_TppEquip_ReloadEquipParameterTables2_Hook();
            ok = Install_MotionLoader_ReceiverTypeHook() && ok;
            ok = Install_MotionLoader_UnderBarrelTypeHook() && ok;
            ok = Install_MotionLoader_BarrelTypeHook() && ok;
            ok = Install_MotionLoader_MagazineTypeHook() && ok;
            ok = Install_MotionLoader_StockTypeHook() && ok;
            ok = Install_MotionLoaderImpl_GetMtarIds_Hook() && ok;
            ok = Install_GetGunMotionIdFromTable_Hook() && ok;
            ok = Install_UiController_UiSightTypeHook() && ok;
            Install_UiUtility_GetWeaponItemNameLangIdHook();
            ok = Install_GetAttackIdGuard() && ok;
            ok = Install_GunInfoGuard() && ok;
            ok = Install_BulletEffectGuard() && ok;
            ok = Install_WeaponKeyLog() && ok;
            ok = Install_FireSoundOverride_Hook() && ok;
            ok = Install_LoadoutRequestGuard() && ok;
            ok = Install_LoadoutGunInfoGetOrBuild() && ok;
            ok = Install_SuppressorGauge_Hook() && ok;
            ok = Install_DamageParameter_Hook() && ok;
            ok = equip::Install_BulletLockOn_Hooks() && ok;
            ok = equip::Install_BulletMultiShot_Hooks() && ok;
            ok = shell::Install_RemoteMissile_Hooks() && ok;
            return ok;
        }
        void Uninstall() override
        {
            shell::Uninstall_RemoteMissile_Hooks();
            equip::Uninstall_BulletMultiShot_Hooks();
            equip::Uninstall_BulletLockOn_Hooks();
            Uninstall_TppEquip_ReloadEquipParameterTables2_Hook();
            Uninstall_MotionLoader_ReceiverTypeHook();
            Uninstall_MotionLoader_UnderBarrelTypeHook();
            Uninstall_MotionLoader_BarrelTypeHook();
            Uninstall_MotionLoader_MagazineTypeHook();
            Uninstall_MotionLoader_StockTypeHook();
            Uninstall_GetGunMotionIdFromTable_Hook();
            Uninstall_MotionLoaderImpl_GetMtarIds_Hook();
            Uninstall_UiController_UiSightTypeHook();
            Uninstall_UiUtility_GetWeaponItemNameLangIdHook();
            Uninstall_GetAttackIdGuard();
            Uninstall_GunInfoGuard();
            Uninstall_WeaponKeyLog();
            Uninstall_FireSoundOverride_Hook();
            Uninstall_LoadoutRequestGuard();
            Uninstall_LoadoutGunInfoGetOrBuild();
            Uninstall_SuppressorGauge_Hook();
            Uninstall_DamageParameter_Hook();
        }
    };

    class SupportHeliWeaponModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "SupportHeliWeapon"; }
        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            if (!Install_SupportHeliCustomizeCodec_Hook())
                return false;
            Install_EquipDevelopControllerImpl_GetHeliPartsId_Hooks();
            Install_CustomizeSelectorCallbackImpl_DecideActWindowPartHelicopter_Hook();
            Install_CustomizeSelectorCallbackImpl_HoverHeliPart_Hook();
            Install_EquipDetailsCallbackImpl_SetupHeliCustomizeDetails_Hook();
            Install_CustomizeControllerImpl_SetSubWeaponAmmo_Hooks();
            Install_LauncherActionImpl_Execute_Hooks();
            Install_SupportCombatAiImpl_StepAttack_Hooks();
            Install_ShellSystemImpl_Fire_Hook();
            Install_BlastSystemImpl_Blast_Hook();
            Install_ShellImpl_UpdateAction_Patch();
            Install_ShellImpl_RequestSmokeCloud_Hook();
            Install_TppHeli2_LuaCommands_Hook();
            Install_SupportHeliCustomizeMenu_Hooks();
            Install_SupportHeliOutfit_Hooks();
            Install_SupportHeliDevelopTab_Hooks();
            Install_GunActionImpl_FireBarrel_Hook();
            Install_TppHeli2_Allocate_Hook();
            return true;
        }
        void Uninstall() override
        {
            Uninstall_TppHeli2_Allocate_Hook();
            Uninstall_GunActionImpl_FireBarrel_Hook();
            Uninstall_SupportHeliDevelopTab_Hooks();
            Uninstall_SupportHeliOutfit_Hooks();
            Uninstall_SupportHeliCustomizeMenu_Hooks();
            Uninstall_TppHeli2_LuaCommands_Hook();
            Uninstall_ShellImpl_RequestSmokeCloud_Hook();
            Uninstall_ShellImpl_UpdateAction_Patch();
            Uninstall_BlastSystemImpl_Blast_Hook();
            Uninstall_ShellSystemImpl_Fire_Hook();
            Uninstall_SupportCombatAiImpl_StepAttack_Hooks();
            Uninstall_LauncherActionImpl_Execute_Hooks();
            Uninstall_CustomizeControllerImpl_SetSubWeaponAmmo_Hooks();
            Uninstall_EquipDetailsCallbackImpl_SetupHeliCustomizeDetails_Hook();
            Uninstall_CustomizeSelectorCallbackImpl_HoverHeliPart_Hook();
            Uninstall_CustomizeSelectorCallbackImpl_DecideActWindowPartHelicopter_Hook();
            Uninstall_EquipDevelopControllerImpl_GetHeliPartsId_Hooks();
            Uninstall_SupportHeliCustomizeCodec_Hook();
        }
    };
}

void RegisterBuiltInFeatureModules()
{
    static AudioBankMemoryModule s_AudioBankMemoryModule;
    static LaserSightColorModule s_LaserSightColorModule;
    static LaserSightSwitchModule s_LaserSightSwitchModule;
    static LuaErrorUnwindModule s_LuaErrorUnwindModule;
    static GeoWorldGuardModule s_GeoWorldGuardModule;
    static WideEquipMessagesModule s_WideEquipMessagesModule;
    static EquipOwnPackModule s_EquipOwnPackModule;
    static EquipCustomPlacedModule s_EquipCustomPlacedModule;
    static Fox2EquipNamesModule s_Fox2EquipNamesModule;
    static SoldierGrenadeIdsModule s_SoldierGrenadeIdsModule;
    static DamageGimmickModule s_DamageGimmickModule;
    static FobLoadoutSyncGuardModule s_FobLoadoutSyncGuardModule;
    static WideEquipAliasGatesModule s_WideEquipAliasGatesModule;
    static MotherBaseSyncGuardModule s_MotherBaseSyncGuardModule;
    static StaffSyncGuardModule s_StaffSyncGuardModule;
    static ItemCheckGuardModule s_ItemCheckGuardModule;
    static BootRecordGuardModule s_BootRecordGuardModule;
    static LoadoutItemLevelGuardModule s_LoadoutItemLevelGuardModule;
    static PartMotionRowsModule s_PartMotionRowsModule;
    static ShellActivateGuardModule s_ShellActivateGuardModule;
    static SupportWeaponModule s_SupportWeaponModule;
    static SupportWeaponGravityModule s_SupportWeaponGravityModule;
    static GradeFamilyModule s_GradeFamilyModule;
    static DecoyModelModule s_DecoyModelModule;
    static SupportHeliWeaponModule s_SupportHeliWeaponModule;
    static LuaBridgeModule s_LuaBridgeModule;
    static EquipBgTextureModule s_EquipBgTextureModule;
    static UixUtilityControlModule s_UixUtilityControlModule;
    static FontGlyphGuardModule s_FontGlyphGuardModule;
    static BinoRadioIconModule s_BinoRadioIconModule;
    static MainPadSlotModule s_MainPadSlotModule;
    static ButtonIconSheetModule s_ButtonIconSheetModule;
    static ButtonMeshNodeGuardModule s_ButtonMeshNodeGuardModule;
    static PadAssignModule s_PadAssignModule;
    static ControllerAssignMenuModule s_ControllerAssignMenuModule;
    static ControlTypePageAssignModule s_ControlTypePageAssignModule;
    static SonyPadLayoutModule s_SonyPadLayoutModule;
    static TextIconsModule s_TextIconsModule;
    static FallDeathVoiceModule s_FallDeathVoiceModule;
    static QuietVoiceLinesModule s_QuietVoiceLinesModule;
    static GameObjectLifeModule s_GameObjectLifeModule;
    static MissionTelopBgTextureModule s_MissionTelopBgTextureModule;
    static LoadingSplashModule s_LoadingSplashModule;
    static GameOverSplashModule s_GameOverSplashModule;
    static BonusPopupBgTextureModule s_BonusPopupBgTextureModule;
    static MissionPreparationBgmModule s_MissionPreparationBgmModule;
    static RewardPopupBgTextureModule s_RewardPopupBgTextureModule;
    static HoldupCancelLookToPlayerModule s_HoldupCancelLookToPlayerModule;
    static CautionTimerModule s_CautionTimerModule;
    static CpAntiAirModule s_CpAntiAirModule;
    static ChangeLocationMenuModule s_ChangeLocationMenuModule;
    static LocationIconModule s_LocationIconModule;
    static MissionListLocationTabsModule s_MissionListLocationTabsModule;
    static AccSeatIdleMotionModule s_AccSeatIdleMotionModule;
    static SupineCureMotionModule s_SupineCureMotionModule;
    static LifeRecoveryCooldownModule s_LifeRecoveryCooldownModule;
    static CustomWeaponLoadoutCostModule s_CustomWeaponLoadoutCostModule;
    static UnderBarrelAmmoMeshModule s_UnderBarrelAmmoMeshModule;
    static SupplyDropItemGuardModule s_SupplyDropItemGuardModule;
    static FobVisitGuardModule s_FobVisitGuardModule;
    static LoadWatchModule s_LoadWatchModule;
    static ItemBatteryModule s_ItemBatteryModule;
    static ItemUseModule s_ItemUseModule;
    static ItemCountModule s_ItemCountModule;
    static ItemParameterModule s_ItemParameterModule;
    static ShieldParamsModule s_ShieldParamsModule;
    static ItemEffectGaugeModule s_ItemEffectGaugeModule;
    static UavEmpModule s_UavEmpModule;
    static SpRespawnModule s_SpRespawnModule;
    static SoldierWideWeaponModule s_SoldierWideWeaponModule;
    static HostageStruggleModule s_HostageStruggleModule;
    static FrameWorkMotionArchiveModule s_FrameWorkMotionArchiveModule;
    static ClipEventsModule s_ClipEventsModule;
    static PhotoAdditionalTextModule s_PhotoAdditionalTextModule;
    static RealizedSahelanFovaModule s_RealizedSahelanFovaModule;
    static MeshVisibilityModule s_MeshVisibilityModule;
    static SoldierHairFovaModule s_SoldierHairFovaModule;
    static ChangeFovaOffScreenModule s_ChangeFovaOffScreenModule;
    static PlayerVoiceFpkModule s_PlayerVoiceFpkModule;
    static PlayerVoiceTypeModule s_PlayerVoiceTypeModule;
    static HeadMotionModule s_HeadMotionModule;
    static EnterDownHoldupForceVoiceModule s_EnterDownHoldupForceVoiceModule;
    static VIPSleepFaintModule s_VIPSleepFaintModule;
    static OfficerWakeModule s_OfficerWakeModule;
    static VIPHoldupModule s_VIPHoldupModule;
    static VIPSoundRecoveryModule s_VIPSoundRecoveryModule;
    static VIPRadioModule s_VIPRadioModule;
    static HoldUpReactionCowardlyReactionsModule s_HoldUpReactionCowardlyReactionsModule;
    static PerSoldierCallSignOverrideModule s_PerSoldierCallSignOverrideModule;
    static LostHostageModule s_LostHostageModule;
    static UpdateOptCamoModule s_UpdateOptCamoModule;
    static CassetteTapePlayHookModule s_CassetteTapePlayHookModule;
    static CustomTapesModule s_CustomTapesModule;
    static CustomTapeOwnershipModule s_CustomTapeOwnershipModule;
    static CassetteTrackPagingModule s_CassetteTrackPagingModule;
    static CassetteTapeSetCurrentAlbumModule s_CassetteTapeSetCurrentAlbumModule;
    static CustomRadioCassetteModule s_CustomRadioCassetteModule;
    static CustomTapeLongFilenameModule s_CustomTapeLongFilenameModule;
    static SoundSystemBeginModule s_SoundSystemBeginModule;
    static CassetteWalkmanEventsModule s_CassetteWalkmanEventsModule;
    static TppPickableModule s_TppPickableModule;
    static EquipIconFtexPathModule s_EquipIconFtexPathModule;
    static MbDvcCustomPopupModule s_MbDvcCustomPopupModule;
    static MissionDeployWarningModule s_MissionDeployWarningModule;
    static MissionMenuHelpModule s_MissionMenuHelpModule;
    static SoldierVoiceTypeQueryModule s_SoldierVoiceTypeQueryModule;
    static VoicePitchOverrideModule s_VoicePitchOverrideModule;
    static AudioVolumeOptionsModule s_AudioVolumeOptionsModule;
    static SoldierAkObjIdMapModule s_SoldierAkObjIdMapModule;
    static TargetCqcStanceModule s_TargetCqcStanceModule;
    static QuietCqcPatchesModule s_QuietCqcPatchesModule;
    static UniqueCharacterSuitSlotModule s_UniqueCharacterSuitSlotModule;
    static PlayerAttachInDemoModule s_PlayerAttachInDemoModule;
    static VehicleDemoStopExemptModule s_VehicleDemoStopExemptModule;
    static SoldierIgnorePlayerModule s_SoldierIgnorePlayerModule;
    static SoldierIgnoreVehicleModule s_SoldierIgnoreVehicleModule;
    static HeadMarkStaffNameModule s_HeadMarkStaffNameModule;
    static SoldierEventHooksModule s_SoldierEventHooksModule;
    static SoldierFactionModule s_SoldierFactionModule;
    static InterrogationVoiceEventModule s_InterrogationVoiceEventModule;
    static SetEyeLampColorModule s_SetEyeLampColorModule;
    static GetGameObjectIdWithIndex s_GetGameObjectIdWithIndex;
    static EnemyLangIdOverrideModule s_EnemyLangIdOverrideModule;
    static CrawlSideRollModule s_CrawlSideRollModule;
    static FobPlayerCharactersModule s_FobPlayerCharactersModule;
    static WormholeNearDeathWarpModule s_WormholeNearDeathWarpModule;
    static PlayerLockPickModule s_PlayerLockPickModule;
    static SubtitlesEventMessageModule s_SubtitlesEventMessageModule;
    static PhaseSneakAiImpl_PreUpdateModule s_PhaseSneakAiImpl_PreUpdateModule;
    static MissionEmergencyModule s_MissionEmergencyModule;
    static ShowMissionIconModule s_ShowMissionIconModule;
    static GameObjectSendCommandModule s_GameObjectSendCommandModule;
    static ReinitializeModule s_ReinitializeModule;
    static InitEquipHudDataModule s_InitEquipHudDataModule;
    static OccasionalChatListModule s_OccasionalChatListModule;
    static SoldierConversationsModule s_SoldierConversationsModule;
    static MotherBaseSalutesModule s_MotherBaseSalutesModule;
    static HeadOptionMissionAbilitiesModule s_HeadOptionMissionAbilitiesModule;
    static SoldierNoticeModule s_SoldierNoticeModule;
    static IdleActingVoiceModule s_IdleActingVoiceModule;
    static SoldierVoiceSwapModule s_SoldierVoiceSwapModule;
    static SoldierCombatVoiceModule s_SoldierCombatVoiceModule;
    static ChaffRadioVoiceModule s_ChaffRadioVoiceModule;
    static SquatIdleRouteModule s_SquatIdleRouteModule;
    static HoldupSelfReleaseModule s_HoldupSelfReleaseModule;
    static InvestigationSquadsModule s_InvestigationSquadsModule;
    static AntiAirGunRouteModule s_AntiAirGunRouteModule;
    static JeepGunnerModule s_JeepGunnerModule;
    static CustomVehiclesModule s_CustomVehiclesModule;
    static RouteCharacterTargetModule s_RouteCharacterTargetModule;
    static RouteWalkEdgesModule s_RouteWalkEdgesModule;
    static CpRadioVariantsModule s_CpRadioVariantsModule;
    static AnimalNoticeModule s_AnimalNoticeModule;
    static BuddyOutfitModule s_BuddyOutfitModule;
    static BattleGearModule s_BattleGearModule;
    static FieldTaxiMenuModule s_FieldTaxiMenuModule;
    static TimeCigaretteUiModule s_TimeCigaretteUiModule;
    static HeadMarkDyingColourModule s_HeadMarkDyingColourModule;
    static DataBaseNewFlagModule s_DataBaseNewFlagModule;
    static MbTopMenuStickyHideModule s_MbTopMenuStickyHideModule;
    static IDroidCustomItemsModule s_IDroidCustomItemsModule;
    static PrefabListControlModule s_PrefabListControlModule;
    static DataBaseBluePrintListModule s_DataBaseBluePrintListModule;
    static HideBinocleModule s_HideBinocleModule;
    static HeliSoundControllerModule s_HeliSoundControllerModule;
    static LandingZoneDangerModule s_LandingZoneDangerModule;
    static HeliSearchLightModule s_HeliSearchLightModule;
    static HeliDamageEffectsModule s_HeliDamageEffectsModule;
    static HeliLifeRateModule s_HeliLifeRateModule;
    static HeliDoorModule s_HeliDoorModule;
    static HeliPlayerMachineGunModule s_HeliPlayerMachineGunModule;
    static VehicleCannotRideModule s_VehicleCannotRideModule;
    static SoldierRideHeliModule s_SoldierRideHeliModule;
    static AnnounceLogModule s_AnnounceLogModule;
    static IdroidLinesModule s_IdroidLinesModule;
    static UavRouteHandlesModule s_UavRouteHandlesModule;
    static TornadoDualPatchModule s_TornadoDualPatchModule;
    static IsWeaponNoUseInPlaceActionModule s_IsWeaponNoUseInPlaceActionModule;
    static ChargeShotModule s_ChargeShotModule;
    static CallComradeCommandModule s_CallComradeCommandModule;
    static BarrierEffectLoadModule s_BarrierEffectLoadModule;
    static BarrierEffectSpawnModule s_BarrierEffectSpawnModule;
    static LuaErrorThrowModule s_LuaErrorThrowModule;
    static CheckpointReloadModule s_CheckpointReloadModule;
    static SupportAttackCrashGuardModule s_SupportAttackCrashGuardModule;
    static ExitTeardownGuardModule s_ExitTeardownGuardModule;
    static StaffFaceContainmentModule       s_StaffFaceContainmentModule;
    static ServerManagerBufferReleaseModule s_ServerManagerBufferReleaseModule;
    static VertexStreamPoolModule s_VertexStreamPoolModule;
    static RendererPoolsModule s_RendererPoolsModule;
    static TextureStoragePoolModule s_TextureStoragePoolModule;
    static TexturePoolsModule s_TexturePoolsModule;
    static TexturePackAbortModule s_TexturePackAbortModule;
    static TextureProxyFallbackModule s_TextureProxyFallbackModule;
    static NoticeSensorModule s_NoticeSensorModule;
    static CommandPostMembersModule s_CommandPostMembersModule;
    static ReticleLayoutResetModule s_ReticleLayoutResetModule;
    static EnhanceLangIdUnlimitedModule s_EnhanceLangIdUnlimitedModule;
    static DevelopArrayGrowModule s_DevelopArrayGrowModule;
    static PlayerOutfitCoreModule s_PlayerOutfitCoreModule;
    static PlayerOutfitEquipModule s_PlayerOutfitEquipModule;
    static PlayerOutfitExtrasModule s_PlayerOutfitExtrasModule;
    static TppEquipConstInjectModule s_TppEquipConstInjectModule;
    static EquipIdTableOverflowModule s_EquipIdTableOverflowModule;
    static GunBasicInjectModule s_GunBasicInjectModule;
    static TppCollectionModule s_TppCollectionModule;
    static DispatchMissionModule s_DispatchMissionModule;

    static std::once_flag s_Once;
    std::call_once(s_Once, []()
        {
            FeatureModuleRegistry::Instance().Register(&s_AudioBankMemoryModule);
            FeatureModuleRegistry::Instance().Register(&s_LuaBridgeModule);
            FeatureModuleRegistry::Instance().Register(&s_EquipBgTextureModule);
            FeatureModuleRegistry::Instance().Register(&s_UixUtilityControlModule);
            FeatureModuleRegistry::Instance().Register(&s_FontGlyphGuardModule);
            FeatureModuleRegistry::Instance().Register(&s_BinoRadioIconModule);
            FeatureModuleRegistry::Instance().Register(&s_ButtonIconSheetModule);
            FeatureModuleRegistry::Instance().Register(&s_ButtonMeshNodeGuardModule);
            FeatureModuleRegistry::Instance().Register(&s_PadAssignModule);
            FeatureModuleRegistry::Instance().Register(&s_ControllerAssignMenuModule);
            FeatureModuleRegistry::Instance().Register(&s_ControlTypePageAssignModule);
            FeatureModuleRegistry::Instance().Register(&s_SonyPadLayoutModule);
            FeatureModuleRegistry::Instance().Register(&s_MainPadSlotModule);
            FeatureModuleRegistry::Instance().Register(&s_TextIconsModule);
            FeatureModuleRegistry::Instance().Register(&s_FallDeathVoiceModule);
            FeatureModuleRegistry::Instance().Register(&s_QuietVoiceLinesModule);
            FeatureModuleRegistry::Instance().Register(&s_GameObjectLifeModule);
            FeatureModuleRegistry::Instance().Register(&s_MissionTelopBgTextureModule);
            FeatureModuleRegistry::Instance().Register(&s_LoadingSplashModule);
            FeatureModuleRegistry::Instance().Register(&s_GameOverSplashModule);
            FeatureModuleRegistry::Instance().Register(&s_BonusPopupBgTextureModule);
            FeatureModuleRegistry::Instance().Register(&s_MissionPreparationBgmModule);
            FeatureModuleRegistry::Instance().Register(&s_RewardPopupBgTextureModule);
            FeatureModuleRegistry::Instance().Register(&s_HoldupCancelLookToPlayerModule);
            FeatureModuleRegistry::Instance().Register(&s_CautionTimerModule);
            FeatureModuleRegistry::Instance().Register(&s_CpAntiAirModule);
            FeatureModuleRegistry::Instance().Register(&s_ChangeLocationMenuModule);
            FeatureModuleRegistry::Instance().Register(&s_LocationIconModule);
            FeatureModuleRegistry::Instance().Register(&s_MissionListLocationTabsModule);
            FeatureModuleRegistry::Instance().Register(&s_AccSeatIdleMotionModule);
            FeatureModuleRegistry::Instance().Register(&s_SupineCureMotionModule);
            FeatureModuleRegistry::Instance().Register(&s_LifeRecoveryCooldownModule);
            FeatureModuleRegistry::Instance().Register(&s_CustomWeaponLoadoutCostModule);
            FeatureModuleRegistry::Instance().Register(&s_UnderBarrelAmmoMeshModule);
            FeatureModuleRegistry::Instance().Register(&s_SupplyDropItemGuardModule);
            FeatureModuleRegistry::Instance().Register(&s_FobVisitGuardModule);
            FeatureModuleRegistry::Instance().Register(&s_LoadWatchModule);
            FeatureModuleRegistry::Instance().Register(&s_ItemBatteryModule);
            FeatureModuleRegistry::Instance().Register(&s_ItemUseModule);
            FeatureModuleRegistry::Instance().Register(&s_ItemCountModule);
            FeatureModuleRegistry::Instance().Register(&s_ItemParameterModule);
            FeatureModuleRegistry::Instance().Register(&s_ShieldParamsModule);
            FeatureModuleRegistry::Instance().Register(&s_ItemEffectGaugeModule);
            FeatureModuleRegistry::Instance().Register(&s_UavEmpModule);
            FeatureModuleRegistry::Instance().Register(&s_SpRespawnModule);
            FeatureModuleRegistry::Instance().Register(&s_SoldierWideWeaponModule);
            FeatureModuleRegistry::Instance().Register(&s_HostageStruggleModule);
            FeatureModuleRegistry::Instance().Register(&s_FrameWorkMotionArchiveModule);
            FeatureModuleRegistry::Instance().Register(&s_ClipEventsModule);
            FeatureModuleRegistry::Instance().Register(&s_PhotoAdditionalTextModule);
            FeatureModuleRegistry::Instance().Register(&s_RealizedSahelanFovaModule);
            FeatureModuleRegistry::Instance().Register(&s_MeshVisibilityModule);
            FeatureModuleRegistry::Instance().Register(&s_SoldierHairFovaModule);
            FeatureModuleRegistry::Instance().Register(&s_ChangeFovaOffScreenModule);
            FeatureModuleRegistry::Instance().Register(&s_PlayerVoiceFpkModule);
            FeatureModuleRegistry::Instance().Register(&s_PlayerVoiceTypeModule);
            FeatureModuleRegistry::Instance().Register(&s_HeadMotionModule);
            FeatureModuleRegistry::Instance().Register(&s_EnterDownHoldupForceVoiceModule);
            FeatureModuleRegistry::Instance().Register(&s_VIPSleepFaintModule);
            FeatureModuleRegistry::Instance().Register(&s_OfficerWakeModule);
            FeatureModuleRegistry::Instance().Register(&s_VIPHoldupModule);
            FeatureModuleRegistry::Instance().Register(&s_VIPSoundRecoveryModule);
            FeatureModuleRegistry::Instance().Register(&s_VIPRadioModule);
            FeatureModuleRegistry::Instance().Register(&s_HoldUpReactionCowardlyReactionsModule);
            FeatureModuleRegistry::Instance().Register(&s_PerSoldierCallSignOverrideModule);
            FeatureModuleRegistry::Instance().Register(&s_LostHostageModule);
            FeatureModuleRegistry::Instance().Register(&s_UpdateOptCamoModule);
            FeatureModuleRegistry::Instance().Register(&s_CassetteTapePlayHookModule);
            FeatureModuleRegistry::Instance().Register(&s_CustomTapesModule);
            FeatureModuleRegistry::Instance().Register(&s_CustomTapeOwnershipModule);
            FeatureModuleRegistry::Instance().Register(&s_SubtitlesEventMessageModule);
            FeatureModuleRegistry::Instance().Register(&s_CassetteTrackPagingModule);
            FeatureModuleRegistry::Instance().Register(&s_CassetteTapeSetCurrentAlbumModule);
            FeatureModuleRegistry::Instance().Register(&s_CustomRadioCassetteModule);
            FeatureModuleRegistry::Instance().Register(&s_CustomTapeLongFilenameModule);
            FeatureModuleRegistry::Instance().Register(&s_SoundSystemBeginModule);
            FeatureModuleRegistry::Instance().Register(&s_CassetteWalkmanEventsModule);
            FeatureModuleRegistry::Instance().Register(&s_TppPickableModule);
            FeatureModuleRegistry::Instance().Register(&s_EquipIconFtexPathModule);
            FeatureModuleRegistry::Instance().Register(&s_MbDvcCustomPopupModule);
            FeatureModuleRegistry::Instance().Register(&s_MissionDeployWarningModule);
            FeatureModuleRegistry::Instance().Register(&s_MissionMenuHelpModule);
            FeatureModuleRegistry::Instance().Register(&s_SoldierAkObjIdMapModule);
            FeatureModuleRegistry::Instance().Register(&s_TargetCqcStanceModule);
            FeatureModuleRegistry::Instance().Register(&s_QuietCqcPatchesModule);
            FeatureModuleRegistry::Instance().Register(&s_UniqueCharacterSuitSlotModule);
            FeatureModuleRegistry::Instance().Register(&s_PlayerAttachInDemoModule);
            FeatureModuleRegistry::Instance().Register(&s_VehicleDemoStopExemptModule);
            FeatureModuleRegistry::Instance().Register(&s_SoldierIgnorePlayerModule);
            FeatureModuleRegistry::Instance().Register(&s_SoldierIgnoreVehicleModule);
            FeatureModuleRegistry::Instance().Register(&s_HeadMarkStaffNameModule);
            FeatureModuleRegistry::Instance().Register(&s_SoldierEventHooksModule);
            FeatureModuleRegistry::Instance().Register(&s_SoldierFactionModule);
            FeatureModuleRegistry::Instance().Register(&s_InterrogationVoiceEventModule);
            FeatureModuleRegistry::Instance().Register(&s_SoldierVoiceTypeQueryModule);
            FeatureModuleRegistry::Instance().Register(&s_VoicePitchOverrideModule);
            FeatureModuleRegistry::Instance().Register(&s_AudioVolumeOptionsModule);
            FeatureModuleRegistry::Instance().Register(&s_SetEyeLampColorModule);
            FeatureModuleRegistry::Instance().Register(&s_GetGameObjectIdWithIndex);
            FeatureModuleRegistry::Instance().Register(&s_EnemyLangIdOverrideModule);
            FeatureModuleRegistry::Instance().Register(&s_CrawlSideRollModule);
            FeatureModuleRegistry::Instance().Register(&s_FobPlayerCharactersModule);
            FeatureModuleRegistry::Instance().Register(&s_WormholeNearDeathWarpModule);
            FeatureModuleRegistry::Instance().Register(&s_PlayerLockPickModule);
            FeatureModuleRegistry::Instance().Register(&s_PhaseSneakAiImpl_PreUpdateModule);
            FeatureModuleRegistry::Instance().Register(&s_MissionEmergencyModule);
            FeatureModuleRegistry::Instance().Register(&s_ShowMissionIconModule);
            FeatureModuleRegistry::Instance().Register(&s_GameObjectSendCommandModule);
            FeatureModuleRegistry::Instance().Register(&s_ReinitializeModule);
            FeatureModuleRegistry::Instance().Register(&s_InitEquipHudDataModule);
            FeatureModuleRegistry::Instance().Register(&s_OccasionalChatListModule);
            FeatureModuleRegistry::Instance().Register(&s_SoldierConversationsModule);
            FeatureModuleRegistry::Instance().Register(&s_MotherBaseSalutesModule);
            FeatureModuleRegistry::Instance().Register(&s_HeadOptionMissionAbilitiesModule);
            FeatureModuleRegistry::Instance().Register(&s_SoldierNoticeModule);
            FeatureModuleRegistry::Instance().Register(&s_IdleActingVoiceModule);
            FeatureModuleRegistry::Instance().Register(&s_SoldierVoiceSwapModule);
            FeatureModuleRegistry::Instance().Register(&s_SoldierCombatVoiceModule);
            FeatureModuleRegistry::Instance().Register(&s_ChaffRadioVoiceModule);
            FeatureModuleRegistry::Instance().Register(&s_SquatIdleRouteModule);
            FeatureModuleRegistry::Instance().Register(&s_HoldupSelfReleaseModule);
            FeatureModuleRegistry::Instance().Register(&s_InvestigationSquadsModule);
            FeatureModuleRegistry::Instance().Register(&s_AntiAirGunRouteModule);
            FeatureModuleRegistry::Instance().Register(&s_JeepGunnerModule);
            FeatureModuleRegistry::Instance().Register(&s_CustomVehiclesModule);
            FeatureModuleRegistry::Instance().Register(&s_RouteCharacterTargetModule);
            FeatureModuleRegistry::Instance().Register(&s_RouteWalkEdgesModule);
            FeatureModuleRegistry::Instance().Register(&s_CpRadioVariantsModule);
            FeatureModuleRegistry::Instance().Register(&s_AnimalNoticeModule);
            FeatureModuleRegistry::Instance().Register(&s_BuddyOutfitModule);
#ifdef _DEBUG   // Battle Gear is unfinished: Debug test builds only, the Release builds players get leave it out
            FeatureModuleRegistry::Instance().Register(&s_BattleGearModule);
#endif
            FeatureModuleRegistry::Instance().Register(&s_FieldTaxiMenuModule);
            FeatureModuleRegistry::Instance().Register(&s_TimeCigaretteUiModule);
            FeatureModuleRegistry::Instance().Register(&s_HeadMarkDyingColourModule);
            FeatureModuleRegistry::Instance().Register(&s_DataBaseBluePrintListModule);
            FeatureModuleRegistry::Instance().Register(&s_DataBaseNewFlagModule);
            FeatureModuleRegistry::Instance().Register(&s_MbTopMenuStickyHideModule);
            FeatureModuleRegistry::Instance().Register(&s_IDroidCustomItemsModule);
            FeatureModuleRegistry::Instance().Register(&s_PrefabListControlModule);
            FeatureModuleRegistry::Instance().Register(&s_HideBinocleModule);
            FeatureModuleRegistry::Instance().Register(&s_HeliSoundControllerModule);
            FeatureModuleRegistry::Instance().Register(&s_LandingZoneDangerModule);
            FeatureModuleRegistry::Instance().Register(&s_HeliSearchLightModule);
            FeatureModuleRegistry::Instance().Register(&s_HeliDamageEffectsModule);
            FeatureModuleRegistry::Instance().Register(&s_HeliLifeRateModule);
            FeatureModuleRegistry::Instance().Register(&s_HeliDoorModule);
            FeatureModuleRegistry::Instance().Register(&s_HeliPlayerMachineGunModule);
            FeatureModuleRegistry::Instance().Register(&s_VehicleCannotRideModule);
            FeatureModuleRegistry::Instance().Register(&s_SoldierRideHeliModule);
            FeatureModuleRegistry::Instance().Register(&s_AnnounceLogModule);
            FeatureModuleRegistry::Instance().Register(&s_IdroidLinesModule);
            FeatureModuleRegistry::Instance().Register(&s_UavRouteHandlesModule);
            FeatureModuleRegistry::Instance().Register(&s_TornadoDualPatchModule);
            FeatureModuleRegistry::Instance().Register(&s_IsWeaponNoUseInPlaceActionModule);
            FeatureModuleRegistry::Instance().Register(&s_ChargeShotModule);
            FeatureModuleRegistry::Instance().Register(&s_CallComradeCommandModule);
            FeatureModuleRegistry::Instance().Register(&s_LuaErrorThrowModule);
            FeatureModuleRegistry::Instance().Register(&s_CheckpointReloadModule);
            FeatureModuleRegistry::Instance().Register(&s_SupportAttackCrashGuardModule);
            FeatureModuleRegistry::Instance().Register(&s_ExitTeardownGuardModule);
            FeatureModuleRegistry::Instance().Register(&s_StaffFaceContainmentModule);
            FeatureModuleRegistry::Instance().Register(&s_ServerManagerBufferReleaseModule);
            FeatureModuleRegistry::Instance().Register(&s_VertexStreamPoolModule);
            FeatureModuleRegistry::Instance().Register(&s_RendererPoolsModule);
            FeatureModuleRegistry::Instance().Register(&s_TextureStoragePoolModule);
            FeatureModuleRegistry::Instance().Register(&s_TexturePoolsModule);
            FeatureModuleRegistry::Instance().Register(&s_TexturePackAbortModule);
            FeatureModuleRegistry::Instance().Register(&s_TextureProxyFallbackModule);
            FeatureModuleRegistry::Instance().Register(&s_NoticeSensorModule);
            FeatureModuleRegistry::Instance().Register(&s_CommandPostMembersModule);
            FeatureModuleRegistry::Instance().Register(&s_ReticleLayoutResetModule);
            FeatureModuleRegistry::Instance().Register(&s_EnhanceLangIdUnlimitedModule);
            FeatureModuleRegistry::Instance().Register(&s_BarrierEffectLoadModule);
            FeatureModuleRegistry::Instance().Register(&s_BarrierEffectSpawnModule);
            FeatureModuleRegistry::Instance().Register(&s_DevelopArrayGrowModule);
            FeatureModuleRegistry::Instance().Register(&s_PlayerOutfitCoreModule);
            FeatureModuleRegistry::Instance().Register(&s_PlayerOutfitEquipModule);
            FeatureModuleRegistry::Instance().Register(&s_PlayerOutfitExtrasModule);
            FeatureModuleRegistry::Instance().Register(&s_TppEquipConstInjectModule);
            FeatureModuleRegistry::Instance().Register(&s_EquipIdTableOverflowModule);
            FeatureModuleRegistry::Instance().Register(&s_GunBasicInjectModule);
            FeatureModuleRegistry::Instance().Register(&s_TppCollectionModule);
            FeatureModuleRegistry::Instance().Register(&s_DispatchMissionModule);
            FeatureModuleRegistry::Instance().Register(&s_LaserSightColorModule);
            FeatureModuleRegistry::Instance().Register(&s_LaserSightSwitchModule);
            FeatureModuleRegistry::Instance().Register(&s_LuaErrorUnwindModule);
            FeatureModuleRegistry::Instance().Register(&s_GeoWorldGuardModule);
            FeatureModuleRegistry::Instance().Register(&s_WideEquipMessagesModule);
            FeatureModuleRegistry::Instance().Register(&s_EquipOwnPackModule);
            FeatureModuleRegistry::Instance().Register(&s_EquipCustomPlacedModule);
            FeatureModuleRegistry::Instance().Register(&s_Fox2EquipNamesModule);
            FeatureModuleRegistry::Instance().Register(&s_SoldierGrenadeIdsModule);
            FeatureModuleRegistry::Instance().Register(&s_DamageGimmickModule);
            FeatureModuleRegistry::Instance().Register(&s_FobLoadoutSyncGuardModule);
            FeatureModuleRegistry::Instance().Register(&s_WideEquipAliasGatesModule);
            FeatureModuleRegistry::Instance().Register(&s_MotherBaseSyncGuardModule);
            FeatureModuleRegistry::Instance().Register(&s_StaffSyncGuardModule);
            FeatureModuleRegistry::Instance().Register(&s_ItemCheckGuardModule);
            FeatureModuleRegistry::Instance().Register(&s_BootRecordGuardModule);
            FeatureModuleRegistry::Instance().Register(&s_LoadoutItemLevelGuardModule);
            FeatureModuleRegistry::Instance().Register(&s_PartMotionRowsModule);
            FeatureModuleRegistry::Instance().Register(&s_ShellActivateGuardModule);
            FeatureModuleRegistry::Instance().Register(&s_SupportWeaponModule);
            FeatureModuleRegistry::Instance().Register(&s_SupportWeaponGravityModule);
            FeatureModuleRegistry::Instance().Register(&s_GradeFamilyModule);
            FeatureModuleRegistry::Instance().Register(&s_DecoyModelModule);
            FeatureModuleRegistry::Instance().Register(&s_SupportHeliWeaponModule);
        });
}
