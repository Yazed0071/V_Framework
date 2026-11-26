#include <pch.h>

#include <atomic>
#include <cstdint>

#include "LuaBroadcaster.h"
#include "MissionCodeGuard.h"
#include "MissionStateReset.h"
#include "../hooks/player/QuietCqcPatches.h"
#include "../hooks/player/PlayerAttachInDemo.h"
#include "../hooks/player/ActionCoreImpl_ExecPostPutMotion.h"
#include "../hooks/player/RespawnActionPluginImpl_PollingImpl.h"
#include "../hooks/player/Player2Impl_UpdateVoiceType.h"
#include "../hooks/player/AccSeatIdleMotion.h"
#include "../hooks/player/AttackActionImpl_SetEquipMotion.h"
#include "../hooks/player/AttackActionImpl_UpdateShootingByPad.h"
#include "../hooks/player/LifeControllerImpl_UpdateHost.h"
#include "../hooks/outfit/UniqueCharacterSuitSlot.h"
#include "log.h"
#include "DeployGuard.h"

#include "../lua/LuaApi.h"
#include "../lua/V_UixUtilitySubViewport.h"

#include "../hooks/bullet/Bullet3Impl_ActivateBulletAtEmptyWorkPatch.h"
#include "../hooks/heli/ActionCoreImpl_Execute.h"
#include "../hooks/heli/FieldTaxiMenu.h"
#include "../hooks/heli/HeliSoundController.h"
#include "../hooks/heli/LightActionImpl_Execute.h"
#include "../hooks/heli/MechaActionImpl_StateOnPassenger.h"
#include "../hooks/vehicle/RideVehicleActionImpl_CheckVehicleIconArea.h"
#include "../hooks/vehicle/CustomVehicles.h"
#include "../hooks/vehicle/VehicleSortieRows.h"
#include "../hooks/vehicle/JeepGunEffects.h"
#include "../hooks/player/RideHeliActionPluginImpl_ExecActionButtonFunction.h"
#include "../hooks/soldier/RideHeliActionImpl_State_GetInAction.h"
#include "../hooks/outfit/AdditionalMotionTable_GetMtarPathId.h"
#include "../hooks/outfit/BlockControllerImpl_LoadPartsNew.h"
#include "../hooks/outfit/OutfitRegistry.h"
#include "../hooks/outfit/OutfitPalette.h"
#include "../hooks/ui/UiPadLock.h"
#include "../hooks/pickable/TppPickableRuntime.h"
#include "../hooks/menupopup/MbDvcMissionListCallbackImpl_ActivatePrefabPopupParameter.h"
#include "../hooks/outfit/OutfitAbilities.h"
#include "../hooks/player/CallMenuCommandSet_UpdateState.h"
#include "../hooks/equip/RankLimitedItems.h"
#include "../hooks/equip/EquipPartParams.h"
#include "../hooks/soldier/NoticeControllerImpl_CheckForceNotice.h"
#include "../hooks/player/CqcActionPluginImpl_StateHoldMove.h"
#include "../hooks/sahelan/RealizedSahelanFovaHook.h"
#include "../hooks/shell/RemoteMissile.h"
#include "../hooks/sahelan/SetEyeLampColorHook.h"
#include "../hooks/sahelan/PhaseSneakAiImpl_PreUpdate.h"
#include "../hooks/soldier/ActionCoreImpl_UpdateOptCamo.h"
#include "../hooks/soldier/CautionStepNormalTimerHook.h"
#include "../hooks/soldier/CommandPost2Impl_RequestInvestigation.h"
#include "../hooks/soldier/CpRadioVariants.h"
#include "../hooks/soldier/FakeSurrender.h"
#include "../hooks/soldier/HoldupAiImpl_StepHoldupStand.h"
#include "../hooks/soldier/RouteAiImpl_StepEvent.h"
#include "../hooks/soldier/RouteAiImpl_SetSubActionParamWithNodeEvent.h"
#include "../hooks/soldier/Soldier2Impl_ProcessSignal.h"
#include "../hooks/soldier/SoldierBackupCall.h"
#include "../hooks/soldier/GetVoiceParamWithCallSign.h"
#include "../hooks/soldier/InterrogationVoiceEvent.h"
#include "../hooks/soldier/LostHostageHook.h"
#include "../hooks/soldier/SoldierVehicleAvoid.h"
#include "../hooks/soldier/Soldier2Impl_UpdateMarkers.h"
#include "../hooks/soldier/SoldierFaction.h"
#include "../hooks/soldier/NoticeControllerImpl_CheckSightNoticePlayer.h"
#include "../hooks/soldier/NoticeControllerImpl_GetOccasionalChat.h"
#include "../hooks/soldier/NoticeControllerImpl_CheckSpecialSaluteVoiceLabel.h"
#include "../hooks/soldier/RadioControllerImpl_StepRadioWaitResponse.h"
#include "../hooks/soldier/SoldierAkObjIdMap.h"
#include "../hooks/soldier/SoldierObjectRtpc.h"
#include "../hooks/soldier/StepRadioDiscovery.h"
#include "../hooks/soldier/VIPHoldupHook.h"
#include "../hooks/soldier/VIPImportant.h"
#include "../hooks/soldier/VIPRadioHook.h"
#include "../hooks/ui/EnemyLangIdOverride.h"
#include "../hooks/ui/HeadMarkMarkerEvCall_SetIconSubType.h"
#include "../hooks/ui/MbTopMenuStickyHide.h"
#include "../hooks/ui/MbDvcSoundControllerImpl_RequestVoice.h"
#include "../hooks/player/TimeCigaretteUiHook.h"
#include "../hooks/soldier/GameObjectLife.h"
#include "../hooks/soldier/Soldier2SoundControllerImpl_CallVoiceWithoutSync.h"
#include "../hooks/soldier/Soldier2Impl_RealizeRealizedSoldier.h"
#include "../hooks/player/Fova2ControllerImpl_ApplyPlayerFova.h"
#include "../hooks/core/AnimationControllerImpl_SetGameObjectIdToCharaControl.h"
#include "../hooks/player/CallComradeCommand.h"
#include "../hooks/player/FalseRadioResponseCommand.h"
#include "../hooks/player/Player2Impl_CallVoice.h"
#include "../hooks/buddy/BattleGearArmory.h"
#include "../hooks/buddy/BattleGearEffects.h"
#include "../hooks/buddy/BattleGearWeapons.h"
#include "../hooks/soldier/NoticeAiImpl_StepRadio.h"
#include "../hooks/soldier/SoldierCombatVoice.h"
#include "../hooks/soldier/SoldierIncomingVoice.h"
#include "../hooks/soldier/SoldierStinkVoice.h"
#include "../hooks/soldier/TppPermanentGimmickUnit_CallNoise.h"
#include "../hooks/soldier/RealizedSoldier2Impl_ExecuteSerially.h"
#include "../hooks/soldier/Soldier2CombatUtil_GrenadeUpdate.h"
#include "../hooks/soldier/SoldierThrowCommand.h"
#include "../hooks/ui/AnnounceLogHook.h"

