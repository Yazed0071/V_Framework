#include "pch.h"

#include <Windows.h>
#include <mutex>

#include "BuiltInModules.h"
#include "FeatureModule.h"

bool Install_SetLuaFunctions_Hook();
bool Uninstall_SetLuaFunctions_Hook();

bool Install_EquipBgTexture_Hook();
bool Uninstall_EquipBgTexture_Hook();

bool Install_MissionTelopBgTexture_Hook();
bool Uninstall_MissionTelopBgTexture_Hook();

bool Install_LoadingSplash_Hook();
bool Uninstall_LoadingSplash_Hook();

bool Install_GameOverSplash_Hook();
bool Uninstall_GameOverSplash_Hook();

bool Install_State_StandHoldupCancelLookToPlayer_Hook(HMODULE hGame);
bool Uninstall_State_StandHoldupCancelLookToPlayer_Hook();

bool Install_CautionStepNormalTimerHook();
bool Uninstall_CautionStepNormalTimerHook();

bool Install_CpAntiAir_Hook();
bool Uninstall_CpAntiAir_Hook();

bool Install_ChangeLocationMenu_Hook();
bool Uninstall_ChangeLocationMenu_Hook();

bool Install_PhotoAdditionalText_Hook();
bool Uninstall_PhotoAdditionalText_Hook();

bool Install_RealizedSahelanFova_Hook();
bool Uninstall_RealizedSahelanFova_Hook();

bool Install_SoldierHairFova_Hook();
bool Uninstall_SoldierHairFova_Hook();

bool Install_PlayerVoiceFpk_Hook();
bool Uninstall_PlayerVoiceFpk_Hook();

bool Install_State_EnterDownHoldupForceVoice_Hook();
bool Uninstall_State_EnterDownHoldupForceVoice_Hook();

bool Install_VIPSleepFaint_Hook();
bool Uninstall_VIPSleepFaint_Hook();

bool Install_VIPHoldup_Hook();
bool Uninstall_VIPHoldup_Hook();

bool Install_VIPSoundRecovery_Hook();
bool Uninstall_VIPSoundRecovery_Hook();

bool Install_VIPRadio_Hook();
bool Uninstall_VIPRadio_Hook();

bool Install_HoldUpReactionCowardlyReactions_Hook();
bool Uninstall_HoldUpReactionCowardlyReactions_Hook();

bool Install_CallSignExtra_Hook();
bool Uninstall_CallSignExtra_Hook();

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

bool Install_CustomRadioCassette_Hooks();
bool Uninstall_CustomRadioCassette_Hooks();

bool Install_Control_PostExternalEvent_Hook();
bool Uninstall_Control_PostExternalEvent_Hook();

bool Install_SoundSystem_BeginSoundSystem_Hook();
bool Uninstall_SoundSystem_BeginSoundSystem_Hook();

bool Install_TppPickableHooks();
bool Uninstall_TppPickableHooks();

bool Install_EquipIconFtexPath_Hook();
bool Uninstall_EquipIconFtexPath_Hook();

bool Install_MbDvcCustomPopup_Hook();
bool Uninstall_MbDvcCustomPopup_Hook();

bool Install_SoldierVoiceTypeQuery_Hook();
bool Uninstall_SoldierVoiceTypeQuery_Hook();

bool Install_VoicePitchOverride_Hook();
bool Uninstall_VoicePitchOverride_Hook();

bool Install_SetEyeLampColor_Hook();
bool Uninstall_SetEyeLampColor_Hook();

bool Install_GetGameObjectIdWithIndex();
bool Uninstall_GetGameObjectIdWithIndex();

bool Install_EnemyLangIdOverride_Hooks();
bool Uninstall_EnemyLangIdOverride_Hooks();

bool Install_BasicActionImpl_StateCrawlSideRoll_Hook();
bool Uninstall_BasicActionImpl_StateCrawlSideRoll_Hook();

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

bool Install_InitEquipHudData();
bool Uninstall_InitEquipHudData();


bool Install_OccasionalChatList_Hook();
bool Uninstall_OccasionalChatList_Hook();

bool Install_SoldierNotice_Hooks();
bool Uninstall_SoldierNotice_Hooks();

bool Install_FieldTaxiMenu();
bool Uninstall_FieldTaxiMenu();

bool Install_TimeCigaretteUi_Hook();
bool Uninstall_TimeCigaretteUi_Hook();

bool Install_HideBinocle_Hook();
bool Uninstall_HideBinocle_Hook();

bool Install_HeliSoundController_Hook();
bool Uninstall_HeliSoundController_Hook();
bool Install_AnnounceLogHook();
bool Uninstall_AnnounceLogHook();

bool Install_TornadoDualPatch();
void Uninstall_TornadoDualPatch();

