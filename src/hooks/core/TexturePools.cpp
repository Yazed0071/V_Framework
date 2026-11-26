#include <pch.h>
#include "TexturePools.h"

#include <Windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

#include "AddressSet.h"
#include "HookArena.h"
#include "HookUtils.h"
#include "NativeCallPatch.h"
#include "TextureProxyRaise.h"
#include "TextureStoragePool.h"
#include "log.h"

namespace
{
    constexpr std::int32_t  kVanillaSlots   = 0x1310;   // 4880
    constexpr std::int32_t  kNewSlots       = 8192;
    constexpr std::int32_t  kKeptFree       = 16;       // a new texture only gets a slot while more than 16 are free
    constexpr std::uint32_t kSlotArrayBytes = kNewSlots * static_cast<std::uint32_t>(sizeof(float));
    constexpr std::uint32_t kPool0Needed    = 0x1C000000;   // 448 MiB, TextureStoragePool's pool 0

    constexpr std::size_t kCapacityOff     = 0x10;   // TextureManager: slot count
    constexpr std::size_t kUsedOff         = 0x14;   // TextureManager: slots in use
    constexpr std::size_t kSlotStatesOff   = 0x20;   // TextureManager: 12-byte record per slot, state byte at +8
    constexpr std::size_t kJobScratchOff   = 0x1C;   // JobPool: scratch bytes every job executor gets
    constexpr std::size_t kProxyMaxLiveOff = 0x04;   // texture streamer proxy pool: most proxies alive at once
    constexpr std::size_t kProxyLiveOff    = 0x20;   // texture streamer proxy pool: proxies alive now

    constexpr std::uint8_t kLeaRcxRip = 0x0D;   // ModRM of lea rcx,[rip+rel32]
    constexpr std::uint8_t kLeaRaxRip = 0x05;   // ModRM of lea rax,[rip+rel32]
    constexpr std::size_t  kLeaLen    = 7;

    const std::uint8_t kCreationCode[] = {
        0x48, 0x85, 0xC0, 0x74, 0x0F, 0xBA, 0x10, 0x13, 0x00, 0x00,   // test rax,rax / jz +0F / mov edx,<slots>
        0x48, 0x8B, 0xC8, 0xE8, 0x00, 0x00, 0x00, 0x00, 0xEB, 0x03,   // mov rcx,rax / call <TextureManager ctor> / jmp +3
        0x48, 0x8B, 0xC3, 0x48, 0x89, 0x05, 0x00, 0x00, 0x00, 0x00 }; // mov rax,rbx / mov [<TextureManager instance>],rax
    const std::uint8_t kCreationMask[] = {
        1, 1, 1, 1, 1, 1, 0, 0, 0, 0,
        1, 1, 1, 1, 0, 0, 0, 0, 1, 1,
        1, 1, 1, 1, 1, 1, 0, 0, 0, 0 };
    constexpr std::size_t kCreationSlotsOff = 6;
    constexpr std::size_t kCreationCallOff  = 14;   // rel32, the call ends at +18
    constexpr std::size_t kCreationStoreOff = 26;   // rel32, the store ends at +30

    const std::uint8_t kScanCountCode[] = {
        0xC7, 0x83, 0x90, 0x0A, 0x03, 0x00, 0x10, 0x13, 0x00, 0x00,   // mov dword ptr [rbx+0x30a90],<slots to scan>
        0x4C, 0x8D, 0xB3, 0x60, 0xEF, 0x03, 0x00 };                   // lea r14,[rbx+0x3ef60]

    const std::uint8_t kClearArrayCode[] = {
        0x48, 0x8D, 0x8B, 0x20, 0xA3, 0x03, 0x00,   // lea rcx,[rbx+0x3a320]: the per-slot float array
        0x33, 0xD2,                                 // xor edx,edx
        0x41, 0xB8, 0x40, 0x4C, 0x00, 0x00 };       // mov r8d,<array bytes> for the memset after it
    constexpr std::size_t kClearArrayBytesOff = 11;

    const std::uint8_t kPlannerCode[] = {
        0x48, 0x8D, 0x86, 0x20, 0xA3, 0x03, 0x00,                     // lea rax,[rsi+0x3a320]
        0x48, 0x89, 0x82, 0x30, 0x01, 0x00, 0x00,                     // mov [rdx+0x130],rax
        0x83, 0xBE, 0x90, 0x0A, 0x03, 0x00, 0x00,                     // cmp dword ptr [rsi+0x30a90],0
        0x75, 0x13,                                                   // jne +13
        0x44, 0x89, 0xA2, 0x38, 0x01, 0x00, 0x00,                     // mov [rdx+0x138],r12d
        0xC7, 0x86, 0x90, 0x0A, 0x03, 0x00, 0x10, 0x13, 0x00, 0x00,   // mov dword ptr [rsi+0x30a90],<slots to scan>
        0xEB, 0x0A,                                                   // jmp +0A
        0xC7, 0x82, 0x38, 0x01, 0x00, 0x00, 0x10, 0x13, 0x00, 0x00 }; // mov dword ptr [rdx+0x138],<slots to scan>
    constexpr std::size_t kPlannerScanOffs[] = { 36, 48 };

