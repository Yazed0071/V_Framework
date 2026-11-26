#include <pch.h>
#include "TextureStoragePool.h"

#include <Windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

// The texture streamer keeps the smallest level of every streamed texture in storage pool 0, one block of 32 KiB
// units sized once when the streamer starts. A texture that does not fit waits for room with no time limit, and the
// texture packs queued behind it wait too. The pool's bookkeeping is sized by the texture count, not by its units, so
// only the size goes up here; the texture slots and the streamer stay vanilla.
namespace
{
    constexpr std::uint32_t kUnitBytes   = 0x8000;       // the unit the setup hands the storage manager
    constexpr std::uint32_t kVanillaSize = 0x10680000;   // 262.5 MiB, 8400 units
    // SSD Afghanistan with Female Avatar asks for about 1300-1400 units more than vanilla's 8400 at its start. At
    // most 0x1D550000: above that the setup's level-2 budget wraps on a GPU at the engine's 512 MiB floor.
    constexpr std::uint32_t kNewSize     = 0x1C000000;   // 448 MiB, 14336 units

    // mov [rdi+0x30a64],ecx / mov dword ptr [rdi+0x30a58],<pool 0 bytes> / mov ecx,[rdi+0x30a64]
    // (the texture streamer's setup FUN_14021dee0, real name unknown; the size site is the middle instruction)
    constexpr std::size_t kWindowBack = 6;      // the window starts at the mov before the size site
    constexpr std::size_t kWindowLen  = 22;
    constexpr std::size_t kSiteLen    = 10;     // the size site's mov
    constexpr std::size_t kSizeOff    = 6;      // its imm32
    const std::uint8_t kWindowCode[kWindowLen] = {
        0x89, 0x8F, 0x64, 0x0A, 0x03, 0x00, 0xC7, 0x87, 0x58, 0x0A, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x8B, 0x8F, 0x64, 0x0A, 0x03, 0x00 };
    // 0: the pool size
    const std::uint8_t kWindowMask[kWindowLen] = {
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0,
        1, 1, 1, 1, 1, 1 };

    // the setup reads the size back and hands it on as the storage manager's pool 0 size
    struct Anchor
    {
        std::size_t  siteOff;   // from the size site
        std::size_t  len;
        std::uint8_t code[8];
    };
    constexpr std::size_t kAnchorCount = 2;
    const Anchor kAnchors[kAnchorCount] = {
        { 0x66, 7, { 0x44, 0x8B, 0x87, 0x58, 0x0A, 0x03, 0x00 } },   // mov r8d,[rdi+0x30a58]
        { 0xA6, 5, { 0x44, 0x89, 0x44, 0x24, 0x2C } },               // mov [rsp+0x2c],r8d: storage parameter 1
    };

    constexpr std::size_t kStorageOff = 0x26388;   // DgTextureStreamer: its texture storage manager, null until the setup
    constexpr std::size_t kPool0Off   = 0x40;      // storage manager: pool 0's BlockAllocator
    constexpr std::size_t kTotalOff   = 0x08;      // BlockAllocator: bytes
    constexpr std::size_t kUnitOff    = 0x0C;      // BlockAllocator: bytes per unit
    constexpr std::size_t kUsedOff    = 0x18;      // BlockAllocator: units in use

