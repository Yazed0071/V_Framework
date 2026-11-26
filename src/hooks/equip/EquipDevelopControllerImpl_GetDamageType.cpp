#include <pch.h>
#include "EquipDevelopControllerImpl_GetDamageType.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "DevelopArrayGrow.h"
#include "EquipDevelop_SetEquipUndeveloped.h"
#include "EquipDevelopControllerImpl_GetEquipIDFromDevelopIndex.h"
#include "EquipParameterTablesImpl_ReloadEquipParameterTablesImpl.h"
#include "EquipPartParams.h"
#include "HookUtils.h"
#include "TppEquip_ReloadEquipIdTable.h"
#include "log.h"
#include "../heli/SupportHeliParts.h"
#include "../heli/SupportHeliWeapon.h"

namespace
{
    using GetDamageType_t  = std::uint8_t(__fastcall*)(void* controller, std::uint16_t developIndex);
    using DevelopIdToRow_t = std::uint16_t(__fastcall*)(void* controller, std::uint32_t developId);

    constexpr std::size_t   kDevelopRowSize     = 0x68;
    constexpr std::size_t   kRowDevelopId       = 0x8;
    constexpr std::size_t   kSlotDevelopIdToRow = 0xE0;
    constexpr std::uint16_t kDamageFlagTranq    = 1u << 3;
    constexpr std::uint16_t kDamageFlagStun     = 1u << 4;
    constexpr std::uint8_t  kTagDamage          = 1;
    constexpr std::uint8_t  kTagSleep           = 2;
    constexpr std::uint8_t  kTagStun            = 3;
    constexpr std::uint32_t kSmokeGrenadeDevelopId = 10050;   // EQP_SWP_SmokeGrenade, its cell shows the smoke tag
    constexpr std::uint32_t kGatlingDevelopId      = 33001;   // GATLING-GUN, the heli's own door guns

    GetDamageType_t g_Orig   = nullptr;
    void*           g_Target = nullptr;

    std::uint32_t DevelopIdOfRow(const void* controller, std::uint16_t developIndex)
    {
        __try
        {
            const auto* row = static_cast<const std::uint8_t*>(controller) + developIndex * kDevelopRowSize;
            return *reinterpret_cast<const std::uint16_t*>(row + kRowDevelopId);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    bool TagFromAttack(std::uint16_t attackId, std::uint8_t& damageType)
    {
        std::uint16_t lethalDamage = 0;
        std::uint16_t flags        = 0;
        if (attackId == 0 || !EquipParam_GetDamageKind(attackId, lethalDamage, flags))
            return false;
        if (flags & kDamageFlagTranq)
            damageType = kTagSleep;
        else if (flags & kDamageFlagStun)
            damageType = kTagStun;
        else if (lethalDamage > 0)
            damageType = kTagDamage;
        else
            return false;
        return true;
    }

    bool VanillaDamageTypeOf(void* controller, std::uint32_t developId, std::uint8_t& damageType)
    {
        void** vtable = *static_cast<void***>(controller);
        auto developIdToRow = reinterpret_cast<DevelopIdToRow_t>(vtable[kSlotDevelopIdToRow / sizeof(void*)]);
        const std::uint16_t index = developIdToRow(controller, developId);
        if (!equip::IsValidFlowIndex(index))
            return false;
        damageType = g_Orig(controller, index);
        return true;
    }

    bool PequodWeaponDamageType(void* controller, std::uint16_t developIndex, std::uint8_t& damageType)
    {
        const std::uint32_t developId = DevelopIdOfRow(controller, developIndex);
        SupportHeliParts::Part part{};
        if (SupportHeliParts::FindByDevelopId(developId, part))
        {
            if (part.kind == SupportHeliParts::Kind::Outfit)
                return false;
            return (part.attackId >= 0 && TagFromAttack(static_cast<std::uint16_t>(part.attackId), damageType))
                || VanillaDamageTypeOf(controller, kGatlingDevelopId, damageType);
        }
        SupportHeliWeapon::Weapon weapon{};
        if (!SupportHeliWeapon::FindByDevelopId(developId, weapon))
            return false;
        if (weapon.cloudShell == SupportHeliWeapon::kSleepCloudShell)
        {
            damageType = kTagSleep;
            return true;
        }
        if (weapon.cloudShell == SupportHeliWeapon::kSmokeCloudShell)
            return VanillaDamageTypeOf(controller, kSmokeGrenadeDevelopId, damageType);
        if (TagFromAttack(weapon.attackId, damageType))
            return true;
        const std::uint32_t baseDevelopId = EquipDevelop_GetDevelopParent(developId);
        return baseDevelopId != 0 && VanillaDamageTypeOf(controller, baseDevelopId, damageType);
    }

    int EquipIdOfRow(const void* controller, std::uint16_t developIndex)
    {
        __try
        {
            const auto* row = static_cast<const std::uint8_t*>(controller) + developIndex * kDevelopRowSize;
            return equip::DevelopRecordEquipId(row);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
    }

    std::uint8_t __fastcall hkGetDamageType(void* controller, std::uint16_t developIndex)
    {
        if (equip::IsValidFlowIndex(developIndex))
        {
            std::uint8_t pequodDamageType = 0;
            if (PequodWeaponDamageType(controller, developIndex, pequodDamageType))
                return pequodDamageType;
            const int equipId = EquipIdOfRow(controller, developIndex);
            const int swpId = equipId > 0 ? TppEquip_GetSupportWeaponIdForEquipId(equipId) : -1;
            if (swpId >= 0)
            {
                const int damageType = EquipParameterTables::SupportWeaponDamageType(static_cast<std::uint32_t>(swpId));
                if (damageType != EquipParameterTables::kAutomaticDamageType)
                    return static_cast<std::uint8_t>(damageType);
            }
        }
        return g_Orig(controller, developIndex);
    }
}

bool Install_EquipDevelopControllerImpl_GetDamageType_Hook()
{
    g_Target = ResolveGameAddress(gAddr.EquipDevelopControllerImpl_GetDamageType);
    if (!g_Target)
    {
        LogError("SupportWeapon: GetDamageType address missing for this build, damageType is ignored");
        return true;
    }
    if (!CreateAndEnableHook(g_Target, reinterpret_cast<void*>(&hkGetDamageType),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("SupportWeapon: GetDamageType hook failed, damageType is ignored");
        g_Target = nullptr;
        return false;
    }
    return true;
}

void Uninstall_EquipDevelopControllerImpl_GetDamageType_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig   = nullptr;
}