    const std::uint8_t kSetupCode[] = {
        0x49, 0x8D, 0x86, 0x20, 0xA3, 0x03, 0x00,   // lea rax,[r14+0x3a320]
        0x48, 0x89, 0x82, 0x60, 0x01, 0x00, 0x00,   // mov [rdx+0x160],rax
        0x49, 0x8D, 0x86, 0x90, 0x0A, 0x03, 0x00 }; // lea rax,[r14+0x30a90]

    constexpr std::size_t kMaxWindowLen   = sizeof(kPlannerCode);
    constexpr std::size_t kMaxStarts      = 8;

    using AddressField = uintptr_t AddressSetRuntime::AddressSet::*;

    struct Window
    {
        const char*         name;
        AddressField        site;
        const std::uint8_t* code;
        const std::uint8_t* mask;
        std::size_t         len;
        std::size_t         starts[kMaxStarts];
        std::size_t         startCount;
    };

    enum WindowId : int
    {
        Creation,
        ScanCount,
        ClearArray,
        Planner,
        EnumerateSetup,
        WindowCount
    };

    const Window kWindows[WindowCount] = {
        { "TextureManager creation", &AddressSetRuntime::AddressSet::TexturePools_ManagerCreateSite,
          kCreationCode, kCreationMask, sizeof(kCreationCode), { 0, 3, 5, 10, 13, 18, 20, 23 }, 8 },
        { "texture streamer scan count", &AddressSetRuntime::AddressSet::TexturePools_StreamerCountSite,
          kScanCountCode, nullptr, sizeof(kScanCountCode), { 0, 10 }, 2 },
        { "texture streamer slot array", &AddressSetRuntime::AddressSet::TexturePools_StreamerArraySite,
          kClearArrayCode, nullptr, sizeof(kClearArrayCode), { 0, 7, 9 }, 3 },
        { "texture streamer planner", &AddressSetRuntime::AddressSet::TexturePools_PlannerSite,
          kPlannerCode, nullptr, sizeof(kPlannerCode), { 0, 7, 14, 21, 23, 30, 40, 42 }, 8 },
        { "texture enumerate setup", &AddressSetRuntime::AddressSet::TexturePools_EnumerateSetupSite,
          kSetupCode, nullptr, sizeof(kSetupCode), { 0, 7, 14 }, 3 },
    };

    struct Write
    {
        std::uint8_t* at  = nullptr;
        std::size_t   len = 0;
        std::uint8_t  bytes[kLeaLen] = {};
        std::uint8_t  old[kLeaLen]   = {};
    };
    constexpr std::size_t kMaxWrites = 8;

    enum class PrePatchState : int
    {
        NotAttempted,
        AddressMissing,
        SiteUnexpected,
        SlotsUnexpected,
        AlreadyBuilt,
        Pool0TooSmall,
        NoArrayMemory,
        WriteFailed,
        SiteRaced,
        Applied,
    };

    std::atomic<PrePatchState> g_PrePatch{ PrePatchState::NotAttempted };
    std::uint8_t*              g_Sites[WindowCount] = {};
    std::uint8_t               g_LeftCode[WindowCount][kMaxWindowLen] = {};
    std::uint8_t**             g_Manager    = nullptr;
    std::uint8_t**             g_ProxyPool  = nullptr;
    std::uint8_t**             g_JobPool    = nullptr;
    float*                     g_SlotArray  = nullptr;
    std::int32_t               g_FoundSlots = 0;
    std::uint32_t              g_Pool0Size  = 0;
    bool                       g_ScanSettled = false;
    int                        g_ScanTries   = 0;
    bool                       g_ArrayStillInStreamer = false;
    DWORD                      g_WriteError = 0;
    const void*                g_RacedAt    = nullptr;
    char                       g_FoundCode[384] = {};
    char                       g_CtorHook[160]  = {};
    std::atomic<bool>          g_StopCheck{ false };
    std::atomic<bool>          g_CheckThreadStarted{ false };
    std::atomic<unsigned>      g_CheckGeneration{ 0 };

