#include "pch.h"
#include "../hooks/outfit/UniqueCharacterDefaultOutfit.h"
#include "../hooks/equip/TppEquip_ReloadEquipIdTable.h"
#include "../hooks/equip/EquipPartParams.h"
#include "../hooks/outfit/FaceFovaWiden.h"
#include "../hooks/outfit/StaffControllerImpl_AddStaff.h"
#include <Windows.h>
#include <atomic>
#include <cstdio>

#include "MinHook.h"
#include "log.h"
#include "DeployGuard.h"
#include "BuiltInModules.h"
#include "FeatureModule.h"
#include "AddressSet.h"
#include "V_FrameWorkState.h"
#include "HookUtils.h"
#include "FastThreadSnapshot.h"

bool g_HookBatchMode = false;

bool Install_SetLuaFunctions_Hook();

namespace equip
{
    void PreApplyDevelopArrayGrowPatches();
}

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace
{
    static std::atomic_bool gStarted{ false };
    static std::atomic_bool gConsoleReady{ false };

    static void LogOwnBuildStamp()
    {
        const auto* base = reinterpret_cast<const std::uint8_t*>(&__ImageBase);
        const auto* dos  = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt   = reinterpret_cast<const IMAGE_NT_HEADERS*>(
            base + dos->e_lfanew);
        LogInfo("V_FrameWork build 0x%08X, game %s",
                static_cast<unsigned>(nt->FileHeader.TimeDateStamp),
                GetGameBuildName(gGameBuild));
    }
}


#ifdef _DEBUG
static void SetupConsole()
{
    if (gConsoleReady.load())
        return;

    EnsureConsole();
    gConsoleReady.store(true);

}
#endif

static DWORD WINAPI InitThread(LPVOID)
{
    #ifdef _DEBUG
    SetupConsole();
    #endif

    InitLog();

    LogOwnBuildStamp();
    HookArena::LogSummary();
    DeployGuard::Init();

    HMODULE hGame = GetModuleHandleW(nullptr);

    const MH_STATUS st = MH_Initialize();
    if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED)
    {
        LogError("Startup: MinHook init failed (%d), V_FrameWork is off", static_cast<int>(st));
        return 0;
    }

    if (!ResolveAddressSet(hGame))
    {
        LogError("Startup: unsupported game exe, V_FrameWork is off");
        return 0;
    }

    InstallCrashHandler();

    RegisterBuiltInFeatureModules();

    V_FrameWorkState::Load();

    g_HookBatchMode = true;
    uniquedefaultoutfit::QueueDevelopRowsEarly();

    const bool allOk = FeatureModuleRegistry::Instance().InstallAll(hGame);
    g_HookBatchMode = false;
    const MH_STATUS applySt = MH_ApplyQueued();
    outfit::NoteStaffHooksApplied(applySt == MH_OK);
    if (applySt == MH_OK)
    {
        EquipParam_InitializeWidePartIds();
        outfit::InstallFaceFovaWiden();
    }
    if (!allOk)
        LogError("Startup: some features failed to install, see errors above");
    V_FrameWorkState::NoteInstallOutcome(allOk && applySt == MH_OK);
    if (applySt != MH_OK)
        LogError("Startup: hooks not applied (MinHook %d), V_FrameWork is off",
                 static_cast<int>(applySt));
    return 0;
}

static void UninstallAll(bool processTerminating)
{
    if (processTerminating)
    {
        DeployGuard::OnCleanExit();
        V_FrameWorkState::SaveOnProcessExit();
        V_FrameWorkState::AbandonFlusherThread();

        fflush(stdout);
        fflush(stderr);
        CloseLog();

        if (gConsoleReady.load())
            FreeConsole();
        return;
    }

    V_FrameWorkState::FlushPendingSaves();

    FeatureModuleRegistry::Instance().UninstallAll();
    MH_Uninitialize();

    fflush(stdout);
    fflush(stderr);

    CloseLog();

    if (gConsoleReady.load())
        FreeConsole();
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
    {
        DisableThreadLibraryCalls(hModule);
        FastThreadSnapshot::Install();

        bool expected = false;
        if (!gStarted.compare_exchange_strong(expected, true))
            return TRUE;

        const bool supportedGame = ResolveAddressSet(GetModuleHandleW(nullptr));
        TppEquip_ReserveInfoListMirrorEarly();
        HookArena::ReserveEarly();
        equip::PreApplyDevelopArrayGrowPatches();
        if (supportedGame && !FeatureIsDisabled("LuaBridge") && MH_Initialize() == MH_OK)
            Install_SetLuaFunctions_Hook();   // the game hands out its Lua libraries once, early in boot; the install thread can miss it

        HANDLE hThread = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
        if (hThread)
            CloseHandle(hThread);

        return TRUE;
    }

    case DLL_PROCESS_DETACH:
    {
        UninstallAll(lpReserved != nullptr);
        return TRUE;
    }
    }

    return TRUE;
}