void IdleActingActionImpl_CallVoice_ClearForReload();   // IdleActingActionImpl_CallVoice.cpp has no header

namespace
{
    std::atomic<std::uint32_t> g_epochMissionCode{ 0xFFFFFFFFu };
    std::atomic<bool>          g_finalizerRegistered{ false };
    std::atomic<bool>          g_finalizerFired{ false };

    void RunAllClears(std::uint32_t code)
    {
        outfit::Reset_PartsTypeChangeTracking();
        Unset_AllSoldierVoicePitch();
        SoldierAkObjIdMap::ClearCommandPostVoiceCents();
        Clear_AllCpPhaseDurationOverrides();
        Clear_SoldierCallSigns();
        Clear_UpdateOptCamoMappedIndexOverrides();
        EnemyLangId_ClearMapOverride();
        EnemyLangId_ClearBinoOverride();
        EnemyLangId_ClearAllMapOverridesForSoldier();
        EnemyLangId_ClearAllBinoOverridesForSoldier();
        Clear_LostHostagesTrap();
        Clear_LostHostageDiscovery();
        Clear_SahelanFovaOverride();
        Clear_EyeLampColor();
        SahelanPhaseForce::g_ForcedPhase.store(-1, std::memory_order_relaxed);
        SahelanPhaseForce::g_CurrentPhase.store(-1, std::memory_order_relaxed);
        Clear_HeartLightColor();
        Clear_InterrogationVoiceEvents();
        CallMenuItems::ClearForNewMission();
        RankLimitedItems::ClearForNewMission();
        CpRadioResponse::Clear();
        SoldierVoiceSwap_ClearHeld();
        GameObjectLife::ClearCaptures();
        CpRadioVariants_ClearForNewMission();
        CpInvestigation_ClearForNewMission();
        SoldierNotice::ClearPending();
        FakeSurrender::Clear();
        AntiAirGunRoute_Clear();
        SquatIdleRoute_Clear();
        HoldupSelfRelease_Clear();
        CallComrade::ClearForNewMission();
        SoldierBackupCall::ClearForNewMission();
        ClearOccasionalChatListOverride();
        MotherBaseSalutes::ClearForNewMission();
        VIPImportant_Clear();
        DeadBodyLabel_Clear();
        VIPHoldup_ClearForReload();
        VIPRadio_ClearForReload();
        Clear_AllSoldierIgnorePlayer();
        Clear_AllSoldierIgnoreVehicle();
        Clear_AllSoldierFactions();
        Clear_TargetCqcStance();
        Clear_HeadMarkEntityColours();
        Clear_AllHeadMarkStaffNames();
        Clear_AllMissionDeployWarnings();
        Set_FriendlyFire(false);
        outfit::RevertAdditionalMotionSwaps();
        outfit::ClearPlayerImpls();
        Clear_TppPickableOverrides();
        ClearAttachInDemo();
        outfit::ClearPreviewColors();
        outfit::ResetOutfitPaletteForMission();
        outfit::ResetChickenCapTracking();
        uipad::ReleaseOwnDisableRequests();
        mbTopMenuStickyHide::ClearForNewMission();
        ClearRetaliationSortie();
        RestoreAllGani();
        Reset_PlayerVoiceLiveSlot();
        FieldTaxi_ResetTaxiState();
        HeliSearchLight_Clear();
        HeliActionCore_Clear();
        HeliDoor_Clear();
        Clear_HeliSoundControllerOverrides();
        Clear_VehicleCannotRide();
        CustomVehicles::ForgetSpawnPicks();
        VehicleSortieRows::ForgetList();
        Clear_HeliPlayerMachineGunEnabled();
        SoldierRideHeli_Clear();
        PlayerAttackAction_Forget();
        LifeRecoveryCooldown_Reset();
        TimeCigaretteUi_ClearController();
        shell::RemoteMissile_ClearForNewMission();
        equip::ChargeShot_ClearForNewMission();
        EquipParam_BulletLanesNewMission();
        SoldierMesh::ClearForNewMission();
        PlayerMesh::ClearForNewMission();
        GameObjectMesh::ClearForNewMission();
        ChangeFovaOffScreen::Clear();
        UixUtilitySubViewport::ClearForNewMission();
        SoldierGrenadeIds_ClearForNewMission();
        SoldierThrow_ClearForNewMission();
        battleGearEffects::Drop();
        JeepGunEffects_Drop();
        battleGearWeapons::ClearScriptPicks();
#ifdef _DEBUG
        battleGearArmory::ClearScriptPick();
#endif
        Reset_AnnounceLogTyping();

        V_FrameWork::EmitMessage("Mission", "MissionStateReset", code);
    }

