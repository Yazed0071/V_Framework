#include "pch.h"
#include "SupplyCboxGameObjectImpl_RestoreRequestFromSVars.h"

#include "AddressSet.h"
#include "GoneEquipCleanup.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using RestoreRequestFromSVars_t = void(__fastcall*)(void* supplyCbox);

    RestoreRequestFromSVars_t g_Orig   = nullptr;
    void*                     g_Target = nullptr;

    void __fastcall hkRestoreRequestFromSVars(void* supplyCbox)
    {
        GoneEquipCleanup::CleanVars(GoneEquipCleanup::ScriptVars());
        g_Orig(supplyCbox);
    }
}

bool Install_SupplyCboxGameObjectImpl_RestoreRequestFromSVars_Hook()
{
    g_Target = ResolveGameAddress(gAddr.SupplyCboxGameObjectImpl_RestoreRequestFromSVars);
    if (!g_Target)
    {
        LogError("GoneEquipCleanup: supply drop restore address missing for this build, removed mods' items stay in pending supply drops");
        return true;
    }
    if (!CreateAndEnableHook(g_Target, reinterpret_cast<void*>(&hkRestoreRequestFromSVars),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("GoneEquipCleanup: supply drop restore hook failed, removed mods' items stay in pending supply drops");
        g_Target = nullptr;
        return false;
    }
    return true;
}

void Uninstall_SupplyCboxGameObjectImpl_RestoreRequestFromSVars_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig   = nullptr;
}
