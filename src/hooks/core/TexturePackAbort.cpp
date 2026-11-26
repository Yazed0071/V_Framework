#include <pch.h>
#include "TexturePackAbort.h"

#include <Windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "AddressSet.h"
#include "HookArena.h"
#include "HookUtils.h"
#include "NativeCallPatch.h"
#include "log.h"

namespace
{
    constexpr std::int32_t kLoadCountOff = 0x26318;   // DgTextureStreamer: .pftxs requests still outstanding

    const std::uint8_t kAbortCode[] = {
        0x48, 0x8B, 0xCF,                           // mov rcx,rdi
        0xE8, 0x00, 0x00, 0x00, 0x00,               // call PftxsLoadRequest::Invalid
        0x48, 0x8D, 0x8B, 0xD8, 0x63, 0x02, 0x00,   // lea rcx,[rbx+0x263d8]
        0x41, 0xB8, 0x30, 0x00, 0x00, 0x00,         // mov r8d,0x30
        0x48, 0x8B, 0xD7,                           // mov rdx,rdi
        0xE8, 0x00, 0x00, 0x00, 0x00,               // call dynamic_sequence_allocator::Deallocate
        0x48, 0x8D, 0x8B, 0x00, 0x64, 0x02, 0x00,   // lea rcx,[rbx+0x26400]
        0xE8, 0x00, 0x00, 0x00, 0x00,               // call sequence_list::PopFront
        0x90,                                       // nop
        0x83, 0x7D, 0xC8, 0x00 };                   // cmp dword ptr [rbp-0x38],0
    const std::uint8_t kDoneCode[] = {
        0x48, 0x8B, 0xCF,
        0xE8, 0x00, 0x00, 0x00, 0x00,
        0x48, 0x8D, 0x8B, 0xD8, 0x63, 0x02, 0x00,
        0x41, 0xB8, 0x30, 0x00, 0x00, 0x00,
        0x48, 0x8B, 0xD7,
        0xE8, 0x00, 0x00, 0x00, 0x00,
        0x48, 0x8D, 0x8B, 0x00, 0x64, 0x02, 0x00,
        0xE8, 0x00, 0x00, 0x00, 0x00,
        0x48, 0x8D, 0x8B, 0x18, 0x63, 0x02, 0x00,   // lea rcx,[rbx+0x26318]
        0xE8, 0x00, 0x00, 0x00, 0x00 };             // call pfimpl::AtomicDecrement32WR
    const std::uint8_t kAtomicDecrementCode[] = { 0x83, 0xC8, 0xFF, 0xF0, 0x0F, 0xC1, 0x01, 0xC3 };

    constexpr std::size_t kInvalidCallOff    = 3;
    constexpr std::size_t kDeallocateCallOff = 24;
    constexpr std::size_t kPopFrontCallOff   = 36;
    constexpr std::size_t kDecrementCallOff  = 48;
    constexpr std::size_t kCallLen           = 5;

    const std::size_t kAbortStarts[] = { 0, 3, 8, 15, 21, 24, 29, 36, 41, 42 };
    const std::size_t kDoneStarts[]  = { 0, 3, 8, 15, 21, 24, 29, 36, 41, 48 };

    constexpr std::size_t kThunkLen       = 0x30;
    constexpr std::size_t kGivenBackOff   = 0x28;
    constexpr std::size_t kAlreadyZeroOff = 0x2C;

    std::uint8_t*          g_Thunk = nullptr;
    NativeCallPatch        g_Patch = {};
    std::atomic<bool>      g_StopCheck{ false };
    std::atomic<bool>      g_CheckThreadStarted{ false };
    std::atomic<unsigned>  g_CheckGeneration{ 0 };

    bool IsRel32Byte(std::size_t i, const std::size_t* calls, std::size_t callCount)
    {
        for (std::size_t c = 0; c < callCount; ++c)
            if (i > calls[c] && i < calls[c] + kCallLen)
                return true;
        return false;
    }

    bool CodeMatches(const std::uint8_t* code, const std::uint8_t* expected, std::size_t len)
    {
        const std::size_t calls[] = { kInvalidCallOff, kDeallocateCallOff, kPopFrontCallOff, kDecrementCallOff };
        for (std::size_t i = 0; i < len; ++i)
            if (!IsRel32Byte(i, calls, len > kDecrementCallOff ? 4 : 3) && code[i] != expected[i])
                return false;
        return true;
    }

