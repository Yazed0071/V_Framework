#include <pch.h>

#include <Windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

#include "AddressSet.h"
#include "CpRadioVariants.h"
#include "FoxHashes.h"
#include "GameClock.h"
#include "GetGameObjectIdWithIndex.h"
#include "HookUtils.h"
#include "InvestigateAiImpl_PreUpdate.h"
#include "MissionCodeGuard.h"
#include "FeatureSwitch.h"
#include "NoticeAiImpl_StepRadio.h"
#include "NoticeControllerImpl_CheckForceNotice.h"
#include "NoticeControllerImpl_CheckSightNoticePlayer.h"
#include "NoticeDownComradeAiImpl_StepPreRadio.h"
#include "PlayerAlertPhase.h"
#include "RadioControllerImpl_StepRadioWaitResponse.h"
#include "RevengeLevel.h"
#include "Soldier2SoundControllerImpl_CallVoiceWithoutSync.h"
#include "SoldierRealizedSlot.h"
#include "SoldierVoiceType.h"
#include "HoldupAiImpl_StepHoldupStand.h"
#include "TppPermanentGimmickUnit_CallNoise.h"
#include "UavLifeControllerImpl_SendStatusMessage.h"
#include "log.h"

namespace
{
    using ConvertSpeechLabel_t  = std::uint8_t(__fastcall*)(std::uint32_t label);
    using RequestRadioByCp_t    = std::uint16_t(__fastcall*)(std::uint8_t* radioService, std::uint32_t cp, std::uint32_t radioType,
                                                    std::uint32_t member, std::uint32_t label);
    using CallPart_t            = bool(__fastcall*)(std::uint8_t* speechHandler, std::uint8_t sequence);
    using ActivateChaff_t       = void(__fastcall*)(std::uint8_t* rangeAttack, const void* request, std::uint32_t requestId);
    using MortarState_t         = void(__fastcall*)(std::uint8_t* mortarAction, std::uint32_t slot, std::int32_t proc, void* data);
    using UnrealMortarState_t   = void(__fastcall*)(std::uint8_t* mortarAction, std::uint32_t soldier, std::int32_t proc);
    using HeliUnrealize_t       = void(__fastcall*)(std::uint8_t* heli, std::uint32_t instance, std::uint32_t realizedIndex);
    using AssignRadioInCombat_t = void(__fastcall*)(void* roleController, std::uint32_t cp, std::uint32_t radioType);
    using ResignRadio_t         = void(__fastcall*)(void* roleController, std::uint32_t cp);
    using RequestInvestigation_t = void(__fastcall*)(std::uint8_t* radioService, std::uint32_t cp, const void* request);
    using SetMemberRole_t       = void(__fastcall*)(std::uint8_t* radioService, std::uint32_t member, std::uint32_t role, const void* payload,
                                                    std::uint64_t size);
    using ClearInvestigation_t  = void(__fastcall*)(void* roleController, std::uint32_t cp, std::uint32_t kinds);
    using CpQuery_t             = bool(__fastcall*)(std::uint8_t* radioService, std::uint32_t cp);
    using SequenceLabel_t       = std::uint32_t(__fastcall*)(void* sequencer, std::uint32_t sequence);
    using SpeakAtIndex_t        = bool(__fastcall*)(void* soundController, std::uint32_t soldierIndex, std::uint32_t event,
                                                    std::uint32_t priority, std::uint32_t label, bool understood, float delay);
    using GetPlayerPosition_t   = void(__fastcall*)(void* playerInfo, float* outPosition);
    using IsPlayingVoice_t      = bool(__fastcall*)(void* soundController, std::uint32_t owner);
    using BgmPhaseMismatch_t    = bool(__fastcall*)(std::uint8_t radioType, std::int32_t bgmPhase);

    constexpr std::uint32_t  kMortarOrderRadio        = 0x56;     // CPR0180, AlertAiImpl::UpdateCombatMortar asks it of nobody
    constexpr std::uint32_t  kAnyLabelRadio           = 0x5E;     // a member call whose label has no type of its own
    constexpr std::uint32_t  kUnknownLabelRadio       = 0x5F;
    constexpr std::uint32_t  kGunshipRadio            = 0x47;     // CPR0220, the CP's own chopper order; carries CPR0210 for the radio man
    constexpr std::uint32_t  kAlertBroadcastRadio     = 0x3D;     // CPR0020, AlertAiImpl::StepNormal's own call a few seconds into an alert
    constexpr std::uint32_t  kCameraAlertRadio        = 0x3E;     // CPR0022, the same call when a camera spotted him
    constexpr std::uint32_t  kUavAlertRadio           = 0x3F;     // CPR0500 "UAV has spotted an intruder! Move in ASAP!"
    constexpr std::uint32_t  kCautionStartRadio       = 0x0B;     // CPR0050 "We may have an intruder. Going on alert."
    constexpr std::uint32_t  kCpOnAlertRadio          = 0x4E;     // the CP's own "All units, on alert" call; ours speaks CPR0049 instead
    constexpr std::size_t    kDamageStride            = 0x30;
    constexpr std::size_t    kDamageAttacker          = 0x22;
    enum Line : std::uint32_t { kOrderLine, kRequestLine, kReadyLine, kFlareLine, kGunshipLine, kContactLine, kLineCount };
    constexpr const char*    kLineLabels[kLineCount]  = {
        "CPR0180",   // CP "Lay down suppressing mortar fire on the target!" > the operator "Preparing for mortar strike!"
        "CPR0182",   // he "Requesting mortar support!" > CP "Mortar team: Provide fire support!" (CPR0181's line)
        "CPR0190",   // the operator "Mortar ready to fire!" > CP "Commence strike."
        "CPR0200",   // CP "Fire a flare to illuminate the area." > the flare man "Preparing to fire flare."
        "CPR0210",   // he "Taking fire from enemy gunship!" > CP "Enemy chopper is priority one!"
        "CPR0010" }; // he "Enemy contact, we are now engaging!" > CP "All units, prepare for combat."
    constexpr std::uint32_t  kNoMember                = 0xFFFF;
    constexpr std::uint16_t  kNoHandle                = 0xFFFF;   // RequestRadioByCp's answer when no call went out
    constexpr std::uint32_t  kNoSoldier               = 0xFFFFFFFF;
    constexpr std::ptrdiff_t kServiceToCpController   = -0x18;
    constexpr std::size_t    kServiceRoleController   = 0x18;     // MemberRoleControllerImpl, as AlertAiImpl reaches it
    constexpr std::size_t    kAssignRadioInCombatSlot = 0x68 / sizeof(void*);   // AlertAiImpl::UpdateReinforceRadio's backup request; the debug build has it at 0x60, retail's 0x60 is AssignRadio
    constexpr std::size_t    kResignRadioSlot         = 0x78 / sizeof(void*);   // AlertAiImpl::StepNormal clears the CP's radio men with it when an alert ends
    constexpr std::size_t    kIsLrrpCpSlot            = 0x98 / sizeof(void*);   // CP spec +0x77 bits 0/1/2/4/5, names from the debug build's order
    constexpr std::size_t    kIsOuterBaseCpSlot       = 0xA0 / sizeof(void*);
    constexpr std::size_t    kIsMarchCpSlot           = 0xA8 / sizeof(void*);
    constexpr std::size_t    kIsChildCpSlot           = 0xB0 / sizeof(void*);
    constexpr std::size_t    kIsSkullCpSlot           = 0xB8 / sizeof(void*);
    constexpr std::size_t    kHasBaseToCallSlot       = 0xD8 / sizeof(void*);   // another CP of its group can take the call
    constexpr std::size_t    kRequestInvestigationSlot = 0x80 / sizeof(void*);  // thunk into DoRequestInvestigationWithCpIndex
    constexpr std::size_t    kSetMemberRoleSlot       = 0x130 / sizeof(void*);
    constexpr std::size_t    kClearInvestigationSlot  = 0x28 / sizeof(void*);   // role controller
    constexpr std::size_t    kServiceRoles            = 0x50;     // 0x20 per member: payload, kind at +0x1D
    constexpr std::size_t    kRolePointToken          = 0x0C;     // in an investigator's payload: kept when the CP moves his point a little
    constexpr std::size_t    kServicePoints           = 0x80;     // one investigation point per CP
    constexpr std::size_t    kPointSize               = 0x1C;
    constexpr std::size_t    kPointCause              = 0x10;
    constexpr std::size_t    kPointId                 = 0x12;
    constexpr std::size_t    kPointArea               = 0x14;
    constexpr std::size_t    kPointFlags              = 0x16;
    constexpr std::size_t    kPointInvestigators      = 0x18;
    constexpr std::uint8_t   kPointActive             = 0x40;
    constexpr std::size_t    kRoleLocators            = 0x30;     // role controller: 0x30 per combat locator, its member count at +0x29
    constexpr std::size_t    kLocatorSize             = 0x30;
    constexpr std::size_t    kLocatorMembers          = 0x29;
    constexpr std::size_t    kMemberPosition          = 0x04;     // in the 0x14 member record: x y z
    constexpr std::size_t    kMemberAbilities         = 0x12;
    constexpr std::uint32_t  kMemberUp                = 0x80001;
    constexpr std::uint32_t  kInvestigatorBusy        = 0x7303C214;   // AssignInvestigation's bits 2/9/15/16/17/24/28 and 0x62004010
    constexpr std::uint8_t   kCanInvestigate          = 0x02;
    constexpr std::uint8_t   kInvestigateRole         = 1;
    constexpr std::uint8_t   kGuardRole               = 3;
    constexpr std::uint16_t  kNoLocator               = 0x100;
    constexpr std::uint8_t   kNoticeInvestigation     = 3;        // the kind the soldiers' own notice requests carry
    constexpr std::uint16_t  kNoCause                 = 0x1FF;
    constexpr std::uint32_t  kAllKinds                = 0xFFFFFFFF;
    constexpr std::size_t    kServiceMemberRadioFlags = 0x58;     // 0x14 per member, bit 1: CallPart speaks him through his own voice
    constexpr std::size_t    kMemberRadioFlagsSize    = 0x14;
    constexpr std::uint32_t  kOwnVoiceFlag            = 0x2;
    constexpr std::size_t    kServiceCpStates         = 0x30;     // 0x60 per CP
    constexpr std::size_t    kCpStateSize             = 0x60;
    constexpr std::size_t    kCpStatePhase            = 0x48;
    constexpr std::size_t    kCpStateFlags            = 0x54;
    constexpr std::uint32_t  kCpNoInvestigators       = 0x40000;   // CautionAiImpl / EvasionAiImpl::StepNormal assign nobody while it's set
    constexpr std::int32_t   kPhaseAlert              = 3;
    constexpr std::size_t    kCpControllerMembers     = 0x58;     // 0x18 per member
    constexpr std::size_t    kCpControllerMemberTotal = 0x174;
    constexpr std::size_t    kMemberSize              = 0x18;
    constexpr std::size_t    kMemberGameObjectId      = 0x10;
    constexpr std::size_t    kMemberCp                = 0x12;
    constexpr std::uint16_t  kSoldierType             = 0x400;
    constexpr std::uint16_t  kTypeMask                = 0xFE00;
    constexpr std::uint16_t  kIndexMask               = 0x1FF;
    constexpr std::size_t    kControllerSoldiers      = 0x58;
    constexpr std::size_t    kSoldiersTransforms      = 0x18;
    constexpr std::size_t    kTransformsActiveBits    = 0x08;
    constexpr std::size_t    kTransformsPositions     = 0x20;     // xyz per soldier, 0x10 apart
    constexpr std::size_t    kPositionStride          = 0x10;
    constexpr std::size_t    kSoldiersLife            = 0x38;     // 0x40 per soldier at +8
    constexpr std::size_t    kLifeRecords             = 0x08;
    constexpr std::size_t    kLifeRecordSize          = 0x40;
    constexpr std::size_t    kLifeFlags               = 0x12;
    constexpr std::uint16_t  kLifeFlagsStatus         = 0x3;      // 0 normal, 1 sleep, 2 faint, 3 dying
    constexpr std::size_t    kLifeState               = 0x3E;
    constexpr std::uint8_t   kLifeStateDead           = 2;
    constexpr std::size_t    kSoldiersPlayerInfo      = 0x188;
    constexpr std::size_t    kSoldiersCommandPost     = 0xD0;
    constexpr std::size_t    kSoldiersSoundController = 0xA8;
    constexpr std::size_t    kCommandPostRoles        = 0x10;     // 0x20 per soldier
    constexpr std::size_t    kRoleSize                = 0x20;
    constexpr std::size_t    kRoleKind                = 0x1D;
    constexpr std::uint8_t   kRadioRole               = 2;        // AssignRadioInCombat
    constexpr std::uint8_t   kMortarRole              = 4;        // MemberRoleControllerImpl::IsMortarRoleEmpty
    constexpr std::uint8_t   kAnyRole                 = 0xFF;
    constexpr std::size_t    kGetPlayerPositionSlot   = 0x18 / sizeof(void*);

    constexpr std::size_t    kHandlerSequencer        = 0x08;
    constexpr std::size_t    kHandlerSequences        = 0x38;     // 0x14 per sequence
    constexpr std::size_t    kHandlerService          = 0x48;
    constexpr std::size_t    kHandlerPartVoice        = 0x58;     // soldier voice the part waits on, StartPart sets it for a named member only
    constexpr std::size_t    kSequenceSize            = 0x14;
    constexpr std::size_t    kSequenceCp              = 0x08;
    constexpr std::size_t    kSequenceMember          = 0x0A;     // GetVoiceParamWithCallSign takes the call sign from it
    constexpr std::size_t    kSequenceSoldier         = 0x0C;
    constexpr std::size_t    kSequenceGameObjectId    = 0x0E;     // StartPart fills it for a named member
    constexpr std::size_t    kSequenceLabelSlot       = 0xB0 / sizeof(void*);
    constexpr std::size_t    kSpeakAtIndexSlot        = 0x28 / sizeof(void*);   // __callVoiceWithoutSyncAtInstanceIndex

    constexpr std::size_t    kMortarWorks             = 0x88;     // MortarActionImpl, 0x40 per realized slot
    constexpr std::size_t    kMortarFirstSlot         = 0x90;
    constexpr std::size_t    kMortarWorkSize          = 0x40;
    constexpr std::size_t    kWorkSoldier             = 0x2C;
    constexpr std::size_t    kWorkNextState           = 0x31;
    constexpr std::size_t    kWorkFlags               = 0x33;
    constexpr std::uint8_t   kWorkRegistration        = 0x03;     // 1: the mortar took him
    constexpr std::uint8_t   kWorkFlareShell          = 0x08;
    constexpr std::uint8_t   kFiringState             = 2;        // State_WaitAction moves on to it once the mortar took him
    constexpr std::size_t    kUnrealWorks             = 0x80;     // UnrealMortarActionImpl, far from Snake: 0x24 per soldier, fires on a timer
    constexpr std::size_t    kUnrealFirstSoldier      = 0x88;
    constexpr std::size_t    kUnrealWorkSize          = 0x24;
    constexpr std::size_t    kUnrealNextState         = 0x1F;
    constexpr std::size_t    kUnrealFlags             = 0x20;
    constexpr std::uint8_t   kUnrealFlareShell        = 0x04;
    constexpr std::size_t    kMortarCommands          = 0x80;     // realized; *(+8) is the soldiers' action commands, 0xD0 each
    constexpr std::size_t    kUnrealCommands          = 0x78;
    constexpr std::size_t    kCommandTable            = 0x08;
    constexpr std::size_t    kCommandSize             = 0xD0;
    constexpr std::size_t    kCommandGimmick          = 0x24;
    constexpr std::size_t    kCommandFlare            = 0x26;     // bit 0
    constexpr std::size_t    kCommandId               = 0xBA;
    constexpr std::size_t    kCommandType             = 0xC6;
    constexpr std::uint8_t   kMortarCommandType       = 0x15;
    constexpr std::size_t    kWorkGimmick             = 0x28;
    constexpr std::size_t    kWorkCommandId           = 0x2A;
    constexpr std::size_t    kUnrealWorkGimmick       = 0x10;
    constexpr std::size_t    kUnrealWorkCommandId     = 0x1C;
    constexpr std::int32_t   kStateUpdate             = 0;

    constexpr std::size_t    kRangeAttackChaff        = 0x138;    // one cloud, 0x40
    constexpr std::size_t    kChaffDuration           = 0x20;
    constexpr std::size_t    kChaffElapsed            = 0x24;
    constexpr std::size_t    kChaffFlags              = 0x30;
    constexpr std::uint8_t   kChaffInUse              = 0x01;
    constexpr std::uint8_t   kChaffEnded              = 0x04;
    constexpr std::uint8_t   kChaffEndless            = 0x20;
    constexpr std::uint64_t  kChaffWatchMs            = 120000;

    ConvertSpeechLabel_t g_OrigConvertSpeechLabel = nullptr;
    RequestRadioByCp_t   g_OrigRequestRadioByCp   = nullptr;
    CallPart_t           g_OrigCallPart           = nullptr;
    ActivateChaff_t      g_OrigActivateChaff      = nullptr;
    MortarState_t        g_OrigWaitAction         = nullptr;
    UnrealMortarState_t  g_OrigUnrealWaitAction   = nullptr;
    MortarState_t        g_OrigMortarAction       = nullptr;
    UnrealMortarState_t  g_OrigUnrealMortarAction = nullptr;
    HeliUnrealize_t      g_OrigHeliUnrealize      = nullptr;
    BgmPhaseMismatch_t   g_OrigBgmPhaseMismatch   = nullptr;
    std::uint32_t        g_LineHashes[kLineCount] = {};

    constexpr std::uint64_t kRetryForMs        = 30000;   // a higher call can push a queued one out; vanilla just loses it
    constexpr std::uint64_t kRetryEveryMs      = 1500;
    constexpr std::uint64_t kRequesterWaitMs   = 12000;   // the radio man gets this long to put his request on air
    constexpr std::uint64_t kNamedCallMs       = 30000;   // a call that names a soldier has this long to come on air
    constexpr std::uint64_t kReadyAirMostMs    = 20000;   // his call has this long to get on a busy radio, or he fires without it
    constexpr std::uint64_t kReadyAnswerMostMs = 15000;   // on air: the CP's part has this long to finish
    constexpr std::uint64_t kReadyRepostMs     = 3000;    // the CP drops a member call it kept waiting; he asks again
    constexpr std::uint64_t kReadyHoldMostMs   = 35000;   // on the mortar, strike call included; under chaff it counts from the chaff clearing
    constexpr std::uint64_t kStrikeCallMostMs  = 30000;
    constexpr std::uint64_t kReadyOncePerMs    = 60000;   // one "Mortar ready to fire!" per strike, as vanilla spaces its strike calls
    constexpr std::uint64_t kFlareCallGapMs    = 30000;
    constexpr std::size_t   kCpCount           = 0x100;
    constexpr std::size_t   kSoldierCount      = 0x200;
    constexpr std::uint32_t kNoCp              = 0xFFFFFFFF;
    constexpr std::uint64_t kChannelFreshMs    = 30000;   // no radio line runs longer; past it the channel is someone else's
    constexpr std::uint32_t kSignOffLines[]    = {   // what GetVoiceParamWithCallSign makes of the CE* call ends
        0x6F7C61F5u, 0x6C7C5D3Cu,    // CEA0010 "Over!!", CEA0020
        0x3489C093u, 0x3589C206u,    // CEC0010, CEC0020
        0xE1EEA7EAu, 0xE0EEA677u };  // CEN0010, CEN0020
    constexpr std::uint32_t kCpVoices[] = { 0xEB6A98E8u, 0x3E09CB80u, 0x2B09AE6Au,     // CallPart's CP voices by region
                                            0xCC8D2DC8u, 0xA14739A0u, 0x8E471B8Au };   // and its HQ ones
    constexpr std::uint64_t kJamHoldMostMs     = 12000;   // the longest a chaff jam waits for him to finish his request

    enum class Pending : std::uint8_t { None, Decide, AwaitRequester, CpOrder, Resend };
    struct PendingMortar
    {
        Pending       kind        = Pending::None;
        std::uint64_t untilMs     = 0;
        std::uint64_t triedMs     = 0;
        std::uint32_t caller      = kNoSoldier;   // tried his radio under the chaff: he makes the call once it clears
        bool          callOutSaid = false;
    };
    struct NamedCall   // a CP call that has to name a real soldier: his call sign, his voice
    {
        std::uint32_t label   = 0;
        std::uint32_t soldier = kNoSoldier;
        std::uint32_t member  = kNoMember;
        std::uint64_t untilMs = 0;
        bool          logged  = false;
    };
    struct ReadyHold   // the man on the mortar waits for his radio exchange: "Commence strike.", or his reply to the flare order
    {
        std::uint32_t soldier    = kNoSoldier;
        Line          line       = kReadyLine;
        std::uint32_t cp         = kNoCp;
        std::uint32_t serial     = 0;       // the strike call it answers
        std::uint64_t startedMs  = 0;
        std::uint64_t capFromMs  = 0;
        std::uint64_t askedMs    = 0;
        std::uint64_t lastAskMs  = 0;
        std::uint64_t releasedMs = 0;
        std::uint64_t onAirMs    = 0;
        bool          jammed     = false;   // chaff ate his call: he waits until it clears, then asks again
        bool          callOutSaid = false;
        bool          done       = false;
        bool          refused    = false;
    };
    struct PartSpeaker
    {
        void*         soundController = nullptr;
        std::uint32_t soldier         = kNoSoldier;
    };
    struct PlayingSequence
    {
        std::uint32_t cp      = kNoCp;
        std::uint32_t soldier = kNoSoldier;   // the member the call names, none for the CP's own call
    };
    struct RadioChannel   // where the last line of a CP's call is playing
    {
        void*         soundController = nullptr;
        std::uint32_t owner           = 0;
        std::uint32_t soldier         = kNoSoldier;
        std::uint64_t startedMs       = 0;
    };
    struct CallOnAir
    {
        std::uint64_t onAirMs     = 0;
        std::uint64_t heldSinceMs = 0;      // when a chaff jam first waited on him
        bool          released    = false;  // the jam went through; it is not held twice
    };
    std::mutex                              g_PendingMutex;
    std::array<PendingMortar, kCpCount>     g_Pending{};
    std::array<NamedCall, kCpCount>         g_Named{};
    std::array<std::uint32_t, kCpCount>     g_ReadyHolder{};   // soldier + 1 waiting on that CP's CPR0190, 0 for none
    std::array<std::uint64_t, kCpCount>     g_StrikeOpenedMs{};   // the strike call was decided and hasn't gone off air yet
    std::array<std::uint32_t, kCpCount>     g_StrikeSerial{};     // bumped by each strike call: each gets its own "Mortar ready to fire!"
    std::array<std::uint32_t, kSoldierCount> g_ReadyDoneSerial{};  // the strike his last ready call answered
    std::array<std::uint32_t, kSoldierCount> g_SoldierCp{};        // cp + 1, 0 unknown
    std::array<std::uint64_t, kSoldierCount> g_SoldierCpMs{};
    constexpr std::uint64_t                 kSoldierCpFreshMs = 5000;
    std::array<ReadyHold, kSoldierCount>    g_Ready{};
    std::array<std::uint64_t, kCpCount>     g_FlareCalledMs{};
    std::array<RadioChannel, kCpCount>      g_Channels{};
    std::array<CallOnAir, kCpCount>         g_Calls{};
    std::array<std::atomic<std::uint64_t>, kSoldierCount> g_SpokeMs{};       // a line of his own call last started
    std::array<std::atomic<std::uint64_t>, kSoldierCount> g_SignedOffMs{};   // his "Over!!" last started
    std::array<std::atomic<std::uint64_t>, kSoldierCount> g_CpTurnMs{};      // the CP's reply in his call last started
    IsPlayingVoice_t                        g_IsPlayingVoice = nullptr;
    thread_local PartSpeaker                t_PartSpeaker{};   // while CallPart plays a named soldier's half through the CP's channel
    thread_local PlayingSequence            t_Sequence{};      // while CallPart runs

