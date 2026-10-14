#include "pch.h"
#include "LoadoutManagerImpl_RestoreLoadoutInfosFromVars.h"

#include <cstdint>

#include "AddressSet.h"
#include "GoneEquipCleanup.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using RestoreAll_t    = void(__fastcall*)(void* loadoutManager);
    using RestoreAtSlot_t = void(__fastcall*)(void* loadoutManager, std::uint32_t slot);

    RestoreAll_t    g_OrigRestore           = nullptr;
    RestoreAtSlot_t g_OrigRestoreSortie     = nullptr;
    RestoreAtSlot_t g_OrigRestoreReturnHeli = nullptr;

    void* g_RestoreTarget           = nullptr;
    void* g_RestoreSortieTarget     = nullptr;
    void* g_RestoreReturnHeliTarget = nullptr;

    void __fastcall hkRestoreLoadoutInfosFromVars(void* loadoutManager)
    {
        GoneEquipCleanup::CleanVars(GoneEquipCleanup::ScriptVars());
        g_OrigRestore(loadoutManager);
    }

    void __fastcall hkRestoreSortieLoadoutInfo(void* loadoutManager, std::uint32_t slot)
    {
        GoneEquipCleanup::CleanVars(GoneEquipCleanup::ScriptVars());
        g_OrigRestoreSortie(loadoutManager, slot);
    }

    void __fastcall hkRestoreReturnHeliLoadoutInfo(void* loadoutManager, std::uint32_t slot)
    {
        GoneEquipCleanup::CleanVars(GoneEquipCleanup::ScriptVars());
        g_OrigRestoreReturnHeli(loadoutManager, slot);
    }

    bool HookOne(std::uintptr_t address, void* detour, void** original, void*& target)
    {
        target = ResolveGameAddress(address);
        if (!target)
            return true;
        if (CreateAndEnableHook(target, detour, original))
            return true;
        target = nullptr;
        return false;
    }

    void UnhookOne(void*& target, void** original)
    {
        if (target)
            DisableAndRemoveHook(target);
        target    = nullptr;
        *original = nullptr;
    }
}

bool Install_LoadoutManagerImpl_RestoreLoadoutInfosFromVars_Hooks()
{
    bool ok = HookOne(gAddr.LoadoutManagerImpl_RestoreLoadoutInfosFromVars, reinterpret_cast<void*>(&hkRestoreLoadoutInfosFromVars),
                      reinterpret_cast<void**>(&g_OrigRestore), g_RestoreTarget);
    ok = HookOne(gAddr.LoadoutManagerImpl_RestoreSortieLoadoutInfo, reinterpret_cast<void*>(&hkRestoreSortieLoadoutInfo),
                 reinterpret_cast<void**>(&g_OrigRestoreSortie), g_RestoreSortieTarget) && ok;
    ok = HookOne(gAddr.LoadoutManagerImpl_RestoreReturnHeliLoadoutInfo, reinterpret_cast<void*>(&hkRestoreReturnHeliLoadoutInfo),
                 reinterpret_cast<void**>(&g_OrigRestoreReturnHeli), g_RestoreReturnHeliTarget) && ok;
    if (!g_RestoreTarget || !g_RestoreSortieTarget || !g_RestoreReturnHeliTarget)
        LogError("GoneEquipCleanup: loadout restore hooks missing for this build, removed mods' items can stay in saved loadouts");
    return ok;
}

void Uninstall_LoadoutManagerImpl_RestoreLoadoutInfosFromVars_Hooks()
{
    UnhookOne(g_RestoreTarget, reinterpret_cast<void**>(&g_OrigRestore));
    UnhookOne(g_RestoreSortieTarget, reinterpret_cast<void**>(&g_OrigRestoreSortie));
    UnhookOne(g_RestoreReturnHeliTarget, reinterpret_cast<void**>(&g_OrigRestoreReturnHeli));
}