    // TextureStorageManagerBase::AllocBlock's first instructions. TPP-Mod's r_increase_texture_memory hooks it to put
    // a texture that does not fit pool 0 into pool 1; another module's jump there is only noted.
    const std::uint8_t kAllocBlockEntry[12] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48 };

    enum class PrePatchState : int
    {
        NotAttempted,
        AddressMissing,
        SiteUnexpected,
        SizeUnexpected,
        PoolAlreadyBuilt,
        WriteFailed,
        Applied,
        AlreadyApplied,     // another module got there first
    };

    std::atomic<PrePatchState> g_PrePatch{ PrePatchState::NotAttempted };
    std::uint8_t*              g_Site       = nullptr;   // set once the size code is verified
    std::uint8_t**             g_Streamer   = nullptr;
    std::uint32_t              g_FoundSize  = 0;
    DWORD                      g_WriteError = 0;
    char                       g_FoundCode[192] = {};
    std::uint8_t               g_LeftCode[kWindowLen] = {};   // the window as V_FrameWork left it
    std::atomic<bool>          g_StopCheck{ false };
    std::atomic<bool>          g_CheckThreadStarted{ false };
    std::atomic<unsigned>      g_CheckGeneration{ 0 };   // each Install restarts the check's logging

    double Mib(std::uint64_t bytes)
    {
        return static_cast<double>(bytes) / (1024.0 * 1024.0);
    }

    bool WindowMatches(const std::uint8_t* code)
    {
        for (std::size_t i = 0; i < kWindowLen; ++i)
            if (kWindowMask[i] && code[i] != kWindowCode[i])
                return false;
        return true;
    }

    void NoteFoundCode(char* out, std::size_t cap, const char* where, const std::uint8_t* code, std::size_t size)
    {
        const int head = std::snprintf(out, cap, "%s", where);
        std::size_t at = head > 0 ? static_cast<std::size_t>(head) : 0;
        for (std::size_t i = 0; i < size && at < cap; ++i, at += 3)
            std::snprintf(out + at, cap - at, i + 1 < size ? "%02X " : "%02X", code[i]);
    }

    // another module's jump on one of the window's three instructions: named by its module
    void NoteForeignJump(char* out, std::size_t cap, const std::uint8_t* window)
    {
        const std::size_t starts[] = { 0, kWindowBack, kWindowBack + kSiteLen };
        for (std::size_t start : starts)
        {
            if (!EntryJumpTarget(reinterpret_cast<std::uintptr_t>(window + start)))
                continue;
            char jump[160] = {};
            DescribeFunctionEntry(window + start, jump, sizeof(jump));
            const std::size_t used = std::strlen(out);
            if (used < cap)
                std::snprintf(out + used, cap - used, "; %s", jump);
            return;
        }
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

    // the streamer builds pool 0 in its setup and keeps it in its storage manager; both pointers start null
    bool PoolBuilt(std::uint8_t** streamerInstance)
    {
        std::uint8_t* streamer = nullptr;
        if (!ReadEntryBytesSEH(streamerInstance, &streamer, sizeof(streamer)))
            return true;
        std::uint8_t* storage = nullptr;
        return streamer && (!ReadEntryBytesSEH(streamer + kStorageOff, &storage, sizeof(storage)) || storage);
    }

    // pool 0's BlockAllocator, null while the streamer has none (before its setup and after its teardown)
    std::uint8_t* CurrentPool()
    {
        std::uint8_t* streamer = nullptr;
        std::uint8_t* storage  = nullptr;
        std::uint8_t* pool     = nullptr;
        if (!ReadEntryBytesSEH(g_Streamer, &streamer, sizeof(streamer)) || !streamer
            || !ReadEntryBytesSEH(streamer + kStorageOff, &storage, sizeof(storage)) || !storage
            || !ReadEntryBytesSEH(storage + kPool0Off, &pool, sizeof(pool)))
            return nullptr;
        return pool;
    }

    // Use levels logged once each, so a test run shows how close pool 0 comes to its limit.
    enum UseLevel : int
    {
        AboveVanilla,
        ThreeQuarters,
        NinetyPercent,
        Full,
        LevelCount
    };

    void NoteUse(std::uint32_t used, std::uint32_t units, std::uint32_t unitBytes, bool allocBlockHooked,
                 bool (&logged)[LevelCount])
    {
        const std::uint64_t u = used;
        const std::uint64_t c = units;
        if (!logged[AboveVanilla] && u * unitBytes > kVanillaSize)
        {
            LogInfo("TextureStoragePool: pool 0 in use %u units (%.1f MiB), more than vanilla %.1f MiB",
                    used, Mib(u * unitBytes), Mib(kVanillaSize));
            logged[AboveVanilla] = true;
        }
        if (!logged[ThreeQuarters] && u * 4 >= c * 3 && u + 1 < c)
        {
            LogWarn("TextureStoragePool: pool 0 in use %u of %u units (75%%)", used, units);
            logged[ThreeQuarters] = true;
        }
        if (!logged[NinetyPercent] && u * 10 >= c * 9 && u + 1 < c)
        {
            LogWarn("TextureStoragePool: pool 0 in use %u of %u units (90%%)", used, units);
            logged[NinetyPercent] = true;
        }
        if (!logged[Full] && u + 1 >= c)   // AllocBlock's room test for a one-unit block
        {
            if (allocBlockHooked)
                LogWarn("TextureStoragePool: pool 0 full (%u of %u units), another module's AllocBlock hook may put "
                        "new textures in pool 1", used, units);
            else
                LogError("TextureStoragePool: pool 0 full (%u of %u units), textures wait for room and their packs "
                         "can hang", used, units);
            logged[Full] = true;
        }
    }

    // another module that rewrites the size code after V_FrameWork's check: noted once, never undone
    void NoteSiteChange(bool& noted)
    {
        std::uint8_t code[kWindowLen] = {};
        if (noted || !ReadEntryBytesSEH(g_Site - kWindowBack, code, kWindowLen)
            || std::memcmp(code, g_LeftCode, kWindowLen) == 0)
            return;
        noted = true;
        const char* how  = g_PrePatch.load() == PrePatchState::Applied ? "set" : "checked";
        // the setup reads the size once, so a change after pool 0 is built does nothing in this run
        const char* when = CurrentPool() ? ", after pool 0 was built, so it changes nothing this run" : "";
        if (!WindowMatches(code))
        {
            char found[192] = {};
            NoteFoundCode(found, sizeof(found), "", code, kWindowLen);
            NoteForeignJump(found, sizeof(found), g_Site - kWindowBack);
            LogWarn("TextureStoragePool: another module changed pool 0's size code after V_FrameWork %s it (%s)%s",
                    how, found, when);
            return;
        }
        std::uint32_t size = 0;
        std::memcpy(&size, code + kWindowBack + kSizeOff, sizeof(size));
        LogWarn("TextureStoragePool: another module changed pool 0's size to %u bytes (%.1f MiB) after V_FrameWork %s it%s",
                size, Mib(size), how, when);
    }

    void NoteAllocBlockHook(bool& noted)
    {
        if (noted)
            return;
        const void*  allocBlock = ResolveGameAddress(gAddr.TextureStoragePool_AllocBlock);
        std::uint8_t entry[sizeof(kAllocBlockEntry)] = {};
        if (!allocBlock || !ReadEntryBytesSEH(allocBlock, entry, sizeof(entry))
            || std::memcmp(entry, kAllocBlockEntry, sizeof(entry)) == 0)
            return;
        noted = true;
        char described[160] = {};
        DescribeFunctionEntry(allocBlock, described, sizeof(described));
        LogInfo("TextureStoragePool: texture storage AllocBlock already hooked by another module (%s), "
                "it may move pool 0's overflow to pool 1", described);
    }

    // Logs pool 0's built size, then its use once a second, and notes another module's later change to the size
    // code or to AllocBlock. Started once and never ends (like RendererPools' check thread); it idles while the
    // module is uninstalled.
    DWORD WINAPI PoolCheckThread(LPVOID)
    {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        std::uint8_t* pool      = nullptr;   // the BlockAllocator whose size was logged
        std::uint32_t units     = 0;
        std::uint32_t unitBytes = 0;
        bool          logged[LevelCount] = {};
        bool          lateNoted = false;
        bool          siteNoted = false;
        bool          hookNoted = false;
        unsigned      generation = 0;
        int           startTick = 0;
        for (int tick = 1; ; ++tick)
        {
            Sleep(100);
            if (g_StopCheck.load())
                continue;
            const unsigned current = g_CheckGeneration.load();
            if (current != generation)
            {
                generation = current;
                startTick = tick;
                pool = nullptr;
                lateNoted = false;
                siteNoted = false;
                hookNoted = false;
                std::memset(logged, 0, sizeof(logged));
            }
            if ((tick - startTick) % 10 != 0)
                continue;

            NoteSiteChange(siteNoted);
            NoteAllocBlockHook(hookNoted);

            std::uint8_t* built = CurrentPool();
            std::uint32_t total = 0;
            std::uint32_t unit  = 0;
            if (built && built != pool
                && ReadEntryBytesSEH(built + kTotalOff, &total, sizeof(total))
                && ReadEntryBytesSEH(built + kUnitOff, &unit, sizeof(unit)) && unit && total >= unit)
            {
                pool = built;
                units = total / unit;
                unitBytes = unit;
                std::memset(logged, 0, sizeof(logged));
                const PrePatchState state = g_PrePatch.load();
                if (total == kNewSize || (state != PrePatchState::Applied && state != PrePatchState::AlreadyApplied))
                    LogInfo("TextureStoragePool: pool 0 built with %u units (%.1f MiB)", units, Mib(total));
                else
                    LogWarn("TextureStoragePool: pool 0 built with %u units (%.1f MiB), not %u",
                            units, Mib(total), kNewSize / kUnitBytes);
            }
            if (!pool && !lateNoted && tick - startTick >= 1200)   // 120 s
            {
                LogWarn("TextureStoragePool: pool 0 not built within 120 s, still watching");
                lateNoted = true;
            }

            std::uint32_t used = 0;
            if (pool && built == pool && ReadEntryBytesSEH(pool + kUsedOff, &used, sizeof(used)))
                NoteUse(used, units, unitBytes, hookNoted, logged);
        }
    }

    void StartCheckThread()
    {
        if (g_CheckThreadStarted.exchange(true))
            return;
        HMODULE pinnedModule = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(&PoolCheckThread), &pinnedModule))
            LogWarn("TextureStoragePool: cannot pin the module for the check thread (err %lu)", GetLastError());
        HANDLE thread = CreateThread(nullptr, 0, &PoolCheckThread, nullptr, 0, nullptr);
        if (thread)
            CloseHandle(thread);
        else
        {
            g_CheckThreadStarted = false;
            LogWarn("TextureStoragePool: check thread failed to start, pool 0's size and use are not logged");
        }
    }
}