    double Mib(std::uint64_t bytes)
    {
        return static_cast<double>(bytes) / (1024.0 * 1024.0);
    }

    bool WindowMatches(const Window& window, const std::uint8_t* code)
    {
        for (std::size_t i = 0; i < window.len; ++i)
            if ((!window.mask || window.mask[i]) && code[i] != window.code[i])
                return false;
        return true;
    }

    void DescribeWindow(char* out, std::size_t cap, const Window& window, const std::uint8_t* site,
                        const std::uint8_t* code)
    {
        std::size_t at = 0;
        const int head = std::snprintf(out, cap, "%s:", window.name);
        if (head > 0)
            at = static_cast<std::size_t>(head);
        for (std::size_t i = 0; i < window.len && at < cap; ++i)
        {
            const int wrote = std::snprintf(out + at, cap - at, " %02X", code[i]);
            if (wrote <= 0)
                break;
            at += static_cast<std::size_t>(wrote);
        }
        for (std::size_t s = 0; s < window.startCount; ++s)
        {
            if (!EntryJumpTarget(reinterpret_cast<std::uintptr_t>(site + window.starts[s])))
                continue;
            char jump[160] = {};
            DescribeFunctionEntry(site + window.starts[s], jump, sizeof(jump));
            const std::size_t used = std::strlen(out);
            if (used < cap)
                std::snprintf(out + used, cap - used, "; %s", jump);
            return;
        }
    }

    bool CreationMatches(const std::uint8_t* site, const std::uint8_t* code, const std::uint8_t* ctor,
                         std::uint8_t** manager)
    {
        std::int32_t call  = 0;
        std::int32_t store = 0;
        std::memcpy(&call, code + kCreationCallOff, sizeof(call));
        std::memcpy(&store, code + kCreationStoreOff, sizeof(store));
        return WindowMatches(kWindows[Creation], code)
            && site + kCreationCallOff + sizeof(call) + call == ctor
            && reinterpret_cast<std::uintptr_t>(site + kCreationStoreOff + sizeof(store) + store)
               == reinterpret_cast<std::uintptr_t>(manager);
    }

    bool IsBuilt(std::uint8_t** instance)
    {
        std::uint8_t* object = nullptr;
        return !ReadEntryBytesSEH(instance, &object, sizeof(object)) || object;
    }

