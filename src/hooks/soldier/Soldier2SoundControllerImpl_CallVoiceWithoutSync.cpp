#include "pch.h"

#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>

#include "AddressSet.h"
#include "FoxHashes.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "NoticeControllerImpl_CheckSightNoticePlayer.h"
#include "RadioActionImpl_State_RadioChaffEnd.h"
#include "RadioControllerImpl_StepRadioWaitResponse.h"
#include "Soldier2SoundControllerImpl_CallVoiceWithoutSync.h"
#include "SoldierCombatVoice.h"
#include "SoldierRealizedSlot.h"
#include "SoldierVoiceType.h"
#include "log.h"

namespace
{
    using CallVoiceWithoutSync_t = bool(__fastcall*)(void* self, std::uint32_t slot, std::uint32_t dialogueEvent,
                                                     std::uint32_t priority, std::uint32_t line, bool flag, float delay);
    using Speak_t                = bool(__fastcall*)(void* soundController, std::uint32_t slot, std::uint32_t dialogueEvent,
                                                     int priority, std::uint32_t line, float delay);

    constexpr std::uint32_t kEnemyVoiceEvent         = 0x95EA16B0u;  // DD_vox_ene
    constexpr std::uint32_t kEnemyStateVoiceEvent    = 0xE3BD7BDDu;  // the other enemy dialogue event, breaths and groans
    constexpr std::uint32_t kSurprisedFultonLine     = 0x08C903DDu;  // EVR201
    constexpr std::uint32_t kAngryFultonLine         = 0x08C903DCu;  // EVR200
    constexpr std::uint32_t kWakeVipLine             = 0x9CD0A89Cu;  // EVR121 "Sir, wake up!"
    constexpr std::uint32_t kWakeVipComradeLine      = 0x9CD0A89Fu;  // EVR122, the "comrade" take, recorded for ene_d_ru and ene_d_af only
    constexpr std::uint32_t kMockHeldComradeLine     = 0x92D098DFu;  // EVR180 "The hell you doing?", non-VIP; VIPs get EVR181
    constexpr std::uint32_t kReassureHeldComradeLine = 0x97D0A0FEu;  // EVR170 "It's OK, he's gone", adult voices only
    constexpr std::uint32_t kStunBlastLine           = 0xA22FC28Eu;  // EVD060
    constexpr std::uint32_t kCpRadioEvent            = 0x6B52E59Fu;  // DD_vox_cp_radio
    constexpr std::uint32_t kCallOutTake             = 0x44BAA4D0u;                    // CPI0100_03 "CP, answer! CP!"
    constexpr std::uint32_t kGiveUpTakes[]           = { 0x44BAA4D7u, 0x44BAA4D6u };   // CPI0100_04, _05, right after it (user)
    constexpr std::uint32_t kGiveUpTakeCount         = 2;
    constexpr std::uint32_t kCantHearTakes[]         = { 0x44BAA4D3u, 0x44BAA4D2u, 0x44BAA4D1u };   // CPI0100_00..02 "CP! Repeat that last!", "CP! You're breaking up!"
    constexpr std::uint32_t kJammedTakeCount         = 3;
    constexpr std::uint64_t kJammedTakeAfterMs       = 1500;   // he tries the radio first and gets static
    constexpr std::uint64_t kQuietBetweenTakesMs     = 500;
    constexpr std::uint64_t kShoutAfterJamMs         = 4000;
    constexpr int           kJammedTakePriority      = 3;
    constexpr std::uint64_t kQuietBeforeTakeMs       = 3000;   // after his "...Over!" (user)
    constexpr std::uint64_t kQuietBeforeShoutMs      = 800;
    constexpr float         kNoDelay                 = 0.0f;
    constexpr int           kStunBlastPriority       = 4;
    constexpr float         kStunBlastDelay          = 0.0f;
    constexpr std::uint64_t kStunGruntWindowMs       = 1500;
    constexpr int           kDamageGruntCount        = 300;
    constexpr std::size_t   kControllerSoldiers      = 0x58;
    constexpr std::size_t   kSoundSoldiers           = 0x88;
    constexpr std::size_t   kDamageHandlerSoldiers   = 0x48;
    constexpr std::size_t   kDamageStride            = 0x30;
    constexpr std::size_t   kDamageAttackId          = 0x28;
    constexpr std::size_t   kSoldiersSoundController = 0xA8;
    constexpr std::size_t   kSpeakSlot               = 0x20 / sizeof(void*);
    constexpr std::size_t   kSoldierLife             = 0x38;  // 0x40 per soldier at +8
    constexpr std::size_t   kLifeRecords             = 0x08;
    constexpr std::size_t   kLifeRecordSize          = 0x40;
    constexpr std::size_t   kLifeFlags               = 0x12;
    constexpr std::uint16_t kLifeFlagsStatus         = 0x3;   // 0 normal, 1 sleep, 2 faint, 3 dying
    constexpr std::size_t   kLifeState               = 0x3E;
    constexpr std::uint8_t  kLifeStateDead           = 2;
    constexpr std::uint32_t kSoldierSlotCount        = 0x200;
    constexpr std::uint64_t kHeldGraceMs             = 4000;
    constexpr std::uint64_t kStillHeld               = ~0ull;
    constexpr std::uint64_t kOneTakePerJamMs         = 10000;   // the held call and the jam motion after it both ask

