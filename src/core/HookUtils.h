#pragma once

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "MinHook.h"
#include "log.h"
#include "HookArena.h"


inline uintptr_t GetExeBase()
{
    return reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
}

constexpr uintptr_t EXE_PREFERRED_BASE = 0x140000000ull;


inline constexpr uintptr_t ToRva(uintptr_t absAddr)
{
    return absAddr - EXE_PREFERRED_BASE;
}


inline void* ResolveGameAddress(uintptr_t absAddr)
{
    if (absAddr == 0)
        return nullptr;

    const uintptr_t base = GetExeBase();
    if (!base)
        return nullptr;

    return reinterpret_cast<void*>(base + ToRva(absAddr));
}


extern std::atomic<bool> g_HookBatchMode;

namespace HookRegistry
{
    inline void SetQueued(void* target);
}
inline MH_STATUS ApplyQueuedHooks();

inline MH_STATUS EnableOrQueueHook(void* target)
{
    if (!g_HookBatchMode)
        return MH_EnableHook(target);
    const MH_STATUS st = MH_QueueEnableHook(target);
    if (st == MH_OK)
        HookRegistry::SetQueued(target);
    if (st == MH_OK && !g_HookBatchMode)   // startup applied the queue while we were queuing: apply ours now
        ApplyQueuedHooks();
    return st;
}

inline const char* MhStatusName(MH_STATUS st)
{
    switch (st)
    {
        case MH_UNKNOWN:                    return "MH_UNKNOWN";
        case MH_OK:                         return "MH_OK";
        case MH_ERROR_ALREADY_INITIALIZED:  return "MH_ERROR_ALREADY_INITIALIZED";
        case MH_ERROR_NOT_INITIALIZED:      return "MH_ERROR_NOT_INITIALIZED";
        case MH_ERROR_ALREADY_CREATED:      return "MH_ERROR_ALREADY_CREATED";
        case MH_ERROR_NOT_CREATED:          return "MH_ERROR_NOT_CREATED";
        case MH_ERROR_ENABLED:              return "MH_ERROR_ENABLED";
        case MH_ERROR_DISABLED:             return "MH_ERROR_DISABLED";
        case MH_ERROR_NOT_EXECUTABLE:       return "MH_ERROR_NOT_EXECUTABLE";
        case MH_ERROR_UNSUPPORTED_FUNCTION: return "MH_ERROR_UNSUPPORTED_FUNCTION";
        case MH_ERROR_MEMORY_ALLOC:         return "MH_ERROR_MEMORY_ALLOC";
        case MH_ERROR_MEMORY_PROTECT:       return "MH_ERROR_MEMORY_PROTECT";
        case MH_ERROR_MODULE_NOT_FOUND:     return "MH_ERROR_MODULE_NOT_FOUND";
        case MH_ERROR_FUNCTION_NOT_FOUND:   return "MH_ERROR_FUNCTION_NOT_FOUND";
        default:                            return "MH_ERROR_?";
    }
}