// mgsvtpp builds pool 0 once, when the renderer starts (fox::gr::InitResources), so this is called from DllMain.
void PreApplyTextureStoragePoolPatch()
{
    PrePatchState expected = PrePatchState::NotAttempted;
    if (!g_PrePatch.compare_exchange_strong(expected, PrePatchState::AddressMissing))
        return;

    auto* site     = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.TextureStoragePool_SizeSite));
    auto* streamer = static_cast<std::uint8_t**>(ResolveGameAddress(gAddr.TextureStoragePool_Streamer));
    if (!site || !streamer)
        return;

    std::uint8_t window[kWindowLen] = {};
    if (!ReadEntryBytesSEH(site - kWindowBack, window, kWindowLen))
    {
        g_PrePatch = PrePatchState::SiteUnexpected;
        return;
    }
    NoteFoundCode(g_FoundCode, sizeof(g_FoundCode), "", window, kWindowLen);
    if (!WindowMatches(window))
    {
        NoteForeignJump(g_FoundCode, sizeof(g_FoundCode), site - kWindowBack);
        g_PrePatch = PrePatchState::SiteUnexpected;
        return;
    }
    for (const Anchor& anchor : kAnchors)
    {
        std::uint8_t code[sizeof(Anchor::code)] = {};
        if (!ReadEntryBytesSEH(site + anchor.siteOff, code, anchor.len))
        {
            std::snprintf(g_FoundCode, sizeof(g_FoundCode), "size site +0x%zX: unreadable", anchor.siteOff);
            g_PrePatch = PrePatchState::SiteUnexpected;
            return;
        }
        if (std::memcmp(code, anchor.code, anchor.len) != 0)
        {
            char where[32] = {};
            std::snprintf(where, sizeof(where), "size site +0x%zX: ", anchor.siteOff);
            NoteFoundCode(g_FoundCode, sizeof(g_FoundCode), where, code, anchor.len);
            if (EntryJumpTarget(reinterpret_cast<std::uintptr_t>(site + anchor.siteOff)))
            {
                char jump[160] = {};
                DescribeFunctionEntry(site + anchor.siteOff, jump, sizeof(jump));
                const std::size_t used = std::strlen(g_FoundCode);
                if (used < sizeof(g_FoundCode))
                    std::snprintf(g_FoundCode + used, sizeof(g_FoundCode) - used, "; %s", jump);
            }
            g_PrePatch = PrePatchState::SiteUnexpected;
            return;
        }
    }
    g_Site = site;
    g_Streamer = streamer;
    std::memcpy(g_LeftCode, window, kWindowLen);

    std::memcpy(&g_FoundSize, window + kWindowBack + kSizeOff, sizeof(g_FoundSize));
    if (g_FoundSize == kNewSize)
    {
        g_PrePatch = PrePatchState::AlreadyApplied;
        return;
    }
    if (g_FoundSize != kVanillaSize)
    {
        g_PrePatch = PrePatchState::SizeUnexpected;
        return;
    }
    if (PoolBuilt(streamer))
    {
        g_PrePatch = PrePatchState::PoolAlreadyBuilt;
        return;
    }
    if (!WriteBytes(site + kSizeOff, &kNewSize, sizeof(kNewSize)))
    {
        g_PrePatch = PrePatchState::WriteFailed;
        return;
    }
    std::memcpy(g_LeftCode + kWindowBack + kSizeOff, &kNewSize, sizeof(kNewSize));
    g_PrePatch = PrePatchState::Applied;
}

