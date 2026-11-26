#include <pch.h>

#include "CommandPost2Impl_Initialize.h"

#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using Initialize_t = void*(__fastcall*)(std::uint8_t* commandPost, const std::uint8_t* desc);

    constexpr std::uint32_t kVanillaMembers = 360;
    constexpr std::uint32_t kMembers        = 512;
    constexpr std::size_t   kTableState     = 0x1DA;   // bit 0: the member tables are allocated
    constexpr std::size_t   kInitializeSize = 0x1C00;
    constexpr std::size_t   kMostSites      = 4;

    const std::uint8_t kInitializeEntry[] = { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89,
                                              0x7C, 0x24, 0x20, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57 };

    struct CapacityPattern
    {
        const char*   name;
        std::uint8_t  vanilla[8];
        std::uint8_t  enlarged[8];
        std::size_t   size;
        std::size_t   count;
        std::uint8_t* sites[kMostSites];
    };

    enum class SiteState { Vanilla, Enlarged, Unknown };

    CapacityPattern g_Patterns[] = {
        { "member table counts",        { 0xBA, 0x68, 0x01, 0x00, 0x00 },                   { 0xBA, 0x00, 0x02, 0x00, 0x00 },                   5, 4, {} },   // mov edx,360
        { "4-byte tables, sizing pass", { 0x48, 0x05, 0xA0, 0x05, 0x00, 0x00 },             { 0x48, 0x05, 0x00, 0x08, 0x00, 0x00 },             6, 4, {} },   // add rax,360*4
        { "4-byte tables, build pass",  { 0x48, 0x8D, 0x81, 0xA0, 0x05, 0x00, 0x00 },       { 0x48, 0x8D, 0x81, 0x00, 0x08, 0x00, 0x00 },       7, 4, {} },   // lea rax,[rcx+360*4]
        { "2-byte tables, sizing pass", { 0x48, 0x05, 0xD0, 0x02, 0x00, 0x00 },             { 0x48, 0x05, 0x00, 0x04, 0x00, 0x00 },             6, 2, {} },   // add rax,360*2
        { "2-byte tables, build pass",  { 0x48, 0x8D, 0x81, 0xD0, 0x02, 0x00, 0x00 },       { 0x48, 0x8D, 0x81, 0x00, 0x04, 0x00, 0x00 },       7, 2, {} },   // lea rax,[rcx+360*2]
        { "member setup loop",          { 0xBB, 0x68, 0x01, 0x00, 0x00 },                   { 0xBB, 0x00, 0x02, 0x00, 0x00 },                   5, 1, {} },   // mov ebx,360
        { "synchronizer records",       { 0xC7, 0x44, 0x24, 0x64, 0x68, 0x01, 0x00, 0x00 }, { 0xC7, 0x44, 0x24, 0x64, 0x00, 0x02, 0x00, 0x00 }, 8, 1, {} },   // mov dword [rsp+0x64],360
    };

    Initialize_t               g_Orig = nullptr;
    void*                      g_Target = nullptr;
    std::uint8_t*              g_CodeStart = nullptr;
    std::size_t                g_CodeSpan = 0;
    std::mutex                 g_CodeLock;   // the sizes may only change while no Initialize runs
    std::atomic<std::uint8_t*> g_EnlargedCommandPost{ nullptr };
    std::atomic<std::uint32_t> g_BuildCount{ 0 };
    std::atomic<bool>          g_CodeChangeReported{ false };

    bool FindSites(std::uint8_t* code)
    {
        static std::uint8_t body[kInitializeSize];
        if (!ReadEntryBytesSEH(code, body, sizeof(body)))
            return false;
        std::uint8_t* first = code + sizeof(body);
        std::uint8_t* end = code;
        for (CapacityPattern& pattern : g_Patterns)
        {
            std::size_t found = 0;
            for (std::size_t offset = 0; offset + pattern.size <= sizeof(body); ++offset)
            {
                if (std::memcmp(body + offset, pattern.vanilla, pattern.size) != 0)
                    continue;
                if (found < kMostSites)
                    pattern.sites[found] = code + offset;
                ++found;
            }
            if (found != pattern.count)
            {
                LogWarn("CommandPost: Initialize differs on this build (%s found %zu times, expected %zu), the member tables stay at %u",
                        pattern.name, found, pattern.count, kVanillaMembers);
                return false;
            }
            for (std::size_t i = 0; i < found; ++i)
            {
                first = (std::min)(first, pattern.sites[i]);
                end = (std::max)(end, pattern.sites[i] + pattern.size);
            }
        }
        g_CodeStart = first;
        g_CodeSpan = static_cast<std::size_t>(end - first);
        return true;
    }

    SiteState ReadState(const CapacityPattern& pattern, const std::uint8_t* site)
    {
        std::uint8_t live[8] = {};
        if (!ReadEntryBytesSEH(site, live, pattern.size))
            return SiteState::Unknown;
        if (std::memcmp(live, pattern.vanilla, pattern.size) == 0)
            return SiteState::Vanilla;
        if (std::memcmp(live, pattern.enlarged, pattern.size) == 0)
            return SiteState::Enlarged;
        return SiteState::Unknown;
    }

    // every site or none: both passes must size the tables the same
    bool SetCapacity(bool enlarged)
    {
        bool change = false;
        for (const CapacityPattern& pattern : g_Patterns)
        {
            for (std::size_t i = 0; i < pattern.count; ++i)
            {
                const SiteState state = ReadState(pattern, pattern.sites[i]);
                if (state == SiteState::Unknown)
                {
                    if (!g_CodeChangeReported.exchange(true))
                        LogError("CommandPost: Initialize %s code is not vanilla, the member tables stay as they are", pattern.name);
                    return false;
                }
                change |= (state == SiteState::Enlarged) != enlarged;
            }
        }
        if (!change)
            return true;

        DWORD oldProtect = 0;
        if (!VirtualProtect(g_CodeStart, g_CodeSpan, PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            LogError("CommandPost: VirtualProtect failed (err %lu), the member tables stay as they are", GetLastError());
            return false;
        }
        for (const CapacityPattern& pattern : g_Patterns)
            for (std::size_t i = 0; i < pattern.count; ++i)
                std::memcpy(pattern.sites[i], enlarged ? pattern.enlarged : pattern.vanilla, pattern.size);
        DWORD restored = 0;
        VirtualProtect(g_CodeStart, g_CodeSpan, oldProtect, &restored);
        FlushInstructionCache(GetCurrentProcess(), g_CodeStart, g_CodeSpan);
        return true;
    }

    bool TablesAllocated(const std::uint8_t* commandPost)
    {
        std::uint16_t state = 0;
        return ReadEntryBytesSEH(commandPost + kTableState, &state, sizeof(state)) && (state & 1);
    }

    void* __fastcall hkInitialize(std::uint8_t* commandPost, const std::uint8_t* desc)
    {
        std::lock_guard<std::mutex> lock(g_CodeLock);
        const bool fresh = commandPost && !TablesAllocated(commandPost);
        const bool builtEnlarged = commandPost && commandPost == g_EnlargedCommandPost.load(std::memory_order_acquire);
        const bool wanted = fresh ? !MissionCodeGuard::IsFob() : builtEnlarged;   // a second Initialize reuses the tables it built
        const bool enlarged = SetCapacity(wanted) && wanted;
        void* result = g_Orig(commandPost, desc);
        if (enlarged)
            SetCapacity(false);
        if (fresh)
        {
            g_EnlargedCommandPost.store(enlarged ? commandPost : nullptr, std::memory_order_release);
            g_BuildCount.fetch_add(1, std::memory_order_acq_rel);
            LogDebug("CommandPost: mission %u: member tables built for %u members",
                     static_cast<unsigned>(MissionCodeGuard::GetCurrentMissionCode()), enlarged ? kMembers : kVanillaMembers);
        }
        return result;
    }
}

