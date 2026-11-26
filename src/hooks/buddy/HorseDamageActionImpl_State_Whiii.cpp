#include <pch.h>
#include "HorseDamageActionImpl_State_Whiii.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "FeatureSwitch.h"
#include "HookUtils.h"
#include "log.h"
#include "MissionCodeGuard.h"
#include "../player/Player2Impl_CallVoice.h"

namespace
{
    using StateWhiii_t = void(__fastcall*)(std::uint8_t* self, std::uint32_t horse, std::uint32_t proc, void* param);

    const std::uint8_t kStateWhiiiEntry[] = { 0x41, 0x83, 0xF8, 0x07, 0x0F, 0x87, 0x74, 0x01, 0x00, 0x00, 0x53, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x60 };

    constexpr std::uint32_t kProcEnter     = 2;      // 2 starts the rearing motion, 3 leaves the state
    constexpr std::size_t   kSelfFirstHorse = 0x98;
    constexpr std::size_t   kSelfWorks     = 0xB0;
    constexpr std::size_t   kWorkSize      = 0x1A0;
    constexpr std::size_t   kWorkSlot      = 0x20;
    constexpr std::size_t   kSelfHorseData = 0x70;
    constexpr std::size_t   kDataSlots     = 0x68;
    constexpr std::size_t   kSlotsFirst    = 0x08;
    constexpr std::size_t   kSlotsCount    = 0x10;
    constexpr std::size_t   kSlotSize      = 0x2F0;
    constexpr std::size_t   kSlotRider     = 0x0E;   // game object id on the saddle, -1 = nobody; +0x10 is a body carried across it
    constexpr std::uint32_t kCalmHorse     = 0x5474DDEEu;   // "Easy, easy. Whoa, whoa!"

    StateWhiii_t g_Orig   = nullptr;
    void*        g_Target = nullptr;

    bool HasRider_SEH(const std::uint8_t* self, std::uint32_t horse)
    {
        __try
        {
            const std::uint8_t* work = *reinterpret_cast<std::uint8_t* const*>(self + kSelfWorks)
                + static_cast<std::size_t>(horse - *reinterpret_cast<const std::uint32_t*>(self + kSelfFirstHorse)) * kWorkSize;
            const std::uint32_t slot  = *reinterpret_cast<const std::uint32_t*>(work + kWorkSlot);
            const std::uint8_t* slots = *reinterpret_cast<std::uint8_t* const*>(*reinterpret_cast<std::uint8_t* const*>(self + kSelfHorseData) + kDataSlots);
            if (slot >= *reinterpret_cast<const std::uint32_t*>(slots + kSlotsCount))
                return false;
            const std::uint8_t* record = *reinterpret_cast<std::uint8_t* const*>(slots + kSlotsFirst) + static_cast<std::size_t>(slot) * kSlotSize;
            return *reinterpret_cast<const std::int16_t*>(record + kSlotRider) != -1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void __fastcall hkStateWhiii(std::uint8_t* self, std::uint32_t horse, std::uint32_t proc, void* param)
    {
        const bool rearing = proc == kProcEnter && self && !MissionCodeGuard::ShouldBypassHooks() && HasRider_SEH(self, horse);
        g_Orig(self, horse, proc, param);
        if (!rearing || !FeatureSwitch::IsOn(FeatureSwitch::Feature::BuddyLines))
            return;
        SayMinorPlayerLine(kCalmHorse);
    }
}

bool Install_HorseDamageActionImpl_State_Whiii_Hook()
{
    if (g_Target)
        return true;
    if (!gAddr.HorseDamageActionImpl_State_Whiii)
    {
        LogWarn("HorseVoice: horse rearing state unknown for %s, no calming line", GetGameBuildName(gGameBuild));
        return true;
    }

    void* target = ResolveGameAddress(gAddr.HorseDamageActionImpl_State_Whiii);
    const HookEntry state = CheckHookEntry(target, kStateWhiiiEntry, sizeof(kStateWhiiiEntry));
    if (state == HookEntry::Unknown)
    {
        LogError("HorseVoice: horse rearing state differs on %s, no calming line", GetGameBuildName(gGameBuild));
        return false;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("HorseVoice: horse DamageActionImpl::State_Whiii", target);

    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkStateWhiii), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("HorseVoice: horse rearing hook failed at %p, no calming line", target);
        g_Orig = nullptr;
        return false;
    }
    g_Target = target;
    return true;
}

void Uninstall_HorseDamageActionImpl_State_Whiii_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig   = nullptr;
}
