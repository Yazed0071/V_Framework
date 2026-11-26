#include <pch.h>
#include "Player2Impl_CallVoice.h"

#include <Windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <mutex>
#include <random>

#include "AddressSet.h"
#include "FeatureSwitch.h"
#include "HookUtils.h"
#include "log.h"
#include "MissionCodeGuard.h"
#include "Player2Impl_UpdateVoiceType.h"
#include "../buddy/BuddyOutfitRegistry.h"
#include "../equip/EquipPartParams.h"

namespace
{
    using CallVoice_t = void(__fastcall*)(std::uint8_t* player, std::uint32_t playerId, std::uint32_t voiceId, std::uint32_t priority);
    using CallVoiceWithEvent_t = std::uint64_t(__fastcall*)(std::uint8_t* player, std::uint32_t playerId, std::uint32_t dialogueEvent,
                                                            std::uint32_t voiceId, std::uint32_t priority);
    using IsPlayingVoice_t = bool(__fastcall*)(std::uint8_t* player, std::uint32_t playerId);
    using GetQuarkSystemTable_t = std::uint8_t*(__fastcall*)();
    using StateEvade_t = void(__fastcall*)(void* plugin, std::uint32_t playerIndex, std::uint32_t process, void* argument);
    using ResolveDialogueEvent_t = std::uint32_t(__fastcall*)(std::uint32_t dialogueEvent, const std::uint32_t* arguments, std::uint32_t count,
                                                              std::uint32_t sequence);

    const std::uint8_t kResolveDialogueEventEntry[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18,
                                                        0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B, 0xEA, 0x8B, 0xD1, 0x48, 0x8B, 0x0D };

