#include <pch.h>

#include "BuddyServiceImpl_SetBuddyGearMainWP.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "AddressSet.h"
#include "BattleGearWeapons.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using Setter_t = void(__fastcall*)(void* service, std::uint8_t value);

    constexpr std::size_t kCallRel = 10;   // call GetScriptVars: rel32 differs per function and between EN and JP
    constexpr std::size_t kVarAt   = 16;   // the low byte of the vars offset in mov [rax+0x2E5x], bl

    const std::uint8_t kSetterEntry[] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x0F, 0xB6, 0xDA, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x88, 0x98,
                                          0x00, 0x2E, 0x00, 0x00, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3 };

    struct Setter
    {
        const char*  what;
        std::uint8_t varLow;
        Setter_t     orig;
        void*        target;
    };

    Setter g_MainType  = { "main weapon type", 0x5B, nullptr, nullptr };
    Setter g_MainGrade = { "main weapon grade", 0x5C, nullptr, nullptr };

    void __fastcall hkSetMainType(void* service, std::uint8_t value)
    {
        if (!battleGearWeapons::LoadoutApplies())
            g_MainType.orig(service, value);
    }

    void __fastcall hkSetMainGrade(void* service, std::uint8_t value)
    {
        if (!battleGearWeapons::LoadoutApplies())
            g_MainGrade.orig(service, value);
    }

    bool HookOne(Setter& s, std::uintptr_t address, void* detour)
    {
        if (s.target)
            return true;
        void* target = address ? ResolveGameAddress(address) : nullptr;
        if (!target)
        {
            LogError("BattleGear: D-Walker %s setter address missing on this build, Battle Gear weapons off", s.what);
            return false;
        }
        std::uint8_t expected[sizeof(kSetterEntry)];
        std::memcpy(expected, kSetterEntry, sizeof(expected));
        expected[kVarAt] = s.varLow;
        std::uint8_t mask[sizeof(kSetterEntry)];
        std::memset(mask, 1, sizeof(mask));
        std::memset(mask + kCallRel, 0, 4);
        const HookEntry state = CheckHookEntryMasked(target, expected, mask, sizeof(expected));
        if (state == HookEntry::Unknown)
        {
            char desc[160] = {};
            DescribeFunctionEntry(target, desc, sizeof(desc));
            LogError("BattleGear: D-Walker %s setter differs on this build (%s), Battle Gear weapons off", s.what, desc);
            return false;
        }
        if (state == HookEntry::ForeignHook)
        {
            char label[96] = {};
            snprintf(label, sizeof(label), "BattleGear: D-Walker %s setter", s.what);
            LogHookedOnTop(label, target);
        }
        if (!CreateAndEnableHook(target, detour, reinterpret_cast<void**>(&s.orig)))
        {
            LogError("BattleGear: D-Walker %s setter hook failed, Battle Gear weapons off", s.what);
            return false;
        }
        s.target = target;
        return true;
    }

    void UnhookOne(Setter& s)
    {
        if (!s.target)
            return;
        DisableAndRemoveHook(s.target);
        s.target = nullptr;
        s.orig = nullptr;
    }
}

bool Install_BuddyServiceImpl_SetBuddyGearMainWP_Hooks()
{
    const bool ok = HookOne(g_MainType, gAddr.BuddyServiceImpl_SetBuddyGearMainWPType, reinterpret_cast<void*>(&hkSetMainType))
                 && HookOne(g_MainGrade, gAddr.BuddyServiceImpl_SetBuddyGearMainWPGrade, reinterpret_cast<void*>(&hkSetMainGrade));
    if (!ok)
        Uninstall_BuddyServiceImpl_SetBuddyGearMainWP_Hooks();
    return ok;
}

void Uninstall_BuddyServiceImpl_SetBuddyGearMainWP_Hooks()
{
    UnhookOne(g_MainGrade);
    UnhookOne(g_MainType);
}