inline bool ReadEntryBytesSEH(const void* at, void* out, std::size_t size)
{
    __try
    {
        std::memcpy(out, at, size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}


namespace HookRegistry
{
    struct Entry
    {
        void*        target;
        void*        detour;
        void*        trampoline;
        bool         passThrough;
        std::uint8_t original[5];   // entry bytes before our hook: tells "ours is off" apart from another DLL's jump
        bool         queued;
        bool         chained;       // our trampoline was pointed at another DLL's jump that went in before ours
    };

    inline constexpr int kMaxEntries = 4096;
    inline SRWLOCK       g_Lock = SRWLOCK_INIT;
    inline Entry         g_Entries[kMaxEntries] = {};
    inline int           g_Count = 0;
    inline bool          g_AnyPassThrough = false;

    inline void Note(void* target, void* detour, void* trampoline, const std::uint8_t (&original)[5])
    {
        AcquireSRWLockExclusive(&g_Lock);
        int i = 0;
        while (i < g_Count && g_Entries[i].target != target)
            ++i;
        if (i < kMaxEntries)
        {
            g_Entries[i] = { target, detour, trampoline, false, {}, false, false };
            std::memcpy(g_Entries[i].original, original, sizeof(original));
            if (i == g_Count)
                ++g_Count;
        }
        ReleaseSRWLockExclusive(&g_Lock);
    }

    inline bool Find(void* target, Entry& out)
    {
        AcquireSRWLockShared(&g_Lock);
        bool found = false;
        for (int i = 0; i < g_Count && !found; ++i)
            if (g_Entries[i].target == target)
            {
                out = g_Entries[i];
                found = true;
            }
        ReleaseSRWLockShared(&g_Lock);
        return found;
    }

    inline void SetPassThrough(void* target, bool on)
    {
        AcquireSRWLockExclusive(&g_Lock);
        for (int i = 0; i < g_Count; ++i)
            if (g_Entries[i].target == target)
                g_Entries[i].passThrough = on;
        if (on)
            g_AnyPassThrough = true;
        ReleaseSRWLockExclusive(&g_Lock);
    }

    inline void SetQueued(void* target)
    {
        AcquireSRWLockExclusive(&g_Lock);
        for (int i = 0; i < g_Count; ++i)
            if (g_Entries[i].target == target)
                g_Entries[i].queued = true;
        ReleaseSRWLockExclusive(&g_Lock);
    }

    inline void ClearQueued()
    {
        AcquireSRWLockExclusive(&g_Lock);
        for (int i = 0; i < g_Count; ++i)
            g_Entries[i].queued = false;
        ReleaseSRWLockExclusive(&g_Lock);
    }

    inline void SetChained(void* target)
    {
        AcquireSRWLockExclusive(&g_Lock);
        for (int i = 0; i < g_Count; ++i)
            if (g_Entries[i].target == target)
                g_Entries[i].chained = true;
        ReleaseSRWLockExclusive(&g_Lock);
    }

    inline void Forget(void* target)
    {
        AcquireSRWLockExclusive(&g_Lock);
        for (int i = 0; i < g_Count; ++i)
            if (g_Entries[i].target == target)
            {
                g_Entries[i] = g_Entries[--g_Count];
                break;
            }
        ReleaseSRWLockExclusive(&g_Lock);
    }
}

inline std::uint8_t* FindHookRelay(void* trampoline, void* detour);
inline bool SetHookRelayTarget(std::uint8_t* relay, const void* to);

// MH_CreateHook that also records the hook, so unhooking later can tell whether another DLL hooked on top of it
inline MH_STATUS CreateHookTracked(LPVOID target, LPVOID detour, LPVOID* original)
{
    std::uint8_t before[5] = {};
    const bool readable = target && ReadEntryBytesSEH(target, before, sizeof(before));
    const MH_STATUS st = MH_CreateHook(target, detour, original);
    if (st == MH_OK && readable && original && *original)
        HookRegistry::Note(target, detour, *original, before);
    return st;
}

inline bool CreateAndEnableHook(void* target, void* detour, void** original)
{
    if (!target || !detour || !original)
        return false;

    MH_STATUS st = CreateHookTracked(target, detour, original);

    if (st != MH_OK && st != MH_ERROR_ALREADY_CREATED)
    {
        if (st == MH_ERROR_MEMORY_ALLOC)
            HookArena::NoteExhausted();
        const DWORD lastErr = GetLastError();
        const uintptr_t base = GetExeBase();
        const uintptr_t abs = reinterpret_cast<uintptr_t>(target);
        LogError("Hook: create failed %p (game+0x%llX): %s, last error %lu",
            target,
            static_cast<unsigned long long>(base ? abs - base : 0ull),
            MhStatusName(st),
            lastErr);
        return false;
    }
    if (st == MH_ERROR_ALREADY_CREATED)
    {
        // a hook left as a pass-through by DisableAndRemoveHook: point its relay back at our detour
        HookRegistry::Entry old{};
        if (HookRegistry::Find(target, old) && old.passThrough && old.detour == detour)
        {
            std::uint8_t* relay = FindHookRelay(old.trampoline, old.trampoline);
            if (relay && SetHookRelayTarget(relay, detour))
            {
                *original = old.trampoline;
                HookRegistry::SetPassThrough(target, false);
            }
        }
    }
    if (st == MH_ERROR_ALREADY_CREATED && !*original)
    {
        HookRegistry::Entry first{};   // MinHook says "already created" only for a target this DLL hooked before
        HMODULE module = nullptr;
        char path[MAX_PATH] = {};
        if (HookRegistry::Find(target, first)
            && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                  static_cast<LPCSTR>(first.detour), &module)
            && GetModuleFileNameA(module, path, MAX_PATH))
        {
            const char* slash = std::strrchr(path, '\\');
            LogError("Hook: %p is already hooked inside V_FrameWork by %s+0x%llX, this second hook is not installed",
                target, slash ? slash + 1 : path,
                static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(first.detour)
                                                - reinterpret_cast<std::uintptr_t>(module)));
        }
        else
        {
            LogError("Hook: %p is already hooked inside V_FrameWork, this second hook is not installed", target);
        }
        return false;
    }

    st = EnableOrQueueHook(target);
    if (st != MH_OK && st != MH_ERROR_ENABLED)
    {
        LogError("Hook: enable failed %p: %s", target, MhStatusName(st));
        return false;
    }

    return true;
}


