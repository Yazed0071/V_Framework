#include "pch.h"

#include "BuddyCodeCave.h"

#include <Windows.h>
#include <cstring>

#include "HookArena.h"
#include "HookUtils.h"
#include "log.h"

void CaveCode::Imm64(std::uint64_t v)
{
    for (int i = 0; i < 8; ++i)
        bytes.push_back(static_cast<std::uint8_t>(v >> (i * 8)));
}

std::size_t CaveCode::Jcc8(std::uint8_t opcode)
{
    bytes.push_back(opcode);
    bytes.push_back(0);
    return bytes.size() - 1;
}

void CaveCode::Bind8(std::size_t jccAt)
{
    bytes[jccAt] = static_cast<std::uint8_t>(bytes.size() - (jccAt + 1));
}

void CaveCode::JmpAbs(std::uintptr_t target)
{
    Emit({ 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 }); // jmp [rip+0], target right after
    Imm64(target);
}

bool InstallBuddyCodeCave(BuddyCodeCave& cave, std::uintptr_t siteAbs,
                          const std::uint8_t* expected, std::size_t size, const CaveCode& code)
{
    if (cave.installed)
        return true;
    if (!siteAbs || size < 5 || size > sizeof(cave.original))
    {
        LogError("BuddyOutfit: %s site missing on this build, that part is off", cave.name);
        return false;
    }

    auto* site = static_cast<std::uint8_t*>(ResolveGameAddress(siteAbs));
    if (!site || std::memcmp(site, expected, size) != 0)
    {
        LogError("BuddyOutfit: %s site bytes differ (another mod patched it?), that part is off", cave.name);
        return false;
    }

    auto* thunk = static_cast<std::uint8_t*>(
        HookArena::AllocateNear(reinterpret_cast<std::uintptr_t>(site), code.bytes.size()));
    if (!thunk)
    {
        LogError("BuddyOutfit: no code page near the %s site, that part is off", cave.name);
        return false;
    }
    std::memcpy(thunk, code.bytes.data(), code.bytes.size());
    FlushInstructionCache(GetCurrentProcess(), thunk, code.bytes.size());

    const std::int64_t rel = static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(thunk))
                           - static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(site) + 5);
    if (rel < INT32_MIN || rel > INT32_MAX)
    {
        LogError("BuddyOutfit: %s thunk out of jump range, that part is off", cave.name);
        return false;
    }

    std::uint8_t patch[sizeof(cave.original)];
    std::memset(patch, 0x90, sizeof(patch));
    patch[0] = 0xE9;
    const std::int32_t rel32 = static_cast<std::int32_t>(rel);
    std::memcpy(patch + 1, &rel32, sizeof(rel32));

    DWORD old = 0;
    if (!VirtualProtect(site, size, PAGE_EXECUTE_READWRITE, &old))
    {
        LogError("BuddyOutfit: %s site unprotect failed, that part is off", cave.name);
        return false;
    }
    std::memcpy(cave.original, site, size);
    std::memcpy(site, patch, size);
    VirtualProtect(site, size, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, size);

    cave.site = site;
    cave.size = size;
    cave.thunk = thunk;
    cave.installed = true;
    return true;
}

void UninstallBuddyCodeCave(BuddyCodeCave& cave)
{
    if (!cave.installed || !cave.site)
        return;
    DWORD old = 0;
    if (VirtualProtect(cave.site, cave.size, PAGE_EXECUTE_READWRITE, &old))
    {
        std::memcpy(cave.site, cave.original, cave.size);
        VirtualProtect(cave.site, cave.size, old, &old);
        FlushInstructionCache(GetCurrentProcess(), cave.site, cave.size);
    }
    cave.installed = false;
    cave.site = nullptr;
}
