#include "pch.h"
#include "NativeCallPatch.h"

#include <Windows.h>
#include <TlHelp32.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace
{
    constexpr std::size_t kMaximumWalkedFrames = 256;
    constexpr int kFreezeRounds = 16;
    SRWLOCK g_PatchLock = SRWLOCK_INIT;

    struct PreparedPatch
    {
        unsigned char* original;
        bool* installed;
        std::uintptr_t begin;
        std::uintptr_t end;
        unsigned char before[6];
        unsigned char after[6];
        unsigned char size;
        bool modify;
        bool touched = false;
    };

    struct Page
    {
        void* address;
        DWORD protection = 0;
        bool captured = false;
        bool writable = false;
    };

    struct Thread
    {
        DWORD id;
        HANDLE handle;
        bool suspended = false;
        bool inspected = false;
    };

    bool ReadReturnAddress(std::uintptr_t stackPointer, std::uintptr_t& value)
    {
        __try
        {
            value = *reinterpret_cast<volatile const std::uintptr_t*>(stackPointer);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool VirtualUnwindFrame(DWORD64 imageBase, PRUNTIME_FUNCTION entry, CONTEXT& context)
    {
        void* handlerData = nullptr;
        DWORD64 establisherFrame = 0;
        __try
        {
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, entry, &context,
                             &handlerData, &establisherFrame, nullptr);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool UnwindOneFrame(CONTEXT& context)
    {
        DWORD64 imageBase = 0;
        const auto entry = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
        if (entry)
            return VirtualUnwindFrame(imageBase, entry, context);
        std::uintptr_t returnAddress = 0;
        if (context.Rsp % sizeof(std::uintptr_t) != 0
            || !ReadReturnAddress(static_cast<std::uintptr_t>(context.Rsp), returnAddress))
            return false;
        context.Rip = returnAddress;
        context.Rsp += sizeof(std::uintptr_t);
        return true;
    }

    bool StackIsQuiet(CONTEXT context, std::uintptr_t begin, std::uintptr_t end)
    {
        for (std::size_t frame = 0; frame < kMaximumWalkedFrames; ++frame)
        {
            const auto ip = static_cast<std::uintptr_t>(context.Rip);
            if (!ip)
                return true;
            if (ip >= begin && ip < end)
                return false;
            const auto previousStack = context.Rsp;
            if (!UnwindOneFrame(context))
                return false;
            if (context.Rip && context.Rsp <= previousStack)
                return false;
        }
        return false;
    }

    class ThreadFreeze
    {
    public:
        ~ThreadFreeze()
        {
            Resume();
            for (std::size_t i = 0; i < threads.size(); ++i)
            {
                const Thread& thread = threads[i];
                if (thread.handle)
                    CloseHandle(thread.handle);
            }
        }

        bool Prepare()
        {
            currentId = GetCurrentThreadId();
            processId = GetCurrentProcessId();
            std::size_t added = 0;
            return Collect(added);
        }

        bool Suspend(const std::vector<PreparedPatch>& patches,
                     std::uintptr_t quietBegin, std::uintptr_t quietEnd)
        {
            for (int round = 0; round < kFreezeRounds; ++round)
            {
                if (!SuspendCollected(patches, quietBegin, quietEnd))
                    return false;
                std::size_t added = 0;
                if (!Collect(added))
                    return false;
                if (!added)
                    return true;
            }
            SetLastError(ERROR_NOT_READY);
            return false;
        }

        bool Resume()
        {
            bool okay = true;
            for (std::size_t i = 0; i < threads.size(); ++i)
            {
                Thread& thread = threads[i];
                if (!thread.suspended)
                    continue;
                if (ResumeThread(thread.handle) == static_cast<DWORD>(-1))
                    okay = false;
                else
                    thread.suspended = false;
            }
            return okay;
        }

    private:
        bool Known(DWORD id) const
        {
            for (std::size_t i = 0; i < threads.size(); ++i)
                if (threads[i].id == id)
                    return true;
            return false;
        }

        bool Collect(std::size_t& added)
        {
            added = 0;
            const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            if (snapshot == INVALID_HANDLE_VALUE)
                return false;
            THREADENTRY32 entry{};
            entry.dwSize = sizeof(entry);
            bool okay = Thread32First(snapshot, &entry) != FALSE;
            DWORD error = ERROR_SUCCESS;
            try
            {
                while (okay)
                {
                    if (entry.th32OwnerProcessID == processId
                        && entry.th32ThreadID != currentId && !Known(entry.th32ThreadID))
                    {
                        const HANDLE handle = OpenThread(THREAD_SUSPEND_RESUME
                            | THREAD_GET_CONTEXT | THREAD_QUERY_LIMITED_INFORMATION,
                            FALSE, entry.th32ThreadID);
                        if (!handle && GetLastError() != ERROR_INVALID_PARAMETER)
                        {
                            error = GetLastError();
                            okay = false;
                            break;
                        }
                        if (handle && GetProcessIdOfThread(handle) != processId)
                        {
                            CloseHandle(handle);
                            error = ERROR_INVALID_HANDLE;
                            okay = false;
                            break;
                        }
                        if (handle)
                        {
                            threads.push_back({ entry.th32ThreadID, handle, false, false });
                            ++added;
                        }
                    }
                    entry.dwSize = sizeof(entry);
                    if (!Thread32Next(snapshot, &entry))
                    {
                        okay = GetLastError() == ERROR_NO_MORE_FILES;
                        if (!okay)
                            error = GetLastError();
                        break;
                    }
                }
            }
            catch (...)
            {
                CloseHandle(snapshot);
                throw;
            }
            CloseHandle(snapshot);
            SetLastError(okay ? ERROR_SUCCESS : error);
            return okay;
        }

        bool SuspendCollected(const std::vector<PreparedPatch>& patches,
                              std::uintptr_t quietBegin, std::uintptr_t quietEnd)
        {
            for (std::size_t i = 0; i < threads.size(); ++i)
            {
                Thread& thread = threads[i];
                if (!thread.handle || thread.suspended || thread.inspected)
                    continue;
                if (SuspendThread(thread.handle) == static_cast<DWORD>(-1))
                {
                    DWORD code = STILL_ACTIVE;
                    if (GetExitCodeThread(thread.handle, &code) && code != STILL_ACTIVE)
                    {
                        thread.inspected = true;
                        continue;
                    }
                    return false;
                }
                thread.suspended = true;
            }
            for (std::size_t i = 0; i < threads.size(); ++i)
            {
                Thread& thread = threads[i];
                if (!thread.suspended || thread.inspected)
                    continue;
                CONTEXT context{};
                context.ContextFlags = CONTEXT_FULL;
                if (!GetThreadContext(thread.handle, &context))
                    return false;
                const std::uintptr_t ip = static_cast<std::uintptr_t>(context.Rip);
                if (quietBegin && !StackIsQuiet(context, quietBegin, quietEnd))
                {
                    SetLastError(ERROR_BUSY);
                    return false;
                }
                for (std::size_t j = 0; j < patches.size(); ++j)
                {
                    const PreparedPatch& patch = patches[j];
                    if (patch.modify && ip > patch.begin && ip < patch.end)
                    {
                        SetLastError(ERROR_BUSY);
                        return false;
                    }
                }
                thread.inspected = true;
            }
            return true;
        }

        std::vector<Thread> threads;
        DWORD currentId = 0;
        DWORD processId = 0;
    };

    bool ReadBytes(const void* address, unsigned char* bytes, std::size_t count)
    {
        __try
        {
            std::memcpy(bytes, address, count);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool WriteBytes(void* address, const unsigned char* bytes, std::size_t count)
    {
        __try
        {
            std::memcpy(address, bytes, count);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool IsExecutable(const void* address)
    {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT
            || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
            return false;
        const DWORD protection = info.Protect & 0xFF;
        return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ
            || protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
    }

    bool PreparePatches(NativeCallPatch* patches, std::size_t count, bool install,
                        std::vector<PreparedPatch>& prepared, std::vector<Page>& pages,
                        std::size_t pageSize)
    {
        prepared.reserve(count);
        if (count > pages.max_size() / 2)
            return false;
        pages.reserve(count * 2);
        for (std::size_t i = 0; i < count; ++i)
        {
            NativeCallPatch& input = patches[i];
            if (!install && !input.installed)
                continue;
            const auto begin = reinterpret_cast<std::uintptr_t>(input.target);
            const auto destination = reinterpret_cast<std::uintptr_t>(input.destination);
            if (!begin || !destination || (input.size != 5 && input.size != 6)
                || begin > (std::numeric_limits<std::uintptr_t>::max)() - input.size
                || !IsExecutable(input.target) || !IsExecutable(input.destination))
                return false;
            const auto next = begin + 5;
            const std::int64_t displacement = destination >= next
                ? static_cast<std::int64_t>(destination - next)
                : -static_cast<std::int64_t>(next - destination);
            if (displacement < (std::numeric_limits<std::int32_t>::min)()
                || displacement > (std::numeric_limits<std::int32_t>::max)())
                return false;

            PreparedPatch patch{};
            patch.original = input.original;
            patch.installed = &input.installed;
            patch.begin = begin;
            patch.end = begin + input.size;
            patch.size = input.size;
            patch.modify = install ? !input.installed : input.installed;
            unsigned char call[6] = { 0xE8, 0, 0, 0, 0, 0x90 };
            const auto relative = static_cast<std::int32_t>(displacement);
            std::memcpy(call + 1, &relative, sizeof(relative));
            std::memcpy(patch.before, input.installed ? call : input.expected, input.size);
            std::memcpy(patch.after, install ? call : input.original, input.size);
            for (const PreparedPatch& existing : prepared)
                if (patch.begin < existing.end && existing.begin < patch.end)
                    return false;
            prepared.push_back(patch);
            if (!patch.modify)
                continue;
            const auto first = begin - begin % pageSize;
            const auto last = (patch.end - 1) - (patch.end - 1) % pageSize;
            for (auto page = first;; page += pageSize)
            {
                if (!IsExecutable(reinterpret_cast<void*>(page)))
                    return false;
                bool found = false;
                for (const Page& existing : pages)
                    if (existing.address == reinterpret_cast<void*>(page))
                        found = true;
                if (!found)
                    pages.push_back({ reinterpret_cast<void*>(page), 0, false, false });
                if (page == last)
                    break;
            }
        }
        return true;
    }

    bool PrepareCodePatches(NativeCodePatch* patches, std::size_t count, bool install,
                            std::vector<PreparedPatch>& prepared, std::vector<Page>& pages,
                            std::size_t pageSize)
    {
        prepared.reserve(count);
        if (count > pages.max_size() / 2)
            return false;
        pages.reserve(count * 2);
        for (std::size_t i = 0; i < count; ++i)
        {
            NativeCodePatch& input = patches[i];
            if (!install && !input.installed)
                continue;
            const auto begin = reinterpret_cast<std::uintptr_t>(input.target);
            if (!begin || input.size == 0 || input.size > sizeof(input.expected)
                || begin > (std::numeric_limits<std::uintptr_t>::max)() - input.size
                || !IsExecutable(input.target))
                return false;
            PreparedPatch patch{};
            patch.original = input.original;
            patch.installed = &input.installed;
            patch.begin = begin;
            patch.end = begin + input.size;
            patch.size = input.size;
            patch.modify = install ? !input.installed : input.installed;
            std::memcpy(patch.before, input.installed ? input.replacement : input.expected,
                        input.size);
            std::memcpy(patch.after, install ? input.replacement : input.original, input.size);
            for (const PreparedPatch& existing : prepared)
                if (patch.begin < existing.end && existing.begin < patch.end)
                    return false;
            prepared.push_back(patch);
            if (!patch.modify)
                continue;
            const auto first = begin - begin % pageSize;
            const auto last = (patch.end - 1) - (patch.end - 1) % pageSize;
            for (auto page = first;; page += pageSize)
            {
                if (!IsExecutable(reinterpret_cast<void*>(page)))
                    return false;
                bool found = false;
                for (const Page& existing : pages)
                    if (existing.address == reinterpret_cast<void*>(page))
                        found = true;
                if (!found)
                    pages.push_back({ reinterpret_cast<void*>(page), 0, false, false });
                if (page == last)
                    break;
            }
        }
        return true;
    }

    bool SetWritable(std::vector<Page>& pages, std::size_t pageSize)
    {
        bool okay = true;
        for (std::size_t i = 0; i < pages.size(); ++i)
        {
            Page& page = pages[i];
            if (page.writable)
                continue;
            DWORD previous = 0;
            if (!VirtualProtect(page.address, pageSize, PAGE_EXECUTE_READWRITE, &previous))
            {
                okay = false;
                continue;
            }
            if (!page.captured)
            {
                page.protection = previous;
                page.captured = true;
            }
            page.writable = true;
        }
        return okay;
    }

    bool RestoreProtection(std::vector<Page>& pages, std::size_t pageSize)
    {
        bool okay = true;
        for (std::size_t i = 0; i < pages.size(); ++i)
        {
            Page& page = pages[i];
            if (!page.writable)
                continue;
            DWORD unused = 0;
            if (!VirtualProtect(page.address, pageSize, page.protection, &unused))
                okay = false;
            else
                page.writable = false;
        }
        return okay;
    }

    bool Flush(const std::vector<PreparedPatch>& patches)
    {
        bool okay = true;
        for (std::size_t i = 0; i < patches.size(); ++i)
        {
            const PreparedPatch& patch = patches[i];
            if (patch.touched && !FlushInstructionCache(GetCurrentProcess(),
                reinterpret_cast<void*>(patch.begin), patch.size))
                okay = false;
        }
        return okay;
    }

    bool Operate(NativeCallPatch* patches, NativeCodePatch* codePatches,
                 std::size_t count, bool install, std::atomic<bool>* readiness,
                 const void* quietFirst, const void* quietLast)
    {
        static_assert(std::atomic<bool>::is_always_lock_free);
        if (!count)
            return true;
        if ((!patches && !codePatches) || (patches && codePatches))
            return false;
        const auto quietBegin = reinterpret_cast<std::uintptr_t>(quietFirst);
        const auto quietEnd = reinterpret_cast<std::uintptr_t>(quietLast);
        if ((quietBegin || quietEnd) && (!quietBegin || quietEnd <= quietBegin))
            return false;
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        if (!info.dwPageSize)
            return false;

        std::vector<PreparedPatch> prepared;
        std::vector<Page> pages;
        if (codePatches
            ? !PrepareCodePatches(codePatches, count, install, prepared, pages, info.dwPageSize)
            : !PreparePatches(patches, count, install, prepared, pages, info.dwPageSize))
        {
            SetLastError(ERROR_INVALID_ADDRESS);
            return false;
        }
        if (prepared.empty())
            return true;

        ThreadFreeze freeze;
        if (!freeze.Prepare() || !freeze.Suspend(prepared, quietBegin, quietEnd))
            return false;
        for (std::size_t i = 0; i < prepared.size(); ++i)
        {
            const PreparedPatch& patch = prepared[i];
            unsigned char current[6]{};
            if (!ReadBytes(reinterpret_cast<void*>(patch.begin), current, patch.size)
                || std::memcmp(current, patch.before, patch.size) != 0)
            {
                SetLastError(ERROR_INVALID_DATA);
                return false;
            }
        }
        if (!SetWritable(pages, info.dwPageSize))
        {
            RestoreProtection(pages, info.dwPageSize);
            return false;
        }

        bool okay = true;
        for (std::size_t i = 0; i < prepared.size(); ++i)
        {
            PreparedPatch& patch = prepared[i];
            if (!patch.modify)
                continue;
            patch.touched = true;
            if (!WriteBytes(reinterpret_cast<void*>(patch.begin), patch.after, patch.size))
            {
                okay = false;
                break;
            }
        }
        if (!Flush(prepared))
            okay = false;
        if (!RestoreProtection(pages, info.dwPageSize))
            okay = false;
        if (!okay)
        {
            bool restored = SetWritable(pages, info.dwPageSize);
            for (std::size_t i = 0; i < prepared.size(); ++i)
            {
                const PreparedPatch& patch = prepared[i];
                if (patch.touched && !WriteBytes(reinterpret_cast<void*>(patch.begin),
                    patch.before, patch.size))
                    restored = false;
            }
            if (!Flush(prepared))
                restored = false;
            if (!RestoreProtection(pages, info.dwPageSize))
                restored = false;
            if (!restored)
            {
                for (std::size_t i = 0; i < prepared.size(); ++i)
                {
                    const PreparedPatch& patch = prepared[i];
                    if (!patch.touched)
                        continue;
                    if (install)
                        std::memcpy(patch.original, patch.before, patch.size);
                    *patch.installed = true;
                }
                SetLastError(ERROR_PARTIAL_COPY);
            }
            return false;
        }
        for (std::size_t i = 0; i < prepared.size(); ++i)
        {
            const PreparedPatch& patch = prepared[i];
            if (!patch.modify)
                continue;
            if (install)
                std::memcpy(patch.original, patch.before, patch.size);
            *patch.installed = install;
        }
        if (readiness)
            readiness->store(install, std::memory_order_release);
        return freeze.Resume();
    }

    bool GuardedOperate(NativeCallPatch* patches, NativeCodePatch* codePatches,
                        std::size_t count, bool install, std::atomic<bool>* readiness,
                        const void* quietBegin, const void* quietEnd)
    {
        AcquireSRWLockExclusive(&g_PatchLock);
        SetLastError(ERROR_SUCCESS);
        bool okay = false;
        try
        {
            okay = Operate(patches, codePatches, count, install, readiness,
                           quietBegin, quietEnd);
        }
        catch (...)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        }
        const DWORD error = GetLastError();
        ReleaseSRWLockExclusive(&g_PatchLock);
        SetLastError(okay ? ERROR_SUCCESS : (error ? error : ERROR_GEN_FAILURE));
        return okay;
    }
}

bool NativePatchErrorIsTransient(unsigned long error)
{
    return error == ERROR_BUSY || error == ERROR_NOT_READY;
}

const char* NativePatchErrorName(unsigned long error)
{
    switch (error)
    {
    case ERROR_SUCCESS:           return "ok";
    case ERROR_BUSY:              return "a thread is executing inside the patched function";
    case ERROR_NOT_READY:         return "new threads kept appearing while freezing";
    case ERROR_INVALID_DATA:      return "the site bytes are not the expected instruction";
    case ERROR_INVALID_ADDRESS:   return "a site is outside executable memory or overlaps another";
    case ERROR_PARTIAL_COPY:      return "a write failed and the rollback did not restore it";
    case ERROR_INVALID_HANDLE:    return "a thread handle belongs to another process";
    case ERROR_MOD_NOT_FOUND:     return "the descriptor producer module cannot be pinned";
    case ERROR_NOT_ENOUGH_MEMORY: return "an allocation failed";
    default:                      return "a system call failed";
    }
}

bool InstallNativeCallPatches(NativeCallPatch* patches, std::size_t count)
{
    return GuardedOperate(patches, nullptr, count, true, nullptr, nullptr, nullptr);
}

bool RemoveNativeCallPatches(NativeCallPatch* patches, std::size_t count)
{
    return GuardedOperate(patches, nullptr, count, false, nullptr, nullptr, nullptr);
}

bool InstallNativeCodePatches(NativeCodePatch* patches, std::size_t count,
    std::atomic<bool>* readiness, const void* quietBegin, const void* quietEnd)
{
    return GuardedOperate(nullptr, patches, count, true, readiness, quietBegin, quietEnd);
}

bool RemoveNativeCodePatches(NativeCodePatch* patches, std::size_t count,
    std::atomic<bool>* readiness, const void* quietBegin, const void* quietEnd)
{
    return GuardedOperate(nullptr, patches, count, false, readiness, quietBegin, quietEnd);
}
