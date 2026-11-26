#include <pch.h>
#include "BuddyQuiet2SoundControllerImpl_CallVoice.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>

#include "AddressSet.h"
#include "BuddyOutfitRegistry.h"
#include "FeatureSwitch.h"
#include "HookUtils.h"
#include "log.h"
#include "MissionCodeGuard.h"

namespace
{
    using CallVoice_t = bool(__fastcall*)(void* self, std::uint32_t slot, std::uint32_t dialogueEvent, std::uint32_t callType,
                                          std::uint32_t line, float noRepeatSeconds);

    const std::uint8_t kCallVoiceEntry[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x8B, 0x41, 0x68, 0xF3, 0x0F, 0x10, 0x4C, 0x24, 0x78, 0x0F, 0x57, 0xC0, 0x0F, 0x2F, 0xC8 };

    constexpr std::size_t   kVtblCallVoice   = 0x18;
    constexpr std::uint32_t kQuietVoiceEvent = 0xCFBC3928;   // her dialogue event in se_b_qui (real name unknown)
    constexpr std::uint32_t kCallType        = 1;            // every line of hers is called with 1 here

    constexpr std::uint32_t kShotHitLines[]    = { 0xBC8D7D4C, 0xBF8D8205, 0xBE8D8092 };   // QUIV_1110, QUIV_1120, QUIV_1130
    constexpr std::uint32_t kShotMissedLines[] = { 0xBB8D7BD9, 0xBA8D7A66, 0xC58D8B97 };   // QUIV_1160, QUIV_1170, QUIV_1180
    constexpr std::uint32_t kProudLines[]      = { 0x478609B8, 0x48860B2B, 0x49860C9E };   // QUIV_1240, QUIV_1250, QUIV_1260
    constexpr std::uint32_t kFedUpLines[]      = { 0x4C884FCE, 0x4B884E5B, 0x4A884CE8 };   // QUIV_1350, QUIV_1360, QUIV_1370
    constexpr std::uint32_t kAngryLines[]      = { 0x448604FF, 0x518857ED, 0x5088567A };   // QUIV_1290, QUIV_1300, QUIV_1310
    constexpr std::uint32_t kLaughLines[]      = { 0x4B860FE4, 0x4C861157, 0x4D8612CA };   // QUIV_1200, QUIV_1210, QUIV_1220
    constexpr std::uint32_t kSniperWolfLaugh   = 0x40818165;                               // QUIV_1410

    constexpr std::uint32_t kTakes          = 3;
    constexpr std::uint32_t kProudTurns     = 4;   // her three takes, then a laugh
    constexpr std::uint32_t kWolfProudTurns = 5;   // and the Sniper Wolf laugh after that

    constexpr std::size_t   kVarQuietBond      = 0x2DE4;   // u16, 0 to 1000
    constexpr std::uint16_t kCloseBond         = 500;
    constexpr std::uint16_t kTrustedBond       = 800;
    constexpr std::uint8_t  kSniperWolfCostume = 5;
    constexpr std::uint64_t kAngryEveryMs      = 5000;

    CallVoice_t g_Orig   = nullptr;
    void*       g_Target = nullptr;

    std::atomic<std::uint32_t> g_ShotHitTurn{ 0 };
    std::atomic<std::uint32_t> g_ShotMissedTurn{ 0 };
    std::atomic<std::uint32_t> g_ProudTurn{ 0 };
    std::atomic<std::uint32_t> g_FedUpTurn{ 0 };
    std::atomic<std::uint32_t> g_AngryTurn{ 0 };
    std::atomic<std::uint64_t> g_AngryMs{ 0 };

    std::uint32_t NextTake(const std::uint32_t (&lines)[kTakes], std::atomic<std::uint32_t>& turn)
    {
        return lines[turn.fetch_add(1, std::memory_order_relaxed) % kTakes];
    }