    std::uint8_t* CallTarget(std::uint8_t* site, const std::uint8_t* code, std::size_t callOff)
    {
        std::int32_t rel = 0;
        std::memcpy(&rel, code + callOff + 1, sizeof(rel));
        return site + callOff + kCallLen + rel;
    }

    void DescribeCode(char* out, std::size_t cap, const char* name, const std::uint8_t* site, const std::uint8_t* code,
                      std::size_t len, const std::size_t* starts, std::size_t startCount)
    {
        std::size_t at = 0;
        const int head = std::snprintf(out, cap, "%s:", name);
        if (head > 0)
            at = static_cast<std::size_t>(head);
        for (std::size_t i = 0; i < len && at < cap; ++i)
        {
            const int wrote = std::snprintf(out + at, cap - at, " %02X", code[i]);
            if (wrote <= 0)
                break;
            at += static_cast<std::size_t>(wrote);
        }
        for (std::size_t s = 0; s < startCount; ++s)
        {
            if (!EntryJumpTarget(reinterpret_cast<std::uintptr_t>(site + starts[s])))
                continue;
            char jump[160] = {};
            DescribeFunctionEntry(site + starts[s], jump, sizeof(jump));
            const std::size_t used = std::strlen(out);
            if (used < cap)
                std::snprintf(out + used, cap - used, "; %s", jump);
            return;
        }
    }