    bool WriteBytes(void* target, const void* src, std::size_t size)
    {
        DWORD oldProtect = 0;
        if (!VirtualProtect(target, size, PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            g_WriteError = GetLastError();
            return false;
        }
        std::memcpy(target, src, size);
        DWORD restored = 0;
        VirtualProtect(target, size, oldProtect, &restored);
        FlushInstructionCache(GetCurrentProcess(), target, size);
        return true;
    }

    bool QueueWrite(Write (&writes)[kMaxWrites], std::size_t& count, std::uint8_t* at, const void* bytes,
                    std::size_t len)
    {
        if (count >= kMaxWrites || len > kLeaLen)
            return false;
        Write& write = writes[count];
        write.at  = at;
        write.len = len;
        std::memcpy(write.bytes, bytes, len);
        if (!ReadEntryBytesSEH(at, write.old, len))
            return false;
        ++count;
        return true;
    }

    bool QueueSlotArrayLea(Write (&writes)[kMaxWrites], std::size_t& count, std::uint8_t* at, std::uint8_t modrm)
    {
        const std::intptr_t distance = reinterpret_cast<std::intptr_t>(g_SlotArray)
                                     - reinterpret_cast<std::intptr_t>(at + kLeaLen);
        if (distance < (std::numeric_limits<std::int32_t>::min)() || distance > (std::numeric_limits<std::int32_t>::max)())
            return false;
        const std::int32_t rel = static_cast<std::int32_t>(distance);
        std::uint8_t lea[kLeaLen] = { 0x48, 0x8D, modrm };   // lea r64,[rip+rel32]
        std::memcpy(lea + 3, &rel, sizeof(rel));
        return QueueWrite(writes, count, at, lea, sizeof(lea));
    }

    bool QueueImm32(Write (&writes)[kMaxWrites], std::size_t& count, std::uint8_t* at, std::uint32_t value)
    {
        return QueueWrite(writes, count, at, &value, sizeof(value));
    }

    bool SiteHolds(const std::uint8_t* at, const std::uint8_t* bytes, std::size_t len)
    {
        std::uint8_t now[kLeaLen] = {};
        return len <= sizeof(now) && ReadEntryBytesSEH(at, now, len) && std::memcmp(now, bytes, len) == 0;
    }

    bool ApplyWrites(Write (&writes)[kMaxWrites], std::size_t count)
    {
        std::size_t done = 0;
        for (; done < count; ++done)
        {
            if (!SiteHolds(writes[done].at, writes[done].old, writes[done].len))
            {
                g_RacedAt = writes[done].at;
                break;
            }
            if (!WriteBytes(writes[done].at, writes[done].bytes, writes[done].len))
                break;
        }
        if (done == count)
        {
            for (std::size_t i = 0; i < count && !g_RacedAt; ++i)
                if (!SiteHolds(writes[i].at, writes[i].bytes, writes[i].len))
                    g_RacedAt = writes[i].at;
            if (!g_RacedAt)
                return true;
        }
        while (done-- > 0)
            if (SiteHolds(writes[done].at, writes[done].bytes, writes[done].len))   // a site another module wrote over stays theirs
                WriteBytes(writes[done].at, writes[done].old, writes[done].len);
        return false;
    }

    std::uint32_t JobScratchBytes(std::uint8_t** jobPoolInstance)
    {
        std::uint8_t* jobPool = nullptr;
        std::uint32_t scratch = 0;
        if (!ReadEntryBytesSEH(jobPoolInstance, &jobPool, sizeof(jobPool)) || !jobPool
            || !ReadEntryBytesSEH(jobPool + kJobScratchOff, &scratch, sizeof(scratch)))
            return 0;
        return scratch;
    }

    void NoteBuiltSize(std::int32_t size)
    {
        const bool applied = g_PrePatch.load() == PrePatchState::Applied;
        const std::int32_t arraySlots = applied ? kNewSlots : kVanillaSlots;
        if (size > arraySlots && (applied || g_ArrayStillInStreamer))
            LogError("TexturePools: TextureManager built with %d slots but the texture streamer's slot array holds %d, "
                     "textures past slot %d overwrite streamer memory", size, arraySlots, arraySlots);
        else if (applied && size != kNewSlots)
            LogWarn("TexturePools: TextureManager built with %d slots, not %d", size, kNewSlots);
        else if (applied)
            LogInfo("TexturePools: TextureManager built with %d slots", size);
        else if (size != kVanillaSlots)
            LogWarn("TexturePools: TextureManager built with %d slots (another module's size)", size);
    }

    enum UseLevel : int
    {
        AboveVanilla,
        ThreeQuarters,
        NinetyPercent,
        Full,
        LevelCount
    };

    void NoteUse(std::int32_t used, std::int32_t capacity, bool (&logged)[LevelCount])
    {
        const std::int64_t u = used;
        const std::int64_t usable = static_cast<std::int64_t>(capacity) - kKeptFree;
        const std::int32_t vanillaUsable = kVanillaSlots - kKeptFree;
        if (!logged[AboveVanilla] && capacity > kVanillaSlots && u > vanillaUsable)
        {
            LogInfo("TexturePools: TextureManager in use %d slots, more than vanilla %d", used, vanillaUsable);
            logged[AboveVanilla] = true;
        }
        if (!logged[ThreeQuarters] && u * 4 >= usable * 3 && u < usable)
        {
            LogWarn("TexturePools: TextureManager in use %d of %d usable slots (75%%)", used, capacity - kKeptFree);
            logged[ThreeQuarters] = true;
        }
        if (!logged[NinetyPercent] && u * 10 >= usable * 9 && u < usable)
        {
            LogWarn("TexturePools: TextureManager in use %d of %d usable slots (90%%)", used, capacity - kKeptFree);
            logged[NinetyPercent] = true;
        }
        if (!logged[Full] && u >= usable)
        {
            LogError("TexturePools: TextureManager full (%d of %d usable slots), new textures fail to load and their "
                     "packs are dropped", used, capacity - kKeptFree);
            logged[Full] = true;
        }
    }

    void NoteProxies(bool& noted)
    {
        std::uint8_t* pool    = nullptr;
        std::uint32_t live    = 0;
        std::uint32_t maxLive = 0;
        if (noted || !g_ProxyPool || !ReadEntryBytesSEH(g_ProxyPool, &pool, sizeof(pool)) || !pool
            || !ReadEntryBytesSEH(pool + kProxyLiveOff, &live, sizeof(live))
            || !ReadEntryBytesSEH(pool + kProxyMaxLiveOff, &maxLive, sizeof(maxLive)) || maxLive == 0 || live < maxLive)
            return;
        noted = true;
        LogWarn("TexturePools: texture streamer proxies full (%u of %u), new streamed textures fail to load and their "
                "packs are dropped", live, maxLive);
    }

    void NoteSiteChanges(bool (&noted)[WindowCount])
    {
        for (int i = 0; i < WindowCount; ++i)
        {
            const Window& window = kWindows[i];
            std::uint8_t  code[kMaxWindowLen] = {};
            if (noted[i] || !g_Sites[i] || !ReadEntryBytesSEH(g_Sites[i], code, window.len)
                || std::memcmp(code, g_LeftCode[i], window.len) == 0)
                continue;
            noted[i] = true;
            char found[384] = {};
            DescribeWindow(found, sizeof(found), window, g_Sites[i], code);
            LogWarn("TexturePools: another module changed the %s code after V_FrameWork %s it (%s)", window.name,
                    g_PrePatch.load() == PrePatchState::Applied ? "set" : "checked", found);
        }
    }

    bool IsPlannerScanByte(std::size_t i)
    {
        for (std::size_t off : kPlannerScanOffs)
            if (i >= off && i < off + sizeof(std::int32_t))
                return true;
        return false;
    }

    void NoteScanPastArray(bool& noted)
    {
        const Window& window = kWindows[Planner];
        std::uint8_t  code[kMaxWindowLen] = {};
        if (noted || g_PrePatch.load() != PrePatchState::Applied
            || !ReadEntryBytesSEH(g_Sites[Planner], code, window.len))
            return;
        for (std::size_t i = 0; i < window.len; ++i)
            if (!IsPlannerScanByte(i) && code[i] != g_LeftCode[Planner][i])
                return;
        for (std::size_t off : kPlannerScanOffs)
        {
            std::int32_t scan = 0;
            std::memcpy(&scan, code + off, sizeof(scan));
            if (scan <= kNewSlots)
                continue;
            noted = true;
            LogError("TexturePools: another module set the texture streamer to scan %d slots but the slot array holds %d, "
                     "textures past slot %d overwrite memory", scan, kNewSlots, kNewSlots);
            return;
        }
    }

    void PlannerBounds(const std::uint8_t* site, const void*& begin, const void*& end)
    {
        DWORD64 imageBase = 0;
        const auto* function = RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(site), &imageBase, nullptr);
        if (!function || function->EndAddress <= function->BeginAddress)
            return;
        begin = reinterpret_cast<const void*>(imageBase + function->BeginAddress);
        end   = reinterpret_cast<const void*>(imageBase + function->EndAddress);
    }

