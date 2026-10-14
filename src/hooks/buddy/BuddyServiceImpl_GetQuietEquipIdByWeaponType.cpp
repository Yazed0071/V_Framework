#include "pch.h"

#include "BuddyServiceImpl_GetQuietEquipIdByWeaponType.h"

#include <cstdint>

#include "AddressSet.h"
#include "BuddyOutfitRegistry.h"
#include "EquipPartParams.h"
#include "GunBasicInject.h"
#include "HookUtils.h"
#include "TppEquip_ReloadEquipIdTable.h"
#include "log.h"

namespace
{
    using ByWeaponType_t = std::uint64_t(__fastcall*)(void* svc, std::uint32_t weaponType);

    ByWeaponType_t g_OrigEquipId    = nullptr;
    ByWeaponType_t g_OrigSuppressor = nullptr;
    void* g_TargetEquipId    = nullptr;
    void* g_TargetSuppressor = nullptr;

    constexpr int kGunBasicMuzzleOptionSlot = 5;

    bool CustomWeapon(std::uint32_t weaponType, buddyoutfit::Entry* e)
    {
        std::uint64_t pack = 0;
        return weaponType >= buddyoutfit::FirstCustomValue(buddyoutfit::Kind::QuietWeapon) && weaponType <= 0xFE
            && buddyoutfit::TryGetByValue(buddyoutfit::Kind::QuietWeapon, static_cast<std::uint8_t>(weaponType), e)
            && TppEquip_GetCustomEquipPack(e->equipId, &pack);
    }

    bool HasSuppressorMuzzle(std::uint16_t equipId)
    {
        const int weaponId = TppEquip_GetSubIdForEquipId(equipId);
        if (weaponId <= 0)
            return false;
        unsigned char row[12] = {};
        const bool haveRow = GunBasic_ReadRowBytes(weaponId, row);
        int parts[11] = {};
        GunBasic_GetLogicalParts(weaponId, parts, haveRow ? row : nullptr);
        return EquipParam_IsSuppressorMuzzleOption(parts[kGunBasicMuzzleOptionSlot]);
    }

    std::uint64_t __fastcall hkGetQuietEquipIdByWeaponType(void* svc, std::uint32_t weaponType)
    {
        buddyoutfit::Entry e;
        if (CustomWeapon(weaponType, &e))
            return e.equipId;
        return g_OrigEquipId(svc, weaponType);
    }

    std::uint64_t __fastcall hkIsUseSuppressorByWeaponType(void* svc, std::uint32_t weaponType)
    {
        buddyoutfit::Entry e;
        if (CustomWeapon(weaponType, &e))
            return HasSuppressorMuzzle(e.equipId);
        return g_OrigSuppressor(svc, weaponType);
    }

    bool HookOne(std::uintptr_t addr, void* detour, void** orig, void*& target, const char* what)
    {
        void* t = ResolveGameAddress(addr);
        if (!t || !CreateAndEnableHook(t, detour, orig))
        {
            LogError("BuddyOutfit: %s hook failed or address missing, custom Quiet weapons off", what);
            return false;
        }
        target = t;
        return true;
    }
}

bool Install_BuddyServiceImpl_GetQuietEquipIdByWeaponType_Hooks()
{
    bool ok = true;
    if (!g_TargetEquipId)
        ok &= HookOne(gAddr.BuddyServiceImpl_GetQuietEquipIdByWeaponType,
                      reinterpret_cast<void*>(&hkGetQuietEquipIdByWeaponType),
                      reinterpret_cast<void**>(&g_OrigEquipId), g_TargetEquipId, "Quiet weapon equip id");
    if (!g_TargetSuppressor)
        ok &= HookOne(gAddr.BuddyServiceImpl_IsUseSuppressorByWeaponType,
                      reinterpret_cast<void*>(&hkIsUseSuppressorByWeaponType),
                      reinterpret_cast<void**>(&g_OrigSuppressor), g_TargetSuppressor, "Quiet weapon suppressor");
    return ok;
}

bool Uninstall_BuddyServiceImpl_GetQuietEquipIdByWeaponType_Hooks()
{
    for (void** t : { &g_TargetSuppressor, &g_TargetEquipId })
    {
        if (*t)
        {
            DisableAndRemoveHook(*t);
            *t = nullptr;
        }
    }
    g_OrigEquipId = nullptr;
    g_OrigSuppressor = nullptr;
    return true;
}