inline std::uintptr_t EntryJumpTarget(std::uintptr_t at)
{
    std::uint8_t b[12] = {};
    if (!ReadEntryBytesSEH(reinterpret_cast<const void*>(at), b, sizeof(b)))
        return 0;
    std::int32_t rel = 0;
    if (b[0] == 0xE9)
    {
        std::memcpy(&rel, b + 1, sizeof(rel));
        return at + 5 + static_cast<std::intptr_t>(rel);
    }
    std::uintptr_t dest = 0;
    if (b[0] == 0xFF && b[1] == 0x25)
    {
        std::memcpy(&rel, b + 2, sizeof(rel));
        const std::uintptr_t slot = at + 6 + static_cast<std::intptr_t>(rel);
        return ReadEntryBytesSEH(reinterpret_cast<const void*>(slot), &dest, sizeof(dest)) ? dest : 0;
    }
    if (b[0] == 0x48 && b[1] == 0xB8 && b[10] == 0xFF && b[11] == 0xE0)   // mov rax, imm64 / jmp rax
    {
        std::memcpy(&dest, b + 2, sizeof(dest));
        return dest;
    }
    return 0;
}

// follows a jump at the entry through hook relays (up to 3 hops) until it lands in a loaded module
inline std::uintptr_t ResolveEntryJump(const void* fn, HMODULE& module)
{
    module = nullptr;
    std::uintptr_t dest = reinterpret_cast<std::uintptr_t>(fn);
    for (int hop = 0; hop < 3; ++hop)
    {
        const std::uintptr_t next = EntryJumpTarget(dest);
        if (!next)
            break;
        dest = next;
        module = nullptr;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(dest), &module) && module)
            break;
    }
    return dest;
}

// another DLL already hooked this function: MinHook can hook on top, our detour runs first and calls through theirs
inline bool EntryHookedByOtherModule(const void* fn)
{
    if (!fn)
        return false;
    HMODULE module = nullptr;
    const std::uintptr_t dest = ResolveEntryJump(fn, module);
    return dest != reinterpret_cast<std::uintptr_t>(fn) && module
        && module != GetModuleHandleW(nullptr);
}

