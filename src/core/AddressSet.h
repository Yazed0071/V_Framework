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
        uintptr_t GetIconFtexPath = 0;
        uintptr_t LoadingTipsEv_UpdateActPhase = 0;
        // Wwise/fox audio — used by SoldierRtpcHook for per-soldier RTPC control.
        uintptr_t AK_SoundEngine_SetRTPCValue = 0;   // AK::SoundEngine::SetRTPCValue (trampoline at 0x14033d520)
        uintptr_t Fox_Sd_ConvertParameterID = 0;     // fox::sd::ConvertParameterID (FNV-1 name hasher wrapper at 0x14032adf0)

        // Static EquipParameterTablesImpl instance (&PTR_PTR_142a711f0 in decomp).
        // First field is the vtable ptr. Subsystem pointers follow:
        //   impl+0x08 → gunBasic (0x202 rows × 12 B = 0x1818 bytes)
        //   impl+0x10 → receiver, +0x18 barrel, +0x20 magazine, etc.
        // SetGunBasic / SetEquipParameters do direct native-shadow writes via
        // this instance — they can't wait for ReloadEquipParameterTablesImpl2
        // to fire, because the boot reload happens before our DLL installs.
        uintptr_t EquipParameterTablesImpl_Instance = 0;

        // Stock EquipIdTableImpl::AddToEquipIdTable(lua_State*) — writes
        // partsPath/packPath/baseWeapon/type/block into the game's native
        // s_internalInfoList + DAT_142c20fb8/fc0 + DAT_142a70928 arrays.
        // Calling this directly with our queue's Lua table populates
        // native storage without waiting for ReloadEquipIdTable to fire
        // (which only happens at boot, before our DLL installs).
        uintptr_t EquipIdTableImpl_AddToEquipIdTable = 0;

        // Address of the native EquipIdTableImpl::s_internalInfoList parts-
        // path array (size 0x289 entries × 0x18 bytes). The hash field at
        // offset 0 of each entry is non-zero when the slot is populated by
        // vanilla. The framework's custom-equipId allocator reads this
        // table directly to find vanilla-free slots — vanilla MGSV's
        // TppEquipParts.lua runs BEFORE our DLL injects, so observing
        // AddToEquipIdTable calls misses vanilla's data; reading the
        // already-populated array is the only reliable way.
        uintptr_t EquipIdTableImpl_s_internalInfoList = 0;

        // ============================================================
        // Player custom-suit subsystem
        // ============================================================

        // Parts / FPK / sub-asset path loaders. Every custom suit-loading hook
        // intercepts these and returns a custom FoxPath when the requested
        // partsType falls in the custom range (0x40..0x7F).
        uintptr_t LoadPlayerPartsParts              = 0;  // body .parts
        uintptr_t LoadPlayerPartsFpk                = 0;  // body .fpk
        uintptr_t LoadPlayerCamoFpk                 = 0;  // camo pattern .fpk
        uintptr_t LoadPlayerSnakeBlackDiamondFpk    = 0;  // diamond-mark .fpk
        uintptr_t Player2BlockController_LoadPartsNew = 0; // LoadPartsPlayerInfo funnel
        uintptr_t UpdatePartsStatus                 = 0;  // head-option waterfall repair

        // DEPRECATED (Phase 1 false lead F1, Phase 5 confirmed unused).
        // No real retail function — left as 0 in EN. Do not hook.
        uintptr_t ResolveSuitToPartsType            = 0;

        // Mission-prep commit. Four-arg function at 0x14973DA60 is the hook
        // target; three-arg wrapper at 0x1462B6590 is invoked from our own
        // TriggerSilentSuitCommit.
        uintptr_t MissionPrep_RequestToChangePlayerPartsInMissionPreparationMode = 0;
        uintptr_t Player2UtilityImpl_CommitWrapper  = 0;

        // DEPRECATED (Phase 5). UI selection commit + supply drop +
        // character-selector preservation + FOVA gates were the legacy
        // outfit subsystem's hooks; the new outfit core relies on
        // Player2UtilityImpl::RequestToChange directly. Kept here only
        // to preserve aggregate-init alignment of EN/JP address arrays.
        uintptr_t ItemSelectorCallbackImpl_DecideActMissionPreparationSetEquipMode = 0;
        uintptr_t ItemSelectorCallbackImpl_DecideActMotherBaseDeviceSupportDropMode = 0;
        uintptr_t SupplyDropSuitSetup               = 0;
        // SupplyCboxGameObjectImpl::RestoreRequestFromSVars —
        // called by ProcessLuaCommand on Lua hash 0xc1324e75 when the
        // player interacts with a delivered supply-drop crate. Reads
        // the saved loadout request (SupplyCboxRequest, lookup hash
        // 0xee90448b1d7e on the sub-object's vtable[0x18]) and tail-
        // calls vtable[0x18] on it to actually apply. Hooking here
        // catches the box-open moment so we can ForcePartsReload our
        // outfit when the vanilla apply silently no-ops on a custom
        // flowIndex.
        uintptr_t SupplyCboxGameObjectImpl_RestoreRequestFromSVars = 0;
        // Player2UtilityImpl::SetSuitAndHandConditionWithLoadoutInfo —
        // called by the supply-drop pickup pipeline to actually apply
        // the dropped suit to the player. Reads bytes from the
        // SupplyCboxLoadoutInfo struct (info[0]=partsType, info[1]=camo,
        // info[0xBC]=flags, info[0xC0]=playerType) and writes them
        // into Quark live player state. Hooking here lets us catch
        // the exact moment the supply-drop crate "consumes" and inject
        // our custom outfit's bytes for our flowIndex.
        uintptr_t Player2UtilityImpl_SetSuitAndHandConditionWithLoadoutInfo = 0;

        // Wrapper that calls SetSuit then runs loadout-apply pass via
        // Player2UtilityImpl::vtable[0x218]. For custom partsType (0x40..0x7F)
        // with the "suit-equip pattern" flags (bit 0 + bit 7, no slot bits
        // 2/3/4), the apply pass writes zeros to all weapon slots — which
        // is what makes the SORTIE PREP weapon slots show NONE while
        // wearing a custom suit. The framework hooks this function and
        // suppresses the apply pass when it detects that pattern, while
        // still calling SetSuit directly so the body change happens.
        // Vanilla weapon clicks reach this function with different flags
        // (slot bits set) and pass through to orig.
        uintptr_t Player2UtilityImpl_LoadoutApplyAfterSetSuit = 0;

        // tpp::gm::player::impl::Player2UtilityImpl::SetInitialConditionWithLoadoutInfo
        // (retail 0x1462C7670). The OTHER SetSuit-caller wrapper besides
        // FUN_1462C93F0. SetSuit's XREF list shows this function calls
        // SetSuit at 0x1462C769E (verified mgsvtpp.exe.c:5962899). The
        // supply-drop equip flow uses THIS function (not FUN_1462C93F0),
        // which is why the LoadoutApplyAfterSetSuit hook never fires for
        // supply-drop and the user reported "weapon icons disappear when
        // wearing custom suit."
        //
        // Function signature: void(this, info, char preserve).
        // Body does:
        //   1. SetSuit(this, info)                        // body change
        //   2. for each of 3 weapon slot entries:
        //        if (slot_keep_bit & info[0xBC]) == 0:    // not in keep mask
        //            if (preserve == 0):                  // not preserve mode
        //                CLEAR slot at lVar4+0x520+slot*2 = 0
        //                CLEAR slot at lVar4+0x548+slot*2 = 0
        //                SET   flag at lVar4+0x57E+slot   = 1
        //   3. additional camo/face/etc. writes
        //   4. notify QuarkSystem[0x130] vtable[0x68](info[0xB9])
        //
        // For custom outfit equip:
        //   info[0xBC] = 0x01 (suit-equip pattern, no slot keep bits)
        //   preserve = 0 (orig's caller passes 0 for fresh equips)
        //   → slots get cleared → weapon icons disappear in SORTIE PREP
        //
        // Fix: hook this function, detect custom partsType in info[0],
        // spoof preserve = 1 before calling orig. This skips the slot-
        // clearing branch (orig keeps existing slot data intact) while
        // still doing the body change via SetSuit. Vanilla equips with
        // valid slot data fall through with preserve unchanged.
        uintptr_t Player2UtilityImpl_SetInitialConditionWithLoadoutInfo = 0;

        // tpp::ui::menu::impl::CharacterSelectorCallbackImpl::ChangeDetailsWindowBuddySelect
        // (retail 0x14163E5F0). Updates the SORTIE PREP > SELECT CHARACTER >
        // UNIFORMS row when the user picks a buddy/character. Reads the
        // current partsType byte from a per-soldier array at
        // (param_1 + 0x9C70 + index*4), translates it via vtable[0x108]
        // on a translator object to a 64-bit string hash, writes the
        // hash to (param_1 + 0xA0D0), then calls Quark vtable[0x1E0]
        // setter with property hash 0x30A0D543E155 to display the text.
        //
        // For custom partsType (0x40..0x7F) the translator returns 0
        // → UI shows blank. The framework hooks this function and, after
        // orig runs, overwrites (param_1 + 0xA0D0) with the registered
        // outfit's `langEquipNameHash` and re-calls Quark's setter so
        // the UNIFORMS row shows the user's chosen lang-string instead.
        uintptr_t CharacterSelectorCallbackImpl_ChangeDetailsWindowBuddySelect = 0;

        // tpp::ui::menu::impl::CharacterSelectorCallbackImpl::OpenBuddySelect
        // (retail 0x14163ECD0). Companion to ChangeDetailsWindowBuddySelect
        // — fires when the buddy-select panel is INITIALLY opened (vs. the
        // Change function which fires only when the user moves the cursor
        // to a different row). Same UNIFORMS-row write path: reads
        // partsType from (this + 0x9C70 + slot*4) where `slot` is computed
        // as (this+0xA0 + this+0x9C) % this+0x94, calls vtable[0x108]
        // translator → buffer at (this + 0xA0D0), then setter
        // 0x30A0D543E155. Hooked alongside the Change function so the
        // initial UNIFORMS row text is correct when the panel first opens.
        uintptr_t CharacterSelectorCallbackImpl_OpenBuddySelect = 0;

        // Deep-hook targets used by OutfitListInject for the SORTIE PREP /
        // R&D suit list. These functions belong to two list-helper objects
        // reached via SetupPrefabListElement's `this`:
        //   - GetDevelopedSuitCount  (vtable[0x230] on this+0x50+0xAC8)
        //   - FillDevelopedFlowIxs   (vtable[0x240] on this+0x50+0xAC8)
        //   - GetSuitInfoTable       (vtable[0x718] on this+0x58)
        //
        // Originally captured at runtime on the first SetupPrefabListElement
        // fire (which only happens once the user opens the UNIFORMS menu).
        // That made R&D listings stale for custom outfits between develop-
        // completion and the user's first uniforms-menu visit. The function
        // body addresses are stable across runs (verified in multiple user
        // logs: 0x140F660C0 / 0x140F65F70 / 0x14024D330), so we hard-code
        // them here and hook directly at DLL init — R&D updates immediately
        // when a custom outfit develop finishes.
        uintptr_t SuitList_GetDevelopedCount    = 0;
        uintptr_t SuitList_FillDevelopedFlowIxs = 0;
        uintptr_t SuitList_GetSuitInfoTable     = 0;
        // SupplyCboxSystemImpl::Reset — VERIFIED 2026-04-26 as THE
        // pickup-time trigger. Fires when the box is consumed/cleared
        // post-pickup. Our hook in OutfitSupplyDropPickup consumes the
        // stash from OutfitSupplyDropSetup's confirm latch and drives
        // ForcePartsReload to equip the custom outfit. Also fires on
        // mission init / despawn — harmless because the stash is empty
        // outside an active confirm→pickup window.
        uintptr_t SupplyCboxSystemImpl_Reset = 0;
        // SupplyCboxActionPluginImpl phase-2 handler at retail
        // 0x1412A2F80 (FUN_1412a2f80 in mgsvtpp.exe.c:2402357).
        //
        // The action plugin's parent state machine has three phases
        // dispatched via ExecStateChangeImpl: phase 1 (chopper inbound /
        // monitoring) → FUN_1412a3ad0; phase 2 (pickup interaction
        // active) → FUN_1412a2f80; phase 3 → FUN_1412a3de0.
        //
        // Inside FUN_1412a2f80, switch is on (param_3 - 1) — decomp
        // case 9 = param_3 == 10 = the pickup-motion-progress check
        // that fires Lua hash 0x6c72b84d at 50% progress (line 2402782).
        // Phase 2 is ONLY entered when the player physically initiates
        // the pickup, so hooking this avoids the 7-second-premature
        // false-fire we got with the phase-1 handler.
        //
        // We trigger ForcePartsReload on the FIRST param_3==10 entry
        // (start of pickup motion). The ~500ms FoxPath load completes
        // mid-animation, masking the visual transition under the
        // player's bend-over-box pose — matches vanilla feel.
        uintptr_t SupplyCboxActionPluginImpl_StateHandler1 = 0;
        uintptr_t CharacterSelectorCallbackImpl_StoreCurrentCharacterSuitAndHeadPartsInfo = 0;
        uintptr_t ResourceTable_DoesNeedFaceFova           = 0;
        uintptr_t ResourceTable_DoesNeedFaceFovaForAvatar  = 0;

        // Camo system global pointer (not a function) — points to the
        // engine-side object whose vtable[1] accepts a Lua 117x82 table and
        // applies it to the runtime camo parameter system.
        uintptr_t CamoSystemObject                  = 0;

        // DEPRECATED (Phase 5). Legacy variant/head-option hooks —
        // Phase 1 false leads F4/F6. Replaced by the Phase-3
        // OutfitHeadOption gate + per-outfit headOptionEquipIds[]
        // and the OutfitListInject flow. Kept here only for align.
        uintptr_t GetSuitVariation                  = 0;
        uintptr_t HeadOptionTableLookup             = 0;
        uintptr_t HasHeadOptions                    = 0;
        uintptr_t HeadOptionIndexGetter             = 0;
        uintptr_t SuitCatalog_FindHeadOptionRow     = 0;
        uintptr_t FetchCurrentHeadOptionKey         = 0;
         
        // Mission-prep UI. UpdateLoadMark deprecated (Phase 1 F5).
        // GetSelectionNum unused since Phase 4. IsEnableCurrentHeadOption
        // is the active Phase-3 head-option gate target.
        uintptr_t MissionPrep_GetSelectionNum       = 0;
        uintptr_t MissionPrep_IsEnableCurrentHeadOption = 0;
        uintptr_t MissionPrep_UpdateLoadMark        = 0;

        // UI list / panel setup. SetupCharacterSlotSelectPrefabListElement
        // is unused. AddListSuit is the call target invoked from
        // Phase-3 OutfitListInject. IsEnableCurrentSuit + SetupEquipPanelParam
        // were legacy hook targets, now unused.
        uintptr_t SetupCharacterSlotSelectPrefabListElement = 0;
        uintptr_t AddListSuit                       = 0;
        uintptr_t IsEnableCurrentSuit               = 0;
        uintptr_t SetupEquipPanelParam              = 0;

        // Phase-2 incorrect attribution — kept here as a deprecated
        // pointer. SetInDecideOpen is NOT the UNIFORMS list builder.
        uintptr_t ItemSelectorCallbackImpl_SetInDecideOpen = 0;

        // Phase-3 verified UNIFORMS list builder. Contains the two
        // AddListSuit call sites (0x1416AA904, 0x1416AAEA4) that
        // append vanilla suit rows. Hooked to append our custom
        // outfits after the vanilla pass completes.
        // (verified mgsvtpp.exe_Addresses.txt:15790389).
        uintptr_t ItemSelectorCallbackImpl_SetupPrefabListElement = 0;

        // Phase-3 FV2 (face variant 2) loaders.
        uintptr_t LoadPlayerCamoFv2                = 0;  // 0x146863F80
        uintptr_t LoadPlayerSnakeBlackDiamondFv2   = 0;  // 0x146864C80

        // UI read-back. GetEquipIdFromLoadoutInfo + IsEquipDeveloped
        // are the Phase-2 OutfitEquippedState hook targets.
        // GetCurrentSuitFlowIndex + SetItemDetail are deprecated.
        // SendTrigger is used by other (non-outfit) subsystems.
        uintptr_t GetCurrentSuitFlowIndex           = 0;
        uintptr_t GetEquipIdFromLoadoutInfo         = 0;
        uintptr_t IsEquipDeveloped                  = 0;
        uintptr_t SetItemDetail                     = 0;
        uintptr_t SendTrigger                       = 0;
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
            0x145E62540ull, // GetIconFtexPath
            0x145ccfcc0ull, // LoadingTipsEv_UpdateActPhase (overrides 0x9d8/0x9e0 w/ DD logo)
            0x14033d520ull, // AK_SoundEngine_SetRTPCValue (thunk → AK::SoundEngine::SetRTPCValue)
            0x14032adf0ull, // Fox_Sd_ConvertParameterID (thunk → fox::sd::ConvertParameterID; RTPC/Switch/State name hash)
            0x142A711F0ull, // EquipParameterTablesImpl_Instance
            0x140A29730ull, // EquipIdTableImpl_AddToEquipIdTable
            0x142C20FB0ull, // EquipIdTableImpl_s_internalInfoList (parts-path array, 0x289 entries × 0x18 bytes)

            // ========= Player custom-suit subsystem =========
            0x146865F80ull, // LoadPlayerPartsParts
            0x146866C80ull, // LoadPlayerPartsFpk
            0x146864180ull, // LoadPlayerCamoFpk
            0x146864E30ull, // LoadPlayerSnakeBlackDiamondFpk
            0x1409B3B60ull, // Player2BlockController_LoadPartsNew
            0x1409CC380ull, // UpdatePartsStatus
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
            0x1416BF490ull, // SetupCharacterSlotSelectPrefabListElement
            0x1416A1AA0ull, // AddListSuit
            0x14A56BFA0ull, // IsEnableCurrentSuit
            0x1416C0690ull, // SetupEquipPanelParam
            // ALIGNMENT FIX 2026-04-26: positions 130..138 below
            // re-ordered to match the struct field declaration order.
            // Previously the EN array had SetInDecideOpen / Setup-
            // PrefabListElement / LoadPlayerCamoFv2 / LoadPlayerSnake-
            // BlackDiamondFv2 listed AFTER the GetCurrentSuitFlowIndex
            // ..SendTrigger group, while the struct declared them
            // before. Aggregate-init binds by position, so e.g.
            // gAddr.ItemSelectorCallbackImpl_SetupPrefabListElement
            // was silently resolving to 0x1416BB9C0 (GetEquipIdFromLoadoutInfo)
            // — every dependent hook landed on the wrong function.
            0x1416A7A30ull, // ItemSelectorCallbackImpl_SetInDecideOpen
            0x1416A9B80ull, // ItemSelectorCallbackImpl_SetupPrefabListElement
            0x146863F80ull, // LoadPlayerCamoFv2
            0x146864C80ull, // LoadPlayerSnakeBlackDiamondFv2
            0x140955C70ull, // GetCurrentSuitFlowIndex
            0x1416BB9C0ull, // GetEquipIdFromLoadoutInfo
            0x14951F860ull, // IsEquipDeveloped
            0x14A56E7F0ull, // SetItemDetail
            0x144B05380ull, // SendTrigger
        };

        return value;
    }

    inline const AddressSet& GetJapaneseAddressSet()
    {
        static const AddressSet value =
        {
            0x0ull, // AddCassetteTapeTrack
            0x0ull, // AddNoise
            0x0ull, // AddNoticeInfo
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
            0x0ull, // GetPlayingTrackId
            0x0ull, // GetQuarkSystemTable
            0x0ull, // GetTrackInfoByName
            0x0ull, // GetVoiceLanguage
            0x0ull, // GetVoiceParamWithCallSign
            0x0ull, // IsGotCassetteTapeTrack
            0x0ull, // KernelAllocAligned
            0x0ull, // LoadPlayerVoiceFpk
            0x0ull, // LoadingScreenOrGameOverSplash2
            0x0ull, // MusicManager_s_instance
            0x0ull, // PathHashCode
            0x0ull, // PauseMusicPlayer
            0x0ull, // PlayOrPauseSelectedTrack
            0x0ull, // RequestCorpse
            0x0ull, // ResumeMusicPlayer
            0x0ull, // SetCassetteTapeTrackNewFlag
            0x0ull, // SetCurrentAlbum
            0x0ull, // SetEquipBackgroundTexture
            0x0ull, // SetLuaFunctions
            0x0ull, // SetTextureName
            0x0ull, // SetupMusicInfos
            0x0ull, // SoundSystemCtor
            0x0ull, // StateRadio
            0x0ull, // StateRadioRequest
            0x0ull, // State_ComradeAction
            0x0ull, // State_EnterDownHoldup
            0x0ull, // State_EnterStandHoldup1
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
            0x0ull, // lua_getfield
            0x0ull, // lua_gettop
            0x0ull, // lua_isnumber
            0x0ull, // lua_isstring
            0x0ull, // lua_objlen
            0x0ull, // lua_pushboolean
            0x0ull, // lua_pushnumber
            0x0ull, // lua_rawgeti
            0x0ull, // lua_settop
            0x0ull, // lua_toboolean
            0x0ull, // lua_tointeger
            0x0ull, // lua_tolstring
            0x0ull, // lua_tonumber
            0x0ull, // lua_type
            0x0ull, // lua_pushstring
            0x0ull, // lua_createtable
            0x0ull, // lua_rawset
            0x0ull, // lua_settable
            0x0ull, // lua_pushnil
            0x0ull, // lua_next
            0x0ull, // lua_gettable
            0x0ull, // lua_pushvalue


			0x0ull, // RegisterConstantEquipIdHashTable
            0x0ull, // EquipParameterTablesImpl_ReloadEquipParameterTablesImpl2
			0x0ull, // EquipIdTableImpl_ReloadEquipIdTable
			0x0ull, // TppMotherBaseManagement_RegCstDev
			0x0ull, // TppMotherBaseManagement_RegFlwDev
			0x0ull, // EquipIdTableImpl_GetSupportWeaponTypeId
            0x0ull, // DeclareAMs
			0x0ull, // GetIconFtexPath
            0x0ull, // LoadingTipsEv_UpdateActPhase
            0x0ull, // AK_SoundEngine_SetRTPCValue
            0x0ull, // Fox_Sd_ConvertParameterID
            0x0ull, // EquipParameterTablesImpl_Instance
            0x0ull, // EquipIdTableImpl_AddToEquipIdTable
            0x0ull, // EquipIdTableImpl_s_internalInfoList

            // ========= Player custom-suit subsystem (JPN — unfilled) =========
            0x0ull, // LoadPlayerPartsParts
            0x0ull, // LoadPlayerPartsFpk
            0x0ull, // LoadPlayerCamoFpk
            0x0ull, // LoadPlayerSnakeBlackDiamondFpk
            0x0ull, // Player2BlockController_LoadPartsNew
            0x0ull, // UpdatePartsStatus
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
            0x0ull, // CharacterSelectorCallbackImpl_ChangeDetailsWindowBuddySelect
            0x0ull, // CharacterSelectorCallbackImpl_OpenBuddySelect
            0x0ull, // SuitList_GetDevelopedCount
            0x0ull, // SuitList_FillDevelopedFlowIxs
            0x0ull, // SuitList_GetSuitInfoTable
            0x0ull, // SupplyCboxSystemImpl_Reset
            0x0ull, // SupplyCboxActionPluginImpl_StateHandler1
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
            0x0ull, // SetupCharacterSlotSelectPrefabListElement
            0x0ull, // AddListSuit
            0x0ull, // IsEnableCurrentSuit
            0x0ull, // SetupEquipPanelParam
            // ALIGNMENT FIX 2026-04-26: positions 130..138 below
            // re-ordered to match the struct field declaration order
            // (mirror of EN array fix).
            0x0ull, // ItemSelectorCallbackImpl_SetInDecideOpen
            0x0ull, // ItemSelectorCallbackImpl_SetupPrefabListElement
            0x0ull, // LoadPlayerCamoFv2
            0x0ull, // LoadPlayerSnakeBlackDiamondFv2
            0x0ull, // GetCurrentSuitFlowIndex
            0x0ull, // GetEquipIdFromLoadoutInfo
            0x0ull, // IsEquipDeveloped
            0x0ull, // SetItemDetail
            0x0ull, // SendTrigger
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
