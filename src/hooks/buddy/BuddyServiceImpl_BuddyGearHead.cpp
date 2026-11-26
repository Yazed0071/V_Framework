#include <pch.h>

#include "BuddyServiceImpl_BuddyGearHead.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "AddressSet.h"
#include "BattleGearBuddy.h"
#include "BattleGearLoadoutSlots.h"
#include "BattleGearWeapons.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using Getter_t       = std::uint32_t(__fastcall*)(void* service);
    using Setter_t       = void(__fastcall*)(void* service, std::uint8_t value);
    using CheckChanged_t = void(__fastcall*)(void* walker);

    constexpr std::uint8_t kBaseArmType  = 0;   // the TASK-ARM
    constexpr std::uint8_t kBaseArmLevel = 1;

    constexpr std::size_t kGetterCallRel = 5;    // call GetScriptVars: rel32 differs per function and between EN and JP
    constexpr std::size_t kGetterVarAt   = 12;   // the low byte of the vars offset in movzx eax, byte [rax+0x2E5x]
    constexpr std::size_t kSetterCallRel = 10;
    constexpr std::size_t kSetterVarAt   = 16;   // the low byte of the vars offset in mov [rax+0x2E5x], bl

    const std::uint8_t kGetterEntry[] = { 0x48, 0x83, 0xEC, 0x28, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x0F, 0xB6, 0x80, 0x00, 0x2E, 0x00, 0x00,
                                          0x48, 0x83, 0xC4, 0x28, 0xC3 };
    const std::uint8_t kSetterEntry[] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x0F, 0xB6, 0xDA, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x88, 0x98,
                                          0x00, 0x2E, 0x00, 0x00, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3 };
    const std::uint8_t kCheckHeadChangedEntry[] = { 0x48, 0x8B, 0xC4, 0x56, 0x48, 0x83, 0xEC, 0x50, 0x66, 0x83, 0x79, 0x28, 0x2C, 0x48,
                                                    0x8B, 0xF1, 0x0F, 0x85, 0x6A, 0x02, 0x00, 0x00 };

    struct Hook
    {
        const char*  what;
        std::uint8_t varLow;
        void*        orig;
        void*        target;
    };

    Hook g_GetType     = { "head type getter", 0x57, nullptr, nullptr };
    Hook g_GetGrade    = { "head grade getter", 0x58, nullptr, nullptr };
    Hook g_SetType     = { "head type setter", 0x57, nullptr, nullptr };
    Hook g_SetGrade    = { "head grade setter", 0x58, nullptr, nullptr };
    Hook g_GetArmType  = { "arm type getter", 0x59, nullptr, nullptr };
    Hook g_GetArmLevel = { "arm grade getter", 0x5A, nullptr, nullptr };
    void* g_CheckHeadOrig = nullptr;
    void* g_CheckHeadTarget = nullptr;

    thread_local int t_HeadChecks = 0;   // the loaded Battle Gear's own walker is asking for its head

    // out in the field the loadout answers. In the ACC only the Battle Gear's own buddy block and walker get its head: the D-Walker's
    // page and Sortie Prep read the same getters for the D-Walker's save
    bool AnswersForBattleGear()
    {
        return battleGearWeapons::LoadoutApplies() || t_HeadChecks > 0 || battleGear::SettingBlockUp();
    }

    bool WearsShownHead()   // in the ACC the Battle Gear's own walker wears the head a Battle Gear menu shows
    {
        return !battleGearWeapons::LoadoutApplies() && (t_HeadChecks > 0 || battleGear::SettingBlockUp())
            && battleGearLoadoutSlots::ShownHeadType() != 0;
    }

    // the originals end in movzx eax, byte: return the whole zero-extended register the same way
    std::uint32_t __fastcall hkGetType(void* service)
    {
        if (WearsShownHead())
            return battleGearLoadoutSlots::ShownHeadType();
        return AnswersForBattleGear() ? battleGearWeapons::HeadType() : reinterpret_cast<Getter_t>(g_GetType.orig)(service);
    }

    std::uint32_t __fastcall hkGetGrade(void* service)
    {
        if (WearsShownHead())
            return battleGearLoadoutSlots::ShownHeadGrade();
        return AnswersForBattleGear() ? battleGearWeapons::HeadGrade() : reinterpret_cast<Getter_t>(g_GetGrade.orig)(service);
    }

    void __fastcall hkSetType(void* service, std::uint8_t value)
    {
        if (!AnswersForBattleGear())
            reinterpret_cast<Setter_t>(g_SetType.orig)(service, value);
    }

    void __fastcall hkSetGrade(void* service, std::uint8_t value)
    {
        if (!AnswersForBattleGear())
            reinterpret_cast<Setter_t>(g_SetGrade.orig)(service, value);
    }

    std::uint32_t __fastcall hkGetArmType(void* service)   // the Battle Gear has no arm options: the D-Walker's saved one stays out of it
    {
        return battleGearWeapons::LoadoutApplies() ? kBaseArmType : reinterpret_cast<Getter_t>(g_GetArmType.orig)(service);
    }

    std::uint32_t __fastcall hkGetArmLevel(void* service)
    {
        return battleGearWeapons::LoadoutApplies() ? kBaseArmLevel : reinterpret_cast<Getter_t>(g_GetArmLevel.orig)(service);
    }

    void __fastcall hkCheckHeadChanged(void* walker)
    {
        const int own = battleGear::IsBattleGearLoaded() ? 1 : 0;
        t_HeadChecks += own;
        reinterpret_cast<CheckChanged_t>(g_CheckHeadOrig)(walker);
        t_HeadChecks -= own;
    }

    bool HookOne(Hook& h, std::uintptr_t address, void* detour, const std::uint8_t* entry, std::size_t entrySize, std::size_t callRel,
                 std::size_t varAt)
    {
        if (h.target)
            return true;
        void* target = address ? ResolveGameAddress(address) : nullptr;
        if (!target)
        {
            LogError("BattleGear: D-Walker %s address missing on this build, the Battle Gear keeps the D-Walker's own", h.what);
            return false;
        }
        std::uint8_t expected[32];
        std::memcpy(expected, entry, entrySize);
        expected[varAt] = h.varLow;
        std::uint8_t mask[32];
        std::memset(mask, 1, entrySize);
        std::memset(mask + callRel, 0, 4);
        const HookEntry state = CheckHookEntryMasked(target, expected, mask, entrySize);
        if (state == HookEntry::Unknown)
        {
            char desc[160] = {};
            DescribeFunctionEntry(target, desc, sizeof(desc));
            LogError("BattleGear: D-Walker %s differs on this build (%s), the Battle Gear keeps the D-Walker's own", h.what, desc);
            return false;
        }
        if (state == HookEntry::ForeignHook)
        {
            char label[96] = {};
            snprintf(label, sizeof(label), "BattleGear: D-Walker %s", h.what);
            LogHookedOnTop(label, target);
        }
        if (!CreateAndEnableHook(target, detour, &h.orig))
        {
            LogError("BattleGear: D-Walker %s hook failed, the Battle Gear keeps the D-Walker's own", h.what);
            return false;
        }
        h.target = target;
        return true;
    }

    void UnhookOne(Hook& h)
    {
        if (!h.target)
            return;
        DisableAndRemoveHook(h.target);
        h.target = nullptr;
        h.orig = nullptr;
    }

    void HookHeadCheck()
    {
        constexpr const char* kConsequence = "the Battle Gear's Sortie Prep preview wears the D-Walker's head";
        void* target = gAddr.WalkerGear2Impl_CheckHeadChanged ? ResolveGameAddress(gAddr.WalkerGear2Impl_CheckHeadChanged) : nullptr;
        if (!target)
        {
            LogError("BattleGear: walker head check address missing on this build, %s", kConsequence);
            return;
        }
        const HookEntry state = CheckHookEntry(target, kCheckHeadChangedEntry, sizeof(kCheckHeadChangedEntry));
        if (state == HookEntry::Unknown)
        {
            char desc[160] = {};
            DescribeFunctionEntry(target, desc, sizeof(desc));
            LogError("BattleGear: walker head check differs on this build (%s), %s", desc, kConsequence);
            return;
        }
        if (state == HookEntry::ForeignHook)
            LogHookedOnTop("BattleGear: walker head check", target);
        if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkCheckHeadChanged), &g_CheckHeadOrig))
        {
            LogError("BattleGear: walker head check hook failed, %s", kConsequence);
            return;
        }
        g_CheckHeadTarget = target;
    }
}

