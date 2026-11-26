#include <pch.h>
#include "EquipControllerImpl_OnShieldHit.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <mutex>

#include "AddressSet.h"
#include "FeatureSwitch.h"
#include "HookUtils.h"
#include "log.h"
#include "MissionCodeGuard.h"
#include "Player2Impl_CallVoice.h"

namespace
{
    using OnShieldHit_t             = void(__fastcall*)(std::uint8_t* equipIface, std::uint32_t slot, const std::uint8_t* hit);
    using IsShieldBreakableDamage_t = bool(__fastcall*)(std::uint8_t* equipController, std::uint32_t slot, float rate, std::uint32_t attackId);
    using GetQuarkSystemTable_t     = std::uint8_t*(__fastcall*)();
    using GetDamageParameter_t      = const std::uint8_t*(__fastcall*)(void* table, std::uint32_t attackId);
    using IsPlayingVoice_t          = bool(__fastcall*)(std::uint8_t* player, std::uint32_t object);
    using CallVoice_t               = void(__fastcall*)(std::uint8_t* player, std::uint32_t object, std::uint32_t line, std::uint32_t priority);
    using HitVoiceSaid_t            = bool(__fastcall*)(std::uint8_t* life, std::uint32_t slot);
    using MarkHitVoice_t            = void(__fastcall*)(std::uint8_t* life, std::uint32_t slot);

    struct BuildSites
    {
        std::uintptr_t onShieldHit;               // EquipControllerImpl, a hit on the equipped shield
        std::uintptr_t isShieldBreakableDamage;   // EquipControllerImpl::IsShieldBreakableDamage
    };


    const std::uint8_t kOnShieldHitEntry[]      = { 0x4C, 0x8B, 0xDC, 0x55, 0x56, 0x57, 0x41, 0x55, 0x49, 0x8D, 0x6B, 0x88, 0x48, 0x81,
                                                    0xEC, 0x58, 0x01, 0x00, 0x00, 0x48, 0x8B, 0x05 };   // the stack cookie's rel32 follows, different on each build
    const std::uint8_t kIsShieldBreakableEntry[] = { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83,
                                                     0xEC, 0x30, 0x4C, 0x8B, 0x41, 0x28 };

    constexpr std::size_t   kIfaceToController   = 0x20;
    constexpr std::size_t   kIfaceOwner          = 0x18;
    constexpr std::size_t   kOwnerCores          = 0x50;
    constexpr std::size_t   kOwnerSlotObject     = 0x70;    // u16 per player
    constexpr std::size_t   kOwnerSlotCore       = 0x78;    // u8 per player, 0xFF = none
    constexpr std::size_t   kOwnerLife           = 0xC0;    // LifeControllerImpl
    constexpr std::size_t   kCoreAction          = 0x78;
    constexpr std::size_t   kActionFirstObject   = 0x08;
    constexpr std::size_t   kActionStates        = 0x280;
    constexpr std::size_t   kActionStateSize     = 0x20;
    constexpr std::uint32_t kShieldRaised        = 1u << 28;
    constexpr std::size_t   kHitAttack           = 0x38;
    constexpr std::size_t   kHitRate             = 0x3C;
    constexpr float         kHitRateScale        = 0.005f;
    constexpr std::size_t   kQuarkApp            = 0x98;
    constexpr std::size_t   kAppDamageTable      = 0xA0;
    constexpr std::size_t   kAttackParamFlags    = 0x14;
    constexpr std::uint8_t  kAttackParamBlast    = 0x20;
    constexpr std::uint16_t kAutoCannon20        = 0x12D;   // ATK_Tankgun_20mmAutoCannon: blast flagged but fired like a gun
    constexpr std::uint16_t kAutoCannon30        = 0x12E;   // ATK_Tankgun_30mmAutoCannon
    constexpr std::size_t   kCallVoiceSlot       = 0x50 / sizeof(void*);   // Player2Impl
    constexpr std::size_t   kIsPlayingVoiceSlot  = 0x88 / sizeof(void*);   // Player2Impl
    constexpr std::size_t   kMarkHitVoiceSlot    = 0xE0 / sizeof(void*);   // LifeControllerImpl, the mark is wiped every update
    constexpr std::size_t   kHitVoiceSaidSlot    = 0xE8 / sizeof(void*);   // LifeControllerImpl
    constexpr std::uint32_t kVoicePriority       = 3;

