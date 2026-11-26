#include <pch.h>

#include "NoticeSensorImpl_Init.h"

#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using Init_t = void*(__fastcall*)(std::uint8_t* sensor, const std::uint8_t* desc);

    constexpr std::uint32_t kVanillaEntries     = 0x20;
    constexpr std::uint32_t kVanillaControllers = 8;
    constexpr std::uint32_t kEntries            = 0x40;
    constexpr std::uint32_t kControllers        = 16;

    constexpr std::size_t kFlags           = 0x28;
    constexpr std::size_t kTableBytes      = 0x48;
    constexpr std::size_t kTableState      = 0x5A;   // bit 0: the table memory is allocated
    constexpr std::size_t kControllerArray = 0x68;
    constexpr std::size_t kControllerSize  = 0x30;
    // a controller's entry array, HUD and game socket; the game links only the first 8 controllers
    constexpr std::size_t kControllerLinks[] = { 0x08, 0x20, 0x28 };

    const std::uint8_t kInitEntry[] = { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18,
                                        0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x40 };

    // mov edx,8 (controllers) / and r8b,2 / xor [rcx-0x10],r8b / mov r8d,edx / call /
    // mov edx,0x20 (entries) / lea rcx,[rbx+0x38] / lea r8d,[rdx-0x1C] (entry alignment 4)
    const std::uint8_t kSizePassVanilla[] = {
        0xBA, 0x08, 0x00, 0x00, 0x00, 0x41, 0x80, 0xE0, 0x02, 0x44, 0x30, 0x41, 0xF0, 0x44, 0x8B, 0xC2, 0xE8,
        0x00, 0x00, 0x00, 0x00, 0xBA, 0x20, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x4B, 0x38, 0x44, 0x8D, 0x42, 0xE4 };
    const std::uint8_t kSizePassEnlarged[] = {
        0xBA, 0x10, 0x00, 0x00, 0x00, 0x41, 0x80, 0xE0, 0x02, 0x44, 0x30, 0x41, 0xF0, 0x44, 0x8B, 0xC2, 0xE8,
        0x00, 0x00, 0x00, 0x00, 0xBA, 0x40, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x4B, 0x38, 0x44, 0x8D, 0x42, 0xC4 };
    const std::uint8_t kSizePassMask[] = {
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };

    // the same sizes again where the game builds the arrays:
    // mov edx,8 / lea rcx,[rbx+0x38] / mov r8d,edx / call / mov edx,0x20 / lea rcx,[rbx+0x38] / lea r8d,[rdx-0x1C]
    const std::uint8_t kBuildPassVanilla[] = {
        0xBA, 0x08, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x4B, 0x38, 0x44, 0x8B, 0xC2, 0xE8, 0x00, 0x00,
        0x00, 0x00, 0xBA, 0x20, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x4B, 0x38, 0x44, 0x8D, 0x42, 0xE4 };
    const std::uint8_t kBuildPassEnlarged[] = {
        0xBA, 0x10, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x4B, 0x38, 0x44, 0x8B, 0xC2, 0xE8, 0x00, 0x00,
        0x00, 0x00, 0xBA, 0x40, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x4B, 0x38, 0x44, 0x8D, 0x42, 0xC4 };
    const std::uint8_t kBuildPassMask[] = {
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0,
        0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };

    // mov dword [rsp+0x24],0x20: game socket slots / mov dword [rsp+0x34],0x20: synchronizer records, one per entry
    const std::uint8_t kSocketSlotsVanilla[]    = { 0xC7, 0x44, 0x24, 0x24, 0x20, 0x00, 0x00, 0x00 };
    const std::uint8_t kSocketSlotsEnlarged[]   = { 0xC7, 0x44, 0x24, 0x24, 0x40, 0x00, 0x00, 0x00 };
    const std::uint8_t kSyncRecordsVanilla[]    = { 0xC7, 0x44, 0x24, 0x34, 0x20, 0x00, 0x00, 0x00 };
    const std::uint8_t kSyncRecordsEnlarged[]   = { 0xC7, 0x44, 0x24, 0x34, 0x40, 0x00, 0x00, 0x00 };
    const std::uint8_t kWholeMask[]             = { 1, 1, 1, 1, 1, 1, 1, 1 };

    struct CapacitySite
    {
        const char*         name;
        std::uint8_t*       code;
        const std::uint8_t* vanilla;
        const std::uint8_t* enlarged;
        const std::uint8_t* mask;
        std::size_t         size;
    };

    enum class SiteState { Vanilla, Enlarged, Unknown };

    CapacitySite g_Sites[] = {
        { "sizing pass",         nullptr, kSizePassVanilla,    kSizePassEnlarged,    kSizePassMask,  sizeof(kSizePassVanilla) },
        { "build pass",          nullptr, kBuildPassVanilla,   kBuildPassEnlarged,   kBuildPassMask, sizeof(kBuildPassVanilla) },
        { "game socket",         nullptr, kSocketSlotsVanilla, kSocketSlotsEnlarged, kWholeMask,     sizeof(kSocketSlotsVanilla) },
        { "synchronizer",        nullptr, kSyncRecordsVanilla, kSyncRecordsEnlarged, kWholeMask,     sizeof(kSyncRecordsVanilla) },
    };

    Init_t                     g_Orig = nullptr;
    void*                      g_Target = nullptr;
    std::mutex                 g_CodeLock;   // the sizes may only change while no Init runs
    std::atomic<std::uint32_t> g_EntrySlots{ kVanillaEntries };
    std::atomic<std::uint32_t> g_ControllerSlots{ kVanillaControllers };
    std::atomic<std::uint32_t> g_BuildCount{ 0 };
    std::atomic<bool>          g_CodeChangeReported{ false };

    bool Matches(const std::uint8_t* live, const std::uint8_t* expected, const std::uint8_t* mask, std::size_t size)
    {
        for (std::size_t i = 0; i < size; ++i)
            if (mask[i] && live[i] != expected[i])
                return false;
        return true;
    }

    SiteState ReadState(const CapacitySite& site)
    {
        std::uint8_t live[64] = {};
        if (!site.code || site.size > sizeof(live) || !ReadEntryBytesSEH(site.code, live, site.size))
            return SiteState::Unknown;
        if (Matches(live, site.vanilla, site.mask, site.size))
            return SiteState::Vanilla;
        if (Matches(live, site.enlarged, site.mask, site.size))
            return SiteState::Enlarged;
        return SiteState::Unknown;
    }

    void ReportCodeChange(const CapacitySite& site)
    {
        if (g_CodeChangeReported.exchange(true))
            return;
        char found[64 * 3] = {};
        std::uint8_t live[64] = {};
        if (site.code && ReadEntryBytesSEH(site.code, live, site.size))
            for (std::size_t i = 0; i < site.size; ++i)
                std::snprintf(found + i * 3, sizeof(found) - i * 3, i + 1 < site.size ? "%02X " : "%02X", live[i]);
        LogError("NoticeSensor: notice table %s code is not vanilla (%s), the table stays as it is",
                 site.name, found[0] ? found : "unreadable");
    }

    // every site or none: both passes must size the arrays the same
    bool SetCapacity(bool enlarged)
    {
        bool change = false;
        for (const CapacitySite& site : g_Sites)
        {
            const SiteState state = ReadState(site);
            if (state == SiteState::Unknown)
            {
                ReportCodeChange(site);
                return false;
            }
            change |= (state == SiteState::Enlarged) != enlarged;
        }
        if (!change)
            return true;

        std::uint8_t* first = g_Sites[0].code;
        std::uint8_t* end = g_Sites[0].code + g_Sites[0].size;
        for (const CapacitySite& site : g_Sites)
        {
            first = (std::min)(first, site.code);
            end = (std::max)(end, site.code + site.size);
        }
        const std::size_t span = static_cast<std::size_t>(end - first);
        DWORD oldProtect = 0;
        if (!VirtualProtect(first, span, PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            LogError("NoticeSensor: VirtualProtect failed (err %lu), the notice table stays as it is", GetLastError());
            return false;
        }
        for (const CapacitySite& site : g_Sites)
        {
            const std::uint8_t* wanted = enlarged ? site.enlarged : site.vanilla;
            for (std::size_t i = 0; i < site.size; ++i)
                if (site.mask[i])
                    site.code[i] = wanted[i];
        }
        DWORD restored = 0;
        VirtualProtect(first, span, oldProtect, &restored);
        FlushInstructionCache(GetCurrentProcess(), first, span);
        return true;
    }

    bool TableAllocated(const std::uint8_t* sensor)
    {
        std::uint16_t state = 0;
        return ReadEntryBytesSEH(sensor + kTableState, &state, sizeof(state)) && (state & 1);
    }

    void LinkExtraControllers(std::uint8_t* sensor)
    {
        auto* controllers = *reinterpret_cast<std::uint8_t**>(sensor + kControllerArray);
        if (!controllers)
            return;
        for (std::uint32_t k = kVanillaControllers; k < kControllers; ++k)
            for (const std::size_t link : kControllerLinks)
                *reinterpret_cast<void**>(controllers + k * kControllerSize + link) = *reinterpret_cast<void**>(controllers + link);
    }

    void* __fastcall hkInit(std::uint8_t* sensor, const std::uint8_t* desc)
    {
        std::lock_guard<std::mutex> lock(g_CodeLock);
        // a second Init on a built table reuses its memory, so only a new table may grow
        const bool fresh = sensor && !TableAllocated(sensor);
        const bool wanted = fresh && !MissionCodeGuard::IsFob();
        const bool enlarged = SetCapacity(wanted) && wanted;
        void* result = g_Orig(sensor, desc);
        if (enlarged)
        {
            SetCapacity(false);
            LinkExtraControllers(sensor);
        }
        if (fresh)
        {
            g_EntrySlots.store(enlarged ? kEntries : kVanillaEntries, std::memory_order_release);
            g_ControllerSlots.store(enlarged ? kControllers : kVanillaControllers, std::memory_order_release);
            g_BuildCount.fetch_add(1, std::memory_order_acq_rel);
            LogDebug("NoticeSensor: mission %u: notice table built with %u entries and %u controllers (flags 0x%02X, %llu bytes)",
                     static_cast<unsigned>(MissionCodeGuard::GetCurrentMissionCode()), g_EntrySlots.load(), g_ControllerSlots.load(),
                     sensor[kFlags], static_cast<unsigned long long>(*reinterpret_cast<std::uint64_t*>(sensor + kTableBytes)));
        }
        return result;
    }
}