bool Install_BuddyServiceImpl_BuddyGearHead_Hooks()
{
    static_assert(sizeof(kGetterEntry) <= 32 && sizeof(kSetterEntry) <= 32, "entry buffers");
    const bool ok =
        HookOne(g_SetType, gAddr.BuddyServiceImpl_SetBuddyGearHeadType, reinterpret_cast<void*>(&hkSetType), kSetterEntry, sizeof(kSetterEntry),
                kSetterCallRel, kSetterVarAt)
        && HookOne(g_SetGrade, gAddr.BuddyServiceImpl_SetBuddyGearHeadGrade, reinterpret_cast<void*>(&hkSetGrade), kSetterEntry,
                   sizeof(kSetterEntry), kSetterCallRel, kSetterVarAt)
        && HookOne(g_GetType, gAddr.BuddyServiceImpl_GetBuddyGearHeadType, reinterpret_cast<void*>(&hkGetType), kGetterEntry,
                   sizeof(kGetterEntry), kGetterCallRel, kGetterVarAt)
        && HookOne(g_GetGrade, gAddr.BuddyServiceImpl_GetBuddyGearHeadGrade, reinterpret_cast<void*>(&hkGetGrade), kGetterEntry,
                   sizeof(kGetterEntry), kGetterCallRel, kGetterVarAt);
    if (!ok)
    {
        Uninstall_BuddyServiceImpl_BuddyGearHead_Hooks();
        return false;
    }
    if (!HookOne(g_GetArmType, gAddr.BuddyServiceImpl_GetBuddyGearArmType, reinterpret_cast<void*>(&hkGetArmType), kGetterEntry,
                 sizeof(kGetterEntry), kGetterCallRel, kGetterVarAt)
        || !HookOne(g_GetArmLevel, gAddr.BuddyServiceImpl_GetBuddyGearArmLevel, reinterpret_cast<void*>(&hkGetArmLevel), kGetterEntry,
                    sizeof(kGetterEntry), kGetterCallRel, kGetterVarAt))
    {
        UnhookOne(g_GetArmLevel);
        UnhookOne(g_GetArmType);
    }
    if (!g_CheckHeadTarget)
        HookHeadCheck();
    return true;
}

void Uninstall_BuddyServiceImpl_BuddyGearHead_Hooks()
{
    if (g_CheckHeadTarget)
    {
        DisableAndRemoveHook(g_CheckHeadTarget);
        g_CheckHeadTarget = nullptr;
        g_CheckHeadOrig = nullptr;
    }
    UnhookOne(g_GetArmLevel);
    UnhookOne(g_GetArmType);
    UnhookOne(g_GetGrade);
    UnhookOne(g_GetType);
    UnhookOne(g_SetGrade);
    UnhookOne(g_SetType);
}
