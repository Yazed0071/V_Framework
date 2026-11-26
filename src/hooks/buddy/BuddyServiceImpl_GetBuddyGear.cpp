#include <pch.h>

#include "BuddyServiceImpl_GetBuddyGear.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "AddressSet.h"
#include "BattleGearBuddy.h"
#include "BattleGearWeapons.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using Getter_t = std::uint32_t(__fastcall*)(void* service);

    constexpr std::uint8_t kMainGrade = 3;
    constexpr std::uint8_t kSubGrade  = 1;

    constexpr std::size_t kCallRel = 5;   // call GetScriptVars: rel32 differs per function and between EN and JP
    constexpr std::size_t kVarAt   = 12;  // the low byte of the vars offset in movzx eax, byte [rax+0x2E5x]

    const std::uint8_t kGetterEntry[] = { 0x48, 0x83, 0xEC, 0x28, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x0F, 0xB6, 0x80, 0x00, 0x2E, 0x00, 0x00,
                                          0x48, 0x83, 0xC4, 0x28, 0xC3 };

    struct Getter
    {
        const char*  what;
        std::uint8_t varLow;
        Getter_t     orig;
        void*        target;
    };

    Getter g_MainType  = { "main weapon type", 0x5B, nullptr, nullptr };
    Getter g_MainGrade = { "main weapon grade", 0x5C, nullptr, nullptr };
    Getter g_SubType   = { "sidearm type", 0x5D, nullptr, nullptr };
    Getter g_SubGrade  = { "sidearm grade", 0x5E, nullptr, nullptr };

    // the originals end in movzx eax, byte: return the whole zero-extended register the same way
    std::uint32_t __fastcall hkMainType(void* service)
    {
        return battleGearWeapons::LoadoutApplies() ? battleGearWeapons::MainWeaponType() : g_MainType.orig(service);
    }

    std::uint32_t __fastcall hkMainGrade(void* service)
    {
        return battleGearWeapons::LoadoutApplies() ? kMainGrade : g_MainGrade.orig(service);
    }

    std::uint32_t __fastcall hkSubType(void* service)
    {
        return battleGearWeapons::LoadoutApplies() ? battleGearWeapons::kHeavyMg : g_SubType.orig(service);
    }

    std::uint32_t __fastcall hkSubGrade(void* service)
    {
        return battleGearWeapons::LoadoutApplies() ? kSubGrade : g_SubGrade.orig(service);
    }

    bool HookOne(Getter& g, std::uintptr_t address, void* detour)
    {
        if (g.target)
            return true;
        void* target = address ? ResolveGameAddress(address) : nullptr;
        if (!target)
        {
            LogError("BattleGear: D-Walker %s getter address missing on this build, Battle Gear weapons off", g.what);
            return false;
        }
        std::uint8_t expected[sizeof(kGetterEntry)];
        std::memcpy(expected, kGetterEntry, sizeof(expected));
        expected[kVarAt] = g.varLow;
        std::uint8_t mask[sizeof(kGetterEntry)];
        std::memset(mask, 1, sizeof(mask));
        std::memset(mask + kCallRel, 0, 4);
        const HookEntry state = CheckHookEntryMasked(target, expected, mask, sizeof(expected));
        if (state == HookEntry::Unknown)
        {
            char desc[160] = {};
            DescribeFunctionEntry(target, desc, sizeof(desc));
            LogError("BattleGear: D-Walker %s getter differs on this build (%s), Battle Gear weapons off", g.what, desc);
            return false;
        }
        if (state == HookEntry::ForeignHook)
        {
            char label[96] = {};
            snprintf(label, sizeof(label), "BattleGear: D-Walker %s getter", g.what);
            LogHookedOnTop(label, target);
        }
        if (!CreateAndEnableHook(target, detour, reinterpret_cast<void**>(&g.orig)))
        {
            LogError("BattleGear: D-Walker %s getter hook failed, Battle Gear weapons off", g.what);
            return false;
        }
        g.target = target;
        return true;
    }

    void UnhookOne(Getter& g)
    {
        if (!g.target)
            return;
        DisableAndRemoveHook(g.target);
        g.target = nullptr;
        g.orig = nullptr;
    }
}

bool Install_BuddyServiceImpl_GetBuddyGear_Hooks()
{
    const bool ok = HookOne(g_MainType, gAddr.BuddyServiceImpl_GetBuddyGearMainWPType, reinterpret_cast<void*>(&hkMainType))
                 && HookOne(g_MainGrade, gAddr.BuddyServiceImpl_GetBuddyGearMainWPGrade, reinterpret_cast<void*>(&hkMainGrade))
                 && HookOne(g_SubType, gAddr.BuddyServiceImpl_GetBuddyGearSubWPType, reinterpret_cast<void*>(&hkSubType))
                 && HookOne(g_SubGrade, gAddr.BuddyServiceImpl_GetBuddyGearSubWPGrade, reinterpret_cast<void*>(&hkSubGrade));
    if (!ok)
        Uninstall_BuddyServiceImpl_GetBuddyGear_Hooks();
    return ok;
}

void Uninstall_BuddyServiceImpl_GetBuddyGear_Hooks()
{
    UnhookOne(g_SubGrade);
    UnhookOne(g_SubType);
    UnhookOne(g_MainGrade);
    UnhookOne(g_MainType);
}
