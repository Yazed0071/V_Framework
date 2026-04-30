#include "pch.h"

#include <Windows.h>
#include <mutex>

#include "BuiltInModules.h"
#include "FeatureModule.h"
#include "EquipIdTable_AddToEquipIdTable.h"
#include "../hooks/outfit/OutfitEquippedState.h"
#include "../hooks/outfit/OutfitCommit.h"
#include "../hooks/outfit/OutfitRuntimeParts.h"
#include "../hooks/outfit/OutfitCamoBonus.h"
#include "../hooks/outfit/OutfitGetCamoufValue.h"
#include "../hooks/outfit/OutfitListInject.h"
#include "../hooks/outfit/OutfitFv2Paths.h"
#include "../hooks/outfit/OutfitHeadOption.h"
#include "../hooks/outfit/OutfitItemSelector.h"
#include "../hooks/outfit/OutfitSupplyDropSetup.h"
#include "../hooks/outfit/OutfitSuitConditionApply.h"
#include "../hooks/outfit/OutfitSupplyDropPickup.h"
#include "../hooks/outfit/OutfitUniformsRow.h"

bool Install_CustomTapeOwnership_Hooks();
bool Uninstall_CustomTapeOwnership_Hooks();

bool Install_SetLuaFunctions_Hook();
bool Uninstall_SetLuaFunctions_Hook();

bool Install_UiTextureOverrides_Hook();
bool Uninstall_UiTextureOverrides_Hook();

bool Install_State_StandHoldupCancelLookToPlayer_Hook(HMODULE hGame);
bool Uninstall_State_StandHoldupCancelLookToPlayer_Hook();

bool Install_CautionStepNormalTimerHook();
bool Uninstall_CautionStepNormalTimerHook();

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

bool Install_ConvertParameterIdLogger_Hook();
bool Uninstall_ConvertParameterIdLogger_Hook();

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

bool Install_SoundSystem_BeginSoundSystem_Hook();
bool Uninstall_SoundSystem_BeginSoundSystem_Hook();

bool Install_SoundMusicPlayer_SetupMusicInfos_Hook();
bool Uninstall_SoundMusicPlayer_SetupMusicInfos_Hook();

bool Install_CassetteTapeSetCurrentAlbum_Hook();
bool Uninstall_CassetteTapeSetCurrentAlbum_Hook();

bool Install_TppPickableHooks();
bool Uninstall_TppPickableHooks();

bool Install_EquipParameterTablesImpl_ReloadEquipParameterTablesImpl2_Hook();
bool Uninstall_EquipParameterTablesImpl_ReloadEquipParameterTablesImpl2_Hook();

namespace EquipIdTableAdd
{
    bool Install_EquipIdTableImpl_ReloadEquipIdTable_Hook();
    bool Uninstall_EquipIdTableImpl_ReloadEquipIdTable_Hook();
}

namespace EquipDevelopAdd
{
    bool Install_TppMotherBaseManagement_EquipDevelopHooks();
    bool Uninstall_TppMotherBaseManagement_EquipDevelopHooks();
}


namespace SupportWeaponType
{
    bool Install_EquipIdTableImpl_GetSupportWeaponTypeId_Hook();
    bool Uninstall_EquipIdTableImpl_GetSupportWeaponTypeId_Hook();
}

namespace DeclareAMs
{
    bool Install_DeclareAMs_Hook();
    bool Uninstall_DeclareAMs_Hook();
}

namespace EquipParams
{
    bool Install_EquipParameterTablesImpl_ReloadEquipParameterTablesImpl2_Hook();
    bool Uninstall_EquipParameterTablesImpl_ReloadEquipParameterTablesImpl2_Hook();
}

bool Install_EquipIconFtexPath_Hook();
bool Uninstall_EquipIconFtexPath_Hook();