    void TryRaiseScan()
    {
        if (g_ScanSettled || g_PrePatch.load() != PrePatchState::Applied)
            return;
        const std::uint32_t scratch = JobScratchBytes(g_JobPool);
        if (scratch == 0)
            return;
        if (scratch < kSlotArrayBytes)
        {
            g_ScanSettled = true;
            LogWarn("TexturePools: slots past %d get no sharper levels (texture job scratch %u bytes, needs %u)",
                    kVanillaSlots, scratch, kSlotArrayBytes);
            return;
        }

        const Window& window = kWindows[Planner];
        std::uint8_t* site = g_Sites[Planner];
        std::uint8_t  code[kMaxWindowLen] = {};
        if (!ReadEntryBytesSEH(site, code, window.len) || std::memcmp(code, g_LeftCode[Planner], window.len) != 0)
        {
            g_ScanSettled = true;
            char found[384] = {};
            DescribeWindow(found, sizeof(found), window, site, code);
            LogWarn("TexturePools: slots past %d get no sharper levels, another module changed the %s (%s)",
                    kVanillaSlots, window.name, found);
            return;
        }

        NativeCodePatch patches[2] = {};
        for (std::size_t i = 0; i < 2; ++i)
        {
            patches[i].target = site + kPlannerScanOffs[i];
            patches[i].size   = sizeof(std::int32_t);
            std::memcpy(patches[i].expected, &kVanillaSlots, sizeof(std::int32_t));
            std::memcpy(patches[i].replacement, &kNewSlots, sizeof(std::int32_t));
        }
        const void* quietBegin = nullptr;
        const void* quietEnd   = nullptr;
        PlannerBounds(site, quietBegin, quietEnd);
        DWORD error = 0;
        for (int attempt = 0; attempt < 50; ++attempt)
        {
            if (InstallNativeCodePatches(patches, 2, nullptr, quietBegin, quietEnd))
            {
                g_ScanSettled = true;
                ReadEntryBytesSEH(site, g_LeftCode[Planner], window.len);
                LogInfo("TexturePools: slots past %d now get sharper levels (texture job scratch %u bytes)",
                        kVanillaSlots, scratch);
                return;
            }
            error = GetLastError();
            if (!NativePatchErrorIsTransient(error))
                break;
            Sleep(4);
        }
        if (NativePatchErrorIsTransient(error) && ++g_ScanTries < 60)   // the planner runs every frame, next second
            return;
        g_ScanSettled = true;
        LogError("TexturePools: slots past %d get no sharper levels, the planner patch was refused (%s)", kVanillaSlots,
                 NativePatchErrorName(error));
    }

#ifdef _DEBUG
    constexpr std::uintptr_t kLowestUserAddress  = 0x10000;
    constexpr std::uintptr_t kHighestUserAddress = 0x00007FFFFFFFFFFF;

