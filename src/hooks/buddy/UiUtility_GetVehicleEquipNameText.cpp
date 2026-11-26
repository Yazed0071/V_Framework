#include <pch.h>

#include "UiUtility_GetVehicleEquipNameText.h"

#include <cstdint>
#include <cstdio>
#include <initializer_list>

#include "AddressSet.h"
#include "BattleGearArmory.h"
#include "BattleGearWeapons.h"
#include "BuddyOutfitRegistry.h"
#include "EquipAliasLease.h"
#include "EquipDevelopControllerImpl_GetDWalkerParts.h"
#include "EquipPartParams.h"
#include "HookUtils.h"
#include "UixUtilityControl.h"
#include "log.h"

namespace
{
    using GetVehicleEquipNameText_t     = const char*(__fastcall*)(void* self, std::uint32_t vehicleEquipId);
    using GetVehicleEquipIconFtexPath_t = std::uint64_t*(__fastcall*)(void* self, std::uint64_t* path, std::uint32_t vehicleEquipId);
    using GetWeaponItemNameLangId_t     = void*(__fastcall*)(std::uint64_t* langId, std::int32_t equipId, bool variant);
    using GetIconFtexPath_t             = std::uint64_t*(__fastcall*)(std::uint64_t* path, std::uint32_t equipId, std::int32_t variant);
    using GetLangText_t                 = const char*(__fastcall*)(std::uint64_t langId);

    constexpr std::uint8_t kNameTextEntry[] =
    {
        0xFF, 0xCA, 0x48, 0xB9, 0x98, 0x9F, 0x16, 0xBF, 0xA0, 0xB8, 0x00, 0x00, 0x83, 0xFA, 0x1A,
    };
    constexpr std::uint8_t kIconFtexPathEntry[] =
    {
        0x41, 0xFF, 0xC8, 0x48, 0xC7, 0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0x41, 0x83, 0xF8, 0x1A,
    };

    constexpr std::size_t   kVarWalkerSidearm   = 0x2E5D;
    constexpr std::uint32_t kVehicleEquipIdD114 = 0x14;
    constexpr std::uint32_t kVehicleEquipIdZeev = 0x16;
    constexpr std::uint32_t kVehicleEquipIdMissilePod = 0x0B;
    constexpr std::uint32_t kVehicleEquipIdStunGun    = 0x0C;
    constexpr std::uint32_t kVehicleEquipIdCupolaGun  = 0x05;   // the tanks' cupola machine gun: the gun on the Battle Gear's roof
    constexpr std::uint64_t kLangBtgRailGun  = 0x5F4DA30DD7DFull;   // name_bg_41000
    constexpr std::uint64_t kLangBtgHMissile = 0x4DDBD7A1AF38ull;   // name_bg_42000
    constexpr std::uint32_t kEquipTypeSmg       = 3;
    constexpr std::int32_t  kNoEquipId          = 0x7FF;

    GetVehicleEquipNameText_t     g_OrigNameText     = nullptr;
    GetVehicleEquipIconFtexPath_t g_OrigIconFtexPath = nullptr;
    void* g_NameTextTarget     = nullptr;
    void* g_IconFtexPathTarget = nullptr;

    std::int32_t CustomSidearmShownAs(std::uint32_t vehicleEquipId)
    {
        if (vehicleEquipId != kVehicleEquipIdD114 && vehicleEquipId != kVehicleEquipIdZeev)
            return 0;
        const std::uint8_t* vars = buddyoutfit::ReadScriptVars();
        if (!vars)
            return 0;
        const std::uint8_t type = vars[kVarWalkerSidearm];
        buddyoutfit::Entry e;
        if (type < buddyoutfit::FirstCustomValue(buddyoutfit::Kind::WalkerSidearm)
            || !buddyoutfit::TryGetByValue(buddyoutfit::Kind::WalkerSidearm, type, &e)
            || !IsUsableWalkerSidearm(e))
            return 0;
        const bool smg = EquipParam_GetEquipType(e.equipId) == kEquipTypeSmg;
        if (vehicleEquipId != (smg ? kVehicleEquipIdZeev : kVehicleEquipIdD114))
            return 0;
        std::int32_t engineId = equip::CurrentAliasOf(e.equipId);
        if (engineId == 0)
            engineId = e.equipId;
        return engineId > 0 && engineId < kNoEquipId ? engineId : 0;
    }

    std::uint64_t BattleGearWeaponLangId(std::uint32_t vehicleEquipId)
    {
        if (!battleGearWeapons::LoadoutApplies())
            return 0;
#ifdef _DEBUG   // Battle Gear is unfinished: Debug test builds only
        if (vehicleEquipId == kVehicleEquipIdStunGun || vehicleEquipId == kVehicleEquipIdMissilePod)
            if (const std::uint64_t registered = battleGearArmory::FittedNameLangId())
                return registered;
#endif
        switch (vehicleEquipId)
        {
        case kVehicleEquipIdStunGun:    return kLangBtgRailGun;
        case kVehicleEquipIdMissilePod: return kLangBtgHMissile;
        default:                        return 0;
        }
    }

    // the Battle Gear's roof gun fills the weapon wheel's sidearm entry (the ZE'EV's id): it shows as the gun it is
    bool IsRoofGunEntry(std::uint32_t vehicleEquipId)
    {
        return vehicleEquipId == kVehicleEquipIdZeev && battleGearWeapons::LoadoutApplies();
    }

