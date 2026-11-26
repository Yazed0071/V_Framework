#include <pch.h>
#include "MbDvcSoundControllerImpl_RequestVoice.h"

#include <atomic>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "../player/IdroidVoiceLines.h"
#include "../player/Player2Impl_CallVoice.h"

namespace
{
    using RequestVoice_t = void(__fastcall*)(void* soundController, std::uint8_t voice);
    using IsFobSortie_t  = bool(__fastcall*)(void* preparation);

    const std::uint8_t kRequestVoiceEntry[] = { 0x80, 0xFA, 0x75, 0x73, 0x34, 0x48, 0x83, 0xC1, 0x18, 0x0F, 0xB6, 0xC2,
                                                0x48, 0x6B, 0xC0, 0x1C, 0x48, 0x03, 0xC8, 0xB8, 0x00, 0x02, 0x00, 0x00 };   // 0x75 voices of 0x1C bytes, flag 0x200 = requested
    const std::uint8_t kIsFobSortieEntry[]  = { 0x48, 0x83, 0xEC, 0x28, 0xE8, 0x00, 0x00, 0x00, 0x00, 0xBA, 0x1B, 0xE6, 0x87, 0x69,
                                                0x41, 0xB8, 0x80, 0x00, 0x00, 0x00 };                                      // 0x6987E61B = MissionPrep2, bit 0x80 = FOB_MISSION
    const std::uint8_t kIsFobSortieMask[]   = { 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };             // the call's rel32 is different on each build

    constexpr std::uint8_t  kVoiceDoneDeploy = 69;     // VOICE_DONE_DEPLOY
    constexpr std::uint32_t kDeployHoldMs    = 4000;   // Snake's own deploy line comes with his ready motion

    RequestVoice_t    g_OrigRequestVoice   = nullptr;
    IsFobSortie_t     g_IsFobSortie        = nullptr;
    void*             g_RequestVoiceTarget = nullptr;
    std::atomic<bool> g_RetaliationSortie{ false };

    void __fastcall hkRequestVoice(void* soundController, std::uint8_t voice)
    {
        if (voice == kVoiceDoneDeploy)
        {
            const bool retaliation = g_RetaliationSortie.exchange(false, std::memory_order_relaxed);
            const bool fobSortie   = g_IsFobSortie(nullptr);
            LogDebug("FobSneakProbe: deploy fob=%u retaliation=%u", fobSortie, retaliation);
            g_OrigRequestVoice(soundController, voice);
            if (retaliation && fobSortie && SayRevengeLine())
                HoldGamePlayerLines(kDeployHoldMs);
            return;
        }
        g_OrigRequestVoice(soundController, voice);
    }

    void LogEntryMismatch(const char* what, const void* fn)
    {
        char entry[160] = {};
        DescribeFunctionEntry(fn, entry, sizeof(entry));
        LogError("RetaliationLine: %s entry mismatch on %s (%s), Snake keeps quiet at retaliation deploys", what,
                 GetGameBuildName(gGameBuild), entry);
    }
}

bool Install_MbDvcSoundControllerImpl_RequestVoice_Hook()
{
    if (g_RequestVoiceTarget)
        return true;

    void* isFobSortie = ResolveGameAddress(gAddr.MissionPreparationSystemImpl_IsFobSortie);
    if (CheckHookEntryMasked(isFobSortie, kIsFobSortieEntry, kIsFobSortieMask, sizeof(kIsFobSortieEntry)) == HookEntry::Unknown)
    {
        LogEntryMismatch("MissionPreparationSystemImpl::IsFobSortie", isFobSortie);
        return true;
    }
    g_IsFobSortie = reinterpret_cast<IsFobSortie_t>(isFobSortie);

    void* target = ResolveGameAddress(gAddr.MbDvcSoundControllerImpl_RequestVoice);
    const HookEntry state = CheckHookEntry(target, kRequestVoiceEntry, sizeof(kRequestVoiceEntry));
    if (state == HookEntry::Unknown)
    {
        LogEntryMismatch("MbDvcSoundControllerImpl::RequestVoice", target);
        return true;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("RetaliationLine: MbDvcSoundControllerImpl::RequestVoice", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkRequestVoice), reinterpret_cast<void**>(&g_OrigRequestVoice)))
    {
        LogError("RetaliationLine: MbDvcSoundControllerImpl::RequestVoice hook failed at %p, Snake keeps quiet at retaliation deploys", target);
        return true;
    }
    g_RequestVoiceTarget = target;
    return true;
}

void Uninstall_MbDvcSoundControllerImpl_RequestVoice_Hook()
{
    if (g_RequestVoiceTarget)
        DisableAndRemoveHook(g_RequestVoiceTarget);
    g_RequestVoiceTarget = nullptr;
    g_RetaliationSortie.store(false, std::memory_order_relaxed);
}

void SetRetaliationSortie(bool retaliation)
{
    g_RetaliationSortie.store(retaliation, std::memory_order_relaxed);
}

void ClearRetaliationSortie()
{
    g_RetaliationSortie.store(false, std::memory_order_relaxed);
}
