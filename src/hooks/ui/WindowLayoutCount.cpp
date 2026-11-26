#include <pch.h>
#include "WindowLayoutCount.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookArena.h"
#include "HookUtils.h"
#include "NativeCallPatch.h"
#include "log.h"
#include "../buddy/BuddyCodeCave.h"

namespace
{
    constexpr std::size_t  kCountRead = 0x61;   // CreateLayouts: movzx eax, byte ptr [r14 + 0x2A], the WindowInfo's layout count
    const std::uint8_t     kCountReadBytes[] = { 0x41, 0x0F, 0xB6, 0x46, 0x2A };

    const void*     g_WideInfo  = nullptr;   // read by the cave
    std::uint32_t   g_WideCount = 0;         // read by the cave
    NativeCodePatch g_Patch{};
    std::uint8_t*   g_Site = nullptr;
    std::atomic<bool> g_Live{ false };

    CaveCode BuildCave(std::uintptr_t back)
    {
        CaveCode code;
        code.Emit({ 0x41, 0x0F, 0xB6, 0x46, 0x2A });   // movzx eax, byte ptr [r14 + 0x2A]
        code.Emit({ 0x51 });                           // push rcx
        code.Emit({ 0x48, 0xB9 });                     // mov rcx, &g_WideInfo
        code.Imm64(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&g_WideInfo)));
        code.Emit({ 0x4C, 0x3B, 0x31 });               // cmp r14, [rcx]
        const std::size_t otherWindow = code.Jcc8(0x75);
        code.Emit({ 0x48, 0xB9 });                     // mov rcx, &g_WideCount
        code.Imm64(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&g_WideCount)));
        code.Emit({ 0x8B, 0x01 });                     // mov eax, [rcx]
        code.Bind8(otherWindow);
        code.Emit({ 0x59 });                           // pop rcx
        code.JmpAbs(back);
        return code;
    }

    bool SiteMatchesSeh(const std::uint8_t* site)
    {
        __try
        {
            return std::memcmp(site, kCountReadBytes, sizeof(kCountReadBytes)) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
}

bool Install_WindowLayoutCount()
{
    if (g_Site)
        return true;

    auto* function = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.WindowResourceCreator_CreateLayouts));
    std::uint8_t* site = function ? function + kCountRead : nullptr;
    if (!site || !SiteMatchesSeh(site))
    {
        LogError("V_UixUtility: the window layout count read is not the known code on %s, CreateWindow takes 255 layouts at most",
                 GetGameBuildName(gGameBuild));
        return false;
    }

    const CaveCode code = BuildCave(reinterpret_cast<std::uintptr_t>(site) + sizeof(kCountReadBytes));
    auto* thunk = static_cast<std::uint8_t*>(HookArena::AllocateNear(reinterpret_cast<std::uintptr_t>(site), code.bytes.size()));
    const std::int64_t rel = thunk
        ? static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(thunk)) - static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(site) + 5)
        : 0;
    if (!thunk || rel < INT32_MIN || rel > INT32_MAX)
    {
        LogError("V_UixUtility: no code page near the window layout count read, CreateWindow takes 255 layouts at most");
        return false;
    }
    std::memcpy(thunk, code.bytes.data(), code.bytes.size());
    FlushInstructionCache(GetCurrentProcess(), thunk, code.bytes.size());

    g_Patch = NativeCodePatch{};
    g_Patch.target = site;
    g_Patch.size = 5;
    std::memcpy(g_Patch.expected, kCountReadBytes, 5);
    g_Patch.replacement[0] = 0xE9;
    const std::int32_t rel32 = static_cast<std::int32_t>(rel);
    std::memcpy(g_Patch.replacement + 1, &rel32, sizeof(rel32));

    DWORD error = ERROR_SUCCESS;
    for (int attempt = 0; attempt < 50; ++attempt)
    {
        if (InstallNativeCodePatches(&g_Patch, 1, nullptr, site, site + 5))
        {
            g_Site = site;
            g_Live.store(true, std::memory_order_release);
            return true;
        }
        error = GetLastError();
        if (!NativePatchErrorIsTransient(error))
            break;
        Sleep(4);
    }
    LogError("V_UixUtility: window layout count patch refused (%s), CreateWindow takes 255 layouts at most",
             NativePatchErrorName(error));
    return false;
}

void Uninstall_WindowLayoutCount()
{
    g_Live.store(false, std::memory_order_release);
    if (!g_Site)
        return;
    RemoveNativeCodePatches(&g_Patch, 1, nullptr, g_Site, g_Site + 5);
    g_Site = nullptr;
}

bool WindowLayoutCount_Live()
{
    return g_Live.load(std::memory_order_acquire);
}

void WindowLayoutCount_Begin(const void* windowInfo, std::uint32_t count)
{
    if (count <= 0xFF)
        return;
    g_WideCount = count;
    g_WideInfo = windowInfo;
}

void WindowLayoutCount_End()
{
    g_WideInfo = nullptr;
    g_WideCount = 0;
}
