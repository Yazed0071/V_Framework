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
#include "../hooks/equip/EquipIdTable_AddToEquipIdTable.h"

bool Install_SetLuaFunctions_Hook();

namespace
{
    static std::atomic_bool gStarted{ false };
    static std::atomic_bool gConsoleReady{ false };
}


static void SetupConsole()
{
    if (gConsoleReady.load())
        return;

    if (!AllocConsole())
        AttachConsole(ATTACH_PARENT_PROCESS);

    FILE* fp = nullptr;
    freopen_s(&fp, "CONOUT$", "w", stdout);
    freopen_s(&fp, "CONOUT$", "w", stderr);
    freopen_s(&fp, "CONIN$", "r", stdin);

    SetConsoleTitleW(L"V_FrameWork");
    gConsoleReady.store(true);

    printf("[DLL] Console ready\n");
    fflush(stdout);
}

static DWORD WINAPI InitThread(LPVOID)
{
    #ifdef _DEBUG
    SetupConsole();
    #endif

    InitLog();

    Log("[DLL] InitThread started.\n");

    HMODULE hGame = GetModuleHandleW(nullptr);

    const MH_STATUS st = MH_Initialize();
    Log("[DLL] MH_Initialize -> %d\n", static_cast<int>(st));
    if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED)
        return 0;

    const bool earlyLuaBridgeOk = Install_SetLuaFunctions_Hook();
    Log("[DLL] Early Install_SetLuaFunctions_Hook -> %s\n", earlyLuaBridgeOk ? "OK" : "FAIL");

    if (!ResolveAddressSet(hGame))
    {
        Log("[DLL] ResolveAddressSet failed.\n");
        return 0;
    }

    // Install the AddToEquipIdTable observer hook IMMEDIATELY after
    // address resolution and BEFORE any other framework state. The
    // observer records which compressed equip-id slots vanilla's boot
    // scripts populate; the framework's custom-equipId allocator
    // queries this to pick truly-free in-bounds slots and avoid the
    // 0x289-bound OOB write that corrupts vanilla UI state.
    //
    // This is installed here (not via the FeatureModule pipeline)
    // because vanilla's TppEquipParts.lua / similar boot scripts may
    // call AddToEquipIdTable from the Lua state we just made writable
    // via SetLuaFunctions — installing later would miss those calls.
    const bool observerOk =
        EquipIdTableAdd::Install_StockAddToEquipIdTable_Observer();
    Log("[DLL] Early Install_StockAddToEquipIdTable_Observer -> %s\n",
        observerOk ? "OK" : "FAIL");

    V_FrameWorkState::Load();

    RegisterBuiltInFeatureModules();



    const bool allOk = FeatureModuleRegistry::Instance().InstallAll(hGame);
    Log("[DLL] FeatureModuleRegistry::InstallAll -> %s\n", allOk ? "OK" : "PARTIAL/FAIL");

    Log("[DLL] InitThread done.\n");
    return 0;
}

static void UninstallAll(bool processTerminating)
{
    if (processTerminating)
    {
        // Process is exiting (Windows DLL_PROCESS_DETACH with
        // lpReserved != nullptr). Per MSDN guidance we MUST NOT call
        // into other DLLs here — MinHook trampolines, FeatureModule
        // uninstalls, and even file I/O may touch DLLs that have
        // already been unloaded by the loader. Skipping the heavy
        // uninstall is the safe path; the OS cleans up the address
        // space anyway when the process terminates.
        //
        // What we DO want to do safely: log a marker so it's clear
        // why the per-hook "uninstall" lines are absent, flush log
        // buffers so the in-progress line lands on disk, and free
        // our AllocConsole handle so conhost.exe (the console host
        // process) terminates with us instead of lingering. Without
        // FreeConsole, conhost can keep the console window alive
        // for a beat after the game exits — user-visible as "the
        // console never closes."
        Log("[DLL] DLL_PROCESS_DETACH: process terminating, skipping "
            "FeatureModule uninstall (per MSDN guidance — other DLLs "
            "may already be unloaded). OS will reclaim address space.\n");
        fflush(stdout);
        fflush(stderr);
        CloseLog();

        if (gConsoleReady.load())
            FreeConsole();
        return;
    }

    FeatureModuleRegistry::Instance().UninstallAll();
    EquipIdTableAdd::Uninstall_StockAddToEquipIdTable_Observer();
    MH_Uninitialize();
    Log("[DLL] UninstallAll done.\n");

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