    std::uint32_t NextProudLine()
    {
        const std::uint8_t* vars = buddyoutfit::ReadScriptVars();
        const bool sniperWolf = vars && vars[buddyoutfit::kVarQuietCostume] == kSniperWolfCostume;
        const std::uint32_t turn = g_ProudTurn.fetch_add(1, std::memory_order_relaxed) % (sniperWolf ? kWolfProudTurns : kProudTurns);
        if (turn < kTakes)
            return kProudLines[turn];
        if (turn >= kProudTurns)
            return kSniperWolfLaugh;
        const std::uint16_t bond = vars ? *reinterpret_cast<const std::uint16_t*>(vars + kVarQuietBond) : 0;
        if (bond >= kTrustedBond)
            return kLaughLines[2];
        if (bond >= kCloseBond)
            return kLaughLines[1];
        return kLaughLines[0];
    }

    std::uint32_t RestoredTake(std::uint32_t line)
    {
        if (line == kShotHitLines[0])
            return NextTake(kShotHitLines, g_ShotHitTurn);
        if (line == kShotMissedLines[0])
            return NextTake(kShotMissedLines, g_ShotMissedTurn);
        if (line == kFedUpLines[0])
            return NextTake(kFedUpLines, g_FedUpTurn);
        if (line == kProudLines[0])
            return NextProudLine();
        return line;
    }

    bool __fastcall hkCallVoice(void* self, std::uint32_t slot, std::uint32_t dialogueEvent, std::uint32_t callType,
                                std::uint32_t line, float noRepeatSeconds)
    {
        if (dialogueEvent == kQuietVoiceEvent && FeatureSwitch::IsOn(FeatureSwitch::Feature::QuietLines)
            && !MissionCodeGuard::ShouldBypassHooks())
        {
            const std::uint32_t take = RestoredTake(line);
            LogDebug("QuietLines: line 0x%08X plays 0x%08X", line, take);
            line = take;
        }
        return g_Orig(self, slot, dialogueEvent, callType, line, noRepeatSeconds);
    }
}

void BuddyQuietVoice_SayAngryLine(void* soundController, std::uint32_t slot)
{
    if (!soundController || !FeatureSwitch::IsOn(FeatureSwitch::Feature::QuietLines) || MissionCodeGuard::ShouldBypassHooks())
        return;
    const std::uint64_t now = GetTickCount64();
    std::uint64_t last = g_AngryMs.load(std::memory_order_relaxed);
    if (last && now - last < kAngryEveryMs)
        return;
    if (!g_AngryMs.compare_exchange_strong(last, now, std::memory_order_relaxed))
        return;
    const std::uint32_t line = NextTake(kAngryLines, g_AngryTurn);
    LogDebug("QuietLines: the player's lethal hit, angry line 0x%08X", line);
    const CallVoice_t callVoice = (*reinterpret_cast<CallVoice_t* const*>(soundController))[kVtblCallVoice / sizeof(void*)];
    callVoice(soundController, slot, kQuietVoiceEvent, kCallType, line, 0.0f);
}

bool Install_BuddyQuiet2SoundControllerImpl_CallVoice_Hook()
{
    if (g_Target)
        return true;
    if (!gAddr.BuddyQuiet2SoundControllerImpl_CallVoice)
    {
        LogWarn("QuietLines: Quiet's voice call unknown for %s, she keeps her vanilla takes", GetGameBuildName(gGameBuild));
        return true;
    }

    void* target = ResolveGameAddress(gAddr.BuddyQuiet2SoundControllerImpl_CallVoice);
    const HookEntry state = CheckHookEntry(target, kCallVoiceEntry, sizeof(kCallVoiceEntry));
    if (state == HookEntry::Unknown)
    {
        LogError("QuietLines: Quiet's voice call differs on %s, she keeps her vanilla takes", GetGameBuildName(gGameBuild));
        return false;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("QuietLines: BuddyQuiet2SoundControllerImpl::CallVoice", target);

    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkCallVoice), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("QuietLines: Quiet's voice call hook failed at %p, she keeps her vanilla takes", target);
        g_Orig = nullptr;
        return false;
    }
    g_Target = target;
    return true;
}

void Uninstall_BuddyQuiet2SoundControllerImpl_CallVoice_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig   = nullptr;
}