    constexpr std::uint32_t kAutoFireLine = 0x200AD0A6u;   // SNAK1100_181110 "Rrrrrrrrgh!"
    constexpr std::uint32_t kBlastLine    = 0x200AD0A5u;   // SNAK1100_181210 "Grgh!"
    constexpr std::uint64_t kBlastGapMs   = 1000;

    struct ShieldHit
    {
        bool          held   = false;     // raised and not broken by this hit
        bool          blast  = false;
        std::uint16_t attack = 0;
        std::uint16_t object = 0;
        std::uint8_t* player = nullptr;   // Player2Impl
        std::uint8_t* life   = nullptr;   // LifeControllerImpl
    };

    OnShieldHit_t             g_Orig                    = nullptr;
    IsShieldBreakableDamage_t g_IsShieldBreakableDamage = nullptr;
    GetQuarkSystemTable_t     g_GetQuarkSystemTable     = nullptr;
    void*                     g_Target                  = nullptr;

    std::mutex    g_Mutex;
    std::uint64_t g_LastBlastMs = 0;

    template <typename T>
    T& At(const std::uint8_t* base, std::size_t offset)
    {
        return *reinterpret_cast<T*>(const_cast<std::uint8_t*>(base) + offset);
    }

    const BuildSites* SitesFor()
    {
        static const BuildSites sites = { gAddr.PlayerEquipControllerImpl_OnShieldHit, gAddr.PlayerEquipControllerImpl_IsShieldBreakableDamage };
        return &sites;
    }

    ShieldHit ReadShieldHit_SEH(std::uint8_t* equipIface, std::uint32_t slot, const std::uint8_t* hit)   // before the hit lands: a break changes his state
    {
        ShieldHit shot;
        __try
        {
            const std::uint8_t* owner = At<std::uint8_t*>(equipIface, kIfaceOwner);
            const std::uint8_t  core  = At<std::uint8_t*>(owner, kOwnerSlotCore)[slot];
            const std::uint16_t object = reinterpret_cast<const std::uint16_t*>(At<std::uint8_t*>(owner, kOwnerSlotObject))[slot];
            if (core == 0xFF || object == 0xFFFF)
                return shot;
            std::uint8_t* const player = At<std::uint8_t**>(owner, kOwnerCores)[core];
            const std::uint8_t* action = At<std::uint8_t*>(player, kCoreAction);
            const std::uint32_t state  = At<std::uint32_t>(At<std::uint8_t*>(action, kActionStates),
                                                          (object - At<std::uint32_t>(action, kActionFirstObject)) * kActionStateSize);
            if (!(state & kShieldRaised))
                return shot;

            shot.object = object;
            shot.player = player;
            shot.life   = At<std::uint8_t*>(owner, kOwnerLife);
            shot.attack = At<std::uint16_t>(hit, kHitAttack);
            const float rate = static_cast<float>(At<std::uint8_t>(hit, kHitRate)) * kHitRateScale;
            if (g_IsShieldBreakableDamage(equipIface - kIfaceToController, slot, rate, shot.attack))
                return shot;
            shot.held = true;

            void* table = At<void*>(At<std::uint8_t*>(g_GetQuarkSystemTable(), kQuarkApp), kAppDamageTable);
            const std::uint8_t* param = (*reinterpret_cast<GetDamageParameter_t* const*>(table))[0](table, shot.attack);
            shot.blast = param && (param[kAttackParamFlags] & kAttackParamBlast) != 0
                && shot.attack != kAutoCannon20 && shot.attack != kAutoCannon30;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            shot.held = false;
        }
        return shot;
    }

