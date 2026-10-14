#include "pch.h"

#include <Windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

#include "AddressSet.h"
#include "CpRadioVariants.h"
#include "FoxHashes.h"
#include "GetGameObjectIdWithIndex.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "NoticeControllerImpl_CheckSightNoticePlayer.h"
#include "PlayerAlertPhase.h"
#include "RadioControllerImpl_StepRadioWaitResponse.h"
#include "Soldier2SoundControllerImpl_CallVoiceWithoutSync.h"
#include "SoldierRealizedSlot.h"
#include "SoldierVoiceType.h"
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
    using AssignRadioInCombat_t = bool(__fastcall*)(void* roleController, std::uint32_t cp, std::uint32_t radioType);
    using SequenceLabel_t       = std::uint32_t(__fastcall*)(void* sequencer, std::uint32_t sequence);
    using SpeakAtIndex_t        = bool(__fastcall*)(void* soundController, std::uint32_t soldierIndex, std::uint32_t event,
                                                    std::uint32_t priority, std::uint32_t label, bool understood, float delay);
    using GetPlayerPosition_t   = void(__fastcall*)(void* playerInfo, float* outPosition);
    using IsPlayingVoice_t      = bool(__fastcall*)(void* soundController, std::uint32_t owner);

    constexpr std::uint32_t  kMortarOrderRadio        = 0x56;     // CPR0180, AlertAiImpl::UpdateCombatMortar asks it of nobody
    constexpr std::uint32_t  kAnyLabelRadio           = 0x5E;     // a member call whose label has no type of its own
    constexpr std::uint32_t  kUnknownLabelRadio       = 0x5F;
    constexpr std::uint32_t  kGunshipRadio            = 0x47;     // CPR0220, the CP's own chopper order; carries CPR0210 for the radio man
    constexpr std::size_t    kDamageStride            = 0x30;
    constexpr std::size_t    kDamageAttacker          = 0x22;
    enum Line : std::uint32_t { kOrderLine, kRequestLine, kReadyLine, kFlareLine, kGunshipLine, kLineCount };
    constexpr const char*    kLineLabels[kLineCount]  = {
        "CPR0180",   // CP "Lay down suppressing mortar fire on the target!" > the operator "Preparing for mortar strike!"
        "CPR0182",   // he "Requesting mortar support!" > CP "Mortar team: Provide fire support!" (CPR0181's line)
        "CPR0190",   // the operator "Mortar ready to fire!" > CP "Commence strike."
        "CPR0200",   // CP "Fire a flare to illuminate the area." > the flare man "Preparing to fire flare."
        "CPR0210" }; // he "Taking fire from enemy gunship!" > CP "Enemy chopper is priority one!"
    constexpr std::uint32_t  kNoMember                = 0xFFFF;
    constexpr std::uint16_t  kNoHandle                = 0xFFFF;   // RequestRadioByCp's answer when no call went out
    constexpr std::uint32_t  kNoSoldier               = 0xFFFFFFFF;
    constexpr std::ptrdiff_t kServiceToCpController   = -0x18;
    constexpr std::size_t    kServiceRoleController   = 0x18;     // MemberRoleControllerImpl, as AlertAiImpl reaches it
    constexpr std::size_t    kAssignRadioInCombatSlot = 0x60 / sizeof(void*);   // AlertAiImpl::UpdateReinforceRadio's backup request
    constexpr std::size_t    kServiceMemberRadioFlags = 0x58;     // 0x14 per member, bit 1: CallPart speaks him through his own voice
    constexpr std::size_t    kMemberRadioFlagsSize    = 0x14;
    constexpr std::uint32_t  kOwnVoiceFlag            = 0x2;
    constexpr std::size_t    kServiceCpStates         = 0x30;     // 0x60 per CP
    constexpr std::size_t    kCpStateSize             = 0x60;
    constexpr std::size_t    kCpStatePhase            = 0x48;
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
    constexpr std::uint64_t  kChaffWatchMs            = 120000;   // past this the cloud is long gone, the pointer is not read

    ConvertSpeechLabel_t g_OrigConvertSpeechLabel = nullptr;
    RequestRadioByCp_t   g_OrigRequestRadioByCp   = nullptr;
    CallPart_t           g_OrigCallPart           = nullptr;
    ActivateChaff_t      g_OrigActivateChaff      = nullptr;
    MortarState_t        g_OrigWaitAction         = nullptr;
    UnrealMortarState_t  g_OrigUnrealWaitAction   = nullptr;
    MortarState_t        g_OrigMortarAction       = nullptr;
    UnrealMortarState_t  g_OrigUnrealMortarAction = nullptr;
    HeliUnrealize_t      g_OrigHeliUnrealize      = nullptr;
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
        Pending       kind    = Pending::None;
        std::uint64_t untilMs = 0;
        std::uint64_t triedMs = 0;
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
        std::uint64_t capFromMs  = 0;       // his wait limit runs from here: his arrival, or the chaff clearing
        std::uint64_t askedMs    = 0;
        std::uint64_t lastAskMs  = 0;
        std::uint64_t releasedMs = 0;
        std::uint64_t onAirMs    = 0;
        bool          jammed     = false;   // chaff ate his call: he waits until it clears, then asks again
        bool          done       = false;
        bool          refused    = false;   // on and off air without his voice: the CP turned it down
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
    using RequestRadioByMember_t = std::uint16_t(__fastcall*)(std::uint8_t* cpController, std::uint32_t client, std::uint32_t local,
                                                              std::uint32_t radioType, std::uint32_t label);
    RequestRadioByMember_t          g_OrigRequestRadioByMember = nullptr;
    constexpr std::size_t           kCpControllerClients = 0x1F0;   // soldiers, helis... each with its own run of members
    constexpr std::size_t           kClientSize          = 0x48;
    constexpr std::size_t           kClientFirstMember   = 0x40;
    constexpr std::uint16_t         kSoldierObjectType   = 2;       // gameObjectId >> 9

    struct GunshipCall
    {
        std::uint32_t soldier   = kNoSoldier;   // the man the heli hit, the fallback speaker
        std::uint64_t pendingMs = 0;
        std::uint64_t awaitMs   = 0;            // the radio man has until then to put it on air
        bool          called    = false;        // once per CP until the heli unrealizes (user)
    };
    std::array<GunshipCall, kCpCount> g_Gunship{};
    std::atomic<std::uint32_t>      g_SupportHeliId{ 0xFFFFFFFF };
    std::atomic<std::uint64_t>      g_SupportHeliCheckedMs{ 0 };
    constexpr std::uint64_t         kSupportHeliRecheckMs = 5000;
    constexpr std::uint64_t         kGunshipAwaitMs       = 12000;

    std::array<std::atomic<std::uint64_t>, kCpCount>      g_CpRadioMs{};        // a call naming nobody last played on this CP
    std::array<std::atomic<std::uint64_t>, kSoldierCount> g_SoldierRadioMs{};   // a call naming him last played

    const char* LineName(std::uint32_t label)
    {
        for (std::uint32_t i = 0; i < kLineCount; ++i)
        {
            if (g_LineHashes[i] == label)
                return kLineLabels[i];
        }
        return "?";
    }

    std::uint8_t __fastcall hk_ConvertSpeechLabelToRadioType(std::uint32_t label)
    {
        if (label != 0 && !MissionCodeGuard::ShouldBypassHooks())
        {
            if (label == g_LineHashes[kOrderLine] || label == g_LineHashes[kRequestLine])
                return static_cast<std::uint8_t>(kMortarOrderRadio);   // CPR0182 is still the vanilla mortar call
            if (label == g_LineHashes[kReadyLine] || label == g_LineHashes[kFlareLine] || label == g_LineHashes[kGunshipLine])
                return static_cast<std::uint8_t>(kUnknownLabelRadio);   // his own call then goes out as 0x5E, past the CP's refusals
        }
        return g_OrigConvertSpeechLabel(label);
    }

    bool InAlert(std::uint8_t* service, std::uint32_t cp)
    {
        __try
        {
            const std::uint8_t* states = *reinterpret_cast<std::uint8_t* const*>(service + kServiceCpStates);
            return *reinterpret_cast<const std::int32_t*>(states + cp * kCpStateSize + kCpStatePhase) == kPhaseAlert;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
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

    bool IsUp(const std::uint8_t* soldiers, std::uint32_t soldierIndex, float* position)   // active and awake, where he stands
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

    bool CanTalk(const std::uint8_t* soldiers, std::uint32_t soldierIndex, float* position)
    {
        bool heldKnown = false;
        return IsUp(soldiers, soldierIndex, position)
            && !CpRadioResponse::IsHeld(static_cast<std::uint16_t>(soldierIndex), heldKnown)
            && SoldierRealizedSlot::Of(soldiers, soldierIndex) != SoldierRealizedSlot::kNotRealized
            && SoldierVoiceType::IsAdult(SoldierVoiceType::Of(soldiers, soldierIndex));
    }

    std::uint32_t RoleHolder(std::uint8_t* service, std::uint32_t cp, const std::uint8_t* soldiers, std::uint8_t role, bool talking,
                             std::uint32_t& memberOut)   // that CP's member in the role, nearest Snake; talking: one who can say his line
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

    std::uint32_t RadioRoleHolder(std::uint8_t* service, std::uint32_t cp, const std::uint8_t* soldiers)   // far from Snake he has no position: the role alone counts
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

    bool MemberOfClient(std::uint8_t* cpController, std::uint32_t client, std::uint32_t local, std::uint32_t& soldier, std::uint32_t& cp)
    {
        __try
        {
            const std::uint8_t* clients = *reinterpret_cast<std::uint8_t* const*>(cpController + kCpControllerClients);
            const std::uint32_t member = *reinterpret_cast<const std::uint16_t*>(clients + client * kClientSize + kClientFirstMember) + local;
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

    std::uint16_t __fastcall hk_RequestRadioByMember(std::uint8_t* cpController, std::uint32_t client, std::uint32_t local,
                                                     std::uint32_t radioType, std::uint32_t label)
    {
        std::uint32_t soldier = kNoSoldier;
        std::uint32_t cp = kNoCp;
        if (label == 0 && !MissionCodeGuard::ShouldBypassHooks() && MemberOfClient(cpController, client, local, soldier, cp))
        {
            Line line = kLineCount;
            {
                std::lock_guard<std::mutex> lock(g_PendingMutex);
                RoleCall& call = g_RoleCalls[cp];
                if (call.soldier == soldier && !call.sent && GetTickCount64() < call.untilMs)
                {
                    call.sent = true;
                    line = call.line;
                }
            }
            if (line != kLineCount)
            {
                LogInfo("VoiceDebug: soldier %u radios %s (his radio-role call 0x%02X, as a plain label)", soldier, kLineLabels[line], radioType);
                return g_OrigRequestRadioByMember(cpController, client, local, kAnyLabelRadio, g_LineHashes[line]);   // past the CP's refusals, his pose kept
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

    void OrderStrikeByCp(std::uint8_t* service, std::uint32_t cp)   // the fallback: the CP orders it, naming the man on the mortar
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
        g_OrigRequestRadioByCp(service, cp, kMortarOrderRadio, kNoMember, g_LineHashes[kOrderLine]);   // unaddressed: naming the busy operator gets it dropped
        LogInfo("VoiceDebug: CP %u orders the strike (CPR0180), names %s %d", cp,
            operatorSoldier != kNoSoldier ? "the mortar operator, soldier" : "nobody (generic voice)",
            operatorSoldier != kNoSoldier ? static_cast<int>(operatorSoldier) : -1);
    }

    bool AssignRequester(std::uint8_t* service, std::uint32_t cp, std::uint32_t radioType = kMortarOrderRadio)   // he takes the radio role and puts the call on air himself, radio pose and all
    {
        __try
        {
            void* roles = *reinterpret_cast<void* const*>(service + kServiceRoleController);
            const auto assign = (*reinterpret_cast<AssignRadioInCombat_t* const*>(roles))[kAssignRadioInCombatSlot];
            return assign(roles, cp, radioType);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void DecideStrikeCall(std::uint8_t* service, std::uint32_t cp, bool again = false)   // the requester as vanilla picks who radios for backup, else the CP
    {
        {
            std::lock_guard<std::mutex> lock(g_PendingMutex);
            g_StrikeOpenedMs[cp] = GetTickCount64();   // the operator's ready call waits for it
            if (!again)
                ++g_StrikeSerial[cp];   // chaff lost the last request: the same strike, the same ready call
        }
        if (!AssignRequester(service, cp))
        {
            OrderStrikeByCp(service, cp);
            return;
        }
        {
            std::lock_guard<std::mutex> lock(g_PendingMutex);
            const std::uint64_t now = GetTickCount64();
            g_Pending[cp] = PendingMortar{ Pending::AwaitRequester, now + kRequesterWaitMs, now };
        }
        const std::uint32_t requester = GiveRoleCall(service, cp, kRequestLine);
        LogInfo("VoiceDebug: CP %u strike: soldier %d takes the radio to request it%s (CPR0182)", cp,
            requester != kNoSoldier ? static_cast<int>(requester) : -1, again ? " again" : "");
    }

    bool ReadSequence(std::uint8_t* speechHandler, std::uint8_t sequence, std::uint16_t& cp, std::uint32_t& label, std::uint8_t*& service)
    {
        __try
        {
            const std::uint8_t* record = *reinterpret_cast<std::uint8_t* const*>(speechHandler + kHandlerSequences) + sequence * kSequenceSize;
            if (*reinterpret_cast<const std::uint16_t*>(record + kSequenceMember) != kNoMember)
                return false;   // a named member already answers in his own voice
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

    bool SpeaksOwnVoice(std::uint8_t* service, std::uint32_t member)   // CallPart then plays his half through him, no redirect needed
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
            named = NamedCall{ label, kNoSoldier, kNoMember, now + kNamedCallMs, false };   // a vanilla CPR0180 still names the operator
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
            return kNoSoldier;   // last resort: the vanilla generic voice
        if (!named.logged)
        {
            LogInfo("VoiceDebug: CP %u %s names soldier %u (member %u)", cp, LineName(label), named.soldier, named.member);
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
            t_PartSpeaker = PartSpeaker{ soundController, speaker };   // his half comes out of the CP's channel: sent to him instead
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
        LogInfo("VoiceDebug: chaff up, mortar calls held until it clears");
    }

    std::uint16_t __fastcall hk_RequestRadioByCp(std::uint8_t* service, std::uint32_t cp, std::uint32_t radioType,
                                                 std::uint32_t member, std::uint32_t label)
    {
        g_Service.store(service, std::memory_order_relaxed);
        if (radioType != kMortarOrderRadio || member != kNoMember || label != 0 || cp >= kCpCount || MissionCodeGuard::ShouldBypassHooks()
            || !(InAlert(service, cp) || PlayerAlertPhase::Read() == PlayerAlertPhase::kAlert))   // the CP's own phase reads 0 now and then in alert
            return g_OrigRequestRadioByCp(service, cp, radioType, member, label);
        if (ChaffIsUp())
        {
            std::lock_guard<std::mutex> lock(g_PendingMutex);
            const std::uint64_t now = GetTickCount64();
            g_Pending[cp] = PendingMortar{ Pending::Decide, now + kRetryForMs, now };
            g_StrikeOpenedMs[cp] = now;
            LogInfo("VoiceDebug: CP %u strike call held, chaff is up", cp);
            return kNoHandle;
        }
        DecideStrikeCall(service, cp);
        return kNoHandle;   // the requester's own call carries it; UpdateCombatMortar only stores the handle
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

    bool StrikeCallOpen(std::uint32_t cp, std::uint64_t now)   // the CPR0182 / CPR0180 exchange hasn't finished, or still has to go out
    {
        return (g_StrikeOpenedMs[cp] && now - g_StrikeOpenedMs[cp] < kStrikeCallMostMs) || g_Pending[cp].kind != Pending::None;
    }

    void ReleaseRound(ReadyHold& hold, std::uint64_t now, const char* why)
    {
        LogInfo("VoiceDebug: %s %u fires (%s, waited %llu ms)", hold.line == kFlareLine ? "flare man" : "operator", hold.soldier, why,
            now - hold.startedMs);
        hold.releasedMs = now;
        hold.startedMs = 0;
        if (hold.line != kFlareLine)
            g_ReadyDoneSerial[hold.soldier] = hold.serial;   // the rest of this strike fires freely
        if (hold.cp < kCpCount && g_ReadyHolder[hold.cp] == hold.soldier + 1)
            g_ReadyHolder[hold.cp] = 0;
    }

    bool PhaseWants(Line line)   // lethal rounds in alert, flares in caution and evasion only (user)
    {
        const int phase = PlayerAlertPhase::Read();
        return line == kFlareLine ? phase == PlayerAlertPhase::kCaution || phase == PlayerAlertPhase::kEvasion
                                  : phase == PlayerAlertPhase::kAlert;
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

    bool HoldRound(std::uint32_t soldier, Line line)   // true while he waits at the mortar for his radio exchange to finish
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
        if (cp == kNoCp)
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
                ReleaseRound(hold, now, hold.line == kFlareLine ? "after his reply" : "after Commence strike");
            else if (chaffUp)
            {
                hold.capFromMs = now;   // no call gets through: his wait starts over once the chaff clears
                if (!hold.onAirMs && !hold.jammed && (hold.askedMs || hold.line == kFlareLine || !StrikeCallOpen(hold.cp, now)))
                {
                    hold.jammed = true;
                    if (!hold.askedMs)
                    {
                        hold.askedMs = now;   // he tries the radio and gets nothing back
                        act = Act::Jam;
                    }
                }
            }
            else if (hold.jammed)
            {
                hold.jammed = false;   // the chaff cleared: he asks again, after the strike call if that still has to go out
                hold.refused = false;
                hold.onAirMs = 0;
                hold.capFromMs = now;
                hold.askedMs = inFlight ? now : 0;
                hold.lastAskMs = now;
                LogInfo("VoiceDebug: chaff cleared, %s %u radios %s again", hold.line == kFlareLine ? "flare man" : "operator", soldier,
                    kLineLabels[hold.line]);
            }
            else if (!hold.askedMs && hold.line != kFlareLine && StrikeCallOpen(hold.cp, now))
                hold.capFromMs = now;   // the strike request is still going out: his own wait starts after it
            else if (hold.refused)
                ReleaseRound(hold, now, "the call was turned down");
            else if (now - hold.capFromMs > kReadyHoldMostMs)
                ReleaseRound(hold, now, "waited too long");
            else if (hold.askedMs && !hold.onAirMs && now - hold.askedMs > kReadyAirMostMs)
                ReleaseRound(hold, now, "the radio stayed busy");
            else if (hold.onAirMs && now - hold.onAirMs > kReadyAnswerMostMs)
                ReleaseRound(hold, now, "no answer in time");
            else if (hold.askedMs && !hold.onAirMs && !inFlight && now - hold.lastAskMs >= kReadyRepostMs)
            {
                hold.lastAskMs = now;   // dropped while the CP was busy: asked again
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
            LogInfo(line == kFlareLine ? "VoiceDebug: flare man %u on the mortar, waits for the flare order (CPR0200)"
                                       : "VoiceDebug: operator %u on the mortar, waits for the strike call before his ready call", soldier);
            return true;
        }
        if (act == Act::Jam)
        {
            const std::uint8_t* soldiers = LiveSoldiers();   // he tries the radio, nothing comes back, he stays on the mortar
            const std::uint32_t slot = SoldierRealizedSlot::Of(soldiers, soldier);
            if (slot != SoldierRealizedSlot::kNotRealized)
                SoldierVoiceSwap_SayJammedTake(soldiers, slot);
            LogInfo("VoiceDebug: soldier %u's %s call jammed, he holds fire until the chaff clears", soldier, kLineLabels[line]);
        }
        else if (act == Act::Post)
        {
            CpRadioResponse::MemberRadio call;
            call.label = g_LineHashes[line];   // his own call: his call sign, his voice, the CP's part in the same call
            const bool posted = CpRadioResponse::RequestMemberRadio(static_cast<std::uint16_t>(kSoldierType | soldier), call);
            LogInfo("VoiceDebug: soldier %u radios %s on CP %u%s", soldier, kLineLabels[line], cp, posted ? "" : ", radio busy, fires");
            if (!posted)
            {
                std::lock_guard<std::mutex> lock(g_PendingMutex);
                ReleaseRound(g_Ready[soldier], now, "no radio");
                return false;
            }
        }
        return true;
    }

    bool NewCommand(std::uint8_t* commandsOwner, std::uint32_t soldier, std::uint16_t workGimmick, std::uint16_t workCommand, bool& flare)
    {   // a new target waits: the fire state re-aims to it once his current round is done
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
            return;   // already on the mortar: a new strike's radio exchange comes before he re-aims
        g_OrigMortarAction(mortarAction, slot, proc, data);
    }

    void __fastcall hk_Unreal_State_MortarAction(std::uint8_t* mortarAction, std::uint32_t soldier, std::int32_t proc)
    {
        bool flare = false;
        if (proc == kStateUpdate && !MissionCodeGuard::ShouldBypassHooks() && PendingUnrealTarget(mortarAction, soldier, flare)
            && HoldRound(soldier, flare ? kFlareLine : kReadyLine))
            return;   // far from Snake his timer would fire it: it pauses too
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
            WriteUnrealNextState(work, nextBefore);   // far from Snake he fires on a timer; it waits for the radio too
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
            WriteNextState(work, nextBefore);   // he stays at the mortar, aiming, until the radio exchange is over
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
        if (any)
            LogInfo("VoiceDebug: heli unrealized, the gunship call (CPR0210) can play again on every CP");
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

    void CallGunship(std::uint8_t* service, std::uint32_t cp, std::uint32_t soldier)   // the radio man, radio pose and all; CPR0220's type carries it
    {
        if (AssignRequester(service, cp, kGunshipRadio))
        {
            {
                std::lock_guard<std::mutex> lock(g_PendingMutex);
                g_Gunship[cp].awaitMs = GetTickCount64() + kGunshipAwaitMs;
            }
            const std::uint32_t radioMan = GiveRoleCall(service, cp, kGunshipLine);
            LogInfo("VoiceDebug: CP %u gunship: soldier %d takes the radio (CPR0210)", cp, radioMan != kNoSoldier ? static_cast<int>(radioMan) : -1);
            return;
        }
        GunshipFallback(cp, soldier);
    }

    void GunshipFallback(std::uint32_t cp, std::uint32_t soldier)   // his call never aired: the man the heli hit, else the nearest, calls it in, no pose
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
            LogInfo("VoiceDebug: CP %u gunship: nobody can call it in", cp);
            return;
        }
        CpRadioResponse::MemberRadio call;
        call.label = g_LineHashes[kGunshipLine];
        const bool posted = CpRadioResponse::RequestMemberRadio(static_cast<std::uint16_t>(kSoldierType | soldier), call);
        LogInfo("VoiceDebug: CP %u gunship: soldier %u calls it in himself (CPR0210)%s", cp, soldier, posted ? "" : ", radio refused");
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
    std::lock_guard<std::mutex> lock(g_PendingMutex);
    g_Calls[cp] = CallOnAir{ GetTickCount64(), 0, false };
    if (radioType == kMortarOrderRadio)
        g_Pending[cp] = PendingMortar{};
    RoleCall& roleCall = g_RoleCalls[cp];
    if (roleCall.sent && roleCall.soldier == soldier && soldier != kNoSoldier)
    {
        if (roleCall.line == kRequestLine)
            g_Pending[cp] = PendingMortar{};   // his request is on air: no CP fallback
        else
            g_Gunship[cp].awaitMs = 0;
    }
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
    if (radioType == kMortarOrderRadio)
        g_StrikeOpenedMs[cp] = 0;   // the CP's own order is over: the operator's turn
    RoleCall& roleCall = g_RoleCalls[cp];
    if (roleCall.sent && roleCall.soldier == soldier && soldier != kNoSoldier)
    {
        if (roleCall.line == kRequestLine)
        {
            if (g_CpTurnMs[soldier].load(std::memory_order_relaxed) < g_Calls[cp].onAirMs && ChaffIsUp())
            {
                const std::uint64_t now = GetTickCount64();
                g_Pending[cp] = PendingMortar{ Pending::Resend, now + kRetryForMs, now };   // the CP never heard it: it goes out again once the chaff clears
                g_StrikeOpenedMs[cp] = now;   // the operator keeps quiet until then
                LogInfo("VoiceDebug: chaff lost soldier %u's strike request (CPR0182), it goes out again once the chaff clears", soldier);
            }
            else
                g_StrikeOpenedMs[cp] = 0;   // request and reply are over: the operator's turn
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
                hold.jammed = true;    // chaff cut it before the CP answered: he asks again once it clears
                hold.onAirMs = 0;
                LogInfo("VoiceDebug: chaff cut soldier %u's %s call, he holds fire until it clears", hold.soldier, kLineLabels[hold.line]);
            }
            else if (g_SpokeMs[hold.soldier].load(std::memory_order_relaxed) >= hold.onAirMs)
                hold.done = true;      // he said his line and the CP's part is over
            else
                hold.refused = true;   // it never got his voice out
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
    const bool stillTalking = channel.soundController && channel.soldier == soldierIndex && ChannelPlaying(channel);
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
                LogInfo("VoiceDebug: chaff up mid-request, soldier %u finishes his call first", soldierIndex);
            }
            return true;
        }
        held.released = true;
        wasHeld = held.heldSinceMs != 0;
        if (wasHeld)
            LogInfo("VoiceDebug: chaff jams soldier %u's call %s (held %llu ms)", soldierIndex, release, now - held.heldSinceMs);
    }
    if (wasHeld)
    {
        const std::uint8_t* soldiers = LiveSoldiers();   // his radio action can finish on its own before the jam reaches it
        const std::uint32_t slot = SoldierRealizedSlot::Of(soldiers, soldierIndex);
        if (slot != SoldierRealizedSlot::kNotRealized)
            SoldierVoiceSwap_SayJammedTake(soldiers, slot);
    }
    return false;
}

void CpRadioVariants_OnCpIdle(std::uint32_t cp)
{
    if (cp >= kCpCount || !g_OrigRequestRadioByCp || MissionCodeGuard::ShouldBypassHooks())
        return;
    std::uint8_t* service = g_Service.load(std::memory_order_relaxed);
    if (!service)
        return;
    const std::uint64_t now = GetTickCount64();
    Pending act = Pending::None;
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
                pending.untilMs = now + kRetryForMs;   // radios are jammed, asked once it clears
                pending.triedMs = now;
            }
            else if (pending.kind == Pending::AwaitRequester)
            {
                if (now > pending.untilMs)
                {
                    act = Pending::AwaitRequester;   // his request never went on air: the CP orders it itself
                    pending = PendingMortar{};
                }
            }
            else if (pending.kind == Pending::Decide || pending.kind == Pending::Resend)
            {
                act = pending.kind;
                pending = PendingMortar{};
            }
            else if (now - pending.triedMs >= kRetryEveryMs)
            {
                pending.triedMs = now;
                act = Pending::CpOrder;
            }
        }
    }
    GunshipCall gunship;
    bool callGunship = false;
    bool gunshipFallback = false;
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
        else if (call.awaitMs && now > call.awaitMs)
        {
            call.awaitMs = 0;
            gunship = call;
            gunshipFallback = true;   // his radio call never went on air
        }
    }
    if (callGunship)
        CallGunship(service, cp, gunship.soldier);
    else if (gunshipFallback)
        GunshipFallback(cp, gunship.soldier);

    if (act == Pending::Decide || act == Pending::Resend)
        DecideStrikeCall(service, cp, act == Pending::Resend);
    else if (act == Pending::AwaitRequester)
    {
        LogInfo("VoiceDebug: CP %u strike request never aired, the CP orders it", cp);
        OrderStrikeByCp(service, cp);
    }
    else if (act == Pending::CpOrder)
    {
        g_OrigRequestRadioByCp(service, cp, kMortarOrderRadio, kNoMember, g_LineHashes[kOrderLine]);
        LogInfo("VoiceDebug: CP %u strike order not on air yet, asked again", cp);
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
    if (!FindMember(service, soldierIndex, member, cp))
        return;
    std::lock_guard<std::mutex> lock(g_PendingMutex);
    GunshipCall& call = g_Gunship[cp];
    if (call.called || call.pendingMs)
        return;
    call.soldier = soldierIndex;
    call.pendingMs = GetTickCount64();   // put on air from the CP's own update
    LogInfo("VoiceDebug: support heli hits soldier %u, CP %u calls in the gunship", soldierIndex, cp);
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
            LogInfo("VoiceDebug: soldier %u signs off his radio call (Over!!)", playing.soldier);
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

bool CpRadioVariants_SpeakSoldierHalf(std::uint32_t priority, std::uint32_t event, std::uint32_t voiceType, std::uint32_t label)
{
    const PartSpeaker speaker = t_PartSpeaker;
    if (speaker.soldier == kNoSoldier)
        return false;
    for (const std::uint32_t cpVoice : kCpVoices)
    {
        if (cpVoice == voiceType)
            return false;   // the CP's own half keeps the CP voice
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
    LogInfo("VoiceDebug: radio line 0x%X spoken by soldier %u%s", label, speaker.soldier, spoke ? "" : " refused, CP channel voice");
    return spoke;
}

bool Install_CpRadioVariants_Hooks()
{
    for (std::uint32_t i = 0; i < kLineCount; ++i)
        g_LineHashes[i] = FoxHashes::StrCode32(kLineLabels[i]);
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
    return true;
}

bool Uninstall_CpRadioVariants_Hooks()
{
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
        g_RoleCalls.fill(RoleCall{});
        g_Ready.fill(ReadyHold{});
        g_FlareCalledMs.fill(0);
        g_Channels.fill(RadioChannel{});
        g_Calls.fill(CallOnAir{});
    }
    for (std::size_t i = 0; i < kSoldierCount; ++i)
    {
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
