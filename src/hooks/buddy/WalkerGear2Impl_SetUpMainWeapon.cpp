#include <pch.h>

#include "WalkerGear2Impl_SetUpMainWeapon.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>

#include "AddressSet.h"
#include "BattleGearBuddy.h"
#include "BattleGearWeapons.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using SetUpMainWeapon_t = void(__fastcall*)(void* walker, std::uint32_t index, std::uint32_t walkerId);
    using SlotOfWalker_t    = std::uint32_t(__fastcall*)(void* walkerInterface, std::uint32_t walkerId);
    using SetVisibility_t   = void(__fastcall*)(void* parts, std::uint32_t slot, bool visible);
    using SetWorldMatrix_t  = void(__fastcall*)(void* parts, std::uint32_t slot, const float* matrix, std::uint32_t flags);
    using UpdateSlot_t      = void(__fastcall*)(void* parts, std::uint32_t slot);

    constexpr std::size_t   kInterface        = 0x20;    // the walker's object interface, type id at +0x08
    constexpr std::size_t   kInterfaceType    = 0x08;
    constexpr std::size_t   kParts            = 0xE0;    // SimplePartsControllerImpl
    constexpr std::size_t   kMainWeaponSlotOf = 0x120;   // interface vtable: parts slot of a walker's main gun
    constexpr std::size_t   kSetVisibility    = 0xB0;    // parts vtable: SetVisibility(slot, visible)
    constexpr std::size_t   kSetWorldMatrix   = 0x90;    // parts vtable: where a slot's model sits,
    constexpr std::size_t   kUpdateSlot       = 0x208;   // then its update over the slot's frame time
    constexpr std::size_t   kApplySlot        = 0x210;
    constexpr std::size_t   kPartsFrameTimes  = 0x18;    // parts: one float per slot
    constexpr std::size_t   kPartsModels      = 0x58;    // parts: one model pointer per slot
    constexpr std::size_t   kAttachmentSlotOf = 0x118;   // interface vtable: parts slot of the left gun mount
    constexpr std::size_t   kArmSlotOf        = 0x128;   // interface vtable: right arm (knife)
    constexpr std::size_t   kArmWeaponSlotOf  = 0x130;   // interface vtable: arm sidearm (ZE'EV)
    constexpr std::size_t   kModelHideMask    = 0x1A4;   // model: 0xFFFFFFFF while SetVisibility(false) holds
    constexpr std::uint32_t kModelHidden      = 0xFFFFFFFF;
    constexpr std::uint32_t kNoSlot           = 0xFFFFFFFF;
    constexpr std::uint16_t kBuddyWalkerType  = 0x2C;

    const std::uint8_t kSetUpMainWeaponEntry[] = { 0x48, 0x89, 0x5C, 0x24, 0x20, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56,
                                                   0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xF0, 0x48, 0x81, 0xEC, 0x10, 0x01, 0x00, 0x00 };

    SetUpMainWeapon_t g_Orig = nullptr;
    void*             g_Target = nullptr;
    std::atomic<std::uint32_t> g_WalkerId{ 0xFFFFFFFF };   // walker id of the last buddy walker set up

    struct WalkerParts
    {
        std::uint8_t*   walkerInterface = nullptr;
        std::uint8_t*   parts = nullptr;
        SlotOfWalker_t  slotOf = nullptr;
        SetVisibility_t setVisibility = nullptr;
    };

    bool ReadBuddyWalkerSEH(void* walker, WalkerParts& out)
    {
        __try
        {
            auto* base = static_cast<std::uint8_t*>(walker);
            out.walkerInterface = base + kInterface;
            if (*reinterpret_cast<const std::uint16_t*>(out.walkerInterface + kInterfaceType) != kBuddyWalkerType)
                return false;
            out.parts = *reinterpret_cast<std::uint8_t**>(base + kParts);
            if (!out.parts)
                return false;
            out.slotOf = *reinterpret_cast<SlotOfWalker_t*>(*reinterpret_cast<std::uint8_t**>(out.walkerInterface) + kMainWeaponSlotOf);
            out.setVisibility = *reinterpret_cast<SetVisibility_t*>(*reinterpret_cast<std::uint8_t**>(out.parts) + kSetVisibility);
            return out.slotOf && out.setVisibility;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool HasModelSEH(const std::uint8_t* parts, std::uint32_t slot)
    {
        __try
        {
            const auto* const* models = *reinterpret_cast<const void* const* const*>(parts + kPartsModels);
            return models && models[slot];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool HideSlotSEH(const WalkerParts& w, std::size_t slotOfOffset, std::uint32_t walkerId)
    {
        __try
        {
            const auto slotOf = *reinterpret_cast<SlotOfWalker_t*>(*reinterpret_cast<std::uint8_t**>(w.walkerInterface) + slotOfOffset);
            const std::uint32_t slot = slotOf(w.walkerInterface, walkerId);
            if (slot == kNoSlot || !HasModelSEH(w.parts, slot))
                return false;
            const auto* const* models = *reinterpret_cast<const std::uint8_t* const* const*>(w.parts + kPartsModels);
            if (*reinterpret_cast<const std::uint32_t*>(models[slot] + kModelHideMask) == kModelHidden)
                return false;
            w.setVisibility(w.parts, slot, false);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void HideBattleGearParts(const WalkerParts& w, std::uint32_t walkerId)
    {
        HideSlotSEH(w, kAttachmentSlotOf, walkerId);   // left mount, the main gun hangs on it
        HideSlotSEH(w, kArmSlotOf, walkerId);          // right arm with the knife
        HideSlotSEH(w, kArmWeaponSlotOf, walkerId);    // the ZE'EV tucked in the hull
        if (battleGearWeapons::HideMainGunAtSetup())
            HideSlotSEH(w, kMainWeaponSlotOf, walkerId);   // the Battle Gear fires from its own barrels
    }

    void __fastcall hkSetUpMainWeapon(void* walker, std::uint32_t index, std::uint32_t walkerId)
    {
        g_Orig(walker, index, walkerId);
        if (!walker)
            return;
        WalkerParts w{};
        if (!ReadBuddyWalkerSEH(walker, w))
            return;
        g_WalkerId.store(walkerId, std::memory_order_release);
        if (battleGear::HoldsAnotherBuddy())   // the ACC shows the D-Walker while the Battle Gear is the sortie pick
            return;
        if (battleGear::IsBattleGearOut() || battleGearWeapons::HideMainGunAtSetup())   // out in the field, or the Customize preview
            HideBattleGearParts(w, walkerId);
    }
}

void KeepBattleGearWalkerPartsHidden(void* walkerInterface)
{
    const std::uint32_t walkerId = g_WalkerId.load(std::memory_order_acquire);
    if (!walkerInterface || walkerId == kNoSlot)
        return;
    WalkerParts w{};
    __try
    {
        w.walkerInterface = static_cast<std::uint8_t*>(walkerInterface);
        w.parts = *reinterpret_cast<std::uint8_t**>(w.walkerInterface + (kParts - kInterface));
        if (!w.parts)
            return;
        w.setVisibility = *reinterpret_cast<SetVisibility_t*>(*reinterpret_cast<std::uint8_t**>(w.parts) + kSetVisibility);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return;
    }
    if (w.setVisibility)
        HideBattleGearParts(w, walkerId);
}

void PlaceBattleGearMainGun(void* walkerInterface, const float* matrix)
{
    const std::uint32_t walkerId = g_WalkerId.load(std::memory_order_acquire);
    if (!walkerInterface || walkerId == kNoSlot)
        return;
    __try
    {
        auto* base = static_cast<std::uint8_t*>(walkerInterface);
        auto* parts = *reinterpret_cast<std::uint8_t**>(base + (kParts - kInterface));
        if (!parts)
            return;
        const auto slotOf = *reinterpret_cast<SlotOfWalker_t*>(*reinterpret_cast<std::uint8_t**>(base) + kMainWeaponSlotOf);
        const std::uint32_t slot = slotOf(base, walkerId);
        if (slot == kNoSlot || !HasModelSEH(parts, slot))
            return;
        std::uint8_t* vtable = *reinterpret_cast<std::uint8_t**>(parts);
        (*reinterpret_cast<SetWorldMatrix_t*>(vtable + kSetWorldMatrix))(parts, slot, matrix, 0);
        (*reinterpret_cast<float**>(parts + kPartsFrameTimes))[slot] = 0.0f;   // the game stepped it this frame already
        (*reinterpret_cast<UpdateSlot_t*>(vtable + kUpdateSlot))(parts, slot);
        (*reinterpret_cast<UpdateSlot_t*>(vtable + kApplySlot))(parts, slot);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

bool Install_WalkerGear2Impl_SetUpMainWeapon_Hook()
{
    if (g_Target)
        return true;
    void* target = ResolveGameAddress(gAddr.WalkerGear2Impl_SetUpMainWeapon);
    if (!target)
    {
        LogError("BattleGear: walker main gun setup address missing on this build, the D-Walker gun shows on the Battle Gear");
        return false;
    }
    const HookEntry state = CheckHookEntry(target, kSetUpMainWeaponEntry, sizeof(kSetUpMainWeaponEntry));
    if (state == HookEntry::Unknown)
    {
        char desc[160] = {};
        DescribeFunctionEntry(target, desc, sizeof(desc));
        LogError("BattleGear: walker main gun setup code differs on this build (%s), the D-Walker gun shows on the Battle Gear", desc);
        return false;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("BattleGear: walker SetUpMainWeapon", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkSetUpMainWeapon), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("BattleGear: walker main gun setup hook failed, the D-Walker gun shows on the Battle Gear");
        return false;
    }
    g_Target = target;
    return true;
}

void Uninstall_WalkerGear2Impl_SetUpMainWeapon_Hook()
{
    if (!g_Target)
        return;
    DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig = nullptr;
}
