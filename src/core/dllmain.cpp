#include "pch.h"
#include "../hooks/outfit/UniqueCharacterDefaultOutfit.h"
#include "../hooks/equip/TppEquip_ReloadEquipIdTable.h"
#include "../hooks/equip/EquipPartParams.h"
#include "../hooks/outfit/FaceFovaWiden.h"
#include "../hooks/outfit/StaffControllerImpl_AddStaff.h"
#include "../hooks/outfit/OutfitPalette.h"
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
void V_FrameWork_NoteSecondCopy();

namespace equip
{
    void PreApplyDevelopArrayGrowPatches();
}

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace
{
    static std::atomic_bool gStarted{ false };
    static std::atomic_bool gConsoleReady{ false };
    static std::atomic_bool gRunning{ false };
    static HANDLE gLuaMayRun = nullptr;
    constexpr DWORD kLuaWaitMs = 30000;

    struct InstanceRecord
    {
        HMODULE first;
        wchar_t secondPath[MAX_PATH];
    };
    static HANDLE gInstanceMap = nullptr;
    static InstanceRecord* gInstance = nullptr;
    static bool gSecondCopy = false;
    static std::atomic_bool gSecondCopyLogged{ false };

    static bool ClaimFirstCopy(HMODULE self)
    {
        wchar_t name[64]{};
        swprintf_s(name, L"Local\\V_FrameWork_%lu", GetCurrentProcessId());
        gInstanceMap = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                          0, sizeof(InstanceRecord), name);
        if (!gInstanceMap)
            return true;
        const bool existed = GetLastError() == ERROR_ALREADY_EXISTS;
        gInstance = static_cast<InstanceRecord*>(
            MapViewOfFile(gInstanceMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(InstanceRecord)));
        if (!gInstance)
            return true;
        if (!existed)
        {
            gInstance->first = self;
            return true;
        }
        GetModuleFileNameW(self, gInstance->secondPath, MAX_PATH);
        return false;
    }

    static void LetLuaIn(bool running)
    {
        gRunning.store(running);
        if (gLuaMayRun)
            SetEvent(gLuaMayRun);
    }

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
        LetLuaIn(false);
        return 0;
    }

    if (!ResolveAddressSet(hGame))
    {
        LogError("Startup: unsupported game exe, V_FrameWork is off");
        LetLuaIn(false);
        return 0;
    }

    InstallCrashHandler();

    RegisterBuiltInFeatureModules();

    V_FrameWorkState::Load();

    if (!FeatureIsDisabled("PlayerOutfitEquip"))
        outfit::Install_OutfitPalettePack();

    g_HookBatchMode = true;
    uniquedefaultoutfit::QueueDevelopRowsEarly();
    LetLuaIn(true);

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
    {
        outfit::Uninstall_OutfitPalettePack();
        LogError("Startup: hooks not applied (MinHook %d), V_FrameWork is off",
                 static_cast<int>(applySt));
    }
    V_FrameWork_NoteSecondCopy();
    return 0;
}

HMODULE V_FrameWork_FirstCopyIfSecond()
{
    return (gSecondCopy && gInstance) ? gInstance->first : nullptr;
}

void V_FrameWork_NoteSecondCopy()
{
    if (gSecondCopy || !gInstance || !gInstance->secondPath[0]
        || gSecondCopyLogged.exchange(true))
        return;
    LogWarn("Startup: a second V_FrameWork.dll was loaded from %ls and left off; one loader is enough",
            gInstance->secondPath);
}

bool StartupLetsLuaIn()
{
    if (gLuaMayRun && WaitForSingleObject(gLuaMayRun, kLuaWaitMs) == WAIT_TIMEOUT)
    {
        LogError("Startup: still not ready for Lua after %u s, V_FrameWork's Lua libraries not registered", kLuaWaitMs / 1000);
        return false;
    }
    return gRunning.load();
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
        if (!ClaimFirstCopy(hModule))
        {
            gSecondCopy = true;   // another loader mapped a renamed copy; that one owns every hook
            return TRUE;
        }
        FastThreadSnapshot::Install();

        bool expected = false;
        if (!gStarted.compare_exchange_strong(expected, true))
            return TRUE;

        const bool supportedGame = ResolveAddressSet(GetModuleHandleW(nullptr));
        TppEquip_ReserveInfoListMirrorEarly();
        HookArena::ReserveEarly();
        equip::PreApplyDevelopArrayGrowPatches();
        if (supportedGame && !FeatureIsDisabled("LuaBridge") && MH_Initialize() == MH_OK)
            Install_SetLuaFunctions_Hook();

        gLuaMayRun = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        HANDLE hThread = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
        if (hThread)
            CloseHandle(hThread);
        else
            LetLuaIn(false);

        return TRUE;
    }

    case DLL_PROCESS_DETACH:
    {
        if (gSecondCopy)
            return TRUE;
        UninstallAll(lpReserved != nullptr);
        return TRUE;
    }
    }

    return TRUE;
}