    const std::uint8_t kStateEvadeEntry[] = { 0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
                                              0x48, 0x8D, 0xAC, 0x24 };
    const std::uint8_t kIsPlayingVoiceEntry[] = { 0x48, 0x8B, 0x49, 0x38, 0x48, 0x8B, 0x01, 0x48, 0xFF, 0x60, 0x10 };
    const std::uint8_t kCallVoiceWithEventEntry[] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x40, 0x8B, 0xDA, 0x4C, 0x8B, 0xD9, 0x41, 0x81, 0xF8,
                                                      0x78, 0x31, 0x6B, 0xE7, 0x75, 0x12, 0x48, 0x8B, 0x81, 0x48, 0x06, 0x00, 0x00 };   // cmp r8d,0xE76B3178: that event means the slot's breath event at +0x648

    constexpr std::uint32_t kDeathLine     = 0x47233A19u;   // PLA0160_001
    constexpr std::uint32_t kFallDeathLine = 0x43EB72CDu;   // PLA0230_001
    constexpr std::uint32_t kBurningDeath  = 0x270D7DE7u;   // the 5-second burning death scream
    constexpr std::size_t   kPlayerState   = 0x68;
    constexpr std::uint8_t  kFellToDeath   = 0x08;          // what makes LifeControllerImpl::UpdateHost send Dead with "FallDeath"

    constexpr std::size_t   kSlotBase       = 0x0C;
    constexpr std::size_t   kVoiceTypes     = 0x650;
    constexpr std::size_t   kSystemPlayer   = 0x08;         // Player2System -> Player2GameObject
    constexpr std::size_t   kPlayerImpl     = 0x58;         // the Player2Impl vanilla Player.CallVoice talks to
    constexpr std::size_t   kImplOwner      = 0x138;
    constexpr std::size_t   kOwnerPlayerId  = 0x218;
    constexpr std::size_t   kCallVoiceSlot  = 0x50 / sizeof(void*);
    constexpr std::size_t   kIsPlayingSlot  = 0x88 / sizeof(void*);
    constexpr std::uint32_t kVoicePriority  = 3;            // what the game's own player lines use
    constexpr std::uint32_t kMinorPriority  = 0;            // under every line of the game's: any of them cuts a minor line off
    constexpr std::uint64_t kMinorLineMs    = 4000;         // longer than any minor take
    constexpr std::uint32_t kMaxPlayers     = 4;

    constexpr std::uint32_t kHorseWalk      = 0x5474DDEDu;  // "Come on." (tongue click)
    constexpr std::uint32_t kHorseTrot      = 0x5474DDEAu;  // "Hup."
    constexpr std::uint32_t kHorseCanter    = 0x5474DDEBu;  // "Hm!"
    constexpr std::uint32_t kHorseGallop    = 0x5474DDE8u;  // "Ha!" / "Ya!"
    constexpr std::uint32_t kHorsePat       = 0x5474DDE1u;  // "Good, good boy." / "Good, good. That's it, way to go" / "Good, good."
    constexpr std::uint64_t kHorseGapMs     = 1500;
    constexpr std::uint64_t kHorsePatGapMs  = 5000;

    constexpr std::uint32_t kStay          = 0x176DDAB1u;  // "Stay."
    constexpr std::uint32_t kSit            = 0x156DD76Bu;  // "Sit."
    constexpr std::uint32_t kDown           = 0x146DD5F8u;  // "Down."
    constexpr std::uint32_t kStayLow        = 0x066942A0u;  // lowered "Stay."
    constexpr std::uint32_t kSitLow         = 0x07694413u;  // lowered "Sit."
    constexpr std::uint32_t kDownLow        = 0x0A6948ECu;  // lowered "Down."
    constexpr std::uint32_t kOk             = 0x1B6DE0FDu;  // "OK."
    constexpr std::uint32_t kStand          = 0x166DD8DEu;  // "Stand."
    constexpr std::uint32_t kOkLow          = 0x09694779u;  // lowered "OK, OK."
    constexpr std::uint32_t kStandLow       = 0x15695A1Du;  // lowered "Stand."
    constexpr std::uint32_t kGoodDog        = 0x186DDC24u;  // "Good, good dog."
    constexpr std::uint32_t kGoodBoy        = 0x8763FD45u;  // "Good boy!"
    constexpr std::uint32_t kAttack         = 0xCC82DC0Fu;
    constexpr std::uint32_t kAttackAlt      = 0xCC82DC08u;  // "Go get him!"
    constexpr std::uint32_t kAttackLow      = 0xFBC52691u;
    constexpr std::uint32_t kAttackLowAlt   = 0xFBC52696u;
    constexpr std::uint32_t kExtract        = 0x7CE464FFu;
    constexpr std::uint32_t kExtractLow     = 0xD4F3BA78u;
    constexpr std::uint32_t kExtractLowAlt  = 0xD4F3BA79u;
    constexpr std::uint32_t kHold           = 0xE97BAE94u;  // "Hold 'em off!"
    constexpr std::uint32_t kHoldAlt        = 0xE97BAE91u;  // "Stop 'em."
    constexpr std::uint32_t kHoldAlt2       = 0xE97BAE95u;  // "Go get him!"
    constexpr std::uint32_t kHoldLow        = 0x8DF0A879u;
    constexpr std::uint32_t kBark           = 0x156B98F4u;  // "Bark."
    constexpr std::uint32_t kBarkLow        = 0x86663A49u;
    constexpr std::uint32_t kCallBack       = 0xCBCE9E53u;  // "DD."
    constexpr std::uint32_t kCallBackLow    = 0xCBCE9E4Eu;
    constexpr std::uint32_t kBeQuiet        = 0x116DD13Fu;  // "Be quiet."
    constexpr std::uint32_t kQuietLow       = 0x146958AAu;  // lowered "Quiet."
    constexpr std::uint32_t kNo             = 0x1A6DDF0Au;  // "No. Bad dog."
    constexpr std::uint32_t kNoLow          = 0x08694586u;  // lowered "No."
    constexpr std::uint32_t kScreamDD       = 0xCBCE9E5Cu;  // "DD!"

    constexpr std::uint32_t kCallQuiet      = 0xF02242B3u;  // "Quiet."
    constexpr std::uint32_t kCallQuietLow   = 0xF02242AEu;
    constexpr std::uint32_t kScreamQuiet    = 0xF02242BCu;  // "Quieeeet!"
    constexpr std::uint32_t kCoverMe        = 0xA2B0C55Au;
    constexpr std::uint32_t kCoverMeLow     = 0xE2DE49F4u;
    constexpr std::uint32_t kCoverFire      = 0xEA53A27Au;  // "Cover fire."
    constexpr std::uint32_t kCoverFireLow   = 0xEA53A268u;  // lowered "Cover fire."
    constexpr std::uint32_t kStartCover     = 0x5B71A2F9u;  // "Fire."
    constexpr std::uint32_t kStartCoverLow  = 0x5B71A2EBu;  // tense "Fire."
    constexpr std::uint32_t kStopCover      = 0x86A3E8C9u;  // "Cease fire." "Cease cover fire." "Stand down."
    constexpr std::uint32_t kStopCoverOk    = 0x86A3E8C8u;  // "OK."
    constexpr std::uint32_t kStopCoverLow   = 0xC6772E2Fu;  // tense "Cease cover fire." "Stand down."
    constexpr std::uint32_t kCeaseFireLow   = 0xC6772E2Du;  // tense "Cease fire."
    constexpr std::uint32_t kStopCoverOkLow = 0xC6772E2Eu;  // tense "OK."
    constexpr std::uint32_t kStealthMode    = 0x640A38B4u;  // "Stealth Mode."
    constexpr std::uint32_t kStealth        = 0x640A38B7u;  // "Stealth."
    constexpr std::uint32_t kInterceptMode  = 0xB0BF8EA0u;  // "Intercept Mode."
    constexpr std::uint32_t kInterceptFire  = 0xB0BF8EA3u;  // "Fire."
    constexpr std::uint32_t kEngageTargets  = 0xB0BF8EA2u;  // "Engage targets."
    constexpr std::uint32_t kSearchMode     = 0x11DF1BABu;  // "Search Mode."
    constexpr std::uint32_t kStandGuard     = 0x11DF1BA9u;  // "Stand guard."
    constexpr std::uint32_t kNormalMode     = 0x1D56D852u;
    constexpr std::uint32_t kCancel         = 0x1D56D854u;  // "Cancel."
    constexpr std::uint32_t kReturn         = 0x1D56D855u;  // "Return."
    constexpr std::uint32_t kChildGo         = 0xBD79A0B7u;  // the children's Go order: "Now." "Let's go." "Come on." "Go."
    constexpr std::uint32_t kComeWithMe      = 0xBD79A0B3u;  // "Come with me."
    constexpr std::uint32_t kAllRight        = 0xBD79A0B6u;  // "All right."
    constexpr std::uint32_t kChildGoTense    = 0x2A10EA62u;
    constexpr std::uint32_t kComeWithMeTense = 0x2A10EA66u;
    constexpr std::uint32_t kAllRightTense   = 0x2A10EA63u;
    constexpr std::uint32_t kShock          = 0x46DC3CDFu;
    constexpr std::uint32_t kShockLong      = 0x46DC3CDCu;  // the long electrocution scream
    constexpr std::uint32_t kPatOutFire     = 0xB37386F3u;  // patting out the fire, tense but not panicking
    constexpr std::uint32_t kFireWontGoOut  = 0xB37386F0u;  // patting out the fire that isn't going out
    constexpr std::uint32_t kDrownDeath     = 0xBA763430u;  // drowning
    constexpr std::uint32_t kHurt           = 0x235985BFu;  // the "Ugh" of a hit
    constexpr std::uint32_t kHurtHeavy      = 0xD8A24D40u;
    constexpr std::uint32_t kHoldDogOff     = 0x482E392Eu;  // "Rrrghh..." holding a dog's head back
    constexpr std::uint32_t kJehuty         = 0x783FF64Fu;  // "Haidara!"
    constexpr std::uint32_t kJehutyLonger   = 0x773FF4DCu;  // "Haidaraaa!"
    constexpr std::uint32_t kJehutyLongest  = 0x7A3FF995u;  // "Haidaraaaaaa!"
    constexpr std::uint32_t kCameraMode     = 0x9E59EC09u;  // "Camera Mode."
    constexpr std::uint32_t kCamera         = 0x9E59EC0Au;  // "Camera."
    constexpr std::uint32_t kQuietFire      = 0xFBCEE86Fu;  // "Fire."
    constexpr std::uint32_t kQuietFireLow   = 0xFBCEE87Du;  // lowered "Fire!"
    constexpr std::uint32_t kTakeHimOut     = 0x8786C4C8u;  // "Take him out." (Snake: "Kill shot.")
    constexpr std::uint32_t kTakeHimOutLow  = 0x87A9570Eu;  // lowered "Take him out." (Snake: "Kill shot.")
    constexpr std::uint32_t kNapTime        = 0x42DEC479u;  // "Nap time." / "Put him to sleep." / "Put 'em to sleep.", by voice
    constexpr std::uint32_t kNapTimeLow     = 0x42DEC46Bu;  // lowered "Nap time." / "Put 'em to sleep."
    constexpr std::uint32_t kHorseGetAway   = 0x5374DC77u;  // "Get, get." "Stay back. Stay back." (DD: "Get." "Go on!" "Stay back!")
    constexpr std::uint32_t kGo             = 0xD221243Fu;  // "Go." / "Move."
    constexpr std::uint32_t kGoLow          = 0xD221242Du;  // lowered "Go!" / "Go."
    constexpr std::uint32_t kHorseStink     = 0x1D2E790Cu;  // "Urgh!" at a foul smell

    constexpr std::uint32_t kPunch          = 0x747178CAu;
    constexpr std::uint32_t kKick           = 0x29085F29u;
    constexpr std::uint32_t kThrow          = 0x8719B80Cu;
    constexpr std::uint32_t kThrowAlt       = 0xBCEE9C6Bu;
    constexpr std::uint32_t kHipFireRoar    = 0x87A50AC6u;  // the long roar while firing a machine gun from the hip
    constexpr std::uint32_t kShot           = 0x58DEB7FAu;
    constexpr std::uint32_t kShotAlt        = 0xE621AB84u;
    constexpr std::uint32_t kBlasted        = 0x48C42BF4u;  // caught by an explosion
    constexpr std::uint32_t kShieldBurst    = 0x200AD0A6u;  // automatic fire on the shield
    constexpr std::uint32_t kShieldBlast    = 0x200AD0A5u;
    constexpr std::uint32_t kChoked         = 0xD492AF3Au;  // grabbed in a chokehold
    constexpr std::uint32_t kChokedOut      = 0xD492AF39u;
    constexpr std::uint32_t kChokedDead     = 0x4129F8D1u;
    constexpr std::uint32_t kCough          = 0x0F53C36Eu;
    constexpr std::uint32_t kSnore          = 0x193AF5C0u;
    constexpr std::uint32_t kSleep          = 0x54368E87u;
    constexpr std::uint32_t kSleepSnake     = 0x203B00C5u;
    constexpr std::uint32_t kWakeBreath     = 0x30B510D2u;
    constexpr std::uint32_t kWakeMoan       = 0xF47FB48Du;
    constexpr std::uint32_t kWakeMoanAlt    = 0xF47FB489u;
    constexpr std::uint32_t kCqcStart       = 0xD9211D3Au;  // the short exhale through the nose as CQC starts
    constexpr std::uint32_t kPunchAlt       = 0x201DEA1Eu;  // also the breath of a dive
    constexpr std::uint32_t kTranqDown      = 0xF602DE38u;  // consciousness slipping away: gas, a dart taking effect
    constexpr std::uint32_t kTranqDownAlt   = 0xF602DE3Bu;  // the other tranquilizer take, asked only from motion data
    constexpr std::uint32_t kHorseDung      = 0x43E89461u;  // "Come on." / "Nngh." as he makes the horse defecate
    constexpr std::uint32_t kHoldUp         = 0x07C16A02u;  // "Hands up!"
    constexpr std::uint32_t kHoldUpAlt      = 0x07C16A01u;  // "Don't move!"
    constexpr std::uint32_t kCurePain       = 0x14863767u;  // the two pain lines of treating a serious injury
    constexpr std::uint32_t kCurePainAlt    = 0x14863764u;
    constexpr std::uint32_t kThrowFail      = 0xA40A9DE5u;  // "Huh!?" "Damn it..." as a throw fails
    constexpr std::uint32_t kPant           = 0xD5F204FDu;  // a few seconds of panting while sprinting
    constexpr std::uint32_t kHurtBreath     = 0xEAF96CFCu;  // breathing hard with built-up damage
    constexpr std::uint32_t kHurtBreathHard = 0xEF5D779Bu;
    constexpr std::uint32_t kFultonScream   = 0xFD5FBA14u;  // "Aaaahhh!" as a Fulton balloon pulls him up

    constexpr std::uint32_t kEliRestrained  = 0xB34D0523u;  // LIQV_0010
    constexpr std::uint32_t kEliStruggle    = 0xB34D0522u;  // LIQV_0011
    constexpr std::uint32_t kEliBattleCry   = 0x3850153Au;  // LIQV_0103
    constexpr std::uint32_t kEliAttackShout = 0x4954ACCAu;  // LIQV_0370
    constexpr std::uint32_t kEliExhale      = 0x365011D3u;  // LIQV_0120
    constexpr std::uint32_t kEliHurtMild    = 0x35501060u;  // LIQV_0130
    constexpr std::uint32_t kEliHurtMedium  = 0x3C501B65u;  // LIQV_0140
    constexpr std::uint32_t kEliHurtSevere  = 0x3B5019F2u;  // LIQV_0150
    constexpr std::uint32_t kEliBreathless  = 0x3B5019F3u;  // LIQV_0151
    constexpr std::uint32_t kEliCurse       = 0x3B5019F0u;  // LIQV_0152
    constexpr std::uint32_t kEliBlastDeath  = 0x3A50181Fu;  // LIQV_0160
    constexpr std::uint32_t kEliDeath       = 0x395016ACu;  // LIQV_0170
    constexpr std::uint32_t kEliFalling     = 0x395016ADu;  // LIQV_0171
    constexpr std::uint32_t kEliCough       = 0x40502191u;  // LIQV_0180
    constexpr std::uint32_t kEliTranquilized = 0x3F50203Eu;  // LIQV_0190
    constexpr std::uint32_t kEliFallAsleep  = 0x3F50203Fu;  // LIQV_0191
    constexpr std::uint32_t kEliSleep       = 0x3E525D22u;  // LIQV_0200
    constexpr std::uint32_t kEliWake        = 0x3F525E95u;  // LIQV_0210
    constexpr std::uint32_t kEliSurprise    = 0x3B5496C0u;  // LIQV_0390
    constexpr std::uint32_t kEliKikongo     = 0x4D56F1EDu;  // LIQV_0430
    constexpr std::uint32_t kEliHey         = 0x4256E0BCu;  // LIQV_0480
    constexpr std::uint32_t kEliEvade       = 0x4656E6E8u;  // LIQV_0440
    constexpr std::uint32_t kEliEvadeLaughs = 0xC656E6E8u;  // no line of his bank: hk_ResolveDialogueEvent answers it with LIQV_0440 minus its first take
    constexpr std::uint32_t kEliEvadeWhoa   = 0xD656E6E8u;  // nor this one: LIQV_0440's first take alone
    constexpr std::uint32_t kEliEvadeTake1  = 0x2FFB35FDu;  // the sound objects of that line's three takes, the same in the English and Japanese bank
    constexpr std::uint32_t kEliEvadeTake2  = 0x2FFB3600u;
    constexpr std::uint32_t kEliEvadeTake3  = 0x2FFB3603u;
    constexpr std::uint32_t kEliVoiceEvent  = 0x57488340u;
    constexpr std::uint32_t kBreathEvent    = 0xE76B3178u;  // how the game names the slot's breath event; Eli's slot keeps Snake's, which holds nothing of his

    constexpr std::uint16_t kGuiltyButterfly           = 2;  // Quiet's tranquilizer rifle, EQP_WP_Quiet_sr_020 firing BL_RfTranq
    constexpr std::uint16_t kGuiltyButterflySuppressed = 3;
    constexpr std::uint16_t kQuietVanillaWeapons       = 5;  // 0-1 Wicked Butterfly, 2-3 Guilty Butterfly, 4 Sinful Butterfly

    constexpr std::uint16_t kAtkInWater     = 0x4A;
    constexpr std::uint16_t kAtkWolfBite    = 0x8C;
    constexpr std::uint16_t kAtkDDogBite    = 0x90;

    constexpr std::size_t   kQuarkPhaseRoot = 0x98;
    constexpr std::size_t   kPhaseHolder    = 0x50;
    constexpr std::size_t   kPhase          = 0x28;         // IsAlertPhase 0x140A51D00 reads it: 6-9 is alert

    constexpr std::uint64_t kBeQuietMs      = 10000;
    constexpr std::uint64_t kNoMs           = 15000;
    constexpr std::uint64_t kBurnedLatelyMs = 1000;         // a burning player takes a fire tick every 0.25 s; the one that empties his life can come back as UpdateHost's death line, with no attack
    constexpr std::uint64_t kStillBurningMs = 2000;
    constexpr std::uint64_t kInWaterLatelyMs = 1000;
    constexpr std::uint64_t kEvadeLatelyMs  = 250;          // the dive's state runs every frame of the dive
    constexpr std::uint64_t kEvadeOnceMs    = 1500;         // one dive asks for its effort line twice
    constexpr std::uint64_t kSprintGapMs    = 1000;         // a sprint asks for the panting line every stride, about twice a second
    constexpr std::uint64_t kSprintEndMs    = 700;
    constexpr std::uint64_t kLongSprintMs   = 5000;         // from here on he catches his breath once the sprint ends
    constexpr std::uint32_t kBreathAsksPerSprint = 2;       // after any sprint the game asks for the out-of-breath line as its countdown passes 1 s and 0
    constexpr std::uint64_t kCureScreamMs   = 3200;         // his long scream; one treatment asks for both pain lines
    constexpr std::uint64_t kShockTakeMs    = 3500;         // his two restrained takes in English; a current asks again with every jolt
    constexpr std::uint64_t kStunHitLatelyMs = 500;
    constexpr std::uint16_t kDamageFlagStun = 1u << 4;      // DamageParameterTables' stun column: blows, shield bashes, body attacks, stun weapons
    constexpr std::uint16_t kDamageFlagTranq = 1u << 3;
    constexpr std::uint16_t kDamageFlagGas  = 1u << 10;     // smoke and sleep gas; smoke alone leaves the tranquilizer column clear
    constexpr std::uint64_t kSmokeHitLatelyMs = 500;
    constexpr std::uint64_t kCoughTakeMs    = 4500;         // his one cough take; smoke hits him again every few seconds
    constexpr std::uint64_t kCqcLatelyMs    = 2500;         // a combo's punches and kick follow its command within this

    enum class DogOrder { None, Bark, Busy, Other, CallBack };

    CallVoice_t g_Orig       = nullptr;
    void*       g_HookTarget = nullptr;

    IsPlayingVoice_t           g_OrigIsPlaying   = nullptr;
    void*                      g_IsPlayingTarget = nullptr;
    CallVoiceWithEvent_t       g_OrigWithEvent   = nullptr;
    void*                      g_WithEventTarget = nullptr;
    thread_local bool          t_MinorLine       = false;   // SayMinorPlayerLine is the caller
    thread_local bool          t_OwnLine         = false;   // one of our Say* calls is the caller
    std::atomic<std::uint64_t> g_HoldGameLinesUntilMs{ 0 };
    std::atomic<std::uint64_t> g_MinorLineMs[kMaxPlayers];  // when his minor line started, 0 once another line came

    std::mutex    g_DogMutex;
    std::mt19937  g_Random{ static_cast<std::uint32_t>(GetTickCount64()) };
    DogOrder      g_LastOrder   = DogOrder::None;
    std::uint64_t g_LastOrderMs = 0;

    std::uint64_t g_LastHorseLineMs = 0;
    std::uint64_t g_LastHorsePatMs  = 0;

    thread_local std::uint16_t t_DamageAttack = 0;
    std::atomic<std::uint64_t> g_FireDamageMs{ 0 };
    std::atomic<std::uint64_t> g_InWaterDamageMs{ 0 };
    std::uint64_t g_BurnStartMs      = 0;
    bool          g_SaidStillBurning = false;

    StateEvade_t               g_OrigStateEvade   = nullptr;
    void*                      g_StateEvadeTarget = nullptr;
    std::atomic<std::uint64_t> g_EvadeMs{ 0 };              // the last frame of a dive
    std::uint64_t g_EliEvadeMs           = 0;
    std::uint64_t g_EliCureMs            = 0;
    std::uint64_t g_EliShockMs           = 0;
    std::atomic<std::uint64_t> g_StunHitMs{ 0 };
    std::atomic<std::uint64_t> g_SmokeHitMs{ 0 };
    std::atomic<std::uint64_t> g_CqcCommandMs{ 0 };
    std::uint64_t g_EliCoughMs           = 0;
    ResolveDialogueEvent_t     g_OrigResolve   = nullptr;
    void*                      g_ResolveTarget = nullptr;
    std::atomic<std::uint32_t> g_EliLaughTurn{ 0 };
    std::uint64_t g_SprintStartMs        = 0;
    std::uint64_t g_LastPantMs           = 0;
    std::uint32_t g_BreathAsksSinceSprint = kBreathAsksPerSprint;
    std::atomic<std::uint64_t> g_CatchBreathAfterMs{ 0 };   // the latest stride of a sprint long enough to leave him winded, 0 otherwise

    std::uint32_t ReadVoiceType_SEH(const std::uint8_t* player, std::uint32_t playerId)
    {
        __try
        {
            const std::int32_t slot = static_cast<std::int32_t>(playerId) - *reinterpret_cast<const std::int32_t*>(player + kSlotBase);
            const auto* voiceTypes = *reinterpret_cast<std::uint32_t* const*>(player + kVoiceTypes);
            if (slot < 0 || !voiceTypes)
                return 0;
            return voiceTypes[slot];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    bool HasDogLines(std::uint32_t voiceType)
    {
        switch (voiceType)
        {
        case 0x3CF677C6u: case 0x2EED93D9u: case 0x2EED93DAu: case 0x2EED93DBu: case 0x2EED93DCu:
        case 0x80400AFAu: case 0x80400AF8u: case 0x80400AF9u: case 0x80400AFFu:
            return true;
        default:
            return false;
        }
    }

    bool IsSnake(std::uint32_t voiceType)   // the other voices' leaves for many lines point at sound objects no bank defines: silence
    {
        return voiceType == 0x3CF677C6u;
    }

    bool HasHorseWalkLine(std::uint32_t voiceType)   // "Come on." to D-Horse
    {
        return voiceType == 0x3CF677C6u || voiceType == 0x2EED93D9u || voiceType == 0x2EED93DAu || voiceType == 0x2EED93DBu;
    }

    bool HasCameraModeLines(std::uint32_t voiceType)   // only DD male A and D recorded them
    {
        return voiceType == 0x2EED93D9u || voiceType == 0x2EED93DCu;
    }

    bool HasStopCoverOk(std::uint32_t voiceType, bool tense)   // "OK." to Quiet: Snake; the tense one also DD male D and DD female D
    {
        return voiceType == 0x3CF677C6u || (tense && (voiceType == 0x2EED93DCu || voiceType == 0x80400AFFu));
    }

    bool IsFireAttack(std::uint16_t attack)   // every row DamageParameterTables marks isFire
    {
        switch (attack)
        {
        case 0x45: case 0x46: case 0x47: case 0x48:   // ATK_NormalFlame, ATK_SmallFlame, ATK_MiddleFlame, ATK_LargeFlame
        case 0x79:                                    // ATK_FireBurn
        case 0x96: case 0xF6:                         // ATK_WalkerGear_Flame, ATK_Sahelan_Flame
        case 0xBF: case 0xC0: case 0xC1:              // ATK_MolotovCocktailFire, ATK_MolotovCocktail, ATK_MolotovCocktailFire_G1
        case 0xC2: case 0xC3: case 0xC4:              // ATK_MolotovCocktail_G1, ATK_MolotovCocktailFire_G2, ATK_MolotovCocktail_G2
        case 0x2D:                                    // ATK_VolginGameOver
        case 0xE2: case 0xE3: case 0xE4: case 0xE5:   // ATK_VolRideBullet, ATK_VolRideFirewall, ATK_VolginChargeExplosion, ATK_VolginFirePillar
        case 0xE6: case 0xE7: case 0xE8: case 0xE9:   // ATK_VolginFireball, ATK_VolginBodyFire, ATK_VolginBodyFireVehicle, ATK_VolginBodyFireVehicleCrushed
        case 0xEA: case 0xEB:                         // ATK_VolginFireBullet, ATK_VolginGroundFire
            return true;
        default:
            return false;
        }
    }

    std::int32_t ReadPhase_SEH()   // CallVoiceBuddy says the lowered take of an order from 1 up
    {
        const auto getQuark = reinterpret_cast<GetQuarkSystemTable_t>(ResolveGameAddress(gAddr.GetQuarkSystemTable));
        if (!getQuark)
            return 0;
        __try
        {
            const std::uint8_t* quark  = getQuark();
            const std::uint8_t* root   = quark ? *reinterpret_cast<std::uint8_t* const*>(quark + kQuarkPhaseRoot) : nullptr;
            const std::uint8_t* holder = root ? *reinterpret_cast<std::uint8_t* const*>(root + kPhaseHolder) : nullptr;
            return holder ? *reinterpret_cast<const std::int32_t*>(holder + kPhase) : 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    bool IsAlertPhase_SEH()
    {
        return static_cast<std::uint32_t>(ReadPhase_SEH() - 6) <= 3;
    }

    std::int32_t ReadQuietWeapon_SEH()
    {
        const std::uint8_t* vars = buddyoutfit::ReadScriptVars();
        if (!vars)
            return -1;
        __try
        {
            return *reinterpret_cast<const std::uint16_t*>(vars + buddyoutfit::kVarQuietWeapon);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
    }

    bool HasTakes(std::uint32_t line)
    {
        switch (line)
        {
        case kOk: case kOkLow: case kGoodDog:
        case kStealthMode: case kInterceptMode: case kNormalMode: case kCoverMe: case kCoverMeLow:
            return true;
        default:
            return false;
        }
    }

    DogOrder OrderOf(std::uint32_t line)
    {
        switch (line)
        {
        case kBark: case kBarkLow:
            return DogOrder::Bark;
        case kAttack: case kAttackLow: case kHold: case kHoldLow: case kExtract: case kExtractLow:
            return DogOrder::Busy;
        case kStay: case kStayLow:
            return DogOrder::Other;
        case kCallBack: case kCallBackLow:
            return DogOrder::CallBack;
        default:
            return DogOrder::None;
        }
    }

    bool IsLowered(std::uint32_t line)
    {
        return line == kStayLow || line == kBarkLow || line == kAttackLow || line == kHoldLow || line == kExtractLow || line == kCallBackLow;
    }

    std::uint32_t Pick(std::initializer_list<std::uint32_t> lines)
    {
        return lines.begin()[g_Random() % lines.size()];
    }

    std::uint32_t MixedTake(std::uint32_t line, bool extra)
    {
        switch (line)
        {
        case kStay:          return Pick({ kStay, kSit, kDown });
        case kStayLow:       return Pick({ kStayLow, kSitLow, kDownLow });
        case kOk:            return extra ? Pick({ kOk, kStand }) : line;
        case kOkLow:         return extra ? Pick({ kOkLow, kStandLow }) : line;
        case kGoodDog:       return Pick({ kGoodDog, kGoodBoy });
        case kAttack:        return Pick({ kAttack, kAttackAlt });
        case kAttackLow:     return Pick({ kAttackLow, kAttackLowAlt });
        case kExtractLow:    return extra ? Pick({ kExtractLow, kExtractLowAlt }) : line;
        case kHold:          return extra ? Pick({ kHold, kHoldAlt, kHoldAlt2 }) : Pick({ kHold, kHoldAlt });
        case kStealthMode:   return Pick({ kStealthMode, kStealth });
        case kInterceptMode: return Pick({ kInterceptMode, kInterceptFire, kEngageTargets });
        case kNormalMode:    return Pick({ kNormalMode, kCancel, kReturn });
        case kCoverMe:       return Pick({ kCoverMe, kCoverFire, kStartCover });
        case kCoverMeLow:    return Pick({ kCoverMeLow, kCoverFireLow, kStartCoverLow });
        default:             return line;
        }
    }

    std::uint32_t ChildGoTake(std::uint32_t line)   // the vanilla key is a random set of four takes: each of the six lines gets 1/6
    {
        const bool tense = line == kChildGoTense;
        const std::uint32_t roll = g_Random() % 6;
        if (roll < 4)
            return line;
        if (roll == 4)
            return tense ? kComeWithMeTense : kComeWithMe;
        return tense ? kAllRightTense : kAllRight;
    }

    std::uint32_t StopCoverTake(std::uint32_t line, std::uint32_t voiceType)   // the vanilla keys hold 3 (normal) and 2 (tense) takes: every line equally likely
    {
        const bool tense = line == kStopCoverLow;
        const bool ok    = HasStopCoverOk(voiceType, tense);
        if (!tense)
            return ok && g_Random() % 4 == 0 ? kStopCoverOk : line;
        const std::uint32_t roll = g_Random() % (ok ? 4 : 3);
        if (roll < 2)
            return line;
        return roll == 2 ? kCeaseFireLow : kStopCoverOkLow;
    }

    std::uint32_t QuietFireTake(std::uint32_t line, std::int32_t weapon)   // the vanilla keys hold one take: each line gets 1/2
    {
        const bool tense = line == kQuietFireLow;
        const bool tranq = weapon == kGuiltyButterfly || weapon == kGuiltyButterflySuppressed;
        if (g_Random() % 2 == 0)
            return line;
        if (tranq)
            return tense ? kNapTimeLow : kNapTime;
        return tense ? kTakeHimOutLow : kTakeHimOut;
    }

    std::uint32_t BurnLine()   // the first fire tick after a quiet second starts a new fire
    {
        const std::uint64_t now = GetTickCount64();
        std::lock_guard<std::mutex> lock(g_DogMutex);
        const std::uint64_t last = g_FireDamageMs.exchange(now, std::memory_order_relaxed);
        if (now - last > kBurnedLatelyMs)
        {
            g_BurnStartMs      = now;
            g_SaidStillBurning = false;
            return kPatOutFire;
        }
        if (g_SaidStillBurning || now - g_BurnStartMs < kStillBurningMs)
            return 0;
        g_SaidStillBurning = true;
        return kFireWontGoOut;
    }

    std::uint32_t PlayerLine(const std::uint8_t* player, std::uint32_t playerId, std::uint32_t line)
    {
        const bool painLines  = FeatureSwitch::IsOn(FeatureSwitch::Feature::PainAndDeathLines);
        const bool buddyLines = FeatureSwitch::IsOn(FeatureSwitch::Feature::BuddyLines);
        if (painLines && (line == kHurt || line == kHurtHeavy) && (t_DamageAttack == kAtkWolfBite || t_DamageAttack == kAtkDDogBite))
        {
            const std::uint32_t voiceType = ReadVoiceType_SEH(player, playerId);
            return HasDogLines(voiceType) || voiceType == kPlayerVoiceTypeEli ? kHoldDogOff : line;
        }
        if (line == kJehuty || line == kJehutyLonger)
        {
            if (!IsSnake(ReadVoiceType_SEH(player, playerId)))
                return line;
            std::lock_guard<std::mutex> lock(g_DogMutex);
            return g_Random() % 3 == 0 ? kJehutyLongest : line;   // the game picks one of its two takes: each of the three gets 1/3
        }
        if (buddyLines && line == kSearchMode)
        {
            const std::uint32_t voiceType = ReadVoiceType_SEH(player, playerId);
            if (!HasDogLines(voiceType))
                return line;
            std::lock_guard<std::mutex> lock(g_DogMutex);
            return HasCameraModeLines(voiceType) ? Pick({ kSearchMode, kStandGuard, kCameraMode, kCamera }) : Pick({ kSearchMode, kStandGuard });
        }
        if (buddyLines && (line == kStopCover || line == kStopCoverLow))
        {
            const std::uint32_t voiceType = ReadVoiceType_SEH(player, playerId);
            if (!HasDogLines(voiceType))
                return line;
            std::lock_guard<std::mutex> lock(g_DogMutex);
            return StopCoverTake(line, voiceType);
        }
        if (line == kChildGo || line == kChildGoTense)
        {
            if (!IsSnake(ReadVoiceType_SEH(player, playerId)))
                return line;
            std::lock_guard<std::mutex> lock(g_DogMutex);
            return ChildGoTake(line);
        }
        if (painLines && line == kShock)
        {
            if (!IsSnake(ReadVoiceType_SEH(player, playerId)))
                return line;
            std::lock_guard<std::mutex> lock(g_DogMutex);
            return Pick({ kShock, kShockLong });
        }
        if (buddyLines && (line == kCallQuiet || line == kCallQuietLow))
            return IsSnake(ReadVoiceType_SEH(player, playerId)) && IsAlertPhase_SEH() ? kScreamQuiet : line;
        if (line == kHorseWalk)
            return HasHorseWalkLine(ReadVoiceType_SEH(player, playerId)) ? line : kHorseTrot;
        if (buddyLines && (line == kQuietFire || line == kQuietFireLow))
        {
            const std::int32_t weapon = ReadQuietWeapon_SEH();
            if (!HasDogLines(ReadVoiceType_SEH(player, playerId)) || weapon < 0 || weapon >= kQuietVanillaWeapons)
                return line;   // a custom rifle can be either kind
            std::lock_guard<std::mutex> lock(g_DogMutex);
            return QuietFireTake(line, weapon);
        }
        if (buddyLines && line == kHorseGetAway)
        {
            if (!HasDogLines(ReadVoiceType_SEH(player, playerId)))
                return line;
            const bool tense = ReadPhase_SEH() >= 1;
            std::lock_guard<std::mutex> lock(g_DogMutex);
            return g_Random() % 3 == 0 ? (tense ? kGoLow : kGo) : line;   // the vanilla key holds two takes: each of the three lines gets 1/3
        }
        if (!buddyLines)
            return line;

        const DogOrder order = OrderOf(line);
        if (order == DogOrder::None && !HasTakes(line))
            return line;

        const std::uint32_t voiceType = ReadVoiceType_SEH(player, playerId);
        if (!HasDogLines(voiceType))
            return line;
        const bool extra = IsSnake(voiceType);   // "Stand.", "Be quiet." and the extra Fulton/hold takes
        const bool alert = order == DogOrder::CallBack && IsAlertPhase_SEH();

        std::lock_guard<std::mutex> lock(g_DogMutex);
        if (order == DogOrder::None)
            return MixedTake(line, extra);

        const std::uint64_t now   = GetTickCount64();
        const std::uint64_t since = now - g_LastOrderMs;
        const DogOrder      last  = g_LastOrder;
        g_LastOrder   = order;
        g_LastOrderMs = now;

        if (order != DogOrder::Bark && last == DogOrder::Bark && since <= kBeQuietMs && extra)
            return IsLowered(line) ? kQuietLow : kBeQuiet;
        if (order == DogOrder::CallBack && (last == DogOrder::Busy || last == DogOrder::Bark) && since <= kNoMs)
            return IsLowered(line) ? kNoLow : kNo;
        if (alert)
            return kScreamDD;
        return MixedTake(line, extra);
    }

    bool IsBuddyAttackOrder(std::uint32_t line)   // each order's normal and lowered takes
    {
        switch (line)
        {
        case 0xCC82DC0Fu: case 0xCC82DC08u: case 0xFBC52691u: case 0xFBC52696u:   // D-Dog: attack
        case 0xE97BAE91u: case 0xE97BAE94u: case 0xE97BAE95u: case 0x8DF0A879u:   // D-Dog: stop the enemy
        case 0x7CE464FFu: case 0xD4F3BA78u: case 0xD4F3BA79u:                     // D-Dog: extract
        case 0x4F373918u: case 0x4F37390Au:                                       // attack
        case 0xFBCEE86Fu: case 0xFBCEE87Du:                                       // fire
        case 0x8786C4C8u: case 0x8786C4CAu: case 0x87A9570Cu: case 0x87A9570Eu:   // kill him
        case 0x7FAF76BDu: case 0x7FAF76BFu: case 0x7F8CE479u: case 0x7F8CE47Bu:   // knock him out
        case 0x37076546u: case 0x37076554u:                                       // destroy
        case 0x7CCD42C7u: case 0x7CCD42D5u:                                       // blow up
        case 0xEA53A27Au: case 0xEA53A268u:                                       // cover me
        case 0x42DEC479u: case 0x42DEC46Bu:                                       // put him to sleep
        case 0xCF44CD65u: case 0xCF44CD77u:                                       // aim
        case 0x5B71A2F9u: case 0xA2B0C55Au: case 0x5B71A2EBu: case 0xE2DE49F4u:   // start covering fire
            return true;
        default:
            return false;
        }
    }

    bool IsBuddyOrder(std::uint32_t line)   // every other order or call to D-Dog, Quiet, D-Walker or a buddy in general
    {
        switch (line)
        {
        case 0x1B6DE0FDu: case 0x09694779u:                                       // OK
        case 0x1A6DDF0Au: case 0x08694586u:                                       // no
        case 0x156DD76Bu: case 0x07694413u:                                       // sit
        case 0x146DD5F8u: case 0x0A6948ECu:                                       // down
        case 0x176DDAB1u: case 0x066942A0u:                                       // stay
        case 0x166DD8DEu: case 0x15695A1Du:                                       // stand
        case 0x116DD13Fu: case 0x146958AAu:                                       // quiet
        case 0x156B98F4u: case 0x86663A49u:                                       // bark
        case 0x8263F5A6u: case 0x8463F88Cu: case 0x8663FBD2u:                     // shake, play dead, go get it
        case 0x896400ABu: case 0x8863FF38u:                                       // calling DD at play
        case 0xC0A2858Eu: case 0xC0A2858Du: case 0x8B6C314Cu:                     // come here
        case 0x4883983Bu: case 0x48839838u: case 0x1ED4E719u:                     // come back
        case 0xB23D9DF4u: case 0xB23D9DE6u:                                       // wait
        case 0xEC9CA10Du: case 0xEC9CA10Fu: case 0x2CC892A9u: case 0x2CC892ABu:   // get in
        case 0xD9DCA3D8u: case 0xD9DCA3DAu: case 0xD9AF847Cu: case 0xD9AF847Eu:   // get out
        case 0xD221243Fu: case 0xD221242Du:                                       // go
        case 0x6589DAE8u: case 0x6589DAFAu:                                       // don't go
        case 0x9AADC864u: case 0x9AADC876u:                                       // climb
        case 0x4882AB7Bu: case 0x4882AB69u:                                       // descend
        case 0x9C9C01DCu: case 0x9C9C01CEu:                                       // pull back
        case 0x1035F8B0u: case 0x1035F8A2u:                                       // rescue
        case 0x56EAEB91u: case 0x56EAEB83u:                                       // help me
        case 0x715718BBu: case 0x715718A9u:                                       // hide
        case 0xC38235A4u: case 0xC38235B6u:                                       // go take a look
        case 0xD6477A22u: case 0xD6477A30u:                                       // lure them
        case 0x3BDC16B8u: case 0x3BDC16AAu:                                       // crouch down
        case 0x6872FB8Fu: case 0x6872FB9Du:                                       // lie down
        case 0x21BF9BAEu: case 0x21BF9BBCu:                                       // follow me
        case 0x86A3E8C9u: case 0x86A3E8C8u: case 0xC6772E2Du: case 0xC6772E2Fu: case 0xC6772E2Eu:   // stop covering fire
        case 0x9E59EC09u: case 0x9E59EC0Au:                                       // D-Walker: camera mode
        case 0x640A38B4u: case 0x640A38B7u:                                       // D-Walker: stealth mode
        case 0x11DF1BABu: case 0x11DF1BA9u:                                       // D-Walker: search mode
        case 0xB0BF8EA0u: case 0xB0BF8EA3u: case 0xB0BF8EA2u:                     // D-Walker: intercept mode
        case 0x1D56D852u: case 0x1D56D854u: case 0x1D56D855u:                     // D-Walker: normal mode
        case 0xCBCE9E53u: case 0xCBCE9E4Eu: case 0xCBCE9E5Cu:                     // "DD."
        case 0xF02242B3u: case 0xF02242AEu: case 0xF02242BCu:                     // "Quiet."
            return true;
        default:
            return false;
        }
    }

    std::uint32_t EliLine(std::uint32_t line)   // vox_eli holds none of the player's lines: Eli's own take of each, 0 where he recorded none
    {
        if (IsBuddyAttackOrder(line))
            return kEliKikongo;   // the script has him speak Kikongo to his own unit
        if (line == kGoodDog || line == kGoodBoy || line == 0x0B694A5Fu)   // the last is the lowered "Gooood, gooood."
            return kEliEvadeLaughs;
        if (IsBuddyOrder(line))
            return kEliHey;
        switch (line)
        {
        case kCqcStart: case kHorseDung:
        case kPunch: case kPunchAlt: case kKick:                    return kEliExhale;
        case kHorseTrot: case kHorseCanter: case kHorseGallop:      return kEliEvadeWhoa;
        case kThrow: case kThrowAlt: case kHipFireRoar:             return kEliBattleCry;
        case kThrowFail:                                            return kEliCurse;
        case kHoldUp: case kHoldUpAlt:                              return kEliEvadeLaughs;
        case kHurt: case kShieldBurst: case kShieldBlast:           return kEliHurtMild;
        case kHurtHeavy:                                            return kEliHurtMedium;
        case kShot: case kShotAlt:                                  return kEliHurtSevere;
        case kHurtBreath: case kHurtBreathHard:                     return kEliBreathless;
        case kPatOutFire: case kFireWontGoOut: case kHoldDogOff:
        case kShock: case kShockLong:                               return kEliStruggle;
        case kBlasted: case kBurningDeath: case kDeathLine:         return kEliBlastDeath;
        case kChoked: case kDrownDeath: case kChokedDead:           return kEliDeath;
        case kFallDeathLine: case kFultonScream:                    return kEliFalling;
        case kCough:                                                return kEliCough;
        case kTranqDownAlt:                                         return kEliTranquilized;
        case kTranqDown: case kChokedOut:                           return kEliFallAsleep;
        case kSnore: case kSleep: case kSleepSnake:                 return kEliSleep;
        case kWakeBreath: case kWakeMoan: case kWakeMoanAlt:        return kEliWake;
        default:                                                    return 0;
        }
    }

    std::uint32_t EliTake(std::uint8_t* player, std::uint32_t playerId, std::uint32_t line)   // what he says for this line right now, 0 = nothing
    {
        const std::uint64_t now = GetTickCount64();
        std::lock_guard<std::mutex> lock(g_DogMutex);
        if ((line == kPunch || line == kPunchAlt || line == kCqcStart) && now - g_EvadeMs.load(std::memory_order_relaxed) < kEvadeLatelyMs)
        {
            if (now - g_EliEvadeMs < kEvadeOnceMs)
                return 0;
            g_EliEvadeMs = now;
            return g_ResolveTarget ? kEliEvadeWhoa : kEliEvade;
        }
        if ((line == kPunch || line == kPunchAlt || line == kKick)
            && now - g_CqcCommandMs.load(std::memory_order_relaxed) < kCqcLatelyMs)
            return kEliAttackShout;   // the same effort lines come from climbing and hanging, where he only exhales

        const bool hit = line == kHurt || line == kHurtHeavy || line == kShot || line == kShotAlt;
        if (line == kCough || (hit && now - g_SmokeHitMs.load(std::memory_order_relaxed) < kSmokeHitLatelyMs))
        {
            if (now - g_EliCoughMs < kCoughTakeMs)
                return 0;   // the game asks for the cough and a hit grunt in one frame, and only the later one would play
            g_EliCoughMs = now;
            return kEliCough;
        }
        if (hit && now - g_StunHitMs.load(std::memory_order_relaxed) < kStunHitLatelyMs)
            return kEliSurprise;
        if (line == kShot || line == kShotAlt)
            return Pick({ kEliHurtSevere, kEliHurtMild });   // he recorded one take of each: a burst of hits would repeat a single sound
        if (line == kShock || line == kShockLong)
        {
            if (now - g_EliShockMs < kShockTakeMs)
                return 0;
            g_EliShockMs = now;
            return Pick({ kEliRestrained, kEliStruggle });
        }
        if (line == kCurePain || line == kCurePainAlt)
        {
            if (now - g_EliCureMs < kCureScreamMs)
                return 0;
            g_EliCureMs = now;
            return kEliFalling;
        }
        if (line != kPant && line != kHurtBreath && line != kHurtBreathHard && line != kSleep)
            return EliLine(line);

        if (line == kPant)   // asked every stride; he sprints without a sound
        {
            if (now - g_LastPantMs > kSprintGapMs)
                g_SprintStartMs = now;
            g_LastPantMs = now;
            g_BreathAsksSinceSprint = 0;
            if (now - g_SprintStartMs >= kLongSprintMs)
                g_CatchBreathAfterMs.store(now, std::memory_order_relaxed);
            return 0;
        }
        if (line == kHurtBreath && !t_OwnLine && g_BreathAsksSinceSprint < kBreathAsksPerSprint)
        {
            ++g_BreathAsksSinceSprint;
            return 0;   // the game's asks after a sprint of any length; a long one gets his breath from SayCatchBreathAfterSprint, low life keeps asking past these
        }
        const auto isPlaying = g_OrigIsPlaying ? g_OrigIsPlaying
                                               : reinterpret_cast<IsPlayingVoice_t>((*reinterpret_cast<void***>(player))[kIsPlayingSlot]);
        if (isPlaying(player, playerId))
            return 0;
        return line == kSleep ? kEliSleep : kEliBreathless;
    }

    bool HoldingGameLines()
    {
        return !t_OwnLine && GetTickCount64() < g_HoldGameLinesUntilMs.load(std::memory_order_relaxed)
            && !MissionCodeGuard::ShouldBypassHooks();
    }

    void __fastcall hk_CallVoice(std::uint8_t* player, std::uint32_t playerId, std::uint32_t voiceId, std::uint32_t priority)
    {
        if (HoldingGameLines())
        {
            LogDebug("FobSneakProbe: held player line 0x%08X priority %u", voiceId, priority);
            return;
        }
        if (player && !MissionCodeGuard::ShouldBypassHooks())
        {
            if (voiceId == kDeathLine && FeatureSwitch::IsOn(FeatureSwitch::Feature::PainAndDeathLines))
            {
                const std::uint8_t state = player[kPlayerState];
                const std::uint32_t voiceType = ReadVoiceType_SEH(player, playerId);
                const std::uint64_t sinceFire  = GetTickCount64() - g_FireDamageMs.load(std::memory_order_relaxed);
                const std::uint64_t sinceWater = GetTickCount64() - g_InWaterDamageMs.load(std::memory_order_relaxed);
                if (state & kFellToDeath)
                    voiceId = kFallDeathLine;
                else if (t_DamageAttack == kAtkInWater || sinceWater < kInWaterLatelyMs)
                    voiceId = kDrownDeath;
                else if ((IsFireAttack(t_DamageAttack) || sinceFire < kBurnedLatelyMs) && IsSnake(voiceType))
                    voiceId = kBurningDeath;
            }
            else if (const std::uint32_t said = PlayerLine(player, playerId, voiceId); said != voiceId)
            {
                voiceId = said;
            }
            if (ReadVoiceType_SEH(player, playerId) == kPlayerVoiceTypeEli)
            {
                const std::uint32_t asked = voiceId;
                voiceId = EliTake(player, playerId, voiceId);
                LogDebug("PlayerVoice: Eli says 0x%08X for player line 0x%08X%s", voiceId, asked,
                         GetTickCount64() - g_EvadeMs.load(std::memory_order_relaxed) < kEvadeLatelyMs ? " in a dive" : "");
                if (voiceId == 0)
                    return;
            }
        }
        if (playerId < kMaxPlayers)
            g_MinorLineMs[playerId].store(t_MinorLine ? GetTickCount64() : 0, std::memory_order_relaxed);
        g_Orig(player, playerId, voiceId, priority);
    }

    bool SayingMinorLine(std::uint32_t playerId)
    {
        if (playerId >= kMaxPlayers)
            return false;
        const std::uint64_t started = g_MinorLineMs[playerId].load(std::memory_order_relaxed);
        return started != 0 && GetTickCount64() - started < kMinorLineMs;
    }

    bool __fastcall hk_IsPlayingVoice(std::uint8_t* player, std::uint32_t playerId)   // a minor line doesn't count, so whatever asks may cut it off
    {
        return g_OrigIsPlaying(player, playerId) && (MissionCodeGuard::ShouldBypassHooks() || !SayingMinorLine(playerId));
    }

    std::uint64_t __fastcall hk_CallVoiceWithEvent(std::uint8_t* player, std::uint32_t playerId, std::uint32_t dialogueEvent,
                                                   std::uint32_t voiceId, std::uint32_t priority)
    {
        if (HoldingGameLines())
        {
            LogDebug("FobSneakProbe: held player line 0x%08X event 0x%08X priority %u", voiceId, dialogueEvent, priority);
            return 0;
        }
        if (player && g_Orig && dialogueEvent == kBreathEvent && !MissionCodeGuard::ShouldBypassHooks()
            && ReadVoiceType_SEH(player, playerId) == kPlayerVoiceTypeEli)
        {
            const std::uint32_t said = EliTake(player, playerId, voiceId);
            LogDebug("PlayerVoice: Eli says 0x%08X for breath line 0x%08X", said, voiceId);
            if (said != 0)
                g_Orig(player, playerId, said, priority);
            return 0;
        }
        return g_OrigWithEvent(player, playerId, dialogueEvent, voiceId, priority);
    }

    void __fastcall hk_StateEvade(void* plugin, std::uint32_t playerIndex, std::uint32_t process, void* argument)
    {
        g_EvadeMs.store(GetTickCount64(), std::memory_order_relaxed);
        g_OrigStateEvade(plugin, playerIndex, process, argument);
    }

    void InstallStateEvade()
    {
        void* target = ResolveGameAddress(gAddr.EvadeActionPluginImpl_StateEvade);
        if (!target)
        {
            LogWarn("PlayerVoice: EvadeActionPluginImpl::StateEvade unknown for %s, Liquid's dive keeps the plain effort line",
                    GetGameBuildName(gGameBuild));
            return;
        }
        const HookEntry state = CheckHookEntry(target, kStateEvadeEntry, sizeof(kStateEvadeEntry));
        if (state == HookEntry::Unknown)
        {
            char entry[160] = {};
            DescribeFunctionEntry(target, entry, sizeof(entry));
            LogError("PlayerVoice: EvadeActionPluginImpl::StateEvade differs on %s (%s), Liquid's dive keeps the plain effort line",
                     GetGameBuildName(gGameBuild), entry);
            return;
        }
        if (state == HookEntry::ForeignHook)
            LogHookedOnTop("PlayerVoice: EvadeActionPluginImpl::StateEvade", target);
        if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_StateEvade), reinterpret_cast<void**>(&g_OrigStateEvade)))
        {
            LogError("PlayerVoice: EvadeActionPluginImpl::StateEvade hook failed at %p, Liquid's dive keeps the plain effort line", target);
            g_OrigStateEvade = nullptr;
            return;
        }
        g_StateEvadeTarget = target;
    }

    std::uint32_t __fastcall hk_ResolveDialogueEvent(std::uint32_t dialogueEvent, const std::uint32_t* arguments, std::uint32_t count,
                                                     std::uint32_t sequence)
    {
        if (dialogueEvent != kEliVoiceEvent || count != 2 || !arguments
            || (arguments[1] != kEliEvadeLaughs && arguments[1] != kEliEvadeWhoa))
            return g_OrigResolve(dialogueEvent, arguments, count, sequence);

        const std::uint32_t wholeLine[2] = { arguments[0], kEliEvade };
        if (g_OrigResolve(dialogueEvent, wholeLine, count, sequence) == 0)
            return 0;   // his bank isn't loaded
        if (arguments[1] == kEliEvadeWhoa)
            return kEliEvadeTake1;   // the game plays whatever sound this names
        return g_EliLaughTurn.fetch_add(1, std::memory_order_relaxed) % 2 == 0 ? kEliEvadeTake2 : kEliEvadeTake3;
    }

    void InstallResolveDialogueEvent()
    {
        void* target = ResolveGameAddress(gAddr.AK_ResolveDialogueEvent);
        if (!target)
        {
            LogWarn("PlayerVoice: AK ResolveDialogueEvent unknown for %s, Liquid's hold-up stays silent", GetGameBuildName(gGameBuild));
            return;
        }
        const HookEntry state = CheckHookEntry(target, kResolveDialogueEventEntry, sizeof(kResolveDialogueEventEntry));
        if (state == HookEntry::Unknown)
        {
            char entry[160] = {};
            DescribeFunctionEntry(target, entry, sizeof(entry));
            LogError("PlayerVoice: AK ResolveDialogueEvent differs on %s (%s), Liquid's hold-up stays silent", GetGameBuildName(gGameBuild), entry);
            return;
        }
        if (state == HookEntry::ForeignHook)
            LogHookedOnTop("PlayerVoice: AK ResolveDialogueEvent", target);
        if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_ResolveDialogueEvent), reinterpret_cast<void**>(&g_OrigResolve)))
        {
            LogError("PlayerVoice: AK ResolveDialogueEvent hook failed at %p, Liquid's hold-up stays silent", target);
            g_OrigResolve = nullptr;
            return;
        }
        g_ResolveTarget = target;
    }

    void InstallCallVoiceWithEvent()
    {
        void* target = ResolveGameAddress(gAddr.Player2Impl_CallVoiceWithDialogueEvent);
        const HookEntry state = CheckHookEntry(target, kCallVoiceWithEventEntry, sizeof(kCallVoiceWithEventEntry));
        if (state == HookEntry::Unknown)
        {
            char entry[160] = {};
            DescribeFunctionEntry(target, entry, sizeof(entry));
            LogError("PlayerVoice: Player2Impl::CallVoiceWithDialogueEvent differs on %s (%s), its lines can talk over a retaliation line",
                     GetGameBuildName(gGameBuild), entry);
            return;
        }
        if (state == HookEntry::ForeignHook)
            LogHookedOnTop("PlayerVoice: Player2Impl::CallVoiceWithDialogueEvent", target);
        if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_CallVoiceWithEvent), reinterpret_cast<void**>(&g_OrigWithEvent)))
        {
            LogError("PlayerVoice: Player2Impl::CallVoiceWithDialogueEvent hook failed at %p, its lines can talk over a retaliation line", target);
            g_OrigWithEvent = nullptr;
            return;
        }
        g_WithEventTarget = target;
    }

    bool FindLocalPlayer_SEH(std::uint8_t** player, std::uint32_t* playerId)
    {
        auto* system = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.Player2System));
        if (!system)
            return false;
        __try
        {
            auto* gameObject = *reinterpret_cast<std::uint8_t**>(system + kSystemPlayer);
            if (!gameObject)
                return false;
            auto* impl = *reinterpret_cast<std::uint8_t**>(gameObject + kPlayerImpl);
            if (!impl || !*reinterpret_cast<void**>(impl))
                return false;
            auto* owner = *reinterpret_cast<std::uint8_t**>(impl + kImplOwner);
            if (!owner)
                return false;
            *player   = impl;
            *playerId = *reinterpret_cast<std::uint32_t*>(owner + kOwnerPlayerId);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
}

