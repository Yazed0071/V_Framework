#include "pch.h"
#include "FastThreadSnapshot.h"

#include <Windows.h>
#include <TlHelp32.h>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

// MinHook stops threads by walking a toolhelp snapshot of every thread on the PC: ~180 ms with ~9000 threads, paid on
// every hook made or removed after startup. Only this process's threads matter to it, so this DLL's own imports of the
// three toolhelp calls are pointed here, and a thread snapshot lists just our own threads.

namespace
{
    using CreateSnapshot_t = HANDLE(WINAPI*)(DWORD, DWORD);
    using ThreadWalk_t     = BOOL(WINAPI*)(HANDLE, LPTHREADENTRY32);
    using NtGetNextThread_t = LONG(NTAPI*)(HANDLE process, HANDLE thread, ACCESS_MASK access, ULONG attributes,
                                           ULONG flags, PHANDLE next);

    struct OwnThreads
    {
        std::vector<DWORD> ids;
        std::size_t        next = 0;
    };

    std::mutex                               g_Mutex;
    std::unordered_map<HANDLE, OwnThreads>   g_Snapshots;   // keyed by the event handle given out as the snapshot

    FARPROC Kernel32(const char* name)
    {
        return GetProcAddress(GetModuleHandleW(L"kernel32.dll"), name);
    }

    bool ListOwnThreads(std::vector<DWORD>& ids)
    {
        static const auto getNext = reinterpret_cast<NtGetNextThread_t>(
            GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtGetNextThread"));
        if (!getNext)
            return false;

        HANDLE thread = nullptr;
        HANDLE next = nullptr;
        while (getNext(GetCurrentProcess(), thread, THREAD_QUERY_LIMITED_INFORMATION, 0, 0, &next) >= 0)
        {
            if (thread)
                CloseHandle(thread);
            thread = next;
            if (const DWORD id = GetThreadId(thread))
                ids.push_back(id);
        }
        if (thread)
            CloseHandle(thread);
        return !ids.empty();
    }

    BOOL FillEntry(HANDLE snapshot, LPTHREADENTRY32 entry, bool first, bool& ours)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        auto it = g_Snapshots.find(snapshot);
        ours = it != g_Snapshots.end();
        if (!ours)
            return FALSE;

        OwnThreads& threads = it->second;
        if (first)
            threads.next = 0;
        if (!entry || entry->dwSize < sizeof(THREADENTRY32) || threads.next >= threads.ids.size())
        {
            g_Snapshots.erase(it);   // walked to the end; the handle itself is closed by the caller
            SetLastError(ERROR_NO_MORE_FILES);
            return FALSE;
        }

        const DWORD id = threads.ids[threads.next++];
        entry->cntUsage           = 0;
        entry->th32ThreadID       = id;
        entry->th32OwnerProcessID = GetCurrentProcessId();
        entry->tpBasePri          = THREAD_PRIORITY_NORMAL;
        entry->tpDeltaPri         = 0;
        entry->dwFlags            = 0;
        return TRUE;
    }

    HANDLE WINAPI FastCreateToolhelp32Snapshot(DWORD flags, DWORD processId)
    {
        static const auto real = reinterpret_cast<CreateSnapshot_t>(Kernel32("CreateToolhelp32Snapshot"));
        if (flags != TH32CS_SNAPTHREAD)
            return real(flags, processId);

        OwnThreads threads;
        HANDLE token = ListOwnThreads(threads.ids) ? CreateEventW(nullptr, TRUE, FALSE, nullptr) : nullptr;
        if (!token)
            return real(flags, processId);

        std::lock_guard<std::mutex> lock(g_Mutex);
        for (auto it = g_Snapshots.begin(); it != g_Snapshots.end();)   // a caller that stopped walking early leaves its list behind
        {
            DWORD handleFlags = 0;
            it = GetHandleInformation(it->first, &handleFlags) ? std::next(it) : g_Snapshots.erase(it);
        }
        g_Snapshots[token] = std::move(threads);
        return token;
    }

    BOOL WINAPI FastThread32First(HANDLE snapshot, LPTHREADENTRY32 entry)
    {
        static const auto real = reinterpret_cast<ThreadWalk_t>(Kernel32("Thread32First"));
        bool ours = false;
        const BOOL result = FillEntry(snapshot, entry, true, ours);
        return ours ? result : real(snapshot, entry);
    }

    BOOL WINAPI FastThread32Next(HANDLE snapshot, LPTHREADENTRY32 entry)
    {
        static const auto real = reinterpret_cast<ThreadWalk_t>(Kernel32("Thread32Next"));
        bool ours = false;
        const BOOL result = FillEntry(snapshot, entry, false, ours);
        return ours ? result : real(snapshot, entry);
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