// Player custom-outfit subsystem — Phase-2/3/4 hooks live in
// src/hooks/outfit/. The Phase-5 cleanup removed the legacy
// player-folder hook decls (PlayerPartsPath, PlayerSuitResolver,
// ItemSelectorSuitCommit, MissionPrepCommit, PlayerFaceFovaGate,
// PlayerFaceEquipRepair, CharacterSelectorPreserve, CurrentSuitQuery,
// SuitVariant, MissionPrepUpdateLoadMark) along with their .cpp/.h
// files. Outfit functionality flows entirely through the new modules.

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

    class UiTextureOverridesModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "UiTextureOverrides";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_UiTextureOverrides_Hook();
        }

        void Uninstall() override
        {
            Uninstall_UiTextureOverrides_Hook();
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

    class ConvertParameterIdLoggerModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "ConvertParameterIdLogger";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_ConvertParameterIdLogger_Hook();
        }

        void Uninstall() override
        {
            Uninstall_ConvertParameterIdLogger_Hook();
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

    class CustomTapesModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "CustomTapes";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_SoundMusicPlayer_SetupMusicInfos_Hook();
        }

        void Uninstall() override
        {
            Uninstall_SoundMusicPlayer_SetupMusicInfos_Hook();
        }
    };

    class CustomTapeOwnershipModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "CustomTapeOwnership";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_CustomTapeOwnership_Hooks();
        }

        void Uninstall() override
        {
            Uninstall_CustomTapeOwnership_Hooks();
        }
    };

    class CassetteTapeSetCurrentAlbumModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "CassetteTapeSetCurrentAlbum";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_CassetteTapeSetCurrentAlbum_Hook();
        }

        void Uninstall() override
        {
            Uninstall_CassetteTapeSetCurrentAlbum_Hook();
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
    class EquipParameterTablesReloadModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "EquipParameterTablesReload";
        }
        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return Install_EquipParameterTablesImpl_ReloadEquipParameterTablesImpl2_Hook();
        }
        void Uninstall() override
        {
            Uninstall_EquipParameterTablesImpl_ReloadEquipParameterTablesImpl2_Hook();
        }
    };
    class EquipIdTableReloadModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "EquipIdTableReload";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return EquipIdTableAdd::Install_EquipIdTableImpl_ReloadEquipIdTable_Hook();
        }

        void Uninstall() override
        {
            EquipIdTableAdd::Uninstall_EquipIdTableImpl_ReloadEquipIdTable_Hook();
        }
    };
    class EquipDevelopReloadModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "EquipDevelopReload";
        }
        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return EquipDevelopAdd::Install_TppMotherBaseManagement_EquipDevelopHooks();
        }
        void Uninstall() override
        {
            EquipDevelopAdd::Uninstall_TppMotherBaseManagement_EquipDevelopHooks();
        }
    };
    class SetSupportWeaponTypeModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "SetSupportWeaponType";
        }
        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return SupportWeaponType::Install_EquipIdTableImpl_GetSupportWeaponTypeId_Hook();
        }
        void Uninstall() override
        {
            SupportWeaponType::Uninstall_EquipIdTableImpl_GetSupportWeaponTypeId_Hook();
        }
    };
    class DeclareAMsModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "DeclareAMs";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return DeclareAMs::Install_DeclareAMs_Hook();
        }

        void Uninstall() override
        {
            DeclareAMs::Uninstall_DeclareAMs_Hook();
        }
    };
    class EquipParamsModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override
        {
            return "EquipParams";
        }

        bool Install(HMODULE hGame) override
        {
            UNREFERENCED_PARAMETER(hGame);
            return EquipParams::Install_EquipParameterTablesImpl_ReloadEquipParameterTablesImpl2_Hook();
        }

        void Uninstall() override
        {
            EquipParams::Uninstall_EquipParameterTablesImpl_ReloadEquipParameterTablesImpl2_Hook();
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

    // ================================================================
    // Player custom-outfit subsystem (Phase 2/3/4)
    // ================================================================

    // Phase-2 outfit core. Owns:
    //   - OutfitEquippedState (IsEquipDeveloped + GetEquipIdFromLoadoutInfo)
    //   - OutfitCommit (Player2UtilityImpl::RequestToChange...)
    //   - OutfitRuntimeParts (5 path loaders)
    class PlayerOutfitCoreModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "PlayerOutfitCore"; }

        bool Install(HMODULE) override
        {
            const bool eq      = outfit::Install_OutfitEquippedState_Hooks();
            const bool commit  = outfit::Install_OutfitCommit_Hook();
            const bool runtime = outfit::Install_OutfitRuntimeParts_Hooks();

            // Bonus-camo pin (camoBonusType). Optional polish — if the
            // hook fails to install (e.g. ExecSuitCorrect address not
            // resolved on a future build), the rest of the core still
            // works; outfits with camoBonusType set just won't pin.
            const bool camoBonus = outfit::Install_OutfitCamoBonus_Hook();
            (void)camoBonus;

            // Per-outfit unique camo bonus row (camoBonusValues).
            // Companion to camoBonus — intercepts virtual camo-type ids
            // in GetCamoufValue. Optional same as above.
            const bool camoVal = outfit::Install_OutfitGetCamoufValue_Hook();
            (void)camoVal;

            return eq && commit && runtime;
        }

        void Uninstall() override
        {
            outfit::Uninstall_OutfitGetCamoufValue_Hook();
            outfit::Uninstall_OutfitCamoBonus_Hook();
            outfit::Uninstall_OutfitRuntimeParts_Hooks();
            outfit::Uninstall_OutfitCommit_Hook();
            outfit::Uninstall_OutfitEquippedState_Hooks();
        }
    };

    // Phase-3 outfit UI module. Owns:
    //   - OutfitListInject  (UNIFORMS list-builder wrap, custom row append)
    //   - OutfitFv2Paths    (LoadPlayerCamoFv2 + LoadPlayerSnakeBlackDiamondFv2)
    //   - OutfitHeadOption  (IsEnableCurrentHeadOption gate — submenu list
    //                        build deferred per documented limitation)
    //
    // Independent from PlayerOutfitCore so a partial Phase-3 install
    // failure (e.g. one of the FV2 addresses not resolved on a
    // future build) doesn't take down the core.
    class PlayerOutfitUIModule final : public IFeatureModule
    {
    public:
        const char* GetName() const override { return "PlayerOutfitUI"; }

        bool Install(HMODULE) override
        {
            const bool list       = outfit::Install_OutfitListInject_Hook();
            const bool fv2        = outfit::Install_OutfitFv2Paths_Hooks();
            const bool head       = outfit::Install_OutfitHeadOption_Hook();
            const bool sel        = outfit::Install_OutfitItemSelector_Hook();
            const bool supply     = outfit::Install_OutfitSupplyDropSetup_Hook();
            const bool suitCond   = outfit::Install_OutfitSuitConditionApply_Hook();
            const bool sdPickup   = outfit::Install_OutfitSupplyDropPickup_Hook();
            const bool uniRow     = outfit::Install_OutfitUniformsRow_Hook();

            // List-inject + ItemSelector together make custom outfits
            // selectable end-to-end (the click → pending-developId →
            // commit-rewrite chain). FV2, head-option gate, supply-
            // drop short-circuit, suit-condition apply, supply-drop
            // pickup force-reload, and the UNIFORMS-row name override
            // are nice-to-have polish.
            (void)fv2; (void)head; (void)supply; (void)suitCond;
            (void)sdPickup; (void)uniRow;
            return list && sel;
        }

        void Uninstall() override
        {
            outfit::Uninstall_OutfitUniformsRow_Hook();
            outfit::Uninstall_OutfitSupplyDropPickup_Hook();
            outfit::Uninstall_OutfitSuitConditionApply_Hook();
            outfit::Uninstall_OutfitSupplyDropSetup_Hook();
            outfit::Uninstall_OutfitItemSelector_Hook();
            outfit::Uninstall_OutfitHeadOption_Hook();
            outfit::Uninstall_OutfitFv2Paths_Hooks();
            outfit::Uninstall_OutfitListInject_Hook();
        }
    };
}