bool SayPlayerLine(std::uint32_t line)
{
    if (!gAddr.Player2Impl_CallVoice || MissionCodeGuard::ShouldBypassHooks())
        return false;
    std::uint8_t* player   = nullptr;
    std::uint32_t playerId = 0;
    if (!FindLocalPlayer_SEH(&player, &playerId))
        return false;
    const auto callVoice = reinterpret_cast<CallVoice_t>((*reinterpret_cast<void***>(player))[kCallVoiceSlot]);
    t_OwnLine = true;
    callVoice(player, playerId, line, kVoicePriority);
    t_OwnLine = false;
    return true;
}

bool SayEitherPlayerLine(std::uint32_t first, std::uint32_t second)
{
    std::uint32_t line = 0;
    {
        std::lock_guard<std::mutex> lock(g_DogMutex);
        line = Pick({ first, second });
    }
    return SayPlayerLine(line);
}

void HoldGamePlayerLines(std::uint32_t ms)
{
    g_HoldGameLinesUntilMs.store(GetTickCount64() + ms, std::memory_order_relaxed);
}

bool SayMinorPlayerLine(std::uint32_t line)
{
    if (!gAddr.Player2Impl_CallVoice || MissionCodeGuard::ShouldBypassHooks())
        return false;
    std::uint8_t* player   = nullptr;
    std::uint32_t playerId = 0;
    if (!FindLocalPlayer_SEH(&player, &playerId))
        return false;
    void** vtable = *reinterpret_cast<void***>(player);
    const auto isPlaying = g_OrigIsPlaying ? g_OrigIsPlaying : reinterpret_cast<IsPlayingVoice_t>(vtable[kIsPlayingSlot]);
    if (isPlaying(player, playerId))
        return false;
    t_MinorLine = true;
    t_OwnLine   = true;
    reinterpret_cast<CallVoice_t>(vtable[kCallVoiceSlot])(player, playerId, line, kMinorPriority);
    t_OwnLine   = false;
    t_MinorLine = false;
    return true;
}

