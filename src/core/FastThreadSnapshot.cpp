#include "pch.h"
#include "FastThreadSnapshot.h"

#include <Windows.h>
#include <TlHelp32.h>
#include <atomic>
#include <cstdint>
#include <cstring>

// MinHook stops threads by walking a toolhelp snapshot of every thread on the PC: ~180 ms with ~9000 threads, paid on
// every hook made or removed after startup. Only this process's threads matter to it, so this DLL's own imports of the
// three toolhelp calls are pointed here, and a thread snapshot lists just our own threads.
// Callers take and walk snapshots while other threads are stopped, and a stopped thread may hold the heap lock or any
// other lock, so nothing here allocates or locks: fixed walks claimed with atomics, and only kernel calls.

namespace
{
    using CreateSnapshot_t  = HANDLE(WINAPI*)(DWORD, DWORD);
    using ThreadWalk_t      = BOOL(WINAPI*)(HANDLE, LPTHREADENTRY32);
    using NtGetNextThread_t = LONG(NTAPI*)(HANDLE process, HANDLE thread, ACCESS_MASK access, ULONG attributes,
                                           ULONG flags, PHANDLE next);

    constexpr std::size_t kWalks       = 8;
    constexpr std::size_t kMostThreads = 4096;

    struct Walk
    {
        std::atomic<LONG>   busy{ 0 };
        std::atomic<HANDLE> token{ nullptr };
        std::size_t         count = 0;
        std::size_t         next  = 0;
        DWORD               ids[kMostThreads];
    };

    Walk g_Walks[kWalks];

    CreateSnapshot_t  g_RealCreateSnapshot = nullptr;
    ThreadWalk_t      g_RealThread32First  = nullptr;
    ThreadWalk_t      g_RealThread32Next   = nullptr;
    NtGetNextThread_t g_GetNextThread      = nullptr;

    void Release(Walk& walk)
    {
        walk.token.store(nullptr);
        walk.busy.store(0);
    }

    Walk* Find(HANDLE snapshot)
    {
        for (Walk& walk : g_Walks)
            if (walk.busy.load() && walk.token.load() == snapshot)
                return &walk;
        return nullptr;
    }

    Walk* Claim(HANDLE token)
    {
        for (Walk& walk : g_Walks)   // the handle was just made, so a walk still holding its value was left behind
            if (walk.busy.load() && walk.token.load() == token)
                Release(walk);

        for (int pass = 0; pass < 2; ++pass)
        {
            for (Walk& walk : g_Walks)
            {
                LONG idle = 0;
                if (walk.busy.compare_exchange_strong(idle, 1))
                    return &walk;
            }
            for (Walk& walk : g_Walks)   // a caller that stopped walking early and closed its handle leaves its walk behind
            {
                DWORD flags = 0;
                const HANDLE left = walk.token.load();
                if (walk.busy.load() && left && !GetHandleInformation(left, &flags))
                    Release(walk);
            }
        }
        return nullptr;
    }

    bool ListOwnThreads(Walk& walk)
    {
        walk.count = 0;
        HANDLE thread = nullptr;
        HANDLE next = nullptr;
        bool fits = true;
        while (g_GetNextThread(GetCurrentProcess(), thread, THREAD_QUERY_LIMITED_INFORMATION, 0, 0, &next) >= 0)
        {
            if (thread)
                CloseHandle(thread);
            thread = next;
            const DWORD id = GetThreadId(thread);
            if (!id)
                continue;
            if (walk.count == kMostThreads)
            {
                fits = false;
                break;
            }
            walk.ids[walk.count++] = id;
        }
        if (thread)
            CloseHandle(thread);
        return fits && walk.count != 0;
    }