// for a prologue mismatch: whose jump sits at the entry (hook relays are followed to the module), else the bytes
inline void DescribeFunctionEntry(const void* fn, char* out, std::size_t cap)
{
    if (!out || cap == 0)
        return;
    std::uint8_t b[16] = {};
    if (!fn || !ReadEntryBytesSEH(fn, b, sizeof(b)))
    {
        snprintf(out, cap, "entry unreadable");
        return;
    }

    HMODULE module = nullptr;
    const std::uintptr_t dest = ResolveEntryJump(fn, module);

    if (dest != reinterpret_cast<std::uintptr_t>(fn))
    {
        char path[MAX_PATH] = {};
        if (module && GetModuleFileNameA(module, path, MAX_PATH))
        {
            const char* slash = std::strrchr(path, '\\');
            snprintf(out, cap, "entry jumps to %s+0x%llX", slash ? slash + 1 : path,
                     static_cast<unsigned long long>(dest - reinterpret_cast<std::uintptr_t>(module)));
        }
        else
        {
            snprintf(out, cap, "entry jumps to 0x%llX outside any module", static_cast<unsigned long long>(dest));
        }
        return;
    }

    snprintf(out, cap, "entry %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
             b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
}


constexpr std::size_t kHookPatchZone = 16; // the most a detour writes over an entry (a 14-byte absolute jump)

enum class HookEntry { Expected, ForeignHook, Unknown };

// Expected: the bytes we know are there. ForeignHook: another DLL's jump sits on the entry and the bytes past the jump
// still match, so we hook on top and MinHook chains through theirs. Unknown: not the function we know, leave it alone.
inline HookEntry CheckHookEntry(const void* fn, const std::uint8_t* expected, std::size_t size)
{
    std::uint8_t live[64] = {};
    if (!fn || !expected || size == 0 || size > sizeof(live) || !ReadEntryBytesSEH(fn, live, size))
        return HookEntry::Unknown;
    if (std::memcmp(live, expected, size) == 0)
        return HookEntry::Expected;
    if (!EntryHookedByOtherModule(fn))
        return HookEntry::Unknown;
    if (size > kHookPatchZone
        && std::memcmp(live + kHookPatchZone, expected + kHookPatchZone, size - kHookPatchZone) != 0)
        return HookEntry::Unknown;
    return HookEntry::ForeignHook;
}

// CheckHookEntry with a mask: only bytes whose mask is non-zero are compared
inline HookEntry CheckHookEntryMasked(const void* fn, const std::uint8_t* expected, const std::uint8_t* mask,
                                      std::size_t size)
{
    std::uint8_t live[128] = {};
    if (!fn || !expected || !mask || size == 0 || size > sizeof(live) || !ReadEntryBytesSEH(fn, live, size))
        return HookEntry::Unknown;
    auto differsFrom = [&](std::size_t first)
    {
        for (std::size_t i = first; i < size; ++i)
            if (mask[i] && live[i] != expected[i])
                return true;
        return false;
    };
    if (!differsFrom(0))
        return HookEntry::Expected;
    if (!EntryHookedByOtherModule(fn) || differsFrom(kHookPatchZone))
        return HookEntry::Unknown;
    return HookEntry::ForeignHook;
}

inline void LogHookedOnTop(const char* what, const void* fn)
{
    char entry[160] = {};
    HookRegistry::Entry ours{};
    const bool hooked = HookRegistry::Find(const_cast<void*>(fn), ours) && ours.trampoline;
    DescribeFunctionEntry(hooked ? ours.trampoline : fn, entry, sizeof(entry));   // once hooked, the entry is our jump; the trampoline starts with theirs
    LogInfo("%s: already hooked by another module (%s), installed on top", what, entry);
}

// MinHook's relay sits in the trampoline's 64-byte arena slot: jmp [rip+0] followed by the detour address
inline std::uint8_t* FindHookRelay(void* trampoline, void* detour)
{
    auto* slot = static_cast<std::uint8_t*>(trampoline);
    for (std::size_t i = 0; slot && i + 14 <= 64; ++i)
    {
        std::uint8_t b[14] = {};
        if (!ReadEntryBytesSEH(slot + i, b, sizeof(b)))
            return nullptr;
        std::uint64_t to = 0;
        std::memcpy(&to, b + 6, sizeof(to));
        if (b[0] == 0xFF && b[1] == 0x25 && b[2] == 0 && b[3] == 0 && b[4] == 0 && b[5] == 0
            && to == reinterpret_cast<std::uint64_t>(detour))
            return slot + i;
    }
    return nullptr;
}

inline bool SetHookRelayTarget(std::uint8_t* relay, const void* to)
{
    DWORD previous = 0;
    if (!relay || !VirtualProtect(relay + 6, sizeof(std::uint64_t), PAGE_EXECUTE_READWRITE, &previous))
        return false;
    const std::uint64_t value = reinterpret_cast<std::uint64_t>(to);
    std::memcpy(relay + 6, &value, sizeof(value));
    VirtualProtect(relay + 6, sizeof(std::uint64_t), previous, &previous);
    FlushInstructionCache(GetCurrentProcess(), relay, 14);
    return true;
}

// Another DLL hooked on top of ours: putting our original bytes back would erase its jump, and freeing our slot would
// leave its saved copy of our jump pointing at freed memory. Our relay goes straight to the original code instead, and
// the hook and its slot stay in place for good.
inline bool PassThroughIfCovered(void* target)
{
    HookRegistry::Entry e{};
    if (!target || !HookRegistry::Find(target, e))
        return false;
    if (e.passThrough)
        return true;
    std::uint8_t* relay = FindHookRelay(e.trampoline, e.detour);
    if (relay && e.chained)   // MinHook would put back bytes from before the other DLL's jump
    {
        if (!SetHookRelayTarget(relay, e.trampoline))
            return false;
        HookRegistry::SetPassThrough(target, true);
        LogWarn("Hook: %p calls through another module's hook, ours stays in place as a pass-through so that hook keeps working",
            target);
        return true;
    }
    std::uint8_t live[5] = {};
    if (!relay || !ReadEntryBytesSEH(target, live, sizeof(live)) || std::memcmp(live, e.original, sizeof(live)) == 0)
        return false;
    const std::uintptr_t at = reinterpret_cast<std::uintptr_t>(target);
    const std::uintptr_t jump = live[0] == 0xEB   // MinHook's patch-above form: a short jump back to its long jump
        ? EntryJumpTarget(at + 2 + static_cast<std::int8_t>(live[1]))
        : EntryJumpTarget(at);
    if (jump == reinterpret_cast<std::uintptr_t>(relay))
        return false;
    if (!SetHookRelayTarget(relay, e.trampoline))
        return false;
    HookRegistry::SetPassThrough(target, true);
    char entry[160] = {};
    DescribeFunctionEntry(target, entry, sizeof(entry));
    LogWarn("Hook: %p is hooked on top by another module (%s), ours stays in place as a pass-through so that hook keeps working",
        target, entry);
    return true;
}

// A queued hook copied the function's first bytes when it was created. When another DLL hooks the function before the
// queue is applied, applying ours would overwrite its jump, so our trampoline is pointed at that jump instead.
inline void ChainQueuedOverForeignHooks()
{
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(reinterpret_cast<std::uintptr_t>(&ChainQueuedOverForeignHooks)), &self);
    for (int i = 0;; ++i)
    {
        HookRegistry::Entry e{};
        AcquireSRWLockShared(&HookRegistry::g_Lock);
        const bool have = i < HookRegistry::g_Count;
        if (have)
            e = HookRegistry::g_Entries[i];
        ReleaseSRWLockShared(&HookRegistry::g_Lock);
        if (!have)
            break;
        if (!e.queued || e.passThrough || e.chained || !e.trampoline)
            continue;
        std::uint8_t live[5] = {};
        if (!ReadEntryBytesSEH(e.target, live, sizeof(live)) || std::memcmp(live, e.original, sizeof(live)) == 0)
            continue;
        const std::uintptr_t at = reinterpret_cast<std::uintptr_t>(e.target);
        const std::uintptr_t jump = live[0] == 0xEB
            ? EntryJumpTarget(at + 2 + static_cast<std::int8_t>(live[1]))
            : EntryJumpTarget(at);
        std::uint8_t* relay = FindHookRelay(e.trampoline, e.detour);
        HMODULE owner = nullptr;
        ResolveEntryJump(e.target, owner);
        if (!jump || !relay || jump == reinterpret_cast<std::uintptr_t>(relay)
            || !owner || owner == GetModuleHandleW(nullptr) || owner == self)
            continue;
        std::uint8_t patch[14] = { 0xFF, 0x25, 0, 0, 0, 0 };
        std::memcpy(patch + 6, &jump, sizeof(jump));
        DWORD previous = 0;
        if (!VirtualProtect(e.trampoline, sizeof(patch), PAGE_EXECUTE_READWRITE, &previous))
            continue;
        std::memcpy(e.trampoline, patch, sizeof(patch));
        VirtualProtect(e.trampoline, sizeof(patch), previous, &previous);
        FlushInstructionCache(GetCurrentProcess(), e.trampoline, sizeof(patch));
        HookRegistry::SetChained(e.target);
        char entry[160] = {};
        DescribeFunctionEntry(e.target, entry, sizeof(entry));
        LogInfo("Hook: %p was hooked by another module before ours went in (%s), ours calls through it", e.target, entry);
    }
}