    bool BlastGapOver()
    {
        const std::uint64_t now = GetTickCount64();
        std::lock_guard<std::mutex> lock(g_Mutex);
        if (now - g_LastBlastMs < kBlastGapMs)
            return false;
        g_LastBlastMs = now;
        return true;
    }

    bool MayGrunt_SEH(const ShieldHit& shot, std::uint32_t slot)   // what DamageControllerImpl::CallDamageEffectWithReaction checks before the "Ugh" of a bullet hit
    {
        __try
        {
            const auto isPlayingVoice = reinterpret_cast<IsPlayingVoice_t>((*reinterpret_cast<void* const* const*>(shot.player))[kIsPlayingVoiceSlot]);
            const auto hitVoiceSaid   = reinterpret_cast<HitVoiceSaid_t>((*reinterpret_cast<void* const* const*>(shot.life))[kHitVoiceSaidSlot]);
            return !isPlayingVoice(shot.player, shot.object) && !hitVoiceSaid(shot.life, slot);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void __fastcall hkOnShieldHit(std::uint8_t* equipIface, std::uint32_t slot, const std::uint8_t* hit)
    {
        const bool active = equipIface && hit && !MissionCodeGuard::ShouldBypassHooks()
            && FeatureSwitch::IsOn(FeatureSwitch::Feature::PainAndDeathLines);
        const ShieldHit shot = active ? ReadShieldHit_SEH(equipIface, slot, hit) : ShieldHit{};
        g_Orig(equipIface, slot, hit);
        if (!shot.held)
            return;
        if (shot.blast)
        {
            if (!BlastGapOver())
                return;
            SayPlayerLine(kBlastLine);
            return;
        }
        if (!shot.player || !shot.life || !MayGrunt_SEH(shot, slot))
            return;
        reinterpret_cast<CallVoice_t>((*reinterpret_cast<void* const* const*>(shot.player))[kCallVoiceSlot])(shot.player, shot.object, kAutoFireLine, kVoicePriority);
        reinterpret_cast<MarkHitVoice_t>((*reinterpret_cast<void* const* const*>(shot.life))[kMarkHitVoiceSlot])(shot.life, slot);
    }
}

bool Install_EquipControllerImpl_OnShieldHit_Hook()
{
    if (g_Target)
        return true;
    const BuildSites* sites = SitesFor();
    if (!sites || !gAddr.GetQuarkSystemTable)
    {
        LogWarn("ShieldVoice: addresses unknown for %s, no shield grunts", GetGameBuildName(gGameBuild));
        return true;
    }

    void* breakable = ResolveGameAddress(sites->isShieldBreakableDamage);
    void* target    = ResolveGameAddress(sites->onShieldHit);
    const HookEntry state = CheckHookEntry(target, kOnShieldHitEntry, sizeof(kOnShieldHitEntry));
    if (!breakable || std::memcmp(breakable, kIsShieldBreakableEntry, sizeof(kIsShieldBreakableEntry)) != 0 || state == HookEntry::Unknown)
    {
        LogError("ShieldVoice: shield hit functions differ on %s, no shield grunts", GetGameBuildName(gGameBuild));
        return false;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("ShieldVoice: EquipControllerImpl shield hit", target);

    g_IsShieldBreakableDamage = reinterpret_cast<IsShieldBreakableDamage_t>(breakable);
    g_GetQuarkSystemTable     = reinterpret_cast<GetQuarkSystemTable_t>(ResolveGameAddress(gAddr.GetQuarkSystemTable));
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkOnShieldHit), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("ShieldVoice: shield hit hook failed at %p, no shield grunts", target);
        g_Orig = nullptr;
        return false;
    }
    g_Target = target;
    return true;
}

void Uninstall_EquipControllerImpl_OnShieldHit_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig   = nullptr;
}