    BOOL FillEntry(HANDLE snapshot, LPTHREADENTRY32 entry, bool first, bool& ours)
    {
        Walk* walk = Find(snapshot);
        ours = walk != nullptr;
        if (!ours)
            return FALSE;

        if (first)
            walk->next = 0;
        if (!entry || entry->dwSize < sizeof(THREADENTRY32) || walk->next >= walk->count)
        {
            Release(*walk);   // walked to the end; the handle itself is closed by the caller
            SetLastError(ERROR_NO_MORE_FILES);
            return FALSE;
        }

        entry->cntUsage           = 0;
        entry->th32ThreadID       = walk->ids[walk->next++];
        entry->th32OwnerProcessID = GetCurrentProcessId();
        entry->tpBasePri          = THREAD_PRIORITY_NORMAL;
        entry->tpDeltaPri         = 0;
        entry->dwFlags            = 0;
        return TRUE;
    }

    HANDLE WINAPI FastCreateToolhelp32Snapshot(DWORD flags, DWORD processId)
    {
        if (flags != TH32CS_SNAPTHREAD)
            return g_RealCreateSnapshot(flags, processId);

        const HANDLE token = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        Walk* walk = token ? Claim(token) : nullptr;
        if (walk && ListOwnThreads(*walk))
        {
            walk->next = 0;
            walk->token.store(token);
            return token;
        }
        if (walk)
            Release(*walk);
        if (token)
            CloseHandle(token);
        return g_RealCreateSnapshot(flags, processId);
    }

    BOOL WINAPI FastThread32First(HANDLE snapshot, LPTHREADENTRY32 entry)
    {
        bool ours = false;
        const BOOL result = FillEntry(snapshot, entry, true, ours);
        return ours ? result : g_RealThread32First(snapshot, entry);
    }

    BOOL WINAPI FastThread32Next(HANDLE snapshot, LPTHREADENTRY32 entry)
    {
        bool ours = false;
        const BOOL result = FillEntry(snapshot, entry, false, ours);
        return ours ? result : g_RealThread32Next(snapshot, entry);
    }
}

struct ImportSwap
{
    const char* name;
    void*       function;
};

void FastThreadSnapshot::Install()
{
    HMODULE self = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCSTR>(&FastThreadSnapshot::Install), &self))
        return;

    const HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    const HMODULE ntdll    = GetModuleHandleW(L"ntdll.dll");
    if (!kernel32 || !ntdll)
        return;
    g_RealCreateSnapshot = reinterpret_cast<CreateSnapshot_t>(GetProcAddress(kernel32, "CreateToolhelp32Snapshot"));
    g_RealThread32First  = reinterpret_cast<ThreadWalk_t>(GetProcAddress(kernel32, "Thread32First"));
    g_RealThread32Next   = reinterpret_cast<ThreadWalk_t>(GetProcAddress(kernel32, "Thread32Next"));
    g_GetNextThread      = reinterpret_cast<NtGetNextThread_t>(GetProcAddress(ntdll, "NtGetNextThread"));
    if (!g_RealCreateSnapshot || !g_RealThread32First || !g_RealThread32Next || !g_GetNextThread)
        return;

    const ImportSwap swaps[] = {
        { "CreateToolhelp32Snapshot", reinterpret_cast<void*>(&FastCreateToolhelp32Snapshot) },
        { "Thread32First",            reinterpret_cast<void*>(&FastThread32First) },
        { "Thread32Next",             reinterpret_cast<void*>(&FastThread32Next) },
    };

    auto* base = reinterpret_cast<std::uint8_t*>(self);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
    const IMAGE_DATA_DIRECTORY& imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!imports.VirtualAddress)
        return;

    for (auto* module = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + imports.VirtualAddress); module->Name; ++module)
    {
        if (!module->OriginalFirstThunk)
            continue;
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + module->OriginalFirstThunk);
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + module->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots)
        {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
                continue;
            const char* name = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData)->Name;
            for (const ImportSwap& swap : swaps)
            {
                if (std::strcmp(name, swap.name) != 0)
                    continue;
                DWORD previous = 0;
                if (VirtualProtect(&slots->u1.Function, sizeof(slots->u1.Function), PAGE_READWRITE, &previous))
                {
                    slots->u1.Function = reinterpret_cast<ULONGLONG>(swap.function);
                    VirtualProtect(&slots->u1.Function, sizeof(slots->u1.Function), previous, &previous);
                }
            }
        }
    }
}
