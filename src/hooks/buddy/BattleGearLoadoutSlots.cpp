#include <pch.h>

#include "BattleGearLoadoutSlots.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <intrin.h>
#include <mutex>

#include "AddressSet.h"
#include "BattleGearArmory.h"
#include "BattleGearBuddy.h"
#include "BattleGearCustomizePage.h"
#include "BattleGearWeapons.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using GetSlot_t     = std::uint32_t(__fastcall*)(void* utility);
    using SetSlot_t     = void(__fastcall*)(void* utility, std::uint8_t slot);
    using UsageCost_t   = std::uint32_t(__fastcall*)(void* utility, const std::uint8_t* param);
    using SetupMode_t   = std::uint64_t(__fastcall*)(void* selector, char start, char keepTab, std::int32_t* equipKind);
    using ChangeEquip_t = void(__fastcall*)(void* service, const std::uint8_t* param);

    constexpr std::uint32_t  kHeliSpaceBand     = 4;            // mission code / 10000
    constexpr std::int32_t   kBuddyEquipKind    = 0x1000000;    // the iDroid's equipment entry: the selector picks the list by buddy
    constexpr std::int32_t   kWalkerEquipKind   = 0x10000000;   // the walker's three loadout slots
    constexpr std::uint8_t   kWalkerGearType    = 4;            // BuddyType.WALKER_GEAR
    constexpr std::uintptr_t kUpdateRecordsSize = 0x11A0;       // ItemSelectorRecordCallFunc::UpdateRecords, the item selector's last function

    // tpp::gk::ModelViewerParam, 32-bit fields
    constexpr std::size_t kParamBuddyType = 0x14;
    constexpr std::size_t kParamBodyType  = 0x1C;
    constexpr std::size_t kParamBodyGrade = 0x20;
    constexpr std::size_t kParamMainType  = 0x34;
    constexpr std::size_t kParamMainGrade = 0x38;
    constexpr std::size_t kParamSubType   = 0x3C;
    constexpr std::size_t kParamSubGrade  = 0x40;
    constexpr std::size_t kParamHeadType  = 0x44;
    constexpr std::size_t kParamHeadGrade = 0x48;
    constexpr std::size_t kParamArmType   = 0x4C;
    constexpr std::size_t kParamArmGrade  = 0x50;

    constexpr int kDescribedParams = 8;

    constexpr std::size_t kGetSlotCallRel = 5;    // call GetQuarkSystemTable: rel32 differs between EN and JP
    constexpr std::size_t kSetSlotCallRel = 10;

    // sub rsp, 0x28 / call / mov rcx, [rax+0x98] / mov rax, [rcx+0x10] / movzx eax, byte [rax+0x1B74]
    const std::uint8_t kGetSlotEntry[] = { 0x48, 0x83, 0xEC, 0x28, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x88, 0x98, 0x00, 0x00, 0x00,
                                           0x48, 0x8B, 0x41, 0x10, 0x0F, 0xB6, 0x80, 0x74, 0x1B, 0x00, 0x00 };
    // push rbx / sub rsp, 0x20 / movzx ebx, dl / call / mov rcx, [rax+0x98] / mov rax, [rcx+0x10] / mov [rax+0x1B74], bl
    const std::uint8_t kSetSlotEntry[] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x0F, 0xB6, 0xDA, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B,
                                           0x88, 0x98, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x41, 0x10, 0x88, 0x98, 0x74, 0x1B, 0x00, 0x00 };
    const std::uint8_t kUsageCostEntry[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48,
                                             0x89, 0x7C, 0x24, 0x20, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xFA };
    const std::uint8_t kSetupModeEntry[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57,
                                             0x48, 0x83, 0xEC, 0x30, 0x49, 0x8B, 0xF9, 0x41, 0x0F, 0xB6, 0xF0, 0x0F, 0xB6, 0xEA };
    const std::uint8_t kChangeEquipEntry[] = { 0x40, 0x53, 0x55, 0x56, 0x57, 0x48, 0x81, 0xEC, 0x98, 0x00, 0x00, 0x00 };

    struct Hook
    {
        const char* what;
        const char* without;   // what stays as it was when the hook is missing
        void*       target;
        void*       orig;
    };

    Hook g_GetSlot{ "walker loadout slot getter", "the walker's equipment lists show the D-Walker's slots for the Battle Gear" };
    Hook g_SetSlot{ "walker loadout slot setter", "the walker's equipment lists show the D-Walker's slots for the Battle Gear" };
    Hook g_UsageCost{ "walker deploy cost", "the Battle Gear's equipment list shows the D-Walker's cost" };
    Hook g_SetupMode{ "item selector mode", "the iDroid's equipment entry opens Quiet's list for the Battle Gear" };
    Hook g_ChangeEquip{ "buddy equipment change", "picking a Battle Gear slot on the iDroid does not drop it in again" };

    struct DescribedParam
    {
        const std::uint8_t* param = nullptr;
        int                 slot = 0;
    };

    std::uintptr_t   g_SelectorCodeFirst = 0;   // the item selector's own functions
    std::uintptr_t   g_SelectorCodeEnd = 0;
    std::mutex       g_DescribedMutex;          // the Battle Gear params filled lately and the slot each describes: the menus ask
    DescribedParam   g_Described[kDescribedParams];   // for their cost and the model viewer shows them later
    int              g_DescribedNext = 0;
    std::atomic<std::uint16_t> g_ShownHead{ 0 };   // the head a Battle Gear menu put on the model: type, grade << 8

    bool InHeliSpace()
    {
        return MissionCodeGuard::GetCurrentMissionCode() / 10000 == kHeliSpaceBand;
    }

    // the item selector's walker list is the Battle Gear's while the Battle Gear is the buddy: in the ACC the one Sortie Prep has
    // selected, out in the field the one that is out. Nothing else that asks for a walker slot is: the D-Walker's own Customize
    // screens, its field call
    bool SelectorServesBattleGear(const void* caller)
    {
        const auto at = reinterpret_cast<std::uintptr_t>(caller);
        return at >= g_SelectorCodeFirst && at < g_SelectorCodeEnd && battleGear::IsBattleGearOut();
    }

    void NoteDescribed(const std::uint8_t* param, int slot)
    {
        std::lock_guard<std::mutex> lock(g_DescribedMutex);
        for (DescribedParam& d : g_Described)
        {
            if (d.param == param)
            {
                d.slot = slot;
                return;
            }
        }
        g_Described[g_DescribedNext] = { param, slot };
        g_DescribedNext = (g_DescribedNext + 1) % kDescribedParams;
    }

    bool DescribedSlot(const std::uint8_t* param, int* slot)
    {
        std::lock_guard<std::mutex> lock(g_DescribedMutex);
        for (const DescribedParam& d : g_Described)
        {
            if (d.param == param)
            {
                *slot = d.slot;
                return true;
            }
        }
        return false;
    }

    void Put(std::uint8_t* param, std::size_t at, std::uint32_t value)
    {
        *reinterpret_cast<std::uint32_t*>(param + at) = value;
    }

    void FillParamSEH(std::uint8_t* param, std::uint8_t mainType, std::uint8_t head)
    {
        __try
        {
            Put(param, kParamBuddyType, battleGear::kBuddyType);
            Put(param, kParamBodyType, 0);
            Put(param, kParamBodyGrade, 1);
            Put(param, kParamMainType, mainType);
            Put(param, kParamMainGrade, 1);
            Put(param, kParamSubType, battleGearArmory::kRoofGunType);
            Put(param, kParamSubGrade, 1);
            Put(param, kParamHeadType, head);
            Put(param, kParamHeadGrade, head == battleGearWeapons::kSupportHead ? 2 : 1);   // the stealth grade, as the Battle Gear wears it
            Put(param, kParamArmType, 0);
            Put(param, kParamArmGrade, 1);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    std::uint32_t ParamBuddyTypeSEH(const std::uint8_t* param)
    {
        __try
        {
            return *reinterpret_cast<const std::uint32_t*>(param + kParamBuddyType);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    bool ReadHeadSEH(const std::uint8_t* param, std::uint32_t& type, std::uint32_t& grade)
    {
        __try
        {
            type = *reinterpret_cast<const std::uint32_t*>(param + kParamHeadType);
            grade = *reinterpret_cast<const std::uint32_t*>(param + kParamHeadGrade);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::int32_t EquipKindSEH(const std::int32_t* equipKind)
    {
        __try
        {
            return *equipKind;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    std::uint32_t __fastcall hkGetSlot(void* utility)   // the Battle Gear's customize screens mark its current slot
    {
        if (battleGearCustomizePage::Active() || SelectorServesBattleGear(_ReturnAddress()))
            return static_cast<std::uint32_t>(battleGearArmory::CurrentLoadoutSlot());
        return reinterpret_cast<GetSlot_t>(g_GetSlot.orig)(utility);
    }

    void __fastcall hkSetSlot(void* utility, std::uint8_t slot)
    {
        if (!SelectorServesBattleGear(_ReturnAddress()))
        {
            reinterpret_cast<SetSlot_t>(g_SetSlot.orig)(utility, slot);
            return;
        }
        battleGearArmory::SetCurrentLoadoutSlot(slot);
        battleGearLoadoutSlots::ClearShown();   // the model wears the picked slot's head from now on
#ifdef _DEBUG
        LogDebug("BattleGear loadout: slot %u picked, it is the current one now", static_cast<unsigned>(slot));   // TEMP probe
#endif
    }

    std::uint32_t __fastcall hkUsageCost(void* utility, const std::uint8_t* param)
    {
        if (!param || ParamBuddyTypeSEH(param) != battleGear::kBuddyType)
            return reinterpret_cast<UsageCost_t>(g_UsageCost.orig)(utility, param);
        int slot = 0;
        if (DescribedSlot(param, &slot))
            return battleGearArmory::LoadoutDeployCost(slot);
        if (battleGearCustomizePage::Active())   // the open page's own param: what it has picked so far
            return battleGearArmory::LoadoutDeployCost(battleGearCustomizePage::PickedGuns());
        return battleGearArmory::LoadoutDeployCost(battleGearArmory::CurrentLoadoutSlot());
    }

    // out in the field the iDroid's equipment entry asks for "the buddy's list" and takes Quiet's for every buddy type it does not
    // know; the Battle Gear gets the walker's
    std::uint64_t __fastcall hkSetupMode(void* selector, char start, char keepTab, std::int32_t* equipKind)
    {
        std::int32_t walkerKind = kWalkerEquipKind;
        const bool battleGearList = start && equipKind && EquipKindSEH(equipKind) == kBuddyEquipKind && !InHeliSpace()
                                 && battleGear::IsBattleGearOut();
        return reinterpret_cast<SetupMode_t>(g_SetupMode.orig)(selector, start, keepTab, battleGearList ? &walkerKind : equipKind);
    }

    // the game's own change writes the picked slot's gear and calls the D-Walker in again; the Battle Gear's picked slot is its
    // current one already (the selector set it just before), so it only drops in again
    void __fastcall hkChangeEquip(void* service, const std::uint8_t* param)
    {
        if (param && ParamBuddyTypeSEH(param) == battleGear::kBuddyType && !InHeliSpace() && battleGear::IsBattleGearOut())
        {
            const bool dropped = battleGear::Redeploy();
            LogDebug("BattleGear loadout: equipment change to slot %d, dropping in again: %s", battleGearArmory::CurrentLoadoutSlot(),
                     dropped ? "called" : "refused");
            return;
        }
        reinterpret_cast<ChangeEquip_t>(g_ChangeEquip.orig)(service, param);
    }

    bool HookOne(Hook& h, std::uintptr_t address, void* detour, const std::uint8_t* entry, std::size_t entrySize, std::size_t callRel)
    {
        void* target = address ? ResolveGameAddress(address) : nullptr;
        if (!target)
        {
            LogError("BattleGear: %s address missing on this build, %s", h.what, h.without);
            return false;
        }
        std::uint8_t mask[32];
        std::memset(mask, 1, sizeof(mask));
        if (callRel)
            std::memset(mask + callRel, 0, 4);
        const HookEntry state = CheckHookEntryMasked(target, entry, mask, entrySize);
        if (state == HookEntry::Unknown)
        {
            char desc[160] = {};
            DescribeFunctionEntry(target, desc, sizeof(desc));
            LogError("BattleGear: %s differs on this build (%s), %s", h.what, desc, h.without);
            return false;
        }
        if (state == HookEntry::ForeignHook)
            LogHookedOnTop(h.what, target);
        if (!CreateAndEnableHook(target, detour, &h.orig))
        {
            LogError("BattleGear: %s hook failed, %s", h.what, h.without);
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
}

namespace battleGearLoadoutSlots
{
    bool Install()
    {
        static_assert(sizeof(kGetSlotEntry) <= 32 && sizeof(kSetSlotEntry) <= 32 && sizeof(kUsageCostEntry) <= 32
                      && sizeof(kSetupModeEntry) <= 32, "entry masks");
        if (g_GetSlot.target)
            return true;
        const auto first = reinterpret_cast<std::uintptr_t>(gAddr.ItemSelector_AddListBandana ? ResolveGameAddress(gAddr.ItemSelector_AddListBandana)
                                                                                              : nullptr);
        const auto last = reinterpret_cast<std::uintptr_t>(
            gAddr.ItemSelectorRecordCallFunc_UpdateRecords ? ResolveGameAddress(gAddr.ItemSelectorRecordCallFunc_UpdateRecords) : nullptr);
        if (!first || !last || last <= first)
        {
            LogError("BattleGear: item selector addresses missing on this build, %s", g_GetSlot.without);
            return false;
        }
        g_SelectorCodeFirst = first;
        g_SelectorCodeEnd = last + kUpdateRecordsSize;
        // the getter and the setter go together: one without the other would show one buddy's slot and set the other's
        if (!HookOne(g_GetSlot, gAddr.CustomizeUtilityImpl_GetWalkerLoadoutSlot, reinterpret_cast<void*>(&hkGetSlot), kGetSlotEntry,
                     sizeof(kGetSlotEntry), kGetSlotCallRel)
            || !HookOne(g_SetSlot, gAddr.CustomizeUtilityImpl_SetWalkerLoadoutSlot, reinterpret_cast<void*>(&hkSetSlot), kSetSlotEntry,
                        sizeof(kSetSlotEntry), kSetSlotCallRel))
        {
            Uninstall();
            return false;
        }
        HookOne(g_UsageCost, gAddr.CustomizeUtilityImpl_GetBuddyDWalkerUsageCost, reinterpret_cast<void*>(&hkUsageCost), kUsageCostEntry,
                sizeof(kUsageCostEntry), 0);
        HookOne(g_SetupMode, gAddr.ItemSelectorCallbackImpl_SetupMode, reinterpret_cast<void*>(&hkSetupMode), kSetupModeEntry,
                sizeof(kSetupModeEntry), 0);
        HookOne(g_ChangeEquip, gAddr.BuddyServiceImpl_ChangeBuddyEquipment, reinterpret_cast<void*>(&hkChangeEquip), kChangeEquipEntry,
                sizeof(kChangeEquipEntry), 0);
        return true;
    }

    void Uninstall()
    {
        UnhookOne(g_ChangeEquip);
        UnhookOne(g_SetupMode);
        UnhookOne(g_UsageCost);
        UnhookOne(g_SetSlot);
        UnhookOne(g_GetSlot);
    }

    void AfterGetParam(std::uint8_t buddyType, std::uint8_t* param, std::uint8_t slot, const void* caller)
    {
        if (!param || !g_GetSlot.target)
            return;
        const bool own = buddyType == battleGear::kBuddyType;   // Sortie Prep's Battle Gear rows: the game passes no slot with them
        const bool walkerList = buddyType == kWalkerGearType   // the slot screen in front of the Battle Gear's page, the item selector
                             && (battleGearCustomizePage::Active() || SelectorServesBattleGear(caller));
        if (!own && !walkerList)
            return;
        const int described = own || slot >= battleGearArmory::kLoadoutSlots ? battleGearArmory::CurrentLoadoutSlot() : slot;
        const battleGearArmory::Loadout loadout = battleGearArmory::LoadoutSlot(described);
        battleGearArmory::MainWeapon main;
        const bool known = loadout.mainWeapon && battleGearArmory::TryGetByValue(loadout.mainWeapon, &main);
        NoteDescribed(param, described);
        FillParamSEH(param, known ? main.fireType : battleGearArmory::kRailGun, loadout.head ? loadout.head : battleGearWeapons::kSupportHead);
    }

    void ShowOnModel(const std::uint8_t* param)
    {
        std::uint32_t type = 0;
        std::uint32_t grade = 0;
        if (!param || !ReadHeadSEH(param, type, grade) || type == 0 || type > 0xFF || grade > 0xFF)
            return;
        g_ShownHead.store(static_cast<std::uint16_t>(type | grade << 8), std::memory_order_relaxed);
    }

    std::uint8_t ShownHeadType()
    {
        return static_cast<std::uint8_t>(g_ShownHead.load(std::memory_order_relaxed) & 0xFF);
    }

    std::uint8_t ShownHeadGrade()
    {
        return static_cast<std::uint8_t>(g_ShownHead.load(std::memory_order_relaxed) >> 8);
    }

    void ClearShown()
    {
        g_ShownHead.store(0, std::memory_order_relaxed);
    }
}
