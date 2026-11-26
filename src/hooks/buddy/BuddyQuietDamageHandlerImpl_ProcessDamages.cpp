#include <pch.h>
#include "BuddyQuietDamageHandlerImpl_ProcessDamages.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "BuddyQuiet2SoundControllerImpl_CallVoice.h"
#include "FeatureSwitch.h"
#include "HookUtils.h"
#include "log.h"
#include "MissionCodeGuard.h"

namespace
{
    using ProcessDamages_t = std::uint64_t(__fastcall*)(std::uint8_t* self, std::uint32_t quiet, std::uint8_t* damages, std::uint32_t damageCount);
    using AttackOf_t       = const std::uint8_t*(__fastcall*)(void* attackTable, std::uint16_t attackId);

    const std::uint8_t kProcessDamagesEntry[] = { 0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0xD8, 0x00, 0x00, 0x00 };

    constexpr std::size_t   kSelfSystem          = 0x48;   // self is the handler 0x20 in, as the game passes it
    constexpr std::size_t   kSelfLives           = 0x50;
    constexpr std::size_t   kSelfAttackTable     = 0x60;
    constexpr std::size_t   kSystemInstances     = 0x10;
    constexpr std::size_t   kSystemSound         = 0x78;
    constexpr std::size_t   kInstancesOnField    = 0x08;   // one bit per Quiet
    constexpr std::size_t   kInstancesSlotOwners = 0x28;   // u16 per slot: which Quiet stands in it
    constexpr std::size_t   kInstancesSlotCount  = 0x44;
    constexpr std::size_t   kLivesFirst          = 0x08;
    constexpr std::size_t   kLifeSize            = 0x2C;
    constexpr std::size_t   kLifePoints          = 0x00;
    constexpr std::size_t   kLifeLost            = 0x04;   // an instant kill adds all her life points here
    constexpr std::size_t   kLifeStatus          = 0x25;
    constexpr std::uint8_t  kLifeStatusNoFlinch  = 2;      // the game stops her flinching at this status
    constexpr std::size_t   kDamageSize          = 0x30;
    constexpr std::size_t   kDamageAttacker      = 0x22;
    constexpr std::size_t   kDamageAttackId      = 0x28;
    constexpr std::uint16_t kNotPlayerBits       = 0xFE00; // the type half of a game object id, the player's type is 0
    constexpr std::size_t   kAttackKinds         = 0x14;
    constexpr std::uint16_t kStunOrSleepKinds    = 0x18;
    constexpr std::uint16_t kLethalKinds         = 0x721;
    constexpr std::uint16_t kAttackMagazine      = 0xAD;   // ATK_Magazine, then ATK_GrenadeHit and ATK_DecoyHit
    constexpr std::uint16_t kAttackDecoyHit      = 0xAF;
    constexpr std::uint16_t kAttackBaitHit       = 0xB1;   // ATK_KibidangoHit
    constexpr std::uint16_t kAttackSmoke         = 0xB5;   // ATK_SmokeGrenade

    ProcessDamages_t g_Orig   = nullptr;
    void*            g_Target = nullptr;

    struct Speaker
    {
        void*         sound = nullptr;
        std::uint32_t slot  = 0;
    };

    bool IsLethal(void* attackTable, std::uint16_t attackId)
    {
        if (attackId == kAttackSmoke || attackId == kAttackBaitHit || (attackId >= kAttackMagazine && attackId <= kAttackDecoyHit))
            return false;
        const std::uint8_t* attack = (*reinterpret_cast<AttackOf_t* const*>(attackTable))[0](attackTable, attackId);
        if (!attack)
            return false;
        const std::uint16_t kinds = *reinterpret_cast<const std::uint16_t*>(attack + kAttackKinds);
        return (kinds & kStunOrSleepKinds) == 0 && (kinds & kLethalKinds) != 0;
    }

