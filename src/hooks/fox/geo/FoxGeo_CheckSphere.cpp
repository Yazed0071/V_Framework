#include <pch.h>
#include "FoxGeo_CheckSphere.h"

#include <Windows.h>
#include <intrin.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using CheckSphere_t = int(__fastcall*)(const float* position, float radius, std::uint64_t mask, std::int32_t world);

    constexpr std::int32_t kWorldSlots     = 16;
    constexpr std::size_t  kWorldCollision = 0x28;
    constexpr std::size_t  kWorldWords     = 6;
    constexpr std::size_t  kTableLeaOff    = 0xA4;   // lea rcx,[world table] inside the function
    constexpr std::size_t  kLeaLen         = 7;
    constexpr int          kMostReports    = 8;

    const std::uint8_t kPrologue[] = { 0x48, 0x8B, 0xC4, 0x55, 0x56, 0x57, 0x48, 0x81, 0xEC, 0xF0, 0x0F, 0x00, 0x00 };
    const std::uint8_t kLeaRcx[]   = { 0x48, 0x8D, 0x0D };

    CheckSphere_t         g_Orig = nullptr;
    void*                 g_Target = nullptr;
    const std::uintptr_t* g_Worlds = nullptr;
    std::atomic<unsigned> g_Skipped{ 0 };
    std::atomic<int>      g_Reports{ 0 };

    bool WorldReady(std::int32_t world)
    {
        std::uintptr_t entry = 0;
        std::uintptr_t collision = 0;
        return ReadEntryBytesSEH(g_Worlds + world, &entry, sizeof(entry)) && entry
            && ReadEntryBytesSEH(reinterpret_cast<const void*>(entry + kWorldCollision), &collision, sizeof(collision))
            && collision;
    }

    void ReportWorldNotReady(const float* position, std::int32_t world, const void* caller)
    {
        std::uintptr_t entry = 0;
        std::uintptr_t words[kWorldWords] = {};
        float at[3] = {};
        ReadEntryBytesSEH(g_Worlds + world, &entry, sizeof(entry));
        if (entry)
            ReadEntryBytesSEH(reinterpret_cast<const void*>(entry), words, sizeof(words));
        ReadEntryBytesSEH(position, at, sizeof(at));
        LogWarn("GeoGuard: geometry world %d not ready, check at %.1f %.1f %.1f from %p skipped; world %p holds %llX %llX %llX %llX %llX %llX",
                world, at[0], at[1], at[2], caller, reinterpret_cast<const void*>(entry),
                static_cast<unsigned long long>(words[0]), static_cast<unsigned long long>(words[1]),
                static_cast<unsigned long long>(words[2]), static_cast<unsigned long long>(words[3]),
                static_cast<unsigned long long>(words[4]), static_cast<unsigned long long>(words[5]));
    }

    int __fastcall hkCheckSphere(const float* position, float radius, std::uint64_t mask, std::int32_t world)
    {
        if (world < 0 || world >= kWorldSlots || WorldReady(world))
        {
            if (g_Skipped.load(std::memory_order_relaxed))
            {
                const unsigned skipped = g_Skipped.exchange(0);
                if (skipped && g_Reports.load(std::memory_order_relaxed) <= kMostReports)
                    LogInfo("GeoGuard: geometry world ready again after %u skipped check(s)", skipped);
            }
            return g_Orig(position, radius, mask, world);
        }
        if (g_Skipped.fetch_add(1) == 0 && g_Reports.fetch_add(1) < kMostReports)
            ReportWorldNotReady(position, world, _ReturnAddress());
        return 0;   // the same answer the function gives when geometry is switched off
    }
}

bool Install_FoxGeo_CheckSphere_Hook()
{
    if (!gAddr.FoxGeo_CheckSphere || !gAddr.FoxGeo_WorldTable)
    {
        LogError("GeoGuard: address missing for this build, a missing geometry world can still crash the game");
        return false;
    }

    auto* fn = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.FoxGeo_CheckSphere));
    const HookEntry entry = CheckHookEntry(fn, kPrologue, sizeof(kPrologue));
    if (entry == HookEntry::Unknown)
    {
        char found[160] = {};
        DescribeFunctionEntry(fn, found, sizeof(found));
        LogError("GeoGuard: sphere check at 0x%llX is not the expected code (%s), a missing geometry world can still crash the game",
                 static_cast<unsigned long long>(gAddr.FoxGeo_CheckSphere), found);
        return false;
    }

    std::uint8_t lea[kLeaLen] = {};
    std::int32_t disp = 0;
    const auto* table = static_cast<const std::uintptr_t*>(ResolveGameAddress(gAddr.FoxGeo_WorldTable));
    if (ReadEntryBytesSEH(fn + kTableLeaOff, lea, sizeof(lea)) && std::memcmp(lea, kLeaRcx, sizeof(kLeaRcx)) == 0)
        std::memcpy(&disp, lea + sizeof(kLeaRcx), sizeof(disp));
    if (reinterpret_cast<const std::uintptr_t*>(fn + kTableLeaOff + kLeaLen + disp) != table)
    {
        LogError("GeoGuard: sphere check does not read the world table at 0x%llX, a missing geometry world can still crash the game",
                 static_cast<unsigned long long>(gAddr.FoxGeo_WorldTable));
        return false;
    }
    g_Worlds = table;

    if (!CreateAndEnableHook(fn, reinterpret_cast<void*>(&hkCheckSphere), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("GeoGuard: sphere check hook failed, a missing geometry world can still crash the game");
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("GeoGuard: sphere check", fn);
    g_Target = fn;
    return true;
}

void Uninstall_FoxGeo_CheckSphere_Hook()
{
    if (g_Target)
    {
        DisableAndRemoveHook(g_Target);
        g_Target = nullptr;
    }
    g_Orig = nullptr;
}