bool Install_CommandPost2Impl_Initialize_Hook()
{
    if (g_Target)
        return true;
    auto* target = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.CommandPost2Impl_Initialize));
    if (!target)
    {
        LogWarn("CommandPost: Initialize address missing on %s, the member tables stay at %u", GetGameBuildName(gGameBuild), kVanillaMembers);
        return true;
    }
    const HookEntry state = CheckHookEntry(target, kInitializeEntry, sizeof(kInitializeEntry));
    if (state == HookEntry::Unknown)
    {
        char desc[160] = {};
        DescribeFunctionEntry(target, desc, sizeof(desc));
        LogWarn("CommandPost: Initialize code differs on this build (%s), the member tables stay at %u", desc, kVanillaMembers);
        return true;
    }
    if (!FindSites(target))
        return true;
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("CommandPost: Initialize", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkInitialize), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("CommandPost: Initialize hook failed, the member tables stay at %u", kVanillaMembers);
        return false;
    }
    g_Target = target;
    LogInfo("CommandPost: member tables %u -> %u members outside FOB", kVanillaMembers, kMembers);
    return true;
}

void Uninstall_CommandPost2Impl_Initialize_Hook()
{
    if (!g_Target)
        return;
    DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    std::lock_guard<std::mutex> lock(g_CodeLock);
    SetCapacity(false);   // tables built bigger keep their memory until the game frees them
}

std::uint32_t GetCommandPostMemberCapacity(const std::uint8_t* commandPost)
{
    if (commandPost && commandPost == g_EnlargedCommandPost.load(std::memory_order_acquire))
        return kMembers;
    return kVanillaMembers;
}

std::uint32_t GetCommandPostBuildCount()
{
    return g_BuildCount.load(std::memory_order_acquire);
}