    void RunReloadClears()   // what soldiers, CPs and the heli were in the middle of; never what mod Lua set up
    {
        CpRadioResponse::Clear();
        SoldierVoiceSwap_ClearForReload();
        GameObjectLife::ClearCaptures();
        CpRadioVariants_ClearForNewMission();
        CpInvestigation_ClearForNewMission();
        SoldierNotice::ClearPending();
        FakeSurrender::Clear();
        AntiAirGunRoute_Clear();
        SquatIdleRoute_Clear();
        HoldupSelfRelease_ClearForReload();
        CallComrade::ClearForNewMission();
        SoldierBackupCall::ClearForNewMission();
        InterrogationVoiceEvent_ClearForReload();
        VIPHoldup_ClearForReload();
        VIPRadio_ClearForReload();
        SoldierCombatVoice_Reset();
        SoldierIncomingVoice_ClearForReload();
        SoldierStinkVoice_ClearForReload();
        IdleActingActionImpl_CallVoice_ClearForReload();
        TppPermanentGimmickUnit_ClearForReload();
        NoticeAiImpl_StepRadio_ClearForReload();
        CallComradeCommand::ClearForReload();
        FalseRadioResponseCommand::ClearForReload();
        Player2Impl_CallVoice_ClearForReload();
        Reset_PlayerVoiceLiveSlot();
        PlayerAttackAction_Forget();
        FieldTaxi_ResetTaxiState();
        HeliDoor_ClearForReload();
        HeliSoundController_ClearForReload();
        ChangeFovaOffScreen::Clear();
        SoldierThrow_ClearForNewMission();
        battleGearEffects::Drop();
        JeepGunEffects_Drop();
    }