    const char* __fastcall hkNameText(void* self, std::uint32_t vehicleEquipId)
    {
        if (IsRoofGunEntry(vehicleEquipId))
        {
#ifdef _DEBUG   // Battle Gear is unfinished: Debug test builds only
            if (const std::uint64_t roofGunName = battleGearArmory::FittedRoofGunNameLangId())
            {
                const auto langText = reinterpret_cast<GetLangText_t>(ResolveGameAddress(gAddr.Ui_GetLangText));
                const char* text = langText ? langText(roofGunName) : nullptr;
                if (text && text[0])
                    return text;
            }
#endif
            return g_OrigNameText(self, kVehicleEquipIdCupolaGun);
        }
        if (const std::uint64_t battleGearName = BattleGearWeaponLangId(vehicleEquipId))
        {
            const auto langText = reinterpret_cast<GetLangText_t>(ResolveGameAddress(gAddr.Ui_GetLangText));
            const char* text = langText ? langText(battleGearName) : nullptr;
            if (text && text[0])
                return text;
        }
        const std::int32_t equipId = CustomSidearmShownAs(vehicleEquipId);
        if (equipId == 0)
            return g_OrigNameText(self, vehicleEquipId);
        const auto nameLangId = reinterpret_cast<GetWeaponItemNameLangId_t>(
            UixUtilityControl::Function(UixUtilityControl::UiFunction::GetWeaponItemNameLangId));
        const auto langText = reinterpret_cast<GetLangText_t>(ResolveGameAddress(gAddr.Ui_GetLangText));
        if (!nameLangId || !langText)
            return g_OrigNameText(self, vehicleEquipId);
        std::uint64_t langId = 0;
        nameLangId(&langId, equipId, false);
        return langText(langId);
    }

    std::uint64_t* __fastcall hkIconFtexPath(void* self, std::uint64_t* path, std::uint32_t vehicleEquipId)
    {
        if (IsRoofGunEntry(vehicleEquipId))
        {
#ifdef _DEBUG
            if (const std::uint64_t icon = path ? battleGearArmory::FittedRoofGunIconPath() : 0)
            {
                *path = icon;
                return path;
            }
#endif
            return g_OrigIconFtexPath(self, path, kVehicleEquipIdCupolaGun);
        }
#ifdef _DEBUG
        if (path && (vehicleEquipId == kVehicleEquipIdStunGun || vehicleEquipId == kVehicleEquipIdMissilePod)
            && battleGearWeapons::LoadoutApplies())
        {
            if (const std::uint64_t icon = battleGearArmory::FittedIconPath())
            {
                *path = icon;
                return path;
            }
        }
#endif
        const std::int32_t equipId = CustomSidearmShownAs(vehicleEquipId);
        if (equipId == 0)
            return g_OrigIconFtexPath(self, path, vehicleEquipId);
        const auto iconFtexPath = reinterpret_cast<GetIconFtexPath_t>(
            UixUtilityControl::Function(UixUtilityControl::UiFunction::GetIconFtexPath));
        if (!iconFtexPath)
            return g_OrigIconFtexPath(self, path, vehicleEquipId);
        return iconFtexPath(path, static_cast<std::uint32_t>(equipId), 0);
    }

    bool HookOne(std::uintptr_t addr, const std::uint8_t* entry, std::size_t entrySize,
                 void* detour, void** orig, void*& target, const char* what)
    {
        void* t = addr ? ResolveGameAddress(addr) : nullptr;
        if (!t)
        {
            LogError("BuddyOutfit: %s address missing on this build, custom D-Walker sidearms show a vanilla name", what);
            return false;
        }
        const HookEntry state = CheckHookEntry(t, entry, entrySize);
        if (state == HookEntry::Unknown)
        {
            char desc[160] = {};
            DescribeFunctionEntry(t, desc, sizeof(desc));
            LogError("BuddyOutfit: %s code differs on this build (%s), custom D-Walker sidearms show a vanilla name", what, desc);
            return false;
        }
        if (state == HookEntry::ForeignHook)
        {
            char label[96] = {};
            snprintf(label, sizeof(label), "BuddyOutfit: %s", what);
            LogHookedOnTop(label, t);
        }
        if (!CreateAndEnableHook(t, detour, orig))
        {
            LogError("BuddyOutfit: %s hook failed, custom D-Walker sidearms show a vanilla name", what);
            return false;
        }
        target = t;
        return true;
    }
}

bool Install_UiUtility_GetVehicleEquipNameText_Hooks()
{
    bool ok = true;
    if (!g_NameTextTarget)
        ok &= HookOne(gAddr.UiUtility_GetVehicleEquipNameText, kNameTextEntry, sizeof(kNameTextEntry),
                      reinterpret_cast<void*>(&hkNameText), reinterpret_cast<void**>(&g_OrigNameText),
                      g_NameTextTarget, "GetVehicleEquipNameText");
    if (!g_IconFtexPathTarget)
        ok &= HookOne(gAddr.UiUtility_GetVehicleEquipIconFtexPath, kIconFtexPathEntry, sizeof(kIconFtexPathEntry),
                      reinterpret_cast<void*>(&hkIconFtexPath), reinterpret_cast<void**>(&g_OrigIconFtexPath),
                      g_IconFtexPathTarget, "GetVehicleEquipIconFtexPath");
    return ok;
}

bool Uninstall_UiUtility_GetVehicleEquipNameText_Hooks()
{
    for (void** t : { &g_IconFtexPathTarget, &g_NameTextTarget })
    {
        if (*t)
        {
            DisableAndRemoveHook(*t);
            *t = nullptr;
        }
    }
    return true;
}