    std::uint8_t g_ProbeSlots[(kNewSlots - kVanillaSlots) * 0xC];

    bool CanBeUserMemory(const std::uint8_t* first, std::size_t bytes)
    {
        const std::uintptr_t at = reinterpret_cast<std::uintptr_t>(first);
        return at >= kLowestUserAddress && at <= kHighestUserAddress && bytes <= kHighestUserAddress - at + 1;
    }

    void ProbeSlotsPastVanilla(std::uint8_t* manager, std::int32_t capacity, std::int32_t& lastInUse)
    {
        std::uint8_t* states = nullptr;
        if (capacity <= kVanillaSlots || capacity > kNewSlots
            || !ReadEntryBytesSEH(manager + kSlotStatesOff, &states, sizeof(states))
            || !CanBeUserMemory(states, static_cast<std::size_t>(capacity) * 0xC))
            return;
        const std::size_t bytes = static_cast<std::size_t>(capacity - kVanillaSlots) * 0xC;
        if (!ReadEntryBytesSEH(states + static_cast<std::size_t>(kVanillaSlots) * 0xC, g_ProbeSlots, bytes))
            return;
        std::int32_t inUse = 0;
        for (std::size_t i = 0; i < bytes; i += 0xC)
            inUse += g_ProbeSlots[i + 8] == 3 || g_ProbeSlots[i + 8] == 4;
        if (inUse == lastInUse)
            return;
        lastInUse = inUse;
        std::int32_t scan = 0;
        ReadEntryBytesSEH(g_Sites[Planner] + kPlannerScanOffs[1], &scan, sizeof(scan));
        std::uint8_t* pool    = nullptr;
        std::uint32_t live    = 0;
        std::uint32_t maxLive = 0;
        if (g_ProxyPool && ReadEntryBytesSEH(g_ProxyPool, &pool, sizeof(pool)) && pool)
        {
            ReadEntryBytesSEH(pool + kProxyLiveOff, &live, sizeof(live));
            ReadEntryBytesSEH(pool + kProxyMaxLiveOff, &maxLive, sizeof(maxLive));
        }
        LogDebug("TexturePools probe: %d TextureManager slots past %d in use, the streamer scans %d, streamer proxies "
                 "%u of %u in use", inUse, kVanillaSlots, scan, live, maxLive);
    }
#endif

    DWORD WINAPI SlotCheckThread(LPVOID)
    {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        std::uint8_t* manager  = nullptr;
        std::int32_t  capacity = 0;
        bool          logged[LevelCount] = {};
        bool          siteNoted[WindowCount] = {};
        bool          proxiesNoted = false;
        bool          scanPastNoted = false;
        bool          lateNoted = false;
        bool          wasBuilt = false;
        unsigned      generation = 0;
        int           startTick = 0;
#ifdef _DEBUG
        std::int32_t  probeInUse = -1;
#endif
        for (int tick = 1; ; ++tick)
        {
            Sleep(100);
            if (g_StopCheck.load())
                continue;
            const unsigned current = g_CheckGeneration.load();
            if (current != generation)
            {
                generation   = current;
                startTick    = tick;
                manager      = nullptr;
                capacity     = 0;
                proxiesNoted  = false;
                scanPastNoted = false;
                lateNoted     = false;
                wasBuilt      = false;
                std::memset(logged, 0, sizeof(logged));
                std::memset(siteNoted, 0, sizeof(siteNoted));
            }
            if ((tick - startTick) % 10 != 0)
                continue;

            NoteSiteChanges(siteNoted);
            NoteScanPastArray(scanPastNoted);
            NoteProxies(proxiesNoted);
            NoteTextureProxyRaise();
            TryRaiseScan();

            std::uint8_t* built = nullptr;
            ReadEntryBytesSEH(g_Manager, &built, sizeof(built));
            if (manager && built != manager)
            {
                manager  = nullptr;
                capacity = 0;
                std::memset(logged, 0, sizeof(logged));
            }

            if (!manager)
            {
                std::int32_t size = 0;
                if (built && ReadEntryBytesSEH(built + kCapacityOff, &size, sizeof(size)) && size > 0)
                {
                    manager  = built;
                    capacity = size;
                    wasBuilt = true;
                    NoteBuiltSize(size);
                }
                else if (!wasBuilt && !lateNoted && tick - startTick >= 1200)   // 120 s
                {
                    LogWarn("TexturePools: TextureManager not built within 120 s, still watching");
                    lateNoted = true;
                }
            }

            std::int32_t used = 0;
            if (manager && ReadEntryBytesSEH(manager + kUsedOff, &used, sizeof(used)))
                NoteUse(used, capacity, logged);
#ifdef _DEBUG
            if (manager && (tick - startTick) % 100 == 0)
                ProbeSlotsPastVanilla(manager, capacity, probeInUse);
#endif
        }
    }