    bool InsideFunction(const std::uint8_t* function, const std::uint8_t* site)
    {
        DWORD64 imageBase = 0;
        const auto* entry = RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(function), &imageBase, nullptr);
        if (!entry)
            return true;
        const auto at = reinterpret_cast<DWORD64>(site);
        return at >= imageBase + entry->BeginAddress && at < imageBase + entry->EndAddress;
    }

    std::int64_t Distance(const void* from, const void* to)
    {
        return static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(to))
             - static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(from));
    }

    bool BuildThunk(std::uint8_t* popFront)
    {
        if (g_Thunk)
            return true;
        auto* thunk = static_cast<std::uint8_t*>(
            HookArena::AllocateNear(reinterpret_cast<std::uintptr_t>(g_Patch.target), kThunkLen));
        if (!thunk || (reinterpret_cast<std::uintptr_t>(thunk) & 3) != 0)
            return false;
        const std::int64_t toPopFront = Distance(thunk + 0x25, popFront);
        const std::int64_t toThunk    = Distance(static_cast<std::uint8_t*>(g_Patch.target) + kCallLen, thunk);
        if (toPopFront < INT32_MIN || toPopFront > INT32_MAX || toThunk < INT32_MIN || toThunk > INT32_MAX)
            return false;

        std::uint8_t code[kThunkLen] = {
            0x83, 0xBB, 0, 0, 0, 0, 0x00,         // cmp dword ptr [rbx+<load count>],0
            0x7E, 0x10,                           // jle already zero
            0xF0, 0xFF, 0x8B, 0, 0, 0, 0,         // lock dec dword ptr [rbx+<load count>]
            0xF0, 0xFF, 0x05, 0x11, 0, 0, 0,      // lock inc dword ptr [given back]
            0xEB, 0x07,                           // jmp to PopFront
            0xF0, 0xFF, 0x05, 0x0C, 0, 0, 0,      // lock inc dword ptr [already zero]
            0xE9, 0, 0, 0, 0,                     // jmp PopFront
            0xCC, 0xCC, 0xCC };
        std::memcpy(code + 2, &kLoadCountOff, sizeof(kLoadCountOff));
        std::memcpy(code + 12, &kLoadCountOff, sizeof(kLoadCountOff));
        const std::int32_t rel = static_cast<std::int32_t>(toPopFront);
        std::memcpy(code + 0x21, &rel, sizeof(rel));
        std::memcpy(thunk, code, sizeof(code));
        FlushInstructionCache(GetCurrentProcess(), thunk, kThunkLen);
        g_Thunk = thunk;
        return true;
    }

    std::uint32_t ThunkCounter(std::size_t off)
    {
        std::uint32_t value = 0;
        if (g_Thunk)
            ReadEntryBytesSEH(g_Thunk + off, &value, sizeof(value));
        return value;
    }

    void NoteSiteChange(bool& noted)
    {
        std::uint8_t now[kCallLen] = {};
        std::uint8_t ours[kCallLen] = { 0xE8 };
        std::uint8_t* site = static_cast<std::uint8_t*>(g_Patch.target);
        if (noted || !g_Patch.installed || !ReadEntryBytesSEH(site, now, sizeof(now)))
            return;
        const std::int32_t rel = static_cast<std::int32_t>(Distance(site + kCallLen, g_Thunk));
        std::memcpy(ours + 1, &rel, sizeof(rel));
        if (std::memcmp(now, ours, sizeof(now)) == 0)
            return;
        noted = true;
        char found[192] = {};
        const std::size_t start = 0;
        DescribeCode(found, sizeof(found), "failed texture pack cleanup", site, now, sizeof(now), &start, 1);
        LogWarn("TexturePackAbort: another module changed the failed texture pack code after V_FrameWork set it (%s)",
                found);
    }

    DWORD WINAPI PackAbortCheckThread(LPVOID)
    {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        std::uint32_t givenLogged = ThunkCounter(kGivenBackOff);
        std::uint32_t zeroLogged  = ThunkCounter(kAlreadyZeroOff);
        bool          siteNoted   = false;
        unsigned      generation  = 0;
        for (;;)
        {
            Sleep(1000);
            if (g_StopCheck.load())
                continue;
            const unsigned current = g_CheckGeneration.load();
            if (current != generation)
            {
                generation = current;
                siteNoted  = false;
            }
            NoteSiteChange(siteNoted);

            const std::uint32_t given = ThunkCounter(kGivenBackOff);
            if (given != givenLogged)
            {
                givenLogged = given;
                LogWarn("TexturePackAbort: %u texture pack(s) failed to load so far, their load requests given back",
                        given);
            }
            const std::uint32_t zero = ThunkCounter(kAlreadyZeroOff);
            if (zero != zeroLogged)
            {
                zeroLogged = zero;
                LogWarn("TexturePackAbort: %u failed texture pack(s) found the load count already 0 and left it at 0, "
                        "another mod may give the count back too", zero);
            }
        }
    }

    void StartCheckThread()
    {
        if (g_CheckThreadStarted.exchange(true))
            return;
        HMODULE pinnedModule = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(&PackAbortCheckThread), &pinnedModule))
            LogWarn("TexturePackAbort: cannot pin the module for the check thread (err %lu)", GetLastError());
        HANDLE thread = CreateThread(nullptr, 0, &PackAbortCheckThread, nullptr, 0, nullptr);
        if (thread)
            CloseHandle(thread);
        else
        {
            g_CheckThreadStarted = false;
            LogWarn("TexturePackAbort: check thread failed to start, failed texture packs are not logged");
        }
    }

    void LogLeftVanilla(const char* reason)
    {
        LogWarn("TexturePackAbort: %s, failed texture packs still make every later mission start wait 30 s", reason);
    }
}

