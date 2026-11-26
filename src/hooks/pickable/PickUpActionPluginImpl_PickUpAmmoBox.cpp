#include <pch.h>
#include "PickUpActionPluginImpl_PickUpAmmoBox.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "../equip/EquipControllerImpl_UpdateBattery.h"
#include "../equip/ItemCount.h"

namespace
{
    using PickUpAmmoBox_t        = std::uint64_t(__fastcall*)(std::uint8_t* self, std::uint32_t playerIndex, std::uint16_t handle, std::uint16_t* pickableInfo);
    using DeleteByPlayerPickup_t = void(__fastcall*)(void* pickableSystem, std::uint16_t handle);

    constexpr std::uint16_t kEquipIdMask              = 0x7FF;
    constexpr std::uint16_t kItemBox                  = 0x1B7;   // the supply drop's box for items and fultons
    constexpr std::size_t   kPluginPickableSystem     = 0x68;
    constexpr std::size_t   kDeleteByPlayerPickupSlot = 0x58;

    const std::uint8_t kPrologue[] = { 0x66, 0x44, 0x89, 0x44, 0x24, 0x18, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41,
                                       0x56, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xE1, 0x48, 0x81, 0xEC, 0xE8, 0x00, 0x00, 0x00 };

    PickUpAmmoBox_t g_Orig   = nullptr;
    void*           g_Target = nullptr;

    std::uint16_t BoxIdSEH(const std::uint16_t* pickableInfo)
    {
        __try
        {
            return pickableInfo ? static_cast<std::uint16_t>(*pickableInfo & kEquipIdMask) : 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }

    void DeleteBoxSEH(std::uint8_t* self, std::uint16_t handle)   // the call the game makes itself after a pick-up; it only sets the box's delete flags, so a second call changes nothing
    {
        __try
        {
            void* pickableSystem = *reinterpret_cast<void**>(self + kPluginPickableSystem);
            if (!pickableSystem)
                return;
            const auto deleteByPlayerPickup = reinterpret_cast<DeleteByPlayerPickup_t>(
                (*reinterpret_cast<void***>(pickableSystem))[kDeleteByPlayerPickupSlot / sizeof(void*)]);
            deleteByPlayerPickup(pickableSystem, handle);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    std::uint64_t __fastcall hkPickUpAmmoBox(std::uint8_t* self, std::uint32_t playerIndex, std::uint16_t handle, std::uint16_t* pickableInfo)
    {
        const std::uint16_t box = BoxIdSEH(pickableInfo);
        const std::uint64_t result = g_Orig(self, playerIndex, handle, pickableInfo);
        if (box == kItemBox && self && ItemCount::Any() && ItemBattery::IsActiveHere() && ItemCount::RefillCarried())
            DeleteBoxSEH(self, handle);   // the game leaves the box when its own items were full
        return result;
    }
}

bool Install_PickUpActionPluginImpl_PickUpAmmoBox_Hook()
{
    void* target = gAddr.PickUpActionPluginImpl_PickUpAmmoBox ? ResolveGameAddress(gAddr.PickUpActionPluginImpl_PickUpAmmoBox) : nullptr;
    if (!target)
    {
        LogError("ItemCount: PickUpAmmoBox address missing for %s, supply drops do not refill counted custom items", GetGameBuildName(gGameBuild));
        return false;
    }
    const HookEntry entry = CheckHookEntry(target, kPrologue, sizeof(kPrologue));
    if (entry == HookEntry::Unknown)
    {
        LogError("ItemCount: PickUpActionPluginImpl::PickUpAmmoBox does not match this build, supply drops do not refill counted custom items");
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("ItemCount: PickUpActionPluginImpl::PickUpAmmoBox", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkPickUpAmmoBox), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("ItemCount: PickUpAmmoBox hook failed, supply drops do not refill counted custom items");
        return false;
    }
    g_Target = target;
    return true;
}

void Uninstall_PickUpActionPluginImpl_PickUpAmmoBox_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig   = nullptr;
}
