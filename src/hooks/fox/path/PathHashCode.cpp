#include <pch.h>
#include "PathHashCode.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using PathHashCode_t = std::uint64_t(__fastcall*)(const char* path);

    const std::uint8_t kPathHashCodeEntry[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18,
                                                0x57, 0x48, 0x83, 0xEC, 0x20, 0xBA, 0x2F, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xF1 };   // mov edx,'/' for its strrchr
    const std::uint8_t kPathCodeEntry[]     = { 0x48, 0x8B, 0xC1, 0x48, 0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x48, 0x85, 0xC9, 0x74, 0x08,
                                                0x48, 0x8B, 0xD0, 0xE9, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xC8 };               // the path resolver if there is one, else its own hash
    const std::uint8_t kPathCodeMask[]      = { 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1 };

    constexpr unsigned      kExtensionShift = 51;      // the extension's hash sits above the 51 bits of the path's
    constexpr std::uint64_t kFpkExtension   = 0xA45;   // hash of "fpk"
    constexpr std::size_t   kMaxNames       = 32768;

    PathHashCode_t g_OrigPathHashCode   = nullptr;
    void*          g_PathHashCodeTarget = nullptr;
    PathHashCode_t g_OrigPathCode       = nullptr;
    void*          g_PathCodeTarget     = nullptr;
    std::mutex     g_NamesMutex;
    std::unordered_map<std::uint64_t, std::string> g_PackNames;

    void RememberPack(const char* path, std::uint64_t code)
    {
        if (!path || (code >> kExtensionShift) != kFpkExtension)
            return;
        try
        {
            std::lock_guard<std::mutex> lock(g_NamesMutex);
            if (g_PackNames.size() < kMaxNames)
                g_PackNames.emplace(code, path);
        }
        catch (...)
        {
        }
    }

    std::uint64_t __fastcall hkPathHashCode(const char* path)
    {
        const std::uint64_t code = g_OrigPathHashCode(path);
        RememberPack(path, code);
        return code;
    }

    std::uint64_t __fastcall hkPathCode(const char* path)
    {
        const std::uint64_t code = g_OrigPathCode(path);
        RememberPack(path, code);
        return code;
    }

    void InstallPathCode()
    {
        void* target = ResolveGameAddress(gAddr.Fox_PathCodeFromString);
        const HookEntry state = CheckHookEntryMasked(target, kPathCodeEntry, kPathCodeMask, sizeof(kPathCodeEntry));
        if (state == HookEntry::Unknown)
        {
            char entry[160] = {};
            DescribeFunctionEntry(target, entry, sizeof(entry));
            LogError("LoadWatch: fox path code differs on %s (%s), most stuck packs are named by their path code only",
                     GetGameBuildName(gGameBuild), entry);
            return;
        }
        if (state == HookEntry::ForeignHook)
            LogHookedOnTop("LoadWatch: fox path code", target);
        if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkPathCode), reinterpret_cast<void**>(&g_OrigPathCode)))
        {
            LogError("LoadWatch: fox path code hook failed at %p, most stuck packs are named by their path code only", target);
            return;
        }
        g_PathCodeTarget = target;
    }
}

bool Install_PathHashCode_Hook()
{
    if (!g_PathCodeTarget)
        InstallPathCode();
    if (g_PathHashCodeTarget)
        return true;

    void* target = ResolveGameAddress(gAddr.PathHashCode);
    const HookEntry state = CheckHookEntry(target, kPathHashCodeEntry, sizeof(kPathHashCodeEntry));
    if (state == HookEntry::Unknown)
    {
        char entry[160] = {};
        DescribeFunctionEntry(target, entry, sizeof(entry));
        LogError("LoadWatch: path_hash_code differs on %s (%s), stuck packs are named by their path code only",
                 GetGameBuildName(gGameBuild), entry);
        return true;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("LoadWatch: path_hash_code", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkPathHashCode), reinterpret_cast<void**>(&g_OrigPathHashCode)))
    {
        LogError("LoadWatch: path_hash_code hook failed at %p, stuck packs are named by their path code only", target);
        return true;
    }
    g_PathHashCodeTarget = target;
    return true;
}

void Uninstall_PathHashCode_Hook()
{
    if (g_PathCodeTarget)
        DisableAndRemoveHook(g_PathCodeTarget);
    g_PathCodeTarget = nullptr;
    if (g_PathHashCodeTarget)
        DisableAndRemoveHook(g_PathHashCodeTarget);
    g_PathHashCodeTarget = nullptr;
}

std::string PackPathFor(std::uint64_t pathCode)
{
    std::lock_guard<std::mutex> lock(g_NamesMutex);
    const auto found = g_PackNames.find(pathCode);
    return found != g_PackNames.end() ? found->second : std::string();
}