bool SayEitherMinorPlayerLine(std::uint32_t first, std::uint32_t second)
{
    std::uint32_t line = 0;
    {
        std::lock_guard<std::mutex> lock(g_DogMutex);
        line = Pick({ first, second });
    }
    return SayMinorPlayerLine(line);
}

void SayHorseGaitLine(std::uint8_t state)
{
    if (!FeatureSwitch::IsOn(FeatureSwitch::Feature::BuddyLines))
        return;
    std::uint32_t line = 0;
    {
        std::lock_guard<std::mutex> lock(g_DogMutex);
        switch (state)
        {
        case 13: line = Pick({ kHorseWalk, kHorseTrot }); break;   // RideOnIdleToTrot
        case 14: line = kHorseCanter; break;                       // RideOnIdleToCanter
        default: return;   // every other gait change already says its line through a voice event in player2_horse.mtar
        }
        const std::uint64_t now = GetTickCount64();
        if (now - g_LastHorseLineMs < kHorseGapMs)
            return;
        g_LastHorseLineMs = now;
    }
    SayMinorPlayerLine(line);
}

void SayHorsePatLine()
{
    {
        std::lock_guard<std::mutex> lock(g_DogMutex);
        const std::uint64_t now = GetTickCount64();
        if (now - g_LastHorsePatMs < kHorsePatGapMs)
            return;
        g_LastHorsePatMs = now;
    }
    SayMinorPlayerLine(kHorsePat);
}