bool Install_TextureStoragePool()
{
    if (g_PrePatch.load() == PrePatchState::NotAttempted)
        PreApplyTextureStoragePoolPatch();   // DllMain skipped it; the same checks apply

    bool ok = true;
    switch (g_PrePatch.load())
    {
    case PrePatchState::Applied:
        LogInfo("TextureStoragePool: pool 0 size %.1f -> %.1f MiB (%u -> %u units)",
                Mib(kVanillaSize), Mib(kNewSize), kVanillaSize / kUnitBytes, kNewSize / kUnitBytes);
        break;
    case PrePatchState::AlreadyApplied:
        LogInfo("TextureStoragePool: pool 0 size already %.1f MiB (another module set it)", Mib(kNewSize));
        break;
    case PrePatchState::PoolAlreadyBuilt:
        LogWarn("TextureStoragePool: pool 0 was built before V_FrameWork loaded, it stays %.1f MiB", Mib(g_FoundSize));
        break;
    case PrePatchState::SizeUnexpected:
        if (g_FoundSize > kNewSize)
            LogInfo("TextureStoragePool: another module set pool 0 to %u bytes (%.1f MiB, more than V_FrameWork's %.1f), "
                    "left as it is", g_FoundSize, Mib(g_FoundSize), Mib(kNewSize));
        else
            LogWarn("TextureStoragePool: another module set pool 0 to %u bytes (%.1f MiB, vanilla %.1f), left as it is",
                    g_FoundSize, Mib(g_FoundSize), Mib(kVanillaSize));
        break;
    case PrePatchState::AddressMissing:
    case PrePatchState::NotAttempted:
        LogWarn("TextureStoragePool: pool 0 addresses missing on %s, it stays vanilla", GetGameBuildName(gGameBuild));
        break;
    case PrePatchState::SiteUnexpected:
        LogError("TextureStoragePool: pool 0 size code unexpected (%s), it stays vanilla",
                 g_FoundCode[0] ? g_FoundCode : "unreadable");
        break;
    case PrePatchState::WriteFailed:
        LogError("TextureStoragePool: pool 0 VirtualProtect failed (err %lu), it stays vanilla", g_WriteError);
        ok = false;
        break;
    }

    if (g_Site)
    {
        g_CheckGeneration.fetch_add(1);
        g_StopCheck = false;
        StartCheckThread();
    }
    return ok;
}

void Uninstall_TextureStoragePool()
{
    // pool 0 is built once, so the new size stays; the check thread idles
    g_StopCheck = true;
}

std::uint32_t GetTextureStoragePool0Size()
{
    auto*        site = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.TextureStoragePool_SizeSite));
    std::uint8_t window[kWindowLen] = {};
    if (!site || !ReadEntryBytesSEH(site - kWindowBack, window, kWindowLen) || !WindowMatches(window))
        return 0;
    std::uint32_t size = 0;
    std::memcpy(&size, window + kWindowBack + kSizeOff, sizeof(size));
    return size;
}
