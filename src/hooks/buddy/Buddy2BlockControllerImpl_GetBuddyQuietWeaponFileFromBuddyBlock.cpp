#include "pch.h"

#include "Buddy2BlockControllerImpl_GetBuddyQuietWeaponFileFromBuddyBlock.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_set>

#include "AddressSet.h"
#include "BuddyOutfitRegistry.h"
#include "FoxPathInternal.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "TppEquip_ReloadEquipIdTable.h"
#include "log.h"

namespace
{
    using GetWeaponFile_t = std::uint64_t*(__fastcall*)(void* self, std::uint64_t* outPath, std::uint16_t weaponType);
    using FoxPath_Path_t  = void*(__fastcall*)(void* path, std::uint64_t code64ext);

    GetWeaponFile_t g_Orig = nullptr;
    void*           g_Target = nullptr;
    FoxPath_Path_t  g_FoxPath_Path = nullptr;

    std::mutex                      g_MissingMutex;
    std::unordered_set<std::string> g_MissingLogged;

    std::uint64_t* __fastcall hkGetBuddyQuietWeaponFileFromBuddyBlock(void* self, std::uint64_t* outPath,
                                                                      std::uint16_t weaponType)
    {
        buddyoutfit::Entry e;
        if (outPath && g_FoxPath_Path
            && weaponType >= buddyoutfit::FirstCustomValue(buddyoutfit::Kind::QuietWeapon) && weaponType <= 0xFE
            && buddyoutfit::TryGetByValue(buddyoutfit::Kind::QuietWeapon, static_cast<std::uint8_t>(weaponType), &e)
            && !MissionCodeGuard::ShouldBypassHooks())
        {
            std::uint64_t pack = 0;
            if (TppEquip_GetCustomEquipPack(e.equipId, &pack) && fox::detail::PathExistsByCode(pack))
            {
                g_FoxPath_Path(outPath, pack);
                return outPath;
            }
            {
                std::lock_guard<std::mutex> lock(g_MissingMutex);
                if (g_MissingLogged.insert(e.key).second)
                    LogWarn("BuddyOutfit '%s': the weapon's EquipIdTable pack is missing or not installed, Quiet uses WICKED BUTTERFLY",
                        e.key.c_str());
            }
            if (std::uint8_t* vars = buddyoutfit::ReadScriptVars())
                *reinterpret_cast<std::uint16_t*>(vars + buddyoutfit::kVarQuietWeapon) = 0;
            return g_Orig(self, outPath, 0);
        }
        return g_Orig(self, outPath, weaponType);
    }
}

bool Install_Buddy2BlockControllerImpl_GetBuddyQuietWeaponFileFromBuddyBlock_Hook()
{
    if (g_Target)
        return true;
    g_FoxPath_Path = reinterpret_cast<FoxPath_Path_t>(ResolveGameAddress(gAddr.FoxPath_Path));
    void* target = ResolveGameAddress(gAddr.Buddy2BlockControllerImpl_GetBuddyQuietWeaponFile);
    if (!target || !g_FoxPath_Path
        || !CreateAndEnableHook(target, reinterpret_cast<void*>(&hkGetBuddyQuietWeaponFileFromBuddyBlock),
                                reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("BuddyOutfit: Quiet weapon pack hook failed or address missing, custom Quiet weapons off");
        return false;
    }
    g_Target = target;
    return true;
}

bool Uninstall_Buddy2BlockControllerImpl_GetBuddyQuietWeaponFileFromBuddyBlock_Hook()
{
    if (!g_Target)
        return true;
    DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig = nullptr;
    return true;
}
