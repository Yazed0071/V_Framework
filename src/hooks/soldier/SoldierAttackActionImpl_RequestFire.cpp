#include <pch.h>
#include "SoldierAttackActionImpl_RequestFire.h"

#include <Windows.h>
#include <cstddef>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"
#include "RideHeliActionImpl_State_GetInAction.h"

namespace
{
    using RequestFire_t = void(__fastcall*)(void* impl, std::uint32_t realizedSlot, std::uint8_t* request, const float* direction, bool flag);
    using QuarkTable_t  = std::uint8_t*(__fastcall*)();
    using EquipType_t   = std::int32_t(__fastcall*)(void* equipTypes, std::uint16_t equipId);

    constexpr std::size_t   kImplWork          = 0x1C0;   // 0x2F0 per realized soldier
    constexpr std::size_t   kImplWorkBase      = 0x1DC;   // realized slot of entry 0
    constexpr std::size_t   kWorkSize          = 0x2F0;
    constexpr std::size_t   kWorkSoldier       = 0x2A0;
    constexpr std::size_t   kImplSoldiers      = 0x78;    // the Soldier2 part
    constexpr std::size_t   kPartEquip         = 0xA0;    // EquipController: +8 eight bytes per soldier (slot u8 at +6), +0x10 three equip ids per soldier
    constexpr std::size_t   kQuarkEquip        = 0x98;    // QuarkSystemTable -> +0x1E8 the equip system, whose +8 knows each equip id's type
    constexpr std::size_t   kEquipSystem       = 0x1E8;
    constexpr std::int32_t  kEquipTypeGrenadeLauncher = 7;
    constexpr std::int32_t  kEquipTypeMissile         = 8;
    constexpr std::size_t   kRequestIgnoreObject = 0x2C;  // u16 game object id the shell flies through, 0xFFFF none

    RequestFire_t g_OrigRequestFire = nullptr;
    QuarkTable_t  g_QuarkTable      = nullptr;
    void*         g_Target          = nullptr;
    bool          g_Installed       = false;

    std::uint32_t SoldierOfSeh(std::uint8_t* impl, std::uint32_t realizedSlot)
    {
        __try
        {
            const std::uint32_t entry = realizedSlot - static_cast<std::uint32_t>(*reinterpret_cast<std::int32_t*>(impl + kImplWorkBase));
            return *reinterpret_cast<std::uint32_t*>(*reinterpret_cast<std::uint8_t**>(impl + kImplWork) + static_cast<std::size_t>(entry) * kWorkSize + kWorkSoldier);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0xFFFFFFFFu;
        }
    }

    bool FiresShellsSeh(std::uint8_t* impl, std::uint32_t index)
    {
        __try
        {
            auto* equip = *reinterpret_cast<std::uint8_t**>(*reinterpret_cast<std::uint8_t**>(impl + kImplSoldiers) + kPartEquip);
            const std::uint8_t slot = (*reinterpret_cast<std::uint8_t**>(equip + 8))[static_cast<std::size_t>(index) * 8 + 6];
            const std::uint16_t equipId = *reinterpret_cast<std::uint16_t*>(*reinterpret_cast<std::uint8_t**>(equip + 0x10) + (static_cast<std::size_t>(index) * 3 + slot) * 2);
            auto* equipSystem = *reinterpret_cast<std::uint8_t**>(*reinterpret_cast<std::uint8_t**>(g_QuarkTable() + kQuarkEquip) + kEquipSystem);
            void* types = *reinterpret_cast<void**>(equipSystem + 8);
            const std::int32_t type = reinterpret_cast<EquipType_t>((*reinterpret_cast<void***>(types))[0])(types, equipId);
            return type == kEquipTypeGrenadeLauncher || type == kEquipTypeMissile;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // a door rider's rocket or grenade flies through his own heli, as GZ hands the heli along with a rider's shot
    void __fastcall hkRequestFire(void* impl, std::uint32_t realizedSlot, std::uint8_t* request, const float* direction, bool flag)
    {
        if (request && SoldierRideHeli_AnyRider() && !MissionCodeGuard::ShouldBypassHooks())
        {
            auto* self = static_cast<std::uint8_t*>(impl);
            const std::uint32_t index = SoldierOfSeh(self, realizedSlot);
            const std::uint16_t heli = index < 0x200 ? SoldierRideHeli_Heli(index) : 0;
            if (heli != 0 && FiresShellsSeh(self, index))
                *reinterpret_cast<std::uint16_t*>(request + kRequestIgnoreObject) = heli;
        }
        g_OrigRequestFire(impl, realizedSlot, request, direction, flag);
    }
}

bool Install_SoldierAttackActionImpl_RequestFire_Hook()
{
    if (g_Installed)
        return true;

    g_QuarkTable = gAddr.GetQuarkSystemTable ? reinterpret_cast<QuarkTable_t>(ResolveGameAddress(gAddr.GetQuarkSystemTable)) : nullptr;
    g_Target     = gAddr.SoldierAttackActionImpl_RequestFire ? ResolveGameAddress(gAddr.SoldierAttackActionImpl_RequestFire) : nullptr;
    if (!g_QuarkTable || !g_Target)
    {
        LogError("SoldierAttackActionImpl_RequestFire: address missing for this build, heli riders' rockets can hit their own heli");
        g_Target = nullptr;
        return false;
    }
    if (!CreateAndEnableHook(g_Target, reinterpret_cast<void*>(&hkRequestFire), reinterpret_cast<void**>(&g_OrigRequestFire)))
    {
        LogError("SoldierAttackActionImpl_RequestFire: hook failed, heli riders' rockets can hit their own heli");
        g_Target = nullptr;
        return false;
    }
    g_Installed = true;
    return true;
}

bool Uninstall_SoldierAttackActionImpl_RequestFire_Hook()
{
    if (!g_Installed)
        return true;
    DisableAndRemoveHook(g_Target);
    g_Target          = nullptr;
    g_OrigRequestFire = nullptr;
    g_Installed       = false;
    return true;
}