    void StartCheckThread()
    {
        if (g_CheckThreadStarted.exchange(true))
            return;
        HMODULE pinnedModule = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(&SlotCheckThread), &pinnedModule))
            LogWarn("TexturePools: cannot pin the module for the check thread (err %lu)", GetLastError());
        HANDLE thread = CreateThread(nullptr, 0, &SlotCheckThread, nullptr, 0, nullptr);
        if (thread)
            CloseHandle(thread);
        else
        {
            g_CheckThreadStarted = false;
            LogWarn("TexturePools: check thread failed to start, texture slot use is not logged");
        }
    }
}

void PreApplyTexturePoolsPatch()
{
    PrePatchState expected = PrePatchState::NotAttempted;
    if (!g_PrePatch.compare_exchange_strong(expected, PrePatchState::AddressMissing))
        return;

    std::uint8_t* sites[WindowCount] = {};
    for (int i = 0; i < WindowCount; ++i)
    {
        sites[i] = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.*kWindows[i].site));
        if (!sites[i])
            return;
    }
    auto* ctor      = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.TexturePools_ManagerCtor));
    auto* manager   = static_cast<std::uint8_t**>(ResolveGameAddress(gAddr.TexturePools_ManagerInstance));
    auto* jobPool   = static_cast<std::uint8_t**>(ResolveGameAddress(gAddr.TexturePools_JobPool));
    auto* proxyPool = static_cast<std::uint8_t**>(ResolveGameAddress(gAddr.TexturePools_ProxyPool));
    auto* streamer  = static_cast<std::uint8_t**>(ResolveGameAddress(gAddr.TextureStoragePool_Streamer));
    if (!ctor || !manager || !jobPool || !proxyPool || !streamer)
        return;

    std::uint8_t code[WindowCount][kMaxWindowLen] = {};
    for (int i = 0; i < WindowCount; ++i)
    {
        if (!ReadEntryBytesSEH(sites[i], code[i], kWindows[i].len))
        {
            std::snprintf(g_FoundCode, sizeof(g_FoundCode), "%s: unreadable", kWindows[i].name);
            g_PrePatch = PrePatchState::SiteUnexpected;
            return;
        }
    }
    if (!CreationMatches(sites[Creation], code[Creation], ctor, manager))
    {
        DescribeWindow(g_FoundCode, sizeof(g_FoundCode), kWindows[Creation], sites[Creation], code[Creation]);
        g_PrePatch = PrePatchState::SiteUnexpected;
        return;
    }
    g_Manager   = manager;
    g_ProxyPool = proxyPool;
    g_JobPool   = jobPool;
    for (int i = 0; i < WindowCount; ++i)
    {
        g_Sites[i] = sites[i];
        std::memcpy(g_LeftCode[i], code[i], kWindows[i].len);
    }
    g_ArrayStillInStreamer = WindowMatches(kWindows[ClearArray], code[ClearArray])
                  && WindowMatches(kWindows[Planner], code[Planner])
                  && WindowMatches(kWindows[EnumerateSetup], code[EnumerateSetup]);

    std::memcpy(&g_FoundSlots, code[Creation] + kCreationSlotsOff, sizeof(g_FoundSlots));
    if (g_FoundSlots != kVanillaSlots)
    {
        g_PrePatch = PrePatchState::SlotsUnexpected;
        return;
    }
    for (int i = ScanCount; i < WindowCount; ++i)
    {
        if (WindowMatches(kWindows[i], code[i]))
            continue;
        DescribeWindow(g_FoundCode, sizeof(g_FoundCode), kWindows[i], sites[i], code[i]);
        g_PrePatch = PrePatchState::SiteUnexpected;
        return;
    }
    if (EntryHookedByOtherModule(ctor))
        DescribeFunctionEntry(ctor, g_CtorHook, sizeof(g_CtorHook));

    if (IsBuilt(manager) || IsBuilt(streamer))
    {
        g_PrePatch = PrePatchState::AlreadyBuilt;
        return;
    }
    g_Pool0Size = GetTextureStoragePool0Size();
    if (g_Pool0Size < kPool0Needed)
    {
        g_PrePatch = PrePatchState::Pool0TooSmall;
        return;
    }
    g_SlotArray = static_cast<float*>(HookArena::AllocateNear(reinterpret_cast<std::uintptr_t>(sites[ClearArray]),
                                                              kSlotArrayBytes));
    if (!g_SlotArray)
    {
        g_PrePatch = PrePatchState::NoArrayMemory;
        return;
    }
    Write       writes[kMaxWrites];
    std::size_t count = 0;
    const bool queued = QueueSlotArrayLea(writes, count, sites[ClearArray], kLeaRcxRip)
                     && QueueImm32(writes, count, sites[ClearArray] + kClearArrayBytesOff, kSlotArrayBytes)
                     && QueueSlotArrayLea(writes, count, sites[EnumerateSetup], kLeaRaxRip)
                     && QueueSlotArrayLea(writes, count, sites[Planner], kLeaRaxRip)
                     && QueueImm32(writes, count, sites[Creation] + kCreationSlotsOff, kNewSlots);
    if (!queued)
    {
        g_PrePatch = PrePatchState::NoArrayMemory;
        return;
    }
    if (!ApplyWrites(writes, count))
    {
        g_PrePatch = g_RacedAt ? PrePatchState::SiteRaced : PrePatchState::WriteFailed;
        return;
    }
    for (int i = 0; i < WindowCount; ++i)
        ReadEntryBytesSEH(sites[i], g_LeftCode[i], kWindows[i].len);
    g_PrePatch = PrePatchState::Applied;
}