    std::atomic<std::uint8_t*>  g_Service{ nullptr };      // the radio service, CommandPost2Impl
    std::atomic<std::uint8_t*>  g_RangeAttack{ nullptr };
    std::atomic<std::uint64_t>  g_ChaffAtMs{ 0 };
    struct RoleCall   // the radio man we gave the role to: his own request goes out as our line
    {
        std::uint32_t soldier = kNoSoldier;
        Line          line    = kRequestLine;
        std::uint64_t untilMs = 0;
        bool          sent    = false;
    };
    std::array<RoleCall, kCpCount>  g_RoleCalls{};
    constexpr std::uint64_t         kRoleCallMs = 20000;
    struct RadioOrder   // Lua's AssignRadioInCombat: he takes the radio role, pose and all, for one call of this label
    {
        std::uint32_t soldier = kNoSoldier;
        std::uint32_t member  = kNoMember;   // set when his request goes out
        std::uint32_t label   = 0;
        std::uint32_t type    = 0;            // what the label means to the CP
        std::uint64_t untilMs = 0;
        bool          queued  = false;        // asked from Lua: the CP hands the radio over on its own idle tick
        bool          noPose  = false;        // he only talks: no radio role, his call goes out as a member radio
        bool          reply   = false;        // the soldiers' answer to the CP's camera or UAV call, not Lua's
        std::uint64_t postedMs = 0;
        bool          sent    = false;
        bool          onAir   = false;
        bool          done    = false;        // off air: the role goes back on the next idle tick
    };
    std::array<RadioOrder, kCpCount> g_RadioOrders{};
    std::array<bool, kCpCount>       g_SpottedReplyDue{};  // user: after the CP's CPR0022 or CPR0500 the soldier nearest Snake answers
    constexpr std::uint64_t          kNoPoseCallMs = 30000;   // a call without the pose keeps asking a busy radio this long
    std::uint32_t                    g_CameraReplyLabel = 0;   // V_CPR0022ENE, the soldier's cpr0022_01 alone
    std::uint32_t                    g_UavReplyLabel = 0;      // CPR0341ENE "Will investigate immediately.", a camera checker's answer (NoticeCameraAiImpl::StepRadioStart, 0x2D)
    std::uint32_t                    g_InvestigateReplyTake2 = 0;   // V_CPR0340ENE: cpr0340_01, the same answer recorded for CPR0340 and never played
    using RequestRadioByMember_t = std::uint16_t(__fastcall*)(std::uint8_t* cpController, std::uint32_t client, std::uint32_t local,
                                                              std::uint32_t radioType, std::uint32_t label);
    RequestRadioByMember_t          g_OrigRequestRadioByMember = nullptr;
    constexpr std::size_t           kCpControllerClients = 0x1F0;   // soldiers, helis... each with its own run of members
    constexpr std::size_t           kClientSize          = 0x48;
    constexpr std::size_t           kClientFirstMember   = 0x40;
    constexpr std::uint16_t         kSoldierObjectType   = 2;       // gameObjectId >> 9

    struct GunshipCall
    {
        std::uint32_t soldier     = kNoSoldier;   // the man the heli hit: tries his radio under chaff, the fallback speaker
        std::uint64_t pendingMs   = 0;
        std::uint64_t awaitMs     = 0;            // the radio man has until then to put it on air
        bool          called      = false;        // once per CP until the heli unrealizes (user)
        bool          callOutSaid = false;
    };
    std::array<GunshipCall, kCpCount> g_Gunship{};
    std::atomic<std::uint32_t>      g_SupportHeliId{ 0xFFFFFFFF };
    std::atomic<std::uint64_t>      g_SupportHeliCheckedMs{ 0 };
    constexpr std::uint64_t         kSupportHeliRecheckMs = 5000;
    constexpr std::uint64_t         kGunshipAwaitMs       = 12000;

    std::array<std::atomic<std::uint64_t>, kCpCount>      g_CpRadioMs{};        // a call naming nobody last played on this CP
    std::array<std::atomic<std::uint64_t>, kSoldierCount> g_SoldierRadioMs{};   // a call naming him last played

    struct ContactCall   // the radio man calls the alert in himself (CPR0010); the CP's own CPR0020 is the fallback
    {
        std::uint32_t soldier = kNoSoldier;
        std::uint32_t member  = kNoMember;   // set when his request goes out
        std::uint64_t untilMs = 0;
        bool          sent    = false;
        bool          onAir   = false;
        bool          done    = false;   // off air: his radio role goes on the next idle tick
        bool          cpAnswered = false;
    };
    std::array<ContactCall, kCpCount> g_Contact{};

    struct HeldUpReport   // a held-up soldier who freed himself calls it in, then the CP answers with CPR0049
    {
        std::uint32_t soldier  = 0xFFFFFFFF;
        bool          onAir    = false;
        bool          replyDue = false;
        std::uint64_t replyMs  = 0;       // the CP's answer was asked for; its 0x4E call speaks CPR0049 for a while
        std::uint64_t requestMs = 0;      // when his hold-up call was asked for; one that never airs goes stale
    };
    constexpr std::uint64_t            kHeldUpReportMs    = 60000;
    std::array<HeldUpReport, kCpCount> g_HeldUpReports{};
    std::uint32_t                      g_StayOnAlertLabel = 0;   // CPR0049, no radio type of its own
    constexpr std::uint64_t            kAnswerWindowMs    = 20000;
    constexpr std::uint64_t           kContactWaitMs = 10000;   // user: if he hasn't started his call by then, the CP calls CPR0020 itself
    constexpr std::uint64_t           kAlertCallDelayMs = 3000;

    constexpr std::uint8_t   kSomethingToCheckRadio = 0x0C;     // CPR0052 "Got something worth checking here" > "Tighten security, and stay alert"
    constexpr std::uint8_t   kSearchCompleteRadio   = 0x3A;     // CPR0070, an investigator's own end report
    constexpr std::uint8_t   kExplosionRadio        = 0x05;     // CPR0031 "There was an explosion, damage unknown!"
    constexpr std::uint8_t   kExplosionAgainRadio   = 0x06;     // CPR0035, his second threat since his CP last calmed down
    constexpr std::uint8_t   kInstallationRadio     = 0x04;     // CPR0145 "Got damage to some equipment here." > "Move to investigate, ASAP."
    constexpr std::uint64_t  kEquipmentReportMs     = 30000;    // his CPR0145 has this long to come on air as CPR0346 / CPR0345
    constexpr std::uint8_t   kUavDownRadio          = 0x44;     // CPR0510 "UAV feed is down... Whoever's closest, go and investigate immediately."
    constexpr std::uint64_t  kUavDownCallDelayMs    = 5000;     // user: as late as a downed camera's CPR0340 (MemberRoleControllerImpl::UpdateCheckGimmick, 5 s)
    constexpr std::uint64_t  kUavReplyAirMs         = 10000;    // RadioAiImpl::StepWait holds him 1-3 s, then the CP's radio may still be busy
    constexpr std::uint64_t  kUavReplyMostMs        = 30000;
    constexpr std::uint8_t   kInvestigateReplyRadio = 0x2D;     // CPR0341ENE, a camera or lights checker's answer (and our UAV checker's)
    constexpr std::uint64_t  kBlastReportMs         = 60000;    // long enough for his radio step to finish
    constexpr int            kStealthForTeams       = 3;        // user: TppRevenge.GetRevengeLv(STEALTH) 3 and up, vanilla below
    constexpr int            kStealthAlwaysTeam     = 5;        // user: a find brings a team 1 in 3 at level 3, 2 in 3 at 4, always at 5
    constexpr std::size_t    kSurveyTeamMost        = 2;        // user: two; the finder is one of them only for a vehicle blast
    constexpr std::uint64_t  kSurveyCallMs          = 30000;    // the find call has this long to come on air and end
    constexpr std::uint64_t  kSurveyMostMs          = 240000;
    constexpr std::uint64_t  kFinderHoldMs          = 60000;
    constexpr std::uint64_t  kWakeStartMs           = 10000;    // the nearest one's downed-comrade node has this long to start
    constexpr std::uint64_t  kWakeMostMs            = 90000;    // walking up to 100 m, then seeing to him
    constexpr std::uint64_t  kFinderHoldMostMs      = kSurveyCallMs + kWakeStartMs + kWakeMostMs;
    constexpr int            kFirstSightNotice      = 0x1E;     // what starts NoticeDownComradeAi
    constexpr std::uint16_t  kNoComrade             = 0xFFFF;
    constexpr float          kSameSpotRange         = 10.0f;    // a second find there stays vanilla: that finder walks over and wakes him
    constexpr float          kSurveyTeamSpread      = 100.0f;   // AssignSelectTargetInvestigation's 10000.0 squared

    enum class Survey : std::uint8_t { None, Found, Told, Dispatch, Replying, Waking, Out };
    struct SurveyTeam   // user design: a soldier finds something, the CP sends up to four others, one of them reports the sweep
    {
        Survey        stage    = Survey::None;
        std::uint8_t* service  = nullptr;      // the mission it belongs to
        std::uint32_t finder   = kNoSoldier;
        std::uint32_t member   = kNoMember;    // the finder's
        float         spot[3]  = {};
        float         pointAt[3] = {};         // where the CP put the point; it moves it when it hands the point to another job
        std::uint32_t team[kSurveyTeamMost]   = {};
        std::uint32_t teamSoldier[kSurveyTeamMost] = {};
        std::uint32_t teamSize = 0;
        bool          calledOff = false;       // one of them lied "All clear" to the CP
        std::uint16_t token    = 0;            // the point the CP made of the spot
        std::uint64_t untilMs  = 0;
        bool          fromBody = false;        // a downed comrade: only that node can hold him
        std::uint16_t comrade  = kNoComrade;   // that comrade's GameObjectId
        std::uint32_t waker    = kNoSoldier;   // user: the team's nearest sees to him before the sweep
        bool          wakerUp  = false;        // his downed-comrade node has started
        bool          wakeDone = false;
        bool          busy     = false;        // the CP was already investigating something else
        std::uint8_t  radioType = kSomethingToCheckRadio;   // his find call: 0x0C, or 5/6 for a vehicle blast
        bool          blast    = false;        // user: CPR0143, and the man who reported it goes with the team
        bool          uav      = false;        // user: CPR0510, the CP's own order: the nearest one goes, nobody found it
        bool          replied  = false;        // user: he answers the CP first, in his radio pose, as a camera's checker does
        std::uint32_t replier  = kNoSoldier;
        std::uint32_t missingTeam = 0;         // the SITREP group of the members the player took out
        bool          reported[kSurveyTeamMost] = {};
        bool          second   = false;        // user: sent after a SITREP nobody answered; no third team comes
    };
    struct SurveyAsked   // user: a member taken out gets his CP's SITREP; nobody answering it brings a second team to the spot
    {
        std::uint8_t* service  = nullptr;
        std::uint32_t team     = 0;            // CpRadioResponse's SITREP group
        float         spot[3]  = {};
        bool          second   = false;        // they were the second team already
        bool          sendTeam = false;        // nobody answered: the CP's next idle tick sends the second team
    };
    constexpr std::size_t kSurveyedSpotsKept = 8;
    struct SurveyedSpots   // the bodies stay down after a sweep: the next guard who finds one gets vanilla and wakes him
    {
        std::uint8_t* service = nullptr;
        std::uint32_t next    = 0;
        std::uint32_t count   = 0;
        float         spot[kSurveyedSpotsKept][3] = {};
    };
    std::array<SurveyTeam, kCpCount>                      g_Survey{};
    std::array<SurveyAsked, kCpCount>                     g_SurveyAsked{};
    std::array<SurveyedSpots, kCpCount>                   g_SurveyedSpot{};
#ifdef _DEBUG
    std::array<std::atomic<std::uint16_t>, kSoldierCount> g_ProbeSurveyCp{};   // TEMP probe: CP + 1 while he's on a survey team that's out
#endif
    std::array<std::atomic<std::uint64_t>, kSoldierCount> g_FinderHoldUntilMs{};
    std::array<std::atomic<std::uint64_t>, kSoldierCount> g_FinderWatchUntilMs{};   // holding his spot: not picked for a team
    std::array<std::atomic<bool>, kSoldierCount>          g_FinderHeld{};   // his Wait step is ours until it sends him back to his route
    std::array<std::atomic<bool>, kSoldierCount>          g_Waker{};
    std::array<std::atomic<std::uint64_t>, kSoldierCount> g_BlastReporterUntilMs{};   // his NoiseThreat node mustn't file vanilla's point
    std::array<std::atomic<std::int32_t>, kCpCount>       g_CpPhaseSeen{};
    std::array<std::atomic<std::uint64_t>, kCpCount>      g_CpCalmedMs{};   // the CP last dropped back to sneak
    std::uint32_t g_SendTeamLabel   = 0;   // CPR0148 "Maintain watch where you are. We'll send another team to check it out."
    std::uint32_t g_StayOnGuardLabel = 0;  // CPR0142 "Stay on guard where you are."
    std::uint32_t g_SweepDoneLabel  = 0;   // CPR0147 "Finished our sweep. There's nobody here. Heading back." > "Head back immediately."
    std::uint32_t g_BlastLabel      = 0;   // CPR0143 "Got damage to a vehicle. Looks like the work of a hostile." > "Move to investigate, ASAP."
    std::uint32_t g_CommsLabel      = 0;   // CPR0346 "This comms equipment's been destroyed. It's the enemy."
    std::uint32_t g_PowerLabel      = 0;   // CPR0345 "Power system's been taken out. This is enemy sabotage."

    struct EquipmentReport   // user: a guard who hears an antenna, comms gear or a generator go names it instead of CPR0145
    {
        std::uint32_t soldier = kNoSoldier;
        std::uint32_t member  = kNoMember;
        std::uint32_t label   = 0;
        std::uint64_t untilMs = 0;
    };
    std::array<EquipmentReport, kCpCount> g_EquipmentReports{};

    struct UavDown   // it fell on the UAV's update; the CP's first idle tick 5 s later says it
    {
        bool          due          = false;
        std::uint8_t* cpController = nullptr;   // the one the UAV belongs to
        float         spot[3]      = {};
        std::uint64_t downMs       = 0;
    };
    std::array<UavDown, kCpCount> g_UavDown{};

    std::uint8_t __fastcall hk_ConvertSpeechLabelToRadioType(std::uint32_t label)
    {
        if (label != 0 && !MissionCodeGuard::ShouldBypassHooks())
        {
            if (label == g_LineHashes[kContactLine])
                return static_cast<std::uint8_t>(kAlertBroadcastRadio);
            if (label == g_LineHashes[kOrderLine] || label == g_LineHashes[kRequestLine])
                return static_cast<std::uint8_t>(kMortarOrderRadio);   // CPR0182 is still the vanilla mortar call
            if (label == g_LineHashes[kReadyLine] || label == g_LineHashes[kFlareLine] || label == g_LineHashes[kGunshipLine])
                return static_cast<std::uint8_t>(kUnknownLabelRadio);   // his own call then goes out as 0x5E, past the CP's refusals
        }
        return g_OrigConvertSpeechLabel(label);
    }