    bool ShotByPlayer_SEH(std::uint8_t* self, std::uint32_t quiet, const std::uint8_t* damages, std::uint32_t damageCount, Speaker& speaker)
    {
        __try
        {
            void* attackTable = *reinterpret_cast<void**>(self + kSelfAttackTable);
            bool shot = false;
            for (std::uint32_t i = 0; attackTable && !shot && i < damageCount; ++i)
            {
                const std::uint8_t* damage = damages + static_cast<std::size_t>(i) * kDamageSize;
                shot = (*reinterpret_cast<const std::uint16_t*>(damage + kDamageAttacker) & kNotPlayerBits) == 0
                    && IsLethal(attackTable, *reinterpret_cast<const std::uint16_t*>(damage + kDamageAttackId));
            }
            if (!shot)
                return false;

            const std::uint8_t* life = *reinterpret_cast<std::uint8_t* const*>(*reinterpret_cast<std::uint8_t* const*>(self + kSelfLives) + kLivesFirst)
                + static_cast<std::size_t>(quiet) * kLifeSize;
            if (life[kLifeStatus] == kLifeStatusNoFlinch
                || *reinterpret_cast<const std::uint16_t*>(life + kLifeLost) >= *reinterpret_cast<const std::uint16_t*>(life + kLifePoints))
                return false;

            const std::uint8_t* system    = *reinterpret_cast<std::uint8_t* const*>(self + kSelfSystem);
            const std::uint8_t* instances = *reinterpret_cast<std::uint8_t* const*>(system + kSystemInstances);
            const std::uint8_t* onField   = *reinterpret_cast<std::uint8_t* const*>(instances + kInstancesOnField);
            if ((onField[quiet >> 3] & (1u << (quiet & 7))) == 0)
                return false;
            const std::uint16_t* owners = *reinterpret_cast<const std::uint16_t* const*>(instances + kInstancesSlotOwners);
            const std::uint32_t  slots  = *reinterpret_cast<const std::uint32_t*>(instances + kInstancesSlotCount);
            for (std::uint32_t slot = 0; slot < slots; ++slot)
            {
                if (owners[slot] != quiet)
                    continue;
                speaker.sound = *reinterpret_cast<void* const*>(system + kSystemSound);
                speaker.slot  = slot;
                return speaker.sound != nullptr;
            }
            return false;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint64_t __fastcall hkProcessDamages(std::uint8_t* self, std::uint32_t quiet, std::uint8_t* damages, std::uint32_t damageCount)
    {
        const std::uint64_t result = g_Orig(self, quiet, damages, damageCount);
        Speaker speaker;
        if (self && damages && FeatureSwitch::IsOn(FeatureSwitch::Feature::QuietLines) && !MissionCodeGuard::ShouldBypassHooks()
            && ShotByPlayer_SEH(self, quiet, damages, damageCount, speaker))
            BuddyQuietVoice_SayAngryLine(speaker.sound, speaker.slot);
        return result;
    }
}

bool Install_BuddyQuietDamageHandlerImpl_ProcessDamages_Hook()
{
    if (g_Target)
        return true;
    if (!gAddr.BuddyQuietDamageHandlerImpl_ProcessDamages)
    {
        LogWarn("QuietLines: Quiet's damage handler unknown for %s, no angry line", GetGameBuildName(gGameBuild));
        return true;
    }

    void* target = ResolveGameAddress(gAddr.BuddyQuietDamageHandlerImpl_ProcessDamages);
    const HookEntry state = CheckHookEntry(target, kProcessDamagesEntry, sizeof(kProcessDamagesEntry));
    if (state == HookEntry::Unknown)
    {
        LogError("QuietLines: Quiet's damage handler differs on %s, no angry line", GetGameBuildName(gGameBuild));
        return false;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("QuietLines: buddy Quiet DamageHandlerImpl::ProcessDamages", target);

    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkProcessDamages), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("QuietLines: Quiet's damage handler hook failed at %p, no angry line", target);
        g_Orig = nullptr;
        return false;
    }
    g_Target = target;
    return true;
}

void Uninstall_BuddyQuietDamageHandlerImpl_ProcessDamages_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig   = nullptr;
}