void SayHorseDungLine()
{
    std::uint8_t* player   = nullptr;
    std::uint32_t playerId = 0;
    if (!FeatureSwitch::IsOn(FeatureSwitch::Feature::BuddyLines) || !FindLocalPlayer_SEH(&player, &playerId)
        || !HasDogLines(ReadVoiceType_SEH(player, playerId)))
        return;   // Ocelot never recorded it
    const bool said = SayMinorPlayerLine(kHorseStink);
}

std::uint16_t SwapDamageAttack(std::uint16_t attackId)
{
    const std::uint16_t previous = t_DamageAttack;
    t_DamageAttack = attackId;
    std::uint16_t lethalDamage = 0;
    std::uint16_t flags        = 0;
    if (attackId != 0 && EquipParam_GetDamageKind(attackId, lethalDamage, flags))
    {
        if (flags & kDamageFlagStun)
            g_StunHitMs.store(GetTickCount64(), std::memory_order_relaxed);
        if ((flags & kDamageFlagGas) && !(flags & kDamageFlagTranq))
            g_SmokeHitMs.store(GetTickCount64(), std::memory_order_relaxed);
    }
    if (attackId == kAtkInWater)
        g_InWaterDamageMs.store(GetTickCount64(), std::memory_order_relaxed);
    if (IsFireAttack(attackId) && FeatureSwitch::IsOn(FeatureSwitch::Feature::PainAndDeathLines))
    {
        if (const std::uint32_t line = BurnLine())
        {
            SayPlayerLine(line);
        }
    }
    return previous;
}