void RegisterBuiltInFeatureModules()
{
    static LuaBridgeModule s_LuaBridgeModule;
    static EquipParamsModule s_EquipParamsModule;
    static EquipParameterTablesReloadModule s_EquipParameterTablesReloadModule;
    static EquipIdTableReloadModule s_EquipIdTableReloadModule;
    static SetSupportWeaponTypeModule s_SetSupportWeaponTypeModule;
    static EquipDevelopReloadModule s_EquipDevelopReloadModule;
    static UiTextureOverridesModule s_UiTextureOverridesModule;
    static DeclareAMsModule s_DeclareAMsModule;
    static HoldupCancelLookToPlayerModule s_HoldupCancelLookToPlayerModule;
    static CautionTimerModule s_CautionTimerModule;
    static PlayerVoiceFpkModule s_PlayerVoiceFpkModule;
    static EnterDownHoldupForceVoiceModule s_EnterDownHoldupForceVoiceModule;
    static VIPSleepFaintModule s_VIPSleepFaintModule;
    static VIPHoldupModule s_VIPHoldupModule;
    static VIPSoundRecoveryModule s_VIPSoundRecoveryModule;
    static VIPRadioModule s_VIPRadioModule;
    static ConvertParameterIdLoggerModule s_ConvertParameterIdLoggerModule;
    static HoldUpReactionCowardlyReactionsModule s_HoldUpReactionCowardlyReactionsModule;
    static PerSoldierCallSignOverrideModule s_PerSoldierCallSignOverrideModule;
    static LostHostageModule s_LostHostageModule;
    static UpdateOptCamoModule s_UpdateOptCamoModule;
    static CassetteTapePlayHookModule s_CassetteTapePlayHookModule;
    static SoundSystemBeginModule s_SoundSystemBeginModule;
    static CustomTapesModule s_CustomTapesModule;
    static CustomTapeOwnershipModule s_CustomTapeOwnershipModule;
    static CassetteTapeSetCurrentAlbumModule s_CassetteTapeSetCurrentAlbumModule;
    static TppPickableModule s_TppPickableModule;
    static EquipIconFtexPathModule s_EquipIconFtexPathModule;

    // Outfit subsystem (Phase 2/3/4 — replaces the legacy 4-module
    // bundle that lived here through Phase 4).
    static PlayerOutfitCoreModule             s_PlayerOutfitCoreModule;
    static PlayerOutfitUIModule               s_PlayerOutfitUIModule;

    static std::once_flag s_Once;
    std::call_once(s_Once, []()
        {
            FeatureModuleRegistry::Instance().Register(&s_EquipDevelopReloadModule);
            FeatureModuleRegistry::Instance().Register(&s_EquipParamsModule);
            FeatureModuleRegistry::Instance().Register(&s_CustomTapesModule);
            FeatureModuleRegistry::Instance().Register(&s_LuaBridgeModule);
            FeatureModuleRegistry::Instance().Register(&s_EquipParameterTablesReloadModule);
            FeatureModuleRegistry::Instance().Register(&s_EquipIdTableReloadModule);
            FeatureModuleRegistry::Instance().Register(&s_DeclareAMsModule);
            FeatureModuleRegistry::Instance().Register(&s_SetSupportWeaponTypeModule);
            FeatureModuleRegistry::Instance().Register(&s_UiTextureOverridesModule);
            FeatureModuleRegistry::Instance().Register(&s_HoldupCancelLookToPlayerModule);
            FeatureModuleRegistry::Instance().Register(&s_CautionTimerModule);
            FeatureModuleRegistry::Instance().Register(&s_PlayerVoiceFpkModule);
            FeatureModuleRegistry::Instance().Register(&s_EnterDownHoldupForceVoiceModule);
            FeatureModuleRegistry::Instance().Register(&s_VIPSleepFaintModule);
            FeatureModuleRegistry::Instance().Register(&s_VIPHoldupModule);
            FeatureModuleRegistry::Instance().Register(&s_VIPSoundRecoveryModule);
            FeatureModuleRegistry::Instance().Register(&s_VIPRadioModule);
            FeatureModuleRegistry::Instance().Register(&s_ConvertParameterIdLoggerModule);
            FeatureModuleRegistry::Instance().Register(&s_HoldUpReactionCowardlyReactionsModule);
            FeatureModuleRegistry::Instance().Register(&s_PerSoldierCallSignOverrideModule);
            FeatureModuleRegistry::Instance().Register(&s_LostHostageModule);
            FeatureModuleRegistry::Instance().Register(&s_UpdateOptCamoModule);
            FeatureModuleRegistry::Instance().Register(&s_CassetteTapePlayHookModule);
            FeatureModuleRegistry::Instance().Register(&s_SoundSystemBeginModule);
            FeatureModuleRegistry::Instance().Register(&s_CustomTapeOwnershipModule);
            FeatureModuleRegistry::Instance().Register(&s_CassetteTapeSetCurrentAlbumModule);
            FeatureModuleRegistry::Instance().Register(&s_TppPickableModule);
            FeatureModuleRegistry::Instance().Register(&s_EquipIconFtexPathModule);

            // Phase-2 outfit core. Installs the new clean hooks:
            //   - OutfitEquippedState  (IsEquipDeveloped + GetEquipIdFromLoadoutInfo)
            //   - OutfitCommit         (Player2UtilityImpl::RequestToChange...)
            //   - OutfitRuntimeParts   (5 path loaders)
            FeatureModuleRegistry::Instance().Register(&s_PlayerOutfitCoreModule);

            // Phase-3 outfit UI. Must register AFTER PlayerOutfitCore
            // because the list-inject hook calls AddListSuit which
            // expects the equipped-state hooks to be live for proper
            // row state.
            //   - OutfitListInject     (SetupPrefabListElement append)
            //   - OutfitFv2Paths       (LoadPlayerCamoFv2 + LoadPlayerSnakeBlackDiamondFv2)
            //   - OutfitHeadOption     (IsEnableCurrentHeadOption gate)
            FeatureModuleRegistry::Instance().Register(&s_PlayerOutfitUIModule);
        });
}