bool Install_EquipCrossSetEquipItemPatch();
void Uninstall_EquipCrossSetEquipItemPatch();

bool Install_IsWeaponNoUseInPlaceActionPatch();
void Uninstall_IsWeaponNoUseInPlaceActionPatch();

bool Install_IsItemNoUsePatch();
void Uninstall_IsItemNoUsePatch();

bool Install_BarrierEffectLoadPatch();
void Uninstall_BarrierEffectLoadPatch();

bool Install_BarrierEffectSpawn();
void Uninstall_BarrierEffectSpawn();

bool Install_SupportAttackCrashGuard();
void Uninstall_SupportAttackCrashGuard();

namespace SoldierAkObjIdMap { bool Install(); bool Uninstall(); }

// Custom-outfit prep-summary feed (EN-15.4). Index resolvers + developed-bit.
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
    bool Install_OutfitHeadOption_Hook();
    void Uninstall_OutfitHeadOption_Hook();
    bool Install_OutfitCamoBonus_Hook();
    void Uninstall_OutfitCamoBonus_Hook();
    bool Install_OutfitGetCamoufValue_Hook();
    void Uninstall_OutfitGetCamoufValue_Hook();
}
namespace EquipDevelopAdd
{
    bool Install_TppMotherBaseManagement_EquipDevelopHooks();
    bool Uninstall_TppMotherBaseManagement_EquipDevelopHooks();
}
void EquipDevelop_InstallDevelopSyncHooks();
bool Install_TppEquip_RegisterConstant_Hook();
bool Uninstall_TppEquip_RegisterConstant_Hook();
bool Install_TppEquip_ReloadEquipIdTable_Hook();
bool Uninstall_TppEquip_ReloadEquipIdTable_Hook();
bool Install_TppEquip_ReloadEquipParameterTables2_Hook();
bool Uninstall_TppEquip_ReloadEquipParameterTables2_Hook();
bool Install_MotionLoader_ReceiverTypeHook();
void Uninstall_MotionLoader_ReceiverTypeHook();


namespace
{
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
            return Install_CallSignExtra_Hook();
        }

        void Uninstall() override
        {
            Uninstall_CallSignExtra_Hook();
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

    class SoldierNoticeModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "SoldierNotice"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_SoldierNotice_Hooks(); }
        void Uninstall() override { Uninstall_SoldierNotice_Hooks(); }
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

    class AnnounceLogModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "AnnounceLog"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_AnnounceLogHook(); }
        void Uninstall() override { Uninstall_AnnounceLogHook(); }
    };

    class TornadoDualPatchModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "TornadoDualPatch"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_TornadoDualPatch(); }
        void Uninstall() override { Uninstall_TornadoDualPatch(); }
    };

    class EquipCrossSetEquipItemModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "EquipCrossSetEquipItem"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_EquipCrossSetEquipItemPatch(); }
        void Uninstall() override { Uninstall_EquipCrossSetEquipItemPatch(); }
    };

    class IsWeaponNoUseInPlaceActionModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "IsWeaponNoUseInPlaceAction"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_IsWeaponNoUseInPlaceActionPatch(); }
        void Uninstall() override { Uninstall_IsWeaponNoUseInPlaceActionPatch(); }
    };

    class IsItemNoUseModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "IsItemNoUse"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_IsItemNoUsePatch(); }
        void Uninstall() override { Uninstall_IsItemNoUsePatch(); }
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

    class SupportAttackCrashGuardModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "SupportAttackCrashGuard"; }
        bool Install(HMODULE hGame) override { UNREFERENCED_PARAMETER(hGame); return Install_SupportAttackCrashGuard(); }
        void Uninstall() override { Uninstall_SupportAttackCrashGuard(); }
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

            EquipDevelop_InstallDevelopSyncHooks();

            const bool runtime = outfit::Install_OutfitRuntimeParts_Hooks();
            (void)runtime;
            return a && b;
        }
        void Uninstall() override
        {
            outfit::Uninstall_OutfitRuntimeParts_Hooks();
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
            (void)apply; (void)select; (void)list;
            return true;
        }
        void Uninstall() override
        {
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
            (void)head; (void)camo; (void)value;
            return true;
        }
        void Uninstall() override
        {
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
            return ok;
        }
        void Uninstall() override
        {
            Uninstall_TppEquip_ReloadEquipParameterTables2_Hook();
            Uninstall_MotionLoader_ReceiverTypeHook();
        }
    };
}