bool TexturePoolsSlotsRaised()
{
    return g_PrePatch.load() == PrePatchState::Applied;
}

bool Install_TexturePools()
{
    if (g_PrePatch.load() == PrePatchState::NotAttempted)
        PreApplyTexturePoolsPatch();

    bool ok = true;
    switch (g_PrePatch.load())
    {
    case PrePatchState::Applied:
        LogInfo("TexturePools: TextureManager slots %d -> %d (%d usable)", kVanillaSlots, kNewSlots, kNewSlots - kKeptFree);
        LogTextureProxyRaise();
        if (g_CtorHook[0])
            LogInfo("TexturePools: TextureManager constructor already hooked by another module (%s), the new slot count "
                    "goes through it", g_CtorHook);
        break;
    case PrePatchState::SlotsUnexpected:
        LogWarn("TexturePools: another module set the TextureManager to %d slots (vanilla %d), texture slots left as "
                "they are", g_FoundSlots, kVanillaSlots);
        break;
    case PrePatchState::AlreadyBuilt:
        LogWarn("TexturePools: the TextureManager or the texture streamer was built before V_FrameWork loaded, texture "
                "slots stay vanilla");
        break;
    case PrePatchState::Pool0TooSmall:
        LogWarn("TexturePools: texture pool 0 is %.1f MiB, not V_FrameWork's %.1f MiB (TextureStoragePool off or left "
                "vanilla), texture slots stay vanilla", Mib(g_Pool0Size), Mib(kPool0Needed));
        break;
    case PrePatchState::NoArrayMemory:
        LogError("TexturePools: no memory within 2 GB of the exe for the slot array, texture slots stay vanilla");
        break;
    case PrePatchState::AddressMissing:
    case PrePatchState::NotAttempted:
        LogWarn("TexturePools: addresses missing on %s, texture slots stay vanilla", GetGameBuildName(gGameBuild));
        break;
    case PrePatchState::SiteUnexpected:
        LogError("TexturePools: code unexpected (%s), texture slots stay vanilla",
                 g_FoundCode[0] ? g_FoundCode : "unreadable");
        break;
    case PrePatchState::WriteFailed:
        LogError("TexturePools: VirtualProtect failed (err %lu), the sites written are put back, texture slots stay "
                 "vanilla", g_WriteError);
        ok = false;
        break;
    case PrePatchState::SiteRaced:
        LogError("TexturePools: another module changed the texture code at %p while V_FrameWork wrote it, V_FrameWork's "
                 "own writes are put back, texture slots stay vanilla", g_RacedAt);
        break;
    }

    if (g_Manager)
    {
        g_CheckGeneration.fetch_add(1);
        g_StopCheck = false;
        StartCheckThread();
    }
    return ok;
}

void Uninstall_TexturePools()
{
    g_StopCheck = true;
}