    CallVoiceWithoutSync_t g_OrigCallVoice = nullptr;
    std::atomic<std::uint32_t> g_LineRoll{ 0x2545F491u };

    std::array<std::atomic<std::uint64_t>, kSoldierSlotCount> g_HeldUntilMs{};
    std::array<std::atomic<std::uint64_t>, kSoldierSlotCount> g_StunGruntsUntilMs{};  // by realized slot
    std::array<std::atomic<std::uint64_t>, kSoldierSlotCount> g_JammedTakeMs{};

    bool FirstTakeOfJam(std::uint32_t soldierIndex)
    {
        const std::uint64_t now = GetTickCount64();
        std::uint64_t last = g_JammedTakeMs[soldierIndex].load(std::memory_order_relaxed);
        while (!last || now - last > kOneTakePerJamMs)
        {
            if (g_JammedTakeMs[soldierIndex].compare_exchange_weak(last, now, std::memory_order_relaxed))
                return true;
        }
        return false;
    }

    bool WasHeldJustNow(std::uint32_t soldierIndex)
    {
        return soldierIndex < kSoldierSlotCount
            && GetTickCount64() <= g_HeldUntilMs[soldierIndex].load(std::memory_order_relaxed);
    }

    const std::uint8_t* PointerAt(const void* base, std::size_t offset)
    {
        __try
        {
            return *reinterpret_cast<const std::uint8_t* const*>(static_cast<const std::uint8_t*>(base) + offset);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    bool IsConscious(const std::uint8_t* soldiers, std::uint32_t soldierIndex)
    {
        __try
        {
            const auto* lifeBlock = *reinterpret_cast<const std::uint8_t* const*>(soldiers + kSoldierLife);
            const auto* life = *reinterpret_cast<const std::uint8_t* const*>(lifeBlock + kLifeRecords)
                + soldierIndex * kLifeRecordSize;
            return life[kLifeState] != kLifeStateDead
                && (*reinterpret_cast<const std::uint16_t*>(life + kLifeFlags) & kLifeFlagsStatus) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool IsStunGrenade(std::uint16_t attackId)
    {
        switch (attackId)
        {
        case 0xB6: case 0xB7: case 0xB8:                        // ATK_StunGrenade, _G1, _G2
        case 0xB9: case 0xBA: case 0xBB:                        // ATK_StunGrenadeGrazed, _G1, _G2
        case 0x1C4: case 0x1C5: case 0x1CC: case 0x1CD:         // ATK_StunGrenade_G3, Grazed_G3, _G4, Grazed_G4
        case 0x1CE: case 0x1CF: case 0x1D0: case 0x1D1:         // ATK_StunGrenade_G5, Grazed_G5, _G6, Grazed_G6
            return true;
        default:
            return false;
        }
    }

    bool HitByStunGrenade(const void* damages, std::uint32_t count)
    {
        __try
        {
            const auto* damage = static_cast<const std::uint8_t*>(damages);
            for (std::uint32_t i = 0; i < count; ++i, damage += kDamageStride)
            {
                if (IsStunGrenade(*reinterpret_cast<const std::uint16_t*>(damage + kDamageAttackId)))
                    return true;
            }
            return false;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool HasStunBlastRecording(std::uint32_t voiceType)   // EVD060 has no Soviet or PF recordings
    {
        switch (voiceType)
        {
        case SoldierVoiceType::kEneA:    case SoldierVoiceType::kEneB:    case SoldierVoiceType::kEneC:    case SoldierVoiceType::kEneD:
        case SoldierVoiceType::kEneAFml: case SoldierVoiceType::kEneBFml: case SoldierVoiceType::kEneCFml: case SoldierVoiceType::kEneDFml:
        case SoldierVoiceType::kChildA:  case SoldierVoiceType::kChildB:  case SoldierVoiceType::kChildC:  case SoldierVoiceType::kChildD:
            return true;
        default:
            return false;
        }
    }

    bool IsDamageGrunt(std::uint32_t line)
    {
        static const std::array<std::uint32_t, kDamageGruntCount> grunts = [] {
            std::array<std::uint32_t, kDamageGruntCount> hashes{};
            char name[8] = {};
            for (int number = 0; number < kDamageGruntCount; ++number)
            {
                std::snprintf(name, sizeof(name), "evd%03d", number);
                hashes[number] = FoxHashes::FNVHash32(name);
            }
            return hashes;
        }();
        return std::find(grunts.begin(), grunts.end(), line) != grunts.end();
    }

    bool Speak(const std::uint8_t* soldiers, std::uint32_t slot, std::uint32_t line)
    {
        __try
        {
            void* soundController = *reinterpret_cast<void* const*>(soldiers + kSoldiersSoundController);
            const auto speak = (*reinterpret_cast<Speak_t* const*>(soundController))[kSpeakSlot];
            return speak(soundController, slot, kEnemyVoiceEvent, kStunBlastPriority, line, kStunBlastDelay);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint32_t VoiceTypeInSlot(void* soundController, std::uint32_t slot)
    {
        const std::uint8_t* soldiers = PointerAt(soundController, kSoundSoldiers);
        const std::uint32_t soldierIndex = SoldierRealizedSlot::SoldierIn(soldiers, slot);
        return soldierIndex != SoldierRealizedSlot::kNotRealized
            ? SoldierVoiceType::Of(soldiers, soldierIndex) : SoldierVoiceType::kUnknown;
    }

    std::uint32_t NextRoll()
    {
        std::uint32_t roll = g_LineRoll.load(std::memory_order_relaxed);
        roll ^= roll << 13;
        roll ^= roll >> 7;
        roll ^= roll << 5;
        g_LineRoll.store(roll, std::memory_order_relaxed);
        return roll;
    }

    bool CoinFlip()
    {
        return (NextRoll() & 1) != 0;
    }

    bool InStunGruntWindow(std::uint32_t slot)
    {
        return slot < kSoldierSlotCount
            && GetTickCount64() < g_StunGruntsUntilMs[slot].load(std::memory_order_relaxed);
    }

    std::uint32_t JammedTakeSpeaker(const std::uint8_t* soldiers, std::uint32_t slot)   // the takes are shouted over gunfire: alert only
    {
        const std::uint32_t soldierIndex = SoldierRealizedSlot::SoldierIn(soldiers, slot);
        if (soldierIndex == SoldierRealizedSlot::kNotRealized
            || !SoldierVoiceType::IsAdult(SoldierVoiceType::Of(soldiers, soldierIndex))
            || !SoldierCombatVoice_InAlert(soldiers, soldierIndex))
            return SoldierRealizedSlot::kNotRealized;
        return soldierIndex;
    }

    std::uint32_t SayJammedTakes(std::uint32_t soldierIndex)   // he only says he can't hear the CP if the CP was talking; the first take, for the log
    {
        if (CpRadioResponse::CpOpensCallTo(static_cast<std::uint16_t>(soldierIndex)))
        {
            const std::uint32_t take = kCantHearTakes[NextRoll() % kJammedTakeCount];
            SoldierCombatVoice_SayLater(soldierIndex, kCpRadioEvent, take, kJammedTakePriority, kJammedTakeAfterMs, kQuietBeforeTakeMs);
            return take;
        }
        const std::uint32_t giveUp = kGiveUpTakes[NextRoll() % kGiveUpTakeCount];
        SoldierCombatVoice_SayLater(soldierIndex, kCpRadioEvent, kCallOutTake, kJammedTakePriority, kJammedTakeAfterMs, kQuietBeforeTakeMs);
        SoldierCombatVoice_SayLater(soldierIndex, kCpRadioEvent, giveUp, kJammedTakePriority, kJammedTakeAfterMs + 1, kQuietBetweenTakesMs);   // queued behind the first
        return kCallOutTake;
    }

    bool SayJammedRadioFirst(void* self, std::uint32_t slot, std::uint32_t priority, std::uint32_t line, bool flag, float delay)
    {
        const std::uint8_t* soldiers = PointerAt(self, kSoundSoldiers);
        const std::uint32_t soldierIndex = JammedTakeSpeaker(soldiers, slot);
        if (soldierIndex == SoldierRealizedSlot::kNotRealized)
            return g_OrigCallVoice(self, slot, kEnemyVoiceEvent, priority, line, flag, delay);

        const std::uint32_t take = FirstTakeOfJam(soldierIndex) ? SayJammedTakes(soldierIndex) : 0;
        SoldierCombatVoice_SayLater(soldierIndex, kEnemyVoiceEvent, line, static_cast<int>(priority), kShoutAfterJamMs, kQuietBeforeShoutMs);
        LogInfo("VoiceDebug: chaff jams soldier %u, radio take 0x%X in %llu ms, shout 0x%X in %llu ms", soldierIndex, take,
            kJammedTakeAfterMs, line, kShoutAfterJamMs);
        return true;
    }

    std::atomic<std::uint32_t> g_WatchedSlot{ 0xFFFFFFFFu };
    std::atomic<std::uint64_t> g_WatchUntilMs{ 0 };

    bool __fastcall hk_CallVoiceWithoutSync(void* self, std::uint32_t slot, std::uint32_t dialogueEvent,
                                            std::uint32_t priority, std::uint32_t line, bool flag, float delay)
    {
        if (line != kStunBlastLine && InStunGruntWindow(slot)
            && (dialogueEvent == kEnemyStateVoiceEvent || (dialogueEvent == kEnemyVoiceEvent && IsDamageGrunt(line))))
        {
            LogInfo("VoiceDebug: stun window dropped slot %u event 0x%X line 0x%X", slot, dialogueEvent, line);
            return false;
        }

        if (dialogueEvent == kEnemyVoiceEvent && RadioChaffEnd_IsShoutingInstead())
        {
            RadioChaffEnd_NoteShout();
            return SayJammedRadioFirst(self, slot, priority, line, flag, delay);
        }

        if (line == kWakeVipLine && dialogueEvent == kEnemyVoiceEvent && !MissionCodeGuard::ShouldBypassHooks())
        {
            const std::uint32_t voiceType = VoiceTypeInSlot(self, slot);
            if (voiceType == SoldierVoiceType::kEneDRu || voiceType == SoldierVoiceType::kEneDAf)
                line = kWakeVipComradeLine;
            LogInfo("VoiceDebug: wake VIP slot %u voice 0x%X -> %s", slot, voiceType,
                line == kWakeVipComradeLine ? "EVR122" : "EVR121");
        }

        if (line == kMockHeldComradeLine && dialogueEvent == kEnemyVoiceEvent && !MissionCodeGuard::ShouldBypassHooks())
        {
            const std::uint32_t voiceType = VoiceTypeInSlot(self, slot);
            if (SoldierVoiceType::IsAdult(voiceType) && CoinFlip())
                line = kReassureHeldComradeLine;
            LogInfo("VoiceDebug: held comrade slot %u voice 0x%X -> %s", slot, voiceType,
                line == kReassureHeldComradeLine ? "EVR170" : "EVR180");
        }

        const std::uint8_t* soundSoldiers = PointerAt(self, kSoundSoldiers);
        line = SoldierCombatVoice_SwapLine(soundSoldiers, slot, dialogueEvent, line);

        if (line == kSurprisedFultonLine && dialogueEvent == kEnemyVoiceEvent && !MissionCodeGuard::ShouldBypassHooks())
        {
            void* noticeController = SoldierNotice_LiveController();
            const std::uint8_t* soldiers = noticeController ? PointerAt(noticeController, kControllerSoldiers) : nullptr;
            const std::uint32_t soldierIndex = SoldierRealizedSlot::SoldierIn(soldiers, slot);
            const bool known = soldierIndex != SoldierRealizedSlot::kNotRealized;
            const bool heldJustNow = known && WasHeldJustNow(soldierIndex);
            const bool conscious = known && IsConscious(soldiers, soldierIndex);
            if (heldJustNow || conscious)
                line = kAngryFultonLine;
            const bool accepted = g_OrigCallVoice(self, slot, dialogueEvent, priority, line, flag, delay);
            LogInfo("VoiceDebug: Fulton slot %u soldier %d heldJustNow %d conscious %d -> %s accepted %d", slot,
                known ? static_cast<int>(soldierIndex) : -1, heldJustNow ? 1 : 0, conscious ? 1 : 0,
                line == kAngryFultonLine ? "EVR200" : "EVR201", accepted ? 1 : 0);
            g_WatchedSlot.store(slot, std::memory_order_relaxed);
            g_WatchUntilMs.store(GetTickCount64() + 3000, std::memory_order_relaxed);
            return accepted;
        }
        if (slot == g_WatchedSlot.load(std::memory_order_relaxed) && GetTickCount64() < g_WatchUntilMs.load(std::memory_order_relaxed))
            LogInfo("VoiceDebug: Fulton slot %u then says event 0x%X line 0x%X prio %u", slot, dialogueEvent, line, priority);
        const bool accepted = g_OrigCallVoice(self, slot, dialogueEvent, priority, line, flag, delay);
        if (accepted)
            SoldierCombatVoice_OnSaid(soundSoldiers, slot, dialogueEvent, line);
        return accepted;
    }
}

void SoldierVoiceSwap_OnHeld(std::uint16_t soldierIndex, bool held)
{
    if (soldierIndex >= kSoldierSlotCount)
        return;
    g_HeldUntilMs[soldierIndex].store(held ? kStillHeld : GetTickCount64() + kHeldGraceMs, std::memory_order_relaxed);
}

bool SoldierVoiceSwap_SayJammedTake(const std::uint8_t* soldiers, std::uint32_t slot)
{
    const std::uint32_t soldierIndex = soldiers ? JammedTakeSpeaker(soldiers, slot) : SoldierRealizedSlot::kNotRealized;
    if (soldierIndex == SoldierRealizedSlot::kNotRealized)
        return false;
    if (!FirstTakeOfJam(soldierIndex))
        return true;   // already asked for when his held call was jammed
    const std::uint32_t take = SayJammedTakes(soldierIndex);
    LogInfo("VoiceDebug: chaff jams soldier %u (no vanilla shout), radio take 0x%X in %llu ms", soldierIndex, take, kJammedTakeAfterMs);
    return true;
}

void SoldierVoiceSwap_OnSoldierDamages(void* damageHandler, std::uint32_t soldierIndex, const void* damages, std::uint32_t count)
{
    if (!damageHandler || !damages || soldierIndex >= kSoldierSlotCount || MissionCodeGuard::ShouldBypassHooks()
        || !HitByStunGrenade(damages, count))
        return;

    const std::uint8_t* soldiers = PointerAt(damageHandler, kDamageHandlerSoldiers);
    if (!soldiers || !IsConscious(soldiers, soldierIndex))
        return;

    const std::uint32_t voiceType = SoldierVoiceType::Of(soldiers, soldierIndex);
    if (!HasStunBlastRecording(voiceType))
    {
        LogInfo("VoiceDebug: stun soldier %u voice 0x%X has no EVD060, vanilla kept", soldierIndex, voiceType);
        return;
    }

    const std::uint32_t slot = SoldierRealizedSlot::Of(soldiers, soldierIndex);
    if (slot >= kSoldierSlotCount || InStunGruntWindow(slot))
        return;

    g_StunGruntsUntilMs[slot].store(GetTickCount64() + kStunGruntWindowMs, std::memory_order_relaxed);
    const bool spoke = Speak(soldiers, slot, kStunBlastLine);
    LogInfo("VoiceDebug: stun soldier %u slot %u voice 0x%X EVD060 speak %d", soldierIndex, slot, voiceType, spoke ? 1 : 0);
}

bool Install_Soldier2SoundControllerImpl_CallVoiceWithoutSync_Hook()
{
    void* target = ResolveGameAddress(gAddr.Soldier2SoundControllerImpl_CallVoiceWithoutSync);
    if (!target)
    {
        LogError("SoldierVoiceSwap: CallVoiceWithoutSync address missing on this build, EVR200/EVD060 swaps off");
        return false;
    }
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_CallVoiceWithoutSync),
                             reinterpret_cast<void**>(&g_OrigCallVoice)))
    {
        LogError("SoldierVoiceSwap: CallVoiceWithoutSync hook failed, EVR200/EVD060 swaps off");
        return false;
    }
    return true;
}

bool Uninstall_Soldier2SoundControllerImpl_CallVoiceWithoutSync_Hook()
{
    void* target = ResolveGameAddress(gAddr.Soldier2SoundControllerImpl_CallVoiceWithoutSync);
    if (target)
        DisableAndRemoveHook(target);
    g_OrigCallVoice = nullptr;
    for (auto& until : g_StunGruntsUntilMs)
        until.store(0, std::memory_order_relaxed);
    for (auto& take : g_JammedTakeMs)
        take.store(0, std::memory_order_relaxed);
    return true;
}