inline MH_STATUS ApplyQueuedHooks()
{
    ChainQueuedOverForeignHooks();
    const MH_STATUS st = MH_ApplyQueued();
    HookRegistry::ClearQueued();
    return st;
}

inline bool DisableAndRemoveHook(void* target)
{
    if (!target)
        return false;
    if (PassThroughIfCovered(target))
        return true;

    MH_DisableHook(target);
    MH_RemoveHook(target);
    HookRegistry::Forget(target);
    return true;
}

// DLL unload: every hook still tracked goes through DisableAndRemoveHook; true when one had to stay as a pass-through,
// in which case MH_Uninitialize must not run (it would put the original bytes back over the other DLL's jump)
inline bool UnhookAllSafely()
{
    for (;;)
    {
        void* next = nullptr;
        AcquireSRWLockShared(&HookRegistry::g_Lock);
        for (int i = 0; i < HookRegistry::g_Count && !next; ++i)
            if (!HookRegistry::g_Entries[i].passThrough)
                next = HookRegistry::g_Entries[i].target;
        ReleaseSRWLockShared(&HookRegistry::g_Lock);
        if (!next)
            break;
        if (!PassThroughIfCovered(next))
        {
            MH_DisableHook(next);
            MH_RemoveHook(next);
            HookRegistry::Forget(next);
        }
    }
    return HookRegistry::g_AnyPassThrough;
}


// For hooks only reachable once the game runs: a code hook made then stops every thread (~0.5 s on slow PCs), this stops none.
inline bool SwapVtableSlot(void** slot, void* hook, void** original)
{
    if (!slot || !hook || !original)
        return false;
    if (*slot == hook)
        return true;

    DWORD previous = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &previous))
        return false;
    *original = *slot;
    InterlockedExchangePointer(slot, hook);
    VirtualProtect(slot, sizeof(void*), previous, &previous);
    return true;
}


// ourHook: when given and the slot no longer holds it, another DLL swapped the slot after us and keeps its own chain
inline void RestoreVtableSlot(void** slot, void* original, void* ourHook = nullptr)
{
    DWORD previous = 0;
    if (!slot || !original || (ourHook && *slot != ourHook)
        || !VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &previous))
        return;
    InterlockedExchangePointer(slot, original);
    VirtualProtect(slot, sizeof(void*), previous, &previous);
}