    int __cdecl l_MissionStateFinalizer(lua_State* L)
    {
        UNREFERENCED_PARAMETER(L);

        const std::uint32_t code = MissionCodeGuard::GetCurrentMissionCode();

        g_finalizerFired.store(true, std::memory_order_relaxed);
        const std::uint32_t prev = g_epochMissionCode.exchange(code, std::memory_order_relaxed);
        RunAllClears(code);
        return 0;
    }
}

void MissionStateReset::EnsureFinalizerRegistered(lua_State* L)
{
    if (!L || g_finalizerRegistered.load(std::memory_order_relaxed))
        return;
    if (!ResolveLuaApi())
        return;

    const int top = g_lua_gettop(L);

    g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("Mission"));
    if (g_lua_type(L, -1) != LUA_TTABLE)
    {
        g_lua_settop(L, top);
        return;
    }

    g_lua_pushstring(L, const_cast<char*>("AddResidentFinalizer"));
    g_lua_gettable(L, -2);
    if (g_lua_type(L, -1) != LUA_TFUNCTION)
    {
        g_lua_settop(L, top);
        return;
    }

    g_lua_pushstring(L, const_cast<char*>("V_FrameWork_MissionStateReset"));   // the engine wants (name, function); the function alone registers nothing
    g_lua_pushcclosure(L, &l_MissionStateFinalizer, 0);
    const int rc = g_lua_pcall(L, 2, 0, 0);
    g_lua_settop(L, top);

    g_finalizerRegistered.store(true, std::memory_order_relaxed);

    if (rc != 0)
        LogWarn("MissionStateReset: AddResidentFinalizer failed (lua error %d), using mission-code poll", rc);
}

void MissionStateReset::PollMissionChange()
{
    const std::uint32_t code = MissionCodeGuard::GetCurrentMissionCode();
    DeployGuard::OnMissionCode(code);
    QuietCqc_EnforceMissionGuard();
    UniqueCharacterSuitSlot_EnforceMissionGuard();
    UavEmp_EnforceMissionGuard();
    SpRespawn_EnforceMissionGuard();
    EquipParam_EnforceFobVanilla();

    if (g_finalizerFired.load(std::memory_order_relaxed))
    {
        g_epochMissionCode.store(code, std::memory_order_relaxed);
        return;
    }

    std::uint32_t prev = g_epochMissionCode.load(std::memory_order_relaxed);
    if (code == prev)
        return;
    if (!g_epochMissionCode.compare_exchange_strong(prev, code, std::memory_order_relaxed))
        return;

    RunAllClears(code);
    g_finalizerRegistered.store(false, std::memory_order_relaxed);   // the engine refuses silently while its finalizers run: register again
}

void MissionStateReset::OnCheckpointReload()
{
    RunReloadClears();
}