    std::int32_t CpPhase(std::uint8_t* service, std::uint32_t cp)
    {
        __try
        {
            const std::uint8_t* states = *reinterpret_cast<std::uint8_t* const*>(service + kServiceCpStates);
            return *reinterpret_cast<const std::int32_t*>(states + cp * kCpStateSize + kCpStatePhase);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
    }

    bool InAlert(std::uint8_t* service, std::uint32_t cp)
    {
        return CpPhase(service, cp) == kPhaseAlert;
    }

    bool NoInvestigatorsAllowed(std::uint8_t* service, std::uint32_t cp)
    {
        __try
        {
            const std::uint8_t* states = *reinterpret_cast<std::uint8_t* const*>(service + kServiceCpStates);
            return (*reinterpret_cast<const std::uint32_t*>(states + cp * kCpStateSize + kCpStateFlags) & kCpNoInvestigators) != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return true;
        }
    }

    bool RollTeam(int stealth)
    {
        static std::atomic<std::uint32_t> roll{ static_cast<std::uint32_t>(GetTickCount64()) | 1u };
        std::uint32_t x = roll.load(std::memory_order_relaxed);
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        roll.store(x, std::memory_order_relaxed);
        const int thirds = stealth >= kStealthAlwaysTeam ? 3 : stealth - kStealthForTeams + 1;
        return static_cast<int>(x % 3) < thirds;
    }

    bool CoinFlip()
    {
        static std::atomic<std::uint32_t> roll{ static_cast<std::uint32_t>(GetTickCount64()) | 1u };
        std::uint32_t x = roll.load(std::memory_order_relaxed);
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        roll.store(x, std::memory_order_relaxed);
        return (x & 1u) != 0;
    }

    bool ChaffIsUp()
    {
        std::uint8_t* rangeAttack = g_RangeAttack.load(std::memory_order_acquire);
        if (!rangeAttack || GetTickCount64() - g_ChaffAtMs.load(std::memory_order_relaxed) > kChaffWatchMs)
            return false;
        __try
        {
            const std::uint8_t* cloud = *reinterpret_cast<std::uint8_t* const*>(rangeAttack + kRangeAttackChaff);
            const std::uint8_t flags = cloud[kChaffFlags];
            if (!(flags & kChaffInUse) || (flags & kChaffEnded))
                return false;
            return (flags & kChaffEndless)
                || *reinterpret_cast<const float*>(cloud + kChaffElapsed) < *reinterpret_cast<const float*>(cloud + kChaffDuration);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ChannelPlaying(const RadioChannel& channel)
    {
        if (!g_IsPlayingVoice)
            return false;
        __try
        {
            return g_IsPlayingVoice(channel.soundController, channel.owner);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    const std::uint8_t* LiveSoldiers()
    {
        void* noticeController = SoldierNotice_LiveController();
        if (!noticeController)
            return nullptr;
        __try
        {
            return *reinterpret_cast<const std::uint8_t* const*>(static_cast<std::uint8_t*>(noticeController) + kControllerSoldiers);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    bool SnakePosition(const std::uint8_t* soldiers, float* out)
    {
        alignas(16) float snake[4] = {};
        __try
        {
            void* playerInfo = *reinterpret_cast<void* const*>(soldiers + kSoldiersPlayerInfo);
            const auto getPlayerPosition = (*reinterpret_cast<GetPlayerPosition_t* const*>(playerInfo))[kGetPlayerPositionSlot];
            getPlayerPosition(playerInfo, snake);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        out[0] = snake[0];
        out[1] = snake[1];
        out[2] = snake[2];
        return true;
    }

    bool IsUp(const std::uint8_t* soldiers, std::uint32_t soldierIndex, float* position)
    {
        __try
        {
            const std::uint8_t* transforms = *reinterpret_cast<const std::uint8_t* const*>(soldiers + kSoldiersTransforms);
            const std::uint8_t* active = *reinterpret_cast<const std::uint8_t* const*>(transforms + kTransformsActiveBits);
            if (!(active[soldierIndex >> 3] & (1u << (soldierIndex & 7))))
                return false;
            const auto* life = *reinterpret_cast<const std::uint8_t* const*>(
                *reinterpret_cast<const std::uint8_t* const*>(soldiers + kSoldiersLife) + kLifeRecords) + soldierIndex * kLifeRecordSize;
            if (life[kLifeState] == kLifeStateDead || (*reinterpret_cast<const std::uint16_t*>(life + kLifeFlags) & kLifeFlagsStatus) != 0)
                return false;
            const float* at = reinterpret_cast<const float*>(*reinterpret_cast<const std::uint8_t* const*>(transforms + kTransformsPositions)
                + soldierIndex * kPositionStride);
            position[0] = at[0];
            position[1] = at[1];
            position[2] = at[2];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool MemberEntry(std::uint8_t* cpController, std::uint32_t member, std::uint16_t& gameObjectId, std::uint8_t& cp)
    {
        __try
        {
            const std::uint8_t* entry = *reinterpret_cast<std::uint8_t* const*>(cpController + kCpControllerMembers) + member * kMemberSize;
            gameObjectId = *reinterpret_cast<const std::uint16_t*>(entry + kMemberGameObjectId);
            cp = entry[kMemberCp];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint32_t MemberTotal(std::uint8_t* cpController)
    {
        __try
        {
            return *reinterpret_cast<const std::uint32_t*>(cpController + kCpControllerMemberTotal);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    bool FindMember(std::uint8_t* service, std::uint32_t soldierIndex, std::uint32_t& member, std::uint32_t& cp)
    {
        if (!service)
            return false;
        std::uint8_t* cpController = service + kServiceToCpController;
        const std::uint32_t total = MemberTotal(cpController);
        for (std::uint32_t index = 0; index < total; ++index)
        {
            std::uint16_t gameObjectId = 0;
            std::uint8_t memberCp = 0;
            if (MemberEntry(cpController, index, gameObjectId, memberCp) && gameObjectId == (kSoldierType | soldierIndex))
            {
                member = index;
                cp = memberCp;
                return cp < kCpCount;
            }
        }
        return false;
    }

    std::uint8_t RoleOf(const std::uint8_t* soldiers, std::uint32_t soldierIndex)
    {
        __try
        {
            const std::uint8_t* commandPost = *reinterpret_cast<const std::uint8_t* const*>(soldiers + kSoldiersCommandPost);
            const std::uint8_t* roles = *reinterpret_cast<const std::uint8_t* const*>(commandPost + kCommandPostRoles);
            return roles[soldierIndex * kRoleSize + kRoleKind];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    bool OutOfAction(const std::uint8_t* soldiers, std::uint32_t soldierIndex)   // killed, knocked out or held up: he won't get on the radio
    {
        bool heldKnown = false;
        if (CpRadioResponse::IsHeld(static_cast<std::uint16_t>(soldierIndex), heldKnown))
            return true;
        __try
        {
            const auto* life = *reinterpret_cast<const std::uint8_t* const*>(
                *reinterpret_cast<const std::uint8_t* const*>(soldiers + kSoldiersLife) + kLifeRecords) + soldierIndex * kLifeRecordSize;
            return life[kLifeState] == kLifeStateDead || (*reinterpret_cast<const std::uint16_t*>(life + kLifeFlags) & kLifeFlagsStatus) != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool CanTalk(const std::uint8_t* soldiers, std::uint32_t soldierIndex, float* position)
    {
        bool heldKnown = false;
        return IsUp(soldiers, soldierIndex, position)
            && !CpRadioResponse::IsHeld(static_cast<std::uint16_t>(soldierIndex), heldKnown)
            && SoldierRealizedSlot::Of(soldiers, soldierIndex) != SoldierRealizedSlot::kNotRealized
            && SoldierVoiceType::IsAdult(SoldierVoiceType::Of(soldiers, soldierIndex));
    }

    std::uint32_t RoleHolder(std::uint8_t* service, std::uint32_t cp, const std::uint8_t* soldiers, std::uint8_t role, bool talking,
                             std::uint32_t& memberOut)
    {
        float snake[3] = {};
        if (!service || !soldiers || !SnakePosition(soldiers, snake))
            return kNoSoldier;
        std::uint8_t* cpController = service + kServiceToCpController;
        const std::uint32_t total = MemberTotal(cpController);
        std::uint32_t best = kNoSoldier;
        float bestDistance = 0.0f;
        for (std::uint32_t member = 0; member < total; ++member)
        {
            std::uint16_t gameObjectId = 0;
            std::uint8_t memberCp = 0;
            if (!MemberEntry(cpController, member, gameObjectId, memberCp) || memberCp != cp || (gameObjectId & kTypeMask) != kSoldierType)
                continue;
            const std::uint32_t soldierIndex = gameObjectId & kIndexMask;
            float at[3] = {};
            if ((role != kAnyRole && RoleOf(soldiers, soldierIndex) != role)
                || (talking ? !CanTalk(soldiers, soldierIndex, at) : !IsUp(soldiers, soldierIndex, at)))
                continue;
            const float dx = at[0] - snake[0];
            const float dy = at[1] - snake[1];
            const float dz = at[2] - snake[2];
            const float distance = dx * dx + dy * dy + dz * dz;
            if (best != kNoSoldier && distance >= bestDistance)
                continue;
            best = soldierIndex;
            bestDistance = distance;
            memberOut = member;
        }
        return best;
    }

    std::uint32_t RadioRoleHolder(std::uint8_t* service, std::uint32_t cp, const std::uint8_t* soldiers)
    {
        if (!service || !soldiers)
            return kNoSoldier;
        std::uint8_t* cpController = service + kServiceToCpController;
        const std::uint32_t total = MemberTotal(cpController);
        for (std::uint32_t member = 0; member < total; ++member)
        {
            std::uint16_t gameObjectId = 0;
            std::uint8_t memberCp = 0;
            if (MemberEntry(cpController, member, gameObjectId, memberCp) && memberCp == cp && (gameObjectId & kTypeMask) == kSoldierType
                && RoleOf(soldiers, gameObjectId & kIndexMask) == kRadioRole)
                return gameObjectId & kIndexMask;
        }
        return kNoSoldier;
    }

    std::uint32_t GiveRoleCall(std::uint8_t* service, std::uint32_t cp, Line line)   // the radio man AssignRadioInCombat just picked
    {
        const std::uint32_t radioMan = RadioRoleHolder(service, cp, LiveSoldiers());
        if (radioMan == kNoSoldier)
            return kNoSoldier;
        std::lock_guard<std::mutex> lock(g_PendingMutex);
        RoleCall call;
        call.soldier = radioMan;
        call.line = line;
        call.untilMs = GetTickCount64() + kRoleCallMs;
        g_RoleCalls[cp] = call;
        return radioMan;
    }

    bool MemberOfClient(std::uint8_t* cpController, std::uint32_t client, std::uint32_t local, std::uint32_t& soldier, std::uint32_t& cp,
                        std::uint32_t& member)
    {
        __try
        {
            const std::uint8_t* clients = *reinterpret_cast<std::uint8_t* const*>(cpController + kCpControllerClients);
            member = *reinterpret_cast<const std::uint16_t*>(clients + client * kClientSize + kClientFirstMember) + local;
            const std::uint8_t* entry = *reinterpret_cast<std::uint8_t* const*>(cpController + kCpControllerMembers) + member * kMemberSize;
            const std::uint16_t gameObjectId = *reinterpret_cast<const std::uint16_t*>(entry + kMemberGameObjectId);
            if ((gameObjectId & kTypeMask) != kSoldierType)
                return false;
            soldier = gameObjectId & kIndexMask;
            cp = entry[kMemberCp];
            return cp < kCpCount;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint8_t RoleKindOf(std::uint8_t* service, std::uint32_t member)
    {
        __try
        {
            return (*reinterpret_cast<std::uint8_t* const*>(service + kServiceRoles))[member * kRoleSize + kRoleKind];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return kAnyRole;
        }
    }

    bool NearSpot(const float* a, const float* b, float range)
    {
        const float dx = a[0] - b[0];
        const float dy = a[1] - b[1];
        const float dz = a[2] - b[2];
        return dx * dx + dy * dy + dz * dz < range * range;
    }

    bool WasSurveyed(const SurveyedSpots& swept, std::uint8_t* service, const float* spot)
    {
        if (swept.service != service)
            return false;
        for (std::uint32_t i = 0; i < swept.count; ++i)
        {
            if (NearSpot(swept.spot[i], spot, kSameSpotRange))
                return true;
        }
        return false;
    }

    void RememberSurveyed(SurveyedSpots& swept, std::uint8_t* service, const float* spot)
    {
        if (swept.service != service)
            swept = SurveyedSpots{ service };
        float* slot = swept.spot[swept.next];
        slot[0] = spot[0];
        slot[1] = spot[1];
        slot[2] = spot[2];
        swept.next = (swept.next + 1) % kSurveyedSpotsKept;
        if (swept.count < kSurveyedSpotsKept)
            ++swept.count;
    }

    void NoteSomethingToCheck(std::uint8_t* service, std::uint32_t soldier, std::uint32_t cp, std::uint32_t member,
                              std::uint8_t radioType)   // his call goes out unchanged
    {
        if (RoleKindOf(service, member) == kInvestigateRole)
        {
            return;   // an investigator who comes across it is still the sweep
        }
        float spot[3] = {};
        const NoticeRadio from = NoticeAiImpl_RadioFrom(soldier, spot);
        const bool blast = from == NoticeRadio::VehicleBlast;
        if (radioType != kSomethingToCheckRadio && !blast)
        {
            std::uint16_t flags = 0;
            std::int32_t noise = -1;
            bool wreck = false;
            const bool threat = NoticeAiImpl_ThreatNoise(soldier, flags, noise, wreck);
            return;   // 5 and 6 report every explosion: only a vehicle's brings a team (user)
        }
        if (from == NoticeRadio::Vehicle)
            return;   // user: the vehicle that drove at him is long gone by the time a team comes
        const bool fromBody = from == NoticeRadio::None && NoticeDownComradeAiImpl_FoundSpot(soldier, spot);
        if (from == NoticeRadio::None && !fromBody && !IsUp(LiveSoldiers(), soldier, spot))
            return;
        g_Service.store(service, std::memory_order_relaxed);   // a quiet CP may not have made a call of its own yet
        if (!FeatureSwitch::IsOn(FeatureSwitch::Feature::SurveyTeams))
            return;
        const int stealth = RevengeLevel::Stealth();
        if (stealth < kStealthForTeams)
        {
            return;
        }
        if (!blast && !RollTeam(stealth))
        {
            return;
        }
        std::lock_guard<std::mutex> lock(g_PendingMutex);
        SurveyTeam& survey = g_Survey[cp];
        if (survey.stage != Survey::None && survey.service == service)
        {
            return;   // user: one sweep per CP at a time
        }
        if (!blast && WasSurveyed(g_SurveyedSpot[cp], service, spot))   // a blast is over once it's swept; a body stays down
        {
            return;
        }
        survey = SurveyTeam{};
        survey.stage = Survey::Found;
        survey.service = service;
        survey.finder = soldier;
        survey.member = member;
        survey.fromBody = fromBody;
        if (fromBody)
        {
            survey.comrade = NoticeDownComradeAiImpl_FoundComrade(soldier);
            if ((survey.comrade & kTypeMask) != kSoldierType)
                survey.comrade = kNoComrade;
        }
        survey.radioType = radioType;
        survey.blast = blast;
        survey.spot[0] = spot[0];
        survey.spot[1] = spot[1];
        survey.spot[2] = spot[2];
        survey.untilMs = GetTickCount64() + kSurveyCallMs;
    }

    void NoteEquipmentReport(std::uint32_t soldier, std::uint32_t cp, std::uint32_t member)   // his type stays 4: vanilla's investigators still come
    {
        float heard[3] = {};
        if (!FeatureSwitch::IsOn(FeatureSwitch::Feature::SoldierRadioAnswers) || !NoticeAiImpl_HeardNoiseAt(soldier, heard))
            return;
        const DestroyedEquipment what = TppPermanentGimmickUnit_DestroyedAt(heard);
        if (what == DestroyedEquipment::Other)
            return;
        const bool comms = what == DestroyedEquipment::Comms;
        {
            std::lock_guard<std::mutex> lock(g_PendingMutex);
            g_EquipmentReports[cp] = EquipmentReport{ soldier, member, comms ? g_CommsLabel : g_PowerLabel, GetTickCount64() + kEquipmentReportMs };
        }
    }

    std::uint16_t __fastcall hk_RequestRadioByMember(std::uint8_t* cpController, std::uint32_t client, std::uint32_t local,
                                                     std::uint32_t radioType, std::uint32_t label)
    {
        std::uint32_t soldier = kNoSoldier;
        std::uint32_t cp = kNoCp;
        std::uint32_t member = kNoMember;
        const bool contactLine = label != 0 && label == g_LineHashes[kContactLine];   // the alert caller's own post, no radio role (user)
        if ((label == 0 || contactLine) && !MissionCodeGuard::ShouldBypassHooks() && MemberOfClient(cpController, client, local, soldier, cp, member))
        {
            RadioOrder order;
            {
                std::lock_guard<std::mutex> lock(g_PendingMutex);
                RadioOrder& stored = g_RadioOrders[cp];
                if (!contactLine && !stored.queued && stored.soldier == soldier && !stored.sent && GetTickCount64() < stored.untilMs)
                {
                    stored.sent = true;
                    stored.member = member;
                    order = stored;
                }
            }
            if (order.sent)
            {
                return g_OrigRequestRadioByMember(cpController, client, local, order.type, order.label);
            }
            if (HoldupSelfRelease_TakeRadioCall(soldier))
            {
                {
                    std::lock_guard<std::mutex> lock(g_PendingMutex);
                    g_HeldUpReports[cp] = HeldUpReport{ soldier, false, false, 0, GetTickCount64() };
                }
                return g_OrigRequestRadioByMember(cpController, client, local, kCautionStartRadio, 0);
            }
            if (radioType == kSearchCompleteRadio)
            {
                bool uavChecker = false;
                {
                    std::lock_guard<std::mutex> lock(g_PendingMutex);
                    const SurveyTeam& survey = g_Survey[cp];
                    for (std::uint32_t i = 0; survey.uav && survey.stage == Survey::Out && i < survey.teamSize; ++i)
                        uavChecker = survey.team[i] == member || uavChecker;
                }
                if (uavChecker)   // user: the wrecked UAV is no false alarm, his end report puts the CP in Caution
                {
                    return g_OrigRequestRadioByMember(cpController, client, local, kCautionStartRadio, 0);
                }
            }
            if (radioType == kSomethingToCheckRadio || radioType == kExplosionRadio || radioType == kExplosionAgainRadio)
                NoteSomethingToCheck(cpController - kServiceToCpController, soldier, cp, member, static_cast<std::uint8_t>(radioType));
            else if (radioType == kInstallationRadio)
                NoteEquipmentReport(soldier, cp, member);
            Line line = kLineCount;
            {
                std::lock_guard<std::mutex> lock(g_PendingMutex);
                ContactCall& alert = g_Contact[cp];
                RoleCall& call = g_RoleCalls[cp];
                if (alert.soldier == soldier && !alert.sent && radioType == kAlertBroadcastRadio)
                {
                    alert.sent = true;
                    alert.untilMs = GetTickCount64() + kContactWaitMs;   // from here on: how long his posted call may stay off the air
                    alert.member = member;
                    line = kContactLine;
                }
                else if (call.soldier == soldier && !call.sent && GetTickCount64() < call.untilMs)
                {
                    call.sent = true;
                    line = call.line;
                }
            }
            if (line == kContactLine)   // it ends as the CP's own 0x3D: the CP reacts to it as to its broadcast
            {
                return g_OrigRequestRadioByMember(cpController, client, local, kAlertBroadcastRadio, g_LineHashes[line]);
            }
            if (line != kLineCount)
            {
                return g_OrigRequestRadioByMember(cpController, client, local, kAnyLabelRadio, g_LineHashes[line]);
            }
        }
        return g_OrigRequestRadioByMember(cpController, client, local, radioType, label);
    }

    void NameNextCall(std::uint32_t cp, Line line, std::uint32_t soldier, std::uint32_t member)
    {
        if (cp >= kCpCount)
            return;
        std::lock_guard<std::mutex> lock(g_PendingMutex);
        g_Named[cp] = NamedCall{ g_LineHashes[line], soldier, member, GetTickCount64() + kNamedCallMs, false };
    }

    void OrderStrikeByCp(std::uint8_t* service, std::uint32_t cp)
    {
        std::uint32_t member = kNoMember;
        const std::uint32_t operatorSoldier = RoleHolder(service, cp, LiveSoldiers(), kMortarRole, true, member);
        if (operatorSoldier != kNoSoldier)
            NameNextCall(cp, kOrderLine, operatorSoldier, member);
        {
            std::lock_guard<std::mutex> lock(g_PendingMutex);
            const std::uint64_t now = GetTickCount64();
            g_Pending[cp] = PendingMortar{ Pending::CpOrder, now + kRetryForMs, now };
        }
        g_OrigRequestRadioByCp(service, cp, kMortarOrderRadio, kNoMember, g_LineHashes[kOrderLine]);
    }

    bool AssignRequester(std::uint8_t* service, std::uint32_t cp, std::uint32_t radioType = kMortarOrderRadio)   // someone holds the radio role after it
    {
        __try
        {
            void* roles = *reinterpret_cast<void* const*>(service + kServiceRoleController);
            const auto assign = (*reinterpret_cast<AssignRadioInCombat_t* const*>(roles))[kAssignRadioInCombatSlot];
            assign(roles, cp, radioType);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        return RadioRoleHolder(service, cp, LiveSoldiers()) != kNoSoldier;
    }

    bool GiveRadioRole(std::uint8_t* service, std::uint32_t member, std::uint8_t radioType)   // AssignRadioInCombat's own hand-over, to a chosen man
    {
        __try
        {
            std::uint8_t* role = *reinterpret_cast<std::uint8_t* const*>(service + kServiceRoles) + member * kRoleSize;
            const std::uint16_t locator = *reinterpret_cast<const std::uint16_t*>(role);
            if ((role[kRoleKind] == 0 || role[kRoleKind] == kGuardRole) && locator != kNoLocator)   // he leaves his combat post
            {
                std::uint8_t* roles = *reinterpret_cast<std::uint8_t* const*>(service + kServiceRoleController);
                std::uint8_t& onLocator = (*reinterpret_cast<std::uint8_t* const*>(roles + kRoleLocators))[locator * kLocatorSize + kLocatorMembers];
                if (onLocator)
                    --onLocator;
            }
            const auto setMemberRole = (*reinterpret_cast<SetMemberRole_t* const*>(service))[kSetMemberRoleSlot];
            setMemberRole(service, member, kRadioRole, &radioType, sizeof(radioType));
            return role[kRoleKind] == kRadioRole;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ResignRadio(std::uint8_t* service, std::uint32_t cp)   // everyone on the CP's radio role goes back to no role
    {
        __try
        {
            void* roles = *reinterpret_cast<void* const*>(service + kServiceRoleController);
            const auto resign = (*reinterpret_cast<ResignRadio_t* const*>(roles))[kResignRadioSlot];
            resign(roles, cp);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        return true;
    }

    void ResignRadioMan(std::uint8_t* service, std::uint32_t cp, std::uint32_t soldier)   // the role keeps him calling 0x3D every ~25 s
    {
        if (RadioRoleHolder(service, cp, LiveSoldiers()) != soldier || !ResignRadio(service, cp))
            return;
    }

    void CallOutUnderChaff(std::uint32_t soldier)
    {
        const std::uint8_t* soldiers = LiveSoldiers();
        float at[3] = {};
        if (soldier < kSoldierCount && soldiers && CanTalk(soldiers, soldier, at))
            SoldierVoiceSwap_SayCallOut(soldier);
    }

    std::uint32_t StrikeCallerUnderChaff(std::uint8_t* service, std::uint32_t cp)
    {
        const std::uint32_t radioMan = RadioRoleHolder(service, cp, LiveSoldiers());
        if (radioMan != kNoSoldier)
        {
            std::lock_guard<std::mutex> lock(g_PendingMutex);
            const RoleCall& call = g_RoleCalls[cp];
            return call.soldier == radioMan && call.line == kRequestLine ? radioMan : kNoSoldier;
        }
        if (!AssignRequester(service, cp))
            return kNoSoldier;
        const std::uint32_t picked = RadioRoleHolder(service, cp, LiveSoldiers());
        if (picked == kNoSoldier || !ResignRadio(service, cp))   // vanilla's pick, but the role would have him pose and call
            return kNoSoldier;
        return picked;
    }

    bool GiveCallTo(std::uint8_t* service, std::uint32_t cp, std::uint32_t soldier, std::uint8_t radioType, Line line)
    {
        const std::uint8_t* soldiers = LiveSoldiers();
        std::uint32_t member = kNoMember;
        std::uint32_t memberCp = kNoCp;
        float at[3] = {};
        if (soldier >= kSoldierCount || !soldiers || !CanTalk(soldiers, soldier, at) || !FindMember(service, soldier, member, memberCp)
            || memberCp != cp || RadioRoleHolder(service, cp, soldiers) != kNoSoldier)
            return false;
        const std::uint8_t role = RoleKindOf(service, member);
        if ((role != 0 && role != kGuardRole) || !GiveRadioRole(service, member, radioType))
            return false;
        std::lock_guard<std::mutex> lock(g_PendingMutex);
        RoleCall call;
        call.soldier = soldier;
        call.line = line;
        call.untilMs = GetTickCount64() + kRoleCallMs;
        g_RoleCalls[cp] = call;
        return true;
    }

    std::uint8_t* PointOf(std::uint8_t* service, std::uint32_t cp)
    {
        return *reinterpret_cast<std::uint8_t* const*>(service + kServicePoints) + cp * kPointSize;
    }

    std::uint16_t PointToken(std::uint32_t cp, const std::uint8_t* point)   // what the role payload and CompleteInvestigation carry
    {
        return static_cast<std::uint16_t>(((cp & 0x7F) << 7) | (*reinterpret_cast<const std::uint16_t*>(point + kPointId) & 0x7F));
    }

    bool PointBusy(std::uint8_t* service, std::uint32_t cp)   // the CP's one point already has investigators: nobody is sent twice
    {
        __try
        {
            const std::uint8_t* point = PointOf(service, cp);
            return (point[kPointFlags] & kPointActive) && point[kPointInvestigators] != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return true;
        }
    }

    bool PointIsOurs(std::uint8_t* service, std::uint32_t cp, std::uint16_t token)
    {
        __try
        {
            const std::uint8_t* point = PointOf(service, cp);
            return (point[kPointFlags] & kPointActive) && PointToken(cp, point) == token;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ReadPointAt(std::uint8_t* service, std::uint32_t cp, float* out)
    {
        __try
        {
            const float* at = reinterpret_cast<const float*>(PointOf(service, cp));
            out[0] = at[0];
            out[1] = at[1];
            out[2] = at[2];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool CanInvestigate(std::uint8_t* service, std::uint32_t member, float* position)   // AssignInvestigation's own test
    {
        __try
        {
            const std::uint8_t* record = *reinterpret_cast<std::uint8_t* const*>(service + kServiceMemberRadioFlags) + member * kMemberRadioFlagsSize;
            const std::uint32_t flags = *reinterpret_cast<const std::uint32_t*>(record);
            if ((flags & kMemberUp) != kMemberUp || (flags & kInvestigatorBusy) || !(record[kMemberAbilities] & kCanInvestigate))
                return false;
            const float* at = reinterpret_cast<const float*>(record + kMemberPosition);
            position[0] = at[0];
            position[1] = at[1];
            position[2] = at[2];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        return RoleKindOf(service, member) == 0;
    }

    void PickSurveyTeam(std::uint8_t* service, std::uint32_t cp, SurveyTeam& survey, std::uint32_t most)   // the nearest free soldiers to the spot, never the finder
    {
        std::uint8_t* cpController = service + kServiceToCpController;
        const std::uint32_t total = MemberTotal(cpController);
        float distance[kSurveyTeamMost] = {};
        float where[kSurveyTeamMost][3] = {};
        survey.teamSize = 0;
        for (std::uint32_t member = 0; member < total; ++member)
        {
            std::uint16_t gameObjectId = 0;
            std::uint8_t memberCp = 0;
            float at[3] = {};
            bool heldKnown = false;
            if (member == survey.member || !MemberEntry(cpController, member, gameObjectId, memberCp) || memberCp != cp
                || (gameObjectId & kTypeMask) != kSoldierType || !CanInvestigate(service, member, at)
                || CpRadioResponse::IsHeld(static_cast<std::uint16_t>(gameObjectId & kIndexMask), heldKnown)
                || GetTickCount64() < g_FinderWatchUntilMs[gameObjectId & kIndexMask].load(std::memory_order_relaxed))
                continue;
            const float dx = at[0] - survey.spot[0];
            const float dy = at[1] - survey.spot[1];
            const float dz = at[2] - survey.spot[2];
            const float d = dx * dx + dy * dy + dz * dz;
            std::uint32_t slot = survey.teamSize;
            while (slot > 0 && distance[slot - 1] > d)
                --slot;
            if (slot >= most)
                continue;
            for (std::uint32_t i = (survey.teamSize < most ? survey.teamSize : most - 1); i > slot; --i)
            {
                distance[i] = distance[i - 1];
                survey.team[i] = survey.team[i - 1];
                where[i][0] = where[i - 1][0];
                where[i][1] = where[i - 1][1];
                where[i][2] = where[i - 1][2];
            }
            distance[slot] = d;
            survey.team[slot] = member;
            where[slot][0] = at[0];
            where[slot][1] = at[1];
            where[slot][2] = at[2];
            if (survey.teamSize < most)
                ++survey.teamSize;
        }
        std::uint32_t kept = survey.teamSize ? 1 : 0;   // as AssignSelectTargetInvestigation: the rest within 100 m of the nearest
        for (std::uint32_t i = 1; i < survey.teamSize; ++i)
        {
            if (NearSpot(where[i], where[0], kSurveyTeamSpread))
                survey.team[kept++] = survey.team[i];
        }
        survey.teamSize = kept;
    }

    bool RequestSurveyPoint(std::uint8_t* service, std::uint32_t cp, const float* spot)
    {
        struct InvestigationRequest
        {
            float         x, y, z;
            std::uint16_t cause;
            std::uint8_t  kind;
            std::uint8_t  pad;
        } request{ spot[0], spot[1], spot[2], kNoCause, kNoticeInvestigation, 0 };
        __try
        {
            const auto requestInvestigation = (*reinterpret_cast<RequestInvestigation_t* const*>(service))[kRequestInvestigationSlot];
            requestInvestigation(service, cp, &request);
            return (PointOf(service, cp)[kPointFlags] & kPointActive) != 0;   // it clamps the spot into the CP's area, or drops it
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool GiveInvestigateRole(std::uint8_t* service, std::uint32_t cp, std::uint32_t member)   // as AssignInvestigation does it
    {
        __try
        {
            std::uint8_t* point = PointOf(service, cp);
            std::uint8_t payload[0x14] = {};
            *reinterpret_cast<float*>(payload + 0x0) = *reinterpret_cast<const float*>(point + 0x0);
            *reinterpret_cast<float*>(payload + 0x4) = *reinterpret_cast<const float*>(point + 0x4);
            *reinterpret_cast<float*>(payload + 0x8) = *reinterpret_cast<const float*>(point + 0x8);
            *reinterpret_cast<std::uint16_t*>(payload + 0xC) = PointToken(cp, point);
            *reinterpret_cast<std::uint16_t*>(payload + 0xE) = *reinterpret_cast<const std::uint16_t*>(point + kPointArea);
            *reinterpret_cast<std::uint16_t*>(payload + 0x10) = *reinterpret_cast<const std::uint16_t*>(point + kPointCause) & kNoCause;
            std::uint8_t* role = *reinterpret_cast<std::uint8_t* const*>(service + kServiceRoles) + member * kRoleSize;
            const std::uint16_t locator = *reinterpret_cast<const std::uint16_t*>(role);
            if ((role[kRoleKind] == 0 || role[kRoleKind] == kGuardRole) && locator != kNoLocator)
            {
                std::uint8_t* roles = *reinterpret_cast<std::uint8_t* const*>(service + kServiceRoleController);
                std::uint8_t& onLocator = (*reinterpret_cast<std::uint8_t* const*>(roles + kRoleLocators))[locator * kLocatorSize + kLocatorMembers];
                if (onLocator)
                    --onLocator;
            }
            const auto setMemberRole = (*reinterpret_cast<SetMemberRole_t* const*>(service))[kSetMemberRoleSlot];
            setMemberRole(service, member, kInvestigateRole, payload, sizeof(payload));
            ++point[kPointInvestigators];   // Caution's own assigner skips a point that has investigators
            return role[kRoleKind] == kInvestigateRole;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool StillOurs(std::uint8_t* service, std::uint32_t member, std::uint16_t token)   // the game hasn't given him another job since
    {
        __try
        {
            const std::uint8_t* role = *reinterpret_cast<std::uint8_t* const*>(service + kServiceRoles) + member * kRoleSize;
            return role[kRoleKind] == kInvestigateRole && *reinterpret_cast<const std::uint16_t*>(role + kRolePointToken) == token;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void TakeRoleBack(std::uint8_t* service, std::uint32_t member)   // StepRadioToSneak's own release
    {
        const std::uint16_t none = kNoLocator;
        __try
        {
            const auto setMemberRole = (*reinterpret_cast<SetMemberRole_t* const*>(service))[kSetMemberRoleSlot];
            setMemberRole(service, member, 0, &none, sizeof(none));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void ClearPoint(std::uint8_t* service, std::uint32_t cp)
    {
        __try
        {
            void* roles = *reinterpret_cast<void* const*>(service + kServiceRoleController);
            const auto clear = (*reinterpret_cast<ClearInvestigation_t* const*>(roles))[kClearInvestigationSlot];
            clear(roles, cp, kAllKinds);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void EndBlastReport(const SurveyTeam& survey)   // no team after all: vanilla's own point for his report may still go
    {
        if (survey.blast && survey.finder < kSoldierCount)
            g_BlastReporterUntilMs[survey.finder].store(0, std::memory_order_relaxed);
    }

    void ReleaseFinder(std::uint32_t finder)   // his Wait step sends him back to his route on its next update
    {
        if (finder >= kSoldierCount)
            return;
        g_FinderHoldUntilMs[finder].store(0, std::memory_order_relaxed);
        g_FinderWatchUntilMs[finder].store(0, std::memory_order_relaxed);
    }

    void ReportDown(std::uint8_t* service, std::uint32_t cp, SurveyTeam& survey, const std::uint16_t* down, std::uint32_t count)   // user: his CP asks him for a SITREP
    {
        survey.missingTeam = CpRadioResponse::ReportMissing(down, count, survey.missingTeam);
        for (std::uint32_t i = 0; i < count; ++i)
            LogDebug("SurveyProbe: cp %u: soldier %u of the %s team was taken out, the CP will ask for a SITREP", cp,
                     static_cast<std::uint32_t>(down[i]), survey.second ? "second" : "first");
        std::lock_guard<std::mutex> lock(g_PendingMutex);
        SurveyAsked& asked = g_SurveyAsked[cp];
        if (asked.team == survey.missingTeam)
            return;
        asked = SurveyAsked{};
        asked.service = service;
        asked.team    = survey.missingTeam;
        asked.spot[0] = survey.spot[0];
        asked.spot[1] = survey.spot[1];
        asked.spot[2] = survey.spot[2];
        asked.second  = survey.second;
    }

    void SendSurveyTeam(std::uint8_t* service, std::uint32_t cp, SurveyTeam survey)
    {
        ReleaseFinder(survey.finder);   // user: once they start the search he goes back to his route
        if (survey.waker < kSoldierCount)
            g_Waker[survey.waker].store(false, std::memory_order_relaxed);
        std::uint32_t members[kSurveyTeamMost] = {};
        std::uint32_t count = 0;
        std::uint16_t down[kSurveyTeamMost] = {};
        std::uint32_t downCount = 0;
        const std::uint8_t* liveSoldiers = LiveSoldiers();
        for (std::uint32_t i = 0; i < survey.teamSize; ++i)
        {
            float at[3] = {};
            bool heldKnown = false;
            const bool reporter = survey.blast && survey.team[i] == survey.member;   // his notice may still flag him busy from his own line
            if (reporter ? RoleKindOf(service, survey.team[i]) == 0 && IsUp(LiveSoldiers(), survey.finder, at)
                               && !CpRadioResponse::IsHeld(static_cast<std::uint16_t>(survey.finder), heldKnown)
                         : CanInvestigate(service, survey.team[i], at))
            {
                members[count++] = survey.team[i];
                continue;
            }
            std::uint16_t gameObjectId = 0;
            std::uint8_t memberCp = 0;
            if (liveSoldiers && MemberEntry(service + kServiceToCpController, survey.team[i], gameObjectId, memberCp)
                && (gameObjectId & kTypeMask) == kSoldierType && OutOfAction(liveSoldiers, gameObjectId & kIndexMask))
                down[downCount++] = static_cast<std::uint16_t>(gameObjectId & kIndexMask);   // taken out before the sweep, the man seeing to the comrade too
        }
        if (downCount)
            ReportDown(service, cp, survey, down, downCount);
        const bool busy = count != 0 && (PointBusy(service, cp) || NoInvestigatorsAllowed(service, cp));
        if (count == 0 || busy || !RequestSurveyPoint(service, cp, survey.spot))
        {
            {
                std::lock_guard<std::mutex> lock(g_PendingMutex);
                g_Survey[cp] = SurveyTeam{};
            }
            EndBlastReport(survey);
            return;
        }
        survey.token = PointToken(cp, PointOf(service, cp));
        ReadPointAt(service, cp, survey.pointAt);
        survey.teamSize = 0;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            if (!GiveInvestigateRole(service, cp, members[i]))
                continue;
            survey.team[survey.teamSize] = members[i];
            survey.reported[survey.teamSize] = false;
            ++survey.teamSize;
        }
        if (survey.teamSize == 0)
        {
            ClearPoint(service, cp);
            {
                std::lock_guard<std::mutex> lock(g_PendingMutex);
                g_Survey[cp] = SurveyTeam{};
            }
            EndBlastReport(survey);
            return;
        }
        std::int32_t soldiers[kSurveyTeamMost] = { -1, -1 };
        for (std::uint32_t i = 0; i < survey.teamSize; ++i)
        {
            std::uint16_t gameObjectId = 0;
            std::uint8_t memberCp = 0;
            survey.teamSoldier[i] = kNoSoldier;
            if (MemberEntry(service + kServiceToCpController, survey.team[i], gameObjectId, memberCp))
            {
                survey.teamSoldier[i] = gameObjectId & kIndexMask;
                soldiers[i] = static_cast<std::int32_t>(survey.teamSoldier[i]);
            }
        }
        survey.stage = Survey::Out;
        survey.untilMs = GetTickCount64() + kSurveyMostMs;
        {
            std::lock_guard<std::mutex> lock(g_PendingMutex);
            g_Survey[cp] = survey;
            if (!survey.blast && !survey.uav && !survey.second)
                RememberSurveyed(g_SurveyedSpot[cp], service, survey.spot);
        }
#ifdef _DEBUG
        for (std::uint32_t i = 0; i < survey.teamSize; ++i)
        {
            if (survey.teamSoldier[i] < kSoldierCount)
                g_ProbeSurveyCp[survey.teamSoldier[i]].store(static_cast<std::uint16_t>(cp + 1), std::memory_order_relaxed);
        }
        LogDebug("SurveyProbe: cp %u: %s team out to (%.1f, %.1f, %.1f): soldier %d and soldier %d", cp, survey.second ? "second" : "first",
                 survey.spot[0], survey.spot[1], survey.spot[2], soldiers[0], soldiers[1]);
#endif
    }

    void StartWake(std::uint8_t* service, std::uint32_t cp, SurveyTeam survey)   // user: the team sees to the downed comrade first, then sweeps
    {
        std::uint32_t waker = kNoSoldier;
        for (std::uint32_t i = 0; i < survey.teamSize && waker == kNoSoldier; ++i)   // nearest first
        {
            float at[3] = {};
            std::uint16_t gameObjectId = 0;
            std::uint8_t memberCp = 0;
            if (CanInvestigate(service, survey.team[i], at) && MemberEntry(service + kServiceToCpController, survey.team[i], gameObjectId, memberCp))
                waker = gameObjectId & kIndexMask;
        }
        if (waker == kNoSoldier)
        {
            survey.wakeDone = true;
            SendSurveyTeam(service, cp, survey);
            return;
        }
        survey.stage = Survey::Waking;
        survey.waker = waker;
        survey.untilMs = GetTickCount64() + kWakeStartMs;
        {
            std::lock_guard<std::mutex> lock(g_PendingMutex);
            SurveyTeam& stored = g_Survey[cp];
            if (stored.stage != Survey::Dispatch || stored.finder != survey.finder)
                return;
            stored = survey;
        }
        g_Waker[waker].store(true, std::memory_order_relaxed);   // before the notice: his node can wake on the next AI tick
        if (!SoldierNotice::Add(waker, kFirstSightNotice, survey.comrade, nullptr))   // as if he had seen the body himself
        {
            survey.wakeDone = true;
            SendSurveyTeam(service, cp, survey);
            return;
        }
    }

    void AnswerCpCall(std::uint8_t* service, std::uint32_t cp, std::uint32_t soldier, std::uint32_t label, bool pose);

    void AskUavReply(std::uint8_t* service, std::uint32_t cp, SurveyTeam survey)   // the radio role wakes his RadioAi: pose, then his call
    {
        survey.replied = true;
        std::uint16_t gameObjectId = 0;
        std::uint8_t memberCp = 0;
        float at[3] = {};
        const std::uint8_t* soldiers = LiveSoldiers();
        if (survey.teamSize && !ChaffIsUp() && soldiers
            && MemberEntry(service + kServiceToCpController, survey.team[0], gameObjectId, memberCp)
            && CanTalk(soldiers, gameObjectId & kIndexMask, at))
        {
            survey.replier = gameObjectId & kIndexMask;
            AnswerCpCall(service, cp, survey.replier, g_UavReplyLabel, true);
        }
        bool replying = false;
        {
            std::lock_guard<std::mutex> lock(g_PendingMutex);
            replying = survey.replier != kNoSoldier && g_RadioOrders[cp].soldier == survey.replier;   // the CP can turn the role down
            SurveyTeam& stored = g_Survey[cp];
            if (stored.stage != Survey::Dispatch || stored.service != service)
                return;
            survey.stage = replying ? Survey::Replying : Survey::Dispatch;
            survey.untilMs = GetTickCount64() + kUavReplyMostMs;
            stored = survey;
        }
        if (!replying)
            SendSurveyTeam(service, cp, survey);
    }

    void UavReplyTick(std::uint8_t* service, std::uint32_t cp, const SurveyTeam& survey)   // he goes once his call is over
    {
        RadioOrder reply;
        {
            std::lock_guard<std::mutex> lock(g_PendingMutex);
            reply = g_RadioOrders[cp];
        }
        const std::uint64_t now = GetTickCount64();
        if (reply.soldier == survey.replier)
        {
            const bool stalled = !reply.onAir && now - reply.postedMs > kUavReplyAirMs;
            if (!reply.done && !stalled && now <= survey.untilMs)
                return;
            {
                std::lock_guard<std::mutex> lock(g_PendingMutex);
                if (g_RadioOrders[cp].soldier == survey.replier)
                    g_RadioOrders[cp] = RadioOrder{};
            }
            ResignRadioMan(service, cp, survey.replier);   // the role would keep him calling
        }
        float at[3] = {};
        if (!CanInvestigate(service, survey.team[0], at) && now <= survey.untilMs)
            return;   // his radio flags can take a tick to clear
        SendSurveyTeam(service, cp, survey);
    }

    bool SurveyTeamAllDown(const SurveyTeam& survey)   // nobody left to sweep
    {
        const std::uint8_t* soldiers = LiveSoldiers();
        if (!soldiers || survey.teamSize == 0)
            return false;
        for (std::uint32_t i = 0; i < survey.teamSize; ++i)
        {
            if (survey.teamSoldier[i] >= kSoldierCount || !OutOfAction(soldiers, survey.teamSoldier[i]))
                return false;
        }
        return true;
    }

    void ReportTakenOut(std::uint8_t* service, std::uint32_t cp, SurveyTeam& survey)   // user: any member the player takes out gets his CP's SITREP
    {
        const std::uint8_t* soldiers = LiveSoldiers();
        if (!soldiers)
            return;
        std::uint16_t down[kSurveyTeamMost] = {};
        std::uint32_t count = 0;
        for (std::uint32_t i = 0; i < survey.teamSize; ++i)
        {
            if (survey.reported[i] || survey.teamSoldier[i] >= kSoldierCount || !OutOfAction(soldiers, survey.teamSoldier[i]))
                continue;
            survey.reported[i] = true;
            down[count++] = static_cast<std::uint16_t>(survey.teamSoldier[i]);
        }
        if (count == 0)
            return;
        ReportDown(service, cp, survey, down, count);
        std::lock_guard<std::mutex> lock(g_PendingMutex);
        SurveyTeam& stored = g_Survey[cp];
        if (stored.stage != Survey::Out || stored.service != service || stored.token != survey.token)
            return;
        stored.missingTeam = survey.missingTeam;
        for (std::uint32_t i = 0; i < survey.teamSize; ++i)
            stored.reported[i] = survey.reported[i];
    }

    void EndSurvey(std::uint8_t* service, std::uint32_t cp, const SurveyTeam& survey)   // they go back to their posts, and the point goes if it's still this job
    {
        bool anyOurs = false;
        for (std::uint32_t i = 0; i < survey.teamSize; ++i)
            anyOurs = StillOurs(service, survey.team[i], survey.token) || anyOurs;
        const bool pointOurs = PointIsOurs(service, cp, survey.token);
        for (std::uint32_t i = 0; i < survey.teamSize; ++i)
        {
            if (StillOurs(service, survey.team[i], survey.token))
                TakeRoleBack(service, survey.team[i]);
        }
        float pointNow[3] = {};
        const bool unmoved = ReadPointAt(service, cp, pointNow) && NearSpot(pointNow, survey.pointAt, 1.0f);
        if (pointOurs && (anyOurs || unmoved))   // overwriting a point keeps its id: moved and nobody of ours on it, it's another job now
            ClearPoint(service, cp);
#ifdef _DEBUG
        for (std::uint32_t i = 0; i < survey.teamSize; ++i)
        {
            if (survey.teamSoldier[i] < kSoldierCount)
                g_ProbeSurveyCp[survey.teamSoldier[i]].store(0, std::memory_order_relaxed);
        }
        LogDebug("SurveyProbe: cp %u: the %s team's survey is over", cp, survey.second ? "second" : "first");
#endif
        std::lock_guard<std::mutex> lock(g_PendingMutex);
        SurveyTeam& stored = g_Survey[cp];
        if (stored.stage == Survey::Out && stored.service == service && stored.token == survey.token)
            stored = SurveyTeam{};
    }

    void StartSecondTeam(std::uint8_t* service, std::uint32_t cp)   // user: nobody answered the SITREP, so the CP sends a second team to the spot
    {
        SurveyAsked asked;
        SurveyTeam current;
        {
            std::lock_guard<std::mutex> lock(g_PendingMutex);
            asked = g_SurveyAsked[cp];
            g_SurveyAsked[cp] = SurveyAsked{};
            current = g_Survey[cp];
        }
        if (asked.service != service || !FeatureSwitch::IsOn(FeatureSwitch::Feature::SurveyTeams))
            return;
        if (current.stage == Survey::Out && current.missingTeam == asked.team)
            EndSurvey(service, cp, current);   // what's left of the first team goes back to their posts
        else if (current.stage != Survey::None)
            return;   // the CP is on another find already
        if (PointBusy(service, cp) || NoInvestigatorsAllowed(service, cp))
            return;
        SurveyTeam second;
        second.service  = service;
        second.spot[0]  = asked.spot[0];
        second.spot[1]  = asked.spot[1];
        second.spot[2]  = asked.spot[2];
        second.wakeDone = true;
        second.second   = true;
        PickSurveyTeam(service, cp, second, kSurveyTeamMost);
        if (second.teamSize == 0)
            return;
        second.stage = Survey::Dispatch;   // the next idle tick sends them
        std::lock_guard<std::mutex> lock(g_PendingMutex);
        if (g_Survey[cp].stage == Survey::None)
            g_Survey[cp] = second;
    }

    void SurveyTick(std::uint8_t* service, std::uint32_t cp)
    {
        bool sendSecond = false;
        {
            std::lock_guard<std::mutex> lock(g_PendingMutex);
            sendSecond = g_SurveyAsked[cp].sendTeam;
        }
        if (sendSecond)
        {
            StartSecondTeam(service, cp);
            return;
        }
        SurveyTeam survey;
        {
            std::lock_guard<std::mutex> lock(g_PendingMutex);
            survey = g_Survey[cp];
            if (survey.stage != Survey::None && survey.service != service)
            {
                g_Survey[cp] = SurveyTeam{};   // an earlier mission's
                return;
            }
            if ((survey.stage == Survey::Found || survey.stage == Survey::Told) && GetTickCount64() > survey.untilMs)
            {
                g_Survey[cp] = SurveyTeam{};
                EndBlastReport(survey);
                ReleaseFinder(survey.finder);
                return;
            }
        }
        if (survey.stage == Survey::Replying)
        {
            UavReplyTick(service, cp, survey);
            return;
        }
        if (survey.stage == Survey::Dispatch)
        {
            if (survey.uav && !survey.replied)
                AskUavReply(service, cp, survey);
            else if (survey.fromBody && survey.comrade != kNoComrade && !survey.wakeDone)
                StartWake(service, cp, survey);
            else
                SendSurveyTeam(service, cp, survey);
            return;
        }
        if (survey.stage == Survey::Waking)
        {
            if (GetTickCount64() <= survey.untilMs)
                return;
            survey.wakeDone = true;
            SendSurveyTeam(service, cp, survey);
            return;
        }
        if (survey.stage != Survey::Out)
            return;
        if (!survey.calledOff)
            ReportTakenOut(service, cp, survey);
        bool anyOurs = false;
        for (std::uint32_t i = 0; i < survey.teamSize; ++i)
            anyOurs = StillOurs(service, survey.team[i], survey.token) || anyOurs;
        const bool pointOurs = PointIsOurs(service, cp, survey.token);
        const bool late = GetTickCount64() > survey.untilMs;
        const bool allDown = !survey.calledOff && SurveyTeamAllDown(survey);
        if (anyOurs && pointOurs && !late && !survey.calledOff && !allDown)
            return;
        EndSurvey(service, cp, survey);
    }

    void DecideSurvey(std::uint32_t cp, std::uint32_t finder, std::uint8_t radioType)   // as his call goes on air, voiced or skipped for a far player
    {
        SurveyTeam survey;
        {
            std::lock_guard<std::mutex> lock(g_PendingMutex);
            survey = g_Survey[cp];
        }
        if (survey.stage != Survey::Found || survey.finder != finder || survey.radioType != radioType || !survey.service)
            return;
        survey.busy = PointBusy(survey.service, cp) || NoInvestigatorsAllowed(survey.service, cp);   // user: vanilla's own gates
        if (survey.blast && survey.busy)
        {
            {
                std::lock_guard<std::mutex> lock(g_PendingMutex);
                SurveyTeam& stored = g_Survey[cp];
                if (stored.stage == Survey::Found && stored.finder == finder)
                    stored = SurveyTeam{};
            }
            return;
        }
        PickSurveyTeam(survey.service, cp, survey, survey.blast ? kSurveyTeamMost - 1 : kSurveyTeamMost);
        bool heldKnown = false;
        if (survey.blast && !CpRadioResponse::IsHeld(static_cast<std::uint16_t>(finder), heldKnown))
            survey.team[survey.teamSize++] = survey.member;   // "Move to investigate, ASAP." is said to him
        if (survey.blast && survey.teamSize == 0)
        {
            {
                std::lock_guard<std::mutex> lock(g_PendingMutex);
                SurveyTeam& stored = g_Survey[cp];
                if (stored.stage == Survey::Found && stored.finder == finder)
                    stored = SurveyTeam{};
            }
            return;
        }
        if (survey.busy)
            survey.teamSize = 0;
        survey.stage = Survey::Told;
        survey.untilMs = GetTickCount64() + kSurveyCallMs;
        {
            std::lock_guard<std::mutex> lock(g_PendingMutex);
            SurveyTeam& stored = g_Survey[cp];
            if (stored.stage != Survey::Found || stored.finder != finder)
                return;
            stored = survey;
        }
        if (survey.fromBody && survey.teamSize && survey.comrade != kNoComrade && finder < kSoldierCount)   // user: after CPR0142 nobody comes, so he checks the body himself
            g_FinderHoldUntilMs[finder].store(GetTickCount64() + kFinderHoldMs, std::memory_order_relaxed);
        if (survey.blast && finder < kSoldierCount)
            g_BlastReporterUntilMs[finder].store(GetTickCount64() + kBlastReportMs, std::memory_order_relaxed);
    }

    void OrderUavCheck(std::uint8_t* service, std::uint32_t cp, const float* spot)   // user: the CP says CPR0510 and sends the nearest soldier
    {
        SurveyTeam survey;
        survey.service = service;
        survey.spot[0] = spot[0];
        survey.spot[1] = spot[1];
        survey.spot[2] = spot[2];
        survey.radioType = kUavDownRadio;
        survey.uav = true;
        const bool busy = PointBusy(service, cp) || NoInvestigatorsAllowed(service, cp);   // vanilla's own gates
        if (!busy)
            PickSurveyTeam(service, cp, survey, 1);
        bool sending = false;
        if (survey.teamSize)
        {
            std::lock_guard<std::mutex> lock(g_PendingMutex);
            SurveyTeam& stored = g_Survey[cp];
            if (stored.stage == Survey::None || stored.service != service)
            {
                survey.stage = Survey::Told;   // his call's end sends him, as for a find
                survey.untilMs = GetTickCount64() + kSurveyCallMs;
                stored = survey;
                sending = true;
            }
        }
        g_OrigRequestRadioByCp(service, cp, kUavDownRadio, kNoMember, 0);
    }

    void DecideStrikeCall(std::uint8_t* service, std::uint32_t cp, bool again = false, std::uint32_t caller = kNoSoldier)
    {
        {
            std::lock_guard<std::mutex> lock(g_PendingMutex);
            g_StrikeOpenedMs[cp] = GetTickCount64();   // the operator's ready call waits for it
            if (!again)
                ++g_StrikeSerial[cp];   // chaff lost the last request: the same strike, the same ready call
        }
        const bool hisCall = GiveCallTo(service, cp, caller, static_cast<std::uint8_t>(kMortarOrderRadio), kRequestLine);
        if (!hisCall && !AssignRequester(service, cp))
        {
            OrderStrikeByCp(service, cp);
            return;
        }
        {
            std::lock_guard<std::mutex> lock(g_PendingMutex);
            const std::uint64_t now = GetTickCount64();
            g_Pending[cp] = PendingMortar{ Pending::AwaitRequester, now + kRequesterWaitMs, now };
        }
        if (!hisCall)
            GiveRoleCall(service, cp, kRequestLine);
    }

    bool ReadSequence(std::uint8_t* speechHandler, std::uint8_t sequence, std::uint16_t& cp, std::uint32_t& label, std::uint8_t*& service)
    {
        __try
        {
            const std::uint8_t* record = *reinterpret_cast<std::uint8_t* const*>(speechHandler + kHandlerSequences) + sequence * kSequenceSize;
            if (*reinterpret_cast<const std::uint16_t*>(record + kSequenceMember) != kNoMember)
                return false;
            cp = *reinterpret_cast<const std::uint16_t*>(record + kSequenceCp);
            service = *reinterpret_cast<std::uint8_t* const*>(speechHandler + kHandlerService);
            void* sequencer = *reinterpret_cast<void* const*>(speechHandler + kHandlerSequencer);
            label = (*reinterpret_cast<SequenceLabel_t* const*>(sequencer))[kSequenceLabelSlot](sequencer, sequence);
            return service != nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool SpeaksOwnVoice(std::uint8_t* service, std::uint32_t member)
    {
        __try
        {
            const std::uint8_t* flags = *reinterpret_cast<const std::uint8_t* const*>(service + kServiceMemberRadioFlags);
            return (*reinterpret_cast<const std::uint32_t*>(flags + member * kMemberRadioFlagsSize) & kOwnVoiceFlag) != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint32_t NamedSpeakerFor(std::uint8_t* speechHandler, std::uint8_t sequence, const std::uint8_t*& soldiers,
                                  std::uint32_t& member, std::uint8_t*& service)
    {
        std::uint16_t cp = 0;
        std::uint32_t label = 0;
        if (!ReadSequence(speechHandler, sequence, cp, label, service) || cp >= kCpCount)
            return kNoSoldier;
        g_Service.store(service, std::memory_order_relaxed);
        NamedCall named;
        {
            std::lock_guard<std::mutex> lock(g_PendingMutex);
            named = g_Named[cp];
        }
        const std::uint64_t now = GetTickCount64();
        if (named.label != label || now > named.untilMs || named.soldier == kNoSoldier)
        {
            if (label != g_LineHashes[kOrderLine])
                return kNoSoldier;
            named = NamedCall{ label, kNoSoldier, kNoMember, now + kNamedCallMs, false };
            soldiers = LiveSoldiers();
            named.soldier = RoleHolder(service, cp, soldiers, kMortarRole, true, named.member);
            if (named.soldier == kNoSoldier)
                return kNoSoldier;
            std::lock_guard<std::mutex> lock(g_PendingMutex);
            g_Named[cp] = named;
        }
        soldiers = LiveSoldiers();
        float at[3] = {};
        if (!soldiers || !CanTalk(soldiers, named.soldier, at))
            return kNoSoldier;
        if (!named.logged)
        {
            std::lock_guard<std::mutex> lock(g_PendingMutex);
            g_Named[cp].logged = true;
        }
        member = named.member;
        return named.soldier;
    }

    PlayingSequence NoteRadioPlaying(std::uint8_t* speechHandler, std::uint8_t sequence)   // CallPart runs every frame a call is on air
    {
        std::uint16_t cp = 0;
        std::uint16_t member = 0;
        std::uint16_t gameObjectId = 0;
        __try
        {
            const std::uint8_t* record = *reinterpret_cast<std::uint8_t* const*>(speechHandler + kHandlerSequences) + sequence * kSequenceSize;
            cp = *reinterpret_cast<const std::uint16_t*>(record + kSequenceCp);
            member = *reinterpret_cast<const std::uint16_t*>(record + kSequenceMember);
            gameObjectId = *reinterpret_cast<const std::uint16_t*>(record + kSequenceGameObjectId);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return PlayingSequence{};
        }
        if (cp >= kCpCount)
            return PlayingSequence{};
        const std::uint64_t now = GetTickCount64();
        if (member == kNoMember)
        {
            g_CpRadioMs[cp].store(now, std::memory_order_relaxed);
            return PlayingSequence{ cp, kNoSoldier };
        }
        if ((gameObjectId & kTypeMask) != kSoldierType)
            return PlayingSequence{ cp, kNoSoldier };
        g_SoldierRadioMs[gameObjectId & kIndexMask].store(now, std::memory_order_relaxed);
        return PlayingSequence{ cp, static_cast<std::uint32_t>(gameObjectId & kIndexMask) };
    }

    struct PartPatch
    {
        std::uint16_t* record   = nullptr;   // the sequence record, at +0x08
        void**         voice    = nullptr;
        std::uint16_t  member   = 0;
        std::uint16_t  soldier  = 0;
        std::uint16_t  objectId = 0;
        void*          oldVoice = nullptr;
    };

    bool NameInPart(std::uint8_t* speechHandler, std::uint8_t sequence, std::uint32_t soldier, std::uint32_t member,
                    void* soundController, PartPatch& patch)   // CallPart takes the call sign from the record and waits on his voice
    {
        __try
        {
            std::uint8_t* record = *reinterpret_cast<std::uint8_t* const*>(speechHandler + kHandlerSequences) + sequence * kSequenceSize;
            auto* named = reinterpret_cast<std::uint16_t*>(record + kSequenceMember);
            patch.record   = named;
            patch.voice    = reinterpret_cast<void**>(speechHandler + kHandlerPartVoice);
            patch.member   = named[0];
            patch.soldier  = named[1];
            patch.objectId = named[2];
            patch.oldVoice = *patch.voice;
            named[0] = static_cast<std::uint16_t>(member);
            named[1] = static_cast<std::uint16_t>(soldier);
            named[2] = static_cast<std::uint16_t>(kSoldierType | soldier);
            *patch.voice = soundController;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void UnnameInPart(const PartPatch& patch)   // FinishSequence and StartPart see the CP's own call again
    {
        __try
        {
            patch.record[0] = patch.member;
            patch.record[1] = patch.soldier;
            patch.record[2] = patch.objectId;
            *patch.voice = patch.oldVoice;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    bool __fastcall hk_CallPart(std::uint8_t* speechHandler, std::uint8_t sequence)
    {
        if (MissionCodeGuard::ShouldBypassHooks())
            return g_OrigCallPart(speechHandler, sequence);
        const PlayingSequence playing = NoteRadioPlaying(speechHandler, sequence);
        const PlayingSequence outer = t_Sequence;
        t_Sequence = playing;   // the voices CallPart starts belong to this call
        const std::uint8_t* soldiers = nullptr;
        std::uint32_t member = kNoMember;
        std::uint8_t* service = nullptr;
        const std::uint32_t speaker = NamedSpeakerFor(speechHandler, sequence, soldiers, member, service);
        void* soundController = nullptr;
        PartPatch patch;
        bool patched = false;
        if (speaker != kNoSoldier && member != kNoMember)
        {
            __try
            {
                soundController = *reinterpret_cast<void* const*>(soldiers + kSoldiersSoundController);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                soundController = nullptr;
            }
            patched = soundController && NameInPart(speechHandler, sequence, speaker, member, soundController, patch);
        }
        if (patched && !SpeaksOwnVoice(service, member))
            t_PartSpeaker = PartSpeaker{ soundController, speaker };
        const bool done = g_OrigCallPart(speechHandler, sequence);
        t_PartSpeaker = PartSpeaker{};
        t_Sequence = outer;
        if (patched)
            UnnameInPart(patch);
        return done;
    }

    void __fastcall hk_ActivateChaff(std::uint8_t* rangeAttack, const void* request, std::uint32_t requestId)
    {
        g_OrigActivateChaff(rangeAttack, request, requestId);
        g_RangeAttack.store(rangeAttack, std::memory_order_release);
        g_ChaffAtMs.store(GetTickCount64(), std::memory_order_relaxed);
    }

    bool AirsCpCalls(std::uint8_t* service, std::uint32_t cp)   // cp::ActionControllerImpl::StateRadio: patrols and guard posts only get their HQ calls on air, these other three nothing
    {
        __try
        {
            CpQuery_t const* cpVtable = *reinterpret_cast<CpQuery_t const* const*>(service);
            return !cpVtable[kIsLrrpCpSlot](service, cp) && !cpVtable[kIsOuterBaseCpSlot](service, cp)
                && !cpVtable[kIsMarchCpSlot](service, cp) && !cpVtable[kIsChildCpSlot](service, cp) && !cpVtable[kIsSkullCpSlot](service, cp);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return true;
        }
    }

    bool SendsHqRadioMan(std::uint8_t* service, std::uint32_t cp)   // AlertAiImpl::StepNormal's own pick right after its 0x3D: a guard post radios HQ
    {
        __try
        {
            CpQuery_t const* cpVtable = *reinterpret_cast<CpQuery_t const* const*>(service);
            return !cpVtable[kIsChildCpSlot](service, cp) && !cpVtable[kIsMarchCpSlot](service, cp) && !cpVtable[kIsSkullCpSlot](service, cp)
                && (cpVtable[kIsLrrpCpSlot](service, cp) || cpVtable[kIsOuterBaseCpSlot](service, cp))
                && cpVtable[kHasBaseToCallSlot](service, cp);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool SpottedUnderChaff(std::uint8_t* service, std::uint32_t cp)
    {
        if (!ChaffIsUp())
            return false;
        bool callsAtOnce = false;   // AlertAiImpl::StepNormal calls once its alert is 3.0 s old, these CPs at once
        __try
        {
            CpQuery_t const* cpVtable = *reinterpret_cast<CpQuery_t const* const*>(service);
            callsAtOnce = cpVtable[kIsChildCpSlot](service, cp) || cpVtable[kIsMarchCpSlot](service, cp) || cpVtable[kIsSkullCpSlot](service, cp);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            callsAtOnce = false;
        }
        return GetTickCount64() - g_ChaffAtMs.load(std::memory_order_relaxed) >= (callsAtOnce ? 0 : kAlertCallDelayMs);   // up before he was spotted
    }

    bool CallContactIn(std::uint8_t* service, std::uint32_t cp)   // vanilla's reinforcement picker hands one of them the radio
    {
        if (!SpottedUnderChaff(service, cp))
        {
            return false;   // user: the soldier calls it in only when Snake was spotted while chaff was up
        }
        if (SendsHqRadioMan(service, cp))
        {
            return false;   // user: guard posts stay vanilla, his radio role would block that HQ call
        }
        const std::uint32_t busy = RadioRoleHolder(service, cp, LiveSoldiers());
        if (busy != kNoSoldier)
        {
            return false;   // AssignRadioInCombat would just leave the job with him
        }
        if (!AssignRequester(service, cp, kAlertBroadcastRadio))
            return false;
        const std::uint32_t caller = RadioRoleHolder(service, cp, LiveSoldiers());
        if (caller == kNoSoldier || !ResignRadio(service, cp))   // vanilla's pick, but he only talks: the role is what makes him pose (user)
            return false;
        {
            std::lock_guard<std::mutex> lock(g_PendingMutex);
            g_Contact[cp] = ContactCall{ caller, kNoMember, GetTickCount64() + kContactWaitMs, false, false };
        }
        CpRadioResponse::MemberRadio call;
        call.label = g_LineHashes[kContactLine];
        if (!CpRadioResponse::RequestMemberRadio(static_cast<std::uint16_t>(kSoldierType | caller), call))
        {
            std::lock_guard<std::mutex> lock(g_PendingMutex);
            g_Contact[cp] = ContactCall{};
            return false;
        }
        return true;
    }

    void StartRadioOrder(std::uint8_t* service, std::uint32_t cp, RadioOrder order)   // Lua's AssignRadioInCombat, on the CP's own tick
    {
        std::uint32_t type = hk_ConvertSpeechLabelToRadioType(order.label);   // as CallRadio does: the CP acts on what the label means
        if (type == kUnknownLabelRadio)
            type = kAnyLabelRadio;
        bool alertCall = false;
        {
            std::lock_guard<std::mutex> lock(g_PendingMutex);
            alertCall = g_Contact[cp].soldier != kNoSoldier;
        }
        const std::uint32_t busy = RadioRoleHolder(service, cp, LiveSoldiers());
        std::uint32_t soldier = kNoSoldier;
        std::uint32_t member = kNoMember;
        std::uint32_t memberCp = kNoCp;
        const char* refused = nullptr;
        const bool needsRadioRole = !order.noPose || order.soldier == kNoSoldier;   // a named man who only talks waits in the CP's queue
        if (needsRadioRole && (alertCall || busy != kNoSoldier))
            refused = "its radio is taken by another call";
        else if (order.soldier == kNoSoldier)
        {
            if (AssignRequester(service, cp, type))
                soldier = RadioRoleHolder(service, cp, LiveSoldiers());
            if (soldier == kNoSoldier || !FindMember(service, soldier, member, memberCp))
                refused = "nobody there can take the radio";
            else if (order.noPose && !ResignRadio(service, cp))   // vanilla's pick, but the role is what makes him pose
                refused = "its radio man didn't give the role back";
        }
        else
        {
            if (!FindMember(service, order.soldier, member, memberCp) || memberCp != cp || OutOfAction(LiveSoldiers(), order.soldier))
                refused = "the soldier has left it, or is down or held up";
            else if (!order.noPose && !GiveRadioRole(service, member, static_cast<std::uint8_t>(type)))
                refused = "the soldier didn't take the radio role";
            else
                soldier = order.soldier;
        }
        {
            std::lock_guard<std::mutex> lock(g_PendingMutex);
            RadioOrder& stored = g_RadioOrders[cp];
            if (!stored.queued)
                return;
            stored = RadioOrder{};
            if (!refused)
            {
                stored.soldier = soldier;
                stored.label = order.label;
                stored.type = type;
                stored.noPose = order.noPose;
                stored.reply = order.reply;
                stored.sent = order.noPose;   // his member radio carries the label itself
                stored.member = order.noPose ? member : kNoMember;
                stored.postedMs = GetTickCount64();
                stored.untilMs = stored.postedMs + (order.noPose ? kNoPoseCallMs : kRoleCallMs);
            }
        }
        if (!refused && order.noPose)
        {
            CpRadioResponse::MemberRadio call;
            call.label = order.label;
            if (!CpRadioResponse::RequestMemberRadio(static_cast<std::uint16_t>(kSoldierType | soldier), call))
            {
                std::lock_guard<std::mutex> lock(g_PendingMutex);
                if (g_RadioOrders[cp].soldier == soldier)
                    g_RadioOrders[cp] = RadioOrder{};
                refused = "his radio call was refused";
            }
        }
        if (refused && !order.reply)
            LogWarn("AssignRadioInCombat: CP %u: %s, dropped", cp, refused);
    }

    void AnswerCpCall(std::uint8_t* service, std::uint32_t cp, std::uint32_t soldier, std::uint32_t label, bool pose)
    {
        RadioOrder reply;
        {
            std::lock_guard<std::mutex> lock(g_PendingMutex);
            RadioOrder& stored = g_RadioOrders[cp];
            if (stored.queued || stored.soldier != kNoSoldier)
                return;   // the CP's radio already carries a call of ours
            stored = RadioOrder{};
            stored.queued = true;
            stored.noPose = !pose;
            stored.reply = true;
            stored.soldier = soldier;
            stored.label = label;
            stored.untilMs = GetTickCount64() + (pose ? kRoleCallMs : kNoPoseCallMs);
            reply = stored;
        }
        StartRadioOrder(service, cp, reply);
    }

    std::uint16_t __fastcall hk_RequestRadioByCp(std::uint8_t* service, std::uint32_t cp, std::uint32_t radioType,
                                                 std::uint32_t member, std::uint32_t label)
    {
        g_Service.store(service, std::memory_order_relaxed);
        if (radioType == kAlertBroadcastRadio && member == kNoMember && label == 0 && cp < kCpCount
            && !MissionCodeGuard::ShouldBypassHooks() && CallContactIn(service, cp))
            return kNoHandle;
        if (radioType != kMortarOrderRadio || member != kNoMember || label != 0 || cp >= kCpCount || MissionCodeGuard::ShouldBypassHooks()
            || !(InAlert(service, cp) || PlayerAlertPhase::Read() == PlayerAlertPhase::kAlert)   // the CP's own phase reads 0 now and then in alert
            || !AirsCpCalls(service, cp))
            return g_OrigRequestRadioByCp(service, cp, radioType, member, label);
        if (ChaffIsUp())
        {
            std::lock_guard<std::mutex> lock(g_PendingMutex);
            const std::uint64_t now = GetTickCount64();
            const PendingMortar waiting = g_Pending[cp];
            PendingMortar decide{ Pending::Decide, now + kRetryForMs, now };
            if (waiting.kind != Pending::None && waiting.callOutSaid)
            {
                decide.caller = waiting.caller;
                decide.callOutSaid = true;
            }
            g_Pending[cp] = decide;
            g_StrikeOpenedMs[cp] = now;
            return kNoHandle;
        }
        {
            std::lock_guard<std::mutex> lock(g_PendingMutex);
            if (g_Contact[cp].soldier != kNoSoldier)   // the radio-role pick would take the radio off the man calling the alert in
            {
                const std::uint64_t now = GetTickCount64();
                g_Pending[cp] = PendingMortar{ Pending::Decide, now + kRetryForMs + kContactWaitMs, now };
                g_StrikeOpenedMs[cp] = now;
                return kNoHandle;
            }
        }
        DecideStrikeCall(service, cp);
        return kNoHandle;
    }

    std::uint8_t* WorkOf(std::uint8_t* mortarAction, std::uint32_t slot)
    {
        __try
        {
            return *reinterpret_cast<std::uint8_t* const*>(mortarAction + kMortarWorks)
                + (slot - *reinterpret_cast<const std::uint32_t*>(mortarAction + kMortarFirstSlot)) * kMortarWorkSize;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    bool ReadWork(std::uint8_t* work, std::uint8_t& next, std::uint8_t& flags, std::uint32_t& soldier)
    {
        __try
        {
            next = work[kWorkNextState];
            flags = work[kWorkFlags];
            soldier = *reinterpret_cast<const std::uint32_t*>(work + kWorkSoldier);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void WriteNextState(std::uint8_t* work, std::uint8_t next)
    {
        __try
        {
            work[kWorkNextState] = next;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    bool StrikeCallOpen(std::uint32_t cp, std::uint64_t now)
    {
        return (g_StrikeOpenedMs[cp] && now - g_StrikeOpenedMs[cp] < kStrikeCallMostMs) || g_Pending[cp].kind != Pending::None;
    }

    void ReleaseRound(ReadyHold& hold, std::uint64_t now)
    {
        hold.releasedMs = now;
        hold.startedMs = 0;
        if (hold.line != kFlareLine)
            g_ReadyDoneSerial[hold.soldier] = hold.serial;
        if (hold.cp < kCpCount && g_ReadyHolder[hold.cp] == hold.soldier + 1)
            g_ReadyHolder[hold.cp] = 0;
    }

    bool PhaseWants(Line line)   // user: flares and CPR0190 ready calls in caution and evasion only; alert strikes fire without a ready call
    {
        UNREFERENCED_PARAMETER(line);
        const int phase = PlayerAlertPhase::Read();
        return phase == PlayerAlertPhase::kCaution || phase == PlayerAlertPhase::kEvasion;
    }

    std::uint32_t CpOfSoldier(std::uint8_t* service, std::uint32_t soldier, std::uint64_t now)
    {
        {
            std::lock_guard<std::mutex> lock(g_PendingMutex);
            if (g_SoldierCp[soldier] && now - g_SoldierCpMs[soldier] < kSoldierCpFreshMs)
                return g_SoldierCp[soldier] - 1;
        }
        std::uint32_t member = kNoMember;
        std::uint32_t cp = kNoCp;
        if (!FindMember(service, soldier, member, cp))
            return kNoCp;
        std::lock_guard<std::mutex> lock(g_PendingMutex);
        g_SoldierCp[soldier] = cp + 1;
        g_SoldierCpMs[soldier] = now;
        return cp;
    }

    bool HoldRound(std::uint32_t soldier, Line line)
    {
        if (soldier >= kSoldierCount)
            return false;
        const std::uint64_t now = GetTickCount64();
        std::uint8_t* service = g_Service.load(std::memory_order_relaxed);
        if (!service || !PhaseWants(line))
        {
            std::lock_guard<std::mutex> lock(g_PendingMutex);
            if (!g_Ready[soldier].startedMs)
                return false;   // a round that gets no call in this phase
        }
        enum class Act { Hold, Start, Post, Jam } act = Act::Hold;
        std::uint32_t cp = CpOfSoldier(service, soldier, now);
        if (cp == kNoCp || !AirsCpCalls(service, cp))
            return false;
        const bool inFlight = CpRadioResponse::HasMemberRadio(static_cast<std::uint16_t>(kSoldierType | soldier));
        const bool chaffUp = ChaffIsUp();
        {
            std::lock_guard<std::mutex> lock(g_PendingMutex);
            ReadyHold& hold = g_Ready[soldier];
            if (!hold.startedMs)
            {
                if (line == kFlareLine ? hold.releasedMs && now - hold.releasedMs < kReadyOncePerMs
                                       : g_ReadyDoneSerial[soldier] == g_StrikeSerial[cp])
                    return false;   // flares: once a minute a man; a strike: one ready call, its later rounds fire freely
                act = Act::Start;
            }
            else if (hold.done)
                ReleaseRound(hold, now);
            else if (chaffUp)
            {
                hold.capFromMs = now;   // no call gets through: his wait starts over once the chaff clears
                if (!hold.onAirMs && !hold.jammed && (hold.askedMs || hold.line == kFlareLine || !StrikeCallOpen(hold.cp, now)))
                {
                    hold.jammed = true;
                    if (!hold.askedMs)
                        hold.askedMs = now;
                }
                if (hold.jammed && !hold.callOutSaid)
                {
                    hold.callOutSaid = true;
                    act = Act::Jam;
                }
            }
            else if (hold.jammed)
            {
                hold.jammed = false;
                hold.callOutSaid = false;
                hold.refused = false;
                hold.onAirMs = 0;
                hold.capFromMs = now;
                hold.askedMs = inFlight ? now : 0;
                hold.lastAskMs = now;
            }
            else if (!hold.askedMs && hold.line != kFlareLine && StrikeCallOpen(hold.cp, now))
                hold.capFromMs = now;
            else if (hold.refused)
                ReleaseRound(hold, now);
            else if (now - hold.capFromMs > kReadyHoldMostMs)
                ReleaseRound(hold, now);
            else if (hold.askedMs && !hold.onAirMs && now - hold.askedMs > kReadyAirMostMs)
                ReleaseRound(hold, now);
            else if (hold.onAirMs && now - hold.onAirMs > kReadyAnswerMostMs)
                ReleaseRound(hold, now);
            else if (hold.askedMs && !hold.onAirMs && !inFlight && now - hold.lastAskMs >= kReadyRepostMs)
            {
                hold.lastAskMs = now;
                act = Act::Post;
                cp = hold.cp;
            }
            else if (!hold.askedMs && (hold.line == kFlareLine || !StrikeCallOpen(hold.cp, now)))
            {
                hold.askedMs = now;
                hold.lastAskMs = now;
                if (hold.line != kFlareLine)
                    hold.serial = g_StrikeSerial[hold.cp];   // the strike call that just went off air
                act = Act::Post;
                cp = hold.cp;
            }
            if (act == Act::Hold && !hold.startedMs)
                return false;
        }

        if (act == Act::Start)
        {
            std::lock_guard<std::mutex> lock(g_PendingMutex);
            if (line == kFlareLine)
            {
                if (g_FlareCalledMs[cp] && now - g_FlareCalledMs[cp] < kFlareCallGapMs)
                    return false;
                g_FlareCalledMs[cp] = now;
            }
            ReadyHold hold;
            hold.soldier = soldier;
            hold.line = line;
            hold.cp = cp;
            hold.serial = g_StrikeSerial[cp];
            hold.startedMs = now;
            hold.capFromMs = now;
            g_Ready[soldier] = hold;
            g_ReadyHolder[cp] = soldier + 1;
            return true;
        }
        if (act == Act::Jam)
            CallOutUnderChaff(soldier);
        else if (act == Act::Post)
        {
            CpRadioResponse::MemberRadio call;
            call.label = g_LineHashes[line];   // his own call: his call sign, his voice, the CP's part in the same call
            const bool posted = CpRadioResponse::RequestMemberRadio(static_cast<std::uint16_t>(kSoldierType | soldier), call);
            if (!posted)
            {
                std::lock_guard<std::mutex> lock(g_PendingMutex);
                ReleaseRound(g_Ready[soldier], now);
                return false;
            }
        }
        return true;
    }

    bool NewCommand(std::uint8_t* commandsOwner, std::uint32_t soldier, std::uint16_t workGimmick, std::uint16_t workCommand, bool& flare)
    {
        __try
        {
            const std::uint8_t* command = *reinterpret_cast<std::uint8_t* const*>(commandsOwner + kCommandTable) + soldier * kCommandSize;
            if (command[kCommandType] != kMortarCommandType || *reinterpret_cast<const std::uint16_t*>(command + kCommandId) == workCommand
                || *reinterpret_cast<const std::uint16_t*>(command + kCommandGimmick) != workGimmick)
                return false;
            flare = (command[kCommandFlare] & 1) != 0;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool PendingRealTarget(std::uint8_t* mortarAction, std::uint32_t slot, std::uint32_t& soldier, bool& flare)
    {
        std::uint8_t* commands = nullptr;
        std::uint16_t gimmick = 0;
        std::uint16_t commandId = 0;
        __try
        {
            const std::uint8_t* work = *reinterpret_cast<std::uint8_t* const*>(mortarAction + kMortarWorks)
                + (slot - *reinterpret_cast<const std::uint32_t*>(mortarAction + kMortarFirstSlot)) * kMortarWorkSize;
            if ((work[kWorkFlags] & kWorkRegistration) != 1)
                return false;
            soldier = *reinterpret_cast<const std::uint32_t*>(work + kWorkSoldier);
            gimmick = *reinterpret_cast<const std::uint16_t*>(work + kWorkGimmick);
            commandId = *reinterpret_cast<const std::uint16_t*>(work + kWorkCommandId);
            commands = *reinterpret_cast<std::uint8_t* const*>(mortarAction + kMortarCommands);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        return commands && NewCommand(commands, soldier, gimmick, commandId, flare);
    }

    bool PendingUnrealTarget(std::uint8_t* mortarAction, std::uint32_t soldier, bool& flare)
    {
        std::uint8_t* commands = nullptr;
        std::uint16_t gimmick = 0;
        std::uint16_t commandId = 0;
        __try
        {
            const std::uint8_t* work = *reinterpret_cast<std::uint8_t* const*>(mortarAction + kUnrealWorks)
                + (soldier - *reinterpret_cast<const std::uint32_t*>(mortarAction + kUnrealFirstSoldier)) * kUnrealWorkSize;
            if ((work[kUnrealFlags] & kWorkRegistration) != 1)
                return false;
            gimmick = *reinterpret_cast<const std::uint16_t*>(work + kUnrealWorkGimmick);
            commandId = *reinterpret_cast<const std::uint16_t*>(work + kUnrealWorkCommandId);
            commands = *reinterpret_cast<std::uint8_t* const*>(mortarAction + kUnrealCommands);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        return commands && NewCommand(commands, soldier, gimmick, commandId, flare);
    }

    void __fastcall hk_State_MortarAction(std::uint8_t* mortarAction, std::uint32_t slot, std::int32_t proc, void* data)
    {
        std::uint32_t soldier = kNoSoldier;
        bool flare = false;
        if (proc == kStateUpdate && !MissionCodeGuard::ShouldBypassHooks() && PendingRealTarget(mortarAction, slot, soldier, flare)
            && HoldRound(soldier, flare ? kFlareLine : kReadyLine))
            return;
        g_OrigMortarAction(mortarAction, slot, proc, data);
    }

    void __fastcall hk_Unreal_State_MortarAction(std::uint8_t* mortarAction, std::uint32_t soldier, std::int32_t proc)
    {
        bool flare = false;
        if (proc == kStateUpdate && !MissionCodeGuard::ShouldBypassHooks() && PendingUnrealTarget(mortarAction, soldier, flare)
            && HoldRound(soldier, flare ? kFlareLine : kReadyLine))
            return;
        g_OrigUnrealMortarAction(mortarAction, soldier, proc);
    }

    bool ReadUnrealWork(std::uint8_t* mortarAction, std::uint32_t soldier, std::uint8_t*& work, std::uint8_t& next, std::uint8_t& flags)
    {
        __try
        {
            work = *reinterpret_cast<std::uint8_t* const*>(mortarAction + kUnrealWorks)
                + (soldier - *reinterpret_cast<const std::uint32_t*>(mortarAction + kUnrealFirstSoldier)) * kUnrealWorkSize;
            next = work[kUnrealNextState];
            flags = work[kUnrealFlags];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void WriteUnrealNextState(std::uint8_t* work, std::uint8_t next)
    {
        __try
        {
            work[kUnrealNextState] = next;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void __fastcall hk_Unreal_State_WaitAction(std::uint8_t* mortarAction, std::uint32_t soldier, std::int32_t proc)
    {
        if (proc != kStateUpdate || MissionCodeGuard::ShouldBypassHooks())
        {
            g_OrigUnrealWaitAction(mortarAction, soldier, proc);
            return;
        }
        std::uint8_t* work = nullptr;
        std::uint8_t nextBefore = 0;
        std::uint8_t flags = 0;
        const bool known = ReadUnrealWork(mortarAction, soldier, work, nextBefore, flags);
        g_OrigUnrealWaitAction(mortarAction, soldier, proc);
        std::uint8_t next = 0;
        if (!known || !ReadUnrealWork(mortarAction, soldier, work, next, flags) || next != kFiringState || nextBefore == kFiringState
            || (flags & kWorkRegistration) != 1)
            return;
        if (HoldRound(soldier, (flags & kUnrealFlareShell) ? kFlareLine : kReadyLine))
            WriteUnrealNextState(work, nextBefore);
    }

    void __fastcall hk_State_WaitAction(std::uint8_t* mortarAction, std::uint32_t slot, std::int32_t proc, void* data)
    {
        if (proc != kStateUpdate || MissionCodeGuard::ShouldBypassHooks())
        {
            g_OrigWaitAction(mortarAction, slot, proc, data);
            return;
        }
        std::uint8_t* work = WorkOf(mortarAction, slot);
        std::uint8_t nextBefore = 0;
        std::uint8_t flags = 0;
        std::uint32_t soldier = kNoSoldier;
        const bool known = work && ReadWork(work, nextBefore, flags, soldier);
        g_OrigWaitAction(mortarAction, slot, proc, data);
        std::uint8_t next = 0;
        if (!known || !ReadWork(work, next, flags, soldier) || next != kFiringState || nextBefore == kFiringState
            || (flags & kWorkRegistration) != 1)
            return;
        if (HoldRound(soldier, (flags & kWorkFlareShell) ? kFlareLine : kReadyLine))
            WriteNextState(work, nextBefore);
    }

    void __fastcall hk_Heli2Unrealize(std::uint8_t* heli, std::uint32_t instance, std::uint32_t realizedIndex)
    {
        g_OrigHeliUnrealize(heli, instance, realizedIndex);
        bool any = false;
        {
            std::lock_guard<std::mutex> lock(g_PendingMutex);
            for (GunshipCall& call : g_Gunship)
            {
                any = any || call.called || call.pendingMs;
                call = GunshipCall{};   // the heli left: each CP may call it in again (user)
            }
        }
        g_SupportHeliCheckedMs.store(0, std::memory_order_relaxed);
    }

    std::uint32_t SupportHeliId()
    {
        const std::uint64_t now = GetTickCount64();
        if (now - g_SupportHeliCheckedMs.load(std::memory_order_relaxed) >= kSupportHeliRecheckMs)
        {
            std::uint32_t id = 0xFFFFFFFF;
            if (!GetGameObjectIdByName("TppHeli2", "SupportHeli", id))
                id = 0xFFFFFFFF;
            g_SupportHeliId.store(id, std::memory_order_relaxed);
            g_SupportHeliCheckedMs.store(now, std::memory_order_relaxed);
        }
        return g_SupportHeliId.load(std::memory_order_relaxed);
    }

    bool HitByHeli(const void* damages, std::uint32_t count, std::uint16_t heli)
    {
        __try
        {
            const auto* damage = static_cast<const std::uint8_t*>(damages);
            for (std::uint32_t i = 0; i < count; ++i, damage += kDamageStride)
            {
                if (*reinterpret_cast<const std::uint16_t*>(damage + kDamageAttacker) == heli)
                    return true;
            }
            return false;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void GunshipFallback(std::uint32_t cp, std::uint32_t soldier);

    void CallGunship(std::uint8_t* service, std::uint32_t cp, std::uint32_t soldier, bool triedUnderChaff)
    {
        const bool hisCall = triedUnderChaff && GiveCallTo(service, cp, soldier, static_cast<std::uint8_t>(kGunshipRadio), kGunshipLine);
        if (hisCall || AssignRequester(service, cp, kGunshipRadio))
        {
            {
                std::lock_guard<std::mutex> lock(g_PendingMutex);
                g_Gunship[cp].awaitMs = GetTickCount64() + kGunshipAwaitMs;
            }
            if (!hisCall)
                GiveRoleCall(service, cp, kGunshipLine);
            return;
        }
        GunshipFallback(cp, soldier);
    }

    void GunshipFallback(std::uint32_t cp, std::uint32_t soldier)
    {
        const std::uint8_t* soldiers = LiveSoldiers();
        float at[3] = {};
        if (soldier >= kSoldierCount || !soldiers || !CanTalk(soldiers, soldier, at))
        {
            std::uint32_t member = kNoMember;
            soldier = RoleHolder(g_Service.load(std::memory_order_relaxed), cp, soldiers, kAnyRole, true, member);
        }
        if (soldier == kNoSoldier)
        {
            return;
        }
        CpRadioResponse::MemberRadio call;
        call.label = g_LineHashes[kGunshipLine];
        const bool posted = CpRadioResponse::RequestMemberRadio(static_cast<std::uint16_t>(kSoldierType | soldier), call);
    }

    bool __fastcall hk_CheckRadioTypeBgmPhaseMismatch(std::uint8_t radioType, std::int32_t bgmPhase)   // StateRadio drops most types under the combat music
    {
        const bool mismatch = g_OrigBgmPhaseMismatch(radioType, bgmPhase);
        if (!mismatch || MissionCodeGuard::ShouldBypassHooks())
            return mismatch;
        std::lock_guard<std::mutex> lock(g_PendingMutex);
        for (std::uint32_t cp = 0; cp < kCpCount; ++cp)
        {
            const RadioOrder& order = g_RadioOrders[cp];
            if (order.sent && !order.done && order.type == radioType)
            {
                return false;   // user: AssignRadioInCombat is heard in combat too
            }
        }
        return mismatch;
    }

    bool Hook(std::uintptr_t address, void* detour, void** original, const char* name)
    {
        void* target = ResolveGameAddress(address);
        if (!target)
        {
            LogError("CpRadioVariants: %s address missing on this build", name);
            return false;
        }
        if (!CreateAndEnableHook(target, detour, original))
        {
            LogError("CpRadioVariants: %s hook failed", name);
            return false;
        }
        return true;
    }

    void Unhook(std::uintptr_t address, void* original)
    {
        if (!original)
            return;
        if (void* target = ResolveGameAddress(address))
            DisableAndRemoveHook(target);
    }
}

void CpRadioVariants_OnRadioOnAir(std::uint32_t cp, std::uint8_t radioType, std::uint16_t speaker)
{
    if (cp >= kCpCount)
        return;
    const std::uint32_t soldier = (speaker >> 9) == kSoldierObjectType ? (speaker & kIndexMask) : kNoSoldier;
    if ((radioType == kSomethingToCheckRadio || radioType == kExplosionRadio || radioType == kExplosionAgainRadio) && soldier != kNoSoldier
        && !MissionCodeGuard::ShouldBypassHooks())
        DecideSurvey(cp, soldier, radioType);
    std::lock_guard<std::mutex> lock(g_PendingMutex);
    if (g_HeldUpReports[cp].soldier == soldier && soldier != kNoSoldier && radioType == kCautionStartRadio
        && !g_HeldUpReports[cp].replyMs && GetTickCount64() - g_HeldUpReports[cp].requestMs < kHeldUpReportMs)
        g_HeldUpReports[cp].onAir = true;
    g_Calls[cp] = CallOnAir{ GetTickCount64(), 0, false };
    if (radioType == kMortarOrderRadio)
        g_Pending[cp] = PendingMortar{};
    RoleCall& roleCall = g_RoleCalls[cp];
    if (roleCall.sent && roleCall.soldier == soldier && soldier != kNoSoldier)
    {
        if (roleCall.line == kRequestLine)
            g_Pending[cp] = PendingMortar{};
        else
            g_Gunship[cp].awaitMs = 0;
    }
    ContactCall& alert = g_Contact[cp];
    if (alert.sent && alert.soldier == soldier && soldier != kNoSoldier)
        alert.onAir = true;
    RadioOrder& order = g_RadioOrders[cp];
    if (order.sent && order.soldier == soldier && soldier != kNoSoldier)
        order.onAir = true;
    if (g_ReadyHolder[cp])
    {
        ReadyHold& hold = g_Ready[g_ReadyHolder[cp] - 1];
        if (hold.soldier == soldier && hold.askedMs && !hold.onAirMs)
            hold.onAirMs = GetTickCount64();
    }
}

void CpRadioVariants_OnRadioOffAir(std::uint32_t cp, std::uint8_t radioType, std::uint16_t speaker)
{
    if (cp >= kCpCount)
        return;
    const std::uint32_t soldier = (speaker >> 9) == kSoldierObjectType ? (speaker & kIndexMask) : kNoSoldier;
    std::lock_guard<std::mutex> lock(g_PendingMutex);
    HeldUpReport& report = g_HeldUpReports[cp];
    if (report.onAir && report.soldier == soldier && soldier != kNoSoldier)
    {
        report.onAir = false;
        report.replyDue = true;
    }
    else if (report.replyMs && radioType == kCpOnAlertRadio)
        report = HeldUpReport{};
    if (g_Contact[cp].onAir && g_Contact[cp].soldier == soldier && soldier < kSoldierCount)
    {
        g_Contact[cp].done = true;
        g_Contact[cp].cpAnswered = g_CpTurnMs[soldier].load(std::memory_order_relaxed) >= g_Calls[cp].onAirMs;
    }
    if (g_RadioOrders[cp].onAir && g_RadioOrders[cp].soldier == soldier && soldier != kNoSoldier)
        g_RadioOrders[cp].done = true;
    if ((radioType == kCameraAlertRadio || radioType == kUavAlertRadio) && soldier == kNoSoldier && g_Calls[cp].onAirMs
        && g_CpRadioMs[cp].load(std::memory_order_relaxed) >= g_Calls[cp].onAirMs)   // the CP's line was voiced, not skipped for a far player
        g_SpottedReplyDue[cp] = true;
    SurveyTeam& survey = g_Survey[cp];
    if (survey.stage == Survey::Told && survey.finder == soldier && radioType == survey.radioType)
    {
        if (survey.teamSize)
            survey.stage = Survey::Dispatch;   // the next idle tick sends them
        else
        {
            if (!survey.busy)
                RememberSurveyed(g_SurveyedSpot[cp], survey.service, survey.spot);
            survey = SurveyTeam{};
        }
    }
    if (radioType == kMortarOrderRadio)
        g_StrikeOpenedMs[cp] = 0;
    RoleCall& roleCall = g_RoleCalls[cp];
    if (roleCall.sent && roleCall.soldier == soldier && soldier != kNoSoldier)
    {
        if (roleCall.line == kRequestLine)
        {
            if (g_CpTurnMs[soldier].load(std::memory_order_relaxed) < g_Calls[cp].onAirMs && ChaffIsUp())
            {
                const std::uint64_t now = GetTickCount64();
                g_Pending[cp] = PendingMortar{ Pending::Resend, now + kRetryForMs, now, soldier, true };   // the jam already had him call out
                g_StrikeOpenedMs[cp] = now;
            }
            else
                g_StrikeOpenedMs[cp] = 0;
        }
        roleCall = RoleCall{};
    }
    if (g_ReadyHolder[cp])
    {
        ReadyHold& hold = g_Ready[g_ReadyHolder[cp] - 1];
        if (hold.soldier == soldier && hold.onAirMs && !hold.done && !hold.refused)
        {
            if (g_CpTurnMs[hold.soldier].load(std::memory_order_relaxed) < hold.onAirMs && ChaffIsUp())
            {
                hold.jammed = true;
                hold.onAirMs = 0;
            }
            else if (g_SpokeMs[hold.soldier].load(std::memory_order_relaxed) >= hold.onAirMs)
                hold.done = true;
            else
                hold.refused = true;
        }
    }
}

bool CpRadioVariants_HoldChaffJam(std::uint32_t cp, std::uint32_t soldierIndex)
{
    if (cp >= kCpCount || soldierIndex >= kSoldierCount || MissionCodeGuard::ShouldBypassHooks())
        return false;
    const std::uint64_t now = GetTickCount64();
    CallOnAir call;
    RadioChannel channel;
    {
        std::lock_guard<std::mutex> lock(g_PendingMutex);
        call = g_Calls[cp];
        channel = g_Channels[cp];
    }
    if (call.released || !call.onAirMs)
        return false;

    const bool spoke = g_SpokeMs[soldierIndex].load(std::memory_order_relaxed) >= call.onAirMs;
    const bool signedOff = g_SignedOffMs[soldierIndex].load(std::memory_order_relaxed) >= call.onAirMs;
    const bool cpTurn = g_CpTurnMs[soldierIndex].load(std::memory_order_relaxed) >= call.onAirMs;
    const bool stillTalking = channel.soundController && now - channel.startedMs <= kChannelFreshMs && channel.soldier == soldierIndex && ChannelPlaying(channel);
    const char* release = !spoke ? "before he said a word"
        : cpTurn ? "the CP's turn came"
        : signedOff && !stillTalking ? "after his Over!!"
        : call.heldSinceMs && now - call.heldSinceMs > kJamHoldMostMs ? "he took too long"
        : nullptr;

    bool wasHeld = false;
    {
        std::lock_guard<std::mutex> lock(g_PendingMutex);
        CallOnAir& held = g_Calls[cp];
        if (held.onAirMs != call.onAirMs)
            return false;   // a new call went on air meanwhile
        if (!release)
        {
            if (!held.heldSinceMs)
            {
                held.heldSinceMs = now;
            }
            return true;
        }
        held.released = true;
        wasHeld = held.heldSinceMs != 0;
    }
    if (wasHeld)
    {
        const std::uint8_t* soldiers = LiveSoldiers();
        const std::uint32_t slot = SoldierRealizedSlot::Of(soldiers, soldierIndex);
        if (slot != SoldierRealizedSlot::kNotRealized)
            SoldierVoiceSwap_SayJammedTake(soldiers, slot);
    }
    return false;
}

void CpRadioVariants_OnCpIdle(std::uint32_t cp, std::uint8_t* liveService)
{
    if (cp >= kCpCount || !g_OrigRequestRadioByCp || MissionCodeGuard::ShouldBypassHooks())
        return;
    if (liveService)
        g_Service.store(liveService, std::memory_order_relaxed);   // the cached one can be an earlier mission's
    std::uint8_t* service = liveService ? liveService : g_Service.load(std::memory_order_relaxed);
    if (!service)
        return;
    const std::uint64_t now = GetTickCount64();
    const std::int32_t phase = CpPhase(service, cp);
    if (phase == 0 && g_CpPhaseSeen[cp].exchange(phase, std::memory_order_relaxed) > 0)
        g_CpCalmedMs[cp].store(now, std::memory_order_relaxed);
    else if (phase > 0)
        g_CpPhaseSeen[cp].store(phase, std::memory_order_relaxed);
    SurveyTick(service, cp);
    UavDown uav;
    {
        std::lock_guard<std::mutex> lock(g_PendingMutex);
        if (g_UavDown[cp].due && GameClock::NowMs() - g_UavDown[cp].downMs >= kUavDownCallDelayMs)
        {
            uav = g_UavDown[cp];
            g_UavDown[cp] = UavDown{};
        }
    }
    if (uav.due && uav.cpController == service + kServiceToCpController)
        OrderUavCheck(service, cp, uav.spot);
    else if (uav.due)
        LogWarn("CpRadioVariants: a downed UAV of CP %u belongs to another CP controller, no CPR0510", cp);
    RadioOrder order;
    bool spottedReply = false;
    {
        std::lock_guard<std::mutex> lock(g_PendingMutex);
        order = g_RadioOrders[cp];
        spottedReply = g_SpottedReplyDue[cp];
        g_SpottedReplyDue[cp] = false;
    }
    if (order.queued)
        StartRadioOrder(service, cp, order);
    else if (order.soldier != kNoSoldier)
    {
        const std::uint8_t* soldiers = LiveSoldiers();
        const bool gone = !soldiers || OutOfAction(soldiers, order.soldier);
        if (order.done || (!order.onAir && (gone || now > order.untilMs)))
        {
            {
                std::lock_guard<std::mutex> lock(g_PendingMutex);
                if (g_RadioOrders[cp].soldier == order.soldier)
                    g_RadioOrders[cp] = RadioOrder{};
            }
            ResignRadioMan(service, cp, order.soldier);   // one call per order: the role would keep him calling it
        }
        else if (order.noPose && !order.onAir && now - order.postedMs > kRetryEveryMs
                 && !CpRadioResponse::HasMemberRadio(static_cast<std::uint16_t>(kSoldierType | order.soldier)))   // the CP's queue gave up on him
        {
            {
                std::lock_guard<std::mutex> lock(g_PendingMutex);
                if (g_RadioOrders[cp].soldier == order.soldier)
                    g_RadioOrders[cp].postedMs = now;
            }
            CpRadioResponse::MemberRadio call;
            call.label = order.label;
            CpRadioResponse::RequestMemberRadio(static_cast<std::uint16_t>(kSoldierType | order.soldier), call);
        }
    }
    if (spottedReply && FeatureSwitch::IsOn(FeatureSwitch::Feature::SoldierRadioAnswers))
    {
        std::uint32_t member = kNoMember;
        const std::uint32_t soldier = ChaffIsUp() ? kNoSoldier : RoleHolder(service, cp, LiveSoldiers(), kAnyRole, true, member);
        if (soldier != kNoSoldier)
            AnswerCpCall(service, cp, soldier, g_CameraReplyLabel, false);
    }
    ContactCall contactDone;
    {
        std::lock_guard<std::mutex> lock(g_PendingMutex);
        if (g_Contact[cp].done)
        {
            contactDone = g_Contact[cp];
            g_Contact[cp] = ContactCall{};   // his alert call is done: a waiting strike call may go
        }
    }
    bool answerHeldUp = false;
    {
        std::lock_guard<std::mutex> lock(g_PendingMutex);
        HeldUpReport& report = g_HeldUpReports[cp];
        if (report.replyDue)
        {
            report.replyDue = false;
            report.replyMs = GetTickCount64();
            answerHeldUp = true;
        }
    }
    if (answerHeldUp)
    {
        g_OrigRequestRadioByCp(service, cp, kCpOnAlertRadio, kNoMember, 0);
    }
    if (contactDone.soldier != kNoSoldier)
    {
        ResignRadioMan(service, cp, contactDone.soldier);
        const std::uint8_t* soldiers = LiveSoldiers();
        if (!contactDone.cpAnswered && soldiers && OutOfAction(soldiers, contactDone.soldier)
            && (InAlert(service, cp) || PlayerAlertPhase::Read() == PlayerAlertPhase::kAlert))
        {
            g_OrigRequestRadioByCp(service, cp, kAlertBroadcastRadio, kNoMember, 0);
        }
    }

    Pending act = Pending::None;
    std::uint32_t strikeCaller = kNoSoldier;
    bool strikeCallOut = false;
    {
        std::lock_guard<std::mutex> lock(g_PendingMutex);
        PendingMortar& pending = g_Pending[cp];
        if (pending.kind != Pending::None)
        {
            const bool alert = PlayerAlertPhase::Read() == PlayerAlertPhase::kAlert;
            if (!alert || (pending.kind != Pending::AwaitRequester && now > pending.untilMs))
            {
                pending = PendingMortar{};
                g_StrikeOpenedMs[cp] = 0;   // the strike call is given up: the operator stops waiting on it
            }
            else if (ChaffIsUp())
            {
                pending.untilMs = now + kRetryForMs;
                pending.triedMs = now;
                if (!pending.callOutSaid && pending.kind != Pending::CpOrder && g_Contact[cp].soldier == kNoSoldier)
                {
                    pending.callOutSaid = true;
                    strikeCallOut = true;
                }
            }
            else if (pending.kind == Pending::AwaitRequester)
            {
                if (now > pending.untilMs)
                {
                    act = Pending::AwaitRequester;
                    pending = PendingMortar{};
                }
            }
            else if (pending.kind == Pending::Decide || pending.kind == Pending::Resend)
            {
                if (g_Contact[cp].soldier == kNoSoldier)   // the alert call goes first
                {
                    act = pending.kind;
                    strikeCaller = pending.caller;
                    pending = PendingMortar{};
                }
            }
            else if (now - pending.triedMs >= kRetryEveryMs)
            {
                pending.triedMs = now;
                act = Pending::CpOrder;
            }
        }
    }
    if (strikeCallOut)
    {
        const std::uint32_t caller = StrikeCallerUnderChaff(service, cp);
        {
            std::lock_guard<std::mutex> lock(g_PendingMutex);
            PendingMortar& pending = g_Pending[cp];
            if (pending.kind != Pending::None && pending.callOutSaid && pending.caller == kNoSoldier)
                pending.caller = caller;
        }
        CallOutUnderChaff(caller);
    }

    GunshipCall gunship;
    bool callGunship = false;
    bool gunshipFallback = false;
    std::uint32_t gunshipCallOut = kNoSoldier;
    {
        std::lock_guard<std::mutex> lock(g_PendingMutex);
        GunshipCall& call = g_Gunship[cp];
        if (call.pendingMs && !call.called && !ChaffIsUp())
        {
            call.called = true;
            call.pendingMs = 0;
            gunship = call;
            callGunship = true;
        }
        else if (call.pendingMs && !call.called && !call.callOutSaid)
        {
            call.callOutSaid = true;
            gunshipCallOut = call.soldier;
        }
        else if (call.awaitMs && now > call.awaitMs)
        {
            call.awaitMs = 0;
            gunship = call;
            gunshipFallback = true;   // his radio call never went on air
        }
    }
    if (callGunship)
        CallGunship(service, cp, gunship.soldier, gunship.callOutSaid);
    else if (gunshipFallback)
        GunshipFallback(cp, gunship.soldier);
    else if (gunshipCallOut != kNoSoldier)
        CallOutUnderChaff(gunshipCallOut);

    ContactCall contact;
    {
        std::lock_guard<std::mutex> lock(g_PendingMutex);
        contact = g_Contact[cp];
    }
    if (contact.soldier != kNoSoldier && !contact.onAir)
    {
        const std::uint8_t* soldiers = LiveSoldiers();
        const bool canCall = soldiers && !OutOfAction(soldiers, contact.soldier);
        const bool neverAired = now > contact.untilMs
            && (!contact.sent || !CpRadioResponse::HasMemberRadio(static_cast<std::uint16_t>(kSoldierType | contact.soldier)));   // turned down by the CP, or its queue gave up
        if (!canCall || neverAired)
        {
            {
                std::lock_guard<std::mutex> lock(g_PendingMutex);
                g_Contact[cp] = ContactCall{};
            }
            ResignRadioMan(service, cp, contact.soldier);
            const bool alert = InAlert(service, cp) || PlayerAlertPhase::Read() == PlayerAlertPhase::kAlert;
            if (alert)
                g_OrigRequestRadioByCp(service, cp, kAlertBroadcastRadio, kNoMember, 0);
        }
    }

    if (act == Pending::Decide || act == Pending::Resend)
        DecideStrikeCall(service, cp, act == Pending::Resend, strikeCaller);
    else if (act == Pending::AwaitRequester)
    {
        OrderStrikeByCp(service, cp);
    }
    else if (act == Pending::CpOrder)
    {
        g_OrigRequestRadioByCp(service, cp, kMortarOrderRadio, kNoMember, g_LineHashes[kOrderLine]);
    }
}

void CpRadioVariants_OnSoldierDamages(std::uint32_t soldierIndex, const void* damages, std::uint32_t count)
{
    if (!damages || soldierIndex >= kSoldierCount || MissionCodeGuard::ShouldBypassHooks())
        return;
    const std::uint32_t heli = SupportHeliId();
    if (heli == 0xFFFFFFFF || !HitByHeli(damages, count, static_cast<std::uint16_t>(heli)))
        return;
    std::uint8_t* service = g_Service.load(std::memory_order_relaxed);
    std::uint32_t member = kNoMember;
    std::uint32_t cp = kNoCp;
    if (!FindMember(service, soldierIndex, member, cp) || !AirsCpCalls(service, cp))
        return;
    std::lock_guard<std::mutex> lock(g_PendingMutex);
    GunshipCall& call = g_Gunship[cp];
    if (call.called || call.pendingMs)
        return;
    call.soldier = soldierIndex;
    call.pendingMs = GetTickCount64();
}

void CpRadioVariants_OnCpChannelVoice(void* soundController, std::uint32_t owner, std::uint32_t voiceType, std::uint32_t label)
{
    const PlayingSequence playing = t_Sequence;
    if (playing.cp >= kCpCount)
        return;
    const std::uint64_t now = GetTickCount64();
    {
        std::lock_guard<std::mutex> lock(g_PendingMutex);
        g_Channels[playing.cp] = RadioChannel{ soundController, owner, playing.soldier, now };
    }
    if (playing.soldier >= kSoldierCount)
        return;
    for (const std::uint32_t cpVoice : kCpVoices)
    {
        if (cpVoice == voiceType)
        {
            g_CpTurnMs[playing.soldier].store(now, std::memory_order_relaxed);
            return;
        }
    }
    g_SpokeMs[playing.soldier].store(now, std::memory_order_relaxed);
    for (const std::uint32_t signOff : kSignOffLines)
    {
        if (signOff == label)
        {
            g_SignedOffMs[playing.soldier].store(now, std::memory_order_relaxed);
        }
    }
}

std::uint64_t CpRadioVariants_RadioHeardMs(std::uint32_t soldierIndex)
{
    if (soldierIndex >= kSoldierCount)
        return 0;
    std::uint64_t heard = g_SoldierRadioMs[soldierIndex].load(std::memory_order_relaxed);
    std::uint32_t cp = 0;
    std::int32_t phase = 0;
    if (!CpRadioResponse::CpPhaseOf(static_cast<std::uint16_t>(soldierIndex), cp, phase) || cp >= kCpCount)
        return heard;
    const std::uint64_t cpHeard = g_CpRadioMs[cp].load(std::memory_order_relaxed);
    if (cpHeard > heard)
        heard = cpHeard;

    RadioChannel channel;
    {
        std::lock_guard<std::mutex> lock(g_PendingMutex);
        channel = g_Channels[cp];
    }
    const std::uint64_t now = GetTickCount64();
    if (!channel.soundController || now - channel.startedMs > kChannelFreshMs
        || (channel.soldier != soldierIndex && channel.soldier != kNoSoldier))
        return heard;
    return ChannelPlaying(channel) ? now : heard;   // the line keeps playing after the game winds the call down
}

std::uint32_t CpRadioVariants_LabelForCall(std::uint32_t owner, std::uint8_t radioType, std::uint16_t member)
{
    if (radioType == kInvestigateReplyRadio && g_InvestigateReplyTake2 && !MissionCodeGuard::ShouldBypassHooks() && CoinFlip())
        return g_InvestigateReplyTake2;   // user: the unused take mixed with vanilla's cpr0341_01, always
    if (member != kNoMember && owner < kCpCount && !MissionCodeGuard::ShouldBypassHooks())
    {
        std::lock_guard<std::mutex> lock(g_PendingMutex);
        const RadioOrder& order = g_RadioOrders[owner];
        if (order.sent && order.type == radioType && (order.member == member || (kSoldierType | order.soldier) == member))
            return order.label;   // a typed call plays its type's own label otherwise
    }
    if (radioType == kInstallationRadio && member != kNoMember && owner < kCpCount && !MissionCodeGuard::ShouldBypassHooks())
    {
        std::lock_guard<std::mutex> lock(g_PendingMutex);
        EquipmentReport& report = g_EquipmentReports[owner];
        if (report.soldier != kNoSoldier && GetTickCount64() < report.untilMs
            && (report.member == member || (kSoldierType | report.soldier) == member))
        {
            const std::uint32_t label = report.label;
            report = EquipmentReport{};
            return label;
        }
    }
    if (radioType == kSearchCompleteRadio && member != kNoMember && owner < kCpCount && !MissionCodeGuard::ShouldBypassHooks())
    {
        std::lock_guard<std::mutex> lock(g_PendingMutex);
        const SurveyTeam& survey = g_Survey[owner];
        for (std::uint32_t i = 0; survey.stage == Survey::Out && i < survey.teamSize; ++i)
        {
            if (survey.team[i] == member)
            {
                LogDebug("SurveyProbe: cp %u: soldier %u radios CPR0147, the sweep report", owner, survey.teamSoldier[i]);
                return g_SweepDoneLabel;
            }
        }
    }
    if (radioType == kCpOnAlertRadio && g_StayOnAlertLabel && !MissionCodeGuard::ShouldBypassHooks())
    {
        std::lock_guard<std::mutex> lock(g_PendingMutex);
        const std::uint64_t now = GetTickCount64();
        if (owner < kCpCount)
        {
            const HeldUpReport& report = g_HeldUpReports[owner];
            if (report.replyMs && now - report.replyMs < kAnswerWindowMs)
            {
                return g_StayOnAlertLabel;
            }
        }
    }
    if (radioType != kAlertBroadcastRadio || member == kNoMember || MissionCodeGuard::ShouldBypassHooks())
        return 0;
    std::uint32_t cp = kNoCp;
    std::uint32_t soldier = kNoSoldier;
    {
        std::lock_guard<std::mutex> lock(g_PendingMutex);
        for (std::uint32_t i = 0; i < kCpCount && cp == kNoCp; ++i)
        {
            const ContactCall& alert = g_Contact[i];
            if (alert.sent && (alert.member == member || (kSoldierType | alert.soldier) == member))
            {
                cp = i;
                soldier = alert.soldier;
            }
        }
    }
    if (cp == kNoCp)
    {
        return 0;
    }
    return g_LineHashes[kContactLine];
}

std::uint32_t CpRadioVariants_SurveyLabel(std::uint32_t owner, std::uint8_t radioType, std::uint16_t member)
{
    if (member == kNoMember || owner >= kCpCount || MissionCodeGuard::ShouldBypassHooks())
        return 0;
    std::uint32_t finder = kNoSoldier;
    {
        std::lock_guard<std::mutex> lock(g_PendingMutex);
        const SurveyTeam& survey = g_Survey[owner];
        if ((survey.stage != Survey::Found && survey.stage != Survey::Told) || survey.radioType != radioType
            || (survey.member != member && (kSoldierType | survey.finder) != member))
            return 0;
        finder = survey.finder;
    }
    DecideSurvey(owner, finder, radioType);
    std::lock_guard<std::mutex> lock(g_PendingMutex);
    const SurveyTeam& survey = g_Survey[owner];
    if (survey.stage != Survey::Told || survey.finder != finder)
        return 0;
    if (survey.blast)
        return g_BlastLabel;   // his type stays 5/6: a quiet CP still goes to caution when it ends
    return survey.teamSize ? g_SendTeamLabel : g_StayOnGuardLabel;
}

void CpRadioVariants_ClearForNewMission()
{
    {
        std::lock_guard<std::mutex> lock(g_PendingMutex);
        g_Survey.fill(SurveyTeam{});
        g_SurveyAsked.fill(SurveyAsked{});
        g_SurveyedSpot.fill(SurveyedSpots{});
        g_EquipmentReports.fill(EquipmentReport{});
        g_UavDown.fill(UavDown{});
        g_RadioOrders.fill(RadioOrder{});
        g_SpottedReplyDue.fill(false);
        g_Pending.fill(PendingMortar{});
        g_Named.fill(NamedCall{});
        g_ReadyHolder.fill(0);
        g_StrikeOpenedMs.fill(0);
        g_Gunship.fill(GunshipCall{});
        g_Contact.fill(ContactCall{});
        g_HeldUpReports.fill(HeldUpReport{});
        g_RoleCalls.fill(RoleCall{});
        g_Ready.fill(ReadyHold{});
        g_FlareCalledMs.fill(0);
        g_Channels.fill(RadioChannel{});   // holds the old mission's sound controller
        g_Calls.fill(CallOnAir{});
    }
    g_RangeAttack.store(nullptr, std::memory_order_release);
    g_ChaffAtMs.store(0, std::memory_order_relaxed);
    for (auto& hold : g_FinderHoldUntilMs)
        hold.store(0, std::memory_order_relaxed);
    for (auto& watch : g_FinderWatchUntilMs)
        watch.store(0, std::memory_order_relaxed);
    for (auto& held : g_FinderHeld)
        held.store(false, std::memory_order_relaxed);
    for (auto& waker : g_Waker)
        waker.store(false, std::memory_order_relaxed);
    for (auto& reporter : g_BlastReporterUntilMs)
        reporter.store(0, std::memory_order_relaxed);
#ifdef _DEBUG
    for (auto& tagged : g_ProbeSurveyCp)
        tagged.store(0, std::memory_order_relaxed);
#endif
    const std::uint64_t now = GetTickCount64();
    for (std::size_t cp = 0; cp < kCpCount; ++cp)
    {
        g_CpPhaseSeen[cp].store(0, std::memory_order_relaxed);
        g_CpCalmedMs[cp].store(now, std::memory_order_relaxed);   // a new mission starts every base calm
    }
    g_Service.store(nullptr, std::memory_order_relaxed);
}

std::uint64_t CpRadioVariants_CpCalmedMs(std::uint32_t cp)
{
    return cp < kCpCount ? g_CpCalmedMs[cp].load(std::memory_order_relaxed) : 0;
}

bool CpRadioVariants_TakesVehicleBlast(std::uint32_t soldierIndex)
{
    return soldierIndex < kSoldierCount && GetTickCount64() < g_BlastReporterUntilMs[soldierIndex].load(std::memory_order_relaxed);
}

void CpRadioVariants_VehicleBlastReported(std::uint32_t soldierIndex)
{
    if (soldierIndex < kSoldierCount)
        g_BlastReporterUntilMs[soldierIndex].store(0, std::memory_order_relaxed);
}

bool CpRadioVariants_IsWatchingFinder(std::uint32_t soldierIndex)
{
    if (soldierIndex >= kSoldierCount)
        return false;
    const std::uint64_t now = GetTickCount64();
    return now < g_FinderWatchUntilMs[soldierIndex].load(std::memory_order_relaxed)
        || now < g_FinderHoldUntilMs[soldierIndex].load(std::memory_order_relaxed);
}

bool CpRadioVariants_HoldFinder(std::uint32_t soldierIndex)
{
    if (soldierIndex >= kSoldierCount)
        return false;
    const std::uint64_t until = g_FinderHoldUntilMs[soldierIndex].exchange(0, std::memory_order_relaxed);
    if (until == 0 || GetTickCount64() >= until)
        return false;
    g_FinderWatchUntilMs[soldierIndex].store(GetTickCount64() + kFinderHoldMostMs, std::memory_order_relaxed);   // the sweep starting ends it sooner
    g_FinderHeld[soldierIndex].store(true, std::memory_order_relaxed);
    return true;
}

FinderWait CpRadioVariants_FinderWait(std::uint32_t soldierIndex)
{
    if (soldierIndex >= kSoldierCount || !g_FinderHeld[soldierIndex].load(std::memory_order_relaxed))
        return FinderWait::Vanilla;
    if (GetTickCount64() < g_FinderWatchUntilMs[soldierIndex].load(std::memory_order_relaxed))
        return FinderWait::Hold;
    g_FinderHeld[soldierIndex].store(false, std::memory_order_relaxed);
    return FinderWait::Release;
}

void CpRadioVariants_OnUavDown(std::uint32_t cp, std::uint8_t* cpController, const float spot[3])
{
    if (cp >= kCpCount || MissionCodeGuard::ShouldBypassHooks() || !FeatureSwitch::IsOn(FeatureSwitch::Feature::UavDownChecks))
        return;
    std::lock_guard<std::mutex> lock(g_PendingMutex);
    UavDown& uav = g_UavDown[cp];
    uav.due = true;
    uav.cpController = cpController;
    uav.spot[0] = spot[0];
    uav.spot[1] = spot[1];
    uav.spot[2] = spot[2];
    uav.downMs = GameClock::NowMs();
}

bool CpRadioVariants_AssignRadioInCombat(std::uint32_t cp, std::uint32_t soldierIndex, std::uint32_t label, bool noPose)
{
    std::uint8_t* service = g_Service.load(std::memory_order_relaxed);
    if (!service || label == 0 || MissionCodeGuard::ShouldBypassHooks())
    {
        LogWarn("AssignRadioInCombat: %s, ignored", label == 0 ? "needs a label" : "no command post has used its radio yet");
        return false;
    }
    if (soldierIndex != kNoSoldier)
    {
        float at[3] = {};
        bool heldKnown = false;
        std::uint32_t member = kNoMember;
        if (!FindMember(service, soldierIndex, member, cp))
        {
            LogWarn("AssignRadioInCombat: soldier %u is on no command post, ignored", soldierIndex);
            return false;
        }
        if (!IsUp(LiveSoldiers(), soldierIndex, at) || CpRadioResponse::IsHeld(static_cast<std::uint16_t>(soldierIndex), heldKnown))
        {
            LogWarn("AssignRadioInCombat: soldier %u is down or held up, ignored", soldierIndex);
            return false;
        }
    }
    if (cp >= kCpCount)
        return false;
    std::lock_guard<std::mutex> lock(g_PendingMutex);
    RadioOrder& order = g_RadioOrders[cp];
    if ((order.queued || order.soldier != kNoSoldier) && GetTickCount64() < order.untilMs)
    {
        LogWarn("AssignRadioInCombat: CP %u already has a radio call of ours going, ignored", cp);
        return false;
    }
    order = RadioOrder{};
    order.queued = true;
    order.noPose = noPose;
    order.soldier = soldierIndex;
    order.label = label;
    order.untilMs = GetTickCount64() + kRoleCallMs;
    return true;
}

bool CpRadioVariants_IsWaker(std::uint32_t soldierIndex)
{
    return soldierIndex < kSoldierCount && g_Waker[soldierIndex].load(std::memory_order_relaxed);
}

void CpRadioVariants_OnDownComradeNode(std::uint32_t soldierIndex, bool awake)
{
    if (soldierIndex >= kSoldierCount)
        return;
    g_FinderHeld[soldierIndex].store(false, std::memory_order_relaxed);   // a new node, or his old one gone: Wait is vanilla's again
    if (!g_Waker[soldierIndex].load(std::memory_order_relaxed))
        return;
    std::lock_guard<std::mutex> lock(g_PendingMutex);
    for (std::uint32_t cp = 0; cp < kCpCount; ++cp)
    {
        SurveyTeam& survey = g_Survey[cp];
        if (survey.stage != Survey::Waking || survey.waker != soldierIndex)
            continue;
        if (awake && !survey.wakerUp)
        {
            survey.wakerUp = true;
            survey.untilMs = GetTickCount64() + kWakeMostMs;
        }
        else if (!awake && survey.wakerUp)
        {
            survey.stage = Survey::Dispatch;   // the next idle tick sends them, him included
            survey.wakeDone = true;
            g_Waker[soldierIndex].store(false, std::memory_order_relaxed);
        }
        return;
    }
}

bool CpRadioVariants_SurveyMemberOf(std::uint16_t soldierIndex, std::uint32_t& cp, std::uint16_t& member)
{
    std::lock_guard<std::mutex> lock(g_PendingMutex);
    for (std::uint32_t c = 0; c < kCpCount; ++c)
    {
        const SurveyTeam& survey = g_Survey[c];
        if (survey.stage != Survey::Out || survey.calledOff)
            continue;
        for (std::uint32_t i = 0; i < survey.teamSize; ++i)
        {
            if (survey.teamSoldier[i] == soldierIndex)
            {
                cp = c;
                member = static_cast<std::uint16_t>(survey.team[i]);
                return true;
            }
        }
    }
    return false;
}

void CpRadioVariants_CallOffSurvey(std::uint32_t cp)
{
    if (cp >= kCpCount)
        return;
    std::lock_guard<std::mutex> lock(g_PendingMutex);
    if (g_Survey[cp].stage == Survey::Out)
        g_Survey[cp].calledOff = true;   // the next idle tick sends them all back
}

void CpRadioVariants_OnSitrepUnanswered(std::uint32_t cp, const std::uint32_t* teams, std::uint32_t count)
{
    if (cp >= kCpCount || !teams || MissionCodeGuard::ShouldBypassHooks())
        return;
    std::lock_guard<std::mutex> lock(g_PendingMutex);
    SurveyAsked& asked = g_SurveyAsked[cp];
    for (std::uint32_t i = 0; i < count; ++i)
    {
        if (asked.team == 0 || teams[i] != asked.team)
            continue;
        LogDebug("SurveyProbe: cp %u: nobody answered the SITREP, %s", cp, asked.second ? "it was the second team, no third" : "a second team goes to the spot");
        if (asked.second)
            asked = SurveyAsked{};   // user: no third team
        else
            asked.sendTeam = true;
        return;
    }
}

#ifdef _DEBUG
bool CpRadioVariants_ProbeSurveyCp(std::uint32_t soldierIndex, std::uint32_t& cp)
{
    if (soldierIndex >= kSoldierCount)
        return false;
    const std::uint16_t tagged = g_ProbeSurveyCp[soldierIndex].load(std::memory_order_relaxed);
    if (tagged == 0)
        return false;
    cp = tagged - 1u;
    return true;
}
#endif

bool CpRadioVariants_SpeakSoldierHalf(std::uint32_t priority, std::uint32_t event, std::uint32_t voiceType, std::uint32_t label)
{
    const PartSpeaker speaker = t_PartSpeaker;
    if (speaker.soldier == kNoSoldier)
        return false;
    for (const std::uint32_t cpVoice : kCpVoices)
    {
        if (cpVoice == voiceType)
            return false;
    }
    bool spoke = false;
    t_PartSpeaker = PartSpeaker{};   // his own voice call comes back through CallVoice
    __try
    {
        const auto speak = (*reinterpret_cast<SpeakAtIndex_t* const*>(speaker.soundController))[kSpeakAtIndexSlot];
        spoke = speak(speaker.soundController, speaker.soldier, event, priority, label, false, 0.0f);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        spoke = false;
    }
    t_PartSpeaker = speaker;
    return spoke;
}

bool Install_CpRadioVariants_Hooks()
{
    for (std::uint32_t i = 0; i < kLineCount; ++i)
        g_LineHashes[i] = FoxHashes::StrCode32(kLineLabels[i]);
    g_StayOnAlertLabel = FoxHashes::StrCode32("CPR0049");
    g_SendTeamLabel = FoxHashes::StrCode32("CPR0148");
    g_StayOnGuardLabel = FoxHashes::StrCode32("CPR0142");
    g_SweepDoneLabel = FoxHashes::StrCode32("CPR0147");
    g_BlastLabel = FoxHashes::StrCode32("CPR0143");
    g_CommsLabel = FoxHashes::StrCode32("CPR0346");
    g_PowerLabel = FoxHashes::StrCode32("CPR0345");
    g_CameraReplyLabel = FoxHashes::StrCode32("V_CPR0022ENE");
    g_UavReplyLabel = FoxHashes::StrCode32("CPR0341ENE");
    g_InvestigateReplyTake2 = FoxHashes::StrCode32("V_CPR0340ENE");
    g_IsPlayingVoice = reinterpret_cast<IsPlayingVoice_t>(ResolveGameAddress(gAddr.SoundControllerImpl_IsPlayingVoice));
    if (!Hook(gAddr.CpRadioService_ConvertSpeechLabelToRadioType, reinterpret_cast<void*>(&hk_ConvertSpeechLabelToRadioType),
              reinterpret_cast<void**>(&g_OrigConvertSpeechLabel), "ConvertSpeechLabelToRadioType"))
        return false;
    if (!Hook(gAddr.CommandPost2Impl_RequestRadioByCp, reinterpret_cast<void*>(&hk_RequestRadioByCp),
              reinterpret_cast<void**>(&g_OrigRequestRadioByCp), "RequestRadioByCp"))
    {
        Unhook(gAddr.CpRadioService_ConvertSpeechLabelToRadioType, reinterpret_cast<void*>(g_OrigConvertSpeechLabel));
        g_OrigConvertSpeechLabel = nullptr;
        return false;
    }
    if (!Hook(gAddr.RadioSpeechHandlerImpl_CallPart, reinterpret_cast<void*>(&hk_CallPart),
              reinterpret_cast<void**>(&g_OrigCallPart), "CallPart"))
        LogError("CpRadioVariants: CP calls can't name a soldier, his half stays on the generic voice");
    if (!Hook(gAddr.RangeAttackSystemImpl_ActivateChaff, reinterpret_cast<void*>(&hk_ActivateChaff),
              reinterpret_cast<void**>(&g_OrigActivateChaff), "ActivateChaff"))
        LogError("CpRadioVariants: mortar calls are not held under chaff");
    if (!Hook(gAddr.MortarActionImpl_State_WaitAction, reinterpret_cast<void*>(&hk_State_WaitAction),
              reinterpret_cast<void**>(&g_OrigWaitAction), "MortarActionImpl::State_WaitAction"))
        LogError("CpRadioVariants: no CPR0190 / CPR0200 before the mortar fires");
    if (!Hook(gAddr.UnrealMortarActionImpl_State_WaitAction, reinterpret_cast<void*>(&hk_Unreal_State_WaitAction),
              reinterpret_cast<void**>(&g_OrigUnrealWaitAction), "UnrealMortarActionImpl::State_WaitAction"))
        LogError("CpRadioVariants: a mortar man far from Snake fires without waiting for the radio");
    if (!Hook(gAddr.MortarActionImpl_State_MortarAction, reinterpret_cast<void*>(&hk_State_MortarAction),
              reinterpret_cast<void**>(&g_OrigMortarAction), "MortarActionImpl::State_MortarAction"))
        LogError("CpRadioVariants: a mortar man already on the mortar fires a new strike without waiting");
    if (!Hook(gAddr.UnrealMortarActionImpl_State_MortarAction, reinterpret_cast<void*>(&hk_Unreal_State_MortarAction),
              reinterpret_cast<void**>(&g_OrigUnrealMortarAction), "UnrealMortarActionImpl::State_MortarAction"))
        LogError("CpRadioVariants: a mortar man far from Snake fires a new strike without waiting");
    if (!Hook(gAddr.CommandPost2Impl_Controller_RequestRadioByMember, reinterpret_cast<void*>(&hk_RequestRadioByMember),
              reinterpret_cast<void**>(&g_OrigRequestRadioByMember), "CommandPost2Impl::Controller_RequestRadioByMember_"))
        LogError("CpRadioVariants: the radio man's own call can't carry CPR0182 / CPR0210");
    if (!Hook(gAddr.Heli2Impl_Unrealize, reinterpret_cast<void*>(&hk_Heli2Unrealize),
              reinterpret_cast<void**>(&g_OrigHeliUnrealize), "Heli2Impl::Unrealize"))
        LogError("CpRadioVariants: the gunship call stays once per CP for the whole mission");
    if (!Hook(gAddr.CpRadioService_CheckRadioTypeBgmPhaseMismatch, reinterpret_cast<void*>(&hk_CheckRadioTypeBgmPhaseMismatch),
              reinterpret_cast<void**>(&g_OrigBgmPhaseMismatch), "CpRadioService::CheckRadioTypeBgmPhaseMismatch"))
        LogError("CpRadioVariants: AssignRadioInCombat calls of most labels stay silent while the combat music plays");
    Install_TppPermanentGimmickUnit_CallNoise_Hook();
    Install_UavLifeControllerImpl_SendStatusMessage_Hook();
#ifdef _DEBUG
    Install_InvestigateAiImpl_PreUpdate_Hook();   // TEMP probe: which investigation steps a survey team reaches
#endif
    return true;
}

bool Uninstall_CpRadioVariants_Hooks()
{
    Uninstall_UavLifeControllerImpl_SendStatusMessage_Hook();
    Uninstall_TppPermanentGimmickUnit_CallNoise_Hook();
    Unhook(gAddr.CpRadioService_CheckRadioTypeBgmPhaseMismatch, reinterpret_cast<void*>(g_OrigBgmPhaseMismatch));
    g_OrigBgmPhaseMismatch = nullptr;
    Unhook(gAddr.Heli2Impl_Unrealize, reinterpret_cast<void*>(g_OrigHeliUnrealize));
    Unhook(gAddr.UnrealMortarActionImpl_State_MortarAction, reinterpret_cast<void*>(g_OrigUnrealMortarAction));
    Unhook(gAddr.MortarActionImpl_State_MortarAction, reinterpret_cast<void*>(g_OrigMortarAction));
    Unhook(gAddr.CommandPost2Impl_Controller_RequestRadioByMember, reinterpret_cast<void*>(g_OrigRequestRadioByMember));
    Unhook(gAddr.UnrealMortarActionImpl_State_WaitAction, reinterpret_cast<void*>(g_OrigUnrealWaitAction));
    Unhook(gAddr.MortarActionImpl_State_WaitAction, reinterpret_cast<void*>(g_OrigWaitAction));
    Unhook(gAddr.RangeAttackSystemImpl_ActivateChaff, reinterpret_cast<void*>(g_OrigActivateChaff));
    Unhook(gAddr.RadioSpeechHandlerImpl_CallPart, reinterpret_cast<void*>(g_OrigCallPart));
    Unhook(gAddr.CommandPost2Impl_RequestRadioByCp, reinterpret_cast<void*>(g_OrigRequestRadioByCp));
    Unhook(gAddr.CpRadioService_ConvertSpeechLabelToRadioType, reinterpret_cast<void*>(g_OrigConvertSpeechLabel));
    g_OrigWaitAction = nullptr;
    g_OrigUnrealWaitAction = nullptr;
    g_OrigHeliUnrealize = nullptr;
    g_OrigMortarAction = nullptr;
    g_OrigUnrealMortarAction = nullptr;
    g_OrigRequestRadioByMember = nullptr;
    g_OrigActivateChaff = nullptr;
    g_OrigCallPart = nullptr;
    g_OrigRequestRadioByCp = nullptr;
    g_OrigConvertSpeechLabel = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_PendingMutex);
        g_Pending.fill(PendingMortar{});
        g_Named.fill(NamedCall{});
        g_ReadyHolder.fill(0);
        g_StrikeOpenedMs.fill(0);
        g_StrikeSerial.fill(0);
        g_ReadyDoneSerial.fill(0);
        g_SoldierCp.fill(0);
        g_SoldierCpMs.fill(0);
        g_Gunship.fill(GunshipCall{});
        g_Contact.fill(ContactCall{});
        g_HeldUpReports.fill(HeldUpReport{});
        g_RoleCalls.fill(RoleCall{});
        g_Ready.fill(ReadyHold{});
        g_FlareCalledMs.fill(0);
        g_Channels.fill(RadioChannel{});
        g_Calls.fill(CallOnAir{});
        g_Survey.fill(SurveyTeam{});
        g_SurveyAsked.fill(SurveyAsked{});
        g_SurveyedSpot.fill(SurveyedSpots{});
        g_EquipmentReports.fill(EquipmentReport{});
        g_UavDown.fill(UavDown{});
        g_RadioOrders.fill(RadioOrder{});
        g_SpottedReplyDue.fill(false);
    }
    Uninstall_NoticeDownComradeAiImpl_StepPreRadio();
    Uninstall_NoticeAiImpl_StepRadio();
#ifdef _DEBUG
    Uninstall_InvestigateAiImpl_PreUpdate_Hook();
#endif
    for (std::size_t i = 0; i < kSoldierCount; ++i)
    {
        g_FinderHoldUntilMs[i].store(0, std::memory_order_relaxed);
        g_FinderWatchUntilMs[i].store(0, std::memory_order_relaxed);
        g_FinderHeld[i].store(false, std::memory_order_relaxed);
        g_Waker[i].store(false, std::memory_order_relaxed);
        g_BlastReporterUntilMs[i].store(0, std::memory_order_relaxed);
        g_SpokeMs[i].store(0, std::memory_order_relaxed);
        g_SignedOffMs[i].store(0, std::memory_order_relaxed);
        g_CpTurnMs[i].store(0, std::memory_order_relaxed);
        g_SoldierRadioMs[i].store(0, std::memory_order_relaxed);
    }
    for (auto& heard : g_CpRadioMs)
        heard.store(0, std::memory_order_relaxed);
    g_IsPlayingVoice = nullptr;
    g_Service.store(nullptr, std::memory_order_relaxed);
    g_RangeAttack.store(nullptr, std::memory_order_release);
    g_SupportHeliCheckedMs.store(0, std::memory_order_relaxed);
    return true;
}
