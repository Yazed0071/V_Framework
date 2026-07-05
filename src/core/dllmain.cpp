#include "pch.h"
#include <Windows.h>
#include <atomic>
#include <cstdio>

#include "MinHook.h"
#include "log.h"
#include "BuiltInModules.h"
#include "FeatureModule.h"
#include "AddressSet.h"
#include "V_FrameWorkState.h"
#include "HookUtils.h"

bool g_HookBatchMode = false;

bool Install_SetLuaFunctions_Hook();

namespace
{
    static std::atomic_bool gStarted{ false };
    static std::atomic_bool gConsoleReady{ false };
}


#ifdef _DEBUG
static void SetupConsole()
{
    if (gConsoleReady.load())
        return;

    EnsureConsole();
    gConsoleReady.store(true);

    printf("[DLL] Console ready\n");
    fflush(stdout);
}
#endif

static DWORD WINAPI InitThread(LPVOID)
{
    #ifdef _DEBUG
    SetupConsole();
    #endif

    InitLog();

    Log("[DLL] InitThread started.\n");

    HMODULE hGame = GetModuleHandleW(nullptr);

    const MH_STATUS st = MH_Initialize();
#ifdef _DEBUG
    Log("[DLL] MH_Initialize -> %d\n", static_cast<int>(st));
#endif
    if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED)
        return 0;

    if (!ResolveAddressSet(hGame))
    {
        Log("[DLL] ResolveAddressSet failed.\n");
        return 0;
    }

    InstallCrashHandler();

    RegisterBuiltInFeatureModules();

    V_FrameWorkState::Load();

    g_HookBatchMode = true;
    const bool allOk = FeatureModuleRegistry::Instance().InstallAll(hGame);
    g_HookBatchMode = false;
    const MH_STATUS applySt = MH_ApplyQueued();
    Log("[DLL] FeatureModuleRegistry::InstallAll -> %s\n", allOk ? "OK" : "PARTIAL/FAIL");
    Log("[DLL] MH_ApplyQueued -> %d\n", static_cast<int>(applySt));

#ifdef _DEBUG
    Log("[DLL] InitThread done.\n");
#endif
    return 0;
}

static void UninstallAll(bool processTerminating)
{
    if (processTerminating)
    {


#ifdef _DEBUG
        Log("[DLL] DLL_PROCESS_DETACH: process terminating, skipping "
            "FeatureModule uninstall (per MSDN guidance — other DLLs "
            "may already be unloaded). OS will reclaim address space.\n");
#endif
        fflush(stdout);
        fflush(stderr);
        CloseLog();

        if (gConsoleReady.load())
            FreeConsole();
        return;
    }

    FeatureModuleRegistry::Instance().UninstallAll();
    MH_Uninitialize();
#ifdef _DEBUG
    Log("[DLL] UninstallAll done.\n");
#endif

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

        bool expected = false;
        if (!gStarted.compare_exchange_strong(expected, true))
            return TRUE;

        ResolveAddressSet(GetModuleHandleW(nullptr));

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