bool Install_TexturePackAbort()
{
    if (g_Patch.installed)
        return true;

    auto* doExec = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.DgTextureStreamer_DoExecPftxsLoad));
    auto* abort  = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.DgTextureStreamer_DoExecPftxsLoad_AbortCleanupSite));
    auto* done   = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.DgTextureStreamer_DoExecPftxsLoad_DoneCleanupSite));
    char  reason[512] = {};
    if (!doExec || !abort || !done)
    {
        std::snprintf(reason, sizeof(reason), "addresses missing on %s", GetGameBuildName(gGameBuild));
        LogLeftVanilla(reason);
        return true;
    }

    std::uint8_t abortCode[sizeof(kAbortCode)] = {};
    std::uint8_t doneCode[sizeof(kDoneCode)]   = {};
    if (!ReadEntryBytesSEH(abort, abortCode, sizeof(abortCode)) || !ReadEntryBytesSEH(done, doneCode, sizeof(doneCode)))
    {
        LogLeftVanilla("its code is unreadable");
        return true;
    }
    if (!CodeMatches(abortCode, kAbortCode, sizeof(kAbortCode)))
    {
        char found[384] = {};
        DescribeCode(found, sizeof(found), "failed pack cleanup", abort, abortCode, sizeof(abortCode), kAbortStarts,
                     sizeof(kAbortStarts) / sizeof(kAbortStarts[0]));
        std::snprintf(reason, sizeof(reason), "code unexpected (%s)", found);
        LogLeftVanilla(reason);
        return true;
    }
    if (!CodeMatches(doneCode, kDoneCode, sizeof(kDoneCode)))
    {
        char found[384] = {};
        DescribeCode(found, sizeof(found), "loaded pack cleanup", done, doneCode, sizeof(doneCode), kDoneStarts,
                     sizeof(kDoneStarts) / sizeof(kDoneStarts[0]));
        std::snprintf(reason, sizeof(reason), "code unexpected (%s)", found);
        LogLeftVanilla(reason);
        return true;
    }

    std::uint8_t* invalid    = CallTarget(abort, abortCode, kInvalidCallOff);
    std::uint8_t* deallocate = CallTarget(abort, abortCode, kDeallocateCallOff);
    std::uint8_t* popFront   = CallTarget(abort, abortCode, kPopFrontCallOff);
    std::uint8_t* decrement  = CallTarget(done, doneCode, kDecrementCallOff);
    if (invalid != CallTarget(done, doneCode, kInvalidCallOff) || deallocate != CallTarget(done, doneCode, kDeallocateCallOff)
        || popFront != CallTarget(done, doneCode, kPopFrontCallOff)
        || !InsideFunction(doExec, abort) || !InsideFunction(doExec, done))
    {
        LogLeftVanilla("the failed and loaded pack paths no longer call the same functions");
        return true;
    }
    std::uint8_t decrementCode[sizeof(kAtomicDecrementCode)] = {};
    if (!ReadEntryBytesSEH(decrement, decrementCode, sizeof(decrementCode))
        || std::memcmp(decrementCode, kAtomicDecrementCode, sizeof(decrementCode)) != 0)
    {
        char found[192] = {};
        DescribeFunctionEntry(decrement, found, sizeof(found));
        std::snprintf(reason, sizeof(reason), "the load count decrement is not vanilla (%s)", found);
        LogLeftVanilla(reason);
        return true;
    }

    const struct { const char* name; std::uint8_t* entry; } entries[] = {
        { "DgTextureStreamer::DoExecPftxsLoad", doExec },
        { "PftxsLoadRequest::Invalid", invalid },
        { "the .pftxs request Deallocate", deallocate },
        { "the .pftxs queue PopFront", popFront } };
    for (const auto& function : entries)
    {
        if (!EntryJumpTarget(reinterpret_cast<std::uintptr_t>(function.entry)))
            continue;
        char found[192] = {};
        DescribeFunctionEntry(function.entry, found, sizeof(found));
        std::snprintf(reason, sizeof(reason), "%s is hooked by another module (%s), it may give the count back already",
                      function.name, found);
        LogLeftVanilla(reason);
        return true;
    }

    g_Patch = NativeCallPatch{};
    g_Patch.target = abort + kPopFrontCallOff;
    g_Patch.size   = kCallLen;
    std::memcpy(g_Patch.expected, abortCode + kPopFrontCallOff, kCallLen);
    if (!BuildThunk(popFront))
    {
        LogError("TexturePackAbort: no code page near the texture pack loader, failed texture packs still make every "
                 "later mission start wait 30 s");
        return false;
    }
    g_Patch.destination = g_Thunk;

    DWORD error = 0;
    for (int attempt = 0; attempt < 50; ++attempt)
    {
        if (InstallNativeCallPatches(&g_Patch, 1))
        {
            LogInfo("TexturePackAbort: a texture pack that fails to load now gives back its load request (vanilla keeps "
                    "it, and every later mission start waits 30 s for textures)");
            g_CheckGeneration.fetch_add(1);
            g_StopCheck = false;
            StartCheckThread();
            return true;
        }
        error = GetLastError();
        if (!NativePatchErrorIsTransient(error))
            break;
        Sleep(4);
    }
    LogError("TexturePackAbort: patch refused (%s), failed texture packs still make every later mission start wait 30 s",
             NativePatchErrorName(error));
    return false;
}

void Uninstall_TexturePackAbort()
{
    g_StopCheck = true;
    if (g_Patch.installed && !RemoveNativeCallPatches(&g_Patch, 1))
        LogWarn("TexturePackAbort: the failed texture pack code was changed by another module, left as it is (%s)",
                NativePatchErrorName(GetLastError()));
}