void RegisterBuiltInFeatureModules()
{
    static LuaBridgeModule s_LuaBridgeModule;
    static EquipBgTextureModule s_EquipBgTextureModule;
    static MissionTelopBgTextureModule s_MissionTelopBgTextureModule;
    static LoadingSplashModule s_LoadingSplashModule;
    static GameOverSplashModule s_GameOverSplashModule;
    static HoldupCancelLookToPlayerModule s_HoldupCancelLookToPlayerModule;
    static CautionTimerModule s_CautionTimerModule;
    static CpAntiAirModule s_CpAntiAirModule;
    static ChangeLocationMenuModule s_ChangeLocationMenuModule;
    static PhotoAdditionalTextModule s_PhotoAdditionalTextModule;
    static RealizedSahelanFovaModule s_RealizedSahelanFovaModule;
    static SoldierHairFovaModule s_SoldierHairFovaModule;
    static PlayerVoiceFpkModule s_PlayerVoiceFpkModule;
    static EnterDownHoldupForceVoiceModule s_EnterDownHoldupForceVoiceModule;
    static VIPSleepFaintModule s_VIPSleepFaintModule;
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
    static CassetteTapeSetCurrentAlbumModule s_CassetteTapeSetCurrentAlbumModule;
    static CustomRadioCassetteModule s_CustomRadioCassetteModule;
    static CustomTapeLongFilenameModule s_CustomTapeLongFilenameModule;
    static SoundSystemBeginModule s_SoundSystemBeginModule;
    static TppPickableModule s_TppPickableModule;
    static EquipIconFtexPathModule s_EquipIconFtexPathModule;
    static MbDvcCustomPopupModule s_MbDvcCustomPopupModule;
    static SoldierVoiceTypeQueryModule s_SoldierVoiceTypeQueryModule;
    static VoicePitchOverrideModule s_VoicePitchOverrideModule;
    static SoldierAkObjIdMapModule s_SoldierAkObjIdMapModule;
    static SetEyeLampColorModule s_SetEyeLampColorModule;
    static GetGameObjectIdWithIndex s_GetGameObjectIdWithIndex;
    static EnemyLangIdOverrideModule s_EnemyLangIdOverrideModule;
    static CrawlSideRollModule s_CrawlSideRollModule;
    static WormholeNearDeathWarpModule s_WormholeNearDeathWarpModule;
    static PlayerLockPickModule s_PlayerLockPickModule;
    static SubtitlesEventMessageModule s_SubtitlesEventMessageModule;
    static PhaseSneakAiImpl_PreUpdateModule s_PhaseSneakAiImpl_PreUpdateModule;
    static MissionEmergencyModule s_MissionEmergencyModule;
    static ShowMissionIconModule s_ShowMissionIconModule;
    static GameObjectSendCommandModule s_GameObjectSendCommandModule;
    static InitEquipHudDataModule s_InitEquipHudDataModule;
    static OccasionalChatListModule s_OccasionalChatListModule;
    static SoldierNoticeModule s_SoldierNoticeModule;
    static FieldTaxiMenuModule s_FieldTaxiMenuModule;
    static TimeCigaretteUiModule s_TimeCigaretteUiModule;
    static HideBinocleModule s_HideBinocleModule;
    static HeliSoundControllerModule s_HeliSoundControllerModule;
    static AnnounceLogModule s_AnnounceLogModule;
    static TornadoDualPatchModule s_TornadoDualPatchModule;
    static EquipCrossSetEquipItemModule s_EquipCrossSetEquipItemModule;
    static IsWeaponNoUseInPlaceActionModule s_IsWeaponNoUseInPlaceActionModule;
    static IsItemNoUseModule s_IsItemNoUseModule;
    static BarrierEffectLoadModule s_BarrierEffectLoadModule;
    static BarrierEffectSpawnModule s_BarrierEffectSpawnModule;
    static SupportAttackCrashGuardModule s_SupportAttackCrashGuardModule;
    static PlayerOutfitCoreModule s_PlayerOutfitCoreModule;
    static PlayerOutfitEquipModule s_PlayerOutfitEquipModule;
    static PlayerOutfitExtrasModule s_PlayerOutfitExtrasModule;
    static TppEquipConstInjectModule s_TppEquipConstInjectModule;
    static EquipIdTableOverflowModule s_EquipIdTableOverflowModule;
    static GunBasicInjectModule s_GunBasicInjectModule;

    static std::once_flag s_Once;
    std::call_once(s_Once, []()
        {
            FeatureModuleRegistry::Instance().Register(&s_LuaBridgeModule);
            FeatureModuleRegistry::Instance().Register(&s_EquipBgTextureModule);
            FeatureModuleRegistry::Instance().Register(&s_MissionTelopBgTextureModule);
            FeatureModuleRegistry::Instance().Register(&s_LoadingSplashModule);
            FeatureModuleRegistry::Instance().Register(&s_GameOverSplashModule);
            FeatureModuleRegistry::Instance().Register(&s_HoldupCancelLookToPlayerModule);
            FeatureModuleRegistry::Instance().Register(&s_CautionTimerModule);
            FeatureModuleRegistry::Instance().Register(&s_CpAntiAirModule);
            FeatureModuleRegistry::Instance().Register(&s_ChangeLocationMenuModule);
            FeatureModuleRegistry::Instance().Register(&s_PhotoAdditionalTextModule);
            FeatureModuleRegistry::Instance().Register(&s_RealizedSahelanFovaModule);
            FeatureModuleRegistry::Instance().Register(&s_SoldierHairFovaModule);
            FeatureModuleRegistry::Instance().Register(&s_PlayerVoiceFpkModule);
            FeatureModuleRegistry::Instance().Register(&s_EnterDownHoldupForceVoiceModule);
            FeatureModuleRegistry::Instance().Register(&s_VIPSleepFaintModule);
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
            FeatureModuleRegistry::Instance().Register(&s_CassetteTapeSetCurrentAlbumModule);
            FeatureModuleRegistry::Instance().Register(&s_CustomRadioCassetteModule);
            FeatureModuleRegistry::Instance().Register(&s_CustomTapeLongFilenameModule);
            FeatureModuleRegistry::Instance().Register(&s_SoundSystemBeginModule);
            FeatureModuleRegistry::Instance().Register(&s_TppPickableModule);
            FeatureModuleRegistry::Instance().Register(&s_EquipIconFtexPathModule);
            FeatureModuleRegistry::Instance().Register(&s_MbDvcCustomPopupModule);
            FeatureModuleRegistry::Instance().Register(&s_SoldierAkObjIdMapModule);
            FeatureModuleRegistry::Instance().Register(&s_SoldierVoiceTypeQueryModule);
            FeatureModuleRegistry::Instance().Register(&s_VoicePitchOverrideModule);
            FeatureModuleRegistry::Instance().Register(&s_SetEyeLampColorModule);
            FeatureModuleRegistry::Instance().Register(&s_GetGameObjectIdWithIndex);
            FeatureModuleRegistry::Instance().Register(&s_EnemyLangIdOverrideModule);
            FeatureModuleRegistry::Instance().Register(&s_CrawlSideRollModule);
            FeatureModuleRegistry::Instance().Register(&s_WormholeNearDeathWarpModule);
            FeatureModuleRegistry::Instance().Register(&s_PlayerLockPickModule);
            FeatureModuleRegistry::Instance().Register(&s_PhaseSneakAiImpl_PreUpdateModule);
            FeatureModuleRegistry::Instance().Register(&s_MissionEmergencyModule);
            FeatureModuleRegistry::Instance().Register(&s_ShowMissionIconModule);
            FeatureModuleRegistry::Instance().Register(&s_GameObjectSendCommandModule);
            FeatureModuleRegistry::Instance().Register(&s_InitEquipHudDataModule);
            FeatureModuleRegistry::Instance().Register(&s_OccasionalChatListModule);
            FeatureModuleRegistry::Instance().Register(&s_SoldierNoticeModule);
            FeatureModuleRegistry::Instance().Register(&s_FieldTaxiMenuModule);
            FeatureModuleRegistry::Instance().Register(&s_TimeCigaretteUiModule);
            FeatureModuleRegistry::Instance().Register(&s_HideBinocleModule);
            FeatureModuleRegistry::Instance().Register(&s_HeliSoundControllerModule);
            FeatureModuleRegistry::Instance().Register(&s_AnnounceLogModule);
            FeatureModuleRegistry::Instance().Register(&s_TornadoDualPatchModule);
            FeatureModuleRegistry::Instance().Register(&s_EquipCrossSetEquipItemModule);
            FeatureModuleRegistry::Instance().Register(&s_IsWeaponNoUseInPlaceActionModule);
            FeatureModuleRegistry::Instance().Register(&s_IsItemNoUseModule);
            FeatureModuleRegistry::Instance().Register(&s_SupportAttackCrashGuardModule);
            FeatureModuleRegistry::Instance().Register(&s_BarrierEffectLoadModule);
            FeatureModuleRegistry::Instance().Register(&s_BarrierEffectSpawnModule);
            FeatureModuleRegistry::Instance().Register(&s_PlayerOutfitCoreModule);
            FeatureModuleRegistry::Instance().Register(&s_PlayerOutfitEquipModule);
            FeatureModuleRegistry::Instance().Register(&s_PlayerOutfitExtrasModule);
            FeatureModuleRegistry::Instance().Register(&s_TppEquipConstInjectModule);
            FeatureModuleRegistry::Instance().Register(&s_EquipIdTableOverflowModule);
            FeatureModuleRegistry::Instance().Register(&s_GunBasicInjectModule);
        });
}
