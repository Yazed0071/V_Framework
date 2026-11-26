#include <pch.h>
#include "VertexStreamPool.h"

#include <Windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    constexpr std::int32_t kVanillaSlots = 3496;
    constexpr std::int32_t kNewSlots     = 6144;    // SSD Afghanistan ran out at 3496/3496

    // test rax,rax / jz +0F / mov edx,<slots> / mov rcx,rax / call <pool ctor> / jmp +3 / mov rax,rbx /
    // mov [VertexStreamPool_Instance],rax
    constexpr std::size_t kSiteLen      = 30;
    constexpr std::size_t kSlotsOff     = 6;        // imm32 of the mov edx
    constexpr std::size_t kStoreDispOff = 26;
    constexpr std::size_t kCapacityOff  = 0x10;     // slot count in the built pool
    const std::uint8_t kSiteCode[kSiteLen] = {
        0x48, 0x85, 0xC0, 0x74, 0x0F, 0xBA, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xC8, 0xE8, 0x00,
        0x00, 0x00, 0x00, 0xEB, 0x03, 0x48, 0x8B, 0xC3, 0x48, 0x89, 0x05, 0x00, 0x00, 0x00, 0x00 };
    // 0: the slot count, the call target and the store displacement (they differ per build)
    const std::uint8_t kSiteMask[kSiteLen] = {
        1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0,
        0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0 };

    enum class PrePatchState : int
    {
        NotAttempted,
        AddressMissing,
        SiteUnexpected,
        SlotsUnexpected,
        PoolAlreadyBuilt,
        WriteFailed,
        Applied,
        AlreadyApplied,     // another module got there first (the SSD map's old STAFVertexStreams.dll)
    };

    std::atomic<PrePatchState> g_PrePatch{ PrePatchState::NotAttempted };
    std::uint8_t**             g_Instance   = nullptr;
    std::int32_t               g_FoundSlots = 0;
    DWORD                      g_WriteError = 0;
    char                       g_FoundCode[kSiteLen * 3] = {};
    std::atomic<bool>          g_StopCheck{ false };

    bool SiteMatches(const std::uint8_t* code)
    {
        for (std::size_t i = 0; i < kSiteLen; ++i)
            if (kSiteMask[i] && code[i] != kSiteCode[i])
                return false;
        return true;
    }

    void NoteFoundCode(const std::uint8_t* code)
    {
        for (std::size_t i = 0; i < kSiteLen; ++i)
            std::snprintf(g_FoundCode + i * 3, sizeof(g_FoundCode) - i * 3, i + 1 < kSiteLen ? "%02X " : "%02X", code[i]);
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

    DWORD WINAPI PoolCheckThread(LPVOID)
    {
        for (int i = 0; i < 1200 && !g_StopCheck.load(); ++i)   // 120 s
        {
            std::uint8_t* pool     = nullptr;
            std::int32_t  capacity = 0;
            if (ReadEntryBytesSEH(g_Instance, &pool, sizeof(pool)) && pool
                && ReadEntryBytesSEH(pool + kCapacityOff, &capacity, sizeof(capacity)))
            {
                if (capacity == kNewSlots)
                    LogInfo("VertexStreamPool: pool built with %d slots", capacity);
                else
                    LogWarn("VertexStreamPool: pool built with %d slots, not %d", capacity, kNewSlots);
                return 0;
            }
            Sleep(100);
        }
        if (!g_StopCheck.load())
            LogWarn("VertexStreamPool: no pool within 120 s, its size is unchecked");
        return 0;
    }
}

// mgsvtpp builds the pool once, before V_FrameWork's InitThread runs, so this is called from DllMain.
void PreApplyVertexStreamPoolPatch()
{
    PrePatchState expected = PrePatchState::NotAttempted;
    if (!g_PrePatch.compare_exchange_strong(expected, PrePatchState::AddressMissing))
        return;

    auto* site     = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.VertexStreamPool_CreateSite));
    auto* instance = static_cast<std::uint8_t**>(ResolveGameAddress(gAddr.VertexStreamPool_Instance));
    if (!site || !instance)
        return;

    std::uint8_t code[kSiteLen] = {};
    if (!ReadEntryBytesSEH(site, code, kSiteLen))
    {
        g_PrePatch = PrePatchState::SiteUnexpected;
        return;
    }
    NoteFoundCode(code);
    std::int32_t disp = 0;
    std::memcpy(&disp, code + kStoreDispOff, sizeof(disp));
    if (!SiteMatches(code) || reinterpret_cast<std::uint8_t**>(site + kSiteLen + disp) != instance)
    {
        g_PrePatch = PrePatchState::SiteUnexpected;
        return;
    }
    g_Instance = instance;

    std::memcpy(&g_FoundSlots, code + kSlotsOff, sizeof(g_FoundSlots));
    if (g_FoundSlots == kNewSlots)
    {
        g_PrePatch = PrePatchState::AlreadyApplied;
        return;
    }
    if (g_FoundSlots != kVanillaSlots)
    {
        g_PrePatch = PrePatchState::SlotsUnexpected;
        return;
    }
    std::uint8_t* pool = nullptr;
    if (!ReadEntryBytesSEH(instance, &pool, sizeof(pool)) || pool)
    {
        g_PrePatch = PrePatchState::PoolAlreadyBuilt;
        return;
    }
    g_PrePatch = WriteBytes(site + kSlotsOff, &kNewSlots, sizeof(kNewSlots))
        ? PrePatchState::Applied : PrePatchState::WriteFailed;
}

bool Install_VertexStreamPool()
{
    g_StopCheck = false;
    if (g_PrePatch.load() == PrePatchState::NotAttempted)
        PreApplyVertexStreamPoolPatch();   // DllMain skipped it; the same checks apply

    switch (g_PrePatch.load())
    {
    case PrePatchState::Applied:
        LogInfo("VertexStreamPool: pool size %d -> %d slots", kVanillaSlots, kNewSlots);
        break;
    case PrePatchState::AlreadyApplied:
        LogInfo("VertexStreamPool: pool size already %d slots (another module set it)", kNewSlots);
        break;
    case PrePatchState::PoolAlreadyBuilt:
        LogWarn("VertexStreamPool: pool was built before V_FrameWork loaded, it stays %d slots", g_FoundSlots);
        break;
    case PrePatchState::SlotsUnexpected:
        LogWarn("VertexStreamPool: pool size is %d, not vanilla %d (another module changed it), left as it is",
                g_FoundSlots, kVanillaSlots);
        return true;
    case PrePatchState::AddressMissing:
    case PrePatchState::NotAttempted:
        LogWarn("VertexStreamPool: pool addresses missing on %s, pool stays vanilla", GetGameBuildName(gGameBuild));
        return true;
    case PrePatchState::SiteUnexpected:
        LogError("VertexStreamPool: pool creation code unexpected (%s), pool stays vanilla",
                 g_FoundCode[0] ? g_FoundCode : "unreadable");
        return true;
    case PrePatchState::WriteFailed:
        LogError("VertexStreamPool: VirtualProtect failed (err %lu), pool stays vanilla", g_WriteError);
        return false;
    }

    HANDLE thread = CreateThread(nullptr, 0, &PoolCheckThread, nullptr, 0, nullptr);
    if (thread)
        CloseHandle(thread);
    return true;
}

void Uninstall_VertexStreamPool()
{
    // the pool is built once, so the new size stays
    g_StopCheck = true;
}