bool Install_NoticeSensorImpl_Init_Hook()
{
    if (g_Target)
        return true;
    void* target = ResolveGameAddress(gAddr.NoticeSensorImpl_Init);
    g_Sites[0].code = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.NoticeSensorImpl_Init_SizePass));
    g_Sites[1].code = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.NoticeSensorImpl_Init_BuildPass));
    g_Sites[2].code = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.NoticeSensorImpl_Init_NetTableRows));
    g_Sites[3].code = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.NoticeSensorImpl_Init_SyncTableRows));
    if (!target || !g_Sites[0].code || !g_Sites[1].code || !g_Sites[2].code || !g_Sites[3].code)
    {
        LogWarn("NoticeSensor: addresses missing on %s, the notice table stays vanilla", GetGameBuildName(gGameBuild));
        return true;
    }
    for (const CapacitySite& site : g_Sites)
    {
        if (ReadState(site) == SiteState::Unknown)
        {
            ReportCodeChange(site);
            return true;
        }
    }
    const HookEntry state = CheckHookEntry(target, kInitEntry, sizeof(kInitEntry));
    if (state == HookEntry::Unknown)
    {
        char desc[160] = {};
        DescribeFunctionEntry(target, desc, sizeof(desc));
        LogWarn("NoticeSensor: Init code differs on this build (%s), the notice table stays vanilla", desc);
        return true;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("NoticeSensor: Init", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkInit), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("NoticeSensor: Init hook failed, the notice table stays vanilla");
        return false;
    }
    g_Target = target;
    LogInfo("NoticeSensor: notice table %u -> %u entries, %u -> %u controllers outside FOB",
            kVanillaEntries, kEntries, kVanillaControllers, kControllers);
    return true;
}

void Uninstall_NoticeSensorImpl_Init_Hook()
{
    if (!g_Target)
        return;
    DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    std::lock_guard<std::mutex> lock(g_CodeLock);
    SetCapacity(false);   // a table built bigger keeps its memory until the game frees it
}

std::uint32_t GetNoticeSensorEntrySlots()
{
    return g_EntrySlots.load(std::memory_order_acquire);
}

std::uint32_t GetNoticeSensorControllerSlots()
{
    return g_ControllerSlots.load(std::memory_order_acquire);
}

std::uint32_t GetNoticeSensorBuildCount()
{
    return g_BuildCount.load(std::memory_order_acquire);
}