void Player2Impl_CallVoice_ClearForReload()
{
    {
        std::lock_guard<std::mutex> lock(g_DogMutex);
        g_LastOrder        = DogOrder::None;
        g_LastOrderMs      = 0;
        g_LastHorseLineMs  = 0;
        g_LastHorsePatMs   = 0;
        g_BurnStartMs      = 0;
        g_SaidStillBurning = false;
    }
    g_FireDamageMs.store(0, std::memory_order_relaxed);
    g_InWaterDamageMs.store(0, std::memory_order_relaxed);
    g_CatchBreathAfterMs.store(0, std::memory_order_relaxed);
}

void NotePlayerCqcCommand()
{
    g_CqcCommandMs.store(GetTickCount64(), std::memory_order_relaxed);
}

void SayCatchBreathAfterSprint()
{
    const std::uint64_t lastStride = g_CatchBreathAfterMs.load(std::memory_order_relaxed);
    if (lastStride == 0 || GetTickCount64() - lastStride < kSprintEndMs)
        return;
    g_CatchBreathAfterMs.store(0, std::memory_order_relaxed);
    SayMinorPlayerLine(kHurtBreath);   // only Eli's sprint sets the time, and his voice answers this line with his long breathing take
}

bool Install_Player2Impl_CallVoice_Hook()
{
    if (g_HookTarget)
        return true;

    const std::uintptr_t address = gAddr.Player2Impl_CallVoice;
    if (!address)
    {
        LogWarn("FallDeathVoice: Player2Impl::CallVoice unknown for %s, the player keeps the vanilla lines",
                GetGameBuildName(gGameBuild));
        return true;
    }

    void* target = ResolveGameAddress(address);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_CallVoice), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("FallDeathVoice: Player2Impl::CallVoice hook failed at %p, the player keeps the vanilla lines", target);
        return false;
    }
    g_HookTarget = target;
    InstallCallVoiceWithEvent();
    InstallStateEvade();
    InstallResolveDialogueEvent();

    if (!gAddr.Player2Impl_IsPlayingVoice)
    {
        LogWarn("PlayerVoice: Player2Impl::IsPlayingVoice unknown for %s, his minor lines still hold off the hit grunt",
                GetGameBuildName(gGameBuild));
        return true;
    }
    void* isPlaying = ResolveGameAddress(gAddr.Player2Impl_IsPlayingVoice);
    const HookEntry state = CheckHookEntry(isPlaying, kIsPlayingVoiceEntry, sizeof(kIsPlayingVoiceEntry));
    if (state == HookEntry::Unknown)
    {
        LogError("PlayerVoice: Player2Impl::IsPlayingVoice differs on %s, his minor lines still hold off the hit grunt", GetGameBuildName(gGameBuild));
        return true;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("PlayerVoice: Player2Impl::IsPlayingVoice", isPlaying);
    if (!CreateAndEnableHook(isPlaying, reinterpret_cast<void*>(&hk_IsPlayingVoice), reinterpret_cast<void**>(&g_OrigIsPlaying)))
    {
        LogError("PlayerVoice: Player2Impl::IsPlayingVoice hook failed at %p, his minor lines still hold off the hit grunt", isPlaying);
        g_OrigIsPlaying = nullptr;
        return true;
    }
    g_IsPlayingTarget = isPlaying;
    return true;
}

void Uninstall_Player2Impl_CallVoice_Hook()
{
    if (g_ResolveTarget)
    {
        DisableAndRemoveHook(g_ResolveTarget);
        g_ResolveTarget = nullptr;
        g_OrigResolve   = nullptr;
    }
    if (g_StateEvadeTarget)
    {
        DisableAndRemoveHook(g_StateEvadeTarget);
        g_StateEvadeTarget = nullptr;
        g_OrigStateEvade   = nullptr;
    }
    if (g_WithEventTarget)
    {
        DisableAndRemoveHook(g_WithEventTarget);
        g_WithEventTarget = nullptr;
    }
    if (g_IsPlayingTarget)
    {
        DisableAndRemoveHook(g_IsPlayingTarget);
        g_IsPlayingTarget = nullptr;
        g_OrigIsPlaying   = nullptr;
    }
    if (g_HookTarget)
    {
        DisableAndRemoveHook(g_HookTarget);
        g_HookTarget = nullptr;
        g_Orig       = nullptr;
    }
}
