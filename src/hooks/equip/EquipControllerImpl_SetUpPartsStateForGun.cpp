#include <pch.h>

#include "EquipControllerImpl_SetUpPartsStateForGun.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"
#include "LaserSightSwitch.h"

namespace
{
    using SetUpPartsStateForGun_t = void(__fastcall*)(void* self, std::int32_t slot, std::uint16_t* word,
                                                      std::uint16_t* life, std::int16_t* lifeMax,
                                                      char underBarrel, float lifeScale);
    using GetGunInfo_t = std::uint8_t*(__fastcall*)(void* equipSystem, std::uint32_t handle);

    constexpr std::size_t   kSelfInventory      = 0x28;
    constexpr std::size_t   kSelfPlayerManager  = 0x38;
    constexpr std::size_t   kSelfEquipSystem    = 0x48;
    constexpr std::size_t   kManagerCurrentSet  = 0x214;
    constexpr std::size_t   kInventorySlotCount = 0x0C;
    constexpr std::size_t   kInventoryHandles   = 0x40;
    constexpr std::uint32_t kHandleKindMask     = 0x1F;
    constexpr std::uint32_t kHandleKindNone     = 0;
    constexpr std::uint32_t kHandleKindNoParts  = 9;        // the game builds no option word for this kind either
    constexpr std::size_t   kGetGunInfoSlot     = 0x150;
    constexpr std::size_t   kGunInfoFlags       = 0x8A;
    constexpr std::uint16_t kGunHasLaser        = 0x100;
    constexpr int           kShownSlots         = 3;        // primary 1, primary 2, pistol
    constexpr int           kTemporarySlot      = 5;        // the HUD keeps this one apart from the three above
    constexpr std::size_t   kHudWeaponIds       = 0x04;
    constexpr std::size_t   kHudOptionWords     = 0xC0;
    constexpr std::size_t   kHudTemporaryWord   = 0xC6;
    constexpr std::size_t   kHudTemporaryId     = 0x50;

    const std::uint8_t kPrologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20,
        0x41, 0x54, 0x41, 0x56, 0x41, 0x57 };

    SetUpPartsStateForGun_t g_Orig = nullptr;
    void*                   g_Target = nullptr;

    std::uint32_t LaserGunInSlotSEH(std::uint8_t* self, std::int32_t slot, std::uint16_t* word)
    {
        __try
        {
            auto* inventory = *reinterpret_cast<std::uint8_t**>(self + kSelfInventory);
            auto* manager = *reinterpret_cast<std::uint8_t**>(self + kSelfPlayerManager);
            void* equipSystem = *reinterpret_cast<void**>(self + kSelfEquipSystem);
            if (!inventory || !manager || !equipSystem)
                return 0;
            const std::uint32_t set = *reinterpret_cast<std::uint32_t*>(manager + kManagerCurrentSet);
            const std::uint32_t perSet = *reinterpret_cast<std::uint32_t*>(inventory + kInventorySlotCount);
            auto* handles = *reinterpret_cast<std::uint32_t**>(inventory + kInventoryHandles);
            if (!handles)
                return 0;
            const std::uint32_t handle = handles[perSet * set + static_cast<std::uint32_t>(slot)];
            const std::uint32_t kind = handle & kHandleKindMask;
            if (kind == kHandleKindNone || kind == kHandleKindNoParts)
                return 0;

            auto** vtable = *reinterpret_cast<void***>(equipSystem);
            const std::uint8_t* gunInfo = reinterpret_cast<GetGunInfo_t>(vtable[kGetGunInfoSlot / sizeof(void*)])(equipSystem, handle);
            if (!gunInfo || (*reinterpret_cast<const std::uint16_t*>(gunInfo + kGunInfoFlags) & kGunHasLaser) == 0)
                return 0;

            if (slot == kTemporarySlot)
                return *reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uint8_t*>(word) - kHudTemporaryWord + kHudTemporaryId);
            auto* hud = reinterpret_cast<std::uint8_t*>(word) - kHudOptionWords - 2 * slot;
            return *reinterpret_cast<std::uint32_t*>(hud + kHudWeaponIds + 4 * slot);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    void WriteLaserBitsSEH(std::uint16_t* word, bool on)
    {
        __try
        {
            std::uint16_t value = static_cast<std::uint16_t>(*word | LaserSightSwitch::kHudHasLaser);
            if (on)
                value = static_cast<std::uint16_t>(value | LaserSightSwitch::kHudLaserOn);
            *word = value;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void __fastcall hkSetUpPartsStateForGun(void* self, std::int32_t slot, std::uint16_t* word,
                                            std::uint16_t* life, std::int16_t* lifeMax,
                                            char underBarrel, float lifeScale)
    {
        g_Orig(self, slot, word, life, lifeMax, underBarrel, lifeScale);
        if (!self || !word || slot < 0 || (slot >= kShownSlots && slot != kTemporarySlot) || MissionCodeGuard::ShouldBypassHooks())
            return;
        const std::uint32_t weaponId = LaserGunInSlotSEH(static_cast<std::uint8_t*>(self), slot, word);
        if (weaponId == 0)
            return;
        const std::int32_t equipId = LaserSightSwitch::EquipIdOfEngineId(weaponId);
        LaserSightSwitch::KeepSaved(equipId);
        WriteLaserBitsSEH(word, !LaserSightSwitch::IsOff(equipId));
    }
}

bool Install_EquipControllerImpl_SetUpPartsStateForGun_Hook()
{
    void* fn = LaserSightSwitch::CheckedEntry(gAddr.EquipControllerImpl_SetUpPartsStateForGun,
                                              kPrologue, sizeof(kPrologue), "EquipControllerImpl::SetUpPartsStateForGun");
    if (!fn)
        return false;
    if (!CreateAndEnableHook(fn, reinterpret_cast<void*>(&hkSetUpPartsStateForGun), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("LaserSight: SetUpPartsStateForGun hook failed, no LASER tag or LASER box");
        return false;
    }
    g_Target = fn;
    return true;
}

void Uninstall_EquipControllerImpl_SetUpPartsStateForGun_Hook()
{
    if (g_Target)
    {
        DisableAndRemoveHook(g_Target);
        g_Target = nullptr;
    }
    g_Orig = nullptr